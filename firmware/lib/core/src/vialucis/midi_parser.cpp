#include "vialucis/midi_parser.h"

#include <algorithm>
#include <memory>

namespace vialucis {
namespace {

// Bounds-checked big-endian reader over the raw file bytes.
class Reader {
public:
    Reader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }
    size_t remaining() const { return ok_ ? len_ - pos_ : 0; }

    uint8_t u8() {
        if (pos_ + 1 > len_) { ok_ = false; return 0; }
        return data_[pos_++];
    }

    uint16_t u16() {
        uint16_t hi = u8(), lo = u8();
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 8) | u8();
        return v;
    }

    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t b = u8();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        ok_ = false;  // VLQ longer than spec allows
        return v;
    }

    bool match4(const char* tag) {
        if (pos_ + 4 > len_) { ok_ = false; return false; }
        bool m = data_[pos_] == static_cast<uint8_t>(tag[0]) &&
                 data_[pos_ + 1] == static_cast<uint8_t>(tag[1]) &&
                 data_[pos_ + 2] == static_cast<uint8_t>(tag[2]) &&
                 data_[pos_ + 3] == static_cast<uint8_t>(tag[3]);
        pos_ += 4;
        return m;
    }

    void skip(size_t n) {
        if (pos_ + n > len_) { ok_ = false; return; }
        pos_ += n;
    }

    std::string str(size_t n) {
        if (pos_ + n > len_) { ok_ = false; return {}; }
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

private:
    const uint8_t* data_;
    size_t len_;
    size_t pos_ = 0;
    bool ok_ = true;
};

struct OpenNote {
    uint32_t onTick;
    uint8_t velocity;
};

// One slot per (channel, note); overlapping same-note re-triggers close the
// previous instance first, which is the sane behavior for a trainer.
struct NoteTracker {
    OpenNote open[16][128];
    bool active[16][128] = {};

    void on(uint8_t ch, uint8_t note, uint8_t vel, uint32_t tick,
            uint8_t track, std::vector<MidiNote>& out) {
        if (active[ch][note]) off(ch, note, tick, track, out);
        active[ch][note] = true;
        open[ch][note] = {tick, vel};
    }

    void off(uint8_t ch, uint8_t note, uint32_t tick, uint8_t track,
             std::vector<MidiNote>& out) {
        if (!active[ch][note]) return;  // spurious note-off: ignore
        active[ch][note] = false;
        out.push_back({open[ch][note].onTick, tick, note,
                       open[ch][note].velocity, ch, track});
    }

    void closeAll(uint32_t tick, uint8_t track, std::vector<MidiNote>& out) {
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                if (active[ch][n]) off(static_cast<uint8_t>(ch),
                                       static_cast<uint8_t>(n), tick,
                                       track, out);
    }
};

