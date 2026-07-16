#include "vialucis/midi_parser.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace vialucis {
namespace {

// ---------------------------------------------------------------------------
// Streaming Reader (A185): bounds-checked big-endian reader that pulls bytes
// FORWARD from a ByteSource through a small fixed refill buffer. The parser
// never seeks backward within a pass, so a 256-byte gulp buffer is all the RAM
// the file itself ever occupies — the whole-file RAM buffer is gone from the
// parse budget. pos() is the absolute byte offset consumed; remaining() is the
// bytes left of the source's declared size().
// ---------------------------------------------------------------------------
class Reader {
public:
    explicit Reader(ByteSource& src) : src_(src), len_(src.size()) {}

    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }
    size_t remaining() const { return ok_ ? len_ - pos_ : 0; }

    uint8_t u8() {
        int b = next();
        if (b < 0) { ok_ = false; return 0; }
        return static_cast<uint8_t>(b);
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
        if (remaining() < 4) { ok_ = false; return false; }
        bool m = true;
        for (int i = 0; i < 4; ++i)
            if (u8() != static_cast<uint8_t>(tag[i])) m = false;
        return m;  // consumes 4 bytes either way (mirrors the old random reader)
    }

    void skip(size_t n) {
        if (remaining() < n) { ok_ = false; return; }
        while (n > 0) {
            if (bufPos_ >= bufLen_ && !refill()) { ok_ = false; return; }
            size_t avail = bufLen_ - bufPos_;
            size_t take = n < avail ? n : avail;
            bufPos_ += take;
            pos_ += take;
            n -= take;
        }
    }

    std::string str(size_t n) {
        if (remaining() < n) { ok_ = false; return {}; }
        std::string s;
        s.reserve(n);
        for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>(u8()));
        return s;
    }

private:
    bool refill() {
        bufLen_ = src_.read(buf_, sizeof(buf_));
        bufPos_ = 0;
        return bufLen_ > 0;
    }
    int next() {
        if (bufPos_ >= bufLen_ && !refill()) return -1;
        ++pos_;
        return buf_[bufPos_++];
    }

    ByteSource& src_;
    size_t len_;
    size_t pos_ = 0;
    bool ok_ = true;
    uint8_t buf_[256];
    size_t bufLen_ = 0;
    size_t bufPos_ = 0;
};

// ---------------------------------------------------------------------------
// Concrete source over an in-RAM byte buffer — backs the (data, len) overload
// and every existing native caller.
// ---------------------------------------------------------------------------
class BufferByteSource : public ByteSource {
public:
    BufferByteSource(const uint8_t* data, size_t len) : data_(data), len_(len) {}
    size_t size() const override { return len_; }
    size_t read(uint8_t* dst, size_t max) override {
        size_t n = len_ - pos_;
        if (n > max) n = max;
        if (n && data_) {
            std::memcpy(dst, data_ + pos_, n);
            pos_ += n;
        }
        return n;
    }
    void reset() override { pos_ = 0; }

private:
    const uint8_t* data_;
    size_t len_;
    size_t pos_ = 0;
};

// Open-note tracking for the fill pass. A192: was a dense 16x128 slot array
// (~18 KB) — bigger than the device's ENTIRE max contiguous heap block once
// the piano's BLE connection is up (measured live: maxAlloc 17.4 KB with the
// FP-30X connected), so nothing could load exactly when the player needed it
// to. Real files hold a handful of simultaneously-open notes; track them in
// a small bounded list instead (kMaxOpenNotes × 8 B = 2 KB worst case,
// reserved once). A file exceeding the cap (pathological polyphony) is
// refused as TooBigForMemory — it could never play on this device anyway.
// closeAll emits in (channel, note) ascending order, byte-identical to the
// old dense iteration, so the conformance corpus stays exact.
struct NoteTracker {
    struct Open {
        uint8_t ch;
        uint8_t note;
        uint8_t velocity;
        uint32_t onTick;
    };
    static constexpr size_t kMaxOpenNotes = 256;
    std::vector<Open> open;
    bool overflowed = false;

    NoteTracker() { open.reserve(kMaxOpenNotes); }

    void on(uint8_t ch, uint8_t note, uint8_t vel, uint32_t tick,
            uint8_t track, std::vector<MidiNote>& out) {
        off(ch, note, tick, track, out);  // same-note retrigger closes first
        if (open.size() >= kMaxOpenNotes) {
            overflowed = true;  // refused upstream as TooBigForMemory
            return;
        }
        open.push_back({ch, note, vel, tick});
    }

