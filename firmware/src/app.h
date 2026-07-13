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
#include "vialucis/calibration.h"
#include "vialucis/mode_director.h"
#include "vialucis/playback_engine.h"
#include "vialucis/settings.h"

namespace vialucis {

class App {
public:
    void begin();
    void tick(uint64_t nowUs);

    // BLE input (already on the device task; keep fast).
    void onPianoNoteOn(uint8_t note, uint8_t velocity, uint64_t nowUs);

    // REST surface — all return false on invalid requests.
    bool loadSong(const std::string& name);
    bool unloadSong();  // M1: back to the no-song state
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
    // After PUT /api/settings: re-derive configs + save. When the caller saw
    // the 2-point scalars change, geometry deliberately reverts to the
    // 2-point tier (TROUBLESHOOTING documents this) — an untouched scalar
    // never clobbers wizard calibration.
    void applySettings(bool calibScalarsChanged = false);

    // --- calibration (C3) -------------------------------------------------
    // calib_ is HTTP-task-owned like settings_ (the engine holds a table
    // copy from setTable), so the JSON getters stay unfenced; mutations
    // fence the engine swap and keep flash IO outside the critical section.
    std::string calibrationJson() const { return calib_.toJson(); }
    CalibResult applyCalibration(const char* json);  // PUT: parse+apply+save
    ModeDirector::ProbeArm armProbe(uint16_t led, uint32_t timeoutMs);
    void cancelProbe();
    std::string probeJson();

    // --- top mode (M2; the REST/status surface lands at M3) -----------
    bool setPresentation(bool on);

    // --- AFK playlist (E3) ---------------------------------------------
    std::string afkJson();
    bool applyAfk(const char* json, std::string* err);  // PUT: apply+save
    bool afkControl(const std::string& action);  // next | previous

    // --- shows (P2) ------------------------------------------------------
    enum class ShowPlay : uint8_t { Ok, NotFound, BadStream, NoSong, Busy };
    ShowPlay playShow(const std::string& name, std::string* err);
    bool stopShow();
    bool showBusy();  // uploads are refused while anything renders live

    // --- recording (v3 REC4) ---------------------------------------------
    // Arm the tape head. Typed refusals map to REST 409/507. Free-space is
    // checked against the recordBudgetKB setting; count-in is Free-capture
    // only (ignored with a song loaded).
    enum class RecordArm : uint8_t {
        Ok, Playing, AlreadyArmed, LowSpace, LowMemory
    };
    RecordArm recordArm(bool countIn, uint16_t bpm);
    // Finalize: extract the take under the fence, then hand-split + writeSmf +
    // LittleFS save UNFENCED (F-wave discipline). An empty take saves nothing
    // (Empty → 200 {"name":""}); otherwise the auto-named file lands in the
    // song list. nameOut carries the saved name ("" on Empty).
    enum class RecordStop : uint8_t { Saved, Empty, NotArmed, SaveFailed };
    RecordStop recordStop(std::string* nameOut);
    bool recordDiscard();  // false when idle (409 not armed)

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
    void touchWriteActivity();  // fenced callers only
    void stopShowLocked();      // shared show-teardown; caller holds lock_
    // transport() body without the fence; caller must already hold lock_
    // (used by setTestPattern's auto-pause under its own guard).
    bool transportLocked(const std::string& action, uint32_t positionMs);

    Settings settings_;
    Calibration calib_;
    SongStore store_;
    LedOutput leds_;
    BleMidiIo ble_;
    PlaybackEngine engine_;
    // The single frame-source dispatch + top-mode machine (M2). Declared
    // after engine_ — it holds a reference to it.
    ModeDirector director_{engine_, LedOutput::kLedCount};

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

    // Loop-task tick buffer, reused every iteration (REST calls use locals —
    // they run on the HTTP task).
    std::vector<MidiOutMsg> tickOut_;

    // Practice mode/hand as last chosen via REST — a show hijacks the
    // sub-mode for its clock, so stopping one restores what the player had
    // (P-wave closing review; the engine doesn't expose these).
    std::string lastMode_ = "wait";
    std::string lastPractice_ = "both";
    std::string preShowMode_, preShowPractice_;
    bool preShowValid_ = false;
};

}  // namespace vialucis
