#include "app.h"

#include <Arduino.h>

namespace vialucis {

void App::begin() {
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
    std::vector<uint8_t> data;
    if (!store_.read(name, data)) return false;
    MidiParseResult r = parseMidi(data.data(), data.size());
    if (r.error != MidiParseError::Ok) return false;
    std::vector<MidiOutMsg> out;
    engine_.loadSong(std::move(r.song), name, out);
    sendAll(out);
    return true;
}

bool App::transport(const std::string& action, uint32_t positionMs) {
    std::vector<MidiOutMsg> out;
    bool ok = engine_.transport(action, positionMs, out);
    sendAll(out);
    return ok;
}

bool App::setMode(const std::string& mode, const std::string& practice) {
    std::vector<MidiOutMsg> out;
    bool ok = engine_.setMode(mode, practice, out);
    sendAll(out);
    return ok;
}

bool App::setTempo(float percent) { return engine_.setTempo(percent); }

bool App::setLoop(bool enabled, uint32_t startMs, uint32_t endMs) {
    return engine_.setLoop(enabled, startMs, endMs);
}

bool App::setTrack(size_t index, const std::string& hand, bool lights) {
    return engine_.setTrack(index, hand, lights);
}

bool App::setTestPattern(const std::string& pattern) {
    if (pattern == "strip") test_ = TestPattern::Strip;
    else if (pattern == "rainbow") test_ = TestPattern::Rainbow;
    else if (pattern == "off") {
        test_ = TestPattern::None;
        leds_.allOff();
    } else {
        return false;
    }
    return true;
}

void App::applySettings() {
    engine_.configure(settings_, LedOutput::kLedCount);
    leds_.setBrightness(settings_.brightness);
    store_.saveSettings(settings_);
}

void App::onPianoNoteOn(uint8_t note, uint8_t velocity, uint64_t nowUs) {
    (void)velocity;
    engine_.onKeyDown(note, nowUs);
}

void App::tick(uint64_t nowUs) {
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
