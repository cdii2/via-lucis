"""vls_dump — print a baked .vls show stream as its JSON twin.

The editor exports `.vls` and a `.vls.json` twin (docs/SHOW-FORMAT.md §1 "The
JSON debug twin"); this tool reconstructs that twin from the binary alone, so
a stream produced anywhere — the device, a fuzzer, a hand-hacked file — stays
inspectable and round-trip-checkable. It is the Python mirror of the editor's
decodeVls()/jsonTwin(); the field names match byte-for-byte so a diff against
`editor.html`'s `.vls.json` export is meaningful.

Stdlib only. Parses per the SHOW-FORMAT §1 contract, including the two
compatibility rules: a versionMajor newer than this tool's is REFUSED (never
garbage output), while unknown *section types* are reported and skipped (the
same forward-compat path P4's score-follow sections will ride).

Usage:

    python tools/vls_dump.py show.vls           # JSON twin to stdout
    python tools/vls_dump.py show.vls -o out.json
    cat show.vls | python tools/vls_dump.py -    # read from stdin
"""
import argparse
import json
import struct
import sys

# The format version this tool understands (SHOW-FORMAT §1: versionMajor 1).
SUPPORTED_MAJOR = 1

CLOCK_NAMES = ["demo", "freeRun", "scoreFollow"]
BLEND_NAMES = ["opacity", "additive"]
DRIVE_NAMES = ["autonomous", "note-driven"]
SCOPE_NAMES = ["all", "pitchRange", "noteSet", "noteBinding"]

END_TYPE = 0xFFFF


class BadShow(Exception):
    """The stream is malformed or needs newer firmware — mirrors the device's
    typed refusals rather than rendering garbage."""


