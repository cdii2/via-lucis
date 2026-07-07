# Via Lucis — Simulator / Native Tests

Developer-facing. This is the one doc where a terminal is assumed.

## What "native" is

`firmware/platformio.ini` defines two environments:

- **`esp32dev`** — the real target (Arduino framework, FastLED, NimBLE, etc.).
- **`native`** — compiles the core logic **on your desktop** as a plain C++
  program and runs the unit tests there. No ESP32, no strip, no piano.

This works because everything testable lives in `firmware/lib/core/` as pure
C++ — no Arduino headers (`MidiSong`/`parseMidi`, `Scheduler`, `WaitMode`,
`EchoGuard`, `FrameRenderer`, `NoteEmitter`, `KeyLedMap`, `Settings`,
`TrackConfig`). The device layer in `firmware/src/` (FastLED, BLE, WiFi, web
server) is esp32-only and is NOT covered by native tests — it's compile-gated
only. ArduinoJson is the one shared dependency; it's portable C++ and runs
under native too.

Guard macros: `-DVIALUCIS_TARGET_NATIVE` vs `-DVIALUCIS_TARGET_ESP32` — core
code must never need the latter.

## Prerequisites (Windows)

1. **A GCC on PATH.** The `native` platform ignores `platform_packages`
   (the `toolchain-gccmingw32` line in platformio.ini is a no-op there — known
   quirk, see ASSUMPTIONS.md A3), so you need a system compiler:

   ```powershell
   winget install --id BrechtSanders.WinLibs.POSIX.UCRT
   ```

   Then open a **fresh** terminal (PATH updates don't reach already-open
   shells) and verify: `gcc --version`.

2. **PlatformIO CLI.** Either of:
   - The VSCode PlatformIO IDE extension — open a "PlatformIO Core CLI"
     terminal from the PlatformIO sidebar and `pio` is available; or
   - Standalone: `uv tool install platformio` (how this repo's dev machine is
     set up; pip is bootstrapped into its venv via ensurepip because esptool
     needs it).

## Running the tests

From the `firmware/` directory:

```powershell
pio test -e native
```

Run a single module while iterating:

```powershell
pio test -e native -f test_wait_mode
```

Also keep the target compiling — every change must still build for the chip:

```powershell
pio run -e esp32dev
```

### The FAIL/ERROR grep rule

**Do not trust the exit code or a green summary alone** (PROGRESS.md resume
recipe). A crashed test binary can truncate a run that still prints reassuring
lines. Always grep the full output:

```powershell
pio test -e native 2>&1 | Tee-Object -FilePath test.log | Out-Null
Select-String -Path test.log -Pattern "FAIL", "ERROR"
```

Zero matches from the `Select-String` = the run is actually green. Any match =
red, whatever the summary claimed.

## How the tests are organized

```
firmware/test/
  helpers/                 shared test utilities (NOT a test dir — no test_ prefix)
    smf_builder.h          builds Standard MIDI Files in memory for parser tests
  test_scaffold/           smoke test (version string)
  test_midi_parser/
  test_key_led_map/
  test_scheduler/
  test_wait_mode/
  test_frame_renderer/
  test_note_emitter/
  test_settings/
  test_track_config/
```

Rules PlatformIO imposes:

- One directory per module, named `test_<module>`, containing one
  `test_<module>.cpp`. Each directory is built as its **own executable**, so
  every one needs its own `main()`.
- Directories without the `test_` prefix (like `helpers/`) are ignored by the
  test runner — that's where shared headers go. Include them relative to the
  test dir, the way the existing tests do:
  `#include "../helpers/smf_builder.h"`.

The framework is **Unity** (`test_framework = unity` in platformio.ini) — the
minimal C test framework, not the game engine.

## Adding a module test

1. Create `firmware/test/test_mynewthing/test_mynewthing.cpp`.
2. Use this skeleton (matches `test_scaffold`):

   ```cpp
   #include <unity.h>

   #include "vialucis/mynewthing.h"

   void setUp() {}
   void tearDown() {}

   static void test_does_the_thing() {
       TEST_ASSERT_EQUAL_INT(42, vialucis::theThing());
   }

   int main(int, char**) {
       UNITY_BEGIN();
       RUN_TEST(test_does_the_thing);
       return UNITY_END();
   }
   ```

3. Every `static void test_...` function must be registered with `RUN_TEST` in
   `main` — Unity does not auto-discover.
4. The code under test must live in `firmware/lib/core/` and stay free of
   Arduino headers, or the native build breaks for everyone.
5. Run `pio test -e native -f test_mynewthing` until green, then the full
   suite + the esp32dev build + the grep rule before calling it done.

## What the simulator does NOT cover

Hardware-truth items are listed in PROGRESS.md under "Needs Hardware" and
flagged ⚠ VERIFY-ON-HARDWARE in BRINGUP.md: real FP-30X BLE pairing + echo
timing, strip calibration values, felt wait-mode latency, PSU headroom. Native
tests exercise the logic around them (e.g. `EchoGuard` windows, `KeyLedMap`
offsets) with assumed numbers; bring-up measures the real ones.
