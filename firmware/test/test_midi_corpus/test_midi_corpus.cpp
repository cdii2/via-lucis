// Golden MIDI conformance corpus (architecture-review step C2).
//
// corpus/midi/*.mid are hand-authored Standard MIDI Files, committed together
// with their expected twin *.expected.json — BOTH authored from one declarative
// model by corpus/gen/midi/gen_midi_fixtures.py (the twin is computed FROM the
// model, never by parsing the bytes). The corpus is the SAME cross-artifact
// contract idea as the .vls show corpus, applied to the three MIDI consumers:
//
//   1. Firmware (this suite) — parseMidi over the committed bytes must reproduce
//      the twin's firmware view field-by-field (ticks, tempo map, notes with
//      exact tickToMicros times, CC64 pedals), and TrackConfig::defaultsFor must
//      reproduce the twin's per-track hand + lights. Plus writeSmf is byte-pinned
//      to the recording-shaped fixture (the generator ships a byte-exact replica).
//   2. Tools — tools/check_midi_corpus.py (Builder B) guards the editor's
//      embedded hex against these committed files.
//   3. Editor — editor.html?selftest=1 re-parses the embedded hex and pins the
//      editor's CANONICAL-hands view (0=Right,1=Left,2=Other). The corpus pins
//      BOTH views of the same file so the A82 hand-swap (firmware and editor
//      disagreeing on which track is which hand) becomes a failing gate.
//
// Runs with cwd = firmware/ (where pio is invoked); fixtures live at
// ../corpus/midi/.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vialucis/midi_parser.h"
#include "vialucis/smf_writer.h"
#include "vialucis/track_config.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

namespace {

const char* kCorpusDir = "../corpus/midi/";

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Hand handFromName(const char* s) {
    if (std::string(s) == "off") return Hand::Off;
    if (std::string(s) == "left") return Hand::Left;
    if (std::string(s) == "right") return Hand::Right;
    if (std::string(s) == "both") return Hand::Both;
    TEST_FAIL_MESSAGE("unknown hand name in twin");
    return Hand::Off;
}

const char* handName(Hand h) {
    switch (h) {
        case Hand::Off: return "off";
        case Hand::Left: return "left";
        case Hand::Right: return "right";
        case Hand::Both: return "both";
    }
    return "?";
}

// Load a fixture's committed bytes + parsed twin document.
struct Fixture {
    std::string bytes;
    JsonDocument twin;
};

void loadFixture(const char* stem, Fixture& out) {
    out.bytes = readAll(std::string(kCorpusDir) + stem + ".mid");
    std::string twinText = readAll(std::string(kCorpusDir) + stem + ".expected.json");
    TEST_ASSERT_MESSAGE(!out.bytes.empty(),
                        "fixture .mid missing — run pio from firmware/");
    TEST_ASSERT_MESSAGE(!twinText.empty(), "fixture .expected.json missing");
    DeserializationError err = deserializeJson(out.twin, twinText);
    TEST_ASSERT_TRUE_MESSAGE(!err, err.c_str());
}

MidiSong parseFixture(const Fixture& fx) {
    MidiParseResult r = parseMidi(
        reinterpret_cast<const uint8_t*>(fx.bytes.data()), fx.bytes.size());
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(MidiParseError::Ok),
                              static_cast<int>(r.error),
                              "parseMidi did not return Ok");
    return r.song;
}

