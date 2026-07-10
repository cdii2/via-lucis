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
#include "vialucis/fx/note_driven.h"
#include "vialucis/playback_engine.h"
#include "vialucis/settings.h"

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

    // Geometry for the note-driven layers (VL1: everything reads the one
    // per-key table). App calls this alongside engine setTable.
    void setTable(const KeyLedTable& t) { reactive_.setTable(t); }

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
    fx::NoteDriven reactive_;  // E2: the live note-driven layer
    uint32_t fxFrame_ = 0;     // fixed-timestep counter for fx renders

    enum class Test : uint8_t { None, Strip, Rainbow };
    Test test_ = Test::None;

    bool presentation_ = false;
    uint32_t idleTimeoutSec_ = kDefaultAfkTimeoutSec;  // 0 = never
    uint64_t lastActivityUs_ = 0;    // 0 = baseline on first tick
    TopMode lastMode_ = TopMode::Reactive;
};

}  // namespace vialucis
