#include "vialucis/settings.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>

namespace vialucis {
namespace {

std::string colorToHex(const Rgb& c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

unsigned hexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    return static_cast<unsigned>(c - 'A' + 10);
}

// Strict hex color parse (§3-E item 7, hex-color-strictness — design by E1
// Wave, ASSUMPTIONS A165 on we/polish; applied here since settings.cpp is
// E2-owned this wave). The old `sscanf(s+1, "%2x%2x%2x", ...)` used `%2x` as
// a per-conversion MAXIMUM width, not a fixed count: a 5-digit body like
// "#12345" still satisfied all three conversions (the lone trailing digit
// read as the whole blue channel), and anything past the 6th digit (e.g.
// "#0000000000") was silently ignored — both produced a "valid"-looking
// color from a malformed body instead of rejecting it. This version requires
// EXACTLY '#' + 6 hex digits (case-insensitive) and nothing more or fewer;
// any other shape is a parse failure. Callers (readColor) already leave the
// field at its previous value on a `false` return — unchanged here.
bool hexToColor(const char* s, Rgb& out) {
    if (!s || s[0] != '#') return false;
    for (int i = 1; i <= 6; ++i) {
        if (!isHexDigit(s[i])) return false;
    }
    if (s[7] != '\0') return false;  // no trailing junk past the 6th digit
    unsigned r = hexNibble(s[1]) * 16 + hexNibble(s[2]);
    unsigned g = hexNibble(s[3]) * 16 + hexNibble(s[4]);
    unsigned b = hexNibble(s[5]) * 16 + hexNibble(s[6]);
    out = {static_cast<uint8_t>(r), static_cast<uint8_t>(g),
           static_cast<uint8_t>(b)};
    return true;
}

void readColor(JsonVariantConst v, Rgb& into) {
    if (v.is<const char*>()) {
        Rgb c;
        if (hexToColor(v.as<const char*>(), c)) into = c;
    }
}

float clampf(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}

}  // namespace

std::string Settings::toJson(View view) const {
    JsonDocument doc;
    doc["leftColor"] = colorToHex(leftColor);
    doc["rightColor"] = colorToHex(rightColor);
    doc["wrongColor"] = colorToHex(wrongColor);
    doc["previewCap"] = previewCap;
    doc["leadMs"] = leadMs;
    doc["offsetMm"] = offsetMm;
    doc["ledsPerMeter"] = ledsPerMeter;
    doc["brightness"] = brightness;
    doc["echoWindowMs"] = echoWindowMs;
    doc["wifiSsid"] = wifiSsid;
    // wifiPass is write-only across the REST boundary (ruling §6-1): only the
    // Persist doc (flash) carries the secret so the device reconnects after a
    // reboot. The Public view NEVER emits it — it emits `wifiPassSet` instead
    // so the UI can show whether a password is stored without revealing it.
    if (view == View::Persist)
        doc["wifiPass"] = wifiPass;
    else
        doc["wifiPassSet"] = !wifiPass.empty();
    doc["repeatCueEnabled"] = repeatCueEnabled;
    doc["repeatColor"] = colorToHex(repeatColor);
    doc["repeatFillStartPct"] = repeatFillStartPct;
    doc["repeatFillPeakPct"] = repeatFillPeakPct;
    doc["repeatFloorMs"] = repeatFloorMs;
    doc["repeatWaitPulseMs"] = repeatWaitPulseMs;
    doc["afkTimeoutSec"] = afkTimeoutSec;
    doc["recordBudgetKB"] = recordBudgetKB;
    // bleTargetName is NOT a secret (unlike wifiPass) — it's a device name,
    // not a credential — so it rides both views unchanged (ruling §6-1's
    // "secret-ness is a property of the field" cuts the other way here: this
    // field simply isn't one).
    doc["bleTargetName"] = bleTargetName;
    std::string out;
    serializeJson(doc, out);
    return out;
}

bool Settings::fromJson(const char* json, Settings& out) {
    if (!json) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
    if (!doc.is<JsonObjectConst>()) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();

    Rgb prevWrong = out.wrongColor;
    readColor(o["leftColor"], out.leftColor);
    readColor(o["rightColor"], out.rightColor);
    readColor(o["wrongColor"], out.wrongColor);

    if (o["previewCap"].is<float>())
        out.previewCap = clampf(o["previewCap"].as<float>(), 0.0f, 1.0f);
    if (o["leadMs"].is<uint32_t>())
        out.leadMs = std::min<uint32_t>(o["leadMs"].as<uint32_t>(), 10000);
    if (o["offsetMm"].is<float>())
        out.offsetMm = clampf(o["offsetMm"].as<float>(), -kOffsetMmLimit,
                              kOffsetMmLimit);
    if (o["ledsPerMeter"].is<float>()) {
        float v = o["ledsPerMeter"].as<float>();
        if (v > 0.0f)
            out.ledsPerMeter = clampf(v, kLedsPerMeterMin, kLedsPerMeterMax);
    }
    if (o["brightness"].is<uint8_t>())
        out.brightness = o["brightness"].as<uint8_t>();
    if (o["echoWindowMs"].is<uint32_t>())
        out.echoWindowMs =
            std::min<uint32_t>(o["echoWindowMs"].as<uint32_t>(), 5000);
    if (o["wifiSsid"].is<const char*>())
        out.wifiSsid = o["wifiSsid"].as<const char*>();
    // Write-only PATCH (ruling §6-1, A139): the key PRESENT sets the password
    // (an explicit "" CLEARS it); the key ABSENT leaves the stored password
    // untouched — which is what keeps the redacted Public view lossless on a
    // round-trip (GET omits wifiPass → the client PUTs a body without it → the
    // stored secret survives). Clients must never send "" unless they intend to
    // forget the network.
    if (o["wifiPass"].is<const char*>())
        out.wifiPass = o["wifiPass"].as<const char*>();

    if (o["repeatCueEnabled"].is<bool>())
        out.repeatCueEnabled = o["repeatCueEnabled"].as<bool>();
    {
        // A repeat cue that looks like the wrong-note flash would train the
        // player to ignore errors — the collision is rejected (field kept
        // at its previous value), same rule the UI enforces for hand colors.
        Rgb rc = out.repeatColor;
        readColor(o["repeatColor"], rc);
        if (!(rc.r == out.wrongColor.r && rc.g == out.wrongColor.g &&
              rc.b == out.wrongColor.b))
            out.repeatColor = rc;
    }
    if (o["repeatFillStartPct"].is<uint8_t>())
        out.repeatFillStartPct =
            std::min<uint8_t>(o["repeatFillStartPct"].as<uint8_t>(), 100);
    if (o["repeatFillPeakPct"].is<uint8_t>())
        out.repeatFillPeakPct =
            std::min<uint8_t>(o["repeatFillPeakPct"].as<uint8_t>(), 100);
    if (o["repeatFloorMs"].is<uint32_t>())
        out.repeatFloorMs =
            std::min<uint32_t>(o["repeatFloorMs"].as<uint32_t>(), 1000);
    if (o["repeatWaitPulseMs"].is<uint32_t>())
        out.repeatWaitPulseMs =
            std::min<uint32_t>(o["repeatWaitPulseMs"].as<uint32_t>(), 1000);
    if (o["afkTimeoutSec"].is<uint32_t>())
        out.afkTimeoutSec =
            std::min<uint32_t>(o["afkTimeoutSec"].as<uint32_t>(), 86400);
    if (o["recordBudgetKB"].is<uint32_t>())
        // Upper bound is the 256 KB per-song save ceiling (kMaxSongBytes): a
        // take bigger than that can't be written at all, and the record core
        // now hard-clamps there too (B5 ask 4) — so 1024 was an unsaveable,
        // misleading max. Clamp 16..256 to match.
        out.recordBudgetKB = std::min<uint32_t>(
            std::max<uint32_t>(o["recordBudgetKB"].as<uint32_t>(), 16), 256);
    if (o["bleTargetName"].is<const char*>()) {
        // Empty string is a valid, meaningful value (explicitly clears the
        // filter back to accept-any) — always assign, then clamp length.
        std::string v = o["bleTargetName"].as<const char*>();
        if (v.size() > kBleTargetNameMaxLen) v.resize(kBleTargetNameMaxLen);
        out.bleTargetName = v;
    }

    // The collision guard cuts BOTH ways: a wrongColor edit that lands on
    // the current repeatColor is rejected too (Q-wave closing review).
    if (out.wrongColor.r == out.repeatColor.r &&
        out.wrongColor.g == out.repeatColor.g &&
        out.wrongColor.b == out.repeatColor.b)
        out.wrongColor = prevWrong;
    return true;
}

}  // namespace vialucis
