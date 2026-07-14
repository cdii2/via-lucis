// M2 — ModeDirector: the top-mode gate matrix (brief §1) + the forced
// sources (test pattern, calibration probe) + the single frame dispatch.

#include <unity.h>

#include <string>
#include <vector>

#include "../helpers/fx_asserts.h"
#include "../helpers/smf_builder.h"
#include "../helpers/test_songs.h"
#include "vialucis/mode_director.h"
#include "vialucis/playback_engine.h"
#include "vialucis/settings.h"

using namespace vialucis;
using testsongs::chordSong;

void setUp() {}
void tearDown() {}

namespace {

std::vector<MidiOutMsg> gOut;
constexpr uint64_t kSec = 1000000ull;

struct Rig {
    PlaybackEngine engine;
    ModeDirector director{engine, 360};
    Rig() { engine.configure(Settings{}); }
    void load() {
        gOut.clear();
        engine.loadSong(chordSong(), "t.mid", gOut);
    }
    void unload() {
        gOut.clear();
        engine.unloadSong(gOut);
    }
    void tick(uint64_t nowUs) {
        gOut.clear();
        director.tick(nowUs, gOut);
    }
};

using fxtest::litCount;

}  // namespace

// --- the gate matrix -------------------------------------------------------

void test_boot_state_is_reactive() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(1 * kSec) == TopMode::Reactive);
}

void test_idle_timeout_arms_afk_with_no_song() {
    Rig r;
    r.tick(1 * kSec);  // baseline the idle clock
    // 180s of NOTHING but time (status GETs never touch the clock —
    // structurally: there is no director call for a GET).
    r.tick(182 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(182 * kSec) == TopMode::Afk);
    // AFK content: the fallback effect paints, strip is NOT dark (VL5).
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(182 * kSec)) > 0);
}

void test_song_loaded_makes_afk_unreachable() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    // An hour of dead-idle with a song loaded: NEVER AFK (hard requirement).
    for (int m = 1; m <= 60; ++m) {
        uint64_t t = (1 + 60ull * m) * kSec;
        r.tick(t);
        TEST_ASSERT_TRUE(r.director.topMode(t) == TopMode::Practice);
    }
}

void test_any_activity_wakes_afk_within_one_frame() {
    Rig r;
    r.tick(1 * kSec);
    r.tick(200 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(200 * kSec) == TopMode::Afk);
    // A key press wakes it instantly (no song ⇒ Reactive).
    r.director.onKeyDown(60, 100, 200 * kSec + 1000);
    TEST_ASSERT_TRUE(r.director.topMode(200 * kSec + 2000) ==
                     TopMode::Reactive);
    // Pedal/CC (any midi) would arrive via onMidiActivity — same effect.
    r.tick(400 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(400 * kSec) == TopMode::Afk);
    r.director.onMidiActivity(400 * kSec + 1000);
    TEST_ASSERT_TRUE(r.director.topMode(400 * kSec + 2000) ==
                     TopMode::Reactive);
    // A state-CHANGING route wakes it too.
    r.tick(600 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(600 * kSec) == TopMode::Afk);
    r.director.onWriteActivity(600 * kSec + 1000);
    TEST_ASSERT_TRUE(r.director.topMode(600 * kSec + 2000) ==
                     TopMode::Reactive);
}

void test_load_defaults_to_practice_presentation_is_explicit() {
    Rig r;
    r.tick(1 * kSec);
    // Presentation without a song is refused.
    TEST_ASSERT_FALSE(r.director.setPresentation(true));
    r.load();
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Practice);
    TEST_ASSERT_TRUE(r.director.setPresentation(true));
    TEST_ASSERT_TRUE(r.director.topMode(3 * kSec) == TopMode::Presentation);
    TEST_ASSERT_TRUE(r.director.setPresentation(false));
    TEST_ASSERT_TRUE(r.director.topMode(4 * kSec) == TopMode::Practice);
}

void test_timeout_zero_never_arms() {
    Rig r;
    r.director.setIdleTimeoutSec(0);
    r.tick(1 * kSec);
    r.tick(100000 * kSec);  // ~27 hours idle
    TEST_ASSERT_TRUE(r.director.topMode(100000 * kSec) == TopMode::Reactive);
}

void test_unload_returns_to_no_song_states() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    r.director.setPresentation(true);
    r.unload();
    r.director.setPresentation(false);  // App wiring clears it on unload
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Reactive);
    // ...and drifts to AFK on schedule afterwards.
    r.director.onWriteActivity(2 * kSec);  // the unload route itself
    r.tick(190 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(190 * kSec) == TopMode::Afk);
}

