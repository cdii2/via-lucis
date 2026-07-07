#pragma once
// Scripted MidiIo adapter (R1, adapter 2): native tests queue key presses
// with script*() and pump them with poll(), mirroring how the real adapter
// only dispatches during MIDI.read(). Outgoing messages are recorded in
// sent() for assertions. Mirrors the BLE adapter's running-status contract:
// a scripted note-on with velocity 0 dispatches as a note-off.

#include <cstdint>
#include <vector>

#include "vialucis/midi_io.h"

namespace vialucis {

class FakeMidiIo final : public MidiIo {
public:
    void begin() override { began_ = true; }

    void poll() override {
        // Swap-drain so handlers that script more events don't invalidate
        // the iteration.
        std::vector<Scripted> due;
        due.swap(script_);
        for (const Scripted& s : due) {
            if (s.off || s.velocity == 0) {  // running-status note-off
                if (offHandler_) offHandler_(s.note, s.off ? s.velocity : 0);
            } else {
                if (onHandler_) onHandler_(s.note, s.velocity);
            }
        }
    }

    void onNoteOn(NoteHandler h) override { onHandler_ = std::move(h); }
    void onNoteOff(NoteHandler h) override { offHandler_ = std::move(h); }

    void send(const MidiOutMsg& msg) override {
        if (connected_) sent_.push_back(msg);
    }

    bool connected() const override { return connected_; }

    // --- scripting surface (not part of the seam) ------------------------
    void scriptNoteOn(uint8_t note, uint8_t velocity) {
        script_.push_back({note, velocity, false});
    }
    void scriptNoteOff(uint8_t note, uint8_t velocity = 0) {
        script_.push_back({note, velocity, true});
    }
    void setConnected(bool c) { connected_ = c; }

    const std::vector<MidiOutMsg>& sent() const { return sent_; }
    void clearSent() { sent_.clear(); }
    bool began() const { return began_; }

private:
    struct Scripted {
        uint8_t note;
        uint8_t velocity;
        bool off;
    };

    NoteHandler onHandler_;
    NoteHandler offHandler_;
    std::vector<Scripted> script_;
    std::vector<MidiOutMsg> sent_;
    bool connected_ = true;
    bool began_ = false;
};

}  // namespace vialucis
