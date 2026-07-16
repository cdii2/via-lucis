#pragma once
// TwinkleFox, ported from FastLED 3.10.3 (MIT, https://github.com/FastLED/FastLED,
// tag 3.10.3): examples/TwinkleFox/TwinkleFox.ino (Mark Kriegsman, December 2015).
// Ported for native testability and the injected-Rng determinism contract (VL7)
// — see fx/rng.h and fx/effect.h.
//
// Ports drawTwinkles()/computeOneTwinkle()/attackDecayWave8()/
// coolLikeIncandescent() essentially verbatim (arithmetic unchanged, including
// the intra-step 16-bit truncations). Deviations from the sketch's globals:
//
//  - TWINKLE_SPEED (4) and TWINKLE_DENSITY (5) are pinned to the sketch's own
//    documented defaults ("4, 5, and 6 are recommended... default is 4" /
//    "Default is 5").
//  - AUTO_SELECT_BACKGROUND_COLOR is fixed 0, so gBackgroundColor is always
//    Black — the sketch's own literal default (`CRGB gBackgroundColor =
//    CRGB::Black;`), not the commented-out dim-FairyLight example. The
//    auto-dim-from-palette-entries-0/1 branch that AUTO_SELECT gates is dead
//    code under that flag and is not ported.
//  - COOL_LIKE_INCANDESCENT is fixed 1 (the sketch's default): colors fade
//    toward red as they dim.
//  - The sketch's `EVERY_N_SECONDS(30) chooseNextColorPalette()` +
//    `nblendPaletteTowardPalette()` rotation is NOT ported. Via Lucis's one
//    color primitive is the injected Palette16 (brief §3, effect.h) —
//    setPalette() stands in for the whole rotation; callers that want a
//    "TwinkleFox palette tour" would call setPalette() themselves on a
//    slower cadence. Default palette (before setPalette() is called) is
//    OceanColors_p, one of the sketch's own ActivePaletteList entries.
//  - The sketch reseeds its local `PRNG16` LCG to the fixed literal 11337 on
//    every drawTwinkles() call, so every pixel's clock-offset/speed/salt
//    triple is the same sequence every frame, every run, forever — that is
//    what makes different *pixels* look independently random while the
//    *show* itself never varies run to run. Via Lucis needs different seeds
//    to produce different shows (the determinism contract, VL7): the
//    per-frame LCG reset value is derived once from the injected `seed` in
//    reset() instead of hardcoded to 11337. Everything downstream of that
//    reset value is the sketch's arithmetic, unchanged.
//  - clock32 is f.ms (the fixed-timestep frame clock), never millis().

#include <cstdint>
#include <vector>

#include "vialucis/fx/effect.h"
#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/fx/palette.h"
#include "vialucis/fx/rng.h"

namespace vialucis {
namespace fx {

class TwinkleFoxFx : public Effect {
public:
    // Overall twinkle speed, 0 (very slow) to 8 (very fast).
    static constexpr uint8_t kTwinkleSpeed = 4;
    // Overall twinkle density, 0 (none lit) to 8 (all lit at once).
    static constexpr uint8_t kTwinkleDensity = 5;

    void reset(uint32_t seed, uint16_t ledCount) override {
        ledCount_ = ledCount;
        // Derive the per-frame LCG reset value from the injected seed (see
        // file header) instead of the sketch's hardcoded 11337.
        Rng seedRng(seed);
        prng16Start_ = seedRng.random16();
    }

    void render(FxFrame& f) override {
        // "PRNG16" must reset to the same starting value every call so the
        // per-pixel clock/speed/salt sequence is stable frame to frame.
        uint16_t prng16 = prng16Start_;
        uint32_t clock32 = f.ms;

        // gBackgroundColor = CRGB::Black (AUTO_SELECT_BACKGROUND_COLOR == 0,
        // see file header — the auto-dim branch never runs).
        const Rgb bg{0, 0, 0};
        uint8_t backgroundBrightness = averageLight(bg);

        // A162 (§3-E item 10): bound against f.leds.size() too (fire2012.h
        // note on the same guard).
        for (uint16_t i = 0; i < ledCount_ && i < f.leds.size(); ++i) {
            prng16 = static_cast<uint16_t>(prng16 * 2053u + 1384u);
            uint16_t myClockOffset16 = prng16;
            prng16 = static_cast<uint16_t>(prng16 * 2053u + 1384u);
            // Clock speed adjustment, in eighths: 8/8ths (1x) to 23/8ths
            // (~2.9x).
            uint8_t mySpeedMultiplierQ5_3 = static_cast<uint8_t>(
                ((((prng16 & 0xFFu) >> 4) + (prng16 & 0x0Fu)) & 0x0Fu) +
                0x08u);
            uint32_t myClock30 =
                static_cast<uint32_t>((clock32 * mySpeedMultiplierQ5_3) >>
                                       3) +
                myClockOffset16;
            uint8_t myUnique8 = static_cast<uint8_t>(prng16 >> 8);

            Rgb c = computeOneTwinkle(myClock30, myUnique8);

            uint8_t cbright = averageLight(c);
            int16_t deltaBright = static_cast<int16_t>(cbright) -
                                   static_cast<int16_t>(backgroundBrightness);

            if (deltaBright >= 32 || isBlack(bg)) {
                // Significantly brighter than the background (or there is no
                // background to speak of) — use the twinkle color as-is.
                f.leds[i] = c;
            } else if (deltaBright > 0) {
                // Slightly brighter — blend a proportion of the twinkle
                // color into the background.
                Rgb blended = bg;
                nblend(blended, c, static_cast<uint8_t>(deltaBright * 8));
                f.leds[i] = blended;
            } else {
                // Not brighter than the background at all.
                f.leds[i] = bg;
            }
        }
    }

