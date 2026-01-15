
#include <Encoder.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <DmxSimple.h>

// --- Pin-definities ---
#define ENC_CLK 2
#define ENC_DT 7
#define ENC_SW 4
#define DMX_PIN 3

Adafruit_SSD1306 display(128, 64, &Wire);
Encoder myEnc(ENC_CLK, ENC_DT);

// States
enum TimerState { STATE_MINUTES, STATE_SECONDS, STATE_RUN };
TimerState currentState = STATE_MINUTES;

// Waarden
int minutesValue = 0;
int secondsValue = 0;
bool dmxIsOn = false;
unsigned long phaseStart = 0;
bool repeatCycle = true;

unsigned long lastButtonTime = 0;
const unsigned long debounceMs = 200;

void setup() {
  pinMode(ENC_SW, INPUT_PULLUP);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);//encoder roteer instellen
  display.clearDisplay();//scherm instellen
  display.display();

  DmxSimple.usePin(DMX_PIN);//DMX instellen en max aantal channels zijn 1 (0 is standaard 0)
  DmxSimple.maxChannel(1);
  DmxSimple.write(1, 0);

  myEnc.write(0);//op 0 zetten
}

void loop() { // in en loop dus het scherm blijft geupdated
    if (currentState == STATE_MINUTES) {
    long steps = myEnc.read() / 4;
    minutesValue = constrain((int)steps, 0, 59);
    if (buttonPressed()) {
      myEnc.write(0); // reset encoder voor seconden
      currentState = STATE_SECONDS;
    }
    drawScreen("Minuten", minutesValue, secondsValue);
  }
  else if (currentState == STATE_SECONDS) {
    long steps = myEnc.read() / 4;
    secondsValue = constrain((int)steps, 0, 59);
    if (buttonPressed()) {
      currentState = STATE_RUN;
      startDmxPhase(true);
    }
    drawScreen("Seconden instellen", minutesValue, secondsValue);
  }
  else if (currentState == STATE_RUN) {
    unsigned long now = millis();
    int totalSeconds = minutesValue * 60 + secondsValue;
    if (now - phaseStart >= (unsigned long)totalSeconds * 1000UL) {
      if (dmxIsOn) {
        startDmxPhase(false);
      } else {
        if (!repeatCycle) {
          currentState = STATE_MINUTES;
          DmxSimple.write(1, 0);
        } else {
          startDmxPhase(true);
        }
      }
    }
    if (buttonPressed()) {
      currentState = STATE_MINUTES;
      DmxSimple.write(1, 0);
    }
    drawScreen("Loopt", minutesValue, secondsValue);
  }
}

void startDmxPhase(bool turnOn) {
  dmxIsOn = turnOn;
  DmxSimple.write(1, dmxIsOn ? 255 : 0);
  phaseStart = millis();
}

bool buttonPressed() {
  if (digitalRead(ENC_SW) == LOW) {
    unsigned long t = millis();
    if (t - lastButtonTime > debounceMs) {
      lastButtonTime = t;
      while (digitalRead(ENC_SW) == LOW) { delay(5); }
      return true;
    }
  }
  return false;
}

void drawScreen(const char* mode, int minVal, int secVal) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Tijd: ");
  display.print(minVal);
  display.print(":");
  display.print(secVal);

  display.setTextSize(1);
  display.setCursor(0, 28);
  display.print("Modus: ");
  display.println(mode);

  display.display();
}
