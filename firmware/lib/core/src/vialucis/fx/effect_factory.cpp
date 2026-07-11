#include "vialucis/fx/afk_player.h"

#include "vialucis/fx/color_waves.h"
#include "vialucis/fx/fire2012.h"
#include "vialucis/fx/pacifica.h"
#include "vialucis/fx/twinkle_fox.h"

namespace vialucis {
namespace fx {

std::unique_ptr<Effect> makeEffect(const std::string& name) {
    if (name == "fire2012") return std::make_unique<Fire2012Fx>();
    if (name == "pacifica") return std::make_unique<PacificaFx>();
    if (name == "twinklefox") return std::make_unique<TwinkleFoxFx>();
    if (name == "colorwaves") return std::make_unique<ColorWavesFx>();
    return nullptr;
}

bool knownEffect(const std::string& name) {
    for (const std::string& n : effectNames())
        if (n == name) return true;
    return false;
}

const std::vector<std::string>& effectNames() {
    // SYNC NOTE: this list is mirrored in webui/index.html (AFK_EFFECTS),
    // tools/mock_device.py (AFK_EFFECTS) and docs/API.md — update all four
    // together (no runtime discovery endpoint yet, deliberately).
    static const std::vector<std::string> kNames = {
        "fire2012", "pacifica", "twinklefox", "colorwaves",
    };
    return kNames;
}

const Palette16* paletteByName(const std::string& name) {
    if (name == "rainbow") return &rainbowColors();
    if (name == "ocean") return &oceanColors();
    if (name == "forest") return &forestColors();
    if (name == "lava") return &lavaColors();
    if (name == "party") return &partyColors();
    if (name == "cloud") return &cloudColors();
    if (name == "heat") return &heatColors();
    return nullptr;
}

}  // namespace fx
}  // namespace vialucis
