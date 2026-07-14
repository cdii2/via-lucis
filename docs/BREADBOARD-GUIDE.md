# Via Lucis — Breadboard Guide (toddler-proof)

This is the slow, hold-your-hand version of the breadboard part of the build. It
covers **one thing only**: placing the ESP32, the level-shifter chip, and the
capacitor onto the breadboard, and running the small wires between them.

It is a companion to [BUILD-GUIDE.md](BUILD-GUIDE.md) **section 4**, not a
replacement. It re-tells the **exact same wiring** as the frozen §4 table — no new
choices, nothing to decide. If this guide and BUILD-GUIDE §4 ever seem to disagree,
**BUILD-GUIDE §4 wins** — tell the project so the mistake here gets fixed.

If you have never seen a breadboard before, you are the person this guide is for.
Read it once all the way through before you touch anything. Then do it for real,
one step at a time. **No power gets connected anywhere in this guide** — you are
only placing parts. Powering on comes much later, and only when
[BRINGUP.md](BRINGUP.md) tells you.

---

## Part 1 — What you're holding

### The breadboard

A **breadboard** is a white plastic block full of little holes. You push wire ends
and part legs into the holes; inside, hidden metal clips grab them and connect them
together. Nothing is glued or soldered — everything pulls back out. That is the
whole point: you can build a circuit and undo any mistake.

Three things to know about how the holes are connected:

- **The two long rails.** Along the top edge and along the bottom edge run long
  lines of holes, each marked with a coloured stripe: a **red line** and a
  **blue (or black) line**. Every hole along one line is connected to every other
  hole along that same line. We call the red line the **(+) rail** (this is where
  "5V" power lives) and the blue line the **(–) rail** (this is where "ground"
  lives). *"Ground" just means the shared 0-volt reference that every part measures
  against — every part's ground must end up on the same (–) rail.*
- **The short rows of 5 in the middle.** The big middle area is split into little
  groups of **5 holes in a short row**. The 5 holes in one group are connected to
  each other, and to nothing else. So a wire in one hole of a group is joined to
  whatever is in the other 4 holes of that same group.
- **The trench down the middle.** A groove (the **trench**) runs along the centre,
  splitting the board into a top half and a bottom half. The trench breaks the
  connection: a group above the trench is separate from the group below it. Chips
  are meant to sit *straddling* the trench so their two rows of legs land in
  separate groups.

That's it. "Connect a wire to chip pin 3" will always mean: *push the wire into any
free hole in the same 5-hole group as pin 3.* You never touch the metal pin itself.

### The level-shifter chip (74AHCT125)

The ESP32's data pin speaks in a **3.3-volt** whisper; the LED strip only listens
to a **5-volt** voice. The **74AHCT125** is a little black chip that translates the
ESP32's 3.3V whisper into the 5V voice the strip understands. Without it the strip
may work, flicker, or ignore the ESP32 entirely — so it is not optional.

It is a black rectangle with **14 legs** (7 down each long side). It plugs straight
into the breadboard, straddling the trench. It has a **direction** — one way works,
the other way can kill the chip — so Part 4 spends real care getting it the right
way round. (You have four spare chips in the 5-pack, BOM row 4, but let's not use
them.)

### The capacitor (1000µF)

The **capacitor** is a small black can with two legs (BOM row 10). Think of it as a
**tiny shock absorber for power**: when the lights suddenly draw a big gulp of
current, the capacitor smooths that gulp so the voltage doesn't dip and glitch.

It also has a **direction**, and this one matters a lot: a capacitor plugged in
backwards can **pop** like a firecracker. One leg is **negative**, one is
**positive**, and Part 6 shows you how to tell them apart before it goes anywhere
near the board.

### The jumper wires

The kit (BOM row 5) comes with thin coloured wires with a stiff pin on each end.
These are **jumper wires** (also called "Dupont wires"). They push into breadboard
holes to carry signals and small amounts of power between points. In this guide,
**every jumper stays on the breadboard side.** None of them ever carries the
strip's heavy power current — that runs through fat wires and lever-nuts, entirely
off the breadboard (see Part 8).

---

## Part 2 — Before you touch anything

Do these five things first, in order:

1. **Unplug everything.** The power supply (PSU) must be **out of the wall**. The
   USB cable must be **unplugged** from the ESP32 and from the PC. There must be
   **no power anywhere near this workbench.** (Rule for the whole build: USB and
   the PSU are never connected at the same time, and neither is connected during
   this guide at all.)
