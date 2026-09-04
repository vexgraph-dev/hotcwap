#ifndef VULKAN_VULKAN_H
#define VULKAN_VULKAN_H

#include <stdbool.h>
#include <stdint.h>

// vulkan/vk.h — the GPU backend seam (MoltenVK on macOS, software stays).
//
// Runtime loading only: the loader dylib is dlopen'd, every entry point is
// fetched through vkGetInstanceProcAddr. No link-time dependency, so a machine
// without MoltenVK degrades gracefully to the software path.
//
// Milestone contract: init builds the chain instance -> surface -> device ->
// swapchain; clearPresent acquires an image, clears it to a solid color and
// presents. THREAD CONTRACT: init and clearPresent run on thread 0 (the
// surface wraps the window's AppKit view).
//
// Content + policy come from the Window handle itself: the renderer reads the
// root container via Window_getContainer, pacing via Window_getPresentMode,
// and watches Window_renderGeneration for swapchain rebuilds.

typedef struct Window Window;

bool Vk_init(Window *window);
void Vk_shutdown(void);
bool Vk_ready(void);

// Acquire, clear the monitor cache to the window's background color (or the
// basket panel's own color while one is set), render ONE layer — the direct
// children of the window's container basket — at absolute desktop
// coordinates, then blit the window's region (top-left anchored) from the
// monitor's giant cache into the acquired swapchain image and present.
// False when not ready or the swapchain is _out of date. Present pacing
// follows Window_getPresentMode(window).
bool Vk_clearPresent(void);

// Pre-frame callback hook invoked before swapchain acquisition (e.g. offscreen passes).
typedef void (*VkPreFrameFn)(Window *window, int drawW, int drawH, void *userdata);
void Vk_setPreFrameRenderer(VkPreFrameFn fn, void *userdata);

// Frame render callback hook invoked inside swapchain render pass.
typedef void (*VkFrameRenderFn)(void *cmdBuffer, int drawW, int drawH, void *userdata);
void Vk_setFrameRenderer(VkFrameRenderFn fn, void *userdata);

// Set swapchain background clear color (0.0f - 1.0f RGBA).
void Vk_setClearColor(float r, float g, float b, float a);

// Human-readable stop point of the last init attempt ("ok", "no loader", ...).
const char *Vk_status(void);

// Solid-quad primitive: the default Panel draw, exposed so Panel_RenderFn
// overrides can compose real content _out of it ("pointing one function at
// another"). Records an axis-aligned fill at drawable-pixel coords into an
// open render pass; sets its own viewport (whole drawable) and scissor (the
// rect), per the VIEWPORT/SCISSOR SEPARATION LAW. Safe to call several times
// per handler for layered rects.
void Vk_fillRect(void *cmdBuffer, float surfaceW, float surfaceH, float x, float y, float w, float h,
                 float r, float g, float b, float a);

// Unified picture mode combining scaling strategy and 1:1 fill anchors.
typedef enum {
    PICTURE_MODE_FIT               = 0, // Stretch to fill
    PICTURE_MODE_ZOOM_FILL         = 1, // Scale to cover (centered)
    PICTURE_MODE_ZOOM_FIT          = 2, // Scale to contain (centered)
    PICTURE_MODE_FILL_CENTER       = 3, // 1:1 pixel mapping, centered
    PICTURE_MODE_FILL_TOP_LEFT     = 4, // 1:1 pixel mapping, top-left anchor
    PICTURE_MODE_FILL_TOP_RIGHT    = 5, // 1:1 pixel mapping, top-right anchor
    PICTURE_MODE_FILL_BOTTOM_LEFT  = 6, // 1:1 pixel mapping, bottom-left anchor
    PICTURE_MODE_FILL_BOTTOM_RIGHT = 7, // 1:1 pixel mapping, bottom-right anchor
} PictureMode;

// Renders a textured quad using the bindless texture array.
void Vk_drawTexture(void *cmdBuffer, float surfaceW, float surfaceH,
                    float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    int32_t textureId,
                    PictureMode mode,
                    float imgW, float imgH);

#endif

// Renders an SDF text glyph using the bindless texture array.
void Vk_drawSDFText(void *cmdBuffer, float surfaceW, float surfaceH,
                    float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    int32_t textureId, float bold, float smoothness,
                    float u0, float v0, float u1, float v1);

// Renders a color glyph (runtime/baked emoji) from a color atlas page.
// Same quad/UV plumbing as Vk_drawSDFText, but the fragment branch outputs
// raw RGBA: tint is ignored except for alpha (label opacity).
void Vk_drawColorGlyph(void *cmdBuffer, float surfaceW, float surfaceH,
                       float x, float y, float w, float h, float alpha,
                       int32_t textureId,
                       float u0, float v0, float u1, float v1);
