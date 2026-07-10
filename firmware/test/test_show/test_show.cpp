// P1 — the device-side show core: the `.vls` cue-stream parser (show.h) and
// the presentation compositor (show_player.h). Tests build binary streams
// byte-by-byte per docs/SHOW-FORMAT.md §1 and pin parse results, blend math,
// clip-local determinism, and note-binding drive.

#include <unity.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "vialucis/fx/fx_color.h"
#include "vialucis/fx/fx_math.h"
#include "vialucis/key_led_table.h"
#include "vialucis/show.h"
#include "vialucis/show_player.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

namespace {

// -------- binary stream builder (writes bytes per SHOW-FORMAT §1) ----------

void le16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void le32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

std::vector<uint8_t> metaSection(uint8_t clock, uint32_t dur,
                                 const std::string& name) {
    std::vector<uint8_t> p;
    p.push_back(clock);
    le32(p, dur);
    for (char c : name) p.push_back(static_cast<uint8_t>(c));
    p.push_back(0);  // zero terminator
    return p;
}

std::vector<uint8_t> effectsSection(const std::vector<std::string>& names) {
    std::vector<uint8_t> p;
    p.push_back(static_cast<uint8_t>(names.size()));
    for (const std::string& n : names) {
        for (char c : n) p.push_back(static_cast<uint8_t>(c));
        p.push_back(0);
    }
    return p;
}

struct CueSpec {
    uint32_t startMs = 0, endMs = 0xFFFFFFFFu;
    uint8_t effectIndex = 0, blend = 0, opacity = 255, drive = 0;
    uint8_t scopeType = 0, paletteRef = 0xFF, speed = 16, flags = 0;
    uint8_t rangeLo = 0, rangeHi = 0;
    std::vector<uint8_t> notes;
    std::vector<std::pair<uint32_t, uint8_t>> binds;
};

void appendCue(std::vector<uint8_t>& p, const CueSpec& c) {
    le32(p, c.startMs);
    le32(p, c.endMs);
    p.push_back(c.effectIndex);
    p.push_back(c.blend);
    p.push_back(c.opacity);
    p.push_back(c.drive);
    p.push_back(c.scopeType);
    p.push_back(c.paletteRef);
    p.push_back(c.speed);
    p.push_back(c.flags);
    switch (c.scopeType) {
        case 1:
            p.push_back(c.rangeLo);
            p.push_back(c.rangeHi);
            break;
        case 2:
            p.push_back(static_cast<uint8_t>(c.notes.size()));
            for (uint8_t n : c.notes) p.push_back(n);
            break;
        case 3:
            le16(p, static_cast<uint16_t>(c.binds.size()));
            for (auto& nb : c.binds) {
                le32(p, nb.first);
                p.push_back(nb.second);
            }
            break;
        default:
            break;
    }
}

std::vector<uint8_t> cuesSection(const std::vector<CueSpec>& cues) {
    std::vector<uint8_t> p;
    le16(p, static_cast<uint16_t>(cues.size()));
    for (const CueSpec& c : cues) appendCue(p, c);
    return p;
}

struct StreamBuilder {
    uint8_t major = 1, minor = 0;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> sections;
    bool withEnd = true;

