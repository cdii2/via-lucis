#pragma once
// The baked cue stream (`.vls`) parser — THE binary contract from
// docs/SHOW-FORMAT.md §1 (P1 device player). The editor compiles arbitrarily
// deep authoring into a FLAT, song-position cue stream; the ESP32 only ever
// interprets this parsed form ("the editor is not the player" — iron rule).
//
// Compatibility discipline (2A / OV4):
//   - REFUSE versionMajor > ours (NewerMajor) — never garbage rendering.
//   - Unknown SECTION types are SKIPPED via their length prefix.
//   - Unknown cue FLAG bits are ignored (forward-compat).
// Times are song-time milliseconds (u32), little-endian throughout.
//
// Parse is pure/native — no flash, no allocation caps beyond the 64 KB
// whole-load rule (SHOW-FORMAT §2: a playing show lives in a reserved
// buffer). ShowResult mirrors CalibResult's typed-kind + message() style.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "vialucis/fx/palette.h"

namespace vialucis {

// A single baked (onsetMs, note) pair for a note-binding (scopeType 3) cue —
// the song's actual notes in the cue's span, baked by the editor.
struct NoteBind {
    uint32_t onsetMs = 0;
    uint8_t note = 0;
};

// One cue record (SHOW-FORMAT §1 "Cue record"). Fixed 16-byte head plus a
// scope payload decoded into whichever of the members below the scopeType
// selects.
struct ShowCue {
    uint32_t startMs = 0;
    uint32_t endMs = 0;        // 0xFFFFFFFF = open-ended / looping
    uint8_t effectIndex = 0;   // index into Show::effects
    uint8_t blend = 0;         // 0 = opacity, 1 = additive
    uint8_t opacity = 255;     // applied to the blend
    uint8_t drive = 0;         // 0 = autonomous, 1 = note-driven
    uint8_t scopeType = 0;     // 0 all · 1 pitch range · 2 note set · 3 bind
    uint8_t paletteRef = 0xFF; // 0xFF default · 0x80|n stock n · else PALETTES
    uint8_t speed = 16;        // fixed-point ×16 (16 = 1.0)
    uint8_t flags = 0;         // reserved; unknown bits ignored

    // Scope payload (only the one selected by scopeType is populated):
    uint8_t rangeLo = 0, rangeHi = 0;  // type 1: MIDI note range (inclusive)
    std::vector<uint8_t> notes;        // type 2: explicit note set
    std::vector<NoteBind> binds;       // type 3: baked note bindings
};

struct ShowMeta {
    uint8_t clockSource = 0;   // 0 demo · 1 freeRun · 2 scoreFollow (P4)
    uint32_t durationMs = 0;
    std::string name;          // <= 48 bytes
    // Optional trailing META byte (P4 frozen contract): the score-follow
    // track index. Emitted by the editor only for clockSource==2; absent ⇒
    // 0xFF = auto (the play-time mask resolver picks the default scope,
    // A54). 0..N = a track index in the existing track space.
    uint8_t followTrack = 0xFF;
};

// Result of parse(): a typed kind with stable REST-body text (message()).
struct ShowResult {
    enum class Kind : uint8_t {
        Ok,
        BadMagic,
        NewerMajor,             // versionMajor > firmware's
        Truncated,              // ran off the end of the buffer
        BadSection,             // a known section is internally malformed
        UnknownEffect,          // EFFECTS names an unregistered factory
        BadCue,                 // a cue fails a bounds check
        TooLarge,               // len > 64 KB (the whole-load rule)
    };
    Kind kind = Kind::Ok;
    bool ok() const { return kind == Kind::Ok; }
    const char* message() const;  // stable text for REST 400 bodies
};

// The parsed show: the flat stream, ready for ShowPlayer. Cues are stored in
// stream order and STABLE-sorted by startMs (equal starts keep stream order —
// so the "later over earlier" z-order of overlapping same-start cues survives;
// SHOW-FORMAT §1 "Composition").
struct Show {
    static constexpr uint8_t kVersionMajor = 1;
    static constexpr size_t kMaxBytes = 65536;  // the 64 KB whole-load cap

    ShowMeta meta;
    std::vector<std::string> effects;      // EFFECTS factory names
    std::vector<fx::Palette16> palettes;   // custom PALETTES (16×3 RGB each)
    std::vector<ShowCue> cues;             // stream order, then stable by start

    // Parse a whole `.vls` stream. `out` is left in a partial state on
    // failure (callers key off the returned kind, never `out`).
    static ShowResult parse(const uint8_t* data, size_t len, Show& out);
};

}  // namespace vialucis
