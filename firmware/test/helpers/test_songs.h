#pragma once
// Shared song fixtures for the native suites (480 tpq @ default 120bpm ⇒
// quarter note = 500000us). Scheduler, wait-mode and playback-engine tests
// all assert against these exact onset times — one definition keeps the
// three suites honest about the same song.

#include "smf_builder.h"
#include "vialucis/midi_parser.h"

namespace testsongs {

// C4 on@0 off@480t, E4 on@480t off@960t, G4+B4 chord on@960t off@1440t.
inline vialucis::MidiSong chordSong() {
    smf::Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOn(ev, 0, 0, 64, 100);
    smf::noteOff(ev, 480, 0, 64);
    smf::noteOn(ev, 0, 0, 67, 100);
    smf::noteOn(ev, 0, 0, 71, 100);
    smf::noteOff(ev, 480, 0, 67);
    smf::noteOff(ev, 0, 0, 71);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return vialucis::parseMidi(file.data(), file.size()).song;
}

// Two anonymous note tracks (piano convention: t0=Right, t1=Left):
// track 0 note 60, track 1 note 40, both on@0 off@480t.
inline vialucis::MidiSong twoTrackSong() {
    smf::Bytes t0, t1;
    smf::noteOn(t0, 0, 0, 60, 100);
    smf::noteOff(t0, 480, 0, 60);
    smf::noteOn(t1, 0, 0, 40, 100);
    smf::noteOff(t1, 480, 0, 40);
    smf::Bytes file = smf::header(1, 2, 480);
    smf::append(file, smf::track(t0));
    smf::append(file, smf::track(t1));
    return vialucis::parseMidi(file.data(), file.size()).song;
}

}  // namespace testsongs
