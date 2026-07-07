#include "vialucis/key_led_map.h"

#include <cmath>

namespace vialucis {
namespace {

constexpr float kOctaveMm = 164.5f;
constexpr float kCeSlot = 70.5f / 5.0f;   // C,C#,D,D#,E
constexpr float kFbSlot = 94.0f / 7.0f;   // F,F#,G,G#,A,A#,B
// Margin keeps neighboring keys from claiming the same LED.
constexpr float kEdgeMarginMm = 1.0f;

// Center of each note-in-octave, mm from the octave's C left edge.
constexpr float kCenterInOctave[12] = {
    0.5f * kCeSlot,                    // C
    1.5f * kCeSlot,                    // C#
    2.5f * kCeSlot,                    // D
    3.5f * kCeSlot,                    // D#
    4.5f * kCeSlot,                    // E
    70.5f + 0.5f * kFbSlot,            // F
    70.5f + 1.5f * kFbSlot,            // F#
    70.5f + 2.5f * kFbSlot,            // G
    70.5f + 3.5f * kFbSlot,            // G#
    70.5f + 4.5f * kFbSlot,            // A
    70.5f + 5.5f * kFbSlot,            // A#
    70.5f + 6.5f * kFbSlot,            // B
};

// A0 (MIDI 21) left edge in the same virtual space (C0 = MIDI 12 at 0).
const float kA0LeftEdge = kOctaveMm * 0 + kCenterInOctave[9] - 0.5f * kFbSlot;

float slotWidth(uint8_t noteInOctave) {
    return noteInOctave < 5 ? kCeSlot : kFbSlot;
}

float virtualPos(uint8_t midiNote) {
    int octave = (midiNote - 12) / 12;
    int n = (midiNote - 12) % 12;
    return octave * kOctaveMm + kCenterInOctave[n];
}

}  // namespace

float keyCenterMm(uint8_t midiNote) {
    if (midiNote < 21 || midiNote > 108) return -1.0f;
    return virtualPos(midiNote) - kA0LeftEdge;
}

LedRange ledsForNote(uint8_t midiNote, const LedMapConfig& cfg) {
    LedRange out;
    if (midiNote < 21 || midiNote > 108) return out;
    if (cfg.ledsPerMeter <= 0.0f || cfg.ledCount == 0) return out;

    const float pitch = 1000.0f / cfg.ledsPerMeter;
    const float center = keyCenterMm(midiNote) + cfg.offsetMm;
    const float halfSlot = 0.5f * slotWidth((midiNote - 12) % 12);
    const float left = center - halfSlot + kEdgeMarginMm;
    const float right = center + halfSlot - kEdgeMarginMm;

    // LED i's center sits at (i + 0.5) * pitch from the strip start.
    long first = static_cast<long>(std::ceil(left / pitch - 0.5f));
    long last = static_cast<long>(std::floor(right / pitch - 0.5f));

    if (last < first) {  // coarse strip: fall back to the nearest single LED
        long nearest = static_cast<long>(std::floor(center / pitch));
        first = last = nearest;
    }
    if (last < 0 || first >= cfg.ledCount) return out;
    if (first < 0 || last >= cfg.ledCount) return out;  // partially off: treat as unmappable

    out.first = static_cast<uint16_t>(first);
    out.last = static_cast<uint16_t>(last);
    out.valid = true;
    return out;
}

}  // namespace vialucis
