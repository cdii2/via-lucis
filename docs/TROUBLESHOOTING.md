# Via Lucis — Troubleshooting

Symptom-first. Find your symptom in the list, follow its section top to bottom —
the checks are ordered from most likely to least likely. After every fix, go
back to the [BRINGUP.md](BRINGUP.md) step that sent you here and repeat it.

Golden rules while debugging:

- **Unplug the PSU from the wall before touching any wiring.** Every time.
- Change **one thing at a time**, then re-test.
- USB and PSU are never connected at the same time.

Symptoms:

- [Dim red LEDs and brownouts](#dim-red-leds-and-brownouts)
- [Flickering or glitching colors](#flickering-or-glitching-colors)
- [No COM port (CP2102 driver)](#no-com-port-cp2102-driver)
- [Upload fails](#upload-fails)
- [WiFi joins but the UI is unreachable](#wifi-joins-but-the-ui-is-unreachable)
- [BLE will not pair](#ble-will-not-pair)
- [Notes clear themselves (echo)](#notes-clear-themselves-echo)
- [Wrong keys light up (calibration)](#wrong-keys-light-up-calibration)
- [Lights lag behind key presses](#lights-lag-behind-key-presses)

---

## Dim red LEDs and brownouts

**Looks like:** LEDs that should be white or colored come out dim and reddish
(especially mid-strip or far from a power feed), or the ESP32 spontaneously
restarts when many LEDs light ("brownout" = the 5V supply sagging so low the
board resets).

White needs all three color channels at once, so it's the first thing to
starve — starving LEDs go red-ish because red survives at the lowest voltage.

Check, in order:

1. **Is power really injected at BOTH strip ends?** BUILD-GUIDE section 4 rows
   4–7. If only the left end is fed, the right half dims. Both +5V wires to the
   (+) rail, both GND wires to the (–) rail.
2. **Are the screw-adapter terminals tight?** Tug each wire gently; a loose
   screw terminal is a resistor. Re-seat and re-tighten.
3. **Are the power jumpers into the rails fully pushed in?** Push every power
   jumper (rows 2–9) firmly home. If you have them, use short thick jumpers for
   rows 2–7 — power wants fat, short paths.
4. **Feel the connections** after a minute of the Rainbow pattern. Warm screw
   terminals or jumpers = a bad junction carrying too much current through too
   thin a path. Fix that junction (thicker/shorter wire, tighter screw, or run
   the strip-end power wires directly into the screw adapter alongside the rail
   feeds instead of through the breadboard).
5. **Is it the right PSU?** It must say 5V 10A (the BOM ALITOVE). A random 5V
   2A charger will brown out immediately.
6. **Lower the load as a diagnostic:** Settings → Brightness slider down to
   ~80, retry. If the symptom vanishes at low brightness, the problem is
   definitely a power path, not the firmware — keep working checks 1–4. (The
   firmware already caps strip power at 8 A internally; a healthy build never
   hits the cap in normal practice, where only a handful of LEDs are lit.)

## Flickering or glitching colors

**Looks like:** random sparkles, wrong colors, occasional white flashes, LEDs
"snowing" — or the strip does nothing at all during the Strip test. This is
almost always the **data** path, not power.

Check, in order:

1. **Is the level shifter chip fully seated?** PSU off. Press the 74AHCT125
   down firmly and evenly — a half-seated leg is the classic flicker cause.
   Confirm no leg is bent under the chip (pull it out and inspect if unsure;
   pry gently with something flat, evenly from both ends).
2. **Is the chip the right way around?** Notch at top, pin 1 top-left
   (BUILD-GUIDE section 3 diagram). If it was in backwards it is probably dead —
   swap in one of the four spares, correctly oriented.
3. **Re-check the five data-path rows:** BUILD-GUIDE table rows 10–14.
   Especially pin 1 (1OE) to the (–) rail — if 1OE floats, output is dead or
   erratic. And confirm the ESP32 jumper really is on **G16**, not a neighbor.
4. **Common ground.** Every GND (ESP32, chip, both strip ends, PSU) must reach
   the same (–) rail. A missing ground makes data unreadable → glitch city.
5. **Data enters at the input end.** The strip's printed arrows point AWAY from
   the input end; pin 3 (1Y) must feed THAT end. Fed from the wrong end, the
   strip ignores everything (shows as "nothing lights").
6. **Keep the data jumper short** (well under ~30 cm if possible) and away from
   the power wires.
7. Still glitching? Swap the 74AHCT125 for a spare — static kills chips
   invisibly.

## No COM port (CP2102 driver)

**Looks like:** ESP32 plugged into the PC, red LED on, but Device Manager →
Ports (COM & LPT) shows nothing new (or shows a yellow-triangle "Unknown
device"), and PlatformIO Upload says it can't find a port.

Check, in order:

1. **Is it a data cable?** Charge-only USB cables power the board but carry no
   data — the #1 cause. Try a different USB-C cable, ideally one known to
   transfer files from a phone.
2. **Try another USB port** on the PC (prefer a rear port on a desktop).
3. **Install the driver by hand:** search the web for
   **"CP210x USB to UART Bridge VCP Drivers"** (Silicon Labs website), download
   the **CP210x Universal Windows Driver**, unzip, right-click `silabser.inf`
   → **Install**, then unplug/replug the board.
4. **Wrong chip variant?** If Device Manager shows **CH340** under "Other
   devices", your board uses the CH340 USB chip instead — search
   **"CH341SER driver"** (WCH website), install, replug.
5. Unplug/replug and watch Device Manager (View → Devices by connection helps):
   if literally nothing appears/disappears when you plug it in, try the second
   board from the ELEGOO 3-pack — boards do arrive dead occasionally.

## Upload fails

**Looks like:** PlatformIO's Upload task ends red, typically with
`Failed to connect to ESP32` or endless `Connecting........_____....`.

Check, in order:

1. **COM port exists?** If not, do [No COM port](#no-com-port-cp2102-driver)
   first.
2. **Hold the BOOT button.** On the ESP32 board are two tiny buttons: **BOOT**
   (sometimes IO0) and **EN** (sometimes RST). Click Upload again, and when the
   output shows `Connecting....`, **press and hold BOOT** for ~3 seconds, then
   release. Most "failed to connect" cases end right there. (Stubborn boards:
   hold BOOT, tap EN once, release BOOT, while it's connecting.)
3. **Close anything else using the port** — a still-open serial Monitor tab in
   PlatformIO (trash-can icon closes it), or another program. Only one program
   can hold a COM port.
4. **Opened the right folder?** VSCode must have the **`firmware`** folder open
   (BUILD-GUIDE 2c). If the PlatformIO Project Tasks panel doesn't show an
   `esp32dev` entry, the wrong folder is open — File → Open Folder → pick
   `via-lucis\firmware`.
5. If the upload starts (percent lines appear) but dies midway: different cable,
   different USB port, and don't touch the board while it writes.

## WiFi joins but the UI is unreachable

**Looks like:** the ESP32 is powered and (supposedly) on WiFi, but the browser
spins or says "can't reach this page".

First, know which of the two WiFi modes it's in:

- **Hotspot mode ("AP"):** the ESP32 runs its own network `ViaLucis`; the UI is
  always at **`http://192.168.4.1`** — but ONLY while your phone is joined to
  `ViaLucis`.
- **Home mode ("STA"):** the ESP32 joined your router; the UI is at whatever
  address the router gave it, and your phone must be on the same home WiFi.

Check, in order:

1. **Typed it right?** `http://192.168.4.1` — `http` not `https`, dots not
   commas, no `www`. If the browser silently "upgrades" to https, type the
   `http://` part explicitly.
2. **Phone on the right network?** For 192.168.4.1 the phone must be on
   `ViaLucis`. Phones love to hop back to home WiFi because `ViaLucis` has no
   internet — check the phone's WiFi screen says `ViaLucis` right now, and if
   it offers "this network has no internet — stay connected?", choose **stay**.
3. **In home mode, is the address right?** Find the real one:
   - Serial way: USB to PC (PSU off!), PlatformIO Monitor, press EN; the banner
     prints `WiFi STA, ip=192.168.1.xx`.
   - Router way: router's admin page → connected devices → newest device's IP.
   Addresses can change after a router reboot ("DHCP" hands them out); if the
   UI vanishes days later, re-find the IP the same way. Many routers let you
   pin ("reserve") the address for a device — do that once you find it.
4. **Do the `ViaLucis` hotspot test:** if the hotspot exists, join it and use
   192.168.4.1 — if that works, the device is fine and the problem is purely
   the home-network address (check 3). Note the hotspot only appears when the
   ESP32 could NOT join a stored home network (or none is stored).
5. **Phone and ESP32 on different bands/guest networks?** Guest WiFi networks
   often isolate devices from each other. Put the phone on the exact same
   network name you typed into the ESP32's settings. Also: the ESP32 only
   speaks **2.4 GHz** WiFi — if your router has a 5 GHz-only name, use the
   2.4 GHz one in the ESP32's WiFi settings.
6. Power-cycle the ESP32 (PSU off, 5 s, on) and give it ~20 s.

## BLE will not pair

**Looks like:** the BLE chip in the UI header stays red / "No piano" long after
the FP-30X is on.

How it's supposed to work: the ESP32 is the one that connects — it scans and
hooks onto the **first Bluetooth-MIDI device it finds**. There is no pairing
button, no PIN, nothing to do on the piano beyond having its Bluetooth on.

Check, in order:

1. **Is the piano's Bluetooth on?** It is from the factory; if it was ever
   turned off, the FP-30X Owner's Manual section **"Turning the Bluetooth
   Function On/Off"** shows the button-and-key combination to re-enable it.
2. **Close every piano app on your phone/tablet** (Roland Piano App, Piano
   Every Day, Piano Partner 2, GarageBand, any MIDI app). A Bluetooth-MIDI
   device accepts **one** connection at a time — if an app grabbed the piano
   first, the ESP32 is locked out. Force-close the apps, then also check the
   phone's **Bluetooth settings**: if the FP-30X appears there as *connected*,
   disconnect/forget it. Remember: the piano must pair with the **ESP32**, not
   with your phone — the phone only ever talks to the ESP32 over WiFi.
3. **Restart in the right order:** ESP32 off, piano off. Piano on first, wait
   until it's fully up, then ESP32 on. Give it ~30 s, watching the BLE chip
   (the UI re-checks it every few seconds).
4. **Other Bluetooth-MIDI gadgets nearby?** The ESP32 takes the FIRST
   Bluetooth-MIDI device it finds — if you own another one (a MIDI keyboard, a
   BLE-MIDI adapter), turn it off so the piano is the only candidate in the
   room.
5. Distance check: put the ESP32 within a few meters of the piano (it lives on
   the piano anyway, so this is usually already true).

## Notes clear themselves (echo)

**Looks like:** in wait mode (especially with Accompaniment on, or right after
Demo mode), due notes blink out as if pressed when nobody touched a key — the
song "plays itself" past notes.

Why: in Demo/Accompaniment the ESP32 **sends** notes to the piano so the piano's
sound engine plays them. Some pianos then echo those same notes back out, and
an echo looks exactly like a key press. The firmware's **echo guard** ignores,
for each note it sends, matching incoming notes for a short window —
**250 ms** by default (setting `echoWindowMs`). If the piano's echoes arrive
later than that window, they leak through and "press keys" for you.

Fix:

1. Confirm the diagnosis first: does it ONLY happen in Accompaniment/Demo,
   never in plain Wait mode with your hands off the keys? If yes, it's echo
   for sure.
2. Raise the echo window to 500 ms: open the web UI → **Settings** →
   **Echo window (ms)** → type `500`. It saves by itself a moment after you
   stop typing, and survives reboots.

   (Backup path if the field is ever missing — one copy-paste command from a
   PC on the same WiFi: click Start, type `powershell`, press Enter, then
   paste the line below with `192.168.1.50` changed to YOUR device's address
   and press Enter. It prints the settings back; confirm `echoWindowMs` says
   500.)

     ```powershell
     Invoke-RestMethod -Method Put -Uri "http://192.168.1.50/api/settings" -ContentType "application/json" -Body '{"echoWindowMs":500}'
     ```
3. Re-test Accompaniment. Still clearing itself? Repeat with 750, then 1000.
   Each raise trades a little "deafness" right after machine-sent notes for
   immunity to echoes, so stop at the first value that fixes it.
4. If notes clear themselves even in plain Wait mode with all phone apps closed
   and nobody touching the piano, that's not echo — power-cycle everything, and
   if it persists, note exactly when it happens; that's a firmware bug worth
   reporting.

## Wrong keys light up (calibration)

**Looks like:** the light for a note sits above the wrong key, or is off by a
few millimeters, or drifts — correct at the left end but one key off at the
right end.

**The fix is the alignment wizard.** Web UI → **Settings** → **Calibration**
→ **Align lights to keys**. The whole idea: the device lights one dot, you
press the key under it, and it learns where your keys actually are. No
measuring, no numbers.

The wizard walks you through it, but so you know what to expect:

1. **Ready check** — it won't start until your piano is connected over
   Bluetooth and playback is stopped (there's a Stop button right there).
2. **Align the ends** — a white dot lights near each end of the strip. Use
   the ‹ › buttons if the dot isn't clearly over a key (e.g. it's past the
   keyboard's edge), then press the key beneath it. It confirms each press
   ("Got it — C4") so a bumped key can't sneak in — hit "Try again" if the
   wrong press got captured.
3. **Check a key** — the device lights where it now *thinks* a middle key
   is; you press the key under the light. Match → done. Miss → your press
   itself becomes a new guide point exactly where the map was wrong, and it
   checks a different key. Two or three rounds handles even oddly-mounted
   strips.
4. **Fine-tune keys** (optional, from the finish screen) — nudge any single
   key's lights one LED at a time, with a "Show" button that lights the key
   so you can see what you're adjusting.

Mounted the strip right-to-left? Nothing special to do — the wizard notices
the direction from your two key presses and flips everything itself.

Cancel at any point keeps whatever alignment you had before — nothing is
final until the wizard finishes.

Notes for special cases:

- **Strip physically misplaced** (dots landing on the wall, half the
  keyboard dark): slide the strip/channel first — BUILD-GUIDE section 5.
  The wizard fixes mapping, not mounting.
- **The two manual dials** (Strip offset mm / LEDs per meter) still exist
  under **Advanced manual calibration** on the Settings page. They're the
  no-wizard path: offset slides all lights sideways (positive = toward the
  high keys; one LED ≈ 5.6 mm), LEDs-per-meter fixes stretch (right at one
  end, increasingly wrong toward the other — 180 for the BOM strip). Using
  the dials switches you back to standard 2-point alignment; running the
  wizard afterwards wins again.
- Alignment saves to the device and survives reboots — and if its saved
  file is ever corrupted, the device falls back to the manual dials'
  values rather than going dark.

## Lights lag behind key presses

**Looks like:** you press a lit key and its light goes out noticeably late, or
the ramp/step to the next note stutters.

The press→light path is piano → Bluetooth → ESP32 → strip, and it's built to
feel instant. Lag usually comes from the ESP32 being distracted or from radio
congestion (Bluetooth and 2.4 GHz WiFi share the same air).

Check, in order:

1. **Close extra UI tabs.** One phone with one Via Lucis tab open is the
   intended load. A PC browser tab + a phone tab + an old backgrounded tab all
   polling the device adds work.
2. **Move the ESP32** a bit — away from the router, microwave, or a pile of
   electronics; keep it near the piano. Small position changes can massively
   change 2.4 GHz interference.
3. **Try hotspot mode as a test:** temporarily blank the WiFi SSID in Settings
   (delete the name, wait 2 s, Reboot) so the ESP32 runs its own `ViaLucis`
   hotspot, join it with the phone, and practice. If lag disappears, your home
   WiFi is congested — either keep practicing in hotspot mode (everything works
   there; no internet needed) or restore the SSID and move the router/channel.
   To restore: Settings → re-enter SSID + password → Reboot.
4. Busy household WiFi hours (evening streaming) genuinely matter; test at a
   quiet time to confirm.
5. If lag survives hotspot mode with one tab, note it — that's a firmware
   performance issue to report, not a setup problem.
