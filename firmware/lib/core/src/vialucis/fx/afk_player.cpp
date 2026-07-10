#include "vialucis/fx/afk_player.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "vialucis/fx/demo_reel.h"
#include "vialucis/fx/fx_color.h"

namespace vialucis {
namespace fx {

AfkPlayer::AfkPlayer(uint16_t ledCount)
    : ledCount_(ledCount), bufA_(ledCount), bufB_(ledCount) {
    fallback_ = std::make_unique<RainbowFx>();
    fallback_->reset(1, ledCount);
}

void AfkPlayer::setTable(const KeyLedTable& t) {
    table_ = t;
    haveSpan_ = false;
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n) {
        LedRange r = t.forNote(n);
        if (!r.valid) continue;
        uint16_t lo = std::min(r.first, r.last);
        uint16_t hi = std::max(r.first, r.last);
        if (!haveSpan_) {
            spanFirst_ = lo;
            spanLast_ = hi;
            haveSpan_ = true;
        } else {
            spanFirst_ = std::min(spanFirst_, lo);
            spanLast_ = std::max(spanLast_, hi);
        }
    }
}

void AfkPlayer::setConfig(const AfkConfig& c, uint32_t seed) {
    cfg_ = c;
    if (cfg_.dwellSec < 5) cfg_.dwellSec = 5;  // dwell=0 is a config bug
    if (cfg_.masterSpeed < 0.25f) cfg_.masterSpeed = 0.25f;
    if (cfg_.masterSpeed > 4.0f) cfg_.masterSpeed = 4.0f;
    seed_ = seed;
    rng_.reseed(seed);
    effects_.clear();
    for (const AfkTrack& t : cfg_.tracks) {
        std::unique_ptr<Effect> e = makeEffect(t.effect);
        if (e) {
            e->reset(seed ^ static_cast<uint32_t>(effects_.size() + 1),
                     ledCount_);
            if (const Palette16* p = paletteByName(t.palette))
                e->setPalette(*p);
        }
        effects_.push_back(std::move(e));  // null slots render fallback
    }
    current_ = 0;
    frameInTrack_ = 0;
    fading_ = false;
}

Effect* AfkPlayer::effectFor(size_t track) {
    if (track < effects_.size() && effects_[track]) return effects_[track].get();
    return fallback_.get();
}

size_t AfkPlayer::pickNext() {
    size_t n = cfg_.tracks.size();
    if (cfg_.repeatCurrent || n <= 1) return current_;
    if (!cfg_.shuffle) return (current_ + 1) % n;
    // Shuffle: any track but the current one (a 1-track list repeats).
    size_t pick = rng_.random16(static_cast<uint16_t>(n - 1));
    return pick >= current_ ? pick + 1 : pick;
}

void AfkPlayer::startTrack(size_t index) {
    if (!cfg_.tracks.empty()) current_ = index % cfg_.tracks.size();
    frameInTrack_ = 0;
    fading_ = false;
}

void AfkPlayer::next() {
    size_t n = cfg_.tracks.size();
    if (n == 0) return startTrack(0);
    if (cfg_.shuffle && n > 1) {  // manual next overrides repeat-current
        size_t pick = rng_.random16(static_cast<uint16_t>(n - 1));
        startTrack(pick >= current_ ? pick + 1 : pick);
    } else {
        startTrack((current_ + 1) % n);
    }
}
void AfkPlayer::previous() {
    if (cfg_.tracks.empty()) return startTrack(0);
    size_t n = cfg_.tracks.size();
    startTrack((current_ + n - 1) % n);
}

void AfkPlayer::render(std::vector<Rgb>& out) {
    const uint32_t ms =
        static_cast<uint32_t>(fxFrame_ * kFxStepMs * cfg_.masterSpeed);
    FxFrame fa{bufA_, fxFrame_, ms};
    effectFor(current_)->render(fa);

    const uint32_t dwellFrames = cfg_.dwellSec * 1000u / kFxStepMs;
    const uint32_t fadeFrames =
        std::max<uint32_t>(1, cfg_.crossfadeMs / kFxStepMs);
    const bool multi = cfg_.tracks.size() > 1 && !cfg_.repeatCurrent;

    if (multi && !fading_ && frameInTrack_ + fadeFrames >= dwellFrames) {
        fading_ = true;
        fadeTo_ = pickNext();
        fadeFrame_ = 0;
        Effect* incoming = effectFor(fadeTo_);
        incoming->reset(seed_ ^ static_cast<uint32_t>(fadeTo_ + 1),
                        ledCount_);
        if (fadeTo_ < cfg_.tracks.size())
            if (const Palette16* p =
                    paletteByName(cfg_.tracks[fadeTo_].palette))
                incoming->setPalette(*p);
    }

    if (fading_) {
        FxFrame fb{bufB_, fxFrame_, ms};
        effectFor(fadeTo_)->render(fb);
        // Blend fraction walks 0→255 across the fade — no snap frame.
        uint8_t amt = static_cast<uint8_t>(
            std::min<uint32_t>(255, (fadeFrame_ * 255u) / fadeFrames));
        for (uint16_t i = 0; i < ledCount_; ++i)
            nblend(bufA_[i], bufB_[i], amt);
        ++fadeFrame_;
        if (fadeFrame_ > fadeFrames) {
            startTrack(fadeTo_);
        }
    }

    // Brightness cap + LED range mask, then out.
    const float cap = cfg_.brightnessCap / 255.0f;
    for (uint16_t i = 0; i < ledCount_ && i < out.size(); ++i) {
        bool masked = cfg_.aboveKeysOnly && haveSpan_ &&
                      (i < spanFirst_ || i > spanLast_);
        out[i] = masked ? Rgb{} : scaleRgb(bufA_[i], cap);
    }

    ++fxFrame_;
    ++frameInTrack_;
}

std::string afkConfigToJson(const AfkConfig& c) {
    JsonDocument doc;
    JsonArray tracks = doc["tracks"].to<JsonArray>();
    for (const AfkTrack& t : c.tracks) {
        JsonObject o = tracks.add<JsonObject>();
        o["effect"] = t.effect;
        o["palette"] = t.palette;
    }
    doc["shuffle"] = c.shuffle;
    doc["repeatCurrent"] = c.repeatCurrent;
    doc["dwellSec"] = c.dwellSec;
    doc["crossfadeMs"] = c.crossfadeMs;
    doc["brightnessCap"] = c.brightnessCap;
    doc["masterSpeed"] = c.masterSpeed;
    doc["aboveKeysOnly"] = c.aboveKeysOnly;
    std::string out;
    serializeJson(doc, out);
    return out;
}

bool afkConfigFromJson(const char* json, AfkConfig& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (!json) return fail("bad json");
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok)
        return fail("bad json");
    if (!doc.is<JsonObjectConst>()) return fail("bad json");
    JsonObjectConst o = doc.as<JsonObjectConst>();

    AfkConfig c;
    if (o["tracks"].is<JsonArrayConst>()) {
        for (JsonObjectConst t : o["tracks"].as<JsonArrayConst>()) {
            AfkTrack track;
            track.effect = t["effect"] | "";
            track.palette = t["palette"] | "";
            if (!knownEffect(track.effect))
                return fail("unknown effect: " + track.effect);
            if (!track.palette.empty() && !paletteByName(track.palette))
                return fail("unknown palette: " + track.palette);
            c.tracks.push_back(std::move(track));
        }
    }
    c.shuffle = o["shuffle"] | false;
    c.repeatCurrent = o["repeatCurrent"] | false;
    c.dwellSec = o["dwellSec"] | 60u;
    c.crossfadeMs = std::min<uint32_t>(o["crossfadeMs"] | 2000u, 10000u);
    c.brightnessCap = o["brightnessCap"] | uint8_t{96};
    c.masterSpeed = o["masterSpeed"] | 1.0f;
    c.aboveKeysOnly = o["aboveKeysOnly"] | false;
    out = std::move(c);
    return true;
}

}  // namespace fx
}  // namespace vialucis
