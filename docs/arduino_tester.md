# Arduino UNO tester for the PC-1500 I2C master

A cheap test set for the first test of I2C v1.0 on a real PC-1500(A): an **Arduino UNO** with a
generic **LCD Keypad Shield** (DFRobot clone: HD44780 LCD 16×2 and 6 buttons) works as an I2C
device at address **&42**. The PC-1500 is the master (I2C v1.0 can only be a master); it sends
text to the LCD, reads the buttons and the ADC, runs an echo test with random data and lets the
Arduino measure the real speed of the bus.

> **Status:** both sketches compile without warnings (Arduino AVR core 1.8.6, `-Wall -Wextra`).
> `LCD_Keypad_Test` has been run on the shield and works. With a real PC-1500 the speed test
> (menu 6) works: **9.397 kHz, 1011 bytes/s**, and the echo test (menu 5) ran 1000 transfers
> **without errors** (both with I2C v1.0). The timing figures below come from the LH5801
> simulator, increased by the 5 % by which the real PC-1500 was slower. The tester works
> unchanged with the faster versions: with **I2C v1.2 a real PC-1500 measured 16.624 kHz and
> 1789 B/s**, and with **I2C v1.3 17.505 kHz and 1884 B/s**, both with an error-free echo
> test.

| File | Contents |
|---|---|
| `arduino/LCD_Keypad_Test/LCD_Keypad_Test.ino` | stand-alone test of the shield: buttons, LCD, character set, backlight, RST |
| `arduino/PC1500_I2C_Slave/PC1500_I2C_Slave.ino` | I2C device at &42 for the PC-1500 (comments in English) |
| `Basic/pc1500_i2c_arduino_test-v1.0.txt` | PC-1500 test program with a menu (prompts in English; the Polish version is in `Archive/`) |
| [`HELP_arduino_test.md`](../HELP_arduino_test.md) | help for the test program with I2C v1.3: wiring, start, menu, error messages, protocol |

The UNO is a good tester because everything runs at 5 V (no level converters, and the short
5 V pulses that PB2 may produce, see the README, do no harm), it has enough RAM (the slave sketch
uses 8.9 KB of flash and 722 B of RAM) and its hardware I2C (TWI) has a hardware address match.

## 1. Test the shield first

Upload `LCD_Keypad_Test.ino`. The shield uses `LiquidCrystal lcd(8, 9, 4, 5, 6, 7)` (RS, E,
D4–D7), the five buttons RIGHT, UP, DOWN, LEFT and SELECT on a resistor ladder at A0, and the
backlight on D10. Menu (UP/DOWN to select, SELECT or RIGHT to start, LEFT to go back):

| Item | Test |
|---|---|
| 1 Buttons | shows the button and the raw ADC value; hold LEFT for 2 s to leave |
| 2 LCD test | all pixels on and off, checkerboard and its negative, frames of all 32 fields, field addressing, the ROM characters, cursor and blink, scrolling, display off/on |
| 3 LCD charset | browser of the character set, 16 codes at a time (UP/DOWN) |
| 4 Backlight | backlight on/off (D10 is never driven high, some clones have no base resistor) |
| 5 Info / RST | restart counter: RST resets the UNO and cannot be read, so the counter in `.noinit` RAM shows that RST works |

If the screen is blank or shows only boxes in the top row, adjust the contrast with the blue
potentiometer. The ADC thresholds (60, 200, 400, 600, 850) fit most clones; the serial monitor
(115200 bps) shows the raw values if yours differ.

## 2. Wiring

The shield covers the UNO, but A0–A5 and D0–D3 are available on its top headers.

| PC-1500 (60-pin) | In between | UNO / shield |
|---|---|---|
| PC7, pin 10 | Schottky diode, cathode (stripe) toward PC7 | **A5** (SCL) |
| PB2, pin 27 | 470 Ω resistor | **A4** (SDA) |
| GND, pins 52–55 | – | GND |

Optional: 4.7 kΩ from A4 to 5 V and from A5 to 5 V.

