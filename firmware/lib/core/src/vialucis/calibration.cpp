#include "vialucis/calibration.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace vialucis {
namespace {

// Same clamps PUT /api/settings applies to the two scalars — one rule for
// the same numbers on both routes.
float clampf(float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
}

CalibResult tableFail(TableError e, uint8_t badKey = 0) {
    CalibResult r;
    r.kind = CalibResult::Kind::BadTable;
    r.tableError = e;
    r.badKey = badKey;
    return r;
}

CalibResult fail(CalibResult::Kind k) {
    CalibResult r;
    r.kind = k;
    return r;
}

}  // namespace

const char* CalibResult::message() const {
    switch (kind) {
        case Kind::Ok: return "";
        case Kind::BadJson: return "bad json";
        case Kind::BadTier: return "bad tier";
        case Kind::MissingField: return "missing field";
        case Kind::BadTable: break;
    }
    switch (tableError) {
        case TableError::TooFewLandmarks: return "need at least 2 landmarks";
        case TableError::BadLandmarkNote: return "note out of range";
        case TableError::BadLandmarkLed: return "led off strip";
        case TableError::UnsortedLandmarks: return "landmarks must ascend by note";
        case TableError::DirectionMixed: return "led direction inconsistent";
        case TableError::RangeOffStrip: return "key range off strip";
        case TableError::Overlap: return "key ranges overlap";
        case TableError::None: break;
    }
    return "bad table";
}

Calibration Calibration::fromSettings(const Settings& s, uint16_t ledCount,
                                      bool reversed) {
    Calibration c;
    c.tier = "twoPoint";
    c.reversed = reversed;
    c.offsetMm = s.offsetMm;
    c.ledsPerMeter = s.ledsPerMeter;
    c.table = TableBuilder::fromTwoPoint(s.ledMapConfig(ledCount), reversed);
    return c;
}

std::string Calibration::toJson() const {
    JsonDocument doc;
    doc["tier"] = tier;
    doc["reversed"] = reversed;
    doc["ledCount"] = table.ledCount();
    if (tier == "twoPoint") {
        doc["offsetMm"] = offsetMm;
        doc["ledsPerMeter"] = ledsPerMeter;
    }
    if (tier == "multiPoint") {
        JsonArray lm = doc["landmarks"].to<JsonArray>();
        for (const Landmark& m : landmarks) {
            JsonObject o = lm.add<JsonObject>();
            o["note"] = m.note;
            o["led"] = m.led;
        }
    }
    JsonArray keys = doc["keys"].to<JsonArray>();
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n) {
        LedRange r = table.forNote(n);
        if (!r.valid) continue;
        JsonObject o = keys.add<JsonObject>();
        o["note"] = n;
        o["first"] = r.first;
        o["last"] = r.last;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

CalibResult Calibration::fromJson(const char* json, uint16_t ledCount,
                                  Calibration& out) {
    if (!json) return fail(CalibResult::Kind::BadJson);
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok)
        return fail(CalibResult::Kind::BadJson);
    if (!doc.is<JsonObjectConst>()) return fail(CalibResult::Kind::BadJson);
    JsonObjectConst o = doc.as<JsonObjectConst>();

    const char* tier = o["tier"] | static_cast<const char*>(nullptr);
    if (!tier) return fail(CalibResult::Kind::BadTier);
    std::string t = tier;
    if (t != "twoPoint" && t != "multiPoint" && t != "perKey")
        return fail(CalibResult::Kind::BadTier);

    Calibration c;
    c.tier = t;
    c.reversed = o["reversed"] | false;

    if (t == "twoPoint") {
        if (!o["offsetMm"].is<float>() || !o["ledsPerMeter"].is<float>())
            return fail(CalibResult::Kind::MissingField);
        c.offsetMm = clampf(o["offsetMm"].as<float>(), -kOffsetMmLimit,
                            kOffsetMmLimit);
        float lpm = o["ledsPerMeter"].as<float>();
        if (lpm <= 0.0f) return fail(CalibResult::Kind::MissingField);
        c.ledsPerMeter = clampf(lpm, kLedsPerMeterMin, kLedsPerMeterMax);
        LedMapConfig cfg;
        cfg.offsetMm = c.offsetMm;
        cfg.ledsPerMeter = c.ledsPerMeter;
        cfg.ledCount = ledCount;
        c.table = TableBuilder::fromTwoPoint(cfg, c.reversed);
    } else if (t == "multiPoint") {
        if (!o["landmarks"].is<JsonArrayConst>())
            return fail(CalibResult::Kind::MissingField);
        for (JsonObjectConst m : o["landmarks"].as<JsonArrayConst>()) {
            if (!m["note"].is<uint8_t>() || !m["led"].is<uint16_t>())
                return fail(CalibResult::Kind::MissingField);
            c.landmarks.push_back(
                {m["note"].as<uint8_t>(), m["led"].as<uint16_t>()});
        }
        TableError e =
            TableBuilder::fromLandmarks(c.landmarks, ledCount, c.table);
        if (e != TableError::None) return tableFail(e);
        // Direction is data in this tier; reflect it for GET consumers.
        c.reversed = c.landmarks.size() >= 2 &&
                     c.landmarks[1].led < c.landmarks[0].led;
    } else {  // perKey
        if (!o["keys"].is<JsonArrayConst>())
            return fail(CalibResult::Kind::MissingField);
        c.table = KeyLedTable{};
        c.table.setLedCount(ledCount);
        for (JsonObjectConst k : o["keys"].as<JsonArrayConst>()) {
            if (!k["note"].is<uint8_t>() || !k["first"].is<uint16_t>() ||
                !k["last"].is<uint16_t>())
                return fail(CalibResult::Kind::MissingField);
            uint8_t note = k["note"].as<uint8_t>();
            if (note < KeyLedTable::kFirstNote ||
                note >= KeyLedTable::kFirstNote + KeyLedTable::kKeyCount)
                return tableFail(TableError::BadLandmarkNote, note);
            LedRange r;
            r.first = k["first"].as<uint16_t>();
            r.last = k["last"].as<uint16_t>();
            r.valid = true;
            c.table.set(note, r);
        }
        uint8_t badKey = 0;
        TableError e = TableBuilder::validate(c.table, &badKey);
        if (e != TableError::None) return tableFail(e, badKey);
    }

    out = std::move(c);
    return CalibResult{};
}

}  // namespace vialucis
