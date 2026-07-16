// §3-E item 12 / A164: the pure recompute-decision behind GET /api/songs'
// "parseOk" flag — see vialucis/song_parse_cache.h for the design (cached
// per (name,size), invalidated on a size change, no file IO here).

#include <unity.h>

#include "vialucis/song_parse_cache.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static void test_unseen_name_needs_recompute() {
    SongParseCache c;
    TEST_ASSERT_TRUE(c.needsRecompute("a.mid", 100));
}

static void test_get_returns_false_for_unseen_name() {
    SongParseCache c;
    TEST_ASSERT_FALSE(c.get("a.mid"));
}

static void test_matching_size_after_set_does_not_need_recompute() {
    SongParseCache c;
    c.set("a.mid", 100, true);
    TEST_ASSERT_FALSE(c.needsRecompute("a.mid", 100));
    TEST_ASSERT_TRUE(c.get("a.mid"));
}

static void test_cached_parse_failure_is_retained_too() {
    SongParseCache c;
    c.set("bad.mid", 50, false);
    TEST_ASSERT_FALSE(c.needsRecompute("bad.mid", 50));
    TEST_ASSERT_FALSE(c.get("bad.mid"));
}

static void test_size_change_needs_recompute_again() {
    SongParseCache c;
    c.set("a.mid", 100, true);
    // Same name, different size (a re-upload/overwrite landed new bytes) —
    // the cached parseOk from the OLD content can no longer be trusted.
    TEST_ASSERT_TRUE(c.needsRecompute("a.mid", 101));
}

static void test_set_after_size_change_overwrites_stale_entry() {
    SongParseCache c;
    c.set("a.mid", 100, true);
    c.set("a.mid", 101, false);  // overwrite-then-corrupt scenario
    TEST_ASSERT_FALSE(c.needsRecompute("a.mid", 101));
    TEST_ASSERT_FALSE(c.get("a.mid"));
    // The OLD size is now a mismatch too (only one entry per name).
    TEST_ASSERT_TRUE(c.needsRecompute("a.mid", 100));
}

static void test_prune_drops_missing_names_keeps_present_ones() {
    SongParseCache c;
    c.set("keep.mid", 10, true);
    c.set("gone.mid", 20, true);
    TEST_ASSERT_EQUAL_size_t(2, c.size());

    c.prune({"keep.mid"});
    TEST_ASSERT_EQUAL_size_t(1, c.size());
    TEST_ASSERT_FALSE(c.needsRecompute("keep.mid", 10));  // survived
    TEST_ASSERT_TRUE(c.needsRecompute("gone.mid", 20));   // forgotten
}

static void test_prune_forces_recompute_for_a_readded_same_name_song() {
    // Delete "x.mid" then upload a DIFFERENT "x.mid" later, still 20 bytes
    // (same size as the old one) — prune must have actually forgotten it,
    // or a stale parseOk could wrongly survive a same-size coincidence.
    SongParseCache c;
    c.set("x.mid", 20, false);
    c.prune({});  // x.mid no longer exists
    TEST_ASSERT_TRUE(c.needsRecompute("x.mid", 20));
}

// A183: has() separates "checked" from "good" so the budgeted warm-up can
// report an unchecked file as UNKNOWN (no parseOk on the wire) instead of
// falsely badging it bad via get()'s safe-default false.
void test_has_tracks_known_separately_from_good() {
    SongParseCache c;
    TEST_ASSERT_FALSE(c.has("x.mid"));       // never seen: unknown
    c.set("x.mid", 20, false);
    TEST_ASSERT_TRUE(c.has("x.mid"));        // seen and BAD: known
    TEST_ASSERT_FALSE(c.get("x.mid"));
    c.set("y.mid", 9, true);
    TEST_ASSERT_TRUE(c.has("y.mid"));        // seen and good: known
    c.prune({"y.mid"});
    TEST_ASSERT_FALSE(c.has("x.mid"));       // pruned: unknown again
}

// A185: the failure reason is stored additively so the songs list can say
// "memory" vs "corrupt". The bool-only setter defaults a failure to Corrupt
// (pre-A185 behavior); the reason-aware setter records Memory.
void test_fail_reason_defaults_and_records() {
    SongParseCache c;
    // Never seen: no reason.
    TEST_ASSERT_TRUE(c.failReason("x.mid") == ParseFail::None);
    // Good parse: reason None even though we set true.
    c.set("ok.mid", 10, true);
    TEST_ASSERT_TRUE(c.failReason("ok.mid") == ParseFail::None);
    // Bool-only failure -> Corrupt (back-compat).
    c.set("bad.mid", 20, false);
    TEST_ASSERT_FALSE(c.get("bad.mid"));
    TEST_ASSERT_TRUE(c.failReason("bad.mid") == ParseFail::Corrupt);
    // Reason-aware failure -> Memory.
    c.set("big.mid", 30, false, ParseFail::Memory);
    TEST_ASSERT_FALSE(c.get("big.mid"));
    TEST_ASSERT_TRUE(c.failReason("big.mid") == ParseFail::Memory);
    // A parseOk=true always normalizes the reason to None, even if a stray
    // ParseFail is passed.
    c.set("good2.mid", 40, true, ParseFail::Memory);
    TEST_ASSERT_TRUE(c.failReason("good2.mid") == ParseFail::None);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_has_tracks_known_separately_from_good);
    RUN_TEST(test_fail_reason_defaults_and_records);
    RUN_TEST(test_unseen_name_needs_recompute);
    RUN_TEST(test_get_returns_false_for_unseen_name);
    RUN_TEST(test_matching_size_after_set_does_not_need_recompute);
    RUN_TEST(test_cached_parse_failure_is_retained_too);
    RUN_TEST(test_size_change_needs_recompute_again);
    RUN_TEST(test_set_after_size_change_overwrites_stale_entry);
    RUN_TEST(test_prune_drops_missing_names_keeps_present_ones);
    RUN_TEST(test_prune_forces_recompute_for_a_readded_same_name_song);
    return UNITY_END();
}
