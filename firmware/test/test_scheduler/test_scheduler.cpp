#include <unity.h>

#include "../helpers/smf_builder.h"
#include "../helpers/test_songs.h"
#include "vialucis/midi_parser.h"
#include "vialucis/scheduler.h"

using namespace vialucis;
using smf::Bytes;
using testsongs::chordSong;
using testsongs::twoTrackSong;

void setUp() {}
void tearDown() {}

static int countType(const std::vector<SchedEvent>& evs, SchedEventType t) {
    int n = 0;
    for (const auto& e : evs)
        if (e.type == t) ++n;
    return n;
}

static void test_events_arrive_in_time_windows() {
    MidiSong song = chordSong();
    Scheduler s(song);
    // First advance of 100ms: only the tick-0 note-on (C4).
    auto evs = s.advance(100000);
    TEST_ASSERT_EQUAL_INT(1, countType(evs, SchedEventType::NoteOn));
    TEST_ASSERT_EQUAL_UINT8(60, evs[0].note);
    // Up to 510ms total: C4 off + E4 on (both at 500000us).
    evs = s.advance(410000);
    TEST_ASSERT_EQUAL_INT(1, countType(evs, SchedEventType::NoteOff));
    TEST_ASSERT_EQUAL_INT(1, countType(evs, SchedEventType::NoteOn));
}

static void test_off_sorts_before_on_at_same_instant() {
    MidiSong song = chordSong();
    Scheduler s(song);
    auto evs = s.advance(510000);  // crosses t=500000 (C4 off + E4 on)
    bool sawOff = false;
    for (const auto& e : evs) {
        if (e.type == SchedEventType::NoteOff && e.note == 60) sawOff = true;
        if (e.type == SchedEventType::NoteOn && e.note == 64)
            TEST_ASSERT_TRUE_MESSAGE(sawOff, "off must precede on at same time");
    }
    TEST_ASSERT_TRUE(sawOff);
}

static void test_tempo_scales_time() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.setTempoPercent(200.0f);  // double speed: E4 (song t=500000) at real 250000
    auto evs = s.advance(260000);
    bool sawE4 = false;
    for (const auto& e : evs)
        if (e.type == SchedEventType::NoteOn && e.note == 64) sawE4 = true;
    TEST_ASSERT_TRUE(sawE4);
    TEST_ASSERT_EQUAL_UINT64(520000, s.positionUs());
}

static void test_tempo_clamps_to_1_500() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.setTempoPercent(0.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.tempoPercent());
    s.setTempoPercent(9999.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 500.0f, s.tempoPercent());
}

static void test_loop_wraps_and_replays() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.setLoop(0, 600000);  // loop first 0.6s: C4 on/off + E4 on live inside
    s.advance(590000);     // near loop end
    auto evs = s.advance(20000);  // cross 600000 → wrap to 0
    // E4 was sounding at the wrap → must get a note-off.
    TEST_ASSERT_TRUE(countType(evs, SchedEventType::NoteOff) >= 1);
    bool e4Off = false, c4OnAgain = false;
    for (const auto& e : evs) {
        if (e.type == SchedEventType::NoteOff && e.note == 64) e4Off = true;
        if (e.type == SchedEventType::NoteOn && e.note == 60) c4OnAgain = true;
    }
    TEST_ASSERT_TRUE(e4Off);
    TEST_ASSERT_TRUE(c4OnAgain);  // tick-0 event replays after wrap
    TEST_ASSERT_TRUE(s.positionUs() < 600000);
}

