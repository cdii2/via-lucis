#pragma once
// MidiCapture — the recording "tape head" (docs/DESIGN-record.md). An armable
// subsystem clamped on the post-echo-guard input stream: it timestamps note
// on/off + velocity + CC64 into a pre-reserved bounded buffer and finalizes an
// immutable take that feeds the SMF writer (and the hand-split downstream).
//
// Iron rules honoured here:
//   * Latency: the append path is O(1) into a buffer reserved ONCE at arm()
//     (zero steady-state allocation). The tap runs AFTER the wait-mode verdict
//     (REC3 wires it in ModeDirector::onKeyDown), never between key and light.
//     Overflow (byte budget or duration cap) flags + drops; it never blocks,
//     grows, or throws — a full take must not stall the frame.
//   * Echo guard: capture owns its OWN EchoGuard, fed by the emission path via
//     noteSent(). onNoteOn() consults THAT guard only, so device-sent notes are
//     excluded without ever consuming the engine guard's credits.
//
// Pure C++ (no Arduino headers) — native-testable with an injected clock.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vialucis/echo_guard.h"
#include "vialucis/smf_writer.h"  // SmfNoteEvent / SmfPedalEvent — the take model

namespace vialucis {

enum class CaptureState : uint8_t { Idle, Armed, Recording };

// Why an arm() request was refused (core-level; REST adds playing/low-space).
enum class ArmResult : uint8_t { Armed, AlreadyArmed, BadBudget };

// Whether the finished take is whole or was truncated by a limit.
enum class CaptureStatus : uint8_t { Ok, Overflowed };

// One raw timestamped input event, relative to the first note-on (ms). Public
// so the zero-alloc proof can observe the backing buffer; also documents the
// 8-byte wire size the byte budget is measured against.
enum class CaptureEventType : uint8_t { NoteOn, NoteOff, Pedal };
struct CaptureEvent {
    uint32_t tMs;
    CaptureEventType type;
    uint8_t data1;    // note number, or (Pedal) unused
    uint8_t data2;    // velocity, or (Pedal) CC64 value
    uint8_t channel;  // 0-15
};

// The finalized, immutable take: paired notes + pedals in ms, ready for
// writeSmf() (as one performance track, split into hands downstream).
struct CaptureTake {
    std::vector<SmfNoteEvent> notes;   // ~onMs order; writeSmf re-sorts anyway
    std::vector<SmfPedalEvent> pedals;
    uint32_t durationMs = 0;
    bool empty = true;                        // no note ever captured
    CaptureStatus status = CaptureStatus::Ok;  // Overflowed if a limit truncated
};

class MidiCapture {
public:
    // Reserve the event buffer ONCE (the only allocation). budgetBytes bounds
    // the take; maxDurationMs is the duration cap. Refuses if not Idle, or if
    // the budget can't hold a single event.
    ArmResult arm(size_t budgetBytes, uint32_t maxDurationMs, uint64_t nowUs);

    // Input taps — O(1) append, zero steady-state alloc. Times in microseconds
    // (engine convention; the echo guard windows in us). The first accepted
    // note-on starts the clock and trims leading silence (its tMs == 0).
    void onNoteOn(uint8_t note, uint8_t velocity, uint8_t channel, uint64_t nowUs);
    void onNoteOff(uint8_t note, uint8_t channel, uint64_t nowUs);
    void onPedal(uint8_t value, uint8_t channel, uint64_t nowUs);  // CC64

    // Register a note WE transmitted to the piano, into capture's own guard so
    // its echo is excluded from the take (REC3 feeds this from the send path).
    void noteSent(uint8_t note, uint64_t nowUs) { guard_.noteSent(note, nowUs); }
    EchoGuard& echoGuard() { return guard_; }

    // Register a CC64 WE transmitted (demo/accompaniment pedal pass-through) —
    // its echo must be excluded from the take exactly like a note echo (§5a:
    // a take contains only YOUR presses). EchoGuard is note-keyed, so pedal
    // credits get their own count+expiry, value-agnostic within the window.
    void pedalSent(uint64_t nowUs) {
        if (nowUs > pedalExpiryUs_) pedalCredits_ = 0;
        if (pedalCredits_ < 255) ++pedalCredits_;
        pedalExpiryUs_ = nowUs + guard_.windowUs();
    }

    // Finalize → immutable take (pairs on/off; notes still held when Stop is
    // pressed close at the stop time — a take ending on a sustained chord
    // keeps its real duration; trailing silence is trimmed). Returns to Idle.
    // A take with no notes is flagged empty.
    CaptureTake stop(uint64_t nowUs);

    // Drop armed/recording state, keep nothing. Returns to Idle.
    void discard();

    CaptureState state() const { return state_; }
    CaptureStatus status() const { return status_; }

    // 0 until the clock starts; then nowUs - firstNoteUs in ms.
    uint32_t elapsedMs(uint64_t nowUs) const;
    size_t usedBytes() const { return events_.size() * sizeof(CaptureEvent); }
    size_t budgetBytes() const { return budgetBytes_; }
    uint32_t maxDurationMs() const { return maxDurationMs_; }

    // Backing buffer, for the zero-alloc proof (data() + capacity() stability).
    const std::vector<CaptureEvent>& rawEvents() const { return events_; }

private:
    void append(CaptureEventType type, uint8_t d1, uint8_t d2, uint8_t ch,
                uint64_t nowUs);

    CaptureState state_ = CaptureState::Idle;
    CaptureStatus status_ = CaptureStatus::Ok;

    std::vector<CaptureEvent> events_;
    size_t maxEvents_ = 0;       // budget-derived cap (never exceeded → no realloc)
    size_t budgetBytes_ = 0;
    uint32_t maxDurationMs_ = 0;

    uint64_t firstNoteUs_ = 0;   // clock origin (first accepted note-on)
    uint32_t lastEventMs_ = 0;   // most recent captured event time

    EchoGuard guard_;            // capture's OWN guard (never the engine's)
    uint8_t pedalCredits_ = 0;   // CC64 echo credits (value-agnostic window)
    uint64_t pedalExpiryUs_ = 0;
};

}  // namespace vialucis
