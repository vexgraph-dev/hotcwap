#ifndef VULKAN_MAC_H
#define VULKAN_MAC_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "annotation/platform_exclusive.h"
#include "annotation/intention.h"

// Forward declaration for opaque Window handle (defined in hotcwap/window/window.h).
typedef struct Window Window;

// vulkan/vulkan_mac.h — macOS-specific Vulkan backend functions.
;;PLATFORM_EXCLUSIVE("Mac")
;;INTENTION("MoltenVK loader, CAMetalLayer surface creation, and device accessors for macOS.")

// Accessors for cross-platform state owned by vulkan.c.
extern VkDevice Vk_getDevice(void);
extern VkQueue Vk_getQueue(void);
extern VkCommandBuffer Vk_getCmdBuffer(void);
extern VkPipeline Vk_getTriPipeline(void);
extern VkPipelineLayout Vk_getTriLayout(void);
extern uint64_t Vk_getAnimStartNanos(void);
extern PFN_vkGetDeviceProcAddr Vk_getGdpa(void);
extern VkInstance Vk_getInstance(void);
extern PFN_vkGetInstanceProcAddr Vk_getGpa(void);
extern VkPhysicalDevice Vk_getPhys(void);
extern uint32_t Vk_getQueueFamily(void);

// Load a Vulkan device function pointer dynamically.
#define MAC_LOAD_DEVICE(name) \
    static PFN_vk##name name##_fn; \
    if (!name##_fn) { \
        name##_fn = (PFN_vk##name)Vk_getGdpa()(Vk_getDevice(), "vk" #name); \
    }

// Load the Vulkan loader library (MoltenVK on macOS, Khronos loader fallback).
void *VkMac_loadLib(void);

// Create a VkSurfaceKHR from the window's CAMetalLayer.
bool VkMac_createSurface(Window *window, VkInstance instance,
                         PFN_vkGetInstanceProcAddr gpa, VkSurfaceKHR *outSurface);

// Ensure the BGRA8 offscreen/IOSurface render pass exists.
bool VkMac_ensureIOSurfacePass(void);

// Get the BGRA8 offscreen/IOSurface render pass (call VkMac_ensureIOSurfacePass first).
VkRenderPass VkMac_getIOSurfacePass(void);

// Resize render trampoline (no-op on macOS; Vulkan has its own worker thread).
void VkMac_resizeRenderTrampoline(void *userdata);

#endif
