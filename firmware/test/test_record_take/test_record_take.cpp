// REC4 pure helpers: hand-split at the pitch split-point + the next-free
// auto-name scan.

#include <unity.h>

#include <string>
#include <vector>

#include "vialucis/midi_capture.h"
#include "vialucis/midi_parser.h"
#include "vialucis/record_take.h"
#include "vialucis/smf_writer.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static CaptureTake takeWith(const std::vector<SmfNoteEvent>& notes,
                            const std::vector<SmfPedalEvent>& pedals = {}) {
    CaptureTake t;
    t.notes = notes;
    t.pedals = pedals;
    t.empty = notes.empty() && pedals.empty();
    t.durationMs = 1000;
    return t;
}

static const SmfTrack* trackNamed(const SmfInput& in, const char* name) {
    for (const SmfTrack& t : in.tracks)
        if (t.name == name) return &t;
    return nullptr;
}

// --- hand split -------------------------------------------------------------

static void test_split_at_middle_c() {
    // 48 (below C4) → Left, 60 and 72 (>= C4) → Right.
    CaptureTake t = takeWith({
        {0, 500, 48, 90, 0},
        {0, 500, 60, 90, 0},
        {600, 900, 72, 90, 0},
    });
    SmfInput in = splitTakeIntoHands(t, 60);
    TEST_ASSERT_EQUAL_size_t(2, in.tracks.size());
    // Right emitted first (piano convention).
    TEST_ASSERT_EQUAL_STRING("Right", in.tracks[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Left", in.tracks[1].name.c_str());
    const SmfTrack* right = trackNamed(in, "Right");
    const SmfTrack* left = trackNamed(in, "Left");
    TEST_ASSERT_EQUAL_size_t(2, right->notes.size());  // 60, 72
    TEST_ASSERT_EQUAL_size_t(1, left->notes.size());   // 48
}

static void test_empty_hand_omitted() {
    // All notes >= 60 → only a Right track.
    CaptureTake t = takeWith({{0, 500, 64, 90, 0}, {0, 500, 67, 90, 0}});
    SmfInput in = splitTakeIntoHands(t, 60);
    TEST_ASSERT_EQUAL_size_t(1, in.tracks.size());
    TEST_ASSERT_EQUAL_STRING("Right", in.tracks[0].name.c_str());
}

static void test_pedals_attach_to_first_track() {
    // Both hands present; pedals go on the FIRST emitted track (Right).
    CaptureTake t = takeWith(
        {{0, 500, 40, 90, 0}, {0, 500, 72, 90, 0}},
        {{100, 127, 0}, {400, 0, 0}});
    SmfInput in = splitTakeIntoHands(t, 60);
    TEST_ASSERT_EQUAL_size_t(2, in.tracks.size());
    TEST_ASSERT_EQUAL_STRING("Right", in.tracks[0].name.c_str());
    TEST_ASSERT_EQUAL_size_t(2, in.tracks[0].pedals.size());
    TEST_ASSERT_EQUAL_size_t(0, in.tracks[1].pedals.size());
}

static void test_pedals_only_take_gets_a_track() {
    CaptureTake t = takeWith({}, {{100, 127, 0}, {400, 0, 0}});
    SmfInput in = splitTakeIntoHands(t, 60);
    TEST_ASSERT_EQUAL_size_t(1, in.tracks.size());
    TEST_ASSERT_EQUAL_size_t(2, in.tracks[0].pedals.size());
}

static void test_split_round_trips_through_smf() {
    CaptureTake t = takeWith(
        {{0, 500, 48, 80, 0}, {0, 500, 72, 95, 0}},
        {{50, 127, 0}});
    SmfInput in = splitTakeIntoHands(t, 60);
    std::vector<uint8_t> bytes = writeSmf(in);
    MidiParseResult r = parseMidi(bytes.data(), bytes.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(2, r.song.notes.size());
    TEST_ASSERT_EQUAL_size_t(1, r.song.pedal.size());
    // Track names survive so the load heuristic can assign the hands.
    bool sawLeft = false, sawRight = false;
    for (const auto& trk : r.song.tracks) {
        if (trk.name == "Left") sawLeft = true;
        if (trk.name == "Right") sawRight = true;
    }
    TEST_ASSERT_TRUE(sawLeft);
    TEST_ASSERT_TRUE(sawRight);
}

// --- next recording name ----------------------------------------------------

static void test_next_name_empty_is_one() {
    TEST_ASSERT_EQUAL_STRING("recording-1.mid",
                             nextRecordingName({}).c_str());
}

static void test_next_name_fills_gap() {
    // 1 and 3 used, other songs ignored → next is 2.
    std::vector<std::string> existing = {"recording-1.mid", "ode.mid",
                                         "recording-3.mid", "recording-.mid",
                                         "recording-x.mid"};
    TEST_ASSERT_EQUAL_STRING("recording-2.mid",
                             nextRecordingName(existing).c_str());
}

static void test_next_name_after_contiguous() {
    std::vector<std::string> existing = {"recording-1.mid", "recording-2.mid",
                                         "recording-3.mid"};
    TEST_ASSERT_EQUAL_STRING("recording-4.mid",
                             nextRecordingName(existing).c_str());
}

// --- takeWasTruncated (B5: Saved vs SavedTruncated) -------------------------

static void test_take_was_truncated_reflects_capture_status() {
    CaptureTake ok = takeWith({{0, 500, 60, 90, 0}});
    ok.status = CaptureStatus::Ok;
    TEST_ASSERT_FALSE(takeWasTruncated(ok));

    CaptureTake overflowed = takeWith({{0, 500, 60, 90, 0}});
    overflowed.status = CaptureStatus::Overflowed;
    TEST_ASSERT_TRUE(takeWasTruncated(overflowed));
}

// --- PendingSave: a failed save keeps the take retryable (B5) ---------------

static void test_pending_save_starts_empty() {
    PendingSave pending;
    TEST_ASSERT_FALSE(pending.held());
    pending.clear();  // no-op, must not crash/assert on an unheld clear
    TEST_ASSERT_FALSE(pending.held());
}

static void test_pending_save_holds_and_returns_the_take() {
    PendingSave pending;
    CaptureTake t = takeWith({{0, 500, 64, 90, 0}}, {{100, 127, 0}});
    pending.hold(t);
    TEST_ASSERT_TRUE(pending.held());
    TEST_ASSERT_EQUAL_size_t(1, pending.take().notes.size());
    TEST_ASSERT_EQUAL_UINT8(64, pending.take().notes[0].note);
    TEST_ASSERT_EQUAL_size_t(1, pending.take().pedals.size());
}

static void test_pending_save_clear_drops_the_take() {
    PendingSave pending;
    pending.hold(takeWith({{0, 500, 60, 90, 0}}));
    TEST_ASSERT_TRUE(pending.held());
    pending.clear();
    TEST_ASSERT_FALSE(pending.held());
}

static void test_pending_save_hold_replaces_prior_take() {
    // Only the MOST RECENT failed take is retryable — a second failed save
    // (e.g. retry-save itself failing again) replaces the first, it never
    // accumulates a queue.
    PendingSave pending;
    pending.hold(takeWith({{0, 500, 60, 90, 0}}));
    pending.hold(takeWith({{0, 500, 72, 90, 0}}));
    TEST_ASSERT_TRUE(pending.held());
    TEST_ASSERT_EQUAL_size_t(1, pending.take().notes.size());
    TEST_ASSERT_EQUAL_UINT8(72, pending.take().notes[0].note);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_split_at_middle_c);
    RUN_TEST(test_empty_hand_omitted);
    RUN_TEST(test_pedals_attach_to_first_track);
    RUN_TEST(test_pedals_only_take_gets_a_track);
    RUN_TEST(test_split_round_trips_through_smf);
    RUN_TEST(test_next_name_empty_is_one);
    RUN_TEST(test_next_name_fills_gap);
    RUN_TEST(test_next_name_after_contiguous);
    RUN_TEST(test_take_was_truncated_reflects_capture_status);
    RUN_TEST(test_pending_save_starts_empty);
    RUN_TEST(test_pending_save_holds_and_returns_the_take);
    RUN_TEST(test_pending_save_clear_drops_the_take);
    RUN_TEST(test_pending_save_hold_replaces_prior_take);
    return UNITY_END();
}
