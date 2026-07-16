#pragma once
// Per-key LED table (v2 C-wave, DESIGN-lightshow §5) — THE geometry
// primitive. Renderer, effects, and presentation note-binding all read this
// table; nothing downstream re-derives geometry (VL1). The v1 cluster-model
// formula (key_led_map.h) survives only as the math behind the 2-point
// builder. The calibration tiers/wizards are just different ways to fill it.

#include <array>
#include <cstdint>
#include <vector>

#include "vialucis/key_led_map.h"

namespace vialucis {

// One measured calibration point: "this key sits under this LED" —
// the wizard's (LED ↔ note) primitive (DESIGN-lightshow §5).
struct Landmark {
    uint8_t note = 0;
    uint16_t led = 0;
};

// Typed table/builder failures — C3 maps these to REST 400 messages.
enum class TableError : uint8_t {
    None,
    TooFewLandmarks,   // multi-point needs >= 2
    BadLandmarkNote,   // outside the 88 keys
    BadLandmarkLed,    // outside the strip
    UnsortedLandmarks, // notes must strictly ascend
    DirectionMixed,    // LED sequence must be strictly one-directional
    RangeOffStrip,     // an entry escapes [0, ledCount) or is inverted
    Overlap,           // adjacent (valid) keys share LEDs
    TooFewKeys,        // B6b: a perKey PUT needs >= 2 distinct populated keys
                       // (an empty or single-key body would otherwise
                       // validate() as a "sparse but structurally fine"
                       // table and silently blank the whole strip — that
                       // acceptance is correct for revalidating an
                       // already-stored/self-healed doc, wrong for a fresh
                       // wizard submission, so this is enforced at the
                       // Calibration::fromJson API boundary, not inside
                       // TableBuilder::validate()).
};

class KeyLedTable {
public:
    static constexpr uint8_t kFirstNote = 21;  // A0
    static constexpr uint8_t kKeyCount = 88;

    // Default: zero LEDs, every key invalid — real tables come from builders.
    KeyLedTable() = default;

    LedRange forNote(uint8_t midiNote) const {
        if (midiNote < kFirstNote ||
            midiNote >= kFirstNote + kKeyCount)
            return LedRange{};
        return ranges_[midiNote - kFirstNote];
    }

    // Same, with first <= last guaranteed (reversed tables store descending
    // ranges) — THE normalization every mask/span consumer uses (P-wave
    // closing review: AfkPlayer and ShowPlayer each hand-rolled it).
    LedRange forNoteOrdered(uint8_t midiNote) const {
        LedRange r = forNote(midiNote);
        if (r.valid && r.first > r.last) {
            uint16_t t = r.first;
            r.first = r.last;
            r.last = t;
        }
        return r;
    }

    void set(uint8_t midiNote, const LedRange& r) {
        if (midiNote < kFirstNote || midiNote >= kFirstNote + kKeyCount)
            return;
        ranges_[midiNote - kFirstNote] = r;
    }

    uint16_t ledCount() const { return ledCount_; }
    void setLedCount(uint16_t n) { ledCount_ = n; }

private:
    std::array<LedRange, kKeyCount> ranges_{};
    uint16_t ledCount_ = 0;
};

namespace TableBuilder {

// The 2-point tier: the old two-setting math (offsetMm + ledsPerMeter)
// filling the table — integer-identical to calling the v1 formula per key
// when `reversed` is false. `reversed` mirrors the built table for
// right-to-left strip installations (LED 0 at the treble end).
KeyLedTable fromTwoPoint(const LedMapConfig& cfg, bool reversed = false);

// The multi-point tier: piecewise-linear interpolation between measured
// (note, led) landmarks — key-center mm (standard cluster model as the
// shape prior) maps to fractional LED position through the landmark knots;
// end segments extrapolate. A strictly DESCENDING led sequence is a
// reversed mount and mirrors the result. Adjacent-key collisions after
// rounding shrink toward ascending order (a key emptied by the shrink goes
// valid=false — too-coarse strips light one key rather than two wrong ones).
TableError fromLandmarks(const std::vector<Landmark>& landmarks,
                         uint16_t ledCount, KeyLedTable& out);

// The invariants every stored/PUT table must satisfy (C-wave charter):
// in-strip, one consistent direction, adjacent valid keys never overlap.
// Gaps (valid=false keys) are fine — they render dark, as today.
// On failure `badKey` (if given) names the offending MIDI note.
TableError validate(const KeyLedTable& t, uint8_t* badKey = nullptr);

// The table mirrored across the strip (index i -> ledCount-1-i).
KeyLedTable mirrored(const KeyLedTable& t);

}  // namespace TableBuilder

}  // namespace vialucis
