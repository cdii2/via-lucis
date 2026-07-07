#!/usr/bin/env python3
"""Via Lucis public-domain starter pack generator.

Emits the five songs/pd/*.mid files programmatically (raw SMF bytes, no
dependencies) so the arrangements are reproducible and their provenance is
auditable. Run:  python generate.py   (from songs/pd/)

Format for every file: SMF format 1, 480 ticks/quarter, three tracks:
  track 0  conductor (tempo + time signature)
  track 1  "Right"   melody       -> firmware auto-assigns right hand
  track 2  "Left"    simple bass  -> firmware auto-assigns left hand
Velocity 80 throughout. All arrangements original, dedicated CC0.

Melody sources (all public-domain works):
  Ode to Joy          Beethoven 9th "Hymn to Joy" theme, C major (traditional
                      easy key), standard 16-bar form.
  Amazing Grace       "New Britain" hymn tune, G major 3/4, note-for-note from
                      the hymnal melody (Carrell & Clayton 1831 setting as
                      published in Colin Hume's ABC transcription).
  Minuet in G         Petzold, BWV Anh. 114, complete 32 bars, melody and a
                      simplified version of the original bass, from a public
                      ABC transcription cross-checked against the urtext.
  Vivaldi Largo       Winter (RV 297) 2nd mvt, complete 18-bar solo-violin
                      melody from the Mutopia Project LilyPond urtext,
                      transposed Eb->C major; cello pizzicato line simplified
                      to half/whole-note roots.
  Clair de Lune       Debussy, Suite Bergamasque, famous opening (bars 1-9)
                      from the Mutopia Project urtext (E. Fromont 1905 plate),
                      transposed Db->C major, inner voices thinned.
"""
import struct
from pathlib import Path

TPQ = 480          # ticks per quarter note
E = TPQ // 2       # eighth
S = TPQ // 4       # sixteenth
Q = TPQ            # quarter
H = 2 * TPQ        # half
DH = 3 * TPQ       # dotted half
W = 4 * TPQ        # whole
VEL = 80

# ---------------------------------------------------------------- SMF bytes

def vlq(v):
    out = bytearray([v & 0x7F])
    v >>= 7
    while v:
        out.insert(0, 0x80 | (v & 0x7F))
        v >>= 7
    return bytes(out)


def track_chunk(events):
    """events: list of (abs_tick, bytes) -> MTrk chunk with EOT appended."""
    events = sorted(events, key=lambda e: e[0])
    body = bytearray()
    last = 0
    for tick, data in events:
        body += vlq(tick - last) + data
        last = tick
    body += vlq(0) + b"\xff\x2f\x00"          # end of track
    return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def meta(tick, mtype, data):
    return (tick, bytes([0xFF, mtype]) + vlq(len(data)) + data)


def tempo_meta(tick, bpm):
    return meta(tick, 0x51, int(60_000_000 / bpm).to_bytes(3, "big"))


def timesig_meta(tick, num, den_pow2):
    return meta(tick, 0x58, bytes([num, den_pow2, 24, 8]))


def name_meta(name):
    return meta(0, 0x03, name.encode("ascii"))


def notes_to_events(notes):
    """notes: list of (start_tick, midi_note, dur_ticks) -> on/off events."""
    ev = []
    for start, note, dur in notes:
        ev.append((start, bytes([0x90, note, VEL])))
        ev.append((start + dur, bytes([0x80, note, 0])))
    return ev


def write_song(path, bpm, timesig, right_notes, left_notes):
    conductor = track_chunk([tempo_meta(0, bpm), timesig_meta(0, *timesig)])
    right = track_chunk([name_meta("Right")] + notes_to_events(right_notes))
    left = track_chunk([name_meta("Left")] + notes_to_events(left_notes))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, 3, TPQ)
    Path(path).write_bytes(header + conductor + right + left)
    print(f"wrote {path}")


def seq(items, start=0):
    """Melodic sequence -> absolute notes. items: (note|chord|None, dur)."""
    out, t = [], start
    for note, dur in items:
        if note is not None:
            for n in (note if isinstance(note, tuple) else (note,)):
                out.append((t, n, dur))
        t += dur
    return out

# ------------------------------------------------------- note number helper
_PC = dict(C=0, D=2, E=4, F=5, G=7, A=9, B=11)

def N(name):
    """'C4'->60, 'F#3'->54, 'Eb5'->75."""
    pc = _PC[name[0]]
    rest = name[1:]
    if rest[0] == "#":
        pc, rest = pc + 1, rest[1:]
    elif rest[0] == "b":
        pc, rest = pc - 1, rest[1:]
    return 12 * (int(rest) + 1) + pc

