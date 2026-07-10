#include <unity.h>

#include "vialucis/frame_renderer.h"
#include "vialucis/key_led_table.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

// C1 characterization (OV4): the table built by TableBuilder::fromTwoPoint is
// INTEGER-identical to the v1 formula — equality of all 88 LedRange values
// (first, last, valid) and of resulting frames. No float tolerance anywhere.

// Deterministic fuzz PRNG — fixed seed, no <random> (keep esp32-buildable).
static uint32_t lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
static float lcgRange(uint32_t& s, float lo, float hi) {
    return lo + (hi - lo) * (lcg(s) % 10000u) / 10000.0f;
}

static void assertTableMatchesFormula(const LedMapConfig& cfg) {
    KeyLedTable t = TableBuilder::fromTwoPoint(cfg);
    TEST_ASSERT_EQUAL_UINT16(cfg.ledCount, t.ledCount());
    for (uint8_t n = 21; n <= 108; ++n) {
        LedRange v1 = ledsForNote(n, cfg);
        LedRange v2 = t.forNote(n);
        TEST_ASSERT_EQUAL_MESSAGE(v1.valid, v2.valid, "valid must match");
        if (v1.valid) {
            TEST_ASSERT_EQUAL_UINT16(v1.first, v2.first);
            TEST_ASSERT_EQUAL_UINT16(v1.last, v2.last);
        }
    }
}

static void test_default_table_matches_v1_formula() {
    assertTableMatchesFormula(LedMapConfig{});
}

static void test_fuzzed_tables_match_v1_formula() {
    uint32_t seed = 0xC1C1C1C1u;
    for (int i = 0; i < 200; ++i) {
        LedMapConfig cfg;
        cfg.offsetMm = lcgRange(seed, -120.0f, 120.0f);
        cfg.ledsPerMeter = lcgRange(seed, 30.0f, 400.0f);
        cfg.ledCount = static_cast<uint16_t>(1 + lcg(seed) % 500);
        assertTableMatchesFormula(cfg);
    }
}

static void test_degenerate_configs_match_v1_formula() {
    LedMapConfig zeroDensity;
    zeroDensity.ledsPerMeter = 0.0f;  // v1: every key invalid
    assertTableMatchesFormula(zeroDensity);

    LedMapConfig noLeds;
    noLeds.ledCount = 0;
    assertTableMatchesFormula(noLeds);

    LedMapConfig farOffset;
    farOffset.offsetMm = -5000.0f;  // everything off-strip
    assertTableMatchesFormula(farOffset);
}

static void test_out_of_range_notes_invalid() {
    KeyLedTable t = TableBuilder::fromTwoPoint(LedMapConfig{});
    TEST_ASSERT_FALSE(t.forNote(20).valid);
    TEST_ASSERT_FALSE(t.forNote(109).valid);
    TEST_ASSERT_FALSE(t.forNote(0).valid);
    TEST_ASSERT_FALSE(t.forNote(127).valid);
}

static void test_default_constructed_table_is_all_invalid() {
    KeyLedTable t;
    TEST_ASSERT_EQUAL_UINT16(0, t.ledCount());
    for (uint8_t n = 21; n <= 108; ++n) TEST_ASSERT_FALSE(t.forNote(n).valid);
}

// Frame-level characterization: a table-built FrameRenderer paints exactly
// the frame the v1 formula implies. Reference paint = v1 semantics (range
// from ledsForNote, later layers overwrite; here each key painted once on
// its own layer so overwrite order is the only rule exercised per LED).
static void referencePaint(std::vector<Rgb>& frame, const LedMapConfig& cfg,
                           uint8_t note, Rgb color) {
    LedRange r = ledsForNote(note, cfg);
    if (!r.valid) return;
    for (uint16_t i = r.first; i <= r.last && i < frame.size(); ++i)
        frame[i] = color;
}

static void test_table_frames_integer_identical_to_v1() {
    uint32_t seed = 0xF00DF00Du;
    for (int i = 0; i < 20; ++i) {
        LedMapConfig cfg;
        cfg.offsetMm = lcgRange(seed, -50.0f, 50.0f);
        cfg.ledsPerMeter = lcgRange(seed, 60.0f, 360.0f);
        cfg.ledCount = static_cast<uint16_t>(60 + lcg(seed) % 400);

        FrameRenderer r(TableBuilder::fromTwoPoint(cfg), RampConfig{});
        // Distinct keys, one paint each: due chord + a wrong flash.
        r.addDue(60, Rgb{0, 255, 0});
        r.addDue(64, Rgb{0, 0, 255});
        r.addWrong(90, Rgb{255, 0, 0});

        std::vector<Rgb> expect(cfg.ledCount);
        referencePaint(expect, cfg, 60, Rgb{0, 255, 0});
        referencePaint(expect, cfg, 64, Rgb{0, 0, 255});
        referencePaint(expect, cfg, 90, Rgb{255, 0, 0});

        TEST_ASSERT_EQUAL_size_t(expect.size(), r.frame().size());
        for (size_t j = 0; j < expect.size(); ++j) {
            TEST_ASSERT_EQUAL_UINT8(expect[j].r, r.frame()[j].r);
            TEST_ASSERT_EQUAL_UINT8(expect[j].g, r.frame()[j].g);
            TEST_ASSERT_EQUAL_UINT8(expect[j].b, r.frame()[j].b);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_table_matches_v1_formula);
    RUN_TEST(test_fuzzed_tables_match_v1_formula);
    RUN_TEST(test_degenerate_configs_match_v1_formula);
    RUN_TEST(test_out_of_range_notes_invalid);
    RUN_TEST(test_default_constructed_table_is_all_invalid);
    RUN_TEST(test_table_frames_integer_identical_to_v1);
    return UNITY_END();
}
