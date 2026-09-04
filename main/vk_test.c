#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include <string.h>

#include "darling/container.h"
#include "darling/panel.h"
#include "darling/scene.h"
#include "darling/picture.h"
#include "hot/hot.h"
#include "input/key.h"
#include "nio/mem.h"
#include "font/font.h"
#include "darling/label.h"
#include "oop/type.h"
#include "thread/thread.h"
#include "time/nanotime.h"
#include "vulkan/vk.h"
#include "window/window.h"

typedef struct {
    _Atomic bool running;



    // Honest present telemetry: one loop owns clear -> render -> blit ->
    // present for the whole compositor model, so there is exactly one FPS.
    _Atomic uint32_t presentFps;
    _Atomic uint32_t presentFrametimeUs;
} VkProbeState;

static VkProbeState g_state = {0};

// METHOD-SLOT OVERRIDE DEMO: this function is pointed at the HUD panel via
// Panel_setRenderHandler — the C equivalent of @Override. It replaces the
// built-in solid quad entirely and composes out of the public Vk_fillRect
// primitive instead: dark glass backing plus a sweeping accent bar whose
// width breathes with a 1-second sine.

extern int32_t Texture_load(const char *vfsPath);
extern bool    Texture_getSize(int32_t id, uint32_t *outW, uint32_t *outH);

#include "vulkan/vk.h"

static int32_t s_sunflowerId = -1;

// The modes we want to test: FIT, ZOOM_FILL (center), ZOOM_FIT (center), and the true FILL with its anchors.
typedef struct { PictureMode mode; const char *label; } ModeEntry;
static const ModeEntry s_modes[] = {
    { PICTURE_MODE_FIT,               "FIT" },
    { PICTURE_MODE_ZOOM_FILL,         "ZOOM_FILL" },
    { PICTURE_MODE_ZOOM_FIT,          "ZOOM_FIT" },
    { PICTURE_MODE_FILL_CENTER,       "FILL_CENTER" },
    { PICTURE_MODE_FILL_TOP_LEFT,     "FILL_TOP_LEFT" },
    { PICTURE_MODE_FILL_TOP_RIGHT,    "FILL_TOP_RIGHT" },
    { PICTURE_MODE_FILL_BOTTOM_LEFT,  "FILL_BOTTOM_LEFT" },
    { PICTURE_MODE_FILL_BOTTOM_RIGHT, "FILL_BOTTOM_RIGHT" },
};
#define NUM_MODES ((int)(sizeof(s_modes)/sizeof(s_modes[0])))

static _Atomic int s_modeIndex = 0;

static void pic_render(Panel *p, void *data, void *cmdBuffer, float x, float y, float w, float h) {
    (void)p; (void)data; (void)x; (void)y;
    if (s_sunflowerId < 0) {
        extern void Vk_fillRect(void *, float, float, float, float, float, float, float, float, float, float);
        Vk_fillRect(cmdBuffer, w, h, 0, 0, w, h, 1.0f, 0.8f, 0.0f, 1.0f);
        return;
    }

    uint32_t imgW = 1, imgH = 1;
    Texture_getSize(s_sunflowerId, &imgW, &imgH);

    const ModeEntry *m = &s_modes[s_modeIndex];
    Vk_drawTexture(cmdBuffer, w, h, 0, 0, w, h,
                   1.0f, 1.0f, 1.0f, 1.0f,
                   s_sunflowerId,
                   (*m).mode,
                   (float)imgW, (float)imgH);
}


static void hud_pulse(Panel *panel, void *renderer, void *cmdBuffer,
                      float x, float y, float w, float h) {
    (void)panel;
    (void)renderer;

    // Monotonic seconds: no 1s wrap, so the pulse never stutters on
    // frametimes that don't divide the second evenly.
    double t = (double)NanoTime_now() / 1e9;
    float pulse = 0.5f + 0.5f * sinf((float)(t * 6.28318530718));

    float barH = h * 0.08f;
    Vk_fillRect(cmdBuffer, w, h, x, y, w, h - barH, 0.07f, 0.09f, 0.11f, 0.92f);
    Vk_fillRect(cmdBuffer, w, h, x, y + h - barH, w * pulse, barH, 0.18f, 0.80f, 0.44f, 1.0f);
}