class Reader:
    """Little-endian cursor with bounds checks — every read past the end is a
    typed BadShow, never a silent wrap or a stdlib IndexError."""

    def __init__(self, buf):
        self.buf = buf
        self.p = 0

    def need(self, n):
        if n < 0 or self.p + n > len(self.buf):
            raise BadShow(
                "truncated: need %d bytes at offset %d of %d"
                % (n, self.p, len(self.buf))
            )

    def u8(self):
        self.need(1)
        v = self.buf[self.p]
        self.p += 1
        return v

    def u16(self):
        self.need(2)
        v = struct.unpack_from("<H", self.buf, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        self.need(4)
        v = struct.unpack_from("<I", self.buf, self.p)[0]
        self.p += 4
        return v

    def bytes(self, n):
        self.need(n)
        v = self.buf[self.p:self.p + n]
        self.p += n
        return v

    def str0(self):
        # zero-terminated UTF-8 (the editor writes str0()).
        start = self.p
        while self.p < len(self.buf) and self.buf[self.p] != 0:
            self.p += 1
        if self.p >= len(self.buf):
            raise BadShow("unterminated string at offset %d" % start)
        s = self.buf[start:self.p].decode("utf-8", "replace")
        self.p += 1  # consume the terminator
        return s


def _paletteRef_name(ref):
    if ref == 0xFF:
        return "default"
    if ref & 0x80:
        return "stock#%d" % (ref & 0x7F)
    return "custom#%d" % ref


def _cue_twin(r):
    """One 16-byte cue record + its scope payload -> twin dict. Advances r."""
    startMs = r.u32()
    endMs = r.u32()
    effectIndex = r.u8()
    blend = r.u8()
    opacity = r.u8()
    drive = r.u8()
    scopeType = r.u8()
    paletteRef = r.u8()
    speed = r.u8()
    flags = r.u8()

    o = {
        "startMs": startMs,
        "endMs": "open-ended" if endMs == 0xFFFFFFFF else endMs,
        "effectIndex": effectIndex,
        "blend": BLEND_NAMES[blend] if blend < len(BLEND_NAMES) else blend,
        "opacity": opacity,
        "drive": DRIVE_NAMES[drive] if drive < len(DRIVE_NAMES) else drive,
        "scopeType": scopeType,
        "scopeName": SCOPE_NAMES[scopeType] if scopeType < len(SCOPE_NAMES) else "?",
        "paletteRef": _paletteRef_name(paletteRef),
        # Match the editor twin: whole ratios render as ints (16->1), else float.
        "speed": (speed // 16) if speed % 16 == 0 else speed / 16.0,
        "flags": flags,
    }
    if scopeType == 1:
        o["scope"] = {"lo": r.u8(), "hi": r.u8()}
    elif scopeType == 2:
        n = r.u8()
        o["scope"] = {"notes": [r.u8() for _ in range(n)]}
    elif scopeType == 3:
        n = r.u16()
        bound = []
        for _ in range(n):
            onsetMs = r.u32()
            note = r.u8()
            bound.append({"onsetMs": onsetMs, "note": note})
        o["scope"] = {"boundCount": len(bound), "bound": bound}
    return o


def parse_vls(buf):
    """Binary .vls -> JSON-twin dict (the editor's jsonTwin() shape)."""
    r = Reader(buf)
    if r.bytes(4) != b"VLS1":
        raise BadShow("bad magic (want 'VLS1')")
    versionMajor = r.u8()
    versionMinor = r.u8()
    if versionMajor > SUPPORTED_MAJOR:
        raise BadShow(
            "show needs newer firmware (stream major %d > supported %d)"
            % (versionMajor, SUPPORTED_MAJOR)
        )
    sectionCount = r.u16()

    twin = {
        "magic": "VLS1",
        "versionMajor": versionMajor,
        "versionMinor": versionMinor,
        "sections": [],
    }

    for _ in range(sectionCount):
        stype = r.u16()
        length = r.u32()
        body_end = r.p + length
        r.need(length)  # the section body must fit

        if stype == END_TYPE:
            twin["sections"].append({"type": END_TYPE, "name": "END"})
            r.p = body_end
            break

        if stype == 1:  # META
            sr = Reader(r.buf[r.p:body_end])
            clockSource = sr.u8()
            durationMs = sr.u32()
            showName = sr.str0()
            twin["sections"].append({
                "type": 1, "name": "META",
                "clockSource": clockSource,
                "clockSourceName": CLOCK_NAMES[clockSource] if clockSource < len(CLOCK_NAMES) else "?",
                "durationMs": durationMs,
                "showName": showName,
            })
        elif stype == 2:  # EFFECTS
            sr = Reader(r.buf[r.p:body_end])
            n = sr.u8()
            effects = [sr.str0() for _ in range(n)]
            twin["sections"].append({"type": 2, "name": "EFFECTS", "effects": effects})
        elif stype == 3:  # PALETTES
            sr = Reader(r.buf[r.p:body_end])
            n = sr.u8()
            palettes = []
            for _ in range(n):
                palettes.append([[sr.u8(), sr.u8(), sr.u8()] for _ in range(16)])
            twin["sections"].append({"type": 3, "name": "PALETTES", "palettes": palettes})
        elif stype == 4:  # CUES
            sr = Reader(r.buf[r.p:body_end])
            n = sr.u16()
            cues = [_cue_twin(sr) for _ in range(n)]
            twin["sections"].append({
                "type": 4, "name": "CUES", "count": len(cues), "cues": cues,
            })
        else:
            # Unknown section type: skip (SHOW-FORMAT §1 forward-compat rule).
            twin["sections"].append({
                "type": stype, "name": "UNKNOWN", "skipped": True,
                "byteLength": length,
            })

        r.p = body_end

    return twin


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Print a baked .vls show stream as its JSON twin."
    )
    ap.add_argument("path", help="input .vls file, or '-' for stdin")
    ap.add_argument("-o", "--out", help="write JSON here instead of stdout")
    ap.add_argument("--compact", action="store_true",
                    help="single-line JSON instead of indented")
    args = ap.parse_args(argv)

    if args.path == "-":
        buf = sys.stdin.buffer.read()
    else:
        with open(args.path, "rb") as f:
            buf = f.read()

    try:
        twin = parse_vls(buf)
    except BadShow as e:
        sys.stderr.write("vls_dump: %s\n" % e)
        return 1

    text = json.dumps(twin, separators=(",", ":")) if args.compact \
        else json.dumps(twin, indent=2)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
