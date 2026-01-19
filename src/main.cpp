
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <avr/pgmspace.h>

// ===========================================================
// OLED + ENCODER CONFIG
// ===========================================================

#define OLED_CS   10
#define OLED_DC    7
#define OLED_RST   8
Adafruit_SSD1351 display(128, 128, &SPI, OLED_CS, OLED_DC, OLED_RST);

#define ENC_A   5
#define ENC_B   4
#define ENC_SW  2

// Colors (RGB565)
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREY    0x7BEF
#define BLUE    0x001F

// ===========================================================
// UI STATE
// ===========================================================

enum UiMode { MODE_SELECT, MODE_EDIT };
UiMode mode = MODE_SELECT;

// 0 = Channel, 1 = Timer, 2 = Duration
int8_t selectedIndex = 0;
int8_t lastSelectedIndex = -1;

uint8_t channel     = 1;
uint8_t minutes     = 10;
uint8_t seconds     = 0;
uint8_t seconds_dur = 0;

// Timer edit: 0 = MM, 1 = SS
uint8_t timerEditField = 0;

// Redraw flags
bool redrawStatic    = true;
bool redrawHighlight = true;
bool redrawValues    = true;
bool redrawEditBox   = true;

// ===========================================================
// ENCODER
// ===========================================================

int lastA = HIGH;
unsigned long lastButtonMs = 0;
const unsigned long debounceMs = 180;

// ===========================================================
// LAYOUT CONSTANTS
// ===========================================================

#define ROW_X      2
#define ROW_W    124   // 128 - 2*2
#define ROW_H     16


const int16_t MARGIN_X = 5;
const int16_t TITLE_Y  = 8;
const int16_t LINE_H   = 18;
const int16_t ITEM1_Y  = TITLE_Y + 24;
const int16_t ITEM2_Y  = ITEM1_Y + LINE_H;
const int16_t ITEM3_Y  = ITEM2_Y + LINE_H;
const int16_t VAL_X    = MARGIN_X + 68;


// Breedtes (afstemmen op font size 1)
#define VALUE_W   36
#define VALUE_H   10

// Timer subvelden
#define TIME_MM_W  12   // "00"
#define TIME_COL_W  6   // ":"
#define TIME_SS_W  12   // "00"


// ===========================================================
// LOGO (exact jouw data, ongewijzigd)
// ===========================================================



// ----------------- LOGO (1-bit bitmap, PROGMEM) -----------------
// 'TRBL-Logo-Zwart-transparant-Zoomed16x9', 50x30px; tool exporteert 8 bytes/rij -> 240 bytes




#include "logo.h"   // ✅ zet hier jouw bitmap in aparte .h (zoals je nu hebt)



constexpr int LOGO_W = 50;
constexpr int LOGO_H = 30;

// Bytes-per-row (stride) automatisch uit array-grootte
constexpr int LOGO_BYTES =
  sizeof(epd_bitmap_TRBL_Logo_Zwart_transparant_Zoomed16x9);
static_assert(LOGO_BYTES % LOGO_H == 0, "Bitmap-bytes niet deelbaar door LOGO_H");
constexpr int LOGO_BPR = LOGO_BYTES / LOGO_H;  // 210/30=7 of 240/30=8

// Invers tekenen (zwart <-> wit) voor 1-bit bitmap
const bool LOGO_INVERT_BITS = true;

// ----------------- Helpers -----------------

// 1-bit PROGMEM bitmap tekenen (alleen bits -> GEEN kader)
void drawMonoBitmap_P_stride(
  int16_t x, int16_t y,
  int16_t w, int16_t h,
  const uint8_t* bmpPROGMEM,
  uint16_t bytesPerRow,
  uint16_t fgColor,
  bool invert // true = bits omdraaien
) {
  for (int16_t row = 0; row < h; row++) {
    for (int16_t col = 0; col < w; col++) {
      uint16_t byteIndex = row * bytesPerRow + (col >> 3);
      uint8_t  bitMask   = 0x80 >> (col & 7);  // MSB-first
      uint8_t  b         = pgm_read_byte(bmpPROGMEM + byteIndex);

      bool bitSet = (b & bitMask) != 0;
      if (invert) bitSet = !bitSet;

      if (bitSet) {
        display.drawPixel(x + col, y + row, fgColor);
      }
    }
  }
}





