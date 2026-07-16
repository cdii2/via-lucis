// DECIDE-D / FIX-D pins — Christian's rulings A98 (G18 chord-gather epsilon,
// wait-gate ONLY) and A99 (G19 practiced-implies-lit). Assertions state the
// CORRECT (post-fix) behavior. Ported from the throwaway what-if audit repros
// test_C28a/b (wave3) and test_w8 (wave2), plus new pins that guard the
// boundaries: a gap >= epsilon stays sequential, a roll gates once per note,
// the timeline/demo is never altered, and the loop-clamp (A89/A90) survives.

#include <unity.h>

#include <string>
#include <vector>

#include "../helpers/smf_builder.h"
#include "../helpers/test_songs.h"
#include "vialucis/midi_io.h"
#include "vialucis/midi_parser.h"
#include "vialucis/playback_engine.h"
#include "vialucis/scheduler.h"
#include "vialucis/settings.h"
#include "vialucis/wait_mode.h"

using namespace vialucis;
using testsongs::chordSong;

void setUp() {}
void tearDown() {}

namespace {

std::vector<MidiOutMsg> gOut;

void setupEngine(PlaybackEngine& e, MidiSong song, const char* mode,
                 const char* practice = "both") {
    e.configure(Settings{});
    gOut.clear();
    e.loadSong(std::move(song), "test.mid", gOut);
    e.setMode(mode, practice, gOut);
}

bool hasNoteOn(const std::vector<MidiOutMsg>& v, uint8_t note) {
    for (const MidiOutMsg& m : v)
        if (m.type == MidiOutType::NoteOn && m.data1 == note) return true;
    return false;
}

// ---- fixtures -------------------------------------------------------------

// Single track, 67 @ tick 960 then 71 one tick later (+1). usPerQuarter 0 =>
// default 500000us/quarter (tpq 480 => ~1041us/tick, gap ~1041us). 240000 =>
// 500us/tick, gap 500us. Both gaps are below kChordEpsilonUs (10ms).
MidiSong tickAdjacentChord(uint32_t usPerQuarter) {
    smf::Bytes ev;
    if (usPerQuarter) smf::tempoMeta(ev, 0, usPerQuarter);
    smf::noteOn(ev, 960, 0, 67, 100);
    smf::noteOn(ev, 1, 0, 71, 100);  // +1 tick
    smf::noteOff(ev, 480, 0, 67);
    smf::noteOff(ev, 0, 0, 71);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

// Clean 1000us/tick timeline (tempoMeta 480000 @ 480 tpq). Note 60 @ tick 0,
// note 64 @ tick `gapTicks` (=> gap gapTicks*1000 us).
MidiSong twoNoteGap(uint32_t gapTicks) {
    smf::Bytes ev;
    smf::tempoMeta(ev, 0, 480000);
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOn(ev, gapTicks, 0, 64, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOff(ev, 0, 0, 64);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

// 1000us/tick. 60 @ tick0, 64 @ tick5 (absorbed with 60), 67 @ tick50 (a
// separate barrier 45ms later). Exercises arm-past-the-absorbed-group.
MidiSong absorbThenGap() {
    smf::Bytes ev;
    smf::tempoMeta(ev, 0, 480000);
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOn(ev, 5, 0, 64, 100);   // +5ms  -> absorbed
    smf::noteOn(ev, 45, 0, 67, 100);  // +45ms -> separate
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOff(ev, 0, 0, 64);
    smf::noteOff(ev, 0, 0, 67);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

// 1000us/tick roll: 60@0, 64@15ms, 67@30ms — every gap >= epsilon, so a
// deliberate roll must gate THREE times, one press at a time.
MidiSong rollSong() {
    smf::Bytes ev;
    smf::tempoMeta(ev, 0, 480000);
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOn(ev, 15, 0, 64, 100);
    smf::noteOn(ev, 15, 0, 67, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOff(ev, 0, 0, 64);
    smf::noteOff(ev, 0, 0, 67);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

}  // namespace

// ===========================================================================
// A98 / G18 — the gather epsilon (engine-level ported repros: the pins)
// ===========================================================================

// C28a — notes ~1 tick (~1041us) apart gate as ONE chord.
static void test_C28a_tick_adjacent_notes_gather_one_chord() {
    PlaybackEngine e;
    setupEngine(e, tickAdjacentChord(0), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(1100000, gOut);  // advance onto the first onset barrier
    std::string s = e.statusJson();
    TEST_ASSERT_TRUE_MESSAGE(
        s.find("\"pendingNotes\":[67,71]") != std::string::npos,
        "two near-simultaneous onsets must gate as one chord");
}

// C28b — same gather when the two onsets round to <1ms apart (500us/tick).
static void test_C28b_submillisecond_notes_gather_one_chord() {
    PlaybackEngine e;
    setupEngine(e, tickAdjacentChord(240000), "wait");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(600000, gOut);
    std::string s = e.statusJson();
    TEST_ASSERT_TRUE_MESSAGE(
        s.find("\"pendingNotes\":[67,71]") != std::string::npos,
        "sub-millisecond-apart onsets must still gate as one chord");
}

// ===========================================================================
// A98 boundary pins (unit-level WaitMode) — no over-absorb, arm-past-group
// ===========================================================================

// Gap well under epsilon (5ms) -> both notes absorbed into one barrier chord.
static void test_subepsilon_gap_absorbs_both() {
    MidiSong song = twoNoteGap(5);
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    TEST_ASSERT_TRUE(wm.update());
    TEST_ASSERT_EQUAL_size_t(2, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(60, wm.pendingNotes()[0]);
    TEST_ASSERT_EQUAL_UINT8(64, wm.pendingNotes()[1]);
}

// Gap == epsilon exactly (10ms). Half-open [t, t+eps) EXCLUDES the boundary,
// so the second note is a SEPARATE sequential barrier (epsilon is a strict
// lower bound for "these are two events").
static void test_gap_at_epsilon_stays_sequential() {
    MidiSong song = twoNoteGap(10);  // 10ms == kChordEpsilonUs
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(60, wm.pendingNotes()[0]);
}

// Gap over epsilon (20ms) -> two sequential gates; the second note is only
// due AFTER the first is cleared and time advances to it.
static void test_superepsilon_gap_stays_sequential() {
    MidiSong song = twoNoteGap(20);
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(60, wm.pendingNotes()[0]);
    wm.onKeyDown(60, 0);
    sched.advance(2000000);
    TEST_ASSERT_TRUE(wm.update());
    TEST_ASSERT_EQUAL_UINT64(20000, sched.positionUs());
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(64, wm.pendingNotes()[0]);
}

// THE softlock pin: after clearing a 2-note absorbed chord, the next barrier
// must arm STRICTLY PAST the last absorbed onset (tick5=5000us), landing on
// the separate note at 50000us — never re-gating the +1-tick phantom.
static void test_arm_past_absorbed_group_no_phantom_barrier() {
    MidiSong song = absorbThenGap();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    TEST_ASSERT_EQUAL_size_t(2, wm.pendingNotes().size());  // 60 + 64 absorbed
    wm.onKeyDown(60, 0);
    auto fb = wm.onKeyDown(64, 100);
    TEST_ASSERT_EQUAL(KeyVerdict::Cleared, fb.verdict);
    TEST_ASSERT_FALSE(wm.chordPending());
    sched.advance(2000000);
    TEST_ASSERT_TRUE(wm.update());
    TEST_ASSERT_EQUAL_UINT64(50000, sched.positionUs());  // jumped to 67, not 5001
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(67, wm.pendingNotes()[0]);
}

// A deliberate roll (each gap >= epsilon) gates ONCE PER NOTE — three barriers,
// taught one press at a time. Epsilon absorbs slop, never intent.
static void test_roll_gates_three_times() {
    MidiSong song = rollSong();
    Scheduler sched(song);
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(60, wm.pendingNotes()[0]);
    wm.onKeyDown(60, 0);
    sched.advance(2000000);
    wm.update();
    TEST_ASSERT_EQUAL_UINT64(15000, sched.positionUs());
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(64, wm.pendingNotes()[0]);
    wm.onKeyDown(64, 2000000);
    sched.advance(2000000);
    wm.update();
    TEST_ASSERT_EQUAL_UINT64(30000, sched.positionUs());
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(67, wm.pendingNotes()[0]);
}

// ===========================================================================
// A98 — the SONG TIMELINE is never altered (no load-time normalization)
// ===========================================================================

// The scheduler still reports the two roll onsets at their DISTINCT authored
// times — the epsilon lives in the gate only, the timeline is untouched.
static void test_timeline_onsets_stay_distinct() {
    MidiSong song = tickAdjacentChord(240000);  // 67@480000us, 71@480500us
    Scheduler sched(song);
    auto a = sched.notesOnAt(480000, kTrackMaskAll);
    TEST_ASSERT_EQUAL_size_t(1, a.size());
    TEST_ASSERT_EQUAL_UINT8(67, a[0].note);
    auto b = sched.notesOnAt(480500, kTrackMaskAll);
    TEST_ASSERT_EQUAL_size_t(1, b.size());
    TEST_ASSERT_EQUAL_UINT8(71, b[0].note);
}

// Demo mode plays the micro-offset roll EXACTLY as authored — both note-ons
// are emitted to the piano, the epsilon never merges them.
static void test_demo_plays_both_onsets_unmerged() {
    PlaybackEngine e;
    setupEngine(e, tickAdjacentChord(240000), "demo");
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(1000000, gOut);  // past 480500us
    TEST_ASSERT_TRUE_MESSAGE(hasNoteOn(gOut, 67),
                             "demo must play the first roll note");
    TEST_ASSERT_TRUE_MESSAGE(hasNoteOn(gOut, 71),
                             "demo must play the second roll note (unmerged)");
}

// ===========================================================================
// A99 / G19 — practiced-implies-lit (engine-level, ported w8 repro)
// ===========================================================================

// W8 -> A99: lights=false on the practiced track. Wait mode must STILL light
// the due chord — you can never owe an invisible note.
static void test_w8_barrier_lights_even_when_track_lights_off() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "wait");
    TEST_ASSERT_TRUE(e.setTrack(0, "both", false, gOut));  // lights OFF, hand kept
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(200000, gOut);
    std::string status = e.statusJson();
    TEST_ASSERT_TRUE_MESSAGE(
        status.find("\"state\":\"waiting\"") != std::string::npos,
        "engine should be waiting at the first barrier");
    const std::vector<Rgb>& frame = e.renderFrame(200000);
    bool anyLit = false;
    for (const Rgb& px : frame)
        if (px.r || px.g || px.b) anyLit = true;
    TEST_ASSERT_TRUE_MESSAGE(
        anyLit, "a barrier note is owed, so >=1 LED must be lit (A99)");
}

// A99 governs ONLY the gate: with lights off and NOT waiting (follow mode),
// the track stays dark — the flag still rules non-gating visuals.
static void test_lights_off_still_dark_in_follow() {
    PlaybackEngine e;
    setupEngine(e, chordSong(), "follow");
    TEST_ASSERT_TRUE(e.setTrack(0, "both", false, gOut));  // lights OFF
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);
    e.tick(100000, gOut);  // note 60 sounding, but no barrier in follow
    const std::vector<Rgb>& frame = e.renderFrame(100000);
    bool anyLit = false;
    for (const Rgb& px : frame)
        if (px.r || px.g || px.b) anyLit = true;
    TEST_ASSERT_FALSE_MESSAGE(
        anyLit, "follow-along glow must still obey the lights flag (A99 is gate-only)");
}

// ===========================================================================
// A98 interaction guard — the loop-clamp (A89/A90) still holds
// ===========================================================================

// A practiced onset far beyond loopEnd is unreachable: the gather window is
// capped at loopEnd, so the barrier clamps to loopEnd with NO absorbed chord
// (the dead-loop hold stays visible, A90).
static void test_epsilon_does_not_absorb_across_loop_end() {
    MidiSong song = twoNoteGap(40);  // 60@0us, 64@40000us
    Scheduler sched(song);
    sched.setLoop(0, 20000);  // loopEnd 20ms; the 64 onset (40ms) is outside
    WaitMode wm(sched, kTrackMaskAll);
    wm.begin();
    sched.advance(1);
    wm.update();
    // Barrier holds on 60 at tick0; 64 is beyond loopEnd and must NOT absorb.
    TEST_ASSERT_EQUAL_size_t(1, wm.pendingNotes().size());
    TEST_ASSERT_EQUAL_UINT8(60, wm.pendingNotes()[0]);
    wm.onKeyDown(60, 0);
    // After clearing, the next barrier clamps to loopEnd (dead-loop hold),
    // not to the unreachable 64 onset.
    sched.advance(2000000);
    wm.update();
    TEST_ASSERT_EQUAL_UINT64(20000, sched.positionUs());  // held at loopEnd
    TEST_ASSERT_FALSE(wm.chordPending());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_C28a_tick_adjacent_notes_gather_one_chord);
    RUN_TEST(test_C28b_submillisecond_notes_gather_one_chord);
    RUN_TEST(test_subepsilon_gap_absorbs_both);
    RUN_TEST(test_gap_at_epsilon_stays_sequential);
    RUN_TEST(test_superepsilon_gap_stays_sequential);
    RUN_TEST(test_arm_past_absorbed_group_no_phantom_barrier);
    RUN_TEST(test_roll_gates_three_times);
    RUN_TEST(test_timeline_onsets_stay_distinct);
    RUN_TEST(test_demo_plays_both_onsets_unmerged);
    RUN_TEST(test_w8_barrier_lights_even_when_track_lights_off);
    RUN_TEST(test_lights_off_still_dark_in_follow);
    RUN_TEST(test_epsilon_does_not_absorb_across_loop_end);
    return UNITY_END();
}
