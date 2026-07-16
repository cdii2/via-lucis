// M2 â€” ModeDirector: the top-mode gate matrix (brief Â§1) + the forced
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

// A minimal one-cue show with the given clock source (chordSong is the
// loaded song: anchors 60@0 Â· 64@500ms Â· {67,71}@1000ms on the follow mask).
// clock: 0 = demo, 1 = free-run, 2 = score-follow.
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

// --- the gate matrix -------------------------------------------------------

void test_boot_state_is_reactive() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(1 * kSec) == TopMode::Reactive);
}

void test_idle_timeout_arms_afk_with_no_song() {
    Rig r;
    r.tick(1 * kSec);  // baseline the idle clock
    // 180s of NOTHING but time (status GETs never touch the clock â€”
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
    // A key press wakes it instantly (no song â‡’ Reactive).
    r.director.onKeyDown(60, 100, 200 * kSec + 1000);
    TEST_ASSERT_TRUE(r.director.topMode(200 * kSec + 2000) ==
                     TopMode::Reactive);
    // Pedal/CC (any midi) would arrive via onMidiActivity â€” same effect.
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
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", 1 * kSec, gOut));
    const std::vector<Rgb>& f = r.director.renderFrame(1 * kSec);
    TEST_ASSERT_EQUAL_INT(1, litCount(f));  // the walking white dot
    TEST_ASSERT_TRUE(r.director.setTestPattern("rainbow", 1 * kSec, gOut));
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(1 * kSec)) > 300);
    TEST_ASSERT_FALSE(r.director.setTestPattern("nope", 1 * kSec, gOut));
    TEST_ASSERT_TRUE(r.director.setTestPattern("off", 1 * kSec, gOut));
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
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", 2 * kSec + 100000, out));
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);  // paused
    int offs = 0;
    for (const MidiOutMsg& m : out)
        if (m.type == MidiOutType::NoteOff) ++offs;
    TEST_ASSERT_TRUE_MESSAGE(offs >= 1, "pause flushes sounding notes");
    // "off" does not auto-resume; the position is frozen where it paused.
    TEST_ASSERT_TRUE(r.director.setTestPattern("off", 2 * kSec + 150000, out));
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
}

// B-2 (what-if audit G15, A95): a test pattern POST during a playing
// DEMO-clock show must not silently pause the show's transport â€” the pattern
// is a visual overlay that borrows the strip, it must not stall a live
// performance (or the piano's own audio, which the demo transport drives).
// Adapted from test_w29_test_pattern_refused_or_inert_during_show
// (audit/whatif-throwaway).
void test_w29_test_pattern_refused_or_inert_during_show() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("follow", "both", gOut);
    gOut.clear();
    r.director.startShow(scoreFollowShow(0), 99, gOut);  // demo clock
    r.tick(1000);
    r.tick(200000);
    TEST_ASSERT_TRUE(r.director.showPlaying());
    bool accepted = r.director.setTestPattern("rainbow", 200000, gOut);
    // Correct behavior: refuse the pattern while a show performs, or at
    // minimum leave the show's clock running.
    if (accepted) {
        r.tick(400000);
        r.tick(700000);
        TEST_ASSERT_TRUE_MESSAGE(
            r.engine.state() == PlayState::Playing,
            "test pattern must not silently pause a live show's transport");
    }
}

// B-2 (what-if audit G16, A95): the inverse hole â€” a test pattern over a
// SCORE-FOLLOW show must freeze its clock (the follower otherwise keeps
// driving song position under the pattern, since there is no transport to
// auto-pause). Adapted from test_C22_test_pattern_freezes_scorefollow_clock.
void test_C22_test_pattern_freezes_scorefollow_clock() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    r.director.onKeyDown(60, 100, 1000000);  // match first anchor
    r.tick(1100000);
    r.tick(1200000);                          // clock coasts
    uint64_t frozen = r.engine.positionUs();
    r.director.setTestPattern("strip", 1200000, gOut);  // pattern up â‡’ clock freezes
    r.director.onKeyDown(64, 100, 1300000);    // next anchor
    r.tick(1400000);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        frozen, r.engine.positionUs(),
        "a test pattern over a score-follow show must freeze its clock");
}

