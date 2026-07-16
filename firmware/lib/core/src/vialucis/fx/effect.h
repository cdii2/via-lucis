#pragma once
// THE effect seam (E1, brief §3 + VL7): one engine under every aesthetic
// mode — AFK plays effects from a playlist, Reactive drives one from live
// notes, Presentation sequences them on a timeline (P-wave).
//
// Contract:
// - Fixed timestep: render() is called once per kFxStepMs of effect time;
//   time-dependent math derives from FxFrame::ms (= frame * kFxStepMs),
//   NEVER a wall clock. Same seed + ledCount + frame sequence ⇒ identical
//   frames, byte for byte (the determinism tests pin this).
// - Zero allocation inside render() — buffers are sized in reset().
// - Effects paint every LED they own each call (the caller does not clear).
// - Brightness/power discipline: effects write full-range colors; the
//   global FastLED power cap and the AFK brightness cap are applied by the
//   layers above (iron rule — the cap lives in ONE place).
// - Virtual dispatch is fine HERE: effects never run on the practice
//   latency path (AFK/Reactive/Presentation only).

#include <cstdint>
#include <vector>

#include "vialucis/frame_renderer.h"
#include "vialucis/fx/palette.h"
#include "vialucis/fx/rng.h"

namespace vialucis {
namespace fx {

constexpr uint32_t kFxStepMs = 16;  // ~60fps fixed step (VL7)

struct FxFrame {
    std::vector<Rgb>& leds;  // sized to the strip; effect writes its range
    uint32_t frame;          // fixed-timestep counter since reset()
    uint32_t ms;             // frame * kFxStepMs — the ONLY time source
};

class Effect {
public:
    virtual ~Effect() = default;

    // Prepare all state/buffers for a strip of `ledCount`; `seed` feeds the
    // injectable RNG. Called before the first render and on every restart.
    virtual void reset(uint32_t seed, uint16_t ledCount) = 0;

    // Paint one fixed-timestep frame. ZERO allocation.
    virtual void render(FxFrame& f) = 0;

    // Palette-driven effects accept the shared palette primitive; others
    // ignore it (brief §3 — palettes are the one color primitive).
    virtual void setPalette(const Palette16& p) { (void)p; }

    // Restore this effect's OWN default palette — the state it has right
    // after reset(), before any setPalette() call (A164, §3-E item 6). The
    // counterpart to setPalette() for "no override, use whatever this effect
    // normally shows": SHOW-FORMAT's paletteRef==0xFF means exactly that, and
    // without this seam a ShowPlayer cue sharing an effect instance with an
    // earlier cue that DID call setPalette() would keep the earlier cue's
    // palette forever (state bleed between cues). Effects that don't honor
    // setPalette() also don't need to override this (no-op default, matching
    // setPalette's own default).
    virtual void resetPalette() {}
};

}  // namespace fx
}  // namespace vialucis
