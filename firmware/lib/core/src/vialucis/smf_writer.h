#pragma once
// Standard MIDI File writer — the inverse of midi_parser. Takes a small event
// model (per-track named notes + CC64 pedal events) and emits SMF format-1
// bytes. Pure C++ (no Arduino headers) so the native env can round-trip it
// through parseMidi().
//
// Fixed timebase (FROZEN, docs/DESIGN-record.md): ticksPerQuarter = 500 and a
// single tempo meta of 500000 us/quarter (120 BPM) on track 0 ⇒ 1 tick = 1 ms
// exactly. Callers therefore express every time in milliseconds and it maps
// straight to ticks; the editor assigns the musical tempo later.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vialucis {

// A single note: absolute on/off in milliseconds (== ticks). offMs >= onMs.
struct SmfNoteEvent {
    uint32_t onMs;
    uint32_t offMs;
    uint8_t note;      // 0-127
    uint8_t velocity;  // note-on velocity, 1-127
    uint8_t channel;   // 0-15
};

// A CC64 sustain-pedal event at an absolute millisecond.
struct SmfPedalEvent {
    uint32_t tMs;
    uint8_t value;    // 0-127
    uint8_t channel;  // 0-15
};

// One performance track: an optional name (meta 0x03, e.g. "Left"/"Right")
// plus its notes and pedal events. Events need not be pre-sorted.
struct SmfTrack {
    std::string name;
    std::vector<SmfNoteEvent> notes;
    std::vector<SmfPedalEvent> pedals;
};

// The whole take. Track 0 of the emitted file is always a tempo/meta conductor
// track written automatically; these are the performance tracks after it.
struct SmfInput {
    std::vector<SmfTrack> tracks;
};

// Fixed timebase constants (see header note).
constexpr uint16_t kSmfTicksPerQuarter = 500;
constexpr uint32_t kSmfUsPerQuarter = 500000;  // 120 BPM ⇒ 1 tick = 1 ms

// Serialize to SMF format-1 bytes. Never fails: degenerate input (no tracks,
// empty tracks) yields a valid file with just the conductor track.
std::vector<uint8_t> writeSmf(const SmfInput& input);

}  // namespace vialucis
