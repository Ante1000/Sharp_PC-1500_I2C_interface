/*
  LCD_Keypad_Test.ino - program testowy dla Arduino UNO + "LCD Keypad Shield"
  (klon shielda DFRobot: wyświetlacz HD44780 16x2 i 6 przycisków)

  Połączenia shielda:
    LCD:  RS = D8, E = D9, D4..D7 = D4..D7   ->  LiquidCrystal lcd(8, 9, 4, 5, 6, 7)
    RIGHT, UP, DOWN, LEFT, SELECT: drabinka rezystorów na wejściu analogowym A0
    RST:  przycisk RESET Arduino. Procesor nie może go odczytać, więc test RST
          polega na restarcie płytki: licznik restartów leży w RAM-ie, który
          przeżywa reset, ale nie wyłączenie zasilania (menu "5 Info / RST").
    Podświetlenie: D10 (nie w każdym klonie)

  Obsługa:
    UP / DOWN       wybór pozycji menu
    SELECT / RIGHT  uruchomienie testu
    LEFT            powrót do menu (w teście przycisków: przytrzymać LEFT 2 s)
  Monitor portu szeregowego: 115200 bps - naciśnięte przyciski i surowe wartości ADC.

  Pusty ekran albo same prostokąty w górnym wierszu: wyreguluj kontrast
  niebieskim potencjometrem na shieldzie.
*/
#include <LiquidCrystal.h>

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);   // RS, E, D4, D5, D6, D7

const uint8_t KEY_PIN = A0;            // drabinka rezystorów przycisków
const uint8_t BL_PIN = 10;             // podświetlenie

enum Key : uint8_t { K_NONE, K_RIGHT, K_UP, K_DOWN, K_LEFT, K_SELECT };
const char *const KEY_NAME[] = {"------", "RIGHT ", "UP    ", "DOWN  ", "LEFT  ", "SELECT"};
const char KEY_LETTER[] = "-RUDLS";
const uint8_t ALL_KEYS = 0x3E;         // bity 1..5: RIGHT, UP, DOWN, LEFT, SELECT

// Progi ADC między przyciskami. Obejmują obie popularne wersje shielda:
// v1.1 (ok. 0 / 100 / 255 / 410 / 640) i v1.0 (ok. 0 / 145 / 330 / 505 / 740).
// Bez naciśnięcia odczyt wynosi ok. 1023.
Key decodeAdc(int v) {
  if (v < 60) return K_RIGHT;
  if (v < 200) return K_UP;
  if (v < 400) return K_DOWN;
  if (v < 600) return K_LEFT;
  if (v < 850) return K_SELECT;
  return K_NONE;
}

// Zmienne w sekcji .noinit nie są zerowane przy starcie programu: RAM zachowuje
// je po resecie (przycisk RST), a po włączeniu zasilania mają przypadkową wartość.
uint16_t bootMagic __attribute__((section(".noinit")));
uint16_t resetCount __attribute__((section(".noinit")));
bool coldStart;                        // true = start po włączeniu zasilania
bool backlightOn = true;

// ---------------------------------------------------------------- przyciski
int adcValue = 1023;                   // ostatni surowy odczyt A0
Key stableKey = K_NONE;                // stan po eliminacji drgań styków

// Jeden odczyt co 10 ms; nowy stan obowiązuje po 3 kolejnych zgodnych odczytach.
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

// Zwraca przycisk raz, w chwili naciśnięcia; w pozostałych wywołaniach K_NONE.
Key keyPressed() {
  static Key prev = K_NONE;
  Key k = readKey();
  Key ev = (k != prev && k != K_NONE) ? k : K_NONE;
  prev = k;
  if (ev != K_NONE) {
    Serial.print(F("Przycisk "));
    Serial.print(KEY_NAME[ev]);
    Serial.print(F(" ADC="));
    Serial.println(adcValue);
  }
  return ev;
}

// Czeka do ms milisekund albo do naciśnięcia przycisku; zwraca ten przycisk.
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
// Wypisuje tekst w wierszu row i dopełnia go spacjami do 16 znaków.
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

// Podświetlenie. W części klonów D10 steruje bazą tranzystora bez rezystora,
// więc stan wysoki na wyjściu D10 przeciąża pin. Dlatego: włączone = D10 jako
// wejście (rezystor na shieldzie włącza tranzystor), wyłączone = wyjście LOW.
void setBacklight(bool on) {
  backlightOn = on;
  if (on) {
    pinMode(BL_PIN, INPUT);
  } else {
    digitalWrite(BL_PIN, LOW);
    pinMode(BL_PIN, OUTPUT);
  }
}

// ---------------------------------------------------------------- testy
// 1: przyciski. Pokazuje bieżący przycisk i wartość ADC, w drugim wierszu
// listę przycisków naciśniętych przynajmniej raz. Wyjście: LEFT przez 2 s.
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
      Serial.println(F("Wszystkie 5 przyciskow dziala"));
    }
  }
}

// Krok testu LCD: czeka ms albo do przycisku; false = przerwane przez LEFT.
bool stepWait(uint16_t ms) {
  return waitOrKey(ms) != K_LEFT;
}

