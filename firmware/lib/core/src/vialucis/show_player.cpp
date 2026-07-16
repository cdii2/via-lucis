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

    // Precompute per cue (P-wave closing review — bounded per-frame work):
    // 1. the 88-key scope bitmap (mask building is O(88), never O(binds));
    // 2. the time-sorted note on/off event list + cursor (O(1) amortized —
    //    the repeat-cue cursor pattern; off = min(onset+hold, next same-key
    //    onset)).
    keyBits_.assign(show_.cues.size(), {});
    bindEvents_.assign(show_.cues.size(), {});
    bindCursor_.assign(show_.cues.size(), 0);
    for (size_t ci = 0; ci < show_.cues.size(); ++ci) {
        const ShowCue& c = show_.cues[ci];
        std::array<uint8_t, 11>& bits = keyBits_[ci];
        auto setBit = [&](uint8_t note) {
            if (note < 21 || note > 108) return;
            uint8_t k = static_cast<uint8_t>(note - 21);
            bits[k >> 3] |= static_cast<uint8_t>(1u << (k & 7));
        };
        if (c.scopeType == 1) {
            for (int n = c.rangeLo; n <= c.rangeHi; ++n)
                setBit(static_cast<uint8_t>(n));
        } else if (c.scopeType == 2) {
            for (uint8_t n : c.notes) setBit(n);
        } else if (c.scopeType == 3) {
            for (const NoteBind& b : c.binds) setBit(b.note);
        }

        std::vector<BindEvent>& evts = bindEvents_[ci];
        evts.reserve(c.binds.size() * 2);
        for (size_t k = 0; k < c.binds.size(); ++k) {
            uint32_t off = c.binds[k].onsetMs + kBoundHoldMs;
            for (size_t j = 0; j < c.binds.size(); ++j) {
                if (j == k) continue;
                if (c.binds[j].note == c.binds[k].note &&
                    c.binds[j].onsetMs > c.binds[k].onsetMs &&
                    c.binds[j].onsetMs < off)
                    off = c.binds[j].onsetMs;
            }
            evts.push_back({c.binds[k].onsetMs, c.binds[k].note, false});
            evts.push_back({off, c.binds[k].note, true});
        }
        std::sort(evts.begin(), evts.end(),
                  [](const BindEvent& a, const BindEvent& b) {
                      if (a.clipMs != b.clipMs) return a.clipMs < b.clipMs;
                      // Offs before ons at a tie: a shortened off lands
                      // exactly on the next same-key onset and must not
                      // release the re-struck note.
                      return a.off > b.off;
                  });
    }

    scratch_.assign(ledCount_, Rgb{});
    mask_.assign(ledCount_, 0);
}

void ShowPlayer::driveNoteBinds(size_t cueIndex, fx::NoteDriven* nd,
                                uint32_t songMs) {
    const ShowCue& cue = show_.cues[cueIndex];
    const std::vector<BindEvent>& evts = bindEvents_[cueIndex];
    size_t& cur = bindCursor_[cueIndex];

    const int64_t start = static_cast<int64_t>(cue.startMs);
    const int64_t sp = static_cast<int64_t>(cue.speed);
    const int64_t curClip = (static_cast<int64_t>(songMs) - start) * sp / 16;

    while (cur < evts.size() &&
           static_cast<int64_t>(evts[cur].clipMs) <= curClip) {
        if (evts[cur].off) nd->noteOff(evts[cur].note);
        else nd->noteOn(evts[cur].note, kBoundVelocity);
        ++cur;
    }
}

bool ShowPlayer::buildMask(size_t ci) {
    const ShowCue& cue = show_.cues[ci];
    if (cue.scopeType == 0) return true;  // whole strip — mask unused
    std::fill(mask_.begin(), mask_.end(), static_cast<uint8_t>(0));
    const std::array<uint8_t, 11>& bits = keyBits_[ci];
    for (uint8_t k = 0; k < 88; ++k) {
        if (!(bits[k >> 3] & (1u << (k & 7)))) continue;
        LedRange r = table_.forNoteOrdered(static_cast<uint8_t>(21 + k));
        if (!r.valid) continue;  // off-strip renders dark, never crash
        for (uint16_t led = r.first; led <= r.last && led < mask_.size();
             ++led)
            mask_[led] = 1;
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

    // Backward seek: re-reset every effect (their internal accumulators
    // can't rewind), rewind every bind cursor, fresh start.
    if (haveLast_ && songMs < lastSongMs_) {
        for (size_t i = 0; i < effects_.size(); ++i)
            if (effects_[i])
                effects_[i]->reset(seed_ ^ static_cast<uint32_t>(i + 1),
                                   ledCount_);
        std::fill(bindCursor_.begin(), bindCursor_.end(), size_t{0});
        haveLast_ = false;
    }

    for (size_t ci = 0; ci < show_.cues.size(); ++ci) {
        const ShowCue& cue = show_.cues[ci];
        if (!cueActive(cue, songMs)) continue;
        fx::Effect* e = (cue.effectIndex < effects_.size())
                            ? effects_[cue.effectIndex].get()
                            : nullptr;
        if (!e) continue;

        // A164 (§3-E item 6): paletteRef==0xFF means "this effect's own
        // default," not "whatever the last cue on this shared effect
        // instance happened to set" — without the explicit reset, a cue
        // that never names a palette would silently inherit an EARLIER
        // cue's palette (state bleed between cues sharing an effectIndex).
        if (const fx::Palette16* p = paletteForCue(cue)) e->setPalette(*p);
        else e->resetPalette();

        fx::NoteDriven* nd = (cue.effectIndex < nd_.size())
                                 ? nd_[cue.effectIndex]
                                 : nullptr;
        if (nd && cue.drive == 1) driveNoteBinds(ci, nd, songMs);

        // Clip-local time: (songMs - startMs) * speed/16 (song-time axis).
        const uint64_t clipMs =
            static_cast<uint64_t>(songMs - cue.startMs) * cue.speed / 16;
        fx::FxFrame f{scratch_, static_cast<uint32_t>(clipMs / kFxStepMs),
                      static_cast<uint32_t>(clipMs)};
        e->render(f);

        const bool all = buildMask(ci);
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
