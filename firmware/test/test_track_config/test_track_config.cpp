#include <unity.h>

#include "../helpers/smf_builder.h"
#include "vialucis/midi_parser.h"
#include "vialucis/track_config.h"

using namespace vialucis;
using smf::Bytes;

void setUp() {}
void tearDown() {}

static MidiSong makeSong(bool named, bool conductor) {
    Bytes file;
    int ntrks = conductor ? 3 : 2;
    file = smf::header(1, static_cast<uint16_t>(ntrks), 480);
    if (conductor) {
        Bytes t;
        smf::tempoMeta(t, 0, 500000);
        smf::append(file, smf::track(t));
    }
    Bytes t1;
    if (named) smf::trackName(t1, 0, "Piano Right Hand");
    smf::noteOn(t1, 0, 0, 72, 100);
    smf::noteOff(t1, 480, 0, 72);
    smf::append(file, smf::track(t1));
    Bytes t2;
    if (named) smf::trackName(t2, 0, "LH");
    smf::noteOn(t2, 0, 0, 48, 100);
    smf::noteOff(t2, 480, 0, 48);
    smf::append(file, smf::track(t2));
    return parseMidi(file.data(), file.size()).song;
}

static void test_named_tracks_assigned_by_name() {
    MidiSong song = makeSong(true, true);
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    TEST_ASSERT_EQUAL(Hand::Off, cfg.tracks[0].hand);    // conductor
    TEST_ASSERT_EQUAL(Hand::Right, cfg.tracks[1].hand);  // "Piano Right Hand"
    TEST_ASSERT_EQUAL(Hand::Left, cfg.tracks[2].hand);   // "LH"
}

static void test_anonymous_two_track_convention() {
    MidiSong song = makeSong(false, false);
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    TEST_ASSERT_EQUAL(Hand::Right, cfg.tracks[0].hand);
    TEST_ASSERT_EQUAL(Hand::Left, cfg.tracks[1].hand);
}

static void test_single_track_is_both() {
    Bytes t;
    smf::noteOn(t, 0, 0, 60, 100);
    smf::noteOff(t, 480, 0, 60);
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(t));
    MidiSong song = parseMidi(file.data(), file.size()).song;
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    TEST_ASSERT_EQUAL(Hand::Both, cfg.tracks[0].hand);
}

// A pedal-only track (the editor's export puts CC64 on its own track) must
// default audible — Hand::Off would silently drop the sustain from demo
// playback (the emitter masks Pedal events by track). A track with neither
// notes nor pedal stays Off.
static void test_pedal_only_track_defaults_audible() {
    Bytes file = smf::header(1, 3, 480);
    Bytes t1;
    smf::trackName(t1, 0, "Right");
    smf::noteOn(t1, 0, 0, 72, 100);
    smf::noteOff(t1, 480, 0, 72);
    smf::append(file, smf::track(t1));
    Bytes t2;
    smf::trackName(t2, 0, "Pedal");
    smf::cc(t2, 0, 0, 64, 127);
    smf::cc(t2, 480, 0, 64, 0);
    smf::append(file, smf::track(t2));
    Bytes t3;  // empty meta-ish track: no notes, no pedal
    smf::trackName(t3, 0, "Copyright");
    smf::append(file, smf::track(t3));
    MidiSong song = parseMidi(file.data(), file.size()).song;
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    TEST_ASSERT_EQUAL(Hand::Right, cfg.tracks[0].hand);
    TEST_ASSERT_EQUAL(Hand::Both, cfg.tracks[1].hand);  // pedal-only: audible
    TEST_ASSERT_FALSE(cfg.tracks[1].lights);
    TEST_ASSERT_EQUAL(Hand::Off, cfg.tracks[2].hand);
    // Demo emits the pedal track; accompaniment (the player's own foot) and
    // wait-mode barriers (no onsets) don't.
    TEST_ASSERT_TRUE((cfg.audibleMask() & trackBit(1)) != 0);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.accompanimentMask(Hand::Left) & trackBit(1));
}

static void test_masks_for_left_practice() {
    MidiSong song = makeSong(true, true);  // 0=off, 1=right, 2=left
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    TEST_ASSERT_EQUAL_UINT32(trackBit(2), cfg.practicedMask(Hand::Left));
    TEST_ASSERT_EQUAL_UINT32(trackBit(1), cfg.accompanimentMask(Hand::Left));
    TEST_ASSERT_EQUAL_UINT32(trackBit(1) | trackBit(2),
                             cfg.practicedMask(Hand::Both));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.accompanimentMask(Hand::Both));
    TEST_ASSERT_EQUAL_UINT32(trackBit(1) | trackBit(2), cfg.audibleMask());
}

static void test_lights_toggle_excludes_track() {
    MidiSong song = makeSong(true, true);
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    TEST_ASSERT_EQUAL_UINT32(trackBit(1) | trackBit(2), cfg.lightsMask());
    cfg.tracks[1].lights = false;  // SPEC: either hand's lights can go dark
    TEST_ASSERT_EQUAL_UINT32(trackBit(2), cfg.lightsMask());
}

static void test_both_hand_track_practices_under_either_hand() {
    Bytes t;
    smf::noteOn(t, 0, 0, 60, 100);
    smf::noteOff(t, 480, 0, 60);
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(t));
    MidiSong song = parseMidi(file.data(), file.size()).song;
    TrackConfig cfg = TrackConfig::defaultsFor(song);
    TEST_ASSERT_EQUAL_UINT32(trackBit(0), cfg.practicedMask(Hand::Left));
    TEST_ASSERT_EQUAL_UINT32(trackBit(0), cfg.practicedMask(Hand::Right));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_named_tracks_assigned_by_name);
    RUN_TEST(test_anonymous_two_track_convention);
    RUN_TEST(test_single_track_is_both);
    RUN_TEST(test_pedal_only_track_defaults_audible);
    RUN_TEST(test_masks_for_left_practice);
    RUN_TEST(test_lights_toggle_excludes_track);
    RUN_TEST(test_both_hand_track_practices_under_either_hand);
    return UNITY_END();
}
