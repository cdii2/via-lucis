#include "vialucis/key_led_table.h"

namespace vialucis {
namespace TableBuilder {

KeyLedTable fromTwoPoint(const LedMapConfig& cfg) {
    KeyLedTable t;
    t.setLedCount(cfg.ledCount);
    for (uint8_t n = KeyLedTable::kFirstNote;
         n < KeyLedTable::kFirstNote + KeyLedTable::kKeyCount; ++n)
        t.set(n, ledsForNote(n, cfg));
    return t;
}

}  // namespace TableBuilder
}  // namespace vialucis
