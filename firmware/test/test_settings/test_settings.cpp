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
        // M3 growth: the AFK idle timeout.
        "afkTimeoutSec",
        // v3 REC4 growth (the one sanctioned v1 contract change): the
        // recording byte budget. Appended; nothing existing changed.
        "recordBudgetKB",
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

    // M3: afkTimeoutSec — default 180, 0 allowed (never), clamped to a day.
    Settings afk;
    TEST_ASSERT_EQUAL_UINT32(180, afk.afkTimeoutSec);
    TEST_ASSERT_TRUE(Settings::fromJson("{\"afkTimeoutSec\":0}", afk));
    TEST_ASSERT_EQUAL_UINT32(0, afk.afkTimeoutSec);
    TEST_ASSERT_TRUE(Settings::fromJson("{\"afkTimeoutSec\":999999}", afk));
    TEST_ASSERT_EQUAL_UINT32(86400, afk.afkTimeoutSec);
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

static void test_wrong_color_cannot_land_on_repeat_color() {
    // The guard cuts both ways: editing wrongColor onto the current
    // repeatColor (default white) is rejected too.
    Settings out;
    TEST_ASSERT_TRUE(
        Settings::fromJson("{\"wrongColor\":\"#FFFFFF\"}", out));
    TEST_ASSERT_EQUAL_UINT8(255, out.wrongColor.r);  // stayed #FF0000
    TEST_ASSERT_EQUAL_UINT8(0, out.wrongColor.g);
    TEST_ASSERT_EQUAL_UINT8(0, out.wrongColor.b);
    // A non-colliding wrongColor edit still works.
    TEST_ASSERT_TRUE(
        Settings::fromJson("{\"wrongColor\":\"#CC0000\"}", out));
    TEST_ASSERT_EQUAL_UINT8(0xCC, out.wrongColor.r);
}

// REC4: the recording byte budget — default 64 KB (arm() reserves the whole
// budget as one contiguous heap block; 256 KB rarely fits on a stock ESP32
// with BLE + WiFi up), round-trips, clamps to 16–256 KB (B4/B5 ask 4: the
// 256 KB per-song save ceiling is the hard max — a bigger take is unsaveable).
static void test_record_budget_round_trip_and_clamp() {
    Settings s;
    TEST_ASSERT_EQUAL_UINT32(64, s.recordBudgetKB);  // default
    TEST_ASSERT_TRUE(s.toJson().find("\"recordBudgetKB\":64") !=
                     std::string::npos);

    Settings out;
    TEST_ASSERT_TRUE(
        Settings::fromJson("{\"recordBudgetKB\":200}", out));
    TEST_ASSERT_EQUAL_UINT32(200, out.recordBudgetKB);
    // Clamp both ends.
    TEST_ASSERT_TRUE(Settings::fromJson("{\"recordBudgetKB\":4}", out));
    TEST_ASSERT_EQUAL_UINT32(16, out.recordBudgetKB);
    TEST_ASSERT_TRUE(Settings::fromJson("{\"recordBudgetKB\":512}", out));
    TEST_ASSERT_EQUAL_UINT32(256, out.recordBudgetKB);  // over the save ceiling
    TEST_ASSERT_TRUE(Settings::fromJson("{\"recordBudgetKB\":99999}", out));
    TEST_ASSERT_EQUAL_UINT32(256, out.recordBudgetKB);
}

// --- wifiPass write-only (ruling §6-1, A139) ---------------------------------

// THE PIN: the Public view (everything that leaves the device — GET, PUT echo,
// Export) must NEVER contain wifiPass, and MUST advertise wifiPassSet instead.
// If anyone reintroduces the cleartext-password leak, this fails.
static void test_public_view_never_contains_wifipass() {
    Settings s;
    s.wifiSsid = "HomeNet";
    s.wifiPass = "hunter2!";  // obviously-fake fixture — never a real secret
    std::string pub = s.toJson(Settings::View::Public);

    // Parse it and assert key-level, not substring (a value could coincide).
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, pub) == DeserializationError::Ok);
    JsonObjectConst o = doc.as<JsonObjectConst>();
    TEST_ASSERT_FALSE(o["wifiPass"].is<const char*>());  // key absent
    TEST_ASSERT_TRUE(o["wifiPassSet"].is<bool>());
    TEST_ASSERT_TRUE(o["wifiPassSet"].as<bool>());  // non-empty ⇒ true
    TEST_ASSERT_EQUAL_STRING("HomeNet", o["wifiSsid"].as<const char*>());
    // Belt-and-braces: the secret's bytes appear nowhere in the payload.
    TEST_ASSERT_TRUE(pub.find("hunter2!") == std::string::npos);

    // Empty stored password ⇒ wifiPassSet:false, still no wifiPass key.
    Settings empty;
    std::string pub2 = empty.toJson(Settings::View::Public);
    JsonDocument doc2;
    TEST_ASSERT_TRUE(deserializeJson(doc2, pub2) == DeserializationError::Ok);
    JsonObjectConst o2 = doc2.as<JsonObjectConst>();
    TEST_ASSERT_FALSE(o2["wifiPass"].is<const char*>());
    TEST_ASSERT_TRUE(o2["wifiPassSet"].is<bool>());
    TEST_ASSERT_FALSE(o2["wifiPassSet"].as<bool>());
}

