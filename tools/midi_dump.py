"""midi_dump — print a Standard MIDI File as its JSON twin (the
`corpus/midi/*.expected.json` schema).

Independent stdlib-only reimplementation of the SMF-side contract pinned by
the golden MIDI corpus (`corpus/midi/README.md`; ASSUMPTIONS.md A83-A86):
the firmware parser (`firmware/lib/core/src/vialucis/midi_parser.cpp` +
`track_config.cpp` `TrackConfig::defaultsFor`) and the editor's own
`parseMidi()`/`normalizeHands()` (`editor/editor.html`). This tool re-derives
the FULL twin — raw-index parse, `firmwareHands`, AND the editor's
canonical-hands view — straight from those byte-level/behavioral contracts.
It does not import the corpus generator (`corpus/gen/midi/gen_midi_fixtures.py`)
or read its output: it is the fourth independent implementation of the same
rules, so it can catch a drift the other three might share.

Parses running status (cancelled by any meta or sysex event), velocity-0
note-on as note-off, note-on re-strike (auto-closes the previously open
instance of the same (channel, note) at the re-strike tick), dangling notes
closed at end-of-track, and the parser's two GLOBAL stable sorts — notes by
onTick, tempo/pedal by tick — applied once after every track has been
parsed (never per-track). Field names and nesting match
`corpus/midi/*.expected.json` byte-for-byte, so a diff against a committed
twin is meaningful.

Stdlib only. SMF is big-endian throughout (unlike the little-endian `.vls`
format `tools/vls_dump.py` mirrors).

Usage:

    python tools/midi_dump.py song.mid           # JSON twin to stdout
    python tools/midi_dump.py song.mid -o out.json
    cat song.mid | python tools/midi_dump.py -    # read from stdin
"""
import argparse
import json
import sys


class BadMidi(Exception):
    """The stream is malformed — mirrors the firmware parser's typed
    refusals (MidiParseError) rather than rendering garbage."""


class Reader:
    """Bounds-checked big-endian cursor over the raw file bytes; every read
    past the end is a typed BadMidi, never a silent wrap or IndexError."""

    def __init__(self, buf):
        self.buf = buf
        self.p = 0

    def remaining(self):
        return len(self.buf) - self.p

    def u8(self):
        if self.p + 1 > len(self.buf):
            raise BadMidi("truncated: need 1 byte at offset %d" % self.p)
        v = self.buf[self.p]
        self.p += 1
        return v

    def u16(self):
        hi = self.u8()
        lo = self.u8()
        return (hi << 8) | lo

    def u32(self):
        v = 0
        for _ in range(4):
            v = (v << 8) | self.u8()
        return v

    def vlq(self):
        """Variable-length quantity, MSB-first, 7 bits/byte, max 4 bytes."""
        v = 0
        for _ in range(4):
            b = self.u8()
            v = (v << 7) | (b & 0x7F)
            if not (b & 0x80):
                return v
        raise BadMidi("VLQ longer than 4 bytes at offset %d" % self.p)

    def match4(self, tag):
        if self.p + 4 > len(self.buf):
            raise BadMidi("truncated: need 4 bytes at offset %d" % self.p)
        m = self.buf[self.p:self.p + 4] == tag.encode("ascii")
        self.p += 4
        return m

    def skip(self, n):
        if self.p + n > len(self.buf):
            raise BadMidi("truncated: need %d bytes at offset %d" % (n, self.p))
        self.p += n

    def str(self, n):
        if self.p + n > len(self.buf):
            raise BadMidi("truncated: need %d bytes at offset %d" % (n, self.p))
        s = self.buf[self.p:self.p + n].decode("utf-8", "replace")
        self.p += n
        return s


