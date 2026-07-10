#pragma once
// The presentation compositor (P1) — plays a parsed Show onto the strip.
// SHOW-FORMAT §1 "Composition": cues render in stream order, later over
// earlier; §2: the whole show is loaded once into reserved buffers, no flash
// on the render path, zero allocation inside renderAt().
//
// Frame model: renderAt(songMs, out) is the fixed-timestep frame source for
// Presentation mode (analogous to AfkPlayer::render). It is expected to be
// driven with MONOTONICALLY NON-DECREASING songMs, ~one kFxStepMs of song
// time per call (the Scheduler's song-time axis — so Free-run tempo scaling
// falls out for free). A backward jump (seek) re-resets every effect.
//
// Effect instancing: ONE Effect per DISTINCT effectIndex (per the P1 brief).
// Cues that share an effectIndex share one instance and its internal state —
// the editor assigns distinct indices to clips that need independent state.
// The name "notedriven" maps to fx::NoteDriven; every other name goes through
// fx::makeEffect().

#include <cstdint>
#include <memory>
#include <vector>

#include "vialucis/fx/effect.h"
#include "vialucis/fx/note_driven.h"
#include "vialucis/key_led_table.h"
#include "vialucis/show.h"

namespace vialucis {

class ShowPlayer {
public:
    ShowPlayer() = default;

    // Take ownership of a parsed Show, build one Effect per distinct
    // effectIndex, and reserve ALL render buffers. `seed` feeds every
    // effect's RNG (determinism); `table` is the geometry every scope and the
    // note-driven layer read.
    void load(Show&& show, const KeyLedTable& table, uint32_t seed);

    // Render the frame at absolute song time `songMs` into `out` (sized to
    // the strip; resized once on first use if short). ZERO allocation after
    // load(): clears out, walks active cues in composited order, renders each
    // clip's effect into a shared scratch buffer, applies the key-scope mask,
    // and composites per blend mode.
    void renderAt(uint32_t songMs, std::vector<Rgb>& out);

    // --- stats (for GET /api/shows listing, P1) --------------------------
    size_t cueCount() const { return show_.cues.size(); }
    size_t effectCount() const { return show_.effects.size(); }
    uint8_t clockSource() const { return show_.meta.clockSource; }
    uint32_t durationMs() const { return show_.meta.durationMs; }
    const std::string& name() const { return show_.meta.name; }
    uint16_t ledCount() const { return ledCount_; }

private:
    // Fire note-driven bindings whose clip-local time falls in the window
    // (prevClip, curClip] and their note-offs; then render the shared effect.
    void driveNoteBinds(const ShowCue& cue, fx::NoteDriven* nd,
                        int64_t prevSongMs, uint32_t songMs);
    // Build the scope mask for `cue` into mask_ (returns true for scope 0 =
    // whole strip, where mask_ is not consulted).
    bool buildMask(const ShowCue& cue);
    const fx::Palette16* paletteForCue(const ShowCue& cue) const;

    Show show_;
    KeyLedTable table_;
    uint16_t ledCount_ = 0;
    uint32_t seed_ = 1;

    std::vector<std::unique_ptr<fx::Effect>> effects_;  // by effectIndex
    std::vector<fx::NoteDriven*> nd_;                    // notedriven or null
    // Per cue, note-off time (clip-local ms) for each bind: min(onset+300,
    // next same-note onset). Precomputed at load (index-parallel to cue.binds).
    std::vector<std::vector<uint32_t>> noteOffMs_;

    std::vector<Rgb> scratch_;    // one effect's frame
    std::vector<uint8_t> mask_;   // scope mask (1 = in scope)

    uint32_t lastSongMs_ = 0;
    bool haveLast_ = false;
};

}  // namespace vialucis
