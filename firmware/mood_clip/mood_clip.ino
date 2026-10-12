/*
 * Interactive Mood Hair Clip
 * Target: Adafruit ESP32-S3 Reverse TFT Feather (PID 5691)
 * Battery: Adafruit Lithium Ion Polymer, 350-500 mAh, JST-PH 2-pin
 *
 * A wearable 240x135 display that shows your current mood as chunky pixel text
 * with an animated face, so people know whether you're "hangry" or "fed"
 * without having to ask.
 *
 * Inspired by @meshtimes' interactive hair clip (original used a LilyGO
 * T-Display S3). This is an independent reimplementation. The two-state
 * theme model and the halloween palette/ghost come from the IEEE Rising
 * Stars badge project (code/themes.txt).
 *
 * ── Controls ─────────────────────────────────────────────────────────────
 *   D2 tap          toggle mood within the current theme  (A <-> B)
 *   D1 tap          next theme  (food -> halloween -> ...)
 *   D0 tap          toggle auto-cycle (mood flips on a timer)
 *   D0 hold 1.5s    deep sleep  (tap D2 to wake — ext0 is single-pin)
 *   D1+D2 together  cycle brightness
 *
 * ── Board-specific gotchas this code handles ──────────────────────────────
 *   - The three buttons have DIFFERENT polarities. D0 is also the BOOT pin and
 *     is active-LOW with a pullup; D1/D2 are wired to 3V3 and are active-HIGH
 *     with NO external pulldowns, so they need INPUT_PULLDOWN or they float and
 *     register phantom presses.
 *   - TFT_I2C_POWER (GPIO7) gates the LDO feeding both the display and the I2C
 *     rail. It must be HIGH before the TFT or the MAX17048 fuel gauge respond.
 *   - INPUT_PULLDOWN is NOT retained across deep sleep. Waking on D2 requires
 *     rtc_gpio_pulldown_en() or the pin floats high and wakes instantly.
 *   - Rendering goes through a GFXcanvas16 and one drawRGBBitmap() blit.
 *     Drawing straight to the TFT flickers badly on animation.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_MAX1704X.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"
#include "driver/gpio.h"

// ───────────────────────── types ─────────────────────────
//
// These MUST be declared before the first function definition in the sketch.
// The Arduino builder injects auto-generated prototypes immediately above the
// first function it finds, so a later function taking `Face` or `Btn` as a
// parameter would get a prototype emitted before the type exists:
//   error: 'Face' has not been declared
// (arduino-cli #2696; `static` does not exempt a function from generation.)

enum Face : uint8_t {
  FACE_FROWN, FACE_SMILE, FACE_FLAT, FACE_WINK,
  FACE_STAR, FACE_SLEEPY, FACE_DEAD, FACE_HEART, FACE_SHOCK,
  FACE_SPOOKY, FACE_NONE
};

struct Mood {
  const char* text;      // label; '\n' splits it into stacked lines
  uint16_t    bg;
  uint16_t    fg;
  Face        face;
  uint16_t    blinkMs;   // 0 = never blink
  uint8_t     textSize = 0;  // 0 = auto-fit; otherwise fixed, scrolls if too wide
};

// A theme is a pair of moods you toggle between — the same two-state idea as the
// IEEE Rising Stars badge's themes.txt, where each line is
//   name | WORD A | sub A | WORD B | sub B | bgA,fgA,faceA | bgB,fgB,faceB
// Here the sub-captions are folded into the scrolling label, since this display
// shows one line.
struct Theme {
  const char* name;
  Mood        a;         // first state
  Mood        b;         // after tapping D2
};

struct Btn {
  uint8_t  pin;
  bool     stable = false;
  bool     lastRaw = false;
  uint32_t edgeMs = 0;
  uint32_t downMs = 0;
  bool     longFired = false;
};

// Forward declarations. Same reason as the types above: the Arduino builder emits
// generated prototypes ahead of the first function definition, so any function
// whose signature names a custom type needs a real prototype here.
void enterDeepSleep();
static void drawFace(int16_t x, int16_t y, int16_t s, Face f, uint16_t col,
                     bool blinking);
static bool update(Btn& b, uint32_t now, bool* released, bool* held1500);

// ───────────────────────── hardware ─────────────────────────

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_MAX17048 fuelGauge;
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Preferences prefs;

static const int16_t W = 240;    // landscape, rotation 3
static const int16_t H = 135;

// Text column geometry. Shared between render() and applyMood() so the scroll
// entry point matches the wrap point — setting scrollX = W instead left the
// label 76 px off the right edge, i.e. ~1.1 s of empty panel after every change.
static const int16_t FACE_SCALE = 3;
static const int16_t INK_X0 = 3, INK_Y0 = 5, INK_X1 = 19, INK_Y1 = 21;
static const int16_t INK_W = (INK_X1 - INK_X0) * FACE_SCALE;   // 48
static const int16_t INK_H = (INK_Y1 - INK_Y0) * FACE_SCALE;   // 48
static const int16_t TEXT_LEFT = 14 + INK_W + 10;              // 72
static const int16_t TEXT_W = W - TEXT_LEFT - 4;               // 164
static const int16_t TEXT_SIZE = 3;
static const int16_t GLYPH_W = 6 * TEXT_SIZE;                  // 18

// 240*135*2 = 64,800 bytes. Comfortable in the S3's 512 KB SRAM.
GFXcanvas16 canvas(W, H);

// Buttons. Note the polarity difference — this is not a typo.
static const uint8_t BTN_MODE = 0;   // D0, active LOW  (shared with BOOT)
static const uint8_t BTN_PREV = 1;   // D1, active HIGH
static const uint8_t BTN_NEXT = 2;   // D2, active HIGH

static inline bool btnDown(uint8_t pin) {
  return (pin == BTN_MODE) ? (digitalRead(pin) == LOW)
                           : (digitalRead(pin) == HIGH);
}

// ───────────────────────── colour helpers ─────────────────────────

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// 24-bit hex, the way the badge's themes.txt writes colours, converted to the
// display's RGB565 at compile time.
static constexpr uint16_t hex(uint32_t c) {
  return (uint16_t)((((c >> 16) & 0xFF) & 0xF8) << 8 |
                    (((c >> 8) & 0xFF) & 0xFC) << 3 |
                    ((c & 0xFF) >> 3));
}

// Palette from the reference build: hot pink for hangry, lime for fed.
static const uint16_t C_PINK    = rgb(255, 105, 180);
static const uint16_t C_LIME    = rgb(160, 255,  40);
static const uint16_t C_INK     = rgb( 26,  20,  35);
static const uint16_t C_CORAL   = rgb(255, 120, 100);   // low-battery warning

// Halloween colours lifted directly from the IEEE Rising Stars badge themes.txt:
//   halloween | SPOOKED! | BOO | NOT SCARED | BRING CANDY
//             | FF6A00,000000,spooky | 1A0033,FF9E40,smile
static const uint16_t C_PUMPKIN  = hex(0xFF6A00);
static const uint16_t C_BLACK    = hex(0x000000);
static const uint16_t C_NIGHT    = hex(0x1A0033);
static const uint16_t C_CANDLE   = hex(0xFF9E40);

// ───────────────────────── themes ─────────────────────────
//
// D2 toggles between the two moods of the active theme.
// D1 switches theme.
//
// Faces are drawn as chunky filled rects so they read as pixel art at any scale,
// matching the reference's blocky aesthetic. (Face/Mood/Theme are declared at the
// top of the file — see the note there.)
//
// Labels are the badge's WORDs. They're auto-shrunk to fit rather than scrolled
// (see fitTextSize in render): at size 3 the column holds only nine characters,
// so concatenating the sub-captions made every label scroll, and you'd catch it
// mid-phrase instead of reading it at a glance. Shrinking keeps the real wording
// and keeps it still — which is what you want on something worn on your head.
static const Theme THEMES[] = {
  { "food",
    { "hangry",     C_PINK,    C_INK,    FACE_FROWN,  3200 },
    { "fed",        C_LIME,    C_INK,    FACE_SMILE,  4200 } },

  { "halloween",
    { "spooked!",   C_PUMPKIN, C_BLACK,  FACE_SPOOKY, 2600 },
    { "not scared. bring candy",
                    C_NIGHT,   C_CANDLE, FACE_SMILE,  4200, 5 } },
};
static const uint8_t THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

static inline const Mood& activeMood(uint8_t theme, uint8_t state) {
  return state ? THEMES[theme].b : THEMES[theme].a;
}

// ───────────────────────── state ─────────────────────────

RTC_DATA_ATTR uint8_t rtcTheme = 0;     // survive deep sleep
RTC_DATA_ATTR uint8_t rtcState = 0;

struct {
  uint8_t  theme       = 0;             // index into THEMES
  uint8_t  moodState   = 0;             // 0 = mood A, 1 = mood B
  bool     autoCycle   = false;
  uint32_t autoPeriodMs = 30000;
  uint8_t  brightness  = 3;             // index into BRIGHT_LEVELS
  int16_t  scrollX     = 0;
  uint32_t lastFrameMs = 0;
  uint32_t lastMoodMs  = 0;
  uint32_t blinkUntil  = 0;
  uint32_t nextBlinkMs = 0;
  float    battPct     = 100.0f;
  float    battVolts   = 4.2f;
  bool     gaugeOK     = false;
  bool     lowBatt     = false;
  uint32_t dirtyAt     = 0;             // deferred settings write
  bool     showBadge   = false;         // brief overlay after a change
  uint32_t badgeUntil  = 0;
  // Owns its bytes. Storing a const char* here was a real bug: the brightness
  // badge passes a stack buffer from handleButtons(), which dies on return
  // while render() keeps reading it for ~900 ms.
  char     badgeText[24] = {0};
  uint32_t pixelUntil  = 0;             // 0 = pixel already dark
} st;

static const uint8_t BRIGHT_LEVELS[] = { 12, 40, 90, 160, 255 };
static const uint8_t BRIGHT_COUNT = sizeof(BRIGHT_LEVELS);

void enterDeepSleep();

// ───────────────────────── persistence ─────────────────────────

static void saveSettings() {
  prefs.begin("mood", false);
  prefs.putUChar("theme", st.theme);
  prefs.putUChar("state", st.moodState);
  prefs.putBool("auto", st.autoCycle);
  prefs.putUChar("bright", st.brightness);
  prefs.putULong("period", st.autoPeriodMs);
  prefs.end();
}

static void loadSettings() {
  prefs.begin("mood", true);
  st.theme        = prefs.getUChar("theme", 0);
  st.moodState    = prefs.getUChar("state", 0);
  st.autoCycle    = prefs.getBool("auto", false);
  st.brightness   = prefs.getUChar("bright", 3);
  st.autoPeriodMs = prefs.getULong("period", 30000);
  prefs.end();
  if (st.theme >= THEME_COUNT) st.theme = 0;
  if (st.moodState > 1) st.moodState = 0;
  if (st.brightness >= BRIGHT_COUNT) st.brightness = 3;
  if (st.autoPeriodMs < 5000 || st.autoPeriodMs > 600000) st.autoPeriodMs = 30000;
}

static void applyBacklight() {
  // TFT_BACKLITE drives a low-side NMOS, so PWM works directly.
  analogWrite(TFT_BACKLITE, BRIGHT_LEVELS[st.brightness]);
}

static void badge(const char* msg) {
  strlcpy(st.badgeText, msg, sizeof(st.badgeText));   // copy, never alias
  st.showBadge = true;
  st.badgeUntil = millis() + 900;
}

// Refresh everything derived from (theme, moodState).
//
// `persist` is false for automatic (timer-driven) toggles: auto-cycle fires every
// 30 s, and the 2.5 s coalescing window can't merge events that far apart, so
// marking dirty every time meant ~2,880 flash commits a day for no benefit.
static void applyMood(bool announce, bool persist = true) {
  rtcTheme = st.theme;
  rtcState = st.moodState;
  st.scrollX = TEXT_W;            // re-enter at the column's right edge
  st.lastMoodMs = millis();
  st.nextBlinkMs = millis() + activeMood(st.theme, st.moodState).blinkMs;
  if (persist) st.dirtyAt = millis();
  if (announce) {
    // tint the onboard NeoPixel to match — a tiny bit of extra delight
    uint16_t c = activeMood(st.theme, st.moodState).bg;
    uint8_t r = ((c >> 11) & 0x1F) << 3;
    uint8_t g = ((c >> 5) & 0x3F) << 2;
    uint8_t b = (c & 0x1F) << 3;
    pixel.setPixelColor(0, pixel.Color(r / 6, g / 6, b / 6));   // dim
    pixel.show();
    st.pixelUntil = millis() + 600;     // flash, don't latch
  }
}

// D2: flip between the active theme's two moods.
static void toggleMood(bool persist = true) {
  st.moodState ^= 1;
  applyMood(true, persist);
  Serial.printf("[mood] %s / %s\n", THEMES[st.theme].name,
                activeMood(st.theme, st.moodState).text);
}

// D1: next theme. Keeps the current A/B position so switching theme mid-mood
// lands on the comparable state rather than always resetting to A.
static void nextTheme() {
  st.theme = (st.theme + 1) % THEME_COUNT;
  applyMood(true);
  badge(THEMES[st.theme].name);
  Serial.printf("[theme] %s\n", THEMES[st.theme].name);
}

// ───────────────────────── face drawing ─────────────────────────
//
// Coordinates are in a 24x24 box; `s` scales it up. Everything is rects so the
// result stays crisp and pixel-flavoured rather than smoothly antialiased.

static void px(int16_t x, int16_t y, int16_t s, int16_t bx, int16_t by,
               int16_t w, int16_t h, uint16_t col) {
  canvas.fillRect(x + bx * s, y + by * s, w * s, h * s, col);
}

static void drawFace(int16_t x, int16_t y, int16_t s, Face f, uint16_t col,
                     bool blinking) {
  if (f == FACE_NONE) return;

  // Ghost, ported from the IEEE badge's "spooky" face: domed head, straight
  // sides, scalloped hem, hollow eyes and mouth. Drawn as per-row spans rather
  // than the usual rect primitives, because the dome and the three bumps along
  // the bottom need curvature — the blocky rect vocabulary can't express them.
  if (f == FACE_SPOOKY) {
    const int16_t cx = 11;              // body centre within the 24-wide box
    const int16_t halfW = 8;            // straight-side half width
    const int16_t top = 3;              // dome apex
    const int16_t shoulder = 11;        // dome meets the straight sides here
    const int16_t hem = 18;             // where the scallops begin

    for (int16_t by = top; by <= hem; by++) {
      int16_t half;
      if (by < shoulder) {
        // half-disc dome
        int16_t dy = shoulder - by;
        int16_t r2 = halfW * halfW - dy * dy;
        half = (r2 > 0) ? (int16_t)(sqrtf((float)r2) + 0.5f) : 0;
      } else {
        half = halfW;
      }
      if (half > 0) px(x, y, s, cx - half, by, half * 2, 1, col);
    }

    // three scalloped bumps along the hem
    for (int16_t i = 0; i < 3; i++) {
      int16_t bx = cx - halfW + 3 + i * 5;
      px(x, y, s, bx - 2, hem + 1, 4, 1, col);
      px(x, y, s, bx - 1, hem + 2, 2, 1, col);
    }

    // punch the eyes and mouth back out — bg, so the face reads as hollow.
    // Blinking closes the eyes to slits, same idiom as the other faces.
    const uint16_t bg = activeMood(st.theme, st.moodState).bg;
    if (blinking) {
      px(x, y, s, 6, 10, 3, 1, bg);
      px(x, y, s, 13, 10, 3, 1, bg);
    } else {
      px(x, y, s, 6, 8, 3, 4, bg);
      px(x, y, s, 13, 8, 3, 4, bg);
    }
    px(x, y, s, 9, 14, 4, 3, bg);       // open wailing mouth
    return;
  }

  // eyes
  if (blinking || f == FACE_SLEEPY) {
    px(x, y, s, 3, 9, 5, 1, col);
    px(x, y, s, 14, 9, 5, 1, col);
  } else if (f == FACE_STAR) {
    // four-point sparkle eyes
    for (int16_t ex : {3, 14}) {
      px(x, y, s, ex + 2, 5, 1, 5, col);
      px(x, y, s, ex, 7, 5, 1, col);
    }
  } else if (f == FACE_WINK) {
    px(x, y, s, 3, 6, 4, 4, col);
    px(x, y, s, 14, 9, 5, 1, col);          // winking eye
  } else if (f == FACE_DEAD) {
    for (int16_t ex : {3, 14}) {            // x-shaped eyes
      for (int16_t k = 0; k < 4; k++) {
        px(x, y, s, ex + k, 6 + k, 1, 1, col);
        px(x, y, s, ex + 3 - k, 6 + k, 1, 1, col);
      }
    }
  } else if (f == FACE_HEART) {
    for (int16_t ex : {3, 14}) {
      px(x, y, s, ex,     6, 2, 1, col);
      px(x, y, s, ex + 3, 6, 2, 1, col);
      px(x, y, s, ex,     7, 5, 2, col);
      px(x, y, s, ex + 1, 9, 3, 1, col);
      px(x, y, s, ex + 2, 10, 1, 1, col);
    }
  } else if (f == FACE_SHOCK) {
    px(x, y, s, 3, 5, 5, 6, col);
    px(x, y, s, 14, 5, 5, 6, col);
  } else {
    px(x, y, s, 3, 6, 4, 4, col);
    px(x, y, s, 14, 6, 4, 4, col);
  }

  // mouth
  switch (f) {
    case FACE_SMILE:
    case FACE_STAR:
    case FACE_HEART:
      px(x, y, s, 5,  16, 1, 2, col);
      px(x, y, s, 6,  18, 10, 1, col);
      px(x, y, s, 16, 16, 1, 2, col);
      break;
    case FACE_FROWN:
      px(x, y, s, 5,  19, 1, 2, col);
      px(x, y, s, 6,  17, 10, 1, col);
      px(x, y, s, 16, 19, 1, 2, col);
      break;
    case FACE_WINK:
      px(x, y, s, 6,  17, 9, 1, col);
      px(x, y, s, 15, 18, 1, 1, col);
      break;
    case FACE_SHOCK:
      px(x, y, s, 9,  16, 4, 4, col);
      break;
    case FACE_SLEEPY:
      px(x, y, s, 8,  18, 6, 1, col);
      break;
    case FACE_DEAD:
      px(x, y, s, 7,  17, 8, 1, col);
      px(x, y, s, 9,  18, 1, 1, col);
      px(x, y, s, 12, 18, 1, 1, col);
      break;
    case FACE_FLAT:
    default:
      px(x, y, s, 7, 18, 9, 1, col);
      break;
  }
}

// ───────────────────────── rendering ─────────────────────────

static void drawBatteryPip(uint16_t fg) {
  // small unobtrusive indicator, top-right
  const int16_t bw = 22, bh = 10, bx = W - bw - 5, by = 4;
  canvas.drawRect(bx, by, bw, bh, fg);
  canvas.fillRect(bx + bw, by + 3, 2, 4, fg);
  int16_t fillw = (int16_t)((bw - 4) * (st.battPct / 100.0f));
  if (fillw < 0) fillw = 0;
  if (fillw > bw - 4) fillw = bw - 4;
  canvas.fillRect(bx + 2, by + 2, fillw, bh - 4, fg);
}

static void render() {
  const Mood& m = activeMood(st.theme, st.moodState);
  uint16_t bg = m.bg, fg = m.fg;

  // Low battery overrides the palette so it's unmistakable.
  if (st.lowBatt) { bg = C_INK; fg = C_CORAL; }

  canvas.fillScreen(bg);

  bool blinking = (millis() < st.blinkUntil);

  // Face art only occupies x:3..19, y:5..21 of its nominal 24x24 box, so
  // centring on the raw box leaves it visually high and left. Offset by the
  // real ink bounds instead.
  const int16_t faceScale = FACE_SCALE;
  const int16_t faceX = 14 - INK_X0 * faceScale;      // ink starts ~14px in
  const int16_t faceY = (H - INK_H) / 2 - INK_Y0 * faceScale;

  // Label, auto-shrunk to fit the column — the badge's fit_scale() idea. Size 3
  // gives chunky 18x24 px glyphs but only fits nine characters, so a longer word
  // steps down a size rather than scrolling past. Only a label too long even at
  // size 1 falls back to scrolling.
  //
  // A mood with a fixed textSize skips the shrink and scrolls instead, and a
  // '\n' in the label stacks lines. Stacked lines scroll together as one block.
  const int16_t textLeft = TEXT_LEFT;
  const int16_t textW = TEXT_W;

  static const uint8_t MAX_LINES = 3;
  const char* lineStart[MAX_LINES];
  int16_t lineLen[MAX_LINES];
  int16_t lines = 0, longest = 0;
  for (const char* p = m.text; lines < MAX_LINES; ) {
    const char* nl = strchr(p, '\n');
    lineStart[lines] = p;
    lineLen[lines] = nl ? (int16_t)(nl - p) : (int16_t)strlen(p);
    if (lineLen[lines] > longest) longest = lineLen[lines];
    lines++;
    if (!nl) break;
    p = nl + 1;
  }

  int16_t tsize = m.textSize ? m.textSize : TEXT_SIZE;
  if (!m.textSize)
    while (tsize > 1 && longest * 6 * tsize > textW) tsize--;
  int16_t textPx = longest * 6 * tsize;

  const bool scrolls = (textPx > textW);
  const int16_t lineH = 8 * tsize, lineGap = 2 * tsize;
  const int16_t blockH = lines * lineH + (lines - 1) * lineGap;

  canvas.setTextWrap(false);
  canvas.setTextSize(tsize);
  canvas.setTextColor(fg);
  for (int16_t i = 0; i < lines; i++) {
    const int16_t px = lineLen[i] * 6 * tsize;
    const int16_t tx = scrolls ? (textLeft + st.scrollX)
                               : (textLeft + (textW - px) / 2);
    canvas.setCursor(tx, (H - blockH) / 2 + i * (lineH + lineGap));
    canvas.write((const uint8_t*)lineStart[i], lineLen[i]);
  }

  // Mask whatever bled left of the text column, then draw the face on top, so
  // the label looks like it slides out from behind the face. The face lives
  // entirely inside the masked strip, so it only needs drawing once — here.
  canvas.fillRect(0, 0, textLeft, H, bg);
  drawFace(faceX, faceY, faceScale, m.face, fg, blinking);

  if (st.lowBatt) {
    canvas.setTextSize(1);
    canvas.setTextColor(C_CORAL);
    canvas.setCursor(textLeft, H - 14);
    canvas.print("low battery - charge me");
  }

  drawBatteryPip(fg);

  // auto-cycle indicator
  if (st.autoCycle) {
    canvas.fillCircle(6, 6, 3, fg);
  }

  // transient badge overlay (mode changes, brightness)
  if (st.showBadge && st.badgeText[0]) {
    int16_t bwid = (int16_t)strlen(st.badgeText) * 6 + 10;
    // shift up when the low-battery line is showing, or they collide
    int16_t bx = (W - bwid) / 2, by = st.lowBatt ? (H - 44) : (H - 24);
    canvas.fillRect(bx, by, bwid, 16, fg);
    canvas.setTextSize(1);
    canvas.setTextColor(bg);
    canvas.setCursor(bx + 5, by + 5);
    canvas.print(st.badgeText);
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);

  // Advance scroll only for labels that actually overflow. The text is fully
  // hidden once its right edge passes the mask (scrollX < -textPx), then it
  // re-enters from the right of the column.
  if (scrolls) {
    st.scrollX -= 2;
    if (st.scrollX < -textPx) st.scrollX = textW;
  }
}

// ───────────────────────── battery ─────────────────────────

static void readBattery() {
  if (!st.gaugeOK) return;
  float v = fuelGauge.cellVoltage();
  // Adafruit_BusIO returns 0 (not NaN) on an I2C read failure, so an isnan()
  // check alone lets 0.0 V through — which latches lowBatt permanently, since
  // the 3.65 V release threshold is unreachable from zero. Reject both.
  if (isnan(v) || v < 1.0f) return;
  st.battVolts = v;
  st.battPct = fuelGauge.cellPercent();
  if (st.battPct < 0) st.battPct = 0;
  if (st.battPct > 100) st.battPct = 100;

  if (st.battVolts < 3.45f && !st.lowBatt) {
    st.lowBatt = true;
  } else if (st.battVolts > 3.65f && st.lowBatt) {
    st.lowBatt = false;              // hysteresis
  }
}

// ───────────────────────── buttons ─────────────────────────
// (Btn is declared at the top of the file — see the note there.)

static Btn bMode{BTN_MODE}, bPrev{BTN_PREV}, bNext{BTN_NEXT};
static bool btnHeldAtBoot = false;

static bool update(Btn& b, uint32_t now, bool* released, bool* held1500) {
  bool raw = btnDown(b.pin);
  if (raw != b.lastRaw) { b.edgeMs = now; b.lastRaw = raw; }
  bool changed = false;
  if (now - b.edgeMs > 25 && raw != b.stable) {
    b.stable = raw;
    changed = true;
    if (raw) { b.downMs = now; b.longFired = false; }
    else if (released && !b.longFired && now - b.downMs < 700) *released = true;
  }
  if (held1500 && b.stable && !b.longFired && now - b.downMs > 1500) {
    b.longFired = true;
    *held1500 = true;
  }
  return changed;
}

static void handleButtons() {
  uint32_t now = millis();

  // Ignore a button still held from the boot-time gesture until it's released.
  if (btnHeldAtBoot) {
    if (!btnDown(BTN_MODE) && !btnDown(BTN_PREV) && !btnDown(BTN_NEXT))
      btnHeldAtBoot = false;
    return;
  }

  bool modeTap = false, modeHold = false, prevTap = false, nextTap = false;
  update(bMode, now, &modeTap, &modeHold);
  update(bPrev, now, &prevTap, nullptr);
  update(bNext, now, &nextTap, nullptr);

  // D1+D2 together cycles brightness. Releasing a two-button combo produces a
  // release event on each pin, which would otherwise land as prev+next taps and
  // jump the mood around — so swallow taps until BOTH are back up.
  static bool comboLatched = false;
  if (bPrev.stable && bNext.stable) {
    if (!comboLatched) {
      comboLatched = true;
      st.brightness = (st.brightness + 1) % BRIGHT_COUNT;
      applyBacklight();
      char buf[24];
      snprintf(buf, sizeof(buf), "brightness %u/%u", st.brightness + 1, BRIGHT_COUNT);
      badge(buf);
      st.dirtyAt = now;
    }
    return;
  }
  if (comboLatched) {
    if (!bPrev.stable && !bNext.stable) comboLatched = false;
    return;                    // discard the trailing release taps
  }

  if (modeHold) { enterDeepSleep(); return; }

  if (nextTap) toggleMood();     // D2: flip mood within the theme
  if (prevTap) nextTheme();      // D1: next theme

  if (modeTap) {
    st.autoCycle = !st.autoCycle;
    st.lastMoodMs = now;
    badge(st.autoCycle ? "auto-cycle on" : "auto-cycle off");
    st.dirtyAt = now;
  }
}

// ───────────────────────── sleep ─────────────────────────

void enterDeepSleep() {
  // Show a goodbye frame so it doesn't just cut out mid-animation.
  canvas.fillScreen(C_INK);
  canvas.setTextSize(2);
  canvas.setTextColor(C_CANDLE);
  canvas.setCursor(48, H / 2 - 8);
  canvas.print("sleeping...");
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);
  delay(700);

  saveSettings();

  pixel.clear();
  pixel.show();

  // Kill every rail we control. All are enabled by default, so each must be
  // cleared explicitly or the "sleeping" board keeps drawing milliamps.
  //
  // The backlight must be turned off through analogWrite, NOT digitalWrite:
  // applyBacklight() handed GPIO45 to the LEDC peripheral, and core 3.x's
  // digitalWrite refuses to touch a pin it no longer owns (it just logs an
  // error). The LEDC channel would keep driving its last duty and the backlight
  // — by far the biggest load — would stay lit through "deep sleep".
  analogWrite(TFT_BACKLITE, 0);
  digitalWrite(TFT_I2C_POWER, LOW);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, LOW);

  // Wait for release so a still-held button can't immediately re-trigger.
  while (btnDown(BTN_MODE) || btnDown(BTN_PREV) || btnDown(BTN_NEXT)) delay(10);
  delay(80);

  // Wake on D2 (active HIGH). INPUT_PULLDOWN does NOT survive deep sleep, so the
  // pulldown has to be re-established in the RTC domain or GPIO2 floats high and
  // the board wakes the instant it sleeps.
  rtc_gpio_pulldown_en(GPIO_NUM_2);
  rtc_gpio_pullup_dis(GPIO_NUM_2);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 1);   // 1 = wake on HIGH

  // Latch the power-enable pins LOW. Pads go high-impedance at sleep entry
  // unless held, which would let the LDO enable inputs float and partially
  // re-power the display/I2C rail — quietly blowing the standby current budget.
  // Holds release automatically on wake (a deep-sleep wake is a reset, and
  // setup() reconfigures both pins).
  gpio_hold_en((gpio_num_t)TFT_I2C_POWER);
  gpio_hold_en((gpio_num_t)NEOPIXEL_POWER);
  gpio_deep_sleep_hold_en();

  esp_deep_sleep_start();
}

// ───────────────────────── setup / loop ─────────────────────────

void setup() {
  Serial.begin(115200);

  // ext0 leaves the pad in RTC-IO mode; release it before normal GPIO use.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    rtc_gpio_deinit(GPIO_NUM_2);
  }

  // Power the TFT + I2C rail BEFORE touching the display or fuel gauge.
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, LOW);     // stay dark until the first frame is ready
  delay(10);

  // Polarity differs per button — see the header comment.
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLDOWN);
  pinMode(BTN_NEXT, INPUT_PULLDOWN);

  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  pixel.begin();
  pixel.setBrightness(40);
  pixel.clear();
  pixel.show();

  tft.init(135, 240);        // native portrait dimensions
  tft.setRotation(3);        // -> 240x135 landscape, buttons along the bottom
  tft.fillScreen(ST77XX_BLACK);

  loadSettings();

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    st.theme     = (rtcTheme < THEME_COUNT) ? rtcTheme : 0;
    st.moodState = (rtcState <= 1) ? rtcState : 0;
  }

  st.gaugeOK = fuelGauge.begin();
  if (st.gaugeOK) {
    readBattery();
  } else {
    Serial.println("[batt] MAX17048 not found - is a cell plugged in?");
    // Older boards (pre Feb 2023) shipped an LC709203F at 0x0B instead.
  }

  applyMood(false, /*persist=*/false);   // nothing changed; don't write flash
  applyBacklight();

  btnHeldAtBoot = btnDown(BTN_MODE) || btnDown(BTN_PREV) || btnDown(BTN_NEXT);

  Serial.printf("[boot] mood=%s auto=%d batt=%.2fV %.0f%%\n",
                activeMood(st.theme, st.moodState).text,
                st.autoCycle, st.battVolts, st.battPct);
}

