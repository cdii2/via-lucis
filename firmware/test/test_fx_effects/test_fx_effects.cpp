// Regression tests for the Effect ports living under vialucis::fx (the
// effect seam — effect.h). Each ported effect gets one block below shaped
// the same way: determinism, bounds, alloc-by-construction, non-black.
//
// This file is shared across multiple porting efforts — each new effect
// (or effect family, e.g. demo_reel.h's five) adds its own "=== <Name> ==="
// block using the shared helpers just below, and registers its RUN_TESTs in
// main() under a matching comment banner. Please append, don't reorder or
// rewrite existing blocks.

#include <unity.h>

#include <vector>

#include "vialucis/fx/color_waves.h"
#include "vialucis/fx/demo_reel.h"
#include "vialucis/fx/fire2012.h"
#include "vialucis/fx/pacifica.h"
#include "vialucis/fx/palette.h"
#include "vialucis/fx/twinkle_fox.h"

using namespace vialucis;
using fx::FxFrame;
using fx::kFxStepMs;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

namespace {

// Resets a fresh EffectT with `seed`/`ledCount`, renders `frameCount` fixed
// timesteps, and returns every pixel of every frame concatenated — a single
// buffer whose byte-for-byte equality is the determinism contract (VL7).
template <typename EffectT>
std::vector<Rgb> runFrames(uint32_t seed, uint16_t ledCount,
                            uint32_t frameCount) {
    EffectT effect;
    effect.reset(seed, ledCount);
    std::vector<Rgb> leds(ledCount);
    std::vector<Rgb> allPixels;
    allPixels.reserve(static_cast<size_t>(frameCount) * ledCount);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        FxFrame f{leds, frame, frame * kFxStepMs};
        effect.render(f);
        for (const Rgb& px : leds) allPixels.push_back(px);
    }
    return allPixels;
}

bool framesEqual(const std::vector<Rgb>& a, const std::vector<Rgb>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b)
            return false;
    }
    return true;
}

bool anyLit(const std::vector<Rgb>& pixels) {
    for (const Rgb& px : pixels) {
        if (px.r || px.g || px.b) return true;
    }
    return false;
}