# =========================================================== 1  Ode to Joy
# Beethoven, Hymn to Joy theme. C major, 4/4, 16 bars.

def ode_to_joy():
    m = [N(x) for x in
         "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4".split()]
    ph1 = [(m[0], Q), (m[1], Q), (m[2], Q), (m[3], Q),
           (m[4], Q), (m[5], Q), (m[6], Q), (m[7], Q),
           (m[8], Q), (m[9], Q), (m[10], Q), (m[11], Q)]
    end_half = [(N("E4"), Q + E), (N("D4"), E), (N("D4"), H)]   # bar 4
    end_full = [(N("D4"), Q + E), (N("C4"), E), (N("C4"), H)]   # bars 8/16
    ph3 = [(N("D4"), Q), (N("D4"), Q), (N("E4"), Q), (N("C4"), Q),
           (N("D4"), Q), (N("E4"), E), (N("F4"), E), (N("E4"), Q), (N("C4"), Q),
           (N("D4"), Q), (N("E4"), E), (N("F4"), E), (N("E4"), Q), (N("D4"), Q),
           (N("C4"), Q), (N("D4"), Q), (N("G3"), H)]
    right = seq(ph1 + end_half + ph1 + end_full + ph3 + ph1 + end_full)

    C3, G2, D3 = N("C3"), N("G2"), N("D3")
    bars_1_4 = [(C3, W), (C3, H), (G2, H), (C3, H), (G2, H), (G2, W)]
    bars_5_8 = [(C3, W), (C3, H), (G2, H), (C3, H), (G2, H), (G2, H), (C3, H)]
    bars_9_12 = [(G2, H), (C3, H), (G2, H), (C3, H), (G2, W), (D3, H), (G2, H)]
    left = seq(bars_1_4 + bars_5_8 + bars_9_12 + bars_5_8)
    write_song("ode-to-joy.mid", 108, (4, 2), right, left)

# ======================================================== 2  Amazing Grace
# "New Britain" hymn tune. G major, 3/4, quarter pickup + 16 bars.
# Melody per the hymnal ABC transcription (colinhume.com), ties realized.

def amazing_grace():
    G4, A4, B4, E4, D4, D5 = (N(x) for x in "G4 A4 B4 E4 D4 D5".split())
    line = [(G4, H), (B4, E), (G4, E), (B4, H), (A4, Q)]        # "grace how"
    right = seq(
        [(D4, Q)] + line +                                      # pickup, m1-2
        [(G4, H), (E4, Q), (D4, H), (D4, Q)] +                  # m3-4
        line +                                                  # m5-6
        [(D5, DH + H), (B4, Q)] +                               # m7-8 (tie)
        [(D5, H), (B4, E), (G4, E), (B4, H), (A4, Q)] +         # m9-10
        [(G4, H), (E4, Q), (D4, H), (D4, Q)] +                  # m11-12
        line +                                                  # m13-14
        [(G4, DH + H)])                                         # m15-16 (tie)
    G2, C3, D3, E3 = N("G2"), N("C3"), N("D3"), N("E3")
    left = seq(
        [(None, Q)] +                                           # pickup rest
        [(G2, DH), (G2, DH), (C3, DH), (G2, H), (D3, Q),        # m1-4
         (G2, DH), (E3, DH), (D3, DH), (D3, DH),                # m5-8
         (G2, DH), (G2, DH), (C3, DH), (G2, H), (D3, Q),        # m9-12
         (E3, DH), (D3, DH), (G2, DH), (G2, H)])                # m13-16
    write_song("amazing-grace.mid", 90, (3, 2), right, left)

# ========================================================= 3  Minuet in G
# Petzold, BWV Anh. 114. G major, 3/4, full 32 bars (no repeats -- the
# trainer's A-B loop covers repetition). Bass simplified from the original.