MidiParseError parseTrack(Reader& r, uint8_t trackIndex, MidiSong& song) {
    if (!r.match4("MTrk")) return r.ok() ? MidiParseError::BadTrack
                                         : MidiParseError::Truncated;
    uint32_t chunkLen = r.u32();
    if (!r.ok() || chunkLen > r.remaining()) return MidiParseError::Truncated;
    size_t end = r.pos() + chunkLen;

    // A181: NoteTracker is 16x128 slots = ~18 KB — bigger than the whole
    // 16 KB async_tcp stack this parser runs on for HTTP song load and the
    // songs-list parse check (stack overflow proven live 2026-07-16; the
    // route shadowing fixed in Wave A means this path never executed
    // on-device before). Heap-allocate it; the A180 read guard's margin
    // already keeps this allocation safe alongside the file buffer.
    auto tracker_ = std::make_unique<NoteTracker>();
    NoteTracker& tracker = *tracker_;
    uint32_t tick = 0;
    uint8_t runningStatus = 0;
    song.tracks.push_back({});

    while (r.pos() < end && r.ok()) {
        tick += r.vlq();
        uint8_t b = r.u8();
        if (!r.ok()) break;

        if (b == 0xFF) {  // meta event
            uint8_t type = r.u8();
            uint32_t len = r.vlq();
            if (!r.ok() || len > r.remaining()) return MidiParseError::Truncated;
            if (type == 0x2F) {  // End of Track
                r.skip(len);
                break;
            } else if (type == 0x51 && len == 3) {  // Set Tempo
                uint32_t us = (static_cast<uint32_t>(r.u8()) << 16);
                us |= (static_cast<uint32_t>(r.u8()) << 8);
                us |= r.u8();
                song.tempo.push_back({tick, us});
            } else if (type == 0x03) {  // Track Name
                if (song.tracks[trackIndex].name.empty())
                    song.tracks[trackIndex].name = r.str(len);
                else
                    r.skip(len);
            } else {
                r.skip(len);
            }
            runningStatus = 0;  // meta/sysex cancel running status
            continue;
        }

        if (b == 0xF0 || b == 0xF7) {  // SysEx: length-prefixed, skip
            uint32_t len = r.vlq();
            if (!r.ok() || len > r.remaining()) return MidiParseError::Truncated;
            r.skip(len);
            runningStatus = 0;
            continue;
        }

        uint8_t status, data1;
        if (b & 0x80) {
            status = b;
            runningStatus = status;
            data1 = r.u8();
        } else {
            if (!(runningStatus & 0x80)) return MidiParseError::Truncated;
            status = runningStatus;
            data1 = b;
        }

        uint8_t kind = status & 0xF0;
        uint8_t ch = status & 0x0F;
        switch (kind) {
            case 0x80: {  // note off
                r.u8();   // release velocity, unused
                tracker.off(ch, data1 & 0x7F, tick, trackIndex, song.notes);
                break;
            }
            case 0x90: {  // note on (velocity 0 == off)
                uint8_t vel = r.u8();
                if (vel == 0)
                    tracker.off(ch, data1 & 0x7F, tick, trackIndex, song.notes);
                else
                    tracker.on(ch, data1 & 0x7F, vel, tick, trackIndex,
                               song.notes);
                break;
            }
            case 0xB0: {  // control change — keep CC64 only
                uint8_t value = r.u8();
                if (data1 == 64)
                    song.pedal.push_back({tick, value, ch, trackIndex});
                break;
            }
            case 0xA0:    // poly aftertouch (2 data bytes)
            case 0xE0:    // pitch bend (2 data bytes)
                r.u8();
                break;
            case 0xC0:    // program change (1 data byte)
            case 0xD0:    // channel aftertouch (1 data byte)
                break;
            default:
                return MidiParseError::Truncated;  // unknown status: bail safely
        }
        if (!r.ok()) return MidiParseError::Truncated;
    }

    if (!r.ok() || r.pos() > end) return MidiParseError::Truncated;
    tracker.closeAll(tick, trackIndex, song.notes);
    r.skip(end - r.pos());  // tolerate events after EoT inside the declared chunk
    return MidiParseError::Ok;
}

}  // namespace

MidiParseResult parseMidi(const uint8_t* data, size_t len) {
    MidiParseResult result;
    Reader r(data, len);

    if (data == nullptr || len < 14 || !r.match4("MThd")) {
        result.error = MidiParseError::NotMidi;
        return result;
    }
    uint32_t headerLen = r.u32();
    if (headerLen < 6) {
        result.error = MidiParseError::NotMidi;
        return result;
    }
    r.u16();  // format (0/1 both handled identically; 2 parses as-is)
    uint16_t ntrks = r.u16();
    uint16_t division = r.u16();
    r.skip(headerLen - 6);
    if (!r.ok()) {
        result.error = MidiParseError::NotMidi;
        return result;
    }
    if (division & 0x8000) {
        result.error = MidiParseError::SmpteDivision;
        return result;
    }
    result.song.ticksPerQuarter = division;

    for (uint16_t i = 0; i < ntrks; ++i) {
        MidiParseError err = parseTrack(r, static_cast<uint8_t>(i), result.song);
        if (err != MidiParseError::Ok) {
            result.error = err;
            return result;
        }
    }

    std::stable_sort(result.song.notes.begin(), result.song.notes.end(),
                     [](const MidiNote& a, const MidiNote& b) {
                         return a.onTick < b.onTick;
                     });
    std::stable_sort(result.song.tempo.begin(), result.song.tempo.end(),
                     [](const TempoChange& a, const TempoChange& b) {
                         return a.tick < b.tick;
                     });
    std::stable_sort(result.song.pedal.begin(), result.song.pedal.end(),
                     [](const PedalEvent& a, const PedalEvent& b) {
                         return a.tick < b.tick;
                     });
    return result;
}

uint64_t tickToMicros(const MidiSong& song, uint32_t tick) {
    uint64_t us = 0;
    uint32_t curTick = 0;
    uint32_t usPerQuarter = 500000;  // MIDI default, 120 bpm
    uint16_t tpq = song.ticksPerQuarter ? song.ticksPerQuarter : 480;

    for (const TempoChange& tc : song.tempo) {
        if (tc.tick >= tick) break;
        if (tc.tick > curTick) {
            us += static_cast<uint64_t>(tc.tick - curTick) * usPerQuarter / tpq;
            curTick = tc.tick;
        }
        usPerQuarter = tc.usPerQuarter;
    }
    us += static_cast<uint64_t>(tick - curTick) * usPerQuarter / tpq;
    return us;
}

}  // namespace vialucis
