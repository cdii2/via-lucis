// P4 — ScoreFollower: the score-follow Presentation clock (DESIGN-lightshow
// §4a, A53). A pure follower fed a scripted (note, realTimeUs) stream; the
// tests assert the emitted clock TRAJECTORY directly: clean follow, rubato
// tempo tracking, wrong-note-ignored, bounded skip-ahead, backward re-arm,
// rest coast-then-hold, lost→free-run→re-acquire, first-anchor start.

#include <unity.h>

#include <cstdint>
#include <vector>

#include "../helpers/test_songs.h"
#include "vialucis/echo_guard.h"
#include "vialucis/scheduler.h"
#include "vialucis/score_follower.h"

using namespace vialucis;
using testsongs::chordSong;
using testsongs::twoTrackSong;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint64_t kMs = 1000ull;
constexpr uint64_t kSec = 1000000ull;

FollowAnchor anchor(uint64_t us, std::initializer_list<uint8_t> notes) {
    FollowAnchor a;
    a.timeUs = us;
    a.notes.assign(notes);
    return a;
}

// Distinct-note ladder: `count` single-note anchors every `gapUs` starting
// at `startUs`, notes base, base+1, ... (no repeated pitches — tests that
// need pitch ambiguity build their own tables).
std::vector<FollowAnchor> ladder(size_t count, uint64_t startUs,
                                 uint64_t gapUs, uint8_t base) {
    std::vector<FollowAnchor> v;
    for (size_t i = 0; i < count; ++i)
        v.push_back(anchor(startUs + gapUs * i,
                           {static_cast<uint8_t>(base + i)}));
    return v;
}

ScoreFollower armed(std::vector<FollowAnchor> anchors) {
    ScoreFollower f;
    f.arm(std::move(anchors));
    return f;
}

}  // namespace

// --- first-anchor start (§4a Q13) -------------------------------------------

void test_preroll_holds_position_zero_until_first_match() {
    ScoreFollower f = armed(ladder(4, 500 * kMs, 500 * kMs, 60));
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::AwaitingFirst);
    TEST_ASSERT_EQUAL_UINT64(0, f.positionUs(1 * kSec));
    TEST_ASSERT_EQUAL_UINT64(0, f.positionUs(100 * kSec));
    // Noodling before the piece starts does not start the clock.
    f.onNote(100, 5 * kSec);
    TEST_ASSERT_EQUAL_UINT64(0, f.positionUs(6 * kSec));
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::AwaitingFirst);
}

void test_first_matched_anchor_starts_the_clock() {
    ScoreFollower f = armed(ladder(4, 500 * kMs, 500 * kMs, 60));
    f.onNote(60, 10 * kSec);
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::Following);
    TEST_ASSERT_EQUAL_UINT64(500 * kMs, f.positionUs(10 * kSec));
    // Extrapolates forward at the score-seeded tempo (1.0)...
    TEST_ASSERT_EQUAL_UINT64(700 * kMs, f.positionUs(10 * kSec + 200 * kMs));
    // ...and HOLDS at the next scored onset until it is played.
    TEST_ASSERT_EQUAL_UINT64(1000 * kMs, f.positionUs(10 * kSec + 600 * kMs));
    TEST_ASSERT_EQUAL_UINT64(1000 * kMs, f.positionUs(20 * kSec));
}

// --- clean follow ------------------------------------------------------------

void test_clean_follow_snaps_each_anchor_and_extrapolates() {
    ScoreFollower f = armed(ladder(4, 500 * kMs, 500 * kMs, 60));
    f.onNote(60, 10 * kSec);
    f.onNote(61, 10 * kSec + 500 * kMs);  // exactly on time: measured 1.0
    TEST_ASSERT_EQUAL_UINT64(1000 * kMs, f.positionUs(10 * kSec + 500 * kMs));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, f.tempoFactor());
    f.onNote(62, 11 * kSec);
    TEST_ASSERT_EQUAL_UINT64(1500 * kMs, f.positionUs(11 * kSec));
    // Mid-gap extrapolation at tempo 1.0.
    TEST_ASSERT_EQUAL_UINT64(1750 * kMs, f.positionUs(11 * kSec + 250 * kMs));
    // Past the FINAL anchor the tail plays out (no next onset to hold at).
    f.onNote(63, 11 * kSec + 500 * kMs);
    TEST_ASSERT_EQUAL_UINT64(2000 * kMs, f.positionUs(11 * kSec + 500 * kMs));
    TEST_ASSERT_EQUAL_UINT64(3000 * kMs, f.positionUs(12 * kSec + 500 * kMs));
}

