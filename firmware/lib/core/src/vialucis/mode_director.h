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
#include "vialucis/midi_capture.h"
#include "vialucis/playback_engine.h"
#include "vialucis/score_follower.h"
#include "vialucis/settings.h"
#include "vialucis/show_player.h"

namespace vialucis {

// Recording duration cap (REC3, docs/DESIGN-record.md §8): a compile-time
// constant (~10 min), NOT a setting — the byte budget is the tunable bound.
constexpr uint32_t kRecordMaxMs = 600000;

// A113 (2026-07-16, B1a, BUGFIX-PLAN-2026-07-15 §3-B1): an orphaned test
// pattern (a client that POSTs test-strip/test-rainbow then vanishes) must
// not hide the practice/show frame source forever. It auto-clears after
// this many ms of continuous inactivity (no re-set, no "off") — on top of
// the explicit reclaim points (a fresh song load, transport entering
// Playing, a show starting) that fire immediately, not on a timer.
constexpr uint32_t kTestPatternTimeoutMs = 300000;  // 5 min

// Record joins the top modes: entered only via arm with no song loaded (Free
// capture); arming with a song loaded stays Practice (Play-along, capture runs
// alongside and paints nothing). Record outranks Afk — arming IS write
// activity, so AFK can never fire while a take is armed/recording.
enum class TopMode : uint8_t { Reactive, Afk, Practice, Presentation, Record };

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
        heartbeatLed_ = computeHeartbeatLed(t);  // reserved off-key pixel (REC3)
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
        // B1a: a show starting reclaims the strip from an orphaned test
        // pattern immediately — don't wait for the next tick's edge check
        // or the timeout.
        if (test_ != Test::None) {
            test_ = Test::None;
            testPatternSetUs_ = 0;
        }
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
    // STEERS, never WAKES (ruling §6-4 / B2 — see the activity section
    // below for the full two-category split). Advancing the ambient
    // playlist is itself an ambient action: it must never reach the idle
    // clock, or the act of steering an AFK show would dismiss the very
    // show it's steering. Neither of these touches lastActivityUs_; the
    // App seam must not call onWriteActivity() ahead of them
    // (test_b2_afk_next_previous_never_wake_afk pins this contract).
    void afkNext() {
        afk_.next();
        engine_.markFrameDirty();
    }
    void afkPrevious() {
        afk_.previous();
        engine_.markFrameDirty();
    }

    // --- activity (the idle clock's only writers) -----------------------
    // Two named categories, not one universal "any write wakes" (ruling
    // §6-4 — API.md's old wording was wrong):
    //   WAKES  — state mutations: anything that changes what the device IS
    //            doing (load/unload, transport, mode/tempo/loop/track,
    //            settings, calibration, record arm/stop/discard,
    //            presentation/shows, test pattern, AFK CONFIG writes, probe
    //            arm/cancel). The App seam calls onWriteActivity() for
    //            every one of these — see touchWriteActivity() call-sites.
    //   STEERS — ambient transport: afkNext()/afkPrevious() above control
    //            an AFK show that is ALREADY playing. Media-control, not a
    //            state mutation — it must never touch the idle clock.
    // onMidiActivity is always WAKES (a real key press is always real
    // activity); onWriteActivity is the WAKES half of the write split —
    // afkNext/afkPrevious deliberately have no such call.
    void onMidiActivity(uint64_t nowUs) { lastActivityUs_ = nowUs; }  // WAKES
    void onWriteActivity(uint64_t nowUs) { lastActivityUs_ = nowUs; }  // WAKES

    // The BLE note paths: probe capture eats a press BEFORE practice sees
    // it (C3/3A); every event also feeds the Reactive layer (E2) and the
    // idle clock. Velocity rides through for the expressive mapping.
    void onKeyDown(uint8_t note, uint8_t velocity, uint64_t nowUs);
    void onKeyUp(uint8_t note, uint64_t nowUs);
    // Raw CC64 value 0-127 (REC3): reactive latches on value>=64, capture
    // stores the raw value. BLE reduces to bool no longer — the pedal now
    // carries its real value so a take records the pedal faithfully.
    void onPedal(uint8_t value, uint64_t nowUs);

