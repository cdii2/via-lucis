// Regression tests for the FastLED math/color kernel ported into
// vialucis::fx (see fx_math.h / fx_color.h / palette.h for the full
// attribution note: ported from FastLED 3.10.3, MIT license).
//
// Values are pinned two ways:
//  - Where the port task specified an exact expected value (sin8 at the
//    four cardinal thetas, scale8(255,255)/scale8(255,128)), that literal
//    value is asserted, independently re-derived by hand from the ported
//    algorithm (see the port's commit notes).
//  - Everywhere else, the pinned value is the ported implementation's own
//    output, captured once as a byte-for-byte regression anchor.

#include <unity.h>

#include <vector>

#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/fx/palette.h"

using namespace vialucis;
using namespace vialucis::fx;

void setUp() {}
void tearDown() {}

// --- sin8 / cos8 -------------------------------------------------------

void test_sin8_pinned_cardinal_values() {
    // Exact values specified by the port task; re-derived by hand from the
    // sin8_C algorithm (b_m16_interleave lookup table + linear segment).
    TEST_ASSERT_EQUAL_UINT8(128, sin8(0));
    TEST_ASSERT_EQUAL_UINT8(255, sin8(64));
    TEST_ASSERT_EQUAL_UINT8(128, sin8(128));
    TEST_ASSERT_EQUAL_UINT8(1, sin8(192));
}

void test_cos8_is_sin8_shifted_by_quarter_turn() {
    // cos8(theta) := sin8(theta + 64) — ported unchanged from trig8.h.
    TEST_ASSERT_EQUAL_UINT8(sin8(64), cos8(0));
    TEST_ASSERT_EQUAL_UINT8(sin8(0), cos8(192));
}

void test_sin16_cos16_zero_and_quarter_turn() {
    TEST_ASSERT_EQUAL_INT16(0, sin16(0));
    // cos16(0) == sin16(16384) (quarter turn) — approximation of 32767,
    // within FastLED's documented <0.69% error budget.
    TEST_ASSERT_EQUAL_INT16(32645, cos16(0));
    TEST_ASSERT_EQUAL_INT16(32645, sin16(16384));
    TEST_ASSERT_EQUAL_INT16(-32645, sin16(49152));  // three-quarter turn
}

// --- scale8 / scale8_video / scale16 ------------------------------------

void test_scale8_pinned_values() {
    // FASTLED_SCALE8_FIXED==1 (FastLED's default): scale8(255,255) must be
    // 255, never 254 — the whole point of the "fixed" formula.
    TEST_ASSERT_EQUAL_UINT8(255, scale8(255, 255));
    TEST_ASSERT_EQUAL_UINT8(128, scale8(255, 128));
    TEST_ASSERT_EQUAL_UINT8(64, scale8(128, 128));
    TEST_ASSERT_EQUAL_UINT8(0, scale8(0, 255));
    TEST_ASSERT_EQUAL_UINT8(0, scale8(255, 0));
}

void test_scale8_video_never_zeroes_a_lit_nonzero_input() {
    TEST_ASSERT_EQUAL_UINT8(0, scale8_video(0, 10));
    TEST_ASSERT_EQUAL_UINT8(0, scale8_video(10, 0));
    // scale8() would floor 1*10/256 to 0; scale8_video() must not.
    TEST_ASSERT_TRUE(scale8_video(1, 10) > 0);
}

void test_scale16_full_scale_is_identity() {
    TEST_ASSERT_EQUAL_UINT16(65535, scale16(65535, 65535));
    TEST_ASSERT_EQUAL_UINT16(0, scale16(65535, 0));
}

// --- qadd8 / qsub8 saturation --------------------------------------------

void test_qadd8_saturates_at_255() {
    TEST_ASSERT_EQUAL_UINT8(255, qadd8(200, 100));
    TEST_ASSERT_EQUAL_UINT8(30, qadd8(10, 20));
    TEST_ASSERT_EQUAL_UINT8(255, qadd8(255, 1));
}

void test_qsub8_saturates_at_0() {
    TEST_ASSERT_EQUAL_UINT8(0, qsub8(100, 150));
    TEST_ASSERT_EQUAL_UINT8(50, qsub8(100, 50));
    TEST_ASSERT_EQUAL_UINT8(0, qsub8(0, 1));
}

