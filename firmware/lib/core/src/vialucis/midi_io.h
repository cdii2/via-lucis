#pragma once
// The MIDI transport seam (R1): the five methods App consumes from its MIDI
// IO, in portable MidiOutMsg terms. Adapters: BleMidiIo (NimBLE facade, the
// device) and FakeMidiIo (scripted, native tests). Piano-specific behavior
// stays inside an adapter (iron rule: no FP-30X hacks in the core); the
// velocity-0-is-a-note-off rule below is generic MIDI running-status
// semantics, part of the seam contract itself.

#include <cstdint>
#include <functional>

namespace vialucis {

enum class MidiOutType : uint8_t { NoteOn, NoteOff, Cc };

struct MidiOutMsg {
    MidiOutType type;
    uint8_t channel;
    uint8_t data1;  // note number / controller number
    uint8_t data2;  // velocity / controller value
};

class MidiIo {
public:
    using NoteHandler = std::function<void(uint8_t note, uint8_t velocity)>;

    virtual ~MidiIo() = default;

    virtual void begin() = 0;
    virtual void poll() = 0;  // pumps the transport; handlers fire from here

    virtual void onNoteOn(NoteHandler h) = 0;   // key pressed (velocity > 0)
    virtual void onNoteOff(NoteHandler h) = 0;  // key released

    virtual void send(const MidiOutMsg& msg) = 0;
    virtual bool connected() const = 0;
};

// Seam contract, owned once: a note-on with velocity 0 is a note-off (MIDI
// running-status shorthand). Every adapter routes incoming note events
// through here — the filter is load-bearing, since App subscribes only to
// note-ons and this is what keeps a key RELEASE from ever reading as a key
// press.
inline void dispatchNote(const MidiIo::NoteHandler& onNoteOn,
                         const MidiIo::NoteHandler& onNoteOff, bool isOff,
                         uint8_t note, uint8_t velocity) {
    if (isOff || velocity == 0) {
        if (onNoteOff) onNoteOff(note, isOff ? velocity : 0);
    } else {
        if (onNoteOn) onNoteOn(note, velocity);
    }
}

}  // namespace vialucis
