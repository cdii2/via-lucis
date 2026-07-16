#include <unity.h>

#include <algorithm>

#include "vialucis/midi_capture.h"
#include "vialucis/midi_parser.h"
#include "vialucis/smf_writer.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static constexpr uint64_t MS = 1000;  // us per ms, for readable timestamps

static const SmfNoteEvent* findNote(const CaptureTake& t, uint8_t note) {
    for (const SmfNoteEvent& n : t.notes)
        if (n.note == note) return &n;
    return nullptr;
}

// --- happy path: free capture -----------------------------------------------

static void test_free_capture_happy_path() {
    MidiCapture cap;
    TEST_ASSERT_EQUAL(ArmResult::Armed, cap.arm(4096, 600000, 0));
    TEST_ASSERT_EQUAL(CaptureState::Armed, cap.state());

    // Leading silence: clock starts at the FIRST note, whenever it lands.
    cap.onNoteOn(60, 100, 0, 5000 * MS);   // first note at absolute t=5s
    TEST_ASSERT_EQUAL(CaptureState::Recording, cap.state());
    cap.onNoteOff(60, 0, 5500 * MS);
    cap.onNoteOn(64, 90, 0, 6000 * MS);
    cap.onNoteOff(64, 0, 6250 * MS);

    CaptureTake take = cap.stop(6250 * MS);
    TEST_ASSERT_EQUAL(CaptureState::Idle, cap.state());
    TEST_ASSERT_FALSE(take.empty);
    TEST_ASSERT_EQUAL_size_t(2, take.notes.size());

    const SmfNoteEvent* first = findNote(take, 60);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_UINT32(0, first->onMs);     // trimmed to t=0
    TEST_ASSERT_EQUAL_UINT32(500, first->offMs);
    TEST_ASSERT_EQUAL_UINT8(100, first->velocity);

    const SmfNoteEvent* second = findNote(take, 64);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_UINT32(1000, second->onMs);  // 6s - 5s
    TEST_ASSERT_EQUAL_UINT32(1250, second->offMs);
}

// --- velocity preserved -----------------------------------------------------

static void test_velocity_preserved() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(72, 37, 0, 0);
    cap.onNoteOff(72, 0, 100 * MS);
    CaptureTake take = cap.stop(100 * MS);
    TEST_ASSERT_EQUAL_size_t(1, take.notes.size());
    TEST_ASSERT_EQUAL_UINT8(37, take.notes[0].velocity);
}

// --- CC64 captured ----------------------------------------------------------

static void test_cc64_captured() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 90, 0, 0);
    cap.onPedal(127, 0, 50 * MS);
    cap.onPedal(0, 0, 400 * MS);
    cap.onNoteOff(60, 0, 500 * MS);
    CaptureTake take = cap.stop(500 * MS);
    TEST_ASSERT_EQUAL_size_t(2, take.pedals.size());
    TEST_ASSERT_EQUAL_UINT32(50, take.pedals[0].tMs);
    TEST_ASSERT_EQUAL_UINT8(127, take.pedals[0].value);
    TEST_ASSERT_EQUAL_UINT32(400, take.pedals[1].tMs);
    TEST_ASSERT_EQUAL_UINT8(0, take.pedals[1].value);
    // A pedal-DOWN before the first note (still Armed) now OPENS the take
    // (B5: an opening sustain isn't lost) — it becomes the clock's anchor.
    MidiCapture cap2;
    cap2.arm(4096, 600000, 0);
    cap2.onPedal(127, 0, 10 * MS);
    TEST_ASSERT_EQUAL(CaptureState::Recording, cap2.state());
    cap2.onNoteOn(60, 90, 0, 20 * MS);
    CaptureTake t2 = cap2.stop(20 * MS);
    TEST_ASSERT_EQUAL_size_t(1, t2.pedals.size());
    TEST_ASSERT_EQUAL_UINT32(0, t2.pedals[0].tMs);  // anchor = the pedal press
    const SmfNoteEvent* n2 = findNote(t2, 60);
    TEST_ASSERT_NOT_NULL(n2);
    TEST_ASSERT_EQUAL_UINT32(10, n2->onMs);  // 10ms after the pedal anchor
}

