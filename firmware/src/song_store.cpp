#include "song_store.h"

#include <FS.h>
#include <LittleFS.h>

#include "vialucis/record_take.h"  // nextRecordingName() helper (native-tested)

namespace vialucis {

namespace {
constexpr const char* kSettingsPath = "/settings.json";
constexpr const char* kCalibrationPath = "/calibration.json";
constexpr const char* kAfkPath = "/afk.json";
constexpr const char* kShowDir = "/shows";
constexpr const char* kSongDir = "/songs";
}  // namespace

bool SongStore::begin() {
    // formatOnFail stays true here so a brand-new device still auto-initialises
    // its (empty) LittleFS on first boot — replicability is an iron rule. The
    // §6-2 flip to formatOnFail=false (never wipe REAL data on a boot reflex)
    // is B4's boot-policy change, paired there with self-heal. The health
    // signal below still catches the WEDGE (mounted-but-can't-create), which
    // was the actual field failure — and MountFailed wiring is ready for B4.
    bool mounted = LittleFS.begin(/*formatOnFail=*/true);
    bool canCreate = mounted;
    if (mounted) {
        if (!LittleFS.exists(kSongDir)) canCreate = LittleFS.mkdir(kSongDir);
        if (canCreate && !LittleFS.exists(kShowDir))
            canCreate = LittleFS.mkdir(kShowDir);
    }
    health_ = classifyFsHealth(mounted, canCreate);
    return health_ == FsHealth::Mounted;
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

bool SongStore::openUpload(const std::string& name, fs::File& out) {
    if (!validName(name)) return false;
    out = LittleFS.open(songPath(name).c_str(), "w");
    if (!out) {
        // A create that fails is the dir-metadata hard-full wedge (overwrites
        // and deletes still work, but NEW files don't). Surface it so the UI /
        // status can offer the format recovery instead of silent RSTs.
        health_ = FsHealth::Wedged;
        return false;
    }
    return true;
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

SongStore::RenameResult SongStore::rename(const std::string& from,
                                          const std::string& to) {
    if (!validName(from) || !validName(to)) return RenameResult::BadName;
    if (from == to) return RenameResult::Ok;  // no-op rename to itself
    if (!LittleFS.exists(songPath(from).c_str())) return RenameResult::NotFound;
    if (LittleFS.exists(songPath(to).c_str())) return RenameResult::Exists;
    return LittleFS.rename(songPath(from).c_str(), songPath(to).c_str())
               ? RenameResult::Ok
               : RenameResult::NotFound;
}

std::string SongStore::nextRecordingName() {
    std::vector<std::string> names;
    for (const SongFileInfo& s : list()) names.push_back(s.name);
    return vialucis::nextRecordingName(names);
}

size_t SongStore::freeBytes() {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    return total > used ? total - used : 0;
}

size_t SongStore::totalBytes() { return LittleFS.totalBytes(); }
size_t SongStore::usedBytes() { return LittleFS.usedBytes(); }

bool SongStore::format() {
    // Blocks for seconds — the caller (App loop task) invokes this off the
    // async_tcp path after the HTTP reply is already queued.
    LittleFS.end();
    LittleFS.format();
    return begin();  // re-mount, recreate dirs, reset health_
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

size_t SongStore::showSize(const std::string& name) {
    for (const SongFileInfo& s : listShows())
        if (s.name == name) return s.size;
    return 0;
}

bool SongStore::showExists(const std::string& name) {
    if (!validShowName(name)) return false;
    std::string path = std::string(kShowDir) + "/" + name;
    return LittleFS.exists(path.c_str());
}

bool SongStore::openShowUpload(const std::string& name, fs::File& out) {
    if (!validShowName(name)) return false;
    std::string path = std::string(kShowDir) + "/" + name;
    out = LittleFS.open(path.c_str(), "w");
    if (!out) {
        health_ = FsHealth::Wedged;
        return false;
    }
    return true;
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
