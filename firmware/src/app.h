#pragma once
// Device shim: timers, BLE bytes, LEDs and flash on one side; the natively
// tested PlaybackEngine (lib/core) on the other. The REST layer and BLE
// callbacks talk only to this class. Wait-mode latency path (BLE in → match
// → LED out) stays inside tick() and onPianoNoteOn() — keep both lean (iron
// rule); the engine is a concrete member, so nothing here adds indirection.

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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

    // Non-const: takes the cross-task fence (F1) before reading engine state
    // the loop task mutates. web_server holds App& non-const, so dropping
    // const is the clean option (vs a mutable handle).
    std::string statusJson(const WifiStatus* wifi = nullptr);
    void applySettings();  // after PUT /api/settings: re-derive configs + save

    // Raw accessors — boundary invariant (F-wave review R5): these hand out
    // state that is safe UNFENCED only because the loop task never touches
    // store_/settings_ (the engine holds copies from configure) and
    // BleMidiIo::connected() is a single volatile bool. Any future change
    // breaking that must route through the fence instead.
    Settings& settings() { return settings_; }
    SongStore& store() { return store_; }
    BleMidiIo& ble() { return ble_; }

private:
    void sendAll(const std::vector<MidiOutMsg>& msgs);
    // transport() body without the fence; caller must already hold lock_
    // (used by setTestPattern's auto-pause under its own guard).
    bool transportLocked(const std::string& action, uint32_t positionMs);

    Settings settings_;
    SongStore store_;
    LedOutput leds_;
    BleMidiIo ble_;
    PlaybackEngine engine_;

    // Cross-task fence (F1, A33). One plain (non-recursive) FreeRTOS mutex
    // serializes every HTTP-task entry point against the whole of tick().
    // Non-recursive is correct: no locked method calls another (the one
    // locked→locked call, setTestPattern→transportLocked, is lock-free by
    // contract). It is taken ONCE per tick — never per key event — so the
    // latency path gains zero work. Critical sections hold ONLY engine
    // mutations + MIDI sends — flash IO (song read/parse, settings save)
    // happens outside the fence — so a tick waits behind at most one
    // in-flight engine command (ms-scale, only when the web remote is
    // used). Loop-task and REST-path engine SENDS serialize through it too;
    // GET /api/ble's connected() read stays outside (lone volatile bool).
    SemaphoreHandle_t lock_ = nullptr;

    TestPattern test_ = TestPattern::None;
    // Loop-task tick buffer, reused every iteration (REST calls use locals —
    // they run on the HTTP task).
    std::vector<MidiOutMsg> tickOut_;
};

}  // namespace vialucis
