# Via Lucis — Aesthetic & Show Systems (v2 design brief)

**Status:** design-level, grilled 2026-07-09. Build-ready brief for the implementer
(Fable). **No code here on purpose** — this is *what* and *why*, plus the *approach*
notes; the *how* is the builder's. Nothing in this doc changes the LOCKED v1 in
[`SPEC.md`](SPEC.md) **except** the calibration reopening in §5, which is Christian's
explicit call as spec owner.

The through-line: v1 is a *trainer*. v2 adds two more reasons for the lights to exist —
**ambience** (nobody's there) and **performance** (an audience is). The design keeps one
effect engine underneath all of it, sequenced three ways.

---

## 1. Mode model — four top-level modes, gated by "is a song loaded?"

The modes are split by *who the lights serve*, and the gate between them is simply
whether a song is loaded.

| Song loaded? | State | Mode | Lights serve |
| --- | --- | --- | --- |
| No | active input | **Reactive free-play** | you, noodling |
| No | idle ≥ timeout | **AFK Lighting** | an empty room |
| Yes | — | **Practice** *(wait/follow/demo/accompaniment — existing v1)* | your hands |
| Yes | — | **Presentation** | an audience |

Rules:

- **AFK is armed ONLY in the no-song state.** The gate is not "have you been idle" — it
  is "there is **no song** *and* you've been idle." Therefore AFK can **never** interrupt
  a practice session, no matter how long you sit reading music. This is a hard
  requirement, not a preference.
- **AFK entry:** no song loaded + no MIDI + no web-UI activity for the idle timeout
  (default **180 s**; `0` = never). **Any** key press or UI action instantly wakes it;
  since no song is loaded, waking lands you in **Reactive free-play**.
- **Loading a song** disarms AFK for the whole session and puts you in Practice or
  Presentation (an explicit choice — Practice is the default for a loaded song;
  Presentation is entered deliberately).
- **Unloading a song** returns to the no-song state (Reactive, drifting to AFK).

*Approach note:* the no-song state is already the natural boot state (`songName_` empty,
no scheduler, `PlayState::Idle`). The only new primitive is an **`unloadSong`** that
clears the song and returns there. The four modes sit *above* the existing `Mode` enum
(Wait/Follow/Demo/Accompaniment), which becomes the sub-behaviors of *Practice*.

---

## 2. Repeat cue — legibility for fast re-presses (all non-wait modes)

**The problem is dimensionality.** A rhythm game is legible because it spends a whole
spatial axis on *time* (the lane), so you see the queue of upcoming notes and read their
rhythm from the gaps. The LED strip has **no spare axis** — position is spent on *which
key*, brightness on *how soon* for a single note. Brightness can say "soon"; it cannot
stack a queue.

**Therefore the lookahead queue belongs on the falling-notes SCREEN** (the deferred v1.1
flagship — it *is* the rhythm-game lane). The strip stays bounded: the imminent note (the
existing ramp) plus a **repeat cue** for the one thing the strip still gets wrong — a fast
repeated note on one key reads as a single held light instead of two presses.

**The repeat cue:**

- The off-gap before a same-key re-press is filled with an **editable repeat color** that
  ramps in brightness **`startPct → peakPct`** across the gap. The real note then fires at
  100 % in the hand color.
- **The jump-vs-glide behavior is emergent from `peakPct`, not a mode:**
  - `peakPct < 100` → brightness **jumps** cap→100 at onset **+** hue snaps repeat→hand.
    "Now" hits hard (best for fast passages, eyes off the strip).
  - `peakPct = 100` → no brightness change; onset is marked by the **hue snap** alone.
    Smoother, softer downbeat.
- **The onset is never delayed.** When the natural gap is below a visibility floor, the
  fill borrows the missing time **backward from the outgoing note's tail** — the next
  key's light still lands exactly on the beat; only the *visible tail* of the departing
  note is shortened. Sub-floor pathological gaps (e.g. 3 ms apart) correctly collapse to
  one event; no human plays those.
- **Wait mode** has no timing to protect (playback halts), so a same-key re-due fires a
  fixed, comfortably visible pulse.

**Settings panel — "Incoming Re-press":**

| Field | Default | Meaning |
| --- | --- | --- |
| `repeatCueEnabled` | `true` | master toggle |
| `repeatColor` | white | fill/crescendo color; validated ≠ wrong-red (same rule as hand colors) |
| `repeatFillStartPct` | `0` | ramp floor |
| `repeatFillPeakPct` | `45` | ramp ceiling — set **100** for the pure hue-snap glide |
| `repeatFloorMs` | `35` | visibility floor, stolen backward from the outgoing tail |
| `repeatWaitPulseMs` | `60` | fixed wait-mode pulse |

*Approach note:* a new `RepeatFill` layer in the renderer stack —
`None < Ramp < RepeatFill < Due < Wrong`. Value = `lerp(startPct, peakPct, elapsedFrac)`
in `repeatColor`; the `Due` layer overwrites it at onset, so the handoff needs no special
"jump" code. Reuse the existing `previewCap`; do **not** add a second cap variable.
Requires per-key last-onset state. Fill ceiling reuses `previewCap`.

---

## 3. One effect engine, three sequencers

There is **one** effect engine underneath every aesthetic mode; the modes differ only in
how they *sequence* it.

- **Library:** port the classic **MIT-licensed FastLED** effects — Fire2012, Pacifica,
  TwinkleFox, ColorWaves. Plus a shared **palette** system (Rainbow/Ocean/Forest/Lava/Party/custom)
  as the one color primitive most effects already accept.
  - **License discipline:** do **not** lift **WLED** wholesale — it is EUPL-1.2
    (copyleft) and is a whole firmware, not a library. Reimplement any effect *idea* we
    like (rainbows and fire aren't ownable); never copy its code. This matches the
    project's permissive-only, zero-copyleft rule.
  - **Power:** every effect runs under the FastLED global power cap (iron rule — a
    full-white twinkle can out-draw the PSU).
- **Sequencers:**
  - **AFK** = a looping **media-player playlist** of effect-configs: play top→bottom→loop,
    with **shuffle / next / previous / repeat-current**. Each "track" is one effect + its
    settings. Global knobs: idle timeout, brightness cap, master speed, per-effect dwell,
    LED range (whole strip vs only-above-keys). Cross-fade between tracks.
  - **Reactive free-play** = a **single live note-driven layer** (no timeline).
  - **Presentation** = a **timeline** of layered segments (§4).

**Note-driven effects are fully expressive** — the piano's three free expressive signals
all feed the lights, each as a tunable mapping:

| Signal | Feeds | Tunable |
| --- | --- | --- |
| **Velocity** (how hard) | brightness / palette index / ripple size | velocity curve |
| **Release** (note-off) | fade-out tail vs hard cut | release-decay ms |
| **Sustain pedal** (CC64) | hold the glow while pedal down | on/off latch |

*Approach note:* velocity-curve / release-decay / pedal-latch become shared params on the
note-driven effect base, read identically in reactive and presentation. CC64 is already
passed through in demo/accompaniment; it is only *ignored in wait-mode matching*, so
honoring it here is a read, not new plumbing.

---

## 4. Presentation — a piano-roll DAW for light

Presentation is a **timeline sequencer for the strip** — a small "video editor / xLights
for your lights." It is the **heaviest** feature in this brief: it needs an authoring UI
plus a renderer upgrade. Sequence it *after* AFK and Reactive prove out the shared engine.

**Clock: song-position, with a selectable clock source per show.** The show carries a
`clockSource` field:

| Source | Behavior | Use |
| --- | --- | --- |
| **Demo** | device plays the song; one clock owns sound + light | frame-perfect self-playing showpiece |
| **Free-run** | tempo-scaled scheduler; human keeps up | note-bound effects stay locked to the hands; autonomous washes may drift on rubato |
| **Score-follow** | clock **slaved to the performer** — advances on correct notes, tolerant of wrong/extra/skipped | live human performance where the **whole** show, autonomous segments included, breathes with the playing. The flagship, and the hard one |

**Editor** = a browser piano-roll that shows the loaded MIDI over a beat grid; you author
effects against it. Shares its renderer with the falling-notes view (same MIDI-on-a-
timeline data).

**Effect clip** — three independent dimensions:

1. **Time span** — start/end in **beats** (snap to grid, tempo-follows), or looping/
   open-ended.
2. **Key scope** — all keys · a pitch range · a hand/track · a hand-picked note set · **or
   the loaded song's actual notes inside the span** (first-class *note-binding* — "follow
   *these* notes, whatever they are"). Note-binding is the whole point of showing the MIDI
   track in the editor.
3. **Drive** — **autonomous** (runs on the clock — a color wave) or **note-driven** (fires
   as its scoped notes actually sound — §3's expressive effects).

**Composition:** **grouping** bundles clips into a named, reusable unit with its own
opacity; **nesting** lets a group *be* a clip that contains its own timeline (author a
motif once, drop it at every chorus — recursive). Tracks composite top-to-bottom by
**opacity / blend** — a renderer upgrade from today's priority-overwrite layers to real
**alpha/additive** compositing.

**The editor is not the player** — this is what lets the authoring be arbitrarily deep on
a tiny MCU. The **browser editor compiles/flattens** groups + nesting into a compact
**song-position cue stream**; the ESP32 only ever interprets that baked stream. Five
groups deep costs the device nothing.

**Outsource inventory** (all permissive):

| Piece | Outsource | License |
| --- | --- | --- |
| MIDI → notes in the browser | `@tonejs/midi` | MIT |
| Piano-roll / MIDI visualization | Magenta `midi-visualizer` / html-midi-player | Apache-2.0 |
| Editor transport & timing preview | Tone.js | MIT |
| The effects | FastLED classics | MIT |
| Timeline/track editor UI | canvas + a MIT React timeline base | MIT |
| **We own** | note↔effect binding, group/nest, opacity compositor, show-file compiler + device player | — |

The mature light-show sequencers (xLights, Vixen, Falcon Player) are GPL/copyleft desktop
apps, not embeddable — borrow their data-model/UX ideas and optionally *import* their
sequences later, but the editor is ours.

*Approach note:* one position clock, three feeders (device playback / tempo-scaled
scheduler / a wait-mode-derived matcher that advances on match instead of halting).
Score-follow likely reuses `wait_mode`'s existing note-matching machinery.

### 4a. Score-follow clock — VL6, RESOLVED 2026-07-10 (the P4 spec)

Grilled with Christian (batch, 15 decisions; all landed on the recommended
option). This is the **build contract for P4** — Fable builds against it.

**The problem it solves.** A light show needs a *smooth, continuous* clock (so
autonomous washes breathe), but a performer gives *discrete, unpredictable*
evidence (note onsets at unknown real times). Score-follow **estimates** a
continuous song-position from those discrete matches. It reuses wait-mode's
matcher, but where wait-mode *halts* at a barrier, score-follow *releases* the
barrier and *advances the clock* toward the next one.

**Scope (Q1, Q2).** P4 v1 = **step-anchored follow + gentle tempo
extrapolation + free-run fallback** — a genuinely usable flagship, NOT a
research-grade follower. It is a **performance mode: no wrong-note correction**
— wrong notes never red-flash the strip; they simply don't advance the clock.
(Ornament/repeat cleverness and per-note anchor authoring are explicitly a
later wave.)

**The clock model (Q3, Q4, Q5).**
- **Anchors** = each **chord onset of the follow scope** — the exact cadence
  wait-mode already arms barriers on. Reuse that machinery verbatim.
- On a confident match, **snap** the clock to that anchor's song-time, then
  **extrapolate forward** at an estimated tempo until the next anchor, snapping
  again at each match. (Pure step-hold would freeze the washes; a filtered
  continuous estimate is overkill for the MCU.)
- **Tempo = hybrid:** seed from the show's authored tempo, blend toward the
  performer's measured recent inter-anchor tempo, **clamped to a sane band**
  (≈25–300%). The score tempo is always the safe anchor/fallback.

**What is "the score" (Q6, Q7).** A **designated follow track/hand**, chosen in
the editor (default = right-hand/melody), reusing the existing track-mask —
NOT the full MIDI (accompaniment is noise). A chord advances on an **"enough"
match**: its core notes land within a generous timing window; extra/missing
inner voices are tolerated (reuse per-key chord clearing). Never a bare
single-note trigger (ornaments would false-advance).

**Robustness (Q8–Q12).**
- **Wrong / extra notes → ignored for the clock.** Only forward evidence moves
  it; a fumble never makes the lights lurch.
- **Performer ahead → bounded look-ahead:** match against the next few anchors
  within a window, snap to the furthest confident match. (Not unbounded — a
  coincidental far-away pitch must not teleport the clock.)
- **Performer goes back → forward-only in v1:** a large backward run triggers a
  **resync/re-arm** at the new position (treated like a seek), not a smooth
  reverse clock. (Backward seek already hard-resets effects, so smooth reverse
  would look jarring — deferred.)
- **Rests / fermatas → coast then hold:** the clock keeps moving at the last
  tempo up to the next scored onset, then holds; washes keep breathing but the
  clock never runs past unplayed music.
- **Lost / low confidence → fall back to free-run** at the last known tempo and
  **auto-re-acquire** when confident matches resume. The show never freezes or
  dies; it silently snaps back when the performer is found again. **This is the
  safety net that makes the flagship feel robust even when following is
  imperfect.**

**Lifecycle & authoring (Q13, Q14).** The **first matched anchor starts the
clock** (show pre-rolled at position 0; an authored intro segment covers
"autonomous wash before the first note"). No count-in, no downbeat ceremony.
Editor adds exactly **one control: a "follow track" selector** + auto-extracts
anchors from that track — **no per-note anchor editing in v1**.

**Format impact (Q15).** `clockSource = 2` (already reserved + typed-refused in
v1 firmware) **plus one optional META field: the follow-track index.** All
matching params (timing windows, tempo band, look-ahead depth) stay **firmware
constants**, tunable later — **no new TLV section for v1.** The reserved value
+ the unknown-section-skip rule already cover a richer per-show follow config
(windows/weights/tolerances) as a clean later extension with no migration.

**How it hangs together:** reuse the barrier matcher → snap-and-extrapolate
with a clamped hybrid tempo → forgiving, forward-biased robustness that never
lurches → free-run safety net whenever confidence drops. One editor selector
and one format field are the entire authoring/format surface.

**Impl notes for the builder (design-level, not prescriptive):** the natural
seam is a `ScoreFollower` in core that wraps a `WaitMode`-style matcher over
the follow-track mask and, instead of holding the scheduler at the barrier,
drives `Scheduler::seek`/tempo to the estimated position each tick; the
follower is pure and native-testable (feed it a scripted onset stream, assert
the emitted clock trajectory); the director selects it as the Presentation
feeder when `clockSource==2`; the effect backward-seek reset already handles
the re-arm case. Tune the windows/band/look-ahead as native-test constants.

**Explicitly still open after VL6 (later waves, NOT P4 v1):** smooth backward
tracking; ornament/trill/repeat modelling; per-note anchor weighting + per-
section tolerance authoring; a confidence-metric-driven pause. The format
reserves room for all of them.

---

## 5. Calibration — per-key geometry for *any* keyboard

**Reopens the LOCKED "exactly two settings" decision** (`offsetMm`, `ledsPerMeter`) —
Christian's own call as spec owner. The two numbers survive as the simplest tier's math,
so nothing already built is wasted.

**Why reopen:** the current mapping hardcodes standard piano proportions (164.5 mm/octave,
fixed C–E / F–B splits) and shift+scale can only express a linear map. It fits a
standard-proportioned piano (and, by abusing `ledsPerMeter` to encode scale, a uniformly
larger/smaller one) — but it **cannot** fit a keyboard with different proportions
(different black/white ratio, irregular keys). For the open-source "buy two parts and
replicate" promise, and for the note-bound presentation effects that need to know each
key's LEDs, we need a general solution.

**The primitive** — use the only two senses the device has (MIDI + your eyes):

> **The device lights one dot → you press the key directly beneath it → it records
> (LED ↔ note).**

**Three tiers, all filling one per-key LED table** (the renderer only ever reads the
table, so nothing downstream cares which tier produced it):

| Tier | You do | Solves |
| --- | --- | --- |
| **2-point** | light a dot near each end; press the key under each | any **uniform** keyboard, any size (measured, not calculated) |
| **Multi-point** | same, at several landmarks (e.g. every C) | **irregular / non-standard-proportion** keyboards — piecewise-linear interpolation between landmarks |
| **Per-key** | wizard fills it; hand-edit any key | arbitrary layouts, foolproof |

**Hardware precondition (build-guide, not firmware):** software cannot invent LEDs. The
strip must physically **span and out-resolve** the keyboard — at least ~2–3 LEDs per key
for a crisp placement (180/m gives ~4 on a standard 23.5 mm key; wide keys thin this out).
**The BOM scales with key size** — wider keys need a longer/denser strip. Calibration
solves the *mapping*; the *hardware* must be adequate first.

*Approach note:* everything reads one per-key LED table; the three wizards are just
different ways to fill it. `offsetMm`/`ledsPerMeter` become the 2-point tier's math.

---

## Open — not yet designed (future sessions)

These were surfaced but deliberately left for later design passes, not build:

- **Falling-notes screen view itself** — leaned on heavily (it carries the lookahead
  queue) but never designed. Cousin of the presentation piano-roll (shared renderer).
- **Compositor blend-mode set** — which blends beyond opacity (add / max / overlay).
- **Show storage / export / share format** — saving shows to flash; a community-shareable
  format (fits the open-source ethos); possible xLights sequence import.
- **AFK starter catalog + crossfades** — the initial curated effect set and transition
  polish.
- ~~**Score-following robustness**~~ — **DESIGNED 2026-07-10 (VL6, §4a) as the P4
  spec.** The v1 slice (step-anchored follow + hybrid tempo + free-run fallback,
  performance mode) is fully specified. Deferred to later waves: smooth backward
  tracking, ornament/trill/repeat modelling, per-note anchor authoring, a
  confidence-driven pause.
- **BOM scaling specifics** — concrete strip length/density guidance per keyboard size for
  the build guide.