static void test_barrier_halts_exactly() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.setBarrier(500000);  // E4's onset
    auto evs = s.advance(2000000);
    TEST_ASSERT_EQUAL_UINT64(500000, s.positionUs());
    TEST_ASSERT_TRUE(s.atBarrier());
    // Events AT the barrier instant must not fire while held.
    for (const auto& e : evs)
        TEST_ASSERT_FALSE(e.type == SchedEventType::NoteOn && e.note == 64);
    // Held: no further progress.
    evs = s.advance(1000000);
    TEST_ASSERT_EQUAL_size_t(0, evs.size());
    TEST_ASSERT_EQUAL_UINT64(500000, s.positionUs());
    // Released: the barrier-time note fires.
    s.clearBarrier();
    evs = s.advance(1000);
    bool sawE4 = false;
    for (const auto& e : evs)
        if (e.type == SchedEventType::NoteOn && e.note == 64) sawE4 = true;
    TEST_ASSERT_TRUE(sawE4);
}

static void test_seek_repositions_and_flushes_sounding_notes() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.advance(100000);  // C4 sounding
    auto offs = s.seek(1200000);
    TEST_ASSERT_EQUAL_INT(1, countType(offs, SchedEventType::NoteOff));
    TEST_ASSERT_EQUAL_UINT8(60, offs[0].note);
    TEST_ASSERT_EQUAL_UINT64(1200000, s.positionUs());
    // Next events: chord G4+B4 at song t=1000000? No — that's before 1200000.
    // Chord onset = tick 960 = 1000000us < seek point, so only offs remain.
    auto evs = s.advance(600000);
    TEST_ASSERT_EQUAL_INT(0, countType(evs, SchedEventType::NoteOn));
    TEST_ASSERT_EQUAL_INT(2, countType(evs, SchedEventType::NoteOff));
}

static void test_finished_after_last_event() {
    MidiSong song = chordSong();
    Scheduler s(song);
    TEST_ASSERT_FALSE(s.finished());
    s.advance(10000000);
    TEST_ASSERT_TRUE(s.finished());
    TEST_ASSERT_EQUAL_UINT64(s.durationUs(), s.positionUs());
}

static void test_events_carry_track_and_mask_filters() {
    MidiSong song = twoTrackSong();
    Scheduler s(song);
    auto evs = s.advance(100000);
    TEST_ASSERT_EQUAL_INT(2, countType(evs, SchedEventType::NoteOn));
    int kept = 0;
    for (const auto& e : evs)
        if (e.type == SchedEventType::NoteOn && trackInMask(kTrackMaskAll, e.track)) ++kept;
    TEST_ASSERT_EQUAL_INT(2, kept);
    uint32_t onlyTrack1 = trackBit(1);
    for (const auto& e : evs) {
        if (e.type != SchedEventType::NoteOn) continue;
        if (trackInMask(onlyTrack1, e.track))
            TEST_ASSERT_EQUAL_UINT8(40, e.note);
    }
}

static void test_next_onset_query() {
    MidiSong song = chordSong();
    Scheduler s(song);
    TEST_ASSERT_EQUAL_UINT64(0, s.nextOnsetAfter(0, kTrackMaskAll));
    TEST_ASSERT_EQUAL_UINT64(500000, s.nextOnsetAfter(1, kTrackMaskAll));
    TEST_ASSERT_EQUAL_UINT64(1000000, s.nextOnsetAfter(500001, kTrackMaskAll));
    TEST_ASSERT_EQUAL_UINT64(kNoOnset, s.nextOnsetAfter(1000001, kTrackMaskAll));
}

static void test_onsets_between_window() {
    MidiSong song = chordSong();
    Scheduler s(song);
    // Window covering E4 (500000) and the G4+B4 chord (1000000).
    auto evs = s.onsetsBetween(400000, 1000000, kTrackMaskAll);
    TEST_ASSERT_EQUAL_size_t(3, evs.size());
    // Window ends just before the chord.
    evs = s.onsetsBetween(400000, 999999, kTrackMaskAll);
    TEST_ASSERT_EQUAL_size_t(1, evs.size());
    TEST_ASSERT_EQUAL_UINT8(64, evs[0].note);
    // Empty window.
    evs = s.onsetsBetween(1000001, 2000000, kTrackMaskAll);
    TEST_ASSERT_EQUAL_size_t(0, evs.size());
}

