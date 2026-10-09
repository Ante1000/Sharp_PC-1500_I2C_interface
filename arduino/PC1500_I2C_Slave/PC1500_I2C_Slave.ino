/*
  PC1500_I2C_Slave.ino - Arduino UNO + LCD Keypad Shield jako urządzenie I2C (slave)
  dla Sharpa PC-1500(A) z procedurą I2C v1.0 (PC-1500 jest masterem, Arduino ma
  adres 0x42). Program w BASIC-u: pc1500_i2c_arduino_test-v1.0.txt.

  Połączenie (schemat w docs/arduino_tester.md, wszystko na 5 V):
    PC-1500 PC7  pin 10  ---|<|---  A5 = SCL   dioda Schottky'ego, katoda (pasek) do PC7
    PC-1500 PB2  pin 27  ---[470]--  A4 = SDA
    PC-1500 GND  pin 52-55 ------  GND
    4,7 kΩ z A4 do 5 V i z A5 do 5 V
  Dioda jest potrzebna także przy 5 V: sprzętowe I2C w UNO przytrzymuje SCL w stanie
  niskim po każdym bajcie, a PC7 bez diody wymuszałby na tej linii stan wysoki.
  PC-1500 musi być wyjęty z CE-150 (CE-150 steruje linią PB2).

  Protokół. Każde polecenie to osobny zapis (CALL z W > 0, R = 0), potem osobny
  odczyt odpowiedzi (W = 0, R = 32). Nie łączyć zapisu i odczytu w jednym CALL:
  przy powtórzonym STARCIE biblioteka Wire wywołuje onReceive w przerwaniu i AVR
  może wtedy trzymać SCL w stanie niskim, czego nasz master nie widzi. Po adresie
  odczytu master trzyma SCL nisko ok. 73 us; onRequest potrzebuje ok. 25-30 us.
  Najwyżej 32 bajty w transakcji (bufor biblioteki Wire); dłuższy zapis kończy się
  brakiem ACK (E = 2 w PC-1500).

  Odpowiedź: [status][numer polecenia][N][N bajtów danych], N <= 29
    status: 0 = jeszcze liczę (odczytaj ponownie), 1 = OK,
            2 = nieznane polecenie, 3 = zły parametr

  Polecenie (bajt 0)                      Parametry          Dane odpowiedzi
  'P' identyfikator                       -                  "PC1500 I2C v1.0"
  'T' tekst na LCD                        wiersz 0/1, znaki  -  (do 16 znaków)
  'C' czyszczenie LCD                     -                  -
  'A' pomiar ADC                          kanał 0..3 (A0-A3) wartość 0..1023 (2 B),
                                                             napięcie w mV (2 B)
  'K' przyciski shielda                   -                  teraz, ostatnio naciśnięty
                                                             (potem kasowany), licznik
                                                             0 brak, 1 RIGHT, 2 UP,
                                                             3 DOWN, 4 LEFT, 5 SELECT
  'E' echo                                do 29 bajtów       te same bajty
  'F' uzbrojenie pomiaru prędkości        -                  - (odpowiedzi nie czytać)
  'X' dane testowe do pomiaru             dowolne            -
  'S' wynik pomiaru                       -                  ważny (1 B), bajty (1 B),
                                                             czas us (4 B), SCL Hz (2 B),
                                                             bajty/s (2 B)
  'L' podświetlenie                       1 / 0              -
  'D' odczyt wejść D2, D3                 -                  D2, D3 (0/1)
  'O' ustawienie D2/D3                    pin 2/3, stan 0/1 (wyjście) albo 2 (wejście)
  'U' czas od startu Arduino              -                  ms (4 B)
  Liczby wielobajtowe: najmłodszy bajt pierwszy.

  Pomiar prędkości: PC-1500 wysyła 'F', potem (bez czytania odpowiedzi) zapis 'X' +
  30 bajtów; Arduino mierzy czas od pierwszego zbocza SCL do warunku STOP tego zapisu.
  Wynik pokazuje na LCD i oddaje na polecenie 'S'.

  Shield: SELECT przełącza widok LCD (tekst z PC-1500 / statystyka I2C); wszystkie
  przyciski PC-1500 może odczytać poleceniem 'K'. Monitor portu szeregowego: 115200 bps.
*/
#include <LiquidCrystal.h>
#include <Wire.h>

