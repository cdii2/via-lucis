#include "vialucis/fx/afk_player.h"

#include "vialucis/fx/color_waves.h"
#include "vialucis/fx/demo_reel.h"
#include "vialucis/fx/fire2012.h"
#include "vialucis/fx/pacifica.h"
#include "vialucis/fx/pride2015.h"
#include "vialucis/fx/twinkle_fox.h"

namespace vialucis {
namespace fx {

std::unique_ptr<Effect> makeEffect(const std::string& name) {
    if (name == "rainbow") return std::make_unique<RainbowFx>();
    if (name == "confetti") return std::make_unique<ConfettiFx>();
    if (name == "sinelon") return std::make_unique<SinelonFx>();
    if (name == "juggle") return std::make_unique<JuggleFx>();
    if (name == "bpm") return std::make_unique<BpmFx>();
    if (name == "fire2012") return std::make_unique<Fire2012Fx>();
    if (name == "pacifica") return std::make_unique<PacificaFx>();
    if (name == "twinklefox") return std::make_unique<TwinkleFoxFx>();
    if (name == "colorwaves") return std::make_unique<ColorWavesFx>();
    if (name == "pride2015") return std::make_unique<Pride2015Fx>();
    return nullptr;
}

bool knownEffect(const std::string& name) {
    for (const std::string& n : effectNames())
        if (n == name) return true;
    return false;
}

const std::vector<std::string>& effectNames() {
    static const std::vector<std::string> kNames = {
        "rainbow", "confetti", "sinelon",    "juggle",     "bpm",
        "fire2012", "pacifica", "twinklefox", "colorwaves", "pride2015",
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