static void test_next_onset_respects_track_mask() {
    MidiSong song = twoTrackSong();
    Scheduler s(song);
    TEST_ASSERT_EQUAL_UINT64(0, s.nextOnsetAfter(0, trackBit(1)));
    TEST_ASSERT_EQUAL_UINT64(kNoOnset, s.nextOnsetAfter(1, trackBit(1)));
}

// --- R5: caller-owned-buffer variants (zero steady-state alloc) ---------

static void test_out_param_advance_matches_by_value_and_clears_buffer() {
    MidiSong song = chordSong();
    Scheduler byValue(song), byBuffer(song);
    std::vector<SchedEvent> buf;
    buf.push_back({SchedEventType::Pedal, 9, 9, 9, 9, 9});  // stale garbage

    for (uint64_t step : {100000ull, 410000ull, 600000ull}) {
        auto expect = byValue.advance(step);
        byBuffer.advance(step, buf);  // must clear the stale content first
        TEST_ASSERT_EQUAL_size_t(expect.size(), buf.size());
        for (size_t i = 0; i < expect.size(); ++i) {
            TEST_ASSERT_EQUAL_UINT8(expect[i].note, buf[i].note);
            TEST_ASSERT_EQUAL(expect[i].type, buf[i].type);
            TEST_ASSERT_EQUAL_UINT64(expect[i].timeUs, buf[i].timeUs);
        }
    }
    TEST_ASSERT_EQUAL_UINT64(byValue.positionUs(), byBuffer.positionUs());
}

static void test_out_param_queries_clear_then_fill() {
    MidiSong song = chordSong();
    Scheduler s(song);
    std::vector<SchedEvent> buf;

    s.onsetsBetween(400000, 1000000, kTrackMaskAll, buf);
    TEST_ASSERT_EQUAL_size_t(3, buf.size());
    s.onsetsBetween(1000001, 2000000, kTrackMaskAll, buf);
    TEST_ASSERT_EQUAL_size_t(0, buf.size());  // cleared, not appended

    s.notesOnAt(1000000, kTrackMaskAll, buf);
    TEST_ASSERT_EQUAL_size_t(2, buf.size());  // the G4+B4 chord
    s.notesOnAt(123, kTrackMaskAll, buf);
    TEST_ASSERT_EQUAL_size_t(0, buf.size());
}

static void test_out_param_seek_flushes_into_buffer() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.advance(100000);  // C4 sounding
    std::vector<SchedEvent> buf;
    s.seek(700000, buf);
    TEST_ASSERT_EQUAL_size_t(1, buf.size());  // C4's flush note-off
    TEST_ASSERT_EQUAL(SchedEventType::NoteOff, buf[0].type);
    TEST_ASSERT_EQUAL_UINT8(60, buf[0].note);
    TEST_ASSERT_EQUAL_UINT64(700000, s.positionUs());
}

// --- PIN-E coverage pack (audit §3, test-only pinning tests) ---------------

// §3 item 4: song end while holding at the final barrier. A song whose only
// note is zero-length (onTick == offTick == 0) puts the final practiced
// onset exactly at durationUs — the exact "final-onset==duration" case the
// audit flagged as untested. Held at that barrier, the scheduler must stay
// "waiting" (not Finished) even though positionUs already equals durationUs;
// only clearing the barrier lets it actually finish (scheduler.cpp:45-47 +
// emitUpTo's barrier semantics).
static void test_p4_finished_stays_waiting_at_final_barrier_when_onset_equals_duration() {
    MidiSong song;
    song.notes.push_back(MidiNote{0, 0, 60, 100, 0, 0});
    Scheduler s(song);
    TEST_ASSERT_EQUAL_UINT64(0, s.durationUs());
    s.setBarrier(0);  // holds exactly at the only (final) practiced onset
    s.advance(1000000);
    TEST_ASSERT_EQUAL_UINT64(0, s.positionUs());
    TEST_ASSERT_TRUE(s.atBarrier());
    TEST_ASSERT_FALSE_MESSAGE(
        s.finished(),
        "must stay waiting at the barrier, not Finished, even though "
        "positionUs already equals durationUs");
    s.clearBarrier();
    s.advance(1000000);
    TEST_ASSERT_TRUE_MESSAGE(s.finished(),
                             "Finished only after the barrier clears");
}

