#include "song_store.h"

#include <FS.h>
#include <LittleFS.h>

#include "vialucis/config_schema.h"  // stampSchema / schemaLoadable (B4)
#include "vialucis/record_take.h"  // nextRecordingName() helper (native-tested)

namespace vialucis {

namespace {
constexpr const char* kSettingsPath = "/settings.json";
constexpr const char* kCalibrationPath = "/calibration.json";
constexpr const char* kAfkPath = "/afk.json";
constexpr const char* kShowDir = "/shows";
constexpr const char* kSongDir = "/songs";

// Remove every ".tmp" staged file in a directory — an aborted atomic write
// (crash/power-loss between the tmp write and the rename) must not leak space
// or shadow a real file. Uses the same basename convention list() reads.
void removeTempsInDir(const char* dir) {
    File d = LittleFS.open(dir);
    if (!d || !d.isDirectory()) return;
    std::vector<std::string> victims;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        if (f.isDirectory()) continue;
        std::string name = f.name();
        if (isTmpPath(name)) victims.push_back(std::string(dir) + "/" + name);
    }
    for (const std::string& p : victims) LittleFS.remove(p.c_str());
}

// Boot sweep of every atomic-write temp: the two content dirs plus the three
// root config docs.
void sweepTemps() {
    removeTempsInDir(kSongDir);
    removeTempsInDir(kShowDir);
    for (const char* p : {kSettingsPath, kCalibrationPath, kAfkPath}) {
        std::string t = tmpPathFor(p);
        if (LittleFS.exists(t.c_str())) LittleFS.remove(t.c_str());
    }
}
}  // namespace

bool SongStore::begin() {
    // formatOnFail=FALSE (ruling §6-2, SUPERSEDES A108): a boot must NEVER wipe
    // real user data as a reflex. A fresh/unformatted device now mount-FAILS to
    // FsHealth::MountFailed while the web UI is still served from flash — the
    // user recovers via the guarded POST /api/storage/format (docs/API.md
    // "Storage"). Paired with App::begin's config self-heal (a corrupt doc
    // becomes defaults, not a full wipe). The Wedged signal
    // (mounted-but-can't-create — the actual field failure) is still caught.
    bool mounted = LittleFS.begin(/*formatOnFail=*/false);
    bool canCreate = mounted;
    if (mounted) {
        if (!LittleFS.exists(kSongDir)) canCreate = LittleFS.mkdir(kSongDir);
        if (canCreate && !LittleFS.exists(kShowDir))
            canCreate = LittleFS.mkdir(kShowDir);
        sweepTemps();  // clear any crash-orphaned atomic-write temps
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
        std::string name = f.name();
        if (isTmpPath(name)) continue;  // hide in-progress/stale atomic temps
        out.push_back({name, static_cast<size_t>(f.size())});
    }
    return out;
}

bool SongStore::save(const std::string& name, const uint8_t* data,
                     size_t len) {
    if (!validName(name) || len > kMaxSongBytes) return false;
    // Atomic (B4): stage to a ".tmp" then rename over the target, so an
    // interrupted record-take save never truncates an existing same-name song.
    return atomicPersist(
        songPath(name),
        [&](const std::string& tmp) {
            File f = LittleFS.open(tmp.c_str(), "w");
            if (!f) return false;
            size_t written = f.write(data, len);
            f.close();
            return written == len;
        },
        [](const std::string& tmp, const std::string& dst) {
            return LittleFS.rename(tmp.c_str(), dst.c_str());
        },
        [](const std::string& tmp) { LittleFS.remove(tmp.c_str()); });
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
    // Open the ".tmp" sibling (B4 atomic upload): an interrupted transfer never
    // touches an existing same-name song — commitUpload renames on completion.
    out = LittleFS.open(tmpPathFor(songPath(name)).c_str(), "w");
    if (!out) {
        // A create that fails is the dir-metadata hard-full wedge (overwrites
        // and deletes still work, but NEW files don't). Surface it so the UI /
        // status can offer the format recovery instead of silent RSTs.
        health_ = FsHealth::Wedged;
        return false;
    }
    return true;
}

bool SongStore::commitUpload(const std::string& name) {
    if (!validName(name)) return false;
    std::string dst = songPath(name);
    if (!LittleFS.rename(tmpPathFor(dst).c_str(), dst.c_str())) {
        health_ = FsHealth::Wedged;  // a rename that can't land is a wedge too
        return false;
    }
    return true;
}

