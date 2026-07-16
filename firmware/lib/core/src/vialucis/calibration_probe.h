#pragma once
// Calibration probe (C3): the wizard's primitive — light one dot, capture
// the key pressed beneath it (DESIGN-lightshow §5). Pure state machine;
// PlaybackEngine hosts it until the M-wave ModeDirector takes over the
// forced-source slot (3A). Ownership rules (OV4): arms only when NOT
// Playing (the HOST checks state); while armed, note-ons are consumed
// HERE before wait mode ever sees them; pedal/CC never reach it (the BLE
// layer dispatches note-ons only); auto-timeout via tickExpire.

#include <cstdint>

namespace vialucis {

class CalibrationProbe {
public:
    void arm(uint16_t led, uint64_t nowUs, uint64_t timeoutUs) {
        armed_ = true;
        led_ = led;
        deadlineUs_ = nowUs + timeoutUs;
        captured_ = false;
        timedOut_ = false;  // B6d: a fresh arm clears any prior expiry flag
    }

    void cancel() {
        armed_ = false;
        captured_ = false;
        timedOut_ = false;  // explicit user cancel is NOT a timeout
    }

    // Note-on while armed: capture it and disarm. True = consumed — the
    // caller must NOT forward this press to practice.
    bool onNoteOn(uint8_t note) {
        if (!armed_) return false;
        note_ = note;
        captured_ = true;
        armed_ = false;
        timedOut_ = false;  // captured before the deadline: not a timeout
        return true;
    }

    // True exactly when the arm window expires on this call (edge — the
    // host marks the frame dirty so the dot clears).
    bool tickExpire(uint64_t nowUs) {
        if (!armed_ || nowUs < deadlineUs_) return false;
        armed_ = false;
        timedOut_ = true;  // B6d: sticky until the next arm()/cancel()
        return true;
    }

    bool armed() const { return armed_; }
    uint16_t led() const { return led_; }
    bool hasCapture() const { return captured_; }
    uint8_t capturedNote() const { return note_; }
    // B6d: distinguishes "disarmed because it expired" from "disarmed
    // because the wizard cancelled it / a note was captured" — the wizard
    // was polling a stale armed:false ambiguously without this. Sticky
    // across ticks so a poll landing after the expiring tick still sees it;
    // cleared only by the next arm() or cancel().
    bool timedOut() const { return timedOut_; }

private:
    bool armed_ = false;
    bool captured_ = false;
    bool timedOut_ = false;
    uint16_t led_ = 0;
    uint8_t note_ = 0;
    uint64_t deadlineUs_ = 0;
};

}  // namespace vialucis
