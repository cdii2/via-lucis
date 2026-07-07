#pragma once
// Key → LED index mapping for a strip mounted above the keys.
//
// Geometry: "cluster model" of the rear of the keyboard, where the strip sits.
// One octave = 164.5mm = 7 white key fronts. At the rear the C-E group divides
// its 70.5mm evenly across 5 keys (14.1mm slots) and the F-B group divides its
// 94mm across 7 keys (~13.43mm slots). Origin = left edge of the A0 key.
//
// Calibration is exactly two user settings (SPEC): offsetMm and ledsPerMeter.

#include <cstdint>

namespace vialucis {

struct LedMapConfig {
    float ledsPerMeter = 180.0f;  // BOM strip: FCOB 180 IC/m
    float offsetMm = 0.0f;        // + shifts lights toward higher keys
    uint16_t ledCount = 360;      // 2m strip
};

struct LedRange {
    uint16_t first = 0;
    uint16_t last = 0;  // inclusive
    bool valid = false;
};

// Center of a key in mm right of the A0 key's left edge; negative if the note
// is outside the 88-key range (21..108).
float keyCenterMm(uint8_t midiNote);

// LEDs to light for a key. Ranges of adjacent keys never overlap; keys whose
// slot falls off the strip return valid=false.
LedRange ledsForNote(uint8_t midiNote, const LedMapConfig& cfg);

}  // namespace vialucis
