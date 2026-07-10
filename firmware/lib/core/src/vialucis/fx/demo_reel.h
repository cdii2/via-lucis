#pragma once
// Ported from FastLED 3.10.3 examples/DemoReel100/DemoReel100.ino (MIT,
// github.com/FastLED/FastLED tag 3.10.3). Deterministic port (VL7):
// injected Rng, explicit-ms beats.
//
// The five per-pattern functions (rainbow/confetti/sinelon/juggle/bpm) from
// the sketch, each promoted to an Effect subclass under the effect seam
// (effect.h): fixed timestep via FxFrame::ms, zero allocation in render(),
// randomness drawn from an injected vialucis::fx::Rng instead of FastLED's
// global random8()/random16(), and "gHue" (the sketch's free-running global
// that EVERY_N_MILLISECONDS(20){ gHue++; } advances once per loop()) derived
// per-frame as f.ms/20 instead of a wall-clock counter.
//
// CRGB operator semantics ported explicitly, since these effects have no
// CRGB/CHSV types to lean on:
//   leds[i] = CHSV(...)   -> hsv2rgbRainbow(hsv, leds[i])          (assign)
//   leds[i] += CHSV(...)  -> hsv2rgbRainbow(hsv, tmp); qadd8 each channel
//   leds[i] |= CHSV(...)  -> hsv2rgbRainbow(hsv, tmp); channel-wise max
// (CRGB's assignment-from-CHSV and += both go through FastLED's default
// hsv2rgb_dispatch, which is hsv2rgb_rainbow unless FASTLED_HSV_CONVERSION_
// SPECTRUM is defined — it isn't, here or in the vendored FastLED build.)

#include <cstdint>

#include "vialucis/fx/effect.h"
#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/fx/palette.h"
#include "vialucis/fx/rng.h"

namespace vialucis {
namespace fx {

namespace demo_reel_detail {

// gHue: the sketch's EVERY_N_MILLISECONDS(20){ gHue++; } free-running hue,
// re-derived from elapsed fixed-timestep ms (VL7 — no wall clock).
inline uint8_t gHueAt(uint32_t ms) { return static_cast<uint8_t>(ms / 20); }

// leds[i] += CHSV(h,s,v): convert via hsv2rgbRainbow, then qadd8 per
// channel onto the existing pixel (ports CRGB::operator+=(const CRGB&),
// fed by the implicit CHSV->CRGB conversion, which saturates at 0xFF).
inline void addHsv(Rgb& px, const Hsv& hsv) {
    Rgb add;
    hsv2rgbRainbow(hsv, add);
    px.r = qadd8(px.r, add.r);
    px.g = qadd8(px.g, add.g);
    px.b = qadd8(px.b, add.b);
}

// leds[i] |= CHSV(h,s,v): convert via hsv2rgbRainbow, then take the
// per-channel max (ports CRGB::operator|=(const CRGB&)).
inline void orHsv(Rgb& px, const Hsv& hsv) {
    Rgb add;
    hsv2rgbRainbow(hsv, add);
    if (add.r > px.r) px.r = add.r;
    if (add.g > px.g) px.g = add.g;
    if (add.b > px.b) px.b = add.b;
}

}  // namespace demo_reel_detail

// rainbow(): fill_rainbow(leds, NUM_LEDS, gHue, 7) — ported byte-for-byte
// from fl::fill_rainbow (fl/fill.cpp): hue starts at gHue, sat is FastLED's
// fixed 240, val is 255, hue advances by deltahue=7 (wrapping uint8_t) per
// LED.
class RainbowFx : public Effect {
public:
    void reset(uint32_t seed, uint16_t ledCount) override {
        rng_.reseed(seed);  // unused by this effect (no randomness in the
                             // sketch's rainbow()) — seeded for interface
                             // uniformity across the demo-reel set.
        ledCount_ = ledCount;
    }

    void render(FxFrame& f) override {
        uint16_t n = boundedCount(f);
        Hsv hsv{demo_reel_detail::gHueAt(f.ms), 240, 255};
        for (uint16_t i = 0; i < n; ++i) {
            hsv2rgbRainbow(hsv, f.leds[i]);
            hsv.h = static_cast<uint8_t>(hsv.h + 7);
        }
    }

private:
    uint16_t boundedCount(const FxFrame& f) const {
        uint16_t cap = static_cast<uint16_t>(f.leds.size());
        return ledCount_ < cap ? ledCount_ : cap;
    }

    Rng rng_;
    uint16_t ledCount_ = 0;
};

// confetti(): fadeToBlackBy(leds, NUM_LEDS, 10); one random speckle per
// frame at leds[random16(NUM_LEDS)] += CHSV(gHue+random8(64), 200, 255).
class ConfettiFx : public Effect {
public:
    void reset(uint32_t seed, uint16_t ledCount) override {
        rng_.reseed(seed);
        ledCount_ = ledCount;
    }