// B-3 (what-if audit G14, A96): a mode PUT during a playing show must not
// freeze its clock. The original repro (test_s3, audit/whatif-throwaway)
// called the raw PlaybackEngine::setMode directly, bypassing any policy
// layer entirely â€” since wait mode's barrier-holding is inherent to the
// engine (there is no way to "leave the clock running" once mode becomes
// "wait"), the only fix that can work without touching PlaybackEngine
// (FIX-A's territory) is to refuse the switch at the ModeDirector layer, so
// it never reaches the engine at all. Adapted to exercise the new
// ModeDirector::setMode wrapper (which App::setMode now routes through).
void test_s3_setmode_during_show_does_not_freeze_show_clock() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("follow", "both", gOut);
    gOut.clear();
    r.director.startShow(scoreFollowShow(0), 1234, gOut);  // demo clock
    r.tick(1000);
    r.tick(200000);   // demo clock moves
    uint64_t before = r.engine.positionUs();
    TEST_ASSERT_TRUE_MESSAGE(before > 0, "show clock advancing before setMode");
    // Mid-show mode switch (the /api/mode route calls exactly this, via
    // App::setMode -> ModeDirector::setMode):
    bool ok = r.director.setMode("wait", "both", gOut);
    TEST_ASSERT_FALSE_MESSAGE(ok,
                              "mode PUT must be refused while a show plays");
    r.tick(400000);
    r.tick(1400000);  // well past where wait mode would have armed a barrier
    uint64_t after = r.engine.positionUs();
    TEST_ASSERT_TRUE_MESSAGE(after > 900000,
                             "show clock must keep advancing after a mode PUT");
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
    // continuous â€” song position must not jump.
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
    // Song stays loaded â€” this is NOT an unload; it is the explicit exit.
    TEST_ASSERT_TRUE(r.director.setPresentation(false));
    TEST_ASSERT_FALSE_MESSAGE(r.director.showPlaying(),
        "leaving Presentation must not leave a show playing (upload 409 desync)");
    TEST_ASSERT_TRUE(r.director.topMode(3 * kSec) == TopMode::Practice);
}

// --- P4: score-follow â€” the performer IS the Presentation clock -------------
// (scoreFollowShow() is defined in the top anonymous namespace â€” B-2 needs
// it too, for the demo/score-follow test-pattern-during-show tests.)

void test_score_follow_show_slaves_the_clock_to_the_performer() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Presentation);
    TEST_ASSERT_TRUE(r.director.scoreFollowActive());
    // The transport is NOT started â€” the performer is the only clock.
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
    // Pre-rolled at 0 until the first anchor is matched (Â§4a Q13).
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
    // or stall (Â§4a Q8) â€” and it can never red-flash: the engine verdict
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
    r.director.setTestPattern("rainbow", 1 * kSec, gOut);
    TEST_ASSERT_EQUAL(ModeDirector::ProbeArm::Ok,
                      r.director.armProbe(123, 1 * kSec, 30000));
    const std::vector<Rgb>& f = r.director.renderFrame(1 * kSec);
    TEST_ASSERT_EQUAL_INT(1, litCount(f));
    TEST_ASSERT_EQUAL_UINT8(255, f[123].r);
    // Probe cleared â‡’ the pattern resumes.
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
    r.director.onKeyDown(60, 100, 2 * kSec);  // the due key â€” probe eats it
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
// show, including score-follow â€” its transport is deliberately STOPPED (the
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

// --- PIN-E coverage pack (audit Â§3, test-only pinning tests) ---------------

