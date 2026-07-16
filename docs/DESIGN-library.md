# Via Lucis — Bigger Library (DESIGN-library.md)

Grilled and **decided 2026-07-15** (Christian's sign-off on the standalone-lock ruling
below), then **stress-tested via `/plan-eng-review` the same day** — this doc reflects the
review outcomes (phased build, folder-agent shape, DHCP discovery, Wave-A gating). Changes
to decided items require Christian's explicit sign-off. Adjacent canon: [SPEC.md](SPEC.md)
(§Architecture standalone lock, §"Song pipeline" upload seam), [API.md](API.md)
(`/api/songs` endpoints this feature drives), [BUGFIX-PLAN-2026-07-15.md](BUGFIX-PLAN-2026-07-15.md)
(Wave A — the hard prerequisite), [BOM.md](BOM.md) (the deferred microSD path).

## Purpose

The device's song library is bounded by **~0.9 MB of LittleFS flash** — a few dozen small
MIDIs — and the only way to curate it is the device's own web UI, one upload at a time. This
feature lets Christian keep **hundreds** of MIDIs reachable and **swap the device's on-board
set quickly**, the way Spotify's offline-downloads work.

**The device does not change what it is.** It still plays only what is physically on its
flash; it never plays a song off the PC. Any PC-side helper is a *curator*, layered on the
existing upload seam — precisely as the browser editor (ruling A57) and recording
([DESIGN-record.md](DESIGN-record.md)) are optional producers at that same seam.

## The Spotify model (Christian's framing)

Two shelves, and the distinction is the whole design:

- **Shelf** — the full library, **hundreds+**, lives on Christian's PC (a folder on disk).
  Never played from; staging only. *No cloud playing.*
- **Device set** — the **rotating working subset** (dozens) actually on flash. This is all
  the device plays, and it plays it **fully standalone**.

"Downloaded" songs play offline from the device exactly like today. Swapping the device set
must be **fast and reliable** — that is the feature's entire value.

## The standalone-lock ruling (THE crux — signed off)

This brushed the project's most load-bearing LOCK — SPEC §Architecture, *"ESP32 does
everything… No PC, no Raspberry Pi, no cloud,"* and the replicability rule. Per the root
workspace CLAUDE.md's "Rules, not iron rules" (a LOCK is reviewable, not a stronghold):

**Standalone for *practice*; PC-optional for *management*.**

- **Playback is 100% standalone.** The device never streams or plays from the PC. PC off →
  Christian practices everything currently on the device, unaffected. The only thing lost
  without the PC is the ability to *re-shuffle* the on-board set.
- **The replicability promise holds.** A stranger with the BOM + a browser still gets a
  fully working device and curates it through the web UI. No PC-side software is required to
  build or run the device. (Caveat: the A1 repartition below adds one documented flashing
  step — see §Storage.)
- **Rejected:** B2 (device *fetches* from the PC mid-session) and cloud/NAS (C) — both make
  practice depend on an external box and reopen the lock. Cloud also pushes copyrighted
  MIDIs off-LAN. Both stay rejected.

## Build sequencing — PHASED (decided in review: "foundation first, agent after")

The four device-side items below are prerequisites for **any** version of this feature, and
once they land a browser bulk-manager falls out nearly for free. So build the foundation,
live with it, and only build the PC sync agent if the browser path proves insufficient.

```
PHASE 0 — GATE (hard prerequisite, NOT optional)   ── bugfix Wave A, see BUGFIX-PLAN
  ├── un-shadow POST /api/songs/{name}/load etc.   (today: "no song can ever load" via REST)
  ├── fix LittleFS wedge-on-hard-full              (today: FS wedges; swapping WILL hit full)
  └── add POST /api/storage/format                 (A1 recovery + wedge recovery need it)
        │  Acceptance: load/unload/rename reachable; upload-to-full does not wedge;
        │  format clears FS. Until these pass, DO NOT build anything below.
        ▼
PHASE 1 — FOUNDATION (device-side, standalone, replicable)
  ├── A1 repartition → ~1.9 MB songs               (doubles the set; one documented reflash)
  ├── surface free space in GET /api/status/songs  (SongStore::freeBytes() already exists)
  └── hardened single-file upload                   (retry/verify; see transport caveat §4)
  └── browser bulk-manage screen in webui/          (multi-select upload + delete + capacity bar)
        │  This may be ENOUGH. Re-decide Phase 2 only after living with it.
        ▼
PHASE 2 — PC SYNC AGENT (optional, only if Phase 1 insufficient)
  └── watched folder + headless sync agent          (see §Shape; folder semantics §7)
```

