#!/usr/bin/env python3
"""Golden MIDI conformance corpus generator (arch step C2).

Authors BOTH halves of each fixture from ONE declarative python model:

  1. the ``.mid`` bytes  — hand-authored Standard MIDI File (VLQ/meta helpers
     below; every byte here is written by us, nothing copyrighted), and for the
     ``recording-shaped`` fixture a BYTE-FOR-BYTE python replica of the firmware
     ``writeSmf`` (smf_writer.cpp) so the firmware test can byte-compare the real
     writer against the committed file.

  2. the expected twin ``*.expected.json`` — computed FROM THE MODEL by a python
     reference implementation of the parser semantics (midi_parser.cpp), NEVER by
     parsing the bytes we just emitted. The two derivations meeting is the point:
     the firmware/editor suites re-derive from the real code and must match.

The twin pins BOTH representations of the same file (A83):
  * firmware raw view  — note.track = raw MTrk index, hands via TrackConfig::defaultsFor
  * editor canonical view — hands via editor normalizeHands (0=Right,1=Left,2=Other)
with EXACT integer-microsecond times (asserted below): firmware truncating
integer division must equal editor round(ms*1000), or the two views drift.

Run from anywhere:  python corpus/gen/midi/gen_midi_fixtures.py
Writes corpus/midi/<name>.mid + <name>.expected.json and prints the editor
selftest MIDI_CORPUS snippet to stdout.
"""

import json
import os

# ---------------------------------------------------------------------------
# Low-level SMF byte helpers (big-endian; VLQ per SMF spec).
# ---------------------------------------------------------------------------


def u16(v):
    return bytes([(v >> 8) & 0xFF, v & 0xFF])


