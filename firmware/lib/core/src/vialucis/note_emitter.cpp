#include "vialucis/note_emitter.h"

namespace vialucis {

void NoteEmitter::consume(const std::vector<SchedEvent>& events,
                          uint64_t nowUs, std::vector<MidiOutMsg>& out) {
    for (const SchedEvent& e : events) {
        if (!trackInMask(mask_, e.track)) continue;
        switch (e.type) {
            case SchedEventType::NoteOn:
                out.push_back({MidiOutType::NoteOn, e.channel, e.note,
                               e.velocity});
                sounding_.add({e.note, e.channel, e.track});
                // FIX-C (deferred, BUGFIX-PLAN §7): registers at EMIT time,
                // not confirmed-transmit time — see echo_guard.h::noteSent()
                // and ble_midi_io.cpp::send().
                if (guard_) guard_->noteSent(e.note, nowUs);
                break;
            case SchedEventType::NoteOff:
                out.push_back({MidiOutType::NoteOff, e.channel, e.note, 0});
                sounding_.eraseFirst([&e](const Sounding& s) {
                    return s.note == e.note && s.channel == e.channel;
                });
                break;
            case SchedEventType::Pedal:
                out.push_back({MidiOutType::Cc, e.channel, e.note, e.velocity});
                // Latch sustain (CC64) per channel so allOff can release it
                // (A-4/G17). MIDI: value >= 64 is pedal-down, < 64 is up.
                if (e.note == 64 && e.channel < 16)
                    pedalDown_[e.channel] = e.velocity >= 64;
                break;
        }
    }
}

std::vector<MidiOutMsg> NoteEmitter::consume(
    const std::vector<SchedEvent>& events, uint64_t nowUs) {
    std::vector<MidiOutMsg> out;
    consume(events, nowUs, out);
    return out;
}

void NoteEmitter::allOff(std::vector<MidiOutMsg>& out) {
    sounding_.drain([&](const Sounding& s) {
        out.push_back({MidiOutType::NoteOff, s.channel, s.note, 0});
    });
    // Release any still-held sustain pedal so the piano doesn't ring on after
    // a pause/stop/seek/panic (A-4/G17).
    for (uint8_t ch = 0; ch < 16; ++ch) {
        if (pedalDown_[ch]) {
            out.push_back({MidiOutType::Cc, ch, 64, 0});
            pedalDown_[ch] = false;
        }
    }
}

std::vector<MidiOutMsg> NoteEmitter::allOff() {
    std::vector<MidiOutMsg> out;
    allOff(out);
    return out;
}

void NoteEmitter::flushTracksOutsideMask(uint32_t keepMask,
                                         std::vector<MidiOutMsg>& out) {
    // Repeatedly pull the first sounding note whose track dropped out of the
    // mask, emit its note-off, and forget it — until none remain. eraseFirst
    // keeps this class's single erase primitive (no new SoundingSet method).
    // The predicate captures the entry it erases so we can build its note-off.
    // Pedal latches (per-channel) are left to allOff — a track leaving the
    // mask is a hand-reassignment, not a transport halt.
    Sounding s{};
    while (sounding_.eraseFirst([&](const Sounding& e) {
        if (trackInMask(keepMask, e.track)) return false;
        s = e;
        return true;
    })) {
        out.push_back({MidiOutType::NoteOff, s.channel, s.note, 0});
    }
}

}  // namespace vialucis
