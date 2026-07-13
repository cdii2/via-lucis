"""check_midi_corpus — pin midi_dump.py (and the editor's embedded
MIDI_CORPUS hex) to the golden MIDI corpus.

The corpus (corpus/midi/*.mid + *.expected.json) is the cross-artifact
contract for the SMF parser (corpus/midi/README.md; ASSUMPTIONS.md A83-A86):
fixtures + twins are authored by corpus/gen/midi/gen_midi_fixtures.py from a
declarative model, and three consumers must reproduce them —

  - this check:  midi_dump.parse_midi(bytes) == expected twin (semantic),
                 PLUS an embed-drift guard: editor.html hand-copies each
                 fixture's bytes as a hex literal (MIDI_CORPUS) for its own
                 selftest, and nothing but this check enforces that copy
                 stays byte-identical to the committed .mid,
                 PLUS a firmware-coverage guard: test_midi_corpus.cpp must
                 exercise EXACTLY the committed fixture set (else a new
                 fixture shipping without its firmware tests passes silently)
  - firmware:    test_midi_corpus (native suite, field-by-field)
  - editor:      selftest re-parses the SAME embedded hex with the real
                 parseMidi()/normalizeHands() (A85)

Comparison is on PARSED JSON (dict equality), not text, so serializer
formatting differences can never mask or fake a field drift.

Usage:  python tools/check_midi_corpus.py       # exit 0 = everything matches
Regeneration procedure: corpus/midi/README.md.
"""
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from check_corpus import first_difference  # noqa: E402
from midi_dump import BadMidi, parse_midi  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
MIDI_DIR = ROOT / "corpus" / "midi"
EDITOR_HTML = ROOT / "editor" / "editor.html"
FIRMWARE_TEST = ROOT / "firmware" / "test" / "test_midi_corpus" / "test_midi_corpus.cpp"


def check_twins():
    """Check 1: midi_dump.parse_midi(bytes) == the fixture's committed twin.
    Returns (failed_count, [fixture stems])."""
    fixtures = sorted(MIDI_DIR.glob("*.mid"))
    if not fixtures:
        print("check_midi_corpus: FAIL — no fixtures in %s" % MIDI_DIR)
        return 1, []

    failed = 0
    for mid in fixtures:
        expected_path = mid.with_suffix(".expected.json")
        if not expected_path.exists():
            print("check_midi_corpus: FAIL %s (twin) — missing %s"
                  % (mid.name, expected_path.name))
            failed += 1
            continue
        try:
            twin = parse_midi(mid.read_bytes())
        except BadMidi as e:
            print("check_midi_corpus: FAIL %s (twin) — parse_midi refused: %s" % (mid.name, e))
            failed += 1
            continue
        expected = json.loads(expected_path.read_text(encoding="utf-8"))
        diff = first_difference(twin, expected)
        if diff:
            print("check_midi_corpus: FAIL %s (twin) — %s" % (mid.name, diff))
            failed += 1
        else:
            print("check_midi_corpus: PASS %s (twin)" % mid.name)

    return failed, [m.stem for m in fixtures]


def _extract_embed():
    """Parse editor.html's `const MIDI_CORPUS = {...}` block into
    {fixture-stem: joined-lowercase-hex}."""
    html = EDITOR_HTML.read_text(encoding="utf-8")
    start = html.find("const MIDI_CORPUS = {")
    if start == -1:
        raise RuntimeError("MIDI_CORPUS not found in %s" % EDITOR_HTML)
    end_marker = "for(const [cname, cf] of Object.entries(MIDI_CORPUS))"
    end = html.find(end_marker, start)
    if end == -1:
        raise RuntimeError("MIDI_CORPUS consumer loop not found in %s" % EDITOR_HTML)
    block = html[start:end]

    # Each entry: "<stem>": { hex: "hex"+ "hex"+ ... "hex", expect: { ... }
    # Capture everything between `hex:` and the entry's own `expect: {`,
    # then pull out just the quoted hex literals (order-preserving).
    entry_re = re.compile(
        r'"(?P<name>[A-Za-z0-9_-]+)":\s*\{\s*hex:\s*(?P<hexblock>.*?)\s*expect:\s*\{',
        re.S,
    )
    hex_literal_re = re.compile(r'"([0-9a-fA-F]*)"')

    embed = {}
    for m in entry_re.finditer(block):
        parts = hex_literal_re.findall(m.group("hexblock"))
        embed[m.group("name")] = "".join(parts).lower()
    return embed


