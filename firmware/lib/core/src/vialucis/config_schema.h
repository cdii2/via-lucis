#pragma once
// Persisted-doc schema versioning (B4). Every persisted config file
// (settings.json, calibration.json, afk.json) carries "schema":1. A loader
// tolerates an ABSENT schema (a pre-B4 file — treated as 1) and rejects an
// unknown HIGHER schema (a file written by a NEWER firmware a downgrade must
// not silently misread). The decision is pure + native-tested; the string
// helpers use ArduinoJson (available in lib/core) so the stamp/read round-trip
// is testable without a device.

#include <string>

namespace vialucis {

// Current persisted-config schema. Bump when a stored doc's shape changes in a
// way older firmware could misread.
constexpr int kConfigSchema = 1;

// Absent (0) or any schema <= current loads; an unknown HIGHER schema is
// rejected. A negative value (never emitted) is treated as unloadable.
constexpr bool schemaAccepted(int schema) {
    return schema >= 0 && schema <= kConfigSchema;
}

// Add/overwrite "schema" as a field of a JSON object string. Returns the input
// unchanged if it is not parseable as a JSON object (defensive). Pure — no FS.
std::string stampSchema(const std::string& body, int schema = kConfigSchema);

// The "schema" value of a JSON object string, or 0 if absent / not an object.
int readSchema(const std::string& body);

// Whether a stored doc's schema is loadable by THIS firmware (absent counts as
// loadable; garbage reads as 0 = loadable and is caught by the doc's own
// parser downstream — this gate only rejects a known-higher schema).
bool schemaLoadable(const std::string& body);

}  // namespace vialucis
