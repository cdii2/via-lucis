#pragma once
// Playback scheduler: walks a parsed song on a microsecond timeline.
//
// - Tempo 1–500% scales real time into song time (SPEC).
// - Time-range loop (seconds-based) wraps with note-off flushing.
// - Barrier = wait-mode's hold point: song time never crosses it, and events
//   AT the barrier instant stay pending until the barrier is cleared.
// - Events carry their source track; consumers filter with TrackMask (lights
//   and accompaniment want *different* filters, so the scheduler filters
//   nothing itself).

#include <cstdint>
#include <vector>

#include "vialucis/midi_parser.h"
#include "vialucis/sounding_set.h"

namespace vialucis {

enum class SchedEventType : uint8_t { NoteOff = 0, Pedal = 1, NoteOn = 2 };

struct SchedEvent {
    SchedEventType type;
    uint64_t timeUs;   // song time
    uint8_t note;      // Pedal: controller number (64)
    uint8_t velocity;  // Pedal: controller value
    uint8_t channel;
    uint8_t track;
};

constexpr uint32_t kTrackMaskAll = 0xFFFFFFFFu;
constexpr uint64_t kNoOnset = ~0ull;

constexpr uint32_t trackBit(uint8_t track) {
    return track < 32 ? (1u << track) : 0u;
}
constexpr bool trackInMask(uint32_t mask, uint8_t track) {
    return track < 32 ? ((mask >> track) & 1u) != 0 : true;
}

class Scheduler {
public:
    explicit Scheduler(const MidiSong& song);

    void setTempoPercent(float p);  // clamped to [1, 500]
    float tempoPercent() const { return tempo_; }

    void setLoop(uint64_t startUs, uint64_t endUs);  // ignored if end <= start
    void clearLoop() { loopOn_ = false; }

    void setBarrier(uint64_t us);
    void clearBarrier() { barrierOn_ = false; }
    bool atBarrier() const { return barrierOn_ && posUs() == barrier_; }

    uint64_t positionUs() const { return posUs(); }
    uint64_t durationUs() const { return duration_; }
    bool finished() const;

    // Jump to a song time. Returns note-offs for everything sounding so the
    // consumer can silence lights/MIDI.
    std::vector<SchedEvent> seek(uint64_t us);

    // Advance real (wall-clock) time; returns due events in order.
    std::vector<SchedEvent> advance(uint64_t realDeltaUs);

    // First note onset at or after `us` on a masked-in track (kNoOnset if none).
    uint64_t nextOnsetAfter(uint64_t us, uint32_t trackMask) const;

    // All note-ons at exactly `us` on masked-in tracks (a chord, for wait mode).
    std::vector<SchedEvent> notesOnAt(uint64_t us, uint32_t trackMask) const;

    // Note-ons with fromUs <= onset <= toUs on masked-in tracks (ramp preview).
    std::vector<SchedEvent> onsetsBetween(uint64_t fromUs, uint64_t toUs,
                                          uint32_t trackMask) const;

private:
    uint64_t posUs() const { return static_cast<uint64_t>(pos_); }
    size_t indexForTime(uint64_t us) const;
    void emitUpTo(uint64_t stopUs, bool includeStop, std::vector<SchedEvent>& out);
    void flushSounding(uint64_t atUs, std::vector<SchedEvent>& out);

    std::vector<SchedEvent> events_;  // time-sorted; offs before ons at a tie
    SoundingSet<SchedEvent> sounding_;
    size_t idx_ = 0;
    double pos_ = 0.0;
    float tempo_ = 100.0f;
    uint64_t duration_ = 0;
    bool loopOn_ = false;
    uint64_t loopStart_ = 0, loopEnd_ = 0;
    bool barrierOn_ = false;
    uint64_t barrier_ = 0;
};

}  // namespace vialucis
