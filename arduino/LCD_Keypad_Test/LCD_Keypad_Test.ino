/*
  LCD_Keypad_Test.ino - test program for an Arduino UNO + "LCD Keypad Shield"
  (clone of the DFRobot shield: HD44780 display 16x2 and 6 buttons)

  Shield connections:
    LCD:  RS = D8, E = D9, D4..D7 = D4..D7   ->  LiquidCrystal lcd(8, 9, 4, 5, 6, 7)
    RIGHT, UP, DOWN, LEFT, SELECT: resistor ladder on the analog input A0
    RST:  the RESET button of the Arduino. The CPU cannot read it, so the RST test
          is a restart of the board: the restart counter lives in RAM, which
          survives a reset but not a power-off (menu "5 Info / RST").
    Backlight: D10 (not on every clone)

  Use:
    UP / DOWN       select a menu item
    SELECT / RIGHT  start the test
    LEFT            back to the menu (in the button test: hold LEFT for 2 s)
  Serial monitor: 115200 bps - pressed buttons and raw ADC values.

  Blank screen or only boxes in the top row: adjust the contrast with the
  blue potentiometer on the shield.
*/
#include <LiquidCrystal.h>

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);   // RS, E, D4, D5, D6, D7

const uint8_t KEY_PIN = A0;            // resistor ladder of the buttons
const uint8_t BL_PIN = 10;             // backlight

enum Key : uint8_t { K_NONE, K_RIGHT, K_UP, K_DOWN, K_LEFT, K_SELECT };
const char *const KEY_NAME[] = {"------", "RIGHT ", "UP    ", "DOWN  ", "LEFT  ", "SELECT"};
const char KEY_LETTER[] = "-RUDLS";
const uint8_t ALL_KEYS = 0x3E;         // bits 1..5: RIGHT, UP, DOWN, LEFT, SELECT

// ADC thresholds between the buttons. They cover both common versions of the shield:
// v1.1 (about 0 / 100 / 255 / 410 / 640) and v1.0 (about 0 / 145 / 330 / 505 / 740).
// With no button pressed the reading is about 1023.
Key decodeAdc(int v) {
  if (v < 60) return K_RIGHT;
  if (v < 200) return K_UP;
  if (v < 400) return K_DOWN;
  if (v < 600) return K_LEFT;
  if (v < 850) return K_SELECT;
  return K_NONE;
}

// Variables in the .noinit section are not cleared when the program starts: RAM keeps
// them after a reset (RST button), and after power-on they hold random values.
uint16_t bootMagic __attribute__((section(".noinit")));
uint16_t resetCount __attribute__((section(".noinit")));
bool coldStart;                        // true = start after power-on
bool backlightOn = true;

// ---------------------------------------------------------------- buttons
int adcValue = 1023;                   // last raw reading of A0
Key stableKey = K_NONE;                // state after debouncing

// One reading every 10 ms; a new state counts after 3 equal readings in a row.
Key readKey() {
  static Key candidate = K_NONE;
  static uint8_t same = 0;
  static uint32_t last = 0;
  if (millis() - last >= 10) {
    last = millis();
    adcValue = analogRead(KEY_PIN);
    Key k = decodeAdc(adcValue);
    if (k != candidate) {
      candidate = k;
      same = 0;
    } else if (same < 3 && ++same == 3) {
      stableKey = candidate;
    }
  }
  return stableKey;
}

// Returns a button once, when it is pressed; K_NONE in all other calls.
Key keyPressed() {
  static Key prev = K_NONE;
  Key k = readKey();
  Key ev = (k != prev && k != K_NONE) ? k : K_NONE;
  prev = k;
  if (ev != K_NONE) {
    Serial.print(F("Button "));
    Serial.print(KEY_NAME[ev]);
    Serial.print(F(" ADC="));
    Serial.println(adcValue);
  }
  return ev;
}

// Waits up to ms milliseconds or until a button is pressed; returns that button.
Key waitOrKey(uint16_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    Key k = keyPressed();
    if (k != K_NONE) return k;
  }
  return K_NONE;
}

Key waitKey() {
  Key k;
  while ((k = keyPressed()) == K_NONE) {
  }
  return k;
}

// ---------------------------------------------------------------- LCD
// Prints a text in row and pads it with spaces to 16 characters.
void printRow(uint8_t row, const char *s) {
  lcd.setCursor(0, row);
  uint8_t n = 0;
  while (*s && n < 16) {
    lcd.write(*s++);
    n++;
  }
  while (n++ < 16) lcd.write(' ');
}

void printRow(uint8_t row, const __FlashStringHelper *fs) {
  const char *p = (const char *)fs;
  lcd.setCursor(0, row);
  uint8_t n = 0;
  char c;
  while (n < 16 && (c = pgm_read_byte(p++)) != 0) {
    lcd.write(c);
    n++;
  }
  while (n++ < 16) lcd.write(' ');
}

