#include <unity.h>

#include <algorithm>

#include "vialucis/midi_parser.h"
#include "vialucis/smf_writer.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

// Round-trip helper: writeSmf → parseMidi.
static MidiSong roundTrip(const SmfInput& in, MidiParseError* errOut = nullptr) {
    std::vector<uint8_t> bytes = writeSmf(in);
    MidiParseResult r = parseMidi(bytes.data(), bytes.size());
    if (errOut) *errOut = r.error;
    return r.song;
}

// Find a parsed note by (note, channel); nullptr if absent.
static const MidiNote* findNote(const MidiSong& s, uint8_t note, uint8_t ch) {
    for (const MidiNote& n : s.notes)
        if (n.note == note && n.channel == ch) return &n;
    return nullptr;
}

// --- timebase & tempo -------------------------------------------------------

static void test_fixed_timebase_and_tempo() {
    SmfInput in;
    in.tracks.push_back({"", {{0, 100, 60, 90, 0}}, {}});
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_UINT16(500, s.ticksPerQuarter);
    // 1 tick == 1 ms: tick 100 → 100000 us.
    TEST_ASSERT_EQUAL_UINT64(100000, tickToMicros(s, 100));
    // Tempo map carries exactly the one conductor tempo (500000 us/q).
    TEST_ASSERT_EQUAL_size_t(1, s.tempo.size());
    TEST_ASSERT_EQUAL_UINT32(500000, s.tempo[0].usPerQuarter);
}

// --- empty take -------------------------------------------------------------

static void test_empty_take() {
    SmfInput in;  // no tracks at all
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(0, s.notes.size());
    // Conductor track only.
    TEST_ASSERT_EQUAL_size_t(1, s.tracks.size());
}

static void test_empty_performance_track() {
    SmfInput in;
    in.tracks.push_back({"Right", {}, {}});  // named but no events
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(0, s.notes.size());
    TEST_ASSERT_EQUAL_size_t(2, s.tracks.size());  // conductor + Right
    TEST_ASSERT_EQUAL_STRING("Right", s.tracks[1].name.c_str());
}

// --- single note ------------------------------------------------------------

static void test_single_note() {
    SmfInput in;
    in.tracks.push_back({"", {{10, 490, 64, 77, 3}}, {}});
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(1, s.notes.size());
    const MidiNote& n = s.notes[0];
    TEST_ASSERT_EQUAL_UINT32(10, n.onTick);
    TEST_ASSERT_EQUAL_UINT32(490, n.offTick);
    TEST_ASSERT_EQUAL_UINT8(64, n.note);
    TEST_ASSERT_EQUAL_UINT8(77, n.velocity);
    TEST_ASSERT_EQUAL_UINT8(3, n.channel);
    TEST_ASSERT_EQUAL_UINT8(1, n.track);  // performance track 0 == parser track 1
}

// --- chord (simultaneous, delta 0) ------------------------------------------

static void test_chord_simultaneous() {
    SmfInput in;
    SmfTrack t;
    t.notes = {{0, 480, 60, 80, 0}, {0, 480, 64, 80, 0}, {0, 480, 67, 80, 0}};
    in.tracks.push_back(t);
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(3, s.notes.size());
    for (uint8_t note : {60, 64, 67}) {
        const MidiNote* n = findNote(s, note, 0);
        TEST_ASSERT_NOT_NULL(n);
        TEST_ASSERT_EQUAL_UINT32(0, n->onTick);
        TEST_ASSERT_EQUAL_UINT32(480, n->offTick);
    }
}

// --- overlapping / held notes -----------------------------------------------

static void test_overlapping_held_notes() {
    SmfInput in;
    SmfTrack t;
    // Two different notes overlapping; distinct on/off times.
    t.notes = {{0, 300, 60, 90, 0}, {100, 500, 67, 70, 0}};
    in.tracks.push_back(t);
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    const MidiNote* a = findNote(s, 60, 0);
    const MidiNote* b = findNote(s, 67, 0);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT32(0, a->onTick);
    TEST_ASSERT_EQUAL_UINT32(300, a->offTick);
    TEST_ASSERT_EQUAL_UINT32(100, b->onTick);
    TEST_ASSERT_EQUAL_UINT32(500, b->offTick);
}

// --- same-key re-strike at a shared boundary (tie-order) --------------------

