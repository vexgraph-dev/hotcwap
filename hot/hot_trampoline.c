#include "hot/hot_trampoline.h"

#include <string.h>

#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: HotTrampolineTable (hot/hot_trampoline.c)
 * LEVEL: L4 — Self-Management (per-instance swap machinery the loader stands on)
 * ============================================================================
 * One atomic function-pointer table per HotModule instance. Two loaders
 * share this code but resolve through their own tables — same symbol,
 * different targets, zero collision. Reload swaps a row's ptr while
 * fallback_ptr covers mid-swap readers.
 *
 * STRUCT FIELDS (Mirroring hot/hot_trampoline.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   HotTrampoline rows[HOT_MAX_TRAMPOLINES]; // one row per export (max 1024)
 *   _Atomic uint32_t count;                  // used rows (cross-thread readers)
 *
 * SLOT RECORD (public, behaviorless, owned by this table):
 * ----------------------------------------------------------------------------
 *   HotTrampoline (one row per exported symbol):
 *     _Atomic(void*) ptr;                    // current generation target
 *     _Atomic(void*) fallback_ptr;           // prior generation (mid-swap cover)
 *     char name[HOT_MANIFEST_MAX_NAME];      // export symbol name
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - HotTrampolineTable_register(self, name) : allocate a row, returns index
 *   - HotTrampolineTable_set(self, idx, ptr)  : atomic swap, stash old as fallback
 *   - HotTrampolineTable_find(self, name)     : index by symbol name, -1 if absent
 *
 * Getters:
 *   - HotTrampolineTable_get(self, idx)       : current ptr with fallback cover
 * ============================================================================
 */

// CORE FUNCTIONS
// Register a row for a function. Returns the row index.
int HotTrampolineTable_register(HotTrampolineTable *self, const char *name) {
    if (!self || !name) return -1;
    uint32_t idx = atomic_fetch_add(&(*self).count, 1);
    if (idx >= HOT_MAX_TRAMPOLINES) return -1;
    HotTrampoline *row = &(*self).rows[idx];
    strncpy((*row).name, name, HOT_MANIFEST_MAX_NAME - 1);
    (*row).name[HOT_MANIFEST_MAX_NAME - 1] = '\0';
    atomic_store(&(*row).ptr, NULL);
    atomic_store(&(*row).fallback_ptr, NULL);
    return (int)idx;
}

// Set a row's function pointer atomically.
void HotTrampolineTable_set(HotTrampolineTable *self, int idx, void *ptr) {
    if (!self) return;
    if (idx < 0 || idx >= (int)atomic_load(&(*self).count)) return;
    HotTrampoline *row = &(*self).rows[idx];
    void *old = atomic_load(&(*row).ptr);
    if (old && old != ptr) {
        atomic_store(&(*row).fallback_ptr, old);
    }
    atomic_store(&(*row).ptr, ptr);
}

// Find a row by name. Returns -1 if not found.
int HotTrampolineTable_find(HotTrampolineTable *self, const char *name) {
    if (!self || !name) return -1;
    uint32_t count = atomic_load(&(*self).count);
    for (uint32_t i = 0; i < count; i++) {
        HotTrampoline *row = &(*self).rows[i];
        if (strcmp((*row).name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

// GETTERS
// Get a row's current function pointer, falling back to prior generation on mid-swap NULL.
void *HotTrampolineTable_get(HotTrampolineTable *self, int idx) {
    if (!self) return NULL;
    if (idx < 0 || idx >= (int)atomic_load(&(*self).count)) return NULL;
    HotTrampoline *row = &(*self).rows[idx];
    void *ptr = atomic_load(&(*row).ptr);
    if (!ptr) {
        // Poll briefly in case atomic swap is in-flight
        for (int retry = 0; retry < 4 && !ptr; retry++) {
            #if defined(__aarch64__)
            __asm__ volatile("yield");
            #endif
            ptr = atomic_load(&(*row).ptr);
        }
        // Fall back to prior generation rather than returning NULL and crashing caller
        if (!ptr) {
            ptr = atomic_load(&(*row).fallback_ptr);
        }
    }
    return ptr;
}
