#pragma once
// The playback engine (R2): everything between "time passed / key pressed"
// and "this LED frame + these MIDI-out messages". Owns the scheduler, wait
// mode, track config, note emitter, frame renderer and echo guard, plus the
// sounding-light / wrong-flash bookkeeping the device shim used to carry.
// Pure C++ → natively tested. App stays the device shim: timers and BLE
// bytes in, composed frame and MIDI-out messages back out.
//
// Latency rule (iron): concrete class, called directly from App::tick and
// the BLE callback — no virtual dispatch, no allocation on the frame path.
// frameDirty_ semantics are exactly App's: key verdicts bypass the ~60fps
// limiter so the very next tick renders.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vialucis/echo_guard.h"
#include "vialucis/frame_renderer.h"
#include "vialucis/midi_io.h"
#include "vialucis/midi_parser.h"
#include "vialucis/note_emitter.h"
#include "vialucis/scheduler.h"
#include "vialucis/settings.h"
#include "vialucis/sounding_set.h"
#include "vialucis/track_config.h"
#include "vialucis/wait_mode.h"

namespace vialucis {

enum class Mode : uint8_t { Wait, Follow, Demo, Accompaniment };
enum class PlayState : uint8_t { Idle, Playing, Finished };

// Repeat cue (Q-wave, brief §2): the off-gap before a same-key re-press
// fills with this color, lerping startPct → peakPct across the gap. The
// pair is authoritative (VL4) — parameters of the RepeatFill layer, never
// a second global cap. Fractions 0..1 here; the JSON fields carry 0–100.
struct RepeatCueConfig {
    bool enabled = true;
    Rgb color{255, 255, 255};
    float startPct = 0.0f;
    float peakPct = 0.45f;   // 1.0 ⇒ pure hue-snap glide at onset
    uint32_t floorMs = 35;   // visibility floor, borrowed from the tail
    uint32_t waitPulseMs = 60;  // Q2: fixed wait-mode re-due pulse width

    bool operator==(const RepeatCueConfig& o) const {
        return enabled == o.enabled && color.r == o.color.r &&
               color.g == o.color.g && color.b == o.color.b &&
               startPct == o.startPct && peakPct == o.peakPct &&
               floorMs == o.floorMs && waitPulseMs == o.waitPulseMs;
    }
};

// The two wifi facts /api/status reports (docs/API.md). The device layer
// fills this in; the engine authors the status document once — no
// serialize→parse→graft→reserialize splice (R4).
struct WifiStatus {
    std::string mode;  // "sta" | "ap"
    std::string ip;
};

// Top-mode facts for /api/status (M3) — the ModeDirector owns them; the
// engine only serializes (it authors the status document once, R4).
struct TopStatus {
    const char* mode;       // reactive | afk | practice | presentation | record
    uint32_t idleSec;
    uint32_t afkTimeoutSec;
};

// Recording facts for /api/status (v3 REC4) — the ModeDirector owns the tape
// head; the engine only serializes them (into a "record" object placed BEFORE
// wifi, which stays the last key).
struct RecordStatus {
    const char* state;    // idle | armed | recording
    uint32_t elapsedMs;
    uint32_t usedBytes;
    uint32_t budgetBytes;
    bool countIn;
    uint16_t bpm;
};

class PlaybackEngine {
public:
    PlaybackEngine();
    // emitter_ points at our own guard_ — the engine must stay put.
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // Re-derive ramp/colors/guard/repeat-cue after settings changed
    // (brightness and wifi stay device-side). NEVER touches geometry —
    // setTable is the ONE table writer (C-wave closing review: a configure
    // that re-derived two-point geometry was itself the re-derivation site
    // VL1 forbids, and every caller had to remember to setTable after it).
    void configure(const Settings& s);

    // THE geometry writer (C3): swap the per-key table, keep ramp/colors.
    // The engine constructs with the default two-point table (v1 defaults).
    void setTable(const KeyLedTable& t);

    // Repeat-cue parameters (Q1; wired to Settings at Q3). Rebuilds the
    // precomputed windows — floorMs is baked into them at build time.
    void setRepeatCue(const RepeatCueConfig& c);
    const RepeatCueConfig& repeatCue() const { return repeatCue_; }