    void render(FxFrame& f) override {
        uint16_t n = boundedCount(f);
        fadeToBlackBy(f.leds, 10);
        if (n == 0) return;
        uint16_t pos = rng_.random16(n);
        uint8_t hue = static_cast<uint8_t>(demo_reel_detail::gHueAt(f.ms) +
                                            rng_.random8(64));
        demo_reel_detail::addHsv(f.leds[pos], Hsv{hue, 200, 255});
    }

private:
    uint16_t boundedCount(const FxFrame& f) const {
        uint16_t cap = static_cast<uint16_t>(f.leds.size());
        return ledCount_ < cap ? ledCount_ : cap;
    }

    Rng rng_;
    uint16_t ledCount_ = 0;
};

// sinelon(): fadeToBlackBy(leds, NUM_LEDS, 20); a dot at
// beatsin16(13, 0, NUM_LEDS-1) += CHSV(gHue, 255, 192).
class SinelonFx : public Effect {
public:
    void reset(uint32_t seed, uint16_t ledCount) override {
        rng_.reseed(seed);  // unused (sinelon() draws no randomness) —
                             // seeded for interface uniformity.
        ledCount_ = ledCount;
    }

    void render(FxFrame& f) override {
        uint16_t n = boundedCount(f);
        fadeToBlackBy(f.leds, 20);
        if (n == 0) return;
        uint16_t pos = beatsin16(13, 0, static_cast<uint16_t>(n - 1), f.ms);
        demo_reel_detail::addHsv(
            f.leds[pos], Hsv{demo_reel_detail::gHueAt(f.ms), 255, 192});
    }

private:
    uint16_t boundedCount(const FxFrame& f) const {
        uint16_t cap = static_cast<uint16_t>(f.leds.size());
        return ledCount_ < cap ? ledCount_ : cap;
    }

    Rng rng_;
    uint16_t ledCount_ = 0;
};

// juggle(): fadeToBlackBy(leds, NUM_LEDS, 20); eight dots at
// beatsin16(i+7, 0, NUM_LEDS-1) |= CHSV(dothue, 200, 255), dothue += 32
// each dot (wrapping uint8_t).
class JuggleFx : public Effect {
public:
    void reset(uint32_t seed, uint16_t ledCount) override {
        rng_.reseed(seed);  // unused (juggle() draws no randomness) —
                             // seeded for interface uniformity.
        ledCount_ = ledCount;
    }

    void render(FxFrame& f) override {
        uint16_t n = boundedCount(f);
        fadeToBlackBy(f.leds, 20);
        if (n == 0) return;
        uint8_t dothue = 0;
        for (int i = 0; i < 8; ++i) {
            uint16_t pos = beatsin16(static_cast<accum88>(i + 7), 0,
                                      static_cast<uint16_t>(n - 1), f.ms);
            demo_reel_detail::orHsv(f.leds[pos], Hsv{dothue, 200, 255});
            dothue = static_cast<uint8_t>(dothue + 32);
        }
    }

private:
    uint16_t boundedCount(const FxFrame& f) const {
        uint16_t cap = static_cast<uint16_t>(f.leds.size());
        return ledCount_ < cap ? ledCount_ : cap;
    }

    Rng rng_;
    uint16_t ledCount_ = 0;
};

// bpm(): pulsing stripes at 62 BPM through PartyColors_p —
// beat = beatsin8(62, 64, 255); leds[i] = ColorFromPalette(palette,
// gHue+(i*2), beat-gHue+(i*10)). Palette defaults to PartyColors_p (the
// sketch's hardcoded choice) but honors setPalette() like the other
// palette-driven effects in this seam (VL7 extension, not in the sketch).
class BpmFx : public Effect {
public:
    static constexpr uint8_t kBeatsPerMinute = 62;

    void reset(uint32_t seed, uint16_t ledCount) override {
        rng_.reseed(seed);  // unused (bpm() draws no randomness) — seeded
                             // for interface uniformity.
        ledCount_ = ledCount;
        palette_ = partyColors();
    }

    void render(FxFrame& f) override {
        uint16_t n = boundedCount(f);
        uint8_t gHue = demo_reel_detail::gHueAt(f.ms);
        uint8_t beat = beatsin8(kBeatsPerMinute, 64, 255, f.ms);
        for (uint16_t i = 0; i < n; ++i) {
            uint8_t index =
                static_cast<uint8_t>(gHue + static_cast<int>(i) * 2);
            uint8_t brightness = static_cast<uint8_t>(
                static_cast<int>(beat) - static_cast<int>(gHue) +
                static_cast<int>(i) * 10);
            f.leds[i] = colorFromPalette(palette_, index, brightness);
        }
    }

    void setPalette(const Palette16& p) override { palette_ = p; }

private:
    uint16_t boundedCount(const FxFrame& f) const {
        uint16_t cap = static_cast<uint16_t>(f.leds.size());
        return ledCount_ < cap ? ledCount_ : cap;
    }

    Rng rng_;
    uint16_t ledCount_ = 0;
    Palette16 palette_;
};

}  // namespace fx
}  // namespace vialucis
