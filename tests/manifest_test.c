#include "annotation/overview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hot/manifest.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ManifestTest (hot/../tests/manifest_test.c)
 * LEVEL: L3 — Module Code (headless verification harness)
 * ============================================================================
 * Headless suite for the module manifest contract: order-tolerant parsing
 * with optional parent/size keys, fail-closed swap compatibility (value,
 * parent chain, struct size, one-sided refusal), and the FNV-1a contract
 * digest (stable under row reorder, sensitive to any field change).
 *
 * STRUCT FIELDS: none — procedural test harness.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - main(void)
 * ============================================================================
 */

static int g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { printf("[manifest_test] PASS %s\n", name); } \
    else { printf("[manifest_test] FAIL %s\n", name); g_failures++; } \
} while (0)

static bool parse_ok(const char *json, HotManifest *out) {
    return HotManifest_parse(json, strlen(json), out);
}

int main(void) {
    printf("=== Running Manifest Test Suite ===\n");

    // §1 Legacy wire form: no parent/size keys stay unstated.
    {
        HotManifest m;
        const char *json = "{\"name\":\"prim\",\"version\":\"1.0\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5}],\"exports\":[],\"dependencies\":[]}";
        CHECK("legacy parse", parse_ok(json, &m));
        CHECK("legacy count", m.type_id_count == 1);
        CHECK("legacy value", m.type_ids[0].value == 5);
        CHECK("legacy parent unstated", m.type_ids[0].has_parent == false);
        CHECK("legacy size unstated", m.type_ids[0].size == 0);
    }

    // §2 New keys in any order, hex value.
    {
        HotManifest m;
        const char *json = "{\"name\":\"prim\",\"version\":\"1.0\",\"type_ids\":[{\"size\":48,\"name\":\"ID_B\",\"parent\":1,\"value\":0x10}],\"exports\":[],\"dependencies\":[]}";
        CHECK("reordered parse", parse_ok(json, &m));
        CHECK("reordered value hex", m.type_ids[0].value == 16);
        CHECK("reordered parent", m.type_ids[0].has_parent == true && m.type_ids[0].parent == 1);
        CHECK("reordered size", m.type_ids[0].size == 48);
    }

    // §3 Value identity: renumber/remove refuse, growth allows.
    {
        HotManifest old_m;
        HotManifest new_m;
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5},{\"name\":\"ID_B\",\"value\":6}],\"exports\":[],\"dependencies\":[]}", &old_m);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5},{\"name\":\"ID_B\",\"value\":6},{\"name\":\"ID_C\",\"value\":7}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("growth compatible", HotManifest_compatible(&old_m, &new_m) == true);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5},{\"name\":\"ID_B\",\"value\":99}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("renumber refuses", HotManifest_compatible(&old_m, &new_m) == false);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("removal refuses", HotManifest_compatible(&old_m, &new_m) == false);
    }

    // §4 Parent chains and sizes: match passes, mismatch/one-sided refuses.
    {
        HotManifest old_m;
        HotManifest new_m;
        const char *base = "{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_L\",\"value\":5,\"parent\":1,\"size\":64}],\"exports\":[],\"dependencies\":[]}";
        parse_ok(base, &old_m);
        parse_ok(base, &new_m);
        CHECK("stated match compatible", HotManifest_compatible(&old_m, &new_m) == true);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_L\",\"value\":5,\"parent\":2,\"size\":64}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("reparent refuses", HotManifest_compatible(&old_m, &new_m) == false);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_L\",\"value\":5,\"parent\":1,\"size\":72}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("resize refuses", HotManifest_compatible(&old_m, &new_m) == false);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_L\",\"value\":5,\"size\":64}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("one-sided parent refuses", HotManifest_compatible(&old_m, &new_m) == false);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_L\",\"value\":5,\"parent\":1}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("one-sided size refuses", HotManifest_compatible(&old_m, &new_m) == false);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_L\",\"value\":5}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("unstated-vs-stated refuses", HotManifest_compatible(&old_m, &new_m) == false);
    }

    // §5 Legacy-vs-legacy keeps the old pass-through.
    {
        HotManifest old_m;
        HotManifest new_m;
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5}],\"exports\":[],\"dependencies\":[]}", &old_m);
        parse_ok("{\"name\":\"m\",\"version\":\"2\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5}],\"exports\":[],\"dependencies\":[]}", &new_m);
        CHECK("legacy pair compatible", HotManifest_compatible(&old_m, &new_m) == true);
    }

    // §6 Digest: stable under reorder, sensitive to any field.
    {
        HotManifest a;
        HotManifest b;
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5,\"parent\":1,\"size\":64},{\"name\":\"ID_B\",\"value\":6}],\"exports\":[],\"dependencies\":[]}", &a);
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5,\"parent\":1,\"size\":64},{\"name\":\"ID_B\",\"value\":6}],\"exports\":[],\"dependencies\":[]}", &b);
        CHECK("digest equal contracts", HotManifest_digest(&a) == HotManifest_digest(&b));
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_B\",\"value\":6},{\"name\":\"ID_A\",\"value\":5,\"parent\":1,\"size\":64}],\"exports\":[],\"dependencies\":[]}", &b);
        CHECK("digest reorder stable", HotManifest_digest(&a) == HotManifest_digest(&b));
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5,\"parent\":1,\"size\":65},{\"name\":\"ID_B\",\"value\":6}],\"exports\":[],\"dependencies\":[]}", &b);
        CHECK("digest size change", HotManifest_digest(&a) != HotManifest_digest(&b));
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":5,\"parent\":2,\"size\":64},{\"name\":\"ID_B\",\"value\":6}],\"exports\":[],\"dependencies\":[]}", &b);
        CHECK("digest parent change", HotManifest_digest(&a) != HotManifest_digest(&b));
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[{\"name\":\"ID_A\",\"value\":7,\"parent\":1,\"size\":64},{\"name\":\"ID_B\",\"value\":6}],\"exports\":[],\"dependencies\":[]}", &b);
        CHECK("digest value change", HotManifest_digest(&a) != HotManifest_digest(&b));
    }

    // §7 Null safety.
    {
        HotManifest m;
        parse_ok("{\"name\":\"m\",\"version\":\"1\",\"type_ids\":[],\"exports\":[],\"dependencies\":[]}", &m);
        CHECK("compatible null old", HotManifest_compatible(nullptr, &m) == false);
        CHECK("compatible null new", HotManifest_compatible(&m, nullptr) == false);
        CHECK("digest null", HotManifest_digest(nullptr) == 0);
        CHECK("parse null json", HotManifest_parse(nullptr, 10, &m) == false);
        CHECK("parse null out", HotManifest_parse("{}", 2, nullptr) == false);
        CHECK("get missing id", HotManifest_get_type_id(&m, "NOPE") == 0);
    }

    printf("\n=== Manifest Test Summary: %d failures ===\n", g_failures);
    return g_failures > 0 ? 1 : 0;
}
