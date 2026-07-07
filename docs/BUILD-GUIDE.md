# Via Lucis — Build Guide

This is the assembly-day bible. Follow it top to bottom. Every step is meant to be
done by one person, with no help, no soldering, and no terminal commands. When a
word looks like jargon, it is explained the first time it appears.

Read this whole page once before touching anything. Then do it for real.

When something doesn't behave as described, stop and open
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) — don't improvise wiring changes.

---

## 1. Before you start — checklist

Lay all of this on the table first:

- [ ] All 7 parts from [BOM.md](BOM.md):
  - BTF-LIGHTING FCOB LED strip, 2 m, 180 LED/m, 5V (one continuous piece)
  - ELEGOO ESP32 dev board (the "brain" — a small computer board; the 3-pack has two spares)
  - ALITOVE 5V 10A power supply ("PSU" — the power brick)
  - SN74AHCT125N chip ("level shifter" — a 14-legged black chip; 5-pack, you use one)
  - Breadboard + jumper wires (the white plug-board and the colored wires)
  - 2 of the 6 Muzata aluminum channels (the U-shaped rails)
  - 1 **female** barrel-to-screw adapter (small green-and-black block a power plug clicks into)
- [ ] A **USB-C data cable**. Important: some USB cables are charge-only and carry
      no data — the ESP32 will power up but your PC won't see it. A cable that came
      with a phone that transfers photos is usually fine.
- [ ] A Windows PC (for installing the firmware onto the ESP32).
- [ ] Your phone (it becomes the remote control).
- [ ] Your home WiFi network name and password, written down.
- [ ] The Roland FP-30X, powered on.

Two safety rules for the whole build:

1. **The PSU stays unplugged from the wall until this guide says "power on".**
2. **Never have the USB cable and the PSU connected at the same time.** USB is for
   installing firmware; the PSU is for running the lights. One or the other, never both.

---

## 2. Install the software on your PC (do this first, no wiring needed)

### 2a. USB driver (so Windows can talk to the ESP32)

The ESP32 board talks to your PC through a chip called a **CP2102**. Windows needs
a small driver program for it. Windows 11 usually installs it by itself, so first
just try:

1. Plug the ESP32 into your PC with the USB-C cable. A tiny red light on the board
   should come on.
