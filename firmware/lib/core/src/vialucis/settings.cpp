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

bool hexToColor(const char* s, Rgb& out) {
    if (!s || s[0] != '#') return false;
    unsigned r, g, b;
    if (std::sscanf(s + 1, "%2x%2x%2x", &r, &g, &b) != 3) return false;
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

std::string Settings::toJson() const {
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
    doc["wifiPass"] = wifiPass;
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

    readColor(o["leftColor"], out.leftColor);
    readColor(o["rightColor"], out.rightColor);
    readColor(o["wrongColor"], out.wrongColor);

    if (o["previewCap"].is<float>())
        out.previewCap = clampf(o["previewCap"].as<float>(), 0.0f, 1.0f);
    if (o["leadMs"].is<uint32_t>())
        out.leadMs = std::min<uint32_t>(o["leadMs"].as<uint32_t>(), 10000);
    if (o["offsetMm"].is<float>())
        out.offsetMm = clampf(o["offsetMm"].as<float>(), -2000.0f, 2000.0f);
    if (o["ledsPerMeter"].is<float>()) {
        float v = o["ledsPerMeter"].as<float>();
        if (v > 0.0f) out.ledsPerMeter = clampf(v, 10.0f, 1000.0f);
    }
    if (o["brightness"].is<uint8_t>())
        out.brightness = o["brightness"].as<uint8_t>();
    if (o["echoWindowMs"].is<uint32_t>())
        out.echoWindowMs =
            std::min<uint32_t>(o["echoWindowMs"].as<uint32_t>(), 5000);
    if (o["wifiSsid"].is<const char*>())
        out.wifiSsid = o["wifiSsid"].as<const char*>();
    if (o["wifiPass"].is<const char*>())
        out.wifiPass = o["wifiPass"].as<const char*>();
    return true;
}

}  // namespace vialucis