## Decisions

### 1. Problem = breadth, not single-file size
Target is **library breadth** (keep hundreds reachable), *not* playing one enormous
arrangement. Breadth is bounded by **flash** (storage fixes it); single-file playability is
bounded by **RAM during parse/play** (storage does **not** fix it). The 140 KB NieR file is
a RAM-ceiling case, **out of scope** — it would need a streaming-parse engine (separate
project, see §Open threads).

### 2. PC-side shape (Phase 2) = watched folder + headless sync agent
NOT a desktop GUI app. The "library" is a **folder on the PC**; a small **headless/tray
agent** mirrors it onto the device over the existing `/api/songs` API. Management UI is the
OS file manager (Explorer). Rejected: a bespoke cross-platform GUI (a second codebase +
packaging to maintain, the thing most likely to rot); a device-web-UI-only approach with no
persistent shelf (that IS Phase 1's browser screen — it just doesn't give the hundreds-on-PC
shelf, which is why Phase 2 exists).

### 3. Storage now — repartition (A1); microSD (A2) deferred
- **A1 (Phase 1):** repartition to ~1.9 MB songs. Roughly doubles the device set to ~40–90
  small MIDIs, **zero new hardware, zero purchase, stays standalone.** Costs: the reflash
  wipes stored songs (re-sync from the PC shelf), AND it adds a **documented custom-partition
  flashing step** to the stranger setup path — a small dent in "flash from your browser."
  Note it in BUILD-GUIDE. Ceiling = board total flash.
- **A2 (deferred):** microSD → gigabytes, still standalone. The "someday I want *hundreds*
  resident on-device, PC-free" upgrade. Christian likes it but is not buying the part now.
- **Why A1 suffices:** in the Spotify model the device only needs the *working subset*
  (dozens). Hundreds live on the PC shelf. SD is only needed to make the *entire* library
  resident PC-free.

### 4. Transport — manifest-diff over HTTP; resumable upload is FIRMWARE work
Drives the **existing** `/api/songs` API; no HTTP client on the device.
- **Manifest diff:** read `GET /api/songs` (name + size) + free space; compute the delta;
  `POST /api/songs?name=` to add, `DELETE /api/songs/{name}` to remove.
- **`GET /api/songs` is the source of truth.** Every sync starts with a fresh GET and
  reconciles against it — never trust a cached view, because songs can still be added/deleted
  via the device's own web UI behind the agent's back.
- **name+size is an unsafe diff key.** An in-place re-export (same name, ~same size, new
  content) would be silently skipped. Fix **agent-side**: the agent keeps an
  **mtime/content-hash cache** of shelf files and re-uploads on local change regardless of
  the device's size match. No firmware change.
- **Resumable/true-chunked upload needs FIRMWARE support** (temp file + offset + commit/abort
  + checksum). The current `POST /api/songs?name=` is a raw-body stream sink, so **Phase 1
  hardening = whole-file retry + verify only** (retry on TCP reset, re-GET to confirm size).
  Real resume is a later firmware add — document the current contract (chunk size, retry
  policy, verify-after-write) in **API.md** so webui and any future agent don't diverge.
- **Free-space margin (anti-wedge):** LittleFS needs working slack for metadata, rename, and
  temp writes, so "refuse over-fill" is not enough. Concrete policy: **keep ≥1–2 dir-blocks
  free (~32–64 KB reserve), plus a max-single-file threshold**, and never fill past the
  reserve. This is load-bearing — the wedge bug (Phase 0) is triggered by hitting hard-full,
  and swapping is exactly what pushes toward full.