// --- pedal-down while Armed starts the clock (B5) ---------------------------

static void test_pedal_down_while_armed_starts_clock() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    TEST_ASSERT_EQUAL(CaptureState::Armed, cap.state());
    cap.onPedal(127, 0, 5000 * MS);
    TEST_ASSERT_EQUAL(CaptureState::Recording, cap.state());
    CaptureTake take = cap.stop(5000 * MS);
    TEST_ASSERT_EQUAL_size_t(1, take.pedals.size());
    TEST_ASSERT_EQUAL_UINT32(0, take.pedals[0].tMs);
    TEST_ASSERT_EQUAL_UINT8(127, take.pedals[0].value);
    TEST_ASSERT_FALSE(take.empty);
}

// --- pedal-up (release) while Armed has nothing to open, stays dropped -----

static void test_pedal_up_while_armed_does_not_start_clock() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onPedal(0, 0, 5000 * MS);  // a release before any press: no anchor
    TEST_ASSERT_EQUAL(CaptureState::Armed, cap.state());
    cap.onNoteOn(60, 90, 0, 6000 * MS);
    CaptureTake take = cap.stop(6000 * MS);
    TEST_ASSERT_EQUAL_size_t(0, take.pedals.size());
    const SmfNoteEvent* n = findNote(take, 60);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_UINT32(0, n->onMs);  // the note is the real anchor
}

// --- record budget is clamped to kMaxRecordBudgetBytes (B5) -----------------

static void test_arm_clamps_oversize_budget() {
    MidiCapture cap;
    // Request far more than the max a saved take could ever hold.
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      cap.arm(4 * 1024 * 1024, 600000, 0));
    TEST_ASSERT_EQUAL_size_t(kMaxRecordBudgetBytes, cap.budgetBytes());
    // Requesting exactly the max is unaffected (not off-by-one clamped down
    // further).
    MidiCapture cap2;
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      cap2.arm(kMaxRecordBudgetBytes, 600000, 0));
    TEST_ASSERT_EQUAL_size_t(kMaxRecordBudgetBytes, cap2.budgetBytes());
    // A budget under the max is passed through untouched.
    MidiCapture cap3;
    TEST_ASSERT_EQUAL(ArmResult::Armed, cap3.arm(4096, 600000, 0));
    TEST_ASSERT_EQUAL_size_t(4096, cap3.budgetBytes());
}

// --- play-along echo exclusion ----------------------------------------------

static void test_playalong_echo_excluded() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    // Start recording with a genuine user note.
    cap.onNoteOn(60, 90, 0, 0);
    cap.onNoteOff(60, 0, 100 * MS);

    // The engine sends note 67 to the piano; capture is told via noteSent, then
    // the piano echoes 67 back within the guard window ⇒ it must be excluded.
    cap.noteSent(67, 200 * MS);
    cap.onNoteOn(67, 90, 0, 210 * MS);   // echo — excluded
    cap.onNoteOff(67, 0, 260 * MS);

    // A genuine re-press of 67 beyond the one credit ⇒ captured.
    cap.onNoteOn(67, 90, 0, 300 * MS);
    cap.onNoteOff(67, 0, 400 * MS);

    CaptureTake take = cap.stop(400 * MS);
    // Exactly one note 67 (the genuine press), plus note 60.
    int count67 = 0;
    for (const SmfNoteEvent& n : take.notes)
        if (n.note == 67) ++count67;
    TEST_ASSERT_EQUAL_INT(1, count67);
    const SmfNoteEvent* genuine = findNote(take, 67);
    TEST_ASSERT_NOT_NULL(genuine);
    TEST_ASSERT_EQUAL_UINT32(300, genuine->onMs);
}