// ===========================================================
// HELPERS
// ===========================================================

void drawText(const char* txt, int16_t x, int16_t y, uint8_t size, uint16_t color) {
  display.setTextSize(size);
  display.setTextColor(color);
  display.setCursor(x, y);
  display.print(txt);
}

// Geeft Y-positie van rij 0,1,2
int16_t itemY(int index) {
  return (index == 0) ? ITEM1_Y :
         (index == 1) ? ITEM2_Y : ITEM3_Y;
}



void redrawRow(int index) {
  if (index < 0 || index > 2) return;

  int16_t y = itemY(index);

  // ---- Achtergrond (highlight of wit) ----
  uint16_t bg = (selectedIndex == index) ? GREY : WHITE;
  display.fillRect(ROW_X, y - 2, ROW_W, ROW_H, bg);

  // ---- Label ----
  display.setTextSize(1);
  display.setTextColor(BLACK);

  if (index == 0) {
    display.setCursor(MARGIN_X + 2, y);
    display.print("Channel:");
  }
  else if (index == 1) {
    display.setCursor(MARGIN_X + 2, y);
    display.print("Timer:");
  }
  else if (index == 2) {
    display.setCursor(MARGIN_X + 2, y);
    display.print("Duration:");
  }

  // ---- Waarde ----
  char buf[8];
  if (index == 0) {
    snprintf(buf, sizeof(buf), "%u", channel);
  }
  else if (index == 1) {
    snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
  }
  else {
    snprintf(buf, sizeof(buf), "%u", seconds_dur);
  }

  display.setCursor(VAL_X, y);
  display.print(buf);

  // ---- Edit kader ----
  if (mode == MODE_EDIT && selectedIndex == index) {
    display.drawRect(VAL_X - 2, y - 2, 44, ROW_H, BLUE);
  }
}



// ===========================================================
// STATIC UI (1x tekenen)
// ===========================================================



void drawStaticUI() {
  display.fillScreen(WHITE);

  drawText("Menu", MARGIN_X, TITLE_Y, 2, BLACK);
  drawText("Channel:",  MARGIN_X + 2, ITEM1_Y, 1, BLACK);
  drawText("Timer:",    MARGIN_X + 2, ITEM2_Y, 1, BLACK);
  drawText("Duration:", MARGIN_X + 2, ITEM3_Y, 1, BLACK);

  // Logo
  int16_t logoX = (128 - LOGO_W) / 2;
  int16_t logoY = 128 - LOGO_H - 6;

  drawMonoBitmap_P_stride(
    logoX, logoY,
    LOGO_W, LOGO_H,
    epd_bitmap_TRBL_Logo_Zwart_transparant_Zoomed16x9,
    LOGO_BPR, BLACK, LOGO_INVERT_BITS
  );
}

// ===========================================================
// HIGHLIGHT
// ===========================================================

void clearHighlight(int index) {
  display.fillRect(MARGIN_X, itemY(index) - 2, 118, 14, WHITE);
  
}

void drawHighlight(int index) {
  display.fillRect(ROW_X, itemY(index) - 2, ROW_W, ROW_H, GREY);
}

// ===========================================================
// VALUES
// ===========================================================

void drawValues() {
  char buf[8];

  // Channel
  display.fillRect(VAL_X - 1, ITEM1_Y - 1, 40, 10, WHITE);
  snprintf(buf, sizeof(buf), "%u", channel);
  drawText(buf, VAL_X, ITEM1_Y, 1, BLACK);

  // Timer
  display.fillRect(VAL_X - 1, ITEM2_Y - 1, 40, 10, WHITE);
  snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
  drawText(buf, VAL_X, ITEM2_Y, 1, BLACK);

  // Duration (seconds only)
  display.fillRect(VAL_X - 1, ITEM3_Y - 1, 40, 10, WHITE);
  snprintf(buf, sizeof(buf), "%u", seconds_dur);
  drawText(buf, VAL_X, ITEM3_Y, 1, BLACK);
}

// ===========================================================
// EDIT BOX
// ===========================================================

