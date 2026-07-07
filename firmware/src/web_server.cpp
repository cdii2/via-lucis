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

void onJsonBody(const char* path, JsonHandler handle) {
    gServer.on(
        path, HTTP_POST | HTTP_PUT,
        [](AsyncWebServerRequest* req) {
            // All replies come from the body handler — which a bodyless
            // request never invokes. Answer instead of hanging the
            // connection (A30/A32 protocol hygiene).
            if (!req->_tempObject) sendError(req, 400, "empty body");
        },
        nullptr,
        [handle](AsyncWebServerRequest* req, uint8_t* data, size_t len,
                 size_t index, size_t total) {
            BodyIntake& in = intakeFor(req);
            if (in.failed) return;  // rejected earlier; drain silently
            ChunkPlan plan = planChunk(index, len, total, kJsonBodyCap);
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
                   if (!Settings::fromJson(raw.c_str(), app.settings())) {
                       sendError(req, 400, "bad settings");
                       return;
                   }
                   app.applySettings();
                   sendJson(req, 200, app.settings().toJson());
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