- **5 V and GND:** if the shield has V/G rows next to A1–A5, use them; otherwise the ICSP header
  of the UNO (pin 2 = 5 V, pin 6 = GND).
- **The diode is needed even at 5 V.** The hardware I2C of the UNO holds SCL low after every
  byte until its interrupt has run. Without the diode PC7 would drive SCL high at the same time
  (tens of mA through two outputs). With the diode PC7 can only pull SCL low, like a real
  open-drain I2C output: current flows only from the pull-up into PC7 (about 1 mA, (5 V −
  0.3 V) / 4.7 kΩ), and when PC7 is high the diode simply blocks. A Schottky diode gives a
  low level of about 0.3 V; a 1N4148 (0.7 V) would still work at 5 V but not with 3.3 V devices.
- **No diode on SDA:** PB2 must also read SDA (ACK bits and data from the Arduino). The routine
  emulates an open-drain output with the direction register; the 470 Ω only limits the current
  if both sides ever drive the line at the same time.
- **Pull-ups:** `Wire.begin()` already switches on the internal pull-ups of the ATmega on A4 and
  A5 (20–50 kΩ). With short wires (up to 20–30 cm) this is enough at our 9–10 kHz: the rise time
  constant is about 1–7 µs, while the master leaves at least 13 µs between releasing SDA and
  raising SCL and at least 22 µs between the falling SCL edge and reading SDA. Add the external
  4.7 kΩ resistors if the ECHO test shows errors, with longer wires, with other modules on the
  bus (most modules already have 4.7 kΩ on board) or with a diode of high capacitance (1N5817:
  about 100 pF; BAT85 or BAT43: about 10 pF).
- The internal pull-ups only work after `Wire.begin()`. For 1–2 s after a reset (bootloader)
  the lines float; a transaction in this time returns E = 1 or E = 3. Just repeat it.
- **Remove the PC-1500 from the CE-150**, which drives PB2.
- Leave **D0 and D1** free (USB, serial monitor). **D2 and D3** can be set and read by the
  PC-1500, **A1–A3** measured with the ADC (A0 is the button ladder).

## 3. Start

1. Upload `PC1500_I2C_Slave.ino`. The LCD shows "I2C slave 0x42".
2. On the PC-1500 install I2C v1.3 (README: `NEW &4500` in PRO mode, type in
   `Basic/pc1500_i2c_installer-v1.3.txt`, `RUN`).
3. The scanner of the installer (`RUN 500`) must find a device at **&42**.
4. Enter `NEW` in PRO mode to remove the installer; the I2C code stays in memory.
5. Type in `Basic/pc1500_i2c_arduino_test-v1.0.txt` and `RUN` it. At the start it checks that
   the I2C code is in memory (first bytes &FD, &BE) and that the Arduino answers.

## 4. Menu of the PC-1500 test program

| No. | Test | What it does |
|---|---|---|
| 1 | ID | reads "PC1500 I2C v1.0": the simplest write and read test |
| 2 | TEXT | sends a text to row 0 or 1 of the Arduino LCD |
| 3 | ADC | continuous measurement of A0–A3: value and voltage; any key of the PC-1500 stops it. Channel 0 is the button ladder, so pressing the shield buttons changes the value |
| 4 | KEYS | the shield buttons on the PC-1500: the button pressed now and the last one pressed |
| 5 | ECHO | N transfers of 1–29 random bytes; the Arduino sends them back, the PC-1500 compares them and counts the errors |
| 6 | SPEED | speed measurement: command `F`, then a write of 31 bytes; the Arduino measures the time from the first SCL edge to the STOP and reports the SCL frequency and the bytes per second |
| 7 | LIGHT | LCD backlight on/off |
| 8 | CLS | clears the LCD |

On the Arduino, **SELECT** switches the LCD between the text from the PC-1500 and the I2C
statistics (writes, reads, errors, last command). The serial monitor (115200 bps) shows every
command.

## 5. Protocol

Every command is **one write** (`POKE IA, &42, W, 0 : CALL IC, E`), the answer is read with a
**separate read** of 32 bytes (`POKE IA, &42, 0, 32 : CALL IC, E`).

