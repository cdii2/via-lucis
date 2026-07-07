// MidiIo seam (R1): the 5-method interface App consumes from its MIDI
// transport. Exercised polymorphically through MidiIo& so the contract —
// not the fake's own surface — is what these tests pin down.

#include <unity.h>

#include <vector>

#include "vialucis/fake_midi_io.h"
#include "vialucis/midi_io.h"

using namespace vialucis;

namespace {

struct Received {
    uint8_t note;
    uint8_t velocity;
};

std::vector<Received> gOns;
std::vector<Received> gOffs;

void wire(MidiIo& io) {
    gOns.clear();
    gOffs.clear();
    io.onNoteOn([](uint8_t n, uint8_t v) { gOns.push_back({n, v}); });
    io.onNoteOff([](uint8_t n, uint8_t v) { gOffs.push_back({n, v}); });
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_scripted_note_on_arrives_via_poll() {
    FakeMidiIo fake;
    MidiIo& io = fake;
    io.begin();
    wire(io);

    fake.scriptNoteOn(60, 100);
    TEST_ASSERT_EQUAL_UINT(0, gOns.size());  // nothing until poll — like MIDI.read()

    io.poll();
    TEST_ASSERT_EQUAL_UINT(1, gOns.size());
    TEST_ASSERT_EQUAL_UINT8(60, gOns[0].note);
    TEST_ASSERT_EQUAL_UINT8(100, gOns[0].velocity);
}

void test_velocity_zero_note_on_dispatches_as_note_off() {
    // Running-status quirk stays behind the seam: consumers only ever see
    // a note-off (same contract as the NimBLE adapter).
    FakeMidiIo fake;
    MidiIo& io = fake;
    wire(io);

    fake.scriptNoteOn(64, 0);
    io.poll();
    TEST_ASSERT_EQUAL_UINT(0, gOns.size());
    TEST_ASSERT_EQUAL_UINT(1, gOffs.size());
    TEST_ASSERT_EQUAL_UINT8(64, gOffs[0].note);
    TEST_ASSERT_EQUAL_UINT8(0, gOffs[0].velocity);
}

void test_explicit_note_off_keeps_velocity() {
    FakeMidiIo fake;
    MidiIo& io = fake;
    wire(io);

    fake.scriptNoteOff(62, 40);
    io.poll();
    TEST_ASSERT_EQUAL_UINT(1, gOffs.size());
    TEST_ASSERT_EQUAL_UINT8(62, gOffs[0].note);
    TEST_ASSERT_EQUAL_UINT8(40, gOffs[0].velocity);
}

void test_poll_delivers_script_in_order_then_is_drained() {
    FakeMidiIo fake;
    MidiIo& io = fake;
    wire(io);

    fake.scriptNoteOn(60, 90);
    fake.scriptNoteOn(64, 91);
    io.poll();
    TEST_ASSERT_EQUAL_UINT(2, gOns.size());
    TEST_ASSERT_EQUAL_UINT8(60, gOns[0].note);
    TEST_ASSERT_EQUAL_UINT8(64, gOns[1].note);

    io.poll();  // drained: nothing new
    TEST_ASSERT_EQUAL_UINT(2, gOns.size());
}

void test_send_records_messages_through_the_seam() {
    FakeMidiIo fake;
    MidiIo& io = fake;

    io.send({MidiOutType::NoteOn, 0, 60, 100});
    io.send({MidiOutType::Cc, 0, 64, 127});
    TEST_ASSERT_EQUAL_UINT(2, fake.sent().size());
    TEST_ASSERT_EQUAL_UINT8(60, fake.sent()[0].data1);
    TEST_ASSERT_TRUE(fake.sent()[1].type == MidiOutType::Cc);
}

void test_send_while_disconnected_is_dropped() {
    // Same contract as the BLE adapter: nothing goes out without a link.
    FakeMidiIo fake;
    MidiIo& io = fake;
    fake.setConnected(false);

    io.send({MidiOutType::NoteOn, 0, 60, 100});
    TEST_ASSERT_EQUAL_UINT(0, fake.sent().size());
    TEST_ASSERT_FALSE(io.connected());

    fake.setConnected(true);
    TEST_ASSERT_TRUE(io.connected());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_scripted_note_on_arrives_via_poll);
    RUN_TEST(test_velocity_zero_note_on_dispatches_as_note_off);
    RUN_TEST(test_explicit_note_off_keeps_velocity);
    RUN_TEST(test_poll_delivers_script_in_order_then_is_drained);
    RUN_TEST(test_send_records_messages_through_the_seam);
    RUN_TEST(test_send_while_disconnected_is_dropped);
    return UNITY_END();
}
