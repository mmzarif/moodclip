# Bill of materials

| Qty | Part | Spec | ~Cost | Notes |
|---|---|---|---|---|
| 1 | Adafruit ESP32-S3 Reverse TFT Feather | PID **5691** | $25 | 240x135 ST7789, 3 buttons, onboard LiPo charger + MAX17048 fuel gauge |
| 1 | Adafruit Lithium Ion Polymer battery | **350 mAh (PID 2750)** or 400 mAh (PID 3898) | $7 | JST-PH 2-pin, correct polarity from the bag |
| 1 | Alligator hair clip | ~10 mm band, flat top | $0.50 | Any flat-topped metal clip |
| — | PLA or PETG | ~14 g | — | Case is 3 parts |
| — | Jump rings + charms | 4–6 mm rings | $3 | Optional, for the dangle loops |
| — | Thin CA or slow epoxy | — | $4 | Bonding the clip shoe |

**Total: ~$33** assuming you have filament and glue.

## Battery choice matters here

The case battery pocket is **30 × 25 × 4.5 mm**, sized for the **350 mAh
(PID 2750, 	~36 × 20 × 5 mm)** or the **400 mAh (PID 3898)**. Both fit.

| Adafruit cell | Size | Fits? |
|---|---|---|
| 350 mAh (2750) | 36 × 20 × 5 mm | **yes — recommended** |
| 400 mAh (3898) | 35 × 25 × 5 mm | yes, snug |
| 500 mAh (1578) | 36 × 28 × 6 mm | no — too thick/wide |
| 1200 mAh (258) | 62 × 35 × 5 mm | no — longer than the case |
| 2500 mAh (328) | 67 × 50 × 10 mm | no, and far too heavy to wear |

Adafruit's guide sets a **250 mAh minimum** for this board — below that the
charge current is too high a fraction of capacity. Don't go smaller than 350 mAh.

Weight matters more than you'd think for something clipped to hair. The board is
~7 g and a 350 mAh cell ~7 g; with the case you're at roughly **25 g**. That's
fine on a sturdy clip. A 1200 mAh cell would push past 40 g and sag.

## Things not to do

- **No alkaline or NiMH packs.** They destroy the onboard charger.
- **No 7.4 V / 2S RC packs.** Board expects a single 3.7 V cell.
- **Do not trust an unbranded JST-PH pack's polarity.** The connector housing
  carries no standard — Adafruit uses red = +, black = −, but RC-hobby packs are
  often reversed. There is no reverse-polarity protection on BAT. Meter it.

## Filament

| Part | Material | Amount |
|---|---|---|
| tray, backplate, clip_shoe | PETG or PLA+ | ~14 g |

PETG survives being dropped better, which matters for something worn on a head.
Any colour; white or translucent looks closest to the reference build.

## Tools

- 3D printer, 0.4 mm nozzle
- Flush cutters, tweezers
- Small file or craft knife (USB-C port opening — see `ASSEMBLY.md`)
- Thin CA glue or slow epoxy
- Pliers for jump rings, if adding charms
- Multimeter, if using a non-Adafruit battery
