#ifndef HOT_KERNEL_KERNEL_H
#define HOT_KERNEL_KERNEL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/application.h"
#include "nio/mem.h"

// kernel/kernel.h — R0 Host Supervisor (thin nano-VM).
//
// Owns the process lifetime: master arena, transient scratch arena, and the
// Application registry. Boots first, tears down last. Knows NOTHING about
// darling widgets, api-haven schemas, database drivers, language grammars,
// or engines — those attach as opaque Application handles + callbacks
// (AppRunFn / AppTickFn / AppHotReloadFn) + HotModule dylibs.
//
// Lifecycle (vk_test order):
//   kernel -> application -> Kernel_addApplication -> window ->
//   Application_addWindow -> Application_run -> Application_free ->
//   Kernel_destroy.
//
// Ownership law: Kernel REGISTERS applications and multiplexes their
// non-blocking ticks via Kernel_tick / Kernel_run on Thread 0.
// Kernel_destroy stops all apps top-down, bounds-joins threads, tears down
// Vulkan, destroys transient arena, then master arena LAST per Rule 26.

#define KERNEL_MAX_APPS 8
#define KERNEL_ARENA_DEFAULT (64 * 1024 * 1024)
#define KERNEL_TRANSIENT_DEFAULT (64 * 1024 * 1024)

typedef struct Kernel Kernel;

struct Kernel {
    MemoryArena *arena;                              // master session arena (owns structure)
    MemoryArena *transientArena;                     // per-tick scratch arena (reset, never freed mid-frame)
    Application *applications[KERNEL_MAX_APPS];      // registered apps (opaque to engines)
    uint32_t applicationCount;                       // used slots in applications[]
    _Atomic bool running;                            // supervisor active flag
    Thread *presentWorker;                           // present thread: board + all VkPane chains (GUI mode)
};

// --- Overloaded constructors ---
//
//   Kernel()                         -> defaults (64MB master + 64MB transient)
//   Kernel(arenaBytes, transientBytes) -> sized arenas
//
// Arenas come from MemoryArena_create (isolated slab sets, ABI-stable).
// The Kernel struct itself is calloc-owned in Phase 1 (mirrors
// Application_0); migration to arena-owned Kernel is tracked via
// ;;INTENTION in kernel.c per Rule 33.
Kernel *Kernel_0(void);
Kernel *Kernel_1(size_t arenaBytes);
Kernel *Kernel_2(size_t arenaBytes, size_t transientBytes);

#define KERNEL_CHOOSER(_0, _1, _2, NAME, ...) NAME

#define Kernel(...) KERNEL_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Kernel_2, Kernel_1, Kernel_0 \
)(__VA_ARGS__)

// Free supervision AFTER all applications and windows are gone.
// Stops every app, resets transient arena, destroys master arena LAST,
// then frees the Kernel struct. Never call while any Application_run loop
// or present worker is still active — stop those first (Rule 27 bounded).
void Kernel_destroy(Kernel *self);

// --- Supervisor state & execution ---
bool Kernel_isRunning(const Kernel *self);
void Kernel_stop(Kernel *self);
int  Kernel_run(Kernel *self);
bool Kernel_tick(Kernel *self, double dt);

// --- Application registry (multi-app, N apps x M windows per process) ---
// Register a live application. False on NULL, duplicate, or full registry.
bool Kernel_addApplication(Kernel *self, Application *app);
// Unregister an application (swap-remove, order not preserved). False if absent.
// Registered windows stay owned by the Application (detach-only, OS-owned).
bool Kernel_removeApplication(Kernel *self, Application *app);
// Application at index, or NULL when out of range.
Application *Kernel_getApplication(const Kernel *self, uint32_t index);
// Number of registered applications.
uint32_t Kernel_getApplicationCount(const Kernel *self);
// Copy registry into out[] (up to cap), returns entries written.
uint32_t Kernel_getApplications(const Kernel *self, Application **out, uint32_t cap);

// --- Arena access (Rule 24 symmetric getters) ---
MemoryArena *Kernel_getArena(const Kernel *self);
MemoryArena *Kernel_getTransientArena(const Kernel *self);

#endif
