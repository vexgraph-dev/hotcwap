#ifndef HOT_STAGE_H
#define HOT_STAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// hot/stage.h — Phase-4 staged updates (the console flow, on desktop).
//
// Console truth: no unsigned dlopen, so live code swap is impossible and
// every update is a SIGNED PACKAGE. This unit is the same shape with a
// local trust root: a stage dir holds `hot.manifest` (HotManifest schema),
// `files.sha` (lines: `<8-hex-fnv1a> <filename>`), and payload dylibs.
// Verify checks schema + integrity; promote copies payloads into the
// watch dir via temp+rename; the next Hot_poll loads them — no restart.
//
// The FNV-1a checksum is integrity, not security: it is the exact seam
// where a console build plugs signature verification (same call site,
// stronger primitive). Never mistake one for the other on retail hw.

// FNV-1a 32-bit of a file. Returns false on unreadable file.
bool HotStage_hashFile(const char *path, uint32_t *outHash);

// Verify stage dir: schema parses, every files.sha line matches.
// Details land in errOut when provided (up to errCap).
bool HotStage_verify(const char *stageDir, char *errOut, size_t errCap);

// Verify, then copy payloads into hotDir (temp + rename per file).
// Returns false without touching hotDir when verification fails.
bool HotStage_promote(const char *stageDir, const char *hotDir);

#endif
