// The golden-corpus conformance suite (architecture-review candidate 1):
// corpus/shows/*.vls are baked BY THE EDITOR and committed together with the
// editor's own jsonTwin() as *.expected.json. This suite pins the firmware
// parser to that contract field-by-field: whatever the editor bakes, the
// device must read back exactly. tools/check_corpus.py pins vls_dump.py to
// the same fixtures; the editor selftest byte-pins the "minimal" fixture.
// A format change therefore fails one of the three gates until the corpus is
// regenerated (corpus/README.md) — cross-artifact drift becomes visible.
//
// Runs with cwd = firmware/ (where pio is invoked); fixtures live at
// ../corpus/shows/.

#include <unity.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "vialucis/show.h"

using namespace vialucis;

void setUp() {}
void tearDown() {}

namespace {

const char* kCorpusDir = "../corpus/shows/";

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

uint8_t blendFromName(const char* s) {
    if (std::strcmp(s, "opacity") == 0) return 0;
    if (std::strcmp(s, "additive") == 0) return 1;
    TEST_FAIL_MESSAGE("unknown blend name in twin");
    return 0xFF;
}

uint8_t driveFromName(const char* s) {
    if (std::strcmp(s, "autonomous") == 0) return 0;
    if (std::strcmp(s, "note-driven") == 0) return 1;
    TEST_FAIL_MESSAGE("unknown drive name in twin");
    return 0xFF;
}

uint8_t paletteRefFromName(const char* s) {
    if (std::strcmp(s, "default") == 0) return 0xFF;
    int n = 0;
    if (std::sscanf(s, "stock#%d", &n) == 1)
        return static_cast<uint8_t>(0x80 | n);
    if (std::sscanf(s, "custom#%d", &n) == 1) return static_cast<uint8_t>(n);
    TEST_FAIL_MESSAGE("unknown paletteRef name in twin");
    return 0;
}

JsonObjectConst sectionOfType(JsonArrayConst sections, int type) {
    for (JsonObjectConst s : sections)
        if (s["type"].as<int>() == type) return s;
    return JsonObjectConst();
}

void checkCue(const ShowCue& p, JsonObjectConst t) {
    TEST_ASSERT_EQUAL_UINT32(t["startMs"].as<uint32_t>(), p.startMs);
    if (t["endMs"].is<const char*>()) {
        TEST_ASSERT_EQUAL_STRING("open-ended", t["endMs"].as<const char*>());
        TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, p.endMs);
    } else {
        TEST_ASSERT_EQUAL_UINT32(t["endMs"].as<uint32_t>(), p.endMs);
    }
    TEST_ASSERT_EQUAL_UINT8(t["effectIndex"].as<int>(), p.effectIndex);
    TEST_ASSERT_EQUAL_UINT8(blendFromName(t["blend"].as<const char*>()),
                            p.blend);
    TEST_ASSERT_EQUAL_UINT8(t["opacity"].as<int>(), p.opacity);
    TEST_ASSERT_EQUAL_UINT8(driveFromName(t["drive"].as<const char*>()),
                            p.drive);
    TEST_ASSERT_EQUAL_UINT8(t["scopeType"].as<int>(), p.scopeType);
    TEST_ASSERT_EQUAL_UINT8(
        paletteRefFromName(t["paletteRef"].as<const char*>()), p.paletteRef);
    // Twin speed is the ratio (u8/16, whole ratios as ints) — re-scale.
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(std::lround(t["speed"].as<double>() * 16.0)),
        p.speed);
    TEST_ASSERT_EQUAL_UINT8(t["flags"].as<int>(), p.flags);

    if (p.scopeType == 1) {
        TEST_ASSERT_EQUAL_UINT8(t["scope"]["lo"].as<int>(), p.rangeLo);
        TEST_ASSERT_EQUAL_UINT8(t["scope"]["hi"].as<int>(), p.rangeHi);
    } else if (p.scopeType == 2) {
        JsonArrayConst notes = t["scope"]["notes"];
        TEST_ASSERT_EQUAL_size_t(notes.size(), p.notes.size());
        for (size_t i = 0; i < p.notes.size(); ++i)
            TEST_ASSERT_EQUAL_UINT8(notes[i].as<int>(), p.notes[i]);
    } else if (p.scopeType == 3) {
        JsonArrayConst bound = t["scope"]["bound"];
        TEST_ASSERT_EQUAL_size_t(t["scope"]["boundCount"].as<size_t>(),
                                 p.binds.size());
        TEST_ASSERT_EQUAL_size_t(bound.size(), p.binds.size());
        for (size_t i = 0; i < p.binds.size(); ++i) {
            TEST_ASSERT_EQUAL_UINT32(bound[i]["onsetMs"].as<uint32_t>(),
                                     p.binds[i].onsetMs);
            TEST_ASSERT_EQUAL_UINT8(bound[i]["note"].as<int>(),
                                    p.binds[i].note);
        }
    }
}

