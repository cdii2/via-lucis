#include "vialucis/playback_engine.h"

#include <ArduinoJson.h>

#include "vialucis/version.h"

namespace vialucis {

namespace {
constexpr uint64_t kWrongFlashUs = 300000;   // red flash duration
constexpr uint64_t kFramePeriodUs = 16667;   // ~60 fps LED refresh
// Off-gaps below this collapse to one visual event (brief §2's 3ms
// pathology): no human re-press has a sub-10ms silent gap — that's a MIDI
// artifact, and cueing it would flicker.
constexpr uint64_t kRepeatCollapseUs = 10000;

vialucis::Rgb scalePct(vialucis::Rgb c, float k) {
    return {static_cast<uint8_t>(c.r * k + 0.5f),
            static_cast<uint8_t>(c.g * k + 0.5f),
            static_cast<uint8_t>(c.b * k + 0.5f)};
}
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
    renderer_ = FrameRenderer(TableBuilder::fromTwoPoint(s.ledMapConfig(ledCount)),
                              s.rampConfig());
    leftColor_ = s.leftColor;
    rightColor_ = s.rightColor;
    wrongColor_ = s.wrongColor;
}

void PlaybackEngine::setTable(const KeyLedTable& t) {
    renderer_ = FrameRenderer(t, renderer_.ramp());
    frameDirty_ = true;
}

void PlaybackEngine::setRepeatCue(const RepeatCueConfig& c) {
    repeatCue_ = c;
    buildRepeatGaps();  // floorMs is baked into the windows
    resyncRepeatCursors(sched_ ? sched_->positionUs() : 0);
    frameDirty_ = true;
}

PlaybackEngine::ProbeArm PlaybackEngine::armProbe(uint16_t led,
                                                  uint64_t nowUs,
                                                  uint32_t timeoutMs) {
    if (state_ == PlayState::Playing) return ProbeArm::Playing;
    if (led >= renderer_.frame().size()) return ProbeArm::BadLed;
    if (timeoutMs < 1000) timeoutMs = 1000;
    if (timeoutMs > 300000) timeoutMs = 300000;
    probe_.arm(led, nowUs, static_cast<uint64_t>(timeoutMs) * 1000);
    frameDirty_ = true;
    return ProbeArm::Ok;
}

void PlaybackEngine::cancelProbe() {
    if (probe_.armed()) frameDirty_ = true;
    probe_.cancel();
}

std::string PlaybackEngine::probeJson() const {
    std::string out = "{\"armed\":";
    out += probe_.armed() ? "true" : "false";
    out += ",\"led\":";
    out += std::to_string(probe_.led());
    out += ",\"note\":";
    out += probe_.hasCapture() ? std::to_string(probe_.capturedNote())
                               : std::string("null");
    out += "}";
    return out;
}

Rgb PlaybackEngine::colorForTrack(uint8_t track) const {
    Hand h = track < trackCfg_.tracks.size() ? trackCfg_.tracks[track].hand
                                             : Hand::Both;
    if (h == Hand::Left) return leftColor_;
    return rightColor_;  // Right and Both use the right-hand color
}

void PlaybackEngine::buildRepeatGaps() {
    for (auto& v : repeatByKey_) v.clear();
    repeatCursor_.fill(0);
    if (song_.notes.empty()) return;
    // The parser keeps notes sorted by onTick; walk once per key tracking
    // the latest off so far. A re-press while the key still sounds (cross-
    // track overlap) never went dark — no cue. The cue is per KEY, not per
    // track (cross-hand re-presses cue too).
    std::array<uint64_t, 88> lastOff;  // kNoOnset = key never sounded yet
    lastOff.fill(kNoOnset);
    const uint64_t floorUs =
        static_cast<uint64_t>(repeatCue_.floorMs) * 1000;
    for (const MidiNote& n : song_.notes) {
        if (n.note < 21 || n.note > 108) continue;
        size_t k = n.note - 21;
        uint64_t onUs = tickToMicros(song_, n.onTick);
        uint64_t offUs = tickToMicros(song_, n.offTick);
        if (lastOff[k] != kNoOnset && lastOff[k] <= onUs) {
            uint64_t gap = onUs - lastOff[k];
            if (gap >= kRepeatCollapseUs) {
                uint64_t span = gap > floorUs ? gap : floorUs;
                repeatByKey_[k].push_back(
                    {onUs - span, onUs, n.track});
            }
        }
        if (lastOff[k] == kNoOnset || offUs > lastOff[k])
            lastOff[k] = offUs;
    }
}

void PlaybackEngine::resyncRepeatCursors(uint64_t posUs) {
    for (size_t k = 0; k < 88; ++k) {
        const auto& v = repeatByKey_[k];
        size_t lo = 0, hi = v.size();
        while (lo < hi) {  // first window whose onset is still ahead
            size_t mid = (lo + hi) / 2;
            if (v[mid].onsetUs <= posUs) lo = mid + 1;
            else hi = mid;
        }
        repeatCursor_[k] = lo;
    }
}

void PlaybackEngine::rebuildAfterLoad() {
    sched_ = std::make_unique<Scheduler>(song_);
    trackCfg_ = TrackConfig::defaultsFor(song_);
    buildRepeatGaps();
    wait_ = std::make_unique<WaitMode>(*sched_, kTrackMaskAll);
    wait_->setEchoGuard(&guard_);
    soundingLights_.clear();
    wrongFlashes_.clear();
    state_ = PlayState::Idle;
    prevPosUs_ = 0;
    // Loop honesty (F2, A34) falls out by construction: statusJson derives
    // the loop from the Scheduler, and a fresh Scheduler has no loop.
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
        // Play while a probe is armed: the user's intent wins — the wizard
        // sees armed:false, note:null and offers a retry (A38).
        cancelProbe();
        if (state_ == PlayState::Finished) {
            sched_->seek(0);  // flushed note-offs are moot: nothing sounding
            prevPosUs_ = 0;
            resyncRepeatCursors(0);
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
        resyncRepeatCursors(0);
        if (barrierMode()) wait_->resync();
        return true;
    }
    if (action == "seek") {
        stopAllSound(out);
        sched_->seek(static_cast<uint64_t>(positionMs) * 1000);
        prevPosUs_ = sched_->positionUs();
        resyncRepeatCursors(prevPosUs_);
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
        return true;
    }
    if (endMs <= startMs) return false;
    sched_->setLoop(static_cast<uint64_t>(startMs) * 1000,
                    static_cast<uint64_t>(endMs) * 1000);
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
    // Probe capture eats the press BEFORE practice sees it (C3 ownership
    // rule). One bool check when idle — nothing new on the latency path.
    if (probe_.onNoteOn(note)) {
        frameDirty_ = true;
        return;
    }
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
    // Probe auto-timeout runs regardless of play state (it only arms when
    // idle); the dirty mark clears the dot on the next frame.
    if (probe_.tickExpire(nowUs)) frameDirty_ = true;
    if (!sched_ || state_ != PlayState::Playing) return;

    if (lastTickUs_ == 0) lastTickUs_ = nowUs;
    uint64_t delta = nowUs - lastTickUs_;
    lastTickUs_ = nowUs;

    sched_->advance(delta, eventsBuf_);
    uint64_t newPos = sched_->positionUs();
    if (newPos < prevPosUs_) {  // loop wrapped
        if (barrierMode()) wait_->resync();
        soundingLights_.clear();
        resyncRepeatCursors(newPos);  // no phantom fill from the old pass
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
    // Forced source: an armed probe owns the whole strip — one white dot,
    // nothing else (wrong flashes included).
    if (probe_.armed()) {
        renderer_.addDot(probe_.led(), Rgb{255, 255, 255});
        return renderer_.frame();
    }
    if (sched_ && state_ == PlayState::Playing) {
        uint64_t pos = sched_->positionUs();
        uint32_t lightsMask = trackCfg_.lightsMask();

        // Ramp preview: notes coming up within the lead window.
        uint64_t lead = renderer_.ramp().leadUs;
        sched_->onsetsBetween(pos + 1, pos + lead, lightsMask, queryBuf_);
        for (const SchedEvent& e : queryBuf_)
            renderer_.addUpcoming(e.note, colorForTrack(e.track), e.timeUs,
                                  pos);

        // Repeat cue (Q1): active per-key fill windows. Cursor advance is
        // lazy and monotone — O(1) amortized, no event scans (7A/R5).
        // Wait mode has no timing to protect: it gets Q2's fixed pulse,
        // not the crescendo (brief §2 heading: all non-wait modes).
        std::array<bool, 88> fillActive{};
        if (repeatCue_.enabled && mode_ != Mode::Wait) {
            for (size_t k = 0; k < 88; ++k) {
                const std::vector<RepeatWindow>& v = repeatByKey_[k];
                size_t& cur = repeatCursor_[k];
                while (cur < v.size() && v[cur].onsetUs <= pos) ++cur;
                if (cur >= v.size()) continue;
                const RepeatWindow& w = v[cur];
                if (pos < w.fillStartUs) continue;
                if (!trackInMask(lightsMask, w.track)) continue;
                float frac = static_cast<float>(pos - w.fillStartUs) /
                             static_cast<float>(w.onsetUs - w.fillStartUs);
                float pct = repeatCue_.startPct +
                            (repeatCue_.peakPct - repeatCue_.startPct) * frac;
                renderer_.addRepeatFill(static_cast<uint8_t>(21 + k),
                                        scalePct(repeatCue_.color, pct));
                fillActive[k] = true;
            }
        }

        // Sounding notes (follow/demo, and the un-practiced hand elsewhere).
        // A key whose fill window has begun is in its borrowed tail: the
        // outgoing note's Due paint yields so the cue shows — the onset
        // itself is never delayed (iron rule).
        for (const SoundingLight& s : soundingLights_.items()) {
            if (s.note >= 21 && s.note <= 108 && fillActive[s.note - 21])
                continue;
            renderer_.addDue(s.note, colorForTrack(s.track));
        }

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
    // Derived from the Scheduler — the one source of loop truth (A34).
    // clearLoop keeps the last range, so setLoop(false) reports the old
    // startMs/endMs with enabled:false, exactly like the former mirrors.
    JsonObject loop = doc["loop"].to<JsonObject>();
    loop["enabled"] = sched_ ? sched_->loopEnabled() : false;
    loop["startMs"] =
        sched_ ? static_cast<uint32_t>(sched_->loopStartUs() / 1000) : 0;
    loop["endMs"] =
        sched_ ? static_cast<uint32_t>(sched_->loopEndUs() / 1000) : 0;

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