// --- rubato tempo tracking (§4a Q5) ------------------------------------------

void test_slow_performer_drags_the_hybrid_tempo_down() {
    // Scored gap 500ms, played gap 1000ms ⇒ measured 0.5 at every snap.
    ScoreFollower f = armed(ladder(8, 500 * kMs, 500 * kMs, 60));
    uint64_t t = 10 * kSec;
    f.onNote(60, t);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, f.tempoFactor());  // seeded
    float prev = f.tempoFactor();
    for (int i = 1; i < 6; ++i) {
        t += 1000 * kMs;
        f.onNote(static_cast<uint8_t>(60 + i), t);
        TEST_ASSERT_TRUE_MESSAGE(f.tempoFactor() < prev,
                                 "tempo blends toward the measured 0.5");
        TEST_ASSERT_TRUE(f.tempoFactor() >= 0.5f);
        prev = f.tempoFactor();
    }
    TEST_ASSERT_TRUE_MESSAGE(f.tempoFactor() < 0.7f,
                             "after 5 measured intervals the blend is close");
    // The coast rate between anchors reflects the learned tempo.
    uint64_t p0 = f.positionUs(t + 100 * kMs);
    uint64_t p1 = f.positionUs(t + 200 * kMs);
    uint64_t rate = p1 - p0;  // song-us per 100ms real
    TEST_ASSERT_TRUE(rate < 75 * kMs && rate > 45 * kMs);
}

void test_fast_performer_clamps_at_the_tempo_band() {
    // Played absurdly fast: measured clamps at kTempoMax before blending.
    ScoreFollower f = armed(ladder(8, 500 * kMs, 500 * kMs, 60));
    uint64_t t = 10 * kSec;
    f.onNote(60, t);
    for (int i = 1; i < 8; ++i) {
        t += 10 * kMs;  // 500ms of score in 10ms of real time
        f.onNote(static_cast<uint8_t>(60 + i), t);
        TEST_ASSERT_TRUE(f.tempoFactor() <= ScoreFollower::kTempoMax);
    }
    TEST_ASSERT_TRUE_MESSAGE(f.tempoFactor() > 2.0f,
                             "tempo climbed toward the clamp");
}

// --- performance mode: wrong notes ignored (§4a Q8) --------------------------

void test_wrong_notes_never_move_or_stall_the_clock() {
    ScoreFollower f = armed(ladder(6, 500 * kMs, 500 * kMs, 60));
    f.onNote(60, 10 * kSec);
    f.onNote(61, 10 * kSec + 500 * kMs);
    uint64_t query = 11 * kSec + 100 * kMs;
    uint64_t before = f.positionUs(query);
    // Two fumbles (below the lost threshold): the clock must not lurch,
    // rewind, or stall — the trajectory is untouched.
    f.onNote(100, 10 * kSec + 700 * kMs);
    f.onNote(101, 10 * kSec + 800 * kMs);
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::Following);
    TEST_ASSERT_EQUAL_UINT64(before, f.positionUs(query));
    // The next correct note still snaps normally.
    f.onNote(62, 11 * kSec);
    TEST_ASSERT_EQUAL_UINT64(1500 * kMs, f.positionUs(11 * kSec));
}

// --- performer ahead: bounded look-ahead (§4a Q9) ----------------------------

void test_skip_ahead_snaps_to_the_furthest_confident_match() {
    ScoreFollower f = armed(ladder(6, 500 * kMs, 500 * kMs, 60));
    f.onNote(60, 10 * kSec);
    // The performer skips two anchors and plays anchor 3's note (inside the
    // look-ahead depth and window): the clock snaps forward to it.
    f.onNote(63, 10 * kSec + 200 * kMs);
    TEST_ASSERT_EQUAL_UINT64(2000 * kMs,
                             f.positionUs(10 * kSec + 200 * kMs));
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::Following);
}

