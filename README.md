# Via Lucis

**LED piano trainer** — a strip of lights above your piano keys that shows you what to
press, waits until you get it right, and can even play the piano for you.

*Via Lucis* — "the Way of Light."

## What it does

An ESP32 connects to your digital piano over Bluetooth MIDI and drives an addressable
LED strip mounted above the keys. Load a MIDI file, and:

- **Wait mode** — the lights show the next note(s) and the song *waits* until you press
  the right keys. Wrong key = red flash on the key you hit. Chords clear per-key as you
  press each correct note.
- **Follow-along** — lights fire in rhythm at any tempo from 1% to 500%.
- **Hands separate** — practice one hand while the *piano itself plays the other hand*
  (the ESP32 sends MIDI back to the piano — no speakers or synth needed).
- **Demo mode** — the piano performs the piece, lights following, so you hear the target.
- **Loop** — repeat any time range (e.g. 0:45–0:50) until it's clean.
- **Lookahead** — upcoming notes glow dim and swell, then jump to full brightness at
  press-time. Lead time and preview brightness are adjustable.

Everything is controlled from a web UI the ESP32 serves over WiFi — your phone is the
remote. No PC at the piano, no app to install.

## Hardware (~$120, no soldering)

See [docs/BOM.md](docs/BOM.md) for the full parts list with links. Summary:

| Part | ~Price |
|------|--------|
| BTF-LIGHTING FCOB LED strip, 180 LED/m, 5V, 2m | $23 |
| ESP32 dev board (classic ESP-WROOM-32) | $7 |
| 5V 10A power supply + barrel-to-screw adapter | $30 |
| 74AHCT125 level shifter | $4 |
| Breadboard + jumper wires | $10 |
| Aluminum mounting channel | $20 |

Reference piano: **Roland FP-30X** (Bluetooth MIDI). Any piano with BLE-MIDI should
work; piano-specific quirks are isolated behind one small interface.

## Status

**Pre-hardware.** Design is locked ([docs/SPEC.md](docs/SPEC.md)), parts are ordered,
firmware is being written against a simulator.

## License

MIT — see [LICENSE](LICENSE).
