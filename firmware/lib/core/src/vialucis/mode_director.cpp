#include "vialucis/mode_director.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "vialucis/fx/fx_color.h"

namespace vialucis {

void ModeDirector::onKeyDown(uint8_t note, uint8_t velocity,
                             uint64_t nowUs) {
    lastActivityUs_ = nowUs;  // any key press is activity (wakes AFK)
    if (probe_.onNoteOn(note)) {
        engine_.markFrameDirty();
        return;  // consumed — practice never sees it
    }
    // Practice hears the press FIRST (the match is the product); the
    // Reactive layer's state stays warm after it. Its noteOn is a LUT
    // lookup + array writes — no allocation, no transcendental math
    // (velocity curve precomputed in setParams; iron-rule scrutiny, A50).
    if (engine_.songLoaded()) {
        if (scoreFollowActive()) {
            // P4: the performer IS the Presentation clock. Echo-guarded
            // inside the follower; a wrong note is ignored for the clock
            // and can never red-flash (the transport is stopped, so the
            // practice verdict path below is inert by construction). The
            // snap lands on the very next frame.
            follower_.onNote(note, nowUs);
            engine_.driveShowClock(follower_.positionUs(nowUs));
            engine_.markFrameDirty();
        }
        engine_.onKeyDown(note, nowUs);
        reactive_.noteOn(note, velocity);
    } else {
        reactive_.noteOn(note, velocity);
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
    // Leaving presentation ends any playing show — the two states must
    // never desync (a hidden still-playing show would 409 every upload
    // and keep the song clock audibly running; P-wave closing review).
    if (!on) showPlaying_ = false;
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
    // the flag AND any playing show die with it — a later load must land
    // in Practice, never a stale Presentation (M-wave closing review).
    if (presentation_ && !engine_.songLoaded()) {
        presentation_ = false;
        showPlaying_ = false;
    }
    engine_.tick(nowUs, out);
    // P4: between key events the score-follow clock still moves (coast /
    // hold / free-run are functions of real time) — drive song time from
    // the follower's estimate every tick so the show breathes continuously.
    if (scoreFollowActive())
        engine_.driveShowClock(follower_.positionUs(nowUs));
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
    // Same hue mapping every real effect uses (fx_color's rainbow) — the
    // bring-up test pattern must match what effects will show on the same
    // strip (E-wave closing review).
    uint8_t base = static_cast<uint8_t>(nowMs / 10);
    for (size_t i = 0; i < frame_.size(); ++i)
        fx::hsv2rgbRainbow(
            fx::Hsv{static_cast<uint8_t>(base + i), 255, 255}, frame_[i]);
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
            afk_.render(frame_);  // E3: the playlist sequencer
            return frame_;
        case TopMode::Reactive: {  // E2: the live note-driven layer
            fx::FxFrame f{frame_, fxFrame_, fxFrame_ * fx::kFxStepMs};
            ++fxFrame_;
            reactive_.render(f);
            return frame_;
        }
        case TopMode::Presentation:
            if (showPlaying_) {  // P2: the show reads the song-time clock
                showPlayer_.renderAt(
                    static_cast<uint32_t>(engine_.positionUs() / 1000),
                    frame_);
                return frame_;
            }
            std::fill(frame_.begin(), frame_.end(), Rgb{});
            return frame_;
    }
    return frame_;
}

}  // namespace vialucis