// Â§3 item 2: CC64 (sustain pedal) during a wait hold is a no-op for the
// verdict â€” WaitMode has no pedal path at all, and renderFrame never reads
// a pedal latch. Pedal traffic during a barrier hold must leave the wait
// state (and the piano's position) completely untouched.
void test_p2_cc64_during_wait_hold_is_noop_for_verdict() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("wait", "both", gOut);
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);
    r.tick(2 * kSec + 50000);  // barrier holds at chord1 {60}
    std::string before = r.engine.statusJson();
    TEST_ASSERT_TRUE(before.find("\"state\":\"waiting\"") != std::string::npos);
    TEST_ASSERT_TRUE(before.find("\"pendingNotes\":[60]") != std::string::npos);
    uint64_t posBefore = r.engine.positionUs();

    r.director.onPedal(127, 2 * kSec + 60000);  // sustain down mid-hold
    r.tick(2 * kSec + 70000);
    r.director.onPedal(0, 2 * kSec + 80000);    // sustain up
    r.tick(2 * kSec + 90000);

    std::string after = r.engine.statusJson();
    TEST_ASSERT_TRUE(after.find("\"state\":\"waiting\"") != std::string::npos);
    TEST_ASSERT_TRUE(after.find("\"pendingNotes\":[60]") != std::string::npos);
    TEST_ASSERT_EQUAL_UINT64(posBefore, r.engine.positionUs());
}

// Â§3 item 3: presses while Paused/Finished are inert for practice
// (playback_engine.cpp's onKeyDown early-returns when state_ != Playing)
// but still feed capture/reactive (mode_director.cpp's onKeyDown calls
// capture_.onNoteOn unconditionally). Covers both the Paused mid-hold case
// and the Finished-after-song-end case with a Play-along take running
// throughout.
void test_p3_presses_while_paused_or_finished_are_inert_for_practice_but_feed_capture() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("wait", "both", gOut);
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      r.director.armRecord(1 << 20, false, 90, 1 * kSec));

    // --- Paused sub-case ---------------------------------------------------
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);
    r.tick(2 * kSec + 50000);  // barrier holds at chord1 {60}
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[60]") !=
                     std::string::npos);
    gOut.clear();
    r.engine.transport("pause", 0, gOut);
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
    r.director.onKeyDown(60, 100, 2 * kSec + 100000);  // pressed WHILE paused
    // Practice never saw it: the due chord is untouched.
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[60]") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(r.director.recordState() == CaptureState::Recording);
    // Resume: the SAME key must be pressed AGAIN to actually clear it â€” proof
    // the paused press was truly inert, not silently consumed by practice.
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(3 * kSec);
    r.director.onKeyDown(60, 100, 3 * kSec + 10000);
    r.tick(3 * kSec + 700000);  // advance to and hold at chord2 {64}
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[64]") !=
                     std::string::npos);

    // --- Finished sub-case ---------------------------------------------------
    r.director.onKeyDown(64, 100, 3 * kSec + 710000);
    r.tick(3 * kSec + 1200000);  // advance to and hold at the final chord
    r.director.onKeyDown(67, 100, 3 * kSec + 1210000);
    r.director.onKeyDown(71, 100, 3 * kSec + 1220000);
    r.tick(3 * kSec + 2000000);  // song completes
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"state\":\"finished\"") !=
                     std::string::npos);
    r.director.onKeyDown(100, 100, 3 * kSec + 2100000);  // pressed WHILE finished
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"state\":\"finished\"") !=
                     std::string::npos);  // still inert for practice

    CaptureTake take = r.director.stopRecord(3 * kSec + 2300000);
    TEST_ASSERT_FALSE(take.empty);
    bool sawPausedPress = false, sawFinishedPress = false;
    for (const auto& n : take.notes) {
        if (n.note == 60) sawPausedPress = true;
        if (n.note == 100) sawFinishedPress = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawPausedPress, "paused press still fed capture");
    TEST_ASSERT_TRUE_MESSAGE(sawFinishedPress,
                             "finished-state press still fed capture");
}

