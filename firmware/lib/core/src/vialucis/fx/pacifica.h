#pragma once
// Pacifica: gentle, blue-green ocean waves. Ported from FastLED 3.10.3's
// examples/Pacifica/Pacifica.ino (MIT, Mark Kriegsman and Mary Corey March,
// December 2019, "For Dan."), vendored at
// firmware/.pio/libdeps/esp32dev/FastLED/examples/Pacifica/Pacifica.ino.
//
// Ported for native testability (VL7/OV3): the original reads FastLED's
// global millis() clock inside pacifica_loop() (via GET_MILLIS()) and inside
// pacifica_add_whitecaps() (via beat8/beatsin8's implicit clock read); here
// every beat*/sin call takes the fixed-timestep FxFrame::ms explicitly
// (fx_math.h's beat*(..., ms) contract), and the "last ms" bookkeeping that
// used to be a function-local `static` becomes an Effect member so it resets
// cleanly with reset(). The four wave-layer accumulators (sCIStart1..4) and
// the math inside pacifica_one_layer / pacifica_add_whitecaps /
// pacifica_deepen_colors are otherwise a line-for-line port, including the
// original's 16/32-bit truncating arithmetic (so results wrap identically).
//
// CRGB operator quirks replicated explicitly (vialucis::Rgb has no
// operators):
//   - `leds[i] += c`            -> qadd8 per channel (saturating add).
//   - `leds[i] |= CRGB(2,5,7)`  -> per-channel max (CRGB::operator|= keeps
//     the larger of the two channel values; it is NOT bitwise OR).
//   - `CRGB::getAverageLight()` -> scale8(r,85)+scale8(g,85)+scale8(b,85)
//     (FASTLED_SCALE8_FIXED==1 constant, which is FastLED's default build).

#include <cstdint>
#include <vector>

#include "vialucis/fx/effect.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/fx/palette.h"

namespace vialucis {
namespace fx {

namespace pacifica_detail {

inline Rgb hexColor(uint32_t hex) {
    return Rgb{static_cast<uint8_t>(hex >> 16), static_cast<uint8_t>(hex >> 8),
               static_cast<uint8_t>(hex)};
}

// Three custom blue-green palettes, ported byte-for-byte from the sketch's
// pacifica_palette_1/2/3 CRGBPalette16 initializers.
inline const Palette16& palette1() {
    static const Palette16 p = {{
        hexColor(0x000507), hexColor(0x000409), hexColor(0x00030B), hexColor(0x00030D),
        hexColor(0x000210), hexColor(0x000212), hexColor(0x000114), hexColor(0x000117),
        hexColor(0x000019), hexColor(0x00001C), hexColor(0x000026), hexColor(0x000031),
        hexColor(0x00003B), hexColor(0x000046), hexColor(0x14554B), hexColor(0x28AA50),
    }};
    return p;
}

inline const Palette16& palette2() {
    static const Palette16 p = {{
        hexColor(0x000507), hexColor(0x000409), hexColor(0x00030B), hexColor(0x00030D),
        hexColor(0x000210), hexColor(0x000212), hexColor(0x000114), hexColor(0x000117),
        hexColor(0x000019), hexColor(0x00001C), hexColor(0x000026), hexColor(0x000031),
        hexColor(0x00003B), hexColor(0x000046), hexColor(0x0C5F52), hexColor(0x19BE5F),
    }};
    return p;
}

inline const Palette16& palette3() {
    static const Palette16 p = {{
        hexColor(0x000208), hexColor(0x00030E), hexColor(0x000514), hexColor(0x00061A),
        hexColor(0x000820), hexColor(0x000927), hexColor(0x000B2D), hexColor(0x000C33),
        hexColor(0x000E39), hexColor(0x001040), hexColor(0x001450), hexColor(0x001860),
        hexColor(0x001C70), hexColor(0x002080), hexColor(0x1040BF), hexColor(0x2060FF),
    }};
    return p;
}

}  // namespace pacifica_detail

class PacificaFx : public Effect {
public:
    void reset(uint32_t seed, uint16_t ledCount) override {
        (void)seed;  // fully deterministic, no randomness used (per port task)
        ledCount_ = ledCount;
        sCIStart1_ = sCIStart2_ = sCIStart3_ = sCIStart4_ = 0;
        sLastMs_ = 0;
    }

