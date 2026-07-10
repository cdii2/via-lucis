#pragma once
// Math/color routines ported from FastLED 3.10.3 (MIT,
// https://github.com/FastLED/FastLED, tag 3.10.3):
//   src/lib8tion.h, src/lib8tion/math8.h, src/lib8tion/scale8.h,
//   src/lib8tion/trig8.h
// Ported for native testability (VL7/OV3); beat* functions take explicit ms
// (fixed timestep) instead of reading FastLED's global millis() clock.
//
// Only the plain-C (_C suffix) code paths are ported — never the AVR/ASM
// variants. With FASTLED_SCALE8_FIXED == 1 (FastLED's default), scale8()
// never returns short of 255 for full-scale inputs; that fixed-point formula
// is what's ported here.

#include <cstdint>

namespace vialucis {
namespace fx {

// --- fixed-point aliases (mirrors FastLED's fl/int.h) ----------------------
using fract8 = uint8_t;    // unsigned 256ths
using fract16 = uint16_t;  // unsigned 65536ths
using accum88 = uint16_t;  // 8 bits int, 8 bits fraction (Q8.8 BPM)

// --- Scaling (lib8tion/scale8.h, SCALE8_C / FASTLED_SCALE8_FIXED==1) -------

// scale8(i, scale) == (i * (scale+1)) / 256. Ported from the "fixed" C
// variant: scale8(255,255) == 255 (never short-changes full scale).
inline uint8_t scale8(uint8_t i, fract8 scale) {
    return static_cast<uint8_t>((static_cast<uint16_t>(i) *
                                  (1 + static_cast<uint16_t>(scale))) >>
                                 8);
}

// scale8_video(i, scale): guarantees output is zero only if i or scale is
// zero (never rounds a lit pixel down to black).
inline uint8_t scale8_video(uint8_t i, fract8 scale) {
    return static_cast<uint8_t>(
        ((static_cast<int>(i) * static_cast<int>(scale)) >> 8) +
        ((i && scale) ? 1 : 0));
}

// scale16(i, scale) == (i * (scale+1)) / 65536, fixed-point variant.
inline uint16_t scale16(uint16_t i, fract16 scale) {
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(i) * (1 + static_cast<uint32_t>(scale))) /
        65536u);
}

// --- Basic math (lib8tion/math8.h, *_C variants) ---------------------------

// qadd8(i,j) == MIN(i+j, 0xFF).
inline uint8_t qadd8(uint8_t i, uint8_t j) {
    unsigned t = static_cast<unsigned>(i) + static_cast<unsigned>(j);
    if (t > 255) t = 255;
    return static_cast<uint8_t>(t);
}

// qsub8(i,j) == MAX(i-j, 0).
inline uint8_t qsub8(uint8_t i, uint8_t j) {
    int t = static_cast<int>(i) - static_cast<int>(j);
    if (t < 0) t = 0;
    return static_cast<uint8_t>(t);
}

// avg8(i,j): mean average, rounded down.
inline uint8_t avg8(uint8_t i, uint8_t j) {
    return static_cast<uint8_t>((static_cast<unsigned>(i) + j) >> 1);
}

// blend8(a,b,amountOfB): blend a variable proportion (0-255) of b into a
// (BLEND8_C, FASTLED_BLEND_FIXED==1 variant — used by nblend() in
// fx_color.h to blend Rgb channels).
inline uint8_t blend8(uint8_t a, uint8_t b, uint8_t amountOfB) {
    uint16_t partial = static_cast<uint16_t>((static_cast<uint16_t>(a) << 8) |
                                              b);
    partial = static_cast<uint16_t>(partial +
                                     static_cast<uint16_t>(b) * amountOfB);
    partial = static_cast<uint16_t>(partial -
                                     static_cast<uint16_t>(a) * amountOfB);
    return static_cast<uint8_t>(partial >> 8);
}

// --- Linear interpolation (lib8tion.h) --------------------------------------

// lerp8by8(a,b,frac) == a + ((b-a) * frac) / 256, split by sign to avoid
// overflow (mirrors the FastLED source exactly).
inline uint8_t lerp8by8(uint8_t a, uint8_t b, fract8 frac) {
    uint8_t result;
    if (b > a) {
        uint8_t delta = static_cast<uint8_t>(b - a);
        uint8_t scaled = scale8(delta, frac);
        result = static_cast<uint8_t>(a + scaled);
    } else {
        uint8_t delta = static_cast<uint8_t>(a - b);
        uint8_t scaled = scale8(delta, frac);
        result = static_cast<uint8_t>(a - scaled);
    }
    return result;
}

