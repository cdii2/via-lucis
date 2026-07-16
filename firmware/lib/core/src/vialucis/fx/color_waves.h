#pragma once
// ColorWaves. Derived from the Pride2015 example (FastLED 3.10.3, MIT) with
// palette lookup replacing the rainbow hue — reimplementation of the
// colorwaves IDEA; no gist code used (OV3).
//
// ColorWaves is not shipped as a FastLED examples/*.ino sketch (unlike
// Pacifica), and the well-known "ColorWaves" sketch that circulates as a
// community gist is off-limits under this repo's permissive-license rule
// (OV3: never copy gist code, only well-known FastLED example/library
// sources). This effect instead reimplements, directly in this file, the
// same wave-integration technique the Pride2015 example uses (the
// sPseudotime/sHue16/deltams accumulation, the beatsin88-driven brightness
// wave, and the final nblend(..., 64) integration that avoids per-pixel
// flicker — see Pride2015.ino, vendored at
// firmware/.pio/libdeps/esp32dev/FastLED/examples/Pride2015/Pride2015.ino)
// and swaps only the color source: where Pride2015's `pride()` maps
// (hue8, sat8, bri8) through CHSV/hsv2rgb_rainbow, ColorWavesFx looks
// `hue8` up directly in the shared Palette16 primitive via colorFromPalette
// (index = hue8, brightness = bri8 — no saturation shaping, kept simple per
// the port task). Palette arrives through the Effect::setPalette seam
// (defaults to rainbowColors(), matching Pride2015's rainbow hue mapping
// until a caller picks something else).

#include <cstdint>

#include "vialucis/fx/effect.h"
#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/fx/palette.h"

namespace vialucis {
namespace fx {

class ColorWavesFx : public Effect {
public:
    void reset(uint32_t seed, uint16_t ledCount) override {
        (void)seed;  // fully deterministic, no randomness used
        ledCount_ = ledCount;
        sPseudotime_ = 0;
        sLastMs16_ = 0;
        sHue16_ = 0;
        // palette_ is intentionally left as-is: setPalette() is a caller
        // concern independent of reset(), and the member initializer already
        // defaults it to rainbowColors() before any setPalette() call.
    }

    void render(FxFrame& f) override {
        const uint32_t ms = f.ms;

        // Same wave math as Pride2015.ino's pride() (brightdepth,
        // brightnessthetainc16, msmultiplier, hueinc16, sPseudotime/sHue16
        // accumulation) — see the file header for the per-term port notes.
        uint8_t brightdepth = beatsin88(341, 96, 224, ms);
        uint16_t brightnessthetainc16 =
            beatsin88(203, (25 * 256), (40 * 256), ms);
        uint8_t msmultiplier = beatsin88(147, 23, 60, ms);

        uint16_t hue16 = sHue16_;
        uint16_t hueinc16 = beatsin88(113, 1, 3000, ms);

        uint16_t ms16 = static_cast<uint16_t>(ms);
        uint16_t deltams = static_cast<uint16_t>(ms16 - sLastMs16_);
        sLastMs16_ = ms16;
        sPseudotime_ = static_cast<uint16_t>(
            sPseudotime_ + deltams * msmultiplier);
        sHue16_ = static_cast<uint16_t>(
            sHue16_ + deltams * beatsin88(400, 5, 9, ms));
        uint16_t brightnesstheta16 = sPseudotime_;

        // A162 (§3-E item 10): bound against f.leds.size() too (fire2012.h
        // note on the same guard) — pixelnumber below derives from ledCount_,
        // so it needs the same bound as the loop index.
        for (uint16_t i = 0; i < ledCount_ && i < f.leds.size(); ++i) {
            hue16 = static_cast<uint16_t>(hue16 + hueinc16);
            uint8_t hue8 = static_cast<uint8_t>(hue16 / 256);

            brightnesstheta16 = static_cast<uint16_t>(brightnesstheta16 +
                                                        brightnessthetainc16);
            uint16_t b16 = static_cast<uint16_t>(
                static_cast<int32_t>(sin16(brightnesstheta16)) + 32768);

            uint32_t bri16 = (static_cast<uint32_t>(b16) *
                               static_cast<uint32_t>(b16)) / 65536u;
            uint8_t bri8 = static_cast<uint8_t>(
                (bri16 * static_cast<uint32_t>(brightdepth)) / 65536u);
            bri8 = static_cast<uint8_t>(bri8 + (255 - brightdepth));

            // Palette lookup replaces Pride2015's hsv2rgbRainbow: index =
            // hue8, brightness = bri8 (kept simple per the port task — no
            // extra triwave/index shaping beyond what the shared hue8
            // already carries).
            Rgb newcolor = colorFromPalette(palette_, hue8, bri8);

            // Reversed index: bounding `i` alone isn't enough here — if the
            // caller ever hands render() a buffer SHORTER than ledCount_,
            // (ledCount_-1-i) can still land past f.leds.size() even while i
            // itself stays in range (A162).
            uint16_t pixelnumber = static_cast<uint16_t>((ledCount_ - 1) - i);
            if (pixelnumber < f.leds.size())
                nblend(f.leds[pixelnumber], newcolor, 64);
        }
    }

    void setPalette(const Palette16& p) override { palette_ = p; }
    // A158 (§3-E item 6): back to the built-in default (rainbowColors, see
    // the file header) — the state before any setPalette() call.
    void resetPalette() override { palette_ = rainbowColors(); }

private:
    uint16_t ledCount_ = 0;
    uint16_t sPseudotime_ = 0;
    uint16_t sLastMs16_ = 0;
    uint16_t sHue16_ = 0;
    Palette16 palette_ = rainbowColors();
};

}  // namespace fx
}  // namespace vialucis