- **Atomic swap:** **upload-then-delete when room allows**; a failed upload must never leave
  the device with neither copy. When capacity is tight (delete-first required), stage the
  order so a partial failure leaves a consistent, re-syncable state (source is always safe on
  the PC shelf).

### 5. Discovery — DHCP reservation now; mDNS deferred
No mDNS in firmware today, and the device IP already drifted (.182 → .191). A headless agent
on a stale IP fails **silently**. Decision: **DHCP reservation** (pin the device MAC to a
fixed IP in the router, one-time), with the agent taking a **configurable IP** as the
always-works floor. Rejected-for-now: **mDNS** (`vialucis.local`) — new flaky firmware that
needs a manual fallback anyway; **deferred** as a future firmware add for out-of-box stranger
discovery. Subnet-scan rejected (heavy, brittle on segmented nets). Document the reservation
step for strangers.

### 6. The device stays ignorant of the PC shelf
The device UI shows **only what is on flash** — no "available on PC" list, no live PC link.
Trivially satisfied by the folder-agent shape (the shelf is just a folder). Firmware cost
beyond the free-space field = zero.

### 7. Watched-folder semantics (Phase 2 contract — pin before building the agent)
If/when the agent is built, define the selected-set contract explicitly (left open by this
grill, flagged by the outside voice): recommended shape = a **`device/` subfolder** inside
the library folder IS the downloaded set — drop a MIDI in → upload; remove it → delete;
rename → delete-old + upload-new. The rest of the library folder is the shelf (never touched
on-device). Confirm this before Phase 2 build.

## Copyright / privacy

Copyrighted MIDIs live in gitignored `songs/local` and must never be published. The PC shelf
keeps them on local disk / on-LAN — fine. Cloud (C) is rejected partly because it would push
them off-box. No change to the publish-hygiene rule.

## NOT in scope

- **Big single-file playback (RAM ceiling)** — not a storage problem; needs a streaming-parse
  engine. Separate future project.
- **microSD (A2)** — deferred; part not bought. Documented future path.
- **mDNS discovery** — deferred to a future firmware wave; DHCP reservation covers it now.
- **Desktop GUI app** — rejected in favor of folder+agent.
- **True resumable/chunked upload** — needs firmware; Phase 1 does whole-file retry only.
- **Cloud / NAS (C)** — rejected (breaks standalone + copyright).

## What already exists (reuse, do not rebuild)

- Full song REST surface (`GET/POST/DELETE /api/songs`, `POST /api/songs/{name}/load`) — the
  manager is a *client*; no new device protocol. (`load` is currently route-shadowed — Phase 0.)
- `SongStore::freeBytes()` / `showTotalBytes()` already compute capacity — the free-space
  field is a JSON surface, not new logic.
- `tools/mock_device.py` — the agent's + browser flows' test target (no live board needed).
- The device is already a LAN web server serving a UI — Phase 1's bulk-manage screen extends
  `webui/`, it does not build a new surface.

## Open threads (genuinely unresolved)

- **Watched-folder semantics** (§7) — pin the `device/`-subfolder contract before Phase 2.
- **Agent stack/packaging** (Phase 2) — framework, tray vs CLI, distribution. Deferred (per
  `design-sessions-defer-code-to-fable`).
- **microSD trigger** — "when a rotating few-dozen stops being enough and Christian wants the
  whole library resident PC-free."
- **A1 partition table** — exact layout + whether it needs a one-time serial flash vs
  OTA-safe; affects the replicability setup step.

## Implementation note (deferred — no code this session)

Build order is the phased plan above. **Phase 0 (Wave A) gates everything.** Phase 1 is
device-side and replicable. Phase 2 (agent) is optional and re-decided after Phase 1. When
built, dispatch via `/via-lucis-dispatch`.
