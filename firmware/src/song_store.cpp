#include "song_store.h"

#include <FS.h>
#include <LittleFS.h>

namespace vialucis {

namespace {
constexpr const char* kSettingsPath = "/settings.json";
constexpr const char* kSongDir = "/songs";
}  // namespace

bool SongStore::begin() {
    if (!LittleFS.begin(/*formatOnFail=*/true)) return false;
    if (!LittleFS.exists(kSongDir)) LittleFS.mkdir(kSongDir);
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

bool SongStore::saveSettings(const Settings& s) {
    File f = LittleFS.open(kSettingsPath, "w");
    if (!f) return false;
    std::string json = s.toJson();
    size_t written = f.write(reinterpret_cast<const uint8_t*>(json.data()),
                             json.size());
    f.close();
    return written == json.size();
}

}  // namespace vialucis
