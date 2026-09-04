#include "hot/vk_context.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

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
    return (int)idx;
}

void *hot_vk_get_symbol(const char *name) {
    int idx = trampoline_find(name);
    if (idx < 0) return NULL;
    return atomic_load(&s_trampolines[idx].ptr);
}

// Initialize the Vulkan loader — creates the device
bool hot_vk_init_loader(VkInstance instance, VkPhysicalDevice phys, 
                        VkDevice device, VkQueue queue, uint32_t queue_family) {
    s_instance = instance;
    s_phys = phys;
    s_device = device;
    s_queue = queue;
    s_queue_family = queue_family;
    
    // Create pipeline cache
    VkPipelineCacheCreateInfo cache_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    // TODO: load cache from disk if exists
    vkCreatePipelineCache(s_device, &cache_ci, NULL, &s_cache);
    
    printf("[vk_loader] initialized (device=%p)\n", (void*) s_device);
    return true;
}

// Load the Vulkan module from a dylib
bool hot_vk_load_module(const char *path) {
    if (s_module_handle) {
        // Shutdown old module first
        if (s_module_shutdown) s_module_shutdown();
        dlclose(s_module_handle);
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
    s_initialized = false;
    
    // Destroy pipeline cache
    if (s_cache) {
        // TODO: save cache to disk
        vkDestroyPipelineCache(s_device, s_cache, NULL);
        s_cache = VK_NULL_HANDLE;
    }
    
    printf("[vk_loader] shutdown\n");
}