def check_editor_hex(fixture_stems):
    """Check 2: the editor's embedded MIDI_CORPUS hex must byte-match the
    committed .mid files, and its key set must equal the committed fixture
    set exactly (an added-but-not-embedded, or stale, fixture must FAIL)."""
    try:
        embed = _extract_embed()
    except RuntimeError as e:
        print("check_midi_corpus: FAIL editor embed — %s" % e)
        return 1

    failed = 0
    want = set(fixture_stems)
    got = set(embed)
    if want != got:
        missing = sorted(want - got)
        extra = sorted(got - want)
        if missing:
            print("check_midi_corpus: FAIL editor embed — missing fixtures: %s"
                  % ", ".join(missing))
        if extra:
            print("check_midi_corpus: FAIL editor embed — stale fixtures not in corpus/midi: %s"
                  % ", ".join(extra))
        failed += 1

    for stem in sorted(want & got):
        mid_path = MIDI_DIR / (stem + ".mid")
        file_hex = mid_path.read_bytes().hex()
        if embed[stem] != file_hex:
            print("check_midi_corpus: FAIL %s (editor-hex) — embed drifted from committed .mid"
                  % stem)
            failed += 1
        else:
            print("check_midi_corpus: PASS %s (editor-hex)" % stem)

    return failed


def check_firmware_coverage(fixture_stems):
    """Check 3: the firmware suite (test_midi_corpus.cpp) must exercise EXACTLY
    the committed fixture set. Extract every stem passed to checkParse("<stem>")
    / checkHands("<stem>") and require that set to equal the committed
    corpus/midi/*.mid stems — so a 9th fixture that ships without its firmware
    tests (or a stale test naming a deleted fixture) fails a gate instead of
    passing every other check silently."""
    try:
        cpp = FIRMWARE_TEST.read_text(encoding="utf-8")
    except OSError as e:
        print("check_midi_corpus: FAIL firmware-coverage — cannot read %s: %s"
              % (FIRMWARE_TEST.name, e))
        return 1

    call_re = re.compile(r'check(?:Parse|Hands)\(\s*"([^"]+)"\s*\)')
    exercised = set(call_re.findall(cpp))

    want = set(fixture_stems)
    if exercised != want:
        missing = sorted(want - exercised)   # fixtures with no firmware test
        extra = sorted(exercised - want)     # tests naming a non-existent fixture
        if missing:
            print("check_midi_corpus: FAIL firmware-coverage — fixtures with no "
                  "checkParse/checkHands test: %s" % ", ".join(missing))
        if extra:
            print("check_midi_corpus: FAIL firmware-coverage — tests name absent "
                  "fixtures: %s" % ", ".join(extra))
        return 1

    print("check_midi_corpus: PASS firmware-coverage (%d fixtures exercised)"
          % len(want))
    return 0


def main():
    twin_failed, fixture_stems = check_twins()
    hex_failed = check_editor_hex(fixture_stems)
    coverage_failed = check_firmware_coverage(fixture_stems)

    failed = twin_failed + hex_failed + coverage_failed
    if failed:
        print("check_midi_corpus: %d check(s) FAILED" % failed)
        return 1
    print("check_midi_corpus: all %d fixtures match (twin + editor hex + firmware coverage)"
          % len(fixture_stems))
    return 0


if __name__ == "__main__":
    sys.exit(main())