    void add(uint16_t type, std::vector<uint8_t> payload) {
        sections.emplace_back(type, std::move(payload));
    }
    std::vector<uint8_t> build() const {
        std::vector<uint8_t> b = {'V', 'L', 'S', '1'};
        b.push_back(major);
        b.push_back(minor);
        le16(b, static_cast<uint16_t>(sections.size() + (withEnd ? 1 : 0)));
        for (auto& s : sections) {
            le16(b, s.first);
            le32(b, static_cast<uint32_t>(s.second.size()));
            for (uint8_t x : s.second) b.push_back(x);
        }
        if (withEnd) {
            le16(b, 0xFFFF);
            le32(b, 0);
        }
        return b;
    }
};

// A standard well-formed 3-cue stream used by several tests.
StreamBuilder standardStream() {
    StreamBuilder sb;
    sb.add(1, metaSection(0, 5000, "test"));
    sb.add(2, effectsSection({"rainbow", "bpm", "notedriven"}));
    std::vector<CueSpec> cues;
    CueSpec c0;  // whole-strip rainbow
    c0.startMs = 0;
    c0.endMs = 2000;
    c0.effectIndex = 0;
    c0.blend = 0;
    c0.opacity = 200;
    c0.speed = 16;
    cues.push_back(c0);
    CueSpec c1;  // pitch-range bpm, additive
    c1.startMs = 1000;
    c1.endMs = 0xFFFFFFFFu;
    c1.effectIndex = 1;
    c1.blend = 1;
    c1.opacity = 128;
    c1.scopeType = 1;
    c1.rangeLo = 48;
    c1.rangeHi = 72;
    c1.paletteRef = 0x82;  // stock 2
    c1.speed = 32;
    cues.push_back(c1);
    CueSpec c2;  // note-binding notedriven
    c2.startMs = 1500;
    c2.endMs = 4000;
    c2.effectIndex = 2;
    c2.drive = 1;
    c2.scopeType = 3;
    c2.binds = {{100u, uint8_t{60}}, {400u, uint8_t{64}}};
    cues.push_back(c2);
    sb.add(4, cuesSection(cues));
    return sb;
}

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

int litCount(const std::vector<Rgb>& f) {
    int n = 0;
    for (const Rgb& c : f)
        if (c.r || c.g || c.b) ++n;
    return n;
}

// ------------------------------- tests -------------------------------------

// (a) round-trip: build a 3-cue stream, parse Ok, fields exact.
void test_round_trip() {
    std::vector<uint8_t> bytes = standardStream().build();
    Show show;
    ShowResult r = Show::parse(bytes.data(), bytes.size(), show);
    TEST_ASSERT_TRUE(r.ok());

    TEST_ASSERT_EQUAL_UINT8(0, show.meta.clockSource);
    TEST_ASSERT_EQUAL_UINT32(5000, show.meta.durationMs);
    TEST_ASSERT_EQUAL_STRING("test", show.meta.name.c_str());

    TEST_ASSERT_EQUAL_size_t(3, show.effects.size());
    TEST_ASSERT_EQUAL_STRING("rainbow", show.effects[0].c_str());
    TEST_ASSERT_EQUAL_STRING("bpm", show.effects[1].c_str());
    TEST_ASSERT_EQUAL_STRING("notedriven", show.effects[2].c_str());

    TEST_ASSERT_EQUAL_size_t(3, show.cues.size());  // sorted by startMs
    const ShowCue& a = show.cues[0];
    TEST_ASSERT_EQUAL_UINT32(0, a.startMs);
    TEST_ASSERT_EQUAL_UINT32(2000, a.endMs);
    TEST_ASSERT_EQUAL_UINT8(0, a.effectIndex);
    TEST_ASSERT_EQUAL_UINT8(0, a.blend);
    TEST_ASSERT_EQUAL_UINT8(200, a.opacity);
    TEST_ASSERT_EQUAL_UINT8(0, a.scopeType);

    const ShowCue& b = show.cues[1];
    TEST_ASSERT_EQUAL_UINT32(1000, b.startMs);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, b.endMs);
    TEST_ASSERT_EQUAL_UINT8(1, b.effectIndex);
    TEST_ASSERT_EQUAL_UINT8(1, b.blend);
    TEST_ASSERT_EQUAL_UINT8(128, b.opacity);
    TEST_ASSERT_EQUAL_UINT8(1, b.scopeType);
    TEST_ASSERT_EQUAL_UINT8(48, b.rangeLo);
    TEST_ASSERT_EQUAL_UINT8(72, b.rangeHi);
    TEST_ASSERT_EQUAL_UINT8(0x82, b.paletteRef);
    TEST_ASSERT_EQUAL_UINT8(32, b.speed);

