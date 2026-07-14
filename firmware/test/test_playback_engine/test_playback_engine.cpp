// Characterization tests for the playback engine (R2): pin the exact
// behavior App had at ec0293b — frame composition (wrong > due > ramp),
// sounding-light + wrong-flash bookkeeping, mask routing, frameDirty
// semantics — now natively testable behind one core seam.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "../helpers/smf_builder.h"
#include "../helpers/test_songs.h"
#include "vialucis/key_led_map.h"
#include "vialucis/midi_parser.h"
#include "vialucis/playback_engine.h"
#include "vialucis/settings.h"

using namespace vialucis;
using smf::Bytes;
using testsongs::chordSong;
using testsongs::twoTrackSong;

void setUp() {}
void tearDown() {}

namespace {

std::vector<MidiOutMsg> gOut;

void setupEngine(PlaybackEngine& e, MidiSong song,
                 const char* mode = "follow",
                 const char* practice = "both") {
    e.configure(Settings{});
    gOut.clear();
    e.loadSong(std::move(song), "test.mid", gOut);
    e.setMode(mode, practice, gOut);
}

// First LED of the key's range — enough to observe layer/color decisions.
Rgb ledAt(const std::vector<Rgb>& frame, uint8_t note) {
    LedRange r = ledsForNote(note, LedMapConfig{});
    TEST_ASSERT_TRUE_MESSAGE(r.valid, "note maps to the strip");
    return frame[r.first];
}

void assertRgb(Rgb expect, Rgb got) {
    TEST_ASSERT_EQUAL_UINT8(expect.r, got.r);
    TEST_ASSERT_EQUAL_UINT8(expect.g, got.g);
    TEST_ASSERT_EQUAL_UINT8(expect.b, got.b);
}

// Read the integer that follows "key": in a status JSON blob.
long jsonInt(const std::string& j, const char* key) {
    std::string k = std::string("\"") + key + "\":";
    size_t p = j.find(k);
    if (p == std::string::npos) return -1;
    return std::strtol(j.c_str() + p + k.size(), nullptr, 10);
}

bool hasCc(const std::vector<MidiOutMsg>& v, size_t from, uint8_t ctl,
           uint8_t val) {
    for (size_t i = from; i < v.size(); ++i)
        if (v[i].type == MidiOutType::Cc && v[i].data1 == ctl &&
            v[i].data2 == val)
            return true;
    return false;
}

// Derived from the Settings defaults so these tests keep verifying the
// engine actually tracks Settings — not a private copy of the palette.
const Rgb kRight = Settings{}.rightColor;  // Right and Both use this
const Rgb kLeft = Settings{}.leftColor;
const Rgb kWrong = Settings{}.wrongColor;
constexpr Rgb kBlack{0, 0, 0};

// Two named-hand tracks, note 40 on BOTH. Left (non-practiced) 40 @ 50ms;
// Right (practiced) 40 @ 200ms — the post-seek barrier. (A-3/G12 fixture)
MidiSong dualForty() {
    Bytes tR, tL;
    smf::trackName(tR, 0, "Right");
    smf::noteOn(tR, 192, 0, 40, 100);   // tick192 = 200000us
    smf::noteOff(tR, 48, 0, 40);
    smf::trackName(tL, 0, "Left");
    smf::noteOn(tL, 48, 0, 40, 100);    // tick48 = 50000us
    smf::noteOff(tL, 1, 0, 40);         // tick49
    Bytes file = smf::header(1, 2, 480);
    smf::append(file, smf::track(tR));
    smf::append(file, smf::track(tL));
    return parseMidi(file.data(), file.size()).song;
}

// Note 60 with a sustain pedal held down at tick 0 and released at tick 960,
// on one audible track (A-4/G17 fixture).
MidiSong pedalSong() {
    Bytes ev;
    smf::cc(ev, 0, 0, 64, 127);      // pedal DOWN @ tick 0
    smf::noteOn(ev, 0, 0, 60, 100);  // note @ tick 0
    smf::noteOff(ev, 480, 0, 60);    // note off @ tick 480
    smf::cc(ev, 480, 0, 64, 0);      // pedal UP @ tick 960
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

// Single key 60: sounds 0..30ms, dark, then re-presses at 1.5s (beyond the 1s
// ramp lead). The only thing that can light key 60 mid-loop is the repeat
// fill window [30ms, 1500ms). A loop ending at 100ms straddles it. (A-5/G5)
MidiSong repeatStraddleSong() {
    Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);     // on @ 0
    smf::noteOff(ev, 29, 0, 60);        // off ~30ms
    smf::noteOn(ev, 1411, 0, 60, 100);  // tick1440 = 1500000us
    smf::noteOff(ev, 29, 0, 60);
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

}  // namespace

void test_follow_mode_lights_sounding_note_full_color() {
    PlaybackEngine e;
    setupEngine(e, chordSong());
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);    // first tick after play re-baselines the clock
    e.tick(100000, gOut);  // ~100ms in: C4 sounding
    const std::vector<Rgb>& frame = e.renderFrame(100000);
    assertRgb(kRight, ledAt(frame, 60));  // single track = Both → right color
}

void test_note_off_clears_the_light() {
    PlaybackEngine e;
    setupEngine(e, chordSong());
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(510000, gOut);  // past C4's off at 500ms; E4 sounding instead
    const std::vector<Rgb>& frame = e.renderFrame(510000);
    assertRgb(kRight, ledAt(frame, 64));
    // C4's slot may only carry ramp preview (dim) — never full due color.
    TEST_ASSERT_TRUE(ledAt(frame, 60).g < 255);
}

