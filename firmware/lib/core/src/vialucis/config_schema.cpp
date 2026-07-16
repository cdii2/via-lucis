#include "vialucis/config_schema.h"

#include <ArduinoJson.h>

namespace vialucis {

std::string stampSchema(const std::string& body, int schema) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return body;
    if (!doc.is<JsonObject>()) return body;
    doc["schema"] = schema;
    std::string out;
    serializeJson(doc, out);
    return out;
}

int readSchema(const std::string& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return 0;
    if (!doc.is<JsonObjectConst>()) return 0;
    return doc["schema"] | 0;
}

bool schemaLoadable(const std::string& body) {
    return schemaAccepted(readSchema(body));
}

}  // namespace vialucis
