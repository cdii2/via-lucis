#include "vialucis/midi_capture.h"

namespace vialucis {

ArmResult MidiCapture::arm(size_t budgetBytes, uint32_t maxDurationMs,
                           uint64_t nowUs) {
    (void)nowUs;
    if (state_ != CaptureState::Idle) return ArmResult::AlreadyArmed;
    size_t maxEvents = budgetBytes / sizeof(CaptureEvent);
    if (maxEvents == 0)  // can't hold even one event
        return ArmResult::BadBudget;
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
    return ArmResult::Armed;
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
    // Exclude the piano's echo of a CC64 WE sent (demo/accompaniment pedal
    // pass-through) — same §5a rule as note echoes, pedal-credit keyed.
    if (pedalCredits_ > 0) {
        if (nowUs <= pedalExpiryUs_) {
            --pedalCredits_;
            return;
        }
        pedalCredits_ = 0;  // stale credits die
    }
    if (state_ != CaptureState::Recording) return;
    append(CaptureEventType::Pedal, 0, static_cast<uint8_t>(value & 0x7F),
           channel, nowUs);
}

uint32_t MidiCapture::elapsedMs(uint64_t nowUs) const {
    if (state_ != CaptureState::Recording) return 0;
    return static_cast<uint32_t>((nowUs - firstNoteUs_) / 1000);
}

CaptureTake MidiCapture::stop(uint64_t nowUs) {
    CaptureTake take;
    take.status = status_;

    // Notes still held when Stop is pressed close at the STOP time, not the
    // last captured event — a performance ending on a sustained chord keeps
    // that chord's real duration (closing review). Clamped to the duration
    // cap so an overflowed take can't grow past its own truncation point.
    uint32_t stopMs = lastEventMs_;
    if (state_ == CaptureState::Recording && nowUs >= firstNoteUs_) {
        uint64_t elapsed = (nowUs - firstNoteUs_) / 1000;
        if (elapsed > maxDurationMs_) elapsed = maxDurationMs_;
        if (elapsed > stopMs) stopMs = static_cast<uint32_t>(elapsed);
    }

    // Pair note-ons with their offs, mirroring the parser: a re-trigger of an
    // already-open (channel,note) closes the previous instance first; anything
    // still open at the end closes at the stop time.
    struct Open {
        uint32_t onMs;
        uint8_t velocity;
        bool active;
    };
    // Heap, not stack: 16×128 slots is ~16KB, more than an ESP32 FreeRTOS
    // task stack. stop() runs on the REST path where allocation is fine.
    std::vector<Open> open(16 * 128, Open{});

    for (const CaptureEvent& e : events_) {
        uint8_t ch = e.channel & 0x0F;
        switch (e.type) {
            case CaptureEventType::NoteOn: {
                Open& o = open[ch * 128 + e.data1];
                if (o.active)
                    take.notes.push_back(
                        {o.onMs, e.tMs, e.data1, o.velocity, ch});
                o = {e.tMs, e.data2, true};
                break;
            }
            case CaptureEventType::NoteOff: {
                Open& o = open[ch * 128 + e.data1];
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
    // Close everything still held at the stop time. Trailing SILENCE stays
    // trimmed (like the leading kind): only a note actually held through
    // Stop extends the take past its last event.
    bool heldAny = false;
    for (int ch = 0; ch < 16; ++ch)
        for (int n = 0; n < 128; ++n)
            if (open[ch * 128 + n].active) {
                heldAny = true;
                take.notes.push_back({open[ch * 128 + n].onMs, stopMs,
                                      static_cast<uint8_t>(n),
                                      open[ch * 128 + n].velocity,
                                      static_cast<uint8_t>(ch)});
            }

    take.durationMs = heldAny ? stopMs : lastEventMs_;
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
