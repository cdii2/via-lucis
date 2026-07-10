#include "vialucis/mode_director.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace vialucis {
namespace {

// Plain HSV→RGB (spectrum) — a core reimplementation for the rainbow
// producer; FastLED stays device-side (its hue table differs slightly,
// which is fine for an ambient stub).
Rgb hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
    uint8_t region = h / 43;
    uint8_t rem = (h - region * 43) * 6;
    uint8_t p = static_cast<uint8_t>((v * (255 - s)) >> 8);
    uint8_t q = static_cast<uint8_t>((v * (255 - ((s * rem) >> 8))) >> 8);
    uint8_t t =
        static_cast<uint8_t>((v * (255 - ((s * (255 - rem)) >> 8))) >> 8);
    switch (region) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

}  // namespace

void ModeDirector::onKeyDown(uint8_t note, uint8_t velocity,
                             uint64_t nowUs) {
    lastActivityUs_ = nowUs;  // any key press is activity (wakes AFK)
    if (probe_.onNoteOn(note)) {
        engine_.markFrameDirty();
        return;  // consumed — practice never sees it
    }
    // The Reactive layer hears every press (it only PAINTS in Reactive
    // mode, but keeping state warm means entering the mode mid-hold shows
    // the truth). Cost: array writes, no allocation.
    reactive_.noteOn(note, velocity);
    if (engine_.songLoaded()) {
        engine_.onKeyDown(note, nowUs);
    } else {
        engine_.markFrameDirty();  // reactive glow appears within a frame
    }
}

void ModeDirector::onKeyUp(uint8_t note, uint64_t nowUs) {
    lastActivityUs_ = nowUs;
    reactive_.noteOff(note);
    if (!engine_.songLoaded()) engine_.markFrameDirty();
}

void ModeDirector::onPedal(bool down, uint64_t nowUs) {
    lastActivityUs_ = nowUs;
    reactive_.setPedal(down);
}

bool ModeDirector::setPresentation(bool on) {
    if (on && !engine_.songLoaded()) return false;
    if (presentation_ != on) engine_.markFrameDirty();
    presentation_ = on;
    return true;
}

TopMode ModeDirector::topMode(uint64_t nowUs) const {
    if (engine_.songLoaded())
        return presentation_ ? TopMode::Presentation : TopMode::Practice;
    // Same clock guard idleSec uses: an out-of-order timestamp must read
    // as zero idle, never wrap to instant-AFK.
    if (idleTimeoutSec_ > 0 && lastActivityUs_ != 0 &&
        nowUs >= lastActivityUs_ &&
        nowUs - lastActivityUs_ >=
            static_cast<uint64_t>(idleTimeoutSec_) * 1000000ull)
        return TopMode::Afk;
    return TopMode::Reactive;
}

const char* ModeDirector::topModeName(TopMode m) {
    switch (m) {
        case TopMode::Reactive: return "reactive";
        case TopMode::Afk: return "afk";
        case TopMode::Practice: return "practice";
        case TopMode::Presentation: return "presentation";
    }
    return "reactive";
}

uint32_t ModeDirector::idleSec(uint64_t nowUs) const {
    if (lastActivityUs_ == 0 || nowUs < lastActivityUs_) return 0;
    return static_cast<uint32_t>((nowUs - lastActivityUs_) / 1000000ull);
}

bool ModeDirector::setTestPattern(const std::string& name,
                                  std::vector<MidiOutMsg>& out) {
    if (name == "strip") test_ = Test::Strip;
    else if (name == "rainbow") test_ = Test::Rainbow;
    else if (name == "off") {
        test_ = Test::None;  // never auto-resumes (A35): play re-baselines
        engine_.markFrameDirty();
        return true;
    } else {
        return false;
    }
    // F3/A35: the pattern paints over practice while the scheduler clock
    // would keep running — pause it here so EVERY caller keeps the
    // no-skipped-time-burst guarantee, not just one REST route.
    if (engine_.state() == PlayState::Playing)
        engine_.transport("pause", 0, out);
    engine_.markFrameDirty();
    return true;
}