// Present Worker: clears the monitor cache, renders the basket's children
// onto it, blits the window region and presents — the whole loop.
static void vk_present_job(Thread *self, void *task) {
    (void)self;
    (void)task;

    static bool init = false;
    static uint64_t lastReportNanos = 0;
    static uint32_t frameCount = 0;

    if (!init) {
        lastReportNanos = NanoTime_now();
        init = true;
    }

    while (atomic_load_explicit(&g_state.running, memory_order_relaxed)) {
        uint64_t frameStart = NanoTime_now();

        Vk_clearPresent();

        frameCount++;
        uint64_t frameEnd = NanoTime_now();
        uint64_t frameUs = (frameEnd - frameStart) / 1000;
        atomic_store_explicit(&g_state.presentFrametimeUs, (uint32_t)frameUs, memory_order_relaxed);

        uint64_t elapsed = frameEnd - lastReportNanos;
        if (elapsed >= 500000000ULL) { // update every 500ms
            uint32_t fps = (uint32_t)((frameCount * 1000000000ULL) / elapsed);
            atomic_store_explicit(&g_state.presentFps, fps, memory_order_relaxed);
            frameCount = 0;
            lastReportNanos = frameEnd;
        }
    }
}

// Mode-cycling worker: switches picture mode every 3 seconds on its own thread
// so the main event loop stays responsive (e.g. during window resize).
// NOTE: the 3s wait is interruptible (30x100ms slices checking running) so
// window-close teardown joins in ~100ms instead of hanging up to 3s on the
// old monolithic nanosleep(&{3,0}).
static void mode_cycle_job(Thread *self, void *task) {
    (void)self;
    (void)task;

    printf("[texture-test] mode[%d] = %s\n",
           atomic_load(&s_modeIndex), s_modes[atomic_load(&s_modeIndex)].label);
    fflush(stdout);

    while (atomic_load_explicit(&g_state.running, memory_order_relaxed)) {
        for (int i = 0; i < 30; i++) {
            struct timespec ts = { 0, 100 * 1000 * 1000 }; // 100ms slice
            nanosleep(&ts, nullptr);
            if (!atomic_load_explicit(&g_state.running, memory_order_relaxed))
                break;
            if (self && !Thread_isRunning(self))
                break;
        }

        if (!atomic_load_explicit(&g_state.running, memory_order_relaxed))
            break;
        if (self && !Thread_isRunning(self))
            break;

        int next = (atomic_load(&s_modeIndex) + 1) % NUM_MODES;
        atomic_store(&s_modeIndex, next);
        printf("[texture-test] mode[%d] = %s\n", next, s_modes[next].label);
        fflush(stdout);
    }
}