void test_faraway_pitch_never_teleports_the_clock() {
    // Anchor 2 sits beyond the look-ahead song-time window; its pitch is a
    // coincidence the follower must not chase (§4a Q9), even struck thrice
    // (a single far anchor is below the re-acquire evidence bar).
    std::vector<FollowAnchor> v;
    v.push_back(anchor(500 * kMs, {60}));
    v.push_back(anchor(1000 * kMs, {61}));
    v.push_back(anchor(20 * kSec, {70}));
    ScoreFollower f = armed(std::move(v));
    f.onNote(60, 10 * kSec);
    f.onNote(70, 10 * kSec + 300 * kMs);
    f.onNote(70, 10 * kSec + 600 * kMs);
    f.onNote(70, 10 * kSec + 900 * kMs);
    TEST_ASSERT_TRUE(f.positionUs(10 * kSec + 950 * kMs) <= 1000 * kMs);
}

// --- performer goes back: forward-only re-arm (§4a Q10) ----------------------

void test_backward_run_rearms_at_the_new_position() {
    ScoreFollower f = armed(ladder(8, 500 * kMs, 500 * kMs, 60));
    uint64_t t = 10 * kSec;
    for (int i = 0; i < 6; ++i)
        f.onNote(static_cast<uint8_t>(60 + i), t + i * 500 * kMs);
    TEST_ASSERT_EQUAL_UINT64(3000 * kMs,
                             f.positionUs(t + 2500 * kMs));  // at anchor 5
    // A large backward run: the performer restarts at the top. After a
    // confident consecutive walk (3 anchors) the clock RE-ARMS there — a
    // backward seek, handled by the effect reset downstream.
    f.onNote(60, t + 3000 * kMs);
    f.onNote(61, t + 3200 * kMs);
    TEST_ASSERT_TRUE_MESSAGE(
        f.positionUs(t + 3300 * kMs) >= 3000 * kMs,
        "two notes are not yet confident — no lurch backward");
    f.onNote(62, t + 3400 * kMs);
    TEST_ASSERT_EQUAL_UINT64(1500 * kMs, f.positionUs(t + 3400 * kMs));
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::Following);
    // Forward-only from the new position: the next anchor advances normally.
    f.onNote(63, t + 3900 * kMs);
    TEST_ASSERT_EQUAL_UINT64(2000 * kMs, f.positionUs(t + 3900 * kMs));
}

// --- rests / fermatas: coast then hold (§4a Q11) ------------------------------

void test_rest_coasts_at_last_tempo_then_holds() {
    std::vector<FollowAnchor> v;
    v.push_back(anchor(500 * kMs, {60}));
    v.push_back(anchor(1000 * kMs, {61}));
    v.push_back(anchor(4000 * kMs, {62}));  // a 3s scored rest before this
    ScoreFollower f = armed(std::move(v));
    f.onNote(60, 10 * kSec);
    f.onNote(61, 10 * kSec + 500 * kMs);  // tempo 1.0 measured
    uint64_t r = 10 * kSec + 500 * kMs;
    // Coasting through the rest at the last tempo...
    TEST_ASSERT_EQUAL_UINT64(2000 * kMs, f.positionUs(r + 1000 * kMs));
    TEST_ASSERT_EQUAL_UINT64(3500 * kMs, f.positionUs(r + 2500 * kMs));
    // ...up to the next scored onset, then HOLD — never past unplayed music.
    TEST_ASSERT_EQUAL_UINT64(4000 * kMs, f.positionUs(r + 3000 * kMs));
    TEST_ASSERT_EQUAL_UINT64(4000 * kMs, f.positionUs(r + 30 * kSec));
}

// --- lost → free-run → auto-re-acquire (§4a Q12, the safety net) -------------

void test_lost_falls_back_to_freerun_and_reacquires_nearby() {
    ScoreFollower f = armed(ladder(10, 500 * kMs, 500 * kMs, 60));
    f.onNote(60, 10 * kSec);
    f.onNote(61, 10 * kSec + 500 * kMs);
    // Enough junk to lose confidence (none of it in any anchor).
    uint64_t t = 10 * kSec + 600 * kMs;
    for (uint8_t junk = 100; junk < 100 + ScoreFollower::kLostMissThreshold;
         ++junk)
        f.onNote(junk, t += 50 * kMs);
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::FreeRun);
    // Free-run: the clock does NOT hold at the unplayed anchor — the show
    // keeps breathing past it at the last tempo.
    TEST_ASSERT_TRUE(f.positionUs(t + 2000 * kMs) > 1500 * kMs);
    // The performer resurfaces inside the look-ahead window: one confident
    // match snaps the clock back onto them, silently.
    f.onNote(65, t + 2500 * kMs);
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::Following);
    TEST_ASSERT_EQUAL_UINT64(3000 * kMs, f.positionUs(t + 2500 * kMs));
}

