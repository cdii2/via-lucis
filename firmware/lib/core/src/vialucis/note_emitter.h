#pragma once
// Demo / accompaniment emitter: turns scheduler events on masked-in tracks
// into MIDI-out messages for the piano's own sound engine. Every note-on we
// transmit is registered with the EchoGuard so the piano's echo of it can
// never masquerade as a key press (iron rule).

#include <cstdint>
#include <vector>

#include "vialucis/echo_guard.h"
#include "vialucis/midi_io.h"  // MidiOutMsg — the transport seam's message type
#include "vialucis/scheduler.h"
#include "vialucis/sounding_set.h"

namespace vialucis {

class NoteEmitter {
public:
    explicit NoteEmitter(uint32_t emitMask) : mask_(emitMask) {}

    void setEmitMask(uint32_t m) { mask_ = m; }
    void setEchoGuard(EchoGuard* g) { guard_ = g; }

    // Feed the events a Scheduler::advance() returned; messages to send are
    // APPENDED to `out` (R5: producers feed the caller's shared per-tick
    // queue — they never clear it). By-value overloads for convenience.
    void consume(const std::vector<SchedEvent>& events, uint64_t nowUs,
                 std::vector<MidiOutMsg>& out);
    std::vector<MidiOutMsg> consume(const std::vector<SchedEvent>& events,
                                    uint64_t nowUs);

    // Note-offs for everything we currently have sounding (stop/seek/panic),
    // appended to `out`.
    void allOff(std::vector<MidiOutMsg>& out);
    std::vector<MidiOutMsg> allOff();

private:
    struct Sounding {
        uint8_t note, channel;
    };

    uint32_t mask_;
    EchoGuard* guard_ = nullptr;
    SoundingSet<Sounding> sounding_;
    // Per-channel sustain-pedal latch (A-4/G17): a CC64 we pass through in
    // demo/accompaniment leaves the piano's damper raised. allOff (pause /
    // stop / seek / panic) must send CC64=0 wherever a pedal is still down,
    // or the piano stays sustained after playback halts.
    bool pedalDown_[16] = {false};
};

}  // namespace vialucis
