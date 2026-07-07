#pragma once
// WiFi: stored creds → STA; failure/empty → AP "ViaLucis" with the same web
// UI as the config page (FROZEN decision A2).

#include <string>

namespace vialucis {

class WifiManager {
public:
    static constexpr const char* kApSsid = "ViaLucis";
    static constexpr uint32_t kStaTimeoutMs = 15000;

    // Blocks up to kStaTimeoutMs trying STA, then falls back to AP.
    void begin(const std::string& ssid, const std::string& pass);

    bool isAp() const { return ap_; }
    std::string ip() const;

private:
    bool ap_ = false;
};

}  // namespace vialucis