// --- frame dispatch --------------------------------------------------------

void test_reactive_renders_dark_practice_renders_engine() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_EQUAL_INT(0, litCount(r.director.renderFrame(1 * kSec)));
    r.load();
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);
    r.tick(2 * kSec + 100000);  // C4 sounding via the engine
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(2 * kSec + 100000)) > 0);
}

void test_test_pattern_is_a_forced_source_over_any_mode() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", gOut));
    const std::vector<Rgb>& f = r.director.renderFrame(1 * kSec);
    TEST_ASSERT_EQUAL_INT(1, litCount(f));  // the walking white dot
    TEST_ASSERT_TRUE(r.director.setTestPattern("rainbow", gOut));
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(1 * kSec)) > 300);
    TEST_ASSERT_FALSE(r.director.setTestPattern("nope", gOut));
    TEST_ASSERT_TRUE(r.director.setTestPattern("off", gOut));
    TEST_ASSERT_EQUAL_INT(0, litCount(r.director.renderFrame(1 * kSec)));
}

void test_pattern_activation_auto_pauses_playback() {
    // F3/A35 now lives in the director: EVERY caller keeps the
    // no-skipped-time-burst guarantee.
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("demo", "both", gOut);
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);              // baseline
    r.tick(2 * kSec + 100000);     // 100ms in, notes sounding
    std::vector<MidiOutMsg> out;
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", out));
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);  // paused
    int offs = 0;
    for (const MidiOutMsg& m : out)
        if (m.type == MidiOutType::NoteOff) ++offs;
    TEST_ASSERT_TRUE_MESSAGE(offs >= 1, "pause flushes sounding notes");
    // "off" does not auto-resume; the position is frozen where it paused.
    TEST_ASSERT_TRUE(r.director.setTestPattern("off", out));
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
}

void test_stale_presentation_never_survives_unload() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    TEST_ASSERT_TRUE(r.director.setPresentation(true));
    r.unload();
    r.tick(2 * kSec);  // the director notices the song is gone
    r.load();          // a FRESH load must land in Practice, not the
                       // previous song's Presentation
    TEST_ASSERT_TRUE(r.director.topMode(3 * kSec) == TopMode::Practice);
}

void test_afk_plays_the_configured_playlist_not_the_fallback() {
    // E3 end-to-end: a configured track must be what AFK renders. Pacifica
    // is blue/green dominated, so a track that isn't blue/green dominant
    // would mean the fallback (not the configured track) painted the strip.
    Rig r;
    fx::AfkConfig c;
    c.tracks.push_back({"pacifica", ""});
    r.director.setAfkConfig(c, 7);
    r.tick(1 * kSec);
    r.tick(200 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(200 * kSec) == TopMode::Afk);
    uint32_t rSum = 0, gSum = 0, bSum = 0;
    for (int i = 0; i < 30; ++i) {
        const std::vector<Rgb>& f = r.director.renderFrame(200 * kSec);
        for (const Rgb& px : f) {
            rSum += px.r;
            gSum += px.g;
            bSum += px.b;
        }
    }
    TEST_ASSERT_TRUE(gSum + bSum > 0);
    TEST_ASSERT_TRUE_MESSAGE(bSum + gSum > rSum * 3,
                             "configured pacifica, not the fallback");
}

void test_reactive_free_play_glows_and_decays() {
    // E2: with no song, a key press paints its calibrated LEDs; release
    // decays back to dark. The director must feed the layer both edges.
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    TEST_ASSERT_EQUAL_INT(0, litCount(r.director.renderFrame(1 * kSec)));
    r.director.onKeyDown(60, 127, 1 * kSec + 1000);
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(1 * kSec + 2000)) > 0);
    r.director.onKeyUp(60, 1 * kSec + 3000);
    // Default release 400ms = 25 fixed frames; render plenty.
    int lit = 1;
    for (int i = 0; i < 60 && lit > 0; ++i)
        lit = litCount(r.director.renderFrame(2 * kSec + i * 17000));
    TEST_ASSERT_EQUAL_INT(0, lit);
}

// --- P2: presentation playback on the song-time clock ----------------------