def _parse_track(r, track_index, notes, tempo, pedal):
    """One MTrk chunk -> track name; appends into notes/tempo/pedal (still in
    per-track, per-event order — the caller does ONE global stable sort of
    each list after every track is parsed, matching parseMidi())."""
    if not r.match4("MTrk"):
        raise BadMidi("track %d: bad MTrk tag" % track_index)
    chunk_len = r.u32()
    if chunk_len > r.remaining():
        raise BadMidi("track %d: truncated MTrk body" % track_index)
    end = r.p + chunk_len

    name = ""          # first meta 0x03 wins
    active = {}         # (channel, note) -> (onTick, velocity)
    tick = 0
    running_status = 0  # 0 = none; meta/sysex cancel it

    def note_off(ch, note, off_tick):
        key = (ch, note)
        if key not in active:
            return  # spurious note-off: ignore
        on_tick, vel = active.pop(key)
        notes.append({
            "onTick": on_tick, "offTick": off_tick, "note": note,
            "velocity": vel, "channel": ch, "track": track_index,
        })

    def note_on(ch, note, vel, on_tick):
        if (ch, note) in active:
            note_off(ch, note, on_tick)  # re-strike closes the previous instance
        active[(ch, note)] = (on_tick, vel)

    while r.p < end:
        tick += r.vlq()
        b = r.u8()

        if b == 0xFF:  # meta event
            mtype = r.u8()
            mlen = r.vlq()
            if mlen > r.remaining():
                raise BadMidi("track %d: truncated meta at offset %d" % (track_index, r.p))
            if mtype == 0x2F:  # End of Track
                r.skip(mlen)
                break
            elif mtype == 0x51 and mlen == 3:  # Set Tempo
                us = r.u8() << 16
                us |= r.u8() << 8
                us |= r.u8()
                tempo.append({"tick": tick, "usPerQuarter": us})
            elif mtype == 0x03:  # Track Name
                s = r.str(mlen)
                if name == "":
                    name = s
            else:
                r.skip(mlen)
            running_status = 0  # meta cancels running status
            continue

        if b == 0xF0 or b == 0xF7:  # SysEx: length-prefixed, skip
            slen = r.vlq()
            if slen > r.remaining():
                raise BadMidi("track %d: truncated sysex at offset %d" % (track_index, r.p))
            r.skip(slen)
            running_status = 0
            continue

        if b & 0x80:
            status = b
            running_status = status
            data1 = r.u8()
        else:
            if not (running_status & 0x80):
                raise BadMidi(
                    "track %d: data byte with no running status at offset %d"
                    % (track_index, r.p))
            status = running_status
            data1 = b

        kind = status & 0xF0
        ch = status & 0x0F
        if kind == 0x80:  # note off
            r.u8()  # release velocity, unused
            note_off(ch, data1 & 0x7F, tick)
        elif kind == 0x90:  # note on (velocity 0 == off)
            vel = r.u8()
            if vel == 0:
                note_off(ch, data1 & 0x7F, tick)
            else:
                note_on(ch, data1 & 0x7F, vel, tick)
        elif kind == 0xB0:  # control change — keep CC64 (sustain) only
            value = r.u8()
            if data1 == 64:
                pedal.append({
                    "tick": tick, "value": value, "channel": ch, "track": track_index,
                })
        elif kind in (0xA0, 0xE0):  # poly aftertouch / pitch bend: 2 data bytes
            r.u8()
        elif kind in (0xC0, 0xD0):  # program change / channel aftertouch: 1 data byte
            pass
        else:
            raise BadMidi(
                "track %d: unknown status 0x%02x at offset %d" % (track_index, status, r.p))

    if r.p > end:
        raise BadMidi("track %d: event ran past declared chunk length" % track_index)

    # Dangling notes close at the track's own end tick (ch ascending, then
    # note ascending — matches the firmware closeAll loop order).
    end_tick = tick
    for ch in range(16):
        for note in range(128):
            if (ch, note) in active:
                note_off(ch, note, end_tick)

    r.p = end  # tolerate trailing bytes after End-of-Track inside the chunk
    return name


def tick_to_micros(tempo, tpq, tick):
    """Exact-integer tick->microseconds walk of the tempo map
    (midi_parser.cpp tickToMicros): default 500000 us/quarter before the
    first tempo event, truncating integer division per segment. The corpus
    is constructed so every division here is exact (A83)."""
    us = 0
    cur = 0
    uspq = 500000
    tq = tpq if tpq else 480
    for tc in tempo:
        if tc["tick"] >= tick:
            break
        if tc["tick"] > cur:
            us += (tc["tick"] - cur) * uspq // tq
            cur = tc["tick"]
        uspq = tc["usPerQuarter"]
    us += (tick - cur) * uspq // tq
    return us


def _contains_word(name, word):
    return word in name.lower()


def _firmware_hands(track_names, notes, pedal):
    """TrackConfig::defaultsFor (track_config.cpp): hand + lights per raw
    MTrk track index. Notes-empty track -> Both (if it carries pedal, so
    demo playback keeps the sustain) else Off, lights off. Note-bearing:
    name containing left/lh -> Left, right/rh -> Right (case-insensitive
    substring), else Both (provisional). If EXACTLY two note-bearing tracks
    are still Both, first -> Right, second -> Left (piano convention)."""
    n = len(track_names)
    has_notes = [False] * n
    for nt in notes:
        if nt["track"] < n:
            has_notes[nt["track"]] = True
    has_pedal = [False] * n
    for p in pedal:
        if p["track"] < n:
            has_pedal[p["track"]] = True

    hand = ["both"] * n
    lights = [True] * n
    note_tracks = []
    for i in range(n):
        if not has_notes[i]:
            hand[i] = "both" if has_pedal[i] else "off"
            lights[i] = False
            continue
        note_tracks.append(i)
        nm = track_names[i]
        if _contains_word(nm, "left") or _contains_word(nm, "lh"):
            hand[i] = "left"
        elif _contains_word(nm, "right") or _contains_word(nm, "rh"):
            hand[i] = "right"
        else:
            hand[i] = "both"  # provisional; refined below

    if len(note_tracks) == 2:
        a, b = note_tracks
        if hand[a] == "both":
            hand[a] = "right"
        if hand[b] == "both":
            hand[b] = "left"

    return [{"hand": hand[i], "lights": lights[i]} for i in range(n)]


