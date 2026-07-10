// Palette routines ported from FastLED 3.10.3 (MIT,
// https://github.com/FastLED/FastLED, tag 3.10.3):
//   src/fl/colorutils.cpp (ColorFromPalette for CRGBPalette16, LINEARBLEND),
//   src/colorpalettes.cpp (stock palette RGB values).
// Ported for native testability (VL7/OV3). See palette.h for the full
// attribution note.

#include "vialucis/fx/palette.h"

namespace vialucis {
namespace fx {

namespace {

constexpr Rgb fromHex(uint32_t hex) {
    return Rgb{static_cast<uint8_t>(hex >> 16), static_cast<uint8_t>(hex >> 8),
               static_cast<uint8_t>(hex)};
}

}  // namespace

Rgb colorFromPalette(const Palette16& pal, uint8_t index, uint8_t brightness,
                     bool blend) {
    // hi4/lo4: split the 8-bit index into a 4-bit palette slot (0..15) and a
    // 4-bit interpolation fraction within that slot (mirrors FastLED's
    // lsrX4(index) / index & 0x0F).
    uint8_t hi4 = static_cast<uint8_t>(index >> 4);
    uint8_t lo4 = static_cast<uint8_t>(index & 0x0F);

    const Rgb* entry = &pal.entries[hi4];

    bool doBlend = lo4 != 0 && blend;

    uint8_t red1 = entry->r;
    uint8_t green1 = entry->g;
    uint8_t blue1 = entry->b;

    if (doBlend) {
        if (hi4 == 15) {
            entry = &pal.entries[0];
        } else {
            ++entry;
        }

        uint8_t f2 = static_cast<uint8_t>(lo4 << 4);
        uint8_t f1 = static_cast<uint8_t>(255 - f2);

        uint8_t red2 = entry->r;
        red1 = scale8(red1, f1);
        red2 = scale8(red2, f2);
        red1 = static_cast<uint8_t>(red1 + red2);

        uint8_t green2 = entry->g;
        green1 = scale8(green1, f1);
        green2 = scale8(green2, f2);
        green1 = static_cast<uint8_t>(green1 + green2);

        uint8_t blue2 = entry->b;
        blue1 = scale8(blue1, f1);
        blue2 = scale8(blue2, f2);
        blue1 = static_cast<uint8_t>(blue1 + blue2);
    }

    if (brightness != 255) {
        if (brightness) {
            ++brightness;  // adjust for rounding (FASTLED_SCALE8_FIXED==1)
            red1 = scale8(red1, brightness);
            green1 = scale8(green1, brightness);
            blue1 = scale8(blue1, brightness);
        } else {
            red1 = 0;
            green1 = 0;
            blue1 = 0;
        }
    }

    return Rgb{red1, green1, blue1};
}

const Palette16& rainbowColors() {
    static const Palette16 p = {{
        fromHex(0xFF0000), fromHex(0xD52A00), fromHex(0xAB5500), fromHex(0xAB7F00),
        fromHex(0xABAB00), fromHex(0x56D500), fromHex(0x00FF00), fromHex(0x00D52A),
        fromHex(0x00AB55), fromHex(0x0056AA), fromHex(0x0000FF), fromHex(0x2A00D5),
        fromHex(0x5500AB), fromHex(0x7F0081), fromHex(0xAB0055), fromHex(0xD5002B),
    }};
    return p;
}

const Palette16& oceanColors() {
    static const Palette16 p = {{
        fromHex(0x191970), fromHex(0x00008B), fromHex(0x191970), fromHex(0x000080),
        fromHex(0x00008B), fromHex(0x0000CD), fromHex(0x2E8B57), fromHex(0x008080),
        fromHex(0x5F9EA0), fromHex(0x0000FF), fromHex(0x008B8B), fromHex(0x6495ED),
        fromHex(0x7FFFD4), fromHex(0x2E8B57), fromHex(0x00FFFF), fromHex(0x87CEFA),
    }};
    return p;
}

const Palette16& forestColors() {
    static const Palette16 p = {{
        fromHex(0x006400), fromHex(0x006400), fromHex(0x556B2F), fromHex(0x006400),
        fromHex(0x008000), fromHex(0x228B22), fromHex(0x6B8E23), fromHex(0x008000),
        fromHex(0x2E8B57), fromHex(0x66CDAA), fromHex(0x32CD32), fromHex(0x9ACD32),
        fromHex(0x90EE90), fromHex(0x7CFC00), fromHex(0x66CDAA), fromHex(0x228B22),
    }};
    return p;
}

const Palette16& lavaColors() {
    static const Palette16 p = {{
        fromHex(0x000000), fromHex(0x800000), fromHex(0x000000), fromHex(0x800000),
        fromHex(0x8B0000), fromHex(0x8B0000), fromHex(0x800000), fromHex(0x8B0000),
        fromHex(0x8B0000), fromHex(0x8B0000), fromHex(0xFF0000), fromHex(0xFFA500),
        fromHex(0xFFFFFF), fromHex(0xFFA500), fromHex(0xFF0000), fromHex(0x8B0000),
    }};
    return p;
}

const Palette16& partyColors() {
    static const Palette16 p = {{
        fromHex(0x5500AB), fromHex(0x84007C), fromHex(0xB5004B), fromHex(0xE5001B),
        fromHex(0xE81700), fromHex(0xB84700), fromHex(0xAB7700), fromHex(0xABAB00),
        fromHex(0xAB5500), fromHex(0xDD2200), fromHex(0xF2000E), fromHex(0xC2003E),
        fromHex(0x8F0071), fromHex(0x5F00A1), fromHex(0x2F00D0), fromHex(0x0007F9),
    }};
    return p;
}

const Palette16& cloudColors() {
    static const Palette16 p = {{
        fromHex(0x0000FF), fromHex(0x00008B), fromHex(0x00008B), fromHex(0x00008B),
        fromHex(0x00008B), fromHex(0x00008B), fromHex(0x00008B), fromHex(0x00008B),
        fromHex(0x0000FF), fromHex(0x00008B), fromHex(0x87CEEB), fromHex(0x87CEEB),
        fromHex(0xADD8E6), fromHex(0xFFFFFF), fromHex(0xADD8E6), fromHex(0x87CEEB),
    }};
    return p;
}

const Palette16& heatColors() {
    static const Palette16 p = {{
        fromHex(0x000000),
        fromHex(0x330000), fromHex(0x660000), fromHex(0x990000), fromHex(0xCC0000),
        fromHex(0xFF0000), fromHex(0xFF3300), fromHex(0xFF6600), fromHex(0xFF9900),
        fromHex(0xFFCC00), fromHex(0xFFFF00), fromHex(0xFFFF33), fromHex(0xFFFF66),
        fromHex(0xFFFF99), fromHex(0xFFFFCC), fromHex(0xFFFFFF),
    }};
    return p;
}

}  // namespace fx
}  // namespace vialucis