// Â§3 item 6: AFK unreachable during a PAUSED (not merely loaded) practice
// session â€” topMode's gate is engine_.songLoaded() only, with no PlayState
// check, so a real play-then-pause session must stay just as AFK-proof as
// a song that was merely loaded and never played.
void test_p6_afk_unreachable_while_paused_mid_practice() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("follow", "both", gOut);
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);
    r.tick(2 * kSec + 300000);  // real playback happened
    gOut.clear();
    r.engine.transport("pause", 0, gOut);
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
    for (int m = 1; m <= 60; ++m) {
        uint64_t t = (2 + 60ull * m) * kSec;
        r.tick(t);
        TEST_ASSERT_TRUE(r.director.topMode(t) == TopMode::Practice);
    }
}

// Â§3 item 7: test-pattern auto-pause during a LIVE barrier hold â€” the
// pending chord must survive the F3/A35 auto-pause, and "off" (which never
// auto-resumes) followed by an explicit resume must re-arm the exact same
// hold rather than losing or corrupting it.
void test_p7_test_pattern_autopause_preserves_barrier_hold_and_resumes() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("wait", "both", gOut);
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);
    r.tick(2 * kSec + 50000);  // barrier holds at chord1 {60}
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[60]") !=
                     std::string::npos);

    std::vector<MidiOutMsg> patOut;
    TEST_ASSERT_TRUE(
        r.director.setTestPattern("strip", 2 * kSec + 50000, patOut));  // auto-pause
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);
    TEST_ASSERT_TRUE_MESSAGE(
        r.engine.statusJson().find("\"pendingNotes\":[60]") !=
            std::string::npos,
        "the pending chord must survive the auto-pause, not wipe");

    TEST_ASSERT_TRUE(
        r.director.setTestPattern("off", 2 * kSec + 60000, patOut));  // no auto-resume
    TEST_ASSERT_TRUE(r.engine.state() == PlayState::Idle);

    gOut.clear();
    r.engine.transport("play", 0, gOut);  // explicit resume re-arms
    r.tick(3 * kSec);
    r.tick(3 * kSec + 10000);
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[60]") !=
                     std::string::npos);
    // The still-owed note now clears normally â€” the hold truly survived.
    r.director.onKeyDown(60, 100, 3 * kSec + 20000);
    r.tick(3 * kSec + 700000);
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"state\":\"waiting\"") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[64]") !=
                     std::string::npos);
}

// Â§3 item 8: double-arm record race at the ModeDirector layer â€” the second
// arm must be refused (AlreadyArmed) AND must leave the director's OWN
// bookkeeping (countIn_/bpm_) exactly as the first arm set it, not
// partially overwritten by the refused call's parameters.
void test_p8_double_arm_record_race_at_director_layer() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      r.director.armRecord(1 << 20, true, 120, 1 * kSec));
    ArmResult second =
        r.director.armRecord(1 << 20, false, 60, 1 * kSec + 500);
    TEST_ASSERT_EQUAL(ArmResult::AlreadyArmed, second);
    TEST_ASSERT_EQUAL_UINT16(120, r.director.recordBpm());
    TEST_ASSERT_TRUE_MESSAGE(
        r.director.recordCountIn(),
        "the refused second arm must not clobber the first arm's countIn");
}

