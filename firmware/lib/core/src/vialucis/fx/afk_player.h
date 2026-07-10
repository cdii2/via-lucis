#pragma once
// AFK playlist sequencer (E3, brief §3): a looping media-player over
// effect-config tracks — play top→bottom→loop, shuffle / next / previous /
// repeat-current, per-effect dwell, crossfade through a second frame
// buffer, global brightness cap + master speed + LED range (whole strip vs
// above-keys, read from the per-key table). Replaces the M2 rainbow stub.
//
// Runs unattended by definition — the default brightness cap ships
// conservative, and every frame passes through the cap before the global
// FastLED power cap downstream (iron rule).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vialucis/fx/effect.h"
#include "vialucis/fx/rng.h"
#include "vialucis/key_led_table.h"

namespace vialucis {
namespace fx {

struct AfkTrack {
    std::string effect;   // factory name ("fire2012", "pacifica", ...)
    std::string palette;  // "" = the effect's default
};

struct AfkConfig {
    std::vector<AfkTrack> tracks;  // empty ⇒ the built-in fallback
    bool shuffle = false;
    bool repeatCurrent = false;
    uint32_t dwellSec = 60;      // clamped to >= 5 (dwell=0 is a config bug)
    uint32_t crossfadeMs = 2000;
    uint8_t brightnessCap = 96;  // conservative: it runs unattended
    float masterSpeed = 1.0f;    // 0.25..4, scales effect time
    bool aboveKeysOnly = false;  // limit to the table's key span
};

class AfkPlayer {
public:
    explicit AfkPlayer(uint16_t ledCount);

    void setTable(const KeyLedTable& t);
    void setConfig(const AfkConfig& c, uint32_t seed);
    const AfkConfig& config() const { return cfg_; }
    size_t currentTrack() const { return current_; }

    void next();
    void previous();

    // One fixed-timestep frame into `out` (sized ledCount): advances dwell,
    // renders the active effect (and the incoming one during a crossfade),
    // blends, applies the brightness cap and the LED range mask.
    void render(std::vector<Rgb>& out);

private:
    Effect* effectFor(size_t track);
    void startTrack(size_t index);
    size_t pickNext() ;

    uint16_t ledCount_;
    AfkConfig cfg_;
    KeyLedTable table_;
    uint16_t spanFirst_ = 0, spanLast_ = 0;  // above-keys range
    bool haveSpan_ = false;

    std::vector<std::unique_ptr<Effect>> effects_;  // one per track
    std::unique_ptr<Effect> fallback_;              // empty-playlist stub
    Rng rng_{1};
    uint32_t seed_ = 1;

    size_t current_ = 0;
    uint32_t frameInTrack_ = 0;
    uint32_t fxFrame_ = 0;       // global fixed-timestep counter
    bool fading_ = false;
    size_t fadeTo_ = 0;
    uint32_t fadeFrame_ = 0;     // frames into the crossfade
    std::vector<Rgb> bufA_, bufB_;
};

// The effect factory (E1 ports): nullptr for unknown names. Lives with the
// player so the REST layer can validate track names with the same truth.
std::unique_ptr<Effect> makeEffect(const std::string& name);
bool knownEffect(const std::string& name);
const std::vector<std::string>& effectNames();
const Palette16* paletteByName(const std::string& name);  // "" / unknown → null

// /afk.json + GET/PUT /api/afk wire shape (VL2 — documents get resources).
// fromJson validates track effect names against the factory; on failure
// `err` gets the typed message and `out` is untouched.
std::string afkConfigToJson(const AfkConfig& c);
bool afkConfigFromJson(const char* json, AfkConfig& out, std::string* err);

}  // namespace fx
}  // namespace vialucis