2. **Dry hands.** Make sure your hands are dry.
3. **Discharge static.** Touch something metal and earthed first — a radiator, a
   metal desk leg, your PC's metal case. Static electricity you can't feel can kill
   the level-shifter chip invisibly. Do this again any time you've walked across a
   carpet before picking the chip back up.
4. **Lay out only the four breadboard parts.** You need exactly these:

   | Part | BOM row | Looks like |
   |---|---|---|
   | Breadboard + jumper wires | row 5 | white plug-board + thin coloured wires |
   | ESP32 dev board | row 2 | small green circuit board with a metal box and a USB socket |
   | 74AHCT125 level shifter | row 4 | black chip, 14 legs, a notch on one short end |
   | 1000µF capacitor | row 10 | small black can, two legs, a stripe down one side |

5. **Set everything else aside.** The LED strip, the power supply, the fuses, the
   WAGO lever-nuts, the aluminium channel, the wire stripper — **none of those are
   breadboard parts.** They belong to the strip-power wiring, which is
   [BUILD-GUIDE.md](BUILD-GUIDE.md) §4, not this guide. Put them in a separate pile
   so you don't reach for them by mistake.

**Looks right when:** you have a bare breadboard, an ESP32, one black chip, one
capacitor, and a handful of jumper wires in front of you — and nothing is plugged
into power.

---

## Part 3 — How this guide names holes (read this once)

I will point at exact holes so you never have to guess. Here is the map I'm using:

- **Columns** are numbered **1, 2, 3 …** from **left to right** (the numbers are
  usually printed along the top edge of the board).
- **Rows** are lettered **A to J**. Row **A** is the edge **farthest from you**;
  row **J** is the edge **nearest you**. The trench runs between **E** and **F**.
- So a name like **`F8`** means **row F, column 8** — one specific hole. **`A8`**
  is directly behind it (same column, far side).
- **`(+) rail`** = any hole on the red-striped line. **`(–) rail`** = any hole on
  the blue/black-striped line. When I say "into the (+) rail," any red-line hole is
  fine — they're all the same wire.

If your board's columns aren't numbered, or start at a different place, **don't
worry about the exact numbers** — just count holes from the left. The *relative*
positions (which hole is next to which) are all that matter.

**Which board:** this guide assumes **one full-size (830-hole) breadboard** from
the BOJACK kit (BOM row 5). A **half-size (400-hole)** board works exactly the same
way — same relative layout, just fewer spare columns. If your ESP32 turns out too
wide to fit on one board (this is common with these boards), see **"If the ESP32
doesn't fit on one board"** in Part 7 — the chip and capacitor placement never
change.

**Wire colours** (only suggestions — the colour of a wire changes nothing
electrically, it just helps *you* keep track; these match the colours BUILD-GUIDE
already uses):

- **Red** jumper for anything going to the **(+) rail** (5V).
- **Black** jumper for anything going to the **(–) rail** (ground).
- **Green** jumper for the two **data** wires (they carry the signal, not power).

---

## Part 4 — Place the level-shifter chip (get its direction right)

This chip has a direction. We settle that first, before it goes in.

### 4.1 Find pin 1

Look at the chip. One short end has a **half-moon notch** cut into it (some chips
also, or instead, have a small **printed dot** near one corner). That notch/dot
marks the **pin-1 end**.

> **STOP and check — where pin 1 is.**
> Turn the chip so the **notch points to your LEFT**. Now the leg at the
> **front-left corner** (the corner nearest you, on the left) is **pin 1**. From
> pin 1, the legs count like this:
> - **Front row, left → right:** pins **1, 2, 3, 4, 5, 6, 7**.
> - **Back row, right → left:** pins **8, 9, 10, 11, 12, 13, 14**.
>
> So pin 7 is front-right, pin 8 is directly behind it (back-right), and pin 14 is
> back-left — directly behind pin 1.
>
> (BUILD-GUIDE §3 draws the chip standing upright with the notch at the *top* and
> pin 1 top-left. Same chip, same pin 1 — here it's just lying flat with the notch
> turned to the left. If you like, hold the §3 picture next to your chip to
> confirm.)

### 4.2 Seat the chip straddling the trench

Line the chip up so its two rows of legs will drop into **row F** (front) and
**row E** (back), straddling the trench, with **pin 1 in hole `F8`**.

That places every pin at a known hole:

