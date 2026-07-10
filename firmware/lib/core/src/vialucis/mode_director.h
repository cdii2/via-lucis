#pragma once
// ModeDirector (M2, brief §1 + eng review 1A/3A): the top-mode state
// machine ABOVE the practice engine, and the SINGLE frame-source dispatch —
// exactly one producer paints the strip each tick.
//
//                     song loaded?
//          no                          yes
//   ┌────────────────┐         ┌────────────────────┐
//   │ REACTIVE ⇄ AFK │  load──▶│ PRACTICE ⇄ PRESENT │
//   └────────────────┘◀──unload└────────────────────┘
//   AFK NEVER fires while a song is loaded (hard requirement).
//   Forced sources above ALL modes: test pattern · calibration probe dot.
//
// Activity = ANY incoming MIDI message OR any state-CHANGING route; status/
// settings GETs never touch the idle clock (5A) — else the 2×/s-polling web
// remote makes AFK unreachable. Song-loaded truth is READ LIVE from the
// engine (no mirrored flag to forget). App stays a device shim.

#include <cstdint>
#include <string>
#include <vector>

#include "vialucis/calibration_probe.h"
#include "vialucis/fx/afk_player.h"
#include "vialucis/fx/note_driven.h"
#include "vialucis/playback_engine.h"
#include "vialucis/score_follower.h"
#include "vialucis/settings.h"
#include "vialucis/show_player.h"

namespace vialucis {

enum class TopMode : uint8_t { Reactive, Afk, Practice, Presentation };

class ModeDirector {
public:
    ModeDirector(PlaybackEngine& engine, uint16_t ledCount)
        : engine_(engine), ledCount_(ledCount), frame_(ledCount) {
        reactive_.reset(1, ledCount);
    }
    ModeDirector(const ModeDirector&) = delete;
    ModeDirector& operator=(const ModeDirector&) = delete;

    // Geometry for the note-driven layers and the AFK range mask (VL1:
    // everything reads the one per-key table). App calls this alongside
    // engine setTable.
    void setTable(const KeyLedTable& t) {
        table_ = t;
        reactive_.setTable(t);
        afk_.setTable(t);
    }

    // --- presentation playback (P2, P4) ----------------------------------
    // The clock is the Scheduler's song-time axis: Demo = the engine plays
    // the song (mode demo), Free-run = tempo-scaled follow, Score-follow =
    // the performer drives it through the ScoreFollower (P4). The caller
    // (App) sets the practice sub-mode + transport; this starts the show
    // frame source and flips Presentation on.
    // Owns the WHOLE start policy (closing review): the clock source picks
    // the practice sub-mode (Demo = the device plays; Free-run = tempo-
    // scaled follow; Score-follow = follow sub-mode with the transport
    // STOPPED — the follower is the only clock, so practice's verdict path
    // is inert by construction and a wrong note can never red-flash), a
    // leftover practice loop is cleared (a loop wrap would hard-reset every
    // effect mid-performance), and playback starts from the top. Note-offs
    // the transport emits land in `out`.
    void startShow(Show&& show, uint32_t seed,
                   std::vector<MidiOutMsg>& out) {
        uint8_t clock = show.meta.clockSource;
        uint8_t followTrack = show.meta.followTrack;
        showPlayer_.load(std::move(show), table_, seed);
        showPlaying_ = true;
        presentation_ = true;
        engine_.setMode(clock == 0 ? "demo" : "follow", "both", out);
        engine_.setLoop(false, 0, 0);
        engine_.transport("stop", 0, out);
        if (clock == 2) {
            // P4: extract the anchors of the follow scope (the barrier
            // cadence over the resolved track mask) and pre-roll at 0 —
            // the first matched anchor starts the clock (§4a Q13).
            std::vector<FollowAnchor> anchors;
            if (const Scheduler* sched = engine_.scheduler())
                ScoreFollower::extractAnchors(
                    *sched, engine_.followTrackMask(followTrack), anchors);
            follower_.arm(std::move(anchors));
            follower_.setEchoGuard(engine_.echoGuard());
        } else {
            engine_.transport("play", 0, out);
        }
        engine_.markFrameDirty();
    }
    void stopShow() {
        showPlaying_ = false;
        presentation_ = false;
        engine_.markFrameDirty();
    }
    bool showPlaying() const { return showPlaying_; }
    const ShowPlayer& showPlayer() const { return showPlayer_; }
    // P4: score-follow is DERIVED, never mirrored — it is exactly "a show
    // is playing and its clock source is 2", so every path that ends a
    // show (stopShow, setPresentation(false), unload cleanup in tick)
    // deactivates it for free.
    bool scoreFollowActive() const {
        return showPlaying_ && showPlayer_.clockSource() == 2;
    }
    const ScoreFollower& scoreFollower() const { return follower_; }

