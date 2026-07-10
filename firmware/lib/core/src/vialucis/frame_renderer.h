#pragma once
// Lookahead ramp renderer (SPEC feature 8): upcoming notes swell 0% → preview
// cap over the lead time, due notes jump to 100%, wrong presses flash on top.
// Pure geometry + layering; device layer pushes frame() to FastLED (which owns
// the global power cap).
//
// Exactly ONE cap variable and ONE lead-time variable — LOCKED.

#include <cstdint>
#include <vector>

#include "vialucis/key_led_table.h"

namespace vialucis {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

// THE color-brightness scale (round-half-up per channel) — every layer that
// dims a color uses this one definition (ramp, repeat fill, effects).
inline Rgb scaleRgb(Rgb c, float k) {
    return {static_cast<uint8_t>(c.r * k + 0.5f),
            static_cast<uint8_t>(c.g * k + 0.5f),
            static_cast<uint8_t>(c.b * k + 0.5f)};
}

struct RampConfig {
    float previewCap = 0.45f;    // THE cap variable (0..1)
    uint64_t leadUs = 1000000;   // THE lead-time variable
};

class FrameRenderer {
public:
    // Geometry comes ONLY from the per-key table (VL1) — the renderer never
    // re-derives it from settings.
    FrameRenderer(const KeyLedTable& table, const RampConfig& ramp)
        : table_(table), ramp_(ramp), frame_(table.ledCount()),
          layer_(table.ledCount(), Layer::None) {}

    void clear();

    // Upcoming note: brightness = cap * elapsed fraction of the lead window.
    void addUpcoming(uint8_t note, Rgb color, uint64_t onsetUs, uint64_t nowUs);

    // Due / sounding note: full brightness ("now" is unmistakable).
    void addDue(uint8_t note, Rgb color);

    // Repeat cue (Q1, brief §2): fills the off-gap before a same-key
    // re-press; sits between Ramp and Due so the onset's addDue overwrites
    // it — jump vs glide is emergent from the peak setting, never a mode.
    void addRepeatFill(uint8_t note, Rgb color);

    // Wrong press: full-strength flash, wins over everything at that key.
    void addWrong(uint8_t note, Rgb color);

    // Forced single-LED dot (calibration probe) — addressed by LED index,
    // not note; sits above every note layer.
    void addDot(uint16_t led, Rgb color);

    const std::vector<Rgb>& frame() const { return frame_; }
    const RampConfig& ramp() const { return ramp_; }
    void setRamp(const RampConfig& r) { ramp_ = r; }
    const KeyLedTable& table() const { return table_; }

private:
    enum class Layer : uint8_t {
        None = 0, Ramp = 1, RepeatFill = 2, Due = 3, Wrong = 4, Forced = 5
    };

    void paint(uint8_t note, Rgb color, Layer layer);

    KeyLedTable table_;
    RampConfig ramp_;
    std::vector<Rgb> frame_;
    std::vector<Layer> layer_;
};

}  // namespace vialucis
