#include <unity.h>

#include "../helpers/smf_builder.h"
#include "vialucis/midi_parser.h"

using namespace vialucis;
using smf::Bytes;

void setUp() {}
void tearDown() {}

// --- fixtures ---------------------------------------------------------------

// Format 0, 480 tpq, one middle C (note 60) at tick 0 lasting 480 ticks.
static Bytes singleNoteFile() {
    Bytes events;
    smf::noteOn(events, 0, 0, 60, 100);
    smf::noteOff(events, 480, 0, 60);
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(events));
    return file;
}

// --- header / error handling ------------------------------------------------

static void test_rejects_garbage() {
    Bytes junk = {'X', 'Y', 'Z', 'Z', 'Y'};
    auto r = parseMidi(junk.data(), junk.size());
    TEST_ASSERT_EQUAL(MidiParseError::NotMidi, r.error);
}

static void test_rejects_empty() {
    auto r = parseMidi(nullptr, 0);
    TEST_ASSERT_EQUAL(MidiParseError::NotMidi, r.error);
}

static void test_rejects_smpte_division() {
    Bytes file = smf::header(0, 1, 0xE728);  // SMPTE time division (high bit set)
    smf::append(file, smf::track({}));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::SmpteDivision, r.error);
}

static void test_rejects_truncated_track() {
    Bytes file = singleNoteFile();
    file.resize(file.size() - 5);  // cut into the track body
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Truncated, r.error);
}

// --- basic parsing ----------------------------------------------------------

static void test_parses_single_note() {
    Bytes file = singleNoteFile();
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_UINT16(480, r.song.ticksPerQuarter);
    TEST_ASSERT_EQUAL_size_t(1, r.song.notes.size());
    const MidiNote& n = r.song.notes[0];
    TEST_ASSERT_EQUAL_UINT32(0, n.onTick);
    TEST_ASSERT_EQUAL_UINT32(480, n.offTick);
    TEST_ASSERT_EQUAL_UINT8(60, n.note);
    TEST_ASSERT_EQUAL_UINT8(100, n.velocity);
    TEST_ASSERT_EQUAL_UINT8(0, n.track);
}

static void test_note_on_velocity_zero_is_note_off() {
    Bytes events;
    smf::noteOn(events, 0, 0, 64, 90);
    smf::noteOn(events, 240, 0, 64, 0);  // vel 0 == off
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(events));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(1, r.song.notes.size());
    TEST_ASSERT_EQUAL_UINT32(240, r.song.notes[0].offTick);
}

static void test_running_status() {
    // Two notes; second note-on omits the status byte.
    Bytes events;
    smf::noteOn(events, 0, 0, 60, 100);
    smf::putVlq(events, 0);
    events.push_back(64);   // running status: note 64 on
    events.push_back(100);
    smf::noteOff(events, 480, 0, 60);
    smf::putVlq(events, 0);
    events.push_back(64);   // running status: note 64 off (via 0x80 status)
    events.push_back(0);
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(events));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(2, r.song.notes.size());
}

static void test_unclosed_note_closes_at_track_end() {
    Bytes events;
    smf::noteOn(events, 0, 0, 72, 80);  // never released
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(events));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(1, r.song.notes.size());
    TEST_ASSERT_EQUAL_UINT32(0, r.song.notes[0].offTick);  // track ends at tick 0
}

// --- tempo map --------------------------------------------------------------

static void test_default_tempo_is_120bpm() {
    Bytes file = singleNoteFile();
    auto r = parseMidi(file.data(), file.size());
    // 480 tpq at 500000 us/q: tick 480 = 500000 us
    TEST_ASSERT_EQUAL_UINT64(500000, tickToMicros(r.song, 480));
    TEST_ASSERT_EQUAL_UINT64(250000, tickToMicros(r.song, 240));
}

static void test_tempo_change_mid_song() {
    Bytes events;
    smf::tempoMeta(events, 0, 500000);    // 120 bpm from tick 0
    smf::tempoMeta(events, 480, 250000);  // 240 bpm from tick 480
    smf::noteOn(events, 0, 0, 60, 100);   // tick 480
    smf::noteOff(events, 480, 0, 60);     // tick 960
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(events));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    // tick 960 = 480 ticks @500000 + 480 ticks @250000 = 500000 + 250000
    TEST_ASSERT_EQUAL_UINT64(750000, tickToMicros(r.song, 960));
}

// --- multi-track / format 1 ---------------------------------------------------