void test_reacquire_beyond_the_window_needs_a_confident_run() {
    ScoreFollower f = armed(ladder(12, 500 * kMs, 500 * kMs, 60));
    f.onNote(60, 10 * kSec);
    f.onNote(61, 10 * kSec + 500 * kMs);
    // The performer jumps FAR ahead (beyond the look-ahead depth): singles
    // are ignored; a consecutive confident run of 3 re-acquires there.
    uint64_t t = 11 * kSec;
    f.onNote(68, t);
    f.onNote(69, t + 200 * kMs);
    TEST_ASSERT_TRUE(f.positionUs(t + 300 * kMs) <= 1500 * kMs);
    f.onNote(70, t + 400 * kMs);
    TEST_ASSERT_EQUAL_UINT64(5500 * kMs, f.positionUs(t + 400 * kMs));
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::Following);
}

// --- chords: "enough" matches, inner voices tolerated (§4a Q7) ---------------

void test_chord_advances_on_core_notes_missing_inner_voice_ok() {
    std::vector<FollowAnchor> v;
    v.push_back(anchor(500 * kMs, {60, 64, 67, 70}));  // needs 3 of 4
    v.push_back(anchor(1000 * kMs, {62}));
    ScoreFollower f = armed(std::move(v));
    f.onNote(60, 10 * kSec);
    f.onNote(64, 10 * kSec + 20 * kMs);
    TEST_ASSERT_EQUAL_UINT64(0, f.positionUs(10 * kSec + 30 * kMs));
    f.onNote(67, 10 * kSec + 40 * kMs);  // 3rd core note: enough
    TEST_ASSERT_EQUAL_UINT64(500 * kMs, f.positionUs(10 * kSec + 40 * kMs));
}

void test_single_stray_note_never_advances_a_chord() {
    std::vector<FollowAnchor> v;
    v.push_back(anchor(500 * kMs, {60, 64, 67}));  // needs 2 of 3
    v.push_back(anchor(1000 * kMs, {62}));
    ScoreFollower f = armed(std::move(v));
    f.onNote(60, 10 * kSec);  // one note of a chord: not confident
    TEST_ASSERT_EQUAL_UINT64(0, f.positionUs(10 * kSec + 100 * kMs));
    // An interleaved wrong note is tolerated (extra voices — §4a Q7).
    f.onNote(90, 10 * kSec + 50 * kMs);
    f.onNote(64, 10 * kSec + 80 * kMs);  // 2nd core note completes it
    TEST_ASSERT_EQUAL_UINT64(500 * kMs, f.positionUs(10 * kSec + 80 * kMs));
}

void test_stale_partial_chord_restarts_with_the_new_note() {
    std::vector<FollowAnchor> v;
    v.push_back(anchor(500 * kMs, {60, 64}));  // needs both
    v.push_back(anchor(1000 * kMs, {62}));
    ScoreFollower f = armed(std::move(v));
    f.onNote(60, 10 * kSec);
    // The second note lands OUTSIDE the gather window: the pair is not one
    // chord — the partial restarts with the new note.
    uint64_t late = 10 * kSec + ScoreFollower::kChordGatherUs + 100 * kMs;
    f.onNote(64, late);
    TEST_ASSERT_EQUAL_UINT64(0, f.positionUs(late + 10 * kMs));
    f.onNote(60, late + 50 * kMs);  // completes within the fresh window
    TEST_ASSERT_EQUAL_UINT64(500 * kMs, f.positionUs(late + 50 * kMs));
}

