#pragma once
// Fire2012, ported from FastLED 3.10.3 (MIT, https://github.com/FastLED/FastLED,
// tag 3.10.3): examples/Fire2012/Fire2012.ino (Mark Kriegsman, July 2012, part of
// "Five Elements": http://youtu.be/knWiGsmgycY). Ported for native testability and
// the injected-Rng determinism contract (VL7) — see fx/rng.h and fx/effect.h.
//
// Faithful port of the four-step simulation: 1) cool every cell a little,
// 2) drift heat 'up' and diffuse, 3) randomly spark near the bottom,
// 4) map heat -> color via HeatColor(). COOLING=55, SPARKING=120 are the
// sketch's pinned defaults (its comments document suggested tuning ranges:
// COOLING 20-100, SPARKING 50-200 — left as class constants, not exposed as
// runtime params, since nothing in Via Lucis needs to retune them yet).
// gReverseDirection is fixed false (the sketch's optional reverse-strip mode
// was never wired to a Via Lucis control surface).
//
// Deviation: the sketch's `int y = random8(7)` spark index assumes
// NUM_LEDS >= 7 (true for every Via Lucis strip length, 60/360 included).
// For completeness on a hypothetical shorter strip, the spark range is
// clamped to ledCount so the write can never go out of bounds.
//
// heat_ is the "Array of temperature readings at each simulation cell" —
// sized once in reset() and never resized/pushed to in render() (the
// seam's zero-allocation-in-render rule).

#include <cstdint>
#include <vector>

#include "vialucis/fx/effect.h"
#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/fx/rng.h"

namespace vialucis {
namespace fx {

class Fire2012Fx : public Effect {
public:
    // COOLING: how much the air cools as it rises. Less cooling = taller
    // flames; more cooling = shorter flames.
    static constexpr uint8_t kCooling = 55;
    // SPARKING: chance (out of 255) that a new spark lights each frame.
    // Higher = more roaring fire; lower = more flickery.
    static constexpr uint8_t kSparking = 120;

    void reset(uint32_t seed, uint16_t ledCount) override {
        rng_.reseed(seed);
        ledCount_ = ledCount;
        heat_.assign(ledCount, 0);
    }

    void render(FxFrame& f) override {
        if (ledCount_ == 0) {
            return;
        }

        // Step 1. Cool down every cell a little.
        //   heat[i] = qsub8(heat[i], random8(0, ((COOLING*10)/NUM_LEDS)+2));
        // Computed in a wide type first, same as the sketch's int arithmetic,
        // then narrowed to the uint8_t random8() expects (matches the
        // sketch's implicit int->uint8_t truncation at the call site).
        uint16_t coolRangeWide = static_cast<uint16_t>(
            (static_cast<uint16_t>(kCooling) * 10) / ledCount_ + 2);
        uint8_t coolRange = static_cast<uint8_t>(coolRangeWide);
        for (uint16_t i = 0; i < ledCount_; ++i) {
            heat_[i] = qsub8(heat_[i], rng_.random8(0, coolRange));
        }

        // Step 2. Heat from each cell drifts 'up' and diffuses a little.
        //   heat[k] = (heat[k-1] + heat[k-2] + heat[k-2]) / 3;
        for (uint16_t k = ledCount_ - 1; k >= 2; --k) {
            uint16_t sum = static_cast<uint16_t>(heat_[k - 1]) +
                           static_cast<uint16_t>(heat_[k - 2]) +
                           static_cast<uint16_t>(heat_[k - 2]);
            heat_[k] = static_cast<uint8_t>(sum / 3);
        }

        // Step 3. Randomly ignite new 'sparks' of heat near the bottom.
        if (rng_.random8() < kSparking) {
            uint8_t sparkRange =
                ledCount_ >= 7 ? uint8_t{7} : static_cast<uint8_t>(ledCount_);
            uint8_t y = rng_.random8(sparkRange);
            heat_[y] = qadd8(heat_[y], rng_.random8(160, 255));
        }

        // Step 4. Map from heat cells to LED colors. gReverseDirection is
        // fixed false (see file header), so pixelnumber == j always.
        // A162 (§3-E item 10): bound against f.leds.size() too — ledCount_ is
        // this effect's OWN idea of strip length (set once in reset()); if a
        // caller ever handed render() a shorter buffer than that, this write
        // must not run past its end.
        for (uint16_t j = 0; j < ledCount_ && j < f.leds.size(); ++j) {
            f.leds[j] = heatColor(heat_[j]);
        }
    }

    // Fire2012 paints via HeatColor(), not a palette (matches the sketch) —
    // setPalette() is intentionally left at Effect's no-op default.

private:
    Rng rng_;
    std::vector<uint8_t> heat_;
    uint16_t ledCount_ = 0;
};

}  // namespace fx
}  // namespace vialucis