def _hand_of_name(name):
    # LEFT tokens checked BEFORE right, matching firmware defaultsFor: a name
    # matching both sets (e.g. "Left Rhythm" — "rhythm" contains "rh") is Left
    # (A87). Named left/lh -> 1(Left), right/rh -> 0(Right), else None.
    nm = (name or "").lower()
    if "left" in nm or "lh" in nm:
        return 1
    if "right" in nm or "rh" in nm:
        return 0
    return None


def _editor_view(track_names, notes, pedal, tempo, tpq):
    """editor.html handOfName()/normalizeHands(): canonical hand indices
    (0=Right, 1=Left, 2=Other) among note-bearing tracks — named left/lh ->
    1, right/rh -> 0 (left checked first, so "Left Rhythm" is Left; A87), else
    file order (first note-bearing -> 0, second -> 1, third+ -> 2). Pedals
    inherit their source track's mapped hand; a pedal on an unmapped
    (conductor/pedal-only) track buckets to Right (0)."""
    n = len(track_names)
    bears = [False] * n
    for nt in notes:
        if 0 <= nt["track"] < n:
            bears[nt["track"]] = True
    bearing = [ti for ti in range(n) if bears[ti]]

    mapping = {}
    for k, ti in enumerate(bearing):
        named = _hand_of_name(track_names[ti])
        mapping[ti] = named if named is not None else (0 if k == 0 else 1 if k == 1 else 2)

    ed_notes = []
    max_hand = 0
    for nt in notes:  # same order as the raw list (append-at-off, one stable onTick sort)
        hand = mapping.get(nt["track"], 0)
        max_hand = max(max_hand, hand)
        ed_notes.append({
            "onUs": tick_to_micros(tempo, tpq, nt["onTick"]),
            "offUs": tick_to_micros(tempo, tpq, nt["offTick"]),
            "note": nt["note"], "velocity": nt["velocity"], "hand": hand,
        })

    ed_pedals = []
    for p in pedal:
        hand = mapping.get(p["track"], 0)
        max_hand = max(max_hand, hand)
        ed_pedals.append({
            "us": tick_to_micros(tempo, tpq, p["tick"]),
            "value": p["value"], "hand": hand,
        })

    names = ["Right", "Left", "Other"]
    tracks = [names[h] if h < len(names) else ("hand " + str(h)) for h in range(max_hand + 1)]
    return {"tracks": tracks, "notes": ed_notes, "pedals": ed_pedals}


def parse_midi(data):
    """Parse an SMF byte string into the corpus twin dict: ticksPerQuarter,
    tracks (raw MTrk-order names, "" if absent), tempo, notes, pedal,
    firmwareHands, editor."""
    r = Reader(data)
    if len(data) < 14 or not r.match4("MThd"):
        raise BadMidi("bad or missing MThd header")
    header_len = r.u32()
    if header_len < 6:
        raise BadMidi("MThd length %d < 6" % header_len)
    r.u16()  # format (0/1/2 all parse identically downstream)
    ntrks = r.u16()
    division = r.u16()
    r.skip(header_len - 6)
    if division & 0x8000:
        raise BadMidi("SMPTE division not supported")
    tpq = division

    track_names = []
    notes = []
    tempo = []
    pedal = []
    for i in range(ntrks):
        track_names.append(_parse_track(r, i, notes, tempo, pedal))

    # Global stable sorts, once, after every track is parsed (parseMidi()).
    notes.sort(key=lambda n: n["onTick"])
    tempo.sort(key=lambda t: t["tick"])
    pedal.sort(key=lambda p: p["tick"])

    notes_out = [{
        "onTick": n["onTick"], "offTick": n["offTick"],
        "onUs": tick_to_micros(tempo, tpq, n["onTick"]),
        "offUs": tick_to_micros(tempo, tpq, n["offTick"]),
        "note": n["note"], "velocity": n["velocity"],
        "channel": n["channel"], "track": n["track"],
    } for n in notes]
    pedal_out = [{
        "tick": p["tick"], "us": tick_to_micros(tempo, tpq, p["tick"]),
        "value": p["value"], "channel": p["channel"], "track": p["track"],
    } for p in pedal]

    return {
        "ticksPerQuarter": tpq,
        "tracks": track_names,
        "tempo": [{"tick": t["tick"], "usPerQuarter": t["usPerQuarter"]} for t in tempo],
        "notes": notes_out,
        "pedal": pedal_out,
        "firmwareHands": _firmware_hands(track_names, notes, pedal),
        "editor": _editor_view(track_names, notes, pedal, tempo, tpq),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Print a Standard MIDI File as its JSON twin (corpus/midi schema)."
    )
    ap.add_argument("path", help="input .mid file, or '-' for stdin")
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
        twin = parse_midi(buf)
    except BadMidi as e:
        sys.stderr.write("midi_dump: %s\n" % e)
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