def minuet_in_g():
    def bar_qe4(a, b, c, d, e):          # quarter + four eighths
        return [(N(a), Q), (N(b), E), (N(c), E), (N(d), E), (N(e), E)]

    right = seq(
        bar_qe4("D5", "G4", "A4", "B4", "C5") +                 # 1
        [(N("D5"), Q), (N("G4"), Q), (N("G4"), Q)] +            # 2
        bar_qe4("E5", "C5", "D5", "E5", "F#5") +                # 3
        [(N("G5"), Q), (N("G4"), Q), (N("G4"), Q)] +            # 4
        bar_qe4("C5", "D5", "C5", "B4", "A4") +                 # 5
        bar_qe4("B4", "C5", "B4", "A4", "G4") +                 # 6
        bar_qe4("F#4", "G4", "A4", "B4", "G4") +                # 7
        [(N("A4"), DH)] +                                       # 8
        bar_qe4("D5", "G4", "A4", "B4", "C5") +                 # 9
        [(N("D5"), Q), (N("G4"), Q), (N("G4"), Q)] +            # 10
        bar_qe4("E5", "C5", "D5", "E5", "F#5") +                # 11
        [(N("G5"), Q), (N("G4"), Q), (N("G4"), Q)] +            # 12
        bar_qe4("C5", "D5", "C5", "B4", "A4") +                 # 13
        bar_qe4("B4", "C5", "B4", "A4", "G4") +                 # 14
        bar_qe4("A4", "B4", "A4", "G4", "F#4") +                # 15
        [(N("G4"), DH)] +                                       # 16
        bar_qe4("B5", "G5", "A5", "B5", "G5") +                 # 17
        bar_qe4("A5", "D5", "E5", "F#5", "D5") +                # 18
        bar_qe4("G5", "E5", "F#5", "G5", "D5") +                # 19
        [(N("C#5"), Q), (N("B4"), E), (N("C#5"), E), (N("A4"), Q)] +  # 20
        [(N(x), E) for x in "A4 B4 C#5 D5 E5 F#5".split()] +    # 21
        [(N("G5"), Q), (N("F#5"), Q), (N("E5"), Q)] +           # 22
        [(N("F#5"), Q), (N("A4"), Q), (N("C#5"), Q)] +          # 23
        [(N("D5"), DH)] +                                       # 24
        [(N("D5"), Q), (N("G4"), E), (N("F#4"), E), (N("G4"), Q)] +   # 25
        [(N("E5"), Q), (N("G4"), E), (N("F#4"), E), (N("G4"), Q)] +   # 26
        [(N("D5"), Q), (N("C5"), Q), (N("B4"), Q)] +            # 27
        [(N("A4"), E), (N("G4"), E), (N("F#4"), E), (N("G4"), E),
         (N("A4"), Q)] +                                        # 28
        [(N(x), E) for x in "D4 E4 F#4 G4 A4 B4".split()] +     # 29
        [(N("C5"), Q), (N("B4"), Q), (N("A4"), Q)] +            # 30
        [(N("B4"), E), (N("D5"), E), (N("G4"), Q), (N("F#4"), Q)] +   # 31
        [(N("G4"), DH)])                                        # 32

    L = lambda a, b=None, c=None: (
        [(N(a), DH)] if b is None else
        ([(N(a), H), (N(b), Q)] if c is None else
         [(N(a), Q), (N(b), Q), (N(c), Q)]))
    left = seq(
        L("G3", "A3") + L("B3") + L("C4") + L("B3") +           # 1-4
        L("A3") + L("G3") + L("D4", "B3", "G3") + L("D4", "D3") +  # 5-8
        L("B3", "A3") + L("G3", "B3", "G3") + L("C4") +         # 9-11
        L("B3", "G3") + L("A3", "F#3") + L("G3", "B3") +        # 12-14
        L("C4", "D4", "D3") + L("G3", "G2") +                   # 15-16
        L("G3") + L("F#3") + L("E3", "G3", "E3") + L("A3", "A2") +  # 17-20
        L("A3") + L("B3", "D4", "C#4") + L("D4", "F#3", "A3") + # 21-23
        L("D4", "D3", "C4") +                                   # 24
        L("B3") + L("C4") + L("B3", "A3", "G3") +               # 25-27
        [(N("D4"), H), (None, Q)] +                             # 28
        L("D3", "F#3") + L("E3", "G3", "F#3") +                 # 29-30
        L("G3", "B2", "D3") + L("G3", "D3", "G2"))              # 31-32
    write_song("minuet-in-g.mid", 100, (3, 2), right, left)

# ===================================================== 4  Vivaldi Largo
# Winter (RV 297), 2nd movement, complete solo melody (18 bars) from the
# Mutopia urtext, transposed Eb -> C major. Bass = cello line reduced to
# half/whole notes. Trills and slurs dropped; ties realized as long notes.

