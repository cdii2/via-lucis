#include <unity.h>

#include "vialucis/echo_guard.h"
#include "vialucis/note_emitter.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

static SchedEvent on(uint8_t note, uint8_t track, uint8_t vel = 100,
                     uint8_t ch = 0) {
    return {SchedEventType::NoteOn, 0, note, vel, ch, track};
}
static SchedEvent off(uint8_t note, uint8_t track, uint8_t ch = 0) {
    return {SchedEventType::NoteOff, 0, note, 0, ch, track};
}
static SchedEvent pedal(uint8_t value, uint8_t track, uint8_t ch = 0) {
    return {SchedEventType::Pedal, 0, 64, value, ch, track};
}

static void test_demo_emits_all_tracks() {
    NoteEmitter e(kTrackMaskAll);
    auto msgs = e.consume({on(60, 0), on(40, 1)}, 0);
    TEST_ASSERT_EQUAL_size_t(2, msgs.size());
    TEST_ASSERT_EQUAL(MidiOutType::NoteOn, msgs[0].type);
    TEST_ASSERT_EQUAL_UINT8(60, msgs[0].data1);
    TEST_ASSERT_EQUAL_UINT8(100, msgs[0].data2);
    msgs = e.consume({off(60, 0)}, 1000);
    TEST_ASSERT_EQUAL_size_t(1, msgs.size());
    TEST_ASSERT_EQUAL(MidiOutType::NoteOff, msgs[0].type);
}

static void test_accompaniment_mask_filters_practiced_hand() {
    NoteEmitter e(trackBit(1));  // play only the muted hand (track 1)
    auto msgs = e.consume({on(60, 0), on(40, 1)}, 0);
    TEST_ASSERT_EQUAL_size_t(1, msgs.size());
    TEST_ASSERT_EQUAL_UINT8(40, msgs[0].data1);
}

static void test_pedal_passthrough_respects_mask() {
    NoteEmitter e(trackBit(1));
    auto msgs = e.consume({pedal(127, 0), pedal(90, 1)}, 0);
    TEST_ASSERT_EQUAL_size_t(1, msgs.size());
    TEST_ASSERT_EQUAL(MidiOutType::Cc, msgs[0].type);
    TEST_ASSERT_EQUAL_UINT8(64, msgs[0].data1);
    TEST_ASSERT_EQUAL_UINT8(90, msgs[0].data2);
}

static void test_sent_notes_register_with_echo_guard() {
    EchoGuard g;
    NoteEmitter e(kTrackMaskAll);
    e.setEchoGuard(&g);
    e.consume({on(60, 0)}, 5000);
    TEST_ASSERT_TRUE(g.shouldIgnore(60, 6000));
    TEST_ASSERT_FALSE(g.shouldIgnore(60, 7000));  // one credit only
}

static void test_masked_out_notes_do_not_register_echo() {
    EchoGuard g;
    NoteEmitter e(trackBit(1));
    e.setEchoGuard(&g);
    e.consume({on(60, 0)}, 0);  // filtered → never transmitted → no echo
    TEST_ASSERT_FALSE(g.shouldIgnore(60, 1000));
}

static void test_all_off_flushes_sounding() {
    NoteEmitter e(kTrackMaskAll);
    e.consume({on(60, 0), on(64, 0), off(60, 0)}, 0);
    auto msgs = e.allOff();
    TEST_ASSERT_EQUAL_size_t(1, msgs.size());  // only 64 still sounding
    TEST_ASSERT_EQUAL(MidiOutType::NoteOff, msgs[0].type);
    TEST_ASSERT_EQUAL_UINT8(64, msgs[0].data1);
    TEST_ASSERT_EQUAL_size_t(0, e.allOff().size());  // idempotent
}

static void test_orphan_note_off_still_emitted() {
    NoteEmitter e(kTrackMaskAll);
    auto msgs = e.consume({off(60, 0)}, 0);  // e.g. right after a mask flip
    TEST_ASSERT_EQUAL_size_t(1, msgs.size());
    TEST_ASSERT_EQUAL(MidiOutType::NoteOff, msgs[0].type);
}

// R5: producer variants APPEND to the caller's queue — the engine feeds one
// shared out-vector per tick, so consume/allOff must never clear it.
static void test_out_param_consume_and_all_off_append() {
    NoteEmitter e(kTrackMaskAll);
    std::vector<MidiOutMsg> out;
    out.push_back({MidiOutType::Cc, 0, 64, 127});  // already queued this tick

    e.consume({on(60, 0), on(64, 0)}, 0, out);
    TEST_ASSERT_EQUAL_size_t(3, out.size());  // appended after the CC
    TEST_ASSERT_EQUAL_UINT8(60, out[1].data1);

    e.allOff(out);
    TEST_ASSERT_EQUAL_size_t(5, out.size());  // both note-offs appended
    TEST_ASSERT_EQUAL(MidiOutType::NoteOff, out[3].type);
    e.allOff(out);
    TEST_ASSERT_EQUAL_size_t(5, out.size());  // idempotent
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_demo_emits_all_tracks);
    RUN_TEST(test_accompaniment_mask_filters_practiced_hand);
    RUN_TEST(test_pedal_passthrough_respects_mask);
    RUN_TEST(test_sent_notes_register_with_echo_guard);
    RUN_TEST(test_masked_out_notes_do_not_register_echo);
    RUN_TEST(test_all_off_flushes_sounding);
    RUN_TEST(test_orphan_note_off_still_emitted);
    RUN_TEST(test_out_param_consume_and_all_off_append);
    return UNITY_END();
}
