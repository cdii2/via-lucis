#pragma once
// The expressive note-driven base (E2, brief §3): the piano's three free
// signals feed the lights — velocity (how hard), release (note-off tail),
// sustain pedal (CC64 latch) — each behind a tunable. ONE implementation
// serves Reactive free-play (fed live MIDI) and, later, Presentation's
// note-driven clips (fed from the baked timeline): same params, same look.
//
// Geometry comes from the per-key table (VL1) — a note lights exactly its
// calibrated LEDs. Not clock-animated beyond the release decay, so it works
// as an Effect (render decays state by the fixed timestep) driven by
// noteOn/noteOff/setPedal events between frames.

#include <array>
#include <cstdint>

#include "vialucis/fx/effect.h"
#include "vialucis/key_led_table.h"

namespace vialucis {
namespace fx {

class NoteDriven : public Effect {
public:
    struct Params {
        // Velocity curve: level = (vel/127)^gamma. 1 = linear; <1 lifts
        // soft notes; >1 demands force. Clamped 0.25..4 by the setter.
        float velGamma = 1.0f;
        uint32_t releaseMs = 400;  // fade-out tail; 0 = hard cut
        bool pedalLatch = true;    // CC64 holds the glow while down
    };

    void setParams(const Params& p);
    const Params& params() const { return params_; }

    // Geometry: which LEDs a note owns. The table is copied (~530B), same
    // as FrameRenderer does — no dangling references.
    void setTable(const KeyLedTable& t) { table_ = t; }

    // --- the live inputs (Reactive) / timeline inputs (Presentation) -----
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void setPedal(bool down);

    // --- Effect ------------------------------------------------------------
    void reset(uint32_t seed, uint16_t ledCount) override;
    void render(FxFrame& f) override;  // decays releases, paints the table
    void setPalette(const Palette16& p) override { palette_ = p; }
    // A164 (§3-E item 6): back to the built-in default (rainbowColors, same
    // as reset() seeds it to) — the state before any setPalette() call.
    void resetPalette() override { palette_ = rainbowColors(); }

private:
    struct Key {
        float level = 0.0f;    // current glow 0..1
        float held = 0.0f;     // level while down/latched
        uint8_t rawVel = 0;    // retained for future velocity→palette maps
        bool down = false;     // physical key down
        bool latched = false;  // held by pedal after key-up
    };

    Params params_;
    // Velocity curve as a 128-entry LUT rebuilt in setParams: noteOn runs
    // on the BLE key-event path even during practice (the director keeps
    // reactive state warm), so it must never pay a pow() there (iron rule
    // — E-wave closing review).
    std::array<float, 128> velLut_{};
    KeyLedTable table_;
    Palette16 palette_;
    std::array<Key, 88> keys_{};
    bool pedal_ = false;
    uint16_t ledCount_ = 0;
};

}  // namespace fx
}  // namespace vialucis
