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

// Clamp bounds for the 2-point scalars — ONE definition shared by
// PUT /api/settings (settings.cpp) and PUT /api/calibration
// (calibration.cpp), so the two routes can never drift apart.
constexpr float kOffsetMmLimit = 2000.0f;
constexpr float kLedsPerMeterMin = 10.0f;
constexpr float kLedsPerMeterMax = 1000.0f;

// One definition of the product-default AFK timeout — Settings AND the
// ModeDirector's pre-configure default both read it (M-wave closing review:
// two hardcoded 180s would let the native tests silently pin a stale one).
constexpr uint32_t kDefaultAfkTimeoutSec = 180;

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
    // wifiPass is WRITE-ONLY across the REST boundary (ruling §6-1): a LAN/AP
    // client can set it but must never read it back. Redaction is a property of
    // this field, enforced by toJson(View::Public) below — NOT by the route
    // handler. On the wire the Public view emits `wifiPassSet` (bool) instead.
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

    // AFK idle timeout (M3, brief §1): seconds of no-song inactivity before
    // ambient lighting arms. 0 = never.
    uint32_t afkTimeoutSec = kDefaultAfkTimeoutSec;

    // Recording byte budget (v3 REC4, docs/DESIGN-record.md §8): the ceiling
    // on a single take, in KB. Bounds both the in-RAM capture buffer and the
    // free-space check before arming. Clamped 16–256 KB — the 256 KB per-song
    // save ceiling (kMaxSongBytes) is the hard outer bound (B5 ask 4: a bigger
    // take can't be written, so the old 1024 max was unsaveable). Default 64:
    // arm() reserves the
    // whole budget as ONE contiguous heap block, and a stock ESP32 with
    // BLE + WiFi up rarely has a 256 KB block free — 64 KB ≈ 8k events,
    // ample for the 10-min cap; PSRAM boards can raise it. Appended after
    // the v1 key set — the contract test locks this (the one sanctioned v1
    // contract change).
    uint32_t recordBudgetKB = 64;

    // Which audience a serialization is for — secret-ness is a property of the
    // field, decided HERE, not at the route handler (ruling §6-1):
    //   Persist — the full document written to LittleFS flash. Carries
    //             `wifiPass` so a reboot reconnects to WiFi. Used ONLY by the
    //             flash-save path (SongStore::saveSettings).
    //   Public  — everything that LEAVES the device: GET /api/settings, the PUT
    //             echo, and the webui "Export" (which just downloads GET).
    //             `wifiPass` is REDACTED — the key is absent and replaced by
    //             `wifiPassSet: true|false` (true ⇔ a non-empty password is
    //             stored). A native pin asserts Public never contains wifiPass.
    // Default is Persist so the load-bearing flash path is unchanged; every
    // outward call-site MUST pass View::Public explicitly.
    enum class View { Persist, Public };
    std::string toJson(View view = View::Persist) const;
    // Tolerant PATCH: missing fields keep current values; false only on
    // unparseable input. Numeric fields are clamped to sane ranges.
    // wifiPass is write-only (ruling §6-1): a body WITH `wifiPass` sets it, a
    // body WITHOUT it leaves the stored password unchanged, and an explicit
    // `"wifiPass": ""` CLEARS it (A139). Clients that don't intend to change
    // the password must OMIT the key — never send an empty string.
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
