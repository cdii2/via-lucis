#pragma once
// GET /api/songs "parseOk" per-song flag (§3-E item 12, A164 — the queued
// Wave C ask). Re-parsing every song's whole MIDI file on every poll
// (~2x/s while the Songs screen is open) is far too hot for a device-side
// JSON listing, so the parse status is cached in RAM per boot, keyed by
// (name, size). A re-upload/overwrite always changes the stored byte size
// (LittleFS writes are exact-byte; B4's atomic upload never leaves a
// partial-length file in place), so a size mismatch is a reliable "this
// file changed underneath the cache, recompute" signal without needing a
// content hash.
//
// Pure data structure — no file IO, no MIDI parsing — so the recompute
// DECISION is native-tested here; the actual parse (reading bytes off
// LittleFS + parseMidi) is the esp32-only glue in App::songsForList().

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace vialucis {

class SongParseCache {
public:
    // True when `name` has no cached result for exactly this `size` — an
    // unseen name, or a size that doesn't match what's cached (the file
    // changed since the cache last saw it).
    bool needsRecompute(const std::string& name, size_t size) const;

    // Record the parse result for (name, size). Overwrites any prior entry
    // for `name` (a size change always supersedes the old one).
    void set(const std::string& name, size_t size, bool parseOk);

    // The cached result for `name`. Only meaningful right after a
    // needsRecompute(name, size) check for the SAME size returned false;
    // returns false (the safe default: don't badge an unseen file "good")
    // for a name the cache has never seen.
    bool get(const std::string& name) const;

    // True when the cache holds ANY result for `name` (A183: the budgeted
    // warm-up needs "checked vs not-yet-checked" as a separate axis from
    // good-vs-bad, so an unchecked file can be reported as UNKNOWN — no
    // parseOk field on the wire — instead of falsely badged bad).
    bool has(const std::string& name) const;

    // Drop every entry whose name isn't in `currentNames` (deleted/renamed
    // songs) so the cache never grows across a device's uptime.
    void prune(const std::vector<std::string>& currentNames);

    size_t size() const { return entries_.size(); }  // test observability

private:
    struct Entry {
        size_t size;
        bool parseOk;
    };
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace vialucis