ModeDirector::ProbeArm ModeDirector::armProbe(uint16_t led, uint64_t nowUs,
                                              uint32_t timeoutMs) {
    if (engine_.state() == PlayState::Playing) return ProbeArm::Playing;
    if (led >= ledCount_) return ProbeArm::BadLed;
    if (timeoutMs < 1000) timeoutMs = 1000;
    if (timeoutMs > 300000) timeoutMs = 300000;
    probe_.arm(led, nowUs, static_cast<uint64_t>(timeoutMs) * 1000);
    engine_.markFrameDirty();
    return ProbeArm::Ok;
}

void ModeDirector::cancelProbe() {
    if (probe_.armed()) engine_.markFrameDirty();
    probe_.cancel();
}

std::string ModeDirector::probeJson() const {
    JsonDocument doc;
    doc["armed"] = probe_.armed();
    doc["led"] = probe_.led();
    if (probe_.hasCapture()) doc["note"] = probe_.capturedNote();
    else doc["note"] = nullptr;
    std::string out;
    serializeJson(doc, out);
    return out;
}

void ModeDirector::tick(uint64_t nowUs, std::vector<MidiOutMsg>& out) {
    if (lastActivityUs_ == 0) lastActivityUs_ = nowUs;  // boot baseline
    if (probe_.tickExpire(nowUs)) engine_.markFrameDirty();
    // Playback starting cancels an armed probe — user intent wins, and the
    // rule holds for ANY path into Playing, not just one REST route.
    if (probe_.armed() && engine_.state() == PlayState::Playing) {
        probe_.cancel();
        engine_.markFrameDirty();
    }
    // Presentation is per-song: when the song goes away (ANY unload path),
    // the flag dies with it — a later load must land in Practice, never a
    // stale Presentation (M-wave closing review).
    if (presentation_ && !engine_.songLoaded()) presentation_ = false;
    engine_.tick(nowUs, out);
    TopMode m = topMode(nowUs);
    if (m != lastMode_) {
        engine_.markFrameDirty();  // mode flips repaint within one frame
        lastMode_ = m;
    }
}

void ModeDirector::paintTestStrip(uint32_t nowMs) {
    std::fill(frame_.begin(), frame_.end(), Rgb{});
    if (frame_.empty()) return;
    uint16_t pos = static_cast<uint16_t>((nowMs / 20) % frame_.size());
    frame_[pos] = Rgb{255, 255, 255};  // same sweep the device layer had
}

void ModeDirector::paintRainbow(uint32_t nowMs) {
    uint8_t base = static_cast<uint8_t>(nowMs / 10);
    for (size_t i = 0; i < frame_.size(); ++i)
        frame_[i] = hsvToRgb(static_cast<uint8_t>(base + i), 255, 255);
}

const std::vector<Rgb>& ModeDirector::renderFrame(uint64_t nowUs) {
    uint32_t nowMs = static_cast<uint32_t>(nowUs / 1000);
    // Forced sources above ALL modes (3A): probe dot outranks the test
    // pattern (an active interactive flow beats a passive one).
    if (probe_.armed()) {
        std::fill(frame_.begin(), frame_.end(), Rgb{});
        if (probe_.led() < frame_.size())
            frame_[probe_.led()] = Rgb{255, 255, 255};
        return frame_;
    }
    if (test_ == Test::Strip) {
        paintTestStrip(nowMs);
        return frame_;
    }
    if (test_ == Test::Rainbow) {
        paintRainbow(nowMs);
        return frame_;
    }
    switch (topMode(nowUs)) {
        case TopMode::Practice:
            return engine_.renderFrame(nowUs);
        case TopMode::Afk:
            paintRainbow(nowMs);  // VL5 stub — E3's playlist replaces it
            return frame_;
        case TopMode::Reactive: {  // E2: the live note-driven layer
            fx::FxFrame f{frame_, fxFrame_, fxFrame_ * fx::kFxStepMs};
            ++fxFrame_;
            reactive_.render(f);
            return frame_;
        }
        case TopMode::Presentation:  // placeholder until the P-wave player
            std::fill(frame_.begin(), frame_.end(), Rgb{});
            return frame_;
    }
    return frame_;
}

}  // namespace vialucis
