#pragma once
// REST layer per docs/API.md. Thin: parse/validate → App method → JSON reply.

#include "app.h"
#include "wifi_manager.h"

namespace vialucis {

class WebServerLayer {
public:
    void begin(App& app, WifiManager& wifi);
};

}  // namespace vialucis
