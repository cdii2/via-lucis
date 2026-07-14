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

- [ ] All parts from [BOM.md](BOM.md), including the v2.1 protection add-on:
  - BTF-LIGHTING FCOB LED strip, 2 m, 180 LED/m, 5V (one continuous piece)
  - ELEGOO ESP32 dev board (the "brain" — a small computer board; the 3-pack has two spares)
  - ALITOVE 5V 10A power supply ("PSU" — the power brick)
  - SN74AHCT125N chip ("level shifter" — a 14-legged black chip; 5-pack, you use one)
  - Breadboard + jumper wires (the white plug-board and the colored wires)
  - 2 of the 6 Muzata aluminum channels (the U-shaped rails)
  - 1 **female** barrel-to-screw adapter (small green-and-black block a power plug clicks into)
  - 2 inline fuse holders + two **5A** blade fuses (protection add-on)
  - 1 capacitor, 1000µF (small black can with two legs; protection add-on)
  - 4 WAGO lever nuts (clear blocks with orange levers; protection add-on)
  - 22 AWG solid hookup wire + the wire stripper (protection add-on)
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

### The fuses and lever nuts (protection add-on)

- An **inline fuse holder** is a short wire with a plastic capsule in the middle;
  a blade fuse clicks inside. If a short circuit ever tries to pull more than
  5 amps through a strip power feed, the fuse burns out in an instant and cuts
  the power — a $1 part dies instead of a wire catching fire. Our fuses have a
  tiny LED that lights up when the fuse is blown, so you can see at a glance
  which one died.
- A **WAGO lever nut** joins 2–5 wires without tools: lift a lever, push a
  stripped wire end in all the way, snap the lever down. Tug gently to confirm
  it's gripped. They're reusable — lift the lever to release. We use four; put
  a piece of tape on each and label them **A, B, C, D** now, the wiring table
  refers to them by letter.
- **Which wire goes where.** WAGOs need ~11 mm of bare wire and at least 24 AWG
  thickness — the kit's skinny plug-in jumpers (Dupont wires) can't clamp in
  them and stay on the breadboard side only. The two power feeds from the
  adapter (table rows 2–3) carry the full strip current, so they must be thick:
  cut **one lead of each fuse holder in half** and use the freed 16 AWG offcut
  as that holder's row-2/row-3 feed (strip both cut ends). The two low-current
  feeds into the breadboard rails (rows 12–13) are cut from the **22 AWG solid**
  spools. If a fuse-lead offcut comes out too short to reach, fallback: land
  both fuse-holder leads directly in the adapter's + screw terminal instead and
  skip WAGO A — three wires under one screw is tighter to get right, which is
  why the WAGO route is the default.

### The capacitor has a direction too

The 1000µF capacitor is a small can with two legs and it is **polarized** —
connected backwards it can pop like a firecracker. Two ways to spot the negative
leg: the **stripe** printed down one side of the can marks the **negative** leg,
and the negative leg is the **shorter** one. Stripe/short leg → (–) rail, long
leg → (+) rail. Its job: it's a tiny local reservoir that smooths the electrical
"gulp" when the strip turns on or brightness jumps.

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

Strip power runs through **fuses and WAGO lever nuts**, never through the
breadboard — the breadboard's spring rails only carry the small ESP32 + chip
current. Label the four WAGOs A–D with tape before you start.

```
 WALL OUTLET
     │
 [ALITOVE 5V 10A PSU]
     │  5.5 x 2.5 mm barrel plug
     ▼
 [barrel-to-screw adapter, female]
     │(+)                             │(-)
     ▼                                ▼
 [WAGO A]  the + junction         [WAGO B]  the – junction
     ├─[FUSE 5A]─[WAGO C]─ strip LEFT  end +5V   ├── strip LEFT  end GND   ← injection 1
     ├─[FUSE 5A]─[WAGO D]─ strip RIGHT end +5V   ├── strip RIGHT end GND   ← injection 2
     └── breadboard (+) rail                     └── breadboard (–) rail
              │                                       │
              ├── ESP32 VIN                           ├── ESP32 GND
              ├── 74AHCT125 pin 14                    ├── 74AHCT125 pin 7
              └── capacitor + (long leg)              ├── 74AHCT125 pin 1 (1OE)
                                                      └── capacitor – (stripe)

 DATA (separate thin jumper wires):

 ESP32 G16 ────────────► 74AHCT125 pin 2 (1A)
                         74AHCT125 pin 3 (1Y) ────► strip DIN (input end)
```