    // The probe moved to ModeDirector at M2 (3A) — the engine renders
    // practice, the director owns forced sources. External events (probe
    // arm/capture/expiry, mode flips) mark the shared frame clock dirty
    // through this — same semantics as an internal key verdict.
    void markFrameDirty() { frameDirty_ = true; }

    // Take ownership of a parsed song (the device layer does the file IO).
    // Note-offs for anything still sounding are appended to `out`.
    void loadSong(MidiSong&& song, const std::string& name,
                  std::vector<MidiOutMsg>& out);

    // Back to the natural boot state (M1). Reset surface, exact (OV4):
    // song/scheduler/wait/loop/lights/emitter — note-offs appended to
    // `out`. Settings, calibration table, and everything device-side
    // (BLE, idle clock) are untouched.
    void unloadSong(std::vector<MidiOutMsg>& out);

    bool songLoaded() const { return sched_ != nullptr; }

    // REST surface — all return false on invalid requests. Methods that can
    // silence the piano append the needed note-offs to `out`; the caller
    // sends them on its own task, exactly like the pre-R2 device code did.
    bool transport(const std::string& action, uint32_t positionMs,
                   std::vector<MidiOutMsg>& out);
    bool setMode(const std::string& mode, const std::string& practice,
                 std::vector<MidiOutMsg>& out);
    bool setTempo(float percent);
    bool setLoop(bool enabled, uint32_t startMs, uint32_t endMs);
    bool setTrack(size_t index, const std::string& hand, bool lights);

    // BLE-task entry — the latency path. Verdict handling (wrong flash,
    // frame dirty flag) happens here.
    void onKeyDown(uint8_t note, uint64_t nowUs);

    // --- score-follow hooks (P4) ----------------------------------------
    // Const view for anchor extraction at show start (the follower reads
    // onsets via the barrier-cadence queries; it never mutates).
    const Scheduler* scheduler() const { return sched_.get(); }
    // The one MIDI-echo truth (iron rule): the director's follower must
    // never treat our own emitted notes as the performer.
    EchoGuard* echoGuard() { return &guard_; }
    // Resolve a META followTrack byte to a scheduler track mask. 0xFF
    // (auto, A54) or an unusable index falls back: right-hand practiced
    // mask if it has onsets, else the lights mask, else all tracks.
    uint32_t followTrackMask(uint8_t followTrack) const;
    // The director drives the Presentation clock from the ScoreFollower's
    // estimate. Song time is set directly while the transport is stopped —
    // nothing sounds (follow-mode emit mask is 0), so the underlying seek
    // flushes nothing. Refused while Playing: the transport owns the clock.
    void driveShowClock(uint64_t songUs);

    // Loop-task tick: advance playback, append MIDI-out to `out`.
    void tick(uint64_t nowUs, std::vector<MidiOutMsg>& out);

    // True when a frame should be pushed now. Consumes the dirty flag and
    // stamps the frame clock (same decision App::tick made inline).
    bool frameDue(uint64_t nowUs);
    const std::vector<Rgb>& renderFrame(uint64_t nowUs);

    // Status per docs/API.md. With `wifi`, the wifi object is appended as
    // the final key (GET /api/status); without it, the object is omitted
    // (every other route's status reply). With `top`, the M3 top-mode
    // fields are included (before wifi — wifi stays the last key).
    std::string statusJson(const WifiStatus* wifi = nullptr,
                           const TopStatus* top = nullptr,
                           const RecordStatus* rec = nullptr) const;

    PlayState state() const { return state_; }
    uint64_t positionUs() const { return sched_ ? sched_->positionUs() : 0; }

private:
    // Modes where the wait-mode barrier arms (the player is being watched).
    bool barrierMode() const {
        return mode_ == Mode::Wait || mode_ == Mode::Accompaniment;
    }

    void rebuildAfterLoad();   // after song load
    // After mode/practice/track changes. Re-arms the wait barrier only when
    // the practiced set actually changed (or barrier mode was just re-
    // entered, or forceResync) — a no-op mode/track PUT must not wipe
    // partial-chord progress (A-2). rebuildAfterLoad forces it for a fresh
    // song.
    void applyMasks(bool forceResync = false);
    void buildRepeatGaps();    // Q1: one load pass, O(1) render lookups
    void resyncRepeatCursors(uint64_t posUs);  // after seek / loop wrap
    void resetWaitPulse();     // Q2: forget chord history + live pulses
    void stopAllSound(std::vector<MidiOutMsg>& out);
    Rgb colorForTrack(uint8_t track) const;

