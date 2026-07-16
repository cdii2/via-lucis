"""Mock Via Lucis device — preview the web UI with no ESP32 and no hardware.

Serves webui/index.html plus a live-feeling fake of the REST API in
docs/API.md: transport, mode, tempo, loop, track and settings commands all
mutate an in-memory state, position advances while "playing", and wait mode
holds a pending C-major chord. Handy for UI work, screenshots, and kicking
the tires before your parts arrive.

Run (from anywhere, stdlib only):

    python tools/mock_device.py

then open http://localhost:8321

This is a UI preview, not a firmware simulator — the scheduler, wait-mode
matcher and LED pipeline are natively tested instead (see docs/SIMULATOR.md).

L2 (2026-07-16, wl2/library): route matching now splits path from query
string EVERYWHERE (see `_split`) — the mock's own R1-style bug was the
mirror image of the real one: `self.path == "/api/songs"` never matches
`POST /api/songs?name=foo.mid`, so every song upload 404'd. Song upload is
now a real handler that mirrors web_server.cpp's validation order (missing
name -> bad name -> too-large -> free-space precheck -> loaded-name 409) and
storage_budget.h's block-aware math exactly, and GET /api/status reports
fsFree/fsTotal/fsUsed that actually deplete as songs upload, so the webui's
capacity bar and upload margin guard (T6) have something real to react to.
A `/api/mock/fail-uploads` control endpoint (NOT part of docs/API.md — a
mock-only test hook) lets a driver script make the next N uploads drop the
connection with no reply, to exercise T5's retry path deterministically.
"""
import json
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

WEBUI = Path(__file__).resolve().parent.parent / "webui" / "index.html"
PORT = 8321
START_TIME = time.time()

# --- storage budget (L2) ------------------------------------------------
# Mirrors firmware/lib/core/src/vialucis/storage_budget.h EXACTLY: LittleFS
# allocates whole blocks, and every upload must leave a safety reserve free
# (the wedge bug's fix) on top of its block-rounded footprint.
FS_BLOCK_BYTES = 4096
UPLOAD_RESERVE_BYTES = 32 * 1024
MAX_SONG_BYTES = 256 * 1024
# 0x1E0000 = 1,966,080 bytes: the ACTUAL post-A1 `spiffs` partition size
# (ASSUMPTIONS A121, partitions.csv), so the capacity bar/margin guard see
# a realistic, finite total rather than an arbitrary mock number.
FS_TOTAL_BYTES = 0x1E0000
SONG_NAME_CHARSET = set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. ")