    // --- recording (REC3, capture subsystem) ----------------------------
    // The tape head lives here (core, native-testable). arm/stop/discard
    // delegate to it; App owns save + free-space (REC4). Count-in is Free-
    // capture only (ignored when a song is loaded); bpm clamps 20-300.
    ArmResult armRecord(size_t budgetBytes, bool countIn, uint16_t bpm,
                        uint64_t nowUs);
    CaptureTake stopRecord(uint64_t nowUs);
    void discardRecord();
    CaptureState recordState() const { return capture_.state(); }
    CaptureStatus recordStatus() const { return capture_.status(); }
    uint32_t recordElapsedMs(uint64_t nowUs) const {
        return capture_.elapsedMs(nowUs);
    }
    size_t recordUsedBytes() const { return capture_.usedBytes(); }
    size_t recordBudgetBytes() const { return capture_.budgetBytes(); }
    bool recordCountIn() const { return countIn_; }
    uint16_t recordBpm() const { return bpm_; }
    uint16_t recordHeartbeatLed() const { return heartbeatLed_; }

    // Reactive tuning (E2) — velocity curve / release decay / pedal latch.
    void setReactiveParams(const fx::NoteDriven::Params& p) {
        reactive_.setParams(p);
    }

    // --- explicit mode entry --------------------------------------------
    // Presentation is entered deliberately; requires a loaded song.
    bool setPresentation(bool on);
    bool presentation() const { return presentation_; }

    // B-3/A96 (G14, D3): the practice sub-mode (wait/follow/demo/
    // accompaniment) a playing show OWNS as its own clock — switching it
    // mid-performance would arm a barrier (or otherwise disturb the
    // schedule) and freeze the show on stage. Refused wholesale while
    // showPlaying_; the caller (App) never reaches the engine, so it also
    // never clobbers its own last-known-mode bookkeeping on a refused call.
    bool setMode(const std::string& mode, const std::string& practice,
                std::vector<MidiOutMsg>& out);

    void setIdleTimeoutSec(uint32_t s) { idleTimeoutSec_ = s; }
    uint32_t idleTimeoutSec() const { return idleTimeoutSec_; }

    TopMode topMode(uint64_t nowUs) const;
    static const char* topModeName(TopMode m);
    uint32_t idleSec(uint64_t nowUs) const;

    // --- forced sources ---------------------------------------------------
    // Activating a pattern while Playing auto-pauses (F3/A35 — the rule
    // lives HERE so every caller gets it); the note-offs the pause emits
    // land in `out`. "off" never auto-resumes.
    // `nowUs` (B1a) (re)starts the orphan-timeout clock — every activation
    // (including switching strip↔rainbow) refreshes it, so the timeout
    // means "no one has touched this pattern in kTestPatternTimeoutMs",
    // not "since it was first set".
    bool setTestPattern(const std::string& name, uint64_t nowUs,
                        std::vector<MidiOutMsg>& out);
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
    void paintRecordFrame(uint64_t nowUs);  // REC3: monitor + heartbeat + count-in
    // The reserved heartbeat pixel: one LED outside every key's range so the
    // recording indicator never collides with the Reactive monitor.
    uint16_t computeHeartbeatLed(const KeyLedTable& t) const;

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
    // B1a: orphan-timeout clock (activation/refresh time) + the two edge
    // detectors tick() uses to reclaim the strip the instant something more
    // purposeful than the timeout wants it back (a fresh song load, or
    // transport entering Playing). 0 / false are safe baselines — both
    // guards are additionally gated on test_ != Test::None, so an unset
    // timestamp or a stale edge flag is never read while no pattern is up.
    uint64_t testPatternSetUs_ = 0;
    bool wasSongLoadedForPattern_ = false;
    bool wasEnginePlayingForPattern_ = false;

    bool presentation_ = false;
    uint32_t idleTimeoutSec_ = kDefaultAfkTimeoutSec;  // 0 = never
    uint64_t lastActivityUs_ = 0;    // 0 = baseline on first tick
    TopMode lastMode_ = TopMode::Reactive;

    // Recording (REC3). The capture tape head + the Free-capture count-in
    // state (armed timestamp, whether a count-in was requested, its BPM).
    MidiCapture capture_;
    bool countIn_ = false;
    uint16_t bpm_ = 90;
    uint64_t armUs_ = 0;
    uint16_t heartbeatLed_ = ledCount_ ? static_cast<uint16_t>(ledCount_ - 1) : 0;
};

}  // namespace vialucis