const uint8_t I2C_ADDR = 0x42;
const char ID[] = "PC1500 I2C v1.0";

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);   // RS, E, D4, D5, D6, D7
const uint8_t KEY_PIN = A0;
const uint8_t BL_PIN = 10;

enum Key : uint8_t { K_NONE, K_RIGHT, K_UP, K_DOWN, K_LEFT, K_SELECT };
const char *const KEY_NAME[] = {"------", "RIGHT ", "UP    ", "DOWN  ", "LEFT  ", "SELECT"};

const uint8_t ST_BUSY = 0, ST_OK = 1, ST_UNKNOWN = 2, ST_BADARG = 3;
const uint8_t BUF = 32;                // bufor biblioteki Wire w UNO
const uint8_t MAXDATA = BUF - 3;

// ---- wspólne z obsługą przerwań I2C
volatile uint8_t cmdBuf[BUF];
volatile uint8_t cmdLen;
volatile bool cmdReady;                // jest polecenie do wykonania w loop()
volatile uint8_t respBuf[BUF] = {ST_OK, 0, 0};
volatile uint8_t respLen = 3;
volatile uint8_t seq;                  // numer polecenia
volatile uint16_t nWrites, nReads, nOverruns;

enum : uint8_t { M_IDLE, M_ARMED, M_RUNNING, M_DONE };
volatile uint8_t measState = M_IDLE;
volatile uint32_t measStart, measEnd;
volatile uint8_t measBytes;            // bajty mierzonego zapisu łącznie z adresem

// ---- stan programu
uint16_t nErrors;
uint8_t lastCmd = '-';
uint8_t lastKeyEvent = K_NONE, keyCount = 0;
bool backlightOn = true;
uint32_t measDur;
uint16_t measHz, measBps;
bool measReported;

char text[2][17] = {"I2C slave 0x42", "Czekam na PC..."};
enum View : uint8_t { V_TEXT, V_STAT };
View view = V_TEXT;
bool dirty = true;                     // trzeba odświeżyć LCD
char note[2][17];                      // komunikat chwilowy (wynik pomiaru)
uint32_t noteSince;
bool noteOn = false;

// ---------------------------------------------------------------- I2C (przerwania)
// Wywoływane w przerwaniu po STOP-ie zapisu: tylko kopiuje polecenie, wykonuje je loop().
void onI2cReceive(int n) {
  if (n <= 0) return;                  // sam adres: test obecności (skaner)
  if (measState == M_RUNNING) {
    measEnd = micros();
    measBytes = n + 1;
    measState = M_DONE;
  }
  uint8_t i = 0;
  while (Wire.available()) {
    uint8_t b = Wire.read();
    if (i < BUF) cmdBuf[i++] = b;
  }
  if (cmdReady) nOverruns++;           // poprzednie nie zostało jeszcze wykonane
  cmdLen = i;
  cmdReady = true;
  seq++;
  respBuf[0] = ST_BUSY;
  respBuf[1] = seq;
  respBuf[2] = 0;
  respLen = 3;
  nWrites++;
}

// Wywoływane w przerwaniu na początku odczytu: oddaje gotową odpowiedź. Musi się
// zmieścić w ok. 69 us, zanim PC-1500 zacznie taktować dane (zajmuje ok. 25-30 us).
void onI2cRequest() {
  Wire.write((const uint8_t *)respBuf, respLen);
  if (measState == M_RUNNING) measState = M_IDLE;   // pomiar dotyczy tylko zapisu
  nReads++;
}

// Pierwsze zbocze SCL (A5 = PCINT13) po uzbrojeniu pomiaru: początek transakcji.
ISR(PCINT1_vect) {
  if (measState == M_ARMED) {
    measStart = micros();
    measState = M_RUNNING;
  }
  PCMSK1 = 0;
}

void armMeasure() {
  noInterrupts();
  measState = M_ARMED;
  measReported = false;
  PCIFR = _BV(PCIF1);                  // skasuj stare zgłoszenie
  PCMSK1 = _BV(PCINT13);
  PCICR |= _BV(PCIE1);
  interrupts();
}

