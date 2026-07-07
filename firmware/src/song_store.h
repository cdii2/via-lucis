#pragma once
// LittleFS persistence: songs under /songs/, settings at /settings.json.

#include <cstdint>
#include <string>
#include <vector>

#include "vialucis/settings.h"

namespace vialucis {

struct SongFileInfo {
    std::string name;
    size_t size;
};

class SongStore {
public:
    static constexpr size_t kMaxSongBytes = 256 * 1024;

    bool begin();  // mounts LittleFS (formats on first boot)

    std::vector<SongFileInfo> list();
    bool save(const std::string& name, const uint8_t* data, size_t len);
    bool appendChunk(const std::string& name, const uint8_t* data, size_t len,
                     bool first);  // streaming upload
    bool remove(const std::string& name);
    bool read(const std::string& name, std::vector<uint8_t>& out);

    bool loadSettings(Settings& s);
    bool saveSettings(const Settings& s);

    // Keep names boring: alnum, dash, underscore, dot; must end ".mid".
    static bool validName(const std::string& name);

private:
    std::string songPath(const std::string& name) const;
};

}  // namespace vialucis
