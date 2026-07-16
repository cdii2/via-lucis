# ASSUMPTIONS

Autonomous decisions made without asking, one per line, newest on top. Format:
`A<n> (date, iter): decision — rationale.`

- A190 (2026-07-16, ParseMem, we): **FileByteSource yields (`yield()`) every
  16th refill gulp, and the fill-pass NoteTracker is allocated ONCE per parse
  and reused across all tracks** (was `make_unique` PER track). The refill is
  the sole on-device yield seam (iron rule 1): a streamed parse runs the file
  TWICE (count + fill), both on the async_tcp/HTTP task, so a big file could
  otherwise starve the core-0 watchdog (the A183 failure mode) — yielding every
  16 × 256 B ≈ 4 KB feeds it without touching the BLE→match→LED latency path.
  Reusing one tracker is behavior-identical because `closeAll` resets every
  `active[]` slot at end-of-track, so the next track starts clean; it also drops
  the per-track 18 KB alloc/free churn. Reader gulp buffer = 256 B on the stack
  (the 18 KB tracker is the only heap; A181 stack-overflow stays fixed).
- A189 (2026-07-16, ParseMem, we): **the songs-list parse check runs
  `checkMidi` (the COUNT pass only — no NoteTracker, no notes vector) and caches
  a `ParseFail` reason (`Memory`/`Corrupt`) additively**, surfaced as
  `parseFail:"memory"|"corrupt"` alongside `parseOk:false` (absent while
  unknown / when ok). Cheaper than the old read+full-parse AND honest about WHY
  a badge is bad. `SongParseCache` gained a reason-aware `set()` overload +
  `failReason()`; the bool-only `set()` still defaults a failure to `Corrupt`
  (pre-A185 behavior) so nothing else changed. WEBUI ASK in the report (tooltip
  copy) — I do not touch webui/.
- A188 (2026-07-16, ParseMem, we): **`SongLoadOutcome::TooBig` split out of
  `ParseError`; the load route returns `413 {"error":"song too large to load
  (device memory)"}`** — a status AND message distinct from the corrupt `400`
  and the 256 KB upload `413`. Rationale: a VALID-but-large song must not be
  mislabeled corrupt (the player would delete a good file); 413 = "this payload
  is too large to load here", which it is. Documented in API.md with an explicit
  **storage-cap ≠ load-cap** callout (`kMaxSongBytes` 256 KB is a DISK ceiling;
  the load 413 is a RAM ceiling; the two are independent). `kMaxSongBytes`
  unchanged (LOCKED item 6-6 spirit).
- A187 (2026-07-16, ParseMem, we): **retired A180's `wholeFileReadFits` and
  A182's factor-4 `parseWorkFits`, and removed `SongStore::read`** — all three
  were the "buffer the whole file in RAM, then guess if the parse fits with a
  fudge factor" model that streaming (A185) eliminates. What remains device-side
  is `parseNoteBudget(maxAlloc, fixedOverhead, margin)` (storage_budget.h,
  native-pinned): the honest budget = largest allocatable heap block −
  `midiParseFixedOverhead()` (the ~18 KB NoteTracker) − 8 KB working margin,
  clamped to 0. No cheap SIZE-only reject is kept because size alone cannot
  PROVE a file too big (a large file can be mostly meta); the exact count pass
  is the honest gate, and the only size gate that remains is the 256 KB storage
  cap in `openSongSource`.
- A186 (2026-07-16, ParseMem, we): **exact TWO-PASS budgeting — pass 1 COUNTS
  events (note-ons == emitted notes, exactly), the exact output size is checked
  against the budget BEFORE any large allocation, then pass 2 reserves each
  vector once at its final size and fills.** This kills the vector-doubling
  transient A182 fought (capacity == size, zero realloc), and makes the refusal
  a clean typed `MidiParseError::TooBigForMemory` (song handed back EMPTY) — an
  over-budget song is never gambled on an allocation that would abort on
  -fno-exceptions. Trade documented: 2× file IO + 2× parse CPU vs the old single
  pass, paid because parse is HTTP-task/on-demand (not the latency path), the
  IO is streamed (no RAM), and the yield seam (A190) feeds the watchdog. Budget
  defaults to `SIZE_MAX` so every native caller is unchanged. Chose a byte
  budget over a note-count cap so the device math (`getMaxAllocHeap()` in bytes)
  maps directly; `midiParseOutputBytes()` folds notes+tempo+pedal so a
  pedal-heavy file can't slip the cap.
- A185 (2026-07-16, ParseMem, we): **the MIDI parser now STREAMS its input
  through a pure-virtual `ByteSource` (midi_parser.h), pulled forward through a
  256 B refill buffer — the whole-file RAM buffer is gone from the parse
  budget.** The parser only ever reads forward, so a small gulp buffer suffices;
  `parseMidi(const uint8_t*, len, budget=SIZE_MAX)` keeps the buffer API for
  native callers, and a new `parseMidi(ByteSource&, budget)` + `checkMidi(...)`
  serve the device. Chose **virtual dispatch over templating** the parser on the
  source type: dispatch is per-REFILL (once per 256 B), not per byte, so the
  cost is negligible AND the parser instantiates ONCE (flash-thrifty on ESP32),
  and it keeps lib/core pure C++ — the LittleFS file-handle source
  (`FileByteSource`) lives behind the seam in firmware/src, the native fakes in
  tests (iron rule 4). Device song load + list now call `store_.openSongSource`
  (returns a `unique_ptr<ByteSource>` owning the file handle) instead of the
  removed `read()`. The whole corpus is pinned byte-exact through a 1-/2-/3-/7-/
  64-/512-byte chunk source (test_midi_stream) — the equivalence sweep.
- A179 (2026-07-16, E3, we/editor): §3-E's editor items (min cue span,
  durationMs/clockSource, scope lo<=hi, speed clamp) live entirely in
  `editor/editor.html` (the off-device VLS show editor, docs/SHOW-FORMAT.md
  P3), NOT `webui/index.html` — the on-device webui has no cue/scope/speed
  authoring UI at all (confirmed by grep: zero matches for those fields
  outside the record count-in BPM control). The E3 brief said "all in
  webui/index.html"; built the four editor items in editor/editor.html
  instead since that's the only place they exist, and item 5 (count-in
  micro-copy) in webui/index.html since that control does live there. Flagged
  as an ASK in the final report rather than silently deviating.
- A178 (2026-07-16, E3, we/editor): "drop durationMs/clockSource from device
  rows" (§3-E item 2) resolved as a straight bug fix, no doc ask needed —
  `loadShows()`'s per-show row read `s.durationMs`/`s.clockSource`, but
  `GET /api/shows` (API.md "Shows", SHOW-FORMAT.md §3) only ever returns
  `{name,size}` ("per-item meta would mean parsing every file on list —
  deferred"); mock_device.py's `/api/shows` handler already matches that
  schema too. Those two fields were always `undefined` in the row, rendering
  a permanently-fake "0.0s · clock ?" — removed, row now shows just name+size.
  No schema violation, no discrepancy to report.
- A177 (2026-07-16, E3, we/editor): "clamp speed >= 1" (§3-E item 4) is the
  WIRE BYTE floor (SHOW-FORMAT.md's fixed-point x16 speed field), not a
  product decision to disallow slow-motion cues — the inspector's speed
  slider (0.25x-4x) is an intentional creative range, not a bug, and nothing
  in DESIGN-lightshow/SHOW-FORMAT restricts authored speed to >=1.0x. The
  real gap: `bakeCue`'s existing `clamp(Math.round(c.speed*16),1,255)` looks
  like it already floors the byte at 1, but `Math.max`/`Math.min` propagate
  NaN, and a corrupt/hand-edited `.vlp`'s `speed:NaN` (or any non-finite
  value) poisons the clamp to NaN, which then encodes as byte 0 on the wire
  (`NaN & 0xff === 0`) — silently defeating the very floor meant to prevent
  it. Fixed with a `Number.isFinite(c.speed) ? c.speed : 1.0` guard before
  the clamp; `validateShow()` (A176) re-checks `speed>=1` at the final gate
  as defense-in-depth. `speed:0` was already safely floored to byte 1 by the
  pre-existing clamp (no NaN involved) — verified via editor selftest and a
  live upload through tools/mock_device.py, not just a code read.
- A176 (2026-07-16, E3, we/editor): min-cue-span + scope-lo<=hi (§3-E items
  1/3) enforced as TWO layers, matching the item-1 wording exactly — edit-time
  auto-clamp (best-effort UX: the `i_start`/`i_open`-uncheck handlers now
  re-clamp `endMs` the same way `i_end`/canvas-resize already did; introduced
  `MIN_CUE_SPAN_MS=50` so all four call sites share one constant instead of
  three separate hardcoded `50`s) AND a hard `validateShow(m)` gate that runs
  on the COMPILED model (post group-offset, where a violation is actually
  detectable) right before `.vls` export or upload — the only two paths that
  produce a device-consumable artifact (the `.vls.json` debug twin stays
  ungated on purpose, per SHOW-FORMAT.md: "the device never reads the JSON;
  humans and tests do"). `validateShow` returns a message naming the specific
  clip (`Clip "<name>": ...`) or cue index if unnamed, never mutates, and also
  Number.isFinite-guards startMs/endMs so a NaN can't slip past a `<=`
  comparison (NaN<=NaN is false). Verified three ways: 6 new editor-selftest
  assertions (all PASS alongside the existing 519-native-equivalent editor
  suite), and three live browser sessions against `tools/mock_device.py`
  confirming (a) a zero-length cue is rejected client-side with zero network
  calls, (b) a lo>hi scope1 cue is rejected the same way, (c) a valid
  multi-cue show still compiles, encodes, and POSTs to `/api/shows` for a 201
  exactly as before (L2's mock upload path untouched).
- A192 (2026-07-16, bring-up hotfix, dispatcher): **NoteTracker rebuilt as a
  bounded compact open-notes list (kMaxOpenNotes=256 × 8 B ≈ 2 KB, reserved
  once; > cap ⇒ TooBigForMemory) and `kParseHeapMarginBytes` 8→4 KB** — with
  the FP-30X CONNECTED the device's max contiguous heap block is only
  ~17.4 KB (measured live; NimBLE connection buffers eat ~30 KB), so the old
  18 KB dense tracker + 8 KB margin made parseNoteBudget()=0 and EVERY song
  413'd as too-large exactly when the player needed to load one. closeAll
  emits in (ch,note) ascending order — the dense array's iteration order —
  so the 374-test conformance corpus stays byte-exact (530/530 green).
  Fits-math now: budget ≈ maxAlloc − 6 KB → piano-connected ≈ 950 notes;
  piano-off ≈ 4900 notes. Also A191 inside FileByteSource: delay(1) NOT
  yield() every 16th gulp — taskYIELD never lets IDLE feed the core-0 task
  watchdog, and a 105 KB streamed count pass (410 slow LittleFS gulps of
  continuous RUNNING on async_tcp) abort-looped the device (proven live).
- A184 (2026-07-16, bring-up hotfix, dispatcher): **`WiFi.setSleep(false)`
  in both WifiManager paths (STA + recovery AP).** The Arduino default
  (WIFI_PS_MIN_MODEM) parks the WiFi radio between DTIM beacons; under BLE
  coexistence (piano-off scan cycle: lathoub scans at ~83% duty — window
  500/interval 600 slots — for 1 s per E2-backoff slot) every HTTP request
  stalled seconds-to-minutes and the live webui was unusable (wizard screens
  took ~3 min to change; Load appeared completely dead — the JS was fine,
  browser fetches simply have no timeout). Wall-powered device: power saving
  buys nothing. Canonical ESP32 coex fix. Found live 2026-07-16 evening,
  Christian driving the real UI.
- A183 (2026-07-16, bring-up hotfix, dispatcher): **`App::songsForList`
  parse-checks at most `kParseChecksPerListCall` (4) uncached files per call,
  yields (`delay(1)`) between each, and reports still-unchecked files with
  `parseKnown=false` — the route then OMITS `parseOk` (absent = "not checked
  yet", never "bad"; `SongParseCache::has()` added as the known-vs-good
  axis, native-pinned).** Third live crash mode of the day: an unbounded
  cold-cache sweep (13+ files, one HTTP request on async_tcp) starved the
  core-0 IDLE task past the 5 s task-watchdog → `task_wdt: Aborting` →
  reboot, which made bulk_upload's per-file verify GET look like a network
  failure on every attempt. The webui's badge already feature-detects
  `parseOk === false` only, so absent-while-warming needs zero webui change;
  the cache fills across subsequent polls. API.md updated.
- A182 (2026-07-16, bring-up hotfix, dispatcher): **`SongStore::read`'s guard
  upgraded from read-fits (A180) to `parseWorkFits`: file bytes ×
  `kParseExpansionFactor` (4) + margin must fit `getMaxAllocHeap()`** —
  after A180+A181 the list STILL crash-looped; the decoded backtrace showed
  `parseMidi → notes.push_back → operator new → std::bad_alloc →
  terminate`: the 105 KB file READ fine, then the parsed-notes vector (~12-16
  B/event vs 3-4 disk bytes + realloc doubling) blew the heap mid-parse, and
  our TUs compile without -fexceptions so the throw is uncatchable. The
  guard now budgets the whole read+parse job. Consequences: a
  beyond-RAM-ceiling song lists as `parseOk:false` and load-fails typed —
  which is the DESIGN's own position (big-file playback = out-of-scope
  streaming-parse project). Factor 4 is deliberately conservative and
  BRING-UP-TUNABLE (§6-6 spirit): measure the real expansion on hardware,
  then widen. Native-pinned with the exact live failure shape.
- A181 (2026-07-16, bring-up hotfix, dispatcher): **`parseTrack`'s
  `NoteTracker` (16x128 open-note slots ≈ 18 KB) moves from the stack to the
  heap (`std::make_unique`)** — it exceeded the ENTIRE 16 KB async_tcp stack
  it runs on for HTTP song load and E1's songs-list parse check. Proven live:
  `***ERROR*** A stack overflow in task async_tcp` + SW_CPU_RESET on every
  GET /api/songs. Latent since v1 but UNREACHABLE on-device until Wave A
  un-shadowed the load route (R1 swallowed every HTTP path into the parser;
  native tests run with a desktop-sized stack and never noticed). Same
  zeroing semantics (`active` has a member initializer; `open` stays
  uninitialized-but-guarded, as before). The A180 read-guard margin (16 KB)
  covers this transient allocation alongside the file buffer.
- A180 (2026-07-16, bring-up hotfix, dispatcher): **`SongStore::read` refuses
  any whole-file read that `wholeFileReadFits(len, ESP.getMaxAllocHeap())`
  rejects (new pure guard + 16 KB margin in storage_budget.h, native-pinned)**
  — found live at first flash of the wave pipeline: E1's parseOk check
  (A164) reads each changed song fully into a heap vector on the async_tcp
  task, and a 105 KB song against ~80 KB free heap ABORTS the vector resize
  (exceptions disabled) → SW_CPU_RESET → GET /api/songs crash-looped the
  device every call (bulk_upload's per-file verify GET then made every
  subsequent upload look like a network failure). The guard also protects
  song LOAD (same read path — the pre-existing "RAM ceiling" case now fails
  as a clean typed error instead of a crash). Consequence: a song too big
  for the heap lists as `parseOk:false` (honest — the device cannot load
  it) and loads as a 400 parse error, never a reboot.
  to `settings.cpp`'s `hexToColor` — design by E1 (its branch-local A165,
  compacted to A159 at merge), handed over and applied here because `settings.cpp`/
  `test_settings` are E2-owned this wave. Replaced
  `sscanf(s+1, "%2x%2x%2x", ...)` (whose `%2x` is a per-conversion MAXIMUM
  width, not a fixed count — "#12345" and "#0000000000" both slipped through
  as "valid") with manual digit-by-digit validation requiring EXACTLY `#` +
  6 hex digits, case-insensitive, nothing more or fewer. `readColor()`
  already left a field at its previous value on a `hexToColor` rejection —
  unchanged, so the PATCH-leaves-existing-value behavior came for free.
  Added `test_malformed_hex_strings_are_rejected` (8 bad bodies: short,
  long, way-too-long, missing `#`, non-hex letters, embedded space, empty,
  bare `#`; plus a good-value control and a lowercase-hex control). Native
  503 (was 502).
- A174 (2026-07-16, E2, we/ble): Active Sensing (0xFE) → onActivity verified
  NOT a bug (BUGFIX-PLAN §3-E item 5) — pinned with a comment only, not a
  native test: `firmware/src/ble_midi_io.cpp` wires `noteActivity()` from
  exactly three handlers (NoteOn/NoteOff/ControlChange) and never registers
  `MIDI.setHandleActiveSensing(...)`; the FortySevenEffects MIDI Library only
  dispatches to explicitly-registered handlers (no fallback), so an Active
  Sensing byte structurally cannot reach `onActivity`. Not natively testable
  — this file is esp32/NimBLE-only, no native target compiles it. Verify at
  bring-up that AFK doesn't stay disarmed on Active-Sensing-only traffic.
- A173 (2026-07-16, E2, we/ble): FIX-C phantom echo credit documented at
  both ends of the gap, comment only per the Wave E2 brief (mechanism stays
  deferred to hardware bring-up, §7) — `echo_guard.h`'s `noteSent()` (credit
  registered at EMIT time by `NoteEmitter::consume()`, note_emitter.cpp:14,
  NOT owned by this wave) and `ble_midi_io.cpp`'s `send()` (the actual
  transmit, which silently no-ops while disconnected). A note whose send()
  no-oped still owns a credit, so a genuine same-note press after a
  reconnect can be swallowed. note_emitter.cpp/h were left untouched (not on
  this wave's ownership map); see the E2 report's ASKS for the one-line
  pointer another agent may want to add there.
- A172 (2026-07-16, E2, we/ble): `gConnected` (`ble_midi_io.cpp`) changed
  from `volatile bool` to `std::atomic<bool>` (BUGFIX-PLAN §3-E item 3) —
  same read/write pattern app.h already documents as safe unfenced (written
  from the BLE connect/disconnect callbacks, read from `connected()` on
  other tasks), just the correct primitive instead of a convention that
  happened to work. Behavior-preserving; `app.h`'s comment at the
  `settings()/store()/ble()` accessors block still says "a single volatile
  bool" — stale after this change, flagged as an ASK (app.h is off-limits to
  this wave).
