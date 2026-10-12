# Setup

## 1. Board support

Arduino IDE → **File → Preferences → Additional Boards Manager URLs**:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then **Tools → Board → Boards Manager**, search `esp32`, install
**esp32 by Espressif Systems**, version **3.x**.

Select **Tools → Board → esp32 → "Adafruit Feather ESP32-S3 Reverse TFT"**.

Recommended Tools settings:

| Setting | Value |
|---|---|
| USB CDC On Boot | **Enabled** (needed for Serial over USB) |
| PSRAM | QSPI PSRAM |
| Flash Mode | QIO 80 MHz |

## 2. Libraries

**Tools → Manage Libraries**, install:

- **Adafruit GFX Library**
- **Adafruit ST7735 and ST7789 Library**
- **Adafruit MAX1704X** (the onboard fuel gauge)
- **Adafruit NeoPixel**

Adafruit BusIO is pulled in automatically. Accept the dependency prompts.

## 3. Upload

1. Plug in via USB-C.
2. Upload.
3. **Press the reset button.** ESP32-S3 boards do not auto-start the new sketch
   after upload — this trips up nearly everyone once.

If the port doesn't appear, double-tap reset to force the ROM bootloader, then
upload and reset again.

## 4. First run checklist

Open Serial Monitor at **115200**. You should see:

```
[boot] mood=hangry auto=0 batt=4.05V 87%
```

| Symptom | Cause | Fix |
|---|---|---|
| Screen black, serial fine | `TFT_I2C_POWER` not HIGH, or backlight at 0 | The firmware sets both; confirm you didn't edit `setup()` |
| `[batt] MAX17048 not found` | No battery plugged in, or an older board | Gauge needs a cell present. Pre-Feb-2023 boards have an LC709203F at 0x0B instead — see below |
| Moods change on their own | D1/D2 floating | Confirm `INPUT_PULLDOWN` on pins 1 and 2 |
| Buttons do nothing | Polarity assumption wrong | D0 is active-LOW, D1/D2 active-HIGH. Not a typo. |
| Sleeps then wakes instantly | Pulldown lost across sleep | Firmware calls `rtc_gpio_pulldown_en()`; don't remove it |
| Board won't take an upload | S3 bootloader | Double-tap reset, upload, reset |

### Older board revisions

Boards manufactured before **2023-02-08** shipped a **LC709203F** fuel gauge at
I2C `0x0B` instead of the MAX17048 at `0x36`. Check the back silkscreen. If yours
is the older one, swap the library and the object:

```cpp
#include <Adafruit_LC709203F.h>
Adafruit_LC709203F fuelGauge;
// cellVoltage() and cellPercent() have the same names — nothing else changes
```

The firmware degrades gracefully if neither is found: it just shows a full
battery pip and skips the low-battery warning.

## 5. Customising themes

Themes live in one table near the top of the firmware. Each theme is a pair of
moods that D2 toggles between:

```cpp
static const Theme THEMES[] = {
  { "food",
    { "hangry",     C_PINK,    C_INK,    FACE_FROWN,  3200 },
    { "fed",        C_LIME,    C_INK,    FACE_SMILE,  4200 } },

  { "halloween",
    { "spooked!",   C_PUMPKIN, C_BLACK,  FACE_SPOOKY, 2600 },
    { "not scared", C_NIGHT,   C_CANDLE, FACE_SMILE,  4200 } },
};
// per mood:  label, background, text colour, face, blink interval ms (0 = never)
```

Add an entry and it joins the D1 rotation — `THEME_COUNT` is derived from the
array, so nothing else changes.

Available faces: `FACE_FROWN`, `FACE_SMILE`, `FACE_FLAT`, `FACE_WINK`,
`FACE_STAR`, `FACE_SLEEPY`, `FACE_DEAD`, `FACE_HEART`, `FACE_SHOCK`,
`FACE_SPOOKY`, `FACE_NONE`.

Colours: the `C_*` constants, `rgb(r, g, b)`, or `hex(0xRRGGBB)` — the last one
takes the same 6-digit hex the IEEE badge's `themes.txt` uses, so you can copy a
theme line across directly.

**Label length:** text is auto-shrunk to fit the column, stepping down from size
3 as needed (9 characters at size 3, 13 at size 2, 27 at size 1). Anything longer
than that scrolls. Keep labels under about 12 characters and they'll always sit
still, which reads far better on something worn in hair.

## 6. Auto-cycle period

Auto-cycle flips between the active theme's two moods. Default is 30 s — change
`autoPeriodMs` in the state initialiser, or clear the saved value:

```cpp
prefs.begin("mood", false); prefs.clear(); prefs.end();
```

Stored settings override the compiled defaults, so a stale NVS value will look
like your edit did nothing.
