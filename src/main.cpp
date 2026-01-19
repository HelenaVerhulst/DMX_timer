
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
#define ENC_A   5    // (kan later naar 2 als je DMX op D3 wil gebruiken)
#define ENC_B   4
#define ENC_SW  2    // drukknop (actief LOW, met INPUT_PULLUP)

// ----------------- Kleuren (RGB565) -----------------
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREY    0x7BEF
#define RED     0xF800
#define BLUE    0x001F

// ----------------- UI-state -----------------
enum UiMode  { MODE_SELECT, MODE_EDIT };
UiMode mode = MODE_SELECT;

int8_t selectedIndex = 0;   // 0 = Channel, 1 = Timer, 2=duration
bool needRedraw = true;

// Encoder intern
int lastA = HIGH;           // edge-detectie op A (met pullup is idle HIGH)
unsigned long lastButtonMs = 0;
const unsigned long debounceMs = 180;

// ----------------- Instelbare waarden -----------------
uint8_t channel = 1;    // 1..255 (wrap)
uint8_t minutes = 10;   // 0..59
uint8_t seconds = 0;    // 0..59
uint8_t seconds_dur=0;

// Bij TIMER-bewerken: 0 = minutes, 1 = seconds
uint8_t timerEditField = 0;


// ----------------- LOGO (1-bit bitmap, PROGMEM) -----------------
// 'TRBL-Logo-Zwart-transparant-Zoomed16x9', 50x30px; tool exporteert 8 bytes/rij -> 240 bytes
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

void formatTime(char* out, size_t outLen, uint8_t mm, uint8_t ss) {
  snprintf(out, outLen, "%02u:%02u", mm, ss);
}

// ----------------- Home scherm (select + inline edit) -----------------

void drawHome() {
  display.fillScreen(WHITE);

  const int16_t MARGIN_X = 5;
  const int16_t TITLE_Y  = 8;
  const int16_t LINE_H   = 18;              // kleiner: minder verticale ruimte
  const int16_t HILIGHT_H = 14;             // highlight iets compacter
  const int16_t ITEM1_Y  = TITLE_Y + 18 + 6; // "Menu" + kleinere marge
  const int16_t ITEM2_Y  = ITEM1_Y + LINE_H;
  const int16_t ITEM3_Y  = ITEM2_Y + LINE_H;

  drawTextLeft("Menu", MARGIN_X, TITLE_Y, 2, BLACK);

  // --- Grijze highlightbalk voor geselecteerde rij (nu 3 cases) ---
  if (selectedIndex == 0) {
    display.fillRect(MARGIN_X, ITEM1_Y - 2, 118, HILIGHT_H, GREY);
  } else if (selectedIndex == 1) {
    display.fillRect(MARGIN_X, ITEM2_Y - 2, 118, HILIGHT_H, GREY);
  } else { // selectedIndex == 2
    display.fillRect(MARGIN_X, ITEM3_Y - 2, 118, HILIGHT_H, GREY);
  }

  // Labels
  drawTextLeft("Channel:",  MARGIN_X + 2, ITEM1_Y, 1, BLACK);
  drawTextLeft("Timer:",    MARGIN_X + 2, ITEM2_Y, 1, BLACK);
  drawTextLeft("Duration:", MARGIN_X + 2, ITEM3_Y, 1, BLACK);

  // Waarde-posities
  const int16_t VAL_X = MARGIN_X + 68;  // iets dichter

  // Channel-waarde
  char chBuf[8];
  snprintf(chBuf, sizeof(chBuf), "%u", channel);
  drawTextLeft(chBuf, VAL_X, ITEM1_Y, 1, BLACK);

  // Timer-waarde (MM:SS)
  char timeBuf[6];
  formatTime(timeBuf, sizeof(timeBuf), minutes, seconds);
  drawTextLeft(timeBuf, VAL_X, ITEM2_Y, 1, BLACK);

  // Duration-waarde (alleen seconden)
  char durBuf[8];
  snprintf(durBuf, sizeof(durBuf), "%u", seconds_dur);
  drawTextLeft(durBuf, VAL_X, ITEM3_Y, 1, BLACK);

  // --- Blauwe kader rond de waarde die je aan het bewerken bent ---
  if (mode == MODE_EDIT) {
    display.setTextSize(1);

    if (selectedIndex == 0) {
      // Channel -> kader om getal
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(chBuf, 0, 0, &x1, &y1, &w, &h);
      display.drawRect(VAL_X - 2, ITEM1_Y - 2, w + 4, h + 4, BLUE);
    }
    else if (selectedIndex == 1) {
      // Timer -> kader om MM of SS
      int16_t xMM1, yMM1; uint16_t wMM, hMM;
      display.getTextBounds("00", 0, 0, &xMM1, &yMM1, &wMM, &hMM);
      int16_t xMMcol1, yMMcol1; uint16_t wMMcol, hMMcol;
      display.getTextBounds("00:", 0, 0, &xMMcol1, &yMMcol1, &wMMcol, &hMMcol);

      int16_t mmX  = VAL_X;
      int16_t ssX  = VAL_X + wMMcol;
      int16_t boxY = ITEM2_Y - 2;
      uint16_t boxH = hMM + 4;
      uint16_t boxW = wMM + 4;

      if (timerEditField == 0) {
        display.drawRect(mmX - 2, boxY, boxW, boxH, BLUE);
      } else {
        display.drawRect(ssX - 2, boxY, boxW, boxH, BLUE);
      }
    }
    else if (selectedIndex == 2) {
      // Duration -> kader om secondenwaarde
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(durBuf, 0, 0, &x1, &y1, &w, &h);
      display.drawRect(VAL_X - 2, ITEM3_Y - 2, w + 4, h + 4, BLUE);
    }
  }

  // Logo onderaan gecentreerd
  int16_t logoX = (128 - LOGO_W) / 2;
  int16_t logoY = 128 - LOGO_H - 6;   // iets dichter tegen onderrand
  drawMonoBitmap_P_stride(
    logoX, logoY, LOGO_W, LOGO_H,
    epd_bitmap_TRBL_Logo_Zwart_transparant_Zoomed16x9,
    LOGO_BPR,
    BLACK,
    LOGO_INVERT_BITS
  );
}


