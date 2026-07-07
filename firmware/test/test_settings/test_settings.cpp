#include <unity.h>

#include "vialucis/settings.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static void test_defaults_match_spec() {
    Settings s;
    // left=blue, right=green, wrong=red — defaults must not collide (SPEC).
    TEST_ASSERT_EQUAL_UINT8(255, s.leftColor.b);
    TEST_ASSERT_EQUAL_UINT8(255, s.rightColor.g);
    TEST_ASSERT_EQUAL_UINT8(255, s.wrongColor.r);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.45f, s.previewCap);
    TEST_ASSERT_EQUAL_UINT32(1000, s.leadMs);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 180.0f, s.ledsPerMeter);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.offsetMm);
}

static void test_json_round_trip() {
    Settings s;
    s.leftColor = {10, 20, 30};
    s.previewCap = 0.6f;
    s.leadMs = 750;
    s.offsetMm = -12.5f;
    s.ledsPerMeter = 179.5f;
    s.brightness = 99;
    s.wifiSsid = "HomeNet";
    s.wifiPass = "hunter2!";
    std::string json = s.toJson();

    Settings out;
    TEST_ASSERT_TRUE(Settings::fromJson(json.c_str(), out));
    TEST_ASSERT_EQUAL_UINT8(10, out.leftColor.r);
    TEST_ASSERT_EQUAL_UINT8(30, out.leftColor.b);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, out.previewCap);
    TEST_ASSERT_EQUAL_UINT32(750, out.leadMs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -12.5f, out.offsetMm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 179.5f, out.ledsPerMeter);
    TEST_ASSERT_EQUAL_UINT8(99, out.brightness);
    TEST_ASSERT_EQUAL_STRING("HomeNet", out.wifiSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("hunter2!", out.wifiPass.c_str());
}

static void test_missing_fields_keep_defaults() {
    Settings out;
    TEST_ASSERT_TRUE(Settings::fromJson("{\"leadMs\": 500}", out));
    TEST_ASSERT_EQUAL_UINT32(500, out.leadMs);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.45f, out.previewCap);  // untouched
    TEST_ASSERT_EQUAL_UINT8(255, out.leftColor.b);
}

static void test_garbage_json_rejected() {
    Settings out;
    TEST_ASSERT_FALSE(Settings::fromJson("not json at all", out));
    TEST_ASSERT_FALSE(Settings::fromJson("", out));
    TEST_ASSERT_FALSE(Settings::fromJson(nullptr, out));
}

static void test_values_clamped_to_sane_ranges() {
    Settings out;
    TEST_ASSERT_TRUE(Settings::fromJson(
        "{\"previewCap\": 7.5, \"ledsPerMeter\": -3, \"leadMs\": 99999999}",
        out));
    TEST_ASSERT_TRUE(out.previewCap <= 1.0f);
    TEST_ASSERT_TRUE(out.ledsPerMeter > 0.0f);
    TEST_ASSERT_TRUE(out.leadMs <= 10000);  // 10s max lead
}

static void test_color_hex_strings() {
    Settings out;
    TEST_ASSERT_TRUE(Settings::fromJson("{\"leftColor\": \"#FF8000\"}", out));
    TEST_ASSERT_EQUAL_UINT8(0xFF, out.leftColor.r);
    TEST_ASSERT_EQUAL_UINT8(0x80, out.leftColor.g);
    TEST_ASSERT_EQUAL_UINT8(0x00, out.leftColor.b);
    // Serialized form is the same hex shape.
    Settings s;
    s.leftColor = {0xFF, 0x80, 0x00};
    TEST_ASSERT_TRUE(s.toJson().find("#FF8000") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_match_spec);
    RUN_TEST(test_json_round_trip);
    RUN_TEST(test_missing_fields_keep_defaults);
    RUN_TEST(test_garbage_json_rejected);
    RUN_TEST(test_values_clamped_to_sane_ranges);
    RUN_TEST(test_color_hex_strings);
    return UNITY_END();
}
