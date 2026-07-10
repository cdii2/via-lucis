// E2 — the expressive note-driven base: velocity curve, release decay,
// CC64 pedal latch. Same params serve Reactive and Presentation.

#include <unity.h>

#include <vector>

#include "vialucis/fx/note_driven.h"
#include "vialucis/key_led_table.h"

using namespace vialucis;
using fx::FxFrame;
using fx::NoteDriven;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint16_t kLeds = 360;

NoteDriven makeFx() {
    NoteDriven n;
    n.reset(1, kLeds);
    n.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    return n;
}

uint8_t peakAt(const std::vector<Rgb>& leds, uint8_t note) {
    LedRange r = ledsForNote(note, LedMapConfig{});
    uint8_t peak = 0;
    for (uint16_t i = r.first; i <= r.last; ++i) {
        const Rgb& c = leds[i];
        uint8_t m = c.r > c.g ? c.r : c.g;
        if (c.b > m) m = c.b;
        if (m > peak) peak = m;
    }
    return peak;
}

// Render frame n (advances effect-time by one fixed step per call).
uint8_t renderPeak(NoteDriven& n, uint32_t frame, uint8_t note) {
    std::vector<Rgb> leds(kLeds);
    FxFrame f{leds, frame, frame * fx::kFxStepMs};
    n.render(f);
    return peakAt(leds, note);
}

// The exact peak the palette yields for a key at FULL brightness — the
// rainbow entries are not 255-max, so endpoints pin against palette truth.
uint8_t fullPeakFor(uint8_t note) {
    uint8_t idx = static_cast<uint8_t>(((note - 21) * 255) / 87);
    Rgb c = fx::colorFromPalette(fx::rainbowColors(), idx, 255);
    uint8_t m = c.r > c.g ? c.r : c.g;
    return c.b > m ? c.b : m;
}

}  // namespace

void test_velocity_curve_endpoints() {
    NoteDriven n = makeFx();
    n.noteOn(60, 0);  // velocity 0: level 0 — dark
    TEST_ASSERT_EQUAL_UINT8(0, renderPeak(n, 0, 60));
    n.noteOn(60, 127);  // velocity 127: the palette's full brightness
    TEST_ASSERT_EQUAL_UINT8(fullPeakFor(60), renderPeak(n, 1, 60));
}

void test_velocity_gamma_shapes_mid_levels() {
    NoteDriven soft = makeFx();
    NoteDriven hard = makeFx();
    NoteDriven::Params p;
    p.velGamma = 0.5f;  // lifts soft playing
    soft.setParams(p);
    p.velGamma = 2.0f;  // demands force
    hard.setParams(p);
    soft.noteOn(60, 64);
    hard.noteOn(60, 64);
    TEST_ASSERT_TRUE(renderPeak(soft, 0, 60) > renderPeak(hard, 0, 60));
}

void test_release_decays_to_dark_over_release_ms() {
    NoteDriven n = makeFx();
    NoteDriven::Params p;
    p.releaseMs = 160;  // = 10 fixed frames
    p.pedalLatch = false;
    n.setParams(p);
    n.noteOn(60, 127);
    TEST_ASSERT_TRUE(renderPeak(n, 0, 60) > 200);
    n.noteOff(60);
    uint8_t mid = renderPeak(n, 3, 60);
    TEST_ASSERT_TRUE(mid > 0 && mid < 250);  // fading, not cut
    for (uint32_t f = 4; f < 15; ++f) renderPeak(n, f, 60);
    TEST_ASSERT_EQUAL_UINT8(0, renderPeak(n, 15, 60));
}

void test_release_zero_is_hard_cut() {
    NoteDriven n = makeFx();
    NoteDriven::Params p;
    p.releaseMs = 0;
    n.setParams(p);
    n.noteOn(60, 127);
    n.noteOff(60);
    TEST_ASSERT_EQUAL_UINT8(0, renderPeak(n, 0, 60));
}

void test_note_off_during_latch_holds_until_pedal_release() {
    NoteDriven n = makeFx();
    n.setPedal(true);
    n.noteOn(60, 127);
    n.noteOff(60);  // pedal down ⇒ latched
    // Held at full level for many frames — no decay while latched.
    TEST_ASSERT_EQUAL_UINT8(fullPeakFor(60), renderPeak(n, 0, 60));
    TEST_ASSERT_EQUAL_UINT8(fullPeakFor(60), renderPeak(n, 60, 60));
    n.setPedal(false);  // pedal-up releases EVERY latched glow
    for (uint32_t f = 61; f < 100; ++f) renderPeak(n, f, 60);
    TEST_ASSERT_EQUAL_UINT8(0, renderPeak(n, 100, 60));
}

void test_pedal_up_releases_all_latched_keys() {
    NoteDriven n = makeFx();
    n.setPedal(true);
    n.noteOn(60, 127);
    n.noteOn(64, 127);
    n.noteOff(60);
    n.noteOff(64);
    n.setPedal(false);
    NoteDriven::Params p = n.params();
    for (uint32_t f = 0; f < 40; ++f) renderPeak(n, f, 60);
    TEST_ASSERT_EQUAL_UINT8(0, renderPeak(n, 40, 60));
    TEST_ASSERT_EQUAL_UINT8(0, renderPeak(n, 41, 64));
    (void)p;
}

void test_pedal_latch_disabled_ignores_pedal() {
    NoteDriven n = makeFx();
    NoteDriven::Params p;
    p.pedalLatch = false;
    p.releaseMs = 160;
    n.setParams(p);
    n.setPedal(true);
    n.noteOn(60, 127);
    n.noteOff(60);  // decays despite the pedal
    for (uint32_t f = 0; f < 15; ++f) renderPeak(n, f, 60);
    TEST_ASSERT_EQUAL_UINT8(0, renderPeak(n, 15, 60));
}

void test_off_strip_and_out_of_range_notes_stay_dark() {
    NoteDriven n = makeFx();
    n.noteOn(10, 127);   // below the 88 keys: ignored
    n.noteOn(120, 127);  // above: ignored
    std::vector<Rgb> leds(kLeds);
    FxFrame f{leds, 0, 0};
    n.render(f);
    for (const Rgb& c : leds) TEST_ASSERT_TRUE(!c.r && !c.g && !c.b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_velocity_curve_endpoints);
    RUN_TEST(test_velocity_gamma_shapes_mid_levels);
    RUN_TEST(test_release_decays_to_dark_over_release_ms);
    RUN_TEST(test_release_zero_is_hard_cut);
    RUN_TEST(test_note_off_during_latch_holds_until_pedal_release);
    RUN_TEST(test_pedal_up_releases_all_latched_keys);
    RUN_TEST(test_pedal_latch_disabled_ignores_pedal);
    RUN_TEST(test_off_strip_and_out_of_range_notes_stay_dark);
    return UNITY_END();
}
