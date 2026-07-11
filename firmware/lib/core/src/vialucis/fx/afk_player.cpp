#include "vialucis/fx/afk_player.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "vialucis/fx/color_waves.h"
#include "vialucis/fx/fx_color.h"

namespace vialucis {
namespace fx {

AfkPlayer::AfkPlayer(uint16_t ledCount)
    : ledCount_(ledCount), bufA_(ledCount), bufB_(ledCount) {
    fallback_ = std::make_unique<ColorWavesFx>();
    fallback_->reset(1, ledCount);
}

void AfkPlayer::setTable(const KeyLedTable& t) {
    table_ = t;
    haveSpan_ = false;
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n) {
        LedRange r = t.forNoteOrdered(n);  // one normalization (P-review)
        if (!r.valid) continue;
        if (!haveSpan_) {
            spanFirst_ = r.first;
            spanLast_ = r.last;
            haveSpan_ = true;
        } else {
            spanFirst_ = std::min(spanFirst_, r.first);
            spanLast_ = std::max(spanLast_, r.last);
        }
    }
}

AfkPlayer::Prepared AfkPlayer::prepare(const AfkConfig& c, uint32_t seed,
                                       uint16_t ledCount) {
    Prepared p;
    p.cfg = c;
    if (p.cfg.dwellSec < 5) p.cfg.dwellSec = 5;  // dwell=0 is a config bug
    if (p.cfg.masterSpeed < 0.25f) p.cfg.masterSpeed = 0.25f;
    if (p.cfg.masterSpeed > 4.0f) p.cfg.masterSpeed = 4.0f;
    // A fade longer than half the dwell would re-trigger immediately and
    // crossfade forever — tie the two fields together (closing review).
    uint32_t maxFade = p.cfg.dwellSec * 500u;  // half the dwell, in ms
    if (p.cfg.crossfadeMs > maxFade) p.cfg.crossfadeMs = maxFade;
    p.seed = seed;
    for (const AfkTrack& t : p.cfg.tracks) {
        std::unique_ptr<Effect> e = makeEffect(t.effect);
        if (e) {
            e->reset(seed ^ static_cast<uint32_t>(p.effects.size() + 1),
                     ledCount);
            if (const Palette16* pal = paletteByName(t.palette))
                e->setPalette(*pal);
        }
        p.effects.push_back(std::move(e));  // null slots render fallback
    }
    return p;
}

void AfkPlayer::apply(Prepared&& p) {
    // Same track list ⇒ a tuning tweak: keep the show where it is.
    bool sameTracks = p.cfg.tracks.size() == cfg_.tracks.size();
    for (size_t i = 0; sameTracks && i < p.cfg.tracks.size(); ++i)
        sameTracks = p.cfg.tracks[i].effect == cfg_.tracks[i].effect &&
                     p.cfg.tracks[i].palette == cfg_.tracks[i].palette;
    cfg_ = std::move(p.cfg);
    effects_ = std::move(p.effects);
    seed_ = p.seed;
    rng_.reseed(p.seed);
    if (!sameTracks) {
        current_ = 0;
        frameInTrack_ = 0;
        fading_ = false;
    } else if (current_ >= effects_.size() && !effects_.empty()) {
        current_ = 0;
    }
}

Effect* AfkPlayer::effectFor(size_t track) {
    if (track < effects_.size() && effects_[track]) return effects_[track].get();
    return fallback_.get();
}

size_t AfkPlayer::pickNext() {
    // ONE definition of "which track follows" — the auto-advance crossfade
    // and the manual Next button must never disagree (closing review).
    size_t n = cfg_.tracks.size();
    if (n <= 1) return current_;
    if (!cfg_.shuffle) return (current_ + 1) % n;
    size_t pick = rng_.random16(static_cast<uint16_t>(n - 1));
    return pick >= current_ ? pick + 1 : pick;  // any track but current
}

void AfkPlayer::startTrack(size_t index) {
    if (!cfg_.tracks.empty()) current_ = index % cfg_.tracks.size();
    frameInTrack_ = 0;
    fading_ = false;
}

void AfkPlayer::next() {  // manual next overrides repeat-current
    startTrack(pickNext());
}
void AfkPlayer::previous() {
    if (cfg_.tracks.empty()) return startTrack(0);
    size_t n = cfg_.tracks.size();
    startTrack((current_ + n - 1) % n);
}

void AfkPlayer::render(std::vector<Rgb>& out) {
    // Integer time math: speed quantized to 1/16ths (the show format's
    // convention) and accumulated in 64-bit so an unattended week of AFK
    // never loses ms precision the way float×frame-count did (closing
    // review). The eventual uint32 wrap (~49 days) is documented and
    // harmless — beat math wraps with it.
    const uint32_t speed16 =
        static_cast<uint32_t>(cfg_.masterSpeed * 16.0f + 0.5f);
    const uint32_t ms = static_cast<uint32_t>(
        static_cast<uint64_t>(fxFrame_) * kFxStepMs * speed16 / 16u);
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
        if (fadeFrame_ >= fadeFrames) {  // amt hit 255 — hand over now
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
            if (c.tracks.size() >= 16)  // each track owns a live effect —
                return fail("too many tracks (max 16)");  // bound the heap
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
    c.dwellSec = std::min<uint32_t>(o["dwellSec"] | 60u, 86400u);
    c.crossfadeMs = std::min<uint32_t>(o["crossfadeMs"] | 2000u, 10000u);
    c.brightnessCap = o["brightnessCap"] | uint8_t{96};
    c.masterSpeed = o["masterSpeed"] | 1.0f;
    c.aboveKeysOnly = o["aboveKeysOnly"] | false;
    out = std::move(c);
    return true;
}

}  // namespace fx
}  // namespace vialucis
