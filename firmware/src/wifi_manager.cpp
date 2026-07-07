#include "wifi_manager.h"

#include <WiFi.h>

namespace vialucis {

void WifiManager::begin(const std::string& ssid, const std::string& pass) {
    if (!ssid.empty()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED &&
               millis() - start < kStaTimeoutMs) {
            delay(250);
        }
        if (WiFi.status() == WL_CONNECTED) {
            ap_ = false;
            return;
        }
    }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid);  // open AP: it's a piano lamp, not a bank
    ap_ = true;
}

std::string WifiManager::ip() const {
    IPAddress ip = ap_ ? WiFi.softAPIP() : WiFi.localIP();
    return std::string(ip.toString().c_str());
}

}  // namespace vialucis