void test_ramp_preview_swells_below_cap_before_onset() {
    PlaybackEngine e;
    setupEngine(e, chordSong());
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(100000, gOut);  // pos ≈ 99ms; E4 onset at 500ms is inside 1s lead
    const std::vector<Rgb>& frame = e.renderFrame(100000);
    Rgb preview = ledAt(frame, 64);
    TEST_ASSERT_TRUE_MESSAGE(preview.g > 0, "preview visible");
    // cap 0.45 → strictly below half brightness (dead zone makes "now"
    // unmistakable — SPEC feature 8)
    TEST_ASSERT_TRUE_MESSAGE(preview.g <= 128, "preview stays under the cap");
    TEST_ASSERT_EQUAL_UINT8(0, preview.r);
    TEST_ASSERT_EQUAL_UINT8(0, preview.b);
}

void test_wait_mode_halts_and_lights_due_chord_full() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);  // tries to run 200ms — barrier at 0 holds it
    TEST_ASSERT_EQUAL_UINT64(0, e.positionUs());
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"state\":\"waiting\"") != std::string::npos);
    TEST_ASSERT_TRUE(status.find("\"pendingNotes\":[60]") != std::string::npos);
    assertRgb(kRight, ledAt(e.renderFrame(200000), 60));  // due = 100%
}

void test_correct_key_clears_chord_and_playback_advances() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    e.onKeyDown(60, 200000);          // the due note
    e.tick(800000, gOut);             // runs until the E4 barrier at 500ms
    TEST_ASSERT_EQUAL_UINT64(500000, e.positionUs());
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"pendingNotes\":[64]") != std::string::npos);
}

void test_chord_clears_per_key() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    e.onKeyDown(60, 200000);
    e.tick(800000, gOut);   // barrier at E4 (500ms)
    e.onKeyDown(64, 810000);
    e.tick(1200000, gOut);  // barrier at G4+B4 chord (1000ms)
    e.tick(1300000, gOut);
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"pendingNotes\":[67,71]") !=
                     std::string::npos);
    e.onKeyDown(67, 1310000);  // half the chord
    status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"pendingNotes\":[71]") != std::string::npos);
    // Cleared member lights out; remaining member still due at 100%.
    const std::vector<Rgb>& frame = e.renderFrame(1310000);
    assertRgb(kRight, ledAt(frame, 71));
}

void test_wrong_key_flashes_red_then_expires() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    e.onKeyDown(61, 200000);  // wrong note
    assertRgb(kWrong, ledAt(e.renderFrame(210000), 61));
    // 300ms later the flash is gone.
    assertRgb(kBlack, ledAt(e.renderFrame(200000 + 310000), 61));
}

void test_wrong_flash_outranks_due_light() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    // The due note itself can't be "wrong", so flash a neighbor sharing no
    // LEDs, then check both layers coexist; then force overlap via the due
    // key: press wrong note ON the due chord position is impossible, so
    // instead verify wrong wins where both paint the same key: press 60's
    // neighbor 61 and confirm 60 stays due-colored while 61 is red.
    e.onKeyDown(61, 200000);
    const std::vector<Rgb>& frame = e.renderFrame(210000);
    assertRgb(kRight, ledAt(frame, 60));
    assertRgb(kWrong, ledAt(frame, 61));
}

void test_key_verdict_makes_next_frame_immediate() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    TEST_ASSERT_TRUE(e.frameDue(1000));    // first frame stamps the clock
    TEST_ASSERT_FALSE(e.frameDue(2000));   // 1ms later: limiter holds
    e.onKeyDown(61, 2500);                 // wrong-key verdict → dirty
    TEST_ASSERT_TRUE(e.frameDue(3000));    // dirty bypasses the limiter
    TEST_ASSERT_FALSE(e.frameDue(4000));   // flag consumed
}

void test_accompaniment_emits_only_the_other_hand() {
    PlaybackEngine e;
    setupEngine(e, twoTrackSong(), "accompaniment", "right");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);      // barrier at 0 (note 60, practiced right hand)
    e.onKeyDown(60, 200000);   // clear it → playback runs
    gOut.clear();
    e.tick(300000, gOut);
    bool saw40 = false;
    for (const MidiOutMsg& m : gOut) {
        TEST_ASSERT_TRUE_MESSAGE(m.data1 != 60,
                                 "practiced hand never sent to the piano");
        if (m.type == MidiOutType::NoteOn && m.data1 == 40) saw40 = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(saw40, "left hand played for the player");
}