void fillScreen(uint8_t code) {
  for (uint8_t row = 0; row < 2; row++) {
    lcd.setCursor(0, row);
    for (uint8_t col = 0; col < 16; col++) lcd.write(code);
  }
}

// Backlight. On some clones D10 drives the base of a transistor without a resistor,
// so a high level on the D10 output overloads the pin. Therefore: on = D10 as an
// input (a resistor on the shield switches the transistor on), off = output LOW.
void setBacklight(bool on) {
  backlightOn = on;
  if (on) {
    pinMode(BL_PIN, INPUT);
  } else {
    digitalWrite(BL_PIN, LOW);
    pinMode(BL_PIN, OUTPUT);
  }
}

// ---------------------------------------------------------------- tests
// 1: buttons. Shows the current button and the ADC value, in the second row
// the buttons pressed at least once. Exit: LEFT for 2 s.
void testButtons() {
  uint8_t done = 0;
  bool reported = false;
  uint32_t leftSince = 0;
  uint32_t lastDraw = 0;
  uint32_t t0 = millis();
  char buf[17];

  for (;;) {
    Key ev = keyPressed();
    Key now = stableKey;
    if (ev != K_NONE) done |= 1 << ev;

    if (now == K_LEFT) {
      if (leftSince == 0) leftSince = millis();
      else if (millis() - leftSince >= 2000) return;
    } else {
      leftSince = 0;
    }

    if (millis() - lastDraw >= 100) {
      lastDraw = millis();
      snprintf(buf, sizeof buf, "%s  ADC:%4d", KEY_NAME[now], adcValue);
      printRow(0, buf);
      if (millis() - t0 < 2000) {
        printRow(1, F("LEFT 2s = menu"));
      } else {
        char marks[6];
        uint8_t n = 0;
        for (uint8_t i = 1; i <= 5; i++) {
          bool hit = done & (1 << i);
          marks[i - 1] = hit ? KEY_LETTER[i] : '.';
          n += hit;
        }
        marks[5] = 0;
        snprintf(buf, sizeof buf, "%s %u/5%s", marks, (unsigned)n, n == 5 ? " ALL OK" : "");
        printRow(1, buf);
      }
    }

    if (done == ALL_KEYS && !reported) {
      reported = true;
      Serial.println(F("All 5 buttons work"));
    }
  }
}

// Step of the LCD test: waits ms or until a button; false = aborted with LEFT.
bool stepWait(uint16_t ms) {
  return waitOrKey(ms) != K_LEFT;
}

