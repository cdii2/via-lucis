#pragma once
// Demo / accompaniment emitter: turns scheduler events on masked-in tracks
// into MIDI-out messages for the piano's own sound engine. Every note-on we
// transmit is registered with the EchoGuard so the piano's echo of it can
// never masquerade as a key press (iron rule).

#include <cstdint>
#include <vector>

#include "vialucis/echo_guard.h"
#include "vialucis/scheduler.h"

namespace vialucis {

enum class MidiOutType : uint8_t { NoteOn, NoteOff, Cc };

struct MidiOutMsg {
    MidiOutType type;
    uint8_t channel;
    uint8_t data1;  // note number / controller number
    uint8_t data2;  // velocity / controller value
};

class NoteEmitter {
public:
    explicit NoteEmitter(uint32_t emitMask) : mask_(emitMask) {}

    void setEmitMask(uint32_t m) { mask_ = m; }
    void setEchoGuard(EchoGuard* g) { guard_ = g; }

    // Feed the events a Scheduler::advance() returned; get messages to send.
    std::vector<MidiOutMsg> consume(const std::vector<SchedEvent>& events,
                                    uint64_t nowUs);

    // Note-offs for everything we currently have sounding (stop/seek/panic).
    std::vector<MidiOutMsg> allOff();

private:
    struct Sounding {
        uint8_t note, channel;
    };

    uint32_t mask_;
    EchoGuard* guard_ = nullptr;
    std::vector<Sounding> sounding_;
};

}  // namespace vialucis
