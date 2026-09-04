// src/main/enginetest.c — the windowed engine demo (Legacy: process/EngineTest.java).
//
// The showcase for the constructor-idiom Window API: construct hidden,
// mutate, show, run the fixed-timestep loop until Esc or the red button.
// No event wiring here — listeners live with the subsystems that need them
// (see tests/window_test.c); this demo polls state only.
//
//   Window *w = Window();            // default construct
//   Window_setSize(w, 800, 600);
//   Window_setLocation(w, 120, 120);
//   Window_show(w);

#include <stdio.h>

#include "annotation/overview.h"
#include "engine/loop.h"
#include "input/key.h"
#include "window/window.h"

typedef struct {
    Window *window;
    Loop loop;
    int frames;
} EngineTest;

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: EngineTest (main/enginetest.c — windowed engine demo)
 * LEVEL: L3 — Module Code (windowed engine demo harness)
 * ============================================================================
 * Showcase for the constructor-idiom Window API: construct hidden, mutate,
 * show, run the fixed-timestep loop until Esc or close. Polls state only —
 * event listeners live with their subsystems (see tests/window_test.c).
 *
 * STRUCT FIELDS (local to this file):
 * ----------------------------------------------------------------------------
 *   EngineTest { window, loop, frames } // Window + loop + frame counter
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - engine_tick(userdata)            : Poll events, stop on close/Esc
 *   - main()                           : Build 800x600 window, run loop
 * ============================================================================
 */

static EngineTest g_test = {0};

static void engine_tick(void *userdata) {
    EngineTest *t = userdata;
    Window_pollEvents();

    if (Window_shouldClose((*t).window) || Key_isDown(KEY_ESCAPE)) {
        Loop_stop(&(*t).loop);
        return;
    }

    (*t).frames++;
}

int main(void) {
    // Construct hidden, mutate, then reveal — no half-configured flash.
    g_test.window = Window();
    Window_setTitle(g_test.window, "anti enginetest");
    Window_setSize(g_test.window, 800, 600);
    Window_setLocation(g_test.window, 120, 120);
    Window_show(g_test.window);

    printf("enginetest live: %dx%d at (120,120); Esc or red button exits\n",
    Window_width(g_test.window),
    Window_height(g_test.window));

    g_test.loop = (Loop){ .tick = engine_tick, .userdata = &g_test, .frame_ms = 16, .running = false };
    Loop_run(&g_test.loop);

    printf("enginetest done after %d frames\n", g_test.frames);
    Key_shutdown();
    Window_destroy(g_test.window);
    return 0;
}
