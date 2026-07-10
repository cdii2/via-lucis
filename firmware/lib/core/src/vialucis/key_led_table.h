#pragma once
// Per-key LED table (v2 C-wave, DESIGN-lightshow §5) — THE geometry
// primitive. Renderer, effects, and presentation note-binding all read this
// table; nothing downstream re-derives geometry (VL1). The v1 cluster-model
// formula (key_led_map.h) survives only as the math behind the 2-point
// builder. The calibration tiers/wizards are just different ways to fill it.

#include <array>
#include <cstdint>

#include "vialucis/key_led_map.h"

namespace vialucis {

class KeyLedTable {
public:
    static constexpr uint8_t kFirstNote = 21;  // A0
    static constexpr uint8_t kKeyCount = 88;

    // Default: zero LEDs, every key invalid — real tables come from builders.
    KeyLedTable() = default;

    LedRange forNote(uint8_t midiNote) const {
        if (midiNote < kFirstNote ||
            midiNote >= kFirstNote + kKeyCount)
            return LedRange{};
        return ranges_[midiNote - kFirstNote];
    }

    void set(uint8_t midiNote, const LedRange& r) {
        if (midiNote < kFirstNote || midiNote >= kFirstNote + kKeyCount)
            return;
        ranges_[midiNote - kFirstNote] = r;
    }

    uint16_t ledCount() const { return ledCount_; }
    void setLedCount(uint16_t n) { ledCount_ = n; }

private:
    std::array<LedRange, kKeyCount> ranges_{};
    uint16_t ledCount_ = 0;
};

namespace TableBuilder {

// The 2-point tier: the old two-setting math (offsetMm + ledsPerMeter)
// filling the table — integer-identical to calling the v1 formula per key.
KeyLedTable fromTwoPoint(const LedMapConfig& cfg);

}  // namespace TableBuilder

}  // namespace vialucis
