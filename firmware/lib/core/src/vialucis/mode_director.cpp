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
    // Recording tap (REC3) — AFTER the engine verdict/light, so it adds
    // NOTHING between key press and light (iron rule). O(1) append into the
    // pre-reserved buffer; a no-op when capture is Idle, and capture's OWN
    // echo guard drops device echoes. Channel 0: the BLE note callback carries
    // no channel, and the hand split is by pitch downstream, not by channel.
    capture_.onNoteOn(note, velocity, 0, nowUs);
}

void ModeDirector::onKeyUp(uint8_t note, uint64_t nowUs) {
    lastActivityUs_ = nowUs;
    reactive_.noteOff(note);
    if (!engine_.songLoaded()) engine_.markFrameDirty();
    capture_.onNoteOff(note, 0, nowUs);  // REC3 tap (no-op when Idle)
}

void ModeDirector::onPedal(uint8_t value, uint64_t nowUs) {
    lastActivityUs_ = nowUs;
    reactive_.setPedal(value >= 64);      // latch edge for the reactive layer
    capture_.onPedal(value, 0, nowUs);    // REC3 tap: the raw CC64 value
}

ArmResult ModeDirector::armRecord(size_t budgetBytes, bool countIn,
                                  uint16_t bpm, uint64_t nowUs) {
    ArmResult r = capture_.arm(budgetBytes, kRecordMaxMs, nowUs);
    if (r != ArmResult::Armed) return r;
    // Arming is write activity: it resets the idle clock so AFK stays disarmed
    // for the take (Record also outranks Afk in topMode, belt and braces).
    lastActivityUs_ = nowUs;
    // Count-in is Free-capture only — a loaded song already leads the tempo.
    countIn_ = countIn && !engine_.songLoaded();
    bpm_ = bpm < 20 ? 20 : (bpm > 300 ? 300 : bpm);
    armUs_ = nowUs;
    engine_.markFrameDirty();
    return r;
}

CaptureTake ModeDirector::stopRecord(uint64_t nowUs) {
    countIn_ = false;
    engine_.markFrameDirty();
    return capture_.stop(nowUs);
}

void ModeDirector::discardRecord() {
    countIn_ = false;
    capture_.discard();
    engine_.markFrameDirty();
}

uint16_t ModeDirector::computeHeartbeatLed(const KeyLedTable& t) const {
    // One pixel outside the key span, so the heartbeat never sits on a key the
    // Reactive monitor might light. Prefer just above the top key, then just
    // below the bottom key; if the keys fill the whole strip, fall back to 0.
    uint16_t minLed = ledCount_, maxLed = 0;
    bool any = false;
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n) {
        LedRange r = t.forNoteOrdered(n);
        if (!r.valid) continue;
        any = true;
        if (r.first < minLed) minLed = r.first;
        if (r.last > maxLed) maxLed = r.last;
    }
    if (!any) return ledCount_ ? static_cast<uint16_t>(ledCount_ - 1) : 0;
    if (maxLed + 1 < ledCount_) return static_cast<uint16_t>(maxLed + 1);
    if (minLed > 0) return static_cast<uint16_t>(minLed - 1);
    return 0;
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
    // Free capture (no song): an armed/recording take is the Record top-mode,
    // ABOVE Afk and Reactive. Checked before the idle timeout so a take left
    // armed past the timeout never drifts to AFK (§7a: arming disarms AFK).
    if (capture_.state() != CaptureState::Idle) return TopMode::Record;
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
        case TopMode::Record: return "record";
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
    // REC3 echo-feed: every note-on we emit to the piano this tick registers a
    // credit in capture's OWN echo guard, so the piano's echo of it (arriving
    // on a later BLE poll) is dropped from a Play-along take. REST-path
    // emissions (stop / all-off) bypass this scan, but those are note-offs in
    // practice and are never captured as presses (A65). No-op unless armed.
    if (capture_.state() != CaptureState::Idle)
        for (const MidiOutMsg& m : out) {
            if (m.type == MidiOutType::NoteOn)
                capture_.noteSent(m.data1, nowUs);
            // The CC64 pass-through echoes too (§5a covers the pedal as much
            // as the notes) — credit capture's pedal-echo window per send.
            else if (m.type == MidiOutType::Cc && m.data1 == 64)
                capture_.pedalSent(nowUs);
        }
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

void ModeDirector::paintRecordFrame(uint64_t nowUs) {
    // 1) The Reactive monitor is the base — in Free capture the lights follow
    //    YOU as you press (DESIGN-record §9a), exactly the Reactive layer.
    fx::FxFrame f{frame_, fxFrame_, fxFrame_ * fx::kFxStepMs};
    ++fxFrame_;
    reactive_.render(f);
    // 2) Optional count-in (Free capture only): a 1-bar, 4-beat visual pulse
    //    over the whole strip after arming (§3). It NEVER gates the take —
    //    capture still starts on the first real note (leading silence trimmed).
    //    A dim white flash decaying across each beat; no audio in v1.
    if (countIn_ && capture_.state() == CaptureState::Armed && nowUs >= armUs_) {
        uint32_t elapsedMs = static_cast<uint32_t>((nowUs - armUs_) / 1000);
        uint32_t beatMs = bpm_ ? 60000u / bpm_ : 0;
        if (beatMs > 0 && elapsedMs < 4u * beatMs) {
            uint32_t intoBeat = elapsedMs % beatMs;
            uint32_t level = 200u * (beatMs - intoBeat) / beatMs;  // bright→0
            for (Rgb& px : frame_) {
                px.r = static_cast<uint8_t>(std::min<uint32_t>(255, px.r + level));
                px.g = static_cast<uint8_t>(std::min<uint32_t>(255, px.g + level));
                px.b = static_cast<uint8_t>(std::min<uint32_t>(255, px.b + level));
            }
        }
    }
    // 3) The discreet recording heartbeat: ONE reserved pixel outside the key
    //    range, a slow-breathing dim amber that avoids red/blue/green (§9a).
    //    ~2s breath period. Painted last so it always shows.
    if (heartbeatLed_ < frame_.size()) {
        uint32_t phase = static_cast<uint32_t>(nowUs / 1000) % 2000;
        uint32_t tri = phase < 1000 ? phase : (2000 - phase);   // 0..1000
        uint8_t amp = static_cast<uint8_t>(20 + (40 * tri) / 1000);  // 20..60
        frame_[heartbeatLed_] =
            Rgb{amp, static_cast<uint8_t>(amp * 2 / 5), 0};  // warm amber
    }
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
        case TopMode::Record:  // REC3: Free-capture monitor + heartbeat
            paintRecordFrame(nowUs);
            return frame_;
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