// Â§3 item 10: director-level pedal-echo integration. mode_director.cpp's
// tick() scans the engine's MIDI-out for a Cc(64) message and credits
// capture's OWN echo guard (capture_.pedalSent) â€” unit-tested only via
// direct pedalSent() calls until now; this exercises the actual scan.
void test_p10_director_level_pedal_echo_excludes_song_cc64_echo() {
    Rig r;
    r.tick(1 * kSec);
    // Custom song: C4 on@0 off@480t (500ms), sustain pedal CC64=127 @480t.
    smf::Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::cc(ev, 0, 0, 64, 127);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    MidiSong song = parseMidi(file.data(), file.size()).song;

    gOut.clear();
    r.engine.loadSong(std::move(song), "pedal.mid", gOut);
    r.engine.setMode("demo", "both", gOut);
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      r.director.armRecord(1 << 20, false, 90, 1 * kSec));
    // A real user press (distinct pitch from the song) starts the tape.
    r.director.onKeyDown(72, 100, 1 * kSec + 10000);
    TEST_ASSERT_TRUE(r.director.recordState() == CaptureState::Recording);

    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);                    // baseline
    r.tick(2 * kSec + 501000);           // crosses the pedal event at 500ms

    // The piano echoes the CC64 it just received back over BLE â€” must be
    // excluded from the take by the tick-scan credit, not double-captured.
    r.director.onPedal(127, 2 * kSec + 502000);
    CaptureTake take = r.director.stopRecord(2 * kSec + 600000);
    TEST_ASSERT_EQUAL_size_t(0, take.pedals.size());
    TEST_ASSERT_EQUAL_size_t(1, take.notes.size());
    TEST_ASSERT_EQUAL_UINT8(72, take.notes[0].note);
}

// Â§3 item 15: a Play-along take survives a song unload mid-take â€” topMode
// ordering (songLoaded() > Record > idle-timeout) means losing the song
// degrades a live take to Free capture WITHOUT dropping or resetting it.
void test_p15_playalong_take_survives_song_unload_mid_take() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      r.director.armRecord(1 << 20, false, 90, 1 * kSec));
    r.director.onKeyDown(60, 100, 2 * kSec);  // starts the tape (Play-along)
    TEST_ASSERT_TRUE(r.director.recordState() == CaptureState::Recording);
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Practice);

    r.unload();  // mid-take song unload
    r.tick(2 * kSec + 1000);
    TEST_ASSERT_TRUE_MESSAGE(
        r.director.topMode(2 * kSec + 1000) == TopMode::Record,
        "losing the song must degrade to Free capture, not hide the take");
    TEST_ASSERT_TRUE(r.director.recordState() == CaptureState::Recording);

    r.director.onKeyDown(64, 100, 2 * kSec + 2000);  // still the same take
    r.director.onKeyUp(60, 2 * kSec + 3000);
    r.director.onKeyUp(64, 2 * kSec + 4000);
    CaptureTake take = r.director.stopRecord(2 * kSec + 5000);
    TEST_ASSERT_FALSE(take.empty);
    TEST_ASSERT_EQUAL_size_t(2, take.notes.size());
}

// --- B1a: orphaned test-pattern auto-clear (BUGFIX-PLAN Â§3-B1) -------------

void test_b1a_test_pattern_expires_after_timeout() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", 1 * kSec, gOut));
    TEST_ASSERT_TRUE(r.director.testPatternActive());
    uint64_t timeoutUs = static_cast<uint64_t>(kTestPatternTimeoutMs) * 1000ull;
    // Just under the timeout: a vanished client's pattern is still up.
    r.tick(1 * kSec + timeoutUs - 1000);
    TEST_ASSERT_TRUE_MESSAGE(r.director.testPatternActive(),
                             "must not expire before the timeout elapses");
    // At the timeout: auto-cleared, with nothing else having touched it.
    r.tick(1 * kSec + timeoutUs);
    TEST_ASSERT_FALSE_MESSAGE(
        r.director.testPatternActive(),
        "an orphaned test pattern must expire after kTestPatternTimeoutMs "
        "(B1a/A120)");
}