// --- avg8 / lerp8by8 -------------------------------------------------------

void test_avg8_rounds_down() {
    TEST_ASSERT_EQUAL_UINT8(30, avg8(20, 41));  // 30.5 -> 30
}

void test_lerp8by8_endpoints_and_midpoint() {
    TEST_ASSERT_EQUAL_UINT8(0, lerp8by8(0, 255, 0));
    TEST_ASSERT_EQUAL_UINT8(255, lerp8by8(0, 255, 255));
    TEST_ASSERT_EQUAL_UINT8(128, lerp8by8(0, 255, 128));
    TEST_ASSERT_EQUAL_UINT8(127, lerp8by8(255, 0, 128));
}

// --- waveform generators ---------------------------------------------------

void test_triwave8_shape() {
    TEST_ASSERT_EQUAL_UINT8(0, triwave8(0));
    TEST_ASSERT_EQUAL_UINT8(254, triwave8(127));  // peak just before the fold
    TEST_ASSERT_EQUAL_UINT8(254, triwave8(128));  // peak just after the fold
    TEST_ASSERT_EQUAL_UINT8(0, triwave8(255));    // back near zero
}

void test_quadwave8_and_cubicwave8_peak_and_floor() {
    TEST_ASSERT_EQUAL_UINT8(0, quadwave8(0));
    TEST_ASSERT_EQUAL_UINT8(255, quadwave8(128));
    TEST_ASSERT_EQUAL_UINT8(0, cubicwave8(0));
    TEST_ASSERT_EQUAL_UINT8(255, cubicwave8(128));
}

void test_ease8_endpoints_are_identity() {
    TEST_ASSERT_EQUAL_UINT8(0, ease8InOutCubic(0));
    TEST_ASSERT_EQUAL_UINT8(255, ease8InOutCubic(255));
    TEST_ASSERT_EQUAL_UINT8(0, ease8InOutQuad(0));
    TEST_ASSERT_EQUAL_UINT8(255, ease8InOutQuad(255));
}

// --- beat / beatsin determinism (VL7: explicit ms, fixed timestep) --------

void test_beat_functions_take_ms_not_a_global_clock() {
    // Same ms in ⇒ same value out, called repeatedly — no hidden state.
    uint16_t a = beat88(15360 /* 60bpm in Q8.8 */, 12345);
    uint16_t b = beat88(15360, 12345);
    TEST_ASSERT_EQUAL_UINT16(a, b);

    uint8_t s1 = beatsin8(60, 0, 255, 500);
    uint8_t s2 = beatsin8(60, 0, 255, 500);
    TEST_ASSERT_EQUAL_UINT8(s1, s2);
}

void test_beat8_at_ms_zero_is_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, beat8(60, 0));
}

void test_beatsin8_one_full_period_returns_to_start() {
    // 60 BPM ⇒ one beat every 1000ms. beat8's 8-bit truncation absorbs the
    // ~0.05% 60000:65536 conversion error documented in lib8tion.h, so this
    // is exact, not approximate, for this particular (bpm, period) pair.
    uint8_t atStart = beatsin8(60, 0, 255, 0);
    uint8_t after1s = beatsin8(60, 0, 255, 1000);
    TEST_ASSERT_EQUAL_UINT8(atStart, after1s);
    TEST_ASSERT_EQUAL_UINT8(128, atStart);  // sin8(0) == 128, the wave's rest value
}

// --- hsv2rgbRainbow ---------------------------------------------------------

void test_hsv2rgb_rainbow_pure_hues() {
    Rgb rgb;
    hsv2rgbRainbow(Hsv{0, 255, 255}, rgb);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.b);

    // h=85 (~1/3 of the wheel) and h=170 (~2/3) land in the green/blue
    // regions of FastLED's "rainbow" (not naive-spectrum) hue mapping.
    hsv2rgbRainbow(Hsv{85, 255, 255}, rgb);
    TEST_ASSERT_EQUAL_UINT8(59, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(226, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.b);

    hsv2rgbRainbow(Hsv{170, 255, 255}, rgb);
    TEST_ASSERT_EQUAL_UINT8(26, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(229, rgb.b);
}

void test_hsv2rgb_rainbow_zero_saturation_is_white() {
    Rgb rgb;
    hsv2rgbRainbow(Hsv{0, 0, 255}, rgb);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.b);
}

