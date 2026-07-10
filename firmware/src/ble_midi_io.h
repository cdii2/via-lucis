#pragma once
// BLE-MIDI client wrapper — the device adapter of the MidiIo seam. The
// FP-30X is a BLE peripheral; we connect as a client (lathoub
// Arduino-BLE-MIDI over NimBLE). All piano-specific behavior stays behind
// this one interface (iron rule: no FP-30X hacks in the core).
//
// `final` so App's concrete by-value member devirtualizes every call —
// the seam costs nothing on the latency path.

#include <cstdint>
#include <functional>

#include "vialucis/midi_io.h"

namespace vialucis {

class BleMidiIo final : public MidiIo {
public:
    // Scans for the first BLE-MIDI peripheral and keeps reconnecting.
    void begin() override;
    void poll() override;  // call every loop() iteration — pumps the MIDI parser

    void onNoteOn(NoteHandler h) override;   // user pressed a key (velocity > 0)
    void onNoteOff(NoteHandler h) override;  // key released

    // ANY incoming MIDI message (notes, pedal, CC) — the ModeDirector's
    // AFK-wake signal (M2). Device-side only; not part of the core seam.
    void onActivity(std::function<void()> h);

    // Sustain pedal (CC64) edges for the Reactive layer's latch (E2).
    void onPedal(std::function<void(bool down)> h);

    void send(const MidiOutMsg& msg) override;
    bool connected() const override;
};

}  // namespace vialucis
