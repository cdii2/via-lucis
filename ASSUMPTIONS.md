# ASSUMPTIONS

Autonomous decisions made without asking, one per line, newest on top. Format:
`A<n> (date, iter): decision — rationale.`

- A56 (2026-07-10, arch-review run): **CHRISTIAN'S RULING, not an assumption —
  demo_reel and Pride2015 are removed PERMANENTLY (commit f160183); never
  re-add them.** The effect set is now exactly: fire2012, pacifica,
  twinklefox, colorwaves (AFK/registry, fx::effectNames()) plus notedriven
  (stream-only, effectNameValid() in show.cpp). Everything that names
  effects — editor EFFECTS/FIRMWARE_STREAM_EFFECTS, webui AFK_EFFECTS,
  tools/mock_device.py, docs/API.md, the .vls golden corpus — builds on
  this five-name set.
- A55 (2026-07-10, P4 build): ScoreFollower decisions beyond §4a's letter,
  all as named test-tunable constants per Q15 — "enough" chord match =
  majority (n≤2 needs all, else n/2+1: inner voices tolerated, a bare single
  note never advances a chord); chord gather window 600ms (a staler partial
  restarts with the new note); look-ahead = 4 anchors AND ≤8s of song time;
  lost = 4 unmatched notes while Following (silence is NOT lost — silence
  holds, that's a fermata); hybrid tempo = EMA α=0.35 toward the measured
  tempo, measured ONLY across consecutive-anchor snaps (skips/re-arms
  measure omission, not tempo), clamped 0.25–3.0 before and after blending;
  re-acquire/re-arm evidence = the newest unmatched notes (ring of 8, ≤4s
  old) confidently walking ≥3 CONSECUTIVE anchors anywhere in the score,
  re-arming at the walk's end (covers backward runs AND far skips; ties go
  to the anchor nearest the current position); re-acquire also works from
  AwaitingFirst (starting mid-piece starts the clock there); one key press
  credits only the NEAREST window candidate that needs it (a repeated
  melody note advances one anchor per press, never several); past the FINAL
  anchor the clock runs free at the last tempo (the show tail plays out);
  in FreeRun the window stays at the lost anchor (the performer usually
  continues from where they fumbled) while the run-scan covers everywhere
  else. Wiring: clock-2 shows run follow sub-mode with the transport
  STOPPED — the follower is the only clock (engine.driveShowClock refused
  while Playing), so the practice verdict path is inert by construction
  (no red flash possible) and /api/status reads "idle" during a
  score-follow show (topMode says presentation; accepted cosmetic).