// §3 item 5: seek-to-exact-onset / seek-to-exact-barrier-time inclusive
// boundary semantics (indexForTime's lower_bound). Querying or seeking to an
// onset's OWN timestamp must land ON it, not skip past it.
static void test_p5_seek_and_next_onset_inclusive_at_exact_boundary() {
    MidiSong song = chordSong();
    Scheduler s(song);
    TEST_ASSERT_EQUAL_UINT64(500000, s.nextOnsetAfter(500000, kTrackMaskAll));
    s.seek(500000);
    TEST_ASSERT_EQUAL_UINT64(500000, s.positionUs());
    auto onsets = s.notesOnAt(500000, kTrackMaskAll);
    TEST_ASSERT_EQUAL_size_t(1, onsets.size());
    TEST_ASSERT_EQUAL_UINT8(64, onsets[0].note);
    // A barrier set at the same exact time a seek lands on reads as "at
    // barrier" immediately — no extra advance() needed.
    Scheduler s2(song);
    s2.setBarrier(500000);
    s2.seek(500000);
    TEST_ASSERT_TRUE(s2.atBarrier());
}

// §3 item 11: tempo change while barrier-held is inert — the barrier clamp
// compares raw target/barrier values, never the tempo-scaled real-time
// delta, so a tempo jump while held must never move the playhead.
static void test_p11_tempo_change_while_barrier_held_is_inert() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.setBarrier(500000);
    s.advance(2000000);
    TEST_ASSERT_EQUAL_UINT64(500000, s.positionUs());
    TEST_ASSERT_TRUE(s.atBarrier());
    s.setTempoPercent(400.0f);
    auto evs = s.advance(3000000);
    TEST_ASSERT_EQUAL_size_t(0, evs.size());
    TEST_ASSERT_EQUAL_UINT64(500000, s.positionUs());
    TEST_ASSERT_TRUE(s.atBarrier());
}

namespace {
// C4 on@0 off@480t; at the same instant (480t/500000us) a CC64 pedal event
// AND E4's onset — a three-way same-timestamp tie for the stable_sort
// comparator (§3 item 14).
MidiSong pedalTieSong() {
    smf::Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::cc(ev, 0, 0, 64, 127);
    smf::noteOn(ev, 0, 0, 64, 100);
    smf::noteOff(ev, 480, 0, 64);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}
}  // namespace

// §3 item 14: pedal event ordering at same-timestamp ties. The construction
// sort is Off(0) < Pedal(1) < On(2); only the Off-before-On half was ever
// exercised (test_off_sorts_before_on_at_same_instant) — pin the full
// three-way order including Pedal.
static void test_p14_pedal_sorts_between_off_and_on_at_same_instant() {
    MidiSong song = pedalTieSong();
    Scheduler s(song);
    auto evs = s.advance(510000);  // crosses t=500000 (off, pedal, on tie)
    int offIdx = -1, pedalIdx = -1, onIdx = -1;
    for (size_t i = 0; i < evs.size(); ++i) {
        if (evs[i].timeUs != 500000) continue;
        if (evs[i].type == SchedEventType::NoteOff && evs[i].note == 60)
            offIdx = static_cast<int>(i);
        if (evs[i].type == SchedEventType::Pedal)
            pedalIdx = static_cast<int>(i);
        if (evs[i].type == SchedEventType::NoteOn && evs[i].note == 64)
            onIdx = static_cast<int>(i);
    }
    TEST_ASSERT_TRUE(offIdx >= 0 && pedalIdx >= 0 && onIdx >= 0);
    TEST_ASSERT_TRUE_MESSAGE(offIdx < pedalIdx,
                             "Off must sort before Pedal at a tie");
    TEST_ASSERT_TRUE_MESSAGE(pedalIdx < onIdx,
                             "Pedal must sort before On at a tie");
}

