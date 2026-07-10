#pragma once
// Ported from FastLED 3.10.3 examples/Pride2015/Pride2015.ino (MIT,
// https://github.com/FastLED/FastLED, tag 3.10.3) — Mark Kriegsman's
// "animated, ever-changing rainbows". Deterministic port (VL7): the
// static locals became members, millis() became FxFrame::ms (same uint16
// truncation), no randomness. The nblend(64) accumulation blends into the
// caller's persistent buffer exactly like the sketch's led array.

#include "vialucis/fx/effect.h"
#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"

namespace vialucis {
namespace fx {

class Pride2015Fx : public Effect {
public:
    void reset(uint32_t /*seed*/, uint16_t ledCount) override {
        ledCount_ = ledCount;
        sPseudotime_ = 0;
        sLastMillis_ = 0;
        sHue16_ = 0;
    }

    void render(FxFrame& f) override {
        const uint16_t n =
            ledCount_ < f.leds.size() ? ledCount_
                                      : static_cast<uint16_t>(f.leds.size());
        if (n == 0) return;

        uint8_t sat8 = beatsin88(87, 220, 250, f.ms);
        uint8_t brightdepth = beatsin88(341, 96, 224, f.ms);
        uint16_t brightnessthetainc16 =
            beatsin88(203, (25 * 256), (40 * 256), f.ms);
        uint8_t msmultiplier = beatsin88(147, 23, 60, f.ms);

        uint16_t hue16 = sHue16_;
        uint16_t hueinc16 = beatsin88(113, 1, 3000, f.ms);

        uint16_t ms = static_cast<uint16_t>(f.ms);  // sketch's uint16 clock
        uint16_t deltams = ms - sLastMillis_;
        sLastMillis_ = ms;
        sPseudotime_ += deltams * msmultiplier;
        sHue16_ += deltams * beatsin88(400, 5, 9, f.ms);
        uint16_t brightnesstheta16 = sPseudotime_;

        for (uint16_t i = 0; i < n; ++i) {
            hue16 += hueinc16;
            uint8_t hue8 = hue16 / 256;

            brightnesstheta16 += brightnessthetainc16;
            uint16_t b16 = sin16(brightnesstheta16) + 32768;

            uint16_t bri16 =
                static_cast<uint16_t>((static_cast<uint32_t>(b16) *
                                       static_cast<uint32_t>(b16)) /
                                      65536);
            uint8_t bri8 = static_cast<uint8_t>(
                (static_cast<uint32_t>(bri16) * brightdepth) / 65536);
            bri8 += (255 - brightdepth);

            Rgb newcolor;
            hsv2rgbRainbow(Hsv{hue8, sat8, bri8}, newcolor);

            uint16_t pixelnumber = static_cast<uint16_t>((n - 1) - i);
            nblend(f.leds[pixelnumber], newcolor, 64);
        }
    }

private:
    uint16_t ledCount_ = 0;
    uint16_t sPseudotime_ = 0;
    uint16_t sLastMillis_ = 0;
    uint16_t sHue16_ = 0;
};

}  // namespace fx
}  // namespace vialucis
