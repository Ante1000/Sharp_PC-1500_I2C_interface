/*
  PC1500_I2C_Slave.ino - Arduino UNO + LCD Keypad Shield as an I2C device (slave)
  for the Sharp PC-1500(A) with the I2C routine v1.3 (the PC-1500 is the master, the
  Arduino has the address 0x42). BASIC program: pc1500_i2c_arduino_test-v1.0.txt.

  Wiring (diagram in docs/arduino_tester.md, everything at 5 V):
    PC-1500 PC7  pin 10  ---|<|---  A5 = SCL   Schottky diode, cathode (stripe) to PC7
    PC-1500 PB2  pin 27  ---[470]--  A4 = SDA
    PC-1500 GND  pin 52-55 ------  GND
    optional: 4.7 kOhm from A4 to 5 V and from A5 to 5 V
  The diode is needed at 5 V too: the hardware I2C of the UNO holds SCL low after
  every byte, and without the diode PC7 would drive this line high at the same time.
  The PC-1500 must be removed from the CE-150 (the CE-150 drives PB2).

  Protocol. Every command is a separate write (CALL with W > 0, R = 0), followed by a
  separate read of the answer (W = 0, R = 32). Do not combine write and read in one
  CALL: at a repeated START the Wire library calls onReceive inside the interrupt and
  the AVR may hold SCL low meanwhile, which our master cannot see. After the read
  address the master keeps SCL low for about 74 us; onRequest needs about 25-30 us.
  At most 32 bytes per transaction (buffer of the Wire library); a longer write ends
  with a missing ACK (E = 2 on the PC-1500).

  Answer: [status][command number][N][N data bytes], N <= 29
    status: 0 = still working (read again), 1 = OK,
            2 = unknown command, 3 = bad parameter

  Command (byte 0)                        Parameters         Answer data
  'P' identification                      -                  "PC1500 I2C v1.0"
  'T' text on the LCD                     row 0/1, chars     -  (up to 16 characters)
  'C' clear the LCD                       -                  -
  'A' ADC measurement                     channel 0..3 (A0-A3) value 0..1023 (2 B),
                                                             voltage in mV (2 B)
  'K' shield buttons                      -                  now, last pressed
                                                             (then cleared), counter
                                                             0 none, 1 RIGHT, 2 UP,
                                                             3 DOWN, 4 LEFT, 5 SELECT
  'E' echo                                up to 29 bytes     the same bytes
  'F' arm the speed measurement           -                  - (do not read the answer)
  'X' test data for the measurement       any                -
  'S' result of the measurement           -                  valid (1 B), bytes (1 B),
                                                             time us (4 B), SCL Hz (2 B),
                                                             bytes/s (2 B)
  'L' backlight                           1 / 0              -
  'D' read inputs D2, D3                  -                  D2, D3 (0/1)
  'O' set D2/D3                           pin 2/3, state 0/1 (output) or 2 (input)
  'U' time since the Arduino started      -                  ms (4 B)
  Numbers of more than one byte: low byte first.

  Speed measurement: the PC-1500 sends 'F', then (without reading the answer) a write
  of 'X' + 30 bytes; the Arduino measures the time from the first SCL edge to the STOP
  condition of this write. It shows the result on the LCD and returns it for 'S'.

  Shield: SELECT switches the LCD view (text from the PC-1500 / I2C statistics); the
  PC-1500 can read all buttons with the command 'K'. Serial monitor: 115200 bps.
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
const uint8_t BUF = 32;                // buffer of the Wire library on the UNO
const uint8_t MAXDATA = BUF - 3;

// ---- shared with the I2C interrupt handlers
volatile uint8_t cmdBuf[BUF];
volatile uint8_t cmdLen;
volatile bool cmdReady;                // a command is waiting for loop()
volatile uint8_t respBuf[BUF] = {ST_OK, 0, 0};
volatile uint8_t respLen = 3;
volatile uint8_t seq;                  // command number
volatile uint16_t nWrites, nReads, nOverruns;

enum : uint8_t { M_IDLE, M_ARMED, M_RUNNING, M_DONE };
volatile uint8_t measState = M_IDLE;
volatile uint32_t measStart, measEnd;
volatile uint8_t measBytes;            // bytes of the measured write including the address

// ---- program state
uint16_t nErrors;
uint8_t lastCmd = '-';
uint8_t lastKeyEvent = K_NONE, keyCount = 0;
bool backlightOn = true;
uint32_t measDur;
uint16_t measHz, measBps;
bool measReported;

char text[2][17] = {"I2C slave 0x42", "Waiting for PC"};
enum View : uint8_t { V_TEXT, V_STAT };
View view = V_TEXT;
bool dirty = true;                     // the LCD must be redrawn
char note[2][17];                      // temporary message (measurement result)
uint32_t noteSince;
bool noteOn = false;

// ---------------------------------------------------------------- I2C (interrupts)
// Called in the interrupt after the STOP of a write: only copies the command, loop() runs it.
void onI2cReceive(int n) {
  if (n <= 0) return;                  // address only: presence test (scanner)
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
  if (cmdReady) nOverruns++;           // the previous one has not been run yet
  cmdLen = i;
  cmdReady = true;
  seq++;
  respBuf[0] = ST_BUSY;
  respBuf[1] = seq;
  respBuf[2] = 0;
  respLen = 3;
  nWrites++;
}

// Called in the interrupt at the start of a read: hands over the prepared answer. It must
// finish within about 74 us, before the PC-1500 clocks the data (it takes about 25-30 us).
void onI2cRequest() {
  Wire.write((const uint8_t *)respBuf, respLen);
  if (measState == M_RUNNING) measState = M_IDLE;   // the measurement covers writes only
  nReads++;
}

// First SCL edge (A5 = PCINT13) after the measurement was armed: start of the transaction.
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
  PCIFR = _BV(PCIF1);                  // clear an old request
  PCMSK1 = _BV(PCINT13);
  PCICR |= _BV(PCIE1);
  interrupts();
}

// Publishes the answer, unless the next command has already arrived meanwhile.
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

// ---------------------------------------------------------------- buttons, LCD
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

// On some clones D10 drives a transistor without a base resistor: never drive it HIGH.
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

// ---------------------------------------------------------------- commands
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
      if (ch > 3) {                    // A4/A5 are the I2C lines
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
  Serial.print(F(" command "));
  if (c[0] >= 32 && c[0] < 127) Serial.print((char)c[0]);
  else Serial.print(c[0], HEX);
  Serial.print(F(", bytes "));
  Serial.print(n);
  Serial.print(F(" -> status "));
  Serial.println(st);
}

// Result of the speed measurement: on the LCD, to the serial monitor and for 'S'.
void reportMeasure() {
  measReported = true;
  measDur = measEnd - measStart;
  if (measDur == 0) measDur = 1;
  uint32_t bits = (uint32_t)measBytes * 9;   // 8 bits + ACK per byte
  uint32_t hz = bits * 1000000UL / measDur;
  uint32_t bps = (uint32_t)(measBytes - 1) * 1000000UL / measDur;
  measHz = hz > 65535 ? 65535 : hz;
  measBps = bps > 65535 ? 65535 : bps;
  char l0[17], l1[17];
  snprintf(l0, sizeof l0, "SCL %u.%02u kHz", measHz / 1000, (measHz % 1000) / 10);
  snprintf(l1, sizeof l1, "%u B/s  %uB", measBps, (unsigned)(measBytes - 1));
  showNote(l0, l1);
  Serial.print(F("Measurement: "));
  Serial.print(measBytes);
  Serial.print(F(" B incl. address in "));
  Serial.print(measDur);
  Serial.print(F(" us, SCL "));
  Serial.print(measHz);
  Serial.print(F(" Hz, "));
  Serial.print(measBps);
  Serial.println(F(" B/s"));
}

// ---------------------------------------------------------------- start, loop
void setup() {
  Serial.begin(115200);
  setBacklight(true);
  lcd.begin(16, 2);
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  Wire.begin(I2C_ADDR);                // slave mode
  Wire.onReceive(onI2cReceive);
  Wire.onRequest(onI2cRequest);
  Serial.println(F("PC-1500 I2C slave 0x42 ready"));
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
    Serial.print(F("Button "));
    Serial.println(KEY_NAME[k]);
  }

  if (measState == M_DONE && !measReported) reportMeasure();
  render();
}
