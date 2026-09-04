#ifndef HOT_MANIFEST_H
#define HOT_MANIFEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// hot/manifest.h — Module manifest format.
//
// Each module (dylib) exports a JSON manifest that describes:
//   - name: module name (e.g., "primitive", "vulkan", "buffers")
//   - version: semantic version string (e.g., "1.2.3")
//   - type_ids: array of {name, value} pairs — the frozen ABI contract
//   - exports: array of function names exported by this module
//   - dependencies: array of module names this module depends on
//
// On reload, the hotloader verifies:
//   1. All type_ids match the previous version (ABI stability)
//   2. All dependencies are loaded and compatible
//   3. The new dylib's init function succeeds
//
// If verification passes, the function pointer table is atomically swapped.

#define HOT_MANIFEST_MAX_TYPE_IDS 256
#define HOT_MANIFEST_MAX_EXPORTS 128
#define HOT_MANIFEST_MAX_DEPENDENCIES 16
#define HOT_MANIFEST_MAX_NAME 64
#define HOT_MANIFEST_MAX_VERSION 16

// A single type ID entry in the manifest.
typedef struct {
    char name[HOT_MANIFEST_MAX_NAME];   // e.g., "ID_INT"
    uint32_t value;                      // e.g., 0x00000001
} HotTypeId;

// A single function export entry.
typedef struct {
    char name[HOT_MANIFEST_MAX_NAME];   // e.g., "Memory_alloc"
} HotExport;

// A single dependency entry.
typedef struct {
    char name[HOT_MANIFEST_MAX_NAME];   // e.g., "primitive"
} HotDependency;

// The full manifest for a module.
typedef struct {
    char name[HOT_MANIFEST_MAX_NAME];
    char version[HOT_MANIFEST_MAX_VERSION];
    
    uint32_t type_id_count;
    HotTypeId type_ids[HOT_MANIFEST_MAX_TYPE_IDS];
    
    uint32_t export_count;
    HotExport exports[HOT_MANIFEST_MAX_EXPORTS];
    
    uint32_t dependency_count;
    HotDependency dependencies[HOT_MANIFEST_MAX_DEPENDENCIES];
} HotManifest;

// Parse a manifest from JSON string.
// Returns true on success, false on parse error.
bool HotManifest_parse(const char *json, size_t len, HotManifest *out);

// Verify that two manifests are ABI-compatible.
// Returns true if all type_ids match (same name → same value).
bool HotManifest_compatible(const HotManifest *old_manifest, const HotManifest *new_manifest);

// Get a type ID by name. Returns 0 if not found.
uint32_t HotManifest_get_type_id(const HotManifest *manifest, const char *name);

#endif
