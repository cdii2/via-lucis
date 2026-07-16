#pragma once
// Device shim: timers, BLE bytes, LEDs and flash on one side; the natively
// tested PlaybackEngine (lib/core) on the other. The REST layer and BLE
// callbacks talk only to this class. Wait-mode latency path (BLE in → match
// → LED out) stays inside tick() and onPianoNoteOn() — keep both lean (iron
// rule); the engine is a concrete member, so nothing here adds indirection.

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <string>
#include <vector>

#include "ble_midi_io.h"
#include "led_output.h"
#include "song_store.h"
#include "vialucis/calibration.h"
#include "vialucis/mode_director.h"
#include "vialucis/playback_engine.h"
#include "vialucis/record_take.h"  // PendingSave (B5 ask 3)
#include "vialucis/settings.h"
#include "vialucis/song_load.h"        // SongLoadOutcome (A160, §3-E item 2)
#include "vialucis/song_parse_cache.h"  // A170, §3-E item 12

namespace vialucis {

class App {
public:
    void begin();
    void tick(uint64_t nowUs);

    // BLE input (already on the device task; keep fast).
    void onPianoNoteOn(uint8_t note, uint8_t velocity, uint64_t nowUs);

    // REST surface — all return false on invalid requests.
    // A160 (§3-E item 2): NotFound vs ParseError so the REST layer can
    // answer 404 (no such file) vs 400 (file exists, parseMidi rejected it)
    // instead of collapsing both into one generic 400.
    SongLoadOutcome loadSong(const std::string& name);
    bool unloadSong();  // M1: back to the no-song state
    // The currently-loaded song name ("" if none), fenced. The DELETE guard
    // and the upload first-chunk 409 both ask this (ruling §6-3) instead of
    // serialize/reparsing the status doc.
    std::string loadedSongName();
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
    // never clobbers wizard calibration. Returns FALSE when the flash save
    // failed (a full/wedged FS) — the handler maps that to 507 (B4) instead of
    // echoing a lying 200.
    bool applySettings(bool calibScalarsChanged = false);

    // --- calibration (C3) -------------------------------------------------
    // calib_ is HTTP-task-owned like settings_ (the engine holds a table
    // copy from setTable), so the JSON getters stay unfenced; mutations
    // fence the engine swap and keep flash IO outside the critical section.
    std::string calibrationJson() const { return calib_.toJson(); }
    // PUT: parse+apply+save. A non-ok CalibResult is the parse failure (400).
    // On a successful parse+apply, *saveFailed is set true when the flash write
    // failed (507, B4) — the calibration DID apply live but did not persist.
    CalibResult applyCalibration(const char* json, bool* saveFailed = nullptr);

    // True when boot self-heal reset a corrupt persisted config to defaults
    // (B4). Surfaced to the user via /api/status "configReset" — the App-side
    // half is complete here; the status field awaits a DeviceStatus.configReset
    // member in playback_engine.h (B7-owned; see final report handoff).
    bool configWasReset() const { return configReset_; }
    ModeDirector::ProbeArm armProbe(uint16_t led, uint32_t timeoutMs);
    void cancelProbe();
    std::string probeJson();

    // --- top mode (M2; the REST/status surface lands at M3) -----------
    bool setPresentation(bool on);

    // --- AFK playlist (E3) ---------------------------------------------
    std::string afkJson();
    // PUT: apply+save. Returns false on a parse/validation failure (400, `err`
    // set). On success, *saveFailed is set true when the flash write failed
    // (507, B4). The config DID apply live but did not persist.
    bool applyAfk(const char* json, std::string* err, bool* saveFailed = nullptr);
    bool afkControl(const std::string& action);  // next | previous

    // --- storage recovery (A3) -------------------------------------------
    // Request a LittleFS format. The blocking format() runs on the loop task
    // (tick) so the async_tcp reply flushes first; this just raises the flag.
    void requestStorageFormat() { formatRequested_.store(true); }

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
        Ok, Playing, AlreadyArmed, LowSpace, LowMemory, PendingUnsaved
    };
    RecordArm recordArm(bool countIn, uint16_t bpm);
    // Finalize: extract the take under the fence, then hand-split + writeSmf +
    // LittleFS save UNFENCED (F-wave discipline). An empty take saves nothing
    // (Empty → 200 {"name":""}); otherwise the auto-named file lands in the
    // song list. nameOut carries the saved name ("" on Empty).
    // SavedTruncated (B5 ask 1): the take saved, but capture had to drop events
    // at a byte/duration limit — a 200 with a "truncated" flag, not an error.
    enum class RecordStop : uint8_t {
        Saved, SavedTruncated, Empty, NotArmed, SaveFailed
    };
    RecordStop recordStop(std::string* nameOut);
    // B5 ask 3: retry the LAST failed save (held in pendingSave_) without
    // re-recording the performance. Same typed result as recordStop();
    // NotArmed when nothing is pending. On success clears pendingSave_.
    RecordStop retryRecordSave(std::string* nameOut);
    bool recordDiscard();  // false when idle AND nothing pending (409)

    // Raw accessors — boundary invariant (F-wave review R5): these hand out
    // state that is safe UNFENCED only because the loop task never touches
    // store_/settings_ (the engine holds copies from configure) and
    // BleMidiIo::connected() is a single volatile bool. Any future change
    // breaking that must route through the fence instead.
    Settings& settings() { return settings_; }
    SongStore& store() { return store_; }
    BleMidiIo& ble() { return ble_; }

    // --- songs list w/ parse status (A170, §3-E item 12) -------------------
    struct SongListEntry { std::string name; size_t size; bool parseOk; };
    // GET /api/songs annotated with parseOk — cached per (name,size) via
    // parseCache_ so a ~2x/s poll never re-parses every file; only a
    // never-seen name or a changed size (re-upload/overwrite) triggers one
    // re-parse. Runs on the HTTP task, not the latency path.
    std::vector<SongListEntry> songsForList();

private:
    void sendAll(const std::vector<MidiOutMsg>& msgs);
    void touchWriteActivity();  // fenced callers only
    void stopShowLocked();      // shared show-teardown; caller holds lock_
    // Restore the transport + pre-show practice mode/hand the player had before
    // a show hijacked it (caller holds lock_). Shared by the manual stop path
    // (stopShowLocked) and the B1b auto-finish edge in tick() — a show reaching
    // Finished inside director_.tick() tears down its own presentation state, so
    // the App must run this same restore tail.
    void restorePreShowMode();
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

    // B5 ask 3: a take whose save failed is held here so a retry-save can retry
    // it without re-recording — "today the performance is destroyed" on
    // SaveFailed. recordArm refuses while one is held (A-number in report).
    PendingSave pendingSave_;

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

    // Loaded-song name mirror (ruling §6-3): set on a successful loadSong,
    // cleared on unload — the fenced source for loadedSongName(). Kept in
    // lockstep with the engine's own songName_ (both flow from loadSong).
    std::string loadedName_;

    // Storage-format request flag (A3): set from the HTTP task, consumed by
    // the loop task in tick() — LittleFS.format() must not block async_tcp.
    std::atomic<bool> formatRequested_{false};

    // B4 self-heal: set once at boot if a corrupt persisted config was reset to
    // defaults. Surfaced to the UI via /api/status "configReset" (pending the
    // DeviceStatus.configReset field — see report handoff).
    bool configReset_ = false;

    // A170 (§3-E item 12): per-boot, in-RAM parseOk cache for songsForList().
    SongParseCache parseCache_;
};

}  // namespace vialucis
