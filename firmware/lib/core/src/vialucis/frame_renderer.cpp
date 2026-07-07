#include "vialucis/frame_renderer.h"

#include <algorithm>

namespace vialucis {
namespace {

Rgb scale(Rgb c, float k) {
    return {static_cast<uint8_t>(c.r * k + 0.5f),
            static_cast<uint8_t>(c.g * k + 0.5f),
            static_cast<uint8_t>(c.b * k + 0.5f)};
}

uint8_t maxChannel(const Rgb& c) { return std::max(c.r, std::max(c.g, c.b)); }

}  // namespace

void FrameRenderer::clear() {
    std::fill(frame_.begin(), frame_.end(), Rgb{});
    std::fill(layer_.begin(), layer_.end(), Layer::None);
}

void FrameRenderer::paint(uint8_t note, Rgb color, Layer layer) {
    LedRange range = ledsForNote(note, map_);
    if (!range.valid) return;
    for (uint16_t i = range.first; i <= range.last && i < frame_.size(); ++i) {
        if (layer > layer_[i]) {
            frame_[i] = color;
            layer_[i] = layer;
        } else if (layer == layer_[i] &&
                   maxChannel(color) > maxChannel(frame_[i])) {
            frame_[i] = color;  // same layer: brighter wins (double upcoming)
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
    paint(note, scale(color, ramp_.previewCap * fraction), Layer::Ramp);
}

void FrameRenderer::addDue(uint8_t note, Rgb color) {
    paint(note, color, Layer::Due);
}

void FrameRenderer::addWrong(uint8_t note, Rgb color) {
    paint(note, color, Layer::Wrong);
}

}  // namespace vialucis
