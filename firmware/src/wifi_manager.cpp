#include "wifi_manager.h"

#include <WiFi.h>

namespace vialucis {

void WifiManager::begin(const std::string& ssid, const std::string& pass) {
    if (!ssid.empty()) {
        WiFi.mode(WIFI_STA);
        // A184: modem power-save OFF. The Arduino default (WIFI_PS_MIN_MODEM)
        // lets the WiFi radio sleep between DTIM beacons, and under BLE
        // coexistence (the piano-off scan cycle) that turned every HTTP
        // request into a multi-second-to-minutes stall — the live device's
        // webui was unusable (wizard screens took minutes to change; Load
        // appeared dead). This is a wall-powered piano lamp: power saving
        // buys nothing and costs the entire UI. Canonical ESP32 coex fix.
        WiFi.setSleep(false);
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
    WiFi.setSleep(false);  // A184: same rule on the recovery AP
    WiFi.softAP(kApSsid);  // open AP: it's a piano lamp, not a bank
    ap_ = true;
}

std::string WifiManager::ip() const {
    IPAddress ip = ap_ ? WiFi.softAPIP() : WiFi.localIP();
    return std::string(ip.toString().c_str());
}

}  // namespace vialucis