    const ShowCue& c = show.cues[2];
    TEST_ASSERT_EQUAL_UINT32(1500, c.startMs);
    TEST_ASSERT_EQUAL_UINT8(2, c.effectIndex);
    TEST_ASSERT_EQUAL_UINT8(1, c.drive);
    TEST_ASSERT_EQUAL_UINT8(3, c.scopeType);
    TEST_ASSERT_EQUAL_size_t(2, c.binds.size());
    TEST_ASSERT_EQUAL_UINT32(100, c.binds[0].onsetMs);
    TEST_ASSERT_EQUAL_UINT8(60, c.binds[0].note);
    TEST_ASSERT_EQUAL_UINT32(400, c.binds[1].onsetMs);
    TEST_ASSERT_EQUAL_UINT8(64, c.binds[1].note);
}

// (b) truncated at every prefix length: no crash, always non-Ok (Truncated).
void test_truncated_prefixes() {
    std::vector<uint8_t> bytes = standardStream().build();
    for (size_t len = 1; len < bytes.size(); ++len) {
        Show show;
        ShowResult r = Show::parse(bytes.data(), len, show);
        TEST_ASSERT_FALSE(r.ok());
        TEST_ASSERT_EQUAL(ShowResult::Kind::Truncated, r.kind);
    }
    // The full stream still parses.
    Show ok;
    TEST_ASSERT_TRUE(Show::parse(bytes.data(), bytes.size(), ok).ok());
}

// (c) garbage magic ⇒ BadMagic.
void test_bad_magic() {
    std::vector<uint8_t> bytes = standardStream().build();
    bytes[0] = 'X';
    bytes[1] = 'X';
    Show show;
    ShowResult r = Show::parse(bytes.data(), bytes.size(), show);
    TEST_ASSERT_EQUAL(ShowResult::Kind::BadMagic, r.kind);
}

// (d) versionMajor 2 ⇒ NewerMajor with the exact message.
void test_newer_major() {
    StreamBuilder sb = standardStream();
    sb.major = 2;
    std::vector<uint8_t> bytes = sb.build();
    Show show;
    ShowResult r = Show::parse(bytes.data(), bytes.size(), show);
    TEST_ASSERT_EQUAL(ShowResult::Kind::NewerMajor, r.kind);
    TEST_ASSERT_EQUAL_STRING("show needs newer firmware", r.message());
}

// (e) unknown section type skipped, still Ok (and known data intact).
void test_unknown_section_skipped() {
    StreamBuilder sb;
    sb.add(1, metaSection(1, 1234, "hi"));
    sb.add(0x4321, {1, 2, 3, 4, 5});  // unknown section — must be skipped
    sb.add(2, effectsSection({"rainbow"}));
    std::vector<CueSpec> cues;
    CueSpec c;
    c.effectIndex = 0;
    cues.push_back(c);
    sb.add(4, cuesSection(cues));
    std::vector<uint8_t> bytes = sb.build();

    Show show;
    ShowResult r = Show::parse(bytes.data(), bytes.size(), show);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_EQUAL_UINT8(1, show.meta.clockSource);
    TEST_ASSERT_EQUAL_UINT32(1234, show.meta.durationMs);
    TEST_ASSERT_EQUAL_size_t(1, show.effects.size());
    TEST_ASSERT_EQUAL_size_t(1, show.cues.size());
}

// (f) unknown effect name ⇒ UnknownEffect.
void test_unknown_effect() {
    StreamBuilder sb;
    sb.add(1, metaSection(0, 100, "x"));
    sb.add(2, effectsSection({"rainbow", "nonsuch"}));
    std::vector<uint8_t> bytes = sb.build();
    Show show;
    ShowResult r = Show::parse(bytes.data(), bytes.size(), show);
    TEST_ASSERT_EQUAL(ShowResult::Kind::UnknownEffect, r.kind);
}

