# P-POC — editor↔device connectivity proof (OV1)

Proves (or breaks) the VL3 assumptions before the P-wave builds months of
editor on top of them: can an OFF-device browser page talk to the device
API? Open `connectivity.html` and run the matrix against a device (or
`tools/mock_device.py`, which mirrors the CORS behavior the firmware will
ship).

## The matrix and its verdicts (run 2026-07-10 vs the mock)

| Page opened as | Origin | GET | JSON POST/PUT (preflight) | Verdict |
| --- | --- | --- | --- | --- |
| `file://` (downloaded editor) | `null` | PASS | PASS | **The stranger-friendly path.** Works with `Access-Control-Allow-Origin: *` — `null` origins are covered by `*`. |
| Same LAN, `http://` page (or the device's own AP serving it) | http origin | PASS | PASS | Same-origin or plain CORS — no surprises. |
| **HTTPS-hosted (GitHub Pages)** | https origin | **BLOCKED** | **BLOCKED** | **Mixed content — spec-mandated, CORS cannot fix it.** An `https:` page may not fetch `http://192.168.x.x`. This kills "just host the editor on Pages" as the primary path. |

## What the device firmware must ship (P0 inputs)

- `Access-Control-Allow-Origin: *` on every `/api/*` reply.
- An `OPTIONS` handler (204 + the CORS headers) — JSON bodies trigger
  preflights cross-origin.
- `Access-Control-Allow-Private-Network: true` — Chrome's Private Network
  Access preflight for public→private requests (currently warn-mode in
  Chrome, but ship it so the editor doesn't break when Chrome enforces).
- The mock (`tools/mock_device.py`) already implements all three; the
  firmware gains them with `/api/shows` in the P-wave.

## Consequence for editor distribution (VL3 / P0 decision input)

1. **Primary: release artifact** — a single self-contained `editor.html` a
   stranger downloads and double-clicks (`file://` + CORS proven above).
2. Pages hosting can still serve the DOWNLOAD (and docs), just not run the
   editor against a device directly over https.
3. Optional later: the device serves the editor itself (second gzipped
   app) — same-origin, zero setup, but costs flash; a P0 call.

## Still owed on hardware (Needs Hardware)

Re-run `connectivity.html` (file:// + phone-on-AP) against the real ESP32 —
ESPAsyncWebServer must emit the same headers the mock does.