- A54 (2026-07-10, P4 build): META followTrack=0xFF (auto) resolution — the
  device picks the follow scope at PLAY time via engine.followTrackMask():
  the right-hand practiced mask (tracks assigned Right or Both — the §4a Q6
  "melody/right hand" default under the existing track heuristics) when it
  has onsets, else the lights mask, else all tracks; an explicit index that
  is out of the track space or has no onsets falls back to the same auto
  chain (a show authored against a different arrangement still follows
  SOMETHING rather than dying). The optional trailing META byte is parsed
  tolerantly (stored for any clockSource; the editor only emits it for 2);
  absent ⇒ 0xFF. The clock-2 typed refusal ("score-follow not supported
  yet") is deleted rather than kept for later waves — later-wave follow
  features arrive as new TLV sections, which v1 skips by the compatibility
  rule, so there is nothing left to refuse.
- A53 (2026-07-10, VL6 score-follow grill): P4's design is RESOLVED with
  Christian (batch, 15 decisions, all on the recommended option) — full spec in
  docs/DESIGN-lightshow.md §4a. Load-bearing shape: reuse wait-mode's barrier
  matcher over a designated FOLLOW TRACK (editor-selected, default melody/right
  hand); on a confident chord match SNAP the clock to that anchor then
  EXTRAPOLATE forward at a HYBRID tempo (score-seeded, blended toward the
  performer's measured tempo, clamped ≈25–300%); performance mode (NO wrong-note
  red-flash). Robustness = forgiving + forward-biased + never-lurch: wrong/extra
  notes ignored for the clock; performer-ahead = bounded look-ahead snap;
  performer-back = forward-only re-arm (no smooth reverse in v1); rests = coast-
  then-hold (washes keep breathing); lost/low-confidence = FALL BACK TO FREE-RUN
  and auto-re-acquire (the safety net that makes it feel robust). First matched
  anchor starts the clock (no count-in). Format: clockSource=2 (already reserved
  + typed-refused) + one optional META follow-track-index field; matching params
  stay firmware constants (no new TLV section — the reserved value + unknown-
  section-skip rule cover a richer per-show config later with no migration).
  Deferred to later waves (NOT P4 v1): smooth backward tracking, ornament/trill/
  repeat modelling, per-note anchor authoring, confidence-driven pause. Design
  only — no code this session ([[design-sessions-defer-code-to-fable]]); P4
  builds against §4a next.
- A52 (2026-07-10, P-wave closing review): 8 angles, 4 paired finder agents.
  APPLIED — show.cpp: Reader::need 32-bit-wrap guard, parseCues count×16 ≤
  remaining check BEFORE reserve (count-DoS), META name >48 ⇒ BadSection.
  show_player REWORKED for bounded per-frame work: per-cue 88-key scope
  bitmaps (O(88) masks, not O(binds) rebuilds), time-sorted BindEvent lists +
  per-cue cursors (replaces the O(cues) pointer-identity self-lookup),
  buildMask/driveNoteBinds keyed by cue INDEX, backward seek rewinds cursors,
  forNoteOrdered() normalizes lo/hi once. mode_director: startShow owns the
  WHOLE start policy (clock→sub-mode, loop clear, stop+play) and
  setPresentation(false) clears showPlaying_ (state-desync — a hidden playing
  show would 409 every upload). app: lastMode_/lastPractice_ snapshot +
  stopShowLocked() shared teardown (no-ops on stray stop, never halts live
  practice, restores pre-show mode). web_server: CORS layer + send204() for
  both DELETE routes (a bare 204 with no Allow-Origin fails the browser fetch)
  + BodyIntake.cleanup discards a truncated .vls on mid-upload disconnect.
  editor.html: note-set count clamped to 255 (u8 wire count) + CSV validated
  to 0..127, compile() z-order tiebreak by descending lane (lower lane renders
  on top per SHOW-FORMAT §1 — this SETTLES the "later design call" P3 flagged),
  group-instance offset min=0 + onset clamp (negatives wrapped via >>>0).
  tools/vls_dump.py written (binary→JSON twin, stdlib, byte-identical to the
  editor's jsonTwin — verified by round-trip). DEFERRED with reasons —
  upload-quota TOCTOU across parallel uploads (single-user device, ~960KB
  partition headroom); song-upload lacking the show path's busy-guard (v1
  contract: song uploads during playback were always allowed); web_server
  upload-handler parameterization + song_store binary-helper dedupe (2 call
  sites; revisit at a third); ShowPlay device-enum lacking message() (two
  "busy" literals, cosmetic); ShowCue in-RAM ~2.4× wire (typical shows small,
  64KB cap bounds it). P4 score-follow NOT built (gated on VL6). One leftover
  compile error from the half-applied batch fixed (buildMask(cue)→buildMask(ci)).
  285 → 287 native tests; esp32 flash 48.0%, RAM 22.0%.
- A51 (2026-07-10, P1+P2): the show core's spec resolutions are recorded in
  show.h/show_player.h headers (TooLarge kind added; stable-sort preserves
  stream z-order for equal starts; unknown scopeType = BadCue since its
  payload size is unknowable; END optional; section-order-independent
  validation; bound-note velocity = 127 constant; note-off = min(onset+
  300ms, next same-key onset) precomputed at load). P2 wiring: the clock
  IS the Scheduler (Demo → sub-mode demo, Free-run → follow; tempo changes
  keep position continuous — named test); director owns showPlayer_ +
  showPlaying_ and the show dies with the song (same lifetime rule as
  presentation_); App::playShow reads+parses UNFENCED then swaps under the
  fence, refuses to hijack a live practice session (409 busy), and starts
  from the top (stop+play re-baseline). Uploads: stream-sink like songs,
  quota checks on the FIRST chunk (16 shows / 384KB / 64KB per — 507/413),
  409 while anything renders live. GET /api/shows carries formatVersion 1
  and name/size only (per-item meta would parse every file on list —
  SHOW-FORMAT updated). Webui playback surface deliberately deferred: the
  editor drives /api/shows; the webui keeps only the Presentation toggle.
  285 native tests.
- A50 (2026-07-10, E-wave closing review): 8 angles, 4 paired agents, 18
  findings → 14 fixed, 4 accepted/deferred with reasons. The two that
  mattered most: (1) PUT /api/afk built every track's effect UNDER the F1
  fence (heap work while ble_.poll waits — the aesthetic layer stalling the
  sacred path); now AfkPlayer::prepare() allocates unfenced and apply()
  swaps pointers, AND a dial-only config change keeps the show's position
  (same-track-list detection) instead of restarting it. (2) The E2 wiring
  ran std::pow per keypress BEFORE practice saw the note; the velocity
  curve is now a 128-entry LUT built in setParams, and practice hears the
  key first (probe → engine → reactive). Also: tracks capped at 16 (heap
  bound the static-RAM gate can't see); dwellSec clamped ≤86400 and
  crossfadeMs tied to ≤dwell/2 (a fade longer than the dwell crossfaded
  forever); crossfade off-by-one (>= not >, one wasted double-render frame
  per fade); AFK time math went 64-bit integer with speed in 1/16ths (float
  ms lost precision after ~4.7h unattended; the ~49-day uint32 wrap is
  documented-harmless); ONE definition each for CRGB += / |= (fx_color
  addRgb/maxRgb) and for pick-next-track; the bring-up rainbow test pattern
  now uses the same hsv2rgbRainbow as every effect (an eyeball check must
  match what effects show); shared fxtest::litCount/maxChan helper; a
  director-level test proves a CONFIGURED playlist (not the fallback)
  renders; effect-name list mirrored in 4 places got SYNC NOTES (a runtime
  discovery endpoint was deliberately not added). Deferred with reasons:
  factory move out of afk_player.h + shared cap/mask for Presentation
  (P-wave decides the shared shape); NoteDriven velocity→palette mapping
  seam (rawVel now retained, so the seam is open). 272 native tests.
- A49 (2026-07-10, E1+E3): effect provenance — every port from the VENDORED
  FastLED 3.10.3 tree (identical to the repo tag; the pinned commit is the
  3.10.3 release), examples/ for the algorithms; the small math/color kernel
  (lib8tion subset, hsv2rgb_rainbow, HeatColor, ColorFromPalette) ported
  from src/ of the SAME MIT release — OV3's target was gists/forums, not
  the library's own source; every file's header names its exact source
  paths. beat*/beatsin* take explicit ms (VL7 determinism). ColorWaves is
  not in examples/ ⇒ shipped as a palette-driven derivation of the
  Pride2015 example's wave math (idea reimplementation, no gist code).
  TwinkleFox's palette rotation dropped in favor of the ONE setPalette
  primitive.
  E3 player: dwell counts fixed frames (render cadence ≈ wall time only
  while AFK is actually rendering — dwell pauses with the mode, which is
  the right behavior for an ambient show); crossfade renders the incoming
  effect into a second buffer and nblends 0→255 (continuity test measures
  intrinsic effect motion first and bounds the fade against it — a sparky
  fire frame legitimately out-jumps any fade); brightness cap applied
  player-side BEFORE the global FastLED power cap; aboveKeysOnly masks to
  the table's key span; unknown effect/palette names are typed 400s at the
  REST boundary and render the fallback if they somehow land; empty
  playlist = gentle rainbow (VL5's stub promoted to the documented
  fallback). Reactive E2 layer + AFK player both copy the table (VL1 read
  surface). 271 native tests.
- A48 (2026-07-10, E2): NoteDriven = the expressive base (velocity gamma
  0.25–4, releaseMs decay stepped per fixed frame, CC64 latch; pedal-up
  releases every latched glow). Pitch maps linearly across the shared
  palette (low A0 = index 0, C8 = 255); geometry = the per-key table (VL1),
  copied in like FrameRenderer does. The director feeds the layer EVERY key
  edge (state stays warm so entering Reactive mid-hold shows the truth) but
  it paints only in Reactive; no-song key events mark the frame clock dirty
  so the glow lands within a frame. Device: BleMidiIo grew onPedal (CC64
  edge ≥64 = down) and App registers note-off + pedal → director (all fire
  inside ble_.poll under the tick fence). Tunables stay core-side defaults
  for now — no settings/REST growth was sanctioned for E2; exposure can ride
  a later /api/reactive if wanted (open, noted). Endpoint tests pin against
  PALETTE truth (rainbow entries aren't 255-max — a ≥250 assertion was the
  bug, not the code). 8 NoteDriven tests + a director glow/decay test.
- A47 (2026-07-10, M-wave closing review): 8 angles, 4 paired agents, 15
  findings → 11 fixed, 4 resolved-by-decision. Fixed: F3 auto-pause moved
  INTO ModeDirector::setTestPattern (every caller keeps the no-burst
  guarantee; director test pins it + note-off flush); stale presentation_
  now dies in director tick whenever the song is gone (any unload path, one
  owner — App's paired call removed; reload-lands-in-Practice test);
  topMode() gained idleSec's clock guard (out-of-order timestamp reads as
  zero idle, never wraps to instant-AFK); unloadSong clears EchoGuard
  credits (new clearCredits — stale ignore-credits must not leak across
  sessions); dead code removed (FrameRenderer::addDot + Layer::Forced,
  LedOutput::allOff — forced sources render in the director now); shared
  kDefaultAfkTimeoutSec (director + Settings read ONE constant); mock
  loop_off() helper + probe-capture counts as activity; stale zero-new-work
  comment rewritten as the actual per-key cost budget. Decisions: topMode/
  idleSec/afkTimeoutSec ship on EVERY status reply (A46 amended — the webui
  reads them off POST replies; only wifi stays GET-status-exclusive);
  play-cancels-probe happening one loop-tick after the transport call is
  ACCEPTED (bounded staleness ≪ the wizard's 500ms poll; the tick rule
  covers every path into Playing, which the old synchronous call did not).
  188 → 190 native tests.
- A46 (2026-07-10, M3; amended by the M-wave closing review): top-mode
  surface — status JSON grows topMode/idleSec/afkTimeoutSec (before wifi;
  wifi stays the LAST key per R4). TopStatus is an optional ENGINE param,
  but App passes it on EVERY status reply (all routes) — deliberately: the
  webui reads s.topMode straight off POST /api/topmode and unload replies.
  Only wifi remains GET-/api/status-exclusive. The
  idle timeout is a SETTINGS scalar per VL2 (`afkTimeoutSec`, default 180,
  0=never, clamped to a day) — the settings key set grew (contract test
  updated). Mode entry = POST /api/topmode {"mode":"presentation"|"practice"}
  — only the Practice⇄Presentation edge is settable; Reactive/AFK follow from
  song state + activity. Webui: state badge reads "ambient"/"show" for
  afk/presentation, a Presentation toggle appears with a loaded song, and
  Settings gained an "Ambient lights" card. Mock server mirrors the whole
  surface (idle-derived topMode; writes reset its idle clock). 187 → 188
  native tests.
- A45 (2026-07-10, M2): ModeDirector = core class holding a PlaybackEngine&
  (App owns both; declared after engine_). Song-loaded truth is READ LIVE
  from engine_.songLoaded() — no mirrored flag. The probe moved from the
  engine to the director (3A, closing the A38 interim); the engine gained
  ONE public hook, markFrameDirty(), so director events share the one frame
  clock (frameDue stays engine-owned). Play-cancels-probe became a
  director-tick rule — holds for ANY path into Playing, not one REST route.
  Test pattern + rainbow render CORE-SIDE now (plain hsv spectrum — not
  FastLED's exact hue table; visually equivalent, natively testable);
  LedOutput is pure output. AFK stub = the rainbow producer (VL5); Reactive
  and Presentation render dark until E2/P1 fill them. Activity: BleMidiIo
  grew a device-only onActivity hook (note-on/off + CC → onMidiActivity);
  every fenced write REST entry calls touchWriteActivity() FIRST inside the
  fence (u64 store is lock-protected on the 32-bit MCU); GETs structurally
  can't touch the clock (no director call exists on those paths). Idle
  baseline = first tick. Unload clears presentation (App wiring) and the
  route's own write-activity restarts the drift. 7 engine probe tests moved
  into test_mode_director + the 7-case gate matrix. 180 → 187 native tests.
- A44 (2026-07-10, Q-wave closing review): 8 angles in 4 paired finder agents
  over 227c1da..HEAD. Fixes: fillStart clamps to 0 when the visibility floor
  exceeds the onset's own timestamp (uint64 wrap silently killed early
  windows — settings allow 1s floors); the wrongColor↔repeatColor collision
  guard now cuts BOTH ways (editing wrongColor onto the cue color reverts);
  configure() diffs the repeat config and skips setRepeatCue when unchanged
  (an unrelated brightness PUT no longer rebuilds windows under the F1 fence
  or kills a live pulse); WaitMode owns the chord-lifecycle edge — update()
  returns new-chord-loaded and exposes reDueKeys(), engine mirrors
  (lastChordBarrierUs_/prevChordKeys_) DELETED (the exact mirror-state bug
  class A34 eliminated for loops); scaleRgb is THE shared color scale in
  frame_renderer.h (engine copy deleted); resyncRepeatCursors uses
  std::upper_bound; renderFrame early-outs the whole per-key scan when the
  song has zero repeat windows (repeatWindowCount_); duplicate
  resetWaitPulse in rebuildAfterLoad removed. Deferred with reasons: full
  RepeatCue class extraction (M2's ModeDirector restructures dispatch
  anyway — re-evaluate then; noted in the M2 charter's context). Tempo
  alignment self-checked: windows are SONG-time, positionUs is song-time —
  aligned at any tempo. 177 → 180 native tests.
- A43 (2026-07-10, M1): unloadSong resets song/scheduler/wait/loop/lights/
  emitter (emit mask forced 0 — applyMasks can't run without a scheduler) +
  repeat-cue windows and chord history; settings/calibration/BLE/idle clock
  untouched per the OV4 reset surface. Route = POST /api/songs/unload,
  registered BEFORE the /api/songs regex routes so "unload" can never parse
  as a song name. Engine grew songLoaded() (the M2 gate predicate). Webui:
  eject button in the song line, visible only with a song loaded. Post-unload
  REST behaves exactly like never-loaded (transport/tempo return false — the
  v1 contract for no-song). 176 → 177 native tests.
- A42 (2026-07-10, C-wave closing review): 8 finder angles (3 correctness +
  reuse/simplification/efficiency/altitude/conventions) + Codex-informed
  verify; 13 findings applied in one commit (935b09c), 4 accepted-as-is with
  reasons. THE structural ruling: PlaybackEngine::configure() never touches
  geometry — setTable is the ONE table writer (three angles converged on the
  configure-then-setTable ordering hazard; a dropped setTable would have
  silently reverted wizard calibration with no failing test). Behavior
  ruling: an EDIT of offsetMm/ledsPerMeter via PUT /api/settings reverts
  geometry to the 2-point tier (web_server diffs old vs new scalars →
  applySettings(scalarsChanged)) — matches the TROUBLESHOOTING promise that
  the dials win; untouched scalars never clobber wizard tables. Fallback
  caveat documented, not re-architected: a corrupt /calibration.json loses
  the reversed flag (it lives only there); recovery = re-run the wizard
  (direction re-detected from two presses). Accepted-as-is: onJsonBody's
  POST|PUT dual registration (pre-existing v1 pattern on every route),
  Settings↔Calibration scalar mirror (charter-deliberate), KeyLedTable::set
  silent range guard (all callers validate first). Process note: Q2+Q3
  landed as one commit (entangled configure() wiring) — deviation from
  one-item-per-commit, recorded here.
- A41 (2026-07-10, Q2+Q3): wait-mode re-due pulse = chord-history compare (new
  barrier's pending keys ∩ previous chord's keys pulse repeatColor for a fixed
  wall-clock width, then settle into the due light; mode/mask changes, seeks,
  wraps and loads reset the history — no stale pulses). Q3 settings: the six
  Incoming Re-press fields APPENDED to the settings contract (key-set test
  updated; percents 0–100 on the wire → 0..1 fractions in RepeatCueConfig via
  configure()); repeatColor == wrongColor is REJECTED server-side by keeping
  the field's previous value (tolerant-partial style — the body still 200s,
  matching fromJson's clamp semantics) plus the same client-side toast rule
  hand colors follow; floor/pulse clamped 0–1000ms. Webui: "Incoming
  re-press" card (toggle, color, start/peak sliders, fine-timing details).
  176 native tests.
- A40 (2026-07-10, Q1): repeat-cue mechanics — per-key windows precomputed at
  load (7A): fillStart = onset − max(gap, floor), so tail-borrowing falls out
  of the formula; windows sorted per key, render advances an 88-entry cursor
  array lazily (O(1) amortized, zero allocation, no event scans on the frame
  path); seeks/wraps re-derive cursors by binary search off the frame path.
  Off-gaps under 10ms get NO window (the brief's 3ms pathology): sub-
  perception gaps are MIDI artifacts and cueing them would flicker. A key
  whose fill window has opened suppresses its own sounding note's Due paint
  (that IS the borrowed tail — layer order still lets the onset's Due
  overwrite the fill, so jump-vs-glide stays emergent per VL4). Cross-track
  same-key overlap (key never goes dark) ⇒ no cue. The crescendo is gated
  OFF in Wait mode (brief §2 heading: all non-wait modes; Q2's fixed pulse
  owns the halted case). Fill paints only when the INCOMING note's track is
  lights-masked in (checked at render — masks are runtime state). Post-seek
  landing inside a real gap shows the fill immediately — that's position-
  derived truth, not phantom state (the charter's no-phantom test pins the
  stale-cursor case). floorMs is baked into windows; setRepeatCue rebuilds
  them. RepeatCueConfig holds 0..1 fractions; the JSON fields (Q3) carry the
  brief's 0–100 percents. 163 → 173 native tests.
- A39 (2026-07-10, C4): wizard shipped per the hardened /plan-design-review
  spec (autonomous run + Codex outside voice, 18 findings; full pins recorded
  in PROGRESS C4). Key build decisions: the wizard NEVER PUTs twoPoint — its
  two-dot coarse step is a 2-landmark multiPoint PUT (measured beats derived;
  reversal falls out of the data); the twoPoint tier remains the manual-
  scalars path under "Advanced manual calibration". Commit model = snapshot
  GET on open, Cancel PUTs it back (the GET shape is PUT-able for every tier
  — deliberate C3 API property). Coarse dots at 12%/88% of the strip; verify
  key = white key nearest the captured-span midpoint, alternating 25%/75%
  probes on "check another key"; a verify MISS becomes the next landmark at
  the lit LED (the user's press is the measurement). Probe-arm POSTs use raw
  fetch so 409 routes to the preflight gate by status code. UI auto-re-arms
  the device's 30s probe timeout silently ("Still waiting…") — no dead ends;
  body scroll locks while the overlay is open. Mock server grew the full
  calibration surface + a simulated key-presser (1.2s delay, geometry-derived
  note, no-key zone at strip start for the move-dot path, REVERSED switch);
  wizard-abort mid-flow exercised against it via the browse daemon (snapshot
  restore + probe cleanup + overlay close all verified). gz 10.9→15.6KB,
  flash 46.3%/RAM 20.4%. Tests unchanged (webui has no native suite; the
  wizard's device contract is pinned by C3's tests + the API.md doc).
- A38 (2026-07-10, C3): calibration wiring — the document (tier + reversed +
  tier inputs + the finished table) lives in core `Calibration` with typed
  CalibResult (BadJson/BadTier/MissingField/BadTable{TableError,badKey}) whose
  message() strings are the 400 bodies; stored as raw JSON at
  /calibration.json; ANY unreadable/invalid stored doc falls back to
  fromSettings (v1-identical) at boot, not just an absent file. The probe
  state machine lives in core CalibrationProbe HOSTED BY PlaybackEngine until
  M2's ModeDirector takes the forced-source slot (chosen so every charter
  rule — refusal while Playing, capture-before-wait-mode, timeout, cancel,
  forced dot — is natively tested); onKeyDown gains ONE bool check when idle
  (nothing new allocates/blocks on the latency path). Probe details: timeout
  default 30s clamped 1–300s; transport("play") while armed cancels the probe
  (user intent wins; wizard sees note:null and retries); armed probe outranks
  an active test pattern, which resumes when the probe clears; dot = pure
  white at Layer::Forced (new top layer). PUT /api/calibration on tier
  twoPoint writes offsetMm/ledsPerMeter through to settings (they stay the
  2-point tier's inputs); PUT /api/settings on tier twoPoint rebuilds the
  table from the new scalars (preserving reversed) — other tiers ignore the
  scalars entirely. perKey PUTs: unlisted keys are valid=false (dark); the
  body's ledCount is ignored (the device knows its strip). Calibration route
  body cap 12KB (88-key doc ≈ 3.5KB sails too close to the generic 4KB).
  onJsonBody grew an optional per-route cap param. 144 → 163 native tests.
- A37 (2026-07-10, C2): multi-point builder maps in mm-space — piecewise-linear
  (note→keyCenterMm, led) knots with the standard cluster model as the shape
  prior, end segments extrapolating; key slot edges (± the shared 1mm margin,
  now kKeyEdgeMarginMm in key_led_map.h) map through the knots so slot widths
  scale with local density. Uniformly-stretched keyboards come out exact;
  genuinely irregular ones are what the per-key tier is for. Strip direction:
  a strictly-descending landmark LED sequence IS a reversed mount (built
  canonical, then mirrored — no separate flag needed for the measured tier);
  fromTwoPoint grows a `reversed` default-false param (mirror after the
  v1-identical build) since its two scalars can't express direction. Rounding
  collisions between adjacent keys shrink upward (first = prevLast+1); a key
  emptied by the shrink goes valid=false — on a too-coarse strip one dark key
  beats two wrongly-lit ones. fromTwoPoint deliberately gets NO shrink sweep
  (byte-identity with v1 outranks the invariant; v1's own coarse fallback can
  collide). validate() = the REST-boundary invariant check (typed TableError +
  offending key for C3's 400s): in-strip, non-inverted, ONE consistent
  direction, adjacent valid keys never overlap; gaps of invalid keys fine.
  132 → 144 native tests.
- A36 (2026-07-10, C1): KeyLedTable = a class holding std::array<LedRange,88>
  + a ledCount, copied by value into FrameRenderer (≈530 B — same
  copy-of-config style the renderer already used for LedMapConfig; no dangling
  refs, no indirection on the frame path). Default-constructed table = zero
  LEDs / every key invalid (real tables come from builders); PlaybackEngine's
  member initializer builds TableBuilder::fromTwoPoint(LedMapConfig{}) so the
  pre-configure() default behavior is byte-identical to v1. TableBuilder is a
  namespace (fromTwoPoint today; C2 adds the rest). ledsForNote stays public
  as the builder's math and the tests' v1 oracle — production render code no
  longer calls it (grep-verified: FrameRenderer::paint was the only site).
  Characterization = integer equality (OV4): 88 ranges over 200 fuzzed
  configs + degenerate configs (0 density / 0 LEDs / far offset) + LED-level
  frame comparison over 20 fuzzed configs. 126 → 132 native tests.
- A35 (2026-07-07, F3): test-pattern clock — App::setTestPattern auto-pauses
  playback when (and only when) engine_.state()==Playing at activation: build a
  local std::vector<MidiOutMsg>, engine_.transport("pause",0,out), sendAll(out).
  Chosen with zero new engine surface. Guarded on Playing so a pattern from
  Idle/Finished doesn't disturb state (transport("pause") would flip
  Finished→Idle). Pattern "off" does NOT auto-resume — the user presses play,
  and the existing play path (lastTickUs_=0 → next tick re-baselines) makes the
  pattern-off fast-forward burst structurally impossible; the pattern branch in
  tick() early-returns while active, so an unpaused clock would otherwise run
  the whole skipped span in one advance() on pattern-off. Bonus: pause sends
  note-offs, so demo/accompaniment notes no longer ring unstopped through a
  pattern. setTestPattern already holds the F1 fence, so the pattern activation
  and the pause it triggers are ONE atomic unit. Status visibility (amended,
  F-wave review): the auto-pause manifests in /api/status as state
  playing|waiting→idle at a frozen position — that is PART of the sanctioned F3
  behavior delta, not passive-reporting drift. Native engine test (red not
  applicable — the engine already re-baselines; the fix is device wiring gated
  by the esp32dev build) pins the continuity contract the fix relies on:
  Playing at P → pause → 60s wall-clock gap with no ticks → play → tick re-
  baselines (position stays P, delta 0) → next tick advances ~100ms (scaled by
  tempo), NOT the 60s gap. Docs checked (BRINGUP/TROUBLESHOOTING/BUILD-GUIDE):
  the strip-test step is a bare-board pre-song bring-up step with no simultaneous
  playback described, so no sentence contradicts "activating a pattern pauses
  playback" — no doc edit needed. 125 → 126 native tests.
- A34 (2026-07-07, F2; SUPERSEDED SHAPE, F-wave review): status loop honesty —
  final shape = statusJson DERIVES the loop fields from three tiny const
  Scheduler getters (loopEnabled/loopStartUs/loopEndUs); the engine's
  loopEnabled_/loopStartMs_/loopEndMs_ mirror members are DELETED. One source
  of truth: the mirror WAS the bug class (fields someone must remember to
  reset), so honesty after a load now falls out by construction — a fresh
  Scheduler has no loop. This supersedes F2's first cut (rebuildAfterLoad
  clearing the mirrors), which fixed the instance but kept the class.
  Byte-compat: clearLoop() only flips loopOn_ and keeps the last
  loopStart_/loopEnd_, exactly like the old mirrors kept their last values on
  setLoop(false) — so status values are identical in every case except the
  already-sanctioned post-load one. This changes status VALUES, not shape —
  after a load that follows an enabled loop, /api/status now reports loop
  {enabled:false,startMs:0,endMs:0} instead of the previous song's range: the
  F-wave's ONLY sanctioned status delta (all REST routes and reply field names
  stay byte-identical). Native test written red-first (loaded loop 1000–5000 →
  load new song → asserts the reset) and kept UNCHANGED across the supersede —
  it now pins the derived truth. 124 → 125 native tests.
- A33 (2026-07-07, F1): cross-task fence = ONE plain (non-recursive) FreeRTOS
  mutex owned by App (`xSemaphoreCreateMutex` in begin()), chosen over a command
  queue — HTTP handlers need synchronous results (bool + statusJson reply), so a
  queue would still block the HTTP task on a semaphore (same blocking, more
  machinery, plus std::function allocation). App::tick takes the lock ONCE around
  its entire body (ble_.poll()→leds_.show()): poll dispatches onKeyDown → engine
  mutations, and renderFrame()'s returned reference is consumed by leds_.show
  inside the same critical section — closing A32 race 2 (renderer_ swap vs an
  in-flight frame reference) properly. Every HTTP-task entry point locks too
  (loadSong/transport/setMode/setTempo/setLoop/setTrack/setTestPattern/
  applySettings/statusJson); statusJson dropped `const` (web_server holds App&
  non-const — cleaner than a mutable handle). Non-recursive is correct: no locked
  method calls another (onPianoNoteOn deliberately does NOT lock — it runs on the
  loop task inside ble_.poll(), already under tick's lock; transportLocked is
  lock-free by contract, called only under a caller's guard). ZERO new work on
  the latency path: the lock is taken once per tick, not per key event; key
  verdicts and their frame render happen inside the already-held lock. NARROWED
  (F-wave review R1): critical sections hold ONLY engine mutations + MIDI sends —
  loadSong's flash read + parseMidi run UNFENCED before the lock (locals only;
  store_ is HTTP-task-only), and applySettings releases the fence before
  store_.saveSettings (settings_ is HTTP-task-owned; the loop task never reads
  it — the engine holds copies from configure). DOCUMENTED delta, now actually
  true: a tick can wait behind at most one in-flight engine command (ms-scale,
  never flash IO, only when the web remote is used) — that IS the mandated
  serialization. Send-path scope (amended, review R5): loop-task tick sends and
  REST-path engine sends serialize through the lock — previously they hit the
  MIDI lib concurrently; GET /api/ble's connected() READ stays outside the fence
  (a lone volatile bool, benign — fencing it buys nothing). begin() asserts the
  mutex was created (configASSERT — heap exhaustion fails loud at boot, not as a
  mystery xSemaphoreTake(NULL) later). RAII FenceGuard
  struct in app.cpp — no naked take/give. FreeRTOS headers live in firmware/src
  only (app.h/app.cpp); core stays Arduino-free. Native characterization (3 tests)
  pins PlaybackEngine coherence at the exact sequential interleavings the fence
  guarantees: loadSong between ticks while Playing (old sound off, new song Idle@0
  and named, next tick a clean no-op), configure between tick and frame (next
  frame uses new color, no stale state), statusJson between ticks (state/position/
  pending agree). 121 → 124 native tests.
- A32 (2026-07-07, R-wave review): 8-angle code review of the full wave diff vs
  ec0293b. APPLIED (all zero-behavior-change, gates re-run green): velocity-0
  note-off rule now owned once by dispatchNote() in the MidiIo seam (both adapters
  call it — it's generic MIDI running-status, not a piano hack; the old duplication
  meant a future adapter could silently feed key releases to wait mode as
  presses); barrierMode() replaces 9 copies of the wait/accompaniment predicate;
  WaitMode::isPending() replaces renderFrame's re-derived membership scan; dead
  discard-loop around seek(0) removed; JSON routes now 400 on bodyless requests
  instead of hanging the connection (upload route already had the guard); upload
  ?name= validated once on the first chunk and stashed in BodyIntake; test song
  fixtures deduped into test/helpers/test_songs.h (were triplicated byte-identical)
  and test colors derive from Settings{} defaults; FakeMidiIo dead surface dropped.
  ACCEPTED timing deltas (logged, not fixed): loadSong sends silencing note-offs
  after the Scheduler rebuild (ms-scale, HTTP path); frameDirty_ gained REST-path
  writers (A27) — a racing write can lose to frameDue's clear, costing ≤1 frame
  period (16.7ms). DOCUMENTED pre-existing device races carried over UNCHANGED
  from ec0293b (fixing = architecture change, out of refactor-only scope):
  loadSong's Scheduler rebuild vs a concurrent tick (use-after-free window),
  applySettings' renderer_ replacement vs an in-flight renderFrame reference, and
  statusJson iterating wait/track state the loop task mutates — all HTTP-task vs
  loop-task; a serialization fence is the post-wave fix. Also pre-existing (kept):
  loop fields not reset on song load, so /api/status can report the previous
  song's loop as enabled (was true at ec0293b too — fixing changes output);
  test-pattern early-return freezes playback without pausing it (skipped-time
  burst on pattern off, also ec0293b behavior).
- A31 (2026-07-07, R7): X-macro Settings field table REJECTED — the 11 fields carry
  heterogeneous policies (hex-color conversion, conditional/min-only clamps,
  tolerant type-guarded reads), so the table needs ~6 policy columns and reads
  worse than the plain code it would replace; the names are LOCKED (never renamed),
  so rename-hazard protection has near-zero expected value; and teaching
  webui/build.py to parse a C++ header adds a brittle cross-language step to the
  webui_gz.h pipeline. Adopted the protective 20% instead: a native contract test
  locks toJson()'s key set byte-exactly against docs/API.md (no strays, none
  missing), so drift now fails a test instead of silently breaking the web UI.
- A30 (2026-07-07, R6): the intake rewrite quietly fixes three latent REST bugs the
  old duplicated chunk handling had — (1) the upload path parked (void*)1 in
  _tempObject, which ESPAsyncWebServer's request destructor blanket-free()s →
  free((void*)1) heap corruption on every completed upload; (2) an oversize JSON
  body sent a 413 for EVERY arriving chunk (multiple replies on one request);
  (3) a rejected upload (bad name/oversize) later triggered a second "empty upload"
  400 from the completion callback. All three are protocol-hygiene fixes invisible
  to a correct client; SPEC-visible behavior, routes and reply shapes unchanged.
  BodyIntake (heap struct, delete-and-null on disconnect) is now the only thing
  ever stored in _tempObject; a `failed` flag drains remaining chunks silently.
- A29 (2026-07-07, R5): buffer-variant semantics split by role — Scheduler queries
  (advance/seek/notesOnAt/onsetsBetween) CLEAR the caller's buffer then fill (reuse
  pattern), while NoteEmitter producers (consume/allOff) APPEND (they feed the one
  shared per-tick MIDI-out queue, so the engine passes its out-vector straight
  through with no intermediate copy). WaitMode::update also got a reused chord
  buffer — it queries notesOnAt every tick while holding at a barrier, which the
  R-item list didn't name but the zero-steady-state-alloc goal requires.
- A28 (2026-07-07, R3): dedupe shape = shared core SoundingSet<Entry> (add /
  eraseFirst-by-predicate / drain / clear) used by engine lights, scheduler timeline
  and note emitter — NOT the review's alternative of the engine reading the
  scheduler's own sounding list. Live-reading changes observable behavior: paused-
  then-resumed notes would resurrect their lights (engine clears its list on pause,
  scheduler doesn't), and mid-note lights-toggles would apply retroactively. Zero-
  behavior-change mandate wins; identities stay per-consumer (note+track vs
  note+channel vs note+channel+track) via predicates.
- A27 (2026-07-07, R2): PlaybackEngine's sound-stopping paths (pause/stop/seek/
  setMode/loadSong/finish) set the frame-dirty flag instead of App calling
  leds_.allOff() synchronously from the HTTP task. The strip now clears on the very
  next loop tick (sub-ms; a dirty frame bypasses the 60fps limiter and renders dark)
  — visually identical, and it removes a pre-existing cross-task FastLED call (HTTP
  task writing LEDs while the loop task may be mid-show). Engine REST methods take a
  `std::vector<MidiOutMsg>& out` so note-offs are still sent synchronously on the
  caller's task, exactly as before. Engine is non-copyable (emitter_ points at its
  own guard_).
- A26 (2026-07-07, R1): MidiIo seam = abstract interface + `final` concrete adapters,
  NOT a templated App. App keeps its by-value `BleMidiIo` member; because the adapter
  class is `final`, every call through the concrete member devirtualizes — zero new
  indirection on the BLE-in→match→LED-out path — while the named interface + FakeMidiIo
  (queue-then-poll, mirroring MIDI.read()) make the seam real with two adapters.
  Templating App was rejected: App stays device-bound after R2 (the engine owns the
  pure logic), so a compile-time seam there buys nothing and costs a header-ized App.
  MidiOutMsg/MidiOutType moved from note_emitter.h to midi_io.h — the message type
  belongs to the transport seam; note_emitter.h re-exports via include.
- A25 (2026-07-07, W5-agent): ninsheetmusic.org Cloudflare-blocks automation (Turnstile
  loop → ERR_BLOCKED_BY_RESPONSE even in the headed stealth browser), so Tier 0–1
  downloads came from vgmusic.com only; NSM items are queued for Christian with direct
  /download/mid/<id> links (IDs found via web search, no downloads attempted elsewhere).
  vgmusic ensemble files (multi-track, unnamed hands) were kept anyway — the trainer's
  per-track hand UI makes them usable, and SONGBOOK marks them "multi" for later
  MuseScore splitting.
- A24 (2026-07-07, W5-agent): PD melody provenance — Vivaldi Largo and Clair de Lune
  transcribed note-for-note from Mutopia Project LilyPond urtexts (CC/PD typesets of PD
  works), Amazing Grace from the hymnal ABC (colinhume.com), Minuet in G from a public
  ABC transcription incl. its bass line; generate.py writes raw SMF bytes (no Python
  deps) so the pack is reproducible from the repo alone.
- A23 (2026-07-07, W5-agent): PD starter-pack keys/scope — Ode to Joy in C (16 bars);
  Amazing Grace in G 3/4 (full hymn, ties realized as long notes); Minuet in G complete
  32 bars, no repeats (trainer's A-B loop covers repetition), bass simplified to
  quarters+; Vivaldi Winter Largo transposed Eb→C (only accidental left is F#, bass
  stays authentic incl. low C2/D2 — they're just keys and the strip covers them); Clair
  de Lune = famous opening only, urtext bars 1–9 transposed Db→C for beginner ergonomics
  (5 flats + 9/8 was the hard part; bar 9's theme-return lands on the tonic so it closes
  cleanly), inner voices thinned to melody+LH dyads; full Db piece stays Tier 3.
- A22 (2026-07-07, W4-agent): echoWindowMs has no field in the web UI Settings page,
  so TROUBLESHOOTING's echo fix ships a single copy-paste PowerShell line
  (Invoke-RestMethod PUT to /api/settings) with open-PowerShell hand-holding — the
  brief assumed a UI field ("raise echoWindowMs in settings") that doesn't exist;
  adding one to webui is a future nicety, not a doc's job.
- A21 (2026-07-07, W4-agent): BUILD-GUIDE adds an optional tidy-up — jumper the
  74AHCT125's unused inputs/enables (pins 4, 5, 9, 10, 12, 13) to GND — floating
  CMOS inputs are bad practice; purely additive, does not touch the frozen rows.
- A20 (2026-07-07, W4-agent): USB-driver docs cover a CH340 fallback alongside
  CP2102 (search "CH341SER driver") — ELEGOO/clone boards vary in USB chip, and a
  wrong-driver dead end would strand Christian with no AI present.
- A19 (2026-07-07, W4-agent): BRINGUP runs the strip test pattern AFTER the
  PSU+strip hookup (brief's step list had it before) — a walking white dot is
  invisible on an unpowered, unconnected strip; the pre-hookup steps still prove
  flash/AP/UI on the bare board.
- A18 (2026-07-07, W4-agent): docs mandate "never USB and PSU connected at the same
  time" — clone devkits don't reliably have the backfeed diode; costs live serial
  while PSU-powered, so IP discovery is documented via router device list or a
  temporary USB-only session (strip unpowered, WiFi still boots).
- A17 (2026-07-07, W4-agent): the ~0.77m of spare strip beyond C8 stays DARK, never
  cut — BOM allows "trim or leave dark"; leaving it dark is decision-free,
  reversible, and needs no tools on assembly day.
- A16 (2026-07-07, 8): esp32dev uses huge_app.csv partitions (3MB app + ~0.9MB LittleFS,
  no OTA — flashing is over USB anyway): BLE+WiFi+web server = 1.44MB, over the default
  1.3MB slot. Filesystem = littlefs. Libraries: lathoub BLE-MIDI client over NimBLE 1.x
  (lower RAM than Bluedroid), ESP32Async fork of ESPAsyncWebServer (maintained), with
  -DASYNCWEBSERVER_REGEX for path params.
- A15 (2026-07-07, 8): single-track ("Both"-hand) songs light in the right-hand color;
  demo/follow show sounding notes at 100%, wait/accompaniment shows the due chord at
  100% (pending members only) — cleared members go dark, which doubles as per-key
  clear feedback.
- A14 (2026-07-07, W3-agent): web UI visual identity — candle-gold accent ("lux"), seek bar
  drawn as the LED strip (blue→gold→green gradient), wait-mode pending notes rendered as
  glowing piano-key chips; blue/green reserved for left/right-hand meaning per SPEC colors.
- A13 (2026-07-07, W3-agent): upload renames `.midi`→`.mid` and pre-rejects >256 KB
  client-side (mirrors server rules for a faster error); loop times entered as m:ss, a
  bare number is taken as seconds.
- A12 (2026-07-07, W3-agent): previewCap shown as 0–100% mapped to the API's 0–1 float;
  brightness slider min=1 (0 would read as a dead strip); settings PUT sends only changed
  fields (API allows partial), debounced 400 ms, and GET runs on every Settings tab open.
- A11 (2026-07-07, W3-agent): tempo slider is log-mapped over 1–500% (100% ≈ 3/4 of
  travel) with throttled sends while dragging (150 ms) plus an authoritative send on
  release — API clamps, so intermediate values are safe.
- A10 (2026-07-07, W3-agent): `/api/status` doesn't expose the practice hand, so the UI
  keeps the last-chosen hand locally (default `both`) and includes it in every
  `/api/mode` POST — demo/follow ignore it per API.md, so always sending is harmless.
- A9 (2026-07-07, 5): wait-mode key verdicts — presses between chords and re-strikes of
  already-cleared chord members are Ignored (no red flash; noodling isn't an error);
  only a non-member press while a chord is due is Wrong. Duplicate notes across tracks
  in one chord dedupe to a single pending key.
- A8 (2026-07-07, 5): echo guard = one ignore credit per transmitted note-on, 250ms
  default window (BLE round trip is tens of ms), per-note counters; window is a
  variable to re-tune against the real FP-30X at bring-up.
- A7 (2026-07-07, 4): scheduler emits ALL events tagged with their track; hand filtering
  happens in each consumer via TrackMask — lights and accompaniment need *different*
  filters (muted hand: no lights but piano plays it), so filtering inside the scheduler
  would be wrong. Barrier = wait-mode hold: nothing at or past the barrier fires until
  cleared; loop wrap flushes note-offs for sounding notes.
- A6 (2026-07-07, 3): key geometry = rear-of-keyboard cluster model (octave 164.5mm;
  C-E group = 5×14.1mm slots, F-B group = 7×13.43mm slots) — matches where the strip
  physically sits better than front white-key widths; 1mm edge margin guarantees
  adjacent keys never share an LED at 180/m.
- A5 (2026-07-07, 2): MIDI parser scope — SMPTE division files rejected (rare for piano
  sheet MIDIs); format 2 parsed same as 0/1; overlapping same-note retrigger closes the
  prior note; spurious note-offs ignored; notes still open at End-of-Track get closed
  there. Keeps the trainer deterministic on imperfect files.
- A4 (2026-07-07, 2): CC64 captured into a separate `pedal` list (not the note list) —
  demo/accompaniment passes it through, wait-mode matching never sees it (SPEC v1).
- A3 (2026-07-07, 2): dev toolchain = PlatformIO CLI via uv tool + WinLibs MinGW-w64 GCC
  via winget for the native env (PlatformIO's `platform_packages` toolchain is ignored
  by the native platform). BUILD-GUIDE will document VSCode+PlatformIO for strangers.
- A2 (2026-07-07, 1): WiFi = stored creds with AP-mode fallback (`ViaLucis` hotspot +
  config page) — arrival-day simplicity for Christian, replicability for strangers.
- A1 (2026-07-07, 1): LED data = GPIO16, PlatformIO esp32dev+native, core logic pure
  C++ — enables full pre-hardware testing; GPIO16 avoids strapping pins.
