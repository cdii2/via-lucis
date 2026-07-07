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

// The two wifi facts /api/status reports (docs/API.md). The device layer
// fills this in; the engine authors the status document once — no
// serialize→parse→graft→reserialize splice (R4).
struct WifiStatus {
    std::string mode;  // "sta" | "ap"
    std::string ip;
};

class PlaybackEngine {
public:
    PlaybackEngine();
    // emitter_ points at our own guard_ — the engine must stay put.
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // Re-derive renderer/guard/colors after settings changed (brightness and
    // wifi stay device-side).
    void configure(const Settings& s, uint16_t ledCount);

    // Take ownership of a parsed song (the device layer does the file IO).
    // Note-offs for anything still sounding are appended to `out`.
    void loadSong(MidiSong&& song, const std::string& name,
                  std::vector<MidiOutMsg>& out);

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

    // Loop-task tick: advance playback, append MIDI-out to `out`.
    void tick(uint64_t nowUs, std::vector<MidiOutMsg>& out);

    // True when a frame should be pushed now. Consumes the dirty flag and
    // stamps the frame clock (same decision App::tick made inline).
    bool frameDue(uint64_t nowUs);
    const std::vector<Rgb>& renderFrame(uint64_t nowUs);

    // Status per docs/API.md. With `wifi`, the wifi object is appended as
    // the final key (GET /api/status); without it, the object is omitted
    // (every other route's status reply).
    std::string statusJson(const WifiStatus* wifi = nullptr) const;

    PlayState state() const { return state_; }
    uint64_t positionUs() const { return sched_ ? sched_->positionUs() : 0; }

private:
    // Modes where the wait-mode barrier arms (the player is being watched).
    bool barrierMode() const {
        return mode_ == Mode::Wait || mode_ == Mode::Accompaniment;
    }

    void rebuildAfterLoad();   // after song load
    void applyMasks();         // after mode/practice/track changes
    void stopAllSound(std::vector<MidiOutMsg>& out);
    Rgb colorForTrack(uint8_t track) const;

    EchoGuard guard_;
    MidiSong song_;
    std::string songName_;
    std::unique_ptr<Scheduler> sched_;
    std::unique_ptr<WaitMode> wait_;
    NoteEmitter emitter_{0};
    FrameRenderer renderer_{LedMapConfig{}, RampConfig{}};
    TrackConfig trackCfg_;
    Rgb leftColor_, rightColor_, wrongColor_;

    Mode mode_ = Mode::Wait;
    Hand practice_ = Hand::Both;
    PlayState state_ = PlayState::Idle;

    bool loopEnabled_ = false;
    uint32_t loopStartMs_ = 0, loopEndMs_ = 0;

    // Reused every tick/frame (R5): reserved once, steady-state zero alloc
    // on the latency path.
    std::vector<SchedEvent> eventsBuf_;  // tick: advance() output
    std::vector<SchedEvent> queryBuf_;   // renderFrame: onsets/chord queries

    uint64_t lastTickUs_ = 0;
    uint64_t lastFrameUs_ = 0;
    uint64_t prevPosUs_ = 0;
    // Set by the BLE task on key verdicts AND by REST sound-stop paths
    // (A27); consumed by the loop task in frameDue(). Plain volatile: a
    // racing writer can lose its mark to frameDue's clear, costing at most
    // one 16.7ms frame period of delay — acceptable, documented.
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
};

}  // namespace vialucis
