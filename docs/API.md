# Via Lucis — REST API contract (v1 + v2 growth)

The seam between the firmware (`firmware/src/`) and the web UI (`webui/`).
**Both sides build against this file. Change it here first or not at all.**

All bodies are JSON (`Content-Type: application/json`) except the song upload.
Errors: non-2xx with `{"error": "<human message>"}`.

## Serving

- `GET /` → the single-page web UI (gzip-embedded in firmware flash,
  `Content-Encoding: gzip`).
- The UI is one file: `webui/index.html` (inline CSS + JS, no external assets —
  the ESP32 may be reachable only over its own AP with no internet).

## Status

- `GET /api/status` →
  ```json
  {
    "version": "0.1.0",
    "song": "ode-to-joy.mid",        // "" if none loaded
    "state": "idle",                  // idle | playing | waiting | finished
    "mode": "wait",                   // wait | follow | demo | accompaniment
                                      // OMITTED entirely with no song loaded
                                      // (A160, §3-E item 8) — mode_ defaults
                                      // to "wait" internally regardless, so
                                      // emitting it there used to read as
                                      // "topMode":"reactive"+"mode":"wait"
                                      // (practice active with nothing
                                      // loaded). Feature-detect: the webui
                                      // already disables every mode control
                                      // while no song is loaded.
    "positionMs": 12345,
    "durationMs": 98765,
    "tempoPercent": 100,
    "loop": {"enabled": false, "startMs": 0, "endMs": 0},
    "tracks": [                       // from the loaded song
      {"index": 0, "name": "Right", "hand": "right", "lights": true}
    ],
    "pendingNotes": [60, 64],         // wait mode: keys currently owed
    "topMode": "practice",            // v2 M3: reactive | afk | practice | presentation | record
    "idleSec": 12,                    // seconds since the last activity
    "afkTimeoutSec": 180,             // mirror of the setting (0 = never)
    "practice": "both",               // C1: player's last-chosen practice hand
                                      //   (both | left | right) — lets the UI
                                      //   selector reconcile after a reload
                                      //   instead of resetting the device
                                      // (the four fields above appear in EVERY
                                      // status-JSON reply, unlike wifi)
    "record": {                       // v3 REC4: the tape head (before wifi)
      "state": "idle",                //   idle | armed | recording
      "elapsedMs": 0,                 //   ms since the first captured note
      "usedBytes": 0,                 //   raw event bytes captured so far
      "budgetBytes": 65536,           //   recordBudgetKB * 1024
      "countIn": false,               //   Free-capture count-in requested
      "bpm": 90,                      //   count-in BPM (clamped 20-300)
      "overflowed": false             //   B5: capture hit a byte/duration limit
                                      //     and dropped events (take truncated)
    },
    "fs": "ok",                       // A3: "ok" | "error" — LittleFS health
                                      //   ("error" = mount failed OR wedged)
    "fsFree": 512000,                 // LittleFS free bytes
    "fsTotal": 917504,                // LittleFS total bytes (partition size)
    "fsUsed": 405504,                 // LittleFS used bytes
    "heapFree": 142000,               // ESP.getFreeHeap()
    "heapMaxAlloc": 110000,           // largest contiguous heap block
    "uptimeMs": 3600000,              // ms since boot
    "configReset": false,             // B4: true when boot self-heal reset a
                                      //   corrupt persisted config to defaults
                                      //   — the UI should tell the user to
                                      //   re-check settings/calibration/ambient
                                      //   (the eight fields above appear ONLY
                                      //   on GET /api/status, like wifi)
    "wifi": {"mode": "sta", "ip": "192.168.1.50"}  // mode: sta | ap — LAST key
  }
  ```
  The UI polls this (~2×/s while open). Cheap to serve; no allocation storms.
  The `fs*` fields are the single source of storage capacity — the library
  capacity gauge reads them here (GET /api/songs stays a bare list).

## Songs