| Pin | Name | Hole | Pin | Name | Hole |
|---|---|---|---|---|---|
| 1 | 1OE | **F8** | 14 | VCC (5V) | **E8** |
| 2 | 1A  | **F9** | 13 | 4OE | E9 |
| 3 | 1Y  | **F10** | 12 | 4A | E10 |
| 4 | 2OE | F11 | 11 | 4Y | E11 |
| 5 | 2A  | F12 | 10 | 3OE | E12 |
| 6 | 2Y  | F13 | 9 | 3A | E13 |
| 7 | GND | **F14** | 8 | 3Y | E14 |

Push the chip down **gently and evenly** until it is fully seated — the legs
disappear into the holes and the chip body rests just above the board, sitting over
the trench. If a leg splays out or folds under instead of going in, pull the chip
back out, straighten the leg with your fingers, and try again.

**Looks right when:** the chip sits flat across the trench, notch on the left, all
14 legs hidden in the board, pin 1 in `F8`. Nothing else is placed yet.

### 4.3 Wire the chip's power (three wires — go slowly)

Only three wires give the chip power. Getting these backwards is the one way to
kill it, so each is its own step with its own check.

> **STOP and check before each of these three.** Power to the wrong pin kills the
> chip. Confirm the pin number in the table above before you plug each wire.

**Step 1 — pin 14 to (+).** Take a **red** jumper. Put one end in a free hole in
pin 14's group — that's column 8, back half, e.g. hole **`A8`**. Put the other end
in any **(+) rail** (red-line) hole.
*Looks right when:* a red wire runs from behind the chip's back-left leg up to the
red rail.

**Step 2 — pin 7 to (–).** Take a **black** jumper. One end in a free hole in
pin 7's group — column 14, front half, e.g. **`J14`**. Other end in any **(–) rail**
(blue-line) hole.
*Looks right when:* a black wire runs from the chip's front-right leg to the blue
rail.

**Step 3 — pin 1 to (–).** Take another **black** jumper. One end in a free hole in
pin 1's group — column 8, front half, e.g. **`J8`**. Other end in any **(–) rail**
hole. (This pin, "1OE", must be tied to ground to switch the translator on.)
*Looks right when:* a black wire runs from the chip's front-left leg to the blue
rail.

> **STOP and check — the killer mistake.** Pin **14** goes to **(+)** (red).
> Pins **7** and **1** go to **(–)** (black). If you ever see pin 14 on a black
> wire or pin 7/1 on a red wire, fix it now — that is the reversal that fries the
> chip.

---

## Part 5 — Place the ESP32 and run the two data wires

### 5.1 Seat the ESP32

The ESP32 is the small board with a metal box (its radio) and a **USB-C socket** on
one short end. Like the chip, it plugs in **straddling the trench**, with its two
rows of pins in separate groups.

> **STOP and check — which way it faces.** Seat it to the **right of the chip**,
> leaving a few empty columns between them (around columns 30 and up). Turn it so
> the **USB socket points off the right edge** of the board, out where a cable
> could reach it without bumping anything. Press it down evenly until seated.

You don't need exact columns for the ESP32, because you'll find its pins by the
**labels printed on the board**, not by counting holes. Find these three printed
labels along its edges:

| Printed label | What it is |
|---|---|
| **VIN** (sometimes **5V**) | 5V power **in** |
| **GND** (there are several — any one is fine) | ground |
| **G16** / **GPIO16** / **D16** / **RX2** | the LED data output — **frozen, never changes** |

*(GPIO16 is the pin the firmware sends the light data out of. On many 30-pin ESP32
boards it is printed as **RX2**. If your board has no pin marked 16, search the web
for an image of "ESP32 DevKit 30 pin pinout" and find which printed label carries
GPIO16 — it's the RX2 pin.)*

**Looks right when:** the ESP32 straddles the trench to the right of the chip, USB
socket hanging off the right edge, and you can read the VIN, GND, and G16/RX2
labels.

### 5.2 Power the ESP32 (two wires)

**Step 4 — VIN to (+).** A **red** jumper from a free hole in the **VIN** pin's
group to any **(+) rail** hole.
*Looks right when:* a red wire joins VIN to the red rail.

**Step 5 — GND to (–).** A **black** jumper from a free hole in any **GND** pin's
group to any **(–) rail** hole.
*Looks right when:* a black wire joins GND to the blue rail.

### 5.3 The two data wires (the whole reason the chip is here)

**Step 6 — G16 to chip pin 2.** A **green** jumper from a free hole in the
**G16 / RX2** pin's group to a free hole in the **chip pin 2** group (column 9,
front half — e.g. **`H9`**). This carries the ESP32's 3.3V signal *into* the
translator.
*Looks right when:* a green wire runs from the ESP32's G16 pin across to the front
of the chip near pin 2.

