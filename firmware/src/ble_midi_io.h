#pragma once
// BLE-MIDI client wrapper — the device adapter of the MidiIo seam. The
// FP-30X is a BLE peripheral; we connect as a client (lathoub
// Arduino-BLE-MIDI over NimBLE). All piano-specific behavior stays behind
// this one interface (iron rule: no FP-30X hacks in the core).
//
// `final` so App's concrete by-value member devirtualizes every call —
// the seam costs nothing on the latency path.
//
// Scan backoff (Wave E2, BUGFIX-PLAN §3-E item 1): the lathoub library's
// scan/reconnect cycle is entirely poll-driven (no library-side cooldown) —
// poll() throttles how often it lets a DISCONNECTED call through to
// MIDI.read() (bounded exponential backoff), which is the only lever
// available without vendoring/forking the third-party library. A connected
// link is NEVER throttled (iron rule: the latency path stays untouched).
//
// gConnected (ble_midi_io.cpp) is a std::atomic<bool>: written from the BLE
// connect/disconnect callbacks, read from connected() on other tasks.

#include <cstdint>
#include <functional>
#include <string>

#include "vialucis/midi_io.h"

namespace vialucis {

class BleMidiIo final : public MidiIo {
public:
    // Optional target-name filter (Wave E2, BUGFIX-PLAN §3-E item 2): call
    // BEFORE begin() — begin() reads it once at startup, same as wifiSsid,
    // so a change made after boot needs a reboot to take effect. Empty (the
    // default if never called) = accept the first BLE-MIDI peripheral found,
    // unchanged behavior (replicability iron rule).
    void setTargetName(const std::string& name);

    // Scans for the first BLE-MIDI peripheral (or, if setTargetName() was
    // called first, one whose advertised name matches) and keeps
    // reconnecting with a backoff while none is found.
    void begin() override;
    void poll() override;  // call every loop() iteration — pumps the MIDI parser

    void onNoteOn(NoteHandler h) override;   // user pressed a key (velocity > 0)
    void onNoteOff(NoteHandler h) override;  // key released

    // ANY incoming MIDI message (notes, pedal, CC) — the ModeDirector's
    // AFK-wake signal (M2). Device-side only; not part of the core seam.
    void onActivity(std::function<void()> h);

    // Sustain pedal (CC64) — the RAW 0-127 value (REC3): the Reactive layer
    // latches on value>=64, and a recording take stores the real value.
    void onPedal(std::function<void(uint8_t value)> h);

    void send(const MidiOutMsg& msg) override;
    bool connected() const override;
};

}  // namespace vialucis
