#pragma once
// Calibration document (C3): /calibration.json on the device AND the wire
// shape of GET/PUT /api/calibration (VL2 — documents get resources). Owns
// the tier metadata + the per-key table; the engine only ever receives the
// finished KeyLedTable.
//
// Boot rule (CRITICAL upgrade path): a device with NO /calibration.json
// runs on fromSettings(...) — integer-identical to v1's two-setting formula.
// offsetMm/ledsPerMeter stay in Settings as the 2-point tier's inputs.

#include <cstdint>
#include <string>
#include <vector>

#include "vialucis/key_led_table.h"
#include "vialucis/settings.h"

namespace vialucis {

struct CalibResult {
    enum class Kind : uint8_t { Ok, BadJson, BadTier, MissingField, BadTable };
    Kind kind = Kind::Ok;
    TableError tableError = TableError::None;  // detail when kind==BadTable
    uint8_t badKey = 0;  // offending MIDI note for Overlap/RangeOffStrip
    bool ok() const { return kind == Kind::Ok; }
    const char* message() const;  // stable text for REST 400 bodies
};

struct Calibration {
    std::string tier = "twoPoint";  // twoPoint | multiPoint | perKey
    bool reversed = false;          // twoPoint only: mirrored install
    float offsetMm = 0.0f;          // twoPoint inputs (mirror Settings)
    float ledsPerMeter = 180.0f;
    std::vector<Landmark> landmarks;  // multiPoint inputs (round-trip)
    KeyLedTable table;

    // The boot fallback: the settings' 2-point values as a calibration —
    // table integer-identical to the v1 formula (unreversed).
    static Calibration fromSettings(const Settings& s, uint16_t ledCount,
                                    bool reversed = false);

    std::string toJson() const;

    // Parse a stored document or PUT body. Tier decides the required
    // fields; tables are rebuilt from inputs (twoPoint/multiPoint) or read
    // and validated (perKey). `ledCount` is the DEVICE's strip — any
    // ledCount in the body is ignored.
    static CalibResult fromJson(const char* json, uint16_t ledCount,
                                Calibration& out);
};

}  // namespace vialucis