static void test_echo_does_not_start_clock() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.noteSent(60, 1000 * MS);
    cap.onNoteOn(60, 90, 0, 1010 * MS);   // echo — must NOT start recording
    TEST_ASSERT_EQUAL(CaptureState::Armed, cap.state());
    // Real note anchors the clock at its own time.
    cap.onNoteOn(64, 90, 0, 2000 * MS);
    TEST_ASSERT_EQUAL(CaptureState::Recording, cap.state());
    cap.onNoteOff(64, 0, 2100 * MS);
    CaptureTake take = cap.stop(2100 * MS);
    TEST_ASSERT_EQUAL_size_t(1, take.notes.size());
    TEST_ASSERT_EQUAL_UINT8(64, take.notes[0].note);
    TEST_ASSERT_EQUAL_UINT32(0, take.notes[0].onMs);
}

// --- capture guard is independent of any engine guard -----------------------

static void test_guard_is_capture_owned() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 90, 0, 0);
    cap.onNoteOff(60, 0, 50 * MS);
    // Feeding capture's guard must not require or touch any external guard;
    // one credit ⇒ one exclusion.
    cap.noteSent(72, 100 * MS);
    cap.onNoteOn(72, 90, 0, 110 * MS);  // excluded
    cap.onNoteOn(72, 90, 0, 130 * MS);  // second, no credit left ⇒ captured
    cap.onNoteOff(72, 0, 200 * MS);
    CaptureTake take = cap.stop(200 * MS);
    int count72 = 0;
    for (const SmfNoteEvent& n : take.notes)
        if (n.note == 72) ++count72;
    TEST_ASSERT_EQUAL_INT(1, count72);
}

// --- budget overflow: typed, drops, never grows -----------------------------

static void test_budget_overflow_drops_and_never_grows() {
    MidiCapture cap;
    // Budget for exactly 4 events.
    size_t budget = 4 * sizeof(CaptureEvent);
    cap.arm(budget, 600000, 0);
    const CaptureEvent* base = cap.rawEvents().data();
    size_t cap0 = cap.rawEvents().capacity();

    // 3 note on/off pairs = 6 events; only 4 fit.
    for (int i = 0; i < 3; ++i) {
        cap.onNoteOn(60 + i, 90, 0, (i * 100) * MS);
        cap.onNoteOff(60 + i, 0, (i * 100 + 50) * MS);
    }
    TEST_ASSERT_EQUAL(CaptureStatus::Overflowed, cap.status());
    // Buffer never reallocated or grew past budget.
    TEST_ASSERT_EQUAL_PTR(base, cap.rawEvents().data());
    TEST_ASSERT_EQUAL_size_t(cap0, cap.rawEvents().capacity());
    TEST_ASSERT_TRUE(cap.usedBytes() <= budget);
    TEST_ASSERT_EQUAL_size_t(4, cap.rawEvents().size());

    CaptureTake take = cap.stop(250 * MS);
    TEST_ASSERT_EQUAL(CaptureStatus::Overflowed, take.status);
    // The 4 kept events = note 60 (on+off) and note 61 (on+off).
    TEST_ASSERT_NOT_NULL(findNote(take, 60));
    TEST_ASSERT_NOT_NULL(findNote(take, 61));
    TEST_ASSERT_NULL(findNote(take, 62));
}

// --- duration-cap overflow --------------------------------------------------

