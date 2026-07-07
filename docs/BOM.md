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
