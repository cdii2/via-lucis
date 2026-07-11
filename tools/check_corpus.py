"""check_corpus — pin vls_dump.py to the golden .vls corpus.

The corpus (corpus/shows/*.vls + *.expected.json) is the cross-artifact
contract for the baked show format (SHOW-FORMAT §1): fixtures are baked by
the editor, the expected twins are the editor's own jsonTwin() output, and
three consumers must reproduce them —

  - this check:      vls_dump.parse_vls(bytes) == expected twin (semantic)
  - firmware:        test_show_corpus (native suite, field-by-field)
  - editor:          selftest byte-pins the "minimal" fixture's exact bytes

Comparison is on PARSED JSON (dict equality), not text, so serializer
formatting differences can never mask or fake a field drift.

Usage:  python tools/check_corpus.py        # exit 0 = all fixtures match
Regeneration procedure: corpus/README.md.
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from vls_dump import BadShow, parse_vls  # noqa: E402


def first_difference(a, b, path="$"):
    """Human-oriented pointer at the first place two twins diverge."""
    if type(a) is not type(b):
        return "%s: type %s != %s" % (path, type(a).__name__, type(b).__name__)
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                return "%s.%s: missing in dump twin" % (path, k)
            if k not in b:
                return "%s.%s: missing in expected twin" % (path, k)
            d = first_difference(a[k], b[k], "%s.%s" % (path, k))
            if d:
                return d
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return "%s: length %d != %d" % (path, len(a), len(b))
        for i, (x, y) in enumerate(zip(a, b)):
            d = first_difference(x, y, "%s[%d]" % (path, i))
            if d:
                return d
        return None
    if a != b:
        return "%s: %r != %r" % (path, a, b)
    return None


def main():
    shows = pathlib.Path(__file__).resolve().parent.parent / "corpus" / "shows"
    fixtures = sorted(shows.glob("*.vls"))
    if not fixtures:
        print("check_corpus: FAIL — no fixtures in %s" % shows)
        return 1

    failed = 0
    for vls in fixtures:
        expected_path = vls.with_suffix(".expected.json")
        if not expected_path.exists():
            print("check_corpus: FAIL %s — missing %s" % (vls.name, expected_path.name))
            failed += 1
            continue
        try:
            twin = parse_vls(vls.read_bytes())
        except BadShow as e:
            print("check_corpus: FAIL %s — parse_vls refused: %s" % (vls.name, e))
            failed += 1
            continue
        expected = json.loads(expected_path.read_text(encoding="utf-8"))
        diff = first_difference(twin, expected)
        if diff:
            print("check_corpus: FAIL %s — %s" % (vls.name, diff))
            failed += 1
        else:
            print("check_corpus: PASS %s" % vls.name)

    if failed:
        print("check_corpus: %d/%d fixtures FAILED" % (failed, len(fixtures)))
        return 1
    print("check_corpus: all %d fixtures match" % len(fixtures))
    return 0


if __name__ == "__main__":
    sys.exit(main())