    EchoGuard guard_;
    MidiSong song_;
    std::string songName_;
    std::unique_ptr<Scheduler> sched_;
    std::unique_ptr<WaitMode> wait_;
    NoteEmitter emitter_{0};
    FrameRenderer renderer_{TableBuilder::fromTwoPoint(LedMapConfig{}),
                            RampConfig{}};
    TrackConfig trackCfg_;
    Rgb leftColor_, rightColor_, wrongColor_;

    Mode mode_ = Mode::Wait;
    Hand practice_ = Hand::Both;
    PlayState state_ = PlayState::Idle;

    // Last practiced mask / barrier-mode applied (A-2): applyMasks compares
    // against these to decide whether a resync is a real change or a wipe of
    // in-flight chord progress. rebuildAfterLoad seeds them for the new song.
    uint32_t lastPracticedMask_ = 0;
    bool wasBarrierMode_ = false;

    // Loop status is derived from the Scheduler in statusJson (F-wave
    // review, A34): one source of truth, no mirror fields to forget to
    // reset — a fresh Scheduler has no loop by construction.

    // Reused every tick/frame (R5): reserved once, steady-state zero alloc
    // on the latency path.
    std::vector<SchedEvent> eventsBuf_;  // tick: advance() output
    std::vector<SchedEvent> queryBuf_;   // renderFrame: onsets/chord queries

    uint64_t lastTickUs_ = 0;
    uint64_t lastFrameUs_ = 0;
    uint64_t prevPosUs_ = 0;
    // Set by key verdicts AND REST sound-stop paths (A27); consumed by the
    // loop task in frameDue(). D4 (2026-07-14 fix wave): the BLE note path
    // is NOT a concurrent writer — its callback fires on the loop task
    // itself, dispatched from inside ble_.poll() while tick() already holds
    // the F1 fence, so it is serialized with frameDue() by construction,
    // same as every other write here. The real (still acceptable) race is
    // an HTTP-task REST write landing, under its own FenceGuard, between one
    // tick's frameDue() check and the next — the render it asks for lands on
    // the FOLLOWING frame instead of the current one. Worst case one 16.7ms
    // frame period of delay, never a lost mark (the flag stays true until an
    // actual render clears it). Plain volatile documents that this flag
    // crosses the FreeRTOS task boundary; the fence, not the qualifier, is
    // what makes the read-modify-clear itself safe.
    volatile bool frameDirty_ = false;

    struct WrongFlash {
        uint8_t note;
        uint64_t untilUs;
    };
    std::vector<WrongFlash> wrongFlashes_;
    struct SoundingLight {
        uint8_t note;
        uint8_t track;
    };
    SoundingSet<SoundingLight> soundingLights_;

    // Q1 repeat cue. Gap lookups are PRECOMPUTED at load (7A): per key, the
    // time-ordered re-press windows [fillStartUs, onsetUs) with the incoming
    // note's track. Render advances a per-key cursor forward lazily (O(1)
    // amortized); seeks/wraps re-derive cursors by binary search — never on
    // the frame path. fillStart = onset - max(gap, floor): borrowing falls
    // out of the formula. Sub-collapse gaps get no entry at all.
    struct RepeatWindow {
        uint64_t fillStartUs;
        uint64_t onsetUs;
        uint8_t track;  // incoming note's track (lights-mask check at render)
    };
    RepeatCueConfig repeatCue_;
    std::array<std::vector<RepeatWindow>, 88> repeatByKey_;
    std::array<size_t, 88> repeatCursor_{};

    // Q2: wait-mode re-due pulse deadlines. WaitMode reports which keys
    // re-due (it owns the chord lifecycle); the engine only times the fixed
    // wall-clock pulse (the song is halted — no timing to protect).
    std::array<uint64_t, 88> waitPulseUntilUs_{};
    // Set by buildRepeatGaps: total windows across all keys — renderFrame
    // skips the whole per-key scan for songs with no qualifying repeats.
    size_t repeatWindowCount_ = 0;
};

}  // namespace vialucis