void test_repeated_note_advances_one_anchor_per_press() {
    // Repeated melody note: one press must advance ONE anchor, never eat
    // several in a single strike.
    std::vector<FollowAnchor> v;
    v.push_back(anchor(500 * kMs, {60}));
    v.push_back(anchor(1000 * kMs, {60}));
    v.push_back(anchor(1500 * kMs, {60}));
    ScoreFollower f = armed(std::move(v));
    f.onNote(60, 10 * kSec);
    TEST_ASSERT_EQUAL_UINT64(500 * kMs, f.positionUs(10 * kSec));
    f.onNote(60, 10 * kSec + 500 * kMs);
    TEST_ASSERT_EQUAL_UINT64(1000 * kMs,
                             f.positionUs(10 * kSec + 500 * kMs));
    f.onNote(60, 10 * kSec + 1000 * kMs);
    TEST_ASSERT_EQUAL_UINT64(1500 * kMs,
                             f.positionUs(10 * kSec + 1000 * kMs));
}

// --- echo guard (iron rule) ---------------------------------------------------

void test_echoed_notes_are_never_the_performer() {
    ScoreFollower f = armed(ladder(4, 500 * kMs, 500 * kMs, 60));
    EchoGuard guard;
    f.setEchoGuard(&guard);
    guard.noteSent(60, 10 * kSec);  // we transmitted this note
    f.onNote(60, 10 * kSec + 20 * kMs);  // its echo bounces back
    TEST_ASSERT_TRUE_MESSAGE(
        f.state() == ScoreFollower::FollowState::AwaitingFirst,
        "an echo of our own note must not start the clock");
    f.onNote(60, 10 * kSec + 100 * kMs);  // the real press
    TEST_ASSERT_TRUE(f.state() == ScoreFollower::FollowState::Following);
}

// --- anchor extraction: the barrier cadence over the follow mask -------------

void test_extract_anchors_follows_the_track_mask() {
    MidiSong song = chordSong();  // 60@0 · 64@500ms · {67,71}@1000ms
    Scheduler sched(song);
    std::vector<FollowAnchor> a;
    ScoreFollower::extractAnchors(sched, kTrackMaskAll, a);
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(a.size()));
    TEST_ASSERT_EQUAL_UINT64(0, a[0].timeUs);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(a[0].notes.size()));
    TEST_ASSERT_EQUAL_UINT8(60, a[0].notes[0]);
    TEST_ASSERT_EQUAL_UINT64(500 * kMs, a[1].timeUs);
    TEST_ASSERT_EQUAL_UINT64(1000 * kMs, a[2].timeUs);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(a[2].notes.size()));

    MidiSong two = twoTrackSong();  // t0: 60@0 · t1: 40@0
    Scheduler sched2(two);
    ScoreFollower::extractAnchors(sched2, trackBit(0), a);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(a.size()));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(a[0].notes.size()));
    TEST_ASSERT_EQUAL_UINT8(60, a[0].notes[0]);
    ScoreFollower::extractAnchors(sched2, kTrackMaskAll, a);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(a.size()));
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(a[0].notes.size()));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_preroll_holds_position_zero_until_first_match);
    RUN_TEST(test_first_matched_anchor_starts_the_clock);
    RUN_TEST(test_clean_follow_snaps_each_anchor_and_extrapolates);
    RUN_TEST(test_slow_performer_drags_the_hybrid_tempo_down);
    RUN_TEST(test_fast_performer_clamps_at_the_tempo_band);
    RUN_TEST(test_wrong_notes_never_move_or_stall_the_clock);
    RUN_TEST(test_skip_ahead_snaps_to_the_furthest_confident_match);
    RUN_TEST(test_faraway_pitch_never_teleports_the_clock);
    RUN_TEST(test_backward_run_rearms_at_the_new_position);
    RUN_TEST(test_rest_coasts_at_last_tempo_then_holds);
    RUN_TEST(test_lost_falls_back_to_freerun_and_reacquires_nearby);
    RUN_TEST(test_reacquire_beyond_the_window_needs_a_confident_run);
    RUN_TEST(test_chord_advances_on_core_notes_missing_inner_voice_ok);
    RUN_TEST(test_single_stray_note_never_advances_a_chord);
    RUN_TEST(test_stale_partial_chord_restarts_with_the_new_note);
    RUN_TEST(test_repeated_note_advances_one_anchor_per_press);
    RUN_TEST(test_echoed_notes_are_never_the_performer);
    RUN_TEST(test_extract_anchors_follows_the_track_mask);
    UNITY_END();
    return 0;
}
