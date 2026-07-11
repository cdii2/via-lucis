# Golden `.vls` corpus

The cross-artifact contract test for the baked show format
(`docs/SHOW-FORMAT.md` §1). The byte layout is implemented four times — the
editor's encoder/decoder (`editor/editor.html`), the firmware parser
(`firmware/lib/core/src/vialucis/show.cpp`), and `tools/vls_dump.py` — with
no compiler linking them. This corpus makes drift between them a failing
gate instead of an on-device surprise.

## What's here

`shows/<fixture>.vls` — streams **baked by the editor itself** (the only
production encoder), plus `shows/<fixture>.expected.json` — the editor's own
`jsonTwin()` of each stream (pretty-printed, trailing newline).

| fixture | covers |
|---|---|
| `minimal` | smallest editor output: demo clock, one default whole-strip cue; also byte-pinned in the editor selftest |
| `kitchen-sink` | all 5 effects (A56 set), all 4 scope types, both blends, default/stock/custom palette refs, fractional + whole speeds, open-ended cue, same-start z-order tie |
| `scorefollow` | clockSource 2 with an explicit follow track (P4 trailing META byte), note-binding bake from a 2-track song |
| `scorefollow-auto` | clockSource 2 with follow track Auto (explicit 0xFF byte) |

## The three consumers (all must stay green)

1. **Firmware** — `pio test -e native -f test_show_corpus` (from `firmware/`):
   `Show::parse` must reproduce every twin field-by-field.
2. **Tools** — `python tools/check_corpus.py`: `vls_dump.parse_vls` must equal
   every twin semantically (parsed-JSON compare, so serializer formatting
   can't hide drift).
3. **Editor** — `editor.html?selftest=1`: `encodeVls` must reproduce
   `minimal.vls` byte-for-byte (`CORPUS_MINIMAL_HEX`), closing the loop back
   to the artifact that generated the corpus.

## Regenerating (only after a deliberate format change)

The fixtures are generated through the real editor via the gstack browse
daemon, from the repo root:

```bash
B=~/.claude/skills/gstack/browse/dist/browse
for f in minimal kitchen-sink scorefollow scorefollow-auto; do
  "$B" goto "file:///<ABS-REPO-PATH>/editor/editor.html"
  "$B" eval corpus/gen/_helpers.js
  "$B" eval "corpus/gen/$f.setup.js"
  "$B" js "corpusBytesB64()" --out "corpus/shows/$f.vls"
  "$B" js "corpusTwinJson()" --out "corpus/shows/$f.expected.json" --raw
done
```

Then update `CORPUS_MINIMAL_HEX` in `editor/editor.html` (the `_helpers.js`
`corpusBytesHex()` call prints it, with the minimal setup loaded), and re-run
all three consumers. A regeneration is a format-version event — check
`docs/SHOW-FORMAT.md` versioning rules before touching anything.

Effect names in fixtures are governed by A56 (ASSUMPTIONS.md): exactly
fire2012 / pacifica / twinklefox / colorwaves / notedriven.
