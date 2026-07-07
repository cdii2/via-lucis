#include <Arduino.h>

#include "app.h"
#include "vialucis/version.h"
#include "web_server.h"
#include "wifi_manager.h"

namespace {
vialucis::App gApp;
vialucis::WifiManager gWifi;
vialucis::WebServerLayer gWeb;
}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.printf("Via Lucis v%s\n", vialucis::kVersion);

    gApp.begin();
    gWifi.begin(gApp.settings().wifiSsid, gApp.settings().wifiPass);
    Serial.printf("WiFi %s, ip=%s\n", gWifi.isAp() ? "AP \"ViaLucis\"" : "STA",
                  gWifi.ip().c_str());
    gWeb.begin(gApp, gWifi);
}

void loop() {
    gApp.tick(static_cast<uint64_t>(esp_timer_get_time()));
}
