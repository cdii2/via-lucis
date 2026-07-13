# Via Lucis — Record MIDI (DESIGN-record.md)

Grilled and **LOCKED 2026-07-13**. Changes to LOCKED decisions require Christian's
explicit sign-off. Adjacent canon: [SPEC.md](SPEC.md) (song pipeline, echo guard, hand
model, CC64), [DESIGN-lightshow.md](DESIGN-lightshow.md) + [SHOW-FORMAT.md](SHOW-FORMAT.md)
(the editor and the `.vls` light show).

## Purpose

You have practiced the downloaded MIDIs to completion; now the *source MIDI* is the
limiter, not your skill. Recording lets you make your own: **hit record, play, and the
device writes your performance to a `.mid` on flash.** Then the editor micro-manages the
notes into your perfected song, and the existing light-show stack authors its show.

Recording adds **no new pipeline** — it is a new MIDI producer at the seam SPEC §"Song
pipeline" already names ("the upload slot is the seam; anything that produces MIDI can
feed it"). Once a take lands in the song list it is an ordinary song: practice, editor,
`.vls` light show, and P4 score-follow all apply unchanged.

## Core model — recording is an armable *capture subsystem*, not merely a mode

Think of a tape head clamped on the incoming MIDI stream. It can be **armed in two
contexts**, and its on-strip visual differs by context so nothing aesthetic ever lands on
the practice latency path:

1. **Free capture** — *no song loaded; the lights follow YOU.* You are in the **Record
   top-mode**; the strip runs the Reactive monitor (keys light as you press) plus a
   discreet recording heartbeat. This is the "quiet room" take.
2. **Play-along capture** — *a song is loaded; the lights follow the SONG.* You stay in
   **Practice / follow-along** — the reference leads you exactly as normal — and the tape
   head records in the background. Practice owns the strip untouched; the "REC" indicator
   lives in the **web UI / phone only**, never on the strip.

The ModeDirector's single-frame-source dispatch is preserved: Free capture *is* the
Record frame source; Play-along capture paints nothing — Practice remains the source.

## LOCKED decisions

### 1. Capture on the device (1a)
The ESP32 owns the BLE-MIDI link, so the ESP32 records. It timestamps the live stream and
writes a real Standard MIDI File to LittleFS; the take drops into the song list like any
upload. You can record with **just the piano and the device — no laptop.** (Rejected:
browser Web-MIDI capture — the browser is not paired to the piano; hybrid WebSocket
streaming — needs the editor open and connected to record at all.)

### 2. Two capture contexts (2b + 7a)
Both **Free capture** and **Play-along capture** ship in v1, per the core model above.
Play-along is "real" in **follow-along / free**; recording in **wait mode** is allowed but
bakes the wait-pauses into the take's timing (the editor's quantize cleans it — see §3).

### 3. Tempo & count-in (3c)
Default is **free capture, no click** — tempo and quantize are set in the editor
afterward (matches "micro-manage into my perfected song"). An **optional count-in +
metronome is available in Free capture only** (Play-along already has the reference song
as its tempo). When the click is on, **the click is a visual pulse on the LED strip** — no
audio path, and it is the signature Via Lucis move.

### 4. Hand split (4a)
Record **both hands in one pass**; auto-split at a configurable split-point (reuse the
existing TrackConfig hand heuristic); fine-tune the crossover exceptions per-note in the
editor. (Overdub — right hand then left as a second take — is a sanctioned fast-follow,
not v1.)

### 5. Captured data (5a)
Note on/off + **velocity** + **CC64 sustain pedal**, timestamped at **1 ms**. The tape
head taps the input stream **after the echo guard**, so any note the device sends the
piano (accompaniment) is already filtered out — a take contains only *your* keypresses,
never the device's. (Full CC / pitch-bend is out of scope for a trainer.)

### 6. Round-trip / ownership (6a)
The **raw take is immutable**: `recording-<n>.mid` is never overwritten. Editing works on
a copy (the `.vlp` project); you export a cleaned `.mid` plus the baked `.vls` light show.
The raw take is a **playable/practiceable song the instant you stop recording** — editor
cleanup is optional, never a gate.

### 7. Record mode & trigger (7a)
Recording arms from the web UI. In the no-song context this enters the **Record top-mode**
beside Practice / Presentation / Reactive / AFK; arming **disarms AFK** like loading a song
does. Capture **starts on your first note** (leading silence trimmed) and **stops from the
web UI**. In the song-loaded context, arming does not change mode — Practice stays the
frame source and the tape head runs alongside (§2).

### 8. Storage (8a)
A **duration cap** (default ~10 min) + the existing per-song size ceiling + a
**free-space check** before arming. A **`recordBudgetKB` settings scalar** (per VL2:
scalars live in `/api/settings`, adjustable without touching resource files) bounds a
take; over-budget or low-space arming gets a **typed refusal** like the rest of the
device. Takes are **auto-named `recording-<n>.mid`** on the device (no keyboard there) and
renamed later in the editor / web UI.