    void off(uint8_t ch, uint8_t note, uint32_t tick, uint8_t track,
             std::vector<MidiNote>& out) {
        for (size_t i = 0; i < open.size(); ++i) {
            if (open[i].ch == ch && open[i].note == note) {
                out.push_back({open[i].onTick, tick, note, open[i].velocity,
                               ch, track});
                open.erase(open.begin() + static_cast<ptrdiff_t>(i));
                return;
            }
        }
        // spurious note-off: ignore (same as the dense version)
    }

    void closeAll(uint32_t tick, uint8_t track, std::vector<MidiNote>& out) {
        // Emit in (ch, note) ascending order — the dense array's iteration
        // order — so output stays byte-identical to pre-A192 parses.
        std::sort(open.begin(), open.end(), [](const Open& a, const Open& b) {
            return a.ch != b.ch ? a.ch < b.ch : a.note < b.note;
        });
        for (const Open& o : open)
            out.push_back({o.onTick, tick, o.note, o.velocity, o.ch, track});
        open.clear();
    }
};

// ---------------------------------------------------------------------------
// Sink: the ONE seam between counting and filling. The event walker
// (parseTrack) is mode-agnostic — it always reads the same bytes and routes
// every output through this sink, so the count pass can never diverge from the
// fill pass. Count mode only tallies; fill mode tallies AND emits into the
// (already exactly-reserved) song vectors via the NoteTracker.
//
// The emitted-note count equals the note-on(vel>0) count exactly: every on()
// opens one note, and every opened note is closed (emitted) exactly once — by a
// later note-off, a same-note retrigger's implicit close, or closeAll. So
// counting note-ons in pass 1 yields the precise reserve size for pass 2.
// ---------------------------------------------------------------------------
class Sink {
public:
    Sink() : counting_(true) {}  // count-only
    Sink(MidiSong& song, NoteTracker& tracker)
        : counting_(false), song_(&song), tracker_(&tracker) {}

    void beginTrack() {
        ++trackCount_;
        if (!counting_) song_->tracks.push_back({});
    }
    // Consumes `len` bytes either way, so both passes stay byte-aligned.
    void trackName(uint8_t idx, Reader& r, uint32_t len) {
        if (counting_) { r.skip(len); return; }
        if (song_->tracks[idx].name.empty())
            song_->tracks[idx].name = r.str(len);
        else
            r.skip(len);
    }
    void tempo(uint32_t tick, uint32_t us) {
        ++tempoCount_;
        if (!counting_) song_->tempo.push_back({tick, us});
    }
    void pedal(uint32_t tick, uint8_t value, uint8_t ch, uint8_t track) {
        ++pedalCount_;
        if (!counting_) song_->pedal.push_back({tick, value, ch, track});
    }
    void noteOn(uint8_t ch, uint8_t note, uint8_t vel, uint32_t tick,
                uint8_t track) {
        ++noteCount_;
        if (!counting_) tracker_->on(ch, note, vel, tick, track, song_->notes);
    }
    void noteOff(uint8_t ch, uint8_t note, uint32_t tick, uint8_t track) {
        if (!counting_) tracker_->off(ch, note, tick, track, song_->notes);
    }
    void closeAll(uint32_t tick, uint8_t track) {
        if (!counting_) tracker_->closeAll(tick, track, song_->notes);
    }

    size_t noteCount() const { return noteCount_; }
    size_t tempoCount() const { return tempoCount_; }
    size_t pedalCount() const { return pedalCount_; }

private:
    bool counting_;
    MidiSong* song_ = nullptr;
    NoteTracker* tracker_ = nullptr;
    size_t noteCount_ = 0;
    size_t tempoCount_ = 0;
    size_t pedalCount_ = 0;
    size_t trackCount_ = 0;
};

MidiParseError parseHeader(Reader& r, uint16_t& ntrks, uint16_t& tpq) {
    if (!r.match4("MThd")) return MidiParseError::NotMidi;
    uint32_t headerLen = r.u32();
    if (!r.ok() || headerLen < 6) return MidiParseError::NotMidi;
    r.u16();  // format (0/1 both handled identically; 2 parses as-is)
    ntrks = r.u16();
    uint16_t division = r.u16();
    r.skip(headerLen - 6);
    if (!r.ok()) return MidiParseError::NotMidi;
    if (division & 0x8000) return MidiParseError::SmpteDivision;
    tpq = division;
    return MidiParseError::Ok;
}

