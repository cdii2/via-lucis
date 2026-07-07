# CLAUDE.md — Via Lucis

LED piano trainer: ESP32 + addressable LED strip above the keys of Christian's Roland
FP-30X, connected over Bluetooth MIDI. Open-source (MIT). He is learning piano with it;
the build itself is part of the fun — don't over-automate decisions that are his.

## Source of truth

- `docs/SPEC.md` — the design, grilled and LOCKED 2026-07-06. Never contradict LOCKED
  decisions without Christian's explicit sign-off.
- `docs/BOM.md` — verified parts list. Hardware is ordered; until it arrives, all
  firmware work targets the simulator.

## Layout

- `firmware/` — ESP32 firmware (PlatformIO + Arduino framework; FastLED, BLE-MIDI,
  ESPAsyncWebServer, LittleFS). Not scaffolded yet — first code task.
- `webui/` — the browser remote the ESP32 serves. Ships embedded in firmware flash.
- `docs/` — spec, BOM, and (later) build guide for strangers replicating the project.

## Iron rules

- **Wait mode is the product.** Any change that adds latency between key press and
  light response needs scrutiny — BLE-MIDI in, match, LED out must stay snappy.
- **No FP-30X-specific hacks in the core.** Piano quirks live behind one small
  interface (see SPEC "Architecture").
- **MIDI echo guard** — never treat notes we sent to the piano as user key presses.
- **Replicability is a feature.** A stranger with the BOM and a browser should be able
  to flash and run. Keep setup steps few and documented.
- LED patterns must respect the FastLED global power cap (2m COB can out-draw the PSU).
