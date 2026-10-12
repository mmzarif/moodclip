# Interactive Mood Hair Clip — ESP32-S3 Reverse TFT

A wearable display that tells people your mood so they don't have to ask.
Chunky pixel text and an animated face, clipped in your hair, running off a
small LiPo.

Recreation of [@meshtimes](https://instagram.com/meshtimes)' interactive hair
clip. The original used a LilyGO T-Display S3 and switched between two moods —
**hangry** and **fed**. This version runs on an Adafruit ESP32-S3 Reverse TFT
Feather, keeps those two exactly as they were, and adds nine more.

## What's here

| File | What it is |
|---|---|
| `firmware/mood_clip.ino` | Firmware — themed mood pairs, animated faces, 3 buttons, auto-cycle, fuel gauge, deep sleep |
| `stl/*.stl` | Printable case: tray, backplate, clip shoe (+ combined plate) |
| `cad/make_case.py` | Generates the STLs. Edit the `P` dict, re-run. |
| `docs/SETUP.md` | Arduino setup, libraries, first upload |
| `docs/BOM.md` | Parts list |
| `docs/ASSEMBLY.md` | Build order, battery safety, wearing it |

## Bill of materials

| Qty | Part | ~Cost |
|---|---|---|
| 1 | Adafruit ESP32-S3 Reverse TFT Feather (PID 5691) | $25 |
| 1 | Adafruit Lithium Ion Polymer 350–420 mAh (PID 2750 / 4236) | $7 |
| 1 | Alligator hair clip, ~10 mm | $0.50 |
| — | PLA or PETG, ~14 g | — |
| — | Charms, jump rings (optional) | — |

**~$33.** Full detail in `docs/BOM.md`.

## Quick start

**Firmware**

1. Arduino IDE → Boards Manager → install **esp32 by Espressif** (3.x).
2. Select **"Adafruit Feather ESP32-S3 Reverse TFT"**.
3. Library Manager → install: **Adafruit GFX**, **Adafruit ST7735 and ST7789**,
   **Adafruit MAX1704X**, **Adafruit NeoPixel** (Adafruit BusIO comes along).
4. Open `firmware/mood_clip.ino`, upload, then **press reset** — S3 boards need a
   manual reset after upload before the sketch runs.

**Case**

Import `stl/ALL_moodclip_plate.stl`, or the three parts individually. Details in
`docs/ASSEMBLY.md`.

## Controls

| Input | Action |
|---|---|
| **D2** tap | Toggle mood within the current theme (A ↔ B) |
| **D1** tap | Next theme |
| **D0** tap | Toggle auto-cycle (mood flips every 30 s) |
| **D0** hold 1.5 s | Deep sleep — tap **D2** to wake |
| **D1 + D2** together | Cycle brightness (5 levels) |

Theme, mood, brightness and auto-cycle all persist across power cycles and sleep.

## Themes

Each theme is a **pair** of moods you flip between with D2. D1 moves to the next
theme and keeps your A/B position, so switching mid-mood lands on the comparable
state instead of resetting.

### food

| Mood | Label | Background | Text | Face |
|---|---|---|---|---|
| A | `hangry` | hot pink | dark | frown |
| B | `fed` | lime | dark | smile |

### halloween

| Mood | Label | Background | Text | Face |
|---|---|---|---|---|
| A | `spooked!` | pumpkin `FF6A00` | black | **ghost** |
| B | `not scared` | night `1A0033` | candle `FF9E40` | smile |

The food theme is the reference build's two moods, faithful in colour and
wording. The halloween theme — palette, wording and the ghost face — comes from
the [IEEE Rising Stars badge](../../2025-IEEE-Rising-Stars-Badge) project's
`code/themes.txt`, which uses the same two-state idea.

Labels are **auto-shrunk to fit** rather than scrolled: at the default size the
column holds nine characters, so `not scared` steps down one size and still sits
still and centred. Only something too long even at the smallest size scrolls.
That matters for a head-worn display — you want it readable at a glance, not
caught mid-phrase.

Faces blink on their own per-mood timer, and the onboard NeoPixel flashes the
mood's colour for about half a second when you switch.

### Adding a theme

One entry in the `THEMES[]` table:

```cpp
{ "coffee",
  { "wired",   C_AMBER, C_INK,   FACE_SHOCK, 1100 },
  { "asleep",  C_LAV,   C_INK,   FACE_SLEEPY, 5200 } },
```

`THEME_COUNT` is computed from the array, so nothing else needs touching. Faces
available: `FACE_FROWN`, `FACE_SMILE`, `FACE_FLAT`, `FACE_WINK`, `FACE_STAR`,
`FACE_SLEEPY`, `FACE_DEAD`, `FACE_HEART`, `FACE_SHOCK`, `FACE_SPOOKY`,
`FACE_NONE`. Colours: the `C_*` constants, `rgb(r,g,b)`, or `hex(0xRRGGBB)` if
you're copying values out of the badge's `themes.txt`.

## Three things worth knowing

**The three buttons do not share a polarity.** D0 is also the BOOT pin and is
active-**LOW**; D1 and D2 are wired to 3V3 and are active-**HIGH** with no
external pulldowns. They need `INPUT_PULLDOWN` or they float and fire phantom
presses. The firmware handles this; just don't "simplify" it.

**`TFT_I2C_POWER` (GPIO7) gates the display AND the I2C rail.** It must be HIGH
before the TFT or the MAX17048 fuel gauge will respond. A dark screen and a
missing battery gauge are usually the same bug.

**Check LiPo polarity before plugging in.** These boards have no reverse-polarity
protection on BAT, and the JST-PH housing doesn't enforce a convention — pack
vendors differ. Adafruit-branded cells are correct out of the bag; anything else,
meter it first. Reversed polarity destroys the board.

## Battery life

At mid brightness with the scroll animation running, expect roughly **3–5 hours**
on a 350 mAh cell — the backlight dominates. Deep sleep is about 100 µA, so it'll
sit for weeks between wears. Drop brightness to level 1–2 to extend runtime
noticeably; the display is very readable indoors at level 2.

## Credit

Original concept and build by **@meshtimes**. Independent reimplementation for
personal making, not affiliated with or endorsed by the original creator. If you
post a build, credit them.
