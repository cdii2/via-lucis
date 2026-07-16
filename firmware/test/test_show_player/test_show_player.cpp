// P1 — ShowPlayer at its own seam (renderAt() directly, architecture-review
// candidate 3). test_show covers the parser plus blend math / z-order /
// clip-local determinism; THIS suite pins the compositor behaviours that had
// no direct assertions: backward-seek reset determinism, positive scope-mask
// coverage (which LEDs may light, not just "off-table is dark"), the
// mask × blend interaction, and note-bind cursor semantics (rewind on seek,
// offs firing, retrigger at a shortened off).
//
// Shows are built as parsed structs, not bytes — load()/renderAt() is the
// surface under test, Show::parse has its own suite.

#include <unity.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "../helpers/fx_asserts.h"
#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/key_led_table.h"
#include "vialucis/show.h"
#include "vialucis/show_player.h"

using namespace vialucis;
using fxtest::litCount;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kStep = fx::kFxStepMs;  // renderAt's expected cadence

KeyLedTable makeTable() {
    return TableBuilder::fromTwoPoint(LedMapConfig{});  // 360 LEDs
}

uint8_t peakAt(const std::vector<Rgb>& leds, const KeyLedTable& t,
               uint8_t note) {
    LedRange r = t.forNote(note);
    if (!r.valid) return 0;
    uint8_t peak = 0;
    for (uint16_t i = r.first; i <= r.last && i < leds.size(); ++i) {
        const Rgb& c = leds[i];
        if (c.r > peak) peak = c.r;
        if (c.g > peak) peak = c.g;
        if (c.b > peak) peak = c.b;
    }
    return peak;
}

// A whole-strip, open-ended cue for effects[idx] (opacity blend, opaque).
ShowCue wholeStripCue(uint8_t effectIndex) {
    ShowCue c;
    c.effectIndex = effectIndex;
    c.endMs = 0xFFFFFFFFu;
    return c;
}

// Cues must arrive in the order Show::parse would leave them: stable-sorted
// by startMs. Every builder here appends in that order already.
Show makeShow(std::vector<std::string> effects, std::vector<ShowCue> cues) {
    Show s;
    s.meta.durationMs = 100000;
    s.effects = std::move(effects);
    s.cues = std::move(cues);
    return s;
}

ShowPlayer makePlayer(Show&& show, const KeyLedTable& table,
                      uint32_t seed = 7) {
    ShowPlayer p;
    p.load(std::move(show), table, seed);
    return p;
}

// The set of LEDs a key-scoped cue is allowed to light (union of the keys'
// ordered ranges) — mirrors buildMask's geometry read.
std::vector<uint8_t> allowedMask(const KeyLedTable& t,
                                 const std::vector<uint8_t>& notes) {
    std::vector<uint8_t> allowed(t.ledCount(), 0);
    for (uint8_t n : notes) {
        LedRange r = t.forNoteOrdered(n);
        if (!r.valid) continue;
        for (uint16_t i = r.first; i <= r.last && i < allowed.size(); ++i)
            allowed[i] = 1;
    }
    return allowed;
}

void assertLitSubset(const std::vector<Rgb>& frame,
                     const std::vector<uint8_t>& allowed) {
    for (size_t i = 0; i < frame.size(); ++i) {
        if (allowed[i]) continue;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, frame[i].r, "lit outside scope");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, frame[i].g, "lit outside scope");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, frame[i].b, "lit outside scope");
    }
}

// ------------------------------- tests -------------------------------------

// (a) Backward seek re-resets every effect: replaying the same frame cadence
// from 0 reproduces the first pass exactly (fire2012 carries RNG + heat
// state across frames, so this fails if the reset is skipped or partial).
void test_backward_seek_resets_effects() {
    KeyLedTable table = makeTable();
    ShowPlayer p = makePlayer(
        makeShow({"fire2012"}, {wholeStripCue(0)}), table);

    std::vector<Rgb> out(360), firstPass;
    for (uint32_t t = 0; t <= 320; t += kStep) p.renderAt(t, out);
    firstPass = out;
    TEST_ASSERT_TRUE(litCount(firstPass) > 0);

    for (uint32_t t = 336; t <= 640; t += kStep) p.renderAt(t, out);

    // Seek to 0 and replay the identical cadence.
    for (uint32_t t = 0; t <= 320; t += kStep) p.renderAt(t, out);
    for (int i = 0; i < 360; ++i) {
        TEST_ASSERT_EQUAL_UINT8(firstPass[i].r, out[i].r);
        TEST_ASSERT_EQUAL_UINT8(firstPass[i].g, out[i].g);
        TEST_ASSERT_EQUAL_UINT8(firstPass[i].b, out[i].b);
    }
}

