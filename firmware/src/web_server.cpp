#include "web_server.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

#include "reboot_request.h"  // B7 ask 2: loop-task reboot flag
#include "vialucis/body_intake.h"
#include "vialucis/storage_budget.h"

#if __has_include("webui_gz.h")
#include "webui_gz.h"
#define VIALUCIS_HAVE_WEBUI 1
#endif

namespace vialucis {
namespace {

AsyncWebServer gServer(80);

// CORS (P2, proven by the P-POC): the off-device editor talks to these
// routes from file:// (Origin: null) and LAN pages. `*` covers null
// origins; the Private-Network-Access header keeps Chrome's public→private
// preflight happy when it starts enforcing.
void addCors(AsyncWebServerResponse* res) {
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Access-Control-Allow-Methods",
                   "GET, POST, PUT, DELETE, OPTIONS");
    res->addHeader("Access-Control-Allow-Headers", "Content-Type");
    res->addHeader("Access-Control-Allow-Private-Network", "true");
}

void sendJson(AsyncWebServerRequest* req, int code, const std::string& body) {
    AsyncWebServerResponse* res =
        req->beginResponse(code, "application/json", body.c_str());
    addCors(res);
    req->send(res);
}

// A bare 204 still needs the CORS header set, or a browser DELETE reports
// failure after the server succeeded (the fetch sees no Allow-Origin on the
// response). Route every 204 through here.
void send204(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse(204);
    addCors(res);
    req->send(res);
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
    bool failed = false;   // rejected — swallow remaining chunks
    // Deferred reply (A2): a stream-upload error records its {code,msg} here
    // and keeps DRAINING the rest of the body; the completion handler then
    // sends the real 4xx/5xx JSON. Replying mid-body used to RST the socket —
    // the client saw a connection reset instead of the actual error.
    int errCode = 0;
    std::string errMsg;
    // ONE open handle held across every TCP chunk of a stream upload (A2):
    // opened on the first chunk, written each chunk, closed on the last. The
    // old path re-opened per chunk — a LittleFS metadata commit each ~1.4 KB
    // that starved async_tcp into resets.
    fs::File file;
    // Invoked on disconnect if still set (cleared on success). A stream sink
    // that starts writing a file registers a discard here so a mid-upload
    // disconnect never leaves a truncated file counting against quota
    // (SHOW-FORMAT §3 promises the partial is discarded).
    std::function<void()> cleanup;
};

BodyIntake& intakeFor(AsyncWebServerRequest* req) {
    BodyIntake* st = static_cast<BodyIntake*>(req->_tempObject);
    if (!st) {
        st = new BodyIntake();
        req->_tempObject = st;
        req->onDisconnect([req]() {
            BodyIntake* s = static_cast<BodyIntake*>(req->_tempObject);
            if (s) {
                if (s->file) s->file.close();  // release the upload handle
                if (s->cleanup) s->cleanup();  // discard any partial file
                delete s;
            }
            req->_tempObject = nullptr;
        });
    }
    return *st;
}

// Immediate mid-body reply — used only by the small JSON buffer sink, where a
// body is at most a few KB and an early reply won't strand a long transfer.
void intakeFail(AsyncWebServerRequest* req, int code, const char* msg) {
    intakeFor(req).failed = true;
    sendError(req, code, msg);
}

// Deferred error for the stream uploads (A2): record the first {code,msg},
// mark failed so later chunks drain, but DON'T reply until completion.
void intakeDefer(BodyIntake& in, int code, const char* msg) {
    in.failed = true;
    if (in.errCode == 0) {
        in.errCode = code;
        in.errMsg = msg;
    }
}

// Completion handler shared by both stream uploads: send the deferred error if
// one was recorded, else the 201 (or the no-body 400). Every reply on the
// upload path leaves HERE — never mid-body — so clients get JSON, not an RST.
void uploadComplete(AsyncWebServerRequest* req) {
    BodyIntake* st = static_cast<BodyIntake*>(req->_tempObject);
    if (!st) {  // no body arrived at all
        sendError(req, 400, "empty upload");
        return;
    }
    if (st->errCode) {
        sendError(req, st->errCode, st->errMsg.c_str());
        return;
    }
    if (!st->sawBody) {
        sendError(req, 400, "empty upload");
        return;
    }
    JsonDocument doc;
    doc["name"] = st->name;
    std::string out;
    serializeJson(doc, out);
    sendJson(req, 201, out);
}

// Buffer sink: collect a small JSON body across chunks, then hand it to
// `handle`.
using JsonHandler =
    std::function<void(AsyncWebServerRequest*, JsonDocument&)>;

// `path` is an AsyncURIMatcher: a bare "/api/foo" string implicitly converts
// (backward-compatible prefix match, exactly as before), while a parent route
// that must NOT swallow its children passes AsyncURIMatcher::exact("/api/foo")
// (A1). The implicit ctor keeps every existing call-site compiling unchanged.
void onJsonBody(AsyncURIMatcher path, JsonHandler handle,
                size_t bodyCap = kJsonBodyCap) {
    gServer.on(
        std::move(path), HTTP_POST | HTTP_PUT,
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

// POST /api/record/arm has an OPTIONAL body, so it can't use onJsonBody (which
// 400s a bodyless request). This maps App's typed arm result to the contract.
void recordArmReply(App& app, AsyncWebServerRequest* req, bool countIn,
                    uint16_t bpm) {
    switch (app.recordArm(countIn, bpm)) {
        case App::RecordArm::Ok:
            sendJson(req, 200, app.statusJson());
            return;
        case App::RecordArm::AlreadyArmed:
            sendError(req, 409, "already armed");
            return;
        case App::RecordArm::Playing:
            sendError(req, 409, "playing");
            return;
        case App::RecordArm::LowSpace:
            sendError(req, 507, "low space");
            return;
        case App::RecordArm::LowMemory:
            sendError(req, 507, "low memory");
            return;
        case App::RecordArm::PendingUnsaved:
            // B5 ask 3: an earlier save failed and the take is still held —
            // retry-save or discard it before starting a new one.
            sendError(req, 409, "unsaved take pending");
            return;
    }
}

// Shared reply for /api/record/stop and /api/record/retry-save (B5 asks 1+3).
void recordStopReply(AsyncWebServerRequest* req, App::RecordStop r,
                     const std::string& name) {
    switch (r) {
        case App::RecordStop::Saved:
        case App::RecordStop::SavedTruncated:
        case App::RecordStop::Empty: {
            JsonDocument doc;
            doc["name"] = name;
            // B5 ask 1: the take saved but capture dropped events at a limit —
            // flag it so the UI can warn ("recording was truncated").
            if (r == App::RecordStop::SavedTruncated) doc["truncated"] = true;
            std::string out;
            serializeJson(doc, out);
            sendJson(req, 200, out);
            return;
        }
        case App::RecordStop::NotArmed:
            sendError(req, 409, "not armed");
            return;
        case App::RecordStop::SaveFailed:
            // B5 ask 3: the take is retained — POST /api/record/retry-save.
            sendError(req, 500, "write failed");
            return;
    }
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
    // T4: /api/songs stays a BARE ARRAY (coordinator ruling — a wrap would
    // break every existing consumer). The capacity primitive (fsFree/fsTotal)
    // lives in GET /api/status only; the library capacity bar reads it there
    // alongside this list. See docs/API.md + ASSUMPTIONS A6.
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
    // exact() (A1): a plain "/api/songs" would prefix-match — and so SWALLOW —
    // /api/songs/{name}/load, /unload, /rename (their POSTs became "empty
    // upload"). Exact matching is order-independent and lets those children
    // register whenever. Reply always comes from uploadComplete (A2).
    gServer.on(
        AsyncURIMatcher::exact("/api/songs"), HTTP_POST, uploadComplete, nullptr,
        [&app](AsyncWebServerRequest* req, uint8_t* data, size_t len,
               size_t index, size_t total) {
            BodyIntake& in = intakeFor(req);
            if (in.failed) return;  // deferred error already; drain silently
            ChunkPlan plan =
                planChunk(index, len, total, SongStore::kMaxSongBytes);
            if (plan.first) {  // name/size/prechecks: once, on the first chunk
                if (!req->hasParam("name")) {
                    intakeDefer(in, 400, "missing ?name=");
                    return;
                }
                in.name = req->getParam("name")->value().c_str();
                if (!SongStore::validName(in.name)) {
                    intakeDefer(in, 400, "bad name (want *.mid)");
                    return;
                }
                if (plan.tooLarge) {
                    intakeDefer(in, 413, "file too large (256KB max)");
                    return;
                }
                // Block-aware free-space precheck (A2): refuse BEFORE the first
                // byte lands, so a doomed upload never leaves a partial that
                // wedges the FS. 507 = the honest "out of space" answer.
                if (!uploadFits(total, app.store().freeBytes())) {
                    intakeDefer(in, 507, "insufficient storage");
                    return;
                }
                // 409 when the target is the currently-loaded song (§6-3):
                // overwriting it under a live session would desync playback.
                std::string loaded = app.loadedSongName();
                if (!loaded.empty() && loaded == in.name) {
                    intakeDefer(in, 409, "song is loaded");
                    return;
                }
                // Open the ONE handle for the whole transfer.
                if (!app.store().openUpload(in.name, in.file)) {
                    intakeDefer(in, 507, "cannot create file");
                    return;
                }
                // Discard the staged ".tmp" on disconnect (B4 atomic upload:
                // the existing same-name song is never touched until commit).
                std::string discard = in.name;
                in.cleanup = [&app, discard]() {
                    app.store().abortUpload(discard);
                };
            }
            if (!in.file) {  // guard: never write without an open handle
                intakeDefer(in, 500, "upload not open");
                return;
            }
            in.sawBody = true;
            if (in.file.write(data, len) != len) {
                in.file.close();
                app.store().abortUpload(in.name);  // discard the tmp
                in.cleanup = nullptr;
                intakeDefer(in, 500, "write failed");
                return;
            }
            if (plan.last) {
                in.file.close();  // complete the tmp, then rename it over target
                if (!app.store().commitUpload(in.name))
                    intakeDefer(in, 507, "cannot save upload");
                app.store().abortUpload(in.name);  // no-op after a good commit
                in.cleanup = nullptr;
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

    // Rename any song (REC4: rename a recorded take; general-purpose).
    onJsonBody("^\\/api\\/songs\\/([^\\/]+)\\/rename$",
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string from = req->pathArg(0).c_str();
                   std::string to = doc["name"] | "";
                   switch (app.store().rename(from, to)) {
                       case SongStore::RenameResult::Ok: {
                           JsonDocument out;
                           out["name"] = to;
                           std::string body;
                           serializeJson(out, body);
                           sendJson(req, 200, body);
                           return;
                       }
                       case SongStore::RenameResult::BadName:
                           sendError(req, 400, "bad name (want *.mid)");
                           return;
                       case SongStore::RenameResult::NotFound:
                           sendError(req, 404, "no such song");
                           return;
                       case SongStore::RenameResult::Exists:
                           sendError(req, 409, "exists");
                           return;
                   }
               });

    gServer.on("^\\/api\\/songs\\/([^\\/]+)$", HTTP_DELETE,
               [&app](AsyncWebServerRequest* req) {
                   std::string name = req->pathArg(0).c_str();
                   // D2 (what-if audit, 2026-07-14 fix wave): refuse deleting
                   // the currently-loaded song — otherwise status keeps
                   // reporting a "loaded" song the list no longer has (a ghost
                   // the player can't unload or re-select). Asks the
                   // App::loadedSongName() accessor (§6-3) instead of the old
                   // statusJson serialize-then-reparse hack.
                   std::string loaded = app.loadedSongName();
                   if (!loaded.empty() && loaded == name) {
                       sendError(req, 409, "song is loaded");
                       return;
                   }
                   if (!app.store().remove(name)) {
                       sendError(req, 404, "no such song");
                       return;
                   }
                   send204(req);
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
                   // B4: a failed persist is an honest 507, not a lying 200.
                   if (!app.applySettings(scalarsChanged)) {
                       sendError(req, 507, "insufficient storage");
                       return;
                   }
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
    // exact() (A1): a plain "/api/shows" swallowed POST /api/shows/stop and
    // /api/shows/{name}/play — presentation was dead from REST. Reply always
    // comes from uploadComplete (A2 deferred errors).
    gServer.on(
        AsyncURIMatcher::exact("/api/shows"), HTTP_POST, uploadComplete, nullptr,
        [&app](AsyncWebServerRequest* req, uint8_t* data, size_t len,
               size_t index, size_t total) {
            BodyIntake& in = intakeFor(req);
            if (in.failed) return;  // deferred error already; drain
            ChunkPlan plan =
                planChunk(index, len, total, SongStore::kMaxShowBytes);
            if (plan.first) {
                if (!req->hasParam("name")) {
                    intakeDefer(in, 400, "missing ?name=");
                    return;
                }
                in.name = req->getParam("name")->value().c_str();
                if (!SongStore::validShowName(in.name)) {
                    intakeDefer(in, 400, "bad name (want *.vls)");
                    return;
                }
                if (plan.tooLarge) {
                    intakeDefer(in, 413, "show too large");
                    return;
                }
                if (app.showBusy()) {  // never race a live render (OV1)
                    intakeDefer(in, 409, "busy");
                    return;
                }
                // Net-delta quota (A2): overwriting a same-name show frees its
                // old bytes, so an edit -> re-save no longer double-counts
                // itself and blocks the whole loop at the cap.
                size_t existing = app.store().showSize(in.name);
                bool nameExists = app.store().showExists(in.name);
                if (!showCountOk(app.store().listShows().size(), nameExists,
                                 SongStore::kMaxShowCount) ||
                    !showQuotaFits(app.store().showTotalBytes(), existing, total,
                                   SongStore::kMaxShowTotalBytes)) {
                    intakeDefer(in, 507, "show storage full");
                    return;
                }
                // Absolute FS free-space guard too — shows share the partition
                // with songs; refuse rather than risk the hard-full wedge.
                if (!uploadFits(total, app.store().freeBytes())) {
                    intakeDefer(in, 507, "insufficient storage");
                    return;
                }
                if (!app.store().openShowUpload(in.name, in.file)) {
                    intakeDefer(in, 507, "cannot create file");
                    return;
                }
                // Discard the staged ".tmp" on disconnect (B4 atomic upload).
                std::string discard = in.name;
                in.cleanup = [&app, discard]() {
                    app.store().abortShowUpload(discard);
                };
            }
            if (!in.file) {
                intakeDefer(in, 500, "upload not open");
                return;
            }
            in.sawBody = true;
            if (in.file.write(data, len) != len) {
                in.file.close();
                app.store().abortShowUpload(in.name);
                in.cleanup = nullptr;
                intakeDefer(in, 500, "write failed");
                return;
            }
            if (plan.last) {
                in.file.close();  // complete the tmp, then rename over target
                if (!app.store().commitShowUpload(in.name))
                    intakeDefer(in, 507, "cannot save upload");
                app.store().abortShowUpload(in.name);  // no-op after commit
                in.cleanup = nullptr;
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
                   send204(req);
               });

    // --- record (v3 REC4; docs/DESIGN-record.md) -------------------------
    // Arm: optional body {"countIn":bool,"bpm":N}. A bodyless POST arms with
    // defaults (count-in off) — so this can't use onJsonBody, which 400s an
    // empty body. Body chunks accumulate through the buffer sink; the request
    // handler covers the no-body case.
    gServer.on(
        "/api/record/arm", HTTP_POST,
        [&app](AsyncWebServerRequest* req) {
            if (req->_tempObject) return;  // body handler already replied
            recordArmReply(app, req, /*countIn=*/false, /*bpm=*/90);
        },
        nullptr,
        [&app](AsyncWebServerRequest* req, uint8_t* data, size_t len,
               size_t index, size_t total) {
            BodyIntake& in = intakeFor(req);
            if (in.failed) return;
            ChunkPlan plan = planChunk(index, len, total, kJsonBodyCap);
            if (plan.tooLarge) {
                intakeFail(req, 413, "body too large");
                return;
            }
            if (plan.first) in.buf.reserve(total);
            in.buf.concat(reinterpret_cast<const char*>(data), len);
            if (!plan.last) return;
            JsonDocument doc;
            if (deserializeJson(doc, in.buf.c_str()) !=
                DeserializationError::Ok) {
                sendError(req, 400, "bad json");
                return;
            }
            bool countIn = doc["countIn"] | false;
            uint16_t bpm = doc["bpm"] | 90;
            recordArmReply(app, req, countIn, bpm);
        });

    gServer.on("/api/record/stop", HTTP_POST,
               [&app](AsyncWebServerRequest* req) {
                   std::string name;
                   recordStopReply(req, app.recordStop(&name), name);
               });

    // B5 ask 3: retry the last failed save without re-recording. Same reply
    // shape as /stop; 409 "not armed" when nothing is pending.
    gServer.on("/api/record/retry-save", HTTP_POST,
               [&app](AsyncWebServerRequest* req) {
                   std::string name;
                   recordStopReply(req, app.retryRecordSave(&name), name);
               });

    gServer.on("/api/record/discard", HTTP_POST,
               [&app](AsyncWebServerRequest* req) {
                   if (!app.recordDiscard()) {
                       sendError(req, 409, "not armed");
                       return;
                   }
                   sendJson(req, 200, app.statusJson());
               });

    // --- AFK playlist (E3) ---------------------------------------------
    gServer.on("/api/afk", HTTP_GET, [&app](AsyncWebServerRequest* req) {
        sendJson(req, 200, app.afkJson());
    });

    // exact() (A1): a plain "/api/afk" swallowed POST /api/afk/control, which
    // then ran afkConfigFromJson on {"action":...} and WIPED the ambient config
    // to defaults. Exact keeps the config PUT from ever seeing a control body.
    onJsonBody(AsyncURIMatcher::exact("/api/afk"),
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string raw;
                   serializeJson(doc, raw);
                   std::string err;
                   bool saveFailed = false;
                   if (!app.applyAfk(raw.c_str(), &err, &saveFailed)) {
                       sendError(req, 400, err.c_str());
                       return;
                   }
                   if (saveFailed) {  // applied live but didn't persist (B4)
                       sendError(req, 507, "insufficient storage");
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
    // exact() (A1): a plain "/api/calibration" swallowed BOTH probe children —
    // GET returned the wrong document to the wizard and POST answered "bad
    // tier", so calibration Start did nothing. Exact frees the probe routes.
    gServer.on(AsyncURIMatcher::exact("/api/calibration"), HTTP_GET,
               [&app](AsyncWebServerRequest* req) {
                   sendJson(req, 200, app.calibrationJson());
               });

    onJsonBody(
        AsyncURIMatcher::exact("/api/calibration"),
        [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
            std::string raw;
            serializeJson(doc, raw);
            bool saveFailed = false;
            CalibResult r = app.applyCalibration(raw.c_str(), &saveFailed);
            if (!r.ok()) {
                sendError(req, 400, r.message());
                return;
            }
            if (saveFailed) {  // table applied live but didn't persist (B4)
                sendError(req, 507, "insufficient storage");
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
                   if (r == ModeDirector::ProbeArm::Recording) {
                       sendError(req, 409, "recording");
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

    // --- storage recovery (A3; ruling §6-2) ------------------------------
    // Guarded factory-wipe of LittleFS — the ONLY recovery from the hard-full
    // wedge without a USB reflash. Requires the confirm token {"confirm":
    // "ERASE"} (400 without it). Replies immediately, then App runs the
    // seconds-long LittleFS.format() on the loop task (never on async_tcp).
    onJsonBody(AsyncURIMatcher::exact("/api/storage/format"),
               [&app](AsyncWebServerRequest* req, JsonDocument& doc) {
                   std::string confirm = doc["confirm"] | "";
                   if (confirm != "ERASE") {
                       sendError(req, 400, "confirm token required");
                       return;
                   }
                   app.requestStorageFormat();
                   sendJson(req, 200, "{\"formatting\":true}");
               });

    gServer.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
        // B7 ask 2: reply immediately, then let main.cpp's loop() honor the
        // 200 ms grace and ESP.restart() on the loop task — never delay() on
        // async_tcp.
        req->send(200, "application/json", "{}");
        RebootRequest::pending.store(true);
        RebootRequest::requestedAtMs.store(millis());
    });

    gServer.onNotFound([](AsyncWebServerRequest* req) {
        // Cross-origin JSON requests preflight with OPTIONS — answer them
        // for every /api route (the P-POC editor path depends on it).
        if (req->method() == HTTP_OPTIONS) {
            AsyncWebServerResponse* res = req->beginResponse(204);
            addCors(res);
            req->send(res);
            return;
        }
        sendError(req, 404, "not found");
    });

    gServer.begin();
}

}  // namespace vialucis
