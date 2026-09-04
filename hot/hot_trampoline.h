#ifndef HOT_HOT_TRAMPOLINE_H
#define HOT_HOT_TRAMPOLINE_H

#include <stdatomic.h>
#include <stdint.h>

#include "hot/manifest.h"

// hot/hot_trampoline.h — Per-instance atomic function-pointer table.
//
// Each HotModule owns one HotTrampolineTable, so two loader instances share
// the framework code but never collide: same symbol name in two apps resolves
// through two different tables. On reload the new dylib's address is stored
// into ptr while fallback_ptr keeps the prior generation alive, so in-flight
// calls landing mid-swap never jump to NULL.

typedef struct HotTrampoline {
    _Atomic(void*) ptr;             // current generation target
    _Atomic(void*) fallback_ptr;    // prior generation (mid-swap cover)
    char name[HOT_MANIFEST_MAX_NAME]; // export symbol name
} HotTrampoline;

#define HOT_MAX_TRAMPOLINES 1024

typedef struct HotTrampolineTable {
    HotTrampoline rows[HOT_MAX_TRAMPOLINES]; // one row per export
    _Atomic uint32_t count;                  // used rows (cross-thread readers)
} HotTrampolineTable;

// Register a row for a function. Returns the row index.
int HotTrampolineTable_register(HotTrampolineTable *self, const char *name);

// Current function pointer, falling back to the prior generation on mid-swap NULL.
void *HotTrampolineTable_get(HotTrampolineTable *self, int idx);

// Atomically swap a row's target, stashing the old pointer as fallback.
void HotTrampolineTable_set(HotTrampolineTable *self, int idx, void *ptr);

// Find a row by name. Returns -1 if not found.
int HotTrampolineTable_find(HotTrampolineTable *self, const char *name);

#endif
