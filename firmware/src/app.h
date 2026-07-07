#pragma once
// Device-side orchestration: owns the core engine objects and the current
// playback state; the REST layer and BLE callbacks talk only to this class.
// Wait-mode latency path (BLE in → match → LED out) stays inside tick() and
// onPianoNoteOn() — keep both lean (iron rule).

#include <memory>
#include <string>
#include <vector>

#include "ble_midi_io.h"
#include "led_output.h"
#include "song_store.h"
#include "vialucis/echo_guard.h"
#include "vialucis/frame_renderer.h"
#include "vialucis/midi_parser.h"
#include "vialucis/note_emitter.h"
#include "vialucis/scheduler.h"
#include "vialucis/settings.h"
#include "vialucis/track_config.h"
#include "vialucis/wait_mode.h"

namespace vialucis {

enum class Mode : uint8_t { Wait, Follow, Demo, Accompaniment };
enum class PlayState : uint8_t { Idle, Playing, Finished };
enum class TestPattern : uint8_t { None, Strip, Rainbow };

class App {
public:
    void begin();
    void tick(uint64_t nowUs);

    // BLE input (already on the device task; keep fast).
    void onPianoNoteOn(uint8_t note, uint8_t velocity, uint64_t nowUs);

    // REST surface — all return false on invalid requests.
    bool loadSong(const std::string& name);
    bool transport(const std::string& action, uint32_t positionMs);
    bool setMode(const std::string& mode, const std::string& practice);
    bool setTempo(float percent);
    bool setLoop(bool enabled, uint32_t startMs, uint32_t endMs);
    bool setTrack(size_t index, const std::string& hand, bool lights);
    bool setTestPattern(const std::string& pattern);

    std::string statusJson() const;
    Settings& settings() { return settings_; }
    void applySettings();  // after PUT /api/settings: re-derive configs + save

    SongStore& store() { return store_; }
    BleMidiIo& ble() { return ble_; }

private:
    void rebuildEngine();      // after song load
    void applyMasks();         // after mode/practice/track changes
    void stopAllSound();
    void renderFrame(uint64_t nowUs);
    Rgb colorForTrack(uint8_t track) const;

    Settings settings_;
    SongStore store_;
    LedOutput leds_;
    BleMidiIo ble_;
    EchoGuard guard_;

    MidiSong song_;
    std::string songName_;
    std::unique_ptr<Scheduler> sched_;
    std::unique_ptr<WaitMode> wait_;
    NoteEmitter emitter_{0};
    FrameRenderer renderer_{LedMapConfig{}, RampConfig{}};
    TrackConfig trackCfg_;

    Mode mode_ = Mode::Wait;
    Hand practice_ = Hand::Both;
    PlayState state_ = PlayState::Idle;
    TestPattern test_ = TestPattern::None;

    bool loopEnabled_ = false;
    uint32_t loopStartMs_ = 0, loopEndMs_ = 0;

    uint64_t lastTickUs_ = 0;
    uint64_t lastFrameUs_ = 0;
    uint64_t prevPosUs_ = 0;
    volatile bool frameDirty_ = false;  // set from the BLE task on key verdicts

    struct WrongFlash {
        uint8_t note;
        uint64_t untilUs;
    };
    std::vector<WrongFlash> wrongFlashes_;
    struct SoundingLight {
        uint8_t note;
        uint8_t track;
    };
    std::vector<SoundingLight> soundingLights_;
};

}  // namespace vialucis
