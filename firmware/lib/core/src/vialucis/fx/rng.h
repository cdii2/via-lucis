#pragma once
// Injectable PRNG for effects (VL7): every classic port draws randomness
// from one of these instead of FastLED's global random8/16, so a (seed,
// ledCount, frame-count) triple fully determines every frame — natively
// testable, replayable. xorshift32; the 8/16-bit shapes mirror FastLED's.

#include <cstdint>

namespace vialucis {
namespace fx {

class Rng {
public:
    explicit Rng(uint32_t seed = 1) { reseed(seed); }

    void reseed(uint32_t seed) { s_ = seed ? seed : 0x9E3779B9u; }

    uint32_t next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }

    uint8_t random8() { return static_cast<uint8_t>(next()); }
    uint8_t random8(uint8_t lim) {  // [0, lim)
        return lim ? static_cast<uint8_t>(
                         (static_cast<uint16_t>(random8()) * lim) >> 8)
                   : 0;
    }
    uint8_t random8(uint8_t lo, uint8_t hi) {  // [lo, hi)
        return hi > lo ? static_cast<uint8_t>(lo + random8(hi - lo)) : lo;
    }
    uint16_t random16() { return static_cast<uint16_t>(next()); }
    uint16_t random16(uint16_t lim) {
        return lim ? static_cast<uint16_t>(
                         (static_cast<uint32_t>(random16()) * lim) >> 16)
                   : 0;
    }

private:
    uint32_t s_ = 1;
};

}  // namespace fx
}  // namespace vialucis
