#include "hot/hot.h"
#include "hot/manifest.h"
#include "hot/hot_trampoline.h"
#include "hot/hot_retire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdatomic.h>
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: HotModule (hot/hot.c)
 * LEVEL: L4 — Self-Management (watches, verifies, swaps, retires; fixes/upgrades itself)
 * ============================================================================
 * Hotloading system: watches hot_dir for .dylib/.so, verifies ABI via
 * HotManifest, atomically swaps trampoline pointers, retires old handles
 * after a grace period. Hot_poll() runs on main thread only.
 *
 * STRUCT FIELDS (Mirroring typedef struct HotModule — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   char hot_dir[HOT_PATH_LEN];              // watched directory (512B path)
 *   HotModuleInternal modules[HOT_MAX_MODULES]; // per-module slots (max 32)
 *   uint32_t module_count;                   // used slots in modules[]
 *   char last_error[256];                    // last diagnostic string
 *   HotTrampolineTable trampolines;          // per-instance symbols (zeroed by Hot_init calloc)
 *   HotRetireRing retireRing;                // per-instance generational close
 *
 * PRIVATE HELPERS (kept file-local pure-data only, with full fields):
 * ----------------------------------------------------------------------------
 *   HotModuleInternal (per-module state — HotModule's slot type, no own API):
 *     char name[HOT_MANIFEST_MAX_NAME];      // module name (no extension)
 *     char path[HOT_PATH_LEN];               // source dylib path
 *     void *handle;                          // dlopen handle (NULL = unloaded)
 *     uint64_t last_modified;                // mtime ns + size (change stamp)
 *     HotManifest manifest;                  // last verified manifest
 *     bool loaded;                           // true once first load succeeds
 *
 * Segregated (own files, see their overviews):
 *   HotTrampoline → hot/hot_trampoline.h/c
 *   HotRetiredHandle → hot/hot_retire.h/c
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Hot_init(hot_dir)
 *
 * Core Functions:
 *   - HotShutdown(hot)
 *   - Hot_poll(hot, loaded_count)
 *
 * Getters:
 *   - Hot_get_api(hot, module_name)
 *   - Hot_get_symbol(hot, name)
 *   - Hot_last_error(hot)
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
    uint64_t last_modified;          // last file modification timestamp (ns) + size
    HotManifest manifest;            // current manifest
    bool loaded;                     // is currently loaded
} HotModuleInternal;
typedef struct HotModule {
    char hot_dir[HOT_PATH_LEN];
    HotModuleInternal modules[HOT_MAX_MODULES];
    uint32_t module_count;
    char last_error[256];
    HotTrampolineTable trampolines;  // per-instance symbols (no cross-app collision)
    HotRetireRing retireRing;        // per-instance generational close
} HotModule;

// CONSTRUCTORS — HotModule owns the module slots below; row mechanics live
// in hot_trampoline.c, generational close in hot_retire.c.

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
    HotRetireRing_drainAll(&(*hot).retireRing);

    free(hot);
}

// Load a module from a dylib path
static HotResult load_module(HotModule *hot, HotModuleInternal *mod, const char *dylib_path) {
    HotTrampolineTable *table = &(*hot).trampolines;
    HotRetireRing *ring = &(*hot).retireRing;
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
            int tidx = HotTrampolineTable_find(table, entries[i].name);
            if (tidx < 0) tidx = HotTrampolineTable_register(table, entries[i].name);
            if (tidx >= 0) HotTrampolineTable_set(table, tidx, entries[i].fn);
        }
        // Update module state
        if ((*mod).handle) HotRetireRing_retire(ring, (*mod).handle);
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
        int tidx = HotTrampolineTable_find(table, export_name);
        if (tidx < 0) {
            tidx = HotTrampolineTable_register(table, export_name);
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
        HotTrampolineTable_set(table, tidx, fn);
    }

    // 7. Update module state
    if ((*mod).handle) {
        // Retire old dylib to grace period ring after swap
        HotRetireRing_retire(ring, (*mod).handle);
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

    HotRetireRing *ring = &(*hot).retireRing;
    HotRetireRing_advance(ring);
    
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
                HotRetireRing_retire(ring, (*mod).handle);
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

    HotTrampolineTable *table = &(*hot).trampolines;
    HotManifest *manifest = &(*mod).manifest;
    HotExport *first = &(*manifest).exports[0];
    int tidx = HotTrampolineTable_find(table, (*first).name);
    if (tidx < 0) return NULL;

    return HotTrampolineTable_get(table, tidx);
}

HotFn Hot_get_symbol(HotModule *hot, const char *name) {
    if (!hot || !name) return NULL;

    // Find the trampoline by name
    HotTrampolineTable *table = &(*hot).trampolines;
    int tidx = HotTrampolineTable_find(table, name);
    if (tidx < 0) return NULL;

    return HotTrampolineTable_get(table, tidx);
}

const char *Hot_last_error(HotModule *hot) {
    if (!hot) return "NULL hot module";
    return (*hot).last_error;
}
