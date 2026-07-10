#include "vialucis/key_led_table.h"

#include <cmath>

namespace vialucis {
namespace TableBuilder {
namespace {

// Fractional LED position of a point `mm` through the landmark knots
// (x = key-center mm, y = LED index; integer y = that LED's center).
// End segments extrapolate.
float posThroughKnots(float mm, const std::vector<float>& xs,
                      const std::vector<float>& ys) {
    size_t seg = 0;  // segment [seg, seg+1]
    while (seg + 2 < xs.size() && mm > xs[seg + 1]) ++seg;
    float x0 = xs[seg], x1 = xs[seg + 1];
    float y0 = ys[seg], y1 = ys[seg + 1];
    return y0 + (mm - x0) * (y1 - y0) / (x1 - x0);
}

}  // namespace

KeyLedTable fromTwoPoint(const LedMapConfig& cfg, bool reversed) {
    KeyLedTable t;
    t.setLedCount(cfg.ledCount);
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n)
        t.set(n, ledsForNote(n, cfg));
    return reversed ? mirrored(t) : t;
}

KeyLedTable mirrored(const KeyLedTable& t) {
    KeyLedTable out;
    uint16_t n = t.ledCount();
    out.setLedCount(n);
    for (uint8_t k = KeyLedTable::kFirstNote;
         k < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++k) {
        LedRange r = t.forNote(k);
        if (!r.valid) continue;
        LedRange m;
        m.first = static_cast<uint16_t>(n - 1 - r.last);
        m.last = static_cast<uint16_t>(n - 1 - r.first);
        m.valid = true;
        out.set(k, m);
    }
    return out;
}

TableError fromLandmarks(const std::vector<Landmark>& landmarks,
                         uint16_t ledCount, KeyLedTable& out) {
    if (landmarks.size() < 2) return TableError::TooFewLandmarks;
    for (const Landmark& m : landmarks) {
        if (m.note < KeyLedTable::kFirstNote ||
            m.note >= KeyLedTable::kFirstNote + KeyLedTable::kKeyCount)
            return TableError::BadLandmarkNote;
        if (m.led >= ledCount) return TableError::BadLandmarkLed;
    }
    for (size_t i = 1; i < landmarks.size(); ++i)
        if (landmarks[i].note <= landmarks[i - 1].note)
            return TableError::UnsortedLandmarks;
    // LED sequence must be strictly monotonic in ONE direction; descending
    // means the strip is mounted right-to-left (build canonical, mirror).
    bool descending = landmarks[1].led < landmarks[0].led;
    for (size_t i = 1; i < landmarks.size(); ++i) {
        bool down = landmarks[i].led < landmarks[i - 1].led;
        if (landmarks[i].led == landmarks[i - 1].led || down != descending)
            return TableError::DirectionMixed;
    }

    std::vector<float> xs, ys;
    xs.reserve(landmarks.size());
    ys.reserve(landmarks.size());
    for (const Landmark& m : landmarks) {
        xs.push_back(keyCenterMm(m.note));
        ys.push_back(static_cast<float>(
            descending ? (ledCount - 1 - m.led) : m.led));
    }

    KeyLedTable t;
    t.setLedCount(ledCount);
    long prevLast = -1;
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n) {
        const float center = keyCenterMm(n);
        const float half = 0.5f * keySlotWidthMm(n);
        const float left =
            posThroughKnots(center - half + kKeyEdgeMarginMm, xs, ys);
        const float right =
            posThroughKnots(center + half - kKeyEdgeMarginMm, xs, ys);

        long first = static_cast<long>(std::ceil(left));
        long last = static_cast<long>(std::floor(right));
        if (last < first) {  // coarse strip: nearest single LED
            long nearest = static_cast<long>(
                std::floor(posThroughKnots(center, xs, ys) + 0.5f));
            first = last = nearest;
        }
        if (last < 0 || first >= ledCount || first < 0 || last >= ledCount)
            continue;  // off-strip: valid=false ⇒ dark, as today
        if (first <= prevLast) {  // rounding collision with the key below
            first = prevLast + 1;
            if (first > last) continue;  // emptied: dark beats double-lit
        }
        LedRange r;
        r.first = static_cast<uint16_t>(first);
        r.last = static_cast<uint16_t>(last);
        r.valid = true;
        t.set(n, r);
        prevLast = last;
    }

    out = descending ? mirrored(t) : t;
    return TableError::None;
}

TableError validate(const KeyLedTable& t, uint8_t* badKey) {
    int dir = 0;  // 0 = not yet known, +1 ascending, -1 descending
    LedRange prev;
    bool havePrev = false;
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n) {
        LedRange r = t.forNote(n);
        if (!r.valid) continue;
        if (r.first > r.last || r.last >= t.ledCount()) {
            if (badKey) *badKey = n;
            return TableError::RangeOffStrip;
        }
        if (havePrev) {
            if (r.first > prev.last) {
                if (dir < 0) {
                    if (badKey) *badKey = n;
                    return TableError::DirectionMixed;
                }
                dir = 1;
            } else if (r.last < prev.first) {
                if (dir > 0) {
                    if (badKey) *badKey = n;
                    return TableError::DirectionMixed;
                }
                dir = -1;
            } else {
                if (badKey) *badKey = n;
                return TableError::Overlap;
            }
        }
        prev = r;
        havePrev = true;
    }
    return TableError::None;
}

}  // namespace TableBuilder
}  // namespace vialucis
