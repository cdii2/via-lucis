#include "vialucis/show.h"

#include <algorithm>

#include "vialucis/fx/afk_player.h"  // effectNames() — the E-wave registry

namespace vialucis {
namespace {

// A bounds-checked forward reader over the raw stream. Any read past the end
// sets `bad` (mapped to Truncated by the caller) and yields zero.
struct Reader {
    const uint8_t* p;
    size_t len;
    size_t pos = 0;
    bool bad = false;

    // Overflow-safe on 32-bit targets: n is attacker-controlled wire data,
    // and pos+n can wrap size_t there (P-wave closing review).
    bool need(size_t n) const { return n <= len - pos && pos <= len; }

    uint8_t u8() {
        if (!need(1)) { bad = true; return 0; }
        return p[pos++];
    }
    uint16_t u16() {
        if (!need(2)) { bad = true; return 0; }
        uint16_t v = static_cast<uint16_t>(p[pos]) |
                     static_cast<uint16_t>(p[pos + 1]) << 8;
        pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) { bad = true; return 0; }
        uint32_t v = static_cast<uint32_t>(p[pos]) |
                     static_cast<uint32_t>(p[pos + 1]) << 8 |
                     static_cast<uint32_t>(p[pos + 2]) << 16 |
                     static_cast<uint32_t>(p[pos + 3]) << 24;
        pos += 4;
        return v;
    }
};

ShowResult mk(ShowResult::Kind k) {
    ShowResult r;
    r.kind = k;
    return r;
}

// "notedriven" is a valid stream effect name (SHOW-FORMAT §1 EFFECTS) even
// though makeEffect()/knownEffect() don't register it — the player maps it to
// fx::NoteDriven directly. Accept it here so streams validate.
bool effectNameValid(const std::string& name) {
    if (name == "notedriven") return true;
    for (const std::string& n : fx::effectNames())
        if (n == name) return true;
    return false;
}

// META section: u8 clockSource · u32 durationMs · zero-terminated name.
ShowResult parseMeta(Reader& r, size_t end, ShowMeta& meta) {
    uint8_t clock = r.u8();
    uint32_t dur = r.u32();
    if (r.bad || r.pos > end) return mk(ShowResult::Kind::Truncated);
    if (clock == 2) return mk(ShowResult::Kind::ScoreFollowUnsupported);
    if (clock > 2) return mk(ShowResult::Kind::BadSection);
    meta.clockSource = clock;
    meta.durationMs = dur;
    meta.name.clear();
    while (r.pos < end) {
        uint8_t c = r.u8();
        if (c == 0) break;
        if (meta.name.size() >= 48)  // the documented cap IS the contract
            return mk(ShowResult::Kind::BadSection);
        meta.name.push_back(static_cast<char>(c));
    }
    return mk(ShowResult::Kind::Ok);
}

// EFFECTS section: u8 count × zero-terminated factory name.
ShowResult parseEffects(Reader& r, size_t end, std::vector<std::string>& out) {
    uint8_t count = r.u8();
    if (r.bad) return mk(ShowResult::Kind::Truncated);
    out.clear();
    for (uint8_t i = 0; i < count; ++i) {
        std::string name;
        bool terminated = false;
        while (r.pos < end) {
            uint8_t c = r.u8();
            if (c == 0) { terminated = true; break; }
            name.push_back(static_cast<char>(c));
        }
        if (!terminated) return mk(ShowResult::Kind::BadSection);
        if (!effectNameValid(name)) return mk(ShowResult::Kind::UnknownEffect);
        out.push_back(std::move(name));
    }
    return mk(ShowResult::Kind::Ok);
}

// PALETTES section: u8 count × 16×3 RGB bytes.
ShowResult parsePalettes(Reader& r, size_t end,
                         std::vector<fx::Palette16>& out) {
    uint8_t count = r.u8();
    if (r.bad) return mk(ShowResult::Kind::Truncated);
    out.clear();
    for (uint8_t i = 0; i < count; ++i) {
        if (r.pos + 48 > end) return mk(ShowResult::Kind::BadSection);
        fx::Palette16 pal;
        for (int e = 0; e < 16; ++e) {
            pal.entries[e].r = r.u8();
            pal.entries[e].g = r.u8();
            pal.entries[e].b = r.u8();
        }
        out.push_back(pal);
    }
    return mk(ShowResult::Kind::Ok);
}

// CUES section: u16 count × cue record (16-byte head + scope payload). Bounds
// against effects/palettes are validated in a second pass so section ORDER
// doesn't matter (EFFECTS may legally follow CUES).
ShowResult parseCues(Reader& r, size_t end, std::vector<ShowCue>& out) {
    uint16_t count = r.u16();
    if (r.bad) return mk(ShowResult::Kind::Truncated);
    // The count is wire data: a 2-byte section claiming 0xFFFF cues must
    // not reserve megabytes before the per-cue checks run (P-wave closing
    // review). 16 bytes is a cue's minimum encoding.
    if (static_cast<size_t>(count) * 16 > end - r.pos)
        return mk(ShowResult::Kind::BadCue);
    out.clear();
    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (r.pos + 16 > end) return mk(ShowResult::Kind::BadCue);
        ShowCue c;
        c.startMs = r.u32();
        c.endMs = r.u32();
        c.effectIndex = r.u8();
        c.blend = r.u8();
        c.opacity = r.u8();
        c.drive = r.u8();
        c.scopeType = r.u8();
        c.paletteRef = r.u8();
        c.speed = r.u8();
        c.flags = r.u8();
        switch (c.scopeType) {
            case 0:
                break;
            case 1: {
                if (r.pos + 2 > end) return mk(ShowResult::Kind::BadCue);
                c.rangeLo = r.u8();
                c.rangeHi = r.u8();
                break;
            }
            case 2: {
                if (r.pos + 1 > end) return mk(ShowResult::Kind::BadCue);
                uint8_t n = r.u8();
                if (r.pos + n > end) return mk(ShowResult::Kind::BadCue);
                c.notes.reserve(n);
                for (uint8_t k = 0; k < n; ++k) c.notes.push_back(r.u8());
                break;
            }
            case 3: {
                if (r.pos + 2 > end) return mk(ShowResult::Kind::BadCue);
                uint16_t n = r.u16();
                if (r.pos + static_cast<size_t>(n) * 5 > end)
                    return mk(ShowResult::Kind::BadCue);
                c.binds.reserve(n);
                for (uint16_t k = 0; k < n; ++k) {
                    NoteBind nb;
                    nb.onsetMs = r.u32();
                    nb.note = r.u8();
                    c.binds.push_back(nb);
                }
                break;
            }
            default:
                // Unknown scopeType: payload size is unknowable — reject
                // (unlike unknown SECTIONS/flags, which are length-skippable).
                return mk(ShowResult::Kind::BadCue);
        }
        out.push_back(std::move(c));
    }
    return mk(ShowResult::Kind::Ok);
}

}  // namespace