// Field-by-field: parseMidi output vs the firmware view in the twin.
void checkParse(const char* stem) {
    Fixture fx;
    loadFixture(stem, fx);
    MidiSong song = parseFixture(fx);
    JsonObjectConst t = fx.twin.as<JsonObjectConst>();

    TEST_ASSERT_EQUAL_UINT16(t["ticksPerQuarter"].as<uint16_t>(),
                             song.ticksPerQuarter);

    // Tracks (count + names).
    JsonArrayConst tracks = t["tracks"];
    TEST_ASSERT_EQUAL_size_t(tracks.size(), song.tracks.size());
    for (size_t i = 0; i < song.tracks.size(); ++i)
        TEST_ASSERT_EQUAL_STRING(tracks[i].as<const char*>(),
                                 song.tracks[i].name.c_str());

    // Tempo map (count + each entry).
    JsonArrayConst tempo = t["tempo"];
    TEST_ASSERT_EQUAL_size_t(tempo.size(), song.tempo.size());
    for (size_t i = 0; i < song.tempo.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32(tempo[i]["tick"].as<uint32_t>(),
                                 song.tempo[i].tick);
        TEST_ASSERT_EQUAL_UINT32(tempo[i]["usPerQuarter"].as<uint32_t>(),
                                 song.tempo[i].usPerQuarter);
    }

    // Notes (count + every field incl. exact microsecond times).
    JsonArrayConst notes = t["notes"];
    TEST_ASSERT_EQUAL_size_t(notes.size(), song.notes.size());
    for (size_t i = 0; i < song.notes.size(); ++i) {
        const MidiNote& n = song.notes[i];
        JsonObjectConst e = notes[i];
        TEST_ASSERT_EQUAL_UINT32(e["onTick"].as<uint32_t>(), n.onTick);
        TEST_ASSERT_EQUAL_UINT32(e["offTick"].as<uint32_t>(), n.offTick);
        // us fit uint32 in every fixture — cast for the Unity compare.
        TEST_ASSERT_EQUAL_UINT32(
            e["onUs"].as<uint32_t>(),
            static_cast<uint32_t>(tickToMicros(song, n.onTick)));
        TEST_ASSERT_EQUAL_UINT32(
            e["offUs"].as<uint32_t>(),
            static_cast<uint32_t>(tickToMicros(song, n.offTick)));
        TEST_ASSERT_EQUAL_UINT8(e["note"].as<uint8_t>(), n.note);
        TEST_ASSERT_EQUAL_UINT8(e["velocity"].as<uint8_t>(), n.velocity);
        TEST_ASSERT_EQUAL_UINT8(e["channel"].as<uint8_t>(), n.channel);
        TEST_ASSERT_EQUAL_UINT8(e["track"].as<uint8_t>(), n.track);
    }

    // Pedals (count + every field).
    JsonArrayConst pedal = t["pedal"];
    TEST_ASSERT_EQUAL_size_t(pedal.size(), song.pedal.size());
    for (size_t i = 0; i < song.pedal.size(); ++i) {
        const PedalEvent& p = song.pedal[i];
        JsonObjectConst e = pedal[i];
        TEST_ASSERT_EQUAL_UINT32(e["tick"].as<uint32_t>(), p.tick);
        TEST_ASSERT_EQUAL_UINT32(
            e["us"].as<uint32_t>(),
            static_cast<uint32_t>(tickToMicros(song, p.tick)));
        TEST_ASSERT_EQUAL_UINT8(e["value"].as<uint8_t>(), p.value);
        TEST_ASSERT_EQUAL_UINT8(e["channel"].as<uint8_t>(), p.channel);
        TEST_ASSERT_EQUAL_UINT8(e["track"].as<uint8_t>(), p.track);
    }
}

// TrackConfig::defaultsFor vs the twin's firmwareHands (hand enum + lights).
void checkHands(const char* stem) {
    Fixture fx;
    loadFixture(stem, fx);
    MidiSong song = parseFixture(fx);
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    JsonArrayConst hands = fx.twin["firmwareHands"].as<JsonArrayConst>();

    TEST_ASSERT_EQUAL_size_t(hands.size(), cfg.tracks.size());
    for (size_t i = 0; i < cfg.tracks.size(); ++i) {
        Hand want = handFromName(hands[i]["hand"].as<const char*>());
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(want),
                                  static_cast<int>(cfg.tracks[i].hand),
                                  handName(cfg.tracks[i].hand));
        TEST_ASSERT_EQUAL(hands[i]["lights"].as<bool>(), cfg.tracks[i].lights);
    }
}

// The recording-shaped SmfInput — identical to REC_SHAPED_INPUT in the
// generator. writeSmf(this) must equal the committed recording-shaped.mid.
SmfInput recordingShapedInput() {
    SmfInput in;
    SmfTrack right;
    right.name = "Right";
    right.notes = {{0, 500, 72, 100, 0}, {500, 1000, 74, 90, 0}};
    right.pedals = {{0, 127, 0}, {500, 64, 0}, {1000, 0, 0}};
    SmfTrack left;
    left.name = "Left";
    left.notes = {{0, 1000, 48, 80, 0}};
    in.tracks.push_back(right);
    in.tracks.push_back(left);
    return in;
}