void loop() {
  uint32_t now = millis();

  handleButtons();

  // ~33 fps is plenty for scrolling text and keeps the SPI bus relaxed.
  if (now - st.lastFrameMs >= 30) {
    st.lastFrameMs = now;

    // schedule/clear blinks
    const Mood& m = activeMood(st.theme, st.moodState);
    if (m.blinkMs && now >= st.nextBlinkMs) {
      st.blinkUntil = now + 130;
      st.nextBlinkMs = now + m.blinkMs;
    }

    if (st.showBadge && now > st.badgeUntil) st.showBadge = false;

    // The mood-change pixel flash is on its own timer. Without this it latched
    // on at the last mood's colour and sat glowing on a head-worn clip.
    if (st.pixelUntil && now > st.pixelUntil) {
      st.pixelUntil = 0;
      pixel.clear();
      pixel.show();
    }

    render();
  }

  if (st.autoCycle && now - st.lastMoodMs > st.autoPeriodMs) {
    toggleMood(/*persist=*/false);
  }

  static uint32_t lastBatt = 0;
  if (now - lastBatt > 10000) { lastBatt = now; readBattery(); }

  // Coalesce flash writes: button mashing shouldn't hammer NVS.
  if (st.dirtyAt && now - st.dirtyAt > 2500) {
    saveSettings();
    st.dirtyAt = 0;
  }

  delay(1);   // yield
}
