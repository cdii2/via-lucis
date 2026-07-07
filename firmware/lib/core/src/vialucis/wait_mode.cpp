#include "vialucis/wait_mode.h"

#include <algorithm>

namespace vialucis {

void WaitMode::armFrom(uint64_t us) {
    pending_.clear();
    cleared_.clear();
    chordLoaded_ = false;
    barrierTime_ = sched_.nextOnsetAfter(us, mask_);
    if (barrierTime_ == kNoOnset)
        sched_.clearBarrier();  // no more practiced notes: run free
    else
        sched_.setBarrier(barrierTime_);
}

void WaitMode::update() {
    if (barrierTime_ == kNoOnset || chordLoaded_ || !sched_.atBarrier()) return;
    sched_.notesOnAt(barrierTime_, mask_, chordBuf_);
    for (const SchedEvent& e : chordBuf_) {
        if (std::find(pending_.begin(), pending_.end(), e.note) ==
            pending_.end())
            pending_.push_back(e.note);
    }
    chordLoaded_ = true;
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
            // Chord complete: release and arm the next one.
            uint64_t completed = barrierTime_;
            sched_.clearBarrier();
            armFrom(completed + 1);
        }
        return {KeyVerdict::Cleared, note};
    }

    if (std::find(cleared_.begin(), cleared_.end(), note) != cleared_.end())
        return {KeyVerdict::Ignored, note};  // re-strike, not an error

    return {KeyVerdict::Wrong, note};
}

}  // namespace vialucis
