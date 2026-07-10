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
    "positionMs": 12345,
    "durationMs": 98765,
    "tempoPercent": 100,
    "loop": {"enabled": false, "startMs": 0, "endMs": 0},
    "tracks": [                       // from the loaded song
      {"index": 0, "name": "Right", "hand": "right", "lights": true}
    ],
    "pendingNotes": [60, 64],         // wait mode: keys currently owed
    "topMode": "practice",            // v2 M3: reactive | afk | practice | presentation
    "idleSec": 12,                    // seconds since the last activity
    "afkTimeoutSec": 180,             // mirror of the setting (0 = never)
                                      // (the three fields above appear in EVERY
                                      // status-JSON reply, unlike wifi)
    "wifi": {"mode": "sta", "ip": "192.168.1.50"}  // mode: sta | ap — LAST key
  }
  ```
  The UI polls this (~2×/s while open). Cheap to serve; no allocation storms.

## Songs

- `GET /api/songs` → `[{"name": "ode-to-joy.mid", "size": 4321}]`
  (LittleFS `/songs/` directory listing; names are sanitized filenames.)
- `POST /api/songs` — raw body upload, `?name=<filename>.mid` query param.
  → `201 {"name": "..."}`. Rejects non-`.mid` names and files > 256 KB.
- `DELETE /api/songs/{name}` → `204`.
- `POST /api/songs/{name}/load` → `200` + status JSON. Parses the file,
  resets transport to 0, state `idle`.

## Track / hand assignment

- `PUT /api/tracks/{index}` body `{"hand": "left|right|both|off", "lights": true|false}`
  → `200`. `hand` drives wait-mode practice masks + accompaniment; `lights`
  toggles that track's LEDs (SPEC: either hand's lights can be turned off).
  Default assignment on load: track named "Left"/"LH" → left, "Right"/"RH" →
  right; else 2-track songs get track 0=right, 1=left (piano convention);
  single track → both.

## Transport

- `POST /api/transport` body `{"action": "play"}` — actions:
  - `play` (from idle/finished restarts at 0; from pause resumes)
  - `pause`
  - `stop` (position → 0, all lights/notes off)
  - `seek` with `"positionMs": N`
  → `200` + status JSON.

## Mode

- `POST /api/mode` body `{"mode": "wait|follow|demo|accompaniment", "practice": "left|right|both"}`
  → `200`.
  - `practice` = the hand YOU play (default `both`). In `wait` mode it selects
    which tracks arm the barrier; in `accompaniment` the *other* hand's tracks
    are sent to the piano while wait mode watches the practiced hand.
  - `demo`/`follow` ignore `practice`.

## Top mode (v2 M-wave)

The four top modes sit ABOVE the practice sub-modes: Reactive ⇄ AFK with no
song (idle timeout arms AFK; any key or state-changing call wakes it — and
AFK can never fire while a song is loaded), Practice ⇄ Presentation with
one. Only the Practice⇄Presentation edge is directly settable; the rest
follow from song state and activity.

- `POST /api/topmode` body `{"mode": "presentation"}` or `{"mode": "practice"}`
  → `200` + status JSON, `400 no song loaded` (presentation needs one),
  `400 bad mode`. Unloading the song drops presentation automatically.
- The AFK idle timeout is the `afkTimeoutSec` setting (0 = never).
- `POST /api/songs/unload` → `200` + status JSON: back to the no-song state
  (song/transport/loop cleared; settings and calibration untouched). Like
  every state-changing call it resets the idle clock, so the AFK drift
  restarts from the unload.

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
  Effects: `rainbow confetti sinelon juggle bpm fire2012 pacifica
  twinklefox colorwaves pride2015`. Palettes: `rainbow ocean forest lava
  party cloud heat` (`""` = the effect's default). An empty playlist falls
  back to a gentle rainbow.
- `PUT /api/afk` — same shape → `200` + the stored config, or
  `400 {"error": "unknown effect: X" | "unknown palette: X" |
  "too many tracks (max 16)" | "bad json"}`.
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

- `GET /api/settings` → exactly the JSON of `Settings::toJson()`:
  ```json
  {
    "leftColor": "#0000FF", "rightColor": "#00FF00", "wrongColor": "#FF0000",
    "previewCap": 0.45, "leadMs": 1000,
    "offsetMm": 0.0, "ledsPerMeter": 180.0,
    "brightness": 160, "echoWindowMs": 250,
    "wifiSsid": "HomeNet", "wifiPass": "...",
    "repeatCueEnabled": true, "repeatColor": "#FFFFFF",
    "repeatFillStartPct": 0, "repeatFillPeakPct": 45,
    "repeatFloorMs": 35, "repeatWaitPulseMs": 60,
    "afkTimeoutSec": 180
  }
  ```
  The `repeat*` fields are the v2 "Incoming Re-press" cue (Q-wave growth —
  appended; nothing existing changed). Percents are 0–100 (`repeatFillPeakPct`
  100 = pure hue-snap glide at onset). A `repeatColor` equal to `wrongColor`
  is rejected (the field keeps its previous value) — a cue must never look
  like an error.
- `PUT /api/settings` — same shape, partial OK (missing fields unchanged)
  → `200` + full new settings. Persisted to LittleFS immediately.
  WiFi changes apply on next reboot (`POST /api/reboot` to apply now).

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
  ranges overlap / note out of range / led off strip).
  Persisted to `/calibration.json`. Boot with no (or unreadable) file seeds
  the table from the settings' 2-point values — byte-identical to v1.
- `POST /api/calibration/probe` body `{"led": 123, "timeoutMs": 30000}`
  (`timeoutMs` optional, clamped 1s–5min) — lights a single white dot at
  that LED and captures the NEXT piano note-on as the answer (the press
  never reaches practice/wait mode). → `200` + probe status, `400 missing
  led` (no `led` in the body), `400 bad led` (off the strip), `409 playing`
  (probe never arms during playback; starting playback cancels an armed
  probe).
- `GET /api/calibration/probe` → `{"armed": true, "led": 123, "note": null}`
  (`note` = the captured MIDI note after the press; poll ~2×/s while armed).
- `DELETE /api/calibration/probe` — cancel; clears any capture. → `200`.

## Utility

- `POST /api/reboot` → `200` then restarts.
- `POST /api/test` body `{"pattern": "strip"}` — patterns `strip` (walk a
  white dot end to end), `rainbow` (the easter egg), `off`. For BRINGUP.md.
- `GET /api/ble` → `{"connected": true, "device": "BLE-MIDI"}` (`device` is
  `""` while disconnected).

## Design rules

- Web UI is a remote control: ALL state lives on the ESP32; the UI renders
  `/api/status` and sends commands. No client-side state that survives reload.
- Every handler stays fast — LED/wait-mode latency outranks HTTP (iron rule).
- No external fonts/CDNs/frameworks in the UI. Vanilla JS, one file.
