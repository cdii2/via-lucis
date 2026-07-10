#include "vialucis/show_player.h"

#include <algorithm>

#include "vialucis/fx/afk_player.h"  // makeEffect
#include "vialucis/fx/fx_color.h"    // nblend
#include "vialucis/fx/fx_math.h"     // qadd8, scale8
#include "vialucis/fx/palette.h"     // stock palettes

namespace vialucis {
namespace {

using fx::kFxStepMs;

// The stock palette bank (SHOW-FORMAT §1 paletteRef 0x80|n). Order matches
// paletteByName()'s stock set; n out of range -> effect default (nullptr).
const fx::Palette16* stockPalette(uint8_t n) {
    switch (n) {
        case 0: return &fx::rainbowColors();
        case 1: return &fx::oceanColors();
        case 2: return &fx::forestColors();
        case 3: return &fx::lavaColors();
        case 4: return &fx::partyColors();
        case 5: return &fx::cloudColors();
        case 6: return &fx::heatColors();
        default: return nullptr;
    }
}

// Fixed velocity for baked note bindings: the stream carries (onsetMs, note)
// only, so note-driven Presentation clips fire at full velocity (v1 choice).
constexpr uint8_t kBoundVelocity = 127;
// Fixed note-off latency for a bound note: noteOff fires 300 ms after onset
// or at the next same-key onset, whichever is earlier (v1 choice — the stream
// bakes onsets, not durations).
constexpr uint32_t kBoundHoldMs = 300;

bool cueActive(const ShowCue& c, uint32_t songMs) {
    if (songMs < c.startMs) return false;
    if (c.endMs == 0xFFFFFFFFu) return true;
    return songMs < c.endMs;
}

}  // namespace

void ShowPlayer::load(Show&& show, const KeyLedTable& table, uint32_t seed) {
    show_ = std::move(show);
    table_ = table;
    ledCount_ = table.ledCount();
    seed_ = seed;
    lastSongMs_ = 0;
    haveLast_ = false;

    // One Effect per DISTINCT effectIndex actually used by a cue.
    effects_.clear();
    effects_.resize(show_.effects.size());  // unique_ptr: value-init to null
    nd_.assign(show_.effects.size(), nullptr);
    for (const ShowCue& c : show_.cues) {
        size_t idx = c.effectIndex;
        if (idx >= effects_.size() || effects_[idx]) continue;  // already built
        const std::string& name = show_.effects[idx];
        std::unique_ptr<fx::Effect> e;
        if (name == "notedriven") {
            auto ndp = std::make_unique<fx::NoteDriven>();
            nd_[idx] = ndp.get();
            e = std::move(ndp);
        } else {
            e = fx::makeEffect(name);  // parse-validated non-null
        }
        if (e) {
            e->reset(seed_ ^ static_cast<uint32_t>(idx + 1), ledCount_);
            if (nd_[idx]) nd_[idx]->setTable(table_);
        }
        effects_[idx] = std::move(e);
    }

    // Precompute per-cue note-off times (clip-local ms) for note bindings.
    noteOffMs_.assign(show_.cues.size(), {});
    for (size_t ci = 0; ci < show_.cues.size(); ++ci) {
        const ShowCue& c = show_.cues[ci];
        std::vector<uint32_t>& offs = noteOffMs_[ci];
        offs.assign(c.binds.size(), 0);
        for (size_t k = 0; k < c.binds.size(); ++k) {
            uint32_t off = c.binds[k].onsetMs + kBoundHoldMs;
            for (size_t j = 0; j < c.binds.size(); ++j) {
                if (j == k) continue;
                if (c.binds[j].note == c.binds[k].note &&
                    c.binds[j].onsetMs > c.binds[k].onsetMs &&
                    c.binds[j].onsetMs < off)
                    off = c.binds[j].onsetMs;
            }
            offs[k] = off;
        }
    }

    scratch_.assign(ledCount_, Rgb{});
    mask_.assign(ledCount_, 0);
}

void ShowPlayer::driveNoteBinds(const ShowCue& cue, fx::NoteDriven* nd,
                                int64_t prevSongMs, uint32_t songMs) {
    const std::vector<uint32_t>* offs = nullptr;
    // Locate this cue's off-time table by identity (cues are index-aligned).
    for (size_t ci = 0; ci < show_.cues.size(); ++ci) {
        if (&show_.cues[ci] == &cue) { offs = &noteOffMs_[ci]; break; }
    }
    if (!offs) return;

    const int64_t start = static_cast<int64_t>(cue.startMs);
    const int64_t sp = static_cast<int64_t>(cue.speed);
    const int64_t curClip = (static_cast<int64_t>(songMs) - start) * sp / 16;
    const int64_t prevRel = prevSongMs - start;
    // prevClip = -1 on first activation so an onset at clip 0 still fires.
    const int64_t prevClip = prevRel < 0 ? -1 : prevRel * sp / 16;

    for (size_t k = 0; k < cue.binds.size(); ++k) {
        const int64_t onset = static_cast<int64_t>(cue.binds[k].onsetMs);
        if (onset > prevClip && onset <= curClip)
            nd->noteOn(cue.binds[k].note, kBoundVelocity);
        const int64_t off = static_cast<int64_t>((*offs)[k]);
        if (off > prevClip && off <= curClip)
            nd->noteOff(cue.binds[k].note);
    }
}

bool ShowPlayer::buildMask(const ShowCue& cue) {
    if (cue.scopeType == 0) return true;  // whole strip — mask unused
    std::fill(mask_.begin(), mask_.end(), static_cast<uint8_t>(0));
    auto paint = [&](uint8_t note) {
        LedRange r = table_.forNote(note);
        if (!r.valid) return;  // off-strip / >88 keys render dark, never crash
        uint16_t lo = std::min(r.first, r.last);
        uint16_t hi = std::max(r.first, r.last);
        for (uint16_t led = lo; led <= hi && led < mask_.size(); ++led)
            mask_[led] = 1;
    };
    switch (cue.scopeType) {
        case 1:
            for (int n = cue.rangeLo; n <= cue.rangeHi; ++n)
                paint(static_cast<uint8_t>(n));
            break;
        case 2:
            for (uint8_t n : cue.notes) paint(n);
            break;
        case 3:
            for (const NoteBind& b : cue.binds) paint(b.note);
            break;
        default:
            break;
    }
    return false;
}

const fx::Palette16* ShowPlayer::paletteForCue(const ShowCue& cue) const {
    uint8_t ref = cue.paletteRef;
    if (ref == 0xFF) return nullptr;               // effect default
    if (ref & 0x80) return stockPalette(ref & 0x7F);
    if (ref < show_.palettes.size()) return &show_.palettes[ref];
    return nullptr;
}

void ShowPlayer::renderAt(uint32_t songMs, std::vector<Rgb>& out) {
    if (out.size() < ledCount_) out.assign(ledCount_, Rgb{});
    std::fill(out.begin(), out.begin() + ledCount_, Rgb{});

    // Backward seek: re-reset every effect (their internal accumulators can't
    // rewind) and treat this as a fresh start.
    if (haveLast_ && songMs < lastSongMs_) {
        for (size_t i = 0; i < effects_.size(); ++i)
            if (effects_[i])
                effects_[i]->reset(seed_ ^ static_cast<uint32_t>(i + 1),
                                   ledCount_);
        haveLast_ = false;
    }
    const int64_t prevSong = haveLast_
        ? static_cast<int64_t>(lastSongMs_)
        : static_cast<int64_t>(songMs) - static_cast<int64_t>(kFxStepMs);

    for (const ShowCue& cue : show_.cues) {
        if (!cueActive(cue, songMs)) continue;
        fx::Effect* e = (cue.effectIndex < effects_.size())
                            ? effects_[cue.effectIndex].get()
                            : nullptr;
        if (!e) continue;

        if (const fx::Palette16* p = paletteForCue(cue)) e->setPalette(*p);

        fx::NoteDriven* nd = (cue.effectIndex < nd_.size())
                                 ? nd_[cue.effectIndex]
                                 : nullptr;
        if (nd && cue.drive == 1) driveNoteBinds(cue, nd, prevSong, songMs);

        // Clip-local time: (songMs - startMs) * speed/16 (song-time axis).
        const uint64_t clipMs =
            static_cast<uint64_t>(songMs - cue.startMs) * cue.speed / 16;
        fx::FxFrame f{scratch_, static_cast<uint32_t>(clipMs / kFxStepMs),
                      static_cast<uint32_t>(clipMs)};
        e->render(f);

        const bool all = buildMask(cue);
        const uint8_t op = cue.opacity;
        for (uint16_t i = 0; i < ledCount_; ++i) {
            if (!all && !mask_[i]) continue;
            const Rgb& src = scratch_[i];
            if (cue.blend == 1) {  // additive: qadd8 of opacity-scaled source
                out[i].r = fx::qadd8(out[i].r, fx::scale8(src.r, op));
                out[i].g = fx::qadd8(out[i].g, fx::scale8(src.g, op));
                out[i].b = fx::qadd8(out[i].b, fx::scale8(src.b, op));
            } else {  // opacity blend
                fx::nblend(out[i], src, op);
            }
        }
    }

    lastSongMs_ = songMs;
    haveLast_ = true;
}

}  // namespace vialucis
