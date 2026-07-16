// Scratch diagnostic (NOT a suite member): run checkMidi + parseMidi over
// every real .mid in a directory, both buffer and chunked sources, printing
// per-file outcomes — hunting a live-device infinite loop. Delete after use.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "vialucis/midi_parser.h"

using namespace vialucis;

namespace {
// 256-byte chunked source mimicking FileByteSource's gulp size.
class ChunkSource : public ByteSource {
public:
    ChunkSource(const std::vector<uint8_t>& d) : d_(d) {}
    size_t size() const override { return d_.size(); }
    size_t read(uint8_t* dst, size_t max) override {
        size_t take = max < 256 ? max : 256;
        if (pos_ >= d_.size()) return 0;
        if (take > d_.size() - pos_) take = d_.size() - pos_;
        memcpy(dst, d_.data() + pos_, take);
        pos_ += take;
        return take;
    }
    void reset() override { pos_ = 0; }

private:
    const std::vector<uint8_t>& d_;
    size_t pos_ = 0;
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: probe <file.mid> [more...]\n"); return 2; }
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary);
        if (!f) { printf("SKIP unreadable %s\n", argv[i]); continue; }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
        printf("CHECK  %-60s %7zu B ... ", argv[i], data.size());
        fflush(stdout);
        ChunkSource src(data);
        MidiParseError ce = checkMidi(src, SIZE_MAX);
        printf("check=%d ... ", static_cast<int>(ce));
        fflush(stdout);
        ChunkSource src2(data);
        MidiParseResult r = parseMidi(src2, SIZE_MAX);
        printf("parse=%d notes=%zu\n", static_cast<int>(r.error),
               r.song.notes.size());
        fflush(stdout);
    }
    printf("ALL DONE\n");
    return 0;
}
