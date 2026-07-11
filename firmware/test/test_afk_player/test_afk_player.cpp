// E3 — the AFK playlist sequencer: media-player semantics over effect
// tracks, crossfade continuity, caps and range masks.

#include <unity.h>

#include <cstdlib>
#include <vector>

#include "../helpers/fx_asserts.h"
#include "vialucis/fx/afk_player.h"
#include "vialucis/key_led_map.h"

using namespace vialucis;
using fx::AfkConfig;
using fx::AfkPlayer;
using fx::AfkTrack;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint16_t kLeds = 120;

using fxtest::litCount;
using fxtest::maxChan;

int maxFrameDelta(const std::vector<Rgb>& a, const std::vector<Rgb>& b) {
    int worst = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(int(a[i].r) - int(b[i].r)));
        worst = std::max(worst, std::abs(int(a[i].g) - int(b[i].g)));
        worst = std::max(worst, std::abs(int(a[i].b) - int(b[i].b)));
    }
    return worst;
}

}  // namespace

void test_empty_playlist_falls_back_to_stub() {
    AfkPlayer p(kLeds);
    p.setConfig(AfkConfig{}, 7);  // no tracks
    std::vector<Rgb> f(kLeds);
    for (int i = 0; i < 5; ++i) p.render(f);
    TEST_ASSERT_TRUE(litCount(f) > 0);  // the fallback effect paints
}

void test_unknown_effect_name_renders_fallback_not_crash() {
    AfkPlayer p(kLeds);
    AfkConfig c;
    c.tracks.push_back({"definitely-not-an-effect", ""});
    p.setConfig(c, 7);
    std::vector<Rgb> f(kLeds);
    for (int i = 0; i < 5; ++i) p.render(f);
    TEST_ASSERT_TRUE(litCount(f) > 0);
}

void test_dwell_zero_is_clamped() {
    AfkPlayer p(kLeds);
    AfkConfig c;
    c.tracks.push_back({"colorwaves", ""});
    c.tracks.push_back({"fire2012", ""});
    c.dwellSec = 0;  // config bug: clamped to >= 5s, not instant flipping
    p.setConfig(c, 7);
    TEST_ASSERT_TRUE(p.config().dwellSec >= 5);
    std::vector<Rgb> f(kLeds);
    for (int i = 0; i < 10; ++i) p.render(f);
    TEST_ASSERT_EQUAL_size_t(0, p.currentTrack());  // still on track 0
}

void test_shuffle_with_a_single_track_repeats_it() {
    AfkPlayer p(kLeds);
    AfkConfig c;
    c.tracks.push_back({"colorwaves", ""});
    c.shuffle = true;
    c.dwellSec = 5;
    p.setConfig(c, 7);
    std::vector<Rgb> f(kLeds);
    // Render far past several dwell periods: only track 0 exists to play.
    for (int i = 0; i < 5 * 1000 / 16 * 3; ++i) p.render(f);
    TEST_ASSERT_EQUAL_size_t(0, p.currentTrack());
    TEST_ASSERT_TRUE(litCount(f) > 0);
}

void test_crossfade_has_no_snap_frame() {
    // Two SMOOTH effects: intrinsic per-frame motion is small, so any
    // hard cut at the track boundary would dominate the frame delta. (A
    // sparky effect like fire2012 jumps by design — wrong yardstick.)
    AfkPlayer p(kLeds);
    AfkConfig c;
    c.tracks.push_back({"colorwaves", ""});
    c.tracks.push_back({"pacifica", ""});
    c.dwellSec = 5;
    c.crossfadeMs = 2000;
    p.setConfig(c, 7);
    std::vector<Rgb> prev(kLeds), cur(kLeds);
    // Measure the intrinsic (steady-state) jumpiness first.
    int intrinsic = 0;
    p.render(prev);
    for (int i = 0; i < 60; ++i) {
        p.render(cur);
        intrinsic = std::max(intrinsic, maxFrameDelta(prev, cur));
        prev = cur;
    }
    int worstJump = 0;
    // Past the FIRST fade's completion (~dwell frames total incl. the
    // warmup above) but well before the SECOND fade wraps back to track 0.
    int frames = 350;
    for (int i = 0; i < frames; ++i) {
        p.render(cur);
        worstJump = std::max(worstJump, maxFrameDelta(prev, cur));
        prev = cur;
    }
    TEST_ASSERT_EQUAL_size_t(1, p.currentTrack());  // it DID switch
    // A hard cut would jump by ~a full channel swing in one frame; the
    // fade must stay in the same league as the effects' own motion.
    TEST_ASSERT_TRUE_MESSAGE(worstJump <= intrinsic + 24,
                             "no snap frame in the fade");
}

void test_brightness_cap_bounds_every_channel() {
    AfkPlayer p(kLeds);
    AfkConfig c;
    c.tracks.push_back({"colorwaves", ""});
    c.brightnessCap = 64;
    p.setConfig(c, 7);
    std::vector<Rgb> f(kLeds);
    for (int i = 0; i < 30; ++i) {
        p.render(f);
        TEST_ASSERT_TRUE(maxChan(f) <= 64);
    }
}