    void render(FxFrame& f) override {
        const uint32_t ms = f.ms;
        const uint32_t deltams = ms - sLastMs_;
        sLastMs_ = ms;

        const uint16_t speedfactor1 = beatsin16(3, 179, 269, ms);
        const uint16_t speedfactor2 = beatsin16(4, 179, 269, ms);
        const uint32_t deltams1 = (deltams * speedfactor1) / 256;
        const uint32_t deltams2 = (deltams * speedfactor2) / 256;
        const uint32_t deltams21 = (deltams1 + deltams2) / 2;

        sCIStart1_ = static_cast<uint16_t>(
            sCIStart1_ + (deltams1 * beatsin88(1011, 10, 13, ms)));
        sCIStart2_ = static_cast<uint16_t>(
            sCIStart2_ - (deltams21 * beatsin88(777, 8, 11, ms)));
        sCIStart3_ = static_cast<uint16_t>(
            sCIStart3_ - (deltams1 * beatsin88(501, 5, 7, ms)));
        sCIStart4_ = static_cast<uint16_t>(
            sCIStart4_ - (deltams2 * beatsin88(257, 4, 6, ms)));

        // Clear out the LED array to a dim background blue-green.
        // A168 (§3-E item 10): bound against f.leds.size() too — see the
        // fire2012.h note on the same guard.
        for (uint16_t i = 0; i < ledCount_ && i < f.leds.size(); ++i) {
            f.leds[i] = Rgb{2, 6, 10};
        }

        // Render each of four layers, with different scales and speeds, that
        // vary over time.
        oneLayer(pacifica_detail::palette1(), sCIStart1_,
                 beatsin16(3, 11 * 256, 14 * 256, ms),
                 beatsin8(10, 70, 130, ms),
                 static_cast<uint16_t>(0 - beat16(301, ms)), f.leds);
        oneLayer(pacifica_detail::palette2(), sCIStart2_,
                 beatsin16(4, 6 * 256, 9 * 256, ms),
                 beatsin8(17, 40, 80, ms), beat16(401, ms), f.leds);
        oneLayer(pacifica_detail::palette3(), sCIStart3_,
                 static_cast<uint16_t>(6 * 256), beatsin8(9, 10, 38, ms),
                 static_cast<uint16_t>(0 - beat16(503, ms)), f.leds);
        oneLayer(pacifica_detail::palette3(), sCIStart4_,
                 static_cast<uint16_t>(5 * 256), beatsin8(8, 10, 28, ms),
                 beat16(601, ms), f.leds);

        // Add brighter 'whitecaps' where the waves line up more.
        addWhitecaps(f.leds, ms);

        // Deepen the blues and greens a bit.
        deepenColors(f.leds);
    }

private:
    // Add one layer of waves into the led array (pacifica_one_layer).
    // `cistart` is taken by value, exactly as the original sketch passes it
    // (the accumulators sCIStart1..4 are advanced in render(), not here).
    void oneLayer(const Palette16& p, uint16_t cistart, uint16_t wavescale,
                  uint8_t bri, uint16_t ioff, std::vector<Rgb>& leds) {
        uint16_t ci = cistart;
        uint16_t waveangle = ioff;
        uint16_t wavescaleHalf = static_cast<uint16_t>((wavescale / 2) + 20);
        // A168 (§3-E item 10): bound against leds.size() too (fire2012.h note).
        for (uint16_t i = 0; i < ledCount_ && i < leds.size(); ++i) {
            waveangle = static_cast<uint16_t>(waveangle + 250);
            uint16_t s16 = static_cast<uint16_t>(
                static_cast<int32_t>(sin16(waveangle)) + 32768);
            uint16_t cs = static_cast<uint16_t>(
                scale16(s16, wavescaleHalf) + wavescaleHalf);
            ci = static_cast<uint16_t>(ci + cs);
            uint16_t sindex16 = static_cast<uint16_t>(
                static_cast<int32_t>(sin16(ci)) + 32768);
            uint8_t sindex8 = static_cast<uint8_t>(scale16(sindex16, 240));
            Rgb c = colorFromPalette(p, sindex8, bri, /*blend=*/true);
            addRgb(leds[i], c);  // the ONE CRGB+= definition (fx_color.h)
        }
    }

    // Add extra 'white' to areas where the four layers of light have lined
    // up brightly (pacifica_add_whitecaps).
    void addWhitecaps(std::vector<Rgb>& leds, uint32_t ms) {
        uint8_t basethreshold = beatsin8(9, 55, 65, ms);
        uint8_t wave = beat8(7, ms);

        // A168 (§3-E item 10): bound against leds.size() too (fire2012.h note).
        for (uint16_t i = 0; i < ledCount_ && i < leds.size(); ++i) {
            uint8_t threshold = static_cast<uint8_t>(
                scale8(sin8(wave), 20) + basethreshold);
            wave = static_cast<uint8_t>(wave + 7);

            Rgb& dst = leds[i];
            // CRGB::getAverageLight(), FASTLED_SCALE8_FIXED==1 constant (85).
            uint8_t l = static_cast<uint8_t>(scale8(dst.r, 85) +
                                              scale8(dst.g, 85) +
                                              scale8(dst.b, 85));
            if (l > threshold) {
                uint8_t overage = static_cast<uint8_t>(l - threshold);
                uint8_t overage2 = qadd8(overage, overage);
                addRgb(dst,
                       Rgb{overage, overage2, qadd8(overage2, overage2)});
            }
        }
    }

    // Deepen the blues and greens a bit (pacifica_deepen_colors).
    void deepenColors(std::vector<Rgb>& leds) {
        // A168 (§3-E item 10): bound against leds.size() too (fire2012.h note).
        for (uint16_t i = 0; i < ledCount_ && i < leds.size(); ++i) {
            Rgb& dst = leds[i];
            dst.b = scale8(dst.b, 145);
            dst.g = scale8(dst.g, 200);
            // `leds[i] |= CRGB(2,5,7)`: CRGB::operator|= keeps the larger of
            // the two per-channel values (a floor), not a bitwise OR.
            if (dst.r < 2) dst.r = 2;
            if (dst.g < 5) dst.g = 5;
            if (dst.b < 7) dst.b = 7;
        }
    }

    uint16_t ledCount_ = 0;
    uint16_t sCIStart1_ = 0;
    uint16_t sCIStart2_ = 0;
    uint16_t sCIStart3_ = 0;
    uint16_t sCIStart4_ = 0;
    uint32_t sLastMs_ = 0;
};

}  // namespace fx
}  // namespace vialucis
