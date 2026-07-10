#include "song_store.h"

#include <FS.h>
#include <LittleFS.h>

namespace vialucis {

namespace {
constexpr const char* kSettingsPath = "/settings.json";
constexpr const char* kCalibrationPath = "/calibration.json";
constexpr const char* kAfkPath = "/afk.json";
constexpr const char* kShowDir = "/shows";
constexpr const char* kSongDir = "/songs";
}  // namespace

bool SongStore::begin() {
    if (!LittleFS.begin(/*formatOnFail=*/true)) return false;
    if (!LittleFS.exists(kSongDir)) LittleFS.mkdir(kSongDir);
    if (!LittleFS.exists(kShowDir)) LittleFS.mkdir(kShowDir);
    return true;
}

bool SongStore::validName(const std::string& name) {
    if (name.size() < 5 || name.size() > 64) return false;
    if (name.rfind(".mid") != name.size() - 4) return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                  c == ' ';
        if (!ok) return false;
    }
    return true;
}

std::string SongStore::songPath(const std::string& name) const {
    return std::string(kSongDir) + "/" + name;
}

std::vector<SongFileInfo> SongStore::list() {
    std::vector<SongFileInfo> out;
    File dir = LittleFS.open(kSongDir);
    if (!dir || !dir.isDirectory()) return out;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        out.push_back({std::string(f.name()), static_cast<size_t>(f.size())});
    }
    return out;
}

bool SongStore::save(const std::string& name, const uint8_t* data,
                     size_t len) {
    if (!validName(name) || len > kMaxSongBytes) return false;
    File f = LittleFS.open(songPath(name).c_str(), "w");
    if (!f) return false;
    size_t written = f.write(data, len);
    f.close();
    return written == len;
}

bool SongStore::appendChunk(const std::string& name, const uint8_t* data,
                            size_t len, bool first) {
    if (!validName(name)) return false;
    File f = LittleFS.open(songPath(name).c_str(), first ? "w" : "a");
    if (!f) return false;
    if (f.size() + len > kMaxSongBytes) {
        f.close();
        return false;
    }
    size_t written = f.write(data, len);
    f.close();
    return written == len;
}

bool SongStore::remove(const std::string& name) {
    if (!validName(name)) return false;
    return LittleFS.remove(songPath(name).c_str());
}

bool SongStore::read(const std::string& name, std::vector<uint8_t>& out) {
    if (!validName(name)) return false;
    File f = LittleFS.open(songPath(name).c_str(), "r");
    if (!f) return false;
    size_t len = f.size();
    if (len > kMaxSongBytes) {
        f.close();
        return false;
    }
    out.resize(len);
    size_t got = f.read(out.data(), len);
    f.close();
    return got == len;
}

bool SongStore::loadSettings(Settings& s) {
    File f = LittleFS.open(kSettingsPath, "r");
    if (!f) return false;
    String json = f.readString();
    f.close();
    return Settings::fromJson(json.c_str(), s);
}

namespace {

// One read/write discipline for every stored text document (settings,
// calibration, whatever comes next) — closing-review dedupe.
bool readTextFile(const char* path, std::string& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    String s = f.readString();
    f.close();
    out = s.c_str();
    return !out.empty();
}

bool writeTextFile(const char* path, const std::string& body) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t written = f.write(reinterpret_cast<const uint8_t*>(body.data()),
                             body.size());
    f.close();
    return written == body.size();
}

}  // namespace

bool SongStore::loadCalibration(std::string& json) {
    return readTextFile(kCalibrationPath, json);
}

bool SongStore::saveCalibration(const std::string& json) {
    return writeTextFile(kCalibrationPath, json);
}

bool SongStore::saveSettings(const Settings& s) {
    return writeTextFile(kSettingsPath, s.toJson());
}

bool SongStore::validShowName(const std::string& name) {
    if (name.size() < 5 || name.size() > 64) return false;
    if (name.rfind(".vls") != name.size() - 4) return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                  c == '.' || c == ' ';
        if (!ok) return false;
    }
    return true;
}

std::vector<SongFileInfo> SongStore::listShows() {
    std::vector<SongFileInfo> out;
    File dir = LittleFS.open(kShowDir);
    if (!dir || !dir.isDirectory()) return out;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        out.push_back({std::string(f.name()), static_cast<size_t>(f.size())});
    }
    return out;
}

size_t SongStore::showTotalBytes() {
    size_t total = 0;
    for (const SongFileInfo& s : listShows()) total += s.size;
    return total;
}

bool SongStore::appendShowChunk(const std::string& name, const uint8_t* data,
                                size_t len, bool first) {
    if (!validShowName(name)) return false;
    std::string path = std::string(kShowDir) + "/" + name;
    File f = LittleFS.open(path.c_str(), first ? "w" : "a");
    if (!f) return false;
    if (f.size() + len > kMaxShowBytes) {
        f.close();
        return false;
    }
    size_t written = f.write(data, len);
    f.close();
    return written == len;
}

bool SongStore::readShow(const std::string& name, std::vector<uint8_t>& out) {
    if (!validShowName(name)) return false;
    std::string path = std::string(kShowDir) + "/" + name;
    File f = LittleFS.open(path.c_str(), "r");
    if (!f) return false;
    size_t len = f.size();
    if (len > kMaxShowBytes) {
        f.close();
        return false;
    }
    out.resize(len);
    size_t got = f.read(out.data(), len);
    f.close();
    return got == len;
}

bool SongStore::removeShow(const std::string& name) {
    if (!validShowName(name)) return false;
    std::string path = std::string(kShowDir) + "/" + name;
    return LittleFS.remove(path.c_str());
}

bool SongStore::loadAfk(std::string& json) {
    return readTextFile(kAfkPath, json);
}

bool SongStore::saveAfk(const std::string& json) {
    return writeTextFile(kAfkPath, json);
}

}  // namespace vialucis
