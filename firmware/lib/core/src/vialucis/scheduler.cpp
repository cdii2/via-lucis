#include "vialucis/scheduler.h"

#include <algorithm>
#include <cmath>

namespace vialucis {

Scheduler::Scheduler(const MidiSong& song) {
    events_.reserve(song.notes.size() * 2 + song.pedal.size());
    for (const MidiNote& n : song.notes) {
        events_.push_back({SchedEventType::NoteOn, tickToMicros(song, n.onTick),
                           n.note, n.velocity, n.channel, n.track});
        events_.push_back({SchedEventType::NoteOff, tickToMicros(song, n.offTick),
                           n.note, 0, n.channel, n.track});
    }
    for (const PedalEvent& p : song.pedal) {
        events_.push_back({SchedEventType::Pedal, tickToMicros(song, p.tick),
                           64, p.value, p.channel, p.track});
    }
    std::stable_sort(events_.begin(), events_.end(),
                     [](const SchedEvent& a, const SchedEvent& b) {
                         if (a.timeUs != b.timeUs) return a.timeUs < b.timeUs;
                         return static_cast<uint8_t>(a.type) <
                                static_cast<uint8_t>(b.type);
                     });
    for (const SchedEvent& e : events_)
        duration_ = std::max(duration_, e.timeUs);
}

void Scheduler::setTempoPercent(float p) {
    tempo_ = std::min(500.0f, std::max(1.0f, p));
}

void Scheduler::setLoop(uint64_t startUs, uint64_t endUs) {
    if (endUs <= startUs) return;
    loopOn_ = true;
    loopStart_ = startUs;
    loopEnd_ = endUs;
}

void Scheduler::setBarrier(uint64_t us) {
    barrierOn_ = true;
    barrier_ = us;
}

bool Scheduler::finished() const {
    return !loopOn_ && idx_ >= events_.size() && posUs() >= duration_;
}

size_t Scheduler::indexForTime(uint64_t us) const {
    auto it = std::lower_bound(events_.begin(), events_.end(), us,
                               [](const SchedEvent& e, uint64_t t) {
                                   return e.timeUs < t;
                               });
    return static_cast<size_t>(it - events_.begin());
}

void Scheduler::emitUpTo(uint64_t stopUs, bool includeStop,
                         std::vector<SchedEvent>& out) {
    while (idx_ < events_.size() &&
           (events_[idx_].timeUs < stopUs ||
            (includeStop && events_[idx_].timeUs == stopUs))) {
        const SchedEvent& e = events_[idx_++];
        if (e.type == SchedEventType::NoteOn) {
            sounding_.add(e);
        } else if (e.type == SchedEventType::NoteOff) {
            sounding_.eraseFirst([&e](const SchedEvent& s) {
                return s.note == e.note && s.channel == e.channel &&
                       s.track == e.track;
            });
        }
        out.push_back(e);
    }
}

void Scheduler::flushSounding(uint64_t atUs, std::vector<SchedEvent>& out) {
    sounding_.drain([&](const SchedEvent& s) {
        out.push_back({SchedEventType::NoteOff, atUs, s.note, 0, s.channel,
                       s.track});
    });
}

void Scheduler::seek(uint64_t us, std::vector<SchedEvent>& out) {
    out.clear();
    if (us > duration_) us = duration_;  // G6: a seek never lands past the end
    flushSounding(us, out);
    pos_ = static_cast<double>(us);
    idx_ = indexForTime(us);
}

std::vector<SchedEvent> Scheduler::seek(uint64_t us) {
    std::vector<SchedEvent> out;
    seek(us, out);
    return out;
}

void Scheduler::advance(uint64_t realDeltaUs, std::vector<SchedEvent>& out) {
    out.clear();
    double target = pos_ + static_cast<double>(realDeltaUs) * (tempo_ / 100.0);
    const double loopLen = static_cast<double>(loopEnd_) -
                           static_cast<double>(loopStart_);

    for (int guard = 0; guard < 64; ++guard) {
        // Playhead already at/beyond loopEnd while looping: snap it back into
        // the loop instead of escaping past it. Covers a barrier cleared
        // exactly at loopEnd (G1) and a loop set wholly behind the playhead
        // (G2/A89 — the loop is authoritative: wrap in). A barrier holding at
        // this exact position wins (holding beats wrapping), so don't snap
        // while held.
        if (loopOn_ && pos_ >= static_cast<double>(loopEnd_) &&
            !(barrierOn_ && posUs() == barrier_)) {
            flushSounding(posUs(), out);
            double leftover = target - pos_;
            if (leftover >= loopLen)  // O(1) modulo collapse (G4)
                leftover -= std::floor(leftover / loopLen) * loopLen;
            pos_ = static_cast<double>(loopStart_);
            idx_ = indexForTime(loopStart_);
            target = pos_ + leftover;
            continue;
        }

        double stop = target;
        enum { kTarget, kLoop, kBarrier } reason = kTarget;

        if (loopOn_ && pos_ < static_cast<double>(loopEnd_) &&
            stop >= static_cast<double>(loopEnd_)) {
            stop = static_cast<double>(loopEnd_);
            reason = kLoop;
        }
        if (barrierOn_ && pos_ <= static_cast<double>(barrier_) &&
            stop >= static_cast<double>(barrier_)) {
            stop = static_cast<double>(barrier_);
            reason = kBarrier;  // barrier wins ties: holding beats wrapping
        }

        uint64_t stopUs = static_cast<uint64_t>(stop);
        emitUpTo(stopUs, /*includeStop=*/reason != kBarrier, out);
        pos_ = stop;

        if (reason == kBarrier) return;
        if (reason == kLoop) {
            flushSounding(loopEnd_, out);
            double leftover = target - static_cast<double>(loopEnd_);
            if (leftover >= loopLen)  // O(1) modulo collapse (G4): a lag spike
                leftover -= std::floor(leftover / loopLen) * loopLen;
            pos_ = static_cast<double>(loopStart_);
            idx_ = indexForTime(loopStart_);
            target = pos_ + leftover;
            continue;
        }
        break;
    }

    if (!loopOn_ && pos_ > static_cast<double>(duration_) &&
        idx_ >= events_.size())
        pos_ = static_cast<double>(duration_);
}

std::vector<SchedEvent> Scheduler::advance(uint64_t realDeltaUs) {
    std::vector<SchedEvent> out;
    advance(realDeltaUs, out);
    return out;
}

void Scheduler::notesOnAt(uint64_t us, uint32_t trackMask,
                          std::vector<SchedEvent>& out) const {
    out.clear();
    for (size_t i = indexForTime(us);
         i < events_.size() && events_[i].timeUs == us; ++i) {
        const SchedEvent& e = events_[i];
        if (e.type == SchedEventType::NoteOn && trackInMask(trackMask, e.track))
            out.push_back(e);
    }
}

std::vector<SchedEvent> Scheduler::notesOnAt(uint64_t us,
                                             uint32_t trackMask) const {
    std::vector<SchedEvent> out;
    notesOnAt(us, trackMask, out);
    return out;
}

void Scheduler::onsetsBetween(uint64_t fromUs, uint64_t toUs,
                              uint32_t trackMask,
                              std::vector<SchedEvent>& out) const {
    out.clear();
    for (size_t i = indexForTime(fromUs);
         i < events_.size() && events_[i].timeUs <= toUs; ++i) {
        const SchedEvent& e = events_[i];
        if (e.type == SchedEventType::NoteOn && trackInMask(trackMask, e.track))
            out.push_back(e);
    }
}

std::vector<SchedEvent> Scheduler::onsetsBetween(uint64_t fromUs, uint64_t toUs,
                                                 uint32_t trackMask) const {
    std::vector<SchedEvent> out;
    onsetsBetween(fromUs, toUs, trackMask, out);
    return out;
}

uint64_t Scheduler::nextOnsetAfter(uint64_t us, uint32_t trackMask) const {
    for (size_t i = indexForTime(us); i < events_.size(); ++i) {
        const SchedEvent& e = events_[i];
        if (e.type == SchedEventType::NoteOn && trackInMask(trackMask, e.track))
            return e.timeUs;
    }
    return kNoOnset;
}

}  // namespace vialucis
