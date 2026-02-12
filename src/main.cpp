
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <avr/pgmspace.h>
#include <DMXSerial.h>

// Nu DMX doen:
// pinnen juist zetten als 10 en 11 zit er een overlap met het oled_scherm dus moet hier een "timetable"/afspraken voor komen
// DMX heeft min 1hz van info nodig liefst meer dus als oled niet nodig heeft kan men info sturen
// daarna moet er met de opgeslagen data de dmx aangestuurd worden dus de channel, duration en timer zijn nodig voor de dmx aan te spreken
// ook misschien de juiste error afhandeling naar het scherm als iets niet werkt??? (optioneel)

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

uint8_t channel     = 1;   // 1..521 (wrap)
uint8_t minutes     = 10;  // 0..59
uint8_t seconds     = 0;   // 0..59
uint8_t seconds_dur = 0;   // 0..59

// Timer edit: 0 = MM, 1 = SS
uint8_t timerEditField = 0;

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

// Breedtes (afgestemd op font size 1)
#define VALUE_W   36
#define VALUE_H   10

// Timer subvelden
#define TIME_MM_W  12   // "00"
#define TIME_COL_W  6   // ":"
#define TIME_SS_W  12   // "00"


// ===========================================================
// DMX DEFINITIES
// ===========================================================

#define DMX_TX 1     // Hardware UART TX voor DMX
#define DMX_DE 12    // MAX485 Driver Enable

enum DmxState { DMX_IDLE, DMX_WAIT, DMX_ACTIVE };
DmxState dmxState = DMX_IDLE;

unsigned long waitEndMs = 0;
unsigned long activeEndMs = 0;
unsigned long lastDMX = 0;
const uint16_t DMX_RATE = 30;



// ===========================================================
// LOGO (jouw data in aparte header)
// ===========================================================

#include "logo.h"   // moet 'epd_bitmap_TRBL_Logo_Zwart_transparant_Zoomed16x9' definiëren

constexpr int LOGO_W = 50;
constexpr int LOGO_H = 30;

// Bytes-per-row (stride) automatisch uit array-grootte
constexpr int LOGO_BYTES =
  sizeof(epd_bitmap_TRBL_Logo_Zwart_transparant_Zoomed16x9);
static_assert(LOGO_BYTES % LOGO_H == 0, "Bitmap-bytes niet deelbaar door LOGO_H");
constexpr int LOGO_BPR = LOGO_BYTES / LOGO_H;  // 210/30=7 of 240/30=8

// Invers tekenen (zwart <-> wit) voor 1-bit bitmap
const bool LOGO_INVERT_BITS = true;

// ===========================================================
// BITMAP HELPER
// ===========================================================

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

// ---- Kleine update helpers ----
inline void updateChannel(int8_t step) {
  int16_t ch = channel + (step > 0 ? 1 : -1);
  if (ch < 1)   ch = 521;
  if (ch > 521) ch = 1;
  channel = (uint8_t)ch;
}

inline void updateMinutes(int8_t step) {
  int16_t mm = minutes + (step > 0 ? 1 : -1);
  if (mm < 0)  mm = 59;
  if (mm > 59) mm = 0;
  minutes = (uint8_t)mm;
}



// Return true ALS minuten zijn aangepast
inline bool updateSeconds(int8_t step) {
  int16_t ss = seconds + (step > 0 ? 1 : -1);
  bool minutesChanged = false;

  if (ss > 59) {
    ss = 0;
    minutes = (minutes + 1) % 60;
    minutesChanged = true;
  }
  else if (ss < 0) {
    ss = 59;
    minutes = (minutes == 0) ? 59 : minutes - 1;
    minutesChanged = true;
  }

  seconds = (uint8_t)ss;
  return minutesChanged;
}



inline void updateDuration(int8_t step) {
  int16_t ss = seconds_dur + (step > 0 ? 1 : -1);
  if (ss < 0)  ss = 59;
  if (ss > 59) ss = 0;
  seconds_dur = (uint8_t)ss;
}