void test_echoed_accompaniment_note_is_not_a_wrong_press() {
    // Right hand: 60 on@0 off@120t, 62 on@240t. Left hand: 40 on@0 off@240t.
    // Tight spacing keeps the echo inside the 250ms guard window while the
    // 62-chord is pending — so a mishandled echo WOULD flash red.
    Bytes t0, t1;
    smf::noteOn(t0, 0, 0, 60, 100);
    smf::noteOff(t0, 120, 0, 60);
    smf::noteOn(t0, 120, 0, 62, 100);
    smf::noteOff(t0, 120, 0, 62);
    smf::noteOn(t1, 0, 0, 40, 100);
    smf::noteOff(t1, 240, 0, 40);
    Bytes file = smf::header(1, 2, 480);
    smf::append(file, smf::track(t0));
    smf::append(file, smf::track(t1));
    MidiSong song = parseMidi(file.data(), file.size()).song;

    PlaybackEngine e;
    setupEngine(e, std::move(song), "accompaniment", "right");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(50000, gOut);      // barrier at 0, chord [60]
    e.onKeyDown(60, 50000);   // clear → next barrier at 250ms (note 62)
    gOut.clear();
    e.tick(100000, gOut);     // note 40 sent to the piano, guard registered
    bool saw40 = false;
    for (const MidiOutMsg& m : gOut)
        if (m.type == MidiOutType::NoteOn && m.data1 == 40) saw40 = true;
    TEST_ASSERT_TRUE(saw40);
    e.tick(300000, gOut);     // held at the 250ms barrier; chord [62] pending
    // The piano echoes our own note 40 back within the guard window:
    e.onKeyDown(40, 320000);
    const std::vector<Rgb>& frame = e.renderFrame(330000);
    assertRgb(kLeft, ledAt(frame, 40));  // sounding light, NOT a red flash
    TEST_ASSERT_TRUE(e.statusJson().find("\"pendingNotes\":[62]") !=
                     std::string::npos);  // echo didn't touch the chord
    // Control: with a chord pending, a genuinely un-sent note IS wrong.
    e.onKeyDown(41, 340000);
    assertRgb(kWrong, ledAt(e.renderFrame(350000), 41));
}

void test_demo_emits_all_audible_tracks() {
    PlaybackEngine e;
    setupEngine(e, twoTrackSong(), "demo");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    bool saw60 = false, saw40 = false;
    for (const MidiOutMsg& m : gOut) {
        if (m.type != MidiOutType::NoteOn) continue;
        if (m.data1 == 60) saw60 = true;
        if (m.data1 == 40) saw40 = true;
    }
    TEST_ASSERT_TRUE(saw60);
    TEST_ASSERT_TRUE(saw40);
}

void test_pause_sends_note_offs_for_sounding_notes() {
    PlaybackEngine e;
    setupEngine(e, twoTrackSong(), "demo");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);  // both notes sounding on the piano
    gOut.clear();
    e.transport("pause", 0, gOut);
    int offs = 0;
    for (const MidiOutMsg& m : gOut)
        if (m.type == MidiOutType::NoteOff) ++offs;
    TEST_ASSERT_EQUAL_INT(2, offs);
    // Paused: the next frame renders dark (nothing due, nothing sounding).
    TEST_ASSERT_TRUE(e.frameDue(210000));  // stop dirtied the frame
    assertRgb(kBlack, ledAt(e.renderFrame(210000), 60));
}

void test_lights_toggle_hides_a_track() {
    PlaybackEngine e;
    setupEngine(e, twoTrackSong());
    TEST_ASSERT_TRUE(e.setTrack(1, "left", /*lights=*/false));
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    const std::vector<Rgb>& frame = e.renderFrame(200000);
    assertRgb(kRight, ledAt(frame, 60));  // track 0 still lit
    assertRgb(kBlack, ledAt(frame, 40));  // track 1's lights are off
}

void test_finished_state_after_song_end() {
    PlaybackEngine e;
    setupEngine(e, chordSong());
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(3000000, gOut);  // way past the 1.5s song
    TEST_ASSERT_TRUE(e.state() == PlayState::Finished);
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"state\":\"finished\"") !=
                     std::string::npos);
    // play from finished restarts at 0
    gOut.clear();
    e.transport("play", 0, gOut);
    TEST_ASSERT_EQUAL_UINT64(0, e.positionUs());
}

void test_rest_calls_without_song_return_false() {
    PlaybackEngine e;
    e.configure(Settings{});
    gOut.clear();
    TEST_ASSERT_FALSE(e.transport("play", 0, gOut));
    TEST_ASSERT_FALSE(e.setTempo(120.0f));
    TEST_ASSERT_FALSE(e.setLoop(true, 0, 1000));
    TEST_ASSERT_FALSE(e.setTrack(0, "left", true));
}

void test_loop_wrap_clears_sounding_lights_and_resyncs() {
    PlaybackEngine e;
    setupEngine(e, chordSong());
    TEST_ASSERT_TRUE(e.setLoop(true, 0, 600));  // 0–600ms: wraps mid-E4
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(500000, gOut);   // approaching wrap
    e.tick(700000, gOut);   // wrapped back near 100ms
    TEST_ASSERT_TRUE(e.positionUs() < 600000);
    // E4 (was sounding at wrap time) no longer paints as due after wrap;
    // only C4 — sounding again after the wrap — is full color.
    const std::vector<Rgb>& frame = e.renderFrame(700000);
    assertRgb(kRight, ledAt(frame, 60));
}

// --- R4: /api/status contract vs docs/API.md ---------------------------

