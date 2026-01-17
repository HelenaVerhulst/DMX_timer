
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <avr/pgmspace.h>

// ----------------- OLED SPI pins (UNO/Nano) -----------------
#define OLED_CS   10
#define OLED_DC    7
#define OLED_RST   8
Adafruit_SSD1351 display(128, 128, &SPI, OLED_CS, OLED_DC, OLED_RST);

// ----------------- Rotary encoder pins -----------------
#define ENC_A   5    // (verhuis later naar 2 als je DMX op D3 wil gebruiken)
#define ENC_B   4
#define ENC_SW  2

// ----------------- Kleuren (RGB565) -----------------
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREY    0x7BEF
#define RED     0xF800
#define BLUE    0x001F



// ----------------- UI-state -----------------
enum UiState { HOME, PAGE_RED, PAGE_BLUE };
UiState ui = HOME;
int8_t selectedIndex = 0;   // 0 = regel 1, 1 = regel 2
bool needRedraw = true;

// Encoder intern
int lastA = HIGH;           // voor edge-detectie op A (met pullup is idle HIGH)
unsigned long lastButtonMs = 0;
const unsigned long debounceMs = 180;





void drawClockCentered(
  int minutes,
  int seconds,
  uint16_t bgColor,
  uint16_t textColor
) {
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", minutes, seconds);

  // Achtergrond
  display.fillScreen(bgColor);

  // Tekst
  display.setTextSize(3);
  display.setTextColor(textColor);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);

  display.setCursor((128 - w) / 2, (128 - h) / 2);
  display.print(timeBuf);
}

// ----------------- JOUW LOGO (MONO 1-bit, PROGMEM) -----------------
//  Let op: dit is 1-bit bitmap data (geen kleur), ideaal voor drawBitmap-achtig tekenen.
//  We gebruiken een eigen tekenfunctie met expliciete stride, zodat de export (8 bytes/rij) correct werkt.
//
//  'TRBL-Logo-Zwart-transparant-Zoomed16x9', 50x30px  (de tool heeft 8 bytes/rij -> 240 bytes totaal)
const unsigned char epd_bitmap_TRBL_Logo_Zwart_transparant_Zoomed16x9 [] PROGMEM = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x7f, 
  0xc0, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0xff, 
  0xf1, 0x8c, 0x23, 0x8c, 0xff, 0xc0, 0xff, 0xf1, 0x9e, 0x23, 0x88, 0xff, 0xc0, 0xff, 0xf1, 0x9e, 
  0x23, 0x98, 0xff, 0xc0, 0xff, 0xf1, 0x1e, 0x20, 0x18, 0xff, 0xc0, 0xff, 0xf3, 0x18, 0x60, 0x18, 
  0xff, 0xc0, 0xff, 0xe3, 0x00, 0xc0, 0x19, 0xff, 0xc0, 0xff, 0xe3, 0x01, 0xc6, 0x11, 0xff, 0xc0, 
  0xff, 0xe3, 0x03, 0x8f, 0x31, 0xff, 0xc0, 0xff, 0xe2, 0x23, 0x8f, 0x31, 0xff, 0xc0, 0xff, 0xe6, 
  0x21, 0x8e, 0x31, 0xff, 0xc0, 0xff, 0xc6, 0x31, 0x80, 0x30, 0x01, 0xc0, 0xff, 0xc6, 0x30, 0xc0, 
  0x20, 0x03, 0xc0, 0xff, 0xc6, 0x78, 0xe0, 0x60, 0x03, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xc0
};



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

void drawTextLeft(const char* txt, int16_t x, int16_t y, uint8_t size, uint16_t color) {
  display.setTextSize(size);
  display.setTextColor(color);
  display.setCursor(x, y);
  display.print(txt);
}

void drawHome() {
  display.fillScreen(WHITE);

  const int16_t MARGIN_X = 5;
  const int16_t TITLE_Y  = 10;
  const int16_t LINE_H   = 22;
  const int16_t ITEM1_Y  = TITLE_Y + 22 + 8;
  const int16_t ITEM2_Y  = ITEM1_Y + LINE_H;

  drawTextLeft("Menu", MARGIN_X, TITLE_Y, 2, BLACK);

  // Highlight vlak
  if (selectedIndex == 0) {
    display.fillRect(MARGIN_X, ITEM1_Y - 2, 118, 18, GREY);
  } else {
    display.fillRect(MARGIN_X, ITEM2_Y - 2, 118, 18, GREY);
  }

  drawTextLeft("1. Channel instellen", MARGIN_X + 2, ITEM1_Y, 1, BLACK);
  drawTextLeft("2. Timer instellen",   MARGIN_X + 2, ITEM2_Y, 1, BLACK);

  // Logo onderaan gecentreerd
  int16_t logoX = (128 - LOGO_W) / 2;
  int16_t logoY = 128 - LOGO_H - 8;
  drawMonoBitmap_P_stride(
    logoX, logoY, LOGO_W, LOGO_H,
    epd_bitmap_TRBL_Logo_Zwart_transparant_Zoomed16x9,
    LOGO_BPR,
    BLACK,
    LOGO_INVERT_BITS
  );
}