// ===========================================================
// ROW-BASED REDRAW (layout of selectie wisselt)
// ===========================================================

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

  // ---- Waarde ---- (volledige waarde schrijven)
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
    if (index == 1) {
      // Timer: subveld-kader apart
      // (de juiste blauwe kader wordt elders getekend)
      // -> Eerst beide wissen (maar de rij is net getekend op wit/grijs)
      // -> Dan de actieve tekenen:
      int16_t yy = ITEM2_Y;
      if (timerEditField == 0) {
        display.drawRect(VAL_X - 2, yy - 2, TIME_MM_W + 4, ROW_H, BLUE);
      } else {
        int16_t ssx = VAL_X + TIME_MM_W + TIME_COL_W;
        display.drawRect(ssx - 2, yy - 2, TIME_SS_W + 4, ROW_H, BLUE);
      }
    } else {
      // Channel of Duration: 1 kader
      display.drawRect(VAL_X - 2, y - 2, 44, ROW_H, BLUE);
    }
  }
}

// ===========================================================
// VALUE-ONLY REDRAWS (géén rij opnieuw)
// ===========================================================

void redrawChannelValue() {
  int16_t y = ITEM1_Y;
  display.fillRect(VAL_X - 1, y - 1, VALUE_W, VALUE_H, (selectedIndex == 0 ? GREY : WHITE));
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", channel);
  display.setCursor(VAL_X, y);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.print(buf);
  // kader blijft ongemoeid (wordt alleen bij mode/field wissel getekend)
}

int16_t timerMM_X() { return VAL_X; }
int16_t timerSS_X() { return VAL_X + TIME_MM_W + TIME_COL_W; }

void redrawTimerMinutes() {
  int16_t y = ITEM2_Y;
  // Wis enkel het MM blokje in rij-achtergrondkleur
  uint16_t bg = (selectedIndex == 1 ? GREY : WHITE);
  display.fillRect(timerMM_X() - 1, y - 1, TIME_MM_W, VALUE_H, bg);

  char buf[4];
  snprintf(buf, sizeof(buf), "%02u", minutes);
  display.setCursor(timerMM_X(), y);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.print(buf);
}

void redrawTimerSeconds() {
  int16_t y = ITEM2_Y;
  uint16_t bg = (selectedIndex == 1 ? GREY : WHITE);
  display.fillRect(timerSS_X() - 1, y - 1, TIME_SS_W, VALUE_H, bg);

  char buf[4];
  snprintf(buf, sizeof(buf), "%02u", seconds);
  display.setCursor(timerSS_X(), y);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.print(buf);
}

void redrawDurationValue() {
  int16_t y = ITEM3_Y;
  display.fillRect(VAL_X - 1, y - 1, VALUE_W, VALUE_H, (selectedIndex == 2 ? GREY : WHITE));
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", seconds_dur);
  display.setCursor(VAL_X, y);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.print(buf);
}

// ---- Timer kader wisselen zonder rij te herschrijven ----

void clearTimerEditBoxes() {
  int16_t y = ITEM2_Y;
  // Overschrijven met rij-achtergrondkleur (GREY als geselecteerd)
  uint16_t bg = (selectedIndex == 1 ? GREY : WHITE);
  display.drawRect(VAL_X - 2,               y - 2, TIME_MM_W + 4, ROW_H, bg);
  int16_t ssx = VAL_X + TIME_MM_W + TIME_COL_W;
  display.drawRect(ssx - 2,                 y - 2, TIME_SS_W + 4, ROW_H, bg);
}

void drawTimerEditBox() {
  if (mode != MODE_EDIT || selectedIndex != 1) return;
  int16_t y = ITEM2_Y;
  if (timerEditField == 0) {
    display.drawRect(VAL_X - 2, y - 2, TIME_MM_W + 4, ROW_H, BLUE);
  } else {
    int16_t ssx = VAL_X + TIME_MM_W + TIME_COL_W;
    display.drawRect(ssx - 2, y - 2, TIME_SS_W + 4, ROW_H, BLUE);
  }
}

// ===========================================================
// STATIC UI (1x tekenen)
// ===========================================================

