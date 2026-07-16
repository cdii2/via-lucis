#include "vialucis/song_parse_cache.h"

#include <unordered_set>

namespace vialucis {

bool SongParseCache::needsRecompute(const std::string& name,
                                    size_t size) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return true;
    return it->second.size != size;
}

void SongParseCache::set(const std::string& name, size_t size,
                         bool parseOk) {
    entries_[name] = Entry{size, parseOk};
}

bool SongParseCache::get(const std::string& name) const {
    auto it = entries_.find(name);
    return it != entries_.end() && it->second.parseOk;
}

bool SongParseCache::has(const std::string& name) const {
    return entries_.find(name) != entries_.end();
}

void SongParseCache::prune(const std::vector<std::string>& currentNames) {
    std::unordered_set<std::string> keep(currentNames.begin(),
                                         currentNames.end());
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (keep.count(it->first) == 0) it = entries_.erase(it);
        else ++it;
    }
}

}  // namespace vialucis
