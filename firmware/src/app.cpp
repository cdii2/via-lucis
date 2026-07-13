#include "app.h"

#include <Arduino.h>

#include "vialucis/record_take.h"  // splitTakeIntoHands
#include "vialucis/smf_writer.h"   // writeSmf

namespace vialucis {

namespace {
// RAII scope-lock over the App fence (F1). No naked take/give pairs; every
// entry point that can touch engine state holds one of these for its body.
struct FenceGuard {
    SemaphoreHandle_t h;
    explicit FenceGuard(SemaphoreHandle_t s) : h(s) {
        xSemaphoreTake(h, portMAX_DELAY);
    }
    ~FenceGuard() { xSemaphoreGive(h); }
    FenceGuard(const FenceGuard&) = delete;
    FenceGuard& operator=(const FenceGuard&) = delete;
};

// A little flash headroom kept free above the take's byte budget so a save
// never wedges the filesystem right at the limit (REC4 free-space check).
constexpr size_t kRecordSpaceMarginBytes = 8 * 1024;
// RAM headroom above the budget: arm() reserves the WHOLE budget as one
// contiguous heap block, and on -fno-exceptions a failed reserve aborts the
// device — so refuse (typed) when the largest free block can't hold it.
constexpr size_t kRecordHeapMarginBytes = 32 * 1024;
}  // namespace

void App::begin() {
    lock_ = xSemaphoreCreateMutex();  // the cross-task fence (F1, A33)
    // Heap exhaustion at boot must fail loud here, not as a mystery
    // xSemaphoreTake(NULL) abort later.
    configASSERT(lock_ != nullptr);
    tickOut_.reserve(64);  // reused every tick — steady-state zero alloc
    store_.begin();
    store_.loadSettings(settings_);  // keeps defaults if absent
    engine_.configure(settings_);
    // Calibration (C3): a stored /calibration.json wins; anything else —
    // absent file, unreadable, garbage — falls back to the settings'
    // 2-point values, which is byte-identical to v1 (the CRITICAL upgrade
    // path for every existing device).
    std::string calibJson;
    if (!store_.loadCalibration(calibJson) ||
        !Calibration::fromJson(calibJson.c_str(), LedOutput::kLedCount,
                               calib_).ok())
        calib_ = Calibration::fromSettings(settings_, LedOutput::kLedCount);
    engine_.setTable(calib_.table);
    director_.setTable(calib_.table);  // note-driven layers read it too
    director_.setIdleTimeoutSec(settings_.afkTimeoutSec);
    // AFK playlist (E3): stored config or defaults; the shuffle seed is
    // boot-time entropy (native tests seed explicitly instead).
    std::string afkJson;
    fx::AfkConfig afkCfg;
    if (store_.loadAfk(afkJson))
        fx::afkConfigFromJson(afkJson.c_str(), afkCfg, nullptr);
    director_.setAfkConfig(
        afkCfg, static_cast<uint32_t>(esp_timer_get_time()));
    leds_.begin(settings_.brightness);
    ble_.begin();
    ble_.onNoteOn([this](uint8_t note, uint8_t vel) {
        onPianoNoteOn(note, vel, static_cast<uint64_t>(esp_timer_get_time()));
    });
    // Note-offs and the sustain pedal feed the Reactive layer (E2); all of
    // these fire inside ble_.poll(), which tick() calls under the fence.
    ble_.onNoteOff([this](uint8_t note, uint8_t) {
        director_.onKeyUp(note, static_cast<uint64_t>(esp_timer_get_time()));
    });
    ble_.onPedal([this](uint8_t value) {
        director_.onPedal(value, static_cast<uint64_t>(esp_timer_get_time()));
    });
    // ANY midi message wakes/holds off AFK (M2).
    ble_.onActivity([this]() {
        director_.onMidiActivity(static_cast<uint64_t>(esp_timer_get_time()));
    });
}

// State-CHANGING REST routes reset the idle clock; GETs never do (5A).
// Callers hold the fence — the loop task reads the same u64.
void App::touchWriteActivity() {
    director_.onWriteActivity(static_cast<uint64_t>(esp_timer_get_time()));
}

void App::sendAll(const std::vector<MidiOutMsg>& msgs) {
    for (const MidiOutMsg& m : msgs) ble_.send(m);
}

bool App::loadSong(const std::string& name) {
    // Flash read + parse stay UNFENCED: they produce only locals, and store_
    // is HTTP-task-only. The fence covers just the engine mutation + sends,
    // so a concurrent tick never stalls behind file IO (F-wave review R1).
    std::vector<uint8_t> data;
    if (!store_.read(name, data)) return false;
    MidiParseResult r = parseMidi(data.data(), data.size());
    if (r.error != MidiParseError::Ok) return false;
    std::vector<MidiOutMsg> out;
    FenceGuard g(lock_);
    touchWriteActivity();
    engine_.loadSong(std::move(r.song), name, out);
    sendAll(out);
    return true;
}

bool App::unloadSong() {
    std::vector<MidiOutMsg> out;
    FenceGuard g(lock_);
    touchWriteActivity();  // the unload itself restarts the idle drift
    engine_.unloadSong(out);
    // (presentation_ dies in director_.tick — any unload path, one owner.)
    sendAll(out);
    return true;
}

bool App::setPresentation(bool on) {
    FenceGuard g(lock_);
    touchWriteActivity();
    if (!on && director_.showPlaying()) {
        stopShowLocked();  // leaving presentation ends the show properly
        return true;
    }
    return director_.setPresentation(on);
}

// Shared show teardown (caller holds the fence): stop rendering, halt the
// transport, and give the player back the practice mode/hand they had.
void App::stopShowLocked() {
    bool wasPlaying = director_.showPlaying();
    director_.stopShow();
    if (!wasPlaying) return;  // stray /api/shows/stop must not halt practice
    transportLocked("stop", 0);
    if (preShowValid_) {
        std::vector<MidiOutMsg> out;
        engine_.setMode(preShowMode_, preShowPractice_, out);
        sendAll(out);
        preShowValid_ = false;
    }
}

std::string App::afkJson() {
    FenceGuard g(lock_);
    return director_.afkConfigJson();
}

bool App::applyAfk(const char* json, std::string* err) {
    // Parse AND build the per-track effects UNFENCED (heap work — the
    // F-wave discipline: a tick never waits behind allocations); the
    // fenced part is pointer swaps only. Save after.
    fx::AfkConfig cfg;
    if (!fx::afkConfigFromJson(json, cfg, err)) return false;
    std::string doc = fx::afkConfigToJson(cfg);
    fx::AfkPlayer::Prepared prepared = fx::AfkPlayer::prepare(
        cfg, static_cast<uint32_t>(esp_timer_get_time()),
        director_.ledCount());
    {
        FenceGuard g(lock_);
        touchWriteActivity();
        director_.applyAfkPrepared(std::move(prepared));
    }
    store_.saveAfk(doc);
    return true;
}

App::ShowPlay App::playShow(const std::string& name, std::string* err) {
    // Flash read + parse UNFENCED (locals only, F-wave discipline).
    std::vector<uint8_t> data;
    if (!store_.readShow(name, data)) return ShowPlay::NotFound;
    Show show;
    ShowResult r = Show::parse(data.data(), data.size(), show);
    std::vector<uint8_t>().swap(data);  // drop the 64KB buffer pre-fence
    if (!r.ok()) {
        if (err) *err = r.message();
        return ShowPlay::BadStream;
    }
    FenceGuard g(lock_);
    touchWriteActivity();
    if (!engine_.songLoaded()) return ShowPlay::NoSong;
    if (engine_.state() == PlayState::Playing && !director_.showPlaying())
        return ShowPlay::Busy;  // don't hijack a live practice session
    preShowMode_ = lastMode_;   // restored when the show stops
    preShowPractice_ = lastPractice_;
    preShowValid_ = true;
    // The director owns the whole start policy (clock → sub-mode, loop
    // clear, from-the-top transport); we deliver its MIDI side effects.
    std::vector<MidiOutMsg> out;
    director_.startShow(std::move(show),
                        static_cast<uint32_t>(esp_timer_get_time()), out);
    sendAll(out);
    return ShowPlay::Ok;
}

bool App::stopShow() {
    FenceGuard g(lock_);
    touchWriteActivity();
    stopShowLocked();
    return true;
}

bool App::showBusy() {
    FenceGuard g(lock_);
    return engine_.state() == PlayState::Playing || director_.showPlaying();
}

bool App::afkControl(const std::string& action) {
    FenceGuard g(lock_);
    touchWriteActivity();
    if (action == "next") director_.afkNext();
    else if (action == "previous") director_.afkPrevious();
    else return false;
    return true;
}

// Shared engine-pause/transport body; caller must hold the fence.
bool App::transportLocked(const std::string& action, uint32_t positionMs) {
    std::vector<MidiOutMsg> out;
    bool ok = engine_.transport(action, positionMs, out);
    sendAll(out);
    return ok;
}

bool App::transport(const std::string& action, uint32_t positionMs) {
    FenceGuard g(lock_);
    touchWriteActivity();
    // (Play-cancels-probe lives in ModeDirector::tick — any path to
    // Playing, one owner.)
    return transportLocked(action, positionMs);
}

bool App::setMode(const std::string& mode, const std::string& practice) {
    FenceGuard g(lock_);
    touchWriteActivity();
    std::vector<MidiOutMsg> out;
    bool ok = engine_.setMode(mode, practice, out);
    sendAll(out);
    if (ok) {  // remember the player's own choice (show stop restores it)
        lastMode_ = mode;
        lastPractice_ = practice.empty() ? "both" : practice;
    }
    return ok;
}

bool App::setTempo(float percent) {
    FenceGuard g(lock_);
    touchWriteActivity();
    return engine_.setTempo(percent);
}

bool App::setLoop(bool enabled, uint32_t startMs, uint32_t endMs) {
    FenceGuard g(lock_);
    touchWriteActivity();
    return engine_.setLoop(enabled, startMs, endMs);
}

bool App::setTrack(size_t index, const std::string& hand, bool lights) {
    FenceGuard g(lock_);
    touchWriteActivity();
    return engine_.setTrack(index, hand, lights);
}

bool App::setTestPattern(const std::string& pattern) {
    FenceGuard g(lock_);
    touchWriteActivity();
    // The F3 auto-pause rule lives in the director now; we just deliver
    // the note-offs its pause emits.
    std::vector<MidiOutMsg> out;
    if (!director_.setTestPattern(pattern, out)) return false;
    sendAll(out);
    return true;
}

std::string App::statusJson(const WifiStatus* wifi) {
    FenceGuard g(lock_);
    uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
    TopStatus top{ModeDirector::topModeName(director_.topMode(now)),
                  director_.idleSec(now), director_.idleTimeoutSec()};
    const char* rState = "idle";
    switch (director_.recordState()) {
        case CaptureState::Armed: rState = "armed"; break;
        case CaptureState::Recording: rState = "recording"; break;
        case CaptureState::Idle: break;
    }
    RecordStatus rec{rState, director_.recordElapsedMs(now),
                     static_cast<uint32_t>(director_.recordUsedBytes()),
                     static_cast<uint32_t>(director_.recordBudgetBytes()),
                     director_.recordCountIn(), director_.recordBpm()};
    return engine_.statusJson(wifi, &top, &rec);
}

App::RecordArm App::recordArm(bool countIn, uint16_t bpm) {
    size_t budgetBytes =
        static_cast<size_t>(settings_.recordBudgetKB) * 1024;
    // Armed / playing checks under the fence (loop task mutates capture state).
    {
        FenceGuard g(lock_);
        if (director_.recordState() != CaptureState::Idle)
            return RecordArm::AlreadyArmed;
        if (director_.showPlaying()) return RecordArm::Playing;  // §arm refusal
    }
    // Free-space check UNFENCED (store_ is HTTP-task-owned, F-wave discipline):
    // a tick never waits behind LittleFS stat calls.
    if (store_.freeBytes() < budgetBytes + kRecordSpaceMarginBytes)
        return RecordArm::LowSpace;
    // Contiguous-RAM check: the capture buffer is one block of budgetBytes.
    // getMaxAllocHeap is the largest block malloc can hand out RIGHT NOW —
    // total free heap lies under fragmentation.
    if (ESP.getMaxAllocHeap() < budgetBytes + kRecordHeapMarginBytes)
        return RecordArm::LowMemory;
    FenceGuard g(lock_);
    touchWriteActivity();  // arming disarms AFK / restarts the idle drift
    ArmResult r = director_.armRecord(
        budgetBytes, countIn, bpm,
        static_cast<uint64_t>(esp_timer_get_time()));
    if (r == ArmResult::AlreadyArmed) return RecordArm::AlreadyArmed;  // race
    if (r == ArmResult::BadBudget) return RecordArm::LowSpace;
    return RecordArm::Ok;
}

App::RecordStop App::recordStop(std::string* nameOut) {
    CaptureTake take;
    {
        FenceGuard g(lock_);
        touchWriteActivity();
        if (director_.recordState() == CaptureState::Idle)
            return RecordStop::NotArmed;
        take = director_.stopRecord();  // FENCED: state mutation only
    }
    // Empty take: save nothing (DESIGN-record — a stop with no events discards).
    if (take.empty) {
        if (nameOut) nameOut->clear();
        return RecordStop::Empty;
    }
    // UNFENCED (F-wave discipline): hand-split + writeSmf + LittleFS save run
    // outside the fence — a concurrent tick never waits behind the heap work
    // or the flash write.
    SmfInput smf = splitTakeIntoHands(take);
    std::vector<uint8_t> bytes = writeSmf(smf);
    std::string name = store_.nextRecordingName();
    if (!store_.save(name, bytes.data(), bytes.size()))
        return RecordStop::SaveFailed;
    if (nameOut) *nameOut = name;
    return RecordStop::Saved;
}

bool App::recordDiscard() {
    FenceGuard g(lock_);
    touchWriteActivity();
    if (director_.recordState() == CaptureState::Idle) return false;
    director_.discardRecord();
    return true;
}

void App::applySettings(bool calibScalarsChanged) {
    // The settings scalars ARE the 2-point tier's inputs: on that tier the
    // table follows them (preserving the reversed flag). On a wizard tier,
    // an actual scalar EDIT reverts geometry to 2-point — the documented
    // dials-win rule — while unrelated settings PUTs leave the table alone
    // entirely (configure never touches geometry; closing review).
    bool rebuild = calib_.tier == "twoPoint" || calibScalarsChanged;
    if (rebuild)
        calib_ = Calibration::fromSettings(settings_, LedOutput::kLedCount,
                                           calib_.reversed);
    {
        FenceGuard g(lock_);
        touchWriteActivity();
        engine_.configure(settings_);
        if (rebuild) {
            engine_.setTable(calib_.table);
            director_.setTable(calib_.table);
        }
        director_.setIdleTimeoutSec(settings_.afkTimeoutSec);
        leds_.setBrightness(settings_.brightness);
    }
    // Flash write UNFENCED (F-wave review R1): settings_ is HTTP-task-owned —
    // the loop task never reads it (the engine holds copies from configure) —
    // so a concurrent tick never stalls behind LittleFS IO.
    store_.saveSettings(settings_);
    if (rebuild) store_.saveCalibration(calib_.toJson());
}

CalibResult App::applyCalibration(const char* json) {
    // Parse UNFENCED (locals only); fence just the engine table swap.
    Calibration next;
    CalibResult r =
        Calibration::fromJson(json, LedOutput::kLedCount, next);
    if (!r.ok()) return r;
    {
        FenceGuard g(lock_);
        touchWriteActivity();
        engine_.setTable(next.table);
        director_.setTable(next.table);
    }
    calib_ = std::move(next);
    if (calib_.tier == "twoPoint") {
        settings_.offsetMm = calib_.offsetMm;
        settings_.ledsPerMeter = calib_.ledsPerMeter;
        store_.saveSettings(settings_);
    }
    store_.saveCalibration(calib_.toJson());
    return r;
}

ModeDirector::ProbeArm App::armProbe(uint16_t led, uint32_t timeoutMs) {
    FenceGuard g(lock_);
    touchWriteActivity();
    return director_.armProbe(
        led, static_cast<uint64_t>(esp_timer_get_time()), timeoutMs);
}

void App::cancelProbe() {
    FenceGuard g(lock_);
    touchWriteActivity();
    director_.cancelProbe();
}

std::string App::probeJson() {
    // Fenced: the loop task mutates probe state (capture/expiry).
    FenceGuard g(lock_);
    return director_.probeJson();
}

void App::onPianoNoteOn(uint8_t note, uint8_t velocity, uint64_t nowUs) {
    // Runs on the loop task, dispatched from inside ble_.poll() (MIDI.read),
    // which tick() calls while already holding the fence — so this MUST NOT
    // take the lock (the mutex is non-recursive). Per key event the director
    // adds exactly one u64 store (activity) + one probe bool check — anything
    // heavier on this path needs latency scrutiny (iron rule).
    director_.onKeyDown(note, velocity, nowUs);  // probe → reactive → practice
}

void App::tick(uint64_t nowUs) {
    // Fence held once around the whole tick: ble_.poll() dispatches onKeyDown
    // → director/engine mutations, and renderFrame()'s returned reference is
    // consumed by leds_.show() inside the same critical section. The
    // ModeDirector is the single frame-source dispatch (M2) — forced
    // sources, top modes, and practice all route through it.
    FenceGuard g(lock_);
    ble_.poll();

    tickOut_.clear();
    director_.tick(nowUs, tickOut_);
    sendAll(tickOut_);

    if (director_.frameDue(nowUs)) leds_.show(director_.renderFrame(nowUs));
}

}  // namespace vialucis
