#include "hot/stage.h"
#include "hot/manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Stage (hot/stage.c)
 * LEVEL: L4 — Self-Management (verify-then-promote staged-update infra)
 * ============================================================================
 * Phase-4 staged updates: verify-then-promote package flow behind the
 * Hot_poll watch dir. Desktop analog of the console signed-package path.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - HotStage_hashFile(path, outHash)
 *   - HotStage_verify(stageDir, errOut, errCap)
 *   - HotStage_promote(stageDir, hotDir)
 * ============================================================================
 */

bool HotStage_hashFile(const char *path, uint32_t *outHash) {
    if (!path || !outHash)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint32_t h = 2166136261u;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 16777619u;
        }
    }
    fclose(f);
    (*outHash) = h;
    return true;
}

static bool read_manifest(const char *stageDir, HotManifest *out, char *err, size_t errCap) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/hot.manifest", stageDir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err)
            snprintf(err, errCap, "missing hot.manifest");
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 65536) {
        fclose(f);
        if (err)
            snprintf(err, errCap, "bad hot.manifest size");
        return false;
    }
    char *json = (char*) malloc((size_t) len);
    if (!json) {
        fclose(f);
        return false;
    }
    bool ok = fread(json, 1, (size_t) len, f) == (size_t) len
        && HotManifest_parse(json, (size_t) len, out);
    free(json);
    fclose(f);
    if (!ok && err)
        snprintf(err, errCap, "hot.manifest schema parse failed");
    return ok;
}

bool HotStage_verify(const char *stageDir, char *errOut, size_t errCap) {
    if (!stageDir)
        return false;
    HotManifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    if (!read_manifest(stageDir, &manifest, errOut, errCap))
        return false;

    char path[1024];
    snprintf(path, sizeof(path), "%s/files.sha", stageDir);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errOut)
            snprintf(errOut, errCap, "missing files.sha");
        return false;
    }
    char line[1024];
    uint32_t checked = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0')
            continue;
        uint32_t want = 0;
        char name[512];
        if (sscanf(line, "%x %511s", &want, name) != 2) {
            ok = false;
            if (errOut)
                snprintf(errOut, errCap, "malformed files.sha line");
            break;
        }
        snprintf(path, sizeof(path), "%s/%s", stageDir, name);
        uint32_t got = 0;
        if (!HotStage_hashFile(path, &got) || got != want) {
            ok = false;
            if (errOut)
                snprintf(errOut, errCap, "integrity fail: %s", name);
            break;
        }
        checked++;
    }
    fclose(f);
    if (ok && checked == 0) {
        if (errOut)
            snprintf(errOut, errCap, "files.sha empty");
        return false;
    }
    return ok;
}

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in)
        return false;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    uint8_t buf[4096];
    size_t n = 0;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

bool HotStage_promote(const char *stageDir, const char *hotDir) {
    char err[256];
    if (!HotStage_verify(stageDir, err, sizeof(err))) {
        fprintf(stderr, "[stage] verify failed: %s\n", err);
        return false;
    }
    char list[1024];
    snprintf(list, sizeof(list), "%s/files.sha", stageDir);
    FILE *f = fopen(list, "r");
    if (!f)
        return false;
    char line[1024];
    bool ok = true;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0')
            continue;
        uint32_t want = 0;
        char name[512];
        if (sscanf(line, "%x %511s", &want, name) != 2) {
            ok = false;
            break;
        }
        // Stage payloads are `<name>.so`; the watch dir takes the same name.
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;
        char src[1024];
        char tmp[1024];
        char dst[1024];
        snprintf(src, sizeof(src), "%s/%s", stageDir, name);
        snprintf(tmp, sizeof(tmp), "%s/.%s.promote", hotDir, base);
        snprintf(dst, sizeof(dst), "%s/%s", hotDir, base);
        if (!copy_file(src, tmp) || rename(tmp, dst) != 0) {
            unlink(tmp);
            ok = false;
            break;
        }
    }
    fclose(f);
    return ok;
}