    // --- AFK playlist (E3) --------------------------------------------
    // Boot/tests: prepare+apply in one call (allocates — see applyAfk's
    // fence discipline for the REST path).
    void setAfkConfig(const fx::AfkConfig& c, uint32_t seed) {
        afk_.setConfig(c, seed);
        engine_.markFrameDirty();
    }
    // REST path: the caller runs fx::AfkPlayer::prepare() UNFENCED (heap
    // work) and hands the result here under the fence (pointer swaps only).
    void applyAfkPrepared(fx::AfkPlayer::Prepared&& p) {
        afk_.apply(std::move(p));
        engine_.markFrameDirty();
    }
    uint16_t ledCount() const { return ledCount_; }
    std::string afkConfigJson() const {
        return fx::afkConfigToJson(afk_.config());
    }
    void afkNext() {
        afk_.next();
        engine_.markFrameDirty();
    }
    void afkPrevious() {
        afk_.previous();
        engine_.markFrameDirty();
    }

    // --- activity (the idle clock's only writers) -----------------------
    void onMidiActivity(uint64_t nowUs) { lastActivityUs_ = nowUs; }
    void onWriteActivity(uint64_t nowUs) { lastActivityUs_ = nowUs; }

    // The BLE note paths: probe capture eats a press BEFORE practice sees
    // it (C3/3A); every event also feeds the Reactive layer (E2) and the
    // idle clock. Velocity rides through for the expressive mapping.
    void onKeyDown(uint8_t note, uint8_t velocity, uint64_t nowUs);
    void onKeyUp(uint8_t note, uint64_t nowUs);
    void onPedal(bool down, uint64_t nowUs);

    // Reactive tuning (E2) — velocity curve / release decay / pedal latch.
    void setReactiveParams(const fx::NoteDriven::Params& p) {
        reactive_.setParams(p);
    }

    // --- explicit mode entry --------------------------------------------
    // Presentation is entered deliberately; requires a loaded song.
    bool setPresentation(bool on);
    bool presentation() const { return presentation_; }

    void setIdleTimeoutSec(uint32_t s) { idleTimeoutSec_ = s; }
    uint32_t idleTimeoutSec() const { return idleTimeoutSec_; }

    TopMode topMode(uint64_t nowUs) const;
    static const char* topModeName(TopMode m);
    uint32_t idleSec(uint64_t nowUs) const;

    // --- forced sources ---------------------------------------------------
    // Activating a pattern while Playing auto-pauses (F3/A35 — the rule
    // lives HERE so every caller gets it); the note-offs the pause emits
    // land in `out`. "off" never auto-resumes.
    bool setTestPattern(const std::string& name, std::vector<MidiOutMsg>& out);
    bool testPatternActive() const { return test_ != Test::None; }

    enum class ProbeArm : uint8_t { Ok, Playing, BadLed };
    ProbeArm armProbe(uint16_t led, uint64_t nowUs, uint32_t timeoutMs);
    void cancelProbe();
    bool probeArmed() const { return probe_.armed(); }
    std::string probeJson() const;

    // --- the single dispatch ----------------------------------------------
    void tick(uint64_t nowUs, std::vector<MidiOutMsg>& out);
    bool frameDue(uint64_t nowUs) { return engine_.frameDue(nowUs); }
    const std::vector<Rgb>& renderFrame(uint64_t nowUs);

private:
    void paintTestStrip(uint32_t nowMs);
    void paintRainbow(uint32_t nowMs);  // also the M2 AFK stub (VL5)

    PlaybackEngine& engine_;
    CalibrationProbe probe_;
    uint16_t ledCount_;
    std::vector<Rgb> frame_;  // director-produced sources paint here
    fx::NoteDriven reactive_;      // E2: the live note-driven layer
    fx::AfkPlayer afk_{ledCount_};  // E3: replaces the M2 rainbow stub
    uint32_t fxFrame_ = 0;          // fixed-timestep counter for fx renders
    KeyLedTable table_;             // the geometry truth (VL1)
    ShowPlayer showPlayer_;         // P2: the Presentation frame source
    bool showPlaying_ = false;
    ScoreFollower follower_;        // P4: the score-follow clock source

    enum class Test : uint8_t { None, Strip, Rainbow };
    Test test_ = Test::None;

    bool presentation_ = false;
    uint32_t idleTimeoutSec_ = kDefaultAfkTimeoutSec;  // 0 = never
    uint64_t lastActivityUs_ = 0;    // 0 = baseline on first tick
    TopMode lastMode_ = TopMode::Reactive;
};

}  // namespace vialucis
