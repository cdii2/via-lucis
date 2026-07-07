# ASSUMPTIONS

Autonomous decisions made without asking, one per line, newest on top. Format:
`A<n> (date, iter): decision — rationale.`

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
