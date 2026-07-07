#include "led_output.h"

#include <FastLED.h>

namespace vialucis {
namespace {
CRGB gLeds[LedOutput::kLedCount];
}

void LedOutput::begin(uint8_t brightness) {
    FastLED.addLeds<WS2812B, LedOutput::kDataPin, GRB>(gLeds, kLedCount);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, kMaxMilliamps);
    FastLED.setBrightness(brightness);
    FastLED.clear(true);
}

void LedOutput::setBrightness(uint8_t b) { FastLED.setBrightness(b); }

void LedOutput::show(const std::vector<Rgb>& frame) {
    size_t n = frame.size() < kLedCount ? frame.size() : kLedCount;
    for (size_t i = 0; i < n; ++i)
        gLeds[i] = CRGB(frame[i].r, frame[i].g, frame[i].b);
    for (size_t i = n; i < kLedCount; ++i) gLeds[i] = CRGB::Black;
    FastLED.show();
}

void LedOutput::allOff() {
    FastLED.clear(true);
}

void LedOutput::testPattern(uint32_t nowMs) {
    FastLED.clear();
    uint16_t pos = (nowMs / 20) % kLedCount;  // full sweep ≈ 7s
    gLeds[pos] = CRGB::White;
    FastLED.show();
}

void LedOutput::rainbow(uint32_t nowMs) {
    uint8_t base = static_cast<uint8_t>(nowMs / 10);
    for (uint16_t i = 0; i < kLedCount; ++i)
        gLeds[i] = CHSV(base + i, 255, 255);
    FastLED.show();
}

}  // namespace vialucis
