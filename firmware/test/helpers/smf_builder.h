#pragma once
// Test helper: build Standard MIDI File byte streams in memory so parser tests
// don't depend on fixture files. Header-only; test code only.

#include <cstdint>
#include <vector>

namespace smf {

using Bytes = std::vector<uint8_t>;

inline void put16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

inline void put32(Bytes& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

// Variable-length quantity, MSB-first, 7 bits per byte.
inline void putVlq(Bytes& b, uint32_t v) {
    uint8_t stack[5];
    int n = 0;
    do {
        stack[n++] = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;
    } while (v != 0);
    while (n-- > 1) b.push_back(stack[n] | 0x80);
    b.push_back(stack[0]);
}

inline Bytes header(uint16_t format, uint16_t ntrks, uint16_t division) {
    Bytes b = {'M', 'T', 'h', 'd'};
    put32(b, 6);
    put16(b, format);
    put16(b, ntrks);
    put16(b, division);
    return b;
}

// Wrap raw event bytes in an MTrk chunk (appends End of Track automatically).
inline Bytes track(const Bytes& events, bool addEot = true) {
    Bytes body = events;
    if (addEot) {
        body.push_back(0x00);  // delta
        body.push_back(0xFF);
        body.push_back(0x2F);
        body.push_back(0x00);
    }
    Bytes b = {'M', 'T', 'r', 'k'};
    put32(b, static_cast<uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

inline void append(Bytes& dst, const Bytes& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

// Event builders — each starts with a delta time.
inline void noteOn(Bytes& b, uint32_t delta, uint8_t ch, uint8_t note, uint8_t vel) {
    putVlq(b, delta);
    b.push_back(0x90 | (ch & 0x0F));
    b.push_back(note);
    b.push_back(vel);
}

inline void noteOff(Bytes& b, uint32_t delta, uint8_t ch, uint8_t note, uint8_t vel = 0) {
    putVlq(b, delta);
    b.push_back(0x80 | (ch & 0x0F));
    b.push_back(note);
    b.push_back(vel);
}

inline void cc(Bytes& b, uint32_t delta, uint8_t ch, uint8_t controller, uint8_t value) {
    putVlq(b, delta);
    b.push_back(0xB0 | (ch & 0x0F));
    b.push_back(controller);
    b.push_back(value);
}

inline void tempoMeta(Bytes& b, uint32_t delta, uint32_t usPerQuarter) {
    putVlq(b, delta);
    b.push_back(0xFF);
    b.push_back(0x51);
    b.push_back(0x03);
    b.push_back(static_cast<uint8_t>((usPerQuarter >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((usPerQuarter >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(usPerQuarter & 0xFF));
}

inline void trackName(Bytes& b, uint32_t delta, const char* name) {
    putVlq(b, delta);
    b.push_back(0xFF);
    b.push_back(0x03);
    Bytes nameBytes;
    for (const char* p = name; *p; ++p) nameBytes.push_back(static_cast<uint8_t>(*p));
    putVlq(b, static_cast<uint32_t>(nameBytes.size()));
    append(b, nameBytes);
}

}  // namespace smf