// The Persist view (flash only) MUST carry wifiPass so a reboot reconnects —
// and must NOT carry the wifiPassSet marker (that's a wire-only affordance).
static void test_persist_view_carries_wifipass() {
    Settings s;
    s.wifiPass = "hunter2!";
    std::string persist = s.toJson(Settings::View::Persist);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, persist) == DeserializationError::Ok);
    JsonObjectConst o = doc.as<JsonObjectConst>();
    TEST_ASSERT_TRUE(o["wifiPass"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("hunter2!", o["wifiPass"].as<const char*>());
    TEST_ASSERT_FALSE(o["wifiPassSet"].is<bool>());  // no marker in flash doc
    // Default view is Persist (the load-bearing flash path is unchanged).
    TEST_ASSERT_TRUE(s.toJson().find("wifiPass") != std::string::npos);
}

// Write-only PATCH + clear semantics (A139): key present sets, "" clears, key
// absent leaves the stored password untouched (round-trip through the redacted
// Public view is lossless as long as the client omits the key it never saw).
static void test_wifipass_write_only_patch_and_clear() {
    Settings out;
    out.wifiPass = "hunter2!";

    // A body WITHOUT wifiPass leaves the stored password intact — this is what
    // makes the redacted GET → PUT round-trip safe.
    TEST_ASSERT_TRUE(Settings::fromJson("{\"leadMs\":800}", out));
    TEST_ASSERT_EQUAL_STRING("hunter2!", out.wifiPass.c_str());

    // A body WITH wifiPass sets it.
    TEST_ASSERT_TRUE(Settings::fromJson("{\"wifiPass\":\"newpass9\"}", out));
    TEST_ASSERT_EQUAL_STRING("newpass9", out.wifiPass.c_str());

    // An explicit empty string CLEARS it (the documented clear affordance).
    TEST_ASSERT_TRUE(Settings::fromJson("{\"wifiPass\":\"\"}", out));
    TEST_ASSERT_TRUE(out.wifiPass.empty());
}

// The Public view's field set is a contract too: exactly the Persist keys minus
// wifiPass, plus wifiPassSet. Locks the redaction shape byte-exactly.
static void test_public_view_contract_field_names() {
    const char* kPublic[] = {
        "leftColor", "rightColor", "wrongColor",  "previewCap",
        "leadMs",    "offsetMm",   "ledsPerMeter", "brightness",
        "echoWindowMs", "wifiSsid", "wifiPassSet",  // <-- not wifiPass
        "repeatCueEnabled", "repeatColor", "repeatFillStartPct",
        "repeatFillPeakPct", "repeatFloorMs", "repeatWaitPulseMs",
        "afkTimeoutSec", "recordBudgetKB",
    };
    constexpr size_t kCount = sizeof(kPublic) / sizeof(kPublic[0]);
    Settings s;
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, s.toJson(Settings::View::Public)) ==
                     DeserializationError::Ok);
    JsonObjectConst o = doc.as<JsonObjectConst>();
    size_t emitted = 0;
    for (JsonPairConst kv : o) {
        bool known = false;
        for (const char* name : kPublic)
            if (std::string(kv.key().c_str()) == name) known = true;
        TEST_ASSERT_TRUE_MESSAGE(known, kv.key().c_str());  // no strays
        ++emitted;
    }
    TEST_ASSERT_EQUAL_size_t(kCount, emitted);  // none missing either
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_public_view_never_contains_wifipass);
    RUN_TEST(test_persist_view_carries_wifipass);
    RUN_TEST(test_wifipass_write_only_patch_and_clear);
    RUN_TEST(test_public_view_contract_field_names);
    RUN_TEST(test_record_budget_round_trip_and_clamp);
    RUN_TEST(test_wrong_color_cannot_land_on_repeat_color);
    RUN_TEST(test_defaults_match_spec);
    RUN_TEST(test_json_round_trip);
    RUN_TEST(test_missing_fields_keep_defaults);
    RUN_TEST(test_garbage_json_rejected);
    RUN_TEST(test_values_clamped_to_sane_ranges);
    RUN_TEST(test_color_hex_strings);
    RUN_TEST(test_to_json_emits_exactly_the_contract_field_names);
    return UNITY_END();
}