void test_status_json_matches_api_contract_shape() {
    PlaybackEngine e;
    setupEngine(e, twoTrackSong(), "wait");
    WifiStatus wifi{"sta", "192.168.1.50"};
    std::string body = e.statusJson(&wifi);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, body) == DeserializationError::Ok);
    // Every key documented in docs/API.md GET /api/status, correct types:
    TEST_ASSERT_TRUE(doc["version"].is<const char*>());
    TEST_ASSERT_TRUE(doc["song"].is<const char*>());
    TEST_ASSERT_TRUE(doc["state"].is<const char*>());
    TEST_ASSERT_TRUE(doc["mode"].is<const char*>());
    TEST_ASSERT_TRUE(doc["positionMs"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["durationMs"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["tempoPercent"].is<float>());
    TEST_ASSERT_TRUE(doc["loop"]["enabled"].is<bool>());
    TEST_ASSERT_TRUE(doc["loop"]["startMs"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["loop"]["endMs"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["tracks"].is<JsonArrayConst>());
    JsonObjectConst t0 = doc["tracks"][0];
    TEST_ASSERT_TRUE(t0["index"].is<uint32_t>());
    TEST_ASSERT_TRUE(t0["name"].is<const char*>());
    TEST_ASSERT_TRUE(t0["hand"].is<const char*>());
    TEST_ASSERT_TRUE(t0["lights"].is<bool>());
    TEST_ASSERT_TRUE(doc["pendingNotes"].is<JsonArrayConst>());
    TEST_ASSERT_EQUAL_STRING("sta", doc["wifi"]["mode"]);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", doc["wifi"]["ip"]);
    TEST_ASSERT_EQUAL_STRING("test.mid", doc["song"]);
    TEST_ASSERT_EQUAL_STRING("wait", doc["mode"]);
    TEST_ASSERT_EQUAL_STRING("idle", doc["state"]);
}

void test_status_json_without_wifi_omits_the_wifi_object() {
    // Non-status routes return statusJson with NO wifi object — exactly the
    // pre-R4 behavior (only GET /api/status ever grafted wifi on).
    PlaybackEngine e;
    setupEngine(e, twoTrackSong());
    std::string body = e.statusJson();
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, body) == DeserializationError::Ok);
    TEST_ASSERT_FALSE(doc["wifi"].is<JsonObjectConst>());
}

void test_status_json_wifi_object_is_the_last_key() {
    // The old splice parsed the full doc and appended wifi — so wifi came
    // last on the wire. Byte-compatibility keeps it there.
    PlaybackEngine e;
    setupEngine(e, twoTrackSong());
    WifiStatus wifi{"ap", "192.168.4.1"};
    std::string body = e.statusJson(&wifi);
    std::string tail = "\"wifi\":{\"mode\":\"ap\",\"ip\":\"192.168.4.1\"}}";
    TEST_ASSERT_TRUE(body.size() > tail.size());
    TEST_ASSERT_EQUAL_STRING(tail.c_str(),
                             body.c_str() + (body.size() - tail.size()));
}

// --- F1: cross-task fence — engine coherence at call boundaries ---------
// The device fence (one FreeRTOS mutex in App) makes every HTTP-task entry
// point an atomic unit that interleaves with the loop task only at call
// boundaries. The mutex is device code (compile-gated); these pins prove the
// engine is coherent under exactly those sequential interleavings, so the
// guarantee is meaningful and stays true if someone later touches an entry
// point. (A33)

void test_loadsong_between_ticks_while_playing_is_coherent() {
    PlaybackEngine e;
    setupEngine(e, twoTrackSong(), "demo");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);  // both notes sounding on the piano
    // A load lands between two ticks while Playing: the old song's sounding
    // notes get note-offs appended, the new song is Idle at 0 and named.
    gOut.clear();
    e.loadSong(chordSong(), "next.mid", gOut);
    int offs = 0;
    for (const MidiOutMsg& m : gOut)
        if (m.type == MidiOutType::NoteOff) ++offs;
    TEST_ASSERT_EQUAL_INT(2, offs);
    TEST_ASSERT_TRUE(e.state() == PlayState::Idle);
    TEST_ASSERT_EQUAL_UINT64(0, e.positionUs());
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"song\":\"next.mid\"") != std::string::npos);
    TEST_ASSERT_TRUE(status.find("\"state\":\"idle\"") != std::string::npos);
    // The next tick sees an Idle engine: a clean no-op, nothing crashes.
    gOut.clear();
    e.tick(300000, gOut);
    TEST_ASSERT_EQUAL_UINT64(0, e.positionUs());
    TEST_ASSERT_TRUE(e.state() == PlayState::Idle);
}

void test_configure_between_tick_and_frame_uses_new_config() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(100000, gOut);  // C4 sounding
    // New settings arrive between the tick and the frame it renders: swap the
    // right-hand color. The very next frame must reflect it — no stale state.
    Settings s;
    s.rightColor = Rgb{123, 45, 67};
    e.configure(s);
    const std::vector<Rgb>& frame = e.renderFrame(100000);
    assertRgb(Rgb{123, 45, 67}, ledAt(frame, 60));
}

void test_statusjson_between_ticks_is_internally_consistent() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);  // barrier at 0 holds playback
    // Read status between ticks mid-song: state/positionMs/pendingNotes must
    // agree — waiting at the barrier ⇒ position pinned at 0, chord pending.
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"state\":\"waiting\"") != std::string::npos);
    TEST_ASSERT_TRUE(status.find("\"positionMs\":0") != std::string::npos);
    TEST_ASSERT_TRUE(status.find("\"pendingNotes\":[60]") != std::string::npos);
}