### 9. Live feedback (9a)
The **Reactive monitor + heartbeat is the visual for Free capture only.** During
Play-along the Practice frame already gives feedback and stays untouched; recording shows
a **non-strip indicator** (web-UI "REC" and, at most, one reserved off-key-range pixel
that avoids red / blue / green). This is why 9a never interferes with 2b.

## Technical notes (working decisions, not locked)

- **New core primitive — an SMF *writer*** in `lib/core` (the inverse of
  `midi_parser`), native-testable: an event list → SMF format-1 bytes. Round-trip test:
  `parseMidi(write(events))` reproduces the events.
- **Capture is on the input path, so it obeys the latency rule.** The tape-head append is
  O(1) into a pre-reserved bounded ring buffer (zero steady-state alloc), and it runs
  **after** the wait-mode verdict/light — never between key press and light. A dropped or
  budget-exceeded take fails loudly, it never stalls the frame.
- **Raw-take tempo:** stamped with a nominal 120 BPM and **true millisecond delta-times**,
  so absolute timing is preserved; the editor assigns the musical tempo and quantizes.
- **Wait-mode capture:** elapsed real time includes the wait-pauses, so such a take has
  gaps (editor quantize removes them). Follow-along / Free give a clean performance.
- **Storage:** takes land in the song list on LittleFS; naming `recording-<n>.mid`.
- **REST (per VL2 — documents get resources):** `POST /api/record/arm`,
  `/api/record/stop`, `/api/record/discard`; `recordBudgetKB` appends to `/api/settings`
  (v1 contract test updated, append-only). CORS/fence rules as every other route.
- **The editor gains real MIDI note-editing** (the heavy build item, REC6), on top of
  today's read-only piano-roll. Light-show authoring above it already exists (P-wave).
  - **Build approach (A1b):** keep and extend the home-grown canvas roll for the editing
    UX; **vendor [MidiWriterJS](https://github.com/grimmdude/MidiWriterJS) (MIT), inlined**
    into `editor.html` for the SMF *export* side (writing spec-correct multi-track MIDI by
    hand is the error-prone part). Offline / `file://`-safe, no CDN, zero framework.
    Fallback only if extending the canvas fights us: [g200kg/webaudio-pianoroll](https://github.com/g200kg/webaudio-pianoroll)
    (Apache-2.0, single-file, `dragpoly` note editing) — both licenses clear the
    permissive/zero-copyleft rule.
  - **MVP edit set (A2b, "clean a take into a perfected song"):** move (nudge) · delete
    strays · resize held notes · quantize · set tempo · fix hand-split/split-point.
    *Add-note* and *per-note velocity* are compose features → fast-follow.
  - **Interaction (DAW convention — snap default-ON, modifier suspends it):** snap-to-grid
    is ON by default with a **grid-size selector** (1/4, 1/8, 1/16, triplets) + a snap
    toggle; notes lock to the grid as you drag. Hold **Alt** to drag a note **off**-grid
    for fine placement (Alt because Ctrl/Cmd + Shift are taken by selection). **Selection:**
    **Shift+click** adds/removes a note; **click-drag on empty canvas** = marquee box-select;
    dragging any selected note moves the **whole selection together** (relative timing
    preserved); **Ctrl/Cmd+A** = select all; **Delete/Backspace** = remove selection.
    *(Christian may later invert to free-by-default / hold-to-snap — his editor.)*
  - Export = clean `.mid` (MidiWriterJS) + baked `.vls`.

## Capture timing (D1a locked, D1b as the sanctioned upgrade)

Timestamp captured events at **host arrival** (`millis()` in the note callback) for v1;
rely on the editor's quantize to clean the BLE connection-interval jitter (~7–15 ms).
Native-testable with an injected clock. **Sanctioned hardware-bring-up upgrade (D1b):**
BLE-MIDI packets carry their own 13-bit millisecond timestamp (`& 0x1FFF`) — closer to
true key-strike time. At bring-up, *measure* real jitter on the FP-30X; switch to the
packet timestamp only if it matters (needs a source check of whether Arduino-BLE-MIDI
exposes it to the callback). Do NOT pre-build a smoothing filter — measure first.

## Fast-follows (sanctioned, deliberately NOT v1)

- **Overdub** — record R hand, then L as a second take onto its own track (needs
  track-targeting + punch-in + monitor-the-other-track); design it after base capture is
  felt.
- **Count-in polish** — tap-tempo to set the click BPM (v1 ships a fixed settable-BPM
  1-bar visual pulse only).
- **Compose features** — add-note and per-note velocity editing (v1 edit set is cleanup
  only, A2b).
- **D1b packet-timestamp timing** — the capture-fidelity upgrade above, hardware-gated.

## Build sequencing

The **Record wave** in [../PROGRESS.md](../PROGRESS.md) breaks this into REC1–REC7 with
gates. Order: SMF writer → capture subsystem → ModeDirector integration → persistence +
REST → web UI → editor note-editing → docs, one item per commit, native gate + esp32
budgets every step, closing 8-angle `/code-review` before it lands on main.
