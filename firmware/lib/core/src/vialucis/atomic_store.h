#pragma once
// Atomic-persist decision logic (B4). Every persisted-doc write stages into a
// temp sibling, then renames it over the target — LittleFS rename replaces the
// destination atomically, so a reader (or a power loss) sees either the whole
// old file or the whole new one, never a truncated-in-place partial (R2: the
// old "open("w") + write" path wiped config on interruption).
//
// The SEQUENCE lives here as a pure template over injectable FS primitives so
// it is native-testable; only the real LittleFS syscalls (firmware/src/
// song_store.cpp) stay esp32-only.

#include <string>

namespace vialucis {

// The temp sibling a write stages into. ".tmp" is deliberately NOT a valid
// song/show name (validName/validShowName require *.mid / *.vls and a length
// floor), so a temp that survives a crash can never be mistaken for real
// content — the listings filter it and boot sweeps it.
inline std::string tmpPathFor(const std::string& path) { return path + ".tmp"; }

inline bool isTmpPath(const std::string& path) {
    return path.size() >= 4 && path.compare(path.size() - 4, 4, ".tmp") == 0;
}

// Stage -> rename -> cleanup-on-failure. `writeFn(tmp)` writes the bytes to the
// temp path (returns false on any short/failed write); `renameFn(tmp, dst)`
// moves it over the target (atomic on LittleFS); `removeFn(tmp)` discards a
// failed stage. Returns true ONLY when the rename lands — on any failure the
// temp is removed and the existing target is left untouched.
template <class WriteFn, class RenameFn, class RemoveFn>
bool atomicPersist(const std::string& dst, WriteFn writeFn, RenameFn renameFn,
                   RemoveFn removeFn) {
    const std::string tmp = tmpPathFor(dst);
    if (!writeFn(tmp)) {
        removeFn(tmp);  // a partial temp must never linger
        return false;
    }
    if (!renameFn(tmp, dst)) {
        removeFn(tmp);
        return false;
    }
    return true;
}

}  // namespace vialucis
