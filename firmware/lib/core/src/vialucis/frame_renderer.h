#pragma once
// Lookahead ramp renderer (SPEC feature 8): upcoming notes swell 0% → preview
// cap over the lead time, due notes jump to 100%, wrong presses flash on top.
// Pure geometry + layering; device layer pushes frame() to FastLED (which owns
// the global power cap).
//
// Exactly ONE cap variable and ONE lead-time variable — LOCKED.

#include <cstdint>
#include <vector>

#include "vialucis/key_led_map.h"

namespace vialucis {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

struct RampConfig {
    float previewCap = 0.45f;    // THE cap variable (0..1)
    uint64_t leadUs = 1000000;   // THE lead-time variable
};

class FrameRenderer {
public:
    FrameRenderer(const LedMapConfig& map, const RampConfig& ramp)
        : map_(map), ramp_(ramp), frame_(map.ledCount),
          layer_(map.ledCount, Layer::None) {}

    void clear();

    // Upcoming note: brightness = cap * elapsed fraction of the lead window.
    void addUpcoming(uint8_t note, Rgb color, uint64_t onsetUs, uint64_t nowUs);

    // Due / sounding note: full brightness ("now" is unmistakable).
    void addDue(uint8_t note, Rgb color);

    // Wrong press: full-strength flash, wins over everything at that key.
    void addWrong(uint8_t note, Rgb color);

    const std::vector<Rgb>& frame() const { return frame_; }
    const RampConfig& ramp() const { return ramp_; }
    void setRamp(const RampConfig& r) { ramp_ = r; }

private:
    enum class Layer : uint8_t { None = 0, Ramp = 1, Due = 2, Wrong = 3 };

    void paint(uint8_t note, Rgb color, Layer layer);

    LedMapConfig map_;
    RampConfig ramp_;
    std::vector<Rgb> frame_;
    std::vector<Layer> layer_;
};

}  // namespace vialucis
