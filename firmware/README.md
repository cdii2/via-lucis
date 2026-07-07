# firmware

ESP32 firmware for Via Lucis. Not scaffolded yet — first code task is a PlatformIO
project (Arduino framework) plus a desktop simulator so the scheduler, wait mode, and
brightness ramp are testable before the hardware arrives.

Planned stack: FastLED (with global power cap), BLE-MIDI, ESPAsyncWebServer, LittleFS.
See `../docs/SPEC.md` for the locked design.
