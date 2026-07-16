#include "vialucis/playback_engine.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "vialucis/version.h"

namespace vialucis {

namespace {
constexpr uint64_t kWrongFlashUs = 300000;   // red flash duration
constexpr uint64_t kFramePeriodUs = 16667;   // ~60 fps LED refresh
// B7 (BUGFIX-PLAN §3-B): sensible pre-reserve for the wrong-flash hot-path
// vector — generous headroom over how many keys can plausibly be mid-flash
// at once (bounded by fingers, same reasoning as wait_mode's
// kMaxChordNotes). Not a hard cap.
constexpr size_t kMaxSimultaneousWrongFlashes = 16;
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
    wrongFlashes_.reserve(kMaxSimultaneousWrongFlashes);  // B7
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
    // Loop honesty (F2, A34) falls out by construction: statusJson derives
    // the loop from the Scheduler, and a fresh Scheduler has no loop.
    applyMasks(/*forceResync=*/true);  // a fresh song always (re)arms
}

uint32_t PlaybackEngine::currentEmitMask() const {
    if (mode_ == Mode::Demo) return trackCfg_.audibleMask();
    if (mode_ == Mode::Accompaniment)
        return trackCfg_.accompanimentMask(practice_);
    return 0;  // wait/follow: the piano is the player's
}

void PlaybackEngine::applyMasks(bool forceResync) {
    if (!sched_) return;
    emitter_.setEmitMask(currentEmitMask());

    if (wait_) {
        uint32_t pm = trackCfg_.practicedMask(practice_);
        // A resync reloads the whole chord, wiping pending_/cleared_. Only do
        // it when something that affects the barrier actually changed: the
        // practiced set, or a (re)entry into barrier mode — or on a forced
        // (fresh-song) rebuild. An identical mode/track PUT is a no-op here,
        // so half-cleared chord progress survives it (A-2 / G8).
        bool changed = forceResync || pm != lastPracticedMask_ ||
                       barrierMode() != wasBarrierMode_;
        wait_->setPracticedMask(pm);
        lastPracticedMask_ = pm;
        wasBarrierMode_ = barrierMode();
        if (barrierMode()) {
            if (changed) {
                resetWaitPulse();  // real change invalidates chord history
                wait_->resync();
            }
        } else {
            sched_->clearBarrier();
        }
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
            resyncRepeatCursors(0);
            // The seek moved the position, so re-arm the barrier. A plain
            // resume (not Finished) must NOT resync — that would wipe a
            // half-cleared chord on a web-remote double-tap (G7) or across a
            // pause/resume (G9). A-2.
            if (barrierMode()) wait_->resync();
            // B3b: replaying from Finished re-approaches the opening barrier;
            // in-flight echo credits from the just-ended pass are stale and
            // must not eat the player's first real press (parity with seek's
            // A-3/G12 clearCredits).
            guard_.clearCredits();
        }
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
        resyncRepeatCursors(0);
        if (barrierMode()) wait_->resync();
        return true;
    }
    if (action == "seek") {
        stopAllSound(out);
        sched_->seek(static_cast<uint64_t>(positionMs) * 1000);
        resyncRepeatCursors(sched_->positionUs());
        if (barrierMode()) wait_->resync();
        if (state_ == PlayState::Finished) state_ = PlayState::Idle;
        // Echo-credit hygiene (A-3 / G12): a seek moves off wherever our
        // demo/accompaniment emissions were made, so their in-flight echo
        // credits are stale — they must not eat the first genuine press at
        // the re-approached barrier.
        guard_.clearCredits();
        return true;
    }
    return false;
}

bool PlaybackEngine::setMode(const std::string& mode,
                             const std::string& practice,
                             std::vector<MidiOutMsg>& out) {
    Mode oldMode = mode_;
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
    // Echo-credit hygiene (A-3 / G11): leaving an emitting mode (demo /
    // accompaniment) ends its emissions; any in-flight echo credit is now
    // stale and must not eat the first genuine press in the new mode.
    if (oldMode != mode_ &&
        (oldMode == Mode::Demo || oldMode == Mode::Accompaniment))
        guard_.clearCredits();
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
                              bool lights, std::vector<MidiOutMsg>& out) {
    if (index >= trackCfg_.tracks.size()) return false;
    Hand h;
    if (hand == "left") h = Hand::Left;
    else if (hand == "right") h = Hand::Right;
    else if (hand == "both") h = Hand::Both;
    else if (hand == "off") h = Hand::Off;
    else return false;
    trackCfg_.tracks[index].hand = h;
    trackCfg_.tracks[index].lights = lights;
    // B3a: with the new assignment in place, flush note-offs for any track the
    // emitter is still sounding that just dropped out of the emit mask (demo /
    // accompaniment) — otherwise its note-off is masked out of consume() and
    // the piano rings forever. setMode already stopAllSound()s before this, so
    // only a hand change mid-note reaches here with notes still sounding.
    emitter_.flushTracksOutsideMask(currentEmitMask(), out);
    applyMasks();
    return true;
}

uint32_t PlaybackEngine::followTrackMask(uint8_t followTrack) const {
    if (!sched_) return kTrackMaskAll;
    // Explicit editor-chosen track (the META byte), when it is usable.
    if (followTrack != 0xFF &&
        followTrack < trackCfg_.tracks.size()) {
        uint32_t m = trackBit(followTrack);
        if (m != 0 && sched_->nextOnsetAfter(0, m) != kNoOnset) return m;
    }
    // Auto (0xFF, A54): the melody by default — the right-hand practiced
    // mask (§4a Q6), falling back to whatever actually has onsets.
    uint32_t rh = trackCfg_.practicedMask(Hand::Right);
    if (rh != 0 && sched_->nextOnsetAfter(0, rh) != kNoOnset) return rh;
    uint32_t lights = trackCfg_.lightsMask();
    if (lights != 0 && sched_->nextOnsetAfter(0, lights) != kNoOnset)
        return lights;
    return kTrackMaskAll;
}

