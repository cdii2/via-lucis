#pragma once
// ScoreFollower (P4) — the score-follow Presentation clock (DESIGN-lightshow
// §4a, VL6/A53). A light show needs a smooth, continuous clock; a performer
// gives discrete, unpredictable evidence (note onsets at unknown real times).
// This estimates a continuous song position from those onsets: a wait-mode-
// style matcher over the FOLLOW-TRACK anchors that, where wait mode HALTS at
// a barrier, SNAPS the clock to the matched anchor and EXTRAPOLATES forward
// at a clamped hybrid tempo until the next match.
//
// PURE and native-testable: feed a scripted (note, realTimeUs) stream via
// onNote(), read the emitted clock trajectory via positionUs(). No scheduler
// reference is held — anchors are extracted once at show start (the same
// chord-onset cadence wait mode arms barriers on) and handed in by value.
//
// Robustness contract (§4a Q8–Q12 — forgiving, forward-biased, never-lurch):
//   - wrong/extra notes are IGNORED for the clock and never surface a
//     "wrong" verdict (performance mode: no red flash — the caller wires the
//     follower on a path where practice verdicts are structurally inert);
//   - performer ahead → bounded look-ahead (depth + song-time window), snap
//     to the FURTHEST confident match;
//   - performer goes back (replays an earlier run) → forward-only RE-ARM at
//     the new position: a backward snap the consumer treats as a seek (the
//     effect backward-seek reset handles visuals);
//   - rests/fermatas → COAST at the last tempo to the next scored onset,
//     then HOLD (the clock never runs past unplayed music);
//   - lost / low confidence → FALL BACK TO FREE-RUN at the last tempo and
//     AUTO-RE-ACQUIRE when confident matches resume (the safety net).
//
// All matching parameters are named firmware constants (§4a Q15): tunable in
// native tests, no per-show TLV config in v1.

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

#include "vialucis/echo_guard.h"
#include "vialucis/scheduler.h"

namespace vialucis {

// One anchor = one chord onset of the follow scope (the exact cadence wait
// mode arms barriers on). `notes` is deduped.
struct FollowAnchor {
    uint64_t timeUs = 0;  // song time
    std::vector<uint8_t> notes;
};

class ScoreFollower {
public:
    enum class FollowState : uint8_t {
        AwaitingFirst,  // pre-rolled at position 0; first matched anchor
                        // starts the clock (§4a Q13 — no count-in)
        Following,      // snap-and-extrapolate between anchors, hold at next
        FreeRun,        // lost: coast at last tempo, auto-re-acquire
    };

    // ---- test-tunable constants (§4a Q15: firmware constants, no TLV) ----
    // Hybrid tempo band: score-seeded (1.0), blended toward the performer's
    // measured inter-anchor tempo, clamped to a sane band (§4a ≈25–300%).
    static constexpr float kTempoMin = 0.25f;
    static constexpr float kTempoMax = 3.0f;
    static constexpr float kTempoBlend = 0.35f;  // EMA weight per snap
    // Generous chord window: notes of one anchor must land within this real
    // time of each other; a staler partial restarts with the new note.
    static constexpr uint64_t kChordGatherUs = 600000;
    // Bounded look-ahead (§4a Q9): match against the next few anchors only,
    // capped in both anchor count and song time — a coincidental far-away
    // pitch must never teleport the clock.
    static constexpr size_t kLookAheadAnchors = 4;
    static constexpr uint64_t kLookAheadWindowUs = 8000000;
    // Lost detection: this many unmatched notes while Following drops the
    // clock to free-run (silence is NOT lost — silence holds, a fermata).
    static constexpr uint32_t kLostMissThreshold = 4;
    // Re-acquire evidence: the newest unmatched notes, if they confidently
    // walk this many CONSECUTIVE anchors anywhere in the score, re-arm
    // there (covers both the backward run and far skip-ahead cases).
    static constexpr size_t kMissRingSize = 8;
    static constexpr size_t kReacquireAnchors = 3;
    static constexpr uint64_t kMissMaxAgeUs = 4000000;

    // Arm over a fresh anchor table (show start / re-start). Resets every
    // matcher and clock state; the show is pre-rolled at position 0.
    void arm(std::vector<FollowAnchor>&& anchors);

