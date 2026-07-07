#include "vialucis/scheduler.h"

#include <algorithm>

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
            sounding_.push_back(e);
        } else if (e.type == SchedEventType::NoteOff) {
            for (size_t i = 0; i < sounding_.size(); ++i) {
                if (sounding_[i].note == e.note &&
                    sounding_[i].channel == e.channel &&
                    sounding_[i].track == e.track) {
                    sounding_.erase(sounding_.begin() + i);
                    break;
                }
            }
        }
        out.push_back(e);
    }
}

void Scheduler::flushSounding(uint64_t atUs, std::vector<SchedEvent>& out) {
    for (const SchedEvent& s : sounding_)
        out.push_back({SchedEventType::NoteOff, atUs, s.note, 0, s.channel,
                       s.track});
    sounding_.clear();
}

std::vector<SchedEvent> Scheduler::seek(uint64_t us) {
    std::vector<SchedEvent> out;
    flushSounding(us, out);
    pos_ = static_cast<double>(us);
    idx_ = indexForTime(us);
    return out;
}

std::vector<SchedEvent> Scheduler::advance(uint64_t realDeltaUs) {
    std::vector<SchedEvent> out;
    double target = pos_ + static_cast<double>(realDeltaUs) * (tempo_ / 100.0);

    for (int guard = 0; guard < 64; ++guard) {
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

        if (reason == kBarrier) return out;
        if (reason == kLoop) {
            flushSounding(loopEnd_, out);
            double leftover = target - static_cast<double>(loopEnd_);
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
    return out;
}

std::vector<SchedEvent> Scheduler::notesOnAt(uint64_t us,
                                             uint32_t trackMask) const {
    std::vector<SchedEvent> out;
    for (size_t i = indexForTime(us);
         i < events_.size() && events_[i].timeUs == us; ++i) {
        const SchedEvent& e = events_[i];
        if (e.type == SchedEventType::NoteOn && trackInMask(trackMask, e.track))
            out.push_back(e);
    }
    return out;
}

std::vector<SchedEvent> Scheduler::onsetsBetween(uint64_t fromUs, uint64_t toUs,
                                                 uint32_t trackMask) const {
    std::vector<SchedEvent> out;
    for (size_t i = indexForTime(fromUs);
         i < events_.size() && events_[i].timeUs <= toUs; ++i) {
        const SchedEvent& e = events_[i];
        if (e.type == SchedEventType::NoteOn && trackInMask(trackMask, e.track))
            out.push_back(e);
    }
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