void PlaybackEngine::driveShowClock(uint64_t songUs) {
    if (!sched_ || state_ == PlayState::Playing) return;
    // Direct song-time write. Nothing is sounding (score-follow keeps the
    // transport stopped and the follow-mode emit mask is 0), so the seek's
    // note-off flush into the reused buffer stays empty.
    sched_->seek(songUs, queryBuf_);
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

    bool wrapped = false;
    sched_->advance(delta, eventsBuf_, &wrapped);
    if (wrapped) {  // B3c: the scheduler's authoritative loop-wrap signal —
                    // catches short-loop/high-tempo wraps a position compare
                    // (newPos < prevPos) misses after the O(1) modulo collapse.
        if (barrierMode()) wait_->resync();
        soundingLights_.clear();
        resyncRepeatCursors(sched_->positionUs());  // no phantom fill from the
                                                     // old pass
    }

    if (barrierMode()) {
        // Q2: WaitMode owns the chord lifecycle and reports the edge (new
        // chord loaded) plus its same-key re-dues — no barrier-time mirror
        // to forget to reset (Q-wave closing review).
        if (wait_->update()) {
            // B7: a note that just became a member of the newly-loaded due
            // chord must never still paint red from a stale wrong-flash left
            // by a PREVIOUS chord — WaitMode::onKeyDown only ever reports
            // Wrong for a note that is NOT pending at press-time, so a flash
            // can only go stale across exactly this transition (never within
            // one chord's lifetime). Drop it now instead of leaving it to
            // time out up to kWrongFlashUs later (today: solid red on a key
            // the player must press).
            for (uint8_t n : wait_->pendingNotes()) {
                size_t w = 0;
                while (w < wrongFlashes_.size()) {
                    if (wrongFlashes_[w].note == n)
                        wrongFlashes_.erase(wrongFlashes_.begin() + w);
                    else
                        ++w;
                }
            }
            // A chord transition is always a visual change (new due chord
            // lit, any stale wrong-flash just dropped) — render it on the
            // very next tick rather than waiting up to one 60fps period.
            frameDirty_ = true;
            if (repeatCue_.enabled) {
                for (uint8_t n : wait_->reDueKeys()) {
                    if (n < 21 || n > 108) continue;
                    waitPulseUntilUs_[n - 21] =
                        nowUs +
                        static_cast<uint64_t>(repeatCue_.waitPulseMs) * 1000;
                }
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

        // Ramp preview: notes coming up within the lead window. While
        // looping, don't preview onsets at/beyond loopEnd — the loop wraps
        // before reaching them, so their swell would be a phantom (A-5/G5).
        uint64_t lead = renderer_.ramp().leadUs;
        uint64_t previewTo = pos + lead;
        if (sched_->loopEnabled() && sched_->loopEndUs() > pos &&
            previewTo >= sched_->loopEndUs())
            previewTo = sched_->loopEndUs() - 1;
        sched_->onsetsBetween(pos + 1, previewTo, lightsMask, queryBuf_);
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
                // A window whose onset is at/beyond loopEnd is never reached
                // while looping — skip its phantom crescendo (A-5/G5).
                if (sched_->loopEnabled() && w.onsetUs >= sched_->loopEndUs())
                    continue;
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
            // A98/G18: query the SAME epsilon window the gate absorbed so every
            // note WaitMode is holding is covered; isPending() (the one source
            // of "still owed") filters to the genuinely-pending set.
            sched_->notesInWindow(wait_->barrierTimeUs(),
                                  wait_->barrierTimeUs() + kChordEpsilonUs,
                                  trackCfg_.practicedMask(practice_), queryBuf_);
            for (const SchedEvent& e : queryBuf_) {
                // A99/G19 practiced-implies-lit: a genuinely-owed barrier note
                // ALWAYS renders, even if its track's `lights` flag is off —
                // you can never owe an invisible note. lightsMask still governs
                // every OTHER visual path (glow, demo, ramp). No config mutated.
                if (!wait_->isPending(e.note))
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
                                       const TopStatus* top,
                                       const RecordStatus* rec,
                                       const DeviceStatus* dev) const {
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
        // C1: the player's last-chosen practice hand, so the webui selector can
        // reconcile after reload instead of resetting the device to "both".
        if (top->practice) doc["practice"] = top->practice;
    }

    if (rec) {  // v3 REC4 growth — the record object, also BEFORE wifi
        JsonObject r = doc["record"].to<JsonObject>();
        r["state"] = rec->state;
        r["elapsedMs"] = rec->elapsedMs;
        r["usedBytes"] = rec->usedBytes;
        r["budgetBytes"] = rec->budgetBytes;
        r["countIn"] = rec->countIn;
        r["bpm"] = rec->bpm;
    }

    if (dev) {  // A3 telemetry — also BEFORE wifi (wifi stays last)
        doc["fs"] = dev->fs;
        doc["fsFree"] = dev->fsFree;
        doc["fsTotal"] = dev->fsTotal;
        doc["fsUsed"] = dev->fsUsed;
        doc["heapFree"] = dev->heapFree;
        doc["heapMaxAlloc"] = dev->heapMaxAlloc;
        doc["uptimeMs"] = dev->uptimeMs;
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
