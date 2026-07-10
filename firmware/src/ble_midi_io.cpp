#include "ble_midi_io.h"

#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_Client_ESP32.h>

// Empty name ⇒ connect to the first BLE-MIDI peripheral found (the FP-30X is
// the only one in the room). The macro creates globals BLEMIDI and MIDI.
BLEMIDI_CREATE_INSTANCE("", MIDI)

namespace vialucis {
namespace {

BleMidiIo::NoteHandler gOnNoteOn;
BleMidiIo::NoteHandler gOnNoteOff;
std::function<void()> gOnActivity;
volatile bool gConnected = false;

void noteActivity() {
    if (gOnActivity) gOnActivity();
}

void handleNoteOn(byte /*channel*/, byte note, byte velocity) {
    noteActivity();
    dispatchNote(gOnNoteOn, gOnNoteOff, /*isOff=*/false, note, velocity);
}

void handleNoteOff(byte /*channel*/, byte note, byte velocity) {
    noteActivity();
    dispatchNote(gOnNoteOn, gOnNoteOff, /*isOff=*/true, note, velocity);
}

void handleControlChange(byte /*channel*/, byte /*number*/, byte /*value*/) {
    noteActivity();  // pedal/CC count as activity too (M2 charter)
}

}  // namespace

void BleMidiIo::begin() {
    MIDI.begin(MIDI_CHANNEL_OMNI);
    BLEMIDI.setHandleConnected([]() { gConnected = true; });
    BLEMIDI.setHandleDisconnected([]() { gConnected = false; });
    MIDI.setHandleNoteOn(handleNoteOn);
    MIDI.setHandleNoteOff(handleNoteOff);
    MIDI.setHandleControlChange(handleControlChange);
}

void BleMidiIo::poll() { MIDI.read(); }

void BleMidiIo::onNoteOn(NoteHandler h) { gOnNoteOn = std::move(h); }
void BleMidiIo::onNoteOff(NoteHandler h) { gOnNoteOff = std::move(h); }
void BleMidiIo::onActivity(std::function<void()> h) {
    gOnActivity = std::move(h);
}

void BleMidiIo::send(const MidiOutMsg& msg) {
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