// (b) Backward seek rewinds the bind cursor: a bound note fires again on the
// replay (stale cursor ⇒ the onset is silently skipped), and the frames
// before the onset are dark again (envelope cleared by the reset).
void test_backward_seek_rewinds_bind_cursor() {
    ShowCue c = wholeStripCue(0);
    c.drive = 1;
    c.scopeType = 3;
    c.binds = {{48u, uint8_t{60}}};
    KeyLedTable table = makeTable();
    ShowPlayer p = makePlayer(makeShow({"notedriven"}, {c}), table);

    std::vector<Rgb> out(360);
    for (uint32_t t = 0; t <= 32; t += kStep) p.renderAt(t, out);
    TEST_ASSERT_EQUAL_UINT8(0, peakAt(out, table, 60));
    p.renderAt(48, out);
    TEST_ASSERT_TRUE(peakAt(out, table, 60) > 0);
    for (uint32_t t = 64; t <= 480; t += kStep) p.renderAt(t, out);

    // Seek back: pre-onset dark, onset fires again.
    for (uint32_t t = 0; t <= 32; t += kStep) p.renderAt(t, out);
    TEST_ASSERT_EQUAL_UINT8(0, peakAt(out, table, 60));
    p.renderAt(48, out);
    TEST_ASSERT_TRUE(peakAt(out, table, 60) > 0);
}

// (c) Pitch-range scope (type 1): only LEDs belonging to keys 48..72 may
// light; keys inside the range do light (pacifica paints the whole strip,
// so the mask is the only thing keeping the rest dark).
void test_scope_mask_pitch_range() {
    ShowCue c = wholeStripCue(0);
    c.scopeType = 1;
    c.rangeLo = 48;
    c.rangeHi = 72;
    KeyLedTable table = makeTable();
    ShowPlayer p = makePlayer(makeShow({"pacifica"}, {c}), table);

    std::vector<Rgb> out(360);
    p.renderAt(320, out);

    std::vector<uint8_t> notes;
    for (uint8_t n = 48; n <= 72; ++n) notes.push_back(n);
    assertLitSubset(out, allowedMask(table, notes));
    TEST_ASSERT_TRUE(peakAt(out, table, 48) > 0);
    TEST_ASSERT_TRUE(peakAt(out, table, 60) > 0);
    TEST_ASSERT_TRUE(peakAt(out, table, 72) > 0);
    TEST_ASSERT_EQUAL_UINT8(0, peakAt(out, table, 47));
    TEST_ASSERT_EQUAL_UINT8(0, peakAt(out, table, 73));
}

// (d) Note-set scope (type 2): exactly the listed keys' LEDs may light.
void test_scope_mask_note_set() {
    ShowCue c = wholeStripCue(0);
    c.scopeType = 2;
    c.notes = {36, 60, 84};
    KeyLedTable table = makeTable();
    ShowPlayer p = makePlayer(makeShow({"pacifica"}, {c}), table);

    std::vector<Rgb> out(360);
    p.renderAt(320, out);

    assertLitSubset(out, allowedMask(table, {36, 60, 84}));
    TEST_ASSERT_TRUE(peakAt(out, table, 36) > 0);
    TEST_ASSERT_TRUE(peakAt(out, table, 60) > 0);
    TEST_ASSERT_TRUE(peakAt(out, table, 84) > 0);
    TEST_ASSERT_EQUAL_UINT8(0, peakAt(out, table, 50));
    TEST_ASSERT_EQUAL_UINT8(0, peakAt(out, table, 90));
}

// (e) Mask × blend: an additive cue scoped to a pitch range composites
// qadd8(base, scale8(over, op)) INSIDE the mask and leaves the base
// untouched outside it. Solo renders with the same effects table and seed
// derive the exact expected values (the test_show (i) technique, plus mask).
void test_masked_additive_blend() {
    KeyLedTable table = makeTable();
    const uint32_t songMs = 320;
    const uint8_t op = 200;

    // Solo scratch frames: same effects table ⇒ same per-index reset seeds.
    std::vector<Rgb> base(360), over(360);
    makePlayer(makeShow({"fire2012", "pacifica"}, {wholeStripCue(0)}), table)
        .renderAt(songMs, base);
    makePlayer(makeShow({"fire2012", "pacifica"}, {wholeStripCue(1)}), table)
        .renderAt(songMs, over);

    ShowCue top = wholeStripCue(1);
    top.blend = 1;
    top.opacity = op;
    top.scopeType = 1;
    top.rangeLo = 48;
    top.rangeHi = 72;
    ShowPlayer p = makePlayer(
        makeShow({"fire2012", "pacifica"}, {wholeStripCue(0), top}), table);

    std::vector<Rgb> out(360);
    p.renderAt(songMs, out);

    std::vector<uint8_t> notes;
    for (uint8_t n = 48; n <= 72; ++n) notes.push_back(n);
    std::vector<uint8_t> inMask = allowedMask(table, notes);
    for (int i = 0; i < 360; ++i) {
        if (inMask[i]) {
            TEST_ASSERT_EQUAL_UINT8(
                fx::qadd8(base[i].r, fx::scale8(over[i].r, op)), out[i].r);
            TEST_ASSERT_EQUAL_UINT8(
                fx::qadd8(base[i].g, fx::scale8(over[i].g, op)), out[i].g);
            TEST_ASSERT_EQUAL_UINT8(
                fx::qadd8(base[i].b, fx::scale8(over[i].b, op)), out[i].b);
        } else {
            TEST_ASSERT_EQUAL_UINT8(base[i].r, out[i].r);
            TEST_ASSERT_EQUAL_UINT8(base[i].g, out[i].g);
            TEST_ASSERT_EQUAL_UINT8(base[i].b, out[i].b);
        }
    }
}

