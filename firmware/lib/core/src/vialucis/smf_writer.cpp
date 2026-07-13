#include "vialucis/smf_writer.h"

#include <algorithm>

namespace vialucis {
namespace {

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

// Variable-length quantity, MSB-first, 7 bits per byte (SMF delta times).
// Correct for the full 32-bit range → long silences encode as multi-byte.
void putVlq(std::vector<uint8_t>& b, uint32_t v) {
    uint8_t stack[5];
    int n = 0;
    do {
        stack[n++] = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;
    } while (v != 0);
    while (n-- > 1) b.push_back(static_cast<uint8_t>(stack[n] | 0x80));
    b.push_back(stack[0]);
}

// One emitted channel message with its absolute time and a same-time ordering
// key. At an identical timestamp we emit note-offs (0) before pedal (1) before
// note-ons (2): an off must never fall after a re-strike of the same key at the
// same instant, or the parser would drop the held note (the tie-order bug the
// show_player suite already caught elsewhere).
struct RawMsg {
    uint32_t t;
    uint8_t order;
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
};

// Wrap a track body in an MTrk chunk (adds End-of-Track meta), append to file.
void appendTrackChunk(std::vector<uint8_t>& file, std::vector<uint8_t>& body) {
    body.push_back(0x00);  // delta
    body.push_back(0xFF);
    body.push_back(0x2F);
    body.push_back(0x00);
    file.push_back('M');
    file.push_back('T');
    file.push_back('r');
    file.push_back('k');
    putU32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

}  // namespace

std::vector<uint8_t> writeSmf(const SmfInput& input) {
    std::vector<uint8_t> file;

    // Header: format 1, ntrks = conductor + performance tracks.
    uint16_t ntrks = static_cast<uint16_t>(1 + input.tracks.size());
    file.push_back('M');
    file.push_back('T');
    file.push_back('h');
    file.push_back('d');
    putU32(file, 6);
    putU16(file, 1);  // format 1
    putU16(file, ntrks);
    putU16(file, kSmfTicksPerQuarter);

    // Track 0 — conductor: one tempo meta at delta 0, then EoT.
    {
        std::vector<uint8_t> body;
        putVlq(body, 0);
        body.push_back(0xFF);
        body.push_back(0x51);
        body.push_back(0x03);
        body.push_back(static_cast<uint8_t>((kSmfUsPerQuarter >> 16) & 0xFF));
        body.push_back(static_cast<uint8_t>((kSmfUsPerQuarter >> 8) & 0xFF));
        body.push_back(static_cast<uint8_t>(kSmfUsPerQuarter & 0xFF));
        appendTrackChunk(file, body);
    }

    // Performance tracks.
    for (const SmfTrack& tr : input.tracks) {
        std::vector<RawMsg> msgs;
        msgs.reserve(tr.notes.size() * 2 + tr.pedals.size());
        for (const SmfNoteEvent& n : tr.notes) {
            uint32_t off = n.offMs >= n.onMs ? n.offMs : n.onMs;
            msgs.push_back({n.onMs, 2,
                            static_cast<uint8_t>(0x90 | (n.channel & 0x0F)),
                            static_cast<uint8_t>(n.note & 0x7F),
                            static_cast<uint8_t>(n.velocity & 0x7F)});
            msgs.push_back({off, 0,
                            static_cast<uint8_t>(0x80 | (n.channel & 0x0F)),
                            static_cast<uint8_t>(n.note & 0x7F), 0});
        }
        for (const SmfPedalEvent& p : tr.pedals) {
            msgs.push_back({p.tMs, 1,
                            static_cast<uint8_t>(0xB0 | (p.channel & 0x0F)),
                            64, static_cast<uint8_t>(p.value & 0x7F)});
        }
        std::stable_sort(msgs.begin(), msgs.end(),
                         [](const RawMsg& a, const RawMsg& b) {
                             if (a.t != b.t) return a.t < b.t;
                             return a.order < b.order;
                         });

        std::vector<uint8_t> body;
        if (!tr.name.empty()) {
            putVlq(body, 0);
            body.push_back(0xFF);
            body.push_back(0x03);
            putVlq(body, static_cast<uint32_t>(tr.name.size()));
            body.insert(body.end(), tr.name.begin(), tr.name.end());
        }
        uint32_t last = 0;
        for (const RawMsg& m : msgs) {
            putVlq(body, m.t - last);
            last = m.t;
            body.push_back(m.b0);
            body.push_back(m.b1);
            body.push_back(m.b2);
        }
        appendTrackChunk(file, body);
    }

    return file;
}

}  // namespace vialucis
