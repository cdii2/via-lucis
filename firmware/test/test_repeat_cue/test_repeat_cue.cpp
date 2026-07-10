// Q1 — repeat cue (brief §2): the ONE sanctioned practice-visual delta in
// v2. Gap fills are precomputed at load; render is O(1) per key; the onset
// is never delayed — sub-floor gaps borrow backward from the outgoing tail.

#include <unity.h>

#include <vector>

#include "../helpers/smf_builder.h"
#include "vialucis/key_led_map.h"
#include "vialucis/midi_parser.h"
#include "vialucis/playback_engine.h"
#include "vialucis/settings.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

namespace {

std::vector<MidiOutMsg> gOut;

// 480 tpq @ default 120 bpm ⇒ 1 quarter = 480t = 500ms; 1t = 500000/480 us.
constexpr uint64_t kTickUs = 500000ull / 480ull;

// C4 twice: on@0 off@480t, on@960t off@1440t → off-gap = 480t = 500ms.
MidiSong repeatSong() {
    smf::Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOn(ev, 480, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

// C4 with a 24t (25ms) off-gap: on@0 off@480t, on@504t. 25ms < 35ms floor.
MidiSong borrowSong() {
    smf::Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOn(ev, 24, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

// C4 with a 2t (~2ms) off-gap — the 3ms pathology: collapses to one event.
MidiSong pathologySong() {
    smf::Bytes ev;
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::noteOn(ev, 2, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    smf::Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return parseMidi(file.data(), file.size()).song;
}

// Two tracks, SAME key: t0 C4 on@0 off@480t; t1 C4 on@960t — cross-hand
// re-press of one physical key.
MidiSong crossHandSong() {
    smf::Bytes t0, t1;
    smf::noteOn(t0, 0, 0, 60, 100);
    smf::noteOff(t0, 480, 0, 60);
    smf::noteOn(t1, 960, 0, 60, 100);
    smf::noteOff(t1, 480, 0, 60);
    smf::Bytes file = smf::header(1, 2, 480);
    smf::append(file, smf::track(t0));
    smf::append(file, smf::track(t1));
    return parseMidi(file.data(), file.size()).song;
}

void setupEngine(PlaybackEngine& e, MidiSong song, const char* mode) {
    e.configure(Settings{}, 360);
    gOut.clear();
    e.loadSong(std::move(song), "q.mid", gOut);
    e.setMode(mode, "both", gOut);
    gOut.clear();
    e.transport("play", 0, gOut);
    e.tick(1000, gOut);  // baseline the clock (delta 0)
}

// Advance song time to `songUs` (clock was baselined at wall 1000).
void runTo(PlaybackEngine& e, uint64_t songUs) {
    gOut.clear();
    e.tick(1000 + songUs, gOut);
}

Rgb ledAt(const std::vector<Rgb>& frame, uint8_t note) {
    LedRange r = ledsForNote(note, LedMapConfig{});
    return frame[r.first];
}

bool isBlack(Rgb c) { return !c.r && !c.g && !c.b; }
bool isWhiteish(Rgb c) { return c.r == c.g && c.g == c.b && c.r > 0; }

const Rgb kRight = Settings{}.rightColor;

}  // namespace

void test_gap_fill_value_lerps_across_the_gap() {
    PlaybackEngine e;
    setupEngine(e, repeatSong(), "follow");
    // Gap = [500ms, 1000ms). At 750ms elapsedFrac = 0.5 → pct = 0.225.
    runTo(e, 750000);
    Rgb c = ledAt(e.renderFrame(1000 + 750000), 60);
    TEST_ASSERT_TRUE_MESSAGE(isWhiteish(c), "fill is repeatColor (white)");
    uint8_t expect = static_cast<uint8_t>(255 * 0.45f * 0.5f + 0.5f);
    TEST_ASSERT_UINT8_WITHIN(3, expect, c.r);
    // Near the onset the fill approaches the peak (45%).
    runTo(e, 995000);
    Rgb nearOn = ledAt(e.renderFrame(1000 + 995000), 60);
    uint8_t peak = static_cast<uint8_t>(255 * 0.45f + 0.5f);
    TEST_ASSERT_UINT8_WITHIN(4, peak, nearOn.r);
}

void test_onset_is_punctual_and_due_overwrites_fill() {
    PlaybackEngine e;
    setupEngine(e, repeatSong(), "follow");
    runTo(e, 1000000);  // exactly the second onset
    Rgb c = ledAt(e.renderFrame(1000 + 1000000), 60);
    TEST_ASSERT_EQUAL_UINT8(kRight.g, c.g);  // full hand color, no delay
    TEST_ASSERT_EQUAL_UINT8(0, c.r);         // fill's white is gone
}

void test_sub_floor_gap_borrows_from_the_outgoing_tail() {
    PlaybackEngine e;
    setupEngine(e, borrowSong(), "follow");
    // Off at 500ms, re-onset at 525ms (25ms gap < 35ms floor):
    // fillStart = 525 - 35 = 490ms — BEFORE the off. At 495ms the note is
    // still sounding but its tail yields to the fill.
    runTo(e, 495000);
    Rgb c = ledAt(e.renderFrame(1000 + 495000), 60);
    TEST_ASSERT_TRUE_MESSAGE(isWhiteish(c), "borrowed tail shows the fill");
    // Well before the borrow window the note is an ordinary due light.
    PlaybackEngine e2;
    setupEngine(e2, borrowSong(), "follow");
    runTo(e2, 300000);
    Rgb due = ledAt(e2.renderFrame(1000 + 300000), 60);
    TEST_ASSERT_EQUAL_UINT8(kRight.g, due.g);
    TEST_ASSERT_EQUAL_UINT8(0, due.r);
}

void test_pathological_gap_collapses_to_one_event() {
    PlaybackEngine e;
    setupEngine(e, pathologySong(), "follow");
    // Inside the 2ms gap window (and the would-be borrow window) the light
    // must stay a continuous due light — no white fill, no flicker.
    runTo(e, 495000);
    Rgb c = ledAt(e.renderFrame(1000 + 495000), 60);
    TEST_ASSERT_EQUAL_UINT8(kRight.g, c.g);
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
}

void test_first_onset_on_a_key_has_no_fill() {
    PlaybackEngine e;
    setupEngine(e, repeatSong(), "follow");
    runTo(e, 200000);  // first note sounding; no fill anywhere before it
    Rgb c = ledAt(e.renderFrame(1000 + 200000), 60);
    TEST_ASSERT_EQUAL_UINT8(kRight.g, c.g);
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
}

void test_seek_clears_repeat_state_no_phantom_fill() {
    PlaybackEngine e;
    setupEngine(e, repeatSong(), "follow");
    runTo(e, 750000);  // fill active mid-gap
    TEST_ASSERT_TRUE(isWhiteish(ledAt(e.renderFrame(1000 + 750000), 60)));
    // Seek back to 100ms — inside the FIRST note, not in any gap. A stale
    // cursor would keep painting the old fill.
    gOut.clear();
    e.transport("seek", 100, gOut);
    e.transport("play", 0, gOut);
    e.tick(2000000, gOut);  // re-baseline
    const std::vector<Rgb>& f = e.renderFrame(2000000);
    Rgb c = ledAt(f, 60);
    TEST_ASSERT_FALSE_MESSAGE(isWhiteish(c) && c.r < 200,
                              "no phantom fill after seek");
}

void test_loop_wrap_clears_repeat_state() {
    PlaybackEngine e;
    setupEngine(e, repeatSong(), "follow");
    TEST_ASSERT_TRUE(e.setLoop(true, 0, 400));  // loop inside the first note
    runTo(e, 380000);
    runTo(e, 420000);  // wraps back near 20ms
    uint64_t pos = e.positionUs();
    TEST_ASSERT_TRUE(pos < 400000);
    Rgb c = ledAt(e.renderFrame(1000 + 420000), 60);
    TEST_ASSERT_EQUAL_UINT8(kRight.g, c.g);  // due light, not fill
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
}

void test_cross_hand_same_key_re_press_still_cues() {
    PlaybackEngine e;
    setupEngine(e, crossHandSong(), "follow");
    runTo(e, 750000);  // gap between t0's off (500ms) and t1's on (1000ms)
    Rgb c = ledAt(e.renderFrame(1000 + 750000), 60);
    TEST_ASSERT_TRUE_MESSAGE(isWhiteish(c),
                             "cue is per KEY, not per track");
}

void test_disabled_cue_renders_v1_identical_frames() {
    PlaybackEngine e;
    setupEngine(e, repeatSong(), "follow");
    RepeatCueConfig off;
    off.enabled = false;
    e.setRepeatCue(off);
    runTo(e, 750000);
    const std::vector<Rgb>& f = e.renderFrame(1000 + 750000);
    // Mid-gap with the cue off: only the ramp preview may show (dim hand
    // color), never white fill.
    Rgb c = ledAt(f, 60);
    TEST_ASSERT_FALSE(isWhiteish(c) && c.r > 0);
    TEST_ASSERT_EQUAL_UINT8(0, c.r);  // no red/white component at all
}

void test_wait_mode_gets_no_crescendo_fill() {
    PlaybackEngine e;
    setupEngine(e, repeatSong(), "wait");
    // Barrier holds at 0; the fill machinery must stay out of wait mode
    // (Q2's pulse handles re-dues there).
    runTo(e, 200000);
    Rgb c = ledAt(e.renderFrame(1000 + 200000), 60);
    TEST_ASSERT_EQUAL_UINT8(kRight.g, c.g);  // the due chord at 100%
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gap_fill_value_lerps_across_the_gap);
    RUN_TEST(test_onset_is_punctual_and_due_overwrites_fill);
    RUN_TEST(test_sub_floor_gap_borrows_from_the_outgoing_tail);
    RUN_TEST(test_pathological_gap_collapses_to_one_event);
    RUN_TEST(test_first_onset_on_a_key_has_no_fill);
    RUN_TEST(test_seek_clears_repeat_state_no_phantom_fill);
    RUN_TEST(test_loop_wrap_clears_repeat_state);
    RUN_TEST(test_cross_hand_same_key_re_press_still_cues);
    RUN_TEST(test_disabled_cue_renders_v1_identical_frames);
    RUN_TEST(test_wait_mode_gets_no_crescendo_fill);
    return UNITY_END();
}