def u32(v):
    return bytes([(v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF])


def vlq(v):
    """Variable-length quantity, MSB-first, 7 bits/byte (matches putVlq)."""
    stack = []
    while True:
        stack.append(v & 0x7F)
        v >>= 7
        if v == 0:
            break
    out = bytearray()
    for i in range(len(stack) - 1, 0, -1):
        out.append(stack[i] | 0x80)
    out.append(stack[0])
    return bytes(out)


def tempo_bytes(uspq):
    return bytes([(uspq >> 16) & 0xFF, (uspq >> 8) & 0xFF, uspq & 0xFF])


def chunk(tag, body):
    return tag.encode("ascii") + u32(len(body)) + body


# ---------------------------------------------------------------------------
# Declarative track model.  A track is {name, events, running}.  Each event is
# an absolute-tick tuple; events MUST be listed in non-decreasing tick order
# (same-tick order is preserved, which is how off-before-on ties are pinned):
#   ("name",  tick, str)         meta 0x03  (first per track wins in the parser)
#   ("tempo", tick, uspq)        meta 0x51
#   ("on",    tick, ch, note, vel)   0x90 note-on
#   ("off",   tick, ch, note)         0x80 note-off (release vel 0)
#   ("on0",   tick, ch, note)         0x90 ... 00  (velocity-0 note-on == off)
#   ("cc64",  tick, ch, val)          0xB0 controller 64 (sustain)
# running=True compresses consecutive same-status channel messages (running
# status); it changes only the BYTES, never the model the twin is derived from.
# ---------------------------------------------------------------------------


def serialize_track(track):
    body = bytearray()
    last_tick = 0
    running_status = 0  # 0 = none; meta/sysex reset it
    use_running = track.get("running", False)

    def delta(tick):
        nonlocal last_tick
        d = tick - last_tick
        assert d >= 0, "events must be listed in non-decreasing tick order"
        last_tick = tick
        return vlq(d)

    def channel_msg(tick, status, d1, d2=None):
        nonlocal running_status
        body.extend(delta(tick))
        if not (use_running and status == running_status):
            body.append(status)
        running_status = status
        body.append(d1)
        if d2 is not None:
            body.append(d2)

    for ev in track["events"]:
        kind = ev[0]
        if kind == "name":
            _, tick, name = ev
            body.extend(delta(tick))
            body.append(0xFF)
            body.append(0x03)
            nb = name.encode("utf-8")
            body.extend(vlq(len(nb)))
            body.extend(nb)
            running_status = 0
        elif kind == "tempo":
            _, tick, uspq = ev
            body.extend(delta(tick))
            body.append(0xFF)
            body.append(0x51)
            body.append(0x03)
            body.extend(tempo_bytes(uspq))
            running_status = 0
        elif kind == "on":
            _, tick, ch, note, vel = ev
            channel_msg(tick, 0x90 | ch, note, vel)
        elif kind == "on0":
            _, tick, ch, note = ev
            channel_msg(tick, 0x90 | ch, note, 0)
        elif kind == "off":
            _, tick, ch, note = ev
            channel_msg(tick, 0x80 | ch, note, 0)
        elif kind == "cc64":
            _, tick, ch, val = ev
            channel_msg(tick, 0xB0 | ch, 64, val)
        else:
            raise ValueError("unknown event kind " + kind)

    # End of Track.
    body.extend(vlq(0))
    body.append(0xFF)
    body.append(0x2F)
    body.append(0x00)
    return chunk("MTrk", bytes(body))


def build_smf_bytes(model):
    ntrks = len(model["tracks"])
    header = u16(1) + u16(ntrks) + u16(model["tpq"])  # format 1
    out = bytearray(chunk("MThd", header))
    for tr in model["tracks"]:
        out.extend(serialize_track(tr))
    return bytes(out)


# ---------------------------------------------------------------------------
# Reference parser (mirrors midi_parser.cpp NoteTracker + push-at-off + sorts).
# Consumes the typed event model, NOT bytes.
# ---------------------------------------------------------------------------


class ParseSim:
    def __init__(self):
        self.notes = []   # {onTick,offTick,note,velocity,channel,track}
        self.tempo = []   # {tick,usPerQuarter}
        self.pedal = []   # {tick,value,channel,track}
        self.track_names = []

    def parse(self, model):
        for ti, tr in enumerate(model["tracks"]):
            self._parse_track(ti, tr)
        # Global stable sorts (Python sort is stable), matching parseMidi().
        self.notes.sort(key=lambda n: n["onTick"])
        self.tempo.sort(key=lambda t: t["tick"])
        self.pedal.sort(key=lambda p: p["tick"])
        return self

    def _parse_track(self, ti, tr):
        name = ""  # first 0x03 wins
        active = {}  # (ch,note) -> (onTick, vel)
        self.track_names.append("")  # placeholder, set below

        def off(ch, note, tick):
            key = (ch, note)
            if key not in active:
                return  # spurious note-off: ignore
            on_tick, vel = active.pop(key)
            self.notes.append({
                "onTick": on_tick, "offTick": tick, "note": note,
                "velocity": vel, "channel": ch, "track": ti,
            })

        def on(ch, note, vel, tick):
            if (ch, note) in active:
                off(ch, note, tick)  # re-strike closes previous instance
            active[(ch, note)] = (tick, vel)

        for ev in tr["events"]:
            kind = ev[0]
            if kind == "name":
                if name == "":
                    name = ev[2]
            elif kind == "tempo":
                self.tempo.append({"tick": ev[1], "usPerQuarter": ev[2]})
            elif kind == "on":
                on(ev[2], ev[3], ev[4], ev[1])
            elif kind == "on0":
                off(ev[2], ev[3], ev[1])
            elif kind == "off":
                off(ev[2], ev[3], ev[1])
            elif kind == "cc64":
                self.pedal.append({
                    "tick": ev[1], "value": ev[3], "channel": ev[2], "track": ti,
                })

        # closeAll: ch ascending, then note ascending (matches C++ loop order).
        last_tick = 0
        for ev in tr["events"]:
            last_tick = max(last_tick, ev[1])
        for ch in range(16):
            for note in range(128):
                if (ch, note) in active:
                    off(ch, note, last_tick)

        self.track_names[ti] = name


def tick_to_micros(tempo, tpq, tick):
    """Exact-integer tickToMicros (midi_parser.cpp). Asserts each per-segment
    division has zero remainder, enforcing the corpus exactness rule (A83)."""
    us = 0
    cur = 0
    uspq = 500000  # MIDI default, 120 BPM
    tq = tpq if tpq else 480
    for tc in tempo:
        if tc["tick"] >= tick:
            break
        if tc["tick"] > cur:
            num = (tc["tick"] - cur) * uspq
            assert num % tq == 0, "inexact us at tempo boundary tick %d" % tc["tick"]
            us += num // tq
            cur = tc["tick"]
        uspq = tc["usPerQuarter"]
    num = (tick - cur) * uspq
    assert num % tq == 0, "inexact us at tick %d (tpq %d uspq %d)" % (tick, tq, uspq)
    return us + num // tq


# ---------------------------------------------------------------------------
# TrackConfig::defaultsFor reference → firmwareHands.
# ---------------------------------------------------------------------------


def contains_word(name, word):
    return word in name.lower()


def firmware_hands(track_names, notes, pedal):
    n = len(track_names)
    has_notes = [False] * n
    for nt in notes:
        if nt["track"] < n:
            has_notes[nt["track"]] = True
    has_pedal = [False] * n
    for p in pedal:
        if p["track"] < n:
            has_pedal[p["track"]] = True

    hands = ["both"] * n
    lights = [True] * n
    note_tracks = []
    for i in range(n):
        if not has_notes[i]:
            hands[i] = "both" if has_pedal[i] else "off"
            lights[i] = False
            continue
        note_tracks.append(i)
        nm = track_names[i]
        if contains_word(nm, "left") or contains_word(nm, "lh"):
            hands[i] = "left"
        elif contains_word(nm, "right") or contains_word(nm, "rh"):
            hands[i] = "right"
        else:
            hands[i] = "both"
    if len(note_tracks) == 2:
        if hands[note_tracks[0]] == "both":
            hands[note_tracks[0]] = "right"
        if hands[note_tracks[1]] == "both":
            hands[note_tracks[1]] = "left"
    return [{"hand": hands[i], "lights": lights[i]} for i in range(n)]


# ---------------------------------------------------------------------------
# Editor normalizeHands reference → canonical (0=Right,1=Left,2=Other) view.
# ---------------------------------------------------------------------------


def hand_of_name(name):
    nm = (name or "").lower()
    if "right" in nm or "rh" in nm:
        return 0
    if "left" in nm or "lh" in nm:
        return 1
    return None


def editor_view(track_names, notes, pedal, tempo, tpq):
    n = len(track_names)
    bears = [False] * n
    for nt in notes:
        if 0 <= nt["track"] < n:
            bears[nt["track"]] = True
    bearing = [ti for ti in range(n) if bears[ti]]
    mapping = {}
    for k, ti in enumerate(bearing):
        named = hand_of_name(track_names[ti])
        mapping[ti] = named if named is not None else (0 if k == 0 else 1 if k == 1 else 2)

    ed_notes = []
    max_hand = 0
    for nt in notes:  # same order as firmware (append-at-off then stable onTick sort)
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


# ---------------------------------------------------------------------------
# Twin builder (firmware raw view + editor canonical view).
# ---------------------------------------------------------------------------


def build_twin(model):
    sim = ParseSim().parse(model)
    tpq = model["tpq"]
    tempo = sim.tempo

    notes_out = []
    for nt in sim.notes:
        notes_out.append({
            "onTick": nt["onTick"], "offTick": nt["offTick"],
            "onUs": tick_to_micros(tempo, tpq, nt["onTick"]),
            "offUs": tick_to_micros(tempo, tpq, nt["offTick"]),
            "note": nt["note"], "velocity": nt["velocity"],
            "channel": nt["channel"], "track": nt["track"],
        })
    pedal_out = []
    for p in sim.pedal:
        pedal_out.append({
            "tick": p["tick"], "us": tick_to_micros(tempo, tpq, p["tick"]),
            "value": p["value"], "channel": p["channel"], "track": p["track"],
        })

    twin = {
        "ticksPerQuarter": tpq,
        "tracks": list(sim.track_names),
        "tempo": [{"tick": t["tick"], "usPerQuarter": t["usPerQuarter"]} for t in tempo],
        "notes": notes_out,
        "pedal": pedal_out,
        "firmwareHands": firmware_hands(sim.track_names, sim.notes, sim.pedal),
        "editor": editor_view(sim.track_names, sim.notes, sim.pedal, tempo, tpq),
    }
    return twin


# ---------------------------------------------------------------------------
# writeSmf byte-exact replica (smf_writer.cpp) — recording-shaped fixture.
# ---------------------------------------------------------------------------

SMF_TPQ = 500
SMF_USPQ = 500000


def write_smf(smf_input):
    """Byte-for-byte replica of vialucis::writeSmf. smf_input = list of tracks,
    each {name, notes:[(onMs,offMs,note,vel,ch)], pedals:[(tMs,val,ch)]}."""
    file = bytearray()
    ntrks = 1 + len(smf_input)
    file.extend(chunk("MThd", u16(1) + u16(ntrks) + u16(SMF_TPQ)))

    # Track 0 — conductor: one tempo meta at delta 0.
    cbody = bytearray()
    cbody.extend(vlq(0))
    cbody.append(0xFF)
    cbody.append(0x51)
    cbody.append(0x03)
    cbody.extend(tempo_bytes(SMF_USPQ))
    cbody.extend(vlq(0))
    cbody.extend(b"\xFF\x2F\x00")
    file.extend(chunk("MTrk", bytes(cbody)))

    for tr in smf_input:
        msgs = []  # (t, order, b0, b1, b2)
        for (on_ms, off_ms, note, vel, ch) in tr["notes"]:
            off = off_ms if off_ms >= on_ms else on_ms
            msgs.append((on_ms, 2, 0x90 | (ch & 0x0F), note & 0x7F, vel & 0x7F))
            msgs.append((off, 0, 0x80 | (ch & 0x0F), note & 0x7F, 0))
        for (t_ms, val, ch) in tr["pedals"]:
            msgs.append((t_ms, 1, 0xB0 | (ch & 0x0F), 64, val & 0x7F))
        # stable sort by (t, order)
        msgs.sort(key=lambda m: (m[0], m[1]))

        body = bytearray()
        if tr["name"]:
            body.extend(vlq(0))
            body.append(0xFF)
            body.append(0x03)
            nb = tr["name"].encode("utf-8")
            body.extend(vlq(len(nb)))
            body.extend(nb)
        last = 0
        for (t, _order, b0, b1, b2) in msgs:
            body.extend(vlq(t - last))
            last = t
            body.append(b0)
            body.append(b1)
            body.append(b2)
        body.extend(vlq(0))
        body.extend(b"\xFF\x2F\x00")
        file.extend(chunk("MTrk", bytes(body)))
    return bytes(file)


def smf_to_model(smf_input):
    """Build the typed event model equivalent to write_smf's emission order, so
    the twin's ParseSim sees events exactly as the real parser reads the bytes
    (identical same-tick ordering — the collision pin)."""
    tracks = [{"name": "", "events": [("tempo", 0, SMF_USPQ)]}]  # conductor (no name)
    for tr in smf_input:
        msgs = []
        for (on_ms, off_ms, note, vel, ch) in tr["notes"]:
            off = off_ms if off_ms >= on_ms else on_ms
            msgs.append((on_ms, 2, ("on", ch, note, vel)))
            msgs.append((off, 0, ("off", ch, note)))
        for (t_ms, val, ch) in tr["pedals"]:
            msgs.append((t_ms, 1, ("cc64", ch, val)))
        msgs.sort(key=lambda m: (m[0], m[1]))
        events = []
        if tr["name"]:
            events.append(("name", 0, tr["name"]))
        for (t, _order, payload) in msgs:
            if payload[0] == "on":
                events.append(("on", t, payload[1], payload[2], payload[3]))
            elif payload[0] == "off":
                events.append(("off", t, payload[1], payload[2]))
            elif payload[0] == "cc64":
                events.append(("cc64", t, payload[1], payload[2]))
        tracks.append({"name": tr["name"], "events": events})
    return {"tpq": SMF_TPQ, "tracks": tracks}


# ---------------------------------------------------------------------------
# Fixture definitions.
# ---------------------------------------------------------------------------


def note_pair(events, tick_on, tick_off, ch, note, vel):
    events.append(("on", tick_on, ch, note, vel))
    events.append(("off", tick_off, ch, note))


def fixture_conductor_first():
    cond = {"name": "Conductor", "events": [("name", 0, "Conductor"), ("tempo", 0, 500000)]}
    right = {"name": "Right", "events": [("name", 0, "Right")]}
    note_pair(right["events"], 0, 240, 0, 72, 100)
    note_pair(right["events"], 240, 480, 0, 76, 100)
    left = {"name": "Left", "events": [("name", 0, "Left")]}
    note_pair(left["events"], 0, 480, 0, 48, 80)
    return {"tpq": 480, "tracks": [cond, right, left]}


def fixture_named_lh_rh():
    # LH FIRST in file order; name (lower/mixed case) must beat file order.
    lh = {"name": "Piano lh", "events": [("name", 0, "Piano lh")]}
    lh["events"].append(("on", 0, 0, 48, 80))
    lh["events"].append(("on", 240, 0, 50, 80))
    lh["events"].append(("off", 480, 0, 48))
    lh["events"].append(("off", 480, 0, 50))
    rh = {"name": "Piano RH", "events": [("name", 0, "Piano RH")]}
    note_pair(rh["events"], 0, 240, 0, 72, 100)
    return {"tpq": 480, "tracks": [lh, rh]}


def fixture_anonymous_pair():
    # Two unnamed note tracks, NO conductor, NO tempo meta at all.
    t0 = {"name": "", "events": []}
    note_pair(t0["events"], 0, 240, 0, 70, 100)
    t1 = {"name": "", "events": []}
    note_pair(t1["events"], 0, 240, 0, 50, 80)
    return {"tpq": 480, "tracks": [t0, t1]}


def fixture_three_hands():
    cond = {"name": "Conductor", "events": [("name", 0, "Conductor"), ("tempo", 0, 500000)]}
    right = {"name": "Right", "events": [("name", 0, "Right")]}
    note_pair(right["events"], 0, 240, 0, 72, 100)
    left = {"name": "Left", "events": [("name", 0, "Left")]}
    note_pair(left["events"], 0, 240, 0, 48, 80)
    third = {"name": "", "events": []}  # unnamed 3rd note track (ch 3 pins channel field)
    note_pair(third["events"], 0, 240, 3, 60, 90)
    return {"tpq": 480, "tracks": [cond, right, left, third]}


def fixture_pedal_only_track():
    # "Right" notes + a "Pedal" track carrying ONLY CC64 on a non-zero channel.
    right = {"name": "Right", "events": [("name", 0, "Right")]}
    note_pair(right["events"], 0, 240, 0, 72, 100)
    pedal = {"name": "Pedal", "events": [("name", 0, "Pedal")]}
    pedal["events"].append(("cc64", 0, 2, 127))
    pedal["events"].append(("cc64", 240, 2, 0))
    return {"tpq": 480, "tracks": [right, pedal]}


def fixture_tie_order():
    # TPQ 480, all ticks multiples of 3. One track "Right", running status ON.
    #   note 60: on@0 v100, clean off@240, RE-STRIKE on@240 v90, off@480
    #            (off-before-on tie at 240 — clean re-strike)
    #   note 62: on@0 v80, RE-STRIKE on@480 v70 WITHOUT an off (auto-close@480), off@720
    #   note 64: on@0 v64, closed by a VELOCITY-0 note-on at 240 (vel-0 == off)
    ev = [("name", 0, "Right")]
    ev.append(("on", 0, 0, 60, 100))   # A on   (0x90 status)
    ev.append(("on", 0, 0, 62, 80))    # B on   (running status)
    ev.append(("on", 0, 0, 64, 64))    # C on   (running status)
    ev.append(("off", 240, 0, 60))     # A clean off (0x80)
    ev.append(("on", 240, 0, 60, 90))  # A restrike (off BEFORE on at 240)
    ev.append(("on0", 240, 0, 64))     # C vel-0 note-on == off (running 0x90)
    ev.append(("on", 480, 0, 62, 70))  # B restrike, no prior off -> auto-close B@480
    ev.append(("off", 480, 0, 60))     # A off (0x80)
    ev.append(("off", 720, 0, 62))     # B off
    return {"tpq": 480, "tracks": [{"name": "Right", "events": ev, "running": True}]}


def fixture_vlq_tempo():
    # Multi-byte VLQ deltas (2-byte 129 & 480, 3-byte 16386) + 3 tempo segments.
    # seg0 uspq 500000 (ticks mult of 3 exact); seg1 480000 & seg2 240000 both
    # divide tpq 480 exactly, so all cross-segment note times stay exact.
    ev = [("name", 0, "Right"), ("tempo", 0, 500000)]
    ev.append(("on", 0, 0, 60, 100))
    ev.append(("off", 129, 0, 60))          # delta 129 -> 2-byte VLQ (0x81 0x01)
    ev.append(("tempo", 129, 480000))
    ev.append(("on", 16515, 0, 62, 90))     # delta 16386 -> 3-byte VLQ (0x81 0x80 0x02)
    ev.append(("off", 16995, 0, 62))        # delta 480 -> 2-byte VLQ
    ev.append(("tempo", 16995, 240000))
    ev.append(("on", 16995, 0, 64, 80))
    ev.append(("off", 17955, 0, 64))
    return {"tpq": 480, "tracks": [{"name": "Right", "events": ev}]}


# recording-shaped uses the writeSmf replica.
REC_SHAPED_INPUT = [
    {"name": "Right",
     "notes": [(0, 500, 72, 100, 0), (500, 1000, 74, 90, 0)],
     # collision at 500 ms: note-off(72) < pedal(64) < note-on(74)
     "pedals": [(0, 127, 0), (500, 64, 0), (1000, 0, 0)]},
    {"name": "Left",
     "notes": [(0, 1000, 48, 80, 0)],
     "pedals": []},
]


FIXTURES = [
    ("conductor-first", fixture_conductor_first, None),
    ("named-lh-rh", fixture_named_lh_rh, None),
    ("anonymous-pair", fixture_anonymous_pair, None),
    ("three-hands", fixture_three_hands, None),
    ("pedal-only-track", fixture_pedal_only_track, None),
    ("tie-order", fixture_tie_order, None),
    ("vlq-tempo", fixture_vlq_tempo, None),
    ("recording-shaped", lambda: smf_to_model(REC_SHAPED_INPUT), REC_SHAPED_INPUT),
]


# ---------------------------------------------------------------------------
# Emit.
# ---------------------------------------------------------------------------


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    # corpus/gen/midi/ -> repo root
    return os.path.abspath(os.path.join(here, "..", "..", ".."))


def hex_lines(data, width=68):
    h = data.hex()
    parts = [h[i:i + width] for i in range(0, len(h), width)]
    return parts


def js_number_list(objs, keys):
    return objs


def emit():
    root = repo_root()
    out_dir = os.path.join(root, "corpus", "midi")
    os.makedirs(out_dir, exist_ok=True)

    snippets = []
    for name, model_fn, smf_input in FIXTURES:
        model = model_fn()
        if smf_input is not None:
            midi_bytes = write_smf(smf_input)
        else:
            midi_bytes = build_smf_bytes(model)
        twin = build_twin(model)

        with open(os.path.join(out_dir, name + ".mid"), "wb") as f:
            f.write(midi_bytes)
        with open(os.path.join(out_dir, name + ".expected.json"), "w",
                  encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(twin, indent=2) + "\n")

        snippets.append((name, midi_bytes, twin))
        print("wrote %-18s %4d bytes  %2d notes %2d pedal %d tracks"
              % (name, len(midi_bytes), len(twin["notes"]),
                 len(twin["pedal"]), len(twin["tracks"])))

    print_js_snippet(snippets)


def print_js_snippet(snippets):
    print("\n" + "=" * 72)
    print("Editor selftest MIDI_CORPUS snippet (paste into editor.html):")
    print("=" * 72 + "\n")
    lines = ["const MIDI_CORPUS = {"]
    for i, (name, midi_bytes, twin) in enumerate(snippets):
        ed = twin["editor"]
        parts = hex_lines(midi_bytes)
        lines.append('  "%s": {' % name)
        lines.append("    hex:")
        for j, p in enumerate(parts):
            suffix = " +" if j < len(parts) - 1 else ","
            lines.append('      "%s"%s' % (p, suffix))
        # expect object (editor canonical view) + tempo for spot-assert.
        tracks = json.dumps(ed["tracks"])
        lines.append("    expect: {")
        lines.append("      tracks: %s," % tracks)
        notes_js = ", ".join(
            "{onUs:%d,offUs:%d,note:%d,velocity:%d,hand:%d}"
            % (n["onUs"], n["offUs"], n["note"], n["velocity"], n["hand"])
            for n in ed["notes"])
        lines.append("      notes: [%s]," % notes_js)
        peds_js = ", ".join(
            "{us:%d,value:%d,hand:%d}" % (p["us"], p["value"], p["hand"])
            for p in ed["pedals"])
        lines.append("      pedals: [%s]," % peds_js)
        tempo_js = ", ".join(
            "{tick:%d,usPerQuarter:%d}" % (t["tick"], t["usPerQuarter"])
            for t in twin["tempo"])
        lines.append("      tempo: [%s]" % tempo_js)
        lines.append("    }")
        lines.append("  }%s" % ("," if i < len(snippets) - 1 else ""))
    lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    emit()
