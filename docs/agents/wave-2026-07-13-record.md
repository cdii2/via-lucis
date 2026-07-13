# Record wave (v3) — dispatch plan, 2026-07-13

Lead: Fable (session "Lucis · Record lead"). Executes `docs/DESIGN-record.md` (LOCKED)
via PROGRESS.md §"v3 — Record wave" (REC1–REC7). Branch: `v3/record` off main @ 6b8b4d2.
Baseline verified: 305 native tests ALL PASS, esp32dev ~48% flash / ~22% RAM.

## Run order

| Phase | Agent | Model | Items | Where |
|---|---|---|---|---|
| A1 (serial) | Lucis · SMF+Capture | opus | REC1, REC2 | main checkout, branch `v3/record` |
| A2 (serial, after A1) | Lucis · Director+REST | opus | REC3, REC4 | main checkout, branch `v3/record` |
| B1 ∥ B2 ∥ B3 (parallel, after A2) | Lucis · WebUI / Editor / Docs | sonnet / opus / sonnet | REC5 / REC6 / REC7 | worktrees `rec/webui`, `rec/editor`, `rec/docs` off `v3/record` |
| Close | lead | fable | 8-angle /code-review, fixes, merge to main | — |

Wave A runs directly on `v3/record` (nothing else runs concurrently; one REC item per
commit). Wave B agents get isolated worktrees; the lead merges B1/B2/B3 back into
`v3/record`, runs the integrated gate, then the closing review, then merges to main.
**No push** — main is left clean and push-ready for Christian.

## Contract freezes (lead decisions — builders do not relitigate)

### REST (REC4 implements + writes docs/API.md; REC5 consumes)
- `POST /api/record/arm` — body optional `{"countIn": true, "bpm": 90}` (Free capture
  only; bpm clamped 20–300; ignored when a song is loaded). → `200` + status JSON.
  Typed refusals: `409 {"error":"playing"}` (practice transport running in a state that
  forbids arming — presentation/show playing), `409 {"error":"already armed"}`,
  `507 {"error":"low space"}` (free-space check), `400 {"error":"bad json"}`.
- `POST /api/record/stop` — finalize + save → `200 {"name":"recording-<n>.mid"}` +
  the take appears in `/api/songs`. `409 {"error":"not armed"}` if idle. A stop with
  zero captured events discards silently → `200 {"name":""}`.
- `POST /api/record/discard` — drop armed/recording state, save nothing → `200`.
  `409 {"error":"not armed"}` if idle.
- `GET /api/status` gains a `"record"` object (before `"wifi"`, which stays the LAST
  key): `{"state":"idle|armed|recording","elapsedMs":0,"usedBytes":0,"budgetBytes":N,
  "countIn":false,"bpm":90}`.
- `POST /api/songs/{name}/rename` body `{"name":"new-name.mid"}` → `200 {"name":...}`,
  `400` bad/non-.mid name, `404` missing, `409 {"error":"exists"}`. (Needed by REC5
  "rename take"; general-purpose for any song.)
- `recordBudgetKB` appends to `/api/settings` (default 64 — revised from 256 at lead
  review, A75: arm reserves the whole budget as one contiguous RAM block; clamp
  16–1024 KB). Arm also refuses `507 {"error":"low memory"}` when the largest free
  heap block can't hold budget + margin. Contract test updated append-only (the
  sanctioned exception).
- Duration cap = compile-time constant (~10 min, `kRecordMaxMs`), not a setting.

### Core seams (REC1/REC2 build; REC3/REC4 consume)
- `lib/core/src/vialucis/smf_writer.{h,cpp}` — event/track list → SMF **format-1**
  bytes. Fixed timebase: TPQ=500 + tempo meta 500000 us/qn (120 BPM) ⇒ **1 tick =
  1 ms exactly** (true-ms delta times per DESIGN). Track 0 = tempo/meta; performance
  tracks carry name meta (`Left`/`Right` when hand-split applies) + notes + CC64.
  Round-trip native test: `parseMidi(write(events))` reproduces events.
- `lib/core/src/vialucis/midi_capture.{h,cpp}` — `MidiCapture` tape head. Pre-reserved
  bounded ring/array at arm time (byte budget + `kRecordMaxMs`); `onNoteOn/onNoteOff/
  onPedal(valueMs…)` appends are O(1), zero steady-state alloc; capture starts on first
  note (leading silence trimmed — timestamps relative to first note-on); stop/discard;
  typed state + overflow reason. Overflow/full **never blocks or delays** the caller —
  it flags and drops.
- Echo exclusion without credit theft: `WaitMode`/`ScoreFollower` consume `EchoGuard`
  credits destructively, so the tape head must NOT call `shouldIgnore` on the engine's
  guard. Sanctioned mechanism: a **second, capture-owned `EchoGuard`** fed by the same
  send-registration point (`NoteEmitter`/engine emission path) so capture filters
  device-sent notes independently. (Builder may propose better; must not touch the
  wait-mode latency path before the verdict.)
- Tap placement: in `ModeDirector::onKeyDown` **after** `engine_.onKeyDown` (the
  verdict) — REC2 exposes the primitive; REC3 wires it there.

### ModeDirector (REC3)
- `TopMode` gains `Record` (name `"record"` in status). Entered only via arm with no
  song loaded; arming with a song loaded stays Practice (capture runs alongside,
  paints nothing). Arming counts as write activity and Record outranks AFK while
  armed/recording (AFK disarmed). Record frame source = Reactive monitor + discreet
  heartbeat + (optional) count-in pulse; heartbeat avoids red/blue/green.

### A-entries & shared files
- Wave A agents append `ASSUMPTIONS.md` directly (A58+, newest on top).
- Wave B agents do NOT edit `ASSUMPTIONS.md` or `PROGRESS.md` — they report proposed
  A-entries; the lead writes them at merge. Only REC4 touches `docs/API.md`.

## Gates (every REC item, one commit each)
- `pio test -e native` ALL PASS (grep summary for FAIL/ERROR — exit code lies);
  native count strictly grows.
- `pio run -e esp32dev` clean; flash ≤70% / RAM ≤35%.
- Zero new alloc/blocking/indirection on BLE-in→match→LED-out; capture append proven
  O(1)/zero-alloc after the verdict (buffer-stability + full-buffer-never-affects-
  verdict tests).
- v1 contract tests green except the sanctioned `recordBudgetKB` settings append.
- REC5: regenerate `firmware/src/webui_gz.h` via `python webui/build.py`.
- REC6: editor selftest green via browse daemon (`file://…/editor/editor.html?selftest=1`),
  including the existing corpus byte-pin.
- `git ls-files songs/local` empty before any merge.
