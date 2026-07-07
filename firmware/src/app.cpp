#include "app.h"

#include <Arduino.h>

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
}  // namespace

void App::begin() {
    lock_ = xSemaphoreCreateMutex();  // the cross-task fence (F1, A33)
    // Heap exhaustion at boot must fail loud here, not as a mystery
    // xSemaphoreTake(NULL) abort later.
    configASSERT(lock_ != nullptr);
    tickOut_.reserve(64);  // reused every tick — steady-state zero alloc
    store_.begin();
    store_.loadSettings(settings_);  // keeps defaults if absent
    engine_.configure(settings_, LedOutput::kLedCount);
    leds_.begin(settings_.brightness);
    ble_.begin();
    ble_.onNoteOn([this](uint8_t note, uint8_t vel) {
        onPianoNoteOn(note, vel, static_cast<uint64_t>(esp_timer_get_time()));
    });
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
    engine_.loadSong(std::move(r.song), name, out);
    sendAll(out);
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
    return transportLocked(action, positionMs);
}

bool App::setMode(const std::string& mode, const std::string& practice) {
    FenceGuard g(lock_);
    std::vector<MidiOutMsg> out;
    bool ok = engine_.setMode(mode, practice, out);
    sendAll(out);
    return ok;
}

bool App::setTempo(float percent) {
    FenceGuard g(lock_);
    return engine_.setTempo(percent);
}

bool App::setLoop(bool enabled, uint32_t startMs, uint32_t endMs) {
    FenceGuard g(lock_);
    return engine_.setLoop(enabled, startMs, endMs);
}

bool App::setTrack(size_t index, const std::string& hand, bool lights) {
    FenceGuard g(lock_);
    return engine_.setTrack(index, hand, lights);
}

bool App::setTestPattern(const std::string& pattern) {
    FenceGuard g(lock_);
    if (pattern == "strip") test_ = TestPattern::Strip;
    else if (pattern == "rainbow") test_ = TestPattern::Rainbow;
    else if (pattern == "off") {
        test_ = TestPattern::None;
        leds_.allOff();
        return true;  // "off" never auto-resumes — the user presses play,
                      // and that path re-baselines the clock (A35).
    } else {
        return false;
    }
    // Activating a pattern auto-pauses playback ONLY when actually Playing
    // (F3, A35): the pattern branch in tick() early-returns, so an unpaused
    // scheduler clock would otherwise fast-forward the skipped time in one
    // burst on pattern-off. Guarding on Playing avoids flipping Finished->Idle
    // via transport("pause"). Pause also sends note-offs for any demo/
    // accompaniment notes left ringing. This pause is one atomic unit with
    // the pattern activation (same held fence).
    if (engine_.state() == PlayState::Playing) transportLocked("pause", 0);
    return true;
}

std::string App::statusJson(const WifiStatus* wifi) {
    FenceGuard g(lock_);
    return engine_.statusJson(wifi);
}

void App::applySettings() {
    {
        FenceGuard g(lock_);
        engine_.configure(settings_, LedOutput::kLedCount);
        leds_.setBrightness(settings_.brightness);
    }
    // Flash write UNFENCED (F-wave review R1): settings_ is HTTP-task-owned —
    // the loop task never reads it (the engine holds copies from configure) —
    // so a concurrent tick never stalls behind LittleFS IO.
    store_.saveSettings(settings_);
}

void App::onPianoNoteOn(uint8_t note, uint8_t velocity, uint64_t nowUs) {
    // Runs on the loop task, dispatched from inside ble_.poll() (MIDI.read),
    // which tick() calls while already holding the fence — so this MUST NOT
    // take the lock (the mutex is non-recursive). Zero new work per key event.
    (void)velocity;
    engine_.onKeyDown(note, nowUs);
}

void App::tick(uint64_t nowUs) {
    // Fence held once around the whole tick: ble_.poll() dispatches onKeyDown
    // → engine mutations, and renderFrame()'s returned reference is consumed
    // by leds_.show() inside the same critical section (closes the renderer_
    // swap race). The test-pattern branch reads test_ under the lock too.
    FenceGuard g(lock_);
    ble_.poll();

    if (test_ != TestPattern::None) {
        uint32_t ms = static_cast<uint32_t>(nowUs / 1000);
        if (test_ == TestPattern::Strip) leds_.testPattern(ms);
        else leds_.rainbow(ms);
        return;
    }

    tickOut_.clear();
    engine_.tick(nowUs, tickOut_);
    sendAll(tickOut_);

    if (engine_.frameDue(nowUs)) leds_.show(engine_.renderFrame(nowUs));
}

}  // namespace vialucis
