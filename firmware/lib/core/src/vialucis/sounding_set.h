#pragma once
// The one "notes currently sounding" concept (R3). Every consumer that
// tracks live notes — the engine's lights, the scheduler's timeline walk,
// the emitter's piano-bound notes — adds on note-on and erases the FIRST
// entry matching the note-off's identity (first-match, not all: the same
// note can overlap across voices). Deleting this class re-scatters that
// loop into three call sites.
//
// Entry identity differs per consumer (note+track vs note+channel vs
// note+channel+track), so matching is a caller-supplied predicate. Backed
// by a plain vector: tiny sizes, no allocation beyond what each consumer
// already did.

#include <cstddef>
#include <vector>

namespace vialucis {

template <typename Entry>
class SoundingSet {
public:
    void add(const Entry& e) { items_.push_back(e); }

    // Erase the first entry matching `pred`; false if none matched.
    template <typename Pred>
    bool eraseFirst(Pred pred) {
        for (size_t i = 0; i < items_.size(); ++i) {
            if (pred(items_[i])) {
                items_.erase(items_.begin() + i);
                return true;
            }
        }
        return false;
    }

    // Visit every entry (stop/seek/panic: emit its note-off), then empty.
    template <typename Fn>
    void drain(Fn fn) {
        for (const Entry& e : items_) fn(e);
        items_.clear();
    }

    void clear() { items_.clear(); }
    bool empty() const { return items_.empty(); }
    const std::vector<Entry>& items() const { return items_; }

private:
    std::vector<Entry> items_;
};

}  // namespace vialucis
