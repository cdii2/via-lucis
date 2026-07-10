#include <unity.h>

#include <ArduinoJson.h>

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

// R7 (in lieu of a field table — see ASSUMPTIONS A31): the field names ARE
// the REST contract (docs/API.md, webui data-key attrs). This locks the key
// set byte-exactly, so a rename breaks a native test instead of silently
// breaking the web UI.
static void test_to_json_emits_exactly_the_contract_field_names() {
    const char* kContract[] = {
        "leftColor", "rightColor", "wrongColor",  "previewCap",
        "leadMs",    "offsetMm",   "ledsPerMeter", "brightness",
        "echoWindowMs", "wifiSsid", "wifiPass",
        // Q3 growth (sanctioned by the Q-wave charter): appended, nothing
        // existing changed.
        "repeatCueEnabled", "repeatColor", "repeatFillStartPct",
        "repeatFillPeakPct", "repeatFloorMs", "repeatWaitPulseMs",
    };
    constexpr size_t kCount = sizeof(kContract) / sizeof(kContract[0]);

    Settings s;
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, s.toJson()) ==
                     DeserializationError::Ok);
    JsonObjectConst o = doc.as<JsonObjectConst>();

    size_t emitted = 0;
    for (JsonPairConst kv : o) {
        bool known = false;
        for (const char* name : kContract)
            if (std::string(kv.key().c_str()) == name) known = true;
        TEST_ASSERT_TRUE_MESSAGE(known, kv.key().c_str());  // no strays
        ++emitted;
    }
    TEST_ASSERT_EQUAL_size_t(kCount, emitted);  // none missing either
}

// Q3: the six Incoming Re-press fields round-trip; a repeatColor equal to
// wrongColor is REJECTED (kept at its previous value) — a cue must never
// read as an error.
static void test_repeat_cue_fields_round_trip_and_clamp() {
    Settings s;
    TEST_ASSERT_TRUE(s.repeatCueEnabled);
    TEST_ASSERT_EQUAL_UINT8(0, s.repeatFillStartPct);
    TEST_ASSERT_EQUAL_UINT8(45, s.repeatFillPeakPct);
    TEST_ASSERT_EQUAL_UINT32(35, s.repeatFloorMs);
    TEST_ASSERT_EQUAL_UINT32(60, s.repeatWaitPulseMs);

    Settings out;
    TEST_ASSERT_TRUE(Settings::fromJson(
        "{\"repeatCueEnabled\":false,\"repeatColor\":\"#FFAA00\","
        "\"repeatFillStartPct\":20,\"repeatFillPeakPct\":100,"
        "\"repeatFloorMs\":50,\"repeatWaitPulseMs\":90}",
        out));
    TEST_ASSERT_FALSE(out.repeatCueEnabled);
    TEST_ASSERT_EQUAL_UINT8(0xFF, out.repeatColor.r);
    TEST_ASSERT_EQUAL_UINT8(0xAA, out.repeatColor.g);
    TEST_ASSERT_EQUAL_UINT8(20, out.repeatFillStartPct);
    TEST_ASSERT_EQUAL_UINT8(100, out.repeatFillPeakPct);
    TEST_ASSERT_EQUAL_UINT32(50, out.repeatFloorMs);
    TEST_ASSERT_EQUAL_UINT32(90, out.repeatWaitPulseMs);

    Settings clamp;
    TEST_ASSERT_TRUE(Settings::fromJson(
        "{\"repeatFillPeakPct\":250,\"repeatFloorMs\":99999}", clamp));
    TEST_ASSERT_EQUAL_UINT8(100, clamp.repeatFillPeakPct);
    TEST_ASSERT_EQUAL_UINT32(1000, clamp.repeatFloorMs);
}

static void test_wrong_red_repeat_color_rejected() {
    Settings out;  // wrongColor default = #FF0000
    TEST_ASSERT_TRUE(
        Settings::fromJson("{\"repeatColor\":\"#FF0000\"}", out));
    TEST_ASSERT_EQUAL_UINT8(255, out.repeatColor.g);  // stayed white
    TEST_ASSERT_EQUAL_UINT8(255, out.repeatColor.b);
    // A non-colliding value is accepted normally.
    TEST_ASSERT_TRUE(
        Settings::fromJson("{\"repeatColor\":\"#FF0001\"}", out));
    TEST_ASSERT_EQUAL_UINT8(1, out.repeatColor.b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_match_spec);
    RUN_TEST(test_json_round_trip);
    RUN_TEST(test_missing_fields_keep_defaults);
    RUN_TEST(test_garbage_json_rejected);
    RUN_TEST(test_values_clamped_to_sane_ranges);
    RUN_TEST(test_color_hex_strings);
    RUN_TEST(test_to_json_emits_exactly_the_contract_field_names);
    return UNITY_END();
}