def vivaldi_largo():
    def b16(*names):                      # run of sixteenths
        return [(N(x), S) for x in names]

    right = seq(
        # 1
        [(N("C5"), E)] + b16("G5", "F5") + [(N("E5"), E)] + b16("D5", "C5") +
        [(N("D5"), E), (N("G4"), E), (None, E), (N("G4"), E)] +
        # 2
        b16("F5", "E5", "D5", "C5") +
        [(N("B4"), E), (N("F5"), E), (N("F5"), E), (N("E5"), E),
         (None, E), (N("E5"), E)] +
        # 3
        [(N("D5"), E)] + b16("E5", "F5") + [(N("G5"), E)] + b16("A5", "B5") +
        [(N("C5"), E)] + b16("D5", "E5") + [(N("F5"), E)] + b16("G5", "A5") +
        # 4
        [(N("B4"), E)] + b16("C5", "D5") + [(N("E5"), E)] + b16("F5", "G5") +
        [(N("A4"), E)] + b16("B4", "C5") + [(N("D5"), E)] + b16("E5", "C5") +
        # 5  (tied quarters realized: quarter + sixteenth)
        [(N("B4"), Q + S)] + b16("G4", "F#4", "G4") +
        [(N("D5"), Q + S)] + b16("G4", "F#4", "G4") +
        # 6
        [(N("E5"), Q + S)] + b16("G4", "F#4", "G4") +
        [(N("F#5"), Q + S)] + b16("D5", "C5", "D5") +
        # 7
        [(N("G5"), E), (N("G4"), E), (None, E), (N("G5"), E)] +
        b16("G5", "F5", "E5", "D5", "C5", "B4", "A4", "G4") +
        # 8
        [(N("A4"), Q + E), (N("G4"), E), (N("G4"), Q), (None, Q)] +
        # 9
        [(N("G4"), E)] + b16("D5", "C5") + [(N("B4"), E)] + b16("A4", "G4") +
        [(N("A4"), E), (N("D4"), E), (None, E), (N("D4"), E)] +
        # 10
        b16("C5", "B4", "A4", "G4") +
        [(N("F#4"), E), (N("C5"), E), (N("C5"), E), (N("B4"), E),
         (None, E), (N("G4"), E)] +
        # 11
        b16("F5", "E5", "D5", "C5") +
        [(N("B4"), E), (N("F5"), E), (N("F5"), E), (N("E5"), E),
         (None, E), (N("E5"), E)] +
        # 12
        [(N("A4"), E)] + b16("B4", "C5") + [(N("D5"), E)] + b16("E5", "F5") +
        [(N("B4"), E)] + b16("C5", "D5") + [(N("E5"), E)] + b16("F5", "G5") +
        # 13
        [(N("C5"), E)] + b16("D5", "E5") + [(N("F5"), E)] + b16("G5", "A5") +
        [(N("B4"), Q), (None, E)] + b16("B4", "C5") +
        # 14
        b16("D5", "B4", "A4", "G4", "E5", "F5", "G5", "E5") +
        [(N("D5"), E), (N("G4"), E), (None, E)] + b16("B4", "C5") +
        # 15
        b16("D5", "B4", "A4", "G4", "E5", "F5", "G5", "E5") +
        [(N("D5"), E), (N("G4"), E), (None, E)] + b16("D5", "G5") +
        # 16-18  (final C5 held through two whole bars)
        [(N("E5"), E)] + b16("D5", "C5") +
        [(N("B4"), E + S), (N("C5"), S), (N("C5"), H + W + W)])

    C3, G2, B2, A2, F2, Fs2, D2, C2 = (
        N("C3"), N("G2"), N("B2"), N("A2"), N("F2"), N("F#2"), N("D2"), N("C2"))
    left = seq([
        (C3, H), (G2, H),                       # 1
        (G2, H), (C3, H),                       # 2
        (B2, H), (A2, H),                       # 3
        (G2, H), (F2, Q), (Fs2, Q),             # 4
        (G2, W),                                # 5
        (C3, W),                                # 6
        (G2, H), (D2, H),                       # 7
        (D2, H), (G2, H),                       # 8
        (G2, H), (D2, H),                       # 9
        (D2, H), (G2, H),                       # 10
        (G2, H), (C3, H),                       # 11
        (F2, H), (G2, H),                       # 12
        (A2, H), (G2, H),                       # 13
        (G2, W),                                # 14
        (G2, W),                                # 15
        (C2, H), (G2, H),                       # 16
        (C2, W),                                # 17
        (C2, W)])                               # 18
    write_song("vivaldi-largo.mid", 50, (4, 2), right, left)