2. Right-click the Start button → **Device Manager** → expand **Ports (COM & LPT)**.
3. If you see something like **"Silicon Labs CP210x USB to UART Bridge (COM5)"** —
   done, skip to 2b. ("COM port" is just Windows's name for a plugged-in
   serial device; the number after COM can be anything, that's normal.)

If instead you see nothing new, or a device with a yellow warning triangle:

- Search the web for **"CP210x USB to UART Bridge VCP Drivers"** — it's a free
  download on the Silicon Labs website. Get the **"CP210x Universal Windows Driver"**,
  unzip it, right-click the file `silabser.inf` inside → **Install**. Unplug and
  replug the board, then check Device Manager again.
- If Device Manager shows **"CH340"** instead of CP210x (some boards use a
  different chip), search for **"CH341SER driver"** (from the maker, WCH) and
  install that instead. Same result: a COM port appears.

Still no COM port? See [TROUBLESHOOTING.md — No COM port](TROUBLESHOOTING.md#no-com-port-cp2102-driver).

### 2b. VSCode + PlatformIO

**VSCode** (Visual Studio Code) is a free code editor from Microsoft.
**PlatformIO** is a free add-on for it that knows how to build firmware and copy
it onto an ESP32. You will only ever click buttons in it.

1. Search the web for **"Visual Studio Code download"**, download the Windows
   installer from Microsoft's official site, and install with all default options.
2. Open VSCode. On the far left is a vertical strip of icons. Click the one made
   of four squares (**Extensions**).
3. In the search box type **PlatformIO IDE**. Click the result by *PlatformIO*,
   then click **Install**. Wait — it downloads quite a lot the first time
   (a coffee's worth). When it asks to reload the window, let it.
4. When it's done, a small **alien head icon** appears in that left icon strip.
   That's PlatformIO.

### 2c. Open the firmware and install it onto the ESP32 ("flashing")

"Flashing" just means copying the program onto the board's memory.

1. Get the Via Lucis project folder onto this PC (copy the whole `via-lucis`
   folder, or download it from wherever the project lives).
2. In VSCode: **File → Open Folder…** and pick the **`firmware`** folder *inside*
   `via-lucis` (not the `via-lucis` folder itself). If VSCode asks whether you
   trust the authors, click **Yes, I trust**.
3. Keep the ESP32 plugged into USB.
4. Click the **alien head icon** (PlatformIO) in the left strip. A panel called
   **Project Tasks** opens.
5. Expand **esp32dev → General**, then click **Upload**.
6. The first build downloads tools and takes several minutes. A black panel at
   the bottom scrolls text; that's normal. You want it to end with a green
   **[SUCCESS]** line. (It finds the COM port by itself.)
7. If it ends in red with an error, see
   [TROUBLESHOOTING.md — Upload fails](TROUBLESHOOTING.md#upload-fails).

The board is now programmed. **Unplug the USB cable.** Set the board aside; time
to build the hardware.

---

## 3. Know your parts

### The LED strip has a direction

Look closely at the strip: printed on it are small **arrows**. Data flows in the
direction of the arrows. The end the arrows point *away from* is the **input
end** — the end where the data must enter. The very first light at the input end
is **LED 0**. The strip has 3 conductors:

| Strip wire | Meaning |
|---|---|
| **+5V** (usually red) | power in |
| **GND** (usually white/black) | ground ("ground" = the shared 0-volt reference; every part's GND must be connected together) |
| **DIN / data** (usually green, at the input end) | the signal that tells each LED what color to be |

Both ends of the strip have power wires — we will use **both** (called "power
injection": feeding power in at both ends so the far end doesn't go dim).

**Do not cut the strip.** The keyboard only needs about 1.23 m of the 2 m strip;
the spare simply stays dark. (Cutting is possible at the marked lines but is not
needed and can't be undone.)

### The level shifter chip (74AHCT125)

The ESP32 speaks in 3.3-volt signals; the strip wants 5-volt signals. This chip
translates. It's a black rectangle with 14 legs (a "DIP-14" package) that plugs
straight into the breadboard.

**Orientation:** one short edge of the chip has a **notch** (a half-moon dent) or
a small dot. Hold the chip so the notch is at the **top**. Pin 1 is then the
**top-left** leg, and pins count **down the left side (1–7), then up the right
side (8–14)**:

```
              notch
            ┌──. U .──┐
   1OE   1 ─┤         ├─ 14  VCC (5V)
   1A    2 ─┤         ├─ 13  4OE
   1Y    3 ─┤   74    ├─ 12  4A
   2OE   4 ─┤  AHCT   ├─ 11  4Y
   2A    5 ─┤   125   ├─ 10  3OE
   2Y    6 ─┤         ├─  9  3A
   GND   7 ─┤         ├─  8  3Y
            └─────────┘
```

We use only the "1" group: signal enters at **1A (pin 2)**, exits translated at
**1Y (pin 3)**, and **1OE (pin 1)** must be tied to ground to switch that group on.

Plug the chip in so it **straddles the center groove** of the breadboard (legs on
both sides of the ditch), notch pointing toward the breadboard's top edge. Push
gently and evenly until it's fully seated.

### How a breadboard works (30 seconds)

- The long red-striped row along the edge is the **(+) rail**; the blue/black
  striped row is the **(–) rail**. Every hole in one rail is connected
  internally. Everything that says "5V" goes to (+); everything "GND" to (–).
- In the main area, each **short row of 5 holes** is one connected group. The
  groove down the middle separates the two sides.
- So "connect a wire to chip pin 3" means: plug the wire into **any free hole
  in the same 5-hole row** as pin 3. You never touch the pin itself.

### Seating the ESP32

The ESP32's two pin rows plug into the breadboard straddling the center
groove. If the board is too wide to leave free holes next to its pins (common
with these boards), place **two breadboards side by side** (the kit has
spares) and plug the left pin row into one, the right pin row into the other —
now every pin has free holes in its row. Press down evenly until seated.

### The ESP32 pins you'll touch

| ESP32 pin label | What it's for here |
|---|---|
| **VIN** (sometimes "5V") | 5V power **in** when not on USB |
| **GND** (there are several; any one) | ground |
| **G16** / **GPIO16** / **D16** / **RX2** | the LED data output — **frozen, never changes** |

About that last row: different boards print different names for the same pin.
On many 30-pin ESP32 boards the pin for GPIO16 is labeled **RX2**. If your
board has no pin marked 16, search the web for an image of
**"ESP32 DevKit 30 pin pinout"** and find which printed label carries
**GPIO16** — on those diagrams it's the RX2 pin.

---

## 4. Wiring

**PSU unplugged from the wall. USB unplugged. Now wire everything.**

### The complete picture

```
 WALL OUTLET
     │
 [ALITOVE 5V 10A PSU]
     │  5.5 x 2.5 mm barrel plug
     ▼
 [barrel-to-screw adapter, female]
     │(+)                     │(-)
     ▼                        ▼
 breadboard (+) rail      breadboard (–) rail
     │                        │
     ├── strip LEFT end  +5V  ├── strip LEFT end  GND      ← power injection 1
     ├── strip RIGHT end +5V  ├── strip RIGHT end GND      ← power injection 2
     ├── ESP32 VIN            ├── ESP32 GND
     └── 74AHCT125 pin 14     └── 74AHCT125 pin 7
                              └── 74AHCT125 pin 1 (1OE)

 DATA (separate thin jumper wires):

 ESP32 G16 ────────────► 74AHCT125 pin 2 (1A)
                         74AHCT125 pin 3 (1Y) ────► strip DIN (input end)
```

### Parts-to-pins table (the frozen wiring — check every row twice)

| # | From | To | Wire |
|---|------|----|------|
| 1 | PSU barrel plug | barrel-to-screw adapter (click in) | — |
| 2 | Adapter **+** screw terminal | breadboard **(+) rail** | thick jumper |
| 3 | Adapter **–** screw terminal | breadboard **(–) rail** | thick jumper |
| 4 | Strip **input (left) end +5V** | **(+) rail** | strip's own power wire |
| 5 | Strip **input (left) end GND** | **(–) rail** | strip's own power wire |
| 6 | Strip **far (right) end +5V** | **(+) rail** | strip's own power wire |
| 7 | Strip **far (right) end GND** | **(–) rail** | strip's own power wire |
| 8 | ESP32 **VIN** | **(+) rail** | jumper |
| 9 | ESP32 **GND** | **(–) rail** | jumper |
| 10 | 74AHCT125 **pin 14 (VCC)** | **(+) rail** | jumper |
| 11 | 74AHCT125 **pin 7 (GND)** | **(–) rail** | jumper |
| 12 | 74AHCT125 **pin 1 (1OE)** | **(–) rail** | jumper |
| 13 | ESP32 **G16** | 74AHCT125 **pin 2 (1A)** | jumper |
| 14 | 74AHCT125 **pin 3 (1Y)** | strip **DIN** (data, input end) | jumper |

Tidy-up (recommended, keeps the unused parts of the chip quiet): also jumper pins
**4, 5, 9, 10, 12, 13** of the 74AHCT125 to the **(–) rail**. These are the three
unused translator groups; parking them at ground is good practice. Pins 6, 8, 11
stay empty.

If the strip's wires end in a plug (a small white 3-pin connector) rather than
bare ends, the strip bag includes a mating pigtail — click it on and put the
pigtail's bare wire ends into the breadboard/adapter instead.

Key facts behind this wiring (for the curious; already accounted for):

- **All grounds are common.** ESP32, chip, strip, PSU — every GND meets at the
  (–) rail. Signals are meaningless without a shared ground.
- Power is injected at **both** strip ends so LED 359 is as bright as LED 0.
- The firmware caps total strip power at **8 A** (out of the PSU's 10 A) as a
  safety backstop, and the ESP32 shares the same 5V supply.
- Data pin **GPIO16** is frozen in the firmware (`led_output.h`), matching row 13.

**Double-check rows 10–12 before ever powering on:** pin 14 to (+), pins 7 and 1
to (–). Reversing power on the chip kills it (you do have four spares, but still).

---

## 5. Mounting the channel on the FP-30X

1. Take **2** of the 1 m Muzata aluminum channels. **Leave the milky plastic
   diffuser covers in the box** — the bare strip gives crisp per-key light; the
   diffuser blurs it (this is a locked design decision).
2. Place the two channels **end-to-end** on the piano's body, in the flat area
   directly **above/behind the keys**, running the full width of the keyboard.
3. Alignment rule: the **left end of the left channel lines up with the left
   edge of the lowest key** (the leftmost white key, the low A called **A0**).
4. Lay the LED strip into the channel with the **input end (LED 0 — the end
   where the data wire enters) at the LEFT / bass side**. LED 0 sits at the left
   edge of A0. The lights should face up/out toward you.
5. The keyboard spans about 1.23 m, so the strip and channel extend past the
   highest key to the right. That overhang stays dark — normal and expected.
6. Don't fasten anything permanently until after bring-up proves the alignment
   (the channels can just rest in place, or use a small piece of removable tape
   at each end). The strip has adhesive backing — **leave the backing on for
   now**; you may need to slide it a few millimeters during calibration.
7. Route the wires off the **left** side to wherever the breadboard and ESP32
   sit, out of the way of your hands.

---

## 6. First WiFi setup (phone becomes the remote)

The firmware is already on the board from step 2c. How it behaves on boot:

- If it knows your home WiFi, it joins it.
- If it doesn't (first boot ever), it opens its **own** WiFi network — a hotspot
  named **`ViaLucis`**, no password.

To teach it your home WiFi:

1. Power the ESP32 (either via the PSU per section 4, or — if the strip isn't
   wired yet — via USB from any charger; both are fine, just never both at once).
2. On your phone: **Settings → WiFi** → join the network named **`ViaLucis`**.
   Your phone may warn "no internet connection" — that's correct, stay on it.
3. Open the phone's web browser and go to: **`http://192.168.4.1`**
   (type it exactly; note it's `http`, not `https`).
4. The Via Lucis remote appears. Tap **Settings** (gear icon at the bottom).
5. In the **WiFi** card, enter your home network's **SSID** (that's just the
   WiFi network's name, exactly as your phone shows it, capitals and all) and
   its password.
6. Wait ~2 seconds (it saves automatically), then tap **Reboot device**.
7. The ESP32 restarts and joins your home WiFi. Reconnect your phone to your
   **home** WiFi, then find the device's new address — see
   [BRINGUP.md](BRINGUP.md) step 5 for exactly how.

If it can't join your WiFi (wrong password etc.), it falls back to the
`ViaLucis` hotspot after ~15 seconds, so you can always get back in and fix it.

---

## 7. What's next

Go to [BRINGUP.md](BRINGUP.md) and run the first-power checklist in order. It
takes you from "bare board on USB" to "first song in wait mode", one verified
step at a time.