// (g) clockSource 2 ⇒ ScoreFollowUnsupported.
void test_score_follow_unsupported() {
    StreamBuilder sb;
    sb.add(1, metaSection(2, 100, "x"));
    sb.add(2, effectsSection({"rainbow"}));
    std::vector<uint8_t> bytes = sb.build();
    Show show;
    ShowResult r = Show::parse(bytes.data(), bytes.size(), show);
    TEST_ASSERT_EQUAL(ShowResult::Kind::ScoreFollowUnsupported, r.kind);
    TEST_ASSERT_EQUAL_STRING("score-follow not supported yet", r.message());
}

// (h) cue scoped to notes outside the table / beyond 88 keys renders dark.
void test_out_of_range_scope_dark() {
    StreamBuilder sb;
    sb.add(1, metaSection(0, 100, "x"));
    sb.add(2, effectsSection({"rainbow"}));
    std::vector<CueSpec> cues;
    CueSpec c;
    c.effectIndex = 0;
    c.scopeType = 2;
    c.notes = {200, 130, 5, 20};  // all outside 21..108 (off the table)
    cues.push_back(c);
    sb.add(4, cuesSection(cues));
    std::vector<uint8_t> bytes = sb.build();

    Show show;
    TEST_ASSERT_TRUE(Show::parse(bytes.data(), bytes.size(), show).ok());

    ShowPlayer player;
    player.load(std::move(show), makeTable(), 1);
    std::vector<Rgb> out(360);
    player.renderAt(50, out);  // must not crash
    TEST_ASSERT_EQUAL_INT(0, litCount(out));  // nothing in scope ⇒ all dark

    // A pitch range wholly beyond 88 keys is likewise dark, no crash.
    StreamBuilder sb2;
    sb2.add(1, metaSection(0, 100, "x"));
    sb2.add(2, effectsSection({"rainbow"}));
    std::vector<CueSpec> cues2;
    CueSpec c2;
    c2.effectIndex = 0;
    c2.scopeType = 1;
    c2.rangeLo = 120;
    c2.rangeHi = 127;
    cues2.push_back(c2);
    sb2.add(4, cuesSection(cues2));
    std::vector<uint8_t> b2 = sb2.build();
    Show show2;
    TEST_ASSERT_TRUE(Show::parse(b2.data(), b2.size(), show2).ok());
    ShowPlayer p2;
    p2.load(std::move(show2), makeTable(), 1);
    std::vector<Rgb> out2(360);
    p2.renderAt(50, out2);
    TEST_ASSERT_EQUAL_INT(0, litCount(out2));
}

// Helper: a single-cue whole-strip player (opacity blend), used to extract an
// effect's raw scratch (opacity 255 over black == scratch).
ShowPlayer soloPlayer(uint8_t effectIndex, uint8_t blend, uint8_t opacity,
                      uint8_t speed = 16) {
    StreamBuilder sb;
    sb.add(1, metaSection(0, 100000, "x"));
    sb.add(2, effectsSection({"rainbow", "bpm"}));
    std::vector<CueSpec> cues;
    CueSpec c;
    c.effectIndex = effectIndex;
    c.blend = blend;
    c.opacity = opacity;
    c.speed = speed;
    cues.push_back(c);
    sb.add(4, cuesSection(cues));
    std::vector<uint8_t> bytes = sb.build();
    Show show;
    Show::parse(bytes.data(), bytes.size(), show);
    ShowPlayer p;
    p.load(std::move(show), makeTable(), 7);
    return p;
}

