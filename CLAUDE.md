# CLAUDE.md — Via Lucis

LED piano trainer: ESP32 + addressable LED strip above the keys of Christian's Roland
FP-30X, connected over Bluetooth MIDI. Open-source (MIT). He is learning piano with it;
the build itself is part of the fun — don't over-automate decisions that are his.

## Status — orient here first

**Built, flashed, on hold, and not yet usable.** Firmware is code-complete for v1–v3 and
green off-device (530 native tests); the board is wired and the FP-30X pairs. But
key-press → light latency makes wait mode — the whole product — unpracticeable, and the
project is paused. `README.md` §"Status — read this first" is the authoritative account —
read it before planning and never contradict it. Proven on hardware: flash/boot/recovery,
WiFi + web UI from flash, 47 MIDIs on LittleFS, BLE-MIDI pairing, streamed on-device
parse, the strip following a song. **Never exercised on hardware at all:** recording,
`.vls` playback, demo/accompaniment, score-follow — unverified, not working.

## Source of truth

**Design canon** — never contradict without Christian's explicit sign-off.
- `docs/SPEC.md` — the v1 design, grilled and LOCKED 2026-07-06. Its §"Technical notes"
  are explicitly *working decisions, not locked* — don't treat that section as LOCK.
- `docs/DESIGN-lightshow.md` — v2 aesthetic & show systems, grilled 2026-07-09, built.
  Reopens the "exactly two calibration settings" decision (→ per-key LED table) with his
  sign-off. SPEC calls that a LOCK; it sat under the working-decisions heading
  (`docs/retro/hindsight-ledger.md` H8) — signed off either way.
- `docs/DESIGN-record.md` — on-device MIDI recording, LOCKED 2026-07-13, built.
- `docs/DESIGN-library.md` — bigger library, decided 2026-07-15. Qualified SPEC
  §Architecture's standalone lock, with sign-off: **standalone for *practice*, PC-optional
  for *management*.** Phases 0+1 are built; **Phase 2 (T7 watched-folder semantics, T8 PC
  sync agent) is HELD** pending Christian living with Phase 1 — do not build it.
- `docs/BOM.md` — the parts actually bought and wired, not a buy-one-now recommendation.
- `docs/SONGBOOK.md` — practice ladder; §"Legal split" is LOCKED.

**Contracts** — machine-checked. Change the contract file first, or not at all.
- `docs/API.md` — the `firmware/src/` ↔ `webui/` REST seam. Both sides build against it.
- `docs/SHOW-FORMAT.md` — the baked `.vls` cue stream (device player, clocks, editor).
- `corpus/` pins both byte-exact across firmware, editor and tools; all three must be green.

**Ledgers**, both read at cold start: `ASSUMPTIONS.md` (A1–A194, every autonomous decision
+ rationale, newest on top — log an `A<n>` for every call you make without asking; latency
work starts at A184/A194) and `PROGRESS.md` (resume recipe, frozen technical decisions,
work plan, **Needs Christian** / **Needs Hardware** queues).

