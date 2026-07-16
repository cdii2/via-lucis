#pragma once
// FastLED wrapper. Owns the physical strip on GPIO16 (FROZEN — see
// PROGRESS.md) and the global power cap (iron rule: 2m COB at full white can
// out-draw the 10A supply).
//
// B7 (BUGFIX-PLAN-2026-07-15 §3-B): setFrame()/show() are split so a caller
// can copy the frame into the driver's own shadow buffer (gLeds) while still
// holding whatever fence protects the frame's source, release that fence,
// THEN call show() — FastLED.show() bit-bangs the whole strip (~10.8ms with
// interrupts effectively disabled) and must never run inside a cross-task
// lock (the latency-path iron rule: App::tick's fence used to wrap the old
// combined show(frame) across that whole call). setFrame() alone is a plain
// per-pixel copy, cheap enough to keep under a fence.

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
    // Copy `frame` into the shadow buffer (gLeds). No FastLED call — safe to
    // run while still holding the fence that protects `frame`'s source.
    void setFrame(const std::vector<Rgb>& frame);
    // Push the shadow buffer to the physical strip. The ~10.8ms FastLED
    // call — call this AFTER releasing any cross-task fence.
    void show();
    // Back-compat combined call (setFrame + show), kept so existing callers
    // compile unchanged until they migrate to the split above (see the B7
    // report's ASKS FOR B4 for the app.cpp side of that migration).
    void show(const std::vector<Rgb>& frame);
    // The BRINGUP patterns render in core now (ModeDirector, M2) — this
    // class is pure output; blanking is just show()ing a dark frame.
};

}  // namespace vialucis