void test_b1a_test_pattern_timer_resets_on_reactivation() {
    // Switching strip<->rainbow (or re-POSTing the same pattern) is a touch
    // â€” it must restart the orphan timer, not let a stale activation time
    // keep counting toward an early expiry underneath an actively-used
    // pattern.
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", 1 * kSec, gOut));
    uint64_t timeoutUs = static_cast<uint64_t>(kTestPatternTimeoutMs) * 1000ull;
    uint64_t touchAt = 1 * kSec + timeoutUs - 1000;  // just before the ORIGINAL expiry
    TEST_ASSERT_TRUE(r.director.setTestPattern("rainbow", touchAt, gOut));
    // Well past the original activation's timeout, but well under the
    // refreshed one â€” must still be active.
    r.tick(touchAt + timeoutUs - 1000);
    TEST_ASSERT_TRUE_MESSAGE(r.director.testPatternActive(),
                             "re-activating must restart the orphan timer");
    r.tick(touchAt + timeoutUs);
    TEST_ASSERT_FALSE(r.director.testPatternActive());
}

void test_b1a_test_pattern_clears_on_fresh_song_load() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", 1 * kSec, gOut));
    TEST_ASSERT_TRUE(r.director.testPatternActive());
    r.load();  // a fresh load reclaims the strip (next tick's edge check)
    r.tick(2 * kSec);
    TEST_ASSERT_FALSE_MESSAGE(
        r.director.testPatternActive(),
        "a fresh song load must auto-clear an orphaned test pattern (B1a)");
}

void test_b1a_test_pattern_clears_when_play_begins() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    r.tick(2 * kSec);
    // Set while Idle â€” no auto-pause fires (F3/A35 only pauses if it was
    // Playing at set time), reproducing exactly the "vanished client, the
    // engine sits Idle underneath the pattern" scenario B1a targets.
    TEST_ASSERT_TRUE(r.director.setTestPattern("strip", 2 * kSec, gOut));
    TEST_ASSERT_TRUE(r.director.testPatternActive());
    gOut.clear();
    r.engine.transport("play", 0, gOut);  // the physical player hits Play
    r.tick(3 * kSec);
    TEST_ASSERT_FALSE_MESSAGE(
        r.director.testPatternActive(),
        "transport entering Playing must reclaim the strip from an "
        "orphaned pattern (B1a)");
    TEST_ASSERT_TRUE_MESSAGE(
        r.engine.state() == PlayState::Playing,
        "the play action itself must not be undone by the auto-clear");
}

void test_b1a_test_pattern_clears_on_show_start() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.setTestPattern("rainbow", 1 * kSec, gOut));
    TEST_ASSERT_TRUE(r.director.testPatternActive());
    r.load();
    Show s;
    s.effects.push_back("colorwaves");
    ShowCue cue;
    cue.endMs = 0xFFFFFFFFu;
    s.cues.push_back(cue);
    gOut.clear();
    r.director.startShow(std::move(s), 7, gOut);  // no intervening tick()
    TEST_ASSERT_FALSE_MESSAGE(
        r.director.testPatternActive(),
        "starting a show must reclaim the strip from an orphaned pattern "
        "(B1a) immediately, not waiting for the next tick");
}

// --- B1b: a Finished show tears itself down (BUGFIX-PLAN Â§3-B1) ------------

void test_b1b_finished_demo_show_tears_itself_down() {
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.director.startShow(scoreFollowShow(0), 99, gOut);  // demo clock
    TEST_ASSERT_TRUE(r.director.showPlaying());
    TEST_ASSERT_TRUE(r.director.topMode(1 * kSec) == TopMode::Presentation);
    r.tick(1 * kSec + 10000);            // baseline (transport play re-arms
                                          // the engine's own delta clock)
    uint64_t finishedAt = 1 * kSec + 10000 + 1700000;  // ~1.7s later: past
                                                        // chordSong's 1.5s end
    r.tick(finishedAt);
    TEST_ASSERT_TRUE_MESSAGE(
        r.engine.state() == PlayState::Finished,
        "precondition: the demo-clocked practiced song reached its end");
    TEST_ASSERT_FALSE_MESSAGE(
        r.director.showPlaying(),
        "PlayState::Finished must tear the show down without a manual "
        "/api/shows/stop (B1b)");
    TEST_ASSERT_TRUE(r.director.topMode(finishedAt) == TopMode::Practice);
    // The teardown must also lift the show-only refusals: setMode and the
    // probe are refused ONLY while showPlaying_.
    TEST_ASSERT_TRUE_MESSAGE(
        r.director.setMode("wait", "both", gOut),
        "mode PUT must no longer be refused once the show tore itself down");
    TEST_ASSERT_EQUAL_MESSAGE(
        static_cast<int>(ModeDirector::ProbeArm::Ok),
        static_cast<int>(r.director.armProbe(5, finishedAt, 30000)),
        "probe must no longer be refused once the show tore itself down");
}

