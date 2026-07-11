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
"""
import json
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

WEBUI = Path(__file__).resolve().parent.parent / "webui" / "index.html"
PORT = 8321

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
}
settings = {
    "leftColor": "#0000FF", "rightColor": "#00FF00", "wrongColor": "#FF0000",
    "previewCap": 0.45, "leadMs": 1000,
    "offsetMm": 0.0, "ledsPerMeter": 180.0,
    "brightness": 160, "echoWindowMs": 250,
    "wifiSsid": "HomeNet", "wifiPass": "",
    "repeatCueEnabled": True, "repeatColor": "#FFFFFF",
    "repeatFillStartPct": 0, "repeatFillPeakPct": 45,
    "repeatFloorMs": 35, "repeatWaitPulseMs": 60,
    "afkTimeoutSec": 180,
}
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
    {"name": "amazing-grace.mid", "size": 2210},
    {"name": "minuet-in-g.mid", "size": 3105},
    {"name": "vivaldi-largo.mid", "size": 2874},
]
last_tick = time.time()

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
probe = {"armed": False, "led": 0, "note": None, "armed_at": 0.0,
         "timeout_s": 30.0}


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
                "wifi": {"mode": "sta", "ip": "127.0.0.1"}}

    def _body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}")

    def do_GET(self):
        if self.path == "/":
            body = WEBUI.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/api/status":
            self._json(200, self._status())
        elif self.path == "/api/songs":
            self._json(200, songs)
        elif self.path == "/api/settings":
            self._json(200, settings)
        elif self.path == "/api/ble":
            self._json(200, {"connected": True, "device": "FP-30X BLE-MIDI"})
        elif self.path == "/api/calibration":
            self._json(200, calibration)
        elif self.path == "/api/afk":
            self._json(200, afk)
        elif self.path == "/api/shows":
            self._json(200, {"formatVersion": 1,
                             "shows": [{"name": n, "size": s}
                                       for n, s in shows.items()]})
        elif self.path == "/api/calibration/probe":
            probe_tick()
            self._json(200, {"armed": probe["armed"], "led": probe["led"],
                             "note": probe["note"]})
        else:
            self._json(404, {"error": "not found"})

    def do_POST(self):
        top["last_activity"] = time.time()  # writes reset the idle clock
        if self.path == "/api/topmode":
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
        if self.path.startswith("/api/shows"):
            if self.path == "/api/shows/stop":
                show_playing[0] = None
                self._json(200, self._status())
                return
            if self.path.endswith("/play"):
                name = self.path[len("/api/shows/"):-len("/play")]
                if name not in shows:
                    self._json(404, {"error": "no such show"})
                elif not state["song"]:
                    self._json(400, {"error": "no song loaded"})
                else:
                    show_playing[0] = name
                    self._json(200, self._status())
                return
            # upload: /api/shows?name=<n>.vls
            q = urllib.parse.urlparse(self.path).query
            name = urllib.parse.parse_qs(q).get("name", [""])[0]
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
        if self.path == "/api/afk/control":
            b = self._body()
            if b.get("action") not in ("next", "previous"):
                self._json(400, {"error": "bad action"})
                return
            self._json(200, afk)
            return
        if self.path == "/api/songs/unload":
            state.update(song="", positionMs=0, state="idle",
                         loop=loop_off())
            top["presentation"] = False
            self._json(200, self._status())
            return
        if self.path == "/api/transport":
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
        elif self.path == "/api/mode":
            b = self._body()
            state["mode"] = b.get("mode", "wait")
            if state["state"] in ("playing", "waiting"):
                state["state"] = ("waiting" if state["mode"] in
                                  ("wait", "accompaniment") else "playing")
            state["pendingNotes"] = ([60, 64, 67] if state["mode"] in
                                     ("wait", "accompaniment") else [])
            self._json(200, self._status())
        elif self.path == "/api/tempo":
            state["tempoPercent"] = max(
                1, min(500, self._body().get("percent", 100)))
            self._json(200, self._status())
        elif self.path == "/api/loop":
            b = self._body()
            state["loop"] = {"enabled": bool(b.get("enabled")),
                             "startMs": int(b.get("startMs", 0)),
                             "endMs": int(b.get("endMs", 0))}
            self._json(200, self._status())
        elif self.path == "/api/calibration/probe":
            if state["state"] in ("playing", "waiting"):
                self._json(409, {"error": "playing"})
                return
            b = self._body()
            led = int(b.get("led", -1))
            if led < 0 or led >= LED_COUNT:
                self._json(400, {"error": "bad led"})
                return
            probe.update(armed=True, led=led, note=None,
                         armed_at=time.time(),
                         timeout_s=min(300, max(1, int(b.get(
                             "timeoutMs", 30000)) / 1000)))
            self._json(200, {"armed": True, "led": led, "note": None})
        elif self.path == "/api/test":
            self._json(200, {})
        elif self.path == "/api/reboot":
            self._json(200, {})
        elif self.path.startswith("/api/songs/") and self.path.endswith("/load"):
            state["song"] = self.path[len("/api/songs/"):-len("/load")]
            state["positionMs"] = 0
            state["state"] = "idle"
            # Loop resets on song load, matching the firmware (F2/A34).
            state["loop"] = loop_off()
            self._json(200, self._status())
        elif self.path == "/api/songs":
            self._json(201, {"name": "uploaded.mid"})
        else:
            self._json(404, {"error": "not found"})

    def do_PUT(self):
        top["last_activity"] = time.time()
        if self.path == "/api/afk":
            b = self._body()
            for t in b.get("tracks", []):
                if t.get("effect") not in AFK_EFFECTS:
                    self._json(400, {"error": "unknown effect: %s"
                                     % t.get("effect")})
                    return
            afk.update(b)
            self._json(200, afk)
            return
        if self.path == "/api/settings":
            settings.update(self._body())
            self._json(200, settings)
        elif self.path == "/api/calibration":
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
        elif self.path.startswith("/api/tracks/"):
            idx = int(self.path.rsplit("/", 1)[1])
            b = self._body()
            if idx < len(state["tracks"]):
                state["tracks"][idx]["hand"] = b.get("hand", "both")
                state["tracks"][idx]["lights"] = bool(b.get("lights", True))
            self._json(200, self._status())
        else:
            self._json(404, {"error": "not found"})

    def do_DELETE(self):
        if self.path.startswith("/api/shows/"):
            name = self.path[len("/api/shows/"):]
            if shows.pop(name, None) is None:
                self._json(404, {"error": "no such show"})
            else:
                self.send_response(204)
                self._cors()
                self.end_headers()
            return
        if self.path == "/api/calibration/probe":
            probe.update(armed=False, note=None)
            self._json(200, {"armed": False, "led": probe["led"],
                             "note": None})
            return
        self._json(204, {})

    def log_message(self, *a):  # quiet
        pass


if __name__ == "__main__":
    if not WEBUI.is_file():
        raise SystemExit(f"webui/index.html not found at {WEBUI}")
    print(f"Mock Via Lucis at http://localhost:{PORT}")
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