def round_up_to_block(n, block=FS_BLOCK_BYTES):
    return n if block <= 0 else ((n + block - 1) // block) * block


def upload_fits(incoming, free_bytes, reserve=UPLOAD_RESERVE_BYTES,
                block=FS_BLOCK_BYTES):
    """Mirrors vialucis::uploadFits (storage_budget.h) bit for bit."""
    return round_up_to_block(incoming, block) + reserve <= free_bytes


def valid_song_name(name):
    """Mirrors SongStore::validName (song_store.cpp): 5-64 chars, exact
    lowercase ".mid" suffix, restricted charset — no case-folding, the
    firmware does a plain string compare."""
    if not (5 <= len(name) <= 64):
        return False
    if not name.endswith(".mid"):
        return False
    return all(c in SONG_NAME_CHARSET for c in name)


def loop_off():
    return {"enabled": False, "startMs": 0, "endMs": 0}


state = {
    "song": "clair-de-lune.mid",
    "state": "waiting",          # idle | playing | waiting | finished
    "mode": "wait",
    "positionMs": 23400,
    "durationMs": 96000,
    "tempoPercent": 100,
    "loop": loop_off(),
    "tracks": [
        {"index": 0, "name": "Right", "hand": "right", "lights": True},
        {"index": 1, "name": "Left", "hand": "left", "lights": True},
    ],
    "pendingNotes": [60, 64, 67],  # C4-E4-G4 chord owed in wait mode
    "practice": "both",  # C1: player's last-chosen practice hand (webui Wave C)
}
settings = {
    "leftColor": "#0000FF", "rightColor": "#00FF00", "wrongColor": "#FF0000",
    "previewCap": 0.45, "leadMs": 1000,
    "offsetMm": 0.0, "ledsPerMeter": 180.0,
    "brightness": 160, "echoWindowMs": 250,
    "wifiSsid": "HomeNet",
    "repeatCueEnabled": True, "repeatColor": "#FFFFFF",
    "repeatFillStartPct": 0, "repeatFillPeakPct": 45,
    "repeatFloorMs": 35, "repeatWaitPulseMs": 60,
    "afkTimeoutSec": 180,
}
# C4 (webui Wave C, confirmed contract from wc/settings): wifiPass is
# write-only. GET/PUT replies carry `wifiPassSet` instead of the literal
# password; the actual value lives only here, never serialized out.
wifi_pass_actual = ""
top = {"presentation": False, "last_activity": time.time()}
afk = {
    "tracks": [{"effect": "pacifica", "palette": ""},
               {"effect": "fire2012", "palette": "heat"}],
    "shuffle": False, "repeatCurrent": False, "dwellSec": 60,
    "crossfadeMs": 2000, "brightnessCap": 96, "masterSpeed": 1.0,
    "aboveKeysOnly": False,
}
# SYNC NOTE: mirrors effectNames() in fx/effect_factory.cpp (+ webui + API.md).
AFK_EFFECTS = {"fire2012", "pacifica", "twinklefox", "colorwaves"}
shows = {}  # name -> size (P2 mock: enough for the editor's flows)
show_playing = [None]
songs = [
    {"name": "clair-de-lune.mid", "size": 4321},
    {"name": "ode-to-joy.mid", "size": 1290},
    {"name": "amazing-grace.mid", "size": 2210, "parseOk": True},
    {"name": "minuet-in-g.mid", "size": 3105},
    {"name": "vivaldi-largo.mid", "size": 2874},
    # C2 (webui Wave C): test data for the parseOk badge feature-detect —
    # no real firmware field exists yet (see ASKS FOR C-LEAD), but the
    # mock lets the badge rendering be exercised end-to-end regardless.
    {"name": "corrupted-partial.mid", "size": 512, "parseOk": False},
]
last_tick = time.time()

# L2: deterministic upload-failure hook for T5's retry drill (mock-only,
# see /api/mock/fail-uploads below — never a real device route).
fail_hook = {"uploads_left": 0}


def fs_used_bytes():
    """Block-rounded footprint of everything sharing the one LittleFS
    partition the mock knows about (songs + shows) — same spirit as real
    LittleFS.usedBytes() being block-granular, not a raw byte sum."""
    used = sum(round_up_to_block(s["size"]) for s in songs)
    used += sum(round_up_to_block(sz) for sz in shows.values())
    return used


def fs_free_bytes():
    return max(0, FS_TOTAL_BYTES - fs_used_bytes())


# --- calibration (C4) -------------------------------------------------
# Mock geometry: LED 2 sits under A0 (note 21), ~3.85 LEDs per key, so the
# wizard's probe captures a believable note for any LED. Set REVERSED=True
# to rehearse a right-to-left install. LEDs below NO_KEY_BELOW have no key
# beneath them — probes there never capture (exercises timeout / move-dot).
LED_COUNT = 360
LEDS_PER_KEY = 3.85
FIRST_KEY_LED = 2.0
REVERSED = False
NO_KEY_BELOW = 2
CAPTURE_DELAY_S = 1.2

def default_two_point_keys():
    return [{"note": 21 + k,
             "first": int(FIRST_KEY_LED + k * LEDS_PER_KEY),
             "last": int(FIRST_KEY_LED + k * LEDS_PER_KEY) + 2}
            for k in range(88)]


calibration = {
    "tier": "twoPoint", "reversed": False, "ledCount": LED_COUNT,
    "offsetMm": 0.0, "ledsPerMeter": 180.0,
    "keys": default_two_point_keys(),
}
# D (2026-07-16, wd/wizard): added `timedOut` — mirrors the real firmware's
# probeJson() field (docs/API.md, B6d/A119): true only when an armed window
# expired without a capture, reset to false on the NEXT arm or an explicit
# cancel (never true after a cancel). This is what lets the wizard's error
# panel distinguish "gave up waiting" from "you cancelled".
probe = {"armed": False, "led": 0, "note": None, "armed_at": 0.0,
         "timeout_s": 30.0, "timedOut": False}
# D (2026-07-16, wd/wizard): mock-only test-control flag (NOT part of
# docs/API.md — same spirit as fail_hook/A153) letting an E2E driver force
# the probe-arm-while-recording 409 without building out a full Record mock
# (that feature has no mock support at all yet; out of Wave D's scope).
recording_mock = {"active": False}


def note_under_led(led):
    """The key the mock piano-owner would press under this LED (or None)."""
    if led < NO_KEY_BELOW:
        return None
    pos = (LED_COUNT - 1 - led) if REVERSED else led
    key = round((pos - FIRST_KEY_LED) / LEDS_PER_KEY)
    if key < 0 or key > 87:
        return None
    return 21 + key


def probe_tick():
    """Simulate the human: ~1.2s after arming, press the key under the dot."""
    if not probe["armed"]:
        return
    elapsed = time.time() - probe["armed_at"]
    if elapsed >= probe["timeout_s"]:
        probe["armed"] = False  # timed out, no capture
        probe["timedOut"] = True
        return
    if elapsed >= CAPTURE_DELAY_S:
        note = note_under_led(probe["led"])
        if note is not None:
            probe["note"] = note
            probe["armed"] = False
            top["last_activity"] = time.time()  # a key press is activity
        # else: no key under the dot — keep waiting until timeout


def advance():
    """Fake playback: position advances while 'playing'; 'waiting' holds."""
    global last_tick
    now = time.time()
    dt = now - last_tick
    last_tick = now
    if state["state"] == "playing":
        state["positionMs"] = int(
            state["positionMs"] + dt * 1000 * state["tempoPercent"] / 100)
        loop = state["loop"]
        if loop["enabled"] and state["positionMs"] >= loop["endMs"]:
            state["positionMs"] = loop["startMs"]
        if state["positionMs"] >= state["durationMs"]:
            state["positionMs"] = state["durationMs"]
            state["state"] = "finished"


class Handler(BaseHTTPRequestHandler):
    # CORS the DEVICE will need for the off-device editor (VL3/P-POC):
    # any origin (incl. the "null" of file:// pages), JSON bodies, and the
    # Chrome Private-Network-Access preflight header.
    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods",
                         "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Private-Network", "true")

    def do_OPTIONS(self):  # preflight for cross-origin JSON requests
        self.send_response(204)
        self._cors()
        self.end_headers()

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def _status(self):
        advance()
        idle = int(time.time() - top["last_activity"])
        has_song = bool(state["song"])
        if has_song:
            mode = "presentation" if top["presentation"] else "practice"
        elif settings["afkTimeoutSec"] and idle >= settings["afkTimeoutSec"]:
            mode = "afk"
        else:
            mode = "reactive"
        return {"version": "0.1.0-mock", **state,
                "topMode": mode, "idleSec": idle,
                "afkTimeoutSec": settings["afkTimeoutSec"],
                # L2: capacity fields the webui's storage gauge (A5) and
                # T6 margin guard read — real math (storage_budget.h),
                # real depletion as songs/shows are uploaded/deleted.
                "fs": "ok",
                "fsFree": fs_free_bytes(),
                "fsTotal": FS_TOTAL_BYTES,
                "fsUsed": fs_used_bytes(),
                "heapFree": 142000,
                "heapMaxAlloc": 110000,
                "uptimeMs": int((time.time() - START_TIME) * 1000),
                "configReset": False,
                "wifi": {"mode": "sta", "ip": "127.0.0.1"}}

    def _body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}")

    @staticmethod
    def _split(raw_path):
        """L2: path vs query split, used EVERYWHERE below. The mock's own
        R1-style bug was the mirror image of the real route-shadowing bug —
        `self.path == "/api/songs"` never matches `POST
        /api/songs?name=foo.mid` (query string breaks exact equality), so
        uploads 404'd silently. Route on `path` only; read `query` (a
        name -> [values] dict, like urllib.parse.parse_qs) for params."""
        parts = urllib.parse.urlsplit(raw_path)
        return parts.path, urllib.parse.parse_qs(parts.query)

    def do_GET(self):
        path, _query = self._split(self.path)
        if path == "/":
            body = WEBUI.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif path == "/api/status":
            self._json(200, self._status())
        elif path == "/api/songs":
            self._json(200, songs)
        elif path == "/api/settings":
            out = dict(settings)
            out["wifiPassSet"] = bool(wifi_pass_actual)
            self._json(200, out)
        elif path == "/api/ble":
            self._json(200, {"connected": True, "device": "FP-30X BLE-MIDI"})
        elif path == "/api/calibration":
            self._json(200, calibration)
        elif path == "/api/afk":
            self._json(200, afk)
        elif path == "/api/shows":
            self._json(200, {"formatVersion": 1,
                             "shows": [{"name": n, "size": s}
                                       for n, s in shows.items()]})
        elif path == "/api/calibration/probe":
            probe_tick()
            self._json(200, {"armed": probe["armed"], "led": probe["led"],
                             "timedOut": probe["timedOut"],
                             "note": probe["note"]})
        else:
            self._json(404, {"error": "not found"})

    def _handle_song_upload(self, query):
        """POST /api/songs?name=<file>.mid — raw-body upload. Mirrors
        web_server.cpp's validation order EXACTLY: missing name -> bad name
        -> too-large (announced Content-Length) -> free-space precheck
        (507, storage_budget.h math) -> loaded-name (409) -> write. Every
        check happens before the body is read off the socket, same as the
        firmware's first-chunk precheck."""
        length = int(self.headers.get("Content-Length", 0))
        names = query.get("name") or [""]
        name = names[0]
        if not name:
            self._json(400, {"error": "missing ?name="})
            return
        if not valid_song_name(name):
            self._json(400, {"error": "bad name (want *.mid)"})
            return
        if length > MAX_SONG_BYTES:
            self._json(413, {"error": "file too large (256KB max)"})
            return
        if not upload_fits(length, fs_free_bytes()):
            self._json(507, {"error": "insufficient storage"})
            return
        if state["song"] == name:
            self._json(409, {"error": "song is loaded"})
            return
        if fail_hook["uploads_left"] > 0:
            # L2 test hook (T5 retry drill): simulate a mid-transfer network
            # failure by dropping the connection with no HTTP reply at all —
            # a real network-level failure, not a typed HTTP error status,
            # so it exercises the webui's fetch()-rejection retry path
            # rather than its 4xx/5xx handling.
            fail_hook["uploads_left"] -= 1
            self.close_connection = True
            return
        body = self.rfile.read(length)
        for s in songs:
            if s["name"] == name:
                s["size"] = len(body)
                s.pop("parseOk", None)
                break
        else:
            songs.append({"name": name, "size": len(body)})
        self._json(201, {"name": name})

    def do_POST(self):
        path, query = self._split(self.path)
        top["last_activity"] = time.time()  # writes reset the idle clock
        if path == "/api/mock/fail-uploads":
            # L2: test-only control endpoint, NOT part of docs/API.md —
            # a real device has no such route. Body {"times": N}.
            b = self._body()
            fail_hook["uploads_left"] = max(0, int(b.get("times", 1)))
            self._json(200, {"uploadsWillFail": fail_hook["uploads_left"]})
            return
        if path == "/api/mock/set-recording":
            # D (wd/wizard): test-only control endpoint, NOT part of
            # docs/API.md — same spirit as fail-uploads above. Lets an E2E
            # driver force the probe-arm-while-recording 409 (docs/API.md
            # "409 recording") without a full Record-feature mock (unbuilt;
            # out of Wave D's scope). Body {"recording": true|false}.
            b = self._body()
            recording_mock["active"] = bool(b.get("recording", False))
            self._json(200, {"recording": recording_mock["active"]})
            return
        if path == "/api/topmode":
            b = self._body()
            m = b.get("mode")
            if m == "presentation":
                if not state["song"]:
                    self._json(400, {"error": "no song loaded"})
                    return
                top["presentation"] = True
            elif m == "practice":
                top["presentation"] = False
            else:
                self._json(400, {"error": "bad mode"})
                return
            self._json(200, self._status())
            return
        if path.startswith("/api/shows"):
            if path == "/api/shows/stop":
                show_playing[0] = None
                self._json(200, self._status())
                return
            if path.endswith("/play"):
                name = path[len("/api/shows/"):-len("/play")]
                if name not in shows:
                    self._json(404, {"error": "no such show"})
                elif not state["song"]:
                    self._json(400, {"error": "no song loaded"})
                else:
                    show_playing[0] = name
                    self._json(200, self._status())
                return
            # upload: /api/shows?name=<n>.vls
            name = (query.get("name") or [""])[0]
            n = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(n)
            if not name.endswith(".vls"):
                self._json(400, {"error": "bad name (want *.vls)"})
            elif len(body) > 64 * 1024:
                self._json(413, {"error": "show too large"})
            elif len(shows) >= 16 or sum(shows.values()) + len(body) > 384 * 1024:
                self._json(507, {"error": "show storage full"})
            else:
                shows[name] = len(body)
                self._json(201, {"name": name})
            return
        if path == "/api/afk/control":
            b = self._body()
            if b.get("action") not in ("next", "previous"):
                self._json(400, {"error": "bad action"})
                return
            self._json(200, afk)
            return
        if path == "/api/songs/unload":
            state.update(song="", positionMs=0, state="idle",
                         loop=loop_off())
            top["presentation"] = False
            self._json(200, self._status())
            return
        if path.startswith("/api/songs/") and path.endswith("/rename"):
            # L2 additive fix: previously unhandled (404) — the webui's
            # renameSong() already calls this route, so the mock was lying
            # about supporting it. Mirrors API.md: 400 bad/non-.mid name,
            # 404 missing source, 409 exists.
            name = urllib.parse.unquote(path[len("/api/songs/"):-len("/rename")])
            b = self._body()
            next_name = b.get("name", "")
            if not valid_song_name(next_name):
                self._json(400, {"error": "bad name (want *.mid)"})
                return
            src = next((s for s in songs if s["name"] == name), None)
            if src is None:
                self._json(404, {"error": "no such song"})
                return
            if next_name != name and any(s["name"] == next_name for s in songs):
                self._json(409, {"error": "exists"})
                return
            src["name"] = next_name
            if state["song"] == name:
                state["song"] = next_name
            self._json(200, {"name": next_name})
            return
        if path == "/api/songs":
            self._handle_song_upload(query)
            return
        if path == "/api/transport":
            b = self._body()
            a = b.get("action")
            if a == "play":
                if state["state"] == "finished":
                    state["positionMs"] = 0
                state["state"] = ("waiting" if state["mode"] in
                                  ("wait", "accompaniment") else "playing")
            elif a == "pause":
                state["state"] = "idle"
            elif a == "stop":
                state["state"] = "idle"
                state["positionMs"] = 0
            elif a == "seek":
                state["positionMs"] = int(b.get("positionMs", 0))
            self._json(200, self._status())
        elif path == "/api/mode":
            b = self._body()
            state["mode"] = b.get("mode", "wait")
            # C1 (webui Wave C): the device is the practice-hand source of
            # truth — persist whatever the client sent so a later reload's
            # GET /api/status echoes it back for the selector to reconcile.
            if "practice" in b:
                state["practice"] = b["practice"]
            if state["state"] in ("playing", "waiting"):
                state["state"] = ("waiting" if state["mode"] in
                                  ("wait", "accompaniment") else "playing")
            state["pendingNotes"] = ([60, 64, 67] if state["mode"] in
                                     ("wait", "accompaniment") else [])
            self._json(200, self._status())
        elif path == "/api/tempo":
            state["tempoPercent"] = max(
                1, min(500, self._body().get("percent", 100)))
            self._json(200, self._status())
        elif path == "/api/loop":
            b = self._body()
            state["loop"] = {"enabled": bool(b.get("enabled")),
                             "startMs": int(b.get("startMs", 0)),
                             "endMs": int(b.get("endMs", 0))}
            self._json(200, self._status())
        elif path == "/api/calibration/probe":
            if state["state"] in ("playing", "waiting"):
                self._json(409, {"error": "playing"})
                return
            if recording_mock["active"]:
                self._json(409, {"error": "recording"})
                return
            b = self._body()
            led = int(b.get("led", -1))
            if led < 0 or led >= LED_COUNT:
                self._json(400, {"error": "bad led"})
                return
            probe.update(armed=True, led=led, note=None, timedOut=False,
                         armed_at=time.time(),
                         timeout_s=min(300, max(1, int(b.get(
                             "timeoutMs", 30000)) / 1000)))
            self._json(200, {"armed": True, "led": led, "timedOut": False,
                             "note": None})
        elif path == "/api/test":
            self._json(200, {})
        elif path == "/api/reboot":
            self._json(200, {})
        elif path.startswith("/api/songs/") and path.endswith("/load"):
            state["song"] = path[len("/api/songs/"):-len("/load")]
            state["positionMs"] = 0
            state["state"] = "idle"
            # Loop resets on song load, matching the firmware (F2/A34).
            state["loop"] = loop_off()
            self._json(200, self._status())
        else:
            self._json(404, {"error": "not found"})

    def do_PUT(self):
        path, _query = self._split(self.path)
        top["last_activity"] = time.time()
        if path == "/api/afk":
            b = self._body()
            for t in b.get("tracks", []):
                if t.get("effect") not in AFK_EFFECTS:
                    self._json(400, {"error": "unknown effect: %s"
                                     % t.get("effect")})
                    return
            afk.update(b)
            self._json(200, afk)
            return
        if path == "/api/settings":
            global wifi_pass_actual
            b = self._body()
            # C4: PATCH semantics for the write-only field — key ABSENT
            # leaves the stored password unchanged, key PRESENT (including
            # "") sets/clears it. Never let it leak back out via `settings`.
            if "wifiPass" in b:
                wifi_pass_actual = b.pop("wifiPass")
            settings.update(b)
            out = dict(settings)
            out["wifiPassSet"] = bool(wifi_pass_actual)
            self._json(200, out)
        elif path == "/api/calibration":
            b = self._body()
            tier = b.get("tier")
            if tier not in ("twoPoint", "multiPoint", "perKey"):
                self._json(400, {"error": "bad tier"})
                return
            if tier == "multiPoint":
                lm = b.get("landmarks") or []
                if len(lm) < 2:
                    self._json(400, {"error": "need at least 2 landmarks"})
                    return
                notes = [m["note"] for m in lm]
                if notes != sorted(notes) or len(set(notes)) != len(notes):
                    self._json(400,
                               {"error": "landmarks must ascend by note"})
                    return
                leds = [m["led"] for m in lm]
                desc = all(b2 < a2 for a2, b2 in zip(leds, leds[1:]))
                asc = all(b2 > a2 for a2, b2 in zip(leds, leds[1:]))
                if not (asc or desc):
                    self._json(400, {"error": "led direction inconsistent"})
                    return
                calibration.update(tier=tier, reversed=desc, landmarks=lm)
                calibration.pop("offsetMm", None)
                calibration.pop("ledsPerMeter", None)
                # Table rebuild faked: interpolate keys between landmarks.
                lo, hi = lm[0], lm[-1]
                span = max(1, hi["note"] - lo["note"])
                per = (hi["led"] - lo["led"]) / span
                keys = []
                for k in range(88):
                    n = 21 + k
                    first = int(round(lo["led"] + (n - lo["note"]) * per))
                    a2, b2 = sorted((first, first + (2 if per > 0 else -2)))
                    if a2 < 0 or b2 >= LED_COUNT:
                        continue
                    keys.append({"note": n, "first": a2, "last": b2})
                calibration["keys"] = keys
            elif tier == "perKey":
                keys = b.get("keys")
                if not isinstance(keys, list):
                    self._json(400, {"error": "missing field"})
                    return
                calibration.update(tier=tier, keys=keys)
                calibration.pop("landmarks", None)
            else:
                calibration.update(
                    tier=tier,
                    reversed=bool(b.get("reversed", False)),
                    offsetMm=b.get("offsetMm", 0.0),
                    ledsPerMeter=b.get("ledsPerMeter", 180.0))
                calibration.pop("landmarks", None)
                calibration["keys"] = default_two_point_keys()
                settings["offsetMm"] = calibration["offsetMm"]
                settings["ledsPerMeter"] = calibration["ledsPerMeter"]
            self._json(200, calibration)
        elif path.startswith("/api/tracks/"):
            idx = int(path.rsplit("/", 1)[1])
            b = self._body()
            if idx < len(state["tracks"]):
                state["tracks"][idx]["hand"] = b.get("hand", "both")
                state["tracks"][idx]["lights"] = bool(b.get("lights", True))
            self._json(200, self._status())
        else:
            self._json(404, {"error": "not found"})

    def do_DELETE(self):
        path, _query = self._split(self.path)
        if path.startswith("/api/shows/"):
            name = urllib.parse.unquote(path[len("/api/shows/"):])
            if shows.pop(name, None) is None:
                self._json(404, {"error": "no such show"})
            else:
                self.send_response(204)
                self._cors()
                self.end_headers()
            return
        if path == "/api/calibration/probe":
            # An explicit cancel always leaves timedOut false (docs/API.md) —
            # distinct from a timeout expiring on its own.
            probe.update(armed=False, note=None, timedOut=False)
            self._json(200, {"armed": False, "led": probe["led"],
                             "timedOut": False, "note": None})
            return
        # C2 (webui Wave C): DELETE /api/songs/{name} was previously
        # unhandled (fell through to the blanket 204 below, which never
        # mutated `songs` and never enforced the "song is loaded" 409) —
        # added so the unload-then-delete flow has a real 409 to exercise.
        if path.startswith("/api/songs/"):
            name = urllib.parse.unquote(path[len("/api/songs/"):])
            if name == state["song"]:
                self._json(409, {"error": "song is loaded"})
                return
            before = len(songs)
            songs[:] = [s for s in songs if s["name"] != name]
            if len(songs) == before:
                self._json(404, {"error": "no such song"})
            else:
                self.send_response(204)
                self._cors()
                self.end_headers()
            return
        self._json(204, {})

    def log_message(self, *a):  # quiet
        pass


if __name__ == "__main__":
    if not WEBUI.is_file():
        raise SystemExit(f"webui/index.html not found at {WEBUI}")
    print(f"Mock Via Lucis at http://localhost:{PORT}")
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