int main(void) {
    extern void System_initializeAll(void);
    System_initializeAll();

    // Initialize hotloader
    HotModule *hot = Hot_init("hot");
    if (hot) {
        fprintf(stderr, "[hot] watching hot/ for module updates\n");
    }

    Window *w = Window();
    Window_setTitle(w, "anti vk probe");
    Window_setSize(w, 640, 400);
    Window_setUndecorated(w, WINDOW_UNDECORATED_NAKED);
    Window_show(w);

    int vkResult = Vk_init(w);
    printf("vk init: %d (%s)\n", vkResult, Vk_status());
    if (!vkResult) {
        fprintf(stderr, "vk init failed: %s\n", Vk_status());
        Window_destroy(w);
        Key_shutdown();
        return 1;
    }

    // Demo hook: ANTI_BAKE_FONTS=1 refreshes ~/anti/fonts at startup, when
    // Vulkan (and the jump-flood baker) is awake. Delete a .antifont and
    // watch it come back in seconds via the gpu flow (cpu fallback if not).
    if (getenv("ANTI_BAKE_FONTS")) {
        extern size_t FontBake_refreshAllFonts(void);
        printf("[vk_test] ANTI_BAKE_FONTS: refreshing font store...\n");
        size_t n = FontBake_refreshAllFonts();
        printf("[vk_test] ANTI_BAKE_FONTS: %zu families (re)baked\n", n);
    }

    Window_setBlur(w, 1);
    Window_setScenePanel(w, nullptr);

    // 2. Content panel: logical placeholder for UI & floating layers
    // Note: its background color (e.g. black) is ignored and transparent.
    Panel *contentPanel = Panel();
    Panel_setLocation(contentPanel, 0.0f, 0.0f);
    Panel_setSize(contentPanel, 8192.0f, 8192.0f); // max ceiling
    Panel_setSize(contentPanel, 640.0f, 400.0f);   // actual size
    Panel_setBackgroundColor(contentPanel, 0xFF000000u); // ignored placeholder color
    Window_setContentPanel(w, contentPanel);

    // 3. HUD: child in contentPanel, IOSurface-backed, renders via Vulkan into its own buffer
    Panel *hud = Panel();
    Panel_setLocation(hud, 40.0f, 40.0f);
    Panel_setSize(hud, 640.0f, 400.0f); // first call = max buffer allocation size
    Panel_setSize(hud, 220.0f, 100.0f); // second call = actual size (clamped)
    Panel_setBackgroundColor(hud, 0xFF2E7D32u);
    Panel_setRenderHandler(hud, hud_pulse);
    Panel_addContainer(contentPanel, hud);

    // 4. Mini-3D viewport: floating 3D scene inside contentPanel (IOSurface-backed)
    Scene3D *mini3D = Scene3D_0();
    Scene3D_setLocation(mini3D, 0.0f, 0.0f);
    Scene3D_setSize(mini3D, 1000.0f, 1000.0f); // max buffer allocation size
    Scene3D_setParentAnchor(mini3D, CONTAINER_PARENT_ANCHOR_BOTTOM_RIGHT);
    Scene3D_setSelfAnchor(mini3D, CONTAINER_SELF_ANCHOR_BOTTOM_RIGHT);
    Scene *mini3DScene = &(*mini3D).base;
    Panel *mini3DPanel = &(*mini3DScene).base;
    Panel_addContainer(contentPanel, mini3DPanel);

    // 5. Picture testing node (Middle Center anchoring)
    Picture *pic = Picture_0();
    Picture_setSize(pic, 1024.0f, 1024.0f); // first call allocates max bounds
    Picture_setSize(pic, 400.0f, 400.0f);   // Make it bigger to see the flower!
    
    // Load texture via hotloader if available
    const char *initial_path = "/Users/vexgraph/Downloads/sunflower.png";
    s_sunflowerId = Texture_load(initial_path);
    Panel_setRenderHandler(&(*pic).base, pic_render);

    Picture_setParentAnchor(pic, CONTAINER_PARENT_ANCHOR_MIDDLE_CENTER);
    // Set self anchor to its own middle (pivot point)
    Picture_setSelfAnchor(pic, CONTAINER_SELF_ANCHOR_MIDDLE_CENTER);
    
    // x=0, y=0 offset from the anchor point (which is now exactly in the center)
    Picture_setLocation(pic, 0.0f, 0.0f);
    Panel_addContainer(contentPanel, &(*pic).base);


    // --- LABEL TEST START ---
    Font *sysFont = Font_loadSystem("Google Sans Code");
    if (!sysFont) {
        printf("Warning: Comic Sans MS not found, falling back to system default\n");
        sysFont = Font_loadSystem("Helvetica");
    }
    
    Label *label = Label(contentPanel, "hello hello hello hello"
                                       "\n"
                                       "world world world world"
                                       "\n"
                                       "\xF0\x9F\x8C\xBB\xF0\x9F\x8C\xBB"
                                       "\xF0\x9F\x8C\xBB\xF0\x9F\x8C\xBB");
    Label_setFont(label, sysFont);
    Label_setFontFamily(label, "Google Sans Code");
    Label_setFontSize(label, 100.0f);
    Label_setTextColor(label, 0xFFFFFFFF);
    Label_setBackgroundColor(label, 0x99000000);
    Label_setSize(label, 1000.0f, 500.0f);
    Label_setLocation(label, 30.0f, 30.0f);
    Label_setSmoothness(label, 0.f);
    if (sysFont) {
        Font_prewarm(sysFont, "hello world");
        printf("[vk_test] pre-warmed font atlas at 128pt for \"hello world\" texId=%d smoothness=%.2f\n", Font_getTextureId(sysFont), 0.1f);
        fflush(stdout);
    }
    // --- LABEL TEST END ---
    // Enable native IOSurface backing on content panel
    Window_forceNativeContainerOnRoot(w, true);
    printf("native IOSurface: ON (background=Vulkan swapchain, overlays=Vulkan->IOSurface)\n");

    atomic_store(&g_state.running, true);

    // Present Worker: owns the whole cache-clear/render/blit/present loop
    Thread *presentWorker = Thread_new(TYPE_THREAD_UI_SINGLETON, vk_present_job,
                                       1024, true, false);
    if (!presentWorker || !Thread_run(presentWorker)) {
        fprintf(stderr, "failed to start Vulkan present worker thread\n");
        atomic_store(&g_state.running, false);
        Vk_shutdown();
        Memory_free(pic);
        Memory_free(mini3D);
        Memory_free(hud);
        Label_free(label);
        Memory_free(contentPanel);
        Window_destroy(w);
        Key_shutdown();
        return 1;
    }

    // Mode-cycling worker: switches picture mode every 3 seconds in its own thread
    // so the main event loop stays responsive during window resize.
    Thread *modeCycler = Thread_new(TYPE_THREAD_UI_SINGLETON, mode_cycle_job,
                                    1024, true, false);
    if (modeCycler) Thread_run(modeCycler);

    uint64_t lastReport = NanoTime_now();
    char titleBuf[256];

    while (!Window_shouldClose(w) && !Key_isDown(KEY_ESCAPE)) {
        Window_pollEvents();

        // Poll for hot module updates
        if (hot) {
            uint32_t loaded = 0;
            Hot_poll(hot, &loaded);
            if (loaded > 0) {
                printf("[hot] %u module(s) reloaded\n", loaded);
            }
            
            // Check for texture path change
            typedef const char *(*PathFn)(void);
            PathFn get_path = (PathFn)Hot_get_symbol(hot, "hot_texture_path");
            if (get_path) {
                const char *new_path = get_path();
                if (new_path) {
                    // Dedup: only reload if path actually changed
                    static char s_last_path[512] = "";
                    if (strcmp(s_last_path, new_path) != 0) {
                        strncpy(s_last_path, new_path, sizeof(s_last_path) - 1);
                        s_last_path[sizeof(s_last_path) - 1] = '\0';
                        // Reload texture
                        int32_t new_id = Texture_load(new_path);
                        if (new_id >= 0 && new_id != s_sunflowerId) {
                            printf("[hot] texture swapped: %s (id=%d)\n", new_path, new_id);
                            s_sunflowerId = new_id;
                        }
                    }
                }
            }
        }

        int winW = Window_width(w);
        int winH = Window_height(w);

        // 1ms event sleep keeps event pump at 1000Hz with zero idle CPU load
        struct timespec tick = { 0, 1000 * 1000 };
        nanosleep(&tick, nullptr);

        uint64_t now = NanoTime_now();
        uint64_t elapsed = now - lastReport;

        if (elapsed >= 250000000ULL) { // update title every 250ms
            lastReport = now;

            uint32_t pFps = atomic_load_explicit(&g_state.presentFps, memory_order_relaxed);
            float pMs = (float)atomic_load_explicit(&g_state.presentFrametimeUs, memory_order_relaxed) / 1000.0f;

            snprintf(titleBuf, sizeof(titleBuf),
                     "anti | %s | %u FPS (%.2f ms) | %dx%d",
                     s_modes[s_modeIndex].label, pFps, pMs, winW, winH);
            Window_setTitle(w, titleBuf);
            printf("[telemetry] Present FPS: %u (%.2f ms) | %dx%d\n",
                   pFps, pMs, winW, winH);
            fflush(stdout);
        }
    }

    // Teardown
    atomic_store(&g_state.running, false);
    if (presentWorker) {
        Thread_stop(presentWorker);
        Thread_free(presentWorker);
    }
    if (modeCycler) {
        Thread_stop(modeCycler);
        Thread_free(modeCycler);
    }

    Vk_shutdown();
    Memory_free(pic);
    Memory_free(mini3D);
    Memory_free(hud);
    Label_free(label);
    Memory_free(contentPanel);
    Window_destroy(w);
    Key_shutdown();

    // Shutdown hotloader
    if (hot) {
        HotShutdown(hot);
    }

    Memory_freeAll();
    return 0;
}