# ==================================================== 5  Clair de Lune
# Debussy, Suite Bergamasque -- the famous opening, bars 1-9 of the Mutopia
# urtext (E. Fromont 1905), transposed Db -> C major. Right hand keeps the
# top voice exactly (parallel-third dyads, then the single-line descent);
# sustained inner voices are thinned; bar 9 is the urtext's theme-return bar
# used as a tonic close. 9/8: compound meter, eighth = 240 ticks.

def clair_de_lune():
    def ch(*names):
        return tuple(N(x) for x in names)

    right = [
        # bar 1: rest q., high thirds enter
        (2 * E, N("E5"), 4 * E), (2 * E, N("G5"), 4 * E),
        (6 * E, N("C5"), 4 * E), (6 * E, N("E5"), 4 * E),     # ties into b2
        # bar 2 (t=9E)
        (10 * E, N("B4"), E), (10 * E, N("D5"), E),
        (11 * E, N("C5"), E), (11 * E, N("E5"), E),
        (12 * E, N("B4"), 7 * E), (12 * E, N("D5"), 7 * E),   # ties into b3
        # bar 3 (t=18E): triplet-feel long notes (dotted-eighth units = 1.5E)
        (19 * E, N("A4"), E), (19 * E, N("C5"), E),
        (20 * E, N("B4"), E), (20 * E, N("D5"), E),
        (21 * E, N("C5"), int(1.5 * E)),
        (int(22.5 * E), N("E5"), 3 * E),
        (int(25.5 * E), N("C5"), int(2.5 * E)),               # ties into b4
        # bar 4 (t=27E)
        (28 * E, N("G4"), E), (28 * E, N("B4"), E),
        (29 * E, N("A4"), E), (29 * E, N("C5"), E),
        (30 * E, N("G4"), 6 * E), (30 * E, N("B4"), 7 * E),   # B4 into b5
        # bar 5 (t=36E): flowing single line
        (37 * E, N("A4"), E), (38 * E, N("B4"), E), (39 * E, N("A4"), E),
        (40 * E, N("D5"), E), (41 * E, N("A4"), E), (42 * E, N("G4"), E),
        (43 * E, N("A4"), E), (44 * E, N("G4"), 2 * E),
        # bar 6 (t=45E)
        (46 * E, N("F4"), E), (47 * E, N("G4"), E),
        (48 * E, N("F4"), 3 * E), (51 * E, N("E4"), 4 * E),
        # bar 7 (t=54E)
        (55 * E, N("E4"), E), (56 * E, N("F4"), E), (57 * E, N("E4"), E),
        (58 * E, N("A4"), E), (59 * E, N("E4"), E), (60 * E, N("D4"), E),
        (61 * E, N("E4"), E), (62 * E, N("D4"), 2 * E),
        # bar 8 (t=63E)
        (64 * E, N("C4"), E), (65 * E, N("D4"), E),
        (66 * E, N("C4"), 3 * E), (69 * E, N("B3"), 3 * E),
        # bar 9 (t=72E): theme returns over the tonic = the close
        (73 * E, N("E4"), 2 * E), (73 * E, N("G4"), 2 * E),
        (75 * E, N("E5"), 3 * E), (75 * E, N("G5"), 3 * E),
        (78 * E, N("C5"), 3 * E), (78 * E, N("E5"), 3 * E),
    ]
    lh_bars = [
        (0,  [(1, ch("E4", "G4"), 8)]),                        # 1
        (9,  [(0, ch("F4", "Ab4"), 9)]),                       # 2
        (18, [(0, ch("E4", "G4"), 9)]),                        # 3
        (27, [(0, ch("D4", "F4"), 9)]),                        # 4
        (36, [(0, ch("C4", "D4"), 6), (6, ch("B3", "D4"), 3)]),   # 5
        (45, [(0, ch("A3", "C4"), 6), (6, ch("G#3", "B3"), 3)]),  # 6
        (54, [(0, ch("G3", "A3"), 6), (6, ch("F3", "A3"), 3)]),   # 7
        (63, [(0, ch("E3", "G3"), 6), (6, ch("D3", "F3"), 3)]),   # 8
        (72, [(0, ch("C3", "G3"), 9)]),                        # 9
    ]
    left = []
    for bar_start, items in lh_bars:
        for off, chord, dur in items:
            for n in chord:
                left.append(((bar_start + off) * E, n, dur * E))
    write_song("clair-de-lune.mid", 55, (9, 3), right, left)


if __name__ == "__main__":
    import os
    os.chdir(Path(__file__).parent)
    ode_to_joy()
    amazing_grace()
    minuet_in_g()
    vivaldi_largo()
    clair_de_lune()
