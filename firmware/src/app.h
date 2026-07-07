#pragma once
// Device shim: timers, BLE bytes, LEDs and flash on one side; the natively
// tested PlaybackEngine (lib/core) on the other. The REST layer and BLE
// callbacks talk only to this class. Wait-mode latency path (BLE in → match
// → LED out) stays inside tick() and onPianoNoteOn() — keep both lean (iron
// rule); the engine is a concrete member, so nothing here adds indirection.

#include <string>
#include <vector>

#include "ble_midi_io.h"
#include "led_output.h"
#include "song_store.h"
#include "vialucis/playback_engine.h"
#include "vialucis/settings.h"

namespace vialucis {

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

    std::string statusJson() const { return engine_.statusJson(); }
    Settings& settings() { return settings_; }
    void applySettings();  // after PUT /api/settings: re-derive configs + save

    SongStore& store() { return store_; }
    BleMidiIo& ble() { return ble_; }

private:
    void sendAll(const std::vector<MidiOutMsg>& msgs);

    Settings settings_;
    SongStore store_;
    LedOutput leds_;
    BleMidiIo ble_;
    PlaybackEngine engine_;

    TestPattern test_ = TestPattern::None;
    // Loop-task tick buffer, reused every iteration (REST calls use locals —
    // they run on the HTTP task).
    std::vector<MidiOutMsg> tickOut_;
};

}  // namespace vialucis