// ---------- Encoder & Button ----------

// Encoder step: -1 (links), +1 (rechts), 0 (geen)
int8_t readEncoderStep() {
  int a = digitalRead(ENC_A);
  int8_t step = 0;

  // detecteer opgaande flank van A
  if (lastA == LOW && a == HIGH) {
    int b = digitalRead(ENC_B);
    // Met INPUT_PULLUP: LOW = naar GND getrokken.
    // B laag -> links, B hoog -> rechts.
    step = (b == LOW) ? -1 : +1;
  }
  lastA = a;
  return step;
}

// Button click (actief LOW) + debounce (korte druk)
bool buttonClicked() {
  if (digitalRead(ENC_SW) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonMs > debounceMs) {
      lastButtonMs = now;
      // wacht tot los (kort)
      while (digitalRead(ENC_SW) == LOW) { delay(5); }
      return true;
    }
  }
  return false;
}

// ---------- Render ----------

void render() {
  if (!needRedraw) return;
  drawHome();
  needRedraw = false;
}

void setup() {
  Serial.begin(9600);
  while (!Serial) { /* voor sommige boards */ }

  // Encoder – PULLUP en common naar GND
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastA = digitalRead(ENC_A);

  // OLED init
  pinMode(OLED_CS, OUTPUT);
  digitalWrite(OLED_CS, HIGH);
  display.begin();
  display.setRotation(0);
  display.setTextWrap(false);

  // Start-state
  mode = MODE_SELECT;
  selectedIndex = 0;
  timerEditField = 0;

  needRedraw = true;
  render();
}


void loop() {
  // 1) Encoder draaien
  int8_t step = readEncoderStep();
  if (step != 0) {

    // -------- SELECT MODE --------
    if (mode == MODE_SELECT) {
      int8_t old = selectedIndex;
      selectedIndex += (step > 0) ? 1 : -1;

      if (selectedIndex < 0) selectedIndex = 0;
      if (selectedIndex > 2) selectedIndex = 2;

      if (selectedIndex != old) needRedraw = true;
    }

    // -------- EDIT MODE --------
    else { // MODE_EDIT
      if (selectedIndex == 0) {
        // Channel 1..255 met wrap
        int16_t ch = channel + (step > 0 ? 1 : -1);
        if (ch < 1)   ch = 255;
        if (ch > 255) ch = 1;
        channel = (uint8_t)ch;
      }
      else if (selectedIndex == 1) {
        // Timer (MM : SS)
        if (timerEditField == 0) {
          int16_t mm = minutes + (step > 0 ? 1 : -1);
          if (mm < 0)  mm = 59;
          if (mm > 59) mm = 0;
          minutes = (uint8_t)mm;
        } else {
          int16_t ss = seconds + (step > 0 ? 1 : -1);
          if (ss > 59) {
            ss = 0;
            minutes = (minutes + 1) % 60;
          } else if (ss < 0) {
            ss = 59;
            minutes = (minutes == 0) ? 59 : minutes - 1;
          }
          seconds = (uint8_t)ss;
        }
      }
      else if (selectedIndex == 2) {
        // Duration (seconden, wrap 0..59)
        int16_t ss = seconds_dur + (step > 0 ? 1 : -1);
        if (ss > 59) ss = 0;
        else if (ss < 0) ss = 59;
        seconds_dur = (uint8_t)ss;
      }

      needRedraw = true;
    }
  }

  // 2) Drukknop
  if (buttonClicked()) {
    if (mode == MODE_SELECT) {
      mode = MODE_EDIT;
      if (selectedIndex == 1) {
        timerEditField = 0; // start op minutes
      }
    } 
    else { // MODE_EDIT
      if (selectedIndex == 0 || selectedIndex == 2) {
        mode = MODE_SELECT;
      }
      else if (selectedIndex == 1) {
        if (timerEditField == 0) {
          timerEditField = 1;
        } else {
          timerEditField = 0;
          mode = MODE_SELECT;
        }
      }
    }

    needRedraw = true;
  }

  // 3) Render indien nodig
  render();
}

