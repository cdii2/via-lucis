#pragma once
// Palette routines ported from FastLED 3.10.3 (MIT,
// https://github.com/FastLED/FastLED, tag 3.10.3):
//   src/fl/colorutils.cpp (ColorFromPalette for CRGBPalette16, LINEARBLEND),
//   src/fl/colorutils.h (declarations), src/colorpalettes.cpp (stock
//   16-entry gradient palette RGB values: RainbowColors_p, OceanColors_p,
//   ForestColors_p, LavaColors_p, PartyColors_p, CloudColors_p,
//   HeatColors_p).
// Ported for native testability (VL7/OV3).

#include <cstdint>

#include "vialucis/frame_renderer.h"
#include "vialucis/fx/fx_math.h"

namespace vialucis {
namespace fx {

// 16-entry gradient palette — mirrors FastLED's CRGBPalette16 (which is
// itself a 16-entry array of CRGB, indexed 0..15 and interpolated across a
// full uint8_t index range by colorFromPalette()).
struct Palette16 {
    Rgb entries[16];
};

// colorFromPalette: ported from FastLED's ColorFromPalette(CRGBPalette16,
// uint8_t index, uint8_t brightness, TBlendType). `blend` selects
// LINEARBLEND (true, FastLED's usual default) vs NOBLEND (false) — blocky,
// unblended steps between the 16 stops.
Rgb colorFromPalette(const Palette16& pal, uint8_t index,
                     uint8_t brightness = 255, bool blend = true);

// Stock palettes, ported byte-for-byte from FastLED's colorpalettes.cpp.
const Palette16& rainbowColors();
const Palette16& oceanColors();
const Palette16& forestColors();
const Palette16& lavaColors();
const Palette16& partyColors();
const Palette16& cloudColors();
const Palette16& heatColors();

}  // namespace fx
}  // namespace vialucis