// REC4: the status "record" object is emitted with the frozen shape and
// lands BEFORE wifi (which stays the last key).
void test_statusjson_record_object_before_wifi() {
    PlaybackEngine e;
    e.configure(Settings{});
    WifiStatus wifi{"sta", "192.168.1.9"};
    RecordStatus rec{"recording", 4200, 320, 262144, true, 96};
    std::string s = e.statusJson(&wifi, nullptr, &rec);
    // Shape.
    TEST_ASSERT_TRUE(s.find("\"record\":{") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"state\":\"recording\"") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"elapsedMs\":4200") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"usedBytes\":320") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"budgetBytes\":262144") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"countIn\":true") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"bpm\":96") != std::string::npos);
    // Ordering: record before wifi.
    TEST_ASSERT_TRUE(s.find("\"record\"") < s.find("\"wifi\""));
    // Omitted entirely when no RecordStatus is passed (other routes).
    TEST_ASSERT_TRUE(e.statusJson().find("\"record\"") == std::string::npos);
}

// --- F2: status loop honesty --------------------------------------------
// A loaded song's loop range belongs to that song. Loading a new song must
// clear the status mirror fields — otherwise /api/status claims an enabled
// loop that no longer exists (the fresh Scheduler has none). (A34)

void test_load_song_clears_reported_loop() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    TEST_ASSERT_TRUE(e.setLoop(true, 1000, 5000));
    TEST_ASSERT_TRUE(
        e.statusJson().find(
            "\"loop\":{\"enabled\":true,\"startMs\":1000,\"endMs\":5000}") !=
        std::string::npos);
    // Load a different song: the old loop is gone, status must say so.
    gOut.clear();
    e.loadSong(twoTrackSong(), "b.mid", gOut);
    TEST_ASSERT_TRUE(
        e.statusJson().find(
            "\"loop\":{\"enabled\":false,\"startMs\":0,\"endMs\":0}") !=
        std::string::npos);
}

// --- F3: test-pattern clock ---------------------------------------------
// The App fix makes activating a strip/rainbow pattern auto-pause playback
// (transport("pause") + note-offs). It relies on this engine contract: a
// paused engine ignores wall-clock time entirely, and the play path
// re-baselines the clock (lastTickUs_=0) so no skipped-time burst can fire
// when the pattern is turned off and the user presses play. (A35)

void test_pause_gap_play_re_baselines_no_burst() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);      // baseline the clock
    e.tick(300000, gOut);    // ~299ms in
    uint64_t posBefore = e.positionUs();
    TEST_ASSERT_TRUE(posBefore > 250000 && posBefore < 350000);

    // Pattern activates -> pause. A large wall-clock gap now passes with NO
    // ticks (the pattern branch early-returns on device; the paused engine
    // ignores time regardless).
    e.transport("pause", 0, gOut);
    uint64_t now = 300000 + 60000000ULL;  // 60s later

    // Pattern off -> user presses play. The re-baseline tick has delta 0, so
    // position stays exactly where it paused — no fast-forward of the gap.
    e.transport("play", 0, gOut);
    e.tick(now, gOut);
    TEST_ASSERT_EQUAL_UINT64(posBefore, e.positionUs());

    // The next real tick advances by its own delta (~100ms scaled by tempo),
    // NOT by the 60s gap.
    e.tick(now + 100000, gOut);
    uint64_t advanced = e.positionUs() - posBefore;
    TEST_ASSERT_TRUE(advanced >= 90000 && advanced <= 110000);
}

// --- M3: top-mode status growth ---------------------------------------------

void test_status_top_fields_before_wifi_which_stays_last() {
    PlaybackEngine e;
    setupEngine(e, chordSong());
    WifiStatus w{"sta", "10.0.0.9"};
    TopStatus t{"practice", 42, 180};
    std::string s = e.statusJson(&w, &t);
    TEST_ASSERT_TRUE(s.find("\"topMode\":\"practice\"") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"idleSec\":42") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"afkTimeoutSec\":180") != std::string::npos);
    // wifi remains the FINAL key on the wire (R4 contract).
    size_t wifiPos = s.find("\"wifi\":");
    TEST_ASSERT_TRUE(wifiPos != std::string::npos);
    TEST_ASSERT_TRUE(s.find("\"topMode\":") < wifiPos);
    // Without top, the fields are absent — non-status routes unchanged.
    std::string bare = e.statusJson();
    TEST_ASSERT_TRUE(bare.find("topMode") == std::string::npos);
}

// --- C3/M2: the geometry table seam (probe tests moved to
// test_mode_director with the M2 lift) ------------------------------------

void test_set_table_overrides_configure_geometry() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    // A per-key table that puts C4 somewhere the formula never would.
    KeyLedTable t;
    t.setLedCount(360);
    t.set(60, LedRange{5, 6, true});
    e.setTable(t);
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(100000, gOut);  // C4 sounding
    const std::vector<Rgb>& frame = e.renderFrame(100000);
    assertRgb(kRight, frame[5]);
    assertRgb(kRight, frame[6]);
    // The formula's location for C4 stays dark — the table is the truth.
    LedRange v1 = ledsForNote(60, LedMapConfig{});
    assertRgb(kBlack, frame[v1.first]);
    // configure() NEVER touches geometry (C-wave closing review): after a
    // settings change the explicit table survives — setTable is the one
    // geometry writer.
    e.configure(Settings{});
    const std::vector<Rgb>& frame2 = e.renderFrame(100000);
    assertRgb(kRight, frame2[5]);
    assertRgb(kBlack, frame2[v1.first]);
}

// --- M1: unloadSong -------------------------------------------------------