void test_hsv2rgb_rainbow_zero_value_is_black() {
    Rgb rgb;
    hsv2rgbRainbow(Hsv{0, 255, 0}, rgb);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.b);
}

// --- heatColor ---------------------------------------------------------

void test_heat_color_endpoints() {
    Rgb cold = heatColor(0);
    TEST_ASSERT_EQUAL_UINT8(0, cold.r);
    TEST_ASSERT_EQUAL_UINT8(0, cold.g);
    TEST_ASSERT_EQUAL_UINT8(0, cold.b);

    Rgb hot = heatColor(255);
    TEST_ASSERT_EQUAL_UINT8(255, hot.r);
    TEST_ASSERT_EQUAL_UINT8(255, hot.g);
    // Ramps to white but doesn't quite reach 255 in blue at the very top of
    // the input range (255 doesn't map onto an exact heatramp boundary) —
    // this "white-ish, not pure white" is the ported FastLED behavior.
    TEST_ASSERT_EQUAL_UINT8(252, hot.b);
}

void test_heat_color_middle_is_orange() {
    Rgb mid = heatColor(128);
    TEST_ASSERT_EQUAL_UINT8(255, mid.r);
    TEST_ASSERT_EQUAL_UINT8(128, mid.g);
    TEST_ASSERT_EQUAL_UINT8(0, mid.b);
}

// --- colorFromPalette --------------------------------------------------

void test_color_from_palette_index0_is_entry0_exact() {
    Rgb c = colorFromPalette(rainbowColors(), 0);
    TEST_ASSERT_EQUAL_UINT8(255, c.r);
    TEST_ASSERT_EQUAL_UINT8(0, c.g);
    TEST_ASSERT_EQUAL_UINT8(0, c.b);
}

void test_color_from_palette_index8_is_halfway_blend() {
    Palette16 blackToWhite;
    blackToWhite.entries[0] = Rgb{0, 0, 0};
    blackToWhite.entries[1] = Rgb{255, 255, 255};
    for (int i = 2; i < 16; ++i) blackToWhite.entries[i] = Rgb{0, 0, 0};

    Rgb half = colorFromPalette(blackToWhite, 8);  // halfway between 0 and 1
    TEST_ASSERT_EQUAL_UINT8(128, half.r);
    TEST_ASSERT_EQUAL_UINT8(128, half.g);
    TEST_ASSERT_EQUAL_UINT8(128, half.b);

    // NOBLEND (blend=false): blocky steps, index 8 still reads entry 0.
    Rgb noBlend = colorFromPalette(blackToWhite, 8, 255, false);
    TEST_ASSERT_EQUAL_UINT8(0, noBlend.r);
    TEST_ASSERT_EQUAL_UINT8(0, noBlend.g);
    TEST_ASSERT_EQUAL_UINT8(0, noBlend.b);
}

void test_color_from_palette_brightness_scales_down() {
    Rgb full = colorFromPalette(rainbowColors(), 0, 255);
    Rgb dim = colorFromPalette(rainbowColors(), 0, 128);
    TEST_ASSERT_EQUAL_UINT8(255, full.r);
    TEST_ASSERT_TRUE(dim.r < full.r);
    Rgb off = colorFromPalette(rainbowColors(), 0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, off.r);
}