// (i) compositor: additive qadd8 pinned, opacity-128 nblend pinned, and
// later-over-earlier order pinned.
void test_compositor_blends() {
    const uint32_t songMs = 320;

    // Solo renders to derive the exact expected composites.
    ShowPlayer s0 = soloPlayer(0, 0, 255);  // rainbow scratch
    ShowPlayer s1 = soloPlayer(1, 0, 255);  // bpm scratch
    std::vector<Rgb> base(360), over(360);
    s0.renderAt(songMs, base);
    s1.renderAt(songMs, over);

    auto buildCombo = [&](uint8_t topBlend, uint8_t topOpacity,
                          bool topFirst) {
        StreamBuilder sb;
        sb.add(1, metaSection(0, 100000, "x"));
        sb.add(2, effectsSection({"rainbow", "bpm"}));
        CueSpec bg;  // rainbow, whole strip, opaque base
        bg.effectIndex = 0;
        bg.blend = 0;
        bg.opacity = 255;
        CueSpec top;  // bpm on top
        top.effectIndex = 1;
        top.blend = topBlend;
        top.opacity = topOpacity;
        std::vector<CueSpec> cues;
        // Same startMs ⇒ stable sort preserves stream order (z-order).
        if (topFirst) {
            cues = {top, bg};
        } else {
            cues = {bg, top};
        }
        sb.add(4, cuesSection(cues));
        std::vector<uint8_t> bytes = sb.build();
        Show show;
        Show::parse(bytes.data(), bytes.size(), show);
        ShowPlayer p;
        p.load(std::move(show), makeTable(), 7);
        return p;
    };

    // Additive full-opacity: out = qadd8(base, scale8(over, 255)).
    {
        ShowPlayer p = buildCombo(1, 255, /*topFirst=*/false);
        std::vector<Rgb> out(360);
        p.renderAt(songMs, out);
        for (int i = 0; i < 360; ++i) {
            TEST_ASSERT_EQUAL_UINT8(
                fx::qadd8(base[i].r, fx::scale8(over[i].r, 255)), out[i].r);
            TEST_ASSERT_EQUAL_UINT8(
                fx::qadd8(base[i].g, fx::scale8(over[i].g, 255)), out[i].g);
            TEST_ASSERT_EQUAL_UINT8(
                fx::qadd8(base[i].b, fx::scale8(over[i].b, 255)), out[i].b);
        }
    }

    // Opacity blend at 128: out = nblend(base, over, 128).
    {
        ShowPlayer p = buildCombo(0, 128, /*topFirst=*/false);
        std::vector<Rgb> out(360);
        p.renderAt(songMs, out);
        for (int i = 0; i < 360; ++i) {
            Rgb ex = base[i];
            fx::nblend(ex, over[i], 128);
            TEST_ASSERT_EQUAL_UINT8(ex.r, out[i].r);
            TEST_ASSERT_EQUAL_UINT8(ex.g, out[i].g);
            TEST_ASSERT_EQUAL_UINT8(ex.b, out[i].b);
        }
    }

    // Later-over-earlier: bpm last with opaque opacity blend fully replaces
    // the rainbow base ⇒ combo == bpm solo.
    {
        ShowPlayer p = buildCombo(0, 255, /*topFirst=*/false);
        std::vector<Rgb> out(360);
        p.renderAt(songMs, out);
        for (int i = 0; i < 360; ++i) {
            TEST_ASSERT_EQUAL_UINT8(over[i].r, out[i].r);
            TEST_ASSERT_EQUAL_UINT8(over[i].g, out[i].g);
            TEST_ASSERT_EQUAL_UINT8(over[i].b, out[i].b);
        }
    }
    // Reverse the stream order: rainbow now on top ⇒ combo == rainbow solo.
    {
        ShowPlayer p = buildCombo(0, 255, /*topFirst=*/true);
        std::vector<Rgb> out(360);
        p.renderAt(songMs, out);
        for (int i = 0; i < 360; ++i) {
            TEST_ASSERT_EQUAL_UINT8(base[i].r, out[i].r);
            TEST_ASSERT_EQUAL_UINT8(base[i].g, out[i].g);
            TEST_ASSERT_EQUAL_UINT8(base[i].b, out[i].b);
        }
    }
}