void test_b1b_score_follow_show_immune_to_finished_teardown() {
    // The B1b Finished-teardown must only ever fire for demo/follow-clock
    // shows â€” score-follow's transport is deliberately kept out of Playing
    // (the performer is the only clock), so it never reaches
    // PlayState::Finished. Regression guard against a broader condition
    // that mistakes Idle for Finished.
    Rig r;
    r.director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.director.startShow(scoreFollowShow(2), 7, gOut);
    r.director.onKeyDown(60, 100, 1 * kSec);  // starts the follower's clock
    for (int i = 1; i <= 20; ++i) r.tick(1 * kSec + i * 500000ull);
    TEST_ASSERT_TRUE_MESSAGE(
        r.director.showPlaying(),
        "score-follow shows must never auto-teardown via the Finished path");
}

// --- B2: AFK next/previous STEER, never WAKE (ruling Â§6-4) -----------------

void test_b2_afk_next_previous_never_wake_afk() {
    Rig r;
    r.tick(1 * kSec);
    r.tick(200 * kSec);  // idle timeout elapses with no song
    TEST_ASSERT_TRUE(r.director.topMode(200 * kSec) == TopMode::Afk);
    r.director.afkNext();
    TEST_ASSERT_TRUE_MESSAGE(
        r.director.topMode(200 * kSec) == TopMode::Afk,
        "afkNext is ambient transport (STEERS) â€” it must not WAKE AFK");
    r.director.afkPrevious();
    TEST_ASSERT_TRUE_MESSAGE(
        r.director.topMode(200 * kSec) == TopMode::Afk,
        "afkPrevious is ambient transport (STEERS) â€” it must not WAKE AFK");
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
    RUN_TEST(test_w29_test_pattern_refused_or_inert_during_show);
    RUN_TEST(test_C22_test_pattern_freezes_scorefollow_clock);
    RUN_TEST(test_s3_setmode_during_show_does_not_freeze_show_clock);
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
    RUN_TEST(test_p2_cc64_during_wait_hold_is_noop_for_verdict);
    RUN_TEST(test_p3_presses_while_paused_or_finished_are_inert_for_practice_but_feed_capture);
    RUN_TEST(test_p6_afk_unreachable_while_paused_mid_practice);
    RUN_TEST(test_p7_test_pattern_autopause_preserves_barrier_hold_and_resumes);
    RUN_TEST(test_p8_double_arm_record_race_at_director_layer);
    RUN_TEST(test_p10_director_level_pedal_echo_excludes_song_cc64_echo);
    RUN_TEST(test_p15_playalong_take_survives_song_unload_mid_take);
    RUN_TEST(test_b1a_test_pattern_expires_after_timeout);
    RUN_TEST(test_b1a_test_pattern_timer_resets_on_reactivation);
    RUN_TEST(test_b1a_test_pattern_clears_on_fresh_song_load);
    RUN_TEST(test_b1a_test_pattern_clears_when_play_begins);
    RUN_TEST(test_b1a_test_pattern_clears_on_show_start);
    RUN_TEST(test_b1b_finished_demo_show_tears_itself_down);
    RUN_TEST(test_b1b_score_follow_show_immune_to_finished_teardown);
    RUN_TEST(test_b2_afk_next_previous_never_wake_afk);
    return UNITY_END();
}