void test_presentation_plays_a_show_on_the_song_clock() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    Show s;
    s.meta.clockSource = 1;  // free-run
    s.meta.durationMs = 5000;
    s.meta.name = "t";
    s.effects.push_back("colorwaves");
    ShowCue cue;  // whole-strip autonomous effect, open-ended
    cue.endMs = 0xFFFFFFFFu;
    s.cues.push_back(cue);
    gOut.clear();
    r.director.startShow(std::move(s), 7, gOut);
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Presentation);
    // App wiring emulated: free-run = follow mode + play.
    gOut.clear();
    r.engine.setMode("follow", "both", gOut);
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);
    r.tick(2 * kSec + 100000);
    TEST_ASSERT_TRUE_MESSAGE(
        litCount(r.director.renderFrame(2 * kSec + 100000)) > 300,
        "the show paints the strip, not the placeholder dark frame");
    // Named P2 test: a tempo change mid-show keeps the Free-run clock
    // continuous — song position must not jump.
    uint64_t before = r.engine.positionUs();
    TEST_ASSERT_TRUE(r.engine.setTempo(50.0f));
    TEST_ASSERT_EQUAL_UINT64(before, r.engine.positionUs());
    r.tick(2 * kSec + 200000);  // still advancing, just slower
    TEST_ASSERT_TRUE(r.engine.positionUs() > before);
    // Stop: back to Practice, dark handed back to the practice renderer.
    r.director.stopShow();
    TEST_ASSERT_TRUE(r.director.topMode(3 * kSec) == TopMode::Practice);
}

void test_show_dies_with_the_song() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    Show s;
    s.effects.push_back("colorwaves");
    ShowCue cue;
    cue.endMs = 0xFFFFFFFFu;
    s.cues.push_back(cue);
    gOut.clear();
    r.director.startShow(std::move(s), 7, gOut);
    TEST_ASSERT_TRUE(r.director.showPlaying());
    r.unload();
    r.tick(2 * kSec);
    TEST_ASSERT_FALSE(r.director.showPlaying());
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Reactive);
}

// P-wave closing regression: leaving Presentation mid-show must clear
// showPlaying(), or a hidden still-playing show would 409 every subsequent
// upload and keep the song clock running with the strip back in Practice.
void test_leaving_presentation_midshow_clears_show_playing() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    Show s;
    s.effects.push_back("colorwaves");
    ShowCue cue;
    cue.endMs = 0xFFFFFFFFu;
    s.cues.push_back(cue);
    gOut.clear();
    r.director.startShow(std::move(s), 7, gOut);
    TEST_ASSERT_TRUE(r.director.showPlaying());
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Presentation);
    // Song stays loaded — this is NOT an unload; it is the explicit exit.
    TEST_ASSERT_TRUE(r.director.setPresentation(false));
    TEST_ASSERT_FALSE_MESSAGE(r.director.showPlaying(),
        "leaving Presentation must not leave a show playing (upload 409 desync)");
    TEST_ASSERT_TRUE(r.director.topMode(3 * kSec) == TopMode::Practice);
}

// --- P4: score-follow — the performer IS the Presentation clock -------------

namespace {

// A minimal one-cue show with the given clock source (chordSong is the
// loaded song: anchors 60@0 · 64@500ms · {67,71}@1000ms on the follow mask).
Show scoreFollowShow(uint8_t clock) {
    Show s;
    s.meta.clockSource = clock;
    s.meta.durationMs = 5000;
    s.meta.name = "sf";
    s.effects.push_back("colorwaves");
    ShowCue cue;
    cue.endMs = 0xFFFFFFFFu;
    s.cues.push_back(cue);
    return s;
}

}  // namespace

void test_score_follow_show_slaves_the_clock_to_the_performer() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Presentation);
    TEST_ASSERT_TRUE(r.director.scoreFollowActive());
    // The transport is NOT started — the performer is the only clock.
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
    // Pre-rolled at 0 until the first anchor is matched (§4a Q13).
    r.tick(2 * kSec);
    TEST_ASSERT_EQUAL_UINT64(0, r.engine.positionUs());
    // First anchor (60 at song 0) starts the clock; the second snaps the
    // song position on the key event itself, before any tick.
    r.director.onKeyDown(60, 100, 2 * kSec);
    TEST_ASSERT_EQUAL_UINT64(0, r.engine.positionUs());
    r.director.onKeyDown(64, 100, 2 * kSec + 500000);
    TEST_ASSERT_EQUAL_UINT64(500000, r.engine.positionUs());
    // Between events the tick coasts the clock, held at the next onset.
    r.tick(2 * kSec + 900000);
    uint64_t coasted = r.engine.positionUs();
    TEST_ASSERT_TRUE(coasted > 500000 && coasted <= 1000000);
    // The chord anchor {67,71} needs both core notes (per-key clearing).
    r.director.onKeyDown(67, 100, 3 * kSec);
    TEST_ASSERT_EQUAL_size_t(2, r.director.scoreFollower().nextAnchor());
    r.director.onKeyDown(71, 100, 3 * kSec + 100000);
    TEST_ASSERT_EQUAL_size_t(3, r.director.scoreFollower().nextAnchor());
    TEST_ASSERT_EQUAL_UINT64(1000000, r.engine.positionUs());
    // Past the final anchor the show tail keeps breathing on ticks.
    r.tick(4 * kSec);
    TEST_ASSERT_TRUE(r.engine.positionUs() > 1000000);
    // The show paints the strip from the follower-driven song clock.
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(4 * kSec)) > 300);
}