// Publikuje odpowiedź, chyba że w międzyczasie przyszło już następne polecenie.
void reply(uint8_t status, const uint8_t *data, uint8_t len) {
  if (len > MAXDATA) len = MAXDATA;
  noInterrupts();
  if (!cmdReady) {
    for (uint8_t i = 0; i < len; i++) respBuf[3 + i] = data[i];
    respBuf[2] = len;
    respBuf[1] = seq;
    respBuf[0] = status;
    respLen = 3 + len;
  }
  interrupts();
}

// ---------------------------------------------------------------- przyciski, LCD
Key decodeAdc(int v) {
  if (v < 60) return K_RIGHT;
  if (v < 200) return K_UP;
  if (v < 400) return K_DOWN;
  if (v < 600) return K_LEFT;
  if (v < 850) return K_SELECT;
  return K_NONE;
}

int adcValue = 1023;
Key stableKey = K_NONE;

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

Key keyPressed() {
  static Key prev = K_NONE;
  Key k = readKey();
  Key ev = (k != prev && k != K_NONE) ? k : K_NONE;
  prev = k;
  return ev;
}

void printRow(uint8_t row, const char *s) {
  lcd.setCursor(0, row);
  uint8_t n = 0;
  while (*s && n < 16) {
    lcd.write(*s++);
    n++;
  }
  while (n++ < 16) lcd.write(' ');
}

// W części klonów D10 steruje tranzystorem bez rezystora: nigdy nie wystawiać HIGH.
void setBacklight(bool on) {
  backlightOn = on;
  if (on) {
    pinMode(BL_PIN, INPUT);
  } else {
    digitalWrite(BL_PIN, LOW);
    pinMode(BL_PIN, OUTPUT);
  }
}

void setText(uint8_t row, const uint8_t *s, uint8_t len) {
  if (len > 16) len = 16;
  for (uint8_t i = 0; i < len; i++) text[row][i] = s[i] ? s[i] : ' ';
  text[row][len] = 0;
  view = V_TEXT;
  dirty = true;
}

void showNote(const char *l0, const char *l1) {
  strncpy(note[0], l0, 16);
  note[0][16] = 0;
  strncpy(note[1], l1, 16);
  note[1][16] = 0;
  noteOn = true;
  noteSince = millis();
  dirty = true;
}

void render() {
  static uint32_t lastStat = 0;
  if (noteOn) {
    if (millis() - noteSince < 4000) {
      if (dirty) {
        printRow(0, note[0]);
        printRow(1, note[1]);
        dirty = false;
      }
      return;
    }
    noteOn = false;
    dirty = true;
  }
  if (view == V_TEXT) {
    if (dirty) {
      printRow(0, text[0]);
      printRow(1, text[1]);
      dirty = false;
    }
  } else if (dirty || millis() - lastStat >= 250) {
    char buf[17];
    uint16_t w, r, o;
    uint8_t s;
    noInterrupts();
    w = nWrites;
    r = nReads;
    o = nOverruns;
    s = seq;
    interrupts();
    snprintf(buf, sizeof buf, "I2C %02X #%-3u %c", I2C_ADDR, s, lastCmd);
    printRow(0, buf);
    snprintf(buf, sizeof buf, "W%u R%u E%u", w, r, (unsigned)(nErrors + o));
    printRow(1, buf);
    lastStat = millis();
    dirty = false;
  }
}

// ---------------------------------------------------------------- polecenia
void put16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = v >> 8;
}

void put32(uint8_t *p, uint32_t v) {
  for (uint8_t i = 0; i < 4; i++) p[i] = v >> (8 * i);
}

void reportMeasure();

