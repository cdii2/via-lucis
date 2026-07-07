#include "vialucis/track_config.h"

#include <cctype>
#include <string>

namespace vialucis {
namespace {

bool containsWord(const std::string& s, const char* word) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s)
        lower.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower.find(word) != std::string::npos;
}

}  // namespace

TrackConfig TrackConfig::defaultsFor(const MidiSong& song) {
    TrackConfig cfg;
    cfg.tracks.assign(song.tracks.size(), TrackAssign{});

    std::vector<bool> hasNotes(song.tracks.size(), false);
    for (const MidiNote& n : song.notes)
        if (n.track < hasNotes.size()) hasNotes[n.track] = true;

    std::vector<size_t> noteTracks;
    for (size_t i = 0; i < cfg.tracks.size(); ++i) {
        if (!hasNotes[i]) {
            cfg.tracks[i].hand = Hand::Off;
            cfg.tracks[i].lights = false;
            continue;
        }
        noteTracks.push_back(i);
        const std::string& name = song.tracks[i].name;
        if (containsWord(name, "left") || containsWord(name, "lh"))
            cfg.tracks[i].hand = Hand::Left;
        else if (containsWord(name, "right") || containsWord(name, "rh"))
            cfg.tracks[i].hand = Hand::Right;
        else
            cfg.tracks[i].hand = Hand::Both;  // provisional; refined below
    }

    // Two anonymous note tracks: piano convention (first=RH, second=LH).
    if (noteTracks.size() == 2) {
        if (cfg.tracks[noteTracks[0]].hand == Hand::Both)
            cfg.tracks[noteTracks[0]].hand = Hand::Right;
        if (cfg.tracks[noteTracks[1]].hand == Hand::Both)
            cfg.tracks[noteTracks[1]].hand = Hand::Left;
    }
    return cfg;
}

uint32_t TrackConfig::lightsMask() const {
    uint32_t m = 0;
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].hand != Hand::Off && tracks[i].lights)
            m |= trackBit(static_cast<uint8_t>(i));
    return m;
}

uint32_t TrackConfig::practicedMask(Hand practice) const {
    uint32_t m = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        Hand h = tracks[i].hand;
        if (h == Hand::Off) continue;
        bool match = practice == Hand::Both || h == practice || h == Hand::Both;
        if (match) m |= trackBit(static_cast<uint8_t>(i));
    }
    return m;
}

uint32_t TrackConfig::accompanimentMask(Hand practice) const {
    if (practice == Hand::Both) return 0;  // nothing left for the piano
    Hand other = practice == Hand::Left ? Hand::Right : Hand::Left;
    uint32_t m = 0;
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].hand == other) m |= trackBit(static_cast<uint8_t>(i));
    return m;
}

uint32_t TrackConfig::audibleMask() const {
    uint32_t m = 0;
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].hand != Hand::Off) m |= trackBit(static_cast<uint8_t>(i));
    return m;
}

}  // namespace vialucis
