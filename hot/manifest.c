#include "hot/manifest.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// hot/manifest.c — Minimal JSON parser for module manifests.
//
// This is a deliberately simple parser — no external dependencies.
// It handles the specific manifest format we need:
//   {"name": "...", "version": "...", "type_ids": [...], "exports": [...], "dependencies": [...]}

// Skip whitespace
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// Parse a JSON string (simplified — no escape handling for now)
static const char *parse_string(const char *p, char *out, size_t out_size) {
    if (*p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

// Parse a JSON number (uint32)
static const char *parse_uint(const char *p, uint32_t *out) {
    uint32_t val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    *out = val;
    return p;
}

// Parse a hex number (0x...)
static const char *parse_hex(const char *p, uint32_t *out) {
    uint32_t val = 0;
    while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
        uint8_t nibble;
        if (*p >= '0' && *p <= '9') nibble = *p - '0';
        else if (*p >= 'a' && *p <= 'f') nibble = *p - 'a' + 10;
        else nibble = *p - 'A' + 10;
        val = (val << 4) | nibble;
        p++;
    }
    *out = val;
    return p;
}

// Parse a JSON object value after the key
static const char *parse_value(const char *p, char *str_out, size_t str_size, uint32_t *num_out, bool *is_hex) {
    p = skip_ws(p);
    if (*p == '"') {
        p = parse_string(p, str_out, str_size);
        *is_hex = false;
    } else if (p[0] == '0' && p[1] == 'x') {
        p += 2;
        p = parse_hex(p, num_out);
        *is_hex = true;
    } else {
        p = parse_uint(p, num_out);
        *is_hex = false;
    }
    return p;
}

bool HotManifest_parse(const char *json, size_t len, HotManifest *out) {
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));
    
    const char *p = json;
    const char *end = json + len;
    
    // Expect opening brace
    p = skip_ws(p);
    if (*p != '{') return false;
    p++;
    
    while (p < end) {
        p = skip_ws(p);
        if (*p == '}') break; // End of object
        
        // Parse key
        char key[64];
        p = parse_string(p, key, sizeof(key));
        if (!p) return false;
        
        // Expect colon
        p = skip_ws(p);
        if (*p != ':') return false;
        p++;
        
        // Parse value
        p = skip_ws(p);
        
        if (strcmp(key, "name") == 0) {
            char value[64];
            bool is_hex;
            p = parse_value(p, value, sizeof(value), NULL, &is_hex);
            strncpy((*out).name, value, HOT_MANIFEST_MAX_NAME - 1);
        } else if (strcmp(key, "version") == 0) {
            char value[64];
            bool is_hex;
            p = parse_value(p, value, sizeof(value), NULL, &is_hex);
            strncpy((*out).version, value, HOT_MANIFEST_MAX_VERSION - 1);
        } else if (strcmp(key, "type_ids") == 0) {
            // Expect array
            if (*p != '[') return false;
            p++;
            while (p < end) {
                p = skip_ws(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                
                // Expect object: {"name": "...", "value": 0x...}
                if (*p != '{') return false;
                p++;
                
                HotTypeId tid;
                memset(&tid, 0, sizeof(tid));
                
                // Parse "name"
                p = skip_ws(p);
                char tkey[32];
                p = parse_string(p, tkey, sizeof(tkey));
                p = skip_ws(p);
                if (*p != ':') return false;
                p++;
                p = skip_ws(p);
                char tname[64];
                bool is_hex;
                p = parse_value(p, tname, sizeof(tname), NULL, &is_hex);
                strncpy(tid.name, tname, HOT_MANIFEST_MAX_NAME - 1);
                
                // Expect comma
                p = skip_ws(p);
                if (*p != ',') return false;
                p++;
                
                // Parse "value"
                p = skip_ws(p);
                p = parse_string(p, tkey, sizeof(tkey));
                p = skip_ws(p);
                if (*p != ':') return false;
                p++;
                p = skip_ws(p);
                uint32_t tval;
                p = parse_value(p, NULL, 0, &tval, &is_hex);
                tid.value = tval;
                
                // Expect closing brace
                p = skip_ws(p);
                if (*p != '}') return false;
                p++;
                
                if ((*out).type_id_count < HOT_MANIFEST_MAX_TYPE_IDS) {
                    (*out).type_ids[(*out).type_id_count++] = tid;
                }
            }
        } else if (strcmp(key, "exports") == 0) {
            if (*p != '[') return false;
            p++;
            while (p < end) {
                p = skip_ws(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                
                char value[64];
                bool is_hex;
                p = parse_value(p, value, sizeof(value), NULL, &is_hex);
                
                if ((*out).export_count < HOT_MANIFEST_MAX_EXPORTS) {
                    strncpy((*out).exports[(*out).export_count].name, value, HOT_MANIFEST_MAX_NAME - 1);
                    (*out).export_count++;
                }
            }
        } else if (strcmp(key, "dependencies") == 0) {
            if (*p != '[') return false;
            p++;
            while (p < end) {
                p = skip_ws(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                
                char value[64];
                bool is_hex;
                p = parse_value(p, value, sizeof(value), NULL, &is_hex);
                
                if ((*out).dependency_count < HOT_MANIFEST_MAX_DEPENDENCIES) {
                    strncpy((*out).dependencies[(*out).dependency_count].name, value, HOT_MANIFEST_MAX_NAME - 1);
                    (*out).dependency_count++;
                }
            }
        } else {
            // Unknown key — skip value
            if (*p == '"') {
                char dummy[64];
                bool is_hex;
                p = parse_value(p, dummy, sizeof(dummy), NULL, &is_hex);
            } else {
                uint32_t dummy;
                bool is_hex;
                p = parse_value(p, NULL, 0, &dummy, &is_hex);
            }
        }
        
        // Expect comma or closing brace
        p = skip_ws(p);
        if (*p == ',') p++;
        else if (*p == '}') break;
    }
    
    return true;
}

bool HotManifest_compatible(const HotManifest *old_manifest, const HotManifest *new_manifest) {
    if (!old_manifest || !new_manifest) return false;
    
    // Check that all type_ids in the old manifest exist in the new one with the same value
    for (uint32_t i = 0; i < (*old_manifest).type_id_count; i++) {
        uint32_t new_val = HotManifest_get_type_id(new_manifest, (*old_manifest).type_ids[i].name);
        if (new_val != (*old_manifest).type_ids[i].value) {
            return false;
        }
    }
    
    return true;
}

uint32_t HotManifest_get_type_id(const HotManifest *manifest, const char *name) {
    if (!manifest || !name) return 0;
    
    for (uint32_t i = 0; i < (*manifest).type_id_count; i++) {
        if (strcmp((*manifest).type_ids[i].name, name) == 0) {
            return (*manifest).type_ids[i].value;
        }
    }
    
    return 0;
}
