#include "hot/hot.h"
#include "hot/manifest.h"
#include "hot/vk_context.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stdatomic.h>
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Hot (hot/hot.c)
 * ============================================================================
 * Hotloading system for anti.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Hot_init(hot_dir)
 *
 * Core Functions:
 *   - HotShutdown(hot)
 *   - Hot_poll(hot, loaded_count)
 *   - Hot_last_error(hot)
 *
 * Getters:
 *   - Hot_get_api(hot, module_name)
 *   - Hot_get_symbol(hot, name)
 * ============================================================================
 */


// hot/hot.c — Hotloading system implementation.
//
// Architecture:
//   - Each module is a .dylib/.so loaded via dlopen()
//   - Function pointers are accessed through a trampoline table
//   - On reload, the new dylib is loaded, verified, then the trampoline
//     table is atomically swapped
//   - The old dylib is unloaded after a grace period (next frame)
//
// Thread safety:
//   - Hot_poll() must be called from the main thread only
//   - Function pointer tables are swapped atomically (C23 atomics)
//   - No locks needed for the trampoline table itself

#define HOT_MAX_MODULES 32
#define HOT_PATH_LEN 512

// Internal module state
typedef struct {
    char name[HOT_MANIFEST_MAX_NAME];
    char path[HOT_PATH_LEN];
    void *handle;                    // dlopen handle
    uint64_t last_modified;        // last file modification timestamp (ns) + size
    HotManifest manifest;            // current manifest
    bool loaded;                     // is currently loaded
} HotModuleInternal;
typedef struct HotModule {
    char hot_dir[HOT_PATH_LEN];
    HotModuleInternal modules[HOT_MAX_MODULES];
    uint32_t module_count;
    char last_error[256];
} HotModule;

// Trampoline table entry — one per exported function
typedef struct {
    _Atomic(void*) ptr;             // atomic function pointer
    _Atomic(void*) fallback_ptr;    // prior generation pointer for mid-swap resilience
    char name[HOT_MANIFEST_MAX_NAME];
} HotTrampoline;

#define HOT_MAX_TRAMPOLINES 1024
static HotTrampoline s_trampolines[HOT_MAX_TRAMPOLINES];
static _Atomic uint32_t s_trampoline_count = 0;

// Register a trampoline for a function. Returns the trampoline index.
static int trampoline_register(const char *name) {
    uint32_t idx = atomic_fetch_add(&s_trampoline_count, 1);
    if (idx >= HOT_MAX_TRAMPOLINES) return -1;
    strncpy(s_trampolines[idx].name, name, HOT_MANIFEST_MAX_NAME - 1);
    s_trampolines[idx].name[HOT_MANIFEST_MAX_NAME - 1] = '\0';
    atomic_store(&s_trampolines[idx].ptr, NULL);
    atomic_store(&s_trampolines[idx].fallback_ptr, NULL);
    return (int)idx;
}

// Get a trampoline's current function pointer, falling back to prior generation on mid-swap NULL.
static void *trampoline_get(int idx) {
    if (idx < 0 || idx >= (int)atomic_load(&s_trampoline_count)) return NULL;
    void *ptr = atomic_load(&s_trampolines[idx].ptr);
    if (!ptr) {
        // Poll briefly in case atomic swap is in-flight
        for (int retry = 0; retry < 4 && !ptr; retry++) {
            #if defined(__aarch64__)
            __asm__ volatile("yield");
            #endif
            ptr = atomic_load(&s_trampolines[idx].ptr);
        }
        // Fall back to prior generation rather than returning NULL and crashing caller
        if (!ptr) {
            ptr = atomic_load(&s_trampolines[idx].fallback_ptr);
        }
    }
    return ptr;
}

// Set a trampoline's function pointer atomically.
static void trampoline_set(int idx, void *ptr) {
    if (idx < 0 || idx >= (int)atomic_load(&s_trampoline_count)) return;
    void *old = atomic_load(&s_trampolines[idx].ptr);
    if (old && old != ptr) {
        atomic_store(&s_trampolines[idx].fallback_ptr, old);
    }
    atomic_store(&s_trampolines[idx].ptr, ptr);
}

