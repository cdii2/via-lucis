# Via Lucis — Bring-Up Checklist

First-power, in order. **Do not skip steps and do not reorder them** — each step
proves one thing, so when something fails you know exactly where the problem is.

Format of every step: do the action → check the **Expected** result → if it
doesn't match, follow the **If not** link into
[TROUBLESHOOTING.md](TROUBLESHOOTING.md), fix it, and repeat the step before
moving on.

Steps marked **⚠ VERIFY-ON-HARDWARE** exercise behavior that could only be
guessed at before the real piano and strip existed (echo-guard timing,
calibration numbers, latency feel, power headroom). Expect to tune these; the
docs tell you which knob to turn.

Prerequisite: you finished [BUILD-GUIDE.md](BUILD-GUIDE.md) sections 1–3
(software installed, parts understood). Wiring can wait — the first steps use
the bare board.

Reminder that applies to every step: **USB cable and PSU are never connected at
the same time.**

---

## Step 1 — Power the bare ESP32 over USB

Action: no wiring attached at all. Plug the ESP32 into the PC with the USB-C
data cable.

**Expected:** a small red LED on the board lights. Windows Device Manager →
Ports (COM & LPT) shows a **Silicon Labs CP210x** entry (or CH340) with a COM
number.

If not → [No COM port](TROUBLESHOOTING.md#no-com-port-cp2102-driver).

## Step 2 — Flash the firmware

Action: in VSCode with the `firmware` folder open: PlatformIO alien icon →
**Project Tasks → esp32dev → General → Upload** (full walkthrough:
BUILD-GUIDE section 2c).

**Expected:** the bottom panel ends with a green **[SUCCESS]** line, something
like `======= [SUCCESS] Took 42.31 seconds =======`.

If not → [Upload fails](TROUBLESHOOTING.md#upload-fails).

## Step 3 — (Optional) See the boot banner

This step is the only one that peeks at the board's text output ("serial
monitor" — a window that shows what the board prints). It's optional but nice:
it proves the firmware is alive and tells you its WiFi address.

Action: in the same PlatformIO panel: **esp32dev → General → Monitor**. Then
press the small button labeled **EN** (or **RST**) on the ESP32 board to restart
it while you watch.

**Expected:** within a few seconds the monitor prints:

```
Via Lucis v0.1.0
WiFi AP "ViaLucis", ip=192.168.4.1
```

(or, once home WiFi is stored, `WiFi STA, ip=192.168.1.xx` — STA just means
"joined your home network"; the number is its address there.)

Close the monitor with the trash-can icon on that panel when done. If the
monitor shows nothing at all, unplug/replug USB and try Monitor again; if the
board never prints, re-do Step 2.

## Step 4 — Join the ViaLucis hotspot and load the web UI

Action: on your phone, join WiFi network **`ViaLucis`** (no password), then
browse to **`http://192.168.4.1`**.

**Expected:** the Via Lucis remote loads: a dark page with "Via Lucis" at the
top, a **WiFi chip** (small pill-shaped status light) lit in the header, a
**BLE chip** reading "No piano" (BLE = the Bluetooth link to the piano —
correct for it to say that for now), and Play / Songs / Settings buttons along
the bottom.

If the page never loads →
[WiFi joins but the UI is unreachable](TROUBLESHOOTING.md#wifi-joins-but-the-ui-is-unreachable).

## Step 5 — Store home WiFi and reach the UI on your network

Action: Settings page → WiFi card → enter home SSID + password → wait 2 seconds
→ tap **Reboot device** (details: BUILD-GUIDE section 6). Reconnect the phone
to your home WiFi. Now find the ESP32's new address, either way:

- **Serial way (PC + USB):** repeat Step 3; the banner line shows
  `WiFi STA, ip=...` — that IP is the address.
- **Router way (no PC):** open your router's admin page on your phone (the
  address printed on the router itself), look at the connected-devices list for
  a new device, and note its IP address.

Then browse to `http://<that address>` on your phone (e.g. `http://192.168.1.50`).

**Expected:** the same remote UI loads over your home network. Bookmark it.

If not →
[WiFi joins but the UI is unreachable](TROUBLESHOOTING.md#wifi-joins-but-the-ui-is-unreachable).
(Fallback that always works: if the ESP32 can't join your WiFi it re-opens the
`ViaLucis` hotspot after ~15 seconds — rejoin it at 192.168.4.1 and fix the
password.)

## Step 6 — Full hookup: PSU + strip

Action: **unplug USB.** Wire everything exactly per BUILD-GUIDE section 4
(table rows 1–14, plus the tidy-up jumpers), mount the strip per section 5.
Triple-check the 74AHCT125 power pins (14 → +, 7 and 1 → –). Then plug the PSU
into the wall.

**Expected:** the ESP32's red LED lights (it's now fed from the PSU through
VIN), the strip stays **completely dark** (no random colors — dark is correct;
nothing has told it to light yet), and after ~20 seconds the web UI is
reachable again at the same address as Step 5.

If LEDs glow dim red or the board keeps restarting →
[Dim red LEDs and brownouts](TROUBLESHOOTING.md#dim-red-leds-and-brownouts).
If the strip shows random flickering colors →
[Flickering or glitching colors](TROUBLESHOOTING.md#flickering-or-glitching-colors).

## Step 7 — Strip test pattern

Action: web UI → **Settings** → Strip calibration card → tap **Strip test**.

**Expected:** a single **white dot** appears at the LEFT (bass) end of the strip
and walks smoothly all the way to the far right end, then repeats. Every LED it
passes lights; none stay stuck on odd colors. Tap **Lights off** when satisfied.

⚠ VERIFY-ON-HARDWARE (PSU headroom): while the test runs, feel the breadboard
jumpers and screw-adapter connections — everything should stay cool. Also try
**Rainbow** for a minute: colors everywhere, no dimming toward the middle, no
board restart, connections still cool.

If the dot starts at the RIGHT end, the strip is mounted backwards — remount it
with the input end (LED 0) at the left (BUILD-GUIDE section 5).
If nothing lights →
[Flickering or glitching colors](TROUBLESHOOTING.md#flickering-or-glitching-colors)
(covers the no-light case too).
If the strip dims/reddens during Rainbow →
[Dim red LEDs and brownouts](TROUBLESHOOTING.md#dim-red-leds-and-brownouts).

## Step 8 — BLE: the piano connects itself ⚠ VERIFY-ON-HARDWARE

"BLE" is Bluetooth Low Energy — how the piano and the ESP32 talk. The ESP32
does the connecting: it constantly looks for the first Bluetooth-MIDI device it
can find and hooks on. You never "pair" anything by hand.

Action: turn the FP-30X on. Its Bluetooth is on from the factory; if it has
ever been switched off, see the FP-30X Owner's Manual section "Turning the
Bluetooth Function On/Off". Make sure **no phone/tablet piano app is running**
(Roland's apps grab the piano's Bluetooth and lock the ESP32 out). Wait up to
~30 seconds.

**Expected:** the **BLE chip** in the web UI header turns green and changes from
"No piano" to a connected name (shows as "BLE-MIDI").

If not → [BLE will not pair](TROUBLESHOOTING.md#ble-will-not-pair).

## Step 9 — Press a key: nothing should light

Action: with **no song playing** (fresh boot, nothing loaded), press any piano
key.

**Expected:** the piano makes its sound as normal, and the strip stays
**completely dark**. Lights respond to keys only inside a practice session —
a key press outside one means nothing. (This is also your first sanity check
that stray MIDI isn't triggering lights.)

If keys DO light things up here, or notes seem to clear themselves later →
[Notes clear themselves (echo)](TROUBLESHOOTING.md#notes-clear-themselves-echo).

## Step 10 — Upload a song and load it

Action: web UI → **Songs** → **Upload** → pick a `.mid` file from your phone
(any MIDI file under 256 KB works — the repo's song pack if present, or a
download from MuseScore.com or BitMidi saved to the phone first). Then tap the
song in the library list to load it.

**Expected:** the song appears in the library with its size, and after loading,
the Play page shows its name, its duration, and its tracks (e.g. Right/Left
hand rows).

If the upload is rejected, the message says why (only `.mid` names, 256 KB max).

## Step 11 — First wait-mode practice ⚠ VERIFY-ON-HARDWARE

Action: on the Play page, make sure the mode is **Wait**, then press **Play**.

**Expected, in this exact sequence:**

1. The first note (or chord) of the song lights its key position(s) at full
   brightness — green for right hand, blue for left. Nothing else happens; the
   song is **waiting for you**.
2. You may also see upcoming notes glowing faintly and swelling — that's the
   lookahead ramp, capped well below full brightness so "now" vs "next" is
   unmistakable.
3. Press the lit key(s). Each correct key's light goes out as you press it
   (chords clear per key), and once all are pressed, the song advances to the
   next note(s).
4. Press a deliberately WRONG key while notes are due: a **red flash** appears
   at the position of the key you pressed (about a third of a second), and the
   due notes stay lit until you press the right ones.

⚠ VERIFY-ON-HARDWARE items in this step:

- **Repeat-cue legibility at tempo:** load a song with fast same-key
  re-presses (or loop a bar with repeated notes) and play it in Follow mode
  at 100%+. The off-gap before each re-press should show a visible rising
  white glow, and the beat itself must land exactly on time. If the glow is
  too subtle in room light, raise "Glow peaks at" in Settings → Incoming
  re-press; if it reads as a flicker, raise "Minimum visible gap".
- **Echo guard window:** in Demo or Accompaniment mode the ESP32 sends notes TO
  the piano, and the piano may echo them back as if you played them. The
  firmware ignores echoes for 250 ms per sent note (setting `echoWindowMs`). If
  notes ever clear themselves without you touching a key →
  [Notes clear themselves (echo)](TROUBLESHOOTING.md#notes-clear-themselves-echo).
- **Calibration:** if lights sit above the WRONG keys (off by a key or a few
  millimeters) → run the alignment wizard (Step 11b below), or see
  [Wrong keys light up](TROUBLESHOOTING.md#wrong-keys-light-up-calibration).
- **Latency feel:** press-to-light-change should feel instant. If it lags →
  [Lights lag behind key presses](TROUBLESHOOTING.md#lights-lag-behind-key-presses).

## Step 11b — Alignment wizard walk ⚠ VERIFY-ON-HARDWARE

Action: web UI → **Settings** → **Calibration** → **Align lights to keys**.
Walk the whole wizard once on the real strip.

**Expected:**

1. The ready screen shows both checks green (piano connected, playback
   stopped). If you left the song playing, the Stop button there stops it.
2. A single white dot lights near the LEFT end of the strip. If it isn't
   clearly over a key, the ‹ › buttons visibly slide it. Press the key under
   it → the phone shows "Got it — <key name>" within a beat (BLE latency,
   well under a second).
3. Same for a dot near the RIGHT end.
4. The check step lights a dot over a middle key by itself — press that key;
   on a match the wizard finishes with "Lights aligned".
5. After finishing, load a song in Wait mode and confirm the lit positions
   sit squarely over the right keys across the whole range.

⚠ VERIFY-ON-HARDWARE items in this step:

- **Dot visibility at practice brightness** — the single white probe dot must
  be findable in normal room light. If it's too dim, raise Brightness in
  settings for the wizard run.
- **Capture correctness** — the key name shown must be the key you pressed
  (this exercises BLE note-on capture end to end).
- **Cancel restores** — cancel the wizard midway after at least one save and
  confirm the previous alignment still works (play a song; lights unchanged).
- **Reversed-mount check (optional but recommended once):** nothing to do if
  your strip runs left→right; if you mounted it right→left, the wizard must
  still finish and lights must land on the correct keys.

## Step 11c — Ambient lights walk ⚠ VERIFY-ON-HARDWARE

Action: unload any song (⏏ next to the song name), set **Start after idle**
to 30 seconds in Settings → Ambient lights, and wait without touching
anything.

**Expected:**

1. After ~30s the strip drifts into the ambient show (the playlist's first
   effect, or a gentle rainbow with no playlist).
2. Press any piano key → the ambient show stops instantly and the strip
   follows your playing (reactive free-play glow, velocity-sensitive; the
   sustain pedal holds glows).
3. Load a song → ambient can no longer start, no matter how long you idle
   (read music as long as you like).
4. Watch a full effect change (dwell) — the crossfade should be a gentle
   morph, never a hard cut. If ambient is too bright for the room, lower
   the **Ambient brightness cap**.

Afterwards set the idle timeout back (default 180s).

## Step 12 — Done

That's the whole trainer proven end to end. From here, everything is playtime:
try Follow / Demo / Accompaniment modes, the tempo slider, hand toggles, loops.
When alignment is confirmed after a few sessions, press the channels' adhesive
or fastening down properly.