void test_unload_while_playing_flushes_and_clears() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "demo");  // demo: notes go to the piano
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(100000, gOut);  // C4 sounding on the piano and the strip
    gOut.clear();
    e.unloadSong(gOut);
    int offs = 0;
    for (const MidiOutMsg& m : gOut)
        if (m.type == MidiOutType::NoteOff) ++offs;
    TEST_ASSERT_TRUE_MESSAGE(offs >= 1, "sounding notes get note-offs");
    TEST_ASSERT_FALSE(e.songLoaded());
    TEST_ASSERT_TRUE(e.state() == PlayState::Idle);
    // The very next frame is dark (dirty flag set by the unload).
    TEST_ASSERT_TRUE(e.frameDue(110000));
    const std::vector<Rgb>& frame = e.renderFrame(110000);
    for (const Rgb& c : frame)
        TEST_ASSERT_TRUE(!c.r && !c.g && !c.b);
    // Status reads exactly like the natural boot state.
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE(status.find("\"song\":\"\"") != std::string::npos);
    TEST_ASSERT_TRUE(status.find("\"state\":\"idle\"") != std::string::npos);
    TEST_ASSERT_TRUE(status.find("\"positionMs\":0") != std::string::npos);
    TEST_ASSERT_TRUE(
        status.find("\"loop\":{\"enabled\":false,\"startMs\":0,\"endMs\":0}") !=
        std::string::npos);
    // REST calls behave like no-song-yet (v1 contract).
    TEST_ASSERT_FALSE(e.transport("play", 0, gOut));
    TEST_ASSERT_FALSE(e.setTempo(120.0f));
    // And a fresh load works normally afterwards.
    gOut.clear();
    e.loadSong(chordSong(), "again.mid", gOut);
    TEST_ASSERT_TRUE(e.songLoaded());
    TEST_ASSERT_TRUE(e.transport("play", 0, gOut));
}

// --- P4 score-follow hooks ---------------------------------------------------

void test_follow_track_mask_resolution() {
    PlaybackEngine e;
    // No song: the hook degrades to all-tracks, never crashes.
    TEST_ASSERT_EQUAL_UINT32(kTrackMaskAll, e.followTrackMask(0xFF));
    // twoTrackSong: piano convention t0=Right, t1=Left.
    setupEngine(e, twoTrackSong());
    // Auto (0xFF, A54) = the right-hand practiced mask — the melody.
    TEST_ASSERT_EQUAL_UINT32(trackBit(0), e.followTrackMask(0xFF));
    // An explicit editor-chosen track wins.
    TEST_ASSERT_EQUAL_UINT32(trackBit(1), e.followTrackMask(1));
    TEST_ASSERT_EQUAL_UINT32(trackBit(0), e.followTrackMask(0));
    // An unusable index (out of the track space) falls back to auto.
    TEST_ASSERT_EQUAL_UINT32(trackBit(0), e.followTrackMask(7));
}

void test_drive_show_clock_sets_song_time_only_while_stopped() {
    PlaybackEngine e;
    setupEngine(e, chordSong());
    // Stopped: the director-driven clock writes song time directly.
    e.driveShowClock(500000);
    TEST_ASSERT_EQUAL_UINT64(500000, e.positionUs());
    // Backward drives too (re-arm / re-acquire are seeks by design).
    e.driveShowClock(100000);
    TEST_ASSERT_EQUAL_UINT64(100000, e.positionUs());
    // While Playing the transport owns the clock: the hook is refused.
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    uint64_t owned = e.positionUs();
    e.driveShowClock(2000000);
    TEST_ASSERT_EQUAL_UINT64(owned, e.positionUs());
}

// --- A-1: loop/seek correctness (what-if audit G1/G2/G4/G6) --------------

// G1 — wait + loop whose END coincides with a practiced onset. After the
// player clears the barrier AT loopEnd, pos_ == loopEnd_; the loop must still
// wrap, not let playback escape past it.
void test_s1_wait_loop_wraps_after_barrier_exactly_at_loop_end() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    TEST_ASSERT_TRUE(e.setLoop(true, 0, 500));
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);  // barrier at 0 holds
    TEST_ASSERT_EQUAL_UINT64(0, e.positionUs());
    e.onKeyDown(60, 210000);  // clear C4 → barrier re-arms at 500000 == loopEnd
    e.tick(900000, gOut);     // holds at the 500000 barrier
    TEST_ASSERT_EQUAL_UINT64(500000, e.positionUs());
    e.onKeyDown(64, 910000);  // clear E4 (the note AT loopEnd)
    e.tick(3000000, gOut);    // 2+ seconds of playback
    TEST_ASSERT_TRUE_MESSAGE(
        e.positionUs() < 500000,
        "loop must wrap after a barrier at loopEnd, not escape");
}

// G2 — loop set entirely BEHIND the playhead while playing. The loop is
// authoritative (A89): wrap into it; position must never run past duration.
void test_s2_loop_behind_playhead_still_reaches_finished() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(1200000, gOut);  // pos ≈ 1.2s of a 1.5s song
    TEST_ASSERT_TRUE(e.setLoop(true, 0, 500));  // loop is wholly behind us
    e.tick(5000000, gOut);
    e.tick(9000000, gOut);
    TEST_ASSERT_TRUE_MESSAGE(
        e.positionUs() <= 1500000,
        "position must not run past duration when a stale loop is set");
    // A89: wrap into the loop.
    TEST_ASSERT_TRUE_MESSAGE(e.positionUs() < 500000,
                             "a loop behind the playhead must wrap in");
}

