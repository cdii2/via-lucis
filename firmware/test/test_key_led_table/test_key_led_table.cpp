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

// ---------------------------------------------------------------------------
// C2 — remaining builders + validation + strip direction.

static void assertMirrorOf(const KeyLedTable& fwd, const KeyLedTable& rev) {
    TEST_ASSERT_EQUAL_UINT16(fwd.ledCount(), rev.ledCount());
    uint16_t n = fwd.ledCount();
    for (uint8_t k = 21; k <= 108; ++k) {
        LedRange f = fwd.forNote(k);
        LedRange r = rev.forNote(k);
        TEST_ASSERT_EQUAL_MESSAGE(f.valid, r.valid, "validity must mirror");
        if (f.valid) {
            TEST_ASSERT_EQUAL_UINT16(n - 1 - f.last, r.first);
            TEST_ASSERT_EQUAL_UINT16(n - 1 - f.first, r.last);
        }
    }
}

static void test_two_point_reversed_flag_mirrors() {
    LedMapConfig cfg;
    KeyLedTable fwd = TableBuilder::fromTwoPoint(cfg);
    KeyLedTable rev = TableBuilder::fromTwoPoint(cfg, /*reversed=*/true);
    assertMirrorOf(fwd, rev);
    TEST_ASSERT_EQUAL(TableError::None, TableBuilder::validate(rev));
}

static void test_landmark_segments_are_local() {
    // Piecewise semantics: a key whose whole slot lies within one landmark
    // segment is mapped by that segment ALONE — the 3-landmark build must
    // agree exactly with the 2-landmark build of that segment.
    std::vector<Landmark> all = {{21, 2}, {60, 150}, {108, 340}};
    std::vector<Landmark> lowSeg = {{21, 2}, {60, 150}};
    std::vector<Landmark> highSeg = {{60, 150}, {108, 340}};
    KeyLedTable full, low, high;
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(all, 360, full));
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(lowSeg, 360, low));
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(highSeg, 360, high));
    const float knot = keyCenterMm(60);
    int lowChecked = 0, highChecked = 0;
    for (uint8_t k = 21; k <= 108; ++k) {
        float half = 0.5f * keySlotWidthMm(k);
        const KeyLedTable* expect = nullptr;
        if (keyCenterMm(k) + half <= knot) {
            expect = &low;
            ++lowChecked;
        } else if (keyCenterMm(k) - half >= knot) {
            expect = &high;
            ++highChecked;
        } else {
            continue;  // slot straddles the knot: both segments contribute
        }
        TEST_ASSERT_EQUAL(expect->forNote(k).valid, full.forNote(k).valid);
        if (full.forNote(k).valid) {
            TEST_ASSERT_EQUAL_UINT16(expect->forNote(k).first,
                                     full.forNote(k).first);
            TEST_ASSERT_EQUAL_UINT16(expect->forNote(k).last,
                                     full.forNote(k).last);
        }
    }
    TEST_ASSERT_TRUE(lowChecked > 30);   // the comparison actually ran
    TEST_ASSERT_TRUE(highChecked > 30);
}

static void test_landmarks_build_is_monotonic_and_valid() {
    std::vector<Landmark> lm = {{21, 2}, {60, 150}, {108, 340}};
    KeyLedTable t;
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(lm, 360, t));
    TEST_ASSERT_EQUAL(TableError::None, TableBuilder::validate(t));
    // Every key maps (dense strip), ranges strictly ascend.
    uint16_t prevLast = 0;
    bool first = true;
    for (uint8_t k = 21; k <= 108; ++k) {
        LedRange r = t.forNote(k);
        TEST_ASSERT_TRUE_MESSAGE(r.valid, "dense build: every key maps");
        if (!first) TEST_ASSERT_TRUE(r.first > prevLast);
        prevLast = r.last;
        first = false;
    }
}

static void test_landmark_keys_cover_their_led() {
    std::vector<Landmark> lm = {{21, 2}, {60, 150}, {108, 340}};
    KeyLedTable t;
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(lm, 360, t));
    for (const Landmark& m : lm) {
        LedRange r = t.forNote(m.note);
        TEST_ASSERT_TRUE(r.valid);
        TEST_ASSERT_TRUE_MESSAGE(m.led >= r.first && m.led <= r.last,
                                 "landmark key must own its measured LED");
    }
}

static void test_landmarks_bent_map_differs_from_straight() {
    // A genuinely off-line middle landmark must change the mapping —
    // proves the middle segment is actually used.
    std::vector<Landmark> straight = {{21, 2}, {108, 340}};
    std::vector<Landmark> bent = {{21, 2}, {60, 100}, {108, 340}};
    KeyLedTable a, b;
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(straight, 360, a));
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(bent, 360, b));
    bool differs = false;
    for (uint8_t k = 22; k < 108 && !differs; ++k)
        differs = a.forNote(k).first != b.forNote(k).first ||
                  a.forNote(k).last != b.forNote(k).last;
    TEST_ASSERT_TRUE(differs);
}

static void test_landmarks_descending_leds_mirror_ascending() {
    std::vector<Landmark> asc = {{21, 2}, {60, 150}, {108, 340}};
    std::vector<Landmark> desc = {{21, 357}, {60, 209}, {108, 19}};
    // desc led = 359 - asc led
    KeyLedTable fwd, rev;
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(asc, 360, fwd));
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(desc, 360, rev));
    assertMirrorOf(fwd, rev);
    TEST_ASSERT_EQUAL(TableError::None, TableBuilder::validate(rev));
}