**Step 7 — chip pin 3 toward the strip.** A **green** jumper with one end in a free
hole in the **chip pin 3** group (column 10, front half — e.g. **`H10`**). This is
where the *translated* 5V signal leaves the chip and goes to the LED strip's data
input.

> **Leave the other end of this green wire loose for now.** Its far end connects to
> the strip's **DIN** wire (*DIN = "data in," the strip's green data lead at its
> input end*). You make that connection later, when the strip is in place — it is
> **row 22** of the BUILD-GUIDE §4 table. The strip is not a breadboard part, so it
> doesn't go on here. Just park this wire's free end off to the side.

*Looks right when:* a green wire leaves the front of the chip near pin 3, with its
other end dangling free (not in any hole).

---

## Part 6 — Place the capacitor (get its direction right)

The capacitor is **polarized** — *polarized means it has a required direction; one
leg is positive, one is negative.* Backwards, it can pop.

> **STOP and check — find the negative leg.** Two clues, and they agree:
> - There is a **stripe** printed down one side of the can. The leg on the
>   **stripe** side is the **negative (–)** leg.
> - That same negative leg is usually the **shorter** of the two legs.
>
> So: **stripe side / shorter leg = negative (–).** The other (longer) leg is
> **positive (+)**. Say it out loud before you plug it in.

**Step 8 — negative leg to (–).** Bend the legs gently so they reach two nearby
rail holes. Put the **stripe / shorter** leg into a **(–) rail** (blue-line) hole,
around column 18.
*Looks right when:* the striped side of the can points toward the blue rail.

**Step 9 — positive leg to (+).** Put the **longer** leg into a **(+) rail**
(red-line) hole, next to where the negative leg went.
*Looks right when:* the capacitor stands with its longer leg in the red rail, its
striped/shorter leg in the blue rail. Give it a gentle look from the side: stripe →
blue, no-stripe → red.

---

## Part 7 — Making the rails one connected line, and two housekeeping notes

### 7.1 Make the whole (+) rail one wire, and the whole (–) rail one wire

On a full-size breadboard the rails sometimes have a **gap in the middle** — the
red line is really two separate half-lines, and so is the blue. And the top-edge
rails are separate from the bottom-edge rails. To keep this guide simple, make each
colour into **one long connected line** so "any red hole is +" is always true:

**Step 10 — bridge top (+) to bottom (+).** A **red** jumper from a top-edge red
rail hole to a bottom-edge red rail hole (straight down, any column, e.g. around
column 20).

**Step 11 — bridge top (–) to bottom (–).** A **black** jumper from a top-edge blue
rail hole to a bottom-edge blue rail hole (e.g. column 22).

**Step 12 — if a rail has a centre gap, bridge across it.** Look closely at the red
line and the blue line. If either has a visible break near the middle, add a short
jumper across that break (red across the red break, black across the blue break) so
the whole line is joined end to end.

*Looks right when:* you can trace an unbroken chain of red-rail holes across the
whole board, and the same for blue. These bridge wires carry no new circuit — they
just make "the (+) rail" and "the (–) rail" each a single line, exactly as the §4
table assumes.

### 7.2 Housekeeping — park the chip's unused pins (recommended)

The chip has three translator groups we don't use. Parking their inputs at ground
keeps them quiet (good practice, not strictly required). Add **black** jumpers from
each of these chip pins to any **(–) rail** hole:

| Chip pin | Hole |
|---|---|
| 4 | F11 group |
| 5 | F12 group |
| 9 | E13 group |
| 10 | E12 group |
| 12 | E10 group |
| 13 | E9 group |

Leave pins **6, 8, 11** empty. *Looks right when:* six short black wires run from
those chip pins to the blue rail, and nothing is plugged into pins 6, 8, or 11.

### 7.3 If the ESP32 doesn't fit on one board

Some ESP32 boards are so wide they cover every hole on one side of the trench,
leaving no free holes to wire to. If that's you (BUILD-GUIDE §3 warns about it):

- Set a **second breadboard** (the kit has spares) right beside the first.
- Plug the ESP32's **left pin row into board 1** and its **right pin row into
  board 2**, so each pin has free holes in its group.
- Join the two boards' rails: a **red** jumper from board 1's (+) rail to board 2's
  (+) rail, and a **black** jumper from board 1's (–) rail to board 2's (–) rail.