void checkFixture(const char* stem) {
    const std::string vls = readAll(std::string(kCorpusDir) + stem + ".vls");
    const std::string twinText =
        readAll(std::string(kCorpusDir) + stem + ".expected.json");
    TEST_ASSERT_MESSAGE(!vls.empty(),
                        "fixture .vls missing — run pio from firmware/");
    TEST_ASSERT_MESSAGE(!twinText.empty(), "fixture .expected.json missing");

    Show parsed;
    ShowResult r = Show::parse(
        reinterpret_cast<const uint8_t*>(vls.data()), vls.size(), parsed);
    TEST_ASSERT_TRUE_MESSAGE(r.ok(), r.message());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, twinText);
    TEST_ASSERT_TRUE_MESSAGE(!err, err.c_str());
    JsonArrayConst sections = doc["sections"].as<JsonArrayConst>();

    // META
    JsonObjectConst meta = sectionOfType(sections, 1);
    TEST_ASSERT_FALSE(meta.isNull());
    TEST_ASSERT_EQUAL_UINT8(meta["clockSource"].as<int>(),
                            parsed.meta.clockSource);
    TEST_ASSERT_EQUAL_UINT32(meta["durationMs"].as<uint32_t>(),
                             parsed.meta.durationMs);
    TEST_ASSERT_EQUAL_STRING(meta["showName"].as<const char*>(),
                             parsed.meta.name.c_str());
    if (parsed.meta.clockSource == 2) {
        TEST_ASSERT_TRUE_MESSAGE(!meta["followTrack"].isNull(),
                                 "twin lacks followTrack for clock 2");
        TEST_ASSERT_EQUAL_UINT8(meta["followTrack"].as<int>(),
                                parsed.meta.followTrack);
    }

    // EFFECTS
    JsonObjectConst eff = sectionOfType(sections, 2);
    TEST_ASSERT_FALSE(eff.isNull());
    JsonArrayConst effects = eff["effects"];
    TEST_ASSERT_EQUAL_size_t(effects.size(), parsed.effects.size());
    for (size_t i = 0; i < parsed.effects.size(); ++i)
        TEST_ASSERT_EQUAL_STRING(effects[i].as<const char*>(),
                                 parsed.effects[i].c_str());

    // PALETTES
    JsonObjectConst pal = sectionOfType(sections, 3);
    TEST_ASSERT_FALSE(pal.isNull());
    JsonArrayConst palettes = pal["palettes"];
    TEST_ASSERT_EQUAL_size_t(palettes.size(), parsed.palettes.size());
    for (size_t i = 0; i < parsed.palettes.size(); ++i) {
        JsonArrayConst entries = palettes[i];
        TEST_ASSERT_EQUAL_size_t(16, entries.size());
        for (size_t k = 0; k < 16; ++k) {
            TEST_ASSERT_EQUAL_UINT8(entries[k][0].as<int>(),
                                    parsed.palettes[i].entries[k].r);
            TEST_ASSERT_EQUAL_UINT8(entries[k][1].as<int>(),
                                    parsed.palettes[i].entries[k].g);
            TEST_ASSERT_EQUAL_UINT8(entries[k][2].as<int>(),
                                    parsed.palettes[i].entries[k].b);
        }
    }

    // CUES — the twin records stream order; Show::parse stable-sorts by
    // startMs. Apply the same stable sort to the twin before comparing.
    JsonObjectConst cuesSec = sectionOfType(sections, 4);
    TEST_ASSERT_FALSE(cuesSec.isNull());
    JsonArrayConst cues = cuesSec["cues"];
    TEST_ASSERT_EQUAL_size_t(cuesSec["count"].as<size_t>(), cues.size());
    TEST_ASSERT_EQUAL_size_t(cues.size(), parsed.cues.size());

    std::vector<JsonObjectConst> twinCues;
    for (JsonObjectConst c : cues) twinCues.push_back(c);
    std::stable_sort(twinCues.begin(), twinCues.end(),
                     [](JsonObjectConst a, JsonObjectConst b) {
                         return a["startMs"].as<uint32_t>() <
                                b["startMs"].as<uint32_t>();
                     });
    for (size_t i = 0; i < parsed.cues.size(); ++i)
        checkCue(parsed.cues[i], twinCues[i]);
}

void test_corpus_minimal() { checkFixture("minimal"); }
void test_corpus_kitchen_sink() { checkFixture("kitchen-sink"); }
void test_corpus_scorefollow() { checkFixture("scorefollow"); }
void test_corpus_scorefollow_auto() { checkFixture("scorefollow-auto"); }

}  // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_corpus_minimal);
    RUN_TEST(test_corpus_kitchen_sink);
    RUN_TEST(test_corpus_scorefollow);
    RUN_TEST(test_corpus_scorefollow_auto);
    return UNITY_END();
}