static void test_duration_cap_overflow() {
    MidiCapture cap;
    cap.arm(4096, 1000, 0);  // 1000 ms cap
    cap.onNoteOn(60, 90, 0, 0);
    cap.onNoteOff(60, 0, 500 * MS);
    cap.onNoteOn(64, 90, 0, 900 * MS);
    cap.onNoteOff(64, 0, 1100 * MS);   // 1100 ms > cap ⇒ dropped
    cap.onNoteOn(67, 90, 0, 1500 * MS);  // past cap ⇒ dropped
    TEST_ASSERT_EQUAL(CaptureStatus::Overflowed, cap.status());
    CaptureTake take = cap.stop(1500 * MS);
    TEST_ASSERT_EQUAL(CaptureStatus::Overflowed, take.status);
    // Note 64's off was dropped → still held at Stop, so it closes at the
    // stop time CLAMPED to the duration cap (1000 ms), never past truncation.
    const SmfNoteEvent* held = findNote(take, 64);
    TEST_ASSERT_NOT_NULL(held);
    TEST_ASSERT_EQUAL_UINT32(1000, held->offMs);
    TEST_ASSERT_NULL(findNote(take, 67));
}

// --- zero-alloc proof: capacity + data pointer stable arm→stop --------------

static void test_zero_alloc_capacity_stable() {
    MidiCapture cap;
    cap.arm(64 * sizeof(CaptureEvent), 600000, 0);
    const CaptureEvent* base = cap.rawEvents().data();
    size_t cap0 = cap.rawEvents().capacity();
    for (int i = 0; i < 20; ++i) {
        cap.onNoteOn(50 + (i % 12), 80, 0, (i * 10) * MS);
        cap.onNoteOff(50 + (i % 12), 0, (i * 10 + 5) * MS);
        TEST_ASSERT_EQUAL_PTR(base, cap.rawEvents().data());
        TEST_ASSERT_EQUAL_size_t(cap0, cap.rawEvents().capacity());
    }
    cap.stop(200 * MS);
    // The backing buffer never moved during the whole take.
    TEST_ASSERT_EQUAL_PTR(base, cap.rawEvents().data());
    TEST_ASSERT_EQUAL_size_t(cap0, cap.rawEvents().capacity());
}

// --- wait-pause behavior: real gaps preserved -------------------------------

static void test_wait_pause_gaps_preserved() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 90, 0, 0);
    cap.onNoteOff(60, 0, 100 * MS);
    // A long wait-mode pause (player reading music) before the next note — the
    // real elapsed gap is baked in (editor quantize cleans it later, per DESIGN).
    cap.onNoteOn(62, 90, 0, 30000 * MS);  // 30 s later
    cap.onNoteOff(62, 0, 30100 * MS);
    CaptureTake take = cap.stop(30100 * MS);
    const SmfNoteEvent* second = findNote(take, 62);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_UINT32(30000, second->onMs);  // gap included
    TEST_ASSERT_EQUAL_UINT32(30100, take.durationMs);
}

// --- held notes close at the STOP time, trailing silence stays trimmed ------

static void test_held_final_chord_closes_at_stop_time() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    // Final chord pressed at t=0 and HELD through Stop 4 s later — its real
    // duration must survive (the normal way a performance ends).
    cap.onNoteOn(60, 100, 0, 0);
    cap.onNoteOn(64, 95, 0, 0);
    cap.onNoteOn(67, 90, 0, 0);
    CaptureTake take = cap.stop(4000 * MS);
    TEST_ASSERT_EQUAL_size_t(3, take.notes.size());
    for (const SmfNoteEvent& n : take.notes)
        TEST_ASSERT_EQUAL_UINT32(4000, n.offMs);
    TEST_ASSERT_EQUAL_UINT32(4000, take.durationMs);
}

static void test_trailing_silence_trimmed() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 100, 0, 0);
    cap.onNoteOff(60, 0, 1000 * MS);
    // Nothing held: 29 s of silence before Stop must NOT stretch the take.
    CaptureTake take = cap.stop(30000 * MS);
    TEST_ASSERT_EQUAL_UINT32(1000, take.durationMs);
}

// --- pedal echo excluded (CC64 we sent must never enter a take, §5a) --------

