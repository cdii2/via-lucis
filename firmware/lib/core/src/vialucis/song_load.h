#pragma once
// The pure decision behind "why did loading a song fail" (§3-E item 2,
// A154). Before this fix, `POST /api/songs/{name}/load` collapsed BOTH a
// missing file and a file that exists but fails to parse into the same
// generic 400 — a player retrying a typo'd name got the same unhelpful
// answer as a genuinely corrupt upload. App::loadSong (firmware/src/app.cpp,
// esp32-only — LittleFS read + parseMidi) gathers the two FACTS (did the
// read succeed, what did parseMidi say); the DECISION from those two facts
// to an outcome is pure and lives here so it's native-tested like every
// other decision seam in this codebase, even though the glue that gathers
// the facts isn't.

#include "vialucis/midi_parser.h"

namespace vialucis {

// TooBig (A185) is split OUT of ParseError so the load route can answer
// honestly: a song that parses fine but is beyond this device's RAM ceiling is
// "too big for this device", NOT "corrupt". The player can keep the file (it's
// valid) and load it on a roomier boot / a PSRAM board, instead of deleting a
// good song they were told was broken.
enum class SongLoadOutcome : uint8_t { Ok, NotFound, ParseError, TooBig };

// `fileRead` = the store read succeeded (the file exists and was opened).
// `parseErr` is only meaningful when `fileRead` is true.
inline SongLoadOutcome classifySongLoad(bool fileRead,
                                        MidiParseError parseErr) {
    if (!fileRead) return SongLoadOutcome::NotFound;
    if (parseErr == MidiParseError::TooBigForMemory)
        return SongLoadOutcome::TooBig;
    if (parseErr != MidiParseError::Ok) return SongLoadOutcome::ParseError;
    return SongLoadOutcome::Ok;
}

}  // namespace vialucis
