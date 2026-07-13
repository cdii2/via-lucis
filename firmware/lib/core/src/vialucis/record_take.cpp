#include "vialucis/record_take.h"

#include <set>

namespace vialucis {

SmfInput splitTakeIntoHands(const CaptureTake& take, uint8_t splitPoint) {
    SmfTrack left, right;
    left.name = "Left";
    right.name = "Right";
    for (const SmfNoteEvent& n : take.notes) {
        if (n.note < splitPoint)
            left.notes.push_back(n);
        else
            right.notes.push_back(n);
    }
    SmfInput in;
    // Right first (piano convention), then Left; omit an empty hand.
    if (!right.notes.empty()) in.tracks.push_back(std::move(right));
    if (!left.notes.empty()) in.tracks.push_back(std::move(left));
    // A pedals-only take (no notes) still needs a home for the pedals.
    if (in.tracks.empty()) {
        SmfTrack only;
        only.name = "Right";
        in.tracks.push_back(std::move(only));
    }
    // CC64 pedal events attach to the FIRST emitted track (§4).
    in.tracks.front().pedals = take.pedals;
    return in;
}

std::string nextRecordingName(const std::vector<std::string>& existing) {
    const std::string prefix = "recording-";
    const std::string suffix = ".mid";
    std::set<int> used;
    for (const std::string& name : existing) {
        if (name.size() <= prefix.size() + suffix.size()) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) !=
            0)
            continue;
        std::string mid = name.substr(
            prefix.size(), name.size() - prefix.size() - suffix.size());
        if (mid.empty()) continue;
        int n = 0;
        bool digits = true;
        for (char c : mid) {
            if (c < '0' || c > '9') {
                digits = false;
                break;
            }
            n = n * 10 + (c - '0');
        }
        if (digits && n > 0) used.insert(n);
    }
    int next = 1;
    while (used.count(next)) ++next;
    return prefix + std::to_string(next) + suffix;
}

}  // namespace vialucis
