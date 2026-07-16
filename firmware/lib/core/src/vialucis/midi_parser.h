#pragma once
// Standard MIDI File parser → flat, tick-sorted note list + tempo map.
// Pure C++ (no Arduino headers) so the native env can test it.
//
// A185 (streaming parse): the parser reads its input through a ByteSource,
// pulled sequentially in small gulps, so the whole MIDI file never has to sit
// in RAM at once. On-device the source refills from a LittleFS file handle
// (firmware/src/song_store.cpp); native tests feed a byte buffer or a chunk
// fake. Callers may pass a per-parse memory budget (bytes available for the
// parsed note/pedal/tempo output) so an oversized song is rejected as a clean
// typed error (MidiParseError::TooBigForMemory) instead of gambling on an
// allocation that would abort on -fno-exceptions (the live 2026-07-16 crash).

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
    NotMidi,          // missing or malformed MThd header
    SmpteDivision,    // SMPTE time division unsupported
    Truncated,        // ran off the end of the data mid-chunk
    BadTrack,         // MTrk marker missing where one was expected
    TooBigForMemory,  // A185: parses fine, but the notes/pedal/tempo output
                      // would exceed the caller's memory budget for this
                      // device — refuse cleanly, never attempt the allocation.
};

struct MidiParseResult {
    MidiParseError error = MidiParseError::Ok;
    MidiSong song;
};

// Sequential byte source for the streaming parse (A185). Pure virtual so the
// device's LittleFS file handle (firmware/src/song_store.cpp) and the native
// test fakes both plug in without lib/core ever touching an Arduino header
// (iron rule 4 — core stays pure C++). The parser only ever reads FORWARD;
// the two-pass exact-count parse rewinds once via reset().
class ByteSource {
public:
    virtual ~ByteSource() = default;

    // Total number of bytes this source will yield front-to-back. Known
    // upfront (a file's size / a buffer's length) and constant across reset().
    virtual size_t size() const = 0;

    // Copy up to `max` bytes into `dst` from the current position, advance,
    // and return the count copied (0 == the source is exhausted). Short reads
    // are allowed — the parser loops. This is the on-device yield seam: a long
    // streamed parse can feed the task watchdog here (iron rule 1) without ever
    // touching the BLE→match→LED latency path (parse runs on the HTTP task).
    virtual size_t read(uint8_t* dst, size_t max) = 0;

    // Rewind to the first byte. The parse reads the source twice — once to
    // COUNT events (so the exact output size is known and can be checked
    // against the budget before any large allocation), once to FILL — and
    // must observe byte-identical content both times.
    virtual void reset() = 0;
};

// The fixed heap the streaming parse needs beyond its note/pedal/tempo output:
// the ~18 KB NoteTracker open-note map (A181), allocated once per fill pass.
// Device callers subtract this (plus their own working margin) from
// ESP.getMaxAllocHeap() to derive the note budget passed to parseMidi.
size_t midiParseFixedOverhead();

// The largest note/pedal/tempo output (in bytes) a given budget can hold — the
// exact figure parseMidi checks the counted output against. Exposed so device
// callers and tests can reason about the fit without duplicating sizeof math.
size_t midiParseOutputBytes(size_t notes, size_t tempo, size_t pedal);

// Parse a whole MIDI file already resident in a byte buffer. `budgetBytes` caps
// the parsed output memory; SIZE_MAX (the default) means "no limit" so every
// existing native caller is unchanged. Returns TooBigForMemory when the exact
// output would exceed the budget (checked before allocating the output).
MidiParseResult parseMidi(const uint8_t* data, size_t len,
                          size_t budgetBytes = SIZE_MAX);

// Parse from a streaming source (A185). Reads the source twice (count, then
// fill). `budgetBytes` behaves as above.
MidiParseResult parseMidi(ByteSource& src, size_t budgetBytes = SIZE_MAX);

// Validity + fit CHECK only (A185): runs the single counting pass — no
// NoteTracker, no output vectors allocated — and returns the same error a full
// parse would (NotMidi / Truncated / BadTrack / SmpteDivision / TooBigForMemory
// / Ok). This is what the songs-list parseOk badge uses: it needs to know
// whether a file parses AND fits, but never needs the notes themselves, so it
// pays neither the tracker nor the output allocation. Reads the source once.
MidiParseError checkMidi(ByteSource& src, size_t budgetBytes = SIZE_MAX);

// Absolute time of a tick, walking the tempo map (default 500000 us/quarter
// before the first tempo event / when the map is empty).
uint64_t tickToMicros(const MidiSong& song, uint32_t tick);

}  // namespace vialucis