// (f) A bound note's off event fires: onset + 300 ms hold + 400 ms release
// ⇒ the key is fully dark again well before 1000 ms. If offs never fire the
// glow holds forever and this fails.
void test_note_bind_off_goes_dark() {
    ShowCue c = wholeStripCue(0);
    c.drive = 1;
    c.scopeType = 3;
    c.binds = {{100u, uint8_t{60}}};
    KeyLedTable table = makeTable();
    ShowPlayer p = makePlayer(makeShow({"notedriven"}, {c}), table);

    std::vector<Rgb> out(360);
    uint8_t peak112 = 0, peak992 = 0;
    for (uint32_t t = 0; t <= 992; t += kStep) {
        p.renderAt(t, out);
        if (t == 112) peak112 = peakAt(out, table, 60);
        if (t == 992) peak992 = peakAt(out, table, 60);
    }
    TEST_ASSERT_TRUE(peak112 > 0);
    TEST_ASSERT_EQUAL_UINT8(0, peak992);
}

// (g) Retrigger at a shortened off: a second same-key onset at 300 ms cuts
// the first note's hold short — and the RE-STRUCK note must then be HELD at
// full level (down), not decaying. At 480 ms (inside the second hold) the
// key's peak equals the freshly-struck peak at 112 ms. This pins the off/on
// ordering at the tie: the off of the first instance precedes the new onset.
void test_note_bind_retrigger_at_shortened_off() {
    ShowCue c = wholeStripCue(0);
    c.drive = 1;
    c.scopeType = 3;
    c.binds = {{100u, uint8_t{60}}, {300u, uint8_t{60}}};
    KeyLedTable table = makeTable();
    ShowPlayer p = makePlayer(makeShow({"notedriven"}, {c}), table);

    std::vector<Rgb> out(360);
    uint8_t peakHeld = 0, peakRetrig = 0;
    for (uint32_t t = 0; t <= 480; t += kStep) {
        p.renderAt(t, out);
        if (t == 112) peakHeld = peakAt(out, table, 60);
        if (t == 480) peakRetrig = peakAt(out, table, 60);
    }
    TEST_ASSERT_TRUE(peakHeld > 0);
    TEST_ASSERT_EQUAL_UINT8(peakHeld, peakRetrig);
}

// (h) Palette reset (A158, §3-E item 6): a cue with paletteRef==0xFF ("effect
// default") must not inherit whatever palette an EARLIER cue sharing the
// same effectIndex left behind. allRed forces every colorwaves pixel to
// g==0 && b==0 (its palette interpolation never introduces a channel none
// of the entries carry) — if the SECOND cue's reset didn't run, every pixel
// would stay g==0,b==0 forever; a lit pixel with g>0 or b>0 once that cue
// takes over is proof the effect is back on its own rainbow default.
void test_palette_resets_to_effect_default_when_ref_is_0xff() {
    KeyLedTable table = makeTable();
    fx::Palette16 allRed;
    for (auto& e : allRed.entries) e = Rgb{255, 0, 0};

    ShowCue red = wholeStripCue(0);
    red.endMs = 160;
    red.paletteRef = 0;  // -> show.palettes[0], the all-red bank
    ShowCue def = wholeStripCue(0);
    def.startMs = 160;
    def.endMs = 0xFFFFFFFFu;
    def.paletteRef = 0xFF;  // "effect default" — must NOT inherit red

    Show s = makeShow({"colorwaves"}, {red, def});
    s.palettes = {allRed};
    ShowPlayer p = makePlayer(std::move(s), table);

    std::vector<Rgb> out(360);
    bool sawNonRedChannel = false;
    for (uint32_t t = 0; t <= 320; t += kStep) {
        p.renderAt(t, out);
        if (t >= 176) {  // comfortably inside the reset (def) cue's window
            for (const Rgb& c : out) {
                if (c.g > 0 || c.b > 0) { sawNonRedChannel = true; break; }
            }
        }
    }
    TEST_ASSERT_TRUE(sawNonRedChannel);
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_backward_seek_resets_effects);
    RUN_TEST(test_backward_seek_rewinds_bind_cursor);
    RUN_TEST(test_scope_mask_pitch_range);
    RUN_TEST(test_scope_mask_note_set);
    RUN_TEST(test_masked_additive_blend);
    RUN_TEST(test_note_bind_off_goes_dark);
    RUN_TEST(test_note_bind_retrigger_at_shortened_off);
    RUN_TEST(test_palette_resets_to_effect_default_when_ref_is_0xff);
    return UNITY_END();
}