void drawStaticUI() {
  display.fillScreen(WHITE);

  drawText("Menu", MARGIN_X, TITLE_Y, 2, BLACK);
  // labels worden per rij in redrawRow ook gezet, maar dit helpt bij eerste frame
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


void showStatus(const char* msg) {
  display.fillRect(0, 0, 128, 12, GREY);
  display.setCursor(2, 2);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.print(msg);
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


void showError(const char* msg) {
  display.fillRect(0, 0, 128, 12, BLACK);
  display.setCursor(2, 2);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.print(msg);
}


// ===========================================================
// RENDER
// ===========================================================

void render(bool full = false) {
  if (full) {
    drawStaticUI();
    // Eerste keer alle rijen tekenen incl. highlight/waarden
    redrawRow(0);
    redrawRow(1);
    redrawRow(2);
  }
}


// ===========================================================
// DMX ENGINE
// ===========================================================

// Stuurt elk frame (30 Hz) de actuele waarde voor het gekozen kanaal
void dmxWriteFrame() {
  unsigned long now = millis();
  if (now - lastDMX >= (1000UL / DMX_RATE)) {
    lastDMX = now;

    // Alleen tijdens ACTIVE kanaal op 255, anders 0
    if (dmxState == DMX_ACTIVE) {
      DMXSerial.write(channel, 255);
    } else {
      DMXSerial.write(channel, 0);
    }
  }
}

// State-machine: wachten -> actief -> idle
void dmxController() {
  unsigned long now = millis();

  switch (dmxState) {
    case DMX_IDLE:
      // Zorg dat kanaal uit is in idle
      DMXSerial.write(channel, 0);
      break;

    case DMX_WAIT:
      if (now >= waitEndMs) {
        dmxState = DMX_ACTIVE;
        showStatus("DMX: ACTIVE");
      }
      break;

    case DMX_ACTIVE:
      // Waarde wordt in dmxWriteFrame gezet
      if (now >= activeEndMs) {
        DMXSerial.write(channel, 0);
        dmxState = DMX_IDLE;
        showStatus("DMX: IDLE");
      }
      break;
  }
}

// Start een DMX cyclus: wacht (MM:SS), daarna 'duration' seconden actief
void startDmxSequence() {
  // Validatie kanaal (1..512)
  if (channel < 1 || channel > 512) {
    showError("Bad DMX channel");
    return;
  }

  unsigned long delayMs = (unsigned long)minutes * 60000UL + (unsigned long)seconds * 1000UL;
  unsigned long durMs   = (unsigned long)seconds_dur * 1000UL;

  unsigned long now = millis();
  waitEndMs   = now + delayMs;
  activeEndMs = waitEndMs + durMs;

  if (delayMs == 0) {
    dmxState = DMX_ACTIVE;
    showStatus("DMX: ACTIVE");
  } else {
    dmxState = DMX_WAIT;
    showStatus("DMX: WAIT");
  }
}

// Optioneel: handmatig stoppen
void stopDmxSequence() {
  dmxState = DMX_IDLE;
  DMXSerial.write(channel, 0);
  showStatus("DMX: STOP");
}



// ===========================================================
// SETUP
// ===========================================================

void setup() {
  Serial.begin(250000);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  display.begin();
  display.setRotation(0);
  display.setTextWrap(false);

  lastSelectedIndex = -1;

  render(true); // VOLLEDIGE eerste draw

  
 
  // DMX
  DMXSerial.init(DMXController);   // correct ENUM, niet dmxcontroller()
  pinMode(DMX_DE, OUTPUT);
  digitalWrite(DMX_DE, HIGH);      // MAX485 op zenden
  DMXSerial.write(1, 0);

  showStatus("Ready");
}


// ===========================================================
// LOOP
// ===========================================================

// void loop() {
//   int8_t step = readEncoderStep();

//   // ===== ROTARY =====
//   if (step != 0) {
//     if (mode == MODE_SELECT) {
//       // Navigeren tussen rijen
//       int8_t old = selectedIndex;
//       selectedIndex += (step > 0 ? 1 : -1);
//       if (selectedIndex < 0) selectedIndex = 0;
//       if (selectedIndex > 2) selectedIndex = 2;

//       if (old != selectedIndex) {
//         redrawRow(old);
//         redrawRow(selectedIndex);
//       }
//     }
//     else { // MODE_EDIT: alleen minimale updates
//       if (selectedIndex == 0) {
//         updateChannel(step);
//         redrawChannelValue();
//       }
//       else if (selectedIndex == 1) {
//         if (timerEditField == 0) {
//           updateMinutes(step);
//           redrawTimerMinutes();  // alleen MM
//         } else {
          
//           bool mmChanged = updateSeconds(step);

//           // Seconden zijn ALTIJD veranderd
//           redrawTimerSeconds();

//           // Minuten alleen hertekenen als ze effectief aangepast zijn
//           if (mmChanged) {
//             redrawTimerMinutes();
//           }

//         }
//       }
//       else if (selectedIndex == 2) {
//         updateDuration(step);
//         redrawDurationValue();
//       }
//     }
//   }

//   // ===== BUTTON =====
//   if (buttonClicked()) {
//     if (mode == MODE_SELECT) {
//       // Start bewerken
//       mode = MODE_EDIT;
//       timerEditField = 0;
//       if (selectedIndex == 1) {
//         // toon kader rond MM
//         drawTimerEditBox();
//       } else {
//         // voor Channel/Duration volstaat hertekenen van rij voor kader
//         redrawRow(selectedIndex);
//       }
//     }
//     else {
//       // In edit: gedrag per rij
//       if (selectedIndex == 1) {
//         // Timer: wissel MM <-> SS, of klaar
//         if (timerEditField == 0) {
//           // naar seconden: wissel alleen kader, geen tekst
//           clearTimerEditBoxes();
//           timerEditField = 1;
//           drawTimerEditBox();
//         } else {
//           // klaar met timer
//           mode = MODE_SELECT;
//           // volledige rij zodat kader verdwijnt en highlight correct blijft
//           redrawRow(selectedIndex);
//         }
//       } else {
//         // Channel/Duration: klaar
//         mode = MODE_SELECT;
//         redrawRow(selectedIndex);
//       }
//     }
//   }
// }


void loop() {

  // --- Encoder draaien ---
  int8_t step = readEncoderStep();

  if (step != 0) {
    if (mode == MODE_SELECT) {
      // Navigeren door items
      int8_t old = selectedIndex;
      selectedIndex += (step > 0 ? 1 : -1);

      if (selectedIndex < 0) selectedIndex = 0;
      if (selectedIndex > 2) selectedIndex = 2;

      if (old != selectedIndex) {
        redrawRow(old);
        redrawRow(selectedIndex);
      }
    }
    else { // MODE_EDIT
      if (selectedIndex == 0) {
        updateChannel(step);
        redrawChannelValue();
      }
      else if (selectedIndex == 1) {
        if (timerEditField == 0) {
          updateMinutes(step);
          redrawTimerMinutes();
        } else {
          bool mmChanged = updateSeconds(step);
          redrawTimerSeconds();
          if (mmChanged) redrawTimerMinutes();
        }
      }
      else if (selectedIndex == 2) {
        updateDuration(step);
        redrawDurationValue();
      }
    }
  }

  // --- Knop gedrukt ---
  if (buttonClicked()) {

    if (mode == MODE_SELECT) {
      // Start edit mode
      mode = MODE_EDIT;
      timerEditField = 0;

      if (selectedIndex == 1) {
        drawTimerEditBox();
      } else {
        redrawRow(selectedIndex);
      }
    }

    else { // MODE_EDIT → ENTER gedrukt
      if (selectedIndex == 1) {
        // Timer MM → SS
        if (timerEditField == 0) {
          clearTimerEditBoxes();
          timerEditField = 1;
          drawTimerEditBox();
        } else {
          // Klaar met timer
          mode = MODE_SELECT;
          redrawRow(selectedIndex);
        }
      }
      else {
        // Channel of Duration klaar
        mode = MODE_SELECT;
        redrawRow(selectedIndex);

        // ---------- HIER START JE DMX PROGRAMMA ----------
        if (selectedIndex == 2) {
          startDmxSequence();
        }
      }
    }
  }

  // --- DMX non-blocking loops ---
  dmxWriteFrame();
  dmxController();
}

