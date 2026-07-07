#include "vialucis/playback_engine.h"

#include <ArduinoJson.h>

#include "vialucis/version.h"

namespace vialucis {

namespace {
constexpr uint64_t kWrongFlashUs = 300000;   // red flash duration
constexpr uint64_t kFramePeriodUs = 16667;   // ~60 fps LED refresh
}  // namespace

PlaybackEngine::PlaybackEngine() {
    emitter_.setEchoGuard(&guard_);
    Settings defaults;
    leftColor_ = defaults.leftColor;
    rightColor_ = defaults.rightColor;
    wrongColor_ = defaults.wrongColor;
    eventsBuf_.reserve(64);
    queryBuf_.reserve(64);
}

void PlaybackEngine::configure(const Settings& s, uint16_t ledCount) {
    guard_.setWindowUs(static_cast<uint64_t>(s.echoWindowMs) * 1000);
    renderer_ = FrameRenderer(s.ledMapConfig(ledCount), s.rampConfig());
    leftColor_ = s.leftColor;
    rightColor_ = s.rightColor;
    wrongColor_ = s.wrongColor;
}

Rgb PlaybackEngine::colorForTrack(uint8_t track) const {
    Hand h = track < trackCfg_.tracks.size() ? trackCfg_.tracks[track].hand
                                             : Hand::Both;
    if (h == Hand::Left) return leftColor_;
    return rightColor_;  // Right and Both use the right-hand color
}

void PlaybackEngine::rebuildAfterLoad() {
    sched_ = std::make_unique<Scheduler>(song_);
    trackCfg_ = TrackConfig::defaultsFor(song_);
    wait_ = std::make_unique<WaitMode>(*sched_, kTrackMaskAll);
    wait_->setEchoGuard(&guard_);
    soundingLights_.clear();
    wrongFlashes_.clear();
    state_ = PlayState::Idle;
    prevPosUs_ = 0;
    // The fresh Scheduler has no loop; reset the status mirror fields so
    // /api/status stops reporting the previous song's loop (F2, A34).
    loopEnabled_ = false;
    loopStartMs_ = 0;
    loopEndMs_ = 0;
    applyMasks();
}

void PlaybackEngine::applyMasks() {
    if (!sched_) return;
    if (mode_ == Mode::Demo)
        emitter_.setEmitMask(trackCfg_.audibleMask());
    else if (mode_ == Mode::Accompaniment)
        emitter_.setEmitMask(trackCfg_.accompanimentMask(practice_));
    else
        emitter_.setEmitMask(0);  // wait/follow: the piano is the player's

    if (wait_) {
        wait_->setPracticedMask(trackCfg_.practicedMask(practice_));
        if (barrierMode())
            wait_->resync();
        else
            sched_->clearBarrier();
    }
}

void PlaybackEngine::stopAllSound(std::vector<MidiOutMsg>& out) {
    emitter_.allOff(out);
    soundingLights_.clear();
    // The strip clears on the very next tick: a dirty frame renders
    // immediately, and with nothing sounding it comes out dark (A27).
    frameDirty_ = true;
}

void PlaybackEngine::loadSong(MidiSong&& song, const std::string& name,
                              std::vector<MidiOutMsg>& out) {
    stopAllSound(out);
    song_ = std::move(song);
    songName_ = name;
    rebuildAfterLoad();
}

bool PlaybackEngine::transport(const std::string& action, uint32_t positionMs,
                               std::vector<MidiOutMsg>& out) {
    if (!sched_) return false;
    if (action == "play") {
        if (state_ == PlayState::Finished) {
            sched_->seek(0);  // flushed note-offs are moot: nothing sounding
            prevPosUs_ = 0;
        }
        if (barrierMode()) wait_->resync();
        state_ = PlayState::Playing;
        lastTickUs_ = 0;  // next tick re-baselines the clock
        return true;
    }
    if (action == "pause") {
        state_ = PlayState::Idle;
        stopAllSound(out);
        return true;
    }
    if (action == "stop") {
        state_ = PlayState::Idle;
        stopAllSound(out);
        sched_->seek(0);
        prevPosUs_ = 0;
        if (barrierMode()) wait_->resync();
        return true;
    }
    if (action == "seek") {
        stopAllSound(out);
        sched_->seek(static_cast<uint64_t>(positionMs) * 1000);
        prevPosUs_ = sched_->positionUs();
        if (barrierMode()) wait_->resync();
        if (state_ == PlayState::Finished) state_ = PlayState::Idle;
        return true;
    }
    return false;
}

bool PlaybackEngine::setMode(const std::string& mode,
                             const std::string& practice,
                             std::vector<MidiOutMsg>& out) {
    if (mode == "wait") mode_ = Mode::Wait;
    else if (mode == "follow") mode_ = Mode::Follow;
    else if (mode == "demo") mode_ = Mode::Demo;
    else if (mode == "accompaniment") mode_ = Mode::Accompaniment;
    else return false;
    if (practice == "left") practice_ = Hand::Left;
    else if (practice == "right") practice_ = Hand::Right;
    else if (practice == "both" || practice.empty()) practice_ = Hand::Both;
    else return false;
    stopAllSound(out);
    applyMasks();
    return true;
}

bool PlaybackEngine::setTempo(float percent) {
    if (!sched_) return false;
    sched_->setTempoPercent(percent);
    return true;
}

bool PlaybackEngine::setLoop(bool enabled, uint32_t startMs, uint32_t endMs) {
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

bool PlaybackEngine::setTrack(size_t index, const std::string& hand,
                              bool lights) {
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

void PlaybackEngine::onKeyDown(uint8_t note, uint64_t nowUs) {
    if (!wait_ || state_ != PlayState::Playing) return;
    if (!barrierMode()) return;
    KeyFeedback fb = wait_->onKeyDown(note, nowUs);
    if (fb.verdict == KeyVerdict::Wrong)
        wrongFlashes_.push_back({note, nowUs + kWrongFlashUs});
    // Latency path (iron rule): don't wait for the next 60fps frame slot —
    // flag the frame dirty so the very next tick() renders the verdict.
    if (fb.verdict == KeyVerdict::Wrong || fb.verdict == KeyVerdict::Cleared)
        frameDirty_ = true;
}

void PlaybackEngine::tick(uint64_t nowUs, std::vector<MidiOutMsg>& out) {
    if (!sched_ || state_ != PlayState::Playing) return;

    if (lastTickUs_ == 0) lastTickUs_ = nowUs;
    uint64_t delta = nowUs - lastTickUs_;
    lastTickUs_ = nowUs;

    sched_->advance(delta, eventsBuf_);
    uint64_t newPos = sched_->positionUs();
    if (newPos < prevPosUs_) {  // loop wrapped
        if (barrierMode()) wait_->resync();
        soundingLights_.clear();
    }
    prevPosUs_ = newPos;

    if (barrierMode()) wait_->update();

    emitter_.consume(eventsBuf_, nowUs, out);

    uint32_t lightsMask = trackCfg_.lightsMask();
    for (const SchedEvent& e : eventsBuf_) {
        if (!trackInMask(lightsMask, e.track)) continue;
        if (e.type == SchedEventType::NoteOn) {
            soundingLights_.add({e.note, e.track});
        } else if (e.type == SchedEventType::NoteOff) {
            soundingLights_.eraseFirst([&e](const SoundingLight& s) {
                return s.note == e.note && s.track == e.track;
            });
        }
    }

    if (sched_->finished()) {
        state_ = PlayState::Finished;
        stopAllSound(out);
    }
}

bool PlaybackEngine::frameDue(uint64_t nowUs) {
    if (frameDirty_ || nowUs - lastFrameUs_ >= kFramePeriodUs) {
        frameDirty_ = false;
        lastFrameUs_ = nowUs;
        return true;
    }
    return false;
}

const std::vector<Rgb>& PlaybackEngine::renderFrame(uint64_t nowUs) {
    renderer_.clear();
    if (sched_ && state_ == PlayState::Playing) {
        uint64_t pos = sched_->positionUs();
        uint32_t lightsMask = trackCfg_.lightsMask();

        // Ramp preview: notes coming up within the lead window.
        uint64_t lead = renderer_.ramp().leadUs;
        sched_->onsetsBetween(pos + 1, pos + lead, lightsMask, queryBuf_);
        for (const SchedEvent& e : queryBuf_)
            renderer_.addUpcoming(e.note, colorForTrack(e.track), e.timeUs,
                                  pos);

        // Sounding notes (follow/demo, and the un-practiced hand elsewhere).
        for (const SoundingLight& s : soundingLights_.items())
            renderer_.addDue(s.note, colorForTrack(s.track));

        // Wait mode: the due chord at 100%.
        if (barrierMode() && wait_->chordPending()) {
            sched_->notesOnAt(wait_->barrierTimeUs(),
                              trackCfg_.practicedMask(practice_), queryBuf_);
            for (const SchedEvent& e : queryBuf_) {
                if (wait_->isPending(e.note) &&
                    trackInMask(lightsMask, e.track))
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
            renderer_.addWrong(wrongFlashes_[w].note, wrongColor_);
            ++w;
        }
    }

    return renderer_.frame();
}

std::string PlaybackEngine::statusJson(const WifiStatus* wifi) const {
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
    if (wait_ && barrierMode())
        for (uint8_t n : wait_->pendingNotes()) pending.add(n);

    if (wifi) {
        JsonObject w = doc["wifi"].to<JsonObject>();
        w["mode"] = wifi->mode;
        w["ip"] = wifi->ip;
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace vialucis
