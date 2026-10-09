# I2C interface for Sharp PC-1500(A)

**I2C master v1.0** by Alex F4VTS

A machine-language routine that turns the PC-1500(A) into an **I2C bus master** without a
dedicated interface and without any modification of the computer: **SCL on PC7** (pin 10) and
**SDA on PB2** (pin 27), or on **PB0** (pin 9) with the CE-150 modification. These are the same
two pins as the [Software UART for Sharp PC-1500(A)](https://github.com/Ante1000/Sharp_PC-1500_Software_UART)
(SERIN/SEROUT). One diode and a few resistors go between the connector and the I2C bus.

> **Status:** I2C v1.0 has been tested in an LH5801 simulator (instruction timings from MAME,
> two models of the LH5811 port latch, simulated I2C devices). **First test on a real
> PC-1500** with the Arduino UNO tester of this repository: the speed test works and measured
> an SCL clock of 9.397 kHz and 1011 bytes/s; for the same transfer the simulator gives
> 9.871 kHz, so the real PC-1500 is about 5 % slower than the simulator. Further tests
> (DS3231, PCF8574, EEPROM) are still to be done. Please check the wiring carefully and report
> your results.

| Document | Contents |
|---|---|
| this file | features, connection, installation, use in BASIC, examples |
| [docs/arduino_tester.md](docs/arduino_tester.md) | test set: Arduino UNO + LCD Keypad Shield as an I2C device, with a test program for the PC-1500 |
| [HELP_arduino_test_PL.md](HELP_arduino_test_PL.md) | help for the test program `pc1500_i2c_arduino_test-v1.0`, in Polish: wiring, start, menu, error messages, protocol |
| [docs/design_notes.md](docs/design_notes.md) | ideas for the next steps: ESP32 as Wi-Fi bridge and program "disk", I2C compared with SPI 3-wire and other 2-wire protocols, speed measurements in the simulator |
| [Software UART: Modification_for_use_with_CE-150_EN.pdf](https://github.com/Ante1000/Sharp_PC-1500_Software_UART/blob/main/Modification_for_use_with_CE-150_EN.pdf) | optional modification for use with the CE-150 (needed only for SDA on PB0) |
| [Software UART: HELP_NEW_memory.md](https://github.com/Ante1000/Sharp_PC-1500_Software_UART/blob/main/HELP_NEW_memory.md) | RAM start for every memory module |

## Repository contents

| File | Contents |
|---|---|
| `Basic/pc1500_i2c_installer-v1.0.txt` | BASIC installer of the I2C master, with a bus scanner and a read/write test |
| `Basic/pc1500_i2c_arduino_test-v1.0.txt` | PC-1500 test program for the Arduino UNO tester (menu, prompts in Polish) |
| `asm/` | LH5801 assembler source and listing of I2C v1.0 |
| `arduino/PC1500_I2C_Slave/` | Arduino UNO sketch: I2C device at &42 with LCD, buttons, ADC and speed measurement |
| `arduino/LCD_Keypad_Test/` | Arduino UNO sketch that tests the LCD Keypad Shield by itself |
| `docs/` | Arduino tester description and design notes |

## Features

- One `CALL` performs a complete I2C transaction: it **writes W bytes**, then (after a
  repeated START) **reads R bytes**. This covers register reads and writes of almost all
  sensors, clocks (RTC), EEPROMs, port expanders and displays.
- 7-bit addresses, up to 255 bytes in each direction per call, ACK/NACK handled by the
  routine, result code in a BASIC variable.
- Bus recovery: if a device holds SDA low (for example after a transfer was interrupted), up
  to 10 SCL pulses free the bus before the transaction starts.
- SCL clock about 10 kHz (measured on a PC-1500: 9.4 kHz on average over a 32-byte write;
  single clock periods vary between about 60 and 200 µs). I2C has no minimum clock frequency.
- 241 bytes of code. It uses the same TX/RX buffers as the Software UART, so both can be
  installed together but can not operate simultaneously. The UART is not needed for I2C, they are both independent routines.

## Limitations

- Port C of the LH5811 has outputs only, so the PC-1500 cannot read SCL back: **no clock
  stretching** and a single master only. Most RTCs, EEPROMs, port expanders, displays and
  sensors do not stretch the clock. Devices that do (some sensors in "hold master" mode,
  microcontroller-based slaves) may not work; a microcontroller slave must have its answer
  ready in time (see [docs/arduino_tester.md](docs/arduino_tester.md)).
- 10-bit addresses are not supported.
- Interrupts are disabled during the `CALL`, as with SERIN/SEROUT.

## Connection

| Signal | Pin | I2C line |
|---|---|---|
| PC7 | 10 | SCL, through a Schottky diode |
| PB2 (CMT IN) | 27 | SDA, through 470 Ω (default) |
| PB0 | 9 | SDA, alternative (needs the CE-150 modification, see below) |
| GND | 52–55 | GND of the I2C device |

The pins are on the 60-pin connector on the left side of the PC-1500(A); see the
[connector drawing](https://github.com/Ante1000/Sharp_PC-1500_Software_UART/blob/main/img/pc1500_60pin_connector.png)
in the Software UART repository.

```
PC-1500 (60-pin)                                  I2C device / module
            Schottky diode or 1N4149
PC7  pin 10  ────────|<|──────────┬───────────────  SCL
            (cathode/stripe       |
             toward PC7)         4.7k (optional)
                                  |
                                 VCC
PB2  pin 27  ─────────[ 470 ohm]──┬───────────────  SDA
                                  |
                                 4.7k (optional)
                                  |
                                 VCC
GND  pin 55  ─────────────────────────────────────  GND
```

- **VCC** is the supply of the I2C device, 3.3 V or 5 V. The device has its own supply.
-- **The diode on SCL** (BAT43, BAT85, 1N5817 or any other Schottky diode, 1N4148 also works) is
  needed because PC7 can only drive a hard high or low level: with the diode the PC-1500 can
  only pull SCL low and the pull-up resistor makes the high level, as with a real I2C
  (open-drain) output. Current therefore only flows from the pull-up into PC7 (about 1 mA),
  never out of PC7. A 3.3 V device never sees the 5 V of the PC-1500, and a device that holds
  SCL low (clock stretching) never fights against PC7.
- **The 470 Ω resistor on SDA** limits the current if PB2 and a device ever drive SDA at the
  same time. Do not use more than 1 kΩ here, or the low level becomes too high for the device.
- The PC-1500 reads about 2.4 V or more as a high level, so 3.3 V pull-ups are fine.
- PB2 is the cassette input of the CE-150, which drives this line.
- **Pull-up resistors**  4.7–10 kΩ on SCL and SDA. They are not needed if the IC device has them. Many modules already have them on board; then do not add more. If you connect to a microcontroller you can enable internal pull-ups by software if not enabled (e.g. wire.h library does that by default).
- **Remove the PC-1500(A) from the CE-150 while I2C is in use** (as with the UART).
- **SDA on PB0** (with the [CE-150 modification](https://github.com/Ante1000/Sharp_PC-1500_Software_UART/blob/main/Modification_for_use_with_CE-150_EN.pdf)):
  answer `0` to the SDA question of the installer. The modification puts resistors in series
  with PB0 (1 kΩ in the PC-1500 and 1–10 kΩ in the CE-150) and with PC7 (1–5 kΩ). For I2C
  the series resistance must stay low (about 2 kΩ in total on each line), the pull-ups
  should be weak (10–22 kΩ) and the optional control LED must be disconnected, otherwise
  the low level is too high. The Schottky diode goes between the PC7 wire and SCL as above.
  

## Memory

The code and its parameters take the page **RAM start + &400** (A0+&400 … A0+&4FF), so BASIC
must start at **A0+&500**. Find the RAM start with `PRINT PEEK &7863` in RUN mode; `NEW` is
this value × 256 + &500. Before loading the installer, enter in PRO mode:

| `PEEK &7863` | RAM start (A0) | Configuration | Enter in PRO mode |
|---|---|---|---|
| 64 (&40) | &4000 | PC-1500A, PC-1500 without a module | `NEW &4500` |
| 56 (&38) | &3800 | with CE-151, CE-155, CE-157, CE-159 | `NEW &3D00` |
| 32 (&20) | &2000 | CE-159 according to one source | `NEW &2500` |
| 0 (&00) | &0000 | with CE-161, CE-163 | `NEW &500` |

| Address | Contents |
|---|---|
| A0+&0C5 … A0+&1FF | SEROUT, SERIN and RC of the Software UART v6.x (if installed) |
| A0+&200 … A0+&2FE | RX buffer: bytes read from the I2C device |
| A0+&300 … A0+&3FE | TX buffer: bytes to write to the I2C device |
| A0+&400 … A0+&4F0 | I2C code (IC, the `CALL` address) |
| A0+&4FC | IA: device address D (7 bits, 0–127) |
| A0+&4FD | W: number of bytes to write from TX (0–255) |
| A0+&4FE | R: number of bytes to read into RX (0–255) |
| A0+&4FF | IS: result of the last call |
| from A0+&500 | BASIC program |

The Software UART and the I2C master can both stay in memory: install them one after the
other, both after the same `NEW` (for example `NEW &4500`). They share the pins, so connect
either a UART device or an I2C bus at a time.

## Installation

1. Select PRO mode and enter `NEW0` [ENTER], then the `NEW` value from the table above
   (`NEW &4500` on a PC-1500A).
2. Type in `Basic/pc1500_i2c_installer-v1.0.txt` (there is no tape recording of it yet) and
   `RUN` it in RUN mode.
3. The installer asks for the SDA port: ENTER = PB2 (pin 27), `0` = PB0 (pin 9). It then shows
   the addresses and offers the bus scanner.
4. When you no longer need the installer, enter `NEW` in PRO mode. The code stays in memory.

Test programs of the installer (as long as it is in memory):

| Command | Action |
|---|---|
| `RUN 500` | **scanner**: tries every address &08–&77 and lists the devices that answer |
| `RUN 700` | **test**: asks for an address (e.g. `&68` or `104`), the bytes to write and the number of bytes to read, then shows the bytes read in hex |

On a PC-1500 with only 2 KB of RAM the installer does not fit next to `NEW &4500`. Install it
on a PC-1500A, save the code with `CSAVE M "I2C"; &4400, &44FF` and load it on the PC-1500
with `CLOAD M "I2C"` after `NEW &4500`. With an SDA port other than the one chosen during
installation, run the installer again.

## Use in your programs

Compute the addresses as the installer does, so the program works in every RAM configuration:

```
A0 = PEEK &7863 * 256 : IC = A0 + &400 : IA = A0 + &4FC : IS = A0 + &4FF
TX = A0 + &300 : RX = A0 + &200
```

Put the bytes to write into TX, set the device address and both counts, and call the routine:

```
POKE IA, D, W, R : CALL IC, E
```

- If W > 0 (or W = 0 and R = 0): START, address + write bit, then TX+0 … TX+W-1.
- If R > 0: (repeated) START, address + read bit, then R bytes into RX+0 … RX+R-1; the
  routine acknowledges every byte except the last one.
- At the end: STOP. With W = 0 and R = 0 only the address is sent: a presence test.

The result is in E (and in `PEEK IS`; `CALL IC` without a variable also works):

| Result | Meaning |
|---|---|
| 0 | OK |
| 1 | no ACK after the address: no device at this address (or it is busy, e.g. an EEPROM during a write cycle) |
| 2 | no ACK after a data byte: the device refused the data |
| 3 | SDA stays low: the bus is blocked (wiring, missing pull-up, a device holding SDA); nothing was sent |

To check from a program that the I2C code is in memory: its first two bytes are &FD, &BE
(`IF PEEK (IC) <> &FD OR PEEK (IC + 1) <> &BE PRINT "NO I2C"`).

### Examples

**Is there a device at &68?**
```
POKE IA, &68, 0, 0 : CALL IC, E : IF E = 0 PRINT "FOUND"
```

**DS3231 / DS1307 real-time clock (address &68).** The time registers are BCD, so a BCD value
can be written as a hex number. Set the time to 23:59:30, then read it:
```
10 A0 = PEEK &7863 * 256 : IC = A0 + &400 : IA = A0 + &4FC
20 TX = A0 + &300 : RX = A0 + &200
30 POKE TX, 0, &30, &59, &23 : POKE IA, &68, 4, 0 : CALL IC, E : REM REGISTER 0: S, M, H
40 POKE TX, 0 : POKE IA, &68, 1, 3 : CALL IC, E : REM REGISTER 0, READ 3 BYTES
50 IF E <> 0 PRINT "I2C ERROR "; E : END
60 S = PEEK RX AND &7F : M = PEEK (RX + 1) : H = PEEK (RX + 2) AND &3F
70 S = INT (S / 16) * 10 + (S AND 15) : M = INT (M / 16) * 10 + (M AND 15)
80 H = INT (H / 16) * 10 + (H AND 15) : PRINT H; ":"; M; ":"; S
```

**PCF8574 port expander (address &20–&27; PCF8574A: &38–&3F).** Write the eight pins, then
read them (pins written as 1 work as inputs):
```
POKE TX, &F0 : POKE IA, &20, 1, 0 : CALL IC, E
POKE IA, &20, 0, 1 : CALL IC, E : PRINT PEEK RX
```

**24C32 / 24C64 EEPROM (address &50, two address bytes).** Write "ABC" at address &0010, then
read it back. After a write the EEPROM is busy for a few milliseconds and does not answer
(result 1), so the read is repeated until it answers:
```
10 POKE TX, 0, &10, 65, 66, 67 : POKE IA, &50, 5, 0 : CALL IC, E
20 POKE TX, 0, &10 : POKE IA, &50, 2, 3 : CALL IC, E : IF E = 1 GOTO 20
30 PRINT CHR$ (PEEK RX); CHR$ (PEEK (RX + 1)); CHR$ (PEEK (RX + 2))
```
Small EEPROMs (24C01–24C16) have one address byte: `POKE TX, &10, ...` and W one less.

**A microcontroller as I2C device (Arduino, ESP32).** Send the command and read the answer in
two separate calls, and let the answer start with a status byte, so the PC-1500 can poll until
the answer is ready. See [docs/arduino_tester.md](docs/arduino_tester.md) for the reasons and
a complete example.

### Timing

Times from the simulator, increased by the 5 % measured on a real PC-1500, without the time
BASIC needs for the `CALL` statement itself:

| Transaction | Time |
|---|---|
| presence test (W=0, R=0) | 1.3 ms |
| write one register (W=2) | 3.4 ms |
| read 7 registers (W=1, R=7) | 8.4 ms |
| write 255 bytes | 0.23 s |
| read 255 bytes | 0.18 s |

This is about 1.1 KB/s when writing and 1.4 KB/s when reading. With 32-byte writes (Arduino
tester) a real PC-1500 measured 1011 bytes/s, as the address byte and START/STOP count more.

## How it works

- **SCL:** `ORI`/`ANI` on port C (&F008, bit 7) in the ME1 space, as SEROUT drives TX.
- **SDA:** an open-drain output is emulated with the port B direction register DDB (&F00D).
  SDA = 1: the bit is an input and the pull-up makes the high level. SDA = 0: the bit is an
  output with output latch OPB (&F00F) = 0. The latch is cleared each time right after the
  bit becomes an output, because in MAME's model of the LH5811 a write to an input bit does
  not reach the latch and a read copies the pin into it. If the real LH5811 behaves like this
  model, PB2 drives SDA high for about 18 µs a few times per transaction before it pulls it
  low; in the simulation this always happens while SDA is high anyway (no device pulls it
  low at that moment), and the 470 Ω resistor limits the current into a 3.3 V bus. With a
  separate output latch this happens at most once, on the first call.
- Every change of a line is a separate read-modify-write instruction of at least 17 cycles
  (13 µs at 1.3 MHz), so all minimum I2C standard-mode times (4.7 µs) hold without delay
  loops.
- A real PC-1500 runs the routine about 5 % slower than MAME's cycle table predicts (a
  slightly lower CPU clock or extra cycles for some instructions). I2C only has minimum times,
  so a slower master only adds margin; nothing in the code depends on the exact speed.
- The routine returns with C=1 and the result in X, so BASIC stores it in the variable of
  `CALL IC,E`, as SERIN does with the number of received characters.

Sources: `asm/i2c_v1.0.asm` (LH5801 assembler, comments in Polish) and `asm/i2c_v1.0.lst`
(listing with the bytes of every instruction). Symbols that the installer fills in: IP (page
of the code), TP and RP (pages of the TX and RX buffers), BM and DB (bit mask of SDA in port B).

## Author

Alex F4VTS. Contact details are in the
[Software UART repository](https://github.com/Ante1000/Sharp_PC-1500_Software_UART#author).

The I2C master, the Arduino tester and the design notes were written with help from Claude Code.
