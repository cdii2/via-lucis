#pragma once
// Wait mode — the product. Halts the song at each practiced-hand onset until
// the right key(s) are pressed; chords clear per key; wrong presses surface as
// events for the red flash. Latency-critical path: onKeyDown does no
// allocation beyond removing from a tiny pending vector.

#include <cstdint>
#include <vector>

#include "vialucis/echo_guard.h"
#include "vialucis/scheduler.h"

namespace vialucis {

enum class KeyVerdict : uint8_t {
    Cleared,  // correct pending note; light it up / release if chord done
    Wrong,    // red flash at this key
    Ignored,  // noodling between chords or re-strike of a cleared note
    Echo,     // our own transmitted note bounced back — never user input
};

struct KeyFeedback {
    KeyVerdict verdict;
    uint8_t note;
};

class WaitMode {
public:
    WaitMode(Scheduler& sched, uint32_t practicedMask)
        : sched_(sched), mask_(practicedMask) {}

    void setEchoGuard(EchoGuard* g) { guard_ = g; }
    void setPracticedMask(uint32_t m) { mask_ = m; }

    // Arm the first barrier from the current song position.
    void begin() { armFrom(sched_.positionUs()); }

    // Re-arm after a seek or loop wrap moved the song position.
    void resync() {
        sched_.clearBarrier();
        armFrom(sched_.positionUs());
    }

    // Call after each Scheduler::advance(): loads the due chord when the
    // scheduler is holding at our barrier.
    void update();

    KeyFeedback onKeyDown(uint8_t note, uint64_t nowUs);

    bool chordPending() const { return !pending_.empty(); }
    const std::vector<uint8_t>& pendingNotes() const { return pending_; }
    uint64_t barrierTimeUs() const { return barrierTime_; }

private:
    void armFrom(uint64_t us);

    Scheduler& sched_;
    uint32_t mask_;
    EchoGuard* guard_ = nullptr;
    std::vector<uint8_t> pending_;
    std::vector<uint8_t> cleared_;  // members already struck this chord
    uint64_t barrierTime_ = kNoOnset;
    bool chordLoaded_ = false;
};

}  // namespace vialucis
