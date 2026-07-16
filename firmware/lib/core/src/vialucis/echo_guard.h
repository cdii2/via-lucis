#pragma once
// MIDI echo guard (iron rule): notes WE send to the piano may come back on its
// MIDI out. Each send earns exactly one "ignore" for that note within a short
// window, so our own demo/accompaniment notes never count as key presses.

#include <cstdint>

namespace vialucis {

class EchoGuard {
public:
    // Record that we transmitted a note-on to the piano.
    //
    // FIX-C (deferred to hardware bring-up, BUGFIX-PLAN-2026-07-15 §3-E/§7 —
    // comment only, mechanism NOT built here): every caller of this method
    // (NoteEmitter::consume(), firmware/lib/core/src/vialucis/note_emitter.cpp)
    // registers the credit at EMIT time — the instant a note-on is scheduled
    // for send — not at confirmed BLE-transmit time. The actual transmit
    // (BleMidiIo::send(), firmware/src/ble_midi_io.cpp) silently no-ops while
    // disconnected, so a note that never reached the piano can still hold an
    // outstanding "ignore the next echo" credit. If the piano reconnects
    // while that credit is still un-expired and the player genuinely presses
    // the same key, the echo guard swallows the real press. Fixing this
    // needs a live disconnect/reconnect to verify against real hardware
    // (move registration to transmit-confirmed time, or drop credits for a
    // note whose send() no-oped) — hence deferred, not built in this wave.
    void noteSent(uint8_t note, uint64_t nowUs) {
        if (note > 127) return;
        Slot& s = slots_[note];
        if (nowUs > s.expiry) s.count = 0;  // stale credits die first
        if (s.count < 255) ++s.count;
        s.expiry = nowUs + windowUs_;
    }

    // True exactly once per pending un-expired send of this note.
    bool shouldIgnore(uint8_t note, uint64_t nowUs) {
        if (note > 127) return false;
        Slot& s = slots_[note];
        if (s.count == 0) return false;
        if (nowUs > s.expiry) {
            s.count = 0;
            return false;
        }
        --s.count;
        return true;
    }

    uint64_t windowUs() const { return windowUs_; }
    void setWindowUs(uint64_t w) { windowUs_ = w; }

    // Drop every outstanding credit (song unload: nothing we sent is owed
    // an echo anymore — stale credits must not leak into the next session).
    void clearCredits() {
        for (Slot& s : slots_) s.count = 0;
    }

private:
    struct Slot {
        uint8_t count = 0;
        uint64_t expiry = 0;
    };
    Slot slots_[128];
    // BLE round-trip is tens of ms; 250ms catches slow echoes without eating
    // genuine re-presses. Verify against the real FP-30X during bring-up.
    uint64_t windowUs_ = 250000;
};

}  // namespace vialucis