// --- B3c: advance reports an explicit loop-wrap flag ----------------------
// The engine must not infer wrap from a position comparison: on a short loop at
// high tempo a single advance can wrap and land at/after where it started (the
// O(1) modulo collapse), so `newPos < prevPos` misses it and repeat/wait
// cursors never resync — the skipped-repeat-cue bug. The flag is authoritative.
static void test_advance_reports_wrap_flag_short_loop_high_tempo() {
    MidiSong song = chordSong();
    Scheduler s(song);
    s.setLoop(0, 100000);    // 100ms loop
    s.setTempoPercent(500);  // 5x: real time collapses many wraps
    std::vector<SchedEvent> buf;

    bool wrapped = false;
    s.advance(30000, buf, &wrapped);  // 30ms real → 150ms song: crosses 100ms
    TEST_ASSERT_TRUE_MESSAGE(wrapped, "crossing loopEnd must set the wrap flag");
    TEST_ASSERT_EQUAL_UINT64(50000, s.positionUs());  // 150 mod 100

    // A wrap that lands back at the SAME position (50ms → 150ms → 50ms): the
    // exact case a `newPos < prevPos` compare misses, yet must report wrapped.
    bool wrapped2 = false;
    s.advance(20000, buf, &wrapped2);  // +100ms song from 50ms
    TEST_ASSERT_TRUE_MESSAGE(wrapped2,
        "a wrap landing at the same position must still report wrapped");
    TEST_ASSERT_EQUAL_UINT64(50000, s.positionUs());

    // An advance that does not cross loopEnd reports no wrap (flag cleared).
    bool wrapped3 = true;
    s.advance(2000, buf, &wrapped3);   // +10ms song → 60ms, no crossing
    TEST_ASSERT_FALSE_MESSAGE(wrapped3,
        "an advance that doesn't cross loopEnd must report no wrap");
    TEST_ASSERT_EQUAL_UINT64(60000, s.positionUs());

    // The default-nullptr overload still works (existing call-sites unchanged).
    s.advance(2000, buf);
    TEST_ASSERT_EQUAL_UINT64(70000, s.positionUs());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_advance_reports_wrap_flag_short_loop_high_tempo);
    RUN_TEST(test_events_arrive_in_time_windows);
    RUN_TEST(test_off_sorts_before_on_at_same_instant);
    RUN_TEST(test_tempo_scales_time);
    RUN_TEST(test_tempo_clamps_to_1_500);
    RUN_TEST(test_loop_wraps_and_replays);
    RUN_TEST(test_barrier_halts_exactly);
    RUN_TEST(test_seek_repositions_and_flushes_sounding_notes);
    RUN_TEST(test_finished_after_last_event);
    RUN_TEST(test_events_carry_track_and_mask_filters);
    RUN_TEST(test_next_onset_query);
    RUN_TEST(test_onsets_between_window);
    RUN_TEST(test_next_onset_respects_track_mask);
    RUN_TEST(test_out_param_advance_matches_by_value_and_clears_buffer);
    RUN_TEST(test_out_param_queries_clear_then_fill);
    RUN_TEST(test_out_param_seek_flushes_into_buffer);
    RUN_TEST(test_p4_finished_stays_waiting_at_final_barrier_when_onset_equals_duration);
    RUN_TEST(test_p5_seek_and_next_onset_inclusive_at_exact_boundary);
    RUN_TEST(test_p11_tempo_change_while_barrier_held_is_inert);
    RUN_TEST(test_p14_pedal_sorts_between_off_and_on_at_same_instant);
    return UNITY_END();
}
