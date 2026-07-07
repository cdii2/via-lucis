#pragma once
// MIDI echo guard (iron rule): notes WE send to the piano may come back on its
// MIDI out. Each send earns exactly one "ignore" for that note within a short
// window, so our own demo/accompaniment notes never count as key presses.

#include <cstdint>

namespace vialucis {

class EchoGuard {
public:
    // Record that we transmitted a note-on to the piano.
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
