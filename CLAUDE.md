# CLAUDE.md — Via Lucis

LED piano trainer: ESP32 + addressable LED strip above the keys of Christian's Roland
FP-30X, connected over Bluetooth MIDI. Open-source (MIT). He is learning piano with it;
the build itself is part of the fun — don't over-automate decisions that are his.

## Source of truth

- `docs/SPEC.md` — the v1 design, grilled and LOCKED 2026-07-06. Never contradict LOCKED
  decisions without Christian's explicit sign-off.
- `docs/DESIGN-lightshow.md` — the v2 aesthetic & show systems brief, grilled 2026-07-09.
  Design-level, build-ready. It reopens exactly one v1 LOCK (the "exactly two calibration
  settings" decision → per-key LED table), with Christian's sign-off; everything else in
  SPEC.md stands.
- `docs/BOM.md` — verified parts list. Hardware is ordered; until it arrives, all
  firmware work targets the simulator.

## Layout

- `firmware/` — ESP32 firmware (PlatformIO + Arduino framework; FastLED, BLE-MIDI,
  ESPAsyncWebServer, LittleFS). Not scaffolded yet — first code task.
- `webui/` — the browser remote the ESP32 serves. Ships embedded in firmware flash.
- `docs/` — spec, BOM, and (later) build guide for strangers replicating the project.

## Iron rules

- **The latency path is the product.** Practice (wait mode) is the core; any change that
  adds latency between key press and light response needs scrutiny — BLE-MIDI in, match,
  LED out must stay snappy. No aesthetic layer may sit on that path.
- **Cosmetics never delay a note's onset.** Timing cues (e.g. the repeat cue) borrow
  visible time backward from the outgoing note's tail; "now" always lands on the beat.
- **AFK can never interrupt practice.** AFK lighting arms only in the no-song state
  (no song loaded *and* idle). Loading a song disarms it for the session — no idle
  timer may fire mid-practice, however long you sit reading music.
- **No FP-30X-specific hacks in the core.** Piano quirks live behind one small
  interface (see SPEC "Architecture").
- **MIDI echo guard** — never treat notes we sent to the piano as user key presses.
- **Replicability is a feature.** A stranger with the BOM and a browser should be able
  to flash and run. Keep setup steps few and documented. Calibration must fit *any*
  keyboard: everything downstream reads one per-key LED table; the tiers/wizards are
  just different ways to fill it.
- **Every effect respects the FastLED global power cap** — ambient/show effects included
  (a full-white twinkle on 2m COB can out-draw the PSU).
- **Permissive licenses only, zero copyleft.** Port MIT FastLED classics; reimplement
  effect *ideas* freely; never copy WLED (EUPL) or xLights/Vixen (GPL) code.
- **The editor is not the player.** The browser editor compiles groups/nesting into a
  flat song-position cue stream; the ESP32 only interprets baked streams. Authoring
  depth must never cost the device anything.