### Parts-to-pins table (the frozen wiring — check every row twice)

| # | From | To | Wire |
|---|------|----|------|
| 1 | PSU barrel plug | barrel-to-screw adapter (click in) | — |
| 2 | Adapter **+** screw terminal | **WAGO A** | 16 AWG fuse-lead offcut (section 3) |
| 3 | Adapter **–** screw terminal | **WAGO B** | 16 AWG fuse-lead offcut (section 3) |
| 4 | **WAGO A** | fuse holder **1**, either pigtail (**5A fuse clicked in**) | holder's own wire |
| 5 | Fuse holder **1**, other pigtail | **WAGO C** | holder's own wire |
| 6 | Strip **input (left) end +5V** | **WAGO C** | strip's own power wire |
| 7 | **WAGO A** | fuse holder **2**, either pigtail (**5A fuse clicked in**) | holder's own wire |
| 8 | Fuse holder **2**, other pigtail | **WAGO D** | holder's own wire |
| 9 | Strip **far (right) end +5V** | **WAGO D** | strip's own power wire |
| 10 | Strip **input (left) end GND** | **WAGO B** | strip's own power wire |
| 11 | Strip **far (right) end GND** | **WAGO B** | strip's own power wire |
| 12 | **WAGO A** | breadboard **(+) rail** | 22 AWG solid, stripped both ends |
| 13 | **WAGO B** | breadboard **(–) rail** | 22 AWG solid, stripped both ends |
| 14 | Capacitor **+ (longer leg)** | **(+) rail** | its own legs |
| 15 | Capacitor **– (stripe side, shorter leg)** | **(–) rail** | its own legs |
| 16 | ESP32 **VIN** | **(+) rail** | jumper |
| 17 | ESP32 **GND** | **(–) rail** | jumper |
| 18 | 74AHCT125 **pin 14 (VCC)** | **(+) rail** | jumper |
| 19 | 74AHCT125 **pin 7 (GND)** | **(–) rail** | jumper |
| 20 | 74AHCT125 **pin 1 (1OE)** | **(–) rail** | jumper |
| 21 | ESP32 **G16** | 74AHCT125 **pin 2 (1A)** | jumper |
| 22 | 74AHCT125 **pin 3 (1Y)** | strip **DIN** (data, input end) | jumper |

WAGO port count sanity check: A holds 4 wires (adapter, two fuse pigtails,
breadboard feed), B holds 4 (adapter, both strip GNDs, breadboard feed), C and D
hold 2 each (fuse pigtail + strip wire). The fifth port on each stays empty.

Tidy-up (recommended, keeps the unused parts of the chip quiet): also jumper pins
**4, 5, 9, 10, 12, 13** of the 74AHCT125 to the **(–) rail**. These are the three
unused translator groups; parking them at ground is good practice. Pins 6, 8, 11
stay empty.

If the strip's wires end in a plug (a small white 3-pin connector) rather than
bare ends, the strip bag includes a mating pigtail — click it on and land the
pigtail's bare wire ends in the WAGOs instead (power) and the data wire per
row 22.

Key facts behind this wiring (for the curious; already accounted for):

- **All grounds are common.** ESP32, chip, strip, PSU — every GND meets at
  WAGO B / the (–) rail. Signals are meaningless without a shared ground.
- Power is injected at **both** strip ends so LED 359 is as bright as LED 0.
- **Each strip feed is fused at 5 A** (WLED's rule: fuses sized for the wire,
  below what the PSU can deliver). The firmware caps total strip power at
  **8 A**, so each feed carries at most ~4 A — a 5 A fuse never blows in normal
  use, only on a genuine short.
- The breadboard rails now carry **only** the ESP32 + level shifter current
  (well under 1 A). Strip current never touches the breadboard — spring rails
  aren't rated for it.
- Data pin **GPIO16** is frozen in the firmware (`led_output.h`), matching row 21.

**Triple-check before ever powering on:** rows 18–20 (chip power: pin 14 to (+),
pins 7 and 1 to (–) — reversed power kills the chip; you do have four spares, but
still), rows 14–15 (capacitor stripe to (–) — backwards it can pop), and both 5A
fuses actually clicked into their holders.

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