void test_score_follow_wrong_note_never_flashes_or_moves_the_clock() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    r.director.onKeyDown(60, 100, 2 * kSec);
    r.director.onKeyDown(64, 100, 2 * kSec + 500000);
    TEST_ASSERT_EQUAL_UINT64(500000, r.engine.positionUs());
    // A wrong note at the SAME instant: the clock must not move, rewind,
    // or stall (§4a Q8) — and it can never red-flash: the engine verdict
    // path is inert by construction (transport stopped, state Idle).
    r.director.onKeyDown(100, 100, 2 * kSec + 500000);
    TEST_ASSERT_EQUAL_UINT64(500000, r.engine.positionUs());
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
    TEST_ASSERT_TRUE(r.director.scoreFollower().state() ==
                     ScoreFollower::FollowState::Following);
}

void test_score_follow_deactivates_on_every_show_exit_path() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    // stopShow.
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    TEST_ASSERT_TRUE(r.director.scoreFollowActive());
    r.director.stopShow();
    TEST_ASSERT_FALSE(r.director.scoreFollowActive());
    // Leaving Presentation mid-show.
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    TEST_ASSERT_TRUE(r.director.setPresentation(false));
    TEST_ASSERT_FALSE(r.director.scoreFollowActive());
    // Unload (the tick cleanup path).
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    r.unload();
    r.tick(2 * kSec);
    TEST_ASSERT_FALSE(r.director.scoreFollowActive());
    // A demo-clock show never activates it.
    r.load();
    gOut.clear();
    r.director.startShow(scoreFollowShow(0), 7, gOut);
    TEST_ASSERT_TRUE(r.director.showPlaying());
    TEST_ASSERT_FALSE(r.director.scoreFollowActive());
}

// --- the probe as a director-owned forced source (moved from C3) -----------

void test_probe_dot_outranks_test_pattern_and_modes() {
    Rig r;
    r.tick(1 * kSec);
    r.director.setTestPattern("rainbow", gOut);
    TEST_ASSERT_EQUAL(ModeDirector::ProbeArm::Ok,
                      r.director.armProbe(123, 1 * kSec, 30000));
    const std::vector<Rgb>& f = r.director.renderFrame(1 * kSec);
    TEST_ASSERT_EQUAL_INT(1, litCount(f));
    TEST_ASSERT_EQUAL_UINT8(255, f[123].r);
    // Probe cleared ⇒ the pattern resumes.
    r.director.cancelProbe();
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(1 * kSec)) > 300);
}

void test_probe_capture_consumes_before_practice() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("wait", "both", gOut);
    TEST_ASSERT_EQUAL(ModeDirector::ProbeArm::Ok,
                      r.director.armProbe(50, 1 * kSec, 30000));
    r.director.onKeyDown(60, 100, 2 * kSec);  // the due key — probe eats it
    TEST_ASSERT_FALSE(r.director.probeArmed());
    std::string p = r.director.probeJson();
    TEST_ASSERT_TRUE(p.find("\"note\":60") != std::string::npos);
    // Practice never saw it: play still waits for the chord.
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(3 * kSec);
    r.tick(3 * kSec + 200000);
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"state\":\"waiting\"") !=
                     std::string::npos);
}