static void test_same_key_restrike_boundary() {
    SmfInput in;
    SmfTrack t;
    // Same note: first ends exactly when the second begins.
    t.notes = {{0, 100, 62, 90, 0}, {100, 200, 62, 90, 0}};
    in.tracks.push_back(t);
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    // Both instances survive with their own [on,off); neither is zero-length.
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (const MidiNote& n : s.notes) spans.push_back({n.onTick, n.offTick});
    std::sort(spans.begin(), spans.end());
    TEST_ASSERT_EQUAL_UINT32(0, spans[0].first);
    TEST_ASSERT_EQUAL_UINT32(100, spans[0].second);
    TEST_ASSERT_EQUAL_UINT32(100, spans[1].first);
    TEST_ASSERT_EQUAL_UINT32(200, spans[1].second);
}

// --- long silence (multi-byte varlen) ---------------------------------------

static void test_long_silence_multibyte_varlen() {
    SmfInput in;
    SmfTrack t;
    // Second note starts ~500 s later → delta 500000 needs a 3-byte VLQ.
    t.notes = {{0, 100, 60, 90, 0}, {500000, 500100, 72, 90, 0}};
    in.tracks.push_back(t);
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    const MidiNote* late = findNote(s, 72, 0);
    TEST_ASSERT_NOT_NULL(late);
    TEST_ASSERT_EQUAL_UINT32(500000, late->onTick);
    TEST_ASSERT_EQUAL_UINT32(500100, late->offTick);
}

// --- CC64 interleaved -------------------------------------------------------

static void test_cc64_interleaved() {
    SmfInput in;
    SmfTrack t;
    t.notes = {{0, 400, 60, 90, 0}};
    t.pedals = {{50, 127, 0}, {350, 0, 0}};  // pedal down mid-note, up before off
    in.tracks.push_back(t);
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(1, s.notes.size());
    TEST_ASSERT_EQUAL_size_t(2, s.pedal.size());
    TEST_ASSERT_EQUAL_UINT32(50, s.pedal[0].tick);
    TEST_ASSERT_EQUAL_UINT8(127, s.pedal[0].value);
    TEST_ASSERT_EQUAL_UINT32(350, s.pedal[1].tick);
    TEST_ASSERT_EQUAL_UINT8(0, s.pedal[1].value);
    TEST_ASSERT_EQUAL_UINT8(1, s.pedal[0].track);  // on performance track 1
}

// --- two hand-split tracks with names ---------------------------------------

static void test_two_handsplit_tracks() {
    SmfInput in;
    SmfTrack right;
    right.name = "Right";
    right.notes = {{0, 200, 72, 100, 0}};
    SmfTrack left;
    left.name = "Left";
    left.notes = {{0, 400, 48, 100, 1}};
    in.tracks.push_back(right);
    in.tracks.push_back(left);
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(3, s.tracks.size());
    TEST_ASSERT_EQUAL_STRING("Right", s.tracks[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Left", s.tracks[2].name.c_str());
    const MidiNote* r = findNote(s, 72, 0);
    const MidiNote* l = findNote(s, 48, 1);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQUAL_UINT8(1, r->track);
    TEST_ASSERT_EQUAL_UINT8(2, l->track);
}

// --- unsorted input is normalized -------------------------------------------

static void test_unsorted_input_events() {
    SmfInput in;
    SmfTrack t;
    // Deliberately out of time order; writer must sort before delta encoding.
    t.notes = {{300, 400, 67, 80, 0}, {0, 100, 60, 80, 0}, {100, 200, 64, 80, 0}};
    in.tracks.push_back(t);
    MidiParseError err;
    MidiSong s = roundTrip(in, &err);
    TEST_ASSERT_EQUAL(MidiParseError::Ok, err);
    TEST_ASSERT_EQUAL_size_t(3, s.notes.size());
    const MidiNote* a = findNote(s, 60, 0);
    const MidiNote* b = findNote(s, 64, 0);
    const MidiNote* c = findNote(s, 67, 0);
    TEST_ASSERT_EQUAL_UINT32(0, a->onTick);
    TEST_ASSERT_EQUAL_UINT32(100, b->onTick);
    TEST_ASSERT_EQUAL_UINT32(300, c->onTick);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fixed_timebase_and_tempo);
    RUN_TEST(test_empty_take);
    RUN_TEST(test_empty_performance_track);
    RUN_TEST(test_single_note);
    RUN_TEST(test_chord_simultaneous);
    RUN_TEST(test_overlapping_held_notes);
    RUN_TEST(test_same_key_restrike_boundary);
    RUN_TEST(test_long_silence_multibyte_varlen);
    RUN_TEST(test_cc64_interleaved);
    RUN_TEST(test_two_handsplit_tracks);
    RUN_TEST(test_unsorted_input_events);
    return UNITY_END();
}
