#include "vialucis/playback_engine.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "vialucis/version.h"

namespace vialucis {

namespace {
constexpr uint64_t kWrongFlashUs = 300000;   // red flash duration
constexpr uint64_t kFramePeriodUs = 16667;   // ~60 fps LED refresh
// Off-gaps below this collapse to one visual event (brief §2's 3ms
// pathology): no human re-press has a sub-10ms silent gap — that's a MIDI
// artifact, and cueing it would flicker.
constexpr uint64_t kRepeatCollapseUs = 10000;
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

void PlaybackEngine::configure(const Settings& s) {
    guard_.setWindowUs(static_cast<uint64_t>(s.echoWindowMs) * 1000);
    renderer_ = FrameRenderer(renderer_.table(), s.rampConfig());
    leftColor_ = s.leftColor;
    rightColor_ = s.rightColor;
    wrongColor_ = s.wrongColor;
    RepeatCueConfig rc;  // Q3: wire settings (0–100 wire → 0..1 fractions)
    rc.enabled = s.repeatCueEnabled;
    rc.color = s.repeatColor;
    rc.startPct = s.repeatFillStartPct / 100.0f;
    rc.peakPct = s.repeatFillPeakPct / 100.0f;
    rc.floorMs = s.repeatFloorMs;
    rc.waitPulseMs = s.repeatWaitPulseMs;
    // Skip when unchanged: an unrelated settings PUT (brightness, colors)
    // must not rebuild windows under the fence or kill a live pulse
    // (Q-wave closing review).
    if (!(rc == repeatCue_)) setRepeatCue(rc);
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

Rgb PlaybackEngine::colorForTrack(uint8_t track) const {
    Hand h = track < trackCfg_.tracks.size() ? trackCfg_.tracks[track].hand
                                             : Hand::Both;
    if (h == Hand::Left) return leftColor_;
    return rightColor_;  // Right and Both use the right-hand color
}

void PlaybackEngine::buildRepeatGaps() {
    for (auto& v : repeatByKey_) v.clear();
    repeatCursor_.fill(0);
    repeatWindowCount_ = 0;
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
                // A floor larger than the onset's own timestamp must not
                // underflow (settings allow floors up to 1s; songs can
                // re-press within their first second).
                uint64_t fillStart = onUs > span ? onUs - span : 0;
                repeatByKey_[k].push_back({fillStart, onUs, n.track});
                ++repeatWindowCount_;
            }
        }
        if (lastOff[k] == kNoOnset || offUs > lastOff[k])
            lastOff[k] = offUs;
    }
}

void PlaybackEngine::resetWaitPulse() {
    waitPulseUntilUs_.fill(0);
}

void PlaybackEngine::resyncRepeatCursors(uint64_t posUs) {
    resetWaitPulse();
    for (size_t k = 0; k < 88; ++k) {
        const auto& v = repeatByKey_[k];
        // First window whose onset is still ahead of the new position.
        auto it = std::upper_bound(
            v.begin(), v.end(), posUs,
            [](uint64_t pos, const RepeatWindow& w) {
                return pos < w.onsetUs;
            });
        repeatCursor_[k] = static_cast<size_t>(it - v.begin());
    }
}

void PlaybackEngine::rebuildAfterLoad() {
    sched_ = std::make_unique<Scheduler>(song_);
    trackCfg_ = TrackConfig::defaultsFor(song_);
    buildRepeatGaps();  // applyMasks below resets the pulse deadlines
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
        resetWaitPulse();  // mode/mask changes invalidate chord history
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

void PlaybackEngine::unloadSong(std::vector<MidiOutMsg>& out) {
    stopAllSound(out);  // note-offs for anything sounding + dark next frame
    song_ = MidiSong{};
    songName_.clear();
    sched_.reset();     // loop/barrier die with it — statusJson derives
    wait_.reset();
    trackCfg_ = TrackConfig{};
    emitter_.setEmitMask(0);
    state_ = PlayState::Idle;
    prevPosUs_ = 0;
    lastTickUs_ = 0;
    guard_.clearCredits();  // no echo is owed once nothing was sent
    buildRepeatGaps();      // empty song ⇒ empty windows
    resetWaitPulse();
}

bool PlaybackEngine::transport(const std::string& action, uint32_t positionMs,
                               std::vector<MidiOutMsg>& out) {
    if (!sched_) return false;
    if (action == "play") {
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
        resyncRepeatCursors(newPos);  // no phantom fill from the old pass
    }
    prevPosUs_ = newPos;

    if (barrierMode()) {
        // Q2: WaitMode owns the chord lifecycle and reports the edge (new
        // chord loaded) plus its same-key re-dues — no barrier-time mirror
        // to forget to reset (Q-wave closing review).
        if (wait_->update() && repeatCue_.enabled) {
            for (uint8_t n : wait_->reDueKeys()) {
                if (n < 21 || n > 108) continue;
                waitPulseUntilUs_[n - 21] =
                    nowUs +
                    static_cast<uint64_t>(repeatCue_.waitPulseMs) * 1000;
                frameDirty_ = true;
            }
        }
    }

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

        // Repeat cue (Q1): active per-key fill windows. Cursor advance is
        // lazy and monotone — O(1) amortized, no event scans (7A/R5).
        // Wait mode has no timing to protect: it gets Q2's fixed pulse,
        // not the crescendo (brief §2 heading: all non-wait modes).
        std::array<bool, 88> fillActive{};
        if (repeatCue_.enabled && mode_ != Mode::Wait &&
            repeatWindowCount_ > 0) {  // most songs: skip the whole scan
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
                                        scaleRgb(repeatCue_.color, pct));
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

        // Wait mode: the due chord at 100% — except keys inside their Q2
        // re-due pulse window, which flash repeatColor first ("this key
        // AGAIN") and then settle into the ordinary due light.
        if (barrierMode() && wait_->chordPending()) {
            sched_->notesOnAt(wait_->barrierTimeUs(),
                              trackCfg_.practicedMask(practice_), queryBuf_);
            for (const SchedEvent& e : queryBuf_) {
                if (!wait_->isPending(e.note) ||
                    !trackInMask(lightsMask, e.track))
                    continue;
                if (repeatCue_.enabled && e.note >= 21 && e.note <= 108 &&
                    nowUs < waitPulseUntilUs_[e.note - 21])
                    renderer_.addRepeatFill(e.note, repeatCue_.color);
                else
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

std::string PlaybackEngine::statusJson(const WifiStatus* wifi,
                                       const TopStatus* top) const {
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

    if (top) {  // M3 growth — appended before wifi (wifi stays last)
        doc["topMode"] = top->mode;
        doc["idleSec"] = top->idleSec;
        doc["afkTimeoutSec"] = top->afkTimeoutSec;
    }

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
