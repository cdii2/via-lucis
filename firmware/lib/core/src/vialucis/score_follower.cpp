#include "vialucis/score_follower.h"

namespace vialucis {

namespace {
float clampTempo(float t) {
    if (t < ScoreFollower::kTempoMin) return ScoreFollower::kTempoMin;
    if (t > ScoreFollower::kTempoMax) return ScoreFollower::kTempoMax;
    return t;
}
}  // namespace

void ScoreFollower::arm(std::vector<FollowAnchor>&& anchors) {
    anchors_ = std::move(anchors);
    state_ = FollowState::AwaitingFirst;
    cur_ = 0;
    resetSlots();
    baseSongUs_ = 0;
    baseRealUs_ = 0;
    tempo_ = 1.0f;  // score-seeded (§4a Q5)
    haveSnap_ = false;
    lastSnapSongUs_ = 0;
    lastSnapRealUs_ = 0;
    missCount_ = 0;
    ringHead_ = 0;
    ringSize_ = 0;
}

uint64_t ScoreFollower::positionUs(uint64_t realUs) const {
    if (state_ == FollowState::AwaitingFirst) return 0;  // pre-rolled
    uint64_t est = baseSongUs_;
    if (realUs > baseRealUs_) {
        est = baseSongUs_ +
              static_cast<uint64_t>(
                  static_cast<double>(realUs - baseRealUs_) *
                  static_cast<double>(tempo_));
    }
    // Coast to the next scored onset, then HOLD (§4a Q11): the clock never
    // runs past unplayed music while Following. FreeRun doesn't hold (the
    // show never freezes — §4a Q12); past the final anchor the show tail
    // plays out at the last tempo (nothing left to wait for).
    if (state_ == FollowState::Following && cur_ < anchors_.size() &&
        est > anchors_[cur_].timeUs)
        est = anchors_[cur_].timeUs;
    return est;
}

void ScoreFollower::snapTo(size_t idx, uint64_t realUs, bool measureTempo) {
    uint64_t anchorUs = anchors_[idx].timeUs;
    // Hybrid tempo (§4a Q5): blend toward the measured inter-anchor tempo,
    // but ONLY across a consecutive-anchor interval while Following — a
    // look-ahead skip or a re-acquire jump measures omission/silence, not
    // tempo. Clamped to the sane band both before and after blending.
    if (measureTempo && state_ == FollowState::Following && haveSnap_ &&
        anchorUs > lastSnapSongUs_ && realUs > lastSnapRealUs_) {
        float measured = static_cast<float>(
            static_cast<double>(anchorUs - lastSnapSongUs_) /
            static_cast<double>(realUs - lastSnapRealUs_));
        measured = clampTempo(measured);
        tempo_ = clampTempo(tempo_ + kTempoBlend * (measured - tempo_));
    }
    baseSongUs_ = anchorUs;
    baseRealUs_ = realUs;
    lastSnapSongUs_ = anchorUs;
    lastSnapRealUs_ = realUs;
    haveSnap_ = true;
    cur_ = idx + 1;
    resetSlots();
    missCount_ = 0;
    ringSize_ = 0;  // evidence consumed — a stale run must not re-fire
    state_ = FollowState::Following;
}

void ScoreFollower::pushMiss(uint8_t note, uint64_t realUs) {
    if (ringSize_ < kMissRingSize) {
        ring_[(ringHead_ + ringSize_) % kMissRingSize] = {note, realUs};
        ++ringSize_;
    } else {
        ring_[ringHead_] = {note, realUs};
        ringHead_ = (ringHead_ + 1) % kMissRingSize;
    }
}

long ScoreFollower::walkFrom(size_t s, size_t p) const {
    size_t j = s;
    size_t matched = 0;
    NoteSet cleared;
    size_t clearedCount = 0;
    for (size_t i = p; i < ringSize_; ++i) {
        uint8_t note = missAt(i).note;
        if (j >= anchors_.size()) {
            // Ran off the end mid-suffix: only spillover of the final
            // completed anchor is tolerable.
            if (matched > 0 && anchorHas(anchors_[j - 1], note)) continue;
            return -1;
        }
        const FollowAnchor& a = anchors_[j];
        if (!anchorHas(a, note)) {
            // Tolerate spillover: an extra inner voice of the anchor that
            // just completed (§4a Q7 — extra voices tolerated).
            if (j > s && anchorHas(anchors_[j - 1], note)) continue;
            return -1;
        }
        if (cleared.test(note)) continue;  // re-strike within the chord
        cleared.set(note);
        ++clearedCount;
        if (clearedCount >= requiredFor(a.notes.size())) {
            ++matched;
            ++j;
            cleared = NoteSet{};
            clearedCount = 0;
        }
    }
    if (matched >= kReacquireAnchors) return static_cast<long>(j) - 1;
    return -1;
}

long ScoreFollower::reacquireTarget(uint64_t nowUs) const {
    // Only fresh evidence counts.
    size_t first = 0;
    while (first < ringSize_ && nowUs - missAt(first).us > kMissMaxAgeUs)
        ++first;
    size_t n = anchors_.size();
    // Longest usable suffix first — more evidence beats less. Among equal
    // evidence, prefer the walk ending nearest the current position (the
    // performer most likely jumped locally).
    for (size_t p = first; p < ringSize_; ++p) {
        if (ringSize_ - p < kReacquireAnchors) break;  // can't complete
        long best = -1;
        unsigned long bestDist = ~0ul;
        for (size_t s = 0; s < n; ++s) {
            long end = walkFrom(s, p);
            if (end < 0) continue;
            unsigned long dist =
                end >= static_cast<long>(cur_)
                    ? static_cast<unsigned long>(end - static_cast<long>(cur_))
                    : static_cast<unsigned long>(static_cast<long>(cur_) - end);
            if (dist < bestDist) {
                bestDist = dist;
                best = end;
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

void ScoreFollower::onNote(uint8_t note, uint64_t realUs) {
    if (anchors_.empty()) return;
    // Iron rule: our own emitted notes are never the performer.
    if (guard_ && guard_->shouldIgnore(note, realUs)) return;

    // 1) Credit the look-ahead window (§4a Q9: bounded in depth AND song
    //    time). Each press credits the NEAREST candidate that still needs
    //    the note (wait-mode's greedy per-key clearing) — one press of a
    //    repeated melody note must advance one anchor, never several. A
    //    fully-played FURTHER anchor completes its own slot: the skip-ahead
    //    snap to the furthest confident match falls out naturally.
    size_t n = anchors_.size();
    bool restrike = false;
    long creditSlot = -1;
    for (size_t k = 0; k < kLookAheadAnchors; ++k) {
        size_t idx = cur_ + k;
        if (idx >= n) break;
        if (anchors_[idx].timeUs >
            anchors_[cur_].timeUs + kLookAheadWindowUs)
            break;
        if (!anchorHas(anchors_[idx], note)) continue;
        if (slots_[k].cleared.test(note)) {
            restrike = true;  // wait-mode's rule: a re-strike isn't a miss
            continue;
        }
        creditSlot = static_cast<long>(k);
        break;
    }
    if (creditSlot >= 0) {
        size_t k = static_cast<size_t>(creditSlot);
        Slot& s = slots_[k];
        // A partial chord older than the gather window is stale — restart
        // it with this note (per-key clearing, generous timing window).
        if (s.count > 0 && realUs - s.firstUs > kChordGatherUs) s.reset();
        if (s.count == 0) s.firstUs = realUs;
        s.cleared.set(note);
        ++s.count;
        if (s.count >= requiredFor(anchors_[cur_ + k].notes.size())) {
            // Consecutive-anchor matches measure tempo; skips don't.
            snapTo(cur_ + k, realUs, /*measureTempo=*/k == 0);
        }
        return;
    }
    if (restrike) return;  // not a miss

    // 2) A miss: wrong/extra/elsewhere. Ignored for the clock (§4a Q8 — no
    //    rewind, no stall, no red flash), but it IS evidence for the lost
    //    and re-acquire machinery.
    pushMiss(note, realUs);
    if (state_ == FollowState::Following &&
        ++missCount_ >= kLostMissThreshold) {
        // Lost: release the hold and free-run from the current estimate at
        // the last tempo (§4a Q12). Rebase FIRST so the transition is
        // continuous — no jump past the held anchor.
        uint64_t here = positionUs(realUs);
        state_ = FollowState::FreeRun;
        baseSongUs_ = here;
        baseRealUs_ = realUs;
    }
    // Re-acquire / re-arm (§4a Q10, Q12): a confident walk of consecutive
    // anchors anywhere in the score — the backward run, the far skip, or
    // the performer resurfacing after free-run — snaps the clock there.
    long target = reacquireTarget(realUs);
    if (target >= 0)
        snapTo(static_cast<size_t>(target), realUs, /*measureTempo=*/false);
}

void ScoreFollower::extractAnchors(const Scheduler& sched, uint32_t followMask,
                                   std::vector<FollowAnchor>& out) {
    out.clear();
    std::vector<SchedEvent> buf;
    uint64_t t = sched.nextOnsetAfter(0, followMask);
    while (t != kNoOnset) {
        sched.notesOnAt(t, followMask, buf);
        FollowAnchor a;
        a.timeUs = t;
        for (const SchedEvent& e : buf)
            if (!anchorHas(a, e.note)) a.notes.push_back(e.note);
        if (!a.notes.empty()) out.push_back(std::move(a));
        t = sched.nextOnsetAfter(t + 1, followMask);
    }
}

}  // namespace vialucis
