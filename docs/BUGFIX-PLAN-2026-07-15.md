# Master Bug-Fix Plan — 2026-07-15

Produced from (a) live debugging of the real device at 192.168.1.191 (firmware @ `2e7cede`)
and (b) a 20-agent read-only audit of the whole repo (routing, upload/intake, storage,
webui play/songs/settings screens, AFK/ambient, shows, calibration firmware + wizard UX,
playback engine, wait-mode matcher, BLE-MIDI, mode director, persistence, record, LED/power,
runtime/tasks, HTTP semantics, three-way contract diff).

Verdicts on the sacred surfaces first: the **wait-mode matcher is sound** (A98 chord gather
and A99 practiced-implies-lit verified adversarially, no softlock found) and the **FastLED
power-cap iron rule passes** (single `show()`, single cap, no bypass path). The bugs live
almost entirely in the HTTP layer, the storage layer, and the webui — which is why the
device *feels* broken while the core is fine.

---

## 1. Device state RIGHT NOW (read before touching anything)

1. **The filesystem is wedged.** After hitting hard-full during upload testing, creating
   any NEW file in `/songs` fails persistently (survives reboot and freeing ~110 KB).
   Overwrites and deletes still work. Every upload currently dies with a TCP reset at
   0 bytes. Root cause chain in §2-R2.
