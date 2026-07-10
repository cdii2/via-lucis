# Via Lucis — Show format & presentation pipeline (P0, v2 P-wave)

**Status:** design spec, written 2026-07-10 per the P0 charter (eng-review 2A
hardening + OV1/OV4 pins). This file is the contract for P1 (device player),
P2 (clock sources), P3 (editor) — build against it; change it here first.

**The one open ruling:** VL3 editor hosting is **Christian's call, still
queued** (PROGRESS "Needs Christian"). Everything below builds against the
recommendation (off-device editor, release-artifact distribution), which the
P-POC proved viable (`editor/poc/README.md`: `file://` + CORS passes;
HTTPS-hosted → http device is a spec-mandated mixed-content block). The
ruling can flip *distribution* later without touching the format or player.

---

## 1. The baked cue stream (`.vls`)

The editor compiles arbitrarily deep authoring (groups, nesting, per-beat
snapping) into a FLAT, song-position cue stream — "the editor is not the
player" (iron rule). The ESP32 only ever interprets this.

### Container: chunked TLV

```
[0..3]  magic "VLS1"
[4]     u8 versionMajor   (this spec: 1)
[5]     u8 versionMinor   (this spec: 0)
[6..7]  u16 sectionCount
then sectionCount × section:
  u16 type · u32 byteLength · payload[byteLength]
```

- **Compatibility rule (2A):** the device REFUSES `versionMajor >` its own
  with the typed error `"show needs newer firmware"` — never garbage
  rendering. Unknown *section types* and unknown *cue flag bits* are
  SKIPPED/ignored (that is how P4's score-follow sections extend v1 streams
  without breaking them — OV4).
- All integers little-endian. Times are **song-time milliseconds** (u32) —
  the scheduler's song-time axis, so Free-run tempo scaling works exactly
  like practice playback.

### Sections (v1)

| type | name | payload |
| --- | --- | --- |
| 1 | META | u8 clockSource (0=demo, 1=freeRun, 2=scoreFollow) · u32 durationMs · zero-terminated show name (≤48 bytes) · **[optional] u8 followTrackIndex*** |
| 2 | EFFECTS | u8 count × zero-terminated factory name (the E-wave registry: `rainbow`…`pride2015`, plus `notedriven`) |
| 3 | PALETTES | u8 count × 16×3 bytes RGB (custom palettes; stock ones ride effect params) |
| 4 | CUES | u16 count × cue record (below) |
| 0xFFFF | END | empty; must be last |

*The optional trailing META byte (P4, frozen contract): the score-follow
track index. The editor emits it **only for clockSource=2**. `0xFF` = auto
(the device resolves the default follow scope at play time — the
right-hand/melody practiced mask, falling back to lights/all-tracks, A54);
`0..N` = a track index in the existing track space (an unusable index also
falls back to auto). **Absent ⇒ 0xFF.** Streams without the byte remain
valid — pre-P4 clockSource 0/1 shows parse unchanged. All score-follow
matching parameters (timing windows, tempo band, look-ahead depth) are
firmware constants in v1 — no TLV section; a richer per-show follow config
is a later-wave extension via new section types, which v1 firmware skips
per the compatibility rule above.

### Cue record (fixed 16 bytes + scope payload)

```
u32 startMs · u32 endMs        (endMs 0xFFFFFFFF = open-ended/looping)
u8  effectIndex                (into EFFECTS)
u8  blend                      (0 = opacity, 1 = additive)
u8  opacity                    (0..255, applied to the blend)
u8  drive                      (0 = autonomous, 1 = note-driven)
u8  scopeType                  (0 = all · 1 = pitch range · 2 = note set · 3 = note-binding)
u8  paletteRef                 (0xFF = effect default; 0x80|n = stock n; else PALETTES index)
u8  speed                      (fixed-point ×16: 16 = 1.0)
u8  flags                      (reserved; unknown bits ignored)
scope payload: type 1 → u8 lo, u8 hi (MIDI notes) · type 2 → u8 n, n×u8
notes · type 3 → u16 n, n×(u32 onsetMs, u8 note)  [the song's actual notes
in the span — first-class note-binding, baked by the editor]
```

Composition: cues render in stream order, **later over earlier** (the editor
sorts its visual top-to-bottom tracks accordingly at compile time). v1 blend
set = **opacity + additive** (P0 charter); more blend modes are a new flag /
minor bump.

### The JSON debug twin

