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
                sounding_.push_back({e.note, e.channel});
                if (guard_) guard_->noteSent(e.note, nowUs);
                break;
            case SchedEventType::NoteOff: {
                out.push_back({MidiOutType::NoteOff, e.channel, e.note, 0});
                for (size_t i = 0; i < sounding_.size(); ++i) {
                    if (sounding_[i].note == e.note &&
                        sounding_[i].channel == e.channel) {
                        sounding_.erase(sounding_.begin() + i);
                        break;
                    }
                }
                break;
            }
            case SchedEventType::Pedal:
                out.push_back({MidiOutType::Cc, e.channel, e.note, e.velocity});
                break;
        }
    }
    return out;
}

std::vector<MidiOutMsg> NoteEmitter::allOff() {
    std::vector<MidiOutMsg> out;
    for (const Sounding& s : sounding_)
        out.push_back({MidiOutType::NoteOff, s.channel, s.note, 0});
    sounding_.clear();
    return out;
}

}  // namespace vialucis