- `GET /api/songs` →
  `[{"name": "ode-to-joy.mid", "size": 4321, "parseOk": true}]`
  (LittleFS `/songs/` directory listing; names are sanitized filenames.) A
  bare array — capacity/free space is NOT here; read `fsFree`/`fsTotal` from
  `GET /api/status`. `parseOk` (A164, §3-E item 12 — the queued Wave C ask)
  is `false` when the file's MIDI fails to parse (a corrupt/partial upload
  that still landed on the device) OR when the file is beyond what the
  device's RAM can parse/load at all (A182 — such a song can't play on this
  hardware, so the badge is honest). `parseOk` may be **absent** on a song
  the device simply hasn't checked yet: checks are budgeted a few per list
  call (A183) so a cold cache can't stall the request — absent means
  "unknown, warming up", never "bad"; it fills in across subsequent list
  fetches. The webui badges only an explicit `parseOk: false`, so a player isn't
  left guessing why a song silently won't load. It's cheap: a per-boot,
  in-RAM cache keyed by (name, size) means re-parsing only happens for a
  song this call has never seen or whose byte size just changed (a
  re-upload/overwrite) — NOT on every poll.
- `POST /api/songs` — raw body upload, `?name=<filename>.mid` query param.
  → `201 {"name": "..."}`. Rejects non-`.mid` names and files > 256 KB.
  Errors are deferred to the end of the transfer and returned as JSON (never a
  mid-body TCP reset), and a rejected upload leaves no partial file behind:
  - `400` — missing `?name=`, bad/non-`.mid` name.
  - `413 {"error": "file too large (256KB max)"}` — announced size over 256 KB.
  - `507 {"error": "insufficient storage"}` — block-rounded size plus a safety
    reserve wouldn't fit in free space (checked before the first byte is
    written, so it can never wedge the filesystem).
  - `409 {"error": "song is loaded"}` — the target name is the currently-loaded
    song; unload it first (overwriting a live song would desync playback).
- `DELETE /api/songs/{name}` → `204`, or `404 {"error": "no such song"}`, or
  `409 {"error": "song is loaded"}` if it's the currently-loaded song
  (`POST /api/songs/unload` it first — otherwise status/practice would keep
  reporting a "loaded" song the list no longer has).
- `POST /api/songs/{name}/load` → `200` + status JSON. Parses the file,
  resets transport to 0, state `idle`. Typed failures (A154, §3-E item 2 —
  these used to collapse into one generic 400): `404 {"error": "no such
  song"}` when the name isn't on the device, `400 {"error": "cannot load
  song"}` only when the file exists but its MIDI fails to parse.
- `POST /api/songs/{name}/rename` body `{"name": "new-name.mid"}` →
  `200 {"name": "new-name.mid"}`. `400` bad/non-`.mid` name, `404` missing
  source, `409 {"error": "exists"}` if the target name is taken. (Used to
  rename a recorded take; general-purpose for any song.)

### Upload contract (T5/T6, DESIGN-library.md §4, L2)

The definitive shape of `POST /api/songs`, written so webui and any future PC
sync agent (Phase 2, held) implement the SAME behavior instead of drifting.

- **Request shape.** Raw request body = the file bytes, exactly. No
  multipart/form-data, no JSON envelope. `?name=<filename>.mid` is the only
  parameter, and it's on the query string, not the body. `Content-Length`
  must be present and accurate — it is the "announced size" every precheck
  below is based on, checked BEFORE the body is read off the socket (so a
  doomed upload never lands a partial file).
- **Whole-file only — NOT resumable.** Every `POST /api/songs` is one
  complete file, start to finish, in a single request. There is no offset/
  range parameter, no "continue this upload" verb, and no partial-file
  commit. A dropped connection mid-transfer means the WHOLE file is retried
  from byte 0 next attempt (the server-side cleanup already removes the
  partial — see A2 in BUGFIX-PLAN-2026-07-15). This is a decided scope limit
  (DESIGN-library.md §4), not an oversight: real resumable upload needs
  firmware support that does not exist yet (temp file + offset param +
  commit/abort verb + a content checksum to verify the resumed range lines
  up) — see "Future: resumable upload" below.
