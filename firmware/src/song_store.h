#pragma once
// LittleFS persistence: songs under /songs/, settings at /settings.json.

#include <cstdint>
#include <string>
#include <vector>

#include "vialucis/atomic_store.h"    // tmp+rename atomic-persist seam (B4)
#include "vialucis/config_boot.h"     // DocLoad self-heal classification (B4)
#include "vialucis/settings.h"
#include "vialucis/storage_budget.h"  // FsHealth + block/quota math

// Forward-declare so the header stays free of the Arduino FS include; the
// streaming-upload seam hands back an open handle by reference (defined in
// the .cpp, which includes <FS.h>).
namespace fs {
class File;
}

namespace vialucis {

struct SongFileInfo {
    std::string name;
    size_t size;
};

class SongStore {
public:
    static constexpr size_t kMaxSongBytes = 256 * 1024;

    bool begin();  // mounts LittleFS (recreates song/show dirs)

    std::vector<SongFileInfo> list();
    bool save(const std::string& name, const uint8_t* data, size_t len);
    bool appendChunk(const std::string& name, const uint8_t* data, size_t len,
                     bool first);  // streaming upload
    // Streaming upload, single-handle (A2): open ONE File for the whole
    // transfer instead of re-opening per TCP chunk (which cost a LittleFS
    // metadata commit each and starved the async_tcp task into RSTs). The
    // caller holds the handle across chunks and closes it on completion.
    // Returns false (and leaves `out` invalid) on a bad name or a failed
    // create — a create failure flips FsHealth to Wedged.
    //
    // Atomic upload (B4): the handle opens a ".tmp" sibling, so an interrupted
    // or failed transfer NEVER truncates an existing same-name file. The caller
    // writes chunks into the tmp, then commitUpload() renames it over the target
    // (atomic) on the last chunk, or abortUpload() removes the tmp on
    // disconnect/write-fail. The old good file survives every failure path.
    bool openUpload(const std::string& name, fs::File& out);       // songs
    bool openShowUpload(const std::string& name, fs::File& out);   // shows
    bool commitUpload(const std::string& name);      // rename tmp -> song
    void abortUpload(const std::string& name);        // discard the tmp
    bool commitShowUpload(const std::string& name);  // rename tmp -> show
    void abortShowUpload(const std::string& name);    // discard the tmp
    bool remove(const std::string& name);
    bool read(const std::string& name, std::vector<uint8_t>& out);

    // Rename a song (REC4: rename a recorded take, general-purpose). Typed so
    // the REST layer maps to 400 / 404 / 409. Both names are validated.
    enum class RenameResult : uint8_t { Ok, BadName, NotFound, Exists };
    RenameResult rename(const std::string& from, const std::string& to);

    // The next free "recording-<n>.mid" name (REC4): scans /songs and defers
    // the integer logic to the native-tested nextRecordingName() helper.
    std::string nextRecordingName();

    // Free LittleFS bytes — the pre-arm free-space check (REC4). 0 if the FS
    // is unavailable.
    size_t freeBytes();
    // Capacity telemetry for /api/status + /api/songs (A3/T4). 0 if the FS is
    // unavailable.
    size_t totalBytes();
    size_t usedBytes();

    // Filesystem health (A3, ruling §6-2): Mounted / MountFailed / Wedged.
    // begin() sets it; a create failure during an upload flips it to Wedged;
    // format() resets it. Surfaced as fs:"ok"|"error" in /api/status.
    FsHealth fsHealth() const { return health_; }

    // Explicit, guarded recovery (A3, ruling §6-2): wipe LittleFS and re-mount.
    // Destruction is a user decision through this seam, never a boot reflex.
    // LittleFS.format() blocks for seconds — call from the loop task, never on
    // async_tcp mid-response. Returns true when the FS is Mounted afterwards.
    bool format();

    // Config-doc loads return a DocLoad (B4 self-heal): Absent (first boot /
    // v1 upgrade path), Corrupt (present but unreadable, unknown-higher schema,
    // or — for settings — unparseable), or Ok. saveSettings stamps schema and
    // writes atomically; false = the flash write failed (propagates to 507).
    DocLoad loadSettings(Settings& s);
    bool saveSettings(const Settings& s);

    // Calibration document, stored as raw JSON at /calibration.json (C3).
    // Absent ⇒ the caller seeds from the settings' 2-point values (upgrade
    // path); Corrupt ⇒ self-heal to defaults. saveCalibration returns false on
    // a failed atomic write.
    DocLoad loadCalibration(std::string& json);
    bool saveCalibration(const std::string& json);

    // AFK playlist document at /afk.json (E3). Absent ⇒ defaults. saveAfk
    // returns false on a failed atomic write.
    DocLoad loadAfk(std::string& json);
    bool saveAfk(const std::string& json);

    // Shows under /shows/ (P-wave; SHOW-FORMAT.md quotas: 64KB per show,
    // 384KB / 16 shows total — the REST layer maps these to 413/507).
    static constexpr size_t kMaxShowBytes = 64 * 1024;
    static constexpr size_t kMaxShowTotalBytes = 384 * 1024;
    static constexpr size_t kMaxShowCount = 16;
    static bool validShowName(const std::string& name);  // *.vls
    std::vector<SongFileInfo> listShows();
    size_t showTotalBytes();
    // Size of an existing show by name (0 if absent) — the net-delta term in
    // the upload quota so an edit -> re-save doesn't double-count itself (A2).
    size_t showSize(const std::string& name);
    bool showExists(const std::string& name);
    bool appendShowChunk(const std::string& name, const uint8_t* data,
                         size_t len, bool first);
    bool readShow(const std::string& name, std::vector<uint8_t>& out);
    bool removeShow(const std::string& name);

    // Keep names boring: alnum, dash, underscore, dot; must end ".mid".
    static bool validName(const std::string& name);

private:
    std::string songPath(const std::string& name) const;
    FsHealth health_ = FsHealth::MountFailed;  // until begin() succeeds
};

}  // namespace vialucis
