#pragma once
// FastLED wrapper. Owns the physical strip on GPIO16 (FROZEN — see
// PROGRESS.md) and the global power cap (iron rule: 2m COB at full white can
// out-draw the 10A supply).

#include <cstdint>
#include <vector>

#include "vialucis/frame_renderer.h"

namespace vialucis {

class LedOutput {
public:
    static constexpr int kDataPin = 16;      // FROZEN
    static constexpr uint16_t kLedCount = 360;  // 2m FCOB @ 180/m
    // 10A supply feeds strip + ESP32; cap the strip well below the limit.
    static constexpr uint32_t kMaxMilliamps = 8000;

    void begin(uint8_t brightness);
    void setBrightness(uint8_t b);
    void show(const std::vector<Rgb>& frame);
    void allOff();

    // BRINGUP.md patterns.
    void testPattern(uint32_t nowMs);  // white dot walking the strip
    void rainbow(uint32_t nowMs);      // the easter egg
};

}  // namespace vialucis
