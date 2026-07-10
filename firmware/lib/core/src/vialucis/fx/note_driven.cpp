#include "vialucis/fx/note_driven.h"

#include <algorithm>
#include <cmath>

namespace vialucis {
namespace fx {

void NoteDriven::setParams(const Params& p) {
    params_ = p;
    if (params_.velGamma < 0.25f) params_.velGamma = 0.25f;
    if (params_.velGamma > 4.0f) params_.velGamma = 4.0f;
    for (int v = 0; v < 128; ++v)  // pow lives HERE, never per key event
        velLut_[v] = std::pow(v / 127.0f, params_.velGamma);
}

void NoteDriven::reset(uint32_t seed, uint16_t ledCount) {
    (void)seed;  // fully event-driven — no randomness
    ledCount_ = ledCount;
    keys_.fill(Key{});
    pedal_ = false;
    palette_ = rainbowColors();
    setParams(params_);  // (re)build the velocity LUT
}

void NoteDriven::noteOn(uint8_t note, uint8_t velocity) {
    if (note < 21 || note > 108) return;
    Key& k = keys_[note - 21];
    k.rawVel = velocity;
    k.held = velLut_[velocity & 0x7F];  // table lookup — no math on the
    k.level = k.held;                   // key-event path
    k.down = true;
    k.latched = false;
}

void NoteDriven::noteOff(uint8_t note) {
    if (note < 21 || note > 108) return;
    Key& k = keys_[note - 21];
    k.down = false;
    // Pedal down: the glow latches at its held level until pedal-up.
    if (params_.pedalLatch && pedal_) k.latched = true;
}

void NoteDriven::setPedal(bool down) {
    pedal_ = down;
    if (!down) {  // pedal-up releases every latched glow into its decay
        for (Key& k : keys_) k.latched = false;
    }
}

void NoteDriven::render(FxFrame& f) {
    const float decayPerFrame =
        params_.releaseMs == 0
            ? 1.0f
            : static_cast<float>(kFxStepMs) /
                  static_cast<float>(params_.releaseMs);
    std::fill(f.leds.begin(), f.leds.end(), Rgb{});
    for (size_t i = 0; i < keys_.size(); ++i) {
        Key& k = keys_[i];
        if (k.down || k.latched) {
            k.level = k.held;
        } else if (k.level > 0.0f) {
            k.level -= decayPerFrame;
            if (k.level < 0.0f) k.level = 0.0f;
        }
        if (k.level <= 0.0f) continue;
        uint8_t note = static_cast<uint8_t>(21 + i);
        LedRange r = table_.forNote(note);
        if (!r.valid) continue;  // off-strip keys stay dark, as everywhere
        // Pitch maps across the palette — low keys enter at 0, top at 255.
        uint8_t idx = static_cast<uint8_t>((i * 255) / 87);
        Rgb c = colorFromPalette(palette_, idx,
                                 static_cast<uint8_t>(k.level * 255.0f + 0.5f));
        for (uint16_t led = r.first; led <= r.last && led < f.leds.size();
             ++led)
            f.leds[led] = c;
    }
}

}  // namespace fx
}  // namespace vialucis
