// tests/window_test.c — the Window class _test.
//
// Two halves:
//   1. One-shot chrome checks right after create (title/size/constraints,
//      resizable toggle, DRM, minimize/restore).
//   2. A stepped traffic-light tour: each combination is held ~2s so the
//      chrome is visibly verifiable, then the window returns to the default
//      decorated state for the user to close.
//
// Traffic-light semantics (AppKit):
//   red    = Closable       (style bit 1<<1)
//   yellow = Miniaturizable (style bit 1<<2)
//   green  = shown when Resizable; behavior switches on the fullscreen
//            collection bit — button OFF = zoom (fills screen, same desktop),
//            button ON  = native fullscreen (new mac desktop space).

#include <stdio.h>

#include "annotation/overview.h"
#include "engine/loop.h"
#include "input/key.h"
#include "input/mouse.h"
#include "input/touch.h"
#include "window/window.h"

typedef struct {
    Window *window;
    Loop loop;
    int frames;
} win_ctx_t;

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: WindowTest (tests/window_test.c — Window class _test)
 * LEVEL: L4 — Self-Management (tests L4 window chrome; takes level of what it runs)
 * ============================================================================
 * Two halves: one-shot chrome checks after create (title/size/constraints,
 * resizable toggle, minimize/restore), then a stepped traffic-light tour
 * held ~2s per combination for visible verification. Input listeners echo
 * every key/mouse/scroll/move event the pipeline produces.
 *
 * STRUCT FIELDS (local to this file):
 * ----------------------------------------------------------------------------
 *   win_ctx_t { window, loop, frames } // Window + fixed-timestep loop state
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - on_key_down/up(self, keyEvent, nanos) : Key echo; Esc stops the loop
 *   - on_mouse_down(self, mouseEvent, nanos) : Button/tap/position echo
 *   - on_scroll(self, dx, dy)          : Scroll-delta echo
 *   - on_move(self, x, y)              : Throttled (1/60) move echo
 *   - greenLabel(step)                 : Fullscreen-vs-zoom label for a step
 *   - applyStep(ctx, step)             : Apply one chrome combination
 *   - win_tick(userdata)               : Per-frame event pump + frame count
 *   - main()                           : Chrome checks + traffic-light tour
 * ============================================================================
 */

static win_ctx_t *g_winCtx = nullptr;

// --- Input listeners: echo everything the pipeline produces.
// Each listener is an "implementing object": the vtable's .self points back
// at it, and every callback receives it first (the C interface pattern). ---

static void on_key_down(void *self, int keyEvent, uint64_t nanos) {
    (void) self; (void) nanos;
    printf("key DOWN  %-12s mods=%c%c%c%c code=%d\n", Key_name(Key_code(keyEvent)),
           Key_hasShift(keyEvent) ? 'S' : '-', Key_hasControl(keyEvent) ? 'C' : '-',
           Key_hasOption(keyEvent) ? 'O' : '-', Key_hasCommand(keyEvent) ? 'M' : '-',
           Key_code(keyEvent));
}

static void on_key_up(void *self, int keyEvent, uint64_t nanos) {
    (void) self; (void) nanos;
    printf("key UP    %-12s hold=%.1fms taps=%d\n", Key_name(Key_code(keyEvent)),
           Key_lastHoldDurationNanos(Key_code(keyEvent)) / 1e6,
           Key_taps(Key_code(keyEvent)));
    if (Key_code(keyEvent) == KEY_ESCAPE && g_winCtx)
        Loop_stop(&(*g_winCtx).loop); // Esc exits
}

static void on_mouse_down(void *self, int mouseEvent, uint64_t nanos) {
    (void) self; (void) nanos;
    printf("mouse DOWN %s taps=%d at (%.0f, %.0f)\n",
           Mouse_name(Mouse_button(mouseEvent)), Mouse_taps(Mouse_button(mouseEvent)),
           Mouse_x(), Mouse_y());
}

static void on_scroll(void *self, double dx, double dy) {
    (void) self;
    printf("scroll (%.2f, %.2f)\n", dx, dy);
}

static int g_moves = 0;

static void on_move(void *self, double x, double y) {
    (void) self;
    if (++g_moves % 60 == 0) // throttle: one line per ~60 moves
        printf("move (%.0f, %.0f)\n", x, y);
}

static const KeyEvent g_keyListener = {
    .self = nullptr,
    .onKeyDown = on_key_down, .onKeyUp = on_key_up, .onKeyRepeat = nullptr, .onCharTyped = nullptr,
};
static const MouseEvent g_mouseListener = {
    .self = nullptr,
    .onMouseDown = on_mouse_down, .onMouseUp = nullptr, .onMouseRepeat = nullptr,
    .onMouseMove = on_move, .onMouseMoveDelta = nullptr, .onMouseScroll = on_scroll,
    .onMouseDrag = nullptr, .onMouseZoom = nullptr,
};
static const TouchEvent g_touchListener = {
    .self = nullptr,
    .onTouchDown = nullptr, .onTouchUp = nullptr, .onTouchMove = nullptr, .onTouchCancel = nullptr,
};

#define STEP_FRAMES 120 // ~2s at 16ms

typedef struct {
    const char *label;
    bool closable;
    bool miniaturizable;
    bool resizable;
    bool fullscreen_button; // green: false = zoom, true = mac fullscreen
    int undecorated;        // WINDOW_UNDECORATED_*, or -1 to leave style alone
} chrome_step_t;

