// REC3 — the recording capture subsystem wired into ModeDirector: the Record
// top-mode + gating, the tap after the wait-mode verdict, the echo-feed scan
// over the emission path, the Free-capture Record frame source (monitor +
// heartbeat + count-in), and the AFK interaction.

#include <unity.h>

#include <string>
#include <vector>

#include "../helpers/fx_asserts.h"
#include "../helpers/test_songs.h"
#include "vialucis/key_led_table.h"
#include "vialucis/midi_capture.h"
#include "vialucis/mode_director.h"
#include "vialucis/playback_engine.h"
#include "vialucis/settings.h"

using namespace vialucis;
using fxtest::litCount;
using testsongs::chordSong;

void setUp() {}
void tearDown() {}

namespace {

std::vector<MidiOutMsg> gOut;
constexpr uint64_t kSec = 1000000ull;

struct Rig {
    PlaybackEngine engine;
    ModeDirector director{engine, 360};
    Rig() {
        engine.configure(Settings{});
        director.setTable(TableBuilder::fromTwoPoint(LedMapConfig{}));
    }
    void load() {
        gOut.clear();
        engine.loadSong(chordSong(), "t.mid", gOut);
    }
    void tick(uint64_t nowUs) {
        gOut.clear();
        director.tick(nowUs, gOut);
    }
};

int countNote(const CaptureTake& t, uint8_t note) {
    int n = 0;
    for (const SmfNoteEvent& e : t.notes)
        if (e.note == note) ++n;
    return n;
}

}  // namespace

// --- gate matrix -----------------------------------------------------------

void test_arm_no_song_enters_record_mode() {
    Rig r;
    r.tick(1 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(1 * kSec) == TopMode::Reactive);
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      r.director.armRecord(4096, false, 90, 1 * kSec));
    // Arming with no song loaded IS the Record top-mode (before a note plays).
    TEST_ASSERT_TRUE(r.director.topMode(1 * kSec) == TopMode::Record);
    // The Free-capture frame lights the strip (heartbeat at least).
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(1 * kSec)) > 0);
}

void test_arm_song_loaded_stays_practice_and_captures() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Practice);
    // Arming with a song loaded is Play-along: mode stays Practice.
    TEST_ASSERT_EQUAL(ArmResult::Armed,
                      r.director.armRecord(4096, false, 90, 2 * kSec));
    TEST_ASSERT_TRUE(r.director.topMode(2 * kSec) == TopMode::Practice);
    // ...but the tape head still records the presses alongside practice.
    r.director.onKeyDown(55, 90, 2 * kSec + 1000);
    r.director.onKeyUp(55, 2 * kSec + 100000);
    r.director.onKeyDown(57, 90, 2 * kSec + 200000);
    r.director.onKeyUp(57, 2 * kSec + 300000);
    CaptureTake take = r.director.stopRecord();
    TEST_ASSERT_FALSE(take.empty);
    TEST_ASSERT_EQUAL_INT(1, countNote(take, 55));
    TEST_ASSERT_EQUAL_INT(1, countNote(take, 57));
}

void test_afk_disarmed_while_armed() {
    Rig r;
    r.tick(1 * kSec);
    r.director.armRecord(4096, false, 90, 1 * kSec);
    // Well past the 180s idle timeout with a take armed: NEVER AFK — arming
    // is write activity and Record outranks AFK.
    r.tick(400 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(400 * kSec) == TopMode::Record);
    // Stop (empty take) → back to the no-song states. The REST layer (App)
    // resets the idle clock on the stop route just like unload does; drive
    // that write-activity and the strip is back in the Reactive monitor.
    r.director.stopRecord();
    r.director.onWriteActivity(400 * kSec);
    TEST_ASSERT_TRUE(r.director.topMode(400 * kSec) == TopMode::Reactive);
}

void test_stop_finalizes_take_with_pressed_notes() {
    Rig r;
    r.tick(1 * kSec);
    r.director.armRecord(4096, false, 90, 1 * kSec);
    r.director.onKeyDown(60, 100, 1 * kSec + 5000);
    r.director.onKeyUp(60, 1 * kSec + 500000);
    r.director.onKeyDown(64, 90, 1 * kSec + 600000);
    r.director.onKeyUp(64, 1 * kSec + 900000);
    CaptureTake take = r.director.stopRecord();
    TEST_ASSERT_FALSE(take.empty);
    TEST_ASSERT_EQUAL_size_t(2, take.notes.size());
    TEST_ASSERT_EQUAL_INT(1, countNote(take, 60));
    TEST_ASSERT_EQUAL_INT(1, countNote(take, 64));
    TEST_ASSERT_EQUAL(CaptureState::Idle, r.director.recordState());
}

// --- echo exclusion through the director seam ------------------------------

void test_emitted_note_echo_excluded_through_director() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    r.director.armRecord(4096, false, 90, 1 * kSec);
    // Demo mode so the engine emits the song's notes TO the piano.
    gOut.clear();
    r.engine.setMode("demo", "both", gOut);
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    // First playing tick emits note 60 (onset 0) into `out`; the director
    // scans it into capture's OWN echo guard (one credit for 60).
    r.tick(1 * kSec + 10000);
    // The piano echoes 60 back as a key press within the window ⇒ excluded,
    // and it does NOT start the clock.
    r.director.onKeyDown(60, 90, 1 * kSec + 20000);
    TEST_ASSERT_EQUAL(CaptureState::Armed, r.director.recordState());
    // A genuine re-press of 60 beyond the one credit ⇒ captured (starts clock).
    r.director.onKeyDown(60, 90, 1 * kSec + 120000);
    TEST_ASSERT_EQUAL(CaptureState::Recording, r.director.recordState());
    r.director.onKeyUp(60, 1 * kSec + 300000);
    CaptureTake take = r.director.stopRecord();
    TEST_ASSERT_EQUAL_INT(1, countNote(take, 60));  // the genuine press only
}

