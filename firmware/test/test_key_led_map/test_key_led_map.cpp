#include <unity.h>

#include "vialucis/key_led_map.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

// Geometry refresher (cluster model, octave = 164.5mm):
//   C-E cluster: 5 slots of 14.1mm; F-B cluster: 7 slots of ~13.4286mm.
//   Origin = left edge of the A0 key. A0 is an F-B-cluster key, so its center
//   sits half a slot (≈6.714mm) right of the origin.

static void test_a0_center() {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 6.714f, keyCenterMm(21));
}

static void test_middle_c_center() {
    // C4 virtual pos = 4*164.5 + 7.05 = 665.05; minus A0 left edge 124.214
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 540.84f, keyCenterMm(60));
}

static void test_top_c_center() {
    // C8 = 8*164.5 + 7.05 - 124.214
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1198.84f, keyCenterMm(108));
}

static void test_black_key_sits_between_whites() {
    float cs4 = keyCenterMm(61);  // C#4
    TEST_ASSERT_TRUE(cs4 > keyCenterMm(60));
    TEST_ASSERT_TRUE(cs4 < keyCenterMm(62));
}

static void test_out_of_range_notes_invalid() {
    TEST_ASSERT_TRUE(keyCenterMm(20) < 0.0f);
    TEST_ASSERT_TRUE(keyCenterMm(109) < 0.0f);
    LedMapConfig cfg;
    TEST_ASSERT_FALSE(ledsForNote(20, cfg).valid);
    TEST_ASSERT_FALSE(ledsForNote(109, cfg).valid);
}

static void test_default_config_a0_leds() {
    LedMapConfig cfg;  // 180 LED/m, offset 0, 360 LEDs
    LedRange r = ledsForNote(21, cfg);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(0, r.first);
    TEST_ASSERT_EQUAL_UINT16(1, r.last);
}

static void test_every_key_gets_at_least_one_led() {
    LedMapConfig cfg;
    for (uint8_t n = 21; n <= 108; ++n) {
        LedRange r = ledsForNote(n, cfg);
        TEST_ASSERT_TRUE_MESSAGE(r.valid, "key must map");
        TEST_ASSERT_TRUE_MESSAGE(r.last >= r.first, "range must be non-empty");
    }
}

static void test_adjacent_keys_never_share_leds() {
    LedMapConfig cfg;
    for (uint8_t n = 21; n < 108; ++n) {
        LedRange a = ledsForNote(n, cfg);
        LedRange b = ledsForNote(n + 1, cfg);
        TEST_ASSERT_TRUE_MESSAGE(a.last < b.first,
                                 "neighbor keys must not overlap");
    }
}

static void test_offset_shifts_mapping() {
    LedMapConfig cfg;
    LedRange base = ledsForNote(60, cfg);
    cfg.offsetMm = 1000.0f / cfg.ledsPerMeter * 2;  // exactly two LED pitches
    LedRange shifted = ledsForNote(60, cfg);
    TEST_ASSERT_EQUAL_UINT16(base.first + 2, shifted.first);
    TEST_ASSERT_EQUAL_UINT16(base.last + 2, shifted.last);
}

static void test_negative_offset_can_push_key_off_strip() {
    LedMapConfig cfg;
    cfg.offsetMm = -50.0f;  // strip starts 50mm right of A0's left edge
    LedRange r = ledsForNote(21, cfg);
    TEST_ASSERT_FALSE(r.valid);  // A0 now hangs off the start of the strip
}

static void test_range_clamped_to_led_count() {
    LedMapConfig cfg;
    cfg.ledCount = 100;  // short strip: high keys fall off the end
    LedRange r = ledsForNote(108, cfg);
    TEST_ASSERT_FALSE(r.valid);
    LedRange mid = ledsForNote(60, cfg);
    TEST_ASSERT_TRUE(mid.valid);
    TEST_ASSERT_TRUE(mid.last < 100);
}

static void test_sparser_strip_still_maps() {
    LedMapConfig cfg;
    cfg.ledsPerMeter = 60.0f;  // 16.7mm pitch — coarser than a key
    LedRange r = ledsForNote(60, cfg);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_TRUE(r.last >= r.first);
}

static void test_full_keyboard_fits_default_strip() {
    LedMapConfig cfg;  // 2m of 180/m = 360 LEDs vs ~1225mm of keys
    LedRange lo = ledsForNote(21, cfg);
    LedRange hi = ledsForNote(108, cfg);
    TEST_ASSERT_TRUE(lo.valid);
    TEST_ASSERT_TRUE(hi.valid);
    TEST_ASSERT_TRUE(hi.last < 360);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a0_center);
    RUN_TEST(test_middle_c_center);
    RUN_TEST(test_top_c_center);
    RUN_TEST(test_black_key_sits_between_whites);
    RUN_TEST(test_out_of_range_notes_invalid);
    RUN_TEST(test_default_config_a0_leds);
    RUN_TEST(test_every_key_gets_at_least_one_led);
    RUN_TEST(test_adjacent_keys_never_share_leds);
    RUN_TEST(test_offset_shifts_mapping);
    RUN_TEST(test_negative_offset_can_push_key_off_strip);
    RUN_TEST(test_range_clamped_to_led_count);
    RUN_TEST(test_sparser_strip_still_maps);
    RUN_TEST(test_full_keyboard_fits_default_strip);
    return UNITY_END();
}
