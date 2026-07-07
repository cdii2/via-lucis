# Via Lucis — PROGRESS

Mission: **everything decision-free and simulator-tested before the parts arrive.**
Assembly day = wire → flash → pair → practice, no AI assumed present.

## RESUME RECIPE (cold start)

1. Read `docs/SPEC.md` (LOCKED design), `CLAUDE.md` (iron rules), `docs/BOM.md`
   (hardware truth), this file.
2. Verify baseline: `git status` clean on `main`; if `firmware/platformio.ini` exists,
   run `pio test -e native` — must be ALL PASS (grep output for FAIL, don't trust exit
   code alone).
3. Pick the highest unchecked item below (W1 → W6 order). Build it TDD. Gate. Check it
   off. Update ASSUMPTIONS.md with any A<n> decisions. Report. Continue.

## Frozen technical decisions

- **LED data pin: GPIO 16** (not a strapping pin). 74AHCT125: 1A=GPIO16, 1Y→strip DIN,
  1OE→GND, VCC=5V, common ground. Once BUILD-GUIDE.md is written these NEVER move.
- PlatformIO, two envs: `esp32dev` (Arduino framework) + `native` (desktop tests).
- Core logic (`firmware/lib/core/`) is pure C++ — no Arduino headers — so the native
  env tests scheduler/wait/ramp/parser/mapping without hardware.
- Libraries: FastLED (RMT driver, global power cap), a BLE-MIDI lib (evaluate
  lathoub/Arduino-BLE-MIDI first), ESPAsyncWebServer, LittleFS.
- WiFi: stored creds via settings; on failure ESP32 opens AP `ViaLucis` with config
  page. Web UI gzipped into flash.
- Key→LED map: key center in mm from low-A origin → nearest ICs (180 IC/m ⇒ 5.6mm).
  Calibration = two settings: strip offset (mm) + IC-per-meter.

## Work plan

### W1 — core engine (native-tested, zero hardware)
- [x] PlatformIO scaffold (`esp32dev` + `native` envs, lib/core layout, unity tests)
- [x] Standard MIDI File parser → internal note-event list (tempo map, tracks, CC64)
- [x] Key→LED mapping with calibration offsets
- [ ] Scheduler (tempo 1–500%, time-range loop, per-track/hand filter)
- [ ] Wait-mode matcher (per-key chord clearing, wrong-note events, echo guard hook)
- [ ] Lookahead ramp renderer (0→cap over lead-time, jump to 100%; ONE cap var, ONE
      lead var) → LED frame buffer
- [ ] Demo/accompaniment note emitter (same schedule → MIDI-out queue)

### W2 — device layer (compiles, hardware-verification deferred)
- [ ] FastLED output (GPIO16, power cap, brightness setting)
- [ ] BLE-MIDI in/out wrapper + echo guard wiring
- [ ] LittleFS song storage + settings persistence (JSON)
- [ ] WiFi manager (creds → AP fallback) + web server + REST API (songs, transport,
      modes, tempo, loop, colors, ramp, calibration)

### W3 — web UI (phone remote)
- [ ] Single-page app: song list/upload, play/pause, mode toggles (wait/follow/demo/
      accompaniment), hand toggles, tempo slider 1–500%, loop range, settings page
      (colors, ramp cap/lead, calibration), gzip-embedded in firmware

### W4 — docs for a human alone (no AI on assembly day)
- [ ] BUILD-GUIDE.md — wiring diagram (PSU→both strip ends + ESP32 VIN, shifter hookup,
      common ground), channel mounting on FP-30X, PlatformIO install + flash steps,
      every command copy-pasteable
- [ ] BRINGUP.md — ordered checklist with expected output at each step (strip test
      pattern → WiFi → web UI → BLE pairing → key test → first song)
- [ ] TROUBLESHOOTING.md — brownouts/dim-red LEDs, flicker (shifter/ground), BLE
      pairing failures (FP-30X Bluetooth MIDI on/off), echo symptoms, wrong key
      alignment (calibration), CP2102 driver on Windows
- [ ] SIMULATOR.md — how to run/extend native tests

### W5 — songs
- [ ] `songs/pd/` starter pack arranged + committed (Ode to Joy, Amazing Grace, Minuet
      in G, Vivaldi Largo, Clair de Lune)
- [ ] Download Tier 0–1 MIDIs from ninsheetmusic/vgmusic → `songs/local/` (authorized
      2026-07-07); log per-song status in SONGBOOK.md
- [ ] MuseScore-only items → listed for Christian with exact URLs

### W6 — polish (only after W1–W5)
- [ ] Wait-mode latency audit (BLE in → match → LED out)
- [ ] Web-UI niceties; strip test/rainbow easter egg; settings export

## Needs Christian (never blocks the loop)
- MuseScore-account downloads (exact URLs get listed in SONGBOOK.md as found)
- GitHub publish decision (repo has NO remote yet; never publish without him —
  `songs/local/` must stay untracked)
- Physical assembly + all "verify on hardware" items in BRINGUP.md

## Needs Hardware (code-complete + documented only)
- BLE pairing behavior of the real FP-30X (incl. echo test)
- Strip calibration values; actual wait-mode latency; PSU headroom check

## Iteration log

- 2026-07-06 iter 0: repo scaffolded, spec + BOM locked, committed (30136e3).
- 2026-07-07 iter 1: songbook + this work plan written; loop prompt handed to Christian.
- 2026-07-07 iter 2: W1 scaffold + SMF parser DONE. Toolchain installed on this PC:
  PlatformIO via `uv tool install platformio` (pip bootstrapped into its venv via
  ensurepip — esptool needs it), GCC via winget WinLibs (on user PATH; fresh shells may
  need `$env:Path` refresh). Tests: one dir per module under `firmware/test/test_*`,
  shared helpers in `firmware/test/helpers/` (non-test dir). Parser: header-only reader,
  15 native tests green, compiles for esp32.