2. **`ZeldaWW-Dragon_roost_island.mid` was deleted from the device** during diagnosis and
   could not be re-uploaded (wedge). Source is safe at
   `C:\Users\omega\Downloads\midis fo Via Lucis\batch 1\`. The NieR 140 KB partial was
   also deleted. Device holds 44 songs.
3. **Serial is unavailable.** The CP210x/COM5 device is gone from this PC's USB — the
   board is powered and on WiFi but the data cable is disconnected or charge-only.
   **First physical action: re-seat the USB data cable**, then capture COM5 @115200
   during one upload attempt — the LittleFS `errno` in that log confirms the wedge
   mechanism (expected: `LFS_ERR_NOSPC` on dir-metadata split).
4. AFK/ambient config was accidentally reset during live probing (the destructive-reset
   bug fired on a test POST) and has been **restored** (`brightnessCap 36`,
   `aboveKeysOnly true`). The tracks list was already empty before the test.
5. **Recovery sequence for the wedge** (after Wave A firmware is built): USB reflash →
   `POST /api/storage/format` (new endpoint) → bulk re-upload all songs from Downloads
   (script it) → verify count/sizes. A reflash alone does NOT clear LittleFS.
   Optional cheap experiment first: delete several more songs and retry one upload —
   if the dir-pair compacts, the wedge may clear without a format.

---

## 2. Root causes (three systemic, one environmental)

**R1 — Route shadowing.** ESPAsyncWebServer 3.11.2 (silently resolved from `@^3.6.0`)
matches plain-string routes as `path == V || path.startsWith(V + "/")`, first-registered
wins. Five parent routes swallow eight child route×method pairs:

| Victim | Swallowed by | User-visible result |
|---|---|---|
| `POST /api/songs/{name}/load` | `POST /api/songs` (upload) | "empty upload" — **no song can ever be loaded** |
| `POST /api/songs/unload` | same | "empty upload" |
| `POST /api/songs/{name}/rename` | same | "missing ?name=" |
| `POST /api/shows/stop` | `POST /api/shows` (upload) | "empty upload" — shows can't stop |
| `POST /api/shows/{name}/play` | same | shows can't start — **presentation dead from REST** |
| `POST /api/afk/control` | `POST/PUT /api/afk` (config) | **silently WIPES ambient config to defaults** (brightnessCap→96, tracks→[]) |
| `GET /api/calibration/probe` | `GET /api/calibration` | wizard reads the wrong document |
| `POST /api/calibration/probe` | `POST /api/calibration` | `400 "bad tier"` — **probe never arms, wizard dead** |

This one cause = Christian's "Empty upload on load", most of "ambient ignores me", and
"calibration Start does nothing". Confirmed live over REST; victim list verified complete.

**R2 — Storage.** The song-upload path has **no free-space check** (record and shows do),
**no partial-file cleanup on disconnect** (shows has it), re-opens the file **per TCP
chunk** (a LittleFS metadata commit each ~1.4 KB, on the async_tcp network task — which
starves TCP and causes the RSTs), and all saves **truncate-in-place** (power loss = wiped
config). Partials from failed uploads accumulated until LittleFS hit hard-full
(896 KB partition, `huge_app.csv`; block math: 44 songs ≈ 850 KB in 4 KB blocks), at
which point the `/songs` directory metadata pair wedged (a dir split needs a free block)
— creates now fail forever while overwrites/deletes work. No storage gauge, no heap/FS
telemetry in status, no format/factory-reset route, no OTA: **no recovery without USB**.

**R3 — Destructive-defaults parsing.** `afkConfigFromJson` builds a fresh default config
and overlays only present keys — any partial/foreign body (e.g. the swallowed
`{"action":"stop"}`) is "valid" and **resets everything, persisted to flash**. Settings'
`fromJson` PATCHes into the current object (correct pattern); AFK and Calibration don't.
No persisted doc carries a schema version. Calibration's perKey tier loses the
`reversed` flag on revert-to-2-point.

**R4 — (environmental) UI "Add effect" is broken independently:** new tracks default to
`effect:"rainbow"`, which is a **palette** name — firmware rejects the whole playlist PUT
every time, so effects never persist even where routing works.

### Christian's four reported bugs → causes
1. *"Loading any song returns Empty upload"* → R1 (load route swallowed).
2. *"Ambient plays one effect, ignores added effects + brightness cap"* → R1
   (`afk/control` wipes config) + R4 (add-effect always rejected) + the lone effect is
   the built-in empty-playlist fallback. Cap enforcement itself verified correct.
3. *"Align lights to keys — Start does nothing"* → R1 (probe routes swallowed) **plus**
   a real UX defect: the on-screen dot is decorative, never shows which LED is lit.
   Redesign spec in §4-D.
4. *"140 KB NieR upload fails at ~20 KB"* → R2 (it was the exact free space that day;
   no precheck, ugly RST failure, partial left behind → wedge).

---

## 3. Fix waves

Chokepoint rule: `firmware/src/web_server.cpp` is touched by most of Wave A/B — one
lead owns it per wave (per /via-lucis-dispatch). Core modules are parallel-safe.
Gates for every wave: `pio test -e native` (419 baseline, add tests; grep FAIL/ERROR),
`pio run -e esp32dev` clean, nothing staged under `songs/local/`. Routing fixes are
esp32-only behavior → post-flash curl checklist (§5).

### WAVE A — unbrick + showstoppers (firmware lead + one webui builder)

- **A1. Kill route shadowing**: convert the five parent routes
  (`web_server.cpp:210, 440, 614, 637, 642`) to `AsyncURIMatcher::exact(...)`; widen
  `onJsonBody` to accept `AsyncURIMatcher` (implicit ctor keeps other call-sites
  compiling). Exact matchers are order-independent — safer than reordering. Pin
  `ESPAsyncWebServer` to the exact resolved version and pin `platform = espressif32@7.0.1`
  (caret ranges crossed a matcher-semantics change once already).
- **A2. Upload path overhaul** (songs + shows):
  - First-chunk free-space precheck: `507` when `total + reserve > freeBytes()`
    (block-aware: round to 4 KB blocks, keep ≥16-32 KB reserve; share the constant with
    record's margin).
  - Register `in.cleanup` on song uploads (mirror shows) so disconnects/failures remove
    the partial; also remove synchronously on write-fail.
  - Hold ONE open `File` in `BodyIntake` across chunks (open on first, close on last) —
    removes ~100 metadata commits per upload and most of the TCP starvation.
  - Defer mid-body errors: record `{code,msg}` in the intake, drain, send from the
    completion handler → clients get real 4xx/5xx JSON instead of TCP RST.
  - `409` when upload targets the currently-loaded song's name.
  - Show precheck: stop double-counting an existing same-name show (net-delta math) —
    currently blocks the entire edit→re-save loop at the caps.
- **A3. Recovery valve + telemetry**: `POST /api/storage/format` (guarded, confirm
  token) that formats LittleFS and re-runs begin(); add `fsFree/fsTotal/fsUsed`,
  `heapFree`, `heapMaxAlloc`, `uptimeMs` to `/api/status`. Both would have made this
  whole incident visible and recoverable in the field.
- **A4. AFK parse hardening**: `afkConfigFromJson` PATCHes into the current config;
  reject bodies containing no recognized config field; enforce documented clamps
  (`masterSpeed` 0.25-4, `dwellSec` floor 5).
- **A5. webui quick strikes**: "Add effect" defaults to `AFK_EFFECTS[0]`; Songs page
  storage gauge ("X of 896 KB used", red near full) fed by A3; client-side name
  sanitization (charset `[A-Za-z0-9-_ .]`, length 5-64, lowercase `.MID`, transliterate
  accents, explain WHICH rule failed); map fetch-level failures to human messages
  ("device may be out of space"), refresh list in `.finally`.
- **A6. Flash + recover the live device** (§1.5 sequence), then run the §5 checklist.

### WAVE B — majors (firmware core; 3 parallel-safe builders)

- **B1. Mode director latches**: test pattern gets a timeout + auto-clear on
  load/play/show-start (today a vanished client's test pattern hides AND pauses practice
  forever); show reaching `Finished` tears down `showPlaying_`/`presentation_` and
  restores pre-show mode (today it freezes on the last frame and 409-blocks
  probe/record/mode/upload until manual stop).
- **B2. AFK control semantics**: `afkControl` next/previous must NOT `touchWriteActivity`
  (today it would dismiss the ambient show it's controlling — latent behind R1).
  Reconcile API.md ("any write wakes") vs DESIGN-lightshow ("media controls") — see §6.
- **B3. Engine**: `setTrack` gains an `out` param and flushes note-offs for tracks
  leaving the emit mask (today: **stuck note ringing on the piano** when changing hands
  mid-note); play-from-Finished clears echo credits (parity with seek); scheduler
  `advance` returns a wrapped flag instead of the engine inferring wrap from position
  (fixes skipped repeat-cues on short loops at high tempo).
- **B4. Persistence**: atomic saves everywhere (write `.tmp`, rename); propagate save
  failures to `507` instead of echoing 200 (today settings/calibration/afk saves lie on
  a full FS); add `"schema": 1` to every persisted doc; check `begin()`/`mkdir` results
  and surface an `fs:"error"` status flag; decide `formatOnFail` policy (§6); self-heal
  corrupt docs at boot (re-save defaults + surface a "config reset" flag).
- **B5. Record**: surface `Overflowed` in record status + a `SavedTruncated` stop result
  (today long takes silently truncate); keep the take retrievable after `SaveFailed`
  with a retry-save route (today the performance is destroyed — and every save fails on
  a full FS); capture pedal-down while Armed (start the clock) so opening sustain isn't
  lost; clamp `recordBudgetKB` to ≤ 256 (`kMaxSongBytes`) so an over-budget take can't
  be unsaveable by construction.
- **B6. Calibration firmware**: infer `reversed` from perKey table direction (today a
  settings nudge flips a mirrored strip); reject an empty/degenerate perKey table
  (today it validates and blanks every key); refuse probe arm while recording (today the
  probe eats the performer's notes); add a `timedOut` flag to probeJson.
- **B7. Runtime**: 1 ms yield in `loop()` (task-watchdog risk); snapshot the frame and
  release the fence BEFORE `FastLED.show()` (~10.8 ms currently held — direct hit on
  the latency-path rule); reboot via loop-task flag instead of `delay(200)` on
  async_tcp; `reserve()` the three unreserved hot-path vectors (`pending_`, `cleared_`,
  `wrongFlashes_`); drop a wrong-flash entry when its key becomes due/cleared (today a
  key the player must press can render solid red for 300 ms); add
  `App::loadedSongName()` accessor (DELETE handler currently serializes+reparses the
  whole status doc under the fence).

### WAVE C — webui flows (parallel with B; different files)

- **C1. Play screen**: emit the practice hand in `statusJson` and reconcile the
  selector (today it's client-only, resets the device to "both" on any mode tap after
  reload); disable tempo/loop/(mode) controls when no song is loaded (today they error-
  toast); loop staging holds dirty edits so "Set from here" survives the 500 ms poll
  (today staging a new loop is a race the user loses); unify slider error policy.
- **C2. Songs screen**: badge unparseable/partial songs (needs a parse-ok flag or
  load-time distinction); "unload then delete" affordance on the loaded song's 409;
  error state instead of eternal "Loading…" when the list fetch fails.
- **C3. Settings screen**: align input `min/max` with server clamps (ledsPerMeter ≥10
  etc.); symmetric wrongColor↔repeatColor collision toast; validate/round numerics
  before send (NaN bodies silently ignored today); guard the WiFi chip against
  wifi-less command responses (blanks today); subtext distinguishing the three
  brightness controls' units and backends.
- **C4. wifiPass write-only** (pending §6 ruling): never emit in GET/export
  (`wifiPassSet:true` instead) — today any LAN/AP client can read the home WiFi
  password in cleartext, and Export writes it to disk.

### WAVE D — calibration wizard rebuild (webui; uses the full spec from the audit)

Number-in-the-middle direct manipulation, per Christian's design: the big centered
number IS the currently-lit LED index; a full-width slider (0..ledCount-1) plus ±1
fine arrows and a position bar; "move the light onto your LOWEST key, press it" →
confirm (shows note name + LED) → same for HIGHEST key → PUT
`{tier:"multiPoint", landmarks:[2]}` (device keeps doing the interpolation — lowest-risk;
`twoPoint`-scalar alternative noted in the spec). Every probe screen gets a persistent
inline error panel with Retry instead of a 2.6 s toast (today arm failures strand the
user on an instruction screen); debounced re-arm on slider moves; reversed-strip toggle;
anchor-order contradiction auto-sets `reversed` or asks; clamp-at-strip-end messaging;
verify + fine-tune screens adopt the same readout. Kill the decorative pulsing dot.

### WAVE E — polish + docs (fold into adjacent waves opportunistically)

API.md `## Shows` section (5 live routes undocumented); load-nonexistent-song → 404;
405 for wrong-method (optional); CORS on `/api/reboot`; `formatVersion` from
`Show::kVersionMajor`; editor: min cue span (`endMs > startMs` — today a zero-length
clip bricks the whole show at play), drop `durationMs/clockSource` from device rows,
validate scope `lo<=hi`, clamp `speed ≥ 1`; palette reset when `paletteRef==0xFF`
(state bleed between cues); BLE: scan backoff when piano absent (radio contention with
uploads), optional target-name/address setting (today it bonds to ANY BLE-MIDI device
in range), echo credits registered at transmit-time not emit-time (fold into FIX-C —
today reconnects can swallow the first real press); `gConnected` → `std::atomic`;
comment that Active Sensing must never feed `onActivity`; hex color strictness; emit
`idleSec` usage or drop it; `mode` omitted/nulled when no song loaded (the
"topMode:reactive + mode:wait" confusion); unison-note color precedence; effect buffer
write guards (`j < f.leds.size()`); `kMaxMilliamps` 8000 → measure at bring-up;
count-in BPM micro-copy ("visual click only — take tempo is set in the editor").

