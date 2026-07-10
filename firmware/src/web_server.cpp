#include "web_server.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "vialucis/body_intake.h"

#if __has_include("webui_gz.h")
#include "webui_gz.h"
#define VIALUCIS_HAVE_WEBUI 1
#endif

namespace vialucis {
namespace {

AsyncWebServer gServer(80);

void sendJson(AsyncWebServerRequest* req, int code, const std::string& body) {
    req->send(code, "application/json", body.c_str());
}

void sendError(AsyncWebServerRequest* req, int code, const char* msg) {
    JsonDocument doc;
    doc["error"] = msg;
    std::string out;
    serializeJson(doc, out);
    sendJson(req, code, out);
}

// --- body intake (R6) --------------------------------------------------
// One discipline for every request body. BodyIntake is the ONLY thing ever
// stored in _tempObject, always heap-allocated, always deleted-and-nulled
// on disconnect — the request destructor's blanket free(_tempObject) then
// never fires on it (the old upload path parked (void*)1 there, which that
// free() would have chewed on). Chunk boundary math lives in core
// (vialucis/body_intake.h); the two sinks below act on its decisions.

constexpr size_t kJsonBodyCap = 4096;
// A full per-key calibration PUT (88 keys) runs ~3.5KB; give the document
// route real headroom instead of sailing 15% under the generic cap.
constexpr size_t kCalibrationBodyCap = 12288;

struct BodyIntake {
    String buf;            // buffer sink accumulates here
    std::string name;      // stream sink: validated once on the first chunk
    bool sawBody = false;  // stream sink: at least one chunk accepted
    bool failed = false;   // reply already sent — swallow remaining chunks
};

BodyIntake& intakeFor(AsyncWebServerRequest* req) {
    BodyIntake* st = static_cast<BodyIntake*>(req->_tempObject);
    if (!st) {
        st = new BodyIntake();
        req->_tempObject = st;
        req->onDisconnect([req]() {
            delete static_cast<BodyIntake*>(req->_tempObject);
            req->_tempObject = nullptr;
        });
    }
    return *st;
}

void intakeFail(AsyncWebServerRequest* req, int code, const char* msg) {
    intakeFor(req).failed = true;
    sendError(req, code, msg);
}

// Buffer sink: collect a small JSON body across chunks, then hand it to
// `handle`.
using JsonHandler =
    std::function<void(AsyncWebServerRequest*, JsonDocument&)>;

void onJsonBody(const char* path, JsonHandler handle,
                size_t bodyCap = kJsonBodyCap) {
    gServer.on(
        path, HTTP_POST | HTTP_PUT,
        [](AsyncWebServerRequest* req) {
            // All replies come from the body handler — which a bodyless
            // request never invokes. Answer instead of hanging the
            // connection (A30/A32 protocol hygiene).
            if (!req->_tempObject) sendError(req, 400, "empty body");
        },
        nullptr,
        [handle, bodyCap](AsyncWebServerRequest* req, uint8_t* data,
                          size_t len, size_t index, size_t total) {
            BodyIntake& in = intakeFor(req);
            if (in.failed) return;  // rejected earlier; drain silently
            ChunkPlan plan = planChunk(index, len, total, bodyCap);
            if (plan.tooLarge) {
                intakeFail(req, 413, "body too large");
                return;
            }
            if (plan.first) in.buf.reserve(total);
            in.buf.concat(reinterpret_cast<const char*>(data), len);
            if (!plan.last) return;  // more chunks coming
            JsonDocument doc;
            if (deserializeJson(doc, in.buf.c_str()) !=
                DeserializationError::Ok) {
                sendError(req, 400, "bad json");
                return;
            }
            handle(req, doc);
        });
}

}  // namespace

void WebServerLayer::begin(App& app, WifiManager& wifi) {
    // --- UI ------------------------------------------------------------
#ifdef VIALUCIS_HAVE_WEBUI
    gServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(
            200, "text/html", kWebUiGz, kWebUiGzLen);
        res->addHeader("Content-Encoding", "gzip");
        req->send(res);
    });
#else
    gServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain",
                  "Via Lucis firmware built without web UI (run "
                  "webui/build.py)");
    });
