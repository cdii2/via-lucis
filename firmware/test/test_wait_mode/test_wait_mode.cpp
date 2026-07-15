#include <unity.h>

#include "../helpers/smf_builder.h"
#include "../helpers/test_songs.h"
#include "vialucis/echo_guard.h"
#include "vialucis/midi_parser.h"
#include "vialucis/scheduler.h"
#include "vialucis/wait_mode.h"

using namespace vialucis;
using smf::Bytes;
using testsongs::chordSong;
using testsongs::twoTrackSong;

void setUp() {}
void tearDown() {}

static void test_first_chord_loads_at_start() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    TEST_ASSERT_TRUE(wm.chordPending());
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(60, wm.pendingNotes()[0]);
}

static void test_correct_press_clears_and_releases() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    auto fb = wm.onKeyDown(60, 0);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);
    TEST_ASSERT_FALSE(wm.chordPending());
    // Time now flows to the next chord and halts there.
    sched.advance(2000000);
    wm.update();
    TEST_ASSERT_EQUAL_UINT64(500000, sched.positionUs());
    TEST_ASSERT_TRUE(wm.chordPending());
    TEST_ASSERT_EQUAL_UINT8(64, wm.pendingNotes()[0]);
}

static void test_chord_clears_per_key() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    wm.onKeyDown(60, 0);
    sched.advance(2000000);
    wm.update();
    wm.onKeyDown(64, 600000);
    sched.advance(2000000);
    wm.update();
    // Now at the G4+B4 chord.
    TEST_ASSERT_EQUAL_size_t(2, wm.pendingNotes().size());
    auto fb = wm.onKeyDown(67, 1100000);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);
    TEST_ASSERT_TRUE(wm.chordPending());  // B4 still owed
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    fb = wm.onKeyDown(71, 1150000);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);
    TEST_ASSERT_FALSE(wm.chordPending());
}

static void test_wrong_press_reports_wrong_and_keeps_chord() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    auto fb = wm.onKeyDown(61, 0);
    TEST_ASSERT_EQUAL(KeyVerdict::Wrong, fb.verdict);
    TEST_ASSERT_EQUAL_UINT8(61, fb.note);
    TEST_ASSERT_TRUE(wm.chordPending());
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
}

static void test_recleared_member_is_ignored_not_wrong() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    sched.advance(1);  // stay at barrier
    wm.update();
    // At G4+B4 later this matters more, but test on chord: clear 60 twice.
    wm.onKeyDown(60, 0);
    // Chord complete → barrier released; a re-strike of 60 before the next
    // chord is noodling, not an error.
    auto fb = wm.onKeyDown(60, 1000);
    TEST_ASSERT_EQUAL(KeyVerdict::Ignored, fb.verdict);
}

static void test_early_press_between_chords_is_ignored() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    wm.onKeyDown(60, 0);
    sched.advance(100000);  // between chords, not yet at 500000 barrier
    wm.update();
    auto fb = wm.onKeyDown(64, 100000);
    TEST_ASSERT_EQUAL(KeyVerdict::Ignored, fb.verdict);
}

static void test_practiced_mask_limits_barriers_and_chords() {
    MidiSong song = twoTrackSong();
    Scheduler sched(song);
    WaitMode wm(sched, trackBit(0));  // practice track 0 only
    wm.begin();
    sched.advance(1);
    wm.update();
    TEST_ASSERT_TRUE(wm.chordPending());
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(60, wm.pendingNotes()[0]);  // track 1's 40 excluded
    wm.onKeyDown(60, 0);
    // No further practiced onsets: song must run to the end unhindered.
    sched.advance(10000000);
    wm.update();
    TEST_ASSERT_TRUE(sched.finished());
}

static void test_resync_after_seek() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    sched.seek(700000);
    wm.resync();
    sched.advance(2000000);
    wm.update();
    TEST_ASSERT_EQUAL_UINT64(1000000, sched.positionUs());  // held at chord
    TEST_ASSERT_EQUAL_size_t(2, wm.pendingNotes().size());
}

// --- echo guard ---------------------------------------------------------------

static void test_echo_guard_swallows_one_echo() {
    EchoGuard g;
    g.noteSent(60, 1000);
    TEST_ASSERT_TRUE(g.shouldIgnore(60, 5000));    // the echo
    TEST_ASSERT_FALSE(g.shouldIgnore(60, 6000));   // a real press right after
}

static void test_echo_guard_window_expires() {
    EchoGuard g;
    g.noteSent(60, 0);
    TEST_ASSERT_FALSE(g.shouldIgnore(60, g.windowUs() + 1));
}