static void test_pedal_echo_excluded() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 90, 0, 0);
    // Device sends CC64 (accompaniment pass-through); the piano echoes it.
    cap.pedalSent(100 * MS);
    cap.onPedal(127, 0, 110 * MS);   // echo — excluded
    // The player's OWN pedal press beyond the credit is captured.
    cap.onPedal(127, 0, 500 * MS);
    cap.onNoteOff(60, 0, 600 * MS);
    CaptureTake take = cap.stop(600 * MS);
    TEST_ASSERT_EQUAL_size_t(1, take.pedals.size());
    TEST_ASSERT_EQUAL_UINT32(500, take.pedals[0].tMs);
}

static void test_pedal_echo_credit_expires() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 90, 0, 0);
    cap.pedalSent(100 * MS);
    // Beyond the guard window the stale credit must not eat a real press.
    cap.onPedal(127, 0, 100 * MS + cap.echoGuard().windowUs() + 1000);
    CaptureTake take = cap.stop(1000 * MS);
    TEST_ASSERT_EQUAL_size_t(1, take.pedals.size());
}

// --- discard ----------------------------------------------------------------

static void test_discard() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 90, 0, 0);
    cap.onNoteOff(60, 0, 100 * MS);
    cap.discard();
    TEST_ASSERT_EQUAL(CaptureState::Idle, cap.state());
    TEST_ASSERT_EQUAL_size_t(0, cap.rawEvents().size());
    // Can re-arm cleanly after a discard.
    TEST_ASSERT_EQUAL(ArmResult::Armed, cap.arm(4096, 600000, 0));
}

// --- stop with zero events → empty take flagged -----------------------------

static void test_stop_zero_events_empty() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    CaptureTake take = cap.stop(5000 * MS);  // armed but never played
    TEST_ASSERT_TRUE(take.empty);
    TEST_ASSERT_EQUAL_size_t(0, take.notes.size());
    TEST_ASSERT_EQUAL_UINT32(0, take.durationMs);
    TEST_ASSERT_EQUAL(CaptureState::Idle, cap.state());
}

// --- arm refusals -----------------------------------------------------------

static void test_arm_refusals() {
    MidiCapture cap;
    TEST_ASSERT_EQUAL(ArmResult::Armed, cap.arm(4096, 600000, 0));
    // Arming again while not Idle is refused.
    TEST_ASSERT_EQUAL(ArmResult::AlreadyArmed, cap.arm(4096, 600000, 0));
    cap.discard();
    // A budget too small to hold a single event is refused.
    TEST_ASSERT_EQUAL(ArmResult::BadBudget, cap.arm(sizeof(CaptureEvent) - 1,
                                                    600000, 0));
    TEST_ASSERT_EQUAL(CaptureState::Idle, cap.state());
}

// --- integration: capture → writeSmf → parseMidi round-trip -----------------

