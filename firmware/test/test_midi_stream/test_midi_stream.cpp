// Streaming-parse equivalence + memory-budget pins (A185).
//
// The single most important guard here is the CORPUS EQUIVALENCE SWEEP: every
// committed conformance fixture is parsed twice — once through the in-RAM byte
// buffer (the path the 374-check corpus already pins) and once through a
// chunk-feeding fake ByteSource that hands the parser only a few bytes at a
// time — and the two MidiSong outputs are asserted byte-for-byte identical.
// That proves the streaming Reader (which refills a small fixed buffer from the
// source) reproduces the random-access parser exactly, across arbitrary short
// reads (chunk sizes 1, 2, 3, 7, 64, 512) and the two-pass reset().
//
// Then: exact-budget pins (over-budget ⇒ TooBigForMemory, hands back nothing;
// exact boundary ⇒ Ok), pedals+tempo counted in the budget, and a printed
// fits-table (item 5) quantifying what a 64 KB / 90 KB maxAllocHeap now loads.
//
// Runs with cwd = firmware/ (where pio is invoked); fixtures live at
// ../corpus/midi/.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../helpers/smf_builder.h"
#include "vialucis/midi_parser.h"
#include "vialucis/storage_budget.h"  // parseNoteBudget / kParseHeapMarginBytes

using namespace vialucis;
using smf::Bytes;

void setUp() {}
void tearDown() {}

namespace {

// A ByteSource that yields at most `chunk` bytes per read() — the file/streaming
// stand-in. A tiny chunk maximally exercises the Reader's refill loop.
class ChunkByteSource : public ByteSource {
public:
    ChunkByteSource(const uint8_t* d, size_t n, size_t chunk)
        : d_(d), n_(n), chunk_(chunk) {}
    size_t size() const override { return n_; }
    size_t read(uint8_t* dst, size_t max) override {
        size_t avail = n_ - pos_;
        size_t take = avail < chunk_ ? avail : chunk_;
        if (take > max) take = max;
        if (take) std::memcpy(dst, d_ + pos_, take);
        pos_ += take;
        return take;
    }
    void reset() override { pos_ = 0; }

private:
    const uint8_t* d_;
    size_t n_;
    size_t chunk_;
    size_t pos_ = 0;
};

const char* kCorpusDir = "../corpus/midi/";
const char* kFixtures[] = {
    "conductor-first", "named-lh-rh",      "ambiguous-name",
    "anonymous-pair",  "three-hands",      "pedal-only-track",
    "tie-order",       "vlq-tempo",        "recording-shaped",
};

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void assertSongsEqual(const MidiSong& a, const MidiSong& b, const char* ctx) {
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(a.ticksPerQuarter, b.ticksPerQuarter, ctx);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(a.notes.size(), b.notes.size(), ctx);
    for (size_t i = 0; i < a.notes.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a.notes[i].onTick, b.notes[i].onTick, ctx);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a.notes[i].offTick, b.notes[i].offTick, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.notes[i].note, b.notes[i].note, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.notes[i].velocity, b.notes[i].velocity, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.notes[i].channel, b.notes[i].channel, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.notes[i].track, b.notes[i].track, ctx);
    }
    TEST_ASSERT_EQUAL_size_t_MESSAGE(a.tempo.size(), b.tempo.size(), ctx);
    for (size_t i = 0; i < a.tempo.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a.tempo[i].tick, b.tempo[i].tick, ctx);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a.tempo[i].usPerQuarter,
                                         b.tempo[i].usPerQuarter, ctx);
    }
    TEST_ASSERT_EQUAL_size_t_MESSAGE(a.pedal.size(), b.pedal.size(), ctx);
    for (size_t i = 0; i < a.pedal.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a.pedal[i].tick, b.pedal[i].tick, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.pedal[i].value, b.pedal[i].value, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.pedal[i].channel, b.pedal[i].channel, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a.pedal[i].track, b.pedal[i].track, ctx);
    }
    TEST_ASSERT_EQUAL_size_t_MESSAGE(a.tracks.size(), b.tracks.size(), ctx);
    for (size_t i = 0; i < a.tracks.size(); ++i)
        TEST_ASSERT_EQUAL_STRING_MESSAGE(a.tracks[i].name.c_str(),
                                         b.tracks[i].name.c_str(), ctx);
}