// Find a trampoline by name. Returns -1 if not found.
static int trampoline_find(const char *name) {
    uint32_t count = atomic_load(&s_trampoline_count);
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(s_trampolines[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

// Generational handle retirement: keeps old dylib handles alive for N generations
// before closing them with dlclose() to prevent race conditions during module reload.
#define HOT_RETIRED_MAX 16
#define HOT_RETIRED_GENERATIONS 4

typedef struct {
    void *handle;
    uint32_t generation;
} HotRetiredHandle;

static HotRetiredHandle s_retired[HOT_RETIRED_MAX] = {0};
static uint32_t s_current_generation = 0;

static void hot_retire_handle(void *handle) {
    if (!handle) return;

    for (size_t i = 0; i < HOT_RETIRED_MAX; i++) {
        if (s_retired[i].handle && (s_current_generation - s_retired[i].generation >= HOT_RETIRED_GENERATIONS)) {
            dlclose(s_retired[i].handle);
            s_retired[i].handle = NULL;
        }
    }

    size_t slot = HOT_RETIRED_MAX;
    for (size_t i = 0; i < HOT_RETIRED_MAX; i++) {
        if (!s_retired[i].handle) {
            slot = i;
            break;
        }
    }

    if (slot == HOT_RETIRED_MAX) {
        size_t oldest_idx = 0;
        uint32_t oldest_gen = UINT32_MAX;
        for (size_t i = 0; i < HOT_RETIRED_MAX; i++) {
            if (s_retired[i].generation < oldest_gen) {
                oldest_gen = s_retired[i].generation;
                oldest_idx = i;
            }
        }
        dlclose(s_retired[oldest_idx].handle);
        slot = oldest_idx;
    }

    s_retired[slot].handle = handle;
    s_retired[slot].generation = s_current_generation;
}

static void hot_advance_generation(void) {
    s_current_generation++;
    for (size_t i = 0; i < HOT_RETIRED_MAX; i++) {
        if (s_retired[i].handle && (s_current_generation - s_retired[i].generation >= HOT_RETIRED_GENERATIONS)) {
            dlclose(s_retired[i].handle);
            s_retired[i].handle = NULL;
        }
    }
}

// Get file modification time with nanosecond precision + file size
static uint64_t file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(__APPLE__)
    uint64_t sec = (uint64_t) st.st_mtimespec.tv_sec;
    uint64_t nsec = (uint64_t) st.st_mtimespec.tv_nsec;
#elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
    uint64_t sec = (uint64_t) st.st_mtim.tv_sec;
    uint64_t nsec = (uint64_t) st.st_mtim.tv_nsec;
#else
    uint64_t sec = (uint64_t) st.st_mtime;
    uint64_t nsec = 0;
#endif
    return (sec * 1000000000ULL) + nsec + (uint64_t) st.st_size;
}

// Check if a file exists
// static bool file_exists(const char *path) {
//     return access(path, F_OK) == 0;
// }

// Copy a file (for cloning working dylibs)
static bool file_copy(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in); fclose(out);
            return false;
        }
    }
    fclose(in);
    fclose(out);
    return true;
}

// Find a module by name
static HotModuleInternal *find_module(HotModule *hot, const char *name) {
    for (uint32_t i = 0; i < (*hot).module_count; i++) {
        if (strcmp((*hot).modules[i].name, name) == 0) {
            return &(*hot).modules[i];
        }
    }
    return NULL;
}

HotModule *Hot_init(const char *hot_dir) {
    if (!hot_dir) return NULL;
    
    HotModule *hot = (HotModule*) calloc(1, sizeof(HotModule));
    if (!hot) return NULL;
    
    strncpy((*hot).hot_dir, hot_dir, HOT_PATH_LEN - 1);
    (*hot).hot_dir[HOT_PATH_LEN - 1] = '\0';
    
    // Ensure hot directory exists
    DIR *dir = opendir(hot_dir);
    if (!dir) {
        // Try to create it
        #ifdef __APPLE__
        mkdir(hot_dir, 0755);
        #else
        mkdir(hot_dir, 0755);
        #endif
        dir = opendir(hot_dir);
    }
    if (dir) closedir(dir);
    
    return hot;
}

