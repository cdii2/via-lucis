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

// A98/G18 chord-gather epsilon: practiced onsets closer together than this are
// one barrier chord (the gate absorbs MIDI-export slop / rounding). Below
// deliberate human two-note playability, so a real roll/flam (gap >= epsilon)
// still gates one press at a time. Compile-time constant ONLY — never a
// settings key. The song timeline/audio is NEVER touched; this lives purely in
// the wait-gate. Revisit at hardware bring-up.
constexpr uint64_t kChordEpsilonUs = 10000;  // 10 ms

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

    // Re-arm after a seek or loop wrap moved the song position. Chord
    // history dies with the position — a re-due can only be claimed across
    // two chords the player actually walked through.
    void resync() {
        sched_.clearBarrier();
        lastChordKeys_.clear();
        armFrom(sched_.positionUs());
    }

    // Call after each Scheduler::advance(): loads the due chord when the
    // scheduler is holding at our barrier. Returns true exactly when a NEW
    // chord loaded on this call — the chord-lifecycle edge belongs HERE,
    // not to a caller-side barrier-time mirror (Q-wave closing review).
    bool update();

    // Keys of the just-loaded chord that were ALSO in the previous chord —
    // same-key re-dues (the Q2 pulse's input). Valid after update()==true.
    const std::vector<uint8_t>& reDueKeys() const { return reDue_; }

    KeyFeedback onKeyDown(uint8_t note, uint64_t nowUs);

    bool chordPending() const { return !pending_.empty(); }
    // Membership in the due chord — the one definition of "still owed"
    // (renderers must not re-derive this from the raw vector).
    bool isPending(uint8_t note) const {
        for (uint8_t p : pending_)
            if (p == note) return true;
        return false;
    }
    const std::vector<uint8_t>& pendingNotes() const { return pending_; }
    uint64_t barrierTimeUs() const { return barrierTime_; }

private:
    void armFrom(uint64_t us);

    Scheduler& sched_;
    uint32_t mask_;
    EchoGuard* guard_ = nullptr;
    std::vector<SchedEvent> chordBuf_;  // reused by update() — no per-tick alloc
    std::vector<uint8_t> pending_;
    std::vector<uint8_t> cleared_;  // members already struck this chord
    std::vector<uint8_t> lastChordKeys_;  // previous chord (re-due compare)
    std::vector<uint8_t> reDue_;
    uint64_t barrierTime_ = kNoOnset;
    // Latest onset absorbed into the current barrier chord (A98/G18). The next
    // barrier arms strictly AFTER this, not after barrierTime_ — else a note a
    // tick past the barrier re-arms as a phantom second barrier (the softlock).
    uint64_t lastAbsorbedOnsetUs_ = kNoOnset;
    bool chordLoaded_ = false;
};

}  // namespace vialucis
