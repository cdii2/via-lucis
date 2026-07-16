#include "wifi_manager.h"

#include <WiFi.h>
#include <esp_coexist.h>

namespace vialucis {

void WifiManager::begin(const std::string& ssid, const std::string& pass) {
    // A184 (amended twice — see ASSUMPTIONS): the webui stalls under BLE/WiFi
    // radio contention. Attempt 1, WiFi.setSleep(false): REJECTED by the
    // arbiter when BLE is enabled (boot abort, proven live). Attempt 2,
    // esp_coex_preference_set(ESP_COEX_PREFER_WIFI): the device then
    // wdt-crash-looped on async_tcp ~16 s after every boot, timed exactly
    // with the piano's BLE connect (proven live, three boots watched).
    // Reverted to the default (balanced) coexistence — the configuration
    // that ran stable all afternoon. Latency tuning must find another lever
    // (BLE connection params / request pacing), not the coex arbiter.
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
