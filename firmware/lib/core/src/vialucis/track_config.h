#pragma once
// Per-track hand assignment + lights toggle, and the masks every consumer
// derives from them (REST: PUT /api/tracks/{index} — see docs/API.md).

#include <cstdint>
#include <vector>

#include "vialucis/midi_parser.h"
#include "vialucis/scheduler.h"

namespace vialucis {

enum class Hand : uint8_t { Off, Left, Right, Both };

struct TrackAssign {
    Hand hand = Hand::Both;
    bool lights = true;
};

struct TrackConfig {
    std::vector<TrackAssign> tracks;

    // Heuristics (docs/API.md): names containing left/lh → Left, right/rh →
    // Right; else with exactly two note-bearing tracks, first = Right, second
    // = Left (piano convention); a single note-bearing track = Both; tracks
    // without notes (conductor) = Off.
    static TrackConfig defaultsFor(const MidiSong& song);

    // Tracks whose lights may shine (hand assigned + lights on).
    uint32_t lightsMask() const;
    // Tracks the player practices (wait-mode barrier source).
    uint32_t practicedMask(Hand practice) const;
    // Tracks the piano plays FOR the player in accompaniment mode.
    uint32_t accompanimentMask(Hand practice) const;
    // Everything with a hand assigned (demo mode plays all of these).
    uint32_t audibleMask() const;
};

}  // namespace vialucis
