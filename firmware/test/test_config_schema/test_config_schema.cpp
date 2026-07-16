// Persisted-doc schema versioning (B4): stamp on save, tolerate absent, reject
// an unknown higher schema.

#include <unity.h>

#include <string>

#include "vialucis/config_schema.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

void test_schema_accepted_rules() {
    TEST_ASSERT_TRUE(schemaAccepted(0));             // absent -> treat as 1
    TEST_ASSERT_TRUE(schemaAccepted(kConfigSchema));  // exact current
    TEST_ASSERT_FALSE(schemaAccepted(kConfigSchema + 1));  // newer -> reject
    TEST_ASSERT_FALSE(schemaAccepted(-1));                 // never emitted
}

void test_stamp_adds_schema_field() {
    std::string out = stampSchema("{\"a\":1}");
    TEST_ASSERT_TRUE(out.find("\"schema\":1") != std::string::npos);
    TEST_ASSERT_EQUAL_INT(kConfigSchema, readSchema(out));
    // Original data survives the round-trip.
    TEST_ASSERT_TRUE(out.find("\"a\":1") != std::string::npos);
}

void test_stamp_overwrites_existing_schema() {
    std::string out = stampSchema("{\"schema\":99,\"a\":1}", 1);
    TEST_ASSERT_EQUAL_INT(1, readSchema(out));
}

void test_stamp_leaves_non_object_untouched() {
    TEST_ASSERT_EQUAL_STRING("[1,2,3]", stampSchema("[1,2,3]").c_str());
    TEST_ASSERT_EQUAL_STRING("garbage", stampSchema("garbage").c_str());
}

void test_read_schema_absent_is_zero() {
    TEST_ASSERT_EQUAL_INT(0, readSchema("{\"a\":1}"));
    TEST_ASSERT_EQUAL_INT(0, readSchema("not json"));
}

void test_schema_loadable() {
    TEST_ASSERT_TRUE(schemaLoadable("{\"a\":1}"));            // absent -> ok
    TEST_ASSERT_TRUE(schemaLoadable("{\"schema\":1,\"a\":1}"));
    TEST_ASSERT_FALSE(schemaLoadable("{\"schema\":2,\"a\":1}"));  // newer
    // Garbage reads as schema 0 (loadable here) — the doc's own parser
    // rejects it downstream; this gate only guards against a newer schema.
    TEST_ASSERT_TRUE(schemaLoadable("garbage"));
}

void test_stamp_then_loadable_round_trip() {
    std::string doc = stampSchema("{\"tier\":\"twoPoint\"}");
    TEST_ASSERT_TRUE(schemaLoadable(doc));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_schema_accepted_rules);
    RUN_TEST(test_stamp_adds_schema_field);
    RUN_TEST(test_stamp_overwrites_existing_schema);
    RUN_TEST(test_stamp_leaves_non_object_untouched);
    RUN_TEST(test_read_schema_absent_is_zero);
    RUN_TEST(test_schema_loadable);
    RUN_TEST(test_stamp_then_loadable_round_trip);
    return UNITY_END();
}
