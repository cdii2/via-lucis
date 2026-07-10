#pragma once
// Persistent device settings (LittleFS: /settings.json on the device).
// Pure C++ + ArduinoJson (portable, no Arduino headers) → native-testable.
// Field names here ARE the REST contract for GET/PUT /api/settings — see
// docs/API.md; change them in both places or not at all.

#include <cstdint>
#include <string>

#include "vialucis/frame_renderer.h"  // Rgb
#include "vialucis/key_led_map.h"     // LedMapConfig

namespace vialucis {

struct Settings {
    // Colors (SPEC: defaults left=blue right=green wrong=red, customizable,
    // defaults must never collide with wrong).
    Rgb leftColor{0, 0, 255};
    Rgb rightColor{0, 255, 0};
    Rgb wrongColor{255, 0, 0};

    // Lookahead ramp — the ONE cap and ONE lead-time variable (LOCKED).
    float previewCap = 0.45f;  // 0..1
    uint32_t leadMs = 1000;    // 0..10000

    // Calibration — exactly two settings (SPEC).
    float offsetMm = 0.0f;
    float ledsPerMeter = 180.0f;

    uint8_t brightness = 160;     // global FastLED brightness backstop
    uint32_t echoWindowMs = 250;  // echo-guard window; retune at bring-up

    std::string wifiSsid;  // empty ⇒ start AP "ViaLucis" directly
    std::string wifiPass;

    // Incoming Re-press (Q3, brief §2). Percents are 0–100 on the wire;
    // repeatColor is validated ≠ wrongColor (a cue must never read as an
    // error). Appended after the v1 key set — the contract test locks this.
    bool repeatCueEnabled = true;
    Rgb repeatColor{255, 255, 255};
    uint8_t repeatFillStartPct = 0;
    uint8_t repeatFillPeakPct = 45;  // 100 ⇒ pure hue-snap glide
    uint32_t repeatFloorMs = 35;
    uint32_t repeatWaitPulseMs = 60;

    std::string toJson() const;
    // Tolerant: missing fields keep current values; false only on unparseable
    // input. Numeric fields are clamped to sane ranges.
    static bool fromJson(const char* json, Settings& out);

    LedMapConfig ledMapConfig(uint16_t ledCount = 360) const {
        LedMapConfig c;
        c.ledsPerMeter = ledsPerMeter;
        c.offsetMm = offsetMm;
        c.ledCount = ledCount;
        return c;
    }
    RampConfig rampConfig() const {
        RampConfig r;
        r.previewCap = previewCap;
        r.leadUs = static_cast<uint64_t>(leadMs) * 1000;
        return r;
    }
};

}  // namespace vialucis
