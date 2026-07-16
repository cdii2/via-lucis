#include "ble_midi_io.h"

#include <Arduino.h>
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>

#include <algorithm>
#include <atomic>

// Empty name ⇒ connect to the first BLE-MIDI peripheral found (the FP-30X is
// the only one in the room). The macro creates globals BLEMIDI and MIDI.
// setTargetName() below can narrow this before begin() (Wave E2).
BLEMIDI_CREATE_INSTANCE("", MIDI)

namespace vialucis {
namespace {

BleMidiIo::NoteHandler gOnNoteOn;
BleMidiIo::NoteHandler gOnNoteOff;
std::function<void()> gOnActivity;
std::function<void(uint8_t)> gOnPedal;
// Read from connected() (other tasks) / written from the BLE connect-
// /disconnect callbacks — std::atomic<bool> (was volatile bool; Wave E2
// item 3) is the correct primitive for a cross-task flag, not just a
// convention that happened to work.
std::atomic<bool> gConnected{false};

// --- BLE scan backoff (Wave E2, BUGFIX-PLAN §3-E item 1) --------------------
// The lathoub BLE-MIDI client (BLEMIDI_Client_ESP32::available(), vendored
// via lib_deps, not in this repo) is entirely poll-driven: every call while
// disconnected either connects or, once `scanDone`, immediately re-arms a
// fresh 1s scan — there is no library-side cooldown. Since poll() below
// forwards to MIDI.read() -> that available() every loop() iteration, the
// radio scans continuously whenever no piano is in range, contending with
// WiFi (e.g. during uploads). We don't vendor/fork the library, so the only
// lever we have is throttling how often a DISCONNECTED poll() is allowed to
// reach MIDI.read() at all — a connected link always reads immediately
// (iron rule: never delay established traffic).
//
// Values are bring-up-tunable (§6-6 — pick against the real FP-30X/router
// once flashed); they are a starting point, not a measured result.
constexpr uint32_t kBleScanBackoffInitialMs = 2000;   // bring-up-tunable
constexpr uint32_t kBleScanBackoffCapMs = 60000;      // bring-up-tunable
constexpr uint32_t kBleScanBackoffMultiplier = 2;     // bring-up-tunable

uint32_t gBackoffMs = kBleScanBackoffInitialMs;
uint32_t gNextScanAtMs = 0;  // millis() timestamp; 0 == attempt immediately

// Reset to the fastest retry on ANY connect or disconnect event (spec: a
// state change means the radio environment just changed, so we want to be
// quick to notice the NEXT one too — not still serving out a long-idle cap).
void resetScanBackoff() {
    gBackoffMs = kBleScanBackoffInitialMs;
    gNextScanAtMs = 0;
}

void noteActivity() {
    if (gOnActivity) gOnActivity();
}

// Active Sensing (0xFE) must never feed onActivity (BUGFIX-PLAN §3-E item 5,
// verified NOT a bug — pinned here, comment only): noteActivity() above is
// invoked ONLY from handleNoteOn/handleNoteOff/handleControlChange, and
// begin() below wires exactly those three handlers via MIDI.setHandleX(...).
// This file never calls MIDI.setHandleActiveSensing(...). The FortySeven-
// Effects MIDI Library dispatches strictly per explicitly-registered
// handler — no handler means the message is parsed and silently dropped,
// never routed to any of these three as a fallback — so an Active Sensing
// byte (which the piano may send continuously to keep the BLE link alive)
// structurally cannot reach onActivity. Not natively testable: this file is
// esp32/NimBLE-only, no native build target compiles it. Verify at bring-up
// that AFK does not stay disarmed on Active-Sensing-only traffic.

void handleNoteOn(byte /*channel*/, byte note, byte velocity) {
    noteActivity();
    dispatchNote(gOnNoteOn, gOnNoteOff, /*isOff=*/false, note, velocity);
}

void handleNoteOff(byte /*channel*/, byte note, byte velocity) {
    noteActivity();
    dispatchNote(gOnNoteOn, gOnNoteOff, /*isOff=*/true, note, velocity);
}

void handleControlChange(byte /*channel*/, byte number, byte value) {
    noteActivity();  // pedal/CC count as activity too (M2 charter)
    if (number == 64 && gOnPedal) gOnPedal(value);  // sustain, raw 0-127 (REC3)
}

}  // namespace

void BleMidiIo::setTargetName(const std::string& name) {
    // BLEMIDI_Transport::setName() (BLEMIDI_Transport.h) just copies into a
    // member read by the NEXT begin() call — safe to call any time before
    // begin(), a no-op influence-wise afterward until the next boot.
    BLEMIDI.setName(name.c_str());
}

void BleMidiIo::begin() {
    MIDI.begin(MIDI_CHANNEL_OMNI);
    BLEMIDI.setHandleConnected([]() {
        gConnected = true;
        resetScanBackoff();
    });
    BLEMIDI.setHandleDisconnected([]() {
        gConnected = false;
        resetScanBackoff();
    });
    MIDI.setHandleNoteOn(handleNoteOn);
    MIDI.setHandleNoteOff(handleNoteOff);
    MIDI.setHandleControlChange(handleControlChange);
}

void BleMidiIo::poll() {
    if (gConnected) {
        MIDI.read();  // established link: never throttled (iron rule 1)
        return;
    }
    uint32_t now = millis();
    // Signed-difference compare tolerates millis() wraparound (~49 days).
    if (static_cast<int32_t>(now - gNextScanAtMs) < 0) return;  // backing off
    MIDI.read();  // drives the transport's available()/scan() cycle
    gNextScanAtMs = now + gBackoffMs;
    gBackoffMs = std::min(gBackoffMs * kBleScanBackoffMultiplier,
                          kBleScanBackoffCapMs);
}

void BleMidiIo::onNoteOn(NoteHandler h) { gOnNoteOn = std::move(h); }
void BleMidiIo::onNoteOff(NoteHandler h) { gOnNoteOff = std::move(h); }
void BleMidiIo::onActivity(std::function<void()> h) {
    gOnActivity = std::move(h);
}
void BleMidiIo::onPedal(std::function<void(uint8_t)> h) {
    gOnPedal = std::move(h);
}

void BleMidiIo::send(const MidiOutMsg& msg) {
    // FIX-C (deferred to hardware bring-up, BUGFIX-PLAN §3-E/§7 — comment
    // only): if we're disconnected, this send silently no-ops below, but
    // the caller (NoteEmitter::consume(), note_emitter.cpp) already
    // registered an EchoGuard credit for this note at EMIT time (see
    // echo_guard.h::noteSent() for the full explanation). A note that never
    // actually went out over the air can still own an outstanding credit —
    // if the piano reconnects while that credit is un-expired and the
    // player genuinely presses the same key, the echo guard swallows the
    // real press. Needs a live disconnect/reconnect to fix and verify.
    if (!gConnected) return;
    // Library channels are 1-based.
    uint8_t ch = static_cast<uint8_t>(msg.channel + 1);
    switch (msg.type) {
        case MidiOutType::NoteOn:
            MIDI.sendNoteOn(msg.data1, msg.data2, ch);
            break;
        case MidiOutType::NoteOff:
            MIDI.sendNoteOff(msg.data1, msg.data2, ch);
            break;
        case MidiOutType::Cc:
            MIDI.sendControlChange(msg.data1, msg.data2, ch);
            break;
    }
}

bool BleMidiIo::connected() const { return gConnected; }

}  // namespace vialucis