- Everything else — the chip, the capacitor, all the wiring in Parts 4–6 — stays
  exactly the same. The G16→pin 2 green wire (Step 6) simply reaches across from
  board 2 to board 1.

---

## Part 8 — What is NOT breadboard work (don't do it here)

The LED strip's heavy power does **not** touch the breadboard. Feeding 5V to the
strip runs through fat wire, two fuses, and the WAGO lever-nuts — off the board
entirely. That whole job is **BUILD-GUIDE §4, table rows 1–11**:

- **Rows 1–11** — the power supply → barrel adapter → WAGO A/B → fuses → WAGO C/D →
  both ends of the strip. **None of this is on the breadboard.** The thin jumper
  wires from your kit are too skinny to carry strip current and must never be used
  for it.
- **Rows 12–13** — the two wires that feed 5V from WAGO A/B into the breadboard's
  (+) and (–) rails. These are the **one** breadboard connection that waits. You'll
  push a 22 AWG wire into a **(+) rail** hole and one into a **(–) rail** hole
  during §4, because the *other* end of each goes into a WAGO in the very same step.
  **Leave those two rail holes empty for now.** Until they're fed, your breadboard
  has no power source attached — which is exactly what we want at the end of this
  guide.

Do all of that later, from the BUILD-GUIDE §4 table, when the strip is in place.

---

## Part 9 — What the finished breadboard should look like

Top-down sketch of what you built in Parts 4–7 (the ESP32 shown small; yours is
wider):

```
  TOP (+) red rail  ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●
  TOP (–) blue rail ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●
                     │                    │        │
  row A  . . . . . . │ . . . . . . . . . .│. . . . │. . . . .
   ...        (pin14)│red                 │bridge  │bridge
  row E  . . . [14 13 12 11 10  9  8] . . │. . . . │. . . . .   ← chip back row
  ─────────────────  T R E N C H  ───────────────────────────
  row F  . . . [ 1  2  3  4  5  6  7] . . │. . . . │[ E S P 32 ]  USB→
   ...    (pin1)│  │green│              (pin7)     │  VIN GND G16
  row J  . . . .│. │. . .│. . . . . . . . │. . . . │. . . . .
             black│  │   └─(pin3)green ──▶ loose, goes to strip DIN (§4 row 22)
                  │  └─(pin2) from ESP32 G16
        (pin1→–)  └────────────────┐
                                    ▼
  BOT (–) blue rail ●●●●●●●●●●●●●●●●●●●●●●  ← cap stripe leg here
  BOT (+) red rail  ●●●●●●●●●●●●●●●●●●●●●●  ← cap long leg here
                    (two rail-feed holes left EMPTY — filled in §4 rows 12–13)
```

Final eyeball pass, top to bottom:

- [ ] Chip: notch **left**, pin 1 at `F8`, all 14 legs seated over the trench.
- [ ] Chip power: pin **14 → red (+)**, pins **7 and 1 → black (–)**.
- [ ] ESP32: straddling the trench, USB socket off the right edge.
- [ ] ESP32 power: **VIN → red (+)**, **GND → black (–)**.
- [ ] Data: **G16 → chip pin 2** (green); **chip pin 3 → loose green wire** waiting
      for the strip.
- [ ] Capacitor: **stripe/short leg → blue (–)**, **long leg → red (+)**.
- [ ] Rails bridged into one red line and one blue line.
- [ ] (Optional) chip pins 4, 5, 9, 10, 12, 13 parked on **(–)**; pins 6, 8, 11
      empty.
- [ ] The two rail-feed holes are **empty** (they wait for §4).
- [ ] **Nothing is plugged into any power. No USB. No PSU.**

If any wire looks wrong, don't power anything — recheck it against the §4 table, or
see [TROUBLESHOOTING.md](TROUBLESHOOTING.md): a mis-seated or backwards chip shows up
under [Flickering or glitching colors](TROUBLESHOOTING.md#flickering-or-glitching-colors),
and power-path mistakes under
[Dim red LEDs and brownouts](TROUBLESHOOTING.md#dim-red-leds-and-brownouts) — but
those are for later, after power is on.

---

## Part 10 — Stop here

**Do not connect ANY power yet.** Not USB, not the PSU. The breadboard is done, but
it stays dark until the first-power checklist walks you through it safely.

Go to **[BRINGUP.md](BRINGUP.md) Step 1** — it tells you when and how to plug things
in, one verified step at a time.
