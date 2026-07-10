#pragma once
// Shared frame-inspection helpers for the fx/director suites — one
// definition (the E-wave closing review found byte-identical copies).

#include <cstdint>
#include <vector>

#include "vialucis/frame_renderer.h"

namespace fxtest {

inline int litCount(const std::vector<vialucis::Rgb>& f) {
    int n = 0;
    for (const vialucis::Rgb& c : f)
        if (c.r || c.g || c.b) ++n;
    return n;
}

inline uint8_t maxChan(const std::vector<vialucis::Rgb>& f) {
    uint8_t m = 0;
    for (const vialucis::Rgb& c : f) {
        if (c.r > m) m = c.r;
        if (c.g > m) m = c.g;
        if (c.b > m) m = c.b;
    }
    return m;
}

}  // namespace fxtest
