# Hindsight ledger

One line per decision examined by a hindsight-retrospective run, so the next run picks
fresh decisions. Append-only, newest run on top. Verdicts here are analysis only — they
change no canon, reopen no LOCK, and are not a work queue.

Verdict scale: STILL RIGHT / WORTH THE PRICE / WOULD CHANGE.

## Run 2026-07-19 — Fable, 22-agent verified workflow (read → mine → dedup → adversarial verify)

- H1 (engineering) — **Native-only merge gate + unmeasured device envelope**: the sim
  verified logic while nothing represented stack/heap/WDT/core-affinity, and green native
  gates certified merges even once a live device sat on the LAN (SPEC tech note,
  SIMULATOR.md, A180–A194) — **WOULD CHANGE** (early bare devkit + envelope card + on-target
  smoke tier; the sim itself was right, see H9).
- H2 (design) — **Management/recovery plane unpriced in the v1 spec**: zero words on
  storage limits, telemetry, format/recovery; first sustained real use wedged LittleFS
  and wiped AFK config (BUGFIX-PLAN R2/R3, retrofit A105–A112/A133–A138) — **WOULD CHANGE**
  (spec a "device survives its owner" section; covers ~half the 07-15 pain, not R1).
- H3 (design) — **BLE-MIDI as sole piano link, USB never priced** (SPEC LOCKED;
  zero USB-MIDI mentions repo-wide; classic-ESP32 BOM order same day hard-foreclosed
  USB-OTG; coex rent = A184/A192/A194 + permanent 3–8 s HTTP residual) — **WORTH THE
  PRICE** (2026 USB-host driver reality favored BLE; the miss is that no written pricing
  of the alternative ever happened before the lock).
- H4 (engineering) — **Desktop-shaped on-device SMF parse, RAM contract deferred**
  (whole-file buffer + realloc-doubling vector from 92f6565; three guard generations
  retired in one day, A180→A182→A185–A192) — **WORTH THE PRICE** (corpus made the same-day
  streaming rewrite cheap; the free early win was stating a RAM contract sentence like
  SHOW-FORMAT did; bake-at-upload alternative verified NET WORSE).
- H5 (engineering) — **Web layer trusted to a mock twin; route semantics never exercised
  on the real library** (R1 shadowing: 5 parents swallowed 8 child routes; mock had the
  mirror-image bug) — **WORTH THE PRICE**, with a correction: the repo's caret-crossed-
  matcher-semantics narrative (platformio.ini:14, A106) is FALSE — v3.6.0 prefix-matches
  identically; the caret actually delivered the fix primitive. On-device REST checklist
  at first contact is the real lesson.
- H6 (design) — **v2 lightshow depth + versioned format freeze built entirely
  pre-hardware** (44 commits, 07-09/10) — **WORTH THE PRICE** (hardware was in transit;
  format needed zero rework at contact; kernel to keep: stamp resource numbers
  "provisional until measured" + first-flash validation gate per surface — the show
  path got neither and still holds a latent unguarded 64 KB whole-load vs ~17.4 KB
  piano-connected maxAlloc, readShow song_store.cpp:399).
- H7 (engineering) — **huge_app.csv partition default kept until eve of bring-up**
  (A16 → T3/A121-A122) — **STILL RIGHT** (migration rode a mandatory wedge recovery at
  near-zero cost; OTA was foreclosed by the 4 MB module + firmware size jointly, not by
  the repartition; lesson: log a bootstrap default's revisit trigger).
- H8 (design) — **"Exactly two calibration settings" locked pre-hardware then formally
  reopened** — **STILL RIGHT** (premise partly false: the original SPEC filed it as a
  working decision, not a LOCK; reopen cost ≈ one sign-off sentence + C1's
  characterization harness which doubles as a permanent oracle; the invariant-not-
  parameterization lesson is already a CLAUDE.md iron rule).
- H9 (engineering, VINDICATED) — **Simulator-first pure core + corpus-as-contract
  byte-exact fixtures**: the crash cascade was bounded BECAUSE of it — diagnosis-to-
  merged streaming rewrite ~90 min, A192 tracker rebuild same-day 530/530 byte-exact,
  A194 bisect exonerated the core in hours — **WORTH THE PRICE** (asterisks: ByteSource
  was created by the fix, not pre-existing; A89–A91 were real core bugs caught by audit).
- H10 (design, VINDICATED) — **ESP32-only, no companion app, MIDI-upload slot as the one
  universal seam**: record, corpora, bulk tooling, library pipeline, and the 07-16
  recovery all attached with zero spec changes; the 07-15 library grill re-tested and
  bent it ("PC-optional for management") without breaking it — **WORTH THE PRICE**
  (coex class is the chip's price; parse-memory class was a sub-decision, fixed within
  the lock).
- H11 (design, VINDICATED) — **Flat baked .vls cue stream with a named 64 KB RAM cap**:
  the one pre-hardware memory number hardware never punished; P4 extended it with zero
  migration — **STILL RIGHT**, honestly qualified: vindicated partly by light exercise
  (no live .vls upload/play recorded; BRINGUP has no show step), and the whole-load
  path still owes a maxAlloc guard (see H6).
