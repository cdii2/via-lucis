# Golden MIDI conformance corpus

The SMF-side twin of the `.vls` show corpus (`corpus/README.md`): the
cross-artifact contract for Standard MIDI File parsing, pinning
`parseMidi`/`writeSmf`/`TrackConfig::defaultsFor` (firmware) **and** the
editor's own independent `parseMidi` + `normalizeHands` (`editor/editor.html`)
to one contract. The motivating bug is the A82 hand-swap (ASSUMPTIONS.md): the
editor trusted the raw MTrk track index as the hand, so a device recording (or
any DAW file) carrying a conductor track at index 0 rendered wrong colors and
silently swapped hands on export — because no artifact pinned both views of
the same file at once. This corpus does.

## What's here

`<fixture>.mid` — a hand-authored (or, for `recording-shaped`, byte-replica
`writeSmf`-emitted) Standard MIDI File, plus `<fixture>.expected.json` — the
twin computed from the same declarative model that authored the bytes (see
"Regenerating" below), never by parsing the bytes themselves.

| fixture | covers |
|---|---|
| `conductor-first` | the A82 shape: Conductor (no notes) + Right + Left, tempo meta, plain two-hand file |
| `named-lh-rh` | LH track listed FIRST in file order — name (`lh`/`RH`, mixed case) must beat file order for both firmware and editor hand rules |
| `anonymous-pair` | two unnamed note tracks, no conductor, no tempo meta at all — the "first=Right/RH, second=Left/LH" file-order fallback |
| `three-hands` | Conductor + Right + Left + a third unnamed note track — firmware `Hand::Both`(exactly-two-tracks rule doesn't apply) vs. editor's `Other`(2)/amber bucket; channel field pinned via a non-zero channel on the third track |
| `pedal-only-track` | "Right" notes + a separate "Pedal" track carrying only CC64 on a non-zero channel — firmware `Hand::Both`(A78, keeps demo sustain audible) vs. editor's pedal-inherits-source-track-hand rule (unmapped track buckets to Right/0) |
| `tie-order` | running status ON; one track, three notes exercising: clean off then re-strike at the same tick (off-before-on tie), a re-strike with no prior off (auto-close at the re-strike tick), and a velocity-0 note-on used as a note-off |
| `vlq-tempo` | multi-byte VLQ deltas (2-byte and 3-byte) crossing three tempo segments — exercises `tickToMicros`'s per-segment integer division across tempo-map boundaries |
| `recording-shaped` | emitted by a byte-for-byte python replica of firmware `writeSmf` (not hand-authored bytes) — pedal/note/off collision ordering at the same millisecond, cross-checked against the real `writeSmf` by a firmware byte-compare test |

## The three consumers (all must stay green)

1. **Firmware** — `pio test -e native -f test_midi_corpus` (from `firmware/`):
   `parseMidi` + `TrackConfig::defaultsFor` must reproduce every twin's raw
   RAW-INDEX view field-by-field (plus the `recording-shaped` byte-compare
   against the real `writeSmf`).
2. **Tools** — `python tools/check_midi_corpus.py`: `midi_dump.parse_midi`
   must equal every twin semantically (parsed-JSON compare, so serializer
   formatting can't hide drift), AND the editor's embedded `MIDI_CORPUS` hex
   (below) must byte-match the committed `.mid` files.
3. **Editor** — `editor.html?selftest=1`: the real `parseMidi` +
   `normalizeHands` must reproduce every twin's CANONICAL-HANDS view from the
   embedded hex (`MIDI_CORPUS` in `editor.html`, guarded by check #2 above).

## The twin schema (both representations, one file)

Each `.expected.json` pins the SAME parsed file two ways:

- **firmware raw-index view** — `notes[].track` / `pedal[].track` are the raw
  MTrk chunk index; `firmwareHands` is `TrackConfig::defaultsFor`'s per-track
  `{hand, lights}` (notes-empty track -> `both` if it carries pedal else
  `off`, lights off; note-bearing: name-substring left/lh -> `left`,
  right/rh -> `right`, else `both`; if exactly two note-bearing tracks are
  still `both`, first -> `right`, second -> `left`).
- **editor canonical-hands view** (`editor` key) — `normalizeHands`'s 3-bucket
  model (0=Right, 1=Left, 2=Other) among note-bearing tracks only (named
  right/rh -> 0, left/lh -> 1, else file order); pedals inherit their source
  track's mapped hand, an unmapped conductor/pedal-only track's pedal buckets
  to Right(0).

Both views share **exact integer microseconds**: the generator asserts every
pinned tick divides cleanly through `tickToMicros`'s per-segment integer
division, so firmware's truncating division and the editor's µs-domain math
land on the identical value — no fixture here depends on rounding agreeing
between two different arithmetic paths.

## Regenerating (only after a deliberate contract change)

```bash
python corpus/gen/midi/gen_midi_fixtures.py
```

This overwrites every `corpus/midi/<fixture>.mid` + `.expected.json` and
prints an editor `MIDI_CORPUS` snippet to stdout — paste it over the
`const MIDI_CORPUS = {...}` block in `editor.html` (currently ~line 3002),
then re-run all three consumers above. The declarative fixture models inside
the generator are the **authority**: the `.mid` bytes and the expected twin
are both derived from the same model, never one from the other. A
regeneration is therefore a contract-change event, not routine maintenance —
if a consumer disagrees with a regenerated fixture, either that consumer has
a real bug, or the contract changed deliberately and the disagreeing
consumer's source needs the matching fix.