    void setPalette(const Palette16& p) override { palette_ = p; }
    // A158 (§3-E item 6): back to the built-in default (oceanColors, see the
    // file header) — the state before any setPalette() call.
    void resetPalette() override { palette_ = oceanColors(); }

private:
    static bool isBlack(const Rgb& c) {
        return c.r == 0 && c.g == 0 && c.b == 0;
    }

    // CRGB::getAverageLight(), FASTLED_SCALE8_FIXED==1 variant (FastLED's
    // default): the *_LEAVING_R1_DIRTY calls collapse to plain scale8() off
    // AVR (see fx_color.h's attribution note for the same collapse rule).
    static uint8_t averageLight(const Rgb& c) {
        return static_cast<uint8_t>(scale8(c.r, 85) + scale8(c.g, 85) +
                                     scale8(c.b, 85));
    }

    // A triangle wave with a faster attack and a slower decay than
    // triwave8(): climbs steeply for the first third, then eases back down
    // over the remaining two-thirds.
    static uint8_t attackDecayWave8(uint8_t i) {
        if (i < 86) {
            return static_cast<uint8_t>(i * 3);
        }
        i = static_cast<uint8_t>(i - 86);
        return static_cast<uint8_t>(255 - (i + i / 2));
    }

    // If in the 'fading down' part of the cycle, nudges the color a little
    // toward red, the way incandescent bulbs do as they dim.
    static void coolLikeIncandescent(Rgb& c, uint8_t phase) {
        if (phase < 128) {
            return;
        }
        uint8_t cooling = static_cast<uint8_t>((phase - 128) >> 4);
        c.g = qsub8(c.g, cooling);
        c.b = qsub8(c.b, static_cast<uint8_t>(cooling * 2));
    }

    // brightness = f(time), hue = f(time). The low digits of `ms` drive the
    // brightness wave; the high digits pick a color (so color is stable
    // across one fade-in/fade-out cycle) and gate whether this pixel lights
    // at all this cycle, per TWINKLE_DENSITY.
    Rgb computeOneTwinkle(uint32_t ms, uint8_t salt) const {
        uint16_t ticks = static_cast<uint16_t>(ms >> (8 - kTwinkleSpeed));
        uint8_t fastCycle8 = static_cast<uint8_t>(ticks);
        uint16_t slowCycle16 = static_cast<uint16_t>((ticks >> 8) + salt);
        slowCycle16 = static_cast<uint16_t>(
            slowCycle16 + sin8(static_cast<uint8_t>(slowCycle16)));
        slowCycle16 = static_cast<uint16_t>(slowCycle16 * 2053u + 1384u);
        uint8_t slowCycle8 = static_cast<uint8_t>((slowCycle16 & 0xFFu) +
                                                    (slowCycle16 >> 8));

        uint8_t bright = 0;
        if (((slowCycle8 & 0x0Eu) / 2) < kTwinkleDensity) {
            bright = attackDecayWave8(fastCycle8);
        }

        uint8_t hue = static_cast<uint8_t>(slowCycle8 - salt);
        Rgb c{0, 0, 0};
        if (bright > 0) {
            c = colorFromPalette(palette_, hue, bright, /*blend=*/false);
            coolLikeIncandescent(c, fastCycle8);
        }
        return c;
    }

    Palette16 palette_ = oceanColors();
    uint16_t ledCount_ = 0;
    uint16_t prng16Start_ = 0;
};

}  // namespace fx
}  // namespace vialucis
