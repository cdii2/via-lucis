// Storage-budget math (A2 upload precheck) + FS-health transitions (A3,
// ruling §6-2). Pure decisions only — the device side owns LittleFS.

#include <unity.h>

#include "vialucis/storage_budget.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

// --- block rounding -----------------------------------------------------

void test_round_up_to_block() {
    TEST_ASSERT_EQUAL_UINT32(0, roundUpToBlock(0));
    TEST_ASSERT_EQUAL_UINT32(4096, roundUpToBlock(1));
    TEST_ASSERT_EQUAL_UINT32(4096, roundUpToBlock(4096));
    TEST_ASSERT_EQUAL_UINT32(8192, roundUpToBlock(4097));
    TEST_ASSERT_EQUAL_UINT32(8192, roundUpToBlock(8000));
}

// --- upload free-space precheck -----------------------------------------

void test_upload_fits_leaves_reserve() {
    // 4 KB file rounds to one block; +32 KB reserve = 36 KB needed.
    TEST_ASSERT_TRUE(uploadFits(4096, 40 * 1024));
    TEST_ASSERT_FALSE(uploadFits(4096, 36 * 1024 - 1));
    TEST_ASSERT_TRUE(uploadFits(4096, 36 * 1024));
}

void test_upload_rounds_partial_block_before_checking() {
    // 1 byte still costs a whole 4 KB block: needs 4 KB + 32 KB = 36 KB.
    TEST_ASSERT_FALSE(uploadFits(1, 35 * 1024));
    TEST_ASSERT_TRUE(uploadFits(1, 36 * 1024));
}

void test_upload_hard_full_is_refused() {
    // The incident: ~free but not a reserve's worth -> refuse, don't wedge.
    TEST_ASSERT_FALSE(uploadFits(140 * 1024, 20 * 1024));
    TEST_ASSERT_FALSE(uploadFits(1, 0));
}

void test_upload_reserve_and_block_are_tunable() {
    TEST_ASSERT_TRUE(uploadFits(1000, 1000, /*reserve=*/0, /*block=*/0));
    TEST_ASSERT_FALSE(uploadFits(1001, 1000, /*reserve=*/0, /*block=*/0));
}

// --- show net-delta quota (edit -> re-save must not double-count) --------

void test_show_quota_new_name_counts_full() {
    // No same-name show: full incoming counts.
    TEST_ASSERT_TRUE(showQuotaFits(300 * 1024, 0, 64 * 1024, 384 * 1024));
    TEST_ASSERT_FALSE(showQuotaFits(340 * 1024, 0, 64 * 1024, 384 * 1024));
}

void test_show_quota_overwrite_frees_old_bytes() {
    // Total already includes a 60 KB same-name show; re-saving 64 KB must
    // only count the delta, not 60+64 on top of the total.
    TEST_ASSERT_TRUE(
        showQuotaFits(380 * 1024, 60 * 1024, 64 * 1024, 384 * 1024));
    // Same numbers WITHOUT the net-delta would be 380+64 > 384 -> a false
    // rejection; the credit is what unblocks the edit loop.
}

void test_show_quota_clamps_when_existing_understated() {
    // Defensive: existingSameName larger than total never underflows.
    TEST_ASSERT_TRUE(showQuotaFits(10, 999, 64 * 1024, 384 * 1024));
}

void test_show_count_cap() {
    // At the cap, a new name is refused but an overwrite is allowed.
    TEST_ASSERT_FALSE(showCountOk(16, /*nameExists=*/false, 16));
    TEST_ASSERT_TRUE(showCountOk(16, /*nameExists=*/true, 16));
    TEST_ASSERT_TRUE(showCountOk(15, /*nameExists=*/false, 16));
}

// --- FS health transitions ----------------------------------------------

void test_fs_health_classification() {
    TEST_ASSERT_TRUE(classifyFsHealth(true, true) == FsHealth::Mounted);
    TEST_ASSERT_TRUE(classifyFsHealth(false, true) == FsHealth::MountFailed);
    TEST_ASSERT_TRUE(classifyFsHealth(false, false) == FsHealth::MountFailed);
    // Mounted but can't create a file = the wedge.
    TEST_ASSERT_TRUE(classifyFsHealth(true, false) == FsHealth::Wedged);
}

void test_fs_health_field_is_ok_only_when_mounted() {
    TEST_ASSERT_EQUAL_STRING("ok", fsHealthField(FsHealth::Mounted));
    TEST_ASSERT_EQUAL_STRING("error", fsHealthField(FsHealth::MountFailed));
    TEST_ASSERT_EQUAL_STRING("error", fsHealthField(FsHealth::Wedged));
}

// A185: the device derives the parser's note budget from the largest
// allocatable heap block, minus the parser's fixed overhead (the ~18 KB
// NoteTracker) and a working margin. The old whole-file read-fits / factor-4
// parse-work guards are retired — the parser now measures its exact need and
// refuses (TooBigForMemory) itself, so nothing here sizes the whole file.
void test_parse_note_budget_subtracts_overhead_and_margin() {
    // Typical fresh-boot heap: budget = maxAlloc - overhead - margin.
    TEST_ASSERT_EQUAL_size_t(
        100 * 1024 - 18 * 1024 - kParseHeapMarginBytes,
        parseNoteBudget(100 * 1024, 18 * 1024));
    // Custom margin honored.
    TEST_ASSERT_EQUAL_size_t(50 * 1024 - 18 * 1024 - 4 * 1024,
                             parseNoteBudget(50 * 1024, 18 * 1024, 4 * 1024));
    // Heap too fragmented to even cover overhead+margin -> budget 0 (every
    // non-empty song is then refused, never gambled on).
    TEST_ASSERT_EQUAL_size_t(0, parseNoteBudget(20 * 1024, 18 * 1024));
    TEST_ASSERT_EQUAL_size_t(0, parseNoteBudget(0, 18 * 1024));
    // Exactly at the boundary (maxAlloc == overhead+margin) -> 0, one over -> 1.
    TEST_ASSERT_EQUAL_size_t(
        0, parseNoteBudget(18 * 1024 + kParseHeapMarginBytes, 18 * 1024));
    TEST_ASSERT_EQUAL_size_t(
        1, parseNoteBudget(18 * 1024 + kParseHeapMarginBytes + 1, 18 * 1024));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_round_up_to_block);
    RUN_TEST(test_upload_fits_leaves_reserve);
    RUN_TEST(test_upload_rounds_partial_block_before_checking);
    RUN_TEST(test_upload_hard_full_is_refused);
    RUN_TEST(test_upload_reserve_and_block_are_tunable);
    RUN_TEST(test_show_quota_new_name_counts_full);
    RUN_TEST(test_show_quota_overwrite_frees_old_bytes);
    RUN_TEST(test_show_quota_clamps_when_existing_understated);
    RUN_TEST(test_show_count_cap);
    RUN_TEST(test_fs_health_classification);
    RUN_TEST(test_fs_health_field_is_ok_only_when_mounted);
    RUN_TEST(test_parse_note_budget_subtracts_overhead_and_margin);
    return UNITY_END();
}