// 2: wyświetlacz. Kolejne kroki zmienia czas albo dowolny przycisk; LEFT przerywa.
void testLcd() {
  // Własne znaki 5x8 (ostatni, 8. wiersz to linia kursora - też jest testowany).
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
  printRow(1, F("LEFT = przerwij"));
  ok = stepWait(1500);

  if (ok) {                            // wszystkie piksele zapalone
    fillScreen(0);
    ok = stepWait(2000);
  }
  if (ok) {                            // wszystkie zgaszone
    lcd.clear();
    ok = stepWait(1000);
  }
  if (ok) {                            // szachownica i jej negatyw: każdy piksel
    fillScreen(1);                     // raz zapalony, raz zgaszony
    ok = stepWait(1500);
  }
  if (ok) {
    fillScreen(2);
    ok = stepWait(1500);
  }
  if (ok) {                            // ramki: granice wszystkich 32 pól
    fillScreen(3);
    ok = stepWait(1500);
  }
  if (ok) {                            // adresowanie: pole po polu, cyfry 0..F
    lcd.clear();
    for (uint8_t i = 0; i < 32 && ok; i++) {
      lcd.setCursor(i % 16, i / 16);
      lcd.write("0123456789ABCDEF"[i % 16]);
      ok = stepWait(60);
    }
    if (ok) ok = stepWait(1500);
  }
  for (uint16_t base = 0x20; base < 0x100 && ok; base += 32) {   // znaki z ROM-u
    for (uint8_t row = 0; row < 2; row++) {
      lcd.setCursor(0, row);
      for (uint8_t i = 0; i < 16; i++) lcd.write((uint8_t)(base + row * 16 + i));
    }
    ok = stepWait(1200);
  }
  if (ok) {                            // kursor i miganie
    lcd.clear();
    printRow(0, F("Kursor + blink"));
    lcd.setCursor(7, 1);
    lcd.cursor();
    lcd.blink();
    ok = stepWait(2500);
    lcd.noBlink();
    lcd.noCursor();
  }
  if (ok) {                            // przesuwanie zawartości ekranu
    lcd.clear();
    printRow(0, F("Przewijanie"));
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
  if (ok) {                            // wyłączenie i włączenie wyświetlacza
    lcd.clear();
    printRow(0, F("Display off/on"));
    ok = stepWait(800);
    lcd.noDisplay();
    if (ok) ok = stepWait(800);
    lcd.display();
  }
  lcd.clear();                         // kasuje też przesunięcie ekranu
  if (ok) {
    printRow(0, F("Test LCD: koniec"));
    printRow(1, F("dowolny przycisk"));
    waitKey();
  }
}

// 3: przeglądarka znaków: 16 kodów naraz. UP/DOWN/RIGHT - strona, LEFT - menu.
// Kody 00-0F to znaki własne (CGRAM): po teście LCD widać w nich wzory testowe.
void browseCharset() {
  uint8_t base = 0x20;
  char buf[17];
  for (;;) {
    snprintf(buf, sizeof buf, "Kody %02X-%02X U/D", base, base + 15);
    printRow(0, buf);
    lcd.setCursor(0, 1);
    for (uint8_t i = 0; i < 16; i++) lcd.write((uint8_t)(base + i));
    Key k = waitKey();
    if (k == K_LEFT) return;
    if (k == K_UP) base -= 16;         // uint8_t zawija się: 00 <-> F0
    if (k == K_DOWN || k == K_RIGHT) base += 16;
  }
}

// 4: podświetlenie. SELECT/RIGHT/UP/DOWN przełącza, LEFT - menu (z podświetleniem).
// Jeśli nic się nie zmienia, klon nie ma sterowania podświetleniem na D10.
void testBacklight() {
  for (;;) {
    printRow(0, backlightOn ? F("Podswietl.: ON") : F("Podswietl.: OFF"));
    printRow(1, F("SEL=zmien LEFT<"));
    if (waitKey() == K_LEFT) {
      setBacklight(true);
      return;
    }
    setBacklight(!backlightOn);
  }
}

// 5: informacja o starcie i liczniku resetów (test przycisku RST). LEFT - menu.
void showInfo() {
  char buf[17];
  printRow(0, coldStart ? F("Start: zasilanie") : F("Start: RST/reset"));
  uint32_t t0 = millis();
  uint8_t shown = 255;
  for (;;) {
    uint8_t phase = ((millis() - t0) / 2000) % 2;
    if (phase != shown) {
      shown = phase;
      if (phase == 0) {
        snprintf(buf, sizeof buf, "Resety RST: %u", resetCount);
        printRow(1, buf);
      } else {
        printRow(1, F("Wcisnij RST"));
      }
    }
    if (keyPressed() == K_LEFT) return;
  }
}

// ---------------------------------------------------------------- menu
const char *const MENU[] = {"1 Przyciski", "2 Test LCD", "3 Znaki LCD", "4 Podswietlenie",
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
  if (bootMagic != 0xA55A) {           // RAM bez znacznika: włączenie zasilania
    bootMagic = 0xA55A;
    resetCount = 0;
    coldStart = true;
  } else {                             // znacznik przetrwał: reset przyciskiem RST
    resetCount++;
    coldStart = false;
  }

  Serial.begin(115200);
  setBacklight(true);
  lcd.begin(16, 2);

  Serial.println(F("LCD Keypad Shield - test"));
  Serial.println(coldStart ? F("Start po wlaczeniu zasilania") : F("Start po resecie (RST)"));
  Serial.print(F("Licznik resetow: "));
  Serial.println(resetCount);

  printRow(0, F("LCD Keypad Test"));
  printRow(1, coldStart ? F("Start: zasilanie") : F("Start: RST/reset"));
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