Answer: `[status][command number][N][N data bytes]`, N ≤ 29, so the data start at RX+3.

| Status | Meaning |
|---|---|
| 0 | the Arduino is still working on the command: read again (the test program tries up to 50 times) |
| 1 | OK |
| 2 | unknown command |
| 3 | bad parameter |

| Command (byte 0) | Parameters | Answer data |
|---|---|---|
| `P` identification | – | "PC1500 I2C v1.0" |
| `T` text on the LCD | row 0/1, up to 16 characters | – |
| `C` clear the LCD | – | – |
| `A` ADC | channel 0–3 (A0–A3) | value 0–1023 (2 B), voltage in mV (2 B) |
| `K` shield buttons | – | button now, last button pressed (then cleared), counter; 0 none, 1 RIGHT, 2 UP, 3 DOWN, 4 LEFT, 5 SELECT |
| `E` echo | up to 29 bytes | the same bytes |
| `F` arm the speed measurement | – | – (do not read the answer) |
| `X` test data for the measurement | any | – |
| `S` result of the measurement | – | valid (1 B), bytes incl. address (1 B), time in µs (4 B), SCL in Hz (2 B), bytes/s (2 B) |
| `L` backlight | 1 / 0 | – |
| `D` read D2, D3 | – | D2, D3 (0/1) |
| `O` set D2/D3 | pin 2/3, state 0/1 (output) or 2 (input with pull-up) | – |
| `U` time since the Arduino started | – | ms (4 B) |

Numbers of more than one byte: low byte first. The speed measurement: `F` arms a pin-change
interrupt on A5; the first SCL edge of the next transaction starts the time, the STOP of the
`X` write (in `onReceive`) ends it. SCL frequency = 9 clocks per byte (8 bits + ACK) × bytes
including the address / time. For this transfer ('X' + bytes 1…30) the simulator gives
9.871 kHz and 1062 B/s; a real PC-1500 measured **9.397 kHz and 1011 B/s**, about 5 % slower.

## 6. Why the protocol looks like this

The master cannot see when the UNO holds SCL low (no clock stretching, PC7 is an output only).
So the UNO must finish its work for each byte before the PC-1500 raises SCL again. The
simulator (+5 % as measured) gives the time the master keeps SCL low after the ACK clock:

| Situation | v1.0 | v1.1 |
|---|---|---|
| write, after each byte | about 155 µs | about 108 µs |
| read, after the address (before the first data byte) | about 73 µs | about 73 µs |
| read, after each data byte | about 55 µs | about 51 µs |

The `onRequest` handler of the Wire library runs at the address of a read and needs about
25–30 µs on the UNO, so 73 µs is a comfortable margin. (An earlier version of this page gave
44 µs for the read address after a repeated START; that figure came from a measuring script
that miscounted the clocks after the repeated START. The window is 73 µs there too.)
Therefore:

- **Command and answer are always two separate `CALL`s.** Do not combine W > 0 and R > 0 for
  the Arduino (it works for real I2C chips, which answer in hardware). At a repeated START the
  Wire library calls `onReceive` for the bytes just written, inside the interrupt, and the AVR
  may hold SCL low meanwhile; our master cannot see that, so the read address that follows
  could be lost. With two `CALL`s, `onReceive` runs after the STOP, while BASIC is busy.
- `onReceive` (runs at the STOP of a write) only copies the command; `loop()` executes it and
  prepares the answer, which `onRequest` sends with a single `Wire.write()`. No `Serial.print`
  in the handlers. The status byte 0 tells the PC-1500 that the answer is not ready yet.
- **At most 32 bytes per transaction** (buffer of the Wire library). A longer write ends with
  E = 2. For longer transfers the buffers in the Wire library would have to be enlarged (to
  128 B: about 650 B of RAM for the buffers; to 255 B: about 1.3 KB, tight).

The same rules apply to an ESP32 or any other microcontroller as I2C device; see
[design_notes.md](design_notes.md).
