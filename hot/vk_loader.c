#include "hot/vk_context.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Vk_loader (hot/vk_loader.c)
 * LEVEL: L4 — Self-Management (owns VkDevice; loader machinery surviving reload)
 * ============================================================================
 * VkDevice owner and Vulkan module loader.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - trampoline_create(name)
 *
 * Core Functions:
 *   - trampoline_find(name)
 *   - for(i++)
 *   - strncpy(s_trampolines[idx].name, name, 63)
 *   - atomic_store(&s_trampolines[idx].ptr, NULL)
 *   - volatile("yield")
 *   - vk_retire_handle(handle)
 *   - dlclose(s_vk_retired[i].handle)
 *   - vk_advance_generation(void)
 *   - hot_vk_init_loader(instance, phys, device, queue, queue_family)
 *   - vkCreatePipelineCache(s_device, &cache_ci, NULL, &s_cache)
 *   - hot_vk_load_module(path)
 *   - fprintf(stderr, exports\n")
 *   - hot_vk_shutdown(void)
 *   - vkDestroyPipelineCache(s_device, s_cache, NULL)
 *   - printf(shutdown\n")
 *
 * Getters:
 *   - hot_vk_get_symbol(name)
 * ============================================================================
 */


// hot/vk_loader.c — VkDevice owner and Vulkan module loader.
//
// This is the CRITICAL architectural piece: the VkDevice is created HERE,
// in the loader, NOT in the vulkan module. When the module is reloaded,
// the device persists. The module only creates pipelines/render passes
// that are recreated on each reload.

static VkInstance s_instance = VK_NULL_HANDLE;
static VkPhysicalDevice s_phys = VK_NULL_HANDLE;
static VkDevice s_device = VK_NULL_HANDLE;
static VkQueue s_queue = VK_NULL_HANDLE;
static uint32_t s_queue_family = 0;
static VkPipelineCache s_cache = VK_NULL_HANDLE;

// Module handle
static void *s_module_handle = NULL;
static bool s_initialized = false;

// Function pointers from the module
static VkModuleInitFn s_module_init = NULL;
static VkModuleShutdownFn s_module_shutdown = NULL;
static VkModuleGetTrampolinesFn s_module_get_trampolines = NULL;
static VkModuleGetManifestFn s_module_get_manifest = NULL;

// Trampoline table (atomic)
#define MAX_TRAMPOLINES 64
typedef struct {
    _Atomic(void*) ptr;
    _Atomic(void*) fallback_ptr;
    char name[64];
} Trampoline;

static Trampoline s_trampolines[MAX_TRAMPOLINES];
static _Atomic uint32_t s_trampoline_count = 0;

// Find or create a trampoline
static int trampoline_find(const char *name) {
    uint32_t count = atomic_load(&s_trampoline_count);
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(s_trampolines[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int trampoline_create(const char *name) {
    uint32_t idx = atomic_fetch_add(&s_trampoline_count, 1);
    if (idx >= MAX_TRAMPOLINES) return -1;
    strncpy(s_trampolines[idx].name, name, 63);
    s_trampolines[idx].name[63] = '\0';
    atomic_store(&s_trampolines[idx].ptr, NULL);
    atomic_store(&s_trampolines[idx].fallback_ptr, NULL);
    return (int)idx;
}

void *hot_vk_get_symbol(const char *name) {
    int idx = trampoline_find(name);
    if (idx < 0) return NULL;
    void *ptr = atomic_load(&s_trampolines[idx].ptr);
    if (!ptr) {
        for (int retry = 0; retry < 4 && !ptr; retry++) {
            #if defined(__aarch64__)
            __asm__ volatile("yield");
            #endif
            ptr = atomic_load(&s_trampolines[idx].ptr);
        }
        if (!ptr) {
            ptr = atomic_load(&s_trampolines[idx].fallback_ptr);
        }
    }
    return ptr;
}

// Generational handle retirement: keeps old dylib handles alive across
// reload generations to prevent race conditions during module reload.
#define VK_RETIRED_MAX 16
#define VK_RETIRED_GENERATIONS 4

typedef struct {
    void *handle;
    uint32_t generation;
} VkRetiredHandle;

static VkRetiredHandle s_vk_retired[VK_RETIRED_MAX] = {0};
static uint32_t s_vk_generation = 0;

static void vk_retire_handle(void *handle) {
    if (!handle) return;

    for (size_t i = 0; i < VK_RETIRED_MAX; i++) {
        if (s_vk_retired[i].handle && (s_vk_generation - s_vk_retired[i].generation >= VK_RETIRED_GENERATIONS)) {
            dlclose(s_vk_retired[i].handle);
            s_vk_retired[i].handle = NULL;
        }
    }

    size_t slot = VK_RETIRED_MAX;
    for (size_t i = 0; i < VK_RETIRED_MAX; i++) {
        if (!s_vk_retired[i].handle) {
            slot = i;
            break;
        }
    }

    if (slot == VK_RETIRED_MAX) {
        size_t oldest_idx = 0;
        uint32_t oldest_gen = UINT32_MAX;
        for (size_t i = 0; i < VK_RETIRED_MAX; i++) {
            if (s_vk_retired[i].generation < oldest_gen) {
                oldest_gen = s_vk_retired[i].generation;
                oldest_idx = i;
            }
        }
        dlclose(s_vk_retired[oldest_idx].handle);
        slot = oldest_idx;
    }

    s_vk_retired[slot].handle = handle;
    s_vk_retired[slot].generation = s_vk_generation;
}

static void vk_advance_generation(void) {
    s_vk_generation++;
    for (size_t i = 0; i < VK_RETIRED_MAX; i++) {
        if (s_vk_retired[i].handle && (s_vk_generation - s_vk_retired[i].generation >= VK_RETIRED_GENERATIONS)) {
            dlclose(s_vk_retired[i].handle);
            s_vk_retired[i].handle = NULL;
        }
    }
}

// Initialize the Vulkan loader — creates the device
bool hot_vk_init_loader(VkInstance instance, VkPhysicalDevice phys, 
                        VkDevice device, VkQueue queue, uint32_t queue_family) {
    s_instance = instance;
    s_phys = phys;
    s_device = device;
    s_queue = queue;
    s_queue_family = queue_family;
    
    // Create pipeline cache, seeded from disk when a prior run saved one.
    // The cache blob is driver-versioned: vkCreatePipelineCache rejects
    // stale data itself, so a corrupt/mismatched file just falls back to
    // an empty cache — never a fatal error.
    uint8_t *cache_data = nullptr;
    size_t cache_size = 0;
    FILE *cache_in = fopen("hot/.pipeline_cache", "rb");
    if (cache_in) {
        fseek(cache_in, 0, SEEK_END);
        long cache_len = ftell(cache_in);
        fseek(cache_in, 0, SEEK_SET);
        if (cache_len > 0 && cache_len < 16 * 1024 * 1024) {
            cache_data = (uint8_t*) malloc((size_t) cache_len);
            if (cache_data) {
                if (fread(cache_data, 1, (size_t) cache_len, cache_in) == (size_t) cache_len)
                    cache_size = (size_t) cache_len;
                else {
                    free(cache_data);
                    cache_data = nullptr;
                }
            }
        }
        fclose(cache_in);
    }
    VkPipelineCacheCreateInfo cache_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = cache_size,
        .pInitialData = cache_data,
    };
    vkCreatePipelineCache(s_device, &cache_ci, NULL, &s_cache);
    if (cache_data)
        free(cache_data);
    
    printf("[vk_loader] initialized (device=%p)\n", (void*) s_device);
    return true;
}

// Load the Vulkan module from a dylib
bool hot_vk_load_module(const char *path) {
    if (s_module_handle) {
        // Shutdown old module first
        if (s_module_shutdown) s_module_shutdown();
        vk_retire_handle(s_module_handle);
        vk_advance_generation();
        s_module_handle = NULL;
        s_initialized = false;
    }
    
    s_module_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!s_module_handle) {
        fprintf(stderr, "[vk_loader] dlopen failed: %s\n", dlerror());
        return false;
    }
    
    // Get module functions
    s_module_init = (VkModuleInitFn)dlsym(s_module_handle, "VkModuleInit");
    s_module_shutdown = (VkModuleShutdownFn)dlsym(s_module_handle, "VkModuleShutdown");
    s_module_get_trampolines = (VkModuleGetTrampolinesFn)dlsym(s_module_handle, "VkModuleGetTrampolines");
    s_module_get_manifest = (VkModuleGetManifestFn)dlsym(s_module_handle, "VkModuleGetManifest");
    
    if (!s_module_init || !s_module_get_trampolines || !s_module_get_manifest) {
        fprintf(stderr, "[vk_loader] missing required exports\n");
        dlclose(s_module_handle);
        s_module_handle = NULL;
        return false;
    }
    
    // Get manifest and verify ABI
    const VkModuleManifest *manifest = s_module_get_manifest();
    printf("[vk_loader] loading %s v%s\n", (*manifest).name, (*manifest).version);
    
    // Build the context
    VkHotContext context = {
        .instance = s_instance,
        .physical_device = s_phys,
        .device = s_device,
        .queue = s_queue,
        .queue_family = s_queue_family,
        .pipeline_cache = s_cache,
        .pipeline_cache_path = "hot/.pipeline_cache",
        .vulkan_api_version = VK_API_VERSION_1_2,
        .pipeline_cache_size = 0,
        .texture_registry = NULL,
    };
    
    // Initialize the module
    if (!s_module_init(&context)) {
        fprintf(stderr, "[vk_loader] module init failed\n");
        dlclose(s_module_handle);
        s_module_handle = NULL;
        return false;
    }
    
    // Register trampolines
    uint32_t trampoline_count = 0;
    const VkTrampolineEntry *trampolines = s_module_get_trampolines(&trampoline_count);
    for (uint32_t i = 0; i < trampoline_count; i++) {
        int idx = trampoline_find(trampolines[i].name);
        if (idx < 0) idx = trampoline_create(trampolines[i].name);
        if (idx >= 0) {
            void *old = atomic_load(&s_trampolines[idx].ptr);
            if (old && old != trampolines[i].function) {
                atomic_store(&s_trampolines[idx].fallback_ptr, old);
            }
            atomic_store(&s_trampolines[idx].ptr, trampolines[i].function);
        }
    }
    
    s_initialized = true;
    printf("[vk_loader] module loaded (%u trampolines)\n", trampoline_count);
    return true;
}

// Shutdown the Vulkan loader
void hot_vk_shutdown(void) {
    if (s_module_shutdown) s_module_shutdown();
    if (s_module_handle) {
        dlclose(s_module_handle);
        s_module_handle = NULL;
    }
    for (size_t i = 0; i < VK_RETIRED_MAX; i++) {
        if (s_vk_retired[i].handle) {
            dlclose(s_vk_retired[i].handle);
            s_vk_retired[i].handle = NULL;
        }
    }
    s_initialized = false;
    
    // Persist + destroy pipeline cache. vkGetPipelineCacheData sizes the
    // blob; a zero size or error simply skips the write.
    if (s_cache) {
        size_t data_size = 0;
        if (vkGetPipelineCacheData(s_device, s_cache, &data_size, nullptr) == VK_SUCCESS && data_size > 0) {
            uint8_t *data = (uint8_t*) malloc(data_size);
            if (data) {
                if (vkGetPipelineCacheData(s_device, s_cache, &data_size, data) == VK_SUCCESS) {
                    FILE *cache_out = fopen("hot/.pipeline_cache", "wb");
                    if (cache_out) {
                        fwrite(data, 1, data_size, cache_out);
                        fclose(cache_out);
                    }
                }
                free(data);
            }
        }
        vkDestroyPipelineCache(s_device, s_cache, NULL);
        s_cache = VK_NULL_HANDLE;
    }
    
    printf("[vk_loader] shutdown\n");
}