void test_stock_palettes_have_the_documented_endpoints() {
    // Spot-check a couple of the ported stock palettes against the vendored
    // colorpalettes.cpp source values.
    TEST_ASSERT_EQUAL_UINT8(0xFF, rainbowColors().entries[0].r);
    TEST_ASSERT_EQUAL_UINT8(0x00, rainbowColors().entries[0].g);
    TEST_ASSERT_EQUAL_UINT8(0x00, rainbowColors().entries[0].b);

    // HeatColors_p[15] == 0xFFFFFF (white, the hot end).
    TEST_ASSERT_EQUAL_UINT8(255, heatColors().entries[15].r);
    TEST_ASSERT_EQUAL_UINT8(255, heatColors().entries[15].g);
    TEST_ASSERT_EQUAL_UINT8(255, heatColors().entries[15].b);

    // HeatColors_p[0] == 0x000000 (black, the cold end).
    TEST_ASSERT_EQUAL_UINT8(0, heatColors().entries[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, heatColors().entries[0].g);
    TEST_ASSERT_EQUAL_UINT8(0, heatColors().entries[0].b);
}

// --- fade / blend helpers ------------------------------------------------

void test_fade_to_black_by_scales_toward_black() {
    std::vector<Rgb> leds = {Rgb{200, 100, 50}};
    fadeToBlackBy(leds, 128);  // scale = 255-128 = 127 -> ~half
    TEST_ASSERT_EQUAL_UINT8(100, leds[0].r);
    TEST_ASSERT_EQUAL_UINT8(50, leds[0].g);
    TEST_ASSERT_EQUAL_UINT8(25, leds[0].b);

    fadeToBlackBy(leds, 255);  // full fade -> exactly black
    TEST_ASSERT_EQUAL_UINT8(0, leds[0].r);
    TEST_ASSERT_EQUAL_UINT8(0, leds[0].g);
    TEST_ASSERT_EQUAL_UINT8(0, leds[0].b);
}

void test_nblend_zero_and_full_amount_are_noop_and_replace() {
    Rgb a{100, 50, 200};
    Rgb overlay{200, 200, 0};

    Rgb unchanged = a;
    nblend(unchanged, overlay, 0);
    TEST_ASSERT_EQUAL_UINT8(a.r, unchanged.r);
    TEST_ASSERT_EQUAL_UINT8(a.g, unchanged.g);
    TEST_ASSERT_EQUAL_UINT8(a.b, unchanged.b);

    Rgb replaced = a;
    nblend(replaced, overlay, 255);
    TEST_ASSERT_EQUAL_UINT8(overlay.r, replaced.r);
    TEST_ASSERT_EQUAL_UINT8(overlay.g, replaced.g);
    TEST_ASSERT_EQUAL_UINT8(overlay.b, replaced.b);
}

void test_nblend_partial_amount_lands_between() {
    Rgb a{100, 50, 200};
    Rgb overlay{200, 200, 0};
    nblend(a, overlay, 128);
    TEST_ASSERT_EQUAL_UINT8(150, a.r);
    TEST_ASSERT_EQUAL_UINT8(125, a.g);
    TEST_ASSERT_EQUAL_UINT8(100, a.b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sin8_pinned_cardinal_values);
    RUN_TEST(test_cos8_is_sin8_shifted_by_quarter_turn);
    RUN_TEST(test_sin16_cos16_zero_and_quarter_turn);
    RUN_TEST(test_scale8_pinned_values);
    RUN_TEST(test_scale8_video_never_zeroes_a_lit_nonzero_input);
    RUN_TEST(test_scale16_full_scale_is_identity);
    RUN_TEST(test_qadd8_saturates_at_255);
    RUN_TEST(test_qsub8_saturates_at_0);
    RUN_TEST(test_avg8_rounds_down);
    RUN_TEST(test_lerp8by8_endpoints_and_midpoint);
    RUN_TEST(test_triwave8_shape);
    RUN_TEST(test_quadwave8_and_cubicwave8_peak_and_floor);
    RUN_TEST(test_ease8_endpoints_are_identity);
    RUN_TEST(test_beat_functions_take_ms_not_a_global_clock);
    RUN_TEST(test_beat8_at_ms_zero_is_zero);
    RUN_TEST(test_beatsin8_one_full_period_returns_to_start);
    RUN_TEST(test_hsv2rgb_rainbow_pure_hues);
    RUN_TEST(test_hsv2rgb_rainbow_zero_saturation_is_white);
    RUN_TEST(test_hsv2rgb_rainbow_zero_value_is_black);
    RUN_TEST(test_heat_color_endpoints);
    RUN_TEST(test_heat_color_middle_is_orange);
    RUN_TEST(test_color_from_palette_index0_is_entry0_exact);
    RUN_TEST(test_color_from_palette_index8_is_halfway_blend);
    RUN_TEST(test_color_from_palette_brightness_scales_down);
    RUN_TEST(test_stock_palettes_have_the_documented_endpoints);
    RUN_TEST(test_fade_to_black_by_scales_toward_black);
    RUN_TEST(test_nblend_zero_and_full_amount_are_noop_and_replace);
    RUN_TEST(test_nblend_partial_amount_lands_between);
    return UNITY_END();
}