---

## 4. Explicitly NOT bugs (verified, don't re-fix)

- Wait-mode matcher: A98 epsilon/armFrom, A99 lighting — traced adversarially, sound.
- Power cap: architecturally unbypassable; every producer routes through the one show().
- AFK brightnessCap enforcement (applies to fallback too); afkTimeoutSec units; AFK
  arming iron rule (structurally can't fire mid-practice).
- URL encoding client↔firmware (single-encode round-trip verified); path traversal
  (validName allowlist at every path-build site); JSON body caps; torn statusJson
  (fence verified); MIDI parse robustness on truncated files (bounds-checked, 400s
  cleanly); `.vls` interpreter hardening; BLE-MIDI parsing (velocity-0, running
  status, message ordering); LED-gap between keys = intended 1 mm anti-overlap margin.

## 5. Post-flash REST verification checklist (Wave A acceptance)

```
POST /api/songs/{name}/load        → 200 + statusJson (was 400 "empty upload")
POST /api/songs/unload             → 200               (was 400)
POST /api/songs/{name}/rename      → 200               (was 400 "missing ?name=")
POST /api/shows/stop               → 200               (was 400)
GET  /api/calibration/probe        → probeJson         (was calibrationJson)
POST /api/calibration/probe {led}  → 200 armed         (was 400 "bad tier")
POST /api/afk/control {action}     → 200, config UNCHANGED (was silent wipe)
POST /api/songs?name=x.mid (oversize) → 507 JSON, no partial left, list unchanged
POST /api/songs?name=x.mid (fits)  → 201, file present, delete works
GET  /api/status                   → has fsFree/heapFree/uptimeMs
POST /api/storage/format (token)   → 200, FS empty, uploads work
```

## 6. Christian decision queue (everything else proceeds without him)

1. **wifiPass redaction** — recommend write-only (`wifiPassSet:true` in GET/export).
2. **`formatOnFail` at boot** — today a mount hiccup silently erases every song/take;
   recommend `false` + surfaced `fs:"error"` state + manual format route.
3. **Upload over the currently-loaded song** — recommend 409 (matches DELETE).
4. **AFK next/prev wake semantics** — API.md and DESIGN-lightshow disagree; recommend
   media-control (no wake) and amend API.md.
5. **Wizard target tier** — recommend multiPoint-with-2-landmarks (device interpolates).
6. **Ear/eye items at bring-up** — kMaxMilliamps headroom, BLE scan backoff values,
   wrong-flash duration.

## 7. Deferred to hardware bring-up (existing list still stands)

Serial capture of the wedge errno (needs USB re-seat — do this FIRST), FIX-C disconnect
seam (now includes the phantom-echo-credit mechanism), G10, A91 echo-window tuning,
real full-white current draw, RMT-vs-BLE coexistence at 60 fps, upload throughput after
the single-File-handle change.
