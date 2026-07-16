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

    // Silence every note we are sounding on a track NOT in `keepMask`, then
    // forget it (B3a). Called when a track's hand assignment leaves the emit
    // mask mid-note: without this the departing track's note-off is masked
    // out of consume() forever, so the piano keeps ringing. Routed through
    // this emitter (the one MIDI-out owner) so it stays the single send path;
    // note-offs earn NO echo-guard credit (a credit is consumed by the NEXT
    // note-DOWN, and a note-off is never a key press — crediting it would eat
    // a genuine future press of the same key). An infrequent control action,
    // so the per-call scan is not on the latency path.
    void flushTracksOutsideMask(uint32_t keepMask,
                                std::vector<MidiOutMsg>& out);

private:
    struct Sounding {
        uint8_t note, channel, track;
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