// A synthetic file with exactly n note-on/off pairs (n emitted notes), no
// tempo or pedal — a clean knob for the budget boundary pins.
Bytes manyNotes(size_t n) {
    Bytes ev;
    for (size_t i = 0; i < n; ++i) {
        smf::noteOn(ev, 0, 0, static_cast<uint8_t>(60 + (i % 12)), 100);
        smf::noteOff(ev, 10, 0, static_cast<uint8_t>(60 + (i % 12)));
    }
    Bytes file = smf::header(0, 1, 480);
    smf::append(file, smf::track(ev));
    return file;
}

// ---- THE equivalence sweep: whole corpus, buffer vs chunk source ----------
void test_corpus_streaming_equivalence() {
    const size_t chunks[] = {1, 2, 3, 7, 64, 512};
    for (const char* stem : kFixtures) {
        std::string bytes = readAll(std::string(kCorpusDir) + stem + ".mid");
        std::string ctx = std::string("fixture ") + stem +
                          " (run pio from firmware/)";
        TEST_ASSERT_MESSAGE(!bytes.empty(), ctx.c_str());
        const uint8_t* d = reinterpret_cast<const uint8_t*>(bytes.data());

        MidiParseResult ref = parseMidi(d, bytes.size());
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(MidiParseError::Ok),
                                  static_cast<int>(ref.error), ctx.c_str());

        for (size_t chunk : chunks) {
            ChunkByteSource src(d, bytes.size(), chunk);
            MidiParseResult got = parseMidi(src);
            TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(MidiParseError::Ok),
                                      static_cast<int>(got.error), ctx.c_str());
            assertSongsEqual(ref.song, got.song, ctx.c_str());
            // checkMidi (count-only pass) agrees the file parses + fits.
            ChunkByteSource src2(d, bytes.size(), chunk);
            TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(MidiParseError::Ok),
                                      static_cast<int>(checkMidi(src2)),
                                      ctx.c_str());
        }
    }
}

// Parsing the SAME source twice must reproduce it exactly — proves reset()
// rewinds cleanly (the two-pass parse relies on it).
void test_source_reusable_across_parses() {
    Bytes f = manyNotes(50);
    ChunkByteSource src(f.data(), f.size(), 7);
    MidiParseResult a = parseMidi(src);
    MidiParseResult b = parseMidi(src);  // reset() inside must re-read from top
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Ok), static_cast<int>(a.error));
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Ok), static_cast<int>(b.error));
    assertSongsEqual(a.song, b.song, "reused source");
    TEST_ASSERT_EQUAL_size_t(50, b.song.notes.size());
}

// ---- exact-budget pins ----------------------------------------------------
void test_exact_budget_boundary() {
    Bytes f = manyNotes(200);
    size_t need = midiParseOutputBytes(200, 0, 0);
    TEST_ASSERT_EQUAL_size_t(200 * sizeof(MidiNote), need);

    // Exactly the need: Ok, all 200 notes.
    MidiParseResult ok = parseMidi(f.data(), f.size(), need);
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Ok), static_cast<int>(ok.error));
    TEST_ASSERT_EQUAL_size_t(200, ok.song.notes.size());

    // One byte short: TooBigForMemory, and NOTHING handed back (no gamble).
    MidiParseResult over = parseMidi(f.data(), f.size(), need - 1);
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::TooBigForMemory),
                      static_cast<int>(over.error));
    TEST_ASSERT_EQUAL_size_t(0, over.song.notes.size());
    TEST_ASSERT_EQUAL_size_t(0, over.song.tracks.size());

    // Zero budget refuses any non-empty song.
    MidiParseResult zero = parseMidi(f.data(), f.size(), 0);
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::TooBigForMemory),
                      static_cast<int>(zero.error));

    // Default (SIZE_MAX) is unchanged from the old no-budget behavior.
    MidiParseResult dflt = parseMidi(f.data(), f.size());
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Ok), static_cast<int>(dflt.error));
    TEST_ASSERT_EQUAL_size_t(200, dflt.song.notes.size());
}

// checkMidi enforces the same boundary without allocating the notes.
void test_checkmidi_budget_boundary() {
    Bytes f = manyNotes(200);
    size_t need = midiParseOutputBytes(200, 0, 0);
    ChunkByteSource s1(f.data(), f.size(), 512);
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Ok),
                      static_cast<int>(checkMidi(s1, need)));
    ChunkByteSource s2(f.data(), f.size(), 512);
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::TooBigForMemory),
                      static_cast<int>(checkMidi(s2, need - 1)));
}