// --- Easing (lib8tion.h, EASE8_C variants) ----------------------------------

// ease8InOutCubic(i) == 3(x^2) - 2(x^3), 8-bit fixed point.
inline fract8 ease8InOutCubic(fract8 i) {
    uint8_t ii = scale8(i, i);
    uint8_t iii = scale8(ii, i);
    uint16_t r1 = (3 * static_cast<uint16_t>(ii)) -
                  (2 * static_cast<uint16_t>(iii));
    uint8_t result = static_cast<uint8_t>(r1);
    if (r1 & 0x100) {
        result = 255;
    }
    return result;
}

// ease8InOutQuad(i): quadratic (vs cubic) ease-in/ease-out.
inline uint8_t ease8InOutQuad(uint8_t i) {
    uint8_t j = i;
    if (j & 0x80) {
        j = static_cast<uint8_t>(255 - j);
    }
    uint8_t jj = scale8(j, j);
    uint8_t jj2 = static_cast<uint8_t>(jj << 1);
    if (i & 0x80) {
        jj2 = static_cast<uint8_t>(255 - jj2);
    }
    return jj2;
}

// --- Waveform generators (lib8tion.h) ---------------------------------------

// triwave8(in): 0..127 -> 0..254 rising, 128..255 -> 254..0 falling.
inline uint8_t triwave8(uint8_t in) {
    if (in & 0x80) {
        in = static_cast<uint8_t>(255 - in);
    }
    return static_cast<uint8_t>(in << 1);
}

// quadwave8(in): S-shaped ("sine-like") wave, faster than sin8().
inline uint8_t quadwave8(uint8_t in) { return ease8InOutQuad(triwave8(in)); }

// cubicwave8(in): spends more time at the limits than "sine" does.
inline uint8_t cubicwave8(uint8_t in) { return ease8InOutCubic(triwave8(in)); }

// --- Trigonometry (lib8tion/trig8.h, non-AVR _C variants) -------------------

// sin16(theta): fast 16-bit approximation of sin(x)*32767, theta 0..65535.
inline int16_t sin16(uint16_t theta) {
    static const uint16_t base[] = {0,     6393,  12539, 18204,
                                     23170, 27245, 30273, 32137};
    static const uint8_t slope[] = {49, 48, 44, 38, 31, 23, 14, 4};

    uint16_t offset = static_cast<uint16_t>((theta & 0x3FFF) >> 3);  // 0..2047
    if (theta & 0x4000) {
        offset = static_cast<uint16_t>(2047 - offset);
    }

    uint8_t section = static_cast<uint8_t>(offset / 256);  // 0..7
    uint16_t b = base[section];
    uint8_t m = slope[section];

    uint8_t secoffset8 = static_cast<uint8_t>(offset) / 2;

    uint16_t mx = static_cast<uint16_t>(m * secoffset8);
    int16_t y = static_cast<int16_t>(mx + b);

    if (theta & 0x8000) {
        y = static_cast<int16_t>(-y);
    }

    return y;
}

// cos16(theta): fast 16-bit approximation of cos(x)*32767.
inline int16_t cos16(uint16_t theta) {
    return sin16(static_cast<uint16_t>(theta + 16384));
}

// sin8(theta): fast 8-bit approximation of (sin(x)*128)+128, theta 0..255.
inline uint8_t sin8(uint8_t theta) {
    static const uint8_t b_m16_interleave[] = {0, 49, 49, 41, 90, 27, 117, 10};

    uint8_t offset = theta;
    if (theta & 0x40) {
        offset = static_cast<uint8_t>(255 - offset);
    }
    offset &= 0x3F;  // 0..63

    uint8_t secoffset = offset & 0x0F;  // 0..15
    if (theta & 0x40) {
        ++secoffset;
    }

    uint8_t section = static_cast<uint8_t>(offset >> 4);  // 0..3
    uint8_t s2 = static_cast<uint8_t>(section * 2);

    const uint8_t* p = b_m16_interleave + s2;
    uint8_t b = *p;
    ++p;
    uint8_t m16 = *p;

    uint8_t mx = static_cast<uint8_t>((m16 * secoffset) >> 4);

    int8_t y = static_cast<int8_t>(mx + b);
    if (theta & 0x80) {
        y = static_cast<int8_t>(-y);
    }
    y = static_cast<int8_t>(y + 128);

    return static_cast<uint8_t>(y);
}

