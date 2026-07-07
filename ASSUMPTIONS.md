# ASSUMPTIONS

Autonomous decisions made without asking, one per line, newest on top. Format:
`A<n> (date, iter): decision — rationale.`

- A27 (2026-07-07, R2): PlaybackEngine's sound-stopping paths (pause/stop/seek/
  setMode/loadSong/finish) set the frame-dirty flag instead of App calling
  leds_.allOff() synchronously from the HTTP task. The strip now clears on the very
  next loop tick (sub-ms; a dirty frame bypasses the 60fps limiter and renders dark)
  — visually identical, and it removes a pre-existing cross-task FastLED call (HTTP
  task writing LEDs while the loop task may be mid-show). Engine REST methods take a
  `std::vector<MidiOutMsg>& out` so note-offs are still sent synchronously on the
  caller's task, exactly as before. Engine is non-copyable (emitter_ points at its
  own guard_).
- A26 (2026-07-07, R1): MidiIo seam = abstract interface + `final` concrete adapters,
  NOT a templated App. App keeps its by-value `BleMidiIo` member; because the adapter
  class is `final`, every call through the concrete member devirtualizes — zero new
  indirection on the BLE-in→match→LED-out path — while the named interface + FakeMidiIo
  (queue-then-poll, mirroring MIDI.read()) make the seam real with two adapters.
  Templating App was rejected: App stays device-bound after R2 (the engine owns the
  pure logic), so a compile-time seam there buys nothing and costs a header-ized App.
  MidiOutMsg/MidiOutType moved from note_emitter.h to midi_io.h — the message type
  belongs to the transport seam; note_emitter.h re-exports via include.
- A25 (2026-07-07, W5-agent): ninsheetmusic.org Cloudflare-blocks automation (Turnstile
  loop → ERR_BLOCKED_BY_RESPONSE even in the headed stealth browser), so Tier 0–1
  downloads came from vgmusic.com only; NSM items are queued for Christian with direct
  /download/mid/<id> links (IDs found via web search, no downloads attempted elsewhere).
  vgmusic ensemble files (multi-track, unnamed hands) were kept anyway — the trainer's
  per-track hand UI makes them usable, and SONGBOOK marks them "multi" for later
  MuseScore splitting.
- A24 (2026-07-07, W5-agent): PD melody provenance — Vivaldi Largo and Clair de Lune
  transcribed note-for-note from Mutopia Project LilyPond urtexts (CC/PD typesets of PD
  works), Amazing Grace from the hymnal ABC (colinhume.com), Minuet in G from a public
  ABC transcription incl. its bass line; generate.py writes raw SMF bytes (no Python
  deps) so the pack is reproducible from the repo alone.
- A23 (2026-07-07, W5-agent): PD starter-pack keys/scope — Ode to Joy in C (16 bars);
  Amazing Grace in G 3/4 (full hymn, ties realized as long notes); Minuet in G complete
  32 bars, no repeats (trainer's A-B loop covers repetition), bass simplified to
  quarters+; Vivaldi Winter Largo transposed Eb→C (only accidental left is F#, bass
  stays authentic incl. low C2/D2 — they're just keys and the strip covers them); Clair
  de Lune = famous opening only, urtext bars 1–9 transposed Db→C for beginner ergonomics
  (5 flats + 9/8 was the hard part; bar 9's theme-return lands on the tonic so it closes
  cleanly), inner voices thinned to melody+LH dyads; full Db piece stays Tier 3.
- A22 (2026-07-07, W4-agent): echoWindowMs has no field in the web UI Settings page,
  so TROUBLESHOOTING's echo fix ships a single copy-paste PowerShell line
  (Invoke-RestMethod PUT to /api/settings) with open-PowerShell hand-holding — the
  brief assumed a UI field ("raise echoWindowMs in settings") that doesn't exist;
  adding one to webui is a future nicety, not a doc's job.
- A21 (2026-07-07, W4-agent): BUILD-GUIDE adds an optional tidy-up — jumper the
  74AHCT125's unused inputs/enables (pins 4, 5, 9, 10, 12, 13) to GND — floating
  CMOS inputs are bad practice; purely additive, does not touch the frozen rows.
- A20 (2026-07-07, W4-agent): USB-driver docs cover a CH340 fallback alongside
  CP2102 (search "CH341SER driver") — ELEGOO/clone boards vary in USB chip, and a
  wrong-driver dead end would strand Christian with no AI present.
- A19 (2026-07-07, W4-agent): BRINGUP runs the strip test pattern AFTER the
  PSU+strip hookup (brief's step list had it before) — a walking white dot is
  invisible on an unpowered, unconnected strip; the pre-hookup steps still prove
  flash/AP/UI on the bare board.
- A18 (2026-07-07, W4-agent): docs mandate "never USB and PSU connected at the same
  time" — clone devkits don't reliably have the backfeed diode; costs live serial
  while PSU-powered, so IP discovery is documented via router device list or a
  temporary USB-only session (strip unpowered, WiFi still boots).
- A17 (2026-07-07, W4-agent): the ~0.77m of spare strip beyond C8 stays DARK, never
  cut — BOM allows "trim or leave dark"; leaving it dark is decision-free,
  reversible, and needs no tools on assembly day.
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