// 2: display. The steps advance by time or with any button; LEFT aborts.
void testLcd() {
  // Custom 5x8 characters (the last, 8th row is the cursor line - it is tested too).
  static uint8_t allOn[8] = {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
  static uint8_t checkA[8] = {0x15, 0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x15, 0x0A};
  static uint8_t checkB[8] = {0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x15};
  static uint8_t frame[8] = {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F};
  lcd.createChar(0, allOn);
  lcd.createChar(1, checkA);
  lcd.createChar(2, checkB);
  lcd.createChar(3, frame);
  Serial.println(F("Test LCD"));

  bool ok = true;
  lcd.clear();
  printRow(0, F("Test LCD"));
  printRow(1, F("LEFT = abort"));
  ok = stepWait(1500);

  if (ok) {                            // all pixels on
    fillScreen(0);
    ok = stepWait(2000);
  }
  if (ok) {                            // all off
    lcd.clear();
    ok = stepWait(1000);
  }
  if (ok) {                            // checkerboard and its negative: every
    fillScreen(1);                     // pixel once on and once off
    ok = stepWait(1500);
  }
  if (ok) {
    fillScreen(2);
    ok = stepWait(1500);
  }
  if (ok) {                            // frames: borders of all 32 fields
    fillScreen(3);
    ok = stepWait(1500);
  }
  if (ok) {                            // addressing: field by field, digits 0..F
    lcd.clear();
    for (uint8_t i = 0; i < 32 && ok; i++) {
      lcd.setCursor(i % 16, i / 16);
      lcd.write("0123456789ABCDEF"[i % 16]);
      ok = stepWait(60);
    }
    if (ok) ok = stepWait(1500);
  }
  for (uint16_t base = 0x20; base < 0x100 && ok; base += 32) {   // ROM characters
    for (uint8_t row = 0; row < 2; row++) {
      lcd.setCursor(0, row);
      for (uint8_t i = 0; i < 16; i++) lcd.write((uint8_t)(base + row * 16 + i));
    }
    ok = stepWait(1200);
  }
  if (ok) {                            // cursor and blinking
    lcd.clear();
    printRow(0, F("Cursor + blink"));
    lcd.setCursor(7, 1);
    lcd.cursor();
    lcd.blink();
    ok = stepWait(2500);
    lcd.noBlink();
    lcd.noCursor();
  }
  if (ok) {                            // scrolling the screen contents
    lcd.clear();
    printRow(0, F("Scrolling"));
    printRow(1, F("<<<<<<<>>>>>>>>"));
    for (uint8_t i = 0; i < 16 && ok; i++) {
      lcd.scrollDisplayLeft();
      ok = stepWait(120);
    }
    for (uint8_t i = 0; i < 16 && ok; i++) {
      lcd.scrollDisplayRight();
      ok = stepWait(120);
    }
  }
  if (ok) {                            // display off and on again
    lcd.clear();
    printRow(0, F("Display off/on"));
    ok = stepWait(800);
    lcd.noDisplay();
    if (ok) ok = stepWait(800);
    lcd.display();
  }
  lcd.clear();                         // also clears the scroll offset
  if (ok) {
    printRow(0, F("LCD test: done"));
    printRow(1, F("press any key"));
    waitKey();
  }
}

// 3: character browser: 16 codes at a time. UP/DOWN/RIGHT - page, LEFT - menu.
// Codes 00-0F are the custom characters (CGRAM): after the LCD test they show the test patterns.
void browseCharset() {
  uint8_t base = 0x20;
  char buf[17];
  for (;;) {
    snprintf(buf, sizeof buf, "Codes %02X-%02X U/D", base, base + 15);
    printRow(0, buf);
    lcd.setCursor(0, 1);
    for (uint8_t i = 0; i < 16; i++) lcd.write((uint8_t)(base + i));
    Key k = waitKey();
    if (k == K_LEFT) return;
    if (k == K_UP) base -= 16;         // uint8_t wraps around: 00 <-> F0
    if (k == K_DOWN || k == K_RIGHT) base += 16;
  }
}

// 4: backlight. SELECT/RIGHT/UP/DOWN toggles, LEFT - menu (with the backlight on).
// If nothing changes, the clone has no backlight control on D10.
void testBacklight() {
  for (;;) {
    printRow(0, backlightOn ? F("Backlight: ON") : F("Backlight: OFF"));
    printRow(1, F("SEL=toggle LEFT<"));
    if (waitKey() == K_LEFT) {
      setBacklight(true);
      return;
    }
    setBacklight(!backlightOn);
  }
}

// 5: information on the start and the reset counter (test of the RST button). LEFT - menu.
void showInfo() {
  char buf[17];
  printRow(0, coldStart ? F("Start: power on") : F("Start: RST/reset"));
  uint32_t t0 = millis();
  uint8_t shown = 255;
  for (;;) {
    uint8_t phase = ((millis() - t0) / 2000) % 2;
    if (phase != shown) {
      shown = phase;
      if (phase == 0) {
        snprintf(buf, sizeof buf, "RST resets: %u", resetCount);
        printRow(1, buf);
      } else {
        printRow(1, F("Press RST"));
      }
    }
    if (keyPressed() == K_LEFT) return;
  }
}

// ---------------------------------------------------------------- menu
const char *const MENU[] = {"1 Buttons", "2 LCD test", "3 LCD charset", "4 Backlight",
                            "5 Info / RST"};
const uint8_t MENU_N = sizeof MENU / sizeof MENU[0];
uint8_t menuSel = 0;

void showMenu() {
  char buf[17];
  snprintf(buf, sizeof buf, ">%s", MENU[menuSel]);
  printRow(0, buf);
  snprintf(buf, sizeof buf, " %s", MENU[(menuSel + 1) % MENU_N]);
  printRow(1, buf);
}

void setup() {
  if (bootMagic != 0xA55A) {           // RAM without the marker: power-on
    bootMagic = 0xA55A;
    resetCount = 0;
    coldStart = true;
  } else {                             // the marker survived: reset with RST
    resetCount++;
    coldStart = false;
  }

  Serial.begin(115200);
  setBacklight(true);
  lcd.begin(16, 2);

  Serial.println(F("LCD Keypad Shield - test"));
  Serial.println(coldStart ? F("Start after power-on") : F("Start after reset (RST)"));
  Serial.print(F("Reset counter: "));
  Serial.println(resetCount);

  printRow(0, F("LCD Keypad Test"));
  printRow(1, coldStart ? F("Start: power on") : F("Start: RST/reset"));
  waitOrKey(1500);
  showMenu();
}

void loop() {
  Key k = keyPressed();
  if (k == K_NONE) return;
  if (k == K_UP) menuSel = (menuSel + MENU_N - 1) % MENU_N;
  if (k == K_DOWN) menuSel = (menuSel + 1) % MENU_N;
  if (k == K_SELECT || k == K_RIGHT) {
    switch (menuSel) {
      case 0: testButtons(); break;
      case 1: testLcd(); break;
      case 2: browseCharset(); break;
      case 3: testBacklight(); break;
      case 4: showInfo(); break;
    }
  }
  showMenu();
}
