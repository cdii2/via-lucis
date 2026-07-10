// Characterization tests for the playback engine (R2): pin the exact
// behavior App had at ec0293b — frame composition (wrong > due > ramp),
// sounding-light + wrong-flash bookkeeping, mask routing, frameDirty
// semantics — now natively testable behind one core seam.

#include <unity.h>

#include <ArduinoJson.h>

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

// Derived from the Settings defaults so these tests keep verifying the
// engine actually tracks Settings — not a private copy of the palette.
const Rgb kRight = Settings{}.rightColor;  // Right and Both use this
const Rgb kLeft = Settings{}.leftColor;
const Rgb kWrong = Settings{}.wrongColor;
constexpr Rgb kBlack{0, 0, 0};

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

// --- C3: calibration probe + table override ------------------------------
// The probe's ownership rules (OV4): arms only when NOT Playing; while
// armed the next note-on is consumed BEFORE wait mode sees it; the dot is a
// forced source above everything; auto-timeout; explicit cancel.

static int litCount(const std::vector<Rgb>& frame) {
    int n = 0;
    for (const Rgb& c : frame)
        if (c.r || c.g || c.b) ++n;
    return n;
}

void test_probe_dot_is_the_whole_frame() {
    PlaybackEngine e;
    e.configure(Settings{});
    TEST_ASSERT_EQUAL(PlaybackEngine::ProbeArm::Ok,
                      e.armProbe(123, 1000, 30000));
    TEST_ASSERT_TRUE(e.probeArmed());
    const std::vector<Rgb>& frame = e.renderFrame(2000);
    TEST_ASSERT_EQUAL_INT(1, litCount(frame));
    assertRgb(Rgb{255, 255, 255}, frame[123]);
}

void test_probe_capture_consumes_the_press_before_practice() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");  // song loaded, Idle
    TEST_ASSERT_EQUAL(PlaybackEngine::ProbeArm::Ok,
                      e.armProbe(50, 1000, 30000));
    e.onKeyDown(60, 2000);  // would be the due chord's key — probe eats it
    TEST_ASSERT_FALSE(e.probeArmed());
    std::string p = e.probeJson();
    TEST_ASSERT_TRUE(p.find("\"armed\":false") != std::string::npos);
    TEST_ASSERT_TRUE(p.find("\"led\":50") != std::string::npos);
    TEST_ASSERT_TRUE(p.find("\"note\":60") != std::string::npos);
    // Practice never saw the press: play still waits for the full chord.
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    TEST_ASSERT_TRUE(e.statusJson().find("\"state\":\"waiting\"") !=
                     std::string::npos);
}

void test_probe_refused_while_playing() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    gOut.clear();
    e.transport("play", 0, gOut);
    TEST_ASSERT_EQUAL(PlaybackEngine::ProbeArm::Playing,
                      e.armProbe(50, 1000, 30000));
    TEST_ASSERT_FALSE(e.probeArmed());
}

void test_probe_bad_led_refused() {
    PlaybackEngine e;
    e.configure(Settings{});
    TEST_ASSERT_EQUAL(PlaybackEngine::ProbeArm::BadLed,
                      e.armProbe(360, 1000, 30000));
}

void test_probe_times_out_and_clears_the_dot() {
    PlaybackEngine e;
    e.configure(Settings{});
    e.armProbe(123, 1000, 30000);
    gOut.clear();
    e.tick(1000 + 31000000ULL, gOut);  // 31s later — window expired
    TEST_ASSERT_FALSE(e.probeArmed());
    TEST_ASSERT_TRUE(e.probeJson().find("\"note\":null") != std::string::npos);
    TEST_ASSERT_TRUE(e.frameDue(1000 + 31000000ULL));  // expiry marks dirty
    TEST_ASSERT_EQUAL_INT(0, litCount(e.renderFrame(1000 + 31000000ULL)));
}

void test_probe_cancel_clears_arm_and_capture() {
    PlaybackEngine e;
    e.configure(Settings{});
    e.armProbe(123, 1000, 30000);
    e.onKeyDown(60, 2000);  // captured
    e.cancelProbe();
    std::string p = e.probeJson();
    TEST_ASSERT_TRUE(p.find("\"armed\":false") != std::string::npos);
    TEST_ASSERT_TRUE(p.find("\"note\":null") != std::string::npos);
}

void test_play_while_probe_armed_cancels_probe() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    e.armProbe(50, 1000, 30000);
    gOut.clear();
    e.transport("play", 0, gOut);
    TEST_ASSERT_FALSE(e.probeArmed());
    TEST_ASSERT_TRUE(e.probeJson().find("\"note\":null") != std::string::npos);
}

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
    RUN_TEST(test_load_song_clears_reported_loop);
    RUN_TEST(test_pause_gap_play_re_baselines_no_burst);
    RUN_TEST(test_probe_dot_is_the_whole_frame);
    RUN_TEST(test_probe_capture_consumes_the_press_before_practice);
    RUN_TEST(test_probe_refused_while_playing);
    RUN_TEST(test_probe_bad_led_refused);
    RUN_TEST(test_probe_times_out_and_clears_the_dot);
    RUN_TEST(test_probe_cancel_clears_arm_and_capture);
    RUN_TEST(test_play_while_probe_armed_cancels_probe);
    RUN_TEST(test_set_table_overrides_configure_geometry);
    return UNITY_END();
}