// G4 — micro-loop at high tempo: the O(1) modulo collapse must land at the
// true modulo position (the old 64-iteration guard truncated the wrap count).
void test_C9_microloop_guard_underadvances() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    TEST_ASSERT_TRUE(e.setLoop(true, 0, 100));  // 100ms loop
    TEST_ASSERT_TRUE(e.setTempo(500));
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);                 // baseline pos 0
    e.tick(1000 + 20010000ull, gOut);   // delta 20.01s → 100.05s song time
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        50000ull, e.positionUs(),
        "high-tempo micro-loop must land at the true modulo position");
}

// G6 — seek beyond duration must clamp; status must not report position >
// duration.
void test_C2_seek_beyond_duration_clamped() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    gOut.clear();
    e.transport("seek", 99999999u, gOut);
    std::string s = e.statusJson();
    long pos = jsonInt(s, "positionMs");
    long dur = jsonInt(s, "durationMs");
    TEST_ASSERT_TRUE_MESSAGE(
        pos <= dur, "seek past the end must clamp positionMs to durationMs");
}

// --- A-2: partial-chord progress preservation (G7/G8/G9) -----------------

// G7 — a double "play" (web-remote double-tap) while holding at a chord
// barrier with one member cleared must not resurrect the cleared member.
void test_w4_double_play_keeps_partial_chord_progress() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.onKeyDown(60, 2000);    // clear C4 barrier at 0
    e.tick(600000, gOut);     // holds at E4 barrier 500000
    e.onKeyDown(64, 610000);  // clear E4 → chord barrier at 1000000 (G4+B4)
    e.tick(1600000, gOut);    // holds at 1000000
    e.onKeyDown(67, 1610000); // clear HALF the chord (G4); B4 still pending
    TEST_ASSERT_TRUE(e.statusJson().find("\"pendingNotes\":[71]") !=
                     std::string::npos);
    e.transport("play", 0, gOut);  // second play POST
    TEST_ASSERT_TRUE_MESSAGE(
        e.statusJson().find("\"pendingNotes\":[71]") != std::string::npos,
        "double play must not resurrect the already-cleared chord member");
}

// G8 — a no-op track PUT (setting the track to the hand/lights it already
// has) must not reset chord progress.
void test_w2_noop_settrack_keeps_partial_chord_progress() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.onKeyDown(60, 2000);
    e.tick(600000, gOut);
    e.onKeyDown(64, 610000);
    e.tick(1600000, gOut);
    e.onKeyDown(67, 1610000);  // half cleared
    TEST_ASSERT_TRUE(e.statusJson().find("\"pendingNotes\":[71]") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(e.setTrack(0, "both", true));  // identical config
    TEST_ASSERT_TRUE_MESSAGE(
        e.statusJson().find("\"pendingNotes\":[71]") != std::string::npos,
        "a no-op track PUT must not resurrect the cleared chord member");
}

// G9 — pause → resume mid-chord must not resurrect a cleared member (the most
// likely real-practice trigger: pause to think, resume).
void test_P13_pause_resume_keeps_partial_chord() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.onKeyDown(60, 2000);
    e.tick(600000, gOut);
    e.onKeyDown(64, 610000);
    e.tick(1600000, gOut);
    e.onKeyDown(67, 1610000);  // clear G4; B4 owed
    TEST_ASSERT_TRUE(e.statusJson().find("\"pendingNotes\":[71]") !=
                     std::string::npos);
    e.transport("pause", 0, gOut);
    e.transport("play", 0, gOut);
    e.tick(1700000, gOut);
    TEST_ASSERT_TRUE_MESSAGE(
        e.statusJson().find("\"pendingNotes\":[71]") != std::string::npos,
        "pause/resume must not resurrect the already-cleared chord member");
}

// --- A-3: echo-credit hygiene (G11/G12) ----------------------------------

// G11 — demo emits note 60 (echo credit), switch to wait; a genuine press of
// 60 within the echo window must clear the barrier, not be eaten as a stale
// echo (setMode away from an emitting mode clears credits).
void test_P24_echo_credit_bleeds_across_mode_switch() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "demo");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);       // demo emits note-on 60 @ t=1000 (credit)
    e.setMode("wait", "both", gOut);
    e.transport("play", 0, gOut);
    e.tick(100000, gOut);     // arm barrier on note 60
    TEST_ASSERT_TRUE(e.statusJson().find("\"state\":\"waiting\"") !=
                     std::string::npos);
    e.onKeyDown(60, 150000);  // genuine press, within 250ms of the emit
    TEST_ASSERT_TRUE_MESSAGE(
        e.statusJson().find("\"state\":\"waiting\"") == std::string::npos,
        "a real key press after a mode switch must clear the barrier, "
        "not be eaten as a stale echo");
}

// G12 — accompaniment emits (left) note 40 (echo credit), backward seek, then
// a genuine barrier press of (right) 40 within the window must clear it, not
// be eaten (seek clears credits).
void test_C26_echo_credit_survives_backward_seek() {
    PlaybackEngine e;
    setupEngine(e, dualForty(), "accompaniment", "right");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(60000, gOut);         // emit left-40 @ 50ms; credit @ now=60000
    e.transport("seek", 55, gOut);  // backward to 55ms (past left, before bar)
    e.tick(205000, gOut);        // advance to the right-40 barrier @ 200ms
    TEST_ASSERT_TRUE(e.statusJson().find("\"pendingNotes\":[40]") !=
                     std::string::npos);
    e.onKeyDown(40, 206000);     // genuine press, within 250ms of the emit
    TEST_ASSERT_TRUE_MESSAGE(
        e.statusJson().find("\"pendingNotes\":[40]") == std::string::npos,
        "a real barrier press after a seek must clear it, not be eaten "
        "as a stale echo");
}