void drawFullColor(uint16_t color, const char* label) {
  display.fillScreen(color);

  // label centreren
  display.setTextSize(2);
  display.setTextColor(WHITE);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, (128 - h) / 2);
  display.print(label);

  // hint
  display.setTextSize(1);
  const char* back = "Druk om terug te keren";
  display.getTextBounds(back, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, (128 - h) / 2 + 20);
  display.print(back);
}

// ---------- Encoder & Button ----------

// Encoder step: -1 (links), +1 (rechts), 0 (geen)
int8_t readEncoderStep() {
  int a = digitalRead(ENC_A);
  int8_t step = 0;

  // detecteer opgaande flank van A
  if (lastA == LOW && a == HIGH) {
    int b = digitalRead(ENC_B);
    // Met INPUT_PULLUP: LOW betekent naar GND getrokken.
    // B laag -> tegen de klok in (links), B hoog -> met de klok mee (rechts).
    if (b == LOW) step = -1; // LINKS
    else          step = +1; // RECHTS

    Serial.print(F("[ENC] A↑  B="));
    Serial.print(b);
    Serial.print(F("  step="));
    Serial.println(step);
  }
  lastA = a;
  return step;
}

// Button click (actief LOW) + debounce
bool buttonClicked() {
  if (digitalRead(ENC_SW) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonMs > debounceMs) {
      lastButtonMs = now;
      // wacht tot los (kort)
      while (digitalRead(ENC_SW) == LOW) { delay(5); }
      Serial.println(F("[BTN] Klik!"));
      return true;
    }
  }
  return false;
}

// ---------- Render ----------

void render() {
  if (!needRedraw) return;

  switch (ui) {
    case HOME:
      drawHome();
      Serial.print(F("[UI] HOME  selectedIndex="));
      Serial.println(selectedIndex);
      break;
    case PAGE_RED:
      drawFullColor(RED, "Channel");
      Serial.println(F("[UI] PAGE_RED"));
      break;
    case PAGE_BLUE:
      drawFullColor(BLUE, "Timer");
      Serial.println(F("[UI] PAGE_BLUE"));
      break;
  }

  needRedraw = false;
}

void setup() {
  // Serial debug
  Serial.begin(9600);
  while (!Serial) { /* voor sommige boards */ }
  Serial.println(F("Start menu + encoder debug"));

  // Encoder – PULLUP en common naar GND
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastA = digitalRead(ENC_A);
  Serial.print(F("[Pins] ENC_A=")); Serial.print(ENC_A);
  Serial.print(F(" ENC_B=")); Serial.print(ENC_B);
  Serial.print(F(" ENC_SW=")); Serial.println(ENC_SW);

  // OLED init
  pinMode(OLED_CS, OUTPUT);
  digitalWrite(OLED_CS, HIGH);
  display.begin();
  display.setRotation(0);

  needRedraw = true;
  render();
}

void loop() {
  // 1) Encoder draaien -> selectie (alleen in HOME)
  if (ui == HOME) {
    int8_t step = readEncoderStep();
    if (step != 0) {
      int8_t old = selectedIndex;
      selectedIndex += (step > 0) ? 1 : -1;
      if (selectedIndex < 0) selectedIndex = 0;
      if (selectedIndex > 1) selectedIndex = 1;

      if (selectedIndex != old) {
        Serial.print(F("[SEL] ")); Serial.println(selectedIndex);
        needRedraw = true;
      } else {
        Serial.println(F("[SEL] aan grens (0..1)"));
      }
    }
  }

  // 2) Druk -> HOME <-> pagina
  if (buttonClicked()) {
    if (ui == HOME) {
      ui = (selectedIndex == 0) ? PAGE_RED : PAGE_BLUE;
    } else {
      ui = HOME;
    }
    needRedraw = true;
  }

  // 3) Render indien nodig
  render();
}












