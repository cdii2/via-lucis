#pragma once
// Chunk-boundary decisions for the REST body intake (R6). ESPAsyncWebServer
// delivers request bodies as (data, len, index, total) chunks; both sinks —
// buffer-then-parse (JSON routes) and stream-to-store (song upload) — take
// their first/last/too-large decisions from here instead of hand-rolling
// the comparisons. Pure math, natively tested; the device side owns the
// buffers and the reply.

#include <cstddef>

namespace vialucis {

struct ChunkPlan {
    bool tooLarge;  // the announced total exceeds the cap: reject, buffer nothing
    bool first;     // start of a new body
    bool last;      // body complete after this chunk
};

constexpr ChunkPlan planChunk(size_t index, size_t len, size_t total,
                              size_t cap) {
    return {total > cap, index == 0, index + len >= total};
}

}  // namespace vialucis