void clearEditBox() {
  display.drawRect(VAL_X - 2, ITEM1_Y - 2, 44, 14, WHITE);
  display.drawRect(VAL_X - 2, ITEM2_Y - 2, 44, 14, WHITE);
  display.drawRect(VAL_X - 2, ITEM3_Y - 2, 44, 14, WHITE);
  
}

void drawEditBox() {
  if (mode != MODE_EDIT) return;

  int16_t y = itemY(selectedIndex);
  display.drawRect(VAL_X - 2, y - 2, 44, 14, BLUE);
}

// ===========================================================
// ENCODER + BUTTON
// ===========================================================

int8_t readEncoderStep() {
  int a = digitalRead(ENC_A);
  int8_t step = 0;
  if (lastA == LOW && a == HIGH) {
    step = (digitalRead(ENC_B) == LOW) ? -1 : 1;
  }
  lastA = a;
  return step;
}

bool buttonClicked() {
  if (digitalRead(ENC_SW) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonMs > debounceMs) {
      lastButtonMs = now;
      while (digitalRead(ENC_SW) == LOW) delay(5);
      return true;
    }
  }
  return false;
}

// ===========================================================
// RENDER
// ===========================================================

// void render() {
//   if (redrawStatic) {
//     drawStaticUI();
//     redrawStatic = false;
//     redrawHighlight = redrawValues = redrawEditBox = true;
//   }

//   if (redrawHighlight) {
//     if (lastSelectedIndex >= 0)
//       clearHighlight(lastSelectedIndex);
//     drawHighlight(selectedIndex);
//     lastSelectedIndex = selectedIndex;
//     redrawHighlight = false;
//   }

//   if (redrawValues) {
//     drawValues();
//     redrawValues = false;
//   }

//   if (redrawEditBox) {
//     clearEditBox();
//     drawEditBox();
//     redrawEditBox = false;
//   }
// }

void render(bool full = false) {
  if (full) {
    drawStaticUI();
    redrawRow(0);
    redrawRow(1);
    redrawRow(2);
  }
}


// ===========================================================
// SETUP
// ===========================================================

// void setup() {
//   pinMode(ENC_A, INPUT_PULLUP);
//   pinMode(ENC_B, INPUT_PULLUP);
//   pinMode(ENC_SW, INPUT_PULLUP);
  
//   redrawStatic    = true;
//   redrawHighlight = true;
//   redrawValues    = true;
//   redrawEditBox   = true;

//   lastSelectedIndex = -1;


//   display.begin();
//   display.setRotation(0);
//   display.setTextWrap(false);

//   redrawStatic = true;
//   render();
// }


void setup() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  display.begin();
  display.setRotation(0);
  display.setTextWrap(false);

  lastSelectedIndex = -1;

  render(true); // VOLLEDIGE eerste draw
}


// ===========================================================
// LOOP
// ===========================================================


void loop() {
  int8_t step = readEncoderStep();

  // ===== ROTARY =====
  if (step != 0) {
    if (mode == MODE_SELECT) {
      int8_t old = selectedIndex;
      selectedIndex += (step > 0) ? 1 : -1;
      selectedIndex = constrain(selectedIndex, 0, 2);

      if (old != selectedIndex) {
        redrawRow(old);
        redrawRow(selectedIndex);
      }
    }
    else { // MODE_EDIT
      if (selectedIndex == 0) {
        channel = (channel + (step > 0 ? 1 : -1) + 254) % 255 + 1;
      }
      else if (selectedIndex == 1) {
        if (timerEditField == 0)
          minutes = (minutes + (step > 0 ? 1 : -1) + 60) % 60;
        else
          seconds = (seconds + (step > 0 ? 1 : -1) + 60) % 60;
      }
      else {
        seconds_dur = (seconds_dur + (step > 0 ? 1 : -1) + 60) % 60;
      }
      redrawRow(selectedIndex);
    }
  }

  // ===== BUTTON =====
  if (buttonClicked()) {
    if (mode == MODE_SELECT) {
      mode = MODE_EDIT;
      timerEditField = 0;
    }
    else {
      if (selectedIndex == 1 && timerEditField == 0) {
        timerEditField = 1;
      } else {
        mode = MODE_SELECT;
      }
    }
    redrawRow(selectedIndex);
  }
}