void HotShutdown(HotModule *hot) {
    if (!hot) return;
    
    // Unload all modules
    for (uint32_t i = 0; i < (*hot).module_count; i++) {
        HotModuleInternal *mod = &(*hot).modules[i];
        if ((*mod).handle) {
            dlclose((*mod).handle);
            (*mod).handle = NULL;
            (*mod).loaded = false;
        }
    }
    
    // Drain retired handle ring
    for (size_t i = 0; i < HOT_RETIRED_MAX; i++) {
        if (s_retired[i].handle) {
            dlclose(s_retired[i].handle);
            s_retired[i].handle = NULL;
        }
    }

    free(hot);
}

// Load a module from a dylib path
static HotResult load_module(HotModule *hot, HotModuleInternal *mod, const char *dylib_path) {
    // 1. Load the dylib
    void *handle = dlopen(dylib_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        snprintf((*hot).last_error, sizeof((*hot).last_error), 
                 "dlopen(%s) failed: %s", dylib_path, dlerror());
        return HOT_ERROR_DLOPEN_FAILED;
    }
    
    // 2. Get the manifest (try both naming conventions)
    typedef const char *(*ManifestFn)(void);
    ManifestFn get_manifest = (ManifestFn)dlsym(handle, "Hot_manifest");
    if (!get_manifest) {
        // Try VkModuleGetManifest (struct-based)
        typedef const void *(*ManifestStructFn)(void);
        ManifestStructFn get_manifest_struct = (ManifestStructFn)dlsym(handle, "VkModuleGetManifest");
        if (!get_manifest_struct) {
            snprintf((*hot).last_error, sizeof((*hot).last_error),
                     "dlsym(Hot_manifest/VkModuleGetManifest) failed: %s", dlerror());
            dlclose(handle);
            return HOT_ERROR_DLSYM_FAILED;
        }
        // Register trampolines from the struct-based module
        typedef const void *(*TrampolinesFn)(uint32_t*);
        TrampolinesFn get_trampolines = (TrampolinesFn)dlsym(handle, "VkModuleGetTrampolines");
        if (!get_trampolines) {
            snprintf((*hot).last_error, sizeof((*hot).last_error),
                     "dlsym(VkModuleGetTrampolines) failed: %s", dlerror());
            dlclose(handle);
            return HOT_ERROR_DLSYM_FAILED;
        }
        uint32_t trampoline_count = 0;
        const void *trampolines = get_trampolines(&trampoline_count);
        for (uint32_t i = 0; i < trampoline_count; i++) {
            // Trampolines are {const char *name, void *function}
            const struct { const char *name; void *fn; } *entries = trampolines;
            int tidx = trampoline_find(entries[i].name);
            if (tidx < 0) tidx = trampoline_register(entries[i].name);
            if (tidx >= 0) trampoline_set(tidx, entries[i].fn);
        }
        // Update module state
        if ((*mod).handle) hot_retire_handle((*mod).handle);
        (*mod).handle = handle;
        (*mod).last_modified = file_mtime(dylib_path);
        (*mod).loaded = true;
        return HOT_OK;
    }
    
    const char *manifest_json = get_manifest();
    if (!manifest_json) {
        snprintf((*hot).last_error, sizeof((*hot).last_error),
                 "Hot_manifest returned NULL");
        dlclose(handle);
        return HOT_ERROR_DLSYM_FAILED;
    }
    
    // 3. Parse the manifest
    HotManifest new_manifest;
    if (!HotManifest_parse(manifest_json, strlen(manifest_json), &new_manifest)) {
        snprintf((*hot).last_error, sizeof((*hot).last_error),
                 "Failed to parse manifest for %s", (*mod).name);
        dlclose(handle);
        return HOT_ERROR_DLSYM_FAILED;
    }
    
    // 4. Verify ABI compatibility (if previously loaded)
    if ((*mod).loaded) {
        if (!HotManifest_compatible(&(*mod).manifest, &new_manifest)) {
            snprintf((*hot).last_error, sizeof((*hot).last_error),
                     "ABI mismatch for module %s", (*mod).name);
            dlclose(handle);
            return HOT_ERROR_ABI_MISMATCH;
        }
    }
    
    // 5. Call the module's init function
    typedef bool (*InitFn)(void);
    InitFn init = (InitFn)dlsym(handle, "Hot_init_module");
    if (init) {
        if (!init()) {
            snprintf((*hot).last_error, sizeof((*hot).last_error),
                     "Hot_init_module failed for %s", (*mod).name);
            dlclose(handle);
            return HOT_ERROR_INIT_FAILED;
        }
    }
    
    // 6. Register trampolines for all exports
    for (uint32_t i = 0; i < new_manifest.export_count; i++) {
        const char *export_name = new_manifest.exports[i].name;
        
        // Find or create trampoline
        int tidx = trampoline_find(export_name);
        if (tidx < 0) {
            tidx = trampoline_register(export_name);
            if (tidx < 0) {
                snprintf((*hot).last_error, sizeof((*hot).last_error),
                         "Too many trampolines");
                dlclose(handle);
                return HOT_ERROR_OUT_OF_MEMORY;
            }
        }
        
        // Get the function pointer from the dylib
        void *fn = dlsym(handle, export_name);
        if (!fn) {
            snprintf((*hot).last_error, sizeof((*hot).last_error),
                     "dlsym(%s) failed: %s", export_name, dlerror());
            dlclose(handle);
            return HOT_ERROR_DLSYM_FAILED;
        }
        
        // Atomic swap of the trampoline
        trampoline_set(tidx, fn);
    }
    
    // 7. Update module state
    if ((*mod).handle) {
        // Retire old dylib to grace period ring after swap
        hot_retire_handle((*mod).handle);
    }
    
    (*mod).handle = handle;
    (*mod).manifest = new_manifest;
    (*mod).last_modified = file_mtime(dylib_path);
    (*mod).loaded = true;
    
    return HOT_OK;
}

