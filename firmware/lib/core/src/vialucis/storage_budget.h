#pragma once
// Storage-budget math for the upload precheck (A2) and the FS health signal
// (A3, ruling §6-2). Pure decisions only — the device side (SongStore /
// web_server) owns LittleFS and the reply; the block/quota/health rules live
// here so they can be tested natively. The wedge that bricked the device
// (dir-metadata split needs a free block at hard-full) is exactly what the
// block-aware reserve below is meant to keep out of reach.

#include <cstddef>
#include <cstdint>

namespace vialucis {

// LittleFS allocates whole blocks; a file's real footprint is its size rounded
// up to a block, not its raw byte count.
constexpr size_t kFsBlockBytes = 4096;

// Safety headroom kept free above every upload so a metadata/dir split always
// has a spare block — this is the constant that prevents the hard-full wedge.
// 32 KB = 8 blocks; comfortably above the 16 KB floor the plan calls for and
// in the same spirit as record's own free-space margin.
constexpr size_t kUploadReserveBytes = 32 * 1024;

// Round a byte count up to whole LittleFS blocks.
constexpr size_t roundUpToBlock(size_t bytes, size_t block = kFsBlockBytes) {
    return block == 0 ? bytes : ((bytes + block - 1) / block) * block;
}

// A fresh upload of `incoming` bytes fits when its block-rounded footprint plus
// the safety reserve still fits in `freeBytes` (LittleFS free space right now).
// Conservative on purpose: it treats every upload as new (an overwrite that
// frees the old file only makes the real outcome roomier).
constexpr bool uploadFits(size_t incoming, size_t freeBytes,
                          size_t reserve = kUploadReserveBytes,
                          size_t block = kFsBlockBytes) {
    return roundUpToBlock(incoming, block) + reserve <= freeBytes;
}

// A180 (hardware bring-up hotfix): reading a whole song into RAM (the
// GET /api/songs parse-check and song load both do this) must be REFUSED,
// never attempted, when the largest allocatable heap block can't hold it
// plus working margin. On this platform a std::vector allocation failure
// aborts (exceptions disabled), and the read runs on the async_tcp task —
// so an oversized read is a guaranteed crash+reboot mid-request, not an
// error path. Proven live 2026-07-16: a 105 KB song against ~80 KB free
// heap crash-looped every list call. The margin covers the parse's own
// side allocations riding alongside the file buffer.
constexpr size_t kReadHeapMarginBytes = 16 * 1024;
constexpr bool wholeFileReadFits(size_t fileBytes, size_t maxAllocBytes) {
    return fileBytes + kReadHeapMarginBytes <= maxAllocBytes;
}

// A182: reading is not enough — PARSING a MIDI expands it in RAM (the
// notes vector holds ~12-16 B per event vs ~3-4 disk bytes, plus vector
// doubling transients), and with exceptions disabled in our TUs a failed
// mid-parse allocation is an uncatchable abort (proven live via decoded
// backtrace: parseMidi → push_back → operator new → std::bad_alloc →
// terminate). Budget the whole job: file buffer + expansion must fit the
// largest allocatable block with margin. Factor 4 ≈ 1× file + 3× parsed
// notes, deliberately conservative; a file this refuses is almost
// certainly beyond the device's RAM ceiling to LOAD as well (the
// documented out-of-scope streaming-parse case). BRING-UP-TUNABLE (§6-6
// spirit): measure real expansion on hardware and tighten.
constexpr size_t kParseExpansionFactor = 4;
constexpr bool parseWorkFits(size_t fileBytes, size_t maxAllocBytes) {
    return fileBytes * kParseExpansionFactor + kReadHeapMarginBytes <=
           maxAllocBytes;
}

// Show total-bytes quota with net-delta: overwriting a same-name show frees its
// old bytes, so only the delta counts against the cap. `existingSameName` = the
// bytes of the show being replaced (0 for a brand-new name). Without this, the
// edit -> re-save loop double-counts and blocks itself at the cap.
constexpr bool showQuotaFits(size_t existingTotal, size_t existingSameName,
                             size_t incoming, size_t totalCap) {
    size_t base =
        existingTotal >= existingSameName ? existingTotal - existingSameName : 0;
    return base + incoming <= totalCap;
}

// Show count cap: a new name consumes a slot; overwriting an existing show does
// not, so a re-save at the count limit must still be allowed.
constexpr bool showCountOk(size_t existingCount, bool nameExists,
                           size_t maxCount) {
    return nameExists || existingCount < maxCount;
}

// Filesystem health surfaced in /api/status (ruling §6-2). MountFailed = the FS
// would not mount; Wedged = mounted but a file create fails (the dir-metadata
// hard-full signature seen live); Mounted = healthy.
enum class FsHealth : uint8_t { Mounted, MountFailed, Wedged };

// The health transition rule, as a pure function so the mapping is tested once.
constexpr FsHealth classifyFsHealth(bool mounted, bool canCreate) {
    if (!mounted) return FsHealth::MountFailed;
    if (!canCreate) return FsHealth::Wedged;
    return FsHealth::Mounted;
}

// /api/status surface: healthy -> "ok", any fault -> "error" (§6-2).
constexpr const char* fsHealthField(FsHealth h) {
    return h == FsHealth::Mounted ? "ok" : "error";
}

}  // namespace vialucis