- A171 (2026-07-16, E2, we/ble): `bleTargetName` setting (BUGFIX-PLAN §3-E
  item 2) rides BOTH Settings JSON views unchanged (it's a device name, not
  a credential — ruling §6-1's "secret-ness is a property of the field" just
  doesn't apply here) and is clamped to `kBleTargetNameMaxLen` (20 chars) —
  well under the third-party lathoub library's fixed `char mDeviceName[24]`
  buffer (`BLEMIDI_Transport.h`), which is filled via `strncpy` and does NOT
  guarantee null-termination at/over 24 bytes; clamping in code we own
  avoids ever handing that buffer an unterminated string. The library DOES
  support a real name/address filter natively (`BLEMIDI_Transport::setName()`
  feeds `BLEMIDI_Client_ESP32`'s `specificTarget`/`nameTarget`, checked in
  its scan-result callback before ever connecting) — confirmed by reading
  the actual upstream source (github.com/lathoub/Arduino-BLE-MIDI), not
  guessed. It's read ONCE at `MIDI.begin()` (called once, at boot, from
  `App::begin()` — AFTER `store_.loadSettings()`), so a live PUT
  /api/settings change takes effect at the next reboot only, same as
  `wifiSsid`. Wiring the one-line call site is an ASK (app.cpp is off-limits
  to this wave) — see the E2 report.
- A170 (2026-07-16, E2, we/ble): BLE scan backoff (BUGFIX-PLAN §3-E item 1) —
  confirmed via the actual upstream lathoub source
  (`BLEMIDI_Client_ESP32.h::available()`/`scan()`) that the scan/reconnect
  cycle is entirely poll-driven with zero library-side cooldown: every call
  while disconnected either connects or, once `scanDone`, immediately
  re-arms a fresh 1s scan. Since `poll()` forwards to `MIDI.read()` every
  `loop()` iteration, the only lever available without vendoring/forking the
  library is throttling how often a DISCONNECTED `poll()` is let through to
  `MIDI.read()` at all. Implemented as a bounded exponential backoff
  (2s→4s→…→60s cap, `kBleScanBackoff*` constants in `ble_midi_io.cpp`,
  flagged bring-up-tunable per §6-6), reset to the fastest retry on ANY
  connect OR disconnect event; a connected link is never throttled (poll()
  always reads immediately when `gConnected`).
- A169 (2026-07-16, Wave D, wd/wizard): **Slider re-arm debounce = 220ms**
  (`calOnSliderInput` in webui/index.html). No prior art specified a number —
  chosen as comfortably above a single requestAnimationFrame-driven drag
  tick's input-event cadence but well under human perception of "the light
  followed my drag," so a fast end-to-end slider sweep across all 360 LEDs
  sends exactly one `POST /api/calibration/probe` after the drag settles
  instead of one per pixel. Verified live: 4 rapid synthetic `input` events
  dispatched in one JS tick collapsed to exactly one network POST (armed at
  the last value only).
- A168 (2026-07-16, Wave D, wd/wizard): **One shared error-panel component
  (`calErrPanelHtml`/`calWireErrPanel`/`calSetProbeError`) drives every
  probe-arm screen** (step1 anchor capture, step3 verify, fine-tune's
  "Show") — not a per-screen toast. This directly replaces two pre-existing
  bugs the audit named: the old code silently re-armed forever on a bare
  `armed:false` (read as "still waiting," no way out) and bounced any 409
  straight back to the preflight screen with no explanation. Now `timedOut`,
  `409 recording`, `409 playing`, and network-unreachable all render the same
  persistent inline panel with a Retry button that re-invokes the exact
  failed action's closure (re-arm same LED, or re-show same fine-tune key).
  Verified live against the mock for both 409 recording (panel renders "The
  device is recording — stop recording first," Retry recovers once the mock
  flag clears) and REST-level for timedOut/409 playing.
- A167 (2026-07-16, Wave D, wd/wizard): **mock_device.py's probe additively
  gained a real `timedOut` field (mirrors firmware B6d/A119) and a mock-only
  `POST /api/mock/set-recording` control endpoint** (same spirit as A153's
  `fail-uploads` — NOT part of docs/API.md, a real device has no such
  route). The Record feature itself has NO mock support at all yet (no
  `/api/record/*` routes, no `record` object in status) — building that out
  is a separate, unclaimed task; this wave only needed a way to force the
  probe-arm-while-recording 409 deterministically for its own E2E, so it
  added the narrowest possible hook rather than partially building someone
  else's feature.
- A166 (2026-07-16, Wave D, wd/wizard): **The wizard never sends an explicit
  `reversed` field in the multiPoint PUT body** — docs/API.md's multiPoint
  shape only lists `tier`/`landmarks`; `reversed` is a response-only field
  the device infers from landmark LED order (ruling §6-5). The Done screen's
  "Reversed strip" toggle (§3-D item 4) surfaces that inferred value and,
  when the user flips it, re-submits the SAME landmarks with every LED
  mirrored about the strip center (`ledCount-1-led`) rather than swapping
  just the two endpoints — this stays correct after a mismatch-fix has added
  a 3rd guide point mid-flow, and it's the general "flip the whole strip"
  operation rather than a two-point special case. Verified live: toggling
  on a 3-landmark table flipped `reversed` true→false→true server-side with
  each landmark's `led` mirrored correctly.
- A165 (2026-07-16, Wave E1, we/polish): **API.md gets a new `## Shows`
  section** documenting all 5 live routes that had shipped code but no
  contract text: `GET /api/shows` (formatVersion + bare list, mirroring
  `GET /api/songs`), `POST /api/shows` (upload, mirrors the song upload
  contract with show-specific caps — 400/409 busy/413/507 quota/507
  storage), `DELETE /api/shows/{name}`, `POST /api/shows/{name}/play`
  (404/400 parse-error/400 no-song/409 busy), `POST /api/shows/stop`. Shapes
  and error codes transcribed directly from `web_server.cpp` (not invented)
  — the doc now matches shipped behavior exactly.
- A164 (2026-07-16, Wave E1, we/polish): **`GET /api/songs`'s `parseOk`
  (queued Wave C ask, §3-E item 12) is a per-boot, in-RAM cache keyed by
  (name, size)** — `SongParseCache` (`lib/core/src/vialucis/
  song_parse_cache.h/.cpp`, native-tested in isolation, no file IO) decides
  ONLY whether a name+size pair needs re-parsing; `App::songsForList()`
  (esp32-only glue, `app.cpp`) does the actual LittleFS read + `parseMidi`
  when the cache says so. Chosen over parse-at-upload-commit-time (a sidecar
  marker file) because it needed no `song_store.cpp`/upload-path edit at all
  (additive, zero risk to the just-hardened B4 atomic-upload path) and a
  size mismatch is already a reliable "this file changed" signal — LittleFS
  writes are exact-byte and B4's atomic upload never leaves a partial-length
  file in place, so no content hash is needed. `prune()` drops entries for
  deleted/renamed songs each call so the cache can't grow unbounded across a
  device's uptime. `GET /api/songs` stays a bare array per the A111 ruling —
  `parseOk` is just one more field per item, not a shape change.