    // Iron rule: never treat our own emitted notes as the performer.
    void setEchoGuard(EchoGuard* g) { guard_ = g; }

    // The performer's note-on stream (echo-guarded inside). May snap the
    // clock forward (match / look-ahead), backward (re-arm / re-acquire),
    // or do nothing (wrong note — ignored for the clock, never a stall).
    void onNote(uint8_t note, uint64_t realUs);

    // The clock: estimated song position at real time `realUs`. Pure —
    // drives Scheduler position each tick and is asserted directly in tests.
    uint64_t positionUs(uint64_t realUs) const;

    FollowState state() const { return state_; }
    bool started() const { return state_ != FollowState::AwaitingFirst; }
    float tempoFactor() const { return tempo_; }  // song-us per real-us
    size_t nextAnchor() const { return cur_; }
    size_t anchorCount() const { return anchors_.size(); }

    // Extract the anchor table from a scheduled song: each chord onset of
    // the masked-in tracks, in time order — the barrier cadence, verbatim.
    static void extractAnchors(const Scheduler& sched, uint32_t followMask,
                               std::vector<FollowAnchor>& out);

private:
    // 128-bit MIDI-note membership set (zero-alloc chord clearing).
    struct NoteSet {
        uint64_t lo = 0, hi = 0;
        bool test(uint8_t n) const {
            return n < 64 ? ((lo >> n) & 1u) != 0
                          : n < 128 && ((hi >> (n - 64)) & 1u) != 0;
        }
        void set(uint8_t n) {
            if (n < 64) lo |= 1ull << n;
            else if (n < 128) hi |= 1ull << (n - 64);
        }
    };
    // Per look-ahead candidate: which of its chord notes cleared, when the
    // first cleared (the gather window), how many (the "enough" count).
    struct Slot {
        NoteSet cleared;
        uint8_t count = 0;
        uint64_t firstUs = 0;
        void reset() { cleared = NoteSet{}; count = 0; firstUs = 0; }
    };
    struct Miss {
        uint8_t note = 0;
        uint64_t us = 0;
    };

    // "Enough" chord match (§4a Q7): core notes present, inner voices
    // tolerated. 1–2 note anchors need all; larger need a majority.
    static size_t requiredFor(size_t chordSize) {
        return chordSize <= 2 ? chordSize : chordSize / 2 + 1;
    }
    static bool anchorHas(const FollowAnchor& a, uint8_t note) {
        for (uint8_t n : a.notes)
            if (n == note) return true;
        return false;
    }

    void snapTo(size_t idx, uint64_t realUs, bool measureTempo);
    void resetSlots() {
        for (Slot& s : slots_) s.reset();
    }
    void pushMiss(uint8_t note, uint64_t realUs);
    const Miss& missAt(size_t i) const {  // i = 0 → oldest kept entry
        return ring_[(ringHead_ + i) % kMissRingSize];
    }
    // The miss-run re-acquire scan; returns the anchor to re-arm at, or -1.
    long reacquireTarget(uint64_t nowUs) const;
    // Walk ring entries [p, ringSize_) over anchors from `s`; returns the
    // last COMPLETED anchor index when the whole suffix fits and at least
    // kReacquireAnchors anchors completed, else -1.
    long walkFrom(size_t s, size_t p) const;

    std::vector<FollowAnchor> anchors_;
    EchoGuard* guard_ = nullptr;

    FollowState state_ = FollowState::AwaitingFirst;
    size_t cur_ = 0;  // next expected anchor (window base)
    std::array<Slot, kLookAheadAnchors> slots_;

    // The clock model: song position = base + (real − baseReal) · tempo,
    // held at the next anchor while Following.
    uint64_t baseSongUs_ = 0;
    uint64_t baseRealUs_ = 0;
    float tempo_ = 1.0f;  // seeded from the score (§4a Q5)

    // Last snap, for the measured inter-anchor tempo.
    bool haveSnap_ = false;
    uint64_t lastSnapSongUs_ = 0;
    uint64_t lastSnapRealUs_ = 0;

    // Miss bookkeeping (lost detection + re-acquire evidence).
    uint32_t missCount_ = 0;
    std::array<Miss, kMissRingSize> ring_{};
    size_t ringHead_ = 0;
    size_t ringSize_ = 0;
};

}  // namespace vialucis