HotResult Hot_poll(HotModule *hot, uint32_t *loaded_count) {
    if (!hot) return HOT_ERROR_FILE_NOT_FOUND;
    if (loaded_count) *loaded_count = 0;

    hot_advance_generation();
    
    // Scan the hot directory for .dylib files
    DIR *dir = opendir((*hot).hot_dir);
    if (!dir) {
        snprintf((*hot).last_error, sizeof((*hot).last_error),
                 "Cannot open hot directory: %s", (*hot).hot_dir);
        return HOT_ERROR_FILE_NOT_FOUND;
    }
    
    uint32_t reloaded = 0;
    struct dirent *ent;
    
    while ((ent = readdir(dir)) != NULL) {
        // Check if it's a .dylib or .so
        const char *name = (*ent).d_name;
        size_t nlen = strlen(name);
        bool is_dylib = (nlen > 6 && strcmp(name + nlen - 6, ".dylib") == 0) ||
                        (nlen > 3 && strcmp(name + nlen - 3, ".so") == 0);
        if (!is_dylib) continue;
        
        // Extract module name (strip extension)
        char mod_name[HOT_MANIFEST_MAX_NAME];
        strncpy(mod_name, name, HOT_MANIFEST_MAX_NAME - 1);
        mod_name[HOT_MANIFEST_MAX_NAME - 1] = '\0';
        char *dot = strrchr(mod_name, '.');
        if (dot) *dot = '\0';
        
        // Build full path
        char path[HOT_PATH_LEN];
        int path_len = snprintf(path, sizeof(path), "%s/%s", (*hot).hot_dir, name);
        if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
            fprintf(stderr, "[hot] ERROR: Module path '%s/%s' truncated (exceeds %d bytes)\n",
                    (*hot).hot_dir, name, HOT_PATH_LEN);
            continue;
        }
        
        // Check if this is a new or updated module
        uint64_t mtime = file_mtime(path);
        HotModuleInternal *mod = find_module(hot, mod_name);
        
        if (!mod) {
            // New module — add it
            if ((*hot).module_count >= HOT_MAX_MODULES) {
                snprintf((*hot).last_error, sizeof((*hot).last_error),
                         "Too many modules (limit %d reached)", HOT_MAX_MODULES);
                fprintf(stderr, "[hot] ERROR: HOT_MAX_MODULES (%d) exceeded, skipping module '%s'\n",
                        HOT_MAX_MODULES, mod_name);
                continue;
            }
            mod = &(*hot).modules[(*hot).module_count++];
            strncpy((*mod).name, mod_name, HOT_MANIFEST_MAX_NAME - 1);
            (*mod).name[HOT_MANIFEST_MAX_NAME - 1] = '\0';
            strncpy((*mod).path, path, HOT_PATH_LEN - 1);
            (*mod).path[HOT_PATH_LEN - 1] = '\0';
            (*mod).handle = NULL;
            (*mod).loaded = false;
            memset(&(*mod).manifest, 0, sizeof((*mod).manifest));
        }
        
        // Check if file has been modified
        if ((*mod).loaded && (*mod).last_modified >= mtime) {
            continue; // No change
        }
        
        // Clone the dylib first (so we can verify before committing)
        char clone_path[HOT_PATH_LEN];
        int clone_len = snprintf(clone_path, sizeof(clone_path), "%s/.%s.clone", (*hot).hot_dir, name);
        if (clone_len < 0 || (size_t)clone_len >= sizeof(clone_path)) {
            fprintf(stderr, "[hot] ERROR: Clone path '%s/.%s.clone' truncated (exceeds %d bytes)\n",
                    (*hot).hot_dir, name, HOT_PATH_LEN);
            continue;
        }
        
        if (!file_copy(path, clone_path)) {
            snprintf((*hot).last_error, sizeof((*hot).last_error),
                     "Failed to clone %s", path);
            continue;
        }
        
        // Try to load the clone
        HotModuleInternal clone_mod;
        memcpy(&clone_mod, mod, sizeof(clone_mod));
        clone_mod.handle = NULL;
        
        HotResult result = load_module(hot, &clone_mod, clone_path);
        
        if (result == HOT_OK) {
            // Success — commit the swap
            if ((*mod).handle) {
                hot_retire_handle((*mod).handle);
            }
            memcpy(mod, &clone_mod, sizeof(HotModuleInternal));
            reloaded++;
            
            fprintf(stderr, "[hot] reloaded %s v%s\n", 
                    (*mod).name, (*mod).manifest.version);
        } else {
            // Failed — clean up
            if (clone_mod.handle) {
                dlclose(clone_mod.handle);
            }
            fprintf(stderr, "[hot] failed to reload %s: %s\n",
                    (*mod).name, (*hot).last_error);
        }
        
        // Remove clone
        unlink(clone_path);
    }
    
    closedir(dir);
    
    if (loaded_count) *loaded_count = reloaded;
    return HOT_OK;
}

const void *Hot_get_api(HotModule *hot, const char *module_name) {
    if (!hot || !module_name) return NULL;
    
    HotModuleInternal *mod = find_module(hot, module_name);
    if (!mod || !(*mod).loaded) return NULL;
    
    // Return the module's function pointer table
    // For now, return the first export's trampoline
    if ((*mod).manifest.export_count == 0) return NULL;
    
    int tidx = trampoline_find((*mod).manifest.exports[0].name);
    if (tidx < 0) return NULL;
    
    return trampoline_get(tidx);
}

HotFn Hot_get_symbol(HotModule *hot, const char *name) {
    if (!hot || !name) return NULL;
    
    // Find the trampoline by name
    int tidx = trampoline_find(name);
    if (tidx < 0) return NULL;
    
    return trampoline_get(tidx);
}

const char *Hot_last_error(HotModule *hot) {
    if (!hot) return "NULL hot module";
    return (*hot).last_error;
}
