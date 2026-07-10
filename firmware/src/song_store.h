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

    // Calibration document, stored as raw JSON at /calibration.json (C3).
    // Absent file ⇒ false: the caller seeds from the settings' 2-point
    // values (the v1 upgrade path).
    bool loadCalibration(std::string& json);
    bool saveCalibration(const std::string& json);

    // AFK playlist document at /afk.json (E3). Absent ⇒ defaults.
    bool loadAfk(std::string& json);
    bool saveAfk(const std::string& json);

    // Shows under /shows/ (P-wave; SHOW-FORMAT.md quotas: 64KB per show,
    // 384KB / 16 shows total — the REST layer maps these to 413/507).
    static constexpr size_t kMaxShowBytes = 64 * 1024;
    static constexpr size_t kMaxShowTotalBytes = 384 * 1024;
    static constexpr size_t kMaxShowCount = 16;
    static bool validShowName(const std::string& name);  // *.vls
    std::vector<SongFileInfo> listShows();
    size_t showTotalBytes();
    bool appendShowChunk(const std::string& name, const uint8_t* data,
                         size_t len, bool first);
    bool readShow(const std::string& name, std::vector<uint8_t>& out);
    bool removeShow(const std::string& name);

    // Keep names boring: alnum, dash, underscore, dot; must end ".mid".
    static bool validName(const std::string& name);

private:
    std::string songPath(const std::string& name) const;
};

}  // namespace vialucis