// --- a full capture buffer never affects the wait-mode verdict -------------

void test_full_buffer_never_affects_wait_verdict() {
    Rig r;
    r.tick(1 * kSec);
    r.load();
    gOut.clear();
    r.engine.setMode("wait", "both", gOut);
    // Budget for only 4 events — trivially overflowed.
    r.director.armRecord(4 * sizeof(CaptureEvent), false, 90, 1 * kSec);
    gOut.clear();
    r.engine.transport("play", 0, gOut);
    r.tick(2 * kSec);
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[60]") !=
                     std::string::npos);
    // Hammer 20 WRONG presses (21..40, never 60) — overflows capture.
    for (int i = 0; i < 20; ++i) {
        r.director.onKeyDown(static_cast<uint8_t>(21 + i), 90,
                             2 * kSec + (i + 1) * 1000);
        r.director.onKeyUp(static_cast<uint8_t>(21 + i),
                           2 * kSec + (i + 1) * 1000 + 500);
    }
    TEST_ASSERT_EQUAL(CaptureStatus::Overflowed, r.director.recordStatus());
    // The verdict path is untouched: still waiting for exactly 60.
    TEST_ASSERT_TRUE(r.engine.statusJson().find("\"pendingNotes\":[60]") !=
                     std::string::npos);
    // The correct note still clears the barrier — the frame/verdict path is
    // uncorrupted by the overflowing tape head.
    r.director.onKeyDown(60, 100, 3 * kSec);
    r.tick(3 * kSec + 10000);
    TEST_ASSERT_TRUE(r.engine.positionUs() > 0);
}

// --- count-in renders only in Free capture ---------------------------------

void test_count_in_pulse_only_in_free_capture() {
    Rig r;
    r.tick(1 * kSec);
    // Free capture, count-in ON at 120 BPM (beat 500ms, bar 2000ms).
    r.director.armRecord(4096, true, 120, 1 * kSec);
    TEST_ASSERT_TRUE(r.director.recordCountIn());
    // 10ms into beat 0: a bright pulse lights the whole strip.
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(1 * kSec + 10000)) > 300);
    // After the 1-bar (2000ms) count-in, the pulse is gone — only the
    // single heartbeat pixel remains (no keys pressed).
    TEST_ASSERT_TRUE(litCount(r.director.renderFrame(1 * kSec + 2100000)) <= 2);

    // Song loaded: a count-in request is IGNORED (Play-along has the song as
    // tempo) and the frame source is Practice, never the Record pulse.
    Rig r2;
    r2.tick(1 * kSec);
    r2.load();
    r2.director.armRecord(4096, true, 120, 2 * kSec);
    TEST_ASSERT_FALSE(r2.director.recordCountIn());
    TEST_ASSERT_TRUE(r2.director.topMode(2 * kSec) == TopMode::Practice);
}

// --- the heartbeat pixel: outside the key range, not red/blue/green --------

void test_heartbeat_pixel_outside_key_range_and_not_rbg() {
    Rig r;
    KeyLedTable tbl = TableBuilder::fromTwoPoint(LedMapConfig{});
    r.director.setTable(tbl);
    r.tick(1 * kSec);
    r.director.armRecord(4096, false, 90, 1 * kSec);
    uint16_t hb = r.director.recordHeartbeatLed();

    uint16_t minLed = 0xFFFF, maxLed = 0;
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n) {
        LedRange rg = tbl.forNoteOrdered(n);
        if (!rg.valid) continue;
        if (rg.first < minLed) minLed = rg.first;
        if (rg.last > maxLed) maxLed = rg.last;
    }
    TEST_ASSERT_TRUE(hb < minLed || hb > maxLed);  // outside every key

    const std::vector<Rgb>& f = r.director.renderFrame(1 * kSec + 500000);
    Rgb px = f[hb];
    // Warm amber: red present, green present but dimmer, no blue — never a
    // pure red / blue / green that would read as a practice color.
    TEST_ASSERT_EQUAL_UINT8(0, px.b);
    TEST_ASSERT_TRUE(px.r > 0);
    TEST_ASSERT_TRUE(px.g > 0 && px.g < px.r);
}

// --- discard ---------------------------------------------------------------

void test_discard_returns_to_idle() {
    Rig r;
    r.tick(1 * kSec);
    r.director.armRecord(4096, false, 90, 1 * kSec);
    r.director.onKeyDown(60, 90, 1 * kSec + 1000);
    TEST_ASSERT_EQUAL(CaptureState::Recording, r.director.recordState());
    TEST_ASSERT_TRUE(r.director.topMode(1 * kSec + 2000) == TopMode::Record);
    r.director.discardRecord();
    TEST_ASSERT_EQUAL(CaptureState::Idle, r.director.recordState());
    TEST_ASSERT_FALSE(r.director.recordCountIn());
    TEST_ASSERT_TRUE(r.director.topMode(1 * kSec + 2000) == TopMode::Reactive);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_arm_no_song_enters_record_mode);
    RUN_TEST(test_arm_song_loaded_stays_practice_and_captures);
    RUN_TEST(test_afk_disarmed_while_armed);
    RUN_TEST(test_stop_finalizes_take_with_pressed_notes);
    RUN_TEST(test_emitted_note_echo_excluded_through_director);
    RUN_TEST(test_full_buffer_never_affects_wait_verdict);
    RUN_TEST(test_count_in_pulse_only_in_free_capture);
    RUN_TEST(test_heartbeat_pixel_outside_key_range_and_not_rbg);
    RUN_TEST(test_discard_returns_to_idle);
    return UNITY_END();
}