static void test_format1_tracks_and_names() {
    Bytes t0;  // conductor: tempo only
    smf::tempoMeta(t0, 0, 600000);
    Bytes t1;  // right hand
    smf::trackName(t1, 0, "Right");
    smf::noteOn(t1, 0, 0, 72, 100);
    smf::noteOff(t1, 120, 0, 72);
    Bytes t2;  // left hand
    smf::trackName(t2, 0, "Left");
    smf::noteOn(t2, 0, 1, 48, 100);
    smf::noteOff(t2, 240, 1, 48);

    Bytes file = smf::header(1, 3, 480);
    smf::append(file, smf::track(t0));
    smf::append(file, smf::track(t1));
    smf::append(file, smf::track(t2));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);

    TEST_ASSERT_EQUAL_size_t(3, r.song.tracks.size());
    TEST_ASSERT_EQUAL_STRING("Right", r.song.tracks[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Left", r.song.tracks[2].name.c_str());

    TEST_ASSERT_EQUAL_size_t(2, r.song.notes.size());
    // Notes sorted by onTick; same tick — order stable but both present.
    bool sawRight = false, sawLeft = false;
    for (const auto& n : r.song.notes) {
        if (n.track == 1 && n.note == 72) sawRight = true;
        if (n.track == 2 && n.note == 48 && n.channel == 1) sawLeft = true;
    }
    TEST_ASSERT_TRUE(sawRight);
    TEST_ASSERT_TRUE(sawLeft);
    // Tempo from conductor track applies globally.
    TEST_ASSERT_EQUAL_UINT64(600000, tickToMicros(r.song, 480));
}

static void test_notes_sorted_by_on_tick() {
    Bytes t1;
    smf::noteOn(t1, 100, 0, 70, 90);
    smf::noteOff(t1, 50, 0, 70);
    Bytes t2;
    smf::noteOn(t2, 0, 0, 40, 90);
    smf::noteOff(t2, 400, 0, 40);
    Bytes file = smf::header(1, 2, 480);
    smf::append(file, smf::track(t1));
    smf::append(file, smf::track(t2));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(2, r.song.notes.size());
    TEST_ASSERT_TRUE(r.song.notes[0].onTick <= r.song.notes[1].onTick);
    TEST_ASSERT_EQUAL_UINT8(40, r.song.notes[0].note);
}

// --- CC64 sustain -------------------------------------------------------------

static void test_captures_cc64() {
    Bytes events;
    smf::cc(events, 0, 0, 64, 127);   // pedal down
    smf::cc(events, 480, 0, 64, 0);   // pedal up
    smf::cc(events, 0, 0, 7, 100);    // volume CC — must be ignored
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(events));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(2, r.song.pedal.size());
    TEST_ASSERT_EQUAL_UINT8(127, r.song.pedal[0].value);
    TEST_ASSERT_EQUAL_UINT32(480, r.song.pedal[1].tick);
    TEST_ASSERT_EQUAL_UINT8(0, r.song.pedal[1].value);
}

// --- robustness: skip what we don't understand --------------------------------

static void test_skips_sysex_and_unknown_meta() {
    Bytes events;
    // SysEx F0 <len=3> aa bb F7
    smf::putVlq(events, 0);
    events.push_back(0xF0);
    smf::putVlq(events, 3);
    events.push_back(0x11); events.push_back(0x22); events.push_back(0xF7);
    // Unknown meta 0x7F
    smf::putVlq(events, 0);
    events.push_back(0xFF); events.push_back(0x7F);
    smf::putVlq(events, 2);
    events.push_back(0xAA); events.push_back(0xBB);
    // Program change + pitch bend (1- and 2-byte channel messages)
    smf::putVlq(events, 0);
    events.push_back(0xC0); events.push_back(5);
    smf::putVlq(events, 0);
    events.push_back(0xE0); events.push_back(0x00); events.push_back(0x40);
    // ...then a real note
    smf::noteOn(events, 10, 0, 60, 100);
    smf::noteOff(events, 480, 0, 60);
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(events));
    auto r = parseMidi(file.data(), file.size());
    TEST_ASSERT_EQUAL(MidiParseError::Ok, r.error);
    TEST_ASSERT_EQUAL_size_t(1, r.song.notes.size());
    TEST_ASSERT_EQUAL_UINT32(10, r.song.notes[0].onTick);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rejects_garbage);
    RUN_TEST(test_rejects_empty);
    RUN_TEST(test_rejects_smpte_division);
    RUN_TEST(test_rejects_truncated_track);
    RUN_TEST(test_parses_single_note);
    RUN_TEST(test_note_on_velocity_zero_is_note_off);
    RUN_TEST(test_running_status);
    RUN_TEST(test_unclosed_note_closes_at_track_end);
    RUN_TEST(test_default_tempo_is_120bpm);
    RUN_TEST(test_tempo_change_mid_song);
    RUN_TEST(test_format1_tracks_and_names);
    RUN_TEST(test_notes_sorted_by_on_tick);
    RUN_TEST(test_captures_cc64);
    RUN_TEST(test_skips_sysex_and_unknown_meta);
    return UNITY_END();
}
