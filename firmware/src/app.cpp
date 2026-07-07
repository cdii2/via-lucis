#include "app.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "vialucis/version.h"

namespace vialucis {

namespace {
constexpr uint64_t kWrongFlashUs = 300000;   // red flash duration
constexpr uint64_t kFramePeriodUs = 16667;   // ~60 fps LED refresh
}  // namespace

void App::begin() {
    store_.begin();
    store_.loadSettings(settings_);  // keeps defaults if absent
    guard_.setWindowUs(static_cast<uint64_t>(settings_.echoWindowMs) * 1000);
    renderer_ = FrameRenderer(settings_.ledMapConfig(LedOutput::kLedCount),
                              settings_.rampConfig());
    leds_.begin(settings_.brightness);
    emitter_.setEchoGuard(&guard_);
    ble_.begin();
    ble_.onNoteOn([this](uint8_t note, uint8_t vel) {
        onPianoNoteOn(note, vel, static_cast<uint64_t>(esp_timer_get_time()));
    });
}

Rgb App::colorForTrack(uint8_t track) const {
    Hand h = track < trackCfg_.tracks.size() ? trackCfg_.tracks[track].hand
                                             : Hand::Both;
    if (h == Hand::Left) return settings_.leftColor;
    return settings_.rightColor;  // Right and Both use the right-hand color
}

void App::rebuildEngine() {
    sched_ = std::make_unique<Scheduler>(song_);
    trackCfg_ = TrackConfig::defaultsFor(song_);
    wait_ = std::make_unique<WaitMode>(*sched_, kTrackMaskAll);
    wait_->setEchoGuard(&guard_);
    soundingLights_.clear();
    wrongFlashes_.clear();
    state_ = PlayState::Idle;
    prevPosUs_ = 0;
    applyMasks();
}

void App::applyMasks() {
    if (!sched_) return;
    if (mode_ == Mode::Demo)
        emitter_.setEmitMask(trackCfg_.audibleMask());
    else if (mode_ == Mode::Accompaniment)
        emitter_.setEmitMask(trackCfg_.accompanimentMask(practice_));
    else
        emitter_.setEmitMask(0);  // wait/follow: the piano is the player's

    if (wait_) {
        wait_->setPracticedMask(trackCfg_.practicedMask(practice_));
        if (mode_ == Mode::Wait || mode_ == Mode::Accompaniment)
            wait_->resync();
        else
            sched_->clearBarrier();
    }
}

void App::stopAllSound() {
    for (const MidiOutMsg& m : emitter_.allOff()) ble_.send(m);
    leds_.allOff();
    soundingLights_.clear();
}

bool App::loadSong(const std::string& name) {
    std::vector<uint8_t> data;
    if (!store_.read(name, data)) return false;
    MidiParseResult r = parseMidi(data.data(), data.size());
    if (r.error != MidiParseError::Ok) return false;
    stopAllSound();
    song_ = std::move(r.song);
    songName_ = name;
    rebuildEngine();
    return true;
}

bool App::transport(const std::string& action, uint32_t positionMs) {
    if (!sched_) return false;
    if (action == "play") {
        if (state_ == PlayState::Finished) {
            for (const SchedEvent& e : sched_->seek(0)) (void)e;
            prevPosUs_ = 0;
        }
        if (mode_ == Mode::Wait || mode_ == Mode::Accompaniment)
            wait_->resync();
        state_ = PlayState::Playing;
        lastTickUs_ = 0;  // next tick re-baselines the clock
        return true;
    }
    if (action == "pause") {
        state_ = PlayState::Idle;
        stopAllSound();
        return true;
    }
    if (action == "stop") {
        state_ = PlayState::Idle;
        stopAllSound();
        sched_->seek(0);
        prevPosUs_ = 0;
        if (mode_ == Mode::Wait || mode_ == Mode::Accompaniment)
            wait_->resync();
        return true;
    }
    if (action == "seek") {
        stopAllSound();
        sched_->seek(static_cast<uint64_t>(positionMs) * 1000);
        prevPosUs_ = sched_->positionUs();
        if (mode_ == Mode::Wait || mode_ == Mode::Accompaniment)
            wait_->resync();
        if (state_ == PlayState::Finished) state_ = PlayState::Idle;
        return true;
    }
    return false;
}

bool App::setMode(const std::string& mode, const std::string& practice) {
    if (mode == "wait") mode_ = Mode::Wait;
    else if (mode == "follow") mode_ = Mode::Follow;
    else if (mode == "demo") mode_ = Mode::Demo;
    else if (mode == "accompaniment") mode_ = Mode::Accompaniment;
    else return false;
    if (practice == "left") practice_ = Hand::Left;
    else if (practice == "right") practice_ = Hand::Right;
    else if (practice == "both" || practice.empty()) practice_ = Hand::Both;
    else return false;
    stopAllSound();
    applyMasks();
    return true;
}

bool App::setTempo(float percent) {
    if (!sched_) return false;
    sched_->setTempoPercent(percent);
    return true;
}

bool App::setLoop(bool enabled, uint32_t startMs, uint32_t endMs) {
    if (!sched_) return false;
    if (!enabled) {
        sched_->clearLoop();
        loopEnabled_ = false;
        return true;
    }
    if (endMs <= startMs) return false;
    sched_->setLoop(static_cast<uint64_t>(startMs) * 1000,
                    static_cast<uint64_t>(endMs) * 1000);
    loopEnabled_ = true;
    loopStartMs_ = startMs;
    loopEndMs_ = endMs;
    return true;
}

bool App::setTrack(size_t index, const std::string& hand, bool lights) {
    if (index >= trackCfg_.tracks.size()) return false;
    Hand h;
    if (hand == "left") h = Hand::Left;
    else if (hand == "right") h = Hand::Right;
    else if (hand == "both") h = Hand::Both;
    else if (hand == "off") h = Hand::Off;
    else return false;
    trackCfg_.tracks[index].hand = h;
    trackCfg_.tracks[index].lights = lights;
    applyMasks();
    return true;
}

bool App::setTestPattern(const std::string& pattern) {
    if (pattern == "strip") test_ = TestPattern::Strip;
    else if (pattern == "rainbow") test_ = TestPattern::Rainbow;
    else if (pattern == "off") {
        test_ = TestPattern::None;
        leds_.allOff();
    } else {
        return false;
    }
    return true;
}

void App::applySettings() {
    guard_.setWindowUs(static_cast<uint64_t>(settings_.echoWindowMs) * 1000);
    renderer_ = FrameRenderer(settings_.ledMapConfig(LedOutput::kLedCount),
                              settings_.rampConfig());
    leds_.setBrightness(settings_.brightness);
    store_.saveSettings(settings_);
}

void App::onPianoNoteOn(uint8_t note, uint8_t velocity, uint64_t nowUs) {
    (void)velocity;
    if (!wait_ || state_ != PlayState::Playing) return;
    if (mode_ != Mode::Wait && mode_ != Mode::Accompaniment) return;
    KeyFeedback fb = wait_->onKeyDown(note, nowUs);
    if (fb.verdict == KeyVerdict::Wrong)
        wrongFlashes_.push_back({note, nowUs + kWrongFlashUs});
    // Latency path (iron rule): don't wait for the next 60fps frame slot —
    // flag the frame dirty so the very next tick() renders the verdict.
    if (fb.verdict == KeyVerdict::Wrong || fb.verdict == KeyVerdict::Cleared)
        frameDirty_ = true;
}

void App::tick(uint64_t nowUs) {
    ble_.poll();

    if (test_ != TestPattern::None) {
        uint32_t ms = static_cast<uint32_t>(nowUs / 1000);
        if (test_ == TestPattern::Strip) leds_.testPattern(ms);
        else leds_.rainbow(ms);
        return;
    }

    if (sched_ && state_ == PlayState::Playing) {
        if (lastTickUs_ == 0) lastTickUs_ = nowUs;
        uint64_t delta = nowUs - lastTickUs_;
        lastTickUs_ = nowUs;

        std::vector<SchedEvent> events = sched_->advance(delta);
        uint64_t newPos = sched_->positionUs();
        if (newPos < prevPosUs_) {  // loop wrapped
            if (mode_ == Mode::Wait || mode_ == Mode::Accompaniment)
                wait_->resync();
            soundingLights_.clear();
        }
        prevPosUs_ = newPos;

        if (mode_ == Mode::Wait || mode_ == Mode::Accompaniment)
            wait_->update();

        for (const MidiOutMsg& m : emitter_.consume(events, nowUs))
            ble_.send(m);

        uint32_t lightsMask = trackCfg_.lightsMask();
        for (const SchedEvent& e : events) {
            if (!trackInMask(lightsMask, e.track)) continue;
            if (e.type == SchedEventType::NoteOn) {
                soundingLights_.push_back({e.note, e.track});
            } else if (e.type == SchedEventType::NoteOff) {
                for (size_t i = 0; i < soundingLights_.size(); ++i) {
                    if (soundingLights_[i].note == e.note &&
                        soundingLights_[i].track == e.track) {
                        soundingLights_.erase(soundingLights_.begin() + i);
                        break;
                    }
                }
            }
        }

        if (sched_->finished()) {
            state_ = PlayState::Finished;
            stopAllSound();
        }
    }

    if (frameDirty_ || nowUs - lastFrameUs_ >= kFramePeriodUs) {
        frameDirty_ = false;
        lastFrameUs_ = nowUs;
        renderFrame(nowUs);
    }
}

void App::renderFrame(uint64_t nowUs) {
    renderer_.clear();
    if (sched_ && state_ == PlayState::Playing) {
        uint64_t pos = sched_->positionUs();
        uint32_t lightsMask = trackCfg_.lightsMask();

        // Ramp preview: notes coming up within the lead window.
        uint64_t lead = renderer_.ramp().leadUs;
        for (const SchedEvent& e :
             sched_->onsetsBetween(pos + 1, pos + lead, lightsMask))
            renderer_.addUpcoming(e.note, colorForTrack(e.track), e.timeUs,
                                  pos);

        // Sounding notes (follow/demo, and the un-practiced hand elsewhere).
        for (const SoundingLight& s : soundingLights_)
            renderer_.addDue(s.note, colorForTrack(s.track));

        // Wait mode: the due chord at 100%.
        if ((mode_ == Mode::Wait || mode_ == Mode::Accompaniment) &&
            wait_->chordPending()) {
            for (const SchedEvent& e : sched_->notesOnAt(
                     wait_->barrierTimeUs(),
                     trackCfg_.practicedMask(practice_))) {
                bool stillPending = false;
                for (uint8_t p : wait_->pendingNotes())
                    if (p == e.note) stillPending = true;
                if (stillPending && trackInMask(lightsMask, e.track))
                    renderer_.addDue(e.note, colorForTrack(e.track));
            }
        }
    }

    // Wrong flashes ride on top of everything.
    size_t w = 0;
    while (w < wrongFlashes_.size()) {
        if (nowUs >= wrongFlashes_[w].untilUs) {
            wrongFlashes_.erase(wrongFlashes_.begin() + w);
        } else {
            renderer_.addWrong(wrongFlashes_[w].note, settings_.wrongColor);
            ++w;
        }
    }

    leds_.show(renderer_.frame());
}

std::string App::statusJson() const {
    JsonDocument doc;
    doc["version"] = kVersion;
    doc["song"] = songName_;
    const char* state = "idle";
    if (state_ == PlayState::Playing)
        state = (wait_ && sched_ && sched_->atBarrier()) ? "waiting" : "playing";
    else if (state_ == PlayState::Finished)
        state = "finished";
    doc["state"] = state;
    const char* mode = "wait";
    if (mode_ == Mode::Follow) mode = "follow";
    else if (mode_ == Mode::Demo) mode = "demo";
    else if (mode_ == Mode::Accompaniment) mode = "accompaniment";
    doc["mode"] = mode;
    doc["positionMs"] = sched_ ? sched_->positionUs() / 1000 : 0;
    doc["durationMs"] = sched_ ? sched_->durationUs() / 1000 : 0;
    doc["tempoPercent"] = sched_ ? sched_->tempoPercent() : 100.0f;
    JsonObject loop = doc["loop"].to<JsonObject>();
    loop["enabled"] = loopEnabled_;
    loop["startMs"] = loopStartMs_;
    loop["endMs"] = loopEndMs_;

    JsonArray tracks = doc["tracks"].to<JsonArray>();
    for (size_t i = 0; i < trackCfg_.tracks.size(); ++i) {
        JsonObject t = tracks.add<JsonObject>();
        t["index"] = i;
        t["name"] = song_.tracks.size() > i ? song_.tracks[i].name : "";
        const char* hand = "off";
        switch (trackCfg_.tracks[i].hand) {
            case Hand::Left: hand = "left"; break;
            case Hand::Right: hand = "right"; break;
            case Hand::Both: hand = "both"; break;
            case Hand::Off: hand = "off"; break;
        }
        t["hand"] = hand;
        t["lights"] = trackCfg_.tracks[i].lights;
    }

    JsonArray pending = doc["pendingNotes"].to<JsonArray>();
    if (wait_ && (mode_ == Mode::Wait || mode_ == Mode::Accompaniment))
        for (uint8_t n : wait_->pendingNotes()) pending.add(n);

    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace vialucis
