#include "vialucis/midi_capture.h"

namespace vialucis {

ArmResult MidiCapture::arm(size_t budgetBytes, uint32_t maxDurationMs,
                           uint64_t nowUs) {
    (void)nowUs;
    if (state_ != CaptureState::Idle) {
        lastArm_ = ArmResult::AlreadyArmed;
        return lastArm_;
    }
    size_t maxEvents = budgetBytes / sizeof(CaptureEvent);
    if (maxEvents == 0) {  // can't hold even one event
        lastArm_ = ArmResult::BadBudget;
        return lastArm_;
    }
    // The single allocation of a take: reserve exactly the budget once. size()
    // never exceeds this (append drops at the cap), so no reallocation follows
    // — data() and capacity() are stable from here through stop().
    events_.clear();
    events_.shrink_to_fit();
    events_.reserve(maxEvents);
    maxEvents_ = maxEvents;
    budgetBytes_ = budgetBytes;
    maxDurationMs_ = maxDurationMs;
    status_ = CaptureStatus::Ok;
    firstNoteUs_ = 0;
    lastEventMs_ = 0;
    state_ = CaptureState::Armed;
    lastArm_ = ArmResult::Armed;
    return lastArm_;
}

void MidiCapture::append(CaptureEventType type, uint8_t d1, uint8_t d2,
                         uint8_t ch, uint64_t nowUs) {
    // Relative time in ms; the first note-on set the origin.
    uint32_t tMs = static_cast<uint32_t>((nowUs - firstNoteUs_) / 1000);
    if (tMs > maxDurationMs_ || events_.size() >= maxEvents_) {
        status_ = CaptureStatus::Overflowed;  // flag + drop; never grow/block
        return;
    }
    events_.push_back({tMs, type, d1, d2, static_cast<uint8_t>(ch & 0x0F)});
    lastEventMs_ = tMs;
}

void MidiCapture::onNoteOn(uint8_t note, uint8_t velocity, uint8_t channel,
                           uint64_t nowUs) {
    if (state_ == CaptureState::Idle) return;
    if (note > 127) return;
    // Exclude our own echo BEFORE anything else — a device echo must neither be
    // captured nor start the clock. Consumes capture's guard only.
    if (guard_.shouldIgnore(note, nowUs)) return;
    if (velocity == 0) {  // note-on vel 0 is a note-off
        onNoteOff(note, channel, nowUs);
        return;
    }
    if (state_ == CaptureState::Armed) {  // first real note starts the tape
        firstNoteUs_ = nowUs;
        state_ = CaptureState::Recording;
    }
    append(CaptureEventType::NoteOn, note, velocity, channel, nowUs);
}

void MidiCapture::onNoteOff(uint8_t note, uint8_t channel, uint64_t nowUs) {
    if (state_ != CaptureState::Recording) return;  // nothing before first note
    if (note > 127) return;
    append(CaptureEventType::NoteOff, note, 0, channel, nowUs);
}

void MidiCapture::onPedal(uint8_t value, uint8_t channel, uint64_t nowUs) {
    if (state_ != CaptureState::Recording) return;
    append(CaptureEventType::Pedal, 0, static_cast<uint8_t>(value & 0x7F),
           channel, nowUs);
}

uint32_t MidiCapture::elapsedMs(uint64_t nowUs) const {
    if (state_ != CaptureState::Recording) return 0;
    return static_cast<uint32_t>((nowUs - firstNoteUs_) / 1000);
}

CaptureTake MidiCapture::stop() {
    CaptureTake take;
    take.status = status_;

    // Pair note-ons with their offs, mirroring the parser: a re-trigger of an
    // already-open (channel,note) closes the previous instance first; anything
    // still open at the end closes at the last event time.
    struct Open {
        uint32_t onMs;
        uint8_t velocity;
        bool active;
    };
    Open open[16][128] = {};

    for (const CaptureEvent& e : events_) {
        uint8_t ch = e.channel & 0x0F;
        switch (e.type) {
            case CaptureEventType::NoteOn: {
                Open& o = open[ch][e.data1];
                if (o.active)
                    take.notes.push_back(
                        {o.onMs, e.tMs, e.data1, o.velocity, ch});
                o = {e.tMs, e.data2, true};
                break;
            }
            case CaptureEventType::NoteOff: {
                Open& o = open[ch][e.data1];
                if (o.active) {
                    take.notes.push_back(
                        {o.onMs, e.tMs, e.data1, o.velocity, ch});
                    o.active = false;
                }
                break;
            }
            case CaptureEventType::Pedal:
                take.pedals.push_back({e.tMs, e.data2, ch});
                break;
        }
    }
    // Close everything still held at the final event time.
    for (int ch = 0; ch < 16; ++ch)
        for (int n = 0; n < 128; ++n)
            if (open[ch][n].active)
                take.notes.push_back({open[ch][n].onMs, lastEventMs_,
                                      static_cast<uint8_t>(n),
                                      open[ch][n].velocity,
                                      static_cast<uint8_t>(ch)});

    take.durationMs = lastEventMs_;
    take.empty = take.notes.empty() && take.pedals.empty();

    state_ = CaptureState::Idle;
    return take;
}

void MidiCapture::discard() {
    events_.clear();  // capacity retained; nothing kept
    state_ = CaptureState::Idle;
    status_ = CaptureStatus::Ok;
    firstNoteUs_ = 0;
    lastEventMs_ = 0;
}

}  // namespace vialucis
