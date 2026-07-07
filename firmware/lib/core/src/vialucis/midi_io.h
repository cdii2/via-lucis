#pragma once
// The MIDI transport seam (R1): the five methods App consumes from its MIDI
// IO, in portable MidiOutMsg terms. Adapters: BleMidiIo (NimBLE facade, the
// device) and FakeMidiIo (scripted, native tests). All piano-specific
// behavior — e.g. running-status velocity-0 note-offs — stays inside an
// adapter (iron rule: no FP-30X hacks in the core).

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

}  // namespace vialucis