static void test_echo_guard_counts_multiple_sends() {
    EchoGuard g;
    g.noteSent(60, 0);
    g.noteSent(60, 1000);
    TEST_ASSERT_TRUE(g.shouldIgnore(60, 2000));
    TEST_ASSERT_TRUE(g.shouldIgnore(60, 3000));
    TEST_ASSERT_FALSE(g.shouldIgnore(60, 4000));
}

static void test_echo_guard_is_per_note() {
    EchoGuard g;
    g.noteSent(60, 0);
    TEST_ASSERT_FALSE(g.shouldIgnore(61, 100));
    TEST_ASSERT_TRUE(g.shouldIgnore(60, 100));
}

static void test_wait_mode_ignores_echoes_via_guard() {
    MidiSong song = chordSong();
    Scheduler sched(song);
    EchoGuard guard;
    WaitMode wm(sched, kTrackMaskAll);
    wm.setEchoGuard(&guard);
    wm.begin();
    sched.advance(1);
    wm.update();
    guard.noteSent(60, 0);  // accompaniment happened to send C4
    auto fb = wm.onKeyDown(60, 10000);
    TEST_ASSERT_EQUAL(KeyVerdict::Echo, fb.verdict);
    TEST_ASSERT_TRUE(wm.chordPending());  // echo must NOT clear the chord
    fb = wm.onKeyDown(60, 20000);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);  // the real press
    TEST_ASSERT_FALSE(wm.chordPending());
}

// --- PIN-E coverage pack (audit §3, test-only pinning tests) ---------------

namespace {
// Two consecutive chords sharing a note across the barrier: chord1 {60,64}
// onset@0, chord2 {60,67} onset@480t (500ms) — note 60 re-dues.
vialucis::MidiSong heldKeySong() {
    smf::Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOn(ev, 0, 0, 64, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOff(ev, 0, 0, 64);
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOn(ev, 0, 0, 67, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOff(ev, 0, 0, 67);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return vialucis::parseMidi(file.data(), file.size()).song;
}
}  // namespace

// §3 item 1: held-key across two chord boundaries — WaitMode is onset-edge-
// only by design (wait_mode.h header contract). A note pressed to clear
// chord1 must NOT silently carry over and clear chord2's re-due of the SAME
// pitch; only a fresh onKeyDown for the new chord counts.
static void test_p1_held_key_across_two_boundaries_not_double_counted() {
    MidiSong song = heldKeySong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    TEST_ASSERT_EQUAL_size_t(2, wm.pendingNotes().size());  // chord1 {60,64}
    wm.onKeyDown(60, 0);
    auto fb = wm.onKeyDown(64, 1000);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);
    TEST_ASSERT_FALSE(wm.chordPending());  // chord1 fully cleared

    sched.advance(2000000);
    wm.update();
    TEST_ASSERT_EQUAL_UINT64(500000, sched.positionUs());
    TEST_ASSERT_TRUE(wm.chordPending());
    TEST_ASSERT_TRUE(wm.isPending(60));
    TEST_ASSERT_TRUE(wm.isPending(67));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        2, wm.pendingNotes().size(),
        "chord1's press of 60 must not carry over into chord2's re-due 60");

    fb = wm.onKeyDown(67, 2100);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);
    TEST_ASSERT_TRUE_MESSAGE(wm.chordPending(),
                             "60 still owed — a fresh press is required");
    fb = wm.onKeyDown(60, 2200);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);
    TEST_ASSERT_FALSE(wm.chordPending());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_chord_loads_at_start);
    RUN_TEST(test_correct_press_clears_and_releases);
    RUN_TEST(test_chord_clears_per_key);
    RUN_TEST(test_wrong_press_reports_wrong_and_keeps_chord);
    RUN_TEST(test_recleared_member_is_ignored_not_wrong);
    RUN_TEST(test_early_press_between_chords_is_ignored);
    RUN_TEST(test_practiced_mask_limits_barriers_and_chords);
    RUN_TEST(test_resync_after_seek);
    RUN_TEST(test_echo_guard_swallows_one_echo);
    RUN_TEST(test_echo_guard_window_expires);
    RUN_TEST(test_echo_guard_counts_multiple_sends);
    RUN_TEST(test_echo_guard_is_per_note);
    RUN_TEST(test_wait_mode_ignores_echoes_via_guard);
    RUN_TEST(test_p1_held_key_across_two_boundaries_not_double_counted);
    return UNITY_END();
}
