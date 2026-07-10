#pragma once
// Color routines ported from FastLED 3.10.3 (MIT,
// https://github.com/FastLED/FastLED, tag 3.10.3):
//   src/hsv2rgb.cpp (hsv2rgb_rainbow), src/fl/colorutils.cpp (HeatColor,
//   nscale8x3, fadeToBlackBy, nblend), src/lib8tion/scale8.h (nscale8x3).
// Ported for native testability (VL7/OV3).
//
// hsv2rgbRainbow is ported bit-for-bit from hsv2rgb_rainbow's K255/K171/
// K170/K85 sectioning (the Y1 yellow-boost path — Y2/G2/Gscale are all 0 in
// FastLED's defaults and were never live code, so they're omitted here).
// nblend uses blend8() (FASTLED_BLEND_FIXED==1, FastLED's default); scale8()
// calls use the FASTLED_SCALE8_FIXED==1 formula throughout, matching
// FastLED's default build (the *_LEAVING_R1_DIRTY AVR-register-preserving
// variants collapse to plain scale8()/scale8_video() off AVR).

#include <cstdint>
#include <vector>

#include "vialucis/frame_renderer.h"
#include "vialucis/fx/fx_math.h"

namespace vialucis {
namespace fx {

struct Hsv {
    uint8_t h = 0;
    uint8_t s = 0;
    uint8_t v = 0;
};

// hsv2rgbRainbow: ported from FastLED's hsv2rgb_rainbow(). This is the
// "rainbow" hue mapping (as opposed to a naive 6-sector HSV spectrum) —
// yellow gets a brightness boost so it doesn't look dim next to red/green.
inline void hsv2rgbRainbow(const Hsv& hsv, Rgb& rgb) {
    // Level Y1 is a moderate yellow boost, FastLED's default (Y2/G2/Gscale
    // are all 0 in FastLED's defaults, so those branches are omitted).
    const uint8_t K255 = 255;
    const uint8_t K171 = 171;
    const uint8_t K170 = 170;
    const uint8_t K85 = 85;

    uint8_t hue = hsv.h;
    uint8_t sat = hsv.s;
    uint8_t val = hsv.v;

    uint8_t offset = hue & 0x1F;  // 0..31
    uint8_t offset8 = static_cast<uint8_t>(offset << 3);  // 0..248

    uint8_t third = scale8(offset8, 256 / 3);  // max = 85

    uint8_t r, g, b;

    if (!(hue & 0x80)) {
        // 0XX
        if (!(hue & 0x40)) {
            // 00X
            if (!(hue & 0x20)) {
                // 000: R -> O
                r = static_cast<uint8_t>(K255 - third);
                g = third;
                b = 0;
            } else {
                // 001: O -> Y
                r = K171;
                g = static_cast<uint8_t>(K85 + third);
                b = 0;
            }
        } else {
            // 01X
            if (!(hue & 0x20)) {
                // 010: Y -> G
                uint8_t twothirds = scale8(offset8, (256 * 2) / 3);  // max=170
                r = static_cast<uint8_t>(K171 - twothirds);
                g = static_cast<uint8_t>(K170 + third);
                b = 0;
            } else {
                // 011: G -> A
                r = 0;
                g = static_cast<uint8_t>(K255 - third);
                b = third;
            }
        }
    } else {
        // 1XX
        if (!(hue & 0x40)) {
            // 10X
            if (!(hue & 0x20)) {
                // 100: A -> B
                r = 0;
                uint8_t twothirds = scale8(offset8, (256 * 2) / 3);
                g = static_cast<uint8_t>(K171 - twothirds);
                b = static_cast<uint8_t>(K85 + twothirds);
            } else {
                // 101: B -> P
                r = third;
                g = 0;
                b = static_cast<uint8_t>(K255 - third);
            }
        } else {
            if (!(hue & 0x20)) {
                // 110: P -> K
                r = static_cast<uint8_t>(K85 + third);
                g = 0;
                b = static_cast<uint8_t>(K171 - third);
            } else {
                // 111: K -> R
                r = static_cast<uint8_t>(K170 + third);
                g = 0;
                b = static_cast<uint8_t>(K85 - third);
            }
        }
    }

    // Scale down colors if we're desaturated at all, and add the
    // brightness_floor to r, g, and b.
    if (sat != 255) {
        if (sat == 0) {
            r = 255;
            g = 255;
            b = 255;
        } else {
            uint8_t desat = static_cast<uint8_t>(255 - sat);
            desat = scale8_video(desat, desat);

            uint8_t satscale = static_cast<uint8_t>(255 - desat);

            r = scale8(r, satscale);
            g = scale8(g, satscale);
            b = scale8(b, satscale);

            uint8_t brightness_floor = desat;
            r = static_cast<uint8_t>(r + brightness_floor);
            g = static_cast<uint8_t>(g + brightness_floor);
            b = static_cast<uint8_t>(b + brightness_floor);
        }
    }

    // Now scale everything down if we're at value < 255.
    if (val != 255) {
        val = scale8_video(val, val);
        if (val == 0) {
            r = 0;
            g = 0;
            b = 0;
        } else {
            r = scale8(r, val);
            g = scale8(g, val);
            b = scale8(b, val);
        }
    }

    rgb.r = r;
    rgb.g = g;
    rgb.b = b;
}

// heatColor: ported from FastLED's HeatColor(). Approximates black-body
// radiation for "fire" animations; 0 = cool/black, 255 = hot/white.
inline Rgb heatColor(uint8_t temperature) {
    Rgb heatcolor;

    // Scale 'heat' down from 0-255 to 0-191, three equal 'thirds' of 64.
    uint8_t t192 = scale8_video(temperature, 191);

    uint8_t heatramp = t192 & 0x3F;  // 0..63
    heatramp = static_cast<uint8_t>(heatramp << 2);  // 0..252

    if (t192 & 0x80) {
        // hottest third
        heatcolor.r = 255;
        heatcolor.g = 255;
        heatcolor.b = heatramp;
    } else if (t192 & 0x40) {
        // middle third
        heatcolor.r = 255;
        heatcolor.g = heatramp;
        heatcolor.b = 0;
    } else {
        // coolest third
        heatcolor.r = heatramp;
        heatcolor.g = 0;
        heatcolor.b = 0;
    }

    return heatcolor;
}

// nscale8x3: scale r,g,b in place by scale/256 (FASTLED_SCALE8_FIXED==1
// formula — equivalent to scale8() applied per channel).
inline void nscale8x3(uint8_t& r, uint8_t& g, uint8_t& b, fract8 scale) {
    r = fx::scale8(r, scale);
    g = fx::scale8(g, scale);
    b = fx::scale8(b, scale);
}

// fadeToBlackBy: ported from FastLED's fadeToBlackBy(CRGB*, n, fadeBy) —
// scales every LED toward black by fadeBy/255.
inline void fadeToBlackBy(std::vector<Rgb>& leds, uint8_t fadeBy) {
    uint8_t scale = static_cast<uint8_t>(255 - fadeBy);
    for (Rgb& px : leds) {
        nscale8x3(px.r, px.g, px.b, scale);
    }
}

// nblend: ported from FastLED's nblend(CRGB&, const CRGB&, fract8) —
// blends `amount`/255 of overlay into existing, per channel, via blend8().
inline Rgb& nblend(Rgb& existing, const Rgb& overlay, uint8_t amount) {
    if (amount == 0) {
        return existing;
    }
    if (amount == 255) {
        existing = overlay;
        return existing;
    }
    existing.r = blend8(existing.r, overlay.r, amount);
    existing.g = blend8(existing.g, overlay.g, amount);
    existing.b = blend8(existing.b, overlay.b, amount);
    return existing;
}

}  // namespace fx
}  // namespace vialucis
