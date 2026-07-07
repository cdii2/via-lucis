#pragma once
// BLE-MIDI client wrapper. The FP-30X is a BLE peripheral; we connect as a
// client (lathoub Arduino-BLE-MIDI over NimBLE). All piano-specific behavior
// stays behind this one interface (iron rule: no FP-30X hacks in the core).

#include <cstdint>
#include <functional>

#include "vialucis/note_emitter.h"

namespace vialucis {

class BleMidiIo {
public:
    using NoteHandler = std::function<void(uint8_t note, uint8_t velocity)>;

    // Scans for the first BLE-MIDI peripheral and keeps reconnecting.
    void begin();
    void poll();  // call every loop() iteration — pumps the MIDI parser

    void onNoteOn(NoteHandler h);   // user pressed a key (velocity > 0)
    void onNoteOff(NoteHandler h);  // key released

    void send(const MidiOutMsg& msg);
    bool connected() const;
};

}  // namespace vialucis