static void test_landmarks_extrapolate_beyond_ends() {
    // Landmarks only in the middle: outer keys extrapolate on the end
    // segments and still map on a strip long enough to hold them.
    std::vector<Landmark> lm = {{48, 120}, {72, 220}};
    KeyLedTable t;
    TEST_ASSERT_EQUAL(TableError::None,
                      TableBuilder::fromLandmarks(lm, 400, t));
    TEST_ASSERT_TRUE(t.forNote(21).valid);
    TEST_ASSERT_TRUE(t.forNote(108).valid);
    TEST_ASSERT_EQUAL(TableError::None, TableBuilder::validate(t));
}

static void test_landmark_input_errors_are_typed() {
    KeyLedTable t;
    std::vector<Landmark> one = {{60, 100}};
    TEST_ASSERT_EQUAL(TableError::TooFewLandmarks,
                      TableBuilder::fromLandmarks(one, 360, t));
    std::vector<Landmark> unsorted = {{60, 100}, {21, 2}};
    TEST_ASSERT_EQUAL(TableError::UnsortedLandmarks,
                      TableBuilder::fromLandmarks(unsorted, 360, t));
    std::vector<Landmark> dupNote = {{60, 100}, {60, 120}};
    TEST_ASSERT_EQUAL(TableError::UnsortedLandmarks,
                      TableBuilder::fromLandmarks(dupNote, 360, t));
    std::vector<Landmark> badNote = {{10, 2}, {60, 100}};
    TEST_ASSERT_EQUAL(TableError::BadLandmarkNote,
                      TableBuilder::fromLandmarks(badNote, 360, t));
    std::vector<Landmark> badLed = {{21, 2}, {60, 400}};
    TEST_ASSERT_EQUAL(TableError::BadLandmarkLed,
                      TableBuilder::fromLandmarks(badLed, 360, t));
    std::vector<Landmark> flat = {{21, 100}, {60, 100}};
    TEST_ASSERT_EQUAL(TableError::DirectionMixed,
                      TableBuilder::fromLandmarks(flat, 360, t));
    std::vector<Landmark> zigzag = {{21, 2}, {60, 150}, {108, 100}};
    TEST_ASSERT_EQUAL(TableError::DirectionMixed,
                      TableBuilder::fromLandmarks(zigzag, 360, t));
}

static void test_validate_catches_overlap() {
    KeyLedTable t = TableBuilder::fromTwoPoint(LedMapConfig{});
    LedRange bad = t.forNote(61);
    bad.first = t.forNote(60).last;  // now overlaps key 60
    t.set(61, bad);
    uint8_t badKey = 0;
    TEST_ASSERT_EQUAL(TableError::Overlap, TableBuilder::validate(t, &badKey));
    TEST_ASSERT_EQUAL_UINT8(61, badKey);
}

static void test_validate_catches_off_strip() {
    KeyLedTable t = TableBuilder::fromTwoPoint(LedMapConfig{});
    t.set(60, LedRange{350, 360, true});  // last == ledCount: off the end
    uint8_t badKey = 0;
    TEST_ASSERT_EQUAL(TableError::RangeOffStrip,
                      TableBuilder::validate(t, &badKey));
    TEST_ASSERT_EQUAL_UINT8(60, badKey);
    KeyLedTable inverted = TableBuilder::fromTwoPoint(LedMapConfig{});
    inverted.set(60, LedRange{20, 10, true});  // first > last
    TEST_ASSERT_EQUAL(TableError::RangeOffStrip, TableBuilder::validate(inverted));
}

static void test_validate_catches_mixed_direction() {
    KeyLedTable t;
    t.setLedCount(360);
    t.set(30, LedRange{10, 12, true});
    t.set(40, LedRange{50, 52, true});   // ascending so far
    t.set(50, LedRange{30, 32, true});   // drops back — mixed
    TEST_ASSERT_EQUAL(TableError::DirectionMixed, TableBuilder::validate(t));
}

static void test_validate_accepts_sparse_and_empty_tables() {
    KeyLedTable empty;  // all invalid, 0 LEDs — structurally fine (dark)
    TEST_ASSERT_EQUAL(TableError::None, TableBuilder::validate(empty));
    KeyLedTable sparse;
    sparse.setLedCount(360);
    sparse.set(30, LedRange{10, 12, true});
    sparse.set(90, LedRange{300, 302, true});  // gaps of invalid keys are OK
    TEST_ASSERT_EQUAL(TableError::None, TableBuilder::validate(sparse));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_table_matches_v1_formula);
    RUN_TEST(test_fuzzed_tables_match_v1_formula);
    RUN_TEST(test_degenerate_configs_match_v1_formula);
    RUN_TEST(test_out_of_range_notes_invalid);
    RUN_TEST(test_default_constructed_table_is_all_invalid);
    RUN_TEST(test_table_frames_integer_identical_to_v1);
    RUN_TEST(test_two_point_reversed_flag_mirrors);
    RUN_TEST(test_landmark_segments_are_local);
    RUN_TEST(test_landmarks_build_is_monotonic_and_valid);
    RUN_TEST(test_landmark_keys_cover_their_led);
    RUN_TEST(test_landmarks_bent_map_differs_from_straight);
    RUN_TEST(test_landmarks_descending_leds_mirror_ascending);
    RUN_TEST(test_landmarks_extrapolate_beyond_ends);
    RUN_TEST(test_landmark_input_errors_are_typed);
    RUN_TEST(test_validate_catches_overlap);
    RUN_TEST(test_validate_catches_off_strip);
    RUN_TEST(test_validate_catches_mixed_direction);
    RUN_TEST(test_validate_accepts_sparse_and_empty_tables);
    return UNITY_END();
}