MidiParseError parseTrack(Reader& r, uint8_t trackIndex, Sink& sink) {
    if (!r.match4("MTrk")) return r.ok() ? MidiParseError::BadTrack
                                         : MidiParseError::Truncated;
    uint32_t chunkLen = r.u32();
    if (!r.ok() || chunkLen > r.remaining()) return MidiParseError::Truncated;
    size_t end = r.pos() + chunkLen;

    sink.beginTrack();
    uint32_t tick = 0;
    uint8_t runningStatus = 0;

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
                sink.tempo(tick, us);
            } else if (type == 0x03) {  // Track Name
                sink.trackName(trackIndex, r, len);
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
                sink.noteOff(ch, data1 & 0x7F, tick, trackIndex);
                break;
            }
            case 0x90: {  // note on (velocity 0 == off)
                uint8_t vel = r.u8();
                if (vel == 0)
                    sink.noteOff(ch, data1 & 0x7F, tick, trackIndex);
                else
                    sink.noteOn(ch, data1 & 0x7F, vel, tick, trackIndex);
                break;
            }
            case 0xB0: {  // control change — keep CC64 only
                uint8_t value = r.u8();
                if (data1 == 64) sink.pedal(tick, value, ch, trackIndex);
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
    sink.closeAll(tick, trackIndex);
    r.skip(end - r.pos());  // tolerate events after EoT inside the declared chunk
    return MidiParseError::Ok;
}

// One pass over the source: header + every track, routed through `sink`.
MidiParseError runPass(ByteSource& src, Sink& sink, uint16_t& ntrks,
                       uint16_t& tpq) {
    Reader r(src);
    MidiParseError e = parseHeader(r, ntrks, tpq);
    if (e != MidiParseError::Ok) return e;
    for (uint16_t i = 0; i < ntrks; ++i) {
        e = parseTrack(r, static_cast<uint8_t>(i), sink);
        if (e != MidiParseError::Ok) return e;
    }
    return MidiParseError::Ok;
}

}  // namespace

size_t midiParseFixedOverhead() {
    // A192: the tracker's real footprint is its reserved open-list capacity
    // (kMaxOpenNotes × 8 B = 2 KB), not sizeof of the vector header.
    return NoteTracker::kMaxOpenNotes * sizeof(NoteTracker::Open) +
           sizeof(NoteTracker);
}

size_t midiParseOutputBytes(size_t notes, size_t tempo, size_t pedal) {
    return notes * sizeof(MidiNote) + tempo * sizeof(TempoChange) +
           pedal * sizeof(PedalEvent);
}

MidiParseError checkMidi(ByteSource& src, size_t budgetBytes) {
    src.reset();
    Sink counter;  // count-only: no NoteTracker, no output vectors
    uint16_t ntrks = 0, tpq = 480;
    MidiParseError e = runPass(src, counter, ntrks, tpq);
    if (e != MidiParseError::Ok) return e;
    if (midiParseOutputBytes(counter.noteCount(), counter.tempoCount(),
                             counter.pedalCount()) > budgetBytes)
        return MidiParseError::TooBigForMemory;
    return MidiParseError::Ok;
}

MidiParseResult parseMidi(ByteSource& src, size_t budgetBytes) {
    MidiParseResult result;

    // --- Pass 1: count (allocates nothing large) so the exact output size is
    //     known BEFORE any big allocation. This is what makes the refusal
    //     honest: an over-budget song is rejected here, never by gambling on
    //     an allocation that aborts on -fno-exceptions. ---
    uint16_t ntrks = 0, tpq = 480;
    {
        src.reset();
        Sink counter;
        MidiParseError e = runPass(src, counter, ntrks, tpq);
        if (e != MidiParseError::Ok) {
            result.error = e;
            return result;
        }
        if (midiParseOutputBytes(counter.noteCount(), counter.tempoCount(),
                                 counter.pedalCount()) > budgetBytes) {
            result.error = MidiParseError::TooBigForMemory;
            return result;  // song stays empty — hand back nothing on refusal
        }
        result.song.ticksPerQuarter = tpq;
        // Exact reserves — each vector is allocated ONCE at its final size, so
        // there is zero doubling transient (the ~2x realloc spike A182 fought).
        result.song.notes.reserve(counter.noteCount());
        result.song.tempo.reserve(counter.tempoCount());
        result.song.pedal.reserve(counter.pedalCount());
        result.song.tracks.reserve(ntrks);
    }

    // --- Pass 2: fill (A192: the NoteTracker is a small bounded list now,
    //     ~2 KB reserved once — no longer the budget's dominant term). ---
    src.reset();
    auto tracker = std::make_unique<NoteTracker>();
    Sink filler(result.song, *tracker);
    MidiParseError e = runPass(src, filler, ntrks, tpq);
    if (e != MidiParseError::Ok) {  // unreachable for identical bytes; stay safe
        result.error = e;
        result.song = MidiSong{};
        return result;
    }
    if (tracker->overflowed) {  // A192: > kMaxOpenNotes simultaneous opens
        result.error = MidiParseError::TooBigForMemory;
        result.song = MidiSong{};
        return result;
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

MidiParseResult parseMidi(const uint8_t* data, size_t len, size_t budgetBytes) {
    BufferByteSource src(data, len);
    return parseMidi(src, budgetBytes);
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