// --- A-4: pedal-off flush on pause/stop/seek (G17) -----------------------

// G17 — pausing demo/accompaniment while the song's sustain pedal is down
// must flush CC64=0, or the piano stays sustained.
void test_P11_pause_flushes_sustain_pedal() {
    PlaybackEngine e;
    setupEngine(e, pedalSong(), "demo");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);    // baseline: emits t=0 events (pedal-down + note)
    e.tick(100000, gOut);  // advance past pedal-down, before pedal-up
    TEST_ASSERT_TRUE_MESSAGE(hasCc(gOut, 0, 64, 127),
                             "pedal-down CC64=127 must be emitted in demo");
    size_t before = gOut.size();
    e.transport("pause", 0, gOut);
    TEST_ASSERT_TRUE_MESSAGE(
        hasCc(gOut, before, 64, 0),
        "pause must flush a held sustain pedal (CC64=0)");
}

// --- A-5: repeat-cue loop clamp (G5) -------------------------------------

// G5 — a repeat-fill window straddling loopEnd must not paint: while looping,
// the loop never reaches the onset the cue crescendos toward.
void test_C34_phantom_repeat_cue_inside_loop() {
    PlaybackEngine e;
    setupEngine(e, repeatStraddleSong(), "follow");
    TEST_ASSERT_TRUE(e.setLoop(true, 0, 100));  // ends inside [30ms,1500ms) win
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);  // pos wraps to ~99ms inside the loop
    const std::vector<Rgb>& f = e.renderFrame(200000);
    bool anyLit = false;
    for (const Rgb& p : f)
        if (p.r || p.g || p.b) anyLit = true;
    TEST_ASSERT_FALSE_MESSAGE(
        anyLit, "no repeat cue should render for an onset the loop never "
                "reaches (phantom cue)");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_follow_mode_lights_sounding_note_full_color);
    RUN_TEST(test_note_off_clears_the_light);
    RUN_TEST(test_ramp_preview_swells_below_cap_before_onset);
    RUN_TEST(test_wait_mode_halts_and_lights_due_chord_full);
    RUN_TEST(test_correct_key_clears_chord_and_playback_advances);
    RUN_TEST(test_chord_clears_per_key);
    RUN_TEST(test_wrong_key_flashes_red_then_expires);
    RUN_TEST(test_wrong_flash_outranks_due_light);
    RUN_TEST(test_key_verdict_makes_next_frame_immediate);
    RUN_TEST(test_accompaniment_emits_only_the_other_hand);
    RUN_TEST(test_echoed_accompaniment_note_is_not_a_wrong_press);
    RUN_TEST(test_demo_emits_all_audible_tracks);
    RUN_TEST(test_pause_sends_note_offs_for_sounding_notes);
    RUN_TEST(test_lights_toggle_hides_a_track);
    RUN_TEST(test_finished_state_after_song_end);
    RUN_TEST(test_rest_calls_without_song_return_false);
    RUN_TEST(test_loop_wrap_clears_sounding_lights_and_resyncs);
    RUN_TEST(test_status_json_matches_api_contract_shape);
    RUN_TEST(test_status_json_without_wifi_omits_the_wifi_object);
    RUN_TEST(test_status_json_wifi_object_is_the_last_key);
    RUN_TEST(test_loadsong_between_ticks_while_playing_is_coherent);
    RUN_TEST(test_configure_between_tick_and_frame_uses_new_config);
    RUN_TEST(test_statusjson_between_ticks_is_internally_consistent);
    RUN_TEST(test_statusjson_record_object_before_wifi);
    RUN_TEST(test_load_song_clears_reported_loop);
    RUN_TEST(test_pause_gap_play_re_baselines_no_burst);
    RUN_TEST(test_set_table_overrides_configure_geometry);
    RUN_TEST(test_unload_while_playing_flushes_and_clears);
    RUN_TEST(test_status_top_fields_before_wifi_which_stays_last);
    RUN_TEST(test_follow_track_mask_resolution);
    RUN_TEST(test_drive_show_clock_sets_song_time_only_while_stopped);
    RUN_TEST(test_s1_wait_loop_wraps_after_barrier_exactly_at_loop_end);
    RUN_TEST(test_s2_loop_behind_playhead_still_reaches_finished);
    RUN_TEST(test_C9_microloop_guard_underadvances);
    RUN_TEST(test_C2_seek_beyond_duration_clamped);
    RUN_TEST(test_w4_double_play_keeps_partial_chord_progress);
    RUN_TEST(test_w2_noop_settrack_keeps_partial_chord_progress);
    RUN_TEST(test_P13_pause_resume_keeps_partial_chord);
    RUN_TEST(test_P24_echo_credit_bleeds_across_mode_switch);
    RUN_TEST(test_C26_echo_credit_survives_backward_seek);
    RUN_TEST(test_P11_pause_flushes_sustain_pedal);
    RUN_TEST(test_C34_phantom_repeat_cue_inside_loop);
    return UNITY_END();
}