// Bounds + "zero-steady-state-alloc" check: runs `frameCount` frames over a
// strip sized exactly `ledCount` and asserts the run completes with the LED
// buffer still exactly that size. render() is contractually zero-allocation
// (effect.h) — by construction, since every port here touches only fixed-
// size members and the caller-owned `leds` vector, never a container of its
// own; that's not independently measurable from inside Unity, so this test
// instead exercises the volume (100 renders) that would surface a growing
// buffer or an out-of-range write (UB the OS/sanitizer would catch) if one
// existed.
template <typename EffectT>
void assertRunsCleanAt(uint16_t ledCount, uint32_t frameCount) {
    EffectT effect;
    effect.reset(/*seed=*/7, ledCount);
    std::vector<Rgb> leds(ledCount);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        FxFrame f{leds, frame, frame * kFxStepMs};
        effect.render(f);
        TEST_ASSERT_EQUAL_size_t(ledCount, leds.size());
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// === RainbowFx === (fill_rainbow — no randomness; seed doesn't affect
// output, only elapsed ms does)
// ---------------------------------------------------------------------------

void test_rainbow_same_seed_is_deterministic() {
    auto a = runFrames<fx::RainbowFx>(1, 60, 50);
    auto b = runFrames<fx::RainbowFx>(1, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_rainbow_bounds_60_and_360() {
    assertRunsCleanAt<fx::RainbowFx>(60, 100);
    assertRunsCleanAt<fx::RainbowFx>(360, 100);
}

void test_rainbow_lights_up() {
    auto pixels = runFrames<fx::RainbowFx>(1, 60, 5);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

// ---------------------------------------------------------------------------
// === ConfettiFx === (one random sparkle per frame — the one effect here
// whose output depends on seed)
// ---------------------------------------------------------------------------

void test_confetti_same_seed_is_deterministic() {
    auto a = runFrames<fx::ConfettiFx>(42, 60, 50);
    auto b = runFrames<fx::ConfettiFx>(42, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_confetti_different_seeds_differ() {
    auto a = runFrames<fx::ConfettiFx>(1, 60, 50);
    auto b = runFrames<fx::ConfettiFx>(2, 60, 50);
    TEST_ASSERT_FALSE(framesEqual(a, b));
}

void test_confetti_bounds_60_and_360() {
    assertRunsCleanAt<fx::ConfettiFx>(60, 100);
    assertRunsCleanAt<fx::ConfettiFx>(360, 100);
}

void test_confetti_lights_up() {
    auto pixels = runFrames<fx::ConfettiFx>(1, 60, 10);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

// ---------------------------------------------------------------------------
// === SinelonFx === (beatsin16-driven dot — no randomness)
// ---------------------------------------------------------------------------

void test_sinelon_same_seed_is_deterministic() {
    auto a = runFrames<fx::SinelonFx>(1, 60, 50);
    auto b = runFrames<fx::SinelonFx>(1, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_sinelon_bounds_60_and_360() {
    assertRunsCleanAt<fx::SinelonFx>(60, 100);
    assertRunsCleanAt<fx::SinelonFx>(360, 100);
}

void test_sinelon_lights_up() {
    auto pixels = runFrames<fx::SinelonFx>(1, 60, 5);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

// ---------------------------------------------------------------------------
// === JuggleFx === (eight beatsin16 dots — no randomness)
// ---------------------------------------------------------------------------

void test_juggle_same_seed_is_deterministic() {
    auto a = runFrames<fx::JuggleFx>(1, 60, 50);
    auto b = runFrames<fx::JuggleFx>(1, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_juggle_bounds_60_and_360() {
    assertRunsCleanAt<fx::JuggleFx>(60, 100);
    assertRunsCleanAt<fx::JuggleFx>(360, 100);
}

void test_juggle_lights_up() {
    auto pixels = runFrames<fx::JuggleFx>(1, 60, 5);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

// ---------------------------------------------------------------------------
// === BpmFx === (PartyColors pulse at 62 BPM — no randomness)
// ---------------------------------------------------------------------------

void test_bpm_same_seed_is_deterministic() {
    auto a = runFrames<fx::BpmFx>(1, 60, 50);
    auto b = runFrames<fx::BpmFx>(1, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_bpm_bounds_60_and_360() {
    assertRunsCleanAt<fx::BpmFx>(60, 100);
    assertRunsCleanAt<fx::BpmFx>(360, 100);
}

void test_bpm_lights_up() {
    auto pixels = runFrames<fx::BpmFx>(1, 60, 5);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

// ---------------------------------------------------------------------------
// === Fire2012Fx === (FastLED examples/Fire2012/Fire2012.ino — heat buffer +
// random cooling/sparking; the one demo-reel-shaped effect that needs real
// warmup time before it lights, since it starts from an all-zero heat[])
// ---------------------------------------------------------------------------

void test_fire2012_same_seed_is_deterministic() {
    auto a = runFrames<fx::Fire2012Fx>(1234, 60, 50);
    auto b = runFrames<fx::Fire2012Fx>(1234, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_fire2012_different_seeds_differ() {
    auto a = runFrames<fx::Fire2012Fx>(1234, 60, 50);
    auto b = runFrames<fx::Fire2012Fx>(9999, 60, 50);
    TEST_ASSERT_FALSE(framesEqual(a, b));
}

void test_fire2012_bounds_60_and_360() {
    assertRunsCleanAt<fx::Fire2012Fx>(60, 100);
    assertRunsCleanAt<fx::Fire2012Fx>(360, 100);
}

void test_fire2012_lights_up_after_warmup() {
    // SPARKING=120/255 per frame, so 30 frames is generous warmup before
    // requiring at least one lit (non-black) LED.
    auto pixels = runFrames<fx::Fire2012Fx>(7, 60, 30);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

void test_fire2012_bottom_runs_hotter_than_top_on_average() {
    // The whole point of the simulation: heat cools as it drifts "up" the
    // strip, so averaged over many frames the bottom 10 LEDs should read
    // brighter (hotter) than the top 10.
    fx::Fire2012Fx effect;
    const uint16_t ledCount = 60;
    effect.reset(2024, ledCount);
    std::vector<Rgb> leds(ledCount);

    uint64_t bottomSum = 0;
    uint64_t topSum = 0;
    const uint32_t frames = 100;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        FxFrame f{leds, frame, frame * kFxStepMs};
        effect.render(f);
        for (uint16_t i = 0; i < 10; ++i) {
            bottomSum += static_cast<uint32_t>(leds[i].r) + leds[i].g +
                         leds[i].b;
        }
        for (uint16_t i = ledCount - 10; i < ledCount; ++i) {
            topSum += static_cast<uint32_t>(leds[i].r) + leds[i].g +
                      leds[i].b;
        }
    }

    TEST_ASSERT_TRUE(bottomSum > topSum);
}

// ---------------------------------------------------------------------------
// === TwinkleFoxFx === (FastLED examples/TwinkleFox/TwinkleFox.ino —
// per-pixel PRNG16 clock/salt, no per-pixel RAM; setPalette is the one color
// primitive it honors, per the file header's palette-rotation deviation)
// ---------------------------------------------------------------------------

void test_twinklefox_same_seed_is_deterministic() {
    auto a = runFrames<fx::TwinkleFoxFx>(1234, 60, 50);
    auto b = runFrames<fx::TwinkleFoxFx>(1234, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_twinklefox_different_seeds_differ() {
    auto a = runFrames<fx::TwinkleFoxFx>(1234, 60, 50);
    auto b = runFrames<fx::TwinkleFoxFx>(9999, 60, 50);
    TEST_ASSERT_FALSE(framesEqual(a, b));
}

void test_twinklefox_bounds_60_and_360() {
    assertRunsCleanAt<fx::TwinkleFoxFx>(60, 100);
    assertRunsCleanAt<fx::TwinkleFoxFx>(360, 100);
}

void test_twinklefox_lights_up_after_warmup() {
    auto pixels = runFrames<fx::TwinkleFoxFx>(7, 60, 30);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

void test_twinklefox_set_palette_changes_output() {
    // Same seed/frames, two disjoint single-color palettes -> different
    // frame histories, and every lit pixel from the red-only palette must
    // carry zero blue (coolLikeIncandescent only ever subtracts g/b, never
    // introduces a channel the palette didn't provide).
    fx::Palette16 redOnly;
    for (auto& e : redOnly.entries) e = Rgb{255, 0, 0};
    fx::Palette16 blueOnly;
    for (auto& e : blueOnly.entries) e = Rgb{0, 0, 255};

    fx::TwinkleFoxFx a;
    a.setPalette(redOnly);
    fx::TwinkleFoxFx b;
    b.setPalette(blueOnly);

    std::vector<Rgb> ledsA(60);
    std::vector<Rgb> ledsB(60);
    a.reset(1234, 60);
    b.reset(1234, 60);
    std::vector<Rgb> historyA;
    std::vector<Rgb> historyB;
    for (uint32_t frame = 0; frame < 50; ++frame) {
        FxFrame fa{ledsA, frame, frame * kFxStepMs};
        FxFrame fb{ledsB, frame, frame * kFxStepMs};
        a.render(fa);
        b.render(fb);
        for (const Rgb& px : ledsA) historyA.push_back(px);
        for (const Rgb& px : ledsB) historyB.push_back(px);
    }

    TEST_ASSERT_FALSE(framesEqual(historyA, historyB));

    bool checkedAtLeastOneLitPixel = false;
    for (const Rgb& px : historyA) {
        if (px.r || px.g || px.b) {
            TEST_ASSERT_EQUAL_UINT8(0, px.b);
            checkedAtLeastOneLitPixel = true;
        }
    }
    TEST_ASSERT_TRUE(checkedAtLeastOneLitPixel);
}

// ---------------------------------------------------------------------------
// === PacificaFx === (FastLED examples/Pacifica/Pacifica.ino — four
// beatsin-driven wave layers over three custom blue-green palettes; no
// randomness. Pride2015 was intentionally not ported into this repo's
// effect roster.)
// ---------------------------------------------------------------------------

void test_pacifica_same_seed_is_deterministic() {
    auto a = runFrames<fx::PacificaFx>(1, 60, 50);
    auto b = runFrames<fx::PacificaFx>(1, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_pacifica_bounds_60_and_360() {
    assertRunsCleanAt<fx::PacificaFx>(60, 100);
    assertRunsCleanAt<fx::PacificaFx>(360, 100);
}

void test_pacifica_lights_up() {
    auto pixels = runFrames<fx::PacificaFx>(1, 60, 5);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

void test_pacifica_stays_blue_green_dominant() {
    // The whole point of the effect: three of its four custom palettes carry
    // near-zero red, the background fill is Rgb{2,6,10}, and
    // pacifica_deepen_colors() explicitly deepens blue/green — so averaged
    // over many frames, blue+green should dominate red.
    fx::PacificaFx effect;
    const uint16_t ledCount = 60;
    effect.reset(1, ledCount);
    std::vector<Rgb> leds(ledCount);

    uint64_t sumR = 0, sumG = 0, sumB = 0;
    const uint32_t frames = 100;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        FxFrame f{leds, frame, frame * kFxStepMs};
        effect.render(f);
        for (const Rgb& px : leds) {
            sumR += px.r;
            sumG += px.g;
            sumB += px.b;
        }
    }

    TEST_ASSERT_TRUE((sumB + sumG) > sumR);
}

// ---------------------------------------------------------------------------
// === ColorWavesFx === (derived from the Pride2015 example's wave math —
// see color_waves.h for the OV3 provenance note; the only deviation from
// that math is the final color source: colorFromPalette(index=hue8,
// brightness=bri8) instead of hsv2rgbRainbow. setPalette is the one color
// primitive it honors, defaulting to rainbowColors().)
// ---------------------------------------------------------------------------

void test_color_waves_same_seed_is_deterministic() {
    auto a = runFrames<fx::ColorWavesFx>(7, 60, 50);
    auto b = runFrames<fx::ColorWavesFx>(7, 60, 50);
    TEST_ASSERT_TRUE(framesEqual(a, b));
}

void test_color_waves_bounds_60_and_360() {
    assertRunsCleanAt<fx::ColorWavesFx>(60, 100);
    assertRunsCleanAt<fx::ColorWavesFx>(360, 100);
}

void test_color_waves_lights_up() {
    auto pixels = runFrames<fx::ColorWavesFx>(1, 60, 5);
    TEST_ASSERT_TRUE(anyLit(pixels));
}

void test_color_waves_set_palette_changes_output() {
    // Lava (near-zero blue, strong red) vs Ocean (strong blue, modest red):
    // averaged over many frames, each palette should skew the channel sums
    // the way its own stock entries do (see palette.cpp).
    fx::ColorWavesFx lavaFx;
    lavaFx.setPalette(fx::lavaColors());
    fx::ColorWavesFx oceanFx;
    oceanFx.setPalette(fx::oceanColors());

    const uint16_t ledCount = 60;
    lavaFx.reset(3, ledCount);
    oceanFx.reset(3, ledCount);
    std::vector<Rgb> lavaLeds(ledCount);
    std::vector<Rgb> oceanLeds(ledCount);

    uint64_t lavaR = 0, lavaB = 0, oceanR = 0, oceanB = 0;
    const uint32_t frames = 100;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        FxFrame lavaFrame{lavaLeds, frame, frame * kFxStepMs};
        lavaFx.render(lavaFrame);
        for (const Rgb& px : lavaLeds) {
            lavaR += px.r;
            lavaB += px.b;
        }

        FxFrame oceanFrame{oceanLeds, frame, frame * kFxStepMs};
        oceanFx.render(oceanFrame);
        for (const Rgb& px : oceanLeds) {
            oceanR += px.r;
            oceanB += px.b;
        }
    }

    TEST_ASSERT_TRUE(lavaR > lavaB);    // Lava skews red-dominated
    TEST_ASSERT_TRUE(oceanB > oceanR);  // Ocean skews blue-dominated
}

// ---------------------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();

    // RainbowFx
    RUN_TEST(test_rainbow_same_seed_is_deterministic);
    RUN_TEST(test_rainbow_bounds_60_and_360);
    RUN_TEST(test_rainbow_lights_up);

    // ConfettiFx
    RUN_TEST(test_confetti_same_seed_is_deterministic);
    RUN_TEST(test_confetti_different_seeds_differ);
    RUN_TEST(test_confetti_bounds_60_and_360);
    RUN_TEST(test_confetti_lights_up);

    // SinelonFx
    RUN_TEST(test_sinelon_same_seed_is_deterministic);
    RUN_TEST(test_sinelon_bounds_60_and_360);
    RUN_TEST(test_sinelon_lights_up);

    // JuggleFx
    RUN_TEST(test_juggle_same_seed_is_deterministic);
    RUN_TEST(test_juggle_bounds_60_and_360);
    RUN_TEST(test_juggle_lights_up);

    // BpmFx
    RUN_TEST(test_bpm_same_seed_is_deterministic);
    RUN_TEST(test_bpm_bounds_60_and_360);
    RUN_TEST(test_bpm_lights_up);

    // Fire2012Fx
    RUN_TEST(test_fire2012_same_seed_is_deterministic);
    RUN_TEST(test_fire2012_different_seeds_differ);
    RUN_TEST(test_fire2012_bounds_60_and_360);
    RUN_TEST(test_fire2012_lights_up_after_warmup);
    RUN_TEST(test_fire2012_bottom_runs_hotter_than_top_on_average);

    // TwinkleFoxFx
    RUN_TEST(test_twinklefox_same_seed_is_deterministic);
    RUN_TEST(test_twinklefox_different_seeds_differ);
    RUN_TEST(test_twinklefox_bounds_60_and_360);
    RUN_TEST(test_twinklefox_lights_up_after_warmup);
    RUN_TEST(test_twinklefox_set_palette_changes_output);

    // PacificaFx
    RUN_TEST(test_pacifica_same_seed_is_deterministic);
    RUN_TEST(test_pacifica_bounds_60_and_360);
    RUN_TEST(test_pacifica_lights_up);
    RUN_TEST(test_pacifica_stays_blue_green_dominant);

    // ColorWavesFx
    RUN_TEST(test_color_waves_same_seed_is_deterministic);
    RUN_TEST(test_color_waves_bounds_60_and_360);
    RUN_TEST(test_color_waves_lights_up);
    RUN_TEST(test_color_waves_set_palette_changes_output);

    // Other effect ports: add your RUN_TESTs here, under a comment banner
    // naming the effect, following the four-test shape above.

    return UNITY_END();
}
