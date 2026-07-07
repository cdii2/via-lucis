#pragma once
// Standard MIDI File parser → flat, tick-sorted note list + tempo map.
// Pure C++ (no Arduino headers) so the native env can test it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vialucis {

struct MidiNote {
    uint32_t onTick;
    uint32_t offTick;
    uint8_t note;      // MIDI note number 0-127
    uint8_t velocity;  // note-on velocity
    uint8_t channel;   // 0-15
    uint8_t track;     // source track index (hands-separate filter keys off this)
};

struct TempoChange {
    uint32_t tick;
    uint32_t usPerQuarter;
};

// CC64 sustain-pedal event (passed through in demo/accompaniment, ignored by
// wait-mode matching per SPEC).
struct PedalEvent {
    uint32_t tick;
    uint8_t value;
    uint8_t channel;
    uint8_t track;
};

struct TrackInfo {
    std::string name;  // from meta 0x03, empty if absent
};

struct MidiSong {
    uint16_t ticksPerQuarter = 480;
    std::vector<MidiNote> notes;       // sorted by onTick (stable)
    std::vector<TempoChange> tempo;    // sorted by tick; empty ⇒ 120 bpm throughout
    std::vector<PedalEvent> pedal;     // CC64 only, in tick order
    std::vector<TrackInfo> tracks;
};

enum class MidiParseError {
    Ok,
    NotMidi,        // missing or malformed MThd header
    SmpteDivision,  // SMPTE time division unsupported
    Truncated,      // ran off the end of the data mid-chunk
    BadTrack,       // MTrk marker missing where one was expected
};

struct MidiParseResult {
    MidiParseError error = MidiParseError::Ok;
    MidiSong song;
};

MidiParseResult parseMidi(const uint8_t* data, size_t len);

// Absolute time of a tick, walking the tempo map (default 500000 us/quarter
// before the first tempo event / when the map is empty).
uint64_t tickToMicros(const MidiSong& song, uint32_t tick);

}  // namespace vialucis
