#include <unity.h>

#include "vialucis/frame_renderer.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static const Rgb kGreen{0, 255, 0};
static const Rgb kBlue{0, 0, 255};
static const Rgb kRed{255, 0, 0};

static FrameRenderer makeRenderer() {
    LedMapConfig map;  // defaults: 180/m, 360 LEDs
    RampConfig ramp;   // defaults: cap 0.45, lead 1s
    return FrameRenderer(TableBuilder::fromTwoPoint(map), ramp);
}

static bool allBlack(const FrameRenderer& r) {
    for (const Rgb& c : r.frame())
        if (c.r || c.g || c.b) return false;
    return true;
}

// Max channel value over the LEDs of a note's range.
static uint8_t peakChannel(const FrameRenderer& r, uint8_t note, int chan) {
    LedMapConfig map;
    LedRange range = ledsForNote(note, map);
    uint8_t peak = 0;
    for (uint16_t i = range.first; i <= range.last; ++i) {
        const Rgb& c = r.frame()[i];
        uint8_t v = chan == 0 ? c.r : (chan == 1 ? c.g : c.b);
        if (v > peak) peak = v;
    }
    return peak;
}

static void test_frame_sized_and_cleared() {
    FrameRenderer r = makeRenderer();
    TEST_ASSERT_EQUAL_size_t(360, r.frame().size());
    TEST_ASSERT_TRUE(allBlack(r));
    r.addDue(60, kGreen);
    r.clear();
    TEST_ASSERT_TRUE(allBlack(r));
}

static void test_due_note_is_full_brightness() {
    FrameRenderer r = makeRenderer();
    r.addDue(60, kGreen);
    TEST_ASSERT_EQUAL_UINT8(255, peakChannel(r, 60, 1));
    TEST_ASSERT_EQUAL_UINT8(0, peakChannel(r, 60, 0));
}

static void test_ramp_grows_toward_cap() {
    // onset a full lead-time away: dark. Half away: half the cap. Now: cap.
    FrameRenderer far = makeRenderer();
    far.addUpcoming(60, kGreen, 2000000, 1000000);  // exactly lead away
    TEST_ASSERT_EQUAL_UINT8(0, peakChannel(far, 60, 1));

    FrameRenderer half = makeRenderer();
    half.addUpcoming(60, kGreen, 1500000, 1000000);
    uint8_t expectHalf = static_cast<uint8_t>(255 * 0.45f * 0.5f + 0.5f);
    TEST_ASSERT_UINT8_WITHIN(2, expectHalf, peakChannel(half, 60, 1));

    FrameRenderer now = makeRenderer();
    now.addUpcoming(60, kGreen, 1000000, 1000000);
    uint8_t expectCap = static_cast<uint8_t>(255 * 0.45f + 0.5f);
    TEST_ASSERT_UINT8_WITHIN(2, expectCap, peakChannel(now, 60, 1));
}

static void test_ramp_never_exceeds_cap() {
    FrameRenderer r = makeRenderer();
    // Past-due onset fed to the ramp still clamps at the cap; the dead zone
    // between cap and 100% belongs to addDue alone (SPEC).
    r.addUpcoming(60, kGreen, 900000, 1000000);
    uint8_t cap = static_cast<uint8_t>(255 * 0.45f + 0.5f);
    TEST_ASSERT_TRUE(peakChannel(r, 60, 1) <= cap);
}

static void test_upcoming_outside_lead_is_dark() {
    FrameRenderer r = makeRenderer();
    r.addUpcoming(60, kGreen, 5000000, 1000000);
    TEST_ASSERT_TRUE(allBlack(r));
}