const char* ShowResult::message() const {
    switch (kind) {
        case Kind::Ok: return "";
        case Kind::BadMagic: return "bad show magic";
        case Kind::NewerMajor: return "show needs newer firmware";
        case Kind::Truncated: return "show truncated";
        case Kind::BadSection: return "bad show section";
        case Kind::UnknownEffect: return "unknown effect";
        case Kind::ScoreFollowUnsupported: return "score-follow not supported yet";
        case Kind::BadCue: return "bad cue";
        case Kind::TooLarge: return "show too large";
    }
    return "bad show";
}

ShowResult Show::parse(const uint8_t* data, size_t len, Show& out) {
    out = Show{};
    if (len > kMaxBytes) return mk(ShowResult::Kind::TooLarge);
    if (data == nullptr || len < 8) return mk(ShowResult::Kind::Truncated);
    if (!(data[0] == 'V' && data[1] == 'L' && data[2] == 'S' &&
          data[3] == '1'))
        return mk(ShowResult::Kind::BadMagic);
    uint8_t versionMajor = data[4];
    // data[5] = versionMinor — ignored (minor bumps are backward-compatible).
    if (versionMajor > kVersionMajor) return mk(ShowResult::Kind::NewerMajor);

    Reader r{data, len};
    r.pos = 6;
    uint16_t sectionCount = r.u16();

    for (uint16_t s = 0; s < sectionCount; ++s) {
        if (!r.need(6)) return mk(ShowResult::Kind::Truncated);
        uint16_t type = r.u16();
        uint32_t byteLen = r.u32();
        if (type == 0xFFFF) break;  // END — must be last; stop.
        if (!r.need(byteLen)) return mk(ShowResult::Kind::Truncated);
        size_t sectionEnd = r.pos + byteLen;

        ShowResult sub = mk(ShowResult::Kind::Ok);
        switch (type) {
            case 1: sub = parseMeta(r, sectionEnd, out.meta); break;
            case 2: sub = parseEffects(r, sectionEnd, out.effects); break;
            case 3: sub = parsePalettes(r, sectionEnd, out.palettes); break;
            case 4: sub = parseCues(r, sectionEnd, out.cues); break;
            default: break;  // unknown SECTION type — skipped by length (OV4)
        }
        if (!sub.ok()) return sub;
        r.pos = sectionEnd;  // resync to declared bounds (skip any padding)
    }

    // Second pass: bounds now that EFFECTS/PALETTES are fully known.
    for (const ShowCue& c : out.cues) {
        if (c.effectIndex >= out.effects.size())
            return mk(ShowResult::Kind::BadCue);
        if (c.endMs != 0xFFFFFFFFu && c.endMs <= c.startMs)
            return mk(ShowResult::Kind::BadCue);
        if (c.paletteRef != 0xFF && !(c.paletteRef & 0x80)) {
            if (c.paletteRef >= out.palettes.size())
                return mk(ShowResult::Kind::BadCue);
        }
    }

    std::stable_sort(out.cues.begin(), out.cues.end(),
                     [](const ShowCue& a, const ShowCue& b) {
                         return a.startMs < b.startMs;
                     });
    return mk(ShowResult::Kind::Ok);
}

}  // namespace vialucis
