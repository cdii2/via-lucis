# ASSUMPTIONS

Autonomous decisions made without asking, one per line, newest on top. Format:
`A<n> (date, iter): decision — rationale.`

- A16 (2026-07-07, 8): esp32dev uses huge_app.csv partitions (3MB app + ~0.9MB LittleFS,
  no OTA — flashing is over USB anyway): BLE+WiFi+web server = 1.44MB, over the default
  1.3MB slot. Filesystem = littlefs. Libraries: lathoub BLE-MIDI client over NimBLE 1.x
  (lower RAM than Bluedroid), ESP32Async fork of ESPAsyncWebServer (maintained), with
  -DASYNCWEBSERVER_REGEX for path params.
- A15 (2026-07-07, 8): single-track ("Both"-hand) songs light in the right-hand color;
  demo/follow show sounding notes at 100%, wait/accompaniment shows the due chord at
  100% (pending members only) — cleared members go dark, which doubles as per-key
  clear feedback.
- A14 (2026-07-07, W3-agent): web UI visual identity — candle-gold accent ("lux"), seek bar
  drawn as the LED strip (blue→gold→green gradient), wait-mode pending notes rendered as
  glowing piano-key chips; blue/green reserved for left/right-hand meaning per SPEC colors.
- A13 (2026-07-07, W3-agent): upload renames `.midi`→`.mid` and pre-rejects >256 KB
  client-side (mirrors server rules for a faster error); loop times entered as m:ss, a
  bare number is taken as seconds.
- A12 (2026-07-07, W3-agent): previewCap shown as 0–100% mapped to the API's 0–1 float;
  brightness slider min=1 (0 would read as a dead strip); settings PUT sends only changed
  fields (API allows partial), debounced 400 ms, and GET runs on every Settings tab open.
- A11 (2026-07-07, W3-agent): tempo slider is log-mapped over 1–500% (100% ≈ 3/4 of
  travel) with throttled sends while dragging (150 ms) plus an authoritative send on
  release — API clamps, so intermediate values are safe.
- A10 (2026-07-07, W3-agent): `/api/status` doesn't expose the practice hand, so the UI
  keeps the last-chosen hand locally (default `both`) and includes it in every
  `/api/mode` POST — demo/follow ignore it per API.md, so always sending is harmless.
- A9 (2026-07-07, 5): wait-mode key verdicts — presses between chords and re-strikes of
  already-cleared chord members are Ignored (no red flash; noodling isn't an error);
  only a non-member press while a chord is due is Wrong. Duplicate notes across tracks
  in one chord dedupe to a single pending key.
- A8 (2026-07-07, 5): echo guard = one ignore credit per transmitted note-on, 250ms
  default window (BLE round trip is tens of ms), per-note counters; window is a
  variable to re-tune against the real FP-30X at bring-up.
- A7 (2026-07-07, 4): scheduler emits ALL events tagged with their track; hand filtering
  happens in each consumer via TrackMask — lights and accompaniment need *different*
  filters (muted hand: no lights but piano plays it), so filtering inside the scheduler
  would be wrong. Barrier = wait-mode hold: nothing at or past the barrier fires until
  cleared; loop wrap flushes note-offs for sounding notes.
- A6 (2026-07-07, 3): key geometry = rear-of-keyboard cluster model (octave 164.5mm;
  C-E group = 5×14.1mm slots, F-B group = 7×13.43mm slots) — matches where the strip
  physically sits better than front white-key widths; 1mm edge margin guarantees
  adjacent keys never share an LED at 180/m.
- A5 (2026-07-07, 2): MIDI parser scope — SMPTE division files rejected (rare for piano
  sheet MIDIs); format 2 parsed same as 0/1; overlapping same-note retrigger closes the
  prior note; spurious note-offs ignored; notes still open at End-of-Track get closed
  there. Keeps the trainer deterministic on imperfect files.
- A4 (2026-07-07, 2): CC64 captured into a separate `pedal` list (not the note list) —
  demo/accompaniment passes it through, wait-mode matching never sees it (SPEC v1).
- A3 (2026-07-07, 2): dev toolchain = PlatformIO CLI via uv tool + WinLibs MinGW-w64 GCC
  via winget for the native env (PlatformIO's `platform_packages` toolchain is ignored
  by the native platform). BUILD-GUIDE will document VSCode+PlatformIO for strangers.
- A2 (2026-07-07, 1): WiFi = stored creds with AP-mode fallback (`ViaLucis` hotspot +
  config page) — arrival-day simplicity for Christian, replicability for strangers.
- A1 (2026-07-07, 1): LED data = GPIO16, PlatformIO esp32dev+native, core logic pure
  C++ — enables full pre-hardware testing; GPIO16 avoids strapping pins.