- **Free-space precondition (507).** Before accepting any bytes, the
  precheck mirrors `firmware/lib/core/src/vialucis/storage_budget.h`
  EXACTLY: round the announced size up to whole 4 KB LittleFS blocks
  (`kFsBlockBytes`), add a fixed 32 KB safety reserve (`kUploadReserveBytes`
  — the headroom a dir-metadata split needs so the filesystem can never hit
  the hard-full wedge), and refuse with `507 {"error": "insufficient
  storage"}` if that sum exceeds `fsFree` (from `GET /api/status`). A
  well-behaved client (webui's bulk uploader, T6) runs this SAME math
  client-side before even starting an upload, so it can skip a doomed
  attempt with a clear "would eat into the reserve" message instead of
  discovering it via a failed request. The device's own precheck is still
  the final authority — a stale client-side `fsFree` reading never causes a
  wedge, only a wasted round trip.
- **Retry policy.** `507` (out of space) and `409` (target is the
  loaded song) are typed, durable refusals — retrying them changes nothing,
  so clients should surface them immediately and NOT retry. Every other
  failure (a network-level drop/reset, any other HTTP status, or a
  verify-after-write mismatch — see below) should be retried whole-file, up
  to a small bounded attempt count, with a backoff between attempts. Both
  `tools/bulk_upload.py` (the reference implementation) and the webui bulk
  uploader (T5) use **3 attempts, backoff = 1.5s × attempt number**
  (so ~1.5s then ~3s between attempts 1→2→3). There is no different
  policy for "the webui" vs "a sync agent" — one retry contract, one place
  it's written down (here).
- **Verify-after-write (recommended, not optional).** A `201` reply is not
  proof the file is correct on the device — re-`GET /api/songs` after every
  upload and confirm the uploaded name appears with the exact byte size
  sent. A name+size mismatch (or the name missing entirely) should be
  treated as a failure and retried under the same policy above. This is the
  same reason `GET /api/songs` is the reconciliation source of truth for any
  future sync agent (DESIGN-library.md §4) — never trust a local record of
  what "should" be on the device.
- **`409` loaded-song rule.** `POST /api/songs?name=X` where `X` is the
  currently-loaded song's name always `409`s (`{"error": "song is
  loaded"}`), even if the new content is identical — overwriting a live
  song's backing file while it's the active playback source would desync
  transport/practice state. The client must `POST /api/songs/unload` (or let
  the user do so) before a same-name re-upload can proceed. Bulk tooling
  should surface this as an actionable message ("currently loaded — unload
  it first"), never auto-unload silently.
- **Atomic swap guidance (replace-in-place).** When a client wants to swap
  one song for a re-exported version of itself: **upload-then-delete when
  the free-space precheck allows it** (upload the new content under a
  temporary/different name, verify it, delete the old, rename into place —
  or simplest, if the name differs, just upload new + delete old) so a
  failed upload never leaves the device with neither copy. Only when
  capacity is tight enough that the new copy can't coexist with the old
  should a client fall back to delete-first — and even then, a client
  should WARN the user and let them decide, rather than automatically
  deleting anything on their behalf (T6's bulk-manage screen never
  auto-deletes to make room; it skips the upload and says so).
- **Future: resumable upload (NOT implemented — needs firmware).** If a
  future wave adds true resumable/chunked upload, the shape sketched in
  DESIGN-library.md §4 is: a temp-file handle keyed by an upload id, an
  `offset` param on each chunk POST, an explicit `commit`/`abort` verb, and
  a whole-file checksum client and device agree on before commit (so a
  resumed upload can't silently splice mismatched ranges together). Until
  that lands, EVERY client — webui, `tools/bulk_upload.py`, and any Phase 2
  sync agent — implements whole-file retry + verify exactly as described
  above, so behavior never diverges by client.

## Track / hand assignment

- `PUT /api/tracks/{index}` body `{"hand": "left|right|both|off", "lights": true|false}`
  → `200`. `hand` drives wait-mode practice masks + accompaniment; `lights`
  toggles that track's LEDs (SPEC: either hand's lights can be turned off).
  Default assignment on load: track named "Left"/"LH" → left, "Right"/"RH" →
  right; else 2-track songs get track 0=right, 1=left (piano convention);
  single track → both.
  Reassigning a hand mid-note in demo/accompaniment flushes note-offs for any
  note the leaving track was sounding on the piano (B3a — no ringing note left
  behind), routed through the same MIDI-out/echo-guard path as ordinary play.

## Transport

- `POST /api/transport` body `{"action": "play"}` — actions:
  - `play` (from idle/finished restarts at 0; from pause resumes)
  - `pause`
  - `stop` (position → 0, all lights/notes off)
  - `seek` with `"positionMs": N`
  → `200` + status JSON.

## Mode

- `POST /api/mode` body `{"mode": "wait|follow|demo|accompaniment", "practice": "left|right|both"}`
  → `200`, or `400 bad mode` (also returned while a Presentation show is
  playing — a show owns its practice sub-mode as its own clock; switching it
  mid-performance would freeze the show on stage. Stop the show first via
  `POST /api/shows/stop`).
  - `practice` = the hand YOU play (default `both`). In `wait` mode it selects
    which tracks arm the barrier; in `accompaniment` the *other* hand's tracks
    are sent to the piano while wait mode watches the practiced hand.
  - `demo`/`follow` ignore `practice`.

## Top mode (v2 M-wave)

The four top modes sit ABOVE the practice sub-modes: Reactive ⇄ AFK with no
song (idle timeout arms AFK; AFK can never fire while a song is loaded),
Practice ⇄ Presentation with one. Only the Practice⇄Presentation edge is
directly settable; the rest follow from song state and activity.

**Two activity categories (ruling §6-4).** Not every request touches the idle
clock:
- **State mutations WAKE the idle clock** (reset the AFK drift): any key press,
  song load/unload, transport, mode/tempo/loop/track writes, settings,
  calibration, record arm/stop/discard, presentation/shows start·stop, the test
  pattern, AFK config writes (`PUT /api/afk`), and probe arm/cancel.
- **Ambient transport STEERS without waking**: `POST /api/afk/control`
  (`next`/`previous`) advances a playing AFK show but NEVER touches the idle
  clock — waking there would dismiss the very show it is steering.

- `POST /api/topmode` body `{"mode": "presentation"}` or `{"mode": "practice"}`
  → `200` + status JSON, `400 no song loaded` (presentation needs one),
  `400 bad mode`. Unloading the song drops presentation automatically.
- The AFK idle timeout is the `afkTimeoutSec` setting (0 = never).
- `POST /api/songs/unload` → `200` + status JSON: back to the no-song state
  (song/transport/loop cleared; settings and calibration untouched). Like
  every state-changing call it resets the idle clock, so the AFK drift
  restarts from the unload.

## Shows (P2; docs/SHOW-FORMAT.md — A165, §3-E item 1)

Baked `.vls` presentation streams (SHOW-FORMAT.md), stored under `/shows/`
alongside songs but capped separately (**64 KB per show, 384 KB / 16 shows
total** — `SongStore::kMaxShowBytes/kMaxShowTotalBytes/kMaxShowCount`). A
show plays ON TOP of a loaded song (Presentation top mode) — see "Top mode"
above.

- `GET /api/shows` →
  ```json
  {"formatVersion": 1, "shows": [{"name": "intro.vls", "size": 2048}]}
  ```
  `formatVersion` is `Show::kVersionMajor` (the parser's own compatibility
  constant — SHOW-FORMAT's "refuse a stream whose major version is newer
  than ours" rule), not a hand-copied literal. `shows` is a bare list, same
  shape as `GET /api/songs`.
- `POST /api/shows` — raw body upload, `?name=<filename>.vls` query param.
  Mirrors the song upload contract above (whole-file, `Content-Length`
  precondition, atomic tmp+rename, deferred errors so a rejection is JSON
  never a TCP RST) with show-specific caps:
  → `201 {"name": "..."}`.
  - `400` — missing `?name=`, bad/non-`.vls` name.
  - `409 {"error": "busy"}` — a show is currently rendering live; uploads
    are refused while anything plays (never race the render path, OV1).
  - `413 {"error": "show too large"}` — announced size over 64 KB.
  - `507 {"error": "show storage full"}` — the 16-show count cap or the
    384 KB total-bytes cap would be exceeded. Overwriting an EXISTING
    same-name show nets out its old bytes first (an edit → re-save loop
    doesn't double-count itself against the cap).
  - `507 {"error": "insufficient storage"}` — the same block-rounded
    free-space precheck the song upload uses (shows share the LittleFS
    partition with songs).
- `DELETE /api/shows/{name}` → `204`, or `404 {"error": "no such show"}`.
- `POST /api/shows/{name}/play` → `200` + status JSON (enters Presentation
  top mode and starts the show's own clock — demo-clock or score-follow per
  the stream's `clockSource`). Typed refusals:
  - `404 {"error": "no such show"}` — no file by that name.
  - `400 {"error": "<parse error>"}` — the stream failed
    `Show::parse` (bad magic, newer major version, truncated, a malformed
    section/cue, an unregistered effect name, or over the 64 KB whole-load
    cap) — the message text is `ShowResult::message()`, stable across
    calls.
  - `400 {"error": "no song loaded"}` — Presentation needs a loaded song
    underneath it (load one first).
  - `409 {"error": "busy"}` — practice is already `Playing` (a live
    practice session is never hijacked by starting a show).
- `POST /api/shows/stop` → `200` + status JSON. Tears down the render and
  restores the practice mode/hand the player had before the show started
  (a stray stop with nothing playing is a harmless no-op, not an error).

## Record (v3 Record wave)

Record your own playing to a `.mid` on the device (docs/DESIGN-record.md). The
tape head is armable in two contexts: **Free capture** (no song loaded — the
Record top-mode, lights follow you) and **Play-along capture** (a song loaded —
Practice keeps the strip, capture runs alongside; the "REC" indicator is
web-UI-only). Capture starts on your first note (leading silence trimmed) and
excludes any note the device sent the piano (echo guard). Live state is in the
`record` object of `GET /api/status`.

- `POST /api/record/arm` — body OPTIONAL `{"countIn": true, "bpm": 90}`
  (count-in is Free-capture only; `bpm` clamped 20–300; both ignored when a
  song is loaded). A bodyless POST arms with count-in off. → `200` + status
  JSON. Typed refusals:
  - `409 {"error": "already armed"}` — a take is already armed/recording.
  - `409 {"error": "playing"}` — a presentation light show is playing (arming
    is forbidden in that display context).
  - `409 {"error": "unsaved take pending"}` — a previous save failed and the
    take is still held (B5): `POST /api/record/retry-save` or
    `POST /api/record/discard` it before starting a new one.
  - `507 {"error": "low space"}` — LittleFS free space can't cover the byte
    budget (`recordBudgetKB` + a small margin).
  - `507 {"error": "low memory"}` — the device can't reserve the budget as
    one contiguous RAM block right now (arm pre-allocates the whole capture
    buffer). Lower `recordBudgetKB` or reboot.
  - `400 {"error": "bad json"}` — unparseable body.
- `POST /api/record/stop` — finalize + save → `200 {"name": "recording-<n>.mid"}`,
  and the take appears in `GET /api/songs` (an ordinary song: practice / editor
  / `.vls` / score-follow all apply). Auto-named `recording-<n>.mid` (next free
  n). The raw take is hand-split at middle C (60): notes < 60 → a `Left` track,
  ≥ 60 → a `Right` track (empty hands omitted; pedals attach to the first
  track) — the editor fixes crossovers per-note. A stop with **zero** captured
  events saves nothing → `200 {"name": ""}`. `409 {"error": "not armed"}` if
  idle.
  - `200 {"name": "recording-<n>.mid", "truncated": true}` (B5) — the take saved
    but capture had to drop events at a byte/duration limit; the `truncated`
    flag is present only in that case so the UI can warn.
  - `500 {"error": "write failed"}` — the save failed (full/wedged FS) but the
    performance is **retained** (B5): retry with `POST /api/record/retry-save`,
    or drop it with `POST /api/record/discard`. It is NOT lost.
- `POST /api/record/retry-save` (B5) — re-attempt the last failed save without
  re-recording → same replies as `/stop` (`200 {"name":...}` /
  `200 {"name":...,"truncated":true}` / `500 "write failed"` still held /
  `409 "not armed"` when nothing is pending). On success the held take is freed.
- `POST /api/record/discard` — drop the armed/recording state OR a held
  unsaved take, save nothing → `200` + status JSON. `409 {"error": "not armed"}`
  if idle with nothing pending.
- Duration cap is a compile-time constant (~10 min); the byte budget is the
  `recordBudgetKB` setting.

## Ambient (AFK) playlist (v2 E-wave)

The document behind the AFK top mode (VL2 — its own file `/afk.json`, its
own route). Tracks are effect configs played top→bottom→loop.

- `GET /api/afk` →
  ```json
  {
    "tracks": [{"effect": "pacifica", "palette": ""},
               {"effect": "fire2012", "palette": "heat"}],
    "shuffle": false, "repeatCurrent": false,
    "dwellSec": 60, "crossfadeMs": 2000,
    "brightnessCap": 96, "masterSpeed": 1.0,
    "aboveKeysOnly": false
  }
  ```
  Effects: `fire2012 pacifica twinklefox colorwaves`. Palettes: `rainbow
  ocean forest lava party cloud heat` (`""` = the effect's default). An
  empty playlist falls back to a gentle color wave.
- `PUT /api/afk` — same shape → `200` + the stored config, or
  `400 {"error": "unknown effect: X" | "unknown palette: X" |
  "too many tracks (max 16)" | "bad json"}`, or
  `507 {"error": "insufficient storage"}` (B4: applied live but the flash write
  failed).
  `dwellSec` clamps to 5–86400, `masterSpeed` to 0.25–4, `crossfadeMs` to
  ≤10s; a config PUT that only tunes dials (same track list) never restarts
  the running show.
  `brightnessCap` ships conservative (96) — ambient runs unattended and
  still passes the global power cap downstream.
- `POST /api/afk/control` body `{"action": "next"}` or `"previous"` → `200`.

## Tempo / loop

- `POST /api/tempo` body `{"percent": 85}` (1–500, clamped) → `200`.
- `POST /api/loop` body `{"enabled": true, "startMs": 45000, "endMs": 50000}`
  → `200`. `enabled: false` clears.

## Settings

- `GET /api/settings` → the **Public view** of the settings
  (`Settings::toJson(View::Public)`):
  ```json
  {
    "leftColor": "#0000FF", "rightColor": "#00FF00", "wrongColor": "#FF0000",
    "previewCap": 0.45, "leadMs": 1000,
    "offsetMm": 0.0, "ledsPerMeter": 180.0,
    "brightness": 160, "echoWindowMs": 250,
    "wifiSsid": "HomeNet", "wifiPassSet": true,
    "repeatCueEnabled": true, "repeatColor": "#FFFFFF",
    "repeatFillStartPct": 0, "repeatFillPeakPct": 45,
    "repeatFloorMs": 35, "repeatWaitPulseMs": 60,
    "afkTimeoutSec": 180,
    "recordBudgetKB": 64,
    "bleTargetName": ""
  }
  ```
  **`wifiPass` is write-only and NEVER returned** (ruling §6-1). The Public view
  emits **`wifiPassSet`** (`true` when a non-empty WiFi password is stored,
  `false` otherwise) so the UI can show whether a network is configured without
  ever exposing the secret. Any LAN/AP client used to be able to read the home
  WiFi password in cleartext here — that leak is closed, and the webui "Export
  settings" download (which is just this GET) no longer writes the secret to
  disk. Redaction is a property of the `wifiPass` field itself, not this route.
  The `repeat*` fields are the v2 "Incoming Re-press" cue (Q-wave growth —
  appended; nothing existing changed). `recordBudgetKB` is the v3 recording
  byte budget (default 64, clamped 16–256 KB — 256 = the per-song save
  ceiling, a bigger take is unsaveable; arm reserves the whole budget as one
  contiguous RAM block, so a stock ESP32 wants ≤64; PSRAM boards toward 256), the one
  sanctioned v1 contract growth. Percents are 0–100 (`repeatFillPeakPct`
  100 = pure hue-snap glide at onset). A `repeatColor` equal to `wrongColor`
  is rejected (the field keeps its previous value) — a cue must never look
  like an error.
  `bleTargetName` is the Wave E2 optional BLE-MIDI target filter — empty
  (default) accepts the first BLE-MIDI peripheral found; set it to an
  advertised device name to connect only to that one (useful with more than
  one BLE-MIDI device in range). Not a secret — appears in both views
  unchanged. Clamped to 20 characters. Like `wifiSsid`, a change takes
  effect at the **next reboot**, not live.
- `PUT /api/settings` — same shape, partial OK (missing fields unchanged)
  → `200` + the **Public view** of the new settings (again no `wifiPass`).
  Persisted to LittleFS immediately (atomically — see "Persistence" below).
  WiFi changes apply on next reboot (`POST /api/reboot` to apply now).
  - **`wifiPass` write-only semantics** (ruling §6-1): the flash/Persist copy
    keeps the password so the device reconnects after a reboot, but the field
    is accepted **write-only** on PUT and never read back. A body **with**
    `wifiPass` sets it; an explicit **`"wifiPass": ""` CLEARS** it (forget the
    network); a body **without** the key leaves the stored password unchanged.
    A client that isn't changing the password must **omit** the key — because
    GET no longer returns it, a naive "read settings, edit, PUT back" flow must
    not resend an empty `wifiPass` (that would wipe it). Set `wifiSsid` alone to
    switch network name without touching the stored password.
  - `507 {"error": "insufficient storage"}` — the settings applied live but the
    flash write failed (full/wedged FS). The device is not lying with a 200
    (B4); free space (delete songs, or `POST /api/storage/format`) and re-save.
  - `recordBudgetKB` clamps to **16–256 KB** (256 = the per-song save ceiling;
    a bigger take can't be written).

## Calibration (v2 C-wave)

The per-key LED table is the geometry truth; the wizard tiers are just ways
to fill it. `offsetMm`/`ledsPerMeter` in settings remain the 2-point tier's
inputs — changing them (either route) rebuilds the table on that tier only.

- `GET /api/calibration` →
  ```json
  {
    "tier": "twoPoint",              // twoPoint | multiPoint | perKey
    "reversed": false,               // right-to-left strip install
    "ledCount": 360,
    "offsetMm": 0.0,                  // twoPoint only
    "ledsPerMeter": 180.0,            // twoPoint only
    "landmarks": [{"note": 21, "led": 2}],   // multiPoint only
    "keys": [{"note": 21, "first": 0, "last": 1}]  // always: the table (valid keys)
  }
  ```
- `PUT /api/calibration` — body by tier (device rebuilds/validates the table;
  body `ledCount` is ignored — the device knows its strip):
  - `{"tier":"twoPoint","offsetMm":0,"ledsPerMeter":180,"reversed":false}`
    (writes the two scalars through to settings)
  - `{"tier":"multiPoint","landmarks":[{"note":21,"led":2}, ...]}` (>= 2,
    notes strictly ascending; descending LEDs = reversed mount)
  - `{"tier":"perKey","keys":[{"note":60,"first":150,"last":152}, ...]}`
    (unlisted keys stay dark)
  → `200` + the GET shape, or `400 {"error": "<typed message>"}` (bad json /
  bad tier / missing field / need at least 2 landmarks / landmarks must
  ascend by note / led direction inconsistent / key range off strip / key
  ranges overlap / note out of range / led off strip), or
  `507 {"error": "insufficient storage"}` (B4: the table applied live but the
  flash write failed — free space or `POST /api/storage/format`, then re-save).
  Persisted atomically to `/calibration.json`. Boot with no file seeds the
  table from the settings' 2-point values — byte-identical to v1; a *corrupt*
  file self-heals to that same default and raises `configReset` (see Status).
- `POST /api/calibration/probe` body `{"led": 123, "timeoutMs": 30000}`
  (`timeoutMs` optional, clamped 1s–5min) — lights a single white dot at
  that LED and captures the NEXT piano note-on as the answer (the press
  never reaches practice/wait mode). → `200` + probe status, `400 missing
  led` (no `led` in the body), `400 bad led` (off the strip), `409 playing`
  (probe never arms during playback OR a playing Presentation show — including
  score-follow, whose transport stays stopped; starting playback or a show
  cancels an armed probe), `409 recording` (probe never arms while a take is
  armed or recording — it would eat the performer's notes).
- `GET /api/calibration/probe` →
  `{"armed": true, "led": 123, "timedOut": false, "note": null}`
  (`note` = the captured MIDI note after the press; poll ~2×/s while armed).
  `timedOut` is true when the armed window expired without a capture —
  distinct from an explicit cancel, which leaves it false; cleared by the
  next arm or cancel.
- `DELETE /api/calibration/probe` — cancel; clears any capture. → `200`.

## Storage (A3)

- `POST /api/storage/format` body `{"confirm": "ERASE"}` → `200 {"formatting":
  true}`, then LittleFS is wiped and re-mounted. The **only** recovery from a
  hard-full wedge (see below) short of a USB reflash — destroys ALL songs,
  shows, settings, calibration, and ambient config. `400 {"error": "confirm
  token required"}` without the exact token. The reply is sent immediately; the
  seconds-long format runs on the device loop task (never on the network task),
  so poll `GET /api/status` (`fs` back to `"ok"`, `fsUsed` near zero) to confirm
  completion. Storage health is in `GET /api/status`'s `fs` field: `"error"`
  means the FS won't mount or has wedged (new-file creates fail while overwrites
  and deletes still work — the failure mode that motivated this route).
- **First-boot / unformatted device (B4, ruling §6-2).** Boot no longer
  auto-formats LittleFS (`formatOnFail=false` — a boot must never wipe real
  data as a reflex). A brand-new or corrupt-filesystem device therefore boots to
  `fs:"error"` (mount failed) **with the web UI still served from flash**
  (the UI lives in the firmware image, not LittleFS). The user recovers by
  running `POST /api/storage/format` **once** — this initialises the empty
  filesystem, and uploads then work normally. This is the expected first-run
  step for a freshly-flashed board.

## Persistence (B4)

- **Atomic saves.** Every persisted config doc (settings, calibration, ambient)
  and every record-take / song / show file is written to a `.tmp` sibling and
  then renamed over the target (LittleFS rename is atomic). A power loss or full
  FS during a save therefore leaves the **old** file intact — never a
  truncated-in-place partial. A save that can't complete returns `507` (see the
  PUT routes) instead of a lying `200`.
- **Schema.** Persisted config files carry a `"schema": 1` field for forward
  compatibility. Loaders tolerate an absent schema (a pre-B4 file) and reject a
  file written by a *newer* firmware (unknown-higher schema) rather than
  misreading it. This is a file-format detail — the `GET`/`PUT` wire shapes are
  unchanged (no `schema` field on the API).
- **Self-heal + `configReset`.** At boot, a config doc that is present but
  corrupt (unreadable, unknown-higher schema, or unparseable) is reset to
  defaults, re-saved atomically, and flagged via `configReset:true` in
  `GET /api/status` so the UI can tell the user to re-check their settings. An
  *absent* doc is the normal first-boot / upgrade path (silent defaults, no
  flag).

## Utility

- `POST /api/reboot` → `200` then restarts.
- `POST /api/test` body `{"pattern": "strip"}` — patterns `strip` (walk a
  white dot end to end), `rainbow` (the easter egg), `off`. For BRINGUP.md.
  Ordinary practice still auto-pauses while a pattern is up (F3/A35). A
  **playing Presentation show is left alone**: the pattern is a pure visual
  overlay that borrows the strip without touching the show's own clock — a
  demo-clock show's transport (and the piano audio it drives) keeps running
  underneath; a score-follow show's clock freezes for the duration (no
  transport to pause) and resumes tracking once the pattern goes `off`.
- `GET /api/ble` → `{"connected": true, "device": "BLE-MIDI"}` (`device` is
  `""` while disconnected).

## Design rules

- Web UI is a remote control: ALL state lives on the ESP32; the UI renders
  `/api/status` and sends commands. No client-side state that survives reload.
- Every handler stays fast — LED/wait-mode latency outranks HTTP (iron rule).
- No external fonts/CDNs/frameworks in the UI. Vanilla JS, one file.
