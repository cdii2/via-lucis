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
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

WEBUI = Path(__file__).resolve().parent.parent / "webui" / "index.html"
PORT = 8321

state = {
    "song": "clair-de-lune.mid",
    "state": "waiting",          # idle | playing | waiting | finished
    "mode": "wait",
    "positionMs": 23400,
    "durationMs": 96000,
    "tempoPercent": 100,
    "loop": {"enabled": False, "startMs": 0, "endMs": 0},
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
}
songs = [
    {"name": "clair-de-lune.mid", "size": 4321},
    {"name": "ode-to-joy.mid", "size": 1290},
    {"name": "amazing-grace.mid", "size": 2210},
    {"name": "minuet-in-g.mid", "size": 3105},
    {"name": "vivaldi-largo.mid", "size": 2874},
]
last_tick = time.time()


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
    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _status(self):
        advance()
        return {"version": "0.1.0-mock", **state,
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
        else:
            self._json(404, {"error": "not found"})

    def do_POST(self):
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
        elif self.path == "/api/test":
            self._json(200, {})
        elif self.path == "/api/reboot":
            self._json(200, {})
        elif self.path.startswith("/api/songs/") and self.path.endswith("/load"):
            state["song"] = self.path[len("/api/songs/"):-len("/load")]
            state["positionMs"] = 0
            state["state"] = "idle"
            # Loop resets on song load, matching the firmware (F2/A34).
            state["loop"] = {"enabled": False, "startMs": 0, "endMs": 0}
            self._json(200, self._status())
        elif self.path == "/api/songs":
            self._json(201, {"name": "uploaded.mid"})
        else:
            self._json(404, {"error": "not found"})

    def do_PUT(self):
        if self.path == "/api/settings":
            settings.update(self._body())
            self._json(200, settings)
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
        self._json(204, {})

    def log_message(self, *a):  # quiet
        pass


if __name__ == "__main__":
    if not WEBUI.is_file():
        raise SystemExit(f"webui/index.html not found at {WEBUI}")
    print(f"Mock Via Lucis at http://localhost:{PORT}")
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