// cos8(theta): fast 8-bit approximation of (cos(x)*128)+128.
inline uint8_t cos8(uint8_t theta) {
    return sin8(static_cast<uint8_t>(theta + 64));
}

// --- Beat generators (lib8tion.h) -------------------------------------------
// FastLED reads GET_MILLIS() (a global wall clock) here. Via Lucis is
// fixed-timestep and must be deterministic (VL7): every beat* function below
// takes the elapsed time as an explicit `ms` parameter instead. All other
// arithmetic is ported unchanged, including the original's 32-bit intra-step
// arithmetic (so results wrap identically to FastLED's for very large `ms`).

// beat88(bpm88, ms): 16-bit sawtooth wave; bpm88 is Q8.8 fixed point
// (multiply a decimal BPM by 256 to get bpm88).
inline uint16_t beat88(accum88 bpm88, uint32_t ms) {
    // Ratio 65536:60000 approximated as 280:256, per FastLED.
    return static_cast<uint16_t>((ms * static_cast<uint32_t>(bpm88) * 280) >>
                                  16);
}

// beat16(bpm, ms): 16-bit sawtooth wave; bpm is a simple decimal BPM (or a
// pre-scaled Q8.8 accum88, same rule as FastLED: values < 256 are treated as
// simple integer BPM and auto-shifted into Q8.8).
inline uint16_t beat16(accum88 bpm, uint32_t ms) {
    if (bpm < 256) {
        bpm = static_cast<accum88>(bpm << 8);
    }
    return beat88(bpm, ms);
}

// beat8(bpm, ms): 8-bit sawtooth wave.
inline uint8_t beat8(accum88 bpm, uint32_t ms) {
    return static_cast<uint8_t>(beat16(bpm, ms) >> 8);
}

// beatsin88(bpm88, lo, hi, ms, phase): 16-bit sine wave oscillating in
// [lo, hi] at the given Q8.8 BPM.
inline uint16_t beatsin88(accum88 bpm88, uint16_t lo, uint16_t hi,
                           uint32_t ms, uint16_t phase = 0) {
    uint16_t beat = beat88(bpm88, ms);
    uint16_t beatsin = static_cast<uint16_t>(
        sin16(static_cast<uint16_t>(beat + phase)) + 32768);
    uint16_t rangewidth = static_cast<uint16_t>(hi - lo);
    uint16_t scaledbeat = scale16(beatsin, rangewidth);
    return static_cast<uint16_t>(lo + scaledbeat);
}

// beatsin16(bpm, lo, hi, ms, phase): 16-bit sine wave, decimal BPM.
inline uint16_t beatsin16(accum88 bpm, uint16_t lo, uint16_t hi, uint32_t ms,
                           uint16_t phase = 0) {
    uint16_t beat = beat16(bpm, ms);
    uint16_t beatsin = static_cast<uint16_t>(
        sin16(static_cast<uint16_t>(beat + phase)) + 32768);
    uint16_t rangewidth = static_cast<uint16_t>(hi - lo);
    uint16_t scaledbeat = scale16(beatsin, rangewidth);
    return static_cast<uint16_t>(lo + scaledbeat);
}

// beatsin8(bpm, lo, hi, ms, phase): 8-bit sine wave, decimal BPM.
inline uint8_t beatsin8(accum88 bpm, uint8_t lo, uint8_t hi, uint32_t ms,
                         uint8_t phase = 0) {
    uint8_t beat = beat8(bpm, ms);
    uint8_t beatsin = sin8(static_cast<uint8_t>(beat + phase));
    uint8_t rangewidth = static_cast<uint8_t>(hi - lo);
    uint8_t scaledbeat = scale8(beatsin, rangewidth);
    return static_cast<uint8_t>(lo + scaledbeat);
}

}  // namespace fx
}  // namespace vialucis
