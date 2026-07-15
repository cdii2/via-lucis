#include "vialucis/wait_mode.h"

#include <algorithm>

namespace vialucis {

void WaitMode::armFrom(uint64_t us) {
    pending_.clear();
    cleared_.clear();
    chordLoaded_ = false;
    barrierTime_ = sched_.nextOnsetAfter(us, mask_);
    // A-6 / G3 / A90: while looping, a practiced onset at or beyond loopEnd is
    // never reached — the loop wraps first. Left alone the barrier arms past
    // the loop and never holds, so wait mode silently degrades to follow-along
    // forever (a practice softlock). Instead HOLD at loopEnd: the dead loop
    // becomes visible (state "waiting" at the boundary) and the player can fix
    // the loop range or the practiced hand. Only clamp when arming from
    // strictly inside the loop (us < loopEnd); an onset within the loop still
    // gates normally, and an onset exactly at loopEnd already coincides with
    // this hold point (it gates as usual).
    if (sched_.loopEnabled() && us < sched_.loopEndUs() &&
        (barrierTime_ == kNoOnset || barrierTime_ >= sched_.loopEndUs())) {
        barrierTime_ = sched_.loopEndUs();
        sched_.setBarrier(barrierTime_);
        return;
    }
    if (barrierTime_ == kNoOnset)
        sched_.clearBarrier();  // no more practiced notes: run free
    else
        sched_.setBarrier(barrierTime_);
}

bool WaitMode::update() {
    if (barrierTime_ == kNoOnset || chordLoaded_ || !sched_.atBarrier())
        return false;
    // A98/G18: gather practiced onsets in the half-open window
    // [barrierTime_, barrierTime_ + kChordEpsilonUs) into ONE barrier chord —
    // integer-microsecond math on the existing sorted-events walk, no alloc
    // (chordBuf_ is reused). Cap the window at loopEnd while looping: onsets
    // strictly past loopEnd are never reached (A90), so they must not be
    // absorbed; onsets exactly AT loopEnd still gate as usual (loopEndUs()+1
    // keeps the half-open end inclusive of that instant).
    uint64_t windowEnd = barrierTime_ + kChordEpsilonUs;
    if (sched_.loopEnabled() && sched_.loopEndUs() + 1 < windowEnd)
        windowEnd = sched_.loopEndUs() + 1;
    sched_.notesInWindow(barrierTime_, windowEnd, mask_, chordBuf_);
    lastAbsorbedOnsetUs_ = barrierTime_;
    for (const SchedEvent& e : chordBuf_) {
        if (e.timeUs > lastAbsorbedOnsetUs_) lastAbsorbedOnsetUs_ = e.timeUs;
        if (std::find(pending_.begin(), pending_.end(), e.note) ==
            pending_.end())
            pending_.push_back(e.note);
    }
    // Same-key re-dues: members of this chord that were also in the last
    // one. History updates here too — one owner for the chord lifecycle.
    reDue_.clear();
    for (uint8_t n : pending_)
        if (std::find(lastChordKeys_.begin(), lastChordKeys_.end(), n) !=
            lastChordKeys_.end())
            reDue_.push_back(n);
    lastChordKeys_ = pending_;
    chordLoaded_ = true;
    return true;
}

KeyFeedback WaitMode::onKeyDown(uint8_t note, uint64_t nowUs) {
    if (guard_ && guard_->shouldIgnore(note, nowUs))
        return {KeyVerdict::Echo, note};

    if (!chordLoaded_ || pending_.empty())
        return {KeyVerdict::Ignored, note};

    auto it = std::find(pending_.begin(), pending_.end(), note);
    if (it != pending_.end()) {
        pending_.erase(it);
        cleared_.push_back(note);
        if (pending_.empty()) {
            // Chord complete: release and arm the next one. Arm strictly AFTER
            // the LAST absorbed onset (A98/G18), not after barrierTime_: a
            // together-press absorbed up to +epsilon must not leave its later
            // sub-ms onset behind as a phantom second barrier (the softlock).
            uint64_t resumeFrom = lastAbsorbedOnsetUs_ + 1;
            sched_.clearBarrier();
            armFrom(resumeFrom);
        }
        return {KeyVerdict::Cleared, note};
    }

    if (std::find(cleared_.begin(), cleared_.end(), note) != cleared_.end())
        return {KeyVerdict::Ignored, note};  // re-strike, not an error

    return {KeyVerdict::Wrong, note};
}

}  // namespace vialucis