void test_above_keys_only_masks_outside_the_span() {
    AfkPlayer p(kLeds);
    // A table whose keys live in LEDs 20..79 of a 120-LED strip.
    KeyLedTable t;
    t.setLedCount(kLeds);
    t.set(21, LedRange{20, 21, true});
    t.set(108, LedRange{78, 79, true});
    p.setTable(t);
    AfkConfig c;
    c.tracks.push_back({"colorwaves", ""});
    c.aboveKeysOnly = true;
    p.setConfig(c, 7);
    std::vector<Rgb> f(kLeds);
    for (int i = 0; i < 5; ++i) p.render(f);
    for (uint16_t i = 0; i < 20; ++i)
        TEST_ASSERT_TRUE(!f[i].r && !f[i].g && !f[i].b);
    for (uint16_t i = 80; i < kLeds; ++i)
        TEST_ASSERT_TRUE(!f[i].r && !f[i].g && !f[i].b);
    int lit = 0;
    for (uint16_t i = 20; i < 80; ++i)
        if (f[i].r || f[i].g || f[i].b) ++lit;
    TEST_ASSERT_TRUE(lit > 0);
}

void test_manual_next_previous_move_tracks() {
    AfkPlayer p(kLeds);
    AfkConfig c;
    c.tracks.push_back({"colorwaves", ""});
    c.tracks.push_back({"fire2012", ""});
    c.tracks.push_back({"pacifica", ""});
    p.setConfig(c, 7);
    p.next();
    TEST_ASSERT_EQUAL_size_t(1, p.currentTrack());
    p.next();
    TEST_ASSERT_EQUAL_size_t(2, p.currentTrack());
    p.next();  // wraps
    TEST_ASSERT_EQUAL_size_t(0, p.currentTrack());
    p.previous();
    TEST_ASSERT_EQUAL_size_t(2, p.currentTrack());
}

void test_repeat_current_never_advances() {
    AfkPlayer p(kLeds);
    AfkConfig c;
    c.tracks.push_back({"colorwaves", ""});
    c.tracks.push_back({"fire2012", ""});
    c.dwellSec = 5;
    c.repeatCurrent = true;
    p.setConfig(c, 7);
    std::vector<Rgb> f(kLeds);
    for (int i = 0; i < 5 * 1000 / 16 * 3; ++i) p.render(f);
    TEST_ASSERT_EQUAL_size_t(0, p.currentTrack());
}

void test_config_json_round_trip_and_typed_errors() {
    AfkConfig c;
    c.tracks.push_back({"fire2012", "heat"});
    c.tracks.push_back({"colorwaves", "ocean"});
    c.shuffle = true;
    c.dwellSec = 45;
    c.brightnessCap = 120;
    c.masterSpeed = 1.5f;
    c.aboveKeysOnly = true;
    std::string j = fx::afkConfigToJson(c);
    AfkConfig back;
    std::string err;
    TEST_ASSERT_TRUE(fx::afkConfigFromJson(j.c_str(), back, &err));
    TEST_ASSERT_EQUAL_size_t(2, back.tracks.size());
    TEST_ASSERT_EQUAL_STRING("fire2012", back.tracks[0].effect.c_str());
    TEST_ASSERT_EQUAL_STRING("ocean", back.tracks[1].palette.c_str());
    TEST_ASSERT_TRUE(back.shuffle);
    TEST_ASSERT_EQUAL_UINT32(45, back.dwellSec);
    TEST_ASSERT_EQUAL_UINT8(120, back.brightnessCap);
    TEST_ASSERT_TRUE(back.aboveKeysOnly);

    AfkConfig bad;
    TEST_ASSERT_FALSE(fx::afkConfigFromJson("not json{{", bad, &err));
    TEST_ASSERT_TRUE(err.length() > 0);
    TEST_ASSERT_FALSE(fx::afkConfigFromJson(
        "{\"tracks\":[{\"effect\":\"nope\"}]}", bad, &err));
    TEST_ASSERT_TRUE(err.find("nope") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_playlist_falls_back_to_stub);
    RUN_TEST(test_unknown_effect_name_renders_fallback_not_crash);
    RUN_TEST(test_dwell_zero_is_clamped);
    RUN_TEST(test_shuffle_with_a_single_track_repeats_it);
    RUN_TEST(test_crossfade_has_no_snap_frame);
    RUN_TEST(test_brightness_cap_bounds_every_channel);
    RUN_TEST(test_above_keys_only_masks_outside_the_span);
    RUN_TEST(test_manual_next_previous_move_tracks);
    RUN_TEST(test_repeat_current_never_advances);
    RUN_TEST(test_config_json_round_trip_and_typed_errors);
    return UNITY_END();
}
