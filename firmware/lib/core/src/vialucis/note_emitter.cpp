#include "vialucis/note_emitter.h"

namespace vialucis {

std::vector<MidiOutMsg> NoteEmitter::consume(
    const std::vector<SchedEvent>& events, uint64_t nowUs) {
    std::vector<MidiOutMsg> out;
    for (const SchedEvent& e : events) {
        if (!trackInMask(mask_, e.track)) continue;
        switch (e.type) {
            case SchedEventType::NoteOn:
                out.push_back({MidiOutType::NoteOn, e.channel, e.note,
                               e.velocity});
                sounding_.add({e.note, e.channel});
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
                break;
        }
    }
    return out;
}

std::vector<MidiOutMsg> NoteEmitter::allOff() {
    std::vector<MidiOutMsg> out;
    sounding_.drain([&](const Sounding& s) {
        out.push_back({MidiOutType::NoteOff, s.channel, s.note, 0});
    });
    return out;
}

}  // namespace vialucis