static void test_roundtrip_capture_to_smf() {
    MidiCapture cap;
    cap.arm(8192, 600000, 0);
    // A small two-hand-ish performance with a pedal.
    cap.onNoteOn(60, 100, 0, 1000 * MS);   // first note anchors t=0
    cap.onNoteOn(64, 95, 0, 1000 * MS);    // chord partner, same instant
    cap.onPedal(127, 0, 1010 * MS);
    cap.onNoteOff(64, 0, 1500 * MS);
    cap.onNoteOff(60, 0, 1600 * MS);
    cap.onNoteOn(48, 80, 1, 1600 * MS);    // left-hand note, channel 1
    cap.onPedal(0, 0, 1900 * MS);
    cap.onNoteOff(48, 0, 2000 * MS);
    CaptureTake take = cap.stop(2000 * MS);

    // Feed the take to the SMF writer as one performance track.
    SmfInput in;
    SmfTrack t;
    t.name = "Take";
    t.notes = take.notes;
    t.pedals = take.pedals;
    in.tracks.push_back(t);
    std::vector<uint8_t> bytes = writeSmf(in);

    MidiParseResult r = parseMidi(bytes.data(), bytes.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(3, r.song.notes.size());
    TEST_ASSERT_EQUAL_size_t(2, r.song.pedal.size());

    // Timing survives the whole chain (ms → ticks → ms).
    bool saw60 = false, saw48 = false;
    for (const MidiNote& n : r.song.notes) {
        if (n.note == 60) {
            saw60 = true;
            TEST_ASSERT_EQUAL_UINT32(0, n.onTick);
            TEST_ASSERT_EQUAL_UINT32(600, n.offTick);
            TEST_ASSERT_EQUAL_UINT8(100, n.velocity);
        }
        if (n.note == 48) {
            saw48 = true;
            TEST_ASSERT_EQUAL_UINT32(600, n.onTick);   // 1600 - 1000
            TEST_ASSERT_EQUAL_UINT32(1000, n.offTick);
            TEST_ASSERT_EQUAL_UINT8(1, n.channel);
        }
    }
    TEST_ASSERT_TRUE(saw60);
    TEST_ASSERT_TRUE(saw48);
    TEST_ASSERT_EQUAL_UINT32(10, r.song.pedal[0].tick);
    TEST_ASSERT_EQUAL_UINT8(127, r.song.pedal[0].value);
}

// --- PIN-E coverage pack (audit §3, test-only pinning tests) ---------------

// §3 item 9: a re-trigger of an already-open (channel,note) with no
// note-off in between pairs correctly — the STOP-mirroring rule in
// midi_capture.cpp:113-121 closes the previous instance at the re-onset and
// opens a fresh one, exactly like the SMF-writer/parser round trip does.
static void test_p9_retriggered_note_without_off_pairs_correctly() {
    MidiCapture cap;
    cap.arm(4096, 600000, 0);
    cap.onNoteOn(60, 100, 0, 0);
    cap.onNoteOn(60, 80, 0, 300 * MS);  // re-strike, no off in between
    cap.onNoteOff(60, 0, 500 * MS);
    CaptureTake take = cap.stop(500 * MS);
    TEST_ASSERT_EQUAL_size_t(2, take.notes.size());
    TEST_ASSERT_EQUAL_UINT32(0, take.notes[0].onMs);
    TEST_ASSERT_EQUAL_UINT32(300, take.notes[0].offMs);
    TEST_ASSERT_EQUAL_UINT8(100, take.notes[0].velocity);
    TEST_ASSERT_EQUAL_UINT32(300, take.notes[1].onMs);
    TEST_ASSERT_EQUAL_UINT32(500, take.notes[1].offMs);
    TEST_ASSERT_EQUAL_UINT8(80, take.notes[1].velocity);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_free_capture_happy_path);
    RUN_TEST(test_velocity_preserved);
    RUN_TEST(test_cc64_captured);
    RUN_TEST(test_pedal_down_while_armed_starts_clock);
    RUN_TEST(test_pedal_up_while_armed_does_not_start_clock);
    RUN_TEST(test_arm_clamps_oversize_budget);
    RUN_TEST(test_playalong_echo_excluded);
    RUN_TEST(test_echo_does_not_start_clock);
    RUN_TEST(test_guard_is_capture_owned);
    RUN_TEST(test_budget_overflow_drops_and_never_grows);
    RUN_TEST(test_duration_cap_overflow);
    RUN_TEST(test_zero_alloc_capacity_stable);
    RUN_TEST(test_wait_pause_gaps_preserved);
    RUN_TEST(test_held_final_chord_closes_at_stop_time);
    RUN_TEST(test_trailing_silence_trimmed);
    RUN_TEST(test_pedal_echo_excluded);
    RUN_TEST(test_pedal_echo_credit_expires);
    RUN_TEST(test_discard);
    RUN_TEST(test_stop_zero_events_empty);
    RUN_TEST(test_arm_refusals);
    RUN_TEST(test_roundtrip_capture_to_smf);
    RUN_TEST(test_p9_retriggered_note_without_off_pairs_correctly);
    return UNITY_END();
}