static const chrome_step_t g_steps[] = {
    { .label = "default: red + yellow + green(fullscreen)", .closable = true, .miniaturizable = true, .resizable = true, .fullscreen_button = true, .undecorated = -1 },
    { .label = "red only",                                 .closable = true, .miniaturizable = false, .resizable = false, .fullscreen_button = true, .undecorated = -1 },
    { .label = "yellow only",                              .closable = false, .miniaturizable = true, .resizable = false, .fullscreen_button = true, .undecorated = -1 },
    { .label = "green zoom only (fills screen, no new desktop)", .closable = false, .miniaturizable = false, .resizable = true, .fullscreen_button = false, .undecorated = -1 },
    { .label = "green fullscreen only (mac way, new desktop)", .closable = false, .miniaturizable = false, .resizable = true, .fullscreen_button = true, .undecorated = -1 },
    { .label = "yellow + green, no red",                   .closable = false, .miniaturizable = true, .resizable = true, .fullscreen_button = true, .undecorated = -1 },
    { .label = "red + yellow, no green",                   .closable = true, .miniaturizable = true, .resizable = false, .fullscreen_button = true, .undecorated = -1 },
    { .label = "red + green, no yellow",                   .closable = true, .miniaturizable = false, .resizable = true, .fullscreen_button = true, .undecorated = -1 },
    { .label = "naked: hidden title, traffic lights kept", .closable = true, .miniaturizable = true, .resizable = true, .fullscreen_button = true, .undecorated = WINDOW_UNDECORATED_NAKED },
    { .label = "borderless: no chrome at all",             .closable = true, .miniaturizable = true, .resizable = true, .fullscreen_button = true, .undecorated = WINDOW_UNDECORATED_BORDERLESS },
    { .label = "back to decorated",                        .closable = true, .miniaturizable = true, .resizable = true, .fullscreen_button = true, .undecorated = WINDOW_DECORATED },
};

#define STEP_COUNT ((int)(sizeof(g_steps) / sizeof(g_steps[0])))

static const char *greenLabel(const chrome_step_t *step) {
    if (!(*step).resizable)
        return "hidden";
    return (*step).fullscreen_button ? "fullscreen" : "zoom";
}

static void applyStep(win_ctx_t *ctx, const chrome_step_t *step) {
    Window_setClosable((*ctx).window, (*step).closable);
    Window_setMiniaturizable((*ctx).window, (*step).miniaturizable);
    Window_setResizable((*ctx).window, (*step).resizable);
    Window_setFullscreenButton((*ctx).window, (*step).fullscreen_button);
    if ((*step).undecorated >= 0)
        Window_setUndecorated((*ctx).window, (*step).undecorated);
    printf("step %2d/%d %s | red=%d yellow=%d green=%s\n",
           ((*ctx).frames / STEP_FRAMES) + 1, STEP_COUNT, (*step).label,
           Window_isClosable((*ctx).window), Window_isMiniaturizable((*ctx).window),
           greenLabel(step));
}

static void win_tick(void *userdata) {
    win_ctx_t *ctx = userdata;
    Window_pollEvents();

    // Consumer side: drain the input rings into the listeners.
    Window_dispatchEvents((*ctx).window);

    if (Window_shouldClose((*ctx).window)) {
        Loop_stop(&(*ctx).loop);
        return;
    }

    int step = (*ctx).frames / STEP_FRAMES;
    if (step < STEP_COUNT && (*ctx).frames % STEP_FRAMES == 0)
        applyStep(ctx, &g_steps[step]);

    (*ctx).frames++;
    printf("frame=%d\n", (*ctx).frames);
}

int main(void) {
    win_ctx_t ctx = { .frames = 0 };
    g_winCtx = &ctx;

    ctx.window = Window_create("anti", 640, 480);
    Window_addKeyAdapter(ctx.window, &g_keyListener);
    Window_addMouseAdapter(ctx.window, &g_mouseListener);
    Window_addTouchAdapter(ctx.window, &g_touchListener);

    ctx.loop = (Loop){ .tick = win_tick, .userdata = &ctx, .frame_ms = 16, .running = false };

    Window_setTitle(ctx.window, "anti engine");
    Window_setSize(ctx.window, 800, 600);
    Window_setSize(ctx.window, 900, 700);
    Window_setMinSize(ctx.window, 320, 240);
    Window_setMaxSize(ctx.window, 1920, 1080);
    Window_show(ctx.window);

    printf("chrome resizable=%d closable=%d miniaturizable=%d fullscreen=%d\n",
           Window_isResizable(ctx.window), Window_isClosable(ctx.window),
           Window_isMiniaturizable(ctx.window), Window_isFullscreen(ctx.window));

    Window_setResizable(ctx.window, false);
    printf("resizable=%d after disable\n", Window_isResizable(ctx.window));
    Window_setResizable(ctx.window, true);

    Window_setDRM(ctx.window, true);
    Window_setFullscreenButton(ctx.window, false);
    Window_setFullscreenButton(ctx.window, true);
    Window_setLocation(ctx.window, 120, 120);
    Window_center(ctx.window);

    Window_minimize(ctx.window);
    Window_restore(ctx.window);
    printf("minimized=%d after restore\n", Window_isMinimized(ctx.window));

    printf("chrome ready; stepping traffic-light combinations (close the window or press Esc to exit)\n");

    Loop_run(&ctx.loop);

    printf("window closed after %d frames\n", ctx.frames);
    Window_destroy(ctx.window);
    return 0;
}