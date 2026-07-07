# Via Lucis — v1 Specification

Locked by grilling session, 2026-07-06. Changes to LOCKED decisions require Christian's
explicit sign-off.

## Purpose

Christian is learning piano. Via Lucis is the trainer: lights above the keys driven by
an ESP32 that hears every key press over Bluetooth MIDI. It is also an open-source
project intended to be replicable by strangers ("buy two parts, flash from your
browser").

## LOCKED decisions

### Architecture
- **ESP32 does everything.** BLE-MIDI to the piano (bidirectional), songs stored in
  flash (LittleFS), scheduler + wait mode on-device, LEDs driven directly, web UI
  served over WiFi. No PC, no Raspberry Pi, no cloud.
- **Piano:** Roland FP-30X via BLE-MIDI. All piano-specific behavior lives behind one
  small interface — no FP-30X hacks in the core.
- **Open source (MIT).** v1 must work on Christian's piano; generality beyond that is
  a non-goal for v1.

### v1 feature set
1. **Wait mode** (the core): lights show due note(s); playback halts until the correct
   key(s) are pressed.
   - Chords clear **per key** as each correct note is pressed.
   - **Wrong press = red flash** at the pressed key's position. Full-strength feedback,
     not subtle ("if God is for us, who can be against us").
2. **Follow-along playback** — same scheduler, no waiting.
3. **Tempo 1%–500%** (multiplier on scheduler; mostly matters in follow-along).
4. **Hands separate** — per-track filter. Either hand's lights can be turned off.
5. **Time-range loop** — repeat between two timestamps (e.g. 0:45–0:50). Seconds-based,
   not bar-based.
6. **Demo mode** — ESP32 sends the song to the piano over MIDI; piano plays it through
   its own sound engine, lights follow.
7. **Accompaniment** — in hands-separate practice, the piano plays the muted hand while
   wait mode watches the practiced hand.
8. **Lookahead brightness ramp** — upcoming note's LED swells from 0% to a preview cap
   (default ~45%, ONE adjustable variable) over an adjustable lead time (default 1s),
   then **jumps to 100%** at press-time. The dead zone between cap and 100% is what
   makes "now" unmistakable.

### Colors
- Defaults: **left hand = blue, right hand = green, wrong note = red.**
- All colors customizable in settings; shipped defaults must not collide with the
  wrong-note color.

### Song pipeline
- Input format: **MIDI files**, uploaded via the web UI to ESP32 flash.
- Sources: downloaded MIDIs (MuseScore.com, BitMidi, hymn archives); MuseScore desktop
  as the editor / hand-splitter for files with merged tracks.
- **OMR (sheet-music photo → MIDI) is a separate future program**, not part of this
  repo. The upload slot is the seam; anything that produces MIDI can feed it.

### Explicitly deferred (not v1)
- **Falling-notes web view** synced to the lights — flagship v1.1 feature.
- Fading guidance (lights dim as you improve) — needs scoring history; v2.
- Bar-aware looping, per-note statistics, multi-piano support.

## Technical notes (working decisions, not locked)

- **Firmware stack:** PlatformIO + Arduino framework. FastLED (with global power cap —
  2m of COB at full white can exceed the 10A supply), a BLE-MIDI library, ESPAsyncWebServer,
  LittleFS for songs + settings.
- **MIDI echo guard:** notes we send to the piano may be echoed back on its MIDI out;
  wait mode must filter our own transmitted notes out of the input stream. Verify
  against the real FP-30X during bring-up.
- **Key → LED mapping:** compute each key's center position in millimeters from the
  low-A origin, then map to nearest LED indices (180 IC/m COB strip ⇒ 5.6mm per
  segment, ~2.5 segments per key). Calibration = two settings: strip offset (mm) and
  LEDs-per-meter.
- **Headphones:** MIDI-driven notes render through the piano's sound engine, so
  headphone routing works automatically for both the player and demo/accompaniment.
- **Sustain pedal:** pass CC64 through in demo/accompaniment; ignore it in wait-mode
  matching (v1).
- Development before hardware arrives: strip + piano simulator so firmware logic
  (scheduler, wait mode, ramp) is testable on the desktop.

## Hardware

See [BOM.md](BOM.md). Reference build: FCOB 180 LED/m 5V strip (2m, one continuous
piece), classic ESP32-WROOM devkit, 5V 10A PSU with power injected at both strip ends,
74AHCT125 level shifter (3.3V → 5V data), breadboard junctions (no soldering),
aluminum channel mount with the diffuser cover **left off** (per-key crispness).
