#include "kernel/kernel.h"

#include <stdlib.h>
#include <time.h>

#include "annotation/overview.h"
#include "hot/spv_watch.h"
#include "input/key.h"
#include "input/mouse.h"
#include "time/nanotime.h"
#include "vulkan/vk.h"
#include "window/window.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Kernel (kernel/kernel.c)
 * LEVEL: L4 — Self-Management (R0 Supervisor; Rule 17 vs Rule 28: L = edit-risk, R = supervision)
 * ============================================================================
 * R0 Host Supervisor: the thin nano-VM. Owns the master session arena, the
 * transient per-tick scratch arena, and the Application registry (N apps x M
 * windows per process). Boots first, tears down last. Holds windows stable
 * across HotModule swaps; knows nothing about darling widgets, api-haven
 * schemas, database drivers, language grammars, or engines.
 *
 * STRUCT FIELDS (Mirroring kernel/kernel.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   MemoryArena *arena;                          // master session arena
 *   MemoryArena *transientArena;                  // per-tick scratch arena
 *   Application *applications[KERNEL_MAX_APPS];   // registered apps (opaque handles)
 *   uint32_t applicationCount;                    // used slots in applications[]
 *   _Atomic bool running;                         // supervisor active flag
 *
 * PRIVATE HELPERS: None.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Kernel()                            : Kernel_0()
 *   - Kernel(arenaBytes)                  : Kernel_1(arenaBytes)
 *   - Kernel(arenaBytes, transientBytes)  : Kernel_2(arenaBytes, transientBytes)
 *
 * Core Functions:
 *   - Kernel_destroy(self)
 *   - Kernel_stop(self)
 *   - Kernel_isRunning(self)
 *   - Kernel_run(self)
 *   - Kernel_tick(self, dt)
 *   - Kernel_addApplication(self, app)
 *   - Kernel_removeApplication(self, app)
 *
 * Getters:
 *   - Kernel_getApplication(self, index)
 *   - Kernel_getApplicationCount(self)
 *   - Kernel_getApplications(self, out, cap)
 *   - Kernel_getArena(self)
 *   - Kernel_getTransientArena(self)
 * ============================================================================
 */

// ;;INTENTION("Phase-1 Kernel struct is calloc-owned like Application_0; arenas are MemoryArena-owned. Migrating the struct itself into arena storage happens once multi-app Kernel_run multiplexing lands — keeps teardown order provable today per Rule 33.")


// CONSTRUCTORS
Kernel *Kernel_0(void) {
    return Kernel_2(KERNEL_ARENA_DEFAULT, KERNEL_TRANSIENT_DEFAULT);
}

Kernel *Kernel_1(size_t arenaBytes) {
    return Kernel_2(arenaBytes, KERNEL_TRANSIENT_DEFAULT);
}

Kernel *Kernel_2(size_t arenaBytes, size_t transientBytes) {
    Kernel *self = (Kernel*) calloc(1, sizeof(Kernel));
    if (!self)
        return NULL;
    MemoryArena *arena = MemoryArena_create(arenaBytes);
    if (!arena) {
        free(self);
        return NULL;
    }
    MemoryArena *scratch = MemoryArena_create(transientBytes);
    if (!scratch) {
        MemoryArena_destroy(arena);
        free(self);
        return NULL;
    }
    (*self).arena = arena;
    (*self).transientArena = scratch;
    (*self).applicationCount = 0;
    atomic_store_explicit(&(*self).running, true, memory_order_relaxed);
    return self;
}

// CORE FUNCTIONS
void Kernel_destroy(Kernel *self) {
    if (!self)
        return;
    Kernel_stop(self);

    // Bounded wait for worker threads to observe running = false (Rule 27)
    struct timespec ts = { 0, 50 * 1000 * 1000 };
    nanosleep(&ts, nullptr);

    if (Vk_ready())
        Vk_shutdown();

    for (uint32_t i = 0; i < (*self).applicationCount; i++)
        (*self).applications[i] = NULL;
    (*self).applicationCount = 0;

    MemoryArena *scratch = (*self).transientArena;
    MemoryArena *arena = (*self).arena;
    (*self).transientArena = NULL;
    (*self).arena = NULL;
    if (scratch)
        MemoryArena_destroy(scratch);
    if (arena)
        MemoryArena_destroy(arena);
    free(self);
}

void Kernel_stop(Kernel *self) {
    if (!self)
        return;
    atomic_store_explicit(&(*self).running, false, memory_order_relaxed);
    for (uint32_t i = 0; i < (*self).applicationCount; i++) {
        Application *app = (*self).applications[i];
        if (app)
            Application_stop(app);
    }
}

bool Kernel_isRunning(const Kernel *self) {
    if (!self)
        return false;
    return atomic_load_explicit(&(*self).running, memory_order_relaxed);
}

bool Kernel_tick(Kernel *self, double dt) {
    if (!self)
        return false;
    if (!atomic_load_explicit(&(*self).running, memory_order_relaxed))
        return false;

    // 1. Reset per-cycle scratch arena FIRST before any event polling or allocations
    MemoryArena *scratch = (*self).transientArena;
    if (scratch)
        MemoryArena_freeAll(scratch);

    // 2. Single Thread-0 OS event pump
    Window_pollEvents();

    Mouse_dispatchEvents();
    Key_dispatchEvents();

    if (Key_isDown(KEY_ESCAPE)) {
        Kernel_stop(self);
        return false;
    }

    // 3. Poll SPV shader watchers on registered applications
    for (uint32_t i = 0; i < (*self).applicationCount; i++) {
        Application *app = (*self).applications[i];
        if (!app)
            continue;
        SpvWatch *spv = Application_getSpvWatch(app);
        if (spv && SpvWatch_changed(spv))
            SpvWatch_snap(spv);
    }

    // 4. Tick each active application
    bool anyRunning = false;
    for (uint32_t i = 0; i < (*self).applicationCount; i++) {
        Application *app = (*self).applications[i];
        if (!app)
            continue;
        if (Application_isRunning(app)) {
            if (Application_tick(app, dt))
                anyRunning = true;
        }
    }

    if ((*self).applicationCount > 0 && !anyRunning) {
        atomic_store_explicit(&(*self).running, false, memory_order_relaxed);
        return false;
    }

    // 5. Presentation pass: single Kernel-owned present pass
    if (Vk_ready())
        Vk_clearPresent();

    return true;
}

int Kernel_run(Kernel *self) {
    if (!self)
        return -1;
    atomic_store_explicit(&(*self).running, true, memory_order_relaxed);

    // Warm up / start registered applications
    for (uint32_t i = 0; i < (*self).applicationCount; i++) {
        Application *app = (*self).applications[i];
        if (!app)
            continue;
        Application_start(app);

        uint32_t winCount = Application_getWindowCount(app);
        for (uint32_t wIdx = 0; wIdx < winCount; wIdx++) {
            Window *w = Application_getWindow(app, wIdx);
            if (w) {
                if (!Vk_ready())
                    Vk_init(w);
                for (int frame = 0; frame < 60; frame++) {
                    if (Vk_clearPresent())
                        break;
                    struct timespec ws = { 0, 8 * 1000 * 1000 };
                    nanosleep(&ws, nullptr);
                }
                Window_show(w);
            }
        }
    }

    uint64_t lastTick = NanoTime_now();

    while (atomic_load_explicit(&(*self).running, memory_order_relaxed)) {
        uint64_t now = NanoTime_now();
        double dt = (double)(now - lastTick) / 1e9;
        lastTick = now;

        if (!Kernel_tick(self, dt))
            break;

        struct timespec ts = { 0, 1 * 1000 * 1000 };
        nanosleep(&ts, nullptr);
    }

    atomic_store_explicit(&(*self).running, false, memory_order_relaxed);
    return 0;
}

bool Kernel_addApplication(Kernel *self, Application *app) {
    if (!self || !app)
        return false;
    for (uint32_t i = 0; i < (*self).applicationCount; i++)
        if ((*self).applications[i] == app)
            return false;
    if ((*self).applicationCount >= KERNEL_MAX_APPS)
        return false;
    (*self).applications[(*self).applicationCount++] = app;
    return true;
}

bool Kernel_removeApplication(Kernel *self, Application *app) {
    if (!self || !app)
        return false;
    for (uint32_t i = 0; i < (*self).applicationCount; i++) {
        if ((*self).applications[i] == app) {
            (*self).applications[i] = (*self).applications[--(*self).applicationCount];
            (*self).applications[(*self).applicationCount] = NULL;
            return true;
        }
    }
    return false;
}

// GETTERS
Application *Kernel_getApplication(const Kernel *self, uint32_t index) {
    if (!self)
        return NULL;
    if (index >= (*self).applicationCount)
        return NULL;
    return (*self).applications[index];
}

uint32_t Kernel_getApplicationCount(const Kernel *self) {
    if (!self)
        return 0;
    return (*self).applicationCount;
}

uint32_t Kernel_getApplications(const Kernel *self, Application **out, uint32_t cap) {
    if (!self)
        return 0;
    if (!out || cap == 0)
        return 0;
    uint32_t count = (*self).applicationCount;
    uint32_t n = count < cap ? count : cap;
    for (uint32_t i = 0; i < n; i++)
        out[i] = (*self).applications[i];
    return n;
}

MemoryArena *Kernel_getArena(const Kernel *self) {
    if (!self)
        return NULL;
    return (*self).arena;
}

MemoryArena *Kernel_getTransientArena(const Kernel *self) {
    if (!self)
        return NULL;
    return (*self).transientArena;
}