#endif

    // --- status ----------------------------------------------------------
    gServer.on("/api/status", HTTP_GET,
               [&app, &wifi](AsyncWebServerRequest* req) {
                   WifiStatus ws{wifi.isAp() ? "ap" : "sta", wifi.ip()};
                   sendJson(req, 200, app.statusJson(&ws));
               });

    gServer.on("/api/ble", HTTP_GET, [&app](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["connected"] = app.ble().connected();
        doc["device"] = app.ble().connected() ? "BLE-MIDI" : "";
        std::string out;
        serializeJson(doc, out);
        sendJson(req, 200, out);
    });

    // --- songs -----------------------------------------------------------
    gServer.on("/api/songs", HTTP_GET, [&app](AsyncWebServerRequest* req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const SongFileInfo& s : app.store().list()) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = s.name;
            o["size"] = s.size;
        }
        std::string out;
        serializeJson(doc, out);
        sendJson(req, 200, out);
    });

    // Raw-body upload (stream sink): /api/songs?name=<file>.mid
    gServer.on(
        "/api/songs", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            // Final response is sent from the body handler; this guards the
            // no-body case (and stays quiet if a reply already went out).
            BodyIntake* st = static_cast<BodyIntake*>(req->_tempObject);
            if (st && st->failed) return;
            if (!st || !st->sawBody) sendError(req, 400, "empty upload");
        },
        nullptr,
        [&app](AsyncWebServerRequest* req, uint8_t* data, size_t len,
               size_t index, size_t total) {
            BodyIntake& in = intakeFor(req);
            if (in.failed) return;  // rejected earlier; drain silently
            ChunkPlan plan =
                planChunk(index, len, total, SongStore::kMaxSongBytes);
            if (plan.first) {  // name/size can't change mid-request
                if (!req->hasParam("name")) {
                    intakeFail(req, 400, "missing ?name=");
                    return;
                }
                in.name = req->getParam("name")->value().c_str();
                if (!SongStore::validName(in.name)) {
                    intakeFail(req, 400, "bad name (want *.mid)");
                    return;
                }
                if (plan.tooLarge) {
                    intakeFail(req, 413, "file too large (256KB max)");
                    return;
                }
            }
            in.sawBody = true;
            if (!app.store().appendChunk(in.name, data, len, plan.first)) {
                intakeFail(req, 500, "write failed");
                return;
            }
            if (plan.last) {
                JsonDocument doc;
                doc["name"] = in.name;
                std::string out;
                serializeJson(doc, out);
                sendJson(req, 201, out);
            }
        });

    // Exact route BEFORE the regex ones so "unload" can never be taken
    // for a song name.
    gServer.on("/api/songs/unload", HTTP_POST,
               [&app](AsyncWebServerRequest* req) {
                   app.unloadSong();
                   sendJson(req, 200, app.statusJson());
               });

    gServer.on("^\\/api\\/songs\\/([^\\/]+)\\/load$", HTTP_POST,
               [&app](AsyncWebServerRequest* req) {
                   std::string name = req->pathArg(0).c_str();
                   if (!app.loadSong(name)) {
                       sendError(req, 400, "cannot load song");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    gServer.on("^\\/api\\/songs\\/([^\\/]+)$", HTTP_DELETE,
               [&app](AsyncWebServerRequest* req) {
                   std::string name = req->pathArg(0).c_str();
                   if (!app.store().remove(name)) {
                       sendError(req, 404, "no such song");
                       return;
                   }
                   req->send(204);
               });

    // --- tracks ------------------------------------------------------------
    onJsonBody("^\\/api\\/tracks\\/([0-9]+)$",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   size_t index = req->pathArg(0).toInt();
                   std::string hand = doc["hand"] | "both";
                   bool lights = doc["lights"] | true;
                   if (!app.setTrack(index, hand, lights)) {
                       sendError(req, 400, "bad track or hand");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    // --- transport / mode / tempo / loop ---------------------------------
    onJsonBody("/api/transport",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string action = doc["action"] | "";
                   uint32_t posMs = doc["positionMs"] | 0;
                   if (!app.transport(action, posMs)) {
                       sendError(req, 400, "bad action");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    onJsonBody("/api/mode",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string mode = doc["mode"] | "";
                   std::string practice = doc["practice"] | "both";
                   if (!app.setMode(mode, practice)) {
                       sendError(req, 400, "bad mode");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    onJsonBody("/api/tempo",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   float percent = doc["percent"] | 100.0f;
                   if (!app.setTempo(percent)) {
                       sendError(req, 400, "no song loaded");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    onJsonBody("/api/loop",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   bool enabled = doc["enabled"] | false;
                   uint32_t startMs = doc["startMs"] | 0;
                   uint32_t endMs = doc["endMs"] | 0;
                   if (!app.setLoop(enabled, startMs, endMs)) {
                       sendError(req, 400, "bad loop range");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    // --- settings ----------------------------------------------------------
    gServer.on("/api/settings", HTTP_GET,
               [&app](AsyncWebServerRequest* req) {
                   sendJson(req, 200, app.settings().toJson());
               });

    onJsonBody("/api/settings",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string raw;
                   serializeJson(doc, raw);
                   // An EDIT of the 2-point scalars reverts geometry to the
                   // 2-point tier (the documented dials-win rule); any other
                   // settings change leaves calibration untouched.
                   float oldOff = app.settings().offsetMm;
                   float oldLpm = app.settings().ledsPerMeter;
                   if (!Settings::fromJson(raw.c_str(), app.settings())) {
                       sendError(req, 400, "bad settings");
                       return;
                   }
                   bool scalarsChanged =
                       app.settings().offsetMm != oldOff ||
                       app.settings().ledsPerMeter != oldLpm;
                   app.applySettings(scalarsChanged);
                   sendJson(req, 200, app.settings().toJson());
               });

    // --- top mode (M3) -----------------------------------------------------
    onJsonBody("/api/topmode",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string mode = doc["mode"] | "";
                   bool on;
                   if (mode == "presentation") on = true;
                   else if (mode == "practice") on = false;
                   else {
                       sendError(req, 400, "bad mode");
                       return;
                   }
                   if (!app.setPresentation(on)) {
                       sendError(req, 400, "no song loaded");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    // --- shows (P2; SHOW-FORMAT.md §3) -----------------------------------
    gServer.on("/api/shows", HTTP_GET, [&app](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["formatVersion"] = 1;
        JsonArray arr = doc["shows"].to<JsonArray>();
        for (const SongFileInfo& s : app.store().listShows()) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = s.name;
            o["size"] = s.size;
        }
        std::string out;
        serializeJson(doc, out);
        sendJson(req, 200, out);
    });

    // Raw-body show upload (stream sink, mirrors the song upload).
    gServer.on(
        "/api/shows", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            BodyIntake* st = static_cast<BodyIntake*>(req->_tempObject);
            if (st && st->failed) return;
            if (!st || !st->sawBody) sendError(req, 400, "empty upload");
        },
        nullptr,
        [&app](AsyncWebServerRequest* req, uint8_t* data, size_t len,
               size_t index, size_t total) {
            BodyIntake& in = intakeFor(req);
            if (in.failed) return;
            ChunkPlan plan =
                planChunk(index, len, total, SongStore::kMaxShowBytes);
            if (plan.first) {
                if (!req->hasParam("name")) {
                    intakeFail(req, 400, "missing ?name=");
                    return;
                }
                in.name = req->getParam("name")->value().c_str();
                if (!SongStore::validShowName(in.name)) {
                    intakeFail(req, 400, "bad name (want *.vls)");
                    return;
                }
                if (plan.tooLarge) {
                    intakeFail(req, 413, "show too large");
                    return;
                }
                if (app.showBusy()) {  // never race a live render (OV1)
                    intakeFail(req, 409, "busy");
                    return;
                }
                if (app.store().listShows().size() >=
                        SongStore::kMaxShowCount ||
                    app.store().showTotalBytes() + total >
                        SongStore::kMaxShowTotalBytes) {
                    intakeFail(req, 507, "show storage full");
                    return;
                }
            }
            in.sawBody = true;
            if (!app.store().appendShowChunk(in.name, data, len,
                                             plan.first)) {
                intakeFail(req, 500, "write failed");
                return;
            }
            if (plan.last) {
                JsonDocument doc;
                doc["name"] = in.name;
                std::string out;
                serializeJson(doc, out);
                sendJson(req, 201, out);
            }
        });

    gServer.on("/api/shows/stop", HTTP_POST,
               [&app](AsyncWebServerRequest* req) {
                   app.stopShow();
                   sendJson(req, 200, app.statusJson());
               });

    gServer.on("^\\/api\\/shows\\/([^\\/]+)\\/play$", HTTP_POST,
               [&app](AsyncWebServerRequest* req) {
                   std::string name = req->pathArg(0).c_str();
                   std::string err;
                   switch (app.playShow(name, &err)) {
                       case App::ShowPlay::Ok:
                           sendJson(req, 200, app.statusJson());
                           return;
                       case App::ShowPlay::NotFound:
                           sendError(req, 404, "no such show");
                           return;
                       case App::ShowPlay::BadStream:
                           sendError(req, 400, err.c_str());
                           return;
                       case App::ShowPlay::NoSong:
                           sendError(req, 400, "no song loaded");
                           return;
                       case App::ShowPlay::Busy:
                           sendError(req, 409, "busy");
                           return;
                   }
               });

    gServer.on("^\\/api\\/shows\\/([^\\/]+)$", HTTP_DELETE,
               [&app](AsyncWebServerRequest* req) {
                   std::string name = req->pathArg(0).c_str();
                   if (!app.store().removeShow(name)) {
                       sendError(req, 404, "no such show");
                       return;
                   }
                   req->send(204);
               });

    // --- AFK playlist (E3) ---------------------------------------------
    gServer.on("/api/afk", HTTP_GET, [&app](AsyncWebServerRequest* req) {
        sendJson(req, 200, app.afkJson());
    });

    onJsonBody("/api/afk",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string raw;
                   serializeJson(doc, raw);
                   std::string err;
                   if (!app.applyAfk(raw.c_str(), &err)) {
                       sendError(req, 400, err.c_str());
                       return;
                   }
                   sendJson(req, 200, app.afkJson());
               });

    onJsonBody("/api/afk/control",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string action = doc["action"] | "";
                   if (!app.afkControl(action)) {
                       sendError(req, 400, "bad action");
                       return;
                   }
                   sendJson(req, 200, app.afkJson());
               });

    // --- calibration (C3) --------------------------------------------------
    gServer.on("/api/calibration", HTTP_GET,
               [&app](AsyncWebServerRequest* req) {
                   sendJson(req, 200, app.calibrationJson());
               });

    onJsonBody(
        "/api/calibration",
        [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
            std::string raw;
            serializeJson(doc, raw);
            CalibResult r = app.applyCalibration(raw.c_str());
            if (!r.ok()) {
                sendError(req, 400, r.message());
                return;
            }
            sendJson(req, 200, app.calibrationJson());
        },
        kCalibrationBodyCap);

    gServer.on("/api/calibration/probe", HTTP_GET,
               [&app](AsyncWebServerRequest* req) {
                   sendJson(req, 200, app.probeJson());
               });

    gServer.on("/api/calibration/probe", HTTP_DELETE,
               [&app](AsyncWebServerRequest* req) {
                   app.cancelProbe();
                   sendJson(req, 200, app.probeJson());
               });

    onJsonBody("/api/calibration/probe",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   if (!doc["led"].is<uint16_t>()) {
                       sendError(req, 400, "missing led");
                       return;
                   }
                   uint16_t led = doc["led"].as<uint16_t>();
                   uint32_t timeoutMs = doc["timeoutMs"] | 30000u;
                   ModeDirector::ProbeArm r = app.armProbe(led, timeoutMs);
                   if (r == ModeDirector::ProbeArm::Playing) {
                       sendError(req, 409, "playing");
                       return;
                   }
                   if (r == ModeDirector::ProbeArm::BadLed) {
                       sendError(req, 400, "bad led");
                       return;
                   }
                   sendJson(req, 200, app.probeJson());
               });

    // --- utility -----------------------------------------------------------
    onJsonBody("/api/test",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string pattern = doc["pattern"] | "";
                   if (!app.setTestPattern(pattern)) {
                       sendError(req, 400, "bad pattern");
                       return;
                   }
                   sendJson(req, 200, "{}");
               });

    gServer.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{}");
        delay(200);
        ESP.restart();
    });

    gServer.onNotFound([](AsyncWebServerRequest* req) {
        sendError(req, 404, "not found");
    });

    gServer.begin();
}

}  // namespace vialucis