// ---- per-fixture parse + hands ----
void test_conductor_first_parse() { checkParse("conductor-first"); }
void test_conductor_first_hands() { checkHands("conductor-first"); }
void test_named_lh_rh_parse() { checkParse("named-lh-rh"); }
void test_named_lh_rh_hands() { checkHands("named-lh-rh"); }
void test_ambiguous_name_parse() { checkParse("ambiguous-name"); }
void test_ambiguous_name_hands() { checkHands("ambiguous-name"); }
void test_anonymous_pair_parse() { checkParse("anonymous-pair"); }
void test_anonymous_pair_hands() { checkHands("anonymous-pair"); }
void test_three_hands_parse() { checkParse("three-hands"); }
void test_three_hands_hands() { checkHands("three-hands"); }
void test_pedal_only_track_parse() { checkParse("pedal-only-track"); }
void test_pedal_only_track_hands() { checkHands("pedal-only-track"); }
void test_tie_order_parse() { checkParse("tie-order"); }
void test_tie_order_hands() { checkHands("tie-order"); }
void test_vlq_tempo_parse() { checkParse("vlq-tempo"); }
void test_vlq_tempo_hands() { checkHands("vlq-tempo"); }
void test_recording_shaped_parse() { checkParse("recording-shaped"); }
void test_recording_shaped_hands() { checkHands("recording-shaped"); }

// writeSmf byte-pin: the real writer must reproduce the committed fixture.
void test_recording_shaped_writer_bytes() {
    std::string committed = readAll(std::string(kCorpusDir) + "recording-shaped.mid");
    TEST_ASSERT_MESSAGE(!committed.empty(), "recording-shaped.mid missing");
    std::vector<uint8_t> made = writeSmf(recordingShapedInput());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(committed.size(), made.size(),
                                     "writeSmf byte length != committed fixture");
    TEST_ASSERT_EQUAL_MEMORY(committed.data(), made.data(), committed.size());
}

// writeSmf → parseMidi sanity: counts + a few field spot-checks.
void test_recording_shaped_roundtrip() {
    std::vector<uint8_t> bytes = writeSmf(recordingShapedInput());
    MidiParseResult r = parseMidi(bytes.data(), bytes.size());
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Ok),
                      static_cast<int>(r.error));
    const MidiSong& s = r.song;
    TEST_ASSERT_EQUAL_size_t(3, s.notes.size());
    TEST_ASSERT_EQUAL_size_t(3, s.pedal.size());
    TEST_ASSERT_EQUAL_size_t(3, s.tracks.size());  // conductor + Right + Left
    TEST_ASSERT_EQUAL_STRING("", s.tracks[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Right", s.tracks[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Left", s.tracks[2].name.c_str());
    // First note is pitch 72 on the Right track (raw index 1); tie collision at
    // tick 500 kept the held note alive (off-before-on ordering).
    TEST_ASSERT_EQUAL_UINT8(72, s.notes[0].note);
    TEST_ASSERT_EQUAL_UINT8(1, s.notes[0].track);
    TEST_ASSERT_EQUAL_UINT8(127, s.pedal[0].value);
    TEST_ASSERT_EQUAL_UINT8(0, s.pedal[2].value);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_conductor_first_parse);
    RUN_TEST(test_conductor_first_hands);
    RUN_TEST(test_named_lh_rh_parse);
    RUN_TEST(test_named_lh_rh_hands);
    RUN_TEST(test_ambiguous_name_parse);
    RUN_TEST(test_ambiguous_name_hands);
    RUN_TEST(test_anonymous_pair_parse);
    RUN_TEST(test_anonymous_pair_hands);
    RUN_TEST(test_three_hands_parse);
    RUN_TEST(test_three_hands_hands);
    RUN_TEST(test_pedal_only_track_parse);
    RUN_TEST(test_pedal_only_track_hands);
    RUN_TEST(test_tie_order_parse);
    RUN_TEST(test_tie_order_hands);
    RUN_TEST(test_vlq_tempo_parse);
    RUN_TEST(test_vlq_tempo_hands);
    RUN_TEST(test_recording_shaped_parse);
    RUN_TEST(test_recording_shaped_hands);
    RUN_TEST(test_recording_shaped_writer_bytes);
    RUN_TEST(test_recording_shaped_roundtrip);
    return UNITY_END();
}