void processCommand() {
  uint8_t c[BUF];
  uint8_t n;
  noInterrupts();
  if (!cmdReady) {
    interrupts();
    return;
  }
  n = cmdLen;
  for (uint8_t i = 0; i < n; i++) c[i] = cmdBuf[i];
  cmdReady = false;
  interrupts();

  uint8_t out[MAXDATA];
  uint8_t st = ST_OK, len = 0;
  lastCmd = c[0];

  switch (c[0]) {
    case 'P':
      len = sizeof ID - 1;
      memcpy(out, ID, len);
      break;
    case 'T':
      if (n < 2 || c[1] > 1) {
        st = ST_BADARG;
        break;
      }
      setText(c[1], c + 2, n - 2);
      break;
    case 'C':
      setText(0, c, 0);
      setText(1, c, 0);
      break;
    case 'A': {
      uint8_t ch = n >= 2 ? c[1] : 1;
      if (ch > 3) {                    // A4/A5 to linie I2C
        st = ST_BADARG;
        break;
      }
      uint16_t v = analogRead(A0 + ch);
      put16(out, v);
      put16(out + 2, (uint32_t)v * 5000 / 1023);
      len = 4;
      break;
    }
    case 'K':
      out[0] = stableKey;
      out[1] = lastKeyEvent;
      out[2] = keyCount;
      lastKeyEvent = K_NONE;
      len = 3;
      break;
    case 'E':
      len = n - 1;
      if (len > MAXDATA) len = MAXDATA;
      memcpy(out, c + 1, len);
      break;
    case 'F':
      armMeasure();
      break;
    case 'X':
      break;
    case 'S':
      if (measState == M_DONE && !measReported) reportMeasure();
      out[0] = measState == M_DONE;
      out[1] = measBytes;
      put32(out + 2, measDur);
      put16(out + 6, measHz);
      put16(out + 8, measBps);
      len = 10;
      break;
    case 'L':
      setBacklight(n < 2 || c[1] != 0);
      break;
    case 'D':
      out[0] = digitalRead(2);
      out[1] = digitalRead(3);
      len = 2;
      break;
    case 'O':
      if (n < 3 || (c[1] != 2 && c[1] != 3) || c[2] > 2) {
        st = ST_BADARG;
        break;
      }
      if (c[2] == 2) {
        pinMode(c[1], INPUT_PULLUP);
      } else {
        digitalWrite(c[1], c[2]);
        pinMode(c[1], OUTPUT);
      }
      break;
    case 'U':
      put32(out, millis());
      len = 4;
      break;
    default:
      st = ST_UNKNOWN;
  }
  if (st != ST_OK) nErrors++;
  reply(st, out, len);
  if (view == V_STAT) dirty = true;

  Serial.print('#');
  Serial.print(seq);
  Serial.print(F(" polecenie "));
  if (c[0] >= 32 && c[0] < 127) Serial.print((char)c[0]);
  else Serial.print(c[0], HEX);
  Serial.print(F(", bajtow "));
  Serial.print(n);
  Serial.print(F(" -> status "));
  Serial.println(st);
}

// Wynik pomiaru prędkości: na LCD, do monitora i do odpowiedzi na 'S'.
void reportMeasure() {
  measReported = true;
  measDur = measEnd - measStart;
  if (measDur == 0) measDur = 1;
  uint32_t bits = (uint32_t)measBytes * 9;   // 8 bitów + ACK na bajt
  uint32_t hz = bits * 1000000UL / measDur;
  uint32_t bps = (uint32_t)(measBytes - 1) * 1000000UL / measDur;
  measHz = hz > 65535 ? 65535 : hz;
  measBps = bps > 65535 ? 65535 : bps;
  char l0[17], l1[17];
  snprintf(l0, sizeof l0, "SCL %u.%02u kHz", measHz / 1000, (measHz % 1000) / 10);
  snprintf(l1, sizeof l1, "%u B/s  %uB", measBps, (unsigned)(measBytes - 1));
  showNote(l0, l1);
  Serial.print(F("Pomiar: "));
  Serial.print(measBytes);
  Serial.print(F(" B z adresem w "));
  Serial.print(measDur);
  Serial.print(F(" us, SCL "));
  Serial.print(measHz);
  Serial.print(F(" Hz, "));
  Serial.print(measBps);
  Serial.println(F(" B/s"));
}

// ---------------------------------------------------------------- start, pętla
void setup() {
  Serial.begin(115200);
  setBacklight(true);
  lcd.begin(16, 2);
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  Wire.begin(I2C_ADDR);                // tryb slave
  Wire.onReceive(onI2cReceive);
  Wire.onRequest(onI2cRequest);
  Serial.println(F("PC-1500 I2C slave 0x42 gotowy"));
}

void loop() {
  processCommand();

  Key k = keyPressed();
  if (k != K_NONE) {
    lastKeyEvent = k;
    keyCount++;
    if (k == K_SELECT) {
      view = view == V_TEXT ? V_STAT : V_TEXT;
      noteOn = false;
      dirty = true;
    }
    Serial.print(F("Przycisk "));
    Serial.println(KEY_NAME[k]);
  }

  if (measState == M_DONE && !measReported) reportMeasure();
  render();
}
