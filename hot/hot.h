#ifndef HOT_HOT_H
#define HOT_HOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// hot/hot.h — Hotloading system for anti.
//
// Each directory in the engine is a dynamically-loaded module (.dylib/.so).
// Type IDs are frozen ABI contracts — they never change across reloads.
// The hotloader watches for new dylibs, verifies ABI compatibility,
// and atomically swaps function pointer tables (trampolines).
//
// The window/AppKit side is owned by the OS, not the engine. When a dylib
// is reloaded, the NSWindow/NSView/CAMetalLayer persist — only the Vulkan
// swapchain and GPU objects are recreated.

typedef enum {
    HOT_OK = 0,
    HOT_ERROR_FILE_NOT_FOUND,
    HOT_ERROR_DLOPEN_FAILED,
    HOT_ERROR_DLSYM_FAILED,
    HOT_ERROR_ABI_MISMATCH,
    HOT_ERROR_VERSION_MISMATCH,
    HOT_ERROR_INIT_FAILED,
    HOT_ERROR_OUT_OF_MEMORY,
} HotResult;

// Module handle — opaque
typedef struct HotModule HotModule;

// Initialize the hotloader. hot_dir is the directory to watch (e.g., "hot/").
// Returns NULL on failure.
HotModule *Hot_init(const char *hot_dir);

// Shutdown the hotloader and unload all modules.
void HotShutdown(HotModule *hot);

// Poll for updates. Call once per frame from the main loop.
// Returns HOT_OK if nothing changed, HOT_OK + loaded_count > 0 if modules were reloaded.
HotResult Hot_poll(HotModule *hot, uint32_t *loaded_count);

// Get the current API for a module by name.
// Returns NULL if the module is not loaded.
// The returned pointer is stable until the next reload.
const void *Hot_get_api(HotModule *hot, const char *module_name);

// Get a function pointer by name.
// Returns NULL if the symbol is not found in any loaded module.
// The returned pointer is stable until the next reload of that module.
typedef void (*HotFn)(void);
HotFn Hot_get_symbol(HotModule *hot, const char *name);

// Get the last error string (for diagnostics).
const char *Hot_last_error(HotModule *hot);

// Phase-1 lifecycle: graceful per-module teardown + state handoff.
// Shutdown calls the module's Hot_shutdown_module (if exported) on the
// currently loaded handle. Save/Restore move an opaque state blob across
// a swap: Hot_poll saves from the old handle before dlopen and restores
// into the new handle after Hot_init_module. Modules without Hot_save /
// Hot_restore simply skip the handoff. Returns false when the module is
// unknown, unloaded, or the symbol is missing / buffer too small.
void Hot_shutdown_module(HotModule *hot, const char *module_name);
bool Hot_save_module(HotModule *hot, const char *module_name, void *buf, size_t cap, size_t *outLen);
bool Hot_restore_module(HotModule *hot, const char *module_name, const void *buf, size_t len);

// Phase-2 migration: translate a state blob saved by oldVersion into the
// current module's schema. Calls the module's Hot_migrate export; false
// when the module is unknown or exports no migrator.
bool Hot_migrate_module(HotModule *hot, const char *module_name, const char *oldVersion,
                        const void *oldBuf, size_t oldLen, void *newBuf, size_t newCap, size_t *outLen);

#endif