The editor exports `.vls` **and** `.vls.json` — the same structure as
human-readable JSON. Failures stay inspectable: the device never reads the
JSON; humans and tests do. `tools/` gains a `vls_dump.py` (P1) that prints a
binary stream as its JSON twin for round-trip checks.

## 2. Storage & quota

- Shows live in LittleFS `/shows/<name>.vls` (name rules = song names, but
  `.vls`).
- **Per-show cap 64 KB** (refused at upload with `413 show too large`) —
  this is also the RAM decision: a playing show is **whole-loaded into a
  reserved buffer** (≤64 KB, allocated at play, freed at stop/unload).
  **No flash reads on any render path** — same discipline as practice.
  Streaming-from-flash is a future minor-version addition if real shows
  ever outgrow this (a 64 KB stream ≈ 3 500+ cues — plenty).
- **Total quota 384 KB / 16 shows** — `507 show storage full` (typed) on
  upload past either; the reply names the limit. Songs keep their own
  256 KB/file rule; both share the ~1 MB LittleFS partition.

## 3. REST protocol (device side, P1)

- `GET /api/shows` → `{"formatVersion": 1, "shows": [{"name","size"}]}`
  (per-item meta like durationMs would mean parsing every file on list —
  deferred; the editor reads meta from its own project files).
- `POST /api/shows?name=<n>.vls` — raw binary upload (chunked body intake,
  same as songs; disconnect mid-upload discards the partial file). Allowed
  in every mode EXCEPT while a show is playing or practice is Playing
  (`409 busy`) — uploads never race the render path (OV1).
- `DELETE /api/shows/{name}` → `204`.
- `POST /api/shows/{name}/play` → loads + validates + starts Presentation
  (song must be loaded for Demo/Free-run clocks that reference it; typed
  errors otherwise). `POST /api/shows/stop` → back to Practice.
- CORS: the P-POC header set (`Access-Control-Allow-Origin: *`, OPTIONS
  204, `Access-Control-Allow-Private-Network: true`) ships with these
  routes — the off-device editor depends on it.

## 4. Clock sources (P2)

| clockSource | drives | v1 |
| --- | --- | --- |
| Demo | device plays the song (emitter) and the show from ONE scheduler | ✔ |
| Free-run | tempo-scaled scheduler; the human keeps up | ✔ |
| Score-follow | clock slaved to the performer: `ScoreFollower` (a wait-mode-style matcher over the follow track) snaps to matched chord onsets and extrapolates at a clamped hybrid tempo, driving the Scheduler's song time | ✔ **(P4, per DESIGN-lightshow §4a / A53)** |

All three clocks read song-time ms from the existing `Scheduler` — tempo
changes mid-show keep Free-run continuous (named P2 test). Score-follow
keeps the transport STOPPED and writes song time directly from the
follower's estimate (the performer is the only clock; wrong notes are
ignored and never red-flash — performance mode). Still deferred to later
waves (NOT in v1): smooth backward tracking, ornament/trill/repeat
modelling, per-note anchor authoring, a confidence-driven pause — their
richer per-show config arrives as new TLV sections that v1 firmware
skips, no migration.

## 5. Editor (P3) & distribution

- Lives OFF-device in `editor/` (VL3 recommendation): a single-file static
  app (piano-roll over the loaded MIDI via a BUILT-IN SMF parser — a CDN
  dependency like `@tonejs/midi` can't load from `file://`, the proven
  distribution; canvas timeline, effect clips with span/scope/drive,
  grouping+nesting), compiling to the cue stream + JSON twin, uploading
  via `/api/shows`.
- **Distribution: a release artifact** (`editor.html`) the user downloads
  and opens locally — `file://` + CORS proven by the P-POC. The repo/Pages
  page links the download; it does NOT host the live editor (mixed-content
  block).
- **Version matching:** the editor embeds the format version it emits and
  reads the device's supported `versionMajor` from `GET /api/shows` (the
  list reply carries `"formatVersion": 1`); it refuses to upload newer-major
  streams with a human message that names both versions.

## 6. Out of scope for v1 (recorded, not built)

Extended blend modes; show export/share format + xLights import; the
falling-notes screen view; AFK starter catalog. Score-follow's later-wave
extensions (smooth backward tracking, ornament/repeat modelling, per-note
anchor authoring, confidence-driven pause) extend via new TLV sections —
no migration. The v1 score-follow clock itself shipped at P4 (§4).
