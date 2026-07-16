#include "vialucis/frame_renderer.h"

#include <algorithm>

namespace vialucis {
namespace {

uint8_t maxChannel(const Rgb& c) { return std::max(c.r, std::max(c.g, c.b)); }

// A169 (§3-E item 11, unison-note color precedence): order-independent
// average, round-half-up per channel (matches scaleRgb's rounding
// convention). Used ONLY to resolve a same-layer, EQUAL-peak-brightness
// collision -- most commonly both hands' Due color landing on a note they
// strike in unison. Neither "whichever addDue() call happened first" nor
// "whichever track comes first in the MIDI file" is a real product
// decision the player can reason about, so blend the two colors instead of
// picking a side. Commutative (a+b == b+a), so the result NEVER depends on
// call order -- the deterministic part of "deterministic color rule."
Rgb blendRgb(const Rgb& a, const Rgb& b) {
    return {static_cast<uint8_t>((static_cast<uint16_t>(a.r) + b.r + 1) / 2),
            static_cast<uint8_t>((static_cast<uint16_t>(a.g) + b.g + 1) / 2),
            static_cast<uint8_t>((static_cast<uint16_t>(a.b) + b.b + 1) / 2)};
}

}  // namespace

void FrameRenderer::clear() {
    std::fill(frame_.begin(), frame_.end(), Rgb{});
    std::fill(layer_.begin(), layer_.end(), Layer::None);
}

void FrameRenderer::paint(uint8_t note, Rgb color, Layer layer) {
    LedRange range = table_.forNote(note);
    if (!range.valid) return;
    for (uint16_t i = range.first; i <= range.last && i < frame_.size(); ++i) {
        if (layer > layer_[i]) {
            frame_[i] = color;
            layer_[i] = layer;
            continue;
        }
        if (layer != layer_[i]) continue;
        uint8_t curMax = maxChannel(frame_[i]);
        uint8_t newMax = maxChannel(color);
        if (newMax > curMax) {
            frame_[i] = color;  // same layer, strictly brighter wins (e.g. a
                                 // nearer upcoming onset, "double upcoming")
        } else if (newMax == curMax) {
            // A169: equal peak brightness at the same layer -- a genuine tie
            // (e.g. both hands' Due color on a unison note). Blend rather
            // than arbitrarily keep whichever call landed first.
            frame_[i] = blendRgb(frame_[i], color);
        }
    }
}

void FrameRenderer::addUpcoming(uint8_t note, Rgb color, uint64_t onsetUs,
                                uint64_t nowUs) {
    if (onsetUs > nowUs + ramp_.leadUs) return;  // beyond the preview horizon
    float fraction;
    if (onsetUs <= nowUs) {
        fraction = 1.0f;  // due or past: ramp holds at the cap; 100% is addDue's job
    } else {
        fraction = 1.0f - static_cast<float>(onsetUs - nowUs) /
                              static_cast<float>(ramp_.leadUs);
    }
    paint(note, scaleRgb(color, ramp_.previewCap * fraction), Layer::Ramp);
}

void FrameRenderer::addDue(uint8_t note, Rgb color) {
    paint(note, color, Layer::Due);
}

void FrameRenderer::addRepeatFill(uint8_t note, Rgb color) {
    paint(note, color, Layer::RepeatFill);
}

void FrameRenderer::addWrong(uint8_t note, Rgb color) {
    paint(note, color, Layer::Wrong);
}

}  // namespace vialucis
