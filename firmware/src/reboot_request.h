#pragma once
// B7 (BUGFIX-PLAN-2026-07-15 §3-B): reboot request flag.
//
// The old /api/reboot handler called delay(200) then ESP.restart() directly
// on the async_tcp task — blocking the whole network stack for 200ms on
// every reboot request. That handler (web_server.cpp) instead sets
// RebootRequest::pending and replies immediately (see the B7 report's ASKS
// FOR B4 section for the exact edit); main.cpp's loop() — the ONLY consumer
// — polls the flag on the loop task and performs the same ~200ms grace
// delay (so the HTTP reply has time to flush) plus the actual restart
// there, off the network task entirely.
//
// Plain namespace-scope inline statics (C++17, gnu++17 is already the
// project standard) rather than a new .cpp: this header is the whole seam,
// no linkage unit needed.

#include <atomic>
#include <cstdint>

namespace vialucis {

struct RebootRequest {
    // Set true to ask the loop task to reboot; read/consumed only by
    // main.cpp's loop().
    inline static std::atomic<bool> pending{false};
    // millis() timestamp of the request, so the loop-task consumer can honor
    // the same grace period the old delay(200) gave the TCP reply to flush.
    inline static std::atomic<uint32_t> requestedAtMs{0};
};

}  // namespace vialucis
