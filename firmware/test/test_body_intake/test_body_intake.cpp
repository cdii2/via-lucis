// Chunk-boundary math for the REST body intake (R6). Pure decisions only —
// the device-side sinks (buffer-then-parse, stream-to-store) act on these.

#include <unity.h>

#include "vialucis/body_intake.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

void test_single_chunk_body_is_first_and_last() {
    ChunkPlan p = planChunk(0, 100, 100, 4096);
    TEST_ASSERT_FALSE(p.tooLarge);
    TEST_ASSERT_TRUE(p.first);
    TEST_ASSERT_TRUE(p.last);
}

void test_multi_chunk_boundaries() {
    ChunkPlan first = planChunk(0, 1024, 3000, 4096);
    TEST_ASSERT_TRUE(first.first);
    TEST_ASSERT_FALSE(first.last);

    ChunkPlan middle = planChunk(1024, 1024, 3000, 4096);
    TEST_ASSERT_FALSE(middle.first);
    TEST_ASSERT_FALSE(middle.last);

    ChunkPlan last = planChunk(2048, 952, 3000, 4096);
    TEST_ASSERT_FALSE(last.first);
    TEST_ASSERT_TRUE(last.last);
}

void test_oversize_total_rejects_from_the_first_chunk() {
    ChunkPlan p = planChunk(0, 1024, 5000, 4096);
    TEST_ASSERT_TRUE(p.tooLarge);
    // ...and keeps rejecting on later chunks of the same body.
    TEST_ASSERT_TRUE(planChunk(1024, 1024, 5000, 4096).tooLarge);
}

void test_total_exactly_at_cap_is_accepted() {
    TEST_ASSERT_FALSE(planChunk(0, 4096, 4096, 4096).tooLarge);
}

void test_degenerate_empty_body_is_first_and_last() {
    ChunkPlan p = planChunk(0, 0, 0, 4096);
    TEST_ASSERT_TRUE(p.first);
    TEST_ASSERT_TRUE(p.last);
    TEST_ASSERT_FALSE(p.tooLarge);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_chunk_body_is_first_and_last);
    RUN_TEST(test_multi_chunk_boundaries);
    RUN_TEST(test_oversize_total_rejects_from_the_first_chunk);
    RUN_TEST(test_total_exactly_at_cap_is_accepted);
    RUN_TEST(test_degenerate_empty_body_is_first_and_last);
    return UNITY_END();
}