void test_probe_refused_while_playing_and_cancelled_by_play() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    TEST_ASSERT_EQUAL(ModeDirector::ProbeArm::Playing,
                      r.director.armProbe(50, 1 * kSec, 30000));
    gOut.clear();
    r.engine.transport("pause", 0, gOut);
    TEST_ASSERT_EQUAL(ModeDirector::ProbeArm::Ok,
                      r.director.armProbe(50, 2 * kSec, 30000));
    // Playback starting cancels the armed probe on the next tick.
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(3 * kSec);
    TEST_ASSERT_FALSE(r.director.probeArmed());
    TEST_ASSERT_TRUE(r.director.probeJson().find("\"note\":null") !=
                     std::string::npos);
}

// B-1 (what-if audit G13, A94): the probe must refuse during ANY playing
// show, including score-follow — its transport is deliberately STOPPED (the
// performer is the only clock), so the ordinary engine-Playing check alone
// never catches it, and an armed probe would eat the performer's next key
// press before the follower sees it. Adapted from the audit repro
// test_s4_probe_refused_during_score_follow_show (audit/whatif-throwaway).
void test_s4_probe_refused_during_score_follow_show() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("follow", "both", gOut);
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 1234, gOut);
    TEST_ASSERT_TRUE(r.director.scoreFollowActive());
    TEST_ASSERT_EQUAL_MESSAGE(
        static_cast<int>(ModeDirector::ProbeArm::Playing),
        static_cast<int>(r.director.armProbe(5, 1 * kSec, 30000)),
        "probe must be refused while a score-follow show performs");
}

void test_probe_bad_led_timeout_and_cancel() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_EQUAL(ModeDirector::ProbeArm::BadLed,
                      r.director.armProbe(360, 1 * kSec, 30000));
    r.director.armProbe(123, 1 * kSec, 30000);
    r.tick(1 * kSec + 31 * kSec);  // 31s later: expired
    TEST_ASSERT_FALSE(r.director.probeArmed());
    TEST_ASSERT_TRUE(r.director.probeJson().find("\"note\":null") !=
                     std::string::npos);
    r.director.armProbe(123, 40 * kSec, 30000);
    r.director.onKeyDown(60, 100, 41 * kSec);
    r.director.cancelProbe();
    TEST_ASSERT_TRUE(r.director.probeJson().find("\"note\":null") !=
                     std::string::npos);
}

void test_probe_arm_counts_as_activity_but_capture_wakes_too() {
    Rig r;
    r.tick(1 * kSec);
    r.tick(200 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(200 * kSec) == TopMode::Afk);
    // A key press (even one the probe consumes) is activity.
    r.director.armProbe(123, 200 * kSec, 30000);
    r.director.onKeyDown(60, 100, 200 * kSec + 1000);
    TEST_ASSERT_TRUE(r.director.topMode(200 * kSec + 2000) ==
                     TopMode::Reactive);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_state_is_reactive);
    RUN_TEST(test_idle_timeout_arms_afk_with_no_song);
    RUN_TEST(test_song_loaded_makes_afk_unreachable);
    RUN_TEST(test_any_activity_wakes_afk_within_one_frame);
    RUN_TEST(test_load_defaults_to_practice_presentation_is_explicit);
    RUN_TEST(test_timeout_zero_never_arms);
    RUN_TEST(test_unload_returns_to_no_song_states);
    RUN_TEST(test_reactive_renders_dark_practice_renders_engine);
    RUN_TEST(test_test_pattern_is_a_forced_source_over_any_mode);
    RUN_TEST(test_pattern_activation_auto_pauses_playback);
    RUN_TEST(test_stale_presentation_never_survives_unload);
    RUN_TEST(test_afk_plays_the_configured_playlist_not_the_fallback);
    RUN_TEST(test_presentation_plays_a_show_on_the_song_clock);
    RUN_TEST(test_show_dies_with_the_song);
    RUN_TEST(test_leaving_presentation_midshow_clears_show_playing);
    RUN_TEST(test_score_follow_show_slaves_the_clock_to_the_performer);
    RUN_TEST(test_score_follow_wrong_note_never_flashes_or_moves_the_clock);
    RUN_TEST(test_score_follow_deactivates_on_every_show_exit_path);
    RUN_TEST(test_reactive_free_play_glows_and_decays);
    RUN_TEST(test_probe_dot_outranks_test_pattern_and_modes);
    RUN_TEST(test_probe_capture_consumes_before_practice);
    RUN_TEST(test_probe_refused_while_playing_and_cancelled_by_play);
    RUN_TEST(test_s4_probe_refused_during_score_follow_show);
    RUN_TEST(test_probe_bad_led_timeout_and_cancel);
    RUN_TEST(test_probe_arm_counts_as_activity_but_capture_wakes_too);
    return UNITY_END();
}