// (j) autonomous clip-local time: same effect+seed at two different songMs
// with the same clip-local ms renders identical scratch (speed applied).
void test_clip_local_determinism() {
    // speed 32 (2×) at songMs 100 ⇒ clip 200; speed 16 (1×) at 200 ⇒ clip 200.
    ShowPlayer a = soloPlayer(0, 0, 255, /*speed=*/32);
    ShowPlayer b = soloPlayer(0, 0, 255, /*speed=*/16);
    std::vector<Rgb> outA(360), outB(360);
    a.renderAt(100, outA);
    b.renderAt(200, outB);
    for (int i = 0; i < 360; ++i) {
        TEST_ASSERT_EQUAL_UINT8(outA[i].r, outB[i].r);
        TEST_ASSERT_EQUAL_UINT8(outA[i].g, outB[i].g);
        TEST_ASSERT_EQUAL_UINT8(outA[i].b, outB[i].b);
    }
    TEST_ASSERT_TRUE(litCount(outA) > 0);  // non-trivial frame
}

// (k) note-binding drive: a type-3 cue fires its note exactly at onsetMs —
// the frame at the onset lights the key's LEDs; the frame before does not.
void test_note_binding_drive() {
    StreamBuilder sb;
    sb.add(1, metaSection(0, 100000, "x"));
    sb.add(2, effectsSection({"notedriven"}));
    std::vector<CueSpec> cues;
    CueSpec c;
    c.effectIndex = 0;
    c.drive = 1;
    c.scopeType = 3;
    c.speed = 16;
    c.binds = {{48u, uint8_t{60}}};  // onset at clip-local 48 ms
    cues.push_back(c);
    sb.add(4, cuesSection(cues));
    std::vector<uint8_t> bytes = sb.build();

    Show show;
    TEST_ASSERT_TRUE(Show::parse(bytes.data(), bytes.size(), show).ok());
    KeyLedTable table = makeTable();
    ShowPlayer player;
    player.load(std::move(show), table, 1);

    std::vector<Rgb> out(360);
    player.renderAt(0, out);
    player.renderAt(16, out);
    player.renderAt(32, out);  // frame before the onset
    TEST_ASSERT_EQUAL_UINT8(0, peakAt(out, table, 60));
    player.renderAt(48, out);  // frame at the onset
    TEST_ASSERT_TRUE(peakAt(out, table, 60) > 0);
}

// (l) parseCues count-DoS: a CUES section whose u16 count claims far more
// records than its bytes can hold must be rejected (BadCue) BEFORE any
// reserve — never trust the count into an allocation (P-wave closing review).
void test_cue_count_dos() {
    StreamBuilder sb;
    sb.add(1, metaSection(0, 100, "x"));
    sb.add(2, effectsSection({"rainbow"}));
    // CUES payload is JUST the u16 count = 0xFFFF, with zero cue bytes after.
    sb.add(4, std::vector<uint8_t>{0xFF, 0xFF});
    std::vector<uint8_t> bytes = sb.build();
    Show show;
    ShowResult r = Show::parse(bytes.data(), bytes.size(), show);
    TEST_ASSERT_EQUAL(ShowResult::Kind::BadCue, r.kind);
    TEST_ASSERT_EQUAL_size_t(0, show.cues.size());  // nothing reserved/parsed
}

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip);
    RUN_TEST(test_truncated_prefixes);
    RUN_TEST(test_bad_magic);
    RUN_TEST(test_newer_major);
    RUN_TEST(test_unknown_section_skipped);
    RUN_TEST(test_unknown_effect);
    RUN_TEST(test_score_follow_unsupported);
    RUN_TEST(test_out_of_range_scope_dark);
    RUN_TEST(test_compositor_blends);
    RUN_TEST(test_clip_local_determinism);
    RUN_TEST(test_note_binding_drive);
    RUN_TEST(test_cue_count_dos);
    return UNITY_END();
}
