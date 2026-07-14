# Via Lucis — Bill of Materials (v2, ordered 2026-07-06)

All links verified live on 2026-07-06. Total ≈ **$121**. No soldering required.

| # | Part | Link | Price | Notes |
|---|------|------|-------|-------|
| 1 | **LED strip** — BTF-LIGHTING FCOB, 180 LED/m, 5V, 6.5ft (2m), 10mm wide, UL listed | [amazon.com/dp/B0D62KQ63Z](https://www.amazon.com/dp/B0D62KQ63Z) | $22.99 | One continuous piece — no mid-keyboard joint. WS2812B protocol. 88 keys span ~1.23m; trim spare at a marked cut line (every 12.5mm) or leave dark |
| 2 | **Brain** — ELEGOO ESP32 dev board, 3-pack, USB-C | [amazon.com/dp/B0D8T53CQ5](https://www.amazon.com/dp/B0D8T53CQ5) | $19.99 | Classic ESP-WROOM-32. Two spares |
| 3 | **Power** — ALITOVE 5V 10A supply, 5.5×2.5mm plug | [amazon.com/dp/B0852HL336](https://www.amazon.com/dp/B0852HL336) | $21.99 | Inject power at both strip ends. Firmware carries a FastLED power cap as backstop |
| 4 | **Level shifter** — SN74AHCT125N, DIP-14, 5-pack | [amazon.com/dp/B08FHD994N](https://www.amazon.com/dp/B08FHD994N) | $17.98 | Shifts ESP32 3.3V data to 5V. One used, four spares |
| 5 | **Breadboard + jumpers** — BOJACK kit | [amazon.com/dp/B08Y59P6D1](https://www.amazon.com/dp/B08Y59P6D1) | $9.99 | All junctions solderless |
| 6 | **Mounting** — Muzata aluminum U channel, 6× 1m | [amazon.com/dp/B01M09PBYX](https://www.amazon.com/dp/B01M09PBYX) | $19.99 | Use 2 end-to-end. **Leave milky diffuser cover off** — it blurs per-key edges. 10mm strip fits the ~12mm channel |
| 7 | **Barrel-to-screw adapters** — 5.5×2.5mm, 5 pairs, 20A | [amazon.com/dp/B0CXPK3TJ6](https://www.amazon.com/dp/B0CXPK3TJ6) | $7.99 | Must be 2.5mm (not the common 2.1mm) to match the ALITOVE plug. Use one female |

## Protection add-on (v2.1, added 2026-07-14 — order separately)

Adopted from WLED's wiring guidance (their 5V wiring diagram: *"for a safe setup,
fuses are not optional… with multiple injections it is also recommended to fuse
each injection wire separately"*), sized for this build. Links verified live
2026-07-14. Total ≈ **$61**. Still no soldering — everything lands in lever nuts
or the breadboard — but the power runs need a few wire ends cut and stripped
(hence the stripper). The kit's Dupont-style jumpers stay for the breadboard
side only: they're too thin (~26 AWG) for the WAGOs to clamp and must never
carry strip current.

| # | Part | Link | Price | Notes |
|---|------|------|-------|-------|
| 8 | **Inline fuse holders** — Parts Express ATO/ATC, 16AWG pigtails — **buy 2** | [amazon.com/dp/B003N8SD9G](https://www.amazon.com/dp/B003N8SD9G) | $3.65 ea | One per strip power feed (we inject at both ends) |
| 9 | **5A blade fuses** — standard ATC, 10-pack, blown-fuse LED indicator | [amazon.com/dp/B074F1T16F](https://www.amazon.com/dp/B074F1T16F) | $10.95 | 2 used, 8 spares. Each feed carries ≤~4A under the firmware's 8A cap, so 5A never nuisance-blows; the LED lights when a fuse dies |
| 10 | **Bulk capacitor** — 1000µF 16V radial electrolytic, 5-pack | [amazon.com/dp/B07YN6DS58](https://www.amazon.com/dp/B07YN6DS58) | $6.62 | 1 used, across the breadboard power rails. Smooths power-on inrush (WLED marks it recommended-to-optional) |
| 11 | **Lever nuts** — WAGO 221-415, 5-conductor, 10-pack | [amazon.com/dp/B06XH47DC2](https://www.amazon.com/dp/B06XH47DC2) | $8.95 | 4 used: two power-distribution junctions at the PSU adapter, two fuse-to-strip splices. Tool-free, reusable |
| 12 | **Hookup wire** — TUOFENG 22 AWG solid core, 6 colors × 30ft | [amazon.com/dp/B07TX6BX47](https://www.amazon.com/dp/B07TX6BX47) | $15.99 | For the WAGO→breadboard rail feeds (a Dupont jumper can't clamp in a WAGO). Solid 22 AWG fits both WAGO and breadboard; big spares for future projects |
| 13 | **Wire stripper** — Amazon Basics self-adjusting, 10–24 AWG | [amazon.com/dp/B07TS6KCXD](https://www.amazon.com/dp/B07TS6KCXD) | $10.79 | WAGOs need ~11mm of bare wire; covers the 16 AWG fuse leads and the 22 AWG hookup wire |

Why: the PSU can deliver 10 A. A chafed strip wire against the aluminum channel or
a popped jumper shorting +5V to GND would dump that full current through wiring
never rated for it — on a piano. A 5 A fuse on each strip feed blows first. This
change also moves strip power **off the breadboard rails** (spring rails are only
comfortably good for ~1–2 A; the strip may legitimately draw up to 8 A total),
which was the wiring's weakest point. Wiring details: BUILD-GUIDE section 4.

Rejected along the way (and why):

- *hntoolight WS2812B USB kit (B08DJCCJ16)* — right strip, wrong package: bundled RF
  controller we'd cut off, USB power browns out.
- *BTF SMD 144 LED/m ×2 (B01CDTELBE)* — fine, but two segments = a joint mid-keyboard;
  COB looks better and costs less.
- *16.4ft "300 LED" variants* — density trap: 300 LEDs over 5m = 60/m, too sparse for
  per-key alignment. Buy density, not length.
- *BTF FCOB 160 LED/m 1m ×2 (B0CNXKSWD7)* — good, superseded by the single 2m 180/m
  piece.

No cable between piano and ESP32 — the Roland FP-30X connects over Bluetooth MIDI.
ESP32 powers from the same 5V supply as the strip.
