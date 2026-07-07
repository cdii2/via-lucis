# Via Lucis — REST API contract (v1)

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
    "wifi": {"mode": "sta", "ip": "192.168.1.50"}  // mode: sta | ap
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
    "wifiSsid": "HomeNet", "wifiPass": "..."
  }
  ```
- `PUT /api/settings` — same shape, partial OK (missing fields unchanged)
  → `200` + full new settings. Persisted to LittleFS immediately.
  WiFi changes apply on next reboot (`POST /api/reboot` to apply now).

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