// The budget counts pedals and tempo too, not just notes.
void test_budget_counts_pedal_and_tempo() {
    Bytes ev;
    smf::tempoMeta(ev, 0, 500000);
    smf::tempoMeta(ev, 480, 400000);
    smf::cc(ev, 0, 0, 64, 127);   // CC64 pedal down
    smf::cc(ev, 240, 0, 64, 0);   // CC64 pedal up
    smf::noteOn(ev, 0, 0, 60, 100);
    smf::noteOff(ev, 480, 0, 60);
    Bytes f = smf::header(0, 1, 480);
    smf::append(f, smf::track(ev));

    size_t need = midiParseOutputBytes(/*notes=*/1, /*tempo=*/2, /*pedal=*/2);
    MidiParseResult ok = parseMidi(f.data(), f.size(), need);
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Ok), static_cast<int>(ok.error));
    TEST_ASSERT_EQUAL_size_t(1, ok.song.notes.size());
    TEST_ASSERT_EQUAL_size_t(2, ok.song.tempo.size());
    TEST_ASSERT_EQUAL_size_t(2, ok.song.pedal.size());

    // Exactly one PedalEvent short of the need is refused (pedals count).
    MidiParseResult over =
        parseMidi(f.data(), f.size(), need - sizeof(PedalEvent));
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::TooBigForMemory),
                      static_cast<int>(over.error));
}

// A too-big verdict never masks a corrupt file: an unparseable input reports
// its real error even under a zero budget (the count pass fails first).
void test_corrupt_beats_budget() {
    Bytes junk = {'X', 'Y', 'Z', 'Z', 'Y'};
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::NotMidi),
                      static_cast<int>(parseMidi(junk.data(), junk.size(), 0).error));
    Bytes truncated = manyNotes(20);
    truncated.resize(truncated.size() - 5);  // cut into the track body
    TEST_ASSERT_EQUAL(static_cast<int>(MidiParseError::Truncated),
                      static_cast<int>(parseMidi(truncated.data(),
                                                 truncated.size(), 0).error));
}

// ---- item 5: the fits-table (measured, printed for the report) ------------
void test_report_fits_table() {
    const size_t fixed = midiParseFixedOverhead();
    std::printf("\n=== A185 streaming-parse fits-table ===\n");
    std::printf("sizeof(MidiNote)=%zu  sizeof(PedalEvent)=%zu  "
                "sizeof(TempoChange)=%zu  fixedOverhead(NoteTracker)=%zu B  "
                "margin=%zu B\n",
                sizeof(MidiNote), sizeof(PedalEvent), sizeof(TempoChange), fixed,
                kParseHeapMarginBytes);
    const size_t heaps[] = {64 * 1024, 90 * 1024};
    for (size_t maxAlloc : heaps) {
        size_t budget = parseNoteBudget(maxAlloc, fixed);
        size_t maxNotes = budget / sizeof(MidiNote);
        // File-size estimates at representative encoded densities (bytes of SMF
        // per emitted note): ~4 = dense running-status, ~8 = typical two-hand.
        std::printf("maxAllocHeap=%zu KB -> note budget=%zu B -> max %zu notes "
                    "(~%zu KB file @4 B/note, ~%zu KB file @8 B/note)\n",
                    maxAlloc / 1024, budget, maxNotes, maxNotes * 4 / 1024,
                    maxNotes * 8 / 1024);
    }
    // Old A182 factor-4 hard-reject line, for contrast: it refused any file
    // whose bytes*4 + 16 KB exceeded maxAlloc — i.e. ~12 KB at 64 KB heap.
    std::printf("(old A182 factor-4 guard refused files > ~%zu KB at 64 KB "
                "heap; half the 47-song library false-badged)\n",
                (64u * 1024 - 16 * 1024) / 4 / 1024);
    std::printf("=======================================\n");

    // Also record the per-fixture density we measured, so the report cites real
    // corpus numbers, not just formulas.
    for (const char* stem : kFixtures) {
        std::string bytes = readAll(std::string(kCorpusDir) + stem + ".mid");
        if (bytes.empty()) continue;
        MidiParseResult r =
            parseMidi(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        size_t notes = r.song.notes.size();
        std::printf("  %-18s %5zu B  %3zu notes  %3zu tempo  %3zu pedal\n",
                    stem, bytes.size(), notes, r.song.tempo.size(),
                    r.song.pedal.size());
    }
    TEST_PASS();
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_corpus_streaming_equivalence);
    RUN_TEST(test_source_reusable_across_parses);
    RUN_TEST(test_exact_budget_boundary);
    RUN_TEST(test_checkmidi_budget_boundary);
    RUN_TEST(test_budget_counts_pedal_and_tempo);
    RUN_TEST(test_corrupt_beats_budget);
    RUN_TEST(test_report_fits_table);
    return UNITY_END();
}