static void test_due_overrides_ramp_and_wrong_overrides_due() {
    FrameRenderer r = makeRenderer();
    r.addUpcoming(60, kBlue, 1200000, 1000000);
    r.addDue(60, kGreen);
    TEST_ASSERT_EQUAL_UINT8(255, peakChannel(r, 60, 1));
    TEST_ASSERT_EQUAL_UINT8(0, peakChannel(r, 60, 2));  // ramp layer lost
    r.addWrong(60, kRed);
    TEST_ASSERT_EQUAL_UINT8(255, peakChannel(r, 60, 0));
    TEST_ASSERT_EQUAL_UINT8(0, peakChannel(r, 60, 1));  // due layer lost
}

static void test_same_key_double_upcoming_takes_brighter() {
    FrameRenderer r = makeRenderer();
    r.addUpcoming(60, kGreen, 1900000, 1000000);  // dim
    r.addUpcoming(60, kGreen, 1100000, 1000000);  // bright
    FrameRenderer solo = makeRenderer();
    solo.addUpcoming(60, kGreen, 1100000, 1000000);
    TEST_ASSERT_EQUAL_UINT8(peakChannel(solo, 60, 1), peakChannel(r, 60, 1));
}

// A169 (§3-E item 11, unison-note color precedence): two Due-layer paints on
// the SAME note at the SAME (full) brightness — the shape of both hands
// hitting a unison key — blend rather than arbitrarily keeping whichever
// addDue() call landed first. Blending is commutative, so swapping the call
// order must produce the identical result (the "deterministic" half of the
// rule — nobody's iteration order decides the color the player sees).
static void test_unison_due_from_two_hands_blends_and_is_order_independent() {
    FrameRenderer r = makeRenderer();
    r.addDue(60, kBlue);
    r.addDue(60, kGreen);
    TEST_ASSERT_EQUAL_UINT8(128, peakChannel(r, 60, 1));  // green channel
    TEST_ASSERT_EQUAL_UINT8(128, peakChannel(r, 60, 2));  // blue channel
    TEST_ASSERT_EQUAL_UINT8(0, peakChannel(r, 60, 0));    // no red introduced

    FrameRenderer swapped = makeRenderer();
    swapped.addDue(60, kGreen);
    swapped.addDue(60, kBlue);
    TEST_ASSERT_EQUAL_UINT8(peakChannel(r, 60, 1), peakChannel(swapped, 60, 1));
    TEST_ASSERT_EQUAL_UINT8(peakChannel(r, 60, 2), peakChannel(swapped, 60, 2));
}

static void test_unmappable_note_is_harmless() {
    FrameRenderer r = makeRenderer();
    r.addDue(5, kGreen);      // below the 88 keys
    r.addWrong(120, kRed);    // above
    r.addUpcoming(200, kBlue, 1100000, 1000000);
    TEST_ASSERT_TRUE(allBlack(r));
}

static void test_neighboring_keys_light_independently() {
    FrameRenderer r = makeRenderer();
    r.addDue(60, kGreen);
    r.addDue(61, kBlue);
    TEST_ASSERT_EQUAL_UINT8(255, peakChannel(r, 60, 1));
    TEST_ASSERT_EQUAL_UINT8(0, peakChannel(r, 60, 2));
    TEST_ASSERT_EQUAL_UINT8(255, peakChannel(r, 61, 2));
    TEST_ASSERT_EQUAL_UINT8(0, peakChannel(r, 61, 1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_frame_sized_and_cleared);
    RUN_TEST(test_due_note_is_full_brightness);
    RUN_TEST(test_ramp_grows_toward_cap);
    RUN_TEST(test_ramp_never_exceeds_cap);
    RUN_TEST(test_upcoming_outside_lead_is_dark);
    RUN_TEST(test_due_overrides_ramp_and_wrong_overrides_due);
    RUN_TEST(test_same_key_double_upcoming_takes_brighter);
    RUN_TEST(test_unison_due_from_two_hands_blends_and_is_order_independent);
    RUN_TEST(test_unmappable_note_is_harmless);
    RUN_TEST(test_neighboring_keys_light_independently);
    return UNITY_END();
}