- A163 (2026-07-16, Wave E1, we/polish): **unison-note color precedence
  (§3-E item 11) — same-layer, EQUAL-peak-brightness paints on the same LED
  now BLEND (round-half-up average per channel) instead of keeping whichever
  `addDue()`/`addUpcoming()` call happened to land first.** The old tie-break
  (`frame_renderer.cpp`'s `paint()`) only overwrote on a STRICTLY brighter
  color, so an exact tie — most commonly both hands' Due color landing on a
  note struck in unison — silently kept the first call's color, which
  depended on iteration order (track order in the MIDI file, an accident of
  authoring, not a product decision). `blendRgb()` is commutative (a+b ==
  b+a), so the result is provably order-independent — the actual
  "deterministic" half of "define a deterministic color rule." Strictly-
  brighter-wins is UNCHANGED (still resolves e.g. two upcoming previews of
  the same pitch at different lead distances). Pinned:
  `test_unison_due_from_two_hands_blends_and_is_order_independent`
  (`test_frame_renderer.cpp`) asserts both the blended value and that
  swapping call order doesn't change it. No SPEC.md change — this is a
  render-layer tiebreak, not a new customizable color.
- A162 (2026-07-16, Wave E1, we/polish): **effect buffer write guards
  (§3-E item 10) added to fire2012.h, pacifica.h, twinkle_fox.h,
  color_waves.h** — each effect's per-pixel write loops bounded `ledCount_`
  (the effect's OWN idea of strip length, fixed in `reset()`) but never
  checked the CALLER-supplied `FxFrame::leds`/`std::vector<Rgb>&` buffer's
  actual size, unlike `note_driven.cpp`/`afk_player.cpp`, which already had
  this guard. Added `&& i < f.leds.size()` (or `leds.size()` for pacifica's
  helper methods, which take the vector by reference) to every such loop.
  `color_waves.h` needed an EXTRA guard beyond the loop bound: its reversed
  index (`pixelnumber = (ledCount_-1)-i`) can exceed a short buffer even
  while `i` itself stays in range, so that write is now wrapped in its own
  `if (pixelnumber < f.leds.size())`. Cheap (a size() compare per pixel,
  same pattern the two already-guarded effects use) and a no-op in every
  exercised path today (every caller sizes its buffer to `ledCount_`) — pure
  defense-in-depth per the brief. No new tests needed beyond the existing
  suite (which already exercises 60/360-LED bounds); the guard doesn't
  change behavior for any currently-possible call shape.
- A161 (2026-07-16, Wave E1, we/polish): **`idleSec` (§3-E item 9) — KEEP,
  no code change.** Investigated consumers: the webui never reads it (only
  `afkTimeoutSec`/`topMode` drive its ambient-state UI), but it costs
  NOTHING extra to emit — `ModeDirector::idleSec()` is already computed
  every status call for the AFK-arm decision the director itself needs
  (`topMode()`/idle-drift tracking), so dropping the field would save zero
  work and would be a gratuitous breaking change to a documented,
  already-shipped field for a webui-only consumer count of one. Kept for its
  diagnostic value (e.g. asking a user for a status dump when triage-ing an
  AFK-timing report — "topMode:reactive, idleSec:12, afkTimeoutSec:180"
  tells you at a glance whether the timeout SHOULD have fired yet).
- A160 (2026-07-16, Wave E1, we/polish): **`mode` omitted from statusJson
  when no song is loaded (§3-E item 8)** — `playback_engine.cpp`'s
  `mode_` member defaults to Wait regardless of whether a song is loaded, so
  a bare status poll used to report `"mode":"wait"` right alongside
  `"topMode":"reactive"`/`"afk"`, reading like practice mode were somehow
  active with nothing loaded (the exact confusion §3-E item 8 names). Now
  gated on `!songName_.empty()` — omitted entirely rather than emitting a
  value that means nothing yet. Verified safe by READING (not editing)
  `webui/index.html`: every mode-relevant control is already `disabled` when
  `!hasSong` (Wave C1, A140), and the one comparison site
  (`b.dataset.mode === s.mode`) degrades harmlessly to "no button active"
  when `s.mode` is `undefined`. Touches `playback_engine.cpp` — NOT owned by
  E1 this wave per the brief, but the brief explicitly permits a minimal-
  additive touch for this item; flagged in the final report (no sibling
  owns playback_engine this wave). Pinned:
  `test_status_json_omits_mode_with_no_song_loaded`
  (`test_playback_engine.cpp`); the existing `test_status_json_matches_
  api_contract_shape` (which DOES load a song) still requires the field
  present, unchanged. **Heads-up, not fixed (tools/ off-limits this wave):**
  `tools/mock_device.py` still unconditionally emits `"mode":"wait"`
  regardless of song state — a real device and the mock now diverge on this
  one field. ASK for whoever next touches mock_device.py.
- A159 (2026-07-16, Wave E1, we/polish): **hex color strictness (§3-E item
  7) — DESIGNED, NOT APPLIED to the tree; handed to E2 as an exact ASK
  (`settings.cpp`/`settings.h` are E2-owned this wave — off-limits per my
  brief).** The only place colors currently parse from hex strings is
  `settings.cpp`'s `hexToColor` (leftColor/rightColor/wrongColor/
  repeatColor); `show.cpp`'s PALETTES section carries raw RGB bytes, never
  hex text, so it needs no change. The bug: `hexToColor`'s
  `sscanf(s+1, "%2x%2x%2x", &r,&g,&b)` treats `%2x`'s width as a MAXIMUM per
  conversion, not a fixed count — a 5-hex-digit body like `"#12345"` still
  satisfies all three conversions (the lone trailing digit read as the
  entire blue channel) and anything past the 6th hex digit (e.g.
  `"#0000000000"`) is silently ignored — both land a "valid"-looking color
  from a malformed body instead of rejecting it. Fix: replace with manual
  digit-by-digit validation requiring EXACTLY `#` + 6 hex digits, nothing
  more or fewer; on rejection the field stays at its PREVIOUS value
  (`readColor`'s existing behavior, unchanged) — never zeroed. Exact patch
  (both `settings.cpp`'s `hexToColor` + a `test_malformed_hex_strings_are_
  rejected` pin for `test_settings.cpp` covering short/long/non-hex/
  missing-`#`/empty bodies, verified passing against E1's copy of the file
  before being reverted out of this branch) is in the final report for E2
  to apply on `we/ble`.
- A158 (2026-07-16, Wave E1, we/polish): **palette reset on
  `paletteRef==0xFF` (§3-E item 6) — new `Effect::resetPalette()` virtual
  (default no-op, mirroring `setPalette`'s own default), overridden in every
  palette-aware effect (`TwinkleFoxFx`, `ColorWavesFx`, `NoteDriven`) to
  restore that effect's OWN hardcoded default (`oceanColors()`,
  `rainbowColors()`, `rainbowColors()` respectively — the values each
  already used as its pre-`setPalette()` default).** `ShowPlayer::renderAt`
  now calls `e->resetPalette()` on the `paletteForCue()==nullptr` branch
  (paletteRef 0xFF = "effect default") instead of doing nothing — the
  show_player state-bleed bug: a cue sharing an effectIndex with an EARLIER
  cue that DID set a palette would silently inherit that palette forever,
  since "effect default" was never actually re-armed. Pinned at two levels:
  effect-level (`test_twinklefox_reset_palette_restores_default`,
  `test_color_waves_reset_palette_restores_default` in
  `test_fx_effects.cpp` — resetPalette() after setPalette() produces
  byte-identical output to a never-touched instance) and show_player-level
  (`test_palette_resets_to_effect_default_when_ref_is_0xff` in
  `test_show_player.cpp` — an all-red custom palette cue followed by a
  paletteRef:0xFF cue must show non-red channels again). `fire2012.h`/
  `pacifica.h` need no override (no `setPalette()` support — no-op default
  is correct for both).
- A157 (2026-07-16, Wave E1, we/polish): **`formatVersion` in `GET
  /api/shows` now reads `Show::kVersionMajor` instead of the hand-copied
  literal `1`** (§3-E item 5) — one line in `web_server.cpp`; the two values
  happened to already agree, so this is a drift-prevention fix, not a
  behavior change. No test needed (already covered by the existing shape;
  a future SHOW-FORMAT major-version bump now can't silently desync the two).
- A156 (2026-07-16, Wave E1, we/polish): **CORS added to `POST
  /api/reboot`** (§3-E item 4) — it was the one route replying via a bare
  `req->send(...)` instead of the shared `sendJson()` helper, so it never
  got the `Access-Control-Allow-Origin`/private-network headers every other
  route has. Now routed through `sendJson(req, 200, "{}")`, matching the
  posture P2's `addCors()` established everywhere else (the off-device
  editor and any LAN page talking to `/api/*` from `Origin: null`).
- A155 (2026-07-16, Wave E1, we/polish): **405-for-wrong-method (§3-E item
  3, explicitly "optional") — SKIPPED.** ESPAsyncWebServer resolves
  unmatched-method requests by falling through to whatever ELSE matches the
  URI (nothing here) and finally to `onNotFound` (today's 404) — there is no
  cheap, centralized way to answer 405 without registering an explicit
  extra handler per (path, wrong-method) pair for every route in the table.
  Given R1 (route shadowing) was JUST closed this same bugfix arc by
  switching five parents to exact matchers, adding a second wave of
  per-path method registrations reopens exactly the kind of routing-surface
  risk that bug lived in, for a status-code nicety with no functional
  benefit to this API's one real client (the webui, which never sends a
  wrong method). Skipped per the brief's explicit carve-out.
- A154 (2026-07-16, Wave E1, we/polish): **Load-nonexistent-song → 404
  (§3-E item 2) via a new pure decision seam, `vialucis::classifySongLoad`
  (`lib/core/src/vialucis/song_load.h`), native-tested
  (`test_song_load.cpp`) even though the glue that gathers its two inputs
  (`App::loadSong`, `app.cpp`) is esp32-only (LittleFS read + `parseMidi`,
  can't run under `native`).** Before: any load failure — a typo'd filename
  OR a genuinely corrupt file — answered the same generic `400 "cannot load
  song"`. Now: `SongLoadOutcome::NotFound` (`404 "no such song"`) when the
  store read fails, `ParseError` (`400 "cannot load song"`, unchanged text)
  only when the file exists but `parseMidi` rejects it. `App::loadSong`'s
  return type changed from `bool` to `SongLoadOutcome` — safe: no native
  test calls `App::loadSong` (it's esp32-only, `app.h` isn't included by
  any test), and `web_server.cpp` is the only REST caller.
- A153 (2026-07-16, Wave L2, wl2/library): **mock_device.py gets two additive
  fixes beyond the strict T5/T6 ask, same spirit as A146.** (1)
  `POST /api/songs/{name}/rename` was completely unhandled (fell through to
  404) despite the webui's `renameSong()` already calling it — fixed to mirror
  API.md (400 bad/non-.mid name, 404 missing source, 409 exists), since an
  honest mock shouldn't 404 a route production code already depends on. (2)
  Added a mock-only `POST /api/mock/fail-uploads` control endpoint (body
  `{"times": N}`) that makes the next N song uploads drop the connection with
  no HTTP reply at all — a real network-level failure, not a typed error
  status — so T5's retry-on-network-error path has something deterministic to
  exercise. Documented in the mock's own docstring as NOT part of
  docs/API.md; a real device has no such route.
- A152 (2026-07-16, Wave L2, wl2/library): **T6 bulk-manage extends the
  existing Songs screen rather than adding a new page** — the file already had
  exactly three nav pages (Play/Songs/Settings) and an existing-but-dormant
  storage gauge card on the Songs screen (fed by fsFree/fsTotal, previously
  invisible because no mock ever supplied those fields) — bulk upload and
  bulk delete are just more affordances on the screen that already owns
  "songs," not a new concept needing its own page. Multi-select delete is a
  toggled "Select" mode (checkboxes appear, single-song Load/Rename/Delete
  buttons hide) rather than always-on checkboxes, so the common single-song
  path stays exactly as uncluttered as before. Bulk delete on a
  currently-loaded song's 409 is reported per-file ("currently loaded —
  unload it first") rather than auto-unloading — no bulk action silently
  interrupts playback, extending the wifiPass-clear precedent (A145: explicit
  user action only, never an automatic side effect) to this destructive path.
- A151 (2026-07-16, Wave L2, wl2/library): **T5's retry policy is 3 whole-file
  attempts with a 1.5s × attempt-number backoff, matching tools/bulk_upload.py
  bit for bit** (that script is the only prior art for this exact contract).
  `507`/`409` are typed, durable device refusals and are never retried
  (surfaced immediately as `failed`); every other failure — network-level
  drop, any other HTTP status, or a post-write verify mismatch — retries
  under the same backoff. Verify-after-write re-`GET`s `/api/songs` and
  checks name+size; a mismatch is treated exactly like a failed attempt
  (retried, not silently accepted), because a `201` reply is not proof the
  bytes landed correctly. This policy is now the single documented contract
  (docs/API.md "Upload contract") that webui, bulk_upload.py, and any future
  Phase 2 sync agent should all implement identically.
- A150 (2026-07-16, Wave L2, wl2/library): **The T6 free-space margin guard
  mirrors `firmware/lib/core/src/vialucis/storage_budget.h`'s `uploadFits`
  math byte-for-byte in webui JS** (`roundUpToBlock`/`clientUploadFits` in
  index.html): round the file's size up to 4 KB LittleFS blocks, add the
  same 32 KB `kUploadReserveBytes` reserve, refuse to even attempt the
  upload if that exceeds the freshest `GET /api/status` `fsFree` reading.
  Chose to re-fetch `/api/status` immediately before EACH file in a bulk
  queue (rather than maintaining a running client-side estimate across the
  batch) so a concurrent actor (someone using the device's own UI, or a
  slow-draining previous upload in the same queue) can't make the guard
  stale — same "never trust a cached view" principle DESIGN-library.md §4
  applies to the PC-shelf sync agent, applied here to a single browser
  session. A failed status fetch doesn't block the attempt (falls back to
  trying the upload) — the device's own 507 precheck remains the final
  authority either way, so a guard that can't be evaluated should never wedge
  the whole bulk flow.
- A149 (2026-07-16, Wave L2, wl2/library): **mock_device.py's own R1-style
  bug, found while building T5/T6: `self.path == "/api/songs"` never matches
  `POST /api/songs?name=foo.mid`** (the query string breaks exact-string
  route matching), so every song upload silently 404'd — the mirror image of
  the real firmware's route-SHADOWING bug (a prefix match swallowing a
  child), except here it's a query string breaking an exact match into a
  false negative. Fixed by splitting path from query EVERYWHERE in the mock
  (`Handler._split`) and routing on path only. While rewriting the upload
  handler, made it a REAL implementation instead of the old hardcoded
  `{"name": "uploaded.mid"}` stub: mirrors `web_server.cpp`'s exact
  validation order (missing name -> bad name -> too-large -> free-space
  507 -> loaded-name 409 -> write), using the SAME storage_budget.h math
  server-side too. Also added real fsFree/fsTotal/fsUsed to `GET
  /api/status` (previously absent, which is why the webui's existing A5
  storage-gauge card was invisible against the mock) with `fsTotal` set to
  the ACTUAL post-A1 partition size (`0x1E0000` / 1,966,080 bytes, per A121)
  rather than an arbitrary mock number, and `fsUsed` computed from
  block-rounded song+show sizes so it genuinely depletes as songs upload —
  giving T6's capacity bar and margin guard, and T5's retry path (see A150/
  A151), something real to react to end to end.
- A148 (2026-07-16, Wave C, wc/settings C4/Views): **`Settings::toJson(View)`
  defaults to `View::Persist`; outward call-sites pass `View::Public`
  explicitly.** Considered the "safe-by-default" inverse (default Public, opt in
  to Persist), but the Persist path is the load-bearing one — persisting WITHOUT
  `wifiPass` silently breaks WiFi reconnect after a reboot, and it lives in the
  conditionally-owned `song_store.cpp` (`saveSettings` calls the default
  `toJson()`). Defaulting to Persist keeps that path byte-identical and untouched
  (zero song_store edit), while the ONLY two serializations that leave the
  device — GET /api/settings and the PUT echo, both in web_server.cpp — opt into
  Public. The known outward path is guarded by the A147 pin
  (`test_public_view_never_contains_wifipass`) and a Public-view contract pin, so
  a future outward call-site that forgets `View::Public` is the residual risk;
  it's caught by review + the field-set contract test, not the type system.
- A147 (2026-07-16, Wave C, wc/settings C4/Views): **`wifiPass` is a write-only
  field whose redaction lives INSIDE `Settings` (ruling §6-1), not the route
  handler.** `Settings::toJson(View::Public)` — used by every serialization that
  LEAVES the device (GET /api/settings, the PUT echo, and the webui "Export",
  which is just that GET) — omits `wifiPass` entirely and emits
  `wifiPassSet: true|false` (true ⇔ a non-empty password is stored).
  `View::Persist` (flash only) keeps the full doc so a reboot reconnects. Closes
  the cleartext-password leak (any LAN/AP client could read the home WiFi
  password via GET, and Export wrote it to disk). CLEAR affordance: `fromJson`
  keeps PATCH semantics — a body WITH `wifiPass` sets it, WITHOUT it leaves the
  stored password unchanged, and an explicit `"wifiPass": ""` CLEARS it (least
  surprising: sending the field means you're setting the field). This makes the
  redacted GET→PUT round-trip lossless ONLY when the client omits the key it
  never received — flagged to the C3 webui sibling: the password input must not
  resend `""` on save unless the user is deliberately forgetting the network.
  Native pins: `test_public_view_never_contains_wifipass` (the leak-regression
  pin — key-level, not substring), `test_persist_view_carries_wifipass`,
  `test_wifipass_write_only_patch_and_clear`, `test_public_view_contract_field_names`.
- A146 (2026-07-16, Wave C, wc/webui C2): mock_device.py additive fixes needed
  to test C2 at all — `DELETE /api/songs/{name}` was previously unhandled
  (fell through to a blanket `204` that never mutated the `songs` list and
  never enforced the "song is loaded" 409), so the unload-then-delete flow
  had nothing real to exercise; added a proper handler. Also added one
  `parseOk: false` test song (plus a `parseOk: true` one) so the C2 badge
  feature-detect has real data to light up against, since no firmware field
  exists yet (see report's ASKS FOR C-LEAD).
- A145 (2026-07-16, Wave C, wc/webui C4): wifiPass write-only UI built
  against the contract the dispatcher relayed mid-task from wc/settings
  (`GET`/`PUT` reply `wifiPassSet:true|false`, no literal `wifiPass`; PUT
  PATCH semantics — key absent leaves it unchanged, key present incl. `""`
  sets/clears it). Per the dispatcher's explicit ruling, clearing is a
  dedicated "Clear stored password" button + confirm — NEVER an automatic
  consequence of an untouched/blanked field — because the field is always
  rendered blank (write-only), so a naive load→edit-something-else→save
  would otherwise silently wipe the stored password. A `wifiPassTouched`
  flag (set on `input`, read on `change`) means the `wifiPass` key is only
  ever included in a PUT body when the user actually typed a replacement
  this session. `settingsSnapshot` (used for the C1 revert-on-error policy)
  is scrubbed of any `wifiPass` key before being retained, so the secret
  never persists in JS state even transiently, however it arrived on the
  wire (old-firmware fallback included).
- A144 (2026-07-16, Wave C, wc/webui C3): settings input clamps realigned to
  the actual firmware bounds in `settings.h`/`settings.cpp` (read-only,
  not touched) — `offsetMm` ±2000mm (was unbounded), `ledsPerMeter` 10-1000
  (was min=1, no max), `leadMs` max 10000 (was 3000), `recordBudgetKB` max
  256 (was 1024 — the old max was a real bug: firmware clamps to the 256KB
  per-song save ceiling, so 1024 was unsaveable and misleading), `afkDwell`
  max widened to 1440 min (dwellSec's real 86400s ceiling). Added: symmetric
  wrongColor↔repeatColor collision toast (firmware already rejects the
  collision from either field; the UI previously only warned one direction);
  a NaN/invalid-numeric guard on every `[data-key]` number input — blocks
  the send, marks the field (`.field-err`), toasts, and reverts to the last-
  known-good value, instead of letting `JSON.stringify(NaN)->null` sail
  through and get silently dropped by the firmware's typed guard; a guard on
  the header WiFi chip so command replies that omit `wifi` (most non-status
  writes, per API.md) leave the chip alone instead of blanking it; subtext
  under all three brightness-shaped controls (previewCap %, strip
  brightness 1-255 raw, ambient cap 0-255 raw) naming their distinct
  units/backends.
- A143 (2026-07-16, Wave C, wc/webui C2): songs screen — badge rendering for
  `song.parseOk === false` is feature-detected (absent/true/unknown never
  shows it; see report's ASKS FOR C-LEAD for the requested shape); a 409 on
  DELETE (song is loaded) now offers a one-click "unload it and delete?"
  confirm instead of a dead end; a failed `GET /api/songs` now renders an
  error message + Retry button instead of leaving the "Loading…" placeholder
  forever.
- A142 (2026-07-16, Wave C, wc/webui C1): unified slider error policy across
  the app — every slider/field that drives a live device write (seek,
  tempo, the ambient brightness cap, and every `/api/settings` numeric
  field) reverts its displayed value to the last-known-good server value
  and toasts on failure, applied at the commit moment (release/change, or a
  debounce flush) rather than on every intermediate drag tick — an
  in-flight throttled send failing silently mid-drag is fine because the
  release/flush always re-issues the authoritative value and is the one
  that surfaces the error.
- A141 (2026-07-16, Wave C, wc/webui C1): loop staging now tracks per-field
  `loopDirty` flags (start/end/enabled) set the moment the user edits or
  taps "Set from here", cleared only once a PUT actually commits — closes
  the race where a 500ms status poll landing mid-flight (or before the user
  even sends, e.g. staging while Loop is off) stomped the staged value back
  to the device's still-old loop config.
- A140 (2026-07-16, Wave C, wc/webui C1): tempo/loop/mode/practice controls
  are now `disabled` (not just error-toasting on click) whenever no song is
  loaded, mirroring the pattern the transport buttons already used.
- A139 (2026-07-16, Wave C, wc/webui C1): the practice-hand selector now
  reconciles from the live `practice` field in `GET /api/status` (landed
  Wave B-i) instead of a client-only default — closes the bug where any
  mode tap after a page reload silently reset the device's practice hand
  back to "both" because the client had forgotten what the device actually
  had armed.
- A138 (2026-07-16, Wave B-ii, wbii/persist B4): **`SongStore::begin()`
  flips `formatOnFail` to FALSE (ruling §6-2), SUPERSEDING A108.** A boot must
  never wipe real user data as a reflex. A138's safety net for the replicability
  regression A108 feared (a fresh device stranded in MountFailed) is threefold:
  (1) the web UI is served from the firmware image, NOT LittleFS, so a
  mount-failed device still serves the recovery UI; (2) the guarded
  `POST /api/storage/format` initialises the empty FS in one click (documented
  in docs/API.md as the expected first-run step); (3) boot config self-heal
  (A135) turns a corrupt doc into defaults, not a wipe. The Wedged signal is
  still classified. First-boot format path documented in API.md "Storage".
- A137 (2026-07-16, Wave B-ii, wbii/persist B4): **Atomic-persist seam =
  pure template `atomicPersist()` in `lib/core/atomic_store.h` (stage to a
  `.tmp`, rename over target, remove tmp on any failure) over injectable FS
  primitives, so the decision path is native-tested (test_atomic_store) while
  only the LittleFS syscalls stay esp32-only.** Applied to ALL persisted writes:
  the three config docs (via `writeTextFile`), the record-take `save()`, and —
  going beyond the config docs — SONG/SHOW uploads (openUpload now opens the
  tmp; `commitUpload`/`commitShowUpload` rename on the last chunk;
  `abort*Upload` discards on disconnect/write-fail). The upload change closes a
  real data-loss hole: overwriting an existing non-loaded song used to truncate
  it in place at the first chunk, so a failed/interrupted overwrite DESTROYED
  the old good file. `list()`/`listShows()` filter `*.tmp`; `begin()` sweeps
  crash-orphaned temps. `.tmp` is deliberately not a valid song/show name.
- A136 (2026-07-16, Wave B-ii, wbii/persist B4): **Schema versioning lives at
  the SongStore FILE layer, not in each doc's parser.** `writeTextFile` stamps
  `"schema":kConfigSchema` (config_schema.cpp, ArduinoJson, native-tested);
  `readTextFile` rejects an unknown-HIGHER schema as corrupt and tolerates an
  absent one (pre-B4 file). Chosen over emitting schema inside
  `Settings::toJson`/`Calibration::toJson` because (a) it keeps the GET/PUT wire
  shapes byte-identical (no `schema` on the API — lower contract risk), (b) it
  covers afk.json WITHOUT editing the sibling-owned `fx::afkConfig*` serializer,
  and (c) all three docs' parsers already ignore unknown keys, so a leftover
  `schema` field is harmless. The pure `schemaAccepted()` decision is tested in
  test_config_schema.
- A135 (2026-07-16, Wave B-ii, wbii/persist B4): **Boot self-heal decision =
  pure `decideSelfHeal(DocLoad, parsedOk)` in `lib/core/config_boot.h`
  (native-tested).** ABSENT is the first-boot / v1 upgrade path — silent
  defaults, NO reset flag (nothing was lost; for calibration, the settings'
  2-point fallback stays byte-identical to v1). CORRUPT (unreadable,
  unknown-higher schema, or unparseable) or an Ok-read-but-unparseable doc is a
  REAL doc gone bad → defaults + atomic re-save + raise `configReset_`. The
  loaders now return a tri-state `DocLoad` (was bool) so App::begin can tell
  absent from corrupt.
- A134 (2026-07-16, Wave B-ii, wbii/persist B4): **Save failures propagate to
  507; the config still applies LIVE first.** `applySettings` returns bool;
  `applyCalibration`/`applyAfk` gained a `bool* saveFailed` out-param (additive,
  default nullptr). The handlers answer `507 "insufficient storage"` when the
  atomic write fails instead of the old lying 200. Deliberate ordering: the
  in-RAM apply happens before the persist attempt, so a full FS doesn't block a
  live settings/calibration/ambient change — it just warns it isn't durable.
- A133 (2026-07-16, Wave B-ii, wbii/persist B4): **`configReset` surfacing in
  statusJson is BLOCKED on a `DeviceStatus.configReset` field in
  `playback_engine.h` — B7-owned, which my brief forbids me to touch, and which
  B7's finished branch did NOT add.** The App-side half is COMPLETE: self-heal
  computes `configReset_`, `App::configWasReset()` exposes it, and API.md
  documents the field. Since statusJson is single-authored by the engine (R4
  forbids splicing) and every status struct (DeviceStatus/RecordStatus/…) lives
  in playback_engine.h, there is no owned seam to emit it through. Exact 3-line
  patch (struct field + `doc["configReset"]` emit + the app.cpp populate)
  handed to the dispatcher in the final report. Same blocker applies to B5's
  `overflowed` RecordStatus field (its value is already available on my branch
  via `director_.recordStatus() == CaptureStatus::Overflowed`). [Merge note,
  same day: the dispatcher applied the handed-off patch on the merged tree —
  `DeviceStatus.configReset` + `RecordStatus.overflowed` struct fields, both
  emits in playback_engine.cpp, both populates in app.cpp statusJson — so
  BOTH fields are LIVE as documented in API.md; this blocker is resolved.]
- A132 (2026-07-16, Wave B-ii, wbii/persist B4, B5 ask 3): **`recordArm`
  REFUSES with `409 "unsaved take pending"` while `pendingSave_.held()`**
  (new `RecordArm::PendingUnsaved`), rather than silently dropping the retained
  failed take — chosen per B5's recommendation because arming would re-create
  the exact "the performance is destroyed" hazard retry-save exists to prevent.
  `recordDiscard` also drops a held take (so the user can bail), and
  `POST /api/record/retry-save` re-attempts the save from `pendingSave_`.
  `recordBudgetKB` upper clamp lowered 1024→256 (B5 ask 4: 256 KB = the
  per-song save ceiling `kMaxSongBytes`, so a bigger take was unsaveable);
  test_settings updated. pendingSave_ is HTTP-task-owned (all record routes run
  on async_tcp), read unfenced like store_.
- A131 (2026-07-16, Wave B-ii, wbii/record B5): **`MidiCapture::onPedal` now
  starts the clock on a pedal-DOWN (value > 0) while merely Armed** — mirrors
  `onNoteOn`'s "first real event anchors t=0" rule; a pedal-UP (value 0) while
  Armed still has nothing to open and stays dropped. Fixes the reported bug
  (opening sustain lost); `midi_capture.cpp` only, no ModeDirector change
  needed — it already forwards `onPedal` straight through, gate-free.
- A130 (2026-07-16, Wave B-ii, wbii/record B5): **`MidiCapture::arm` clamps
  the requested budget to a new `kMaxRecordBudgetBytes = 256 * 1024`**
  (`midi_capture.h`), matching `SongStore::kMaxSongBytes` (firmware/src,
  duplicated not shared — lib/core can't include firmware/src). Settings
  still allows `recordBudgetKB` up to 1024 (`settings.cpp`, not mine to
  touch); the core clamp is the real safety net so an over-budget take can
  never become unsaveable by construction regardless of what the settings
  layer permits. ASK filed for B4 to optionally tighten settings.cpp's clamp
  to match (better UX: reject at the settings layer with a clear message
  instead of a silent deep clamp) — not required for correctness.
- A129 (2026-07-16, Wave B-ii, wbii/record B5): **Added `PendingSave`
  (`record_take.h`), a pure state holder that keeps a `CaptureTake` alive
  after a failed save** so a retry-save can reuse it instead of the take
  being destroyed. It only holds state — App still owns the actual
  hand-split/writeSmf/LittleFS I/O on each attempt (mine to build; the App
  wiring + REST route are ASKs, since `app.cpp`/`app.h`/`web_server.cpp` are
  outside my ownership). `hold()` always replaces any prior held take (one
  take in flight, matching the rest of record mode).
- A128 (2026-07-16, Wave B-ii, wbii/record B5): **Added `takeWasTruncated()`
  (`record_take.h`)** — a one-line named predicate (`take.status ==
  CaptureStatus::Overflowed`) so App's `recordStop()`/retry-save don't inline
  the enum compare at each call site when deciding `Saved` vs
  `SavedTruncated`. The underlying `CaptureStatus::Overflowed` signal and
  `ModeDirector::recordStatus()` accessor already existed pre-B5 (built in
  the earlier Wave B-i `wbi/director`/`wbi/engine` merges) — B5's job was
  wiring it into a stop-result distinction, which is an ASK (touches
  `app.h`/`app.cpp`/`web_server.cpp`, none mine) plus surfacing `overflowed`
  in `/api/status`'s `record` object (touches `playback_engine.h/.cpp`, also
  not mine).
- A127 (2026-07-16, Wave B-ii, wbii/runtime B7): **Wrong-flash entries are
  dropped for every note in a newly-loaded chord's `pendingNotes()`, inside
  `PlaybackEngine::tick()`'s `wait_->update()` branch** — not in `onKeyDown`.
  `WaitMode::onKeyDown` only ever reports `Wrong` for a note NOT currently
  pending, so a flash can only go stale across exactly a chord *transition*
  (never within one chord's lifetime); the single cleanup site at the
  transition edge is therefore complete, with no dead/untestable code at the
  `onKeyDown` Cleared branch. Also folded in: `wait_->update()==true` now
  unconditionally sets `frameDirty_` (previously only when `repeatCue_` was
  enabled AND `reDueKeys()` was non-empty) — a chord transition is always a
  visual change (new due chord, possibly a just-dropped stale flash), so it
  renders on the very next tick instead of waiting up to one ~16.7ms frame
  period. Minor, deliberate behavior delta (not zero-change); logged per the
  project's convention for such deltas (cf. A32).
- A126 (2026-07-16, Wave B-ii, wbii/runtime B7): **`pending_`/`cleared_`
  (`WaitMode`, reserved in its constructor) and `wrongFlashes_`
  (`PlaybackEngine`, reserved in its constructor) get `reserve(16)`** —
  `kMaxChordNotes` in wait_mode.h, `kMaxSimultaneousWrongFlashes` in
  playback_engine.cpp. 16 = generous headroom over a two-hand chord (~10
  fingers), not a hard cap — a pathological song can still grow past it
  (correctness over memory). Added `WaitMode::clearedNotes()` (public,
  mirrors the existing `pendingNotes()`) so native tests can observe
  capacity; no such accessor was added for `wrongFlashes_` (private, no
  existing test-facing precedent) — its capacity test is skipped per the
  brief's "where observable" carve-out.
- A125 (2026-07-16, Wave B-ii, wbii/runtime B7): **Reboot moves off the
  async_tcp task via a NEW header-only seam, `firmware/src/reboot_request.h`
  (`RebootRequest::pending`/`requestedAtMs`, inline C++17 statics)** — not a
  member on `App` (app.h is B4-owned this wave). `main.cpp`'s `loop()` is the
  ONLY consumer (polls the flag, honors the old 200ms grace delay, then
  `ESP.restart()`); the setter half (web_server.cpp's `/api/reboot` handler
  replying immediately instead of `delay(200)`) is an ASK for B4 — my tree
  compiles either way since the old handler still compiles unchanged against
  the new header (the header is simply unused until B4 wires it in).
- A124 (2026-07-16, Wave B-ii, wbii/runtime B7): **`LedOutput::show(frame)`
  split into `setFrame(frame)` (copy into the `gLeds` shadow buffer — cheap,
  fence-safe) + `show()` (the ~10.8ms `FastLED.show()` bit-bang, must run
  OUTSIDE any cross-task fence) — the OLD combined `show(frame)` overload is
  KEPT (calls `setFrame`+`show` internally) so `app.cpp`'s existing call site
  still compiles untouched until B4 applies the ASK** (App::tick restructured
  to snapshot+setFrame under the fence, release it, then call the bare
  show()). No new snapshot buffer needed in App — `gLeds` (already owned by
  led_output.cpp, sized once to `kLedCount`) IS the snapshot; the copy in
  `setFrame` is the only "snapshot" operation, and it's the existing
  per-pixel loop, not a new allocation. Residual, accepted micro-race noted
  for B4: `LedOutput::setBrightness()` (called from `App::applySettings`,
  itself fenced) can now interleave with an unfenced `show()` on the loop
  task — a single-byte FastLED brightness-scale write/read, not the power
  cap itself (untouched, set once in `begin()`), so left as a documented,
  practically-benign timing note rather than adding a second lock.
- A123 (2026-07-16, Wave B-ii, wbii/runtime B7): **`main.cpp`'s `loop()` gets
  an unconditional `delay(1)` at the end of every iteration** (task-watchdog
  risk: arduino-esp32's `loopTask` calls `loop()` in a tight `for(;;)` with no
  inherent yield, so a `loop()` that never blocks can starve the idle task
  that feeds the watchdog). 1ms is imperceptible against the ~16.7ms LED
  frame period and the BLE/HTTP tasks' own cadence — placed after the reboot-
  flag check so a pending reboot isn't delayed by it.
- A122 (2026-07-16, Wave B-ii, wbii/partition T3): **Migrating an existing
  device from `huge_app.csv` to `partitions.csv` needs an explicit full-chip
  erase (`pio run -t erase`) before the next upload, not just a plain
  re-upload.** Researched rather than assumed: PlatformIO's Arduino-framework
  upload flow (`framework-arduinoespressif32/tools/platformio-build.py`)
  already reflashes bootloader.bin/partitions.bin/boot_app0.bin/firmware.bin
  fresh on every upload regardless of table, and `nvs`/`otadata`/`app0`'s
  offsets are unchanged between the two tables — so those partitions are
  never at risk. The new `spiffs` (LittleFS) partition, though, now starts at
  `0x210000`, a byte range that used to sit inside the OLD 3 MB `app0`
  partition (whatever was last flashed there, not guaranteed erased/blank).
  `SongStore::begin()` calls `LittleFS.begin(/*formatOnFail=*/true)`
  (`song_store.cpp:25`) with no explicit partition offset, so it would very
  likely fail to find a valid superblock at that stale offset and
  auto-reformat anyway — but "likely" isn't "guaranteed," and an unlucky
  stale byte pattern parsing as a corrupt-but-mountable filesystem is a worse
  failure mode than a clean, deliberate wipe. Documented the erase step as
  mandatory (not conditional/best-effort) in BUILD-GUIDE.md §2d for exactly
  this reason. [Merge note, same day: B4's A138 flipped `formatOnFail` to
  FALSE in this same wave, so the "would auto-reformat anyway" fallback no
  longer exists at all — after the migration erase, the fresh partition sits
  in MountFailed until the one-time explicit format; §2d was amended at merge
  with that format step (bulk_upload.py --format or POST /api/storage/format).]
- A121 (2026-07-16, Wave B-ii, wbii/partition T3): **New `spiffs`
  (LittleFS) partition sized at exactly 0x1E0000 (1920 KiB / ~1.9 MB
  decimal), `app0` shrunk to exactly 0x200000 (2 MB), by construction rather
  than by picking the LittleFS size directly.** Kept `nvs`/`otadata`
  unchanged (offsets/sizes identical to `huge_app.csv`) and kept `coredump`
  at its stock `0x3F0000`/`0x10000`, per the brief's "keep these
  conventional." With app0 given exactly 2 MB (current firmware measured at
  1,536,109 B = 73.2% of 2 MB — comfortable headroom, no need to step down
  to 1.8 MB), the remaining gap between app0's end (`0x210000`) and
  coredump's start (`0x3F0000`) falls out to 0x1E0000 automatically — it
  happens to land almost exactly on the ~1.9 MB target from
  DESIGN-library.md §3 item A1, so no further tuning was needed.
- A120 (2026-07-16, Wave B-i, wbi/director B1a): **Test-pattern orphan timeout =
  5 minutes (`kTestPatternTimeoutMs = 300000` in mode_director.h).** Long enough
  to physically inspect the strip end to end, short enough that a vanished
  client can't hide-and-pause practice indefinitely; also auto-clears on song
  load, transport entering Playing, and show start (edge-triggered). Renumbered
  from the builder's in-code A113 at merge (number collision across parallel
  Wave B-i branches).
- A119 (2026-07-15, Wave B lead, wbi/calib B6c/B6d): **The probe-arm-while-
  recording refusal (B6c) and `timedOut` doc field (B6d) live in
  `mode_director.cpp`/`.h`, a sibling-owned core module — I did NOT make
  either edit.** `ModeDirector::armProbe`/`probeJson` are the only place that
  holds BOTH `capture_` (MidiCapture) and `probe_` (CalibrationProbe); my
  owned `calibration_probe.h` has no visibility into capture state, so the
  refusal can't live there. I DID add the `CalibrationProbe::timedOut()`
  primitive (B6d's actual state, with its own native test file
  `test_calibration_probe`) since that's squarely mine; wiring it into
  `probeJson()`'s output doc is the one line the lead needs to land. Exact
  patches for both in the final report. (Applied by the dispatcher at merge.)
- A118 (2026-07-15, Wave B lead, wbi/calib B6b): **A perKey PUT needs >= 2
  DISTINCT populated keys, checked AFTER `TableBuilder::validate()` passes,
  not before.** "Wrong-size" in the brief reads as: fewer genuinely distinct
  populated keys than needed to mean anything (0 = empty, 1 = can't even
  establish a direction, N duplicate-note entries collapsing to <2 via
  last-write-wins overwrite). Deliberately did NOT touch
  `TableBuilder::validate()` itself — a pinned characterization test
  (`test_validate_accepts_sparse_and_empty_tables`) intentionally keeps it
  accepting sparse/empty tables, which is correct when REVALIDATING an
  already-stored/self-healed doc at boot; the floor belongs only at the
  fresh-PUT boundary (`Calibration::fromJson`). Ran validate() BEFORE the
  distinct-count check (not after) so a single off-strip/inverted entry
  still reports the more specific `RangeOffStrip`/`Overlap`/etc. instead of
  a less-informative `TooFewKeys` — this ordering is what the existing
  pinned `test_table_errors_propagate_with_detail` (single off-strip entry)
  required; verified "all-same-LED" is already caught as `Overlap` by
  validate()'s existing pairwise check, so no separate case needed for it.
- A117 (2026-07-15, Wave B lead, wbi/calib B6a): **perKey's `reversed` is
  ALWAYS overwritten by the table's inferred direction, never left as
  whatever the body sent (or defaulted to).** Mirrors multiPoint's existing
  landmark-order inference (§6-5) exactly, just computed from the table's
  own first-two-valid-entries LED order instead of landmark order (both are
  well-defined only because `TableBuilder::validate()` already guarantees a
  single consistent direction across the whole table). This is what
  actually closes the bug: a stale/wrong `reversed` scalar riding in a
  perKey body can no longer disagree with the table it came with.
- A116 (2026-07-16, Wave B-i lead, C1-firmware): **The practice hand rides
  `TopStatus` (new `const char* practice`, default nullptr → omitted), sourced
  from `App::lastPractice_`.** The engine's own `practice_` is NOT the player's
  choice (a show hijacks the sub-mode), and the brief blessed lastPractice_ as
  the owner. Emitted as top-level `"practice"` in the top block (before wifi);
  appears in every App status reply like `topMode`. Engine-only callers passing
  no player choice omit the field. API.md documents it.
- A115 (2026-07-16, Wave B-i lead, B3a): **`setTrack`'s stuck-note flush routes
  through `NoteEmitter` (new `flushTracksOutsideMask`) and mints NO echo-guard
  credit for the flushed note-offs.** A guard credit is consumed by the next
  note-DOWN; a note-off is never a key press, so crediting it would eat a
  genuine future press of that pitch (the opposite of the iron rule). "Through
  the normal send path so the echo guard registers them" is honored as: the
  emitter (the one MIDI-out owner) stays the single send path and its `sounding_`
  bookkeeping stays consistent. Pedal latches (per-channel, not per-track) are
  left to `allOff` — a hand reassignment is not a transport halt.
- A114 (2026-07-16, Wave B-i lead, B3c): **`Scheduler::advance` gains a trailing
  `bool* wrapped = nullptr` out-param (additive) as the ONE authoritative loop-
  wrap signal; the engine's `prevPosUs_` position-compare is deleted.** A
  position compare (`newPos < prevPos`) misses a short-loop/high-tempo wrap that
  lands at/after the start after the O(1) modulo collapse — the skipped-repeat-
  cue bug. Default nullptr keeps every existing call-site (tests, score-follower,
  the by-value overload) source-compatible.
- A113 (2026-07-16, Wave B-i lead, B3b): **Replaying from `Finished` clears echo
  credits** (`guard_.clearCredits()` in `transport("play")`'s Finished branch),
  parity with seek's A-3/G12 clear — the just-ended pass's in-flight credits must
  not swallow the player's first real press at the re-approached opening barrier.
- A112 (2026-07-15, Wave A lead, wa/afk integration): **App::applyAfk seeds the
  parse with the CURRENT config, not a fresh default.** The wa/afk builder made
  `afkConfigFromJson` PATCH into the caller's `out`; a partial `PUT /api/afk`
  body would otherwise reset every unnamed key. Seeds from `afkJson()` (always
  fully populated) before overlaying the request. Boot (app.cpp:~60) stays
  default-seeded — correct there. My call-site edit only; wa/afk merges after.
- A111 (2026-07-15, Wave A lead, T4 — coordinator ruling): **T4 satisfied via
  GET /api/status ONLY; GET /api/songs stays a BARE ARRAY.** I first wrapped
  /api/songs as `{songs:[...],fsFree,fsTotal}`, but the webui builder flagged
  that as a breaking shape change for existing consumers. Reverted per ruling —
  the capacity primitive (fsFree/fsTotal/fsUsed) lives in /api/status, which the
  library gauge reads alongside the unchanged songs list. API.md documents this.
- A110 (2026-07-15, Wave A lead, A3): **/api/status device telemetry added via a
  new `DeviceStatus*` param on `PlaybackEngine::statusJson` (lib/core), mirroring
  the WifiStatus/TopStatus/RecordStatus pattern.** The engine authors the status
  doc ONCE (R4 forbids serialize→splice→reserialize) and owns key order, so the
  only non-hack way to add fs*/heap*/uptime fields before `wifi` is a fourth
  optional pointer arg (default nullptr — every existing caller unaffected).
  Touches one lib/core file beyond firmware/src — **flagged for Wave B3 (engine)
  to rebase.** Fields ride ONLY on GET /api/status (the wifi-bearing call);
  gathered UNFENCED in App (store_ is HTTP-task-owned) so ticks never wait behind
  LittleFS stat calls.
- A109 (2026-07-15, Wave A lead, ruling §6-3): **`App::loadedSongName()` is
  backed by an App-tracked `loadedName_` mirror, not a new PlaybackEngine
  getter.** Set on a successful `loadSong` and cleared on `unloadSong` (the only
  two writers of the engine's own `songName_`), so the two stay in lockstep.
  Keeps the accessor entirely inside firmware/src (owned) rather than editing the
  engine's public surface (Wave B territory), and kills the DELETE handler's
  statusJson serialize/reparse hack. Both the DELETE guard and the upload 409
  first-chunk check ask it.
- A108 (2026-07-15, Wave A lead, A3): **Boot `formatOnFail` left TRUE; FsHealth
  wired; format runs on the loop task.** §6-2 rules formatOnFail:false, but
  flipping it at boot would strand a fresh device (empty/unformatted LittleFS) in
  MountFailed until a manual format — a replicability-iron-rule regression. Per
  §3 that boot-policy flip belongs to **B4 (paired with self-heal)**; A3 delivers
  the seam: the full `FsHealth{Mounted,MountFailed,Wedged}` enum + `classifyFsHealth`
  + the guarded `/api/storage/format` recovery. The live signal that mattered in
  the incident — **Wedged** (mounted but a new-file create fails) — is surfaced
  regardless (openUpload flips it on a create failure). `LittleFS.format()` blocks
  for seconds, so `/api/storage/format` replies immediately and App runs the wipe
  from `tick()` (loop task) via an atomic one-shot flag — never on async_tcp.
- A107 (2026-07-15, Wave A lead, A2): **Upload reserve = 32 KB (own constant
  `kUploadReserveBytes`); record's 8 KB margin left untouched.** The block-aware
  precheck rounds the announced size up to 4 KB LittleFS blocks and requires a
  32 KB (8-block) free reserve so a dir-metadata split always has a spare block
  (the wedge cause). Did NOT lower/raise record's `kRecordSpaceMarginBytes`
  (Wave B5 territory; record already reserves its whole budget + 8 KB, a much
  larger effective floor). Also added the SAME absolute free-space guard to SHOW
  uploads (defensive — shows share the partition; refuse rather than risk the
  wedge) on top of the net-delta quota fix. One open `fs::File` is held across
  chunks (opened first chunk, closed last); errors are DEFERRED (recorded, body
  drained, real JSON sent from the completion handler) so clients never see a
  TCP RST; partials are removed on disconnect and synchronously on write-fail.
- A106 (2026-07-15, Wave A lead, A1): **Route un-shadowing uses
  `AsyncURIMatcher::exact(...)` (verified present in the pinned ESPAsyncWebServer
  3.11.2), not registration reordering.** Plain-string routes resolve to
  `Type::BackwardCompatible` = `path==V || path.startsWith(V+"/")`, so the five
  parents prefix-swallowed eight children; exact matching (`_value==path`) is
  order-independent and future-proof against new children. `onJsonBody` widened
  from `const char*` to `AsyncURIMatcher` (implicit ctor keeps every other
  call-site compiling; regex `^...$` strings still auto-detect). Pinned
  `platform = espressif32@7.0.1` and `ESPAsyncWebServer@3.11.2` (both already the
  resolved/installed versions — no re-download), since a caret range once crossed
  a matcher-semantics change.
- A105 (2026-07-15, library grill + eng-review): **Bigger-library feature —
  standalone for practice, PC-optional for management; PHASED build.**
  Grilled the "external storage" idea then stress-tested it via
  /plan-eng-review (Codex outside voice). Ruled (Christian sign-off, per root
  CLAUDE.md "Rules, not iron rules") the SPEC standalone LOCK is *honored, not
  broken*: device never plays from the PC, so playback stays 100% standalone
  and replicability holds; PC helper is an optional curator at the existing
  upload seam. Spotify model — PC shelf holds hundreds+ (staging, no cloud
  play), device holds a rotating few-dozen played offline. **Build is PHASED
  (review: "foundation first, agent after"): Phase 0 = bugfix Wave A GATE
  (un-shadow /load, fix LittleFS wedge-on-full, add /storage/format) — hard
  prerequisite, nothing builds until it passes; Phase 1 = A1 repartition
  (~1.9 MB, no purchase) + free-space field (SongStore::freeBytes() already
  exists) + hardened whole-file upload + browser bulk-manage screen; Phase 2
  = OPTIONAL folder+headless-sync-agent (NOT a GUI app), only if Phase 1
  insufficient.** Discovery = DHCP reservation now, mDNS deferred. Transport
  = manifest-diff over existing /api/songs, GET is source-of-truth, agent-side
  mtime/hash cache (name+size unsafe), free-space margin ≥32-64 KB anti-wedge,
  upload-then-delete atomicity; TRUE resumable upload needs firmware (deferred).
  A2 microSD deferred (liked, part not bought). Rejected: desktop GUI app, B2
  (device fetches), C (cloud). Big-single-file = RAM ceiling NOT storage,
  out-of-scope. Full design: [docs/DESIGN-library.md](docs/DESIGN-library.md).
  No code this session.
- A104 (2026-07-15, PIN-E coverage pack): **§3 item 13 substituted, not
  skipped.** The literal item ("wizard-tier geometry reverting on scalar
  edit mid-practice") names the "dials-win" rule in `app.cpp::applySettings`
  (`bool rebuild = calib_.tier == "twoPoint" || calibScalarsChanged;`) —
  that decision line lives in `src/app.cpp`, which includes `<Arduino.h>`
  and is esp32dev-only (never compiled under `native`), so it cannot be
  natively pinned as written without a production seam (extracting the
  rule into core, or building a native App test harness) — out of scope
  for a test-only builder. Substituted the core-testable HALF of the same
  concern: `PlaybackEngine::setTable` landing live and leaving a wait-mode
  barrier hold undisturbed mid-swap (`test_p13_table_swap_mid_wait_hold_is_
  live_and_state_preserving`, test_playback_engine.cpp). The tier-SELECTION
  policy itself remains unpinned — flagged for a later wave once a seam
  exists, not built here.
- A103 (2026-07-15, PIN-E coverage pack): **PIN-E coverage pack added —
  15 §3 pinning tests, test-only, no production change.** One test per
  what-if-audit §3 item (`test_p<item#>_*` naming), added to the suite each
  item's own file already lives in (test_scheduler ×4, test_wait_mode ×1,
  test_midi_capture ×1, test_playback_engine ×2, test_mode_director ×7).
  `pio test -e native`: 404 → 419 (+15) after rebasing onto FIX-D (on its
  own branch point it was 392 → 407), ALL PASS, zero FAIL/ERROR. `git
  diff --stat` against the branch point touches only the 5 test files
  above — no `lib/core/src` or `firmware/src` edits. See A104 for the one
  item (13) that needed substitution rather than a literal pin.
- A102 (2026-07-15, DECIDE-D FIX-D build): sub-decisions on the A98 seam.
  (1) The gather window is HALF-OPEN `[barrierTime, barrierTime + eps)` — a
  practiced onset at EXACTLY +epsilon opens the NEXT barrier, never doubles in;
  proven by `test_gap_at_epsilon_stays_sequential`. (2) The next barrier arms
  from `lastAbsorbedOnsetUs + 1` (the max onset absorbed into the chord),
  captured in `WaitMode::update()` — NOT `barrierTime + eps` (would skip a real
  onset sitting between the last absorbed note and +eps) and NOT `barrierTime+1`
  (the old value — that IS the softlock: re-arms on the +1-tick second note).
  (3) While looping, the gather window is capped at `loopEndUs + 1` so onsets
  strictly past loopEnd (unreachable, A90) are never absorbed while an onset
  exactly AT loopEnd still gates; keeps the A89/A90 dead-loop hold byte-identical
  (`test_epsilon_does_not_absorb_across_loop_end`). (4) `Scheduler::notesOnAt`
  (exact instant) and `onsetsBetween` (inclusive) were left untouched — added a
  new `Scheduler::notesInWindow(from, end, mask, out)` for the gate/render, so
  the score-follower and ramp callers stay exact.
- A101 (2026-07-15, DECIDE-D FIX-D build): A99/G19 implemented as a RENDER-time
  override in `playback_engine.cpp` — the wait-mode due-chord block drops its
  `!trackInMask(lightsMask, e.track)` skip and lights any note `wait_->isPending`
  reports as owed, regardless of the track's `lights` flag. The render drives off
  `isPending()` (the one source of "still owed") over the same epsilon window the
  gate absorbed (via `notesInWindow`), so every held note is covered even when
  light-vs-gate timestamps disagree by <=eps (A98's accepted cost). `trackCfg_`
  is NEVER mutated; every other visual path keeps its `lightsMask` filter (proven
  both ways: `test_w8_barrier_lights_even_when_track_lights_off` and
  `test_lights_off_still_dark_in_follow`).
- A100 (2026-07-15, DECIDE-D FIX-D build): A98/G18 chord-gather epsilon =
  `constexpr uint64_t kChordEpsilonUs = 10000` (10 ms) in `wait_mode.h`
  (namespace vialucis; visible to both the gate and the render via the existing
  include). Compile-time constant only, NOT a settings key (comment marks
  "revisit at hardware bring-up"). Integer-microsecond math on the existing
  sorted-events walk, zero new allocation on the onKeyDown/gather hot path
  (chordBuf_ reused). No load-time timeline normalization — demo/follow play the
  file exactly as authored (`test_timeline_onsets_stay_distinct`,
  `test_demo_plays_both_onsets_unmerged`).
- A99 (2026-07-15, DECIDE-D grill, what-if audit G19, CHRISTIAN'S RULING):
  **practiced-implies-lit.** Wait mode's barrier/due notes always render,
  regardless of the track's `lights` flag — you can never owe an invisible
  note. The `lights` flag keeps governing every non-gating visual
  (follow-along glow, demo, ramp on non-practiced tracks). Rejected: gate
  only lit notes (the silent-degrade shape A90 already rejected) and
  keep-blind-gating-with-a-warning. Deliberate blind/memorization practice
  is explicitly deferred to a future purpose-built mode (it would want its
  own affordances, e.g. hint-after-N-seconds), not inherited from a flag
  collision. Render-time override only — the track config itself is never
  mutated. Repro `test_w8_barrier_never_gates_on_unlit_notes` (audit
  branch) becomes the pin when built.
- A98 (2026-07-15, DECIDE-D grill, what-if audit G18, CHRISTIAN'S RULING):
  **chord-gather epsilon lives in the wait-gate ONLY; the song timeline is
  never altered — "style lives in those" (his words).** Micro-offsets in a
  file can be intentional performance (rolls, flams, grace notes): demo/
  accompaniment must play them exactly as authored, so no load-time
  normalization. The gate groups practiced onsets closer than
  **~10ms (compile-time constant, NOT a settings key; revisit at hardware
  bring-up)** into one barrier chord — below deliberate human playability,
  so it only ever absorbs export slop (the G18 softlock: a together-press
  eaten because the second sub-ms barrier armed after the press). Gaps ≥
  epsilon stay SEQUENTIAL gates — a deliberate roll is preserved and
  taught, one press at a time. Per-note override = the existing editor
  (drag/quantize past or under the threshold); no new file-format flags,
  no per-note metadata on the device. Known accepted cost: gate chord vs
  light timestamps can disagree by ≤ epsilon (beneath perception). Repros
  `test_C28a/b_*_split_chord` (audit branch) become the pins when built.
  {name}` refuses (`409 {"error": "song is loaded"}`) rather than silently
  unloading the song first. Rejected the "safely unload first" alternative
  the brief also offered — a DELETE that also stops your live practice
  session mid-chord is a surprising, destructive side effect for what reads
  as a library-management action; the player almost certainly meant "free
  up space," not "kick myself out of what I'm playing." An explicit `POST
  /api/songs/unload` is one call away, matches the existing pattern (probe/
  mode/test-pattern-during-show all refuse rather than silently tearing
  something down), and needed no new engine API — web_server.cpp determines
  "is this the loaded song" from the `"song"` field already on the wire in
  `statusJson()`, so the guard stays entirely inside its D2 scope (no
  App.h/PlaybackEngine change). No native test: web_server.cpp is a
  structurally-untested device shim (compile-gated only, per the wave's
  policy-logic-placement rule) and there's nothing decidable to hoist into
  ModeDirector here — the "is this song loaded" fact is engine-owned name
  string comparison, not a mode/show policy call.
- A96 (2026-07-14, FIX-B B-3, what-if audit G14 + D3, DECIDE): mode PUT
  (`/api/mode`) is refused wholesale while any show plays, via a new
  `ModeDirector::setMode` wrapper App now routes through instead of calling
  `PlaybackEngine::setMode` directly. Rejected alternative: "route through
  show teardown" (the brief's other offered option) — auto-stopping a live
  show because the player poked an unrelated control felt too surprising
  for a stage tool; an explicit `POST /api/shows/stop` is one call away.
  Refusal is also the only fix possible without touching PlaybackEngine
  (FIX-A's territory in this wave): wait mode's barrier-holding is inherent
  to the engine, so there is no partial "leave the clock running" option
  the way B-2 found for the test pattern — switching mode away from what
  the show needs (demo/follow) WILL freeze it, full stop. D3 (App's
  `lastMode_`/`lastPractice_` getting clobbered mid-show) falls out for
  free: App::setMode only assigns them when the call succeeds, and a
  refused call never does. Reuses the existing `400 bad mode` shape (no new
  REST route/response shape) — the message text stays generic since
  web_server.cpp's `/api/mode` handler is out of this pack's scope (D2 guard
  only); a more specific error string is a nice-to-have for a later pack.
- A94 (2026-07-14, FIX-B B-1, what-if audit G13): probe refusal (`ProbeArm::
  Playing`) widened to `engine_.state()==Playing OR showPlaying_` — a
  score-follow show's transport is deliberately stopped (the performer IS
  the clock), so the old engine-state-only check never caught it, and an
  armed probe would eat the performer's next key press before the follower
  saw it (probe consumes before practice/follow, by design). The tick's
  "playback starting cancels an armed probe" auto-cancel got the same
  widening for symmetry, so a show that starts after the probe was armed
  also clears it. No REST shape change — reuses the existing `409 playing`
  typed refusal.
- A95 (2026-07-14, FIX-B B-2, what-if audit G15/G16, DECIDE): one uniform
  rule for test-pattern-during-show, covering both clock kinds — **the
  pattern is a pure visual overlay that must never alter a playing show's
  own clock, in either direction.** Concretely that's TWO symmetric guards
  (not one shared code path, since demo and score-follow drive their clocks
  through entirely different mechanisms): (1) `setTestPattern`'s F3/A35
  auto-pause is skipped while `showPlaying_` — a demo-clock show's clock IS
  the engine's own Playing-state tick, so leaving it alone is sufficient
  (G15: the OLD behavior of pausing it was itself the bug — a live
  performance's transport, and the piano audio it drives, must not stall
  because someone bumped the test-pattern button). (2) `driveShowClock()` —
  the write that feeds the follower's estimate into engine position — is
  skipped in both `onKeyDown` and `tick` while `testPatternActive()`, for
  score-follow (G16: it has no transport to pause, so the OLD bug was the
  opposite — the clock silently kept advancing under the pattern, breaking
  the A35 no-skipped-time guarantee for that path; freezing engine position
  restores it). The follower itself keeps consuming key events under a
  pattern (only the engine-facing write is gated) so it doesn't lose the
  performer's place when the pattern goes `off`; whether resuming needs its
  own re-baseline (mirroring A35's play-side fix) is unexercised by any
  repro and deferred — flagged for hardware bring-up. Rejected alternative:
  refuse the pattern outright while any show plays (B-1's precedent) — fails
  G16's own repro, which activates the pattern and expects the clock frozen,
  not the activation refused.
- A94 (2026-07-14, FIX-B B-1, what-if audit G13): probe refusal (`ProbeArm::
  Playing`) widened to `engine_.state()==Playing OR showPlaying_` — a
  score-follow show's transport is deliberately stopped (the performer IS
  the clock), so the old engine-state-only check never caught it, and an
  armed probe would eat the performer's next key press before the follower
  saw it (probe consumes before practice/follow, by design). The tick's
  "playback starting cancels an armed probe" auto-cancel got the same
  widening for symmetry, so a show that starts after the probe was armed
  also clears it. No REST shape change — reuses the existing `409 playing`
  typed refusal.
- A91 (2026-07-14, what-if audit FIX-A / G10, LOG-ONLY — needs bring-up
  data): **same-pitch accompaniment echo can eat the player's genuine press —
  deliberately NOT changed yet.** When accompaniment plays the muted hand at
  the SAME pitch the practiced hand owes, within the echo window (default
  250ms), the player's correct press is classified `Echo` → phantom wait (the
  light stays; a second press clears it). Both candidate fixes trade one risk
  for another and the right choice depends on real hardware: (a) scope echo
  credits to the emit mask's pitches only — but the practiced and
  accompaniment hands CAN legitimately share a pitch, so a shared-pitch emit
  still mis-credits; (b) shorten the echo window — but too short lets genuine
  slow echoes through as wrong-note red flashes. Decision deferred to FP-30X
  bring-up: measure the piano's actual echo latency/duplication, then pick a
  window and/or a source discriminator from data. Repro
  `test_s5_genuine_press_not_eaten_by_accompaniment_echo_same_pitch` stays on
  the audit branch; NOT added to the suite (it would fail by design).
- A90 (2026-07-14, what-if audit FIX-A / G3): **a loop that excludes every
  practiced onset HOLDS wait mode at loopEnd, not refuse-the-loop.** When
  practicing a hand whose notes all sit outside the loop, the barrier used to
  arm beyond loopEnd and never hold — wait mode silently degraded to
  follow-along forever (a practice softlock). Decision (per SPEC "wait mode is
  the product"): `WaitMode::armFrom` clamps the barrier to loopEnd whenever
  the next practiced onset is at/beyond loopEnd and we're arming from inside
  the loop. The dead loop is then VISIBLE — status shows "waiting" at the
  boundary with no lit chord — so the player fixes the loop range or the
  practiced hand, instead of a silent never-gating loop. Chosen over
  refuse-the-loop-PUT because the practiced set can change (mode/track PUTs)
  after the loop is set; a per-arm hold stays correct across those, a
  set-time refusal does not. An onset exactly at loopEnd coincides with the
  hold and gates as usual (e.g. the G1 re-gate).
- A89 (2026-07-14, what-if audit FIX-A / G2): **a loop set entirely behind
  the playhead wraps INTO the loop (the loop is authoritative), not
  clamp+finish.** Enabling a time-range loop expresses intent to repeat that
  region; letting the song free-run to the end instead is the more surprising
  outcome, and the old behavior left `positionMs` running unbounded past
  `durationMs` forever (`finished()` and the duration clamp were both gated
  off by `loopOn_`). `Scheduler::advance` now snaps any playhead at/beyond
  `loopEnd` back to `loopStart`, O(1)-modulo-collapsing the overshoot. A
  barrier holding at the position still wins the tie (holding beats wrapping).
- A88 (2026-07-13, arch C2 closing review, cleanups — no behavior change):
  batch of verified-finding hardening across the MIDI corpus tooling.
  (1) Generator replaces every invariant-guarding `assert` (the two
  `tick_to_micros` exactness checks + the non-decreasing-tick `delta` guard)
  with explicit `raise ValueError` — `python -O` strips asserts and would let
  an inexact/corrupt golden twin commit silently. (2) Generator `channel_msg`
  now range-validates the authored status/data bytes (status bit7 set + known
  channel-status kind; d1/d2 0..127) and raises — an out-of-range byte
  otherwise desyncs the MTrk stream with no generation-time error. (3) Deleted
  the dead `js_number_list` helper. (4) `tools/midi_dump.py` `-o` opens with
  `newline="\n"` so its output stays LF-exact (text-mode default emitted CRLF
  on Windows). (5) Editor selftest's tempo pin is now UNCONDITIONAL (previously
  skipped for expected-empty tempo, hiding a phantom entry). (6)
  `tools/check_midi_corpus.py` gains a third `firmware-coverage` check: the
  stems in `test_midi_corpus.cpp`'s `checkParse`/`checkHands` calls must equal
  the committed `corpus/midi/*.mid` set, closing the last silent hole (a new
  fixture missing its firmware tests). (7) The firmware suite guards each
  `tickToMicros` value `<= 0xFFFFFFFF` before the uint32 cast (a future long
  fixture would wrap). (8) README's stale "~line 3002" pointer replaced with a
  grep command.
- A87 (2026-07-13, arch C2 closing review, CONFIRMED bug): **editor
  hand-precedence aligned to firmware (left/lh checked before right/rh).**
  Firmware `TrackConfig::defaultsFor` (track_config.cpp) tests left/lh BEFORE
  right/rh, but the editor's `handOfName` (and both python replicas — the
  generator's `hand_of_name` and `tools/midi_dump.py`'s `_hand_of_name`) tested
  right/rh FIRST. Since both use case-insensitive substring matching, a name
  matching BOTH token sets — the realistic "Left Rhythm" ("rhythm" contains
  "rh") — was assigned Left by firmware but canonical Right(0) by the editor:
  exactly the A82-class firmware/editor hand disagreement this corpus exists to
  prevent. Ruling: **left tokens win**, matching firmware (the older, on-device
  contract; A82's intent was that the editor match it). Fixed all three editor-view
  consumers to check left/lh first. New fixture `ambiguous-name` (TPQ 480, "Left
  Rhythm" note-track FIRST in file order + a "Right" track) pins the ruling across
  firmware, editor, and midi_dump; it also pins that the LEFT-named track's note
  sorts FIRST (onTick tie, stable) even though the canonical track list is
  [Right, Left]. Corpus is now 9 fixtures; firmware suite +2 tests (374 total).
- A86 (2026-07-13, arch C2): **tools checker recomputes the FULL twin as an
  independent 4th implementation, plus guards the editor's embedded fixture
  hex.** `tools/midi_dump.py` reimplements the SMF parse, `tickToMicros`,
  `TrackConfig::defaultsFor`, and the editor's `normalizeHands` hand rules
  straight from the C++/JS contracts — it does not import the generator or
  read its output, so it can catch a drift the generator's own reference
  implementation and the two code consumers might share.
  `tools/check_midi_corpus.py` (mirrors `check_corpus.py`) semantically
  diffs every fixture's twin AND regex-extracts the `MIDI_CORPUS` block from
  `editor.html` to byte-compare its embedded hex against the committed
  `.mid` files (key-set equality too — an added fixture missing from the
  embed fails). Rationale: the embed is hand-pasted from the generator's
  printed snippet (A84/A85) with nothing else enforcing sync; undetected
  drift there would silently hollow out the editor cross-pin, since the
  selftest would keep passing against stale bytes that no longer match what
  ships in `corpus/midi/`.
- A85 (2026-07-13, arch C2): **editor selftest embeds the MIDI corpus.** The
  `?selftest=1` block carries `MIDI_CORPUS` — each fixture's committed `.mid`
  bytes as concatenated hex string literals plus the twin's editor-view `expect`
  object — and re-parses the hex with the real `parseMidi`, asserting the
  canonical-hands view (track names, per-note hand/pitch/vel/onUs/offUs, per-pedal
  hand/value/us, tempo). This is the editor half of the A83 cross-pin: the
  firmware suite pins the raw-index view of the SAME files, so the two
  implementations disagreeing on hands fails a gate (the A82 hand-swap, caught a
  wave earlier). The embedded hex is regenerated from the generator (which prints
  the snippet) and guarded against the committed `.mid` files by
  `tools/check_midi_corpus.py` (Builder B). Selftest grows 55 → 99 assertions.
- A84 (2026-07-13, arch C2): **generator authority + writeSmf byte-pin.** The
  MIDI corpus is authored by `corpus/gen/midi/gen_midi_fixtures.py`: each fixture
  is one declarative python model from which the script emits BOTH the `.mid`
  bytes AND the expected twin JSON — the twin is computed from the model by a
  python reference implementation of the parser semantics, NEVER by parsing the
  bytes it just wrote. The generator's models are therefore the authority;
  regenerating (`python corpus/gen/midi/gen_midi_fixtures.py`) is a deliberate
  contract event that re-gates all three consumers. The `recording-shaped`
  fixture is emitted by a byte-for-byte python replica of firmware `writeSmf`;
  `test_recording_shaped_writer_bytes` byte-compares the real `writeSmf` against
  the committed file, so any replica drift fails the gate. The generator also
  prints the editor `MIDI_CORPUS` snippet so regeneration stays copy-paste.
- A83 (2026-07-13, arch C2): **golden MIDI corpus at `corpus/midi/`** — the
  cross-artifact contract for the SMF parser, twinning the `.vls` show corpus
  (A56/candidate-1) for MIDI. 8 fixtures (conductor-first, named-lh-rh,
  anonymous-pair, three-hands, pedal-only-track, tie-order, vlq-tempo,
  recording-shaped) each ship `<name>.mid` + `<name>.expected.json`. The twin
  pins BOTH representations of the same file: the firmware RAW-INDEX view
  (`note.track` = MTrk index, `TrackConfig::defaultsFor` hands) AND the editor
  CANONICAL-HANDS view (`normalizeHands`, 0=Right/1=Left/2=Other) — with
  exact-integer-microsecond times (the generator asserts every pinned tick
  divides cleanly through `tickToMicros`, so firmware truncating integer
  division equals editor `round(ms*1000)`). Rationale: the A82 hand-swap shipped
  because no artifact pinned both views of the same file — three-hands and
  named-lh-rh explicitly pin the firmware/editor divergence and the name-beats-
  file-order rule. Firmware suite: `firmware/test/test_midi_corpus` (~18 tests).
- A82 (2026-07-13, closing review, CONFIRMED finding): **editor hand
  normalization at load** — the roll/counters/export trusted the raw MTrk
  index as the hand, but device recordings (and normal DAW files) carry a
  conductor track at index 0, so a loaded take rendered wrong colors and
  "Export .mid" SWAPPED hands unless Split-hands was clicked first. Fix:
  `normalizeHands()` runs at the end of the editor's parseMidi — name
  right/rh → 0, left/lh → 1, else file order among note-bearing tracks;
  3-bucket hand model (0=Right, 1=Left, 2=Other/amber, editable per-note;
  Other exports to Right by default); `song.tracks` becomes the synthetic
  canonical list, and followTrack indices align with the exported track
  order. Also: pedals-only takes export (refuse only when notes AND pedals
  are empty; pedals always bake into one CC64 track regardless of source
  track), note-drag/marquee repaints are rAF-coalesced, and Backspace is
  swallowed when nothing is selected (history-back ate unsaved edits on
  older browsers). Selftest grows to 55 assertions incl. a conductor+R+L
  round-trip pin.
- A81 (2026-07-13, closing review): webui fixes — count-in/BPM inputs are
  LOCAL while idle (the 2×/s poll no longer wipes the user's pending choice
  before Arm reads it; device state mirrors only once armed); one shared
  `renameSong()` + `humanBytes()` used by both the Record panel and the song
  list (the two rename flows / size formats must never drift). Firmware
  cleanups: dead `lastArm_` removed; `nextRecordingName` digit-length guard
  (rename could construct an 11-digit `recording-<n>.mid` that overflowed the
  int accumulator — UB).
- A80 (2026-07-13, closing review, CONFIRMED finding): **pedal echo
  exclusion** — §5a covers CC64, but only note echoes were guarded. The
  device's demo/accompaniment CC64 pass-through echoing back from the piano
  would have been captured as the player's own pedal press. Fix: capture
  gains a value-agnostic pedal-echo credit window (EchoGuard is note-keyed,
  so pedal credits are a separate count+expiry sharing the same window); the
  director's tick echo-feed scan now also registers `Cc`/64 sends. Native
  tests: echo excluded, credit expiry never eats a real press.
- A79 (2026-07-13, closing review, supersedes the A64 tail): `stop()` takes
  the stop TIME — notes still held when Stop is pressed close at stop time
  (clamped to the duration cap), so a performance ending on a sustained
  chord keeps its real duration instead of collapsing to the last discrete
  event (worst case offMs=0). Trailing SILENCE stays trimmed: durationMs
  extends past the last event only when something was actually held through
  Stop.
- A78 (2026-07-13, REC6 + lead integration fix): the editor's exported `.mid`
  layout = format-1, track "Right" (tempo + right-hand notes), "Left", and a
  separate **"Pedal" track for CC64** (emitted only when pedal events exist —
  MidiWriterJS can't reliably interleave relative-delta CC with explicit-tick
  notes on one track). That layout exposed a device gap: `defaultsFor` gave
  every notes-empty track `Hand::Off`, and the emitter masks Pedal events by
  track — demo playback would silently lose the sustain. FIX (lead, native
  test): a notes-empty track that HAS pedal events now defaults **Hand::Both**
  (audible in demo; silent in accompaniment — pedal is the player's own foot
  there; invisible to wait mode — no onsets), lights off.
- A77 (2026-07-13, REC6): `.vlp` bumped to **v2** — adds editable notes, CC64
  pedals, and the note-edit settings (editTempoBpm / splitPoint / gridDiv /
  noteSnapOn / quantLengths) alongside the existing show fields; v1 files
  still load (missing fields default). The `.vlp` is the mutable project
  copy — the raw take on the device is never rewritten (§6a).
- A76 (2026-07-13, REC6): editor note timing is **millisecond-authoritative**;
  MIDI ticks go stale after edits and are recomputed on export from ms + the
  editor-assigned tempo (vendored MidiWriterJS 3.1.1 MIT, TPQ=128). Grid-
  quantized notes land on clean ticks; velocity re-scales through
  MidiWriter's 1–100 API (±1 — velocity isn't editable in v1 anyway, A2b).
- A75 (2026-07-13, REC4 lead review): `recordBudgetKB` default lowered
  **256 → 64** and arm gained a **contiguous-RAM guard**
  (`ESP.getMaxAllocHeap() >= budget + 32KB` else `507 {"error":"low memory"}`).
  arm() reserves the WHOLE budget as one heap block; with BLE + WiFi up a
  stock ESP32 rarely has a 256 KB block free, and on -fno-exceptions a failed
  reserve ABORTS the device — the old default was a crash-on-arm, and any
  user-raised budget now degrades to a typed refusal instead. 64 KB ≈ 8k
  events, ample for the 10-min cap; PSRAM boards can raise the setting
  (clamp 16–1024 unchanged). Re-verify the margin at hardware bring-up.
- A74 (2026-07-13, REC4): `statusJson` gained a **`RecordStatus` param**
  (mirroring the M3 `TopStatus`), so the `"record"` object is authored inside
  the one status document BEFORE wifi (wifi stays last). This REQUIRED editing
  `playback_engine.{h,cpp}` — one file beyond the wave's listed chokepoint set —
  because R4 forbids serialize→splice→reserialize and only the engine controls
  key order. The change is minimal and mechanical (a third optional pointer
  arg, default `nullptr`, so every existing caller is unaffected).
- A73 (2026-07-13, REC4): `App::recordArm` refusal precedence = **already-armed
  (409) → playing (409) → low-space (507)**. "playing" means a **presentation
  light show is rendering** (`director_.showPlaying()`) — recording DURING
  Practice/Play-along (even mid-song) is explicitly allowed (the point of
  Play-along). The armed/playing checks run under the fence; the free-space
  check runs UNFENCED (store_ is HTTP-task-owned). Free-space needs
  `recordBudgetKB*1024 + 8 KB` margin or arm 507s.
- A72 (2026-07-13, REC4): `App::recordStop` fences ONLY the take extraction
  (`director_.stopRecord`); the hand-split + `writeSmf` + LittleFS `save` run
  UNFENCED after (F-wave discipline — a tick never waits behind heap/flash). An
  **empty take saves nothing** and returns `""` (REST `200 {"name":""}`); a
  save failure is `500 {"error":"write failed"}` (a code beyond the frozen
  Saved/Empty/NotArmed set, for the flash-full edge). The auto-name is the next
  free `recording-<n>.mid` (SongStore scans `/songs`).
- A71 (2026-07-13, REC4): Hand-split (`splitTakeIntoHands`) emits **Right first,
  then Left** (piano convention), **omits an empty hand's track**, attaches ALL
  CC64 pedal events to the **first emitted track**, and a notes-empty
  (pedals-only) take still yields a single `Right` track so the pedals have a
  home. The pure helpers `splitTakeIntoHands` + `nextRecordingName` live in a
  NEW native-testable lib/core module `record_take.{h,cpp}` (App/SongStore that
  call them are Arduino-bound); `POST /api/record/arm` can't use `onJsonBody`
  (which 400s an empty body) since its body is optional, so it uses an explicit
  request+body handler pair (bodyless arms with count-in off, defaults).
- A70 (2026-07-13, REC4): `nextRecordingName` returns the **smallest positive
  integer not already used** (fills a deleted-take gap first), ignoring names
  that don't match `recording-<n>.mid`. `recordBudgetKB` (16–1024 KB) bounds
  BOTH the in-RAM capture buffer reserve AND the free-space check.
  **HARDWARE FLAG (bring-up, not a code change):** the 256 KB DEFAULT reserves
  ~256 KB of heap at `arm()` on a chip with ~250 KB free — on an ESP32 without
  PSRAM the default may need lowering; the byte budget is a setting precisely so
  this can be tuned once real heap headroom is measured. `rename` treats a
  rename-to-self as a no-op `Ok`.
- A69 (2026-07-13, REC3): The recording **heartbeat** is one reserved pixel
  OUTSIDE every key's LED range (§9a), chosen in `computeHeartbeatLed`: the LED
  just above the top key if room, else just below the bottom key, else pixel 0
  (keys fill the strip). Color = a slow-breathing **dim amber** `{amp, amp*2/5,
  0}` with amp 20–60 over a ~2s triangle breath — deliberately R-dominant with
  green present and NO blue, so it can never read as the pure red/blue/green of
  a practice verdict. Painted LAST in the Record frame so it always shows.
- A68 (2026-07-13, REC3): The Free-capture **count-in** is a 1-bar / 4-beat dim
  **white** pulse ADDED (saturating) over the Reactive monitor for the first
  `4*60000/bpm` ms after arming (bpm clamped 20–300 in `armRecord`), decaying
  across each beat. It is Free-capture only (`countIn_ = countIn &&
  !songLoaded()`) and NEVER gates the take — capture still starts on the first
  real note. No audio, no continuing metronome in v1 (sanctioned fast-follow).
- A67 (2026-07-13, REC3): **BLE pedal plumbing** now carries the raw 0–127 CC64
  value end-to-end (`BleMidiIo::onPedal(uint8_t)` → `ModeDirector::onPedal(uint8_t
  value, nowUs)`), replacing the old bool edge. The director computes the
  reactive latch (`value>=64`) internally and feeds capture the raw value —
  a trivial, cleaner deviation from the frozen "App maps down" wording (single
  source of the down/raw split), functionally identical. Additive and small.
- A66 (2026-07-13, REC3): The capture **tap** is a single `capture_.onNoteOn/
  Off/onPedal` call at the END of `ModeDirector::onKeyDown/onKeyUp/onPedal`,
  AFTER the engine verdict/light and the Reactive layer — so it adds nothing to
  the key→light latency path (O(1), no-op when Idle). A probe-consumed press
  early-returns before the tap, so calibration presses are never recorded.
  Channel is always **0** (the BLE note callback carries no channel; the hand
  split is by pitch downstream, not channel). Record top-mode outranks Afk and
  is checked in `topMode` BEFORE the idle timeout, so a take left armed past the
  timeout stays Record (arming disarms AFK, §7a); `armRecord` also resets the
  idle clock for belt-and-braces.
- A65 (2026-07-13, REC3): Capture's **echo guard is fed from the emission path**
  by scanning the `out` vector after `engine_.tick` each director tick: every
  note-on we transmit registers `capture_.noteSent`, so the piano's echo of a
  Play-along accompaniment note (arriving on a later BLE poll, hence after the
  credit is registered) is dropped from the take. The scan runs only when a take
  is armed/recording. **REST-path emissions bypass this scan** (stop / all-off);
  those are note-offs in practice and are never captured as presses, so the gap
  is harmless — noted in a code comment at the scan site.
- A64 (2026-07-13, REC2): MidiCapture `stop()` takes **no clock argument**;
  the take's `durationMs` is the last captured event's time, and notes still
  held at stop close there (mirrors the parser closing unreleased notes at
  End-of-Track). A note held silently for a while before the user hits stop
  loses that trailing hold — acceptable for a trainer take (the editor fixes
  ends), and it keeps stop() side-effect-free.
- A63 (2026-07-13, REC2): Overflow is checked **per append**, dropping the
  offending event and setting `CaptureStatus::Overflowed`, but recording
  **continues** (state stays Recording) — it does not auto-stop. Rationale:
  the iron rule is "never block/grow"; auto-stopping would be a policy the
  director/REST layer should own, not the core tape head. Both limits (byte
  budget via a fixed `maxEvents_`, and `tMs > maxDurationMs`) drop identically.
- A62 (2026-07-13, REC2): Capture callbacks (`onNoteOn/Off/Pedal`, `noteSent`,
  `arm`) take **microseconds** (`nowUs`), not ms — matching the engine's
  clock and, critically, the EchoGuard window (250000 us). Timestamps are
  stored as **ms relative to the first accepted note-on** (`(nowUs -
  firstNoteUs_) / 1000`). Using ms for the guard would break its us window;
  the wave brief's "nowMs" phrasing is superseded by this unit-consistency
  requirement. The echo check runs BEFORE the Armed→Recording transition, so a
  device echo neither is captured nor starts the clock.
- A61 (2026-07-13, REC2): The capture take model reuses **`SmfNoteEvent` /
  `SmfPedalEvent`** (from smf_writer.h) rather than a parallel struct, so a
  finished take feeds `writeSmf` with no conversion. Raw in-flight events use a
  separate packed 8-byte `CaptureEvent` (the byte-budget unit); pairing on/off
  into notes happens once at `stop()` (off the hot path). Capture is a single
  performance stream (both hands one pass); hand-split into two named tracks is
  REC3's job downstream, not the tape head's.
- A60 (2026-07-13, REC1): SmfWriter emits **explicit note-off (0x80 status,
  release velocity 0)** rather than note-on-velocity-0 for note ends. The
  parser treats both identically, but 0x80 is unambiguous and keeps running
  status irrelevant. The writer never uses running status (always a full
  status byte) — simpler, and the parser reads both forms; the few extra bytes
  are irrelevant at the 256 KB take ceiling.
- A59 (2026-07-13, REC1): At an **identical timestamp**, SmfWriter orders
  emitted messages **note-off (0) → CC64 pedal (1) → note-on (2)**. Offs
  before ons at the same instant is required for a same-key re-strike at a
  shared boundary (note ends exactly when it restarts): ons-first would make
  the parser's re-trigger logic collapse the held note to zero length and drop
  the second instance (the tie-order class of bug the show_player suite already
  caught). Verified by test_same_key_restrike_boundary.
- A58 (2026-07-13, REC1): SmfWriter input model = per-track `{name, notes[],
  pedals[]}` with **absolute-millisecond** on/off (`SmfNoteEvent{onMs,offMs,
  note,velocity,channel}`) and pedal (`SmfPedalEvent{tMs,value,channel}`) —
  NOT delta-encoded and NOT required pre-sorted (the writer stable-sorts each
  track before delta encoding, so capture/editor callers can hand it events in
  any order). Track 0 (tempo/meta conductor) is written automatically; callers
  supply only performance tracks. Degenerate input (no tracks / empty tracks)
  yields a valid file with just the conductor — `writeSmf` never fails.
- A57 (2026-07-13, VL3 close): **CHRISTIAN'S RULING, not an assumption — VL3
  editor hosting = Option 1.** The show editor is OFF-device (`editor/editor.html`),
  distributed as a downloadable **release artifact** (`file://` open, proven by the
  P-POC). GitHub Pages / the repo link the download and docs only — it does NOT host
  the live editor (HTTPS→http device is a spec-mandated mixed-content block, not
  fixable by CORS). Device-served editor (a second gzipped app, same-origin) stays an
  OPTIONAL later add — additive, costs flash, revisit only if wanted. No format/player/
  API change follows from this ruling; the firmware CORS header set
  (ACAO:*, OPTIONS 204, Allow-Private-Network) already ships with `/api/shows` as
  designed. VL3 is now CLOSED — the last open v2 planning ruling.
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
