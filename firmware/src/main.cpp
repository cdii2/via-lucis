#include <Arduino.h>
#include <esp_task_wdt.h>

#include "app.h"
#include "reboot_request.h"
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

    // A193: task watchdog = warn, don't abort, and give it 15 s. With the
    // piano CONNECTED (BLE-MIDI at a 7.5-40 ms connection interval) the
    // radio+CPU0 load from BLE + WiFi + NimBLE + async_tcp can legitimately
    // starve the IDLE0 task past the stock 5 s limit under a browser's
    // request burst — the stock panic setting then hard-REBOOTED the device
    // every ~16 s exactly while the player had the piano on (proven live,
    // repeatedly, across coex settings). A saturated-but-progressing device
    // must degrade (slow requests), never crash-loop. Genuine hangs still
    // print the wdt warning on serial for bring-up eyes.
    esp_task_wdt_init(15, /*panic=*/false);

    gApp.begin();
    gWifi.begin(gApp.settings().wifiSsid, gApp.settings().wifiPass);
    Serial.printf("WiFi %s, ip=%s\n", gWifi.isAp() ? "AP \"ViaLucis\"" : "STA",
                  gWifi.ip().c_str());
    gWeb.begin(gApp, gWifi);
}

void loop() {
    gApp.tick(static_cast<uint64_t>(esp_timer_get_time()));

    // B7: reboot via a loop-task flag instead of blocking the async_tcp task
    // with delay(200)+ESP.restart() (the pre-B7 web_server.cpp handler).
    // web_server.cpp sets RebootRequest::pending and replies immediately
    // (ASK for B4 — this file cannot touch web_server.cpp); this consumer
    // honors the same ~200ms grace period the old delay gave the HTTP reply
    // to flush, off the network task entirely.
    if (vialucis::RebootRequest::pending.load()) {
        uint32_t elapsed =
            millis() - vialucis::RebootRequest::requestedAtMs.load();
        if (elapsed >= 200) ESP.restart();
    }

    // B7: 1ms yield — task-watchdog risk otherwise. arduino-esp32's loopTask
    // calls loop() in a tight for(;;) with no inherent yield; if it never
    // blocks, the idle task (which feeds the watchdog) can starve. 1ms is
    // imperceptible against the ~16.7ms LED frame period and the BLE/HTTP
    // tasks' own cadence.
    delay(1);
}
