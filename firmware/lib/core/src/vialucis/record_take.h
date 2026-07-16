#pragma once
// REC4 pure helpers — the persistence-side logic of a recorded take, kept in
// lib/core so it is native-testable (App/SongStore that call these are
// Arduino-bound). Two jobs: split a raw take into hand tracks for the SMF
// writer, and pick the next free auto-name.

#include <string>
#include <vector>

#include "vialucis/midi_capture.h"  // CaptureTake
#include "vialucis/smf_writer.h"    // SmfInput

namespace vialucis {

// Split a raw take into hand tracks at a pitch split-point (default middle C,
// 60, per docs/DESIGN-record.md §4): notes with note < splitPoint go to a
// "Left" track, >= splitPoint to a "Right" track. The names let TrackConfig's
// load heuristic assign the hands. An empty hand's track is omitted entirely;
// the "Right" track is emitted first (piano convention). ALL CC64 pedal events
// attach to the first emitted track. A notes-empty (pedals-only) take still
// yields a single "Right" track carrying the pedals. The editor fixes hand
// crossovers per-note downstream.
SmfInput splitTakeIntoHands(const CaptureTake& take, uint8_t splitPoint = 60);

// The next free "recording-<n>.mid" name given the existing song filenames
// (any order; names that don't match the pattern are ignored). n is the
// smallest positive integer not already used (so a gap is filled first).
std::string nextRecordingName(const std::vector<std::string>& existing);

// Whether a take should be reported as SavedTruncated rather than plain Saved
// once it saves successfully (B5, docs/BUGFIX-PLAN-2026-07-15 §3-B): true iff
// capture had to drop events to a byte or duration limit. A small named
// predicate so App's recordStop()/retry-save don't inline the enum compare at
// every call site.
inline bool takeWasTruncated(const CaptureTake& take) {
    return take.status == CaptureStatus::Overflowed;
}

// REC5 — a take that failed to save is kept here so a retry-save can retry it
// without re-recording the performance (docs/BUGFIX-PLAN-2026-07-15 §3-B B5:
// "today the performance is destroyed" on SaveFailed). Pure state holder: App
// still does the actual hand-split/writeSmf/LittleFS I/O on each attempt —
// this class only keeps the CaptureTake alive between attempts. Holding a new
// take always replaces any previously-held one (only the most recent failed
// take is retryable, matching "one take in flight" everywhere else in record
// mode assumes).
class PendingSave {
public:
    void hold(CaptureTake take) {
        take_ = std::move(take);
        held_ = true;
    }

    bool held() const { return held_; }

    // Precondition: held(). Returned reference is invalidated by the next
    // hold() or clear().
    const CaptureTake& take() const { return take_; }

    // Drop the held take (after a successful retry-save, or an explicit
    // discard). A no-op when nothing is held.
    void clear() {
        held_ = false;
        take_ = CaptureTake{};
    }

private:
    bool held_ = false;
    CaptureTake take_;
};

}  // namespace vialucis
