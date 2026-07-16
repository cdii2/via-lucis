#pragma once
// Boot self-heal decision (B4). At boot each persisted config doc is loaded and
// classified; this pure decision turns that classification into an action so
// the policy is native-testable (App::begin only executes it).

#include <cstdint>

namespace vialucis {

// How a persisted config doc read at boot: Ok (present + readable), Absent
// (first boot / the v1 upgrade path), or Corrupt (present but unreadable, an
// unknown-higher schema, or unparseable).
enum class DocLoad : uint8_t { Ok, Absent, Corrupt };

struct SelfHeal {
    bool useDefaults;  // fall back to defaults in RAM
    bool resave;       // atomically re-write the defaults to flash
    bool configReset;  // raise the user-visible "config was reset" flag
};

// `parsedOk` = the doc both loaded AND parsed into a valid config. Absent is
// the normal first-boot/upgrade path: use defaults silently, DON'T flag a reset
// (nothing was lost, nothing to rewrite). A Corrupt read, or an Ok read whose
// content won't parse, is a REAL doc gone bad: use defaults, re-save them so the
// next boot is clean, and tell the user (configReset).
constexpr SelfHeal decideSelfHeal(DocLoad load, bool parsedOk) {
    if (load == DocLoad::Absent) return SelfHeal{true, false, false};
    if (load == DocLoad::Ok && parsedOk) return SelfHeal{false, false, false};
    return SelfHeal{true, true, true};
}

}  // namespace vialucis
