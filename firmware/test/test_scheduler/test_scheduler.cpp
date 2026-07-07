#include <unity.h>

#include "../helpers/smf_builder.h"
#include "vialucis/midi_parser.h"
#include "vialucis/scheduler.h"

using namespace vialucis;
using smf::Bytes;

void setUp() {}
void tearDown() {}

// 480 tpq @ default 120bpm ⇒ 1 tick ≈ 1041.6us, quarter = 500000us.
// Song: C4 on@0 off@480, E4 on@480 off@960, G4+B4 chord on@960 off@1440 (ticks).
static MidiSong chordSong() {
    Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOn(ev, 0, 0, 64, 100);
    smf::noteOff(ev, 480, 0, 64);
    smf::noteOn(ev, 0, 0, 67, 100);
    smf::noteOn(ev, 0, 0, 71, 100);
    smf::noteOff(ev, 480, 0, 67);
    smf::noteOff(ev, 0, 0, 71);
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    auto r = parseMidi(file.data(), file.size());
    return r.song;
}

// Two tracks: track 0 note 60, track 1 note 40, both on@0 off@480.
static MidiSong twoTrackSong() {
    Bytes t0, t1;
    smf::noteOn(t0, 0, 0, 60, 100);
    smf::noteOff(t0, 480, 0, 60);
    smf::noteOn(t1, 0, 0, 40, 100);
    smf::noteOff(t1, 480, 0, 40);
    Bytes file = smf::header(1, 2, 480);
    smf::append(file, smf::track(t0));
    smf::append(file, smf::track(t1));
    return parseMidi(file.data(), file.size()).song;
}

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

int main(int, char**) {
    UNITY_BEGIN();
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
    return UNITY_END();
}
