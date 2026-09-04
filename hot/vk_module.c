#include "hot/vk_context.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Vk_module (hot/vk_module.c)
 * LEVEL: L3 — Module Code (reloaded vulkan.dylib business logic)
 * ============================================================================
 * Vulkan module implementation.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - register_trampoline(name, fn)
 *   - for(i++)
 *   - vk_begin_frame(void)
 *   - vk_end_frame(void)
 *   - vk_draw_rect(x, y, w, h, r, g, b, a)
 *   - vk_draw_texture(tex_id, x, y, w, h, mode)
 *   - VkModuleInit(context)
 *   - VkModuleShutdown(void)
 *   - printf(shutdown\n")
 *   - VkModuleGetTrampolines(count)
 *   - VkModuleGetManifest(void)
 *   - VkModuleSaveCache(void)
 *   - VkModuleLoadCache(void)
 *
 * Getters:
 *   - get_trampoline(name)
 * ============================================================================
 */


// hot/vk_module.c — Vulkan module implementation.
//
// This file is compiled into its own dylib (vulkan.dylib).
// On reload, this entire file is replaced — but the VkDevice persists
// because it's owned by the loader, not by this module.

// Module state (recreated on reload)
[[maybe_unused]] static VkPipeline s_pipeline = VK_NULL_HANDLE;
[[maybe_unused]] static VkPipelineLayout s_pipeline_layout = VK_NULL_HANDLE;
[[maybe_unused]] static VkRenderPass s_render_pass = VK_NULL_HANDLE;
[[maybe_unused]] static VkFramebuffer s_framebuffers[8];
[[maybe_unused]] static uint32_t s_framebuffer_count = 0;

// Trampoline table — atomically swapped on reload
static VkTrampolineEntry s_trampolines[16];
static uint32_t s_trampoline_count = 0;

// Register a trampoline
static void register_trampoline(const char *name, void *fn) {
    if (s_trampoline_count < 16) {
        s_trampolines[s_trampoline_count].name = name;
        s_trampolines[s_trampoline_count].function = fn;
        s_trampoline_count++;
    }
}

// Get a trampoline by name
[[maybe_unused]] static void *get_trampoline(const char *name) {
    for (uint32_t i = 0; i < s_trampoline_count; i++) {
        if (strcmp(s_trampolines[i].name, name) == 0) {
            return s_trampolines[i].function;
        }
    }
    return NULL;
}

// Pipeline cache — serialized across reloads
static VkPipelineCache s_cache = VK_NULL_HANDLE;

// Module's manifest
static const VkModuleManifest s_manifest = {
    .name = "vulkan",
    .version = "1.0.0",
    .type_id_count = 4,
    .type_ids = {
        {"ID_VK_SWAPCHAIN", 0x00003E},
        {"ID_VK_PIPELINE", 0x000044},
        {"ID_VK_RENDER_PASS", 0x000041},
        {"ID_VK_FRAMEBUFFER", 0x000049},
    },
    .export_count = 4,
    .exports = {"vk_begin_frame", "vk_end_frame", "vk_draw_rect", "vk_draw_texture"},
    .dependency_count = 1,
    .dependencies = {"primitive"},
};

// Trampoline function stubs
static uint32_t vk_begin_frame(void) {
    // Acquire next swapchain image
    // ...
    return 0; // image index
}

static void vk_end_frame(void) {
    // Submit command buffer and present
    // ...
}

static void vk_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    (void) x; (void) y; (void) w; (void) h;
    (void) r; (void) g; (void) b; (void) a;
    // Record a rect draw command
    // ...
}

static void vk_draw_texture(int32_t tex_id, float x, float y, float w, float h, int mode) {
    (void) tex_id; (void) x; (void) y; (void) w; (void) h; (void) mode;
    // Record a texture draw command
    // ...
}

// Module initialization
bool VkModuleInit(const VkHotContext *context) {
    // Store the context (device, queue, etc.)
    // The context is owned by the loader, don't copy it
    
    // Create pipeline cache (load from disk if exists)
    s_cache = (*context).pipeline_cache;
    
    // Build pipelines
    // ...
    
    // Build render passes
    // ...
    
    // Build framebuffers
    // ...
    
    // Register trampolines
    register_trampoline("vk_begin_frame", vk_begin_frame);
    register_trampoline("vk_end_frame", vk_end_frame);
    register_trampoline("vk_draw_rect", vk_draw_rect);
    register_trampoline("vk_draw_texture", vk_draw_texture);
    
    printf("[vulkan] module initialized (device=%p)\n", (void*) (*context).device);
    return true;
}

// Module shutdown
void VkModuleShutdown(void) {
    // Destroy pipelines
    // Destroy render passes
    // Destroy framebuffers
    // (but NOT the device — that's owned by the loader)
    
    printf("[vulkan] module shutdown\n");
}

// Get trampolines
const VkTrampolineEntry *VkModuleGetTrampolines(uint32_t *count) {
    *count = s_trampoline_count;
    return s_trampolines;
}

// Get manifest
const VkModuleManifest *VkModuleGetManifest(void) {
    return &s_manifest;
}

// Pipeline cache serialization
// On reload, save cache to disk before shutdown
void VkModuleSaveCache(void) {
    if (s_cache) {
        // vkGetPipelineCacheData → write to file
    }
}

// On new module init, load cache from disk
void VkModuleLoadCache(void) {
    // Read file → vkCreatePipelineCache with initial data
}