**How-to** — procedure, not design; correctable without sign-off. `docs/BUILD-GUIDE.md`,
`docs/BREADBOARD-GUIDE.md` (**BUILD-GUIDE §4's wiring table wins** where they disagree),
`docs/BRINGUP.md`, `docs/TROUBLESHOOTING.md`, `docs/SIMULATOR.md`, `docs/EXPLAINER.md`.

## Layout

- `firmware/lib/core/` — 67 files, ~8.9k lines. Portable C++ under `namespace vialucis`,
  **zero Arduino headers**. The only thing the 530 native tests cover.
- `firmware/src/` — 15 files, ~2.9k lines of Arduino glue (FastLED, NimBLE, WiFi,
  ESPAsyncWebServer, LittleFS). esp32-only, compile-gated, never natively tested.
- `firmware/test/` — 43 files, ~11.5k lines, 39 Unity suites (`pio test -e native`).
- `webui/` — the single-file browser remote, gzipped into flash. After **any** edit to
  `webui/index.html`, regenerate `firmware/src/webui_gz.h` with `python webui/build.py`
  (generated — never hand-edit). `index.html` and `docs/API.md` change together.
- `editor/` — one 3.3k-line offline `editor.html`, opened from `file://`, shipped as a
  release artifact and **never hosted** (A57 / VL3). It bakes `.vls` cue streams; the
  device only interprets them.
- `corpus/` — golden `.vls` + SMF fixtures; the contract test above lives here.
- `tools/` — stdlib-only Python: `mock_device.py` (web-UI preview, *not* a firmware
  simulator), the two corpus checkers, `vls_dump.py`/`midi_dump.py`, `bulk_upload.py`.
- `songs/` — `pd/` public-domain, committed. `local/` gitignored forever, never published.
- `docs/` — canon, contracts, guides, plus `diagrams/`, `images/`, `agents/`, `retro/`.

## Rules

The codebase cites these **by number** — `iron rule 1`, `iron rule 4` — and by name, in
source comments, `ASSUMPTIONS.md` and `PROGRESS.md`; `git grep -i "iron rule"` finds them
across ~30 files. The numbering is therefore **stable: never renumber, never reorder**;
append only. (Heading reads "Rules" per the workspace policy that a rule stays reviewable
rather than a stronghold — the citations still say "iron rule N".)

1. **The latency path is the product.** Practice (wait mode) is the core; any change that
   adds latency between key press and light response needs scrutiny — BLE-MIDI in, match,
   LED out must stay snappy. No aesthetic layer may sit on that path. *The open wound:
   this rule was unmeasurable until hardware existed, and it is what stopped the build.*
2. **Cosmetics never delay a note's onset.** Timing cues (e.g. the repeat cue) borrow
   visible time backward from the outgoing note's tail; "now" always lands on the beat.
3. **AFK can never interrupt practice.** AFK lighting arms only in the no-song state
   (no song loaded *and* idle). Loading a song disarms it for the session — no idle
   timer may fire mid-practice, however long you sit reading music.
4. **The core stays clean — two halves, both cited as "iron rule 4".** (a) *No
   FP-30X-specific hacks in the core:* piano quirks live behind one small interface (SPEC
   "Architecture"; `midi_io.h`, `ble_midi_io.h`). (b) *`firmware/lib/core/` is pure C++,
   no Arduino headers,* so `env:native` can test it — LittleFS handles, FastLED and NimBLE
   plug in behind a seam from `firmware/src/`.
5. **MIDI echo guard** — never treat notes we sent to the piano as user key presses.
6. **Replicability is a feature.** A stranger with the BOM and a browser should be able
   to flash and run. Keep setup steps few and documented. Calibration must fit *any*
   keyboard: everything downstream reads one per-key LED table; the tiers/wizards are
   just different ways to fill it.
7. **Every effect respects the FastLED global power cap** — ambient/show effects included
   (a full-white twinkle on 2m COB can out-draw the PSU).
8. **Permissive licenses only, zero copyleft.** Port MIT FastLED classics; reimplement
   effect *ideas* freely; never copy WLED (EUPL) or xLights/Vixen (GPL) code.
9. **The editor is not the player.** The browser editor compiles groups/nesting into a
   flat song-position cue stream; the ESP32 only interprets baked streams. Authoring
   depth must never cost the device anything.
10. **Contracts change in their own file first.** A wire-format or REST change lands in
    `docs/SHOW-FORMAT.md` / `docs/API.md` before any implementation moves.
11. **Corpus is contract.** A byte mismatch against `corpus/` is a failed gate, never a
    judgment call. Regenerate fixtures only after a deliberate, documented format change.
12. **Never trust a green gate line — grep the output for FAIL/ERROR.** A crashed binary
    truncates a run that still prints reassuring lines; zero matches = green, anything
    else = red (`docs/SIMULATOR.md`, PROGRESS resume recipe).
13. **Native tests certify logic, never the device envelope.** Stack, heap, watchdog and
    core affinity are invisible to the simulator (A180–A183, A194 core-pin).
14. **`songs/local/` is never committed and never published.** Copyright-unclear
    arrangements stay on Christian's machine and the ESP32 (SONGBOOK "Legal split").
