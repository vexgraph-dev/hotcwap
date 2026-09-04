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

#endif
