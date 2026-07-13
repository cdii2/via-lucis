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

}  // namespace vialucis