void SongStore::abortUpload(const std::string& name) {
    if (!validName(name)) return;
    LittleFS.remove(tmpPathFor(songPath(name)).c_str());
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
    // A180/A182: refuse a read whose read+PARSE the heap can't hold — every
    // consumer of this read (songs-list parse-check, song load) parses next,
    // and a failed mid-parse allocation aborts uncatchably on the async_tcp
    // task (crash-looping GET /api/songs; proven live, decoded backtrace).
    // See storage_budget.h::parseWorkFits — factor is bring-up-tunable.
    if (!parseWorkFits(len, ESP.getMaxAllocHeap())) {
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

namespace {

// One read/write discipline for every stored config doc (settings, calibration,
// afk). Reads are schema-gated (B4): an absent schema is tolerated, an unknown
// HIGHER schema reads as corrupt (returns false). Writes stamp "schema":N and
// land atomically (tmp + rename) so a power loss can never truncate config.
bool readTextFile(const char* path, std::string& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    String s = f.readString();
    f.close();
    out = s.c_str();
    if (out.empty()) return false;
    if (!schemaLoadable(out)) return false;  // newer-schema doc -> corrupt
    return true;
}

bool writeTextFile(const char* path, const std::string& body) {
    const std::string stamped = stampSchema(body);  // add "schema":kConfigSchema
    return atomicPersist(
        path,
        [&](const std::string& tmp) {
            File f = LittleFS.open(tmp.c_str(), "w");
            if (!f) return false;
            size_t written = f.write(
                reinterpret_cast<const uint8_t*>(stamped.data()),
                stamped.size());
            f.close();
            return written == stamped.size();
        },
        [](const std::string& tmp, const std::string& dst) {
            return LittleFS.rename(tmp.c_str(), dst.c_str());
        },
        [](const std::string& tmp) { LittleFS.remove(tmp.c_str()); });
}

}  // namespace

DocLoad SongStore::loadSettings(Settings& s) {
    if (!LittleFS.exists(kSettingsPath)) return DocLoad::Absent;
    std::string body;
    if (!readTextFile(kSettingsPath, body)) return DocLoad::Corrupt;
    if (!Settings::fromJson(body.c_str(), s)) return DocLoad::Corrupt;
    return DocLoad::Ok;
}

bool SongStore::saveSettings(const Settings& s) {
    return writeTextFile(kSettingsPath, s.toJson());
}

DocLoad SongStore::loadCalibration(std::string& json) {
    if (!LittleFS.exists(kCalibrationPath)) return DocLoad::Absent;
    if (!readTextFile(kCalibrationPath, json)) return DocLoad::Corrupt;
    return DocLoad::Ok;
}

bool SongStore::saveCalibration(const std::string& json) {
    return writeTextFile(kCalibrationPath, json);
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
        std::string name = f.name();
        if (isTmpPath(name)) continue;  // hide in-progress/stale atomic temps
        out.push_back({name, static_cast<size_t>(f.size())});
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
    out = LittleFS.open(tmpPathFor(path).c_str(), "w");  // B4 atomic upload
    if (!out) {
        health_ = FsHealth::Wedged;
        return false;
    }
    return true;
}

bool SongStore::commitShowUpload(const std::string& name) {
    if (!validShowName(name)) return false;
    std::string dst = std::string(kShowDir) + "/" + name;
    if (!LittleFS.rename(tmpPathFor(dst).c_str(), dst.c_str())) {
        health_ = FsHealth::Wedged;
        return false;
    }
    return true;
}

void SongStore::abortShowUpload(const std::string& name) {
    if (!validShowName(name)) return;
    std::string dst = std::string(kShowDir) + "/" + name;
    LittleFS.remove(tmpPathFor(dst).c_str());
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

DocLoad SongStore::loadAfk(std::string& json) {
    if (!LittleFS.exists(kAfkPath)) return DocLoad::Absent;
    if (!readTextFile(kAfkPath, json)) return DocLoad::Corrupt;
    return DocLoad::Ok;
}

bool SongStore::saveAfk(const std::string& json) {
    return writeTextFile(kAfkPath, json);
}

}  // namespace vialucis
