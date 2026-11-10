# I2C interface for Sharp PC-1500(A)

**I2C master v1.0 to v1.4** by Alex F4VTS

A machine-language routine that turns the PC-1500(A) into an **I2C bus master** without a
dedicated interface and without any modification of the computer: **SCL on PC7** (pin 10) and
**SDA on PB2** (pin 27), or on **PB0** (pin 9) with the CE-150 modification. These are the same
two pins as the [Software UART for Sharp PC-1500(A)](https://github.com/Ante1000/Sharp_PC-1500_Software_UART)
(SERIN/SEROUT). One diode and a few resistors go between the connector and the I2C bus.

> **Status:** I2C v1.0 has been first tested in an LH5801 simulator (instruction timings from MAME,
> two models of the LH5811 port latch, simulated I2C devices). **First test on a real
> PC-1500** with the Arduino UNO tester of this repository: the speed test works and measured
> an SCL clock of 9.397 kHz and 1011 bytes/s (the simulator gives 9.871 kHz for the same
> transfer, so the real PC-1500 is about 5 % slower), and the echo test ran 1000 transfers of
> 1–29 random bytes **without a single error**. The version 1.4 works correctly with increased
> frequency of 17.5 kHz on a real PC-1500A.
>
> **I2C v1.2** (see [below](#version-12-faster-writing)) has also been tested on a real
> PC-1500 with the Arduino tester: **16.624 kHz and 1789 bytes/s** (1.8 times v1.0) and the
> echo test without errors. **I2C v1.3** (see [below](#version-13-z-flag-from-the-shifts))
> works on the real PC-1500 too: **17.505 kHz and 1884 bytes/s** (1.86 times v1.0), the echo
> test ran 100 transfers without errors, and its installer found that the CPU sets the Z flag
> on `SHL`/`ROL`, which v1.3 needs. **I2C v1.4** (see [below](#version-14-interrupts-on-again))
> is v1.3 with one fix: it switches the interrupts on again before it returns to BASIC. Its
> timing is that of v1.3, so the v1.3 measurements apply to it (**SCL about 17.5 kHz**). v1.4
> has also been run on a real PC-1500 with the LCD clock demo (`Basic/pc1500_i2c_lcd_clock-v1.0.txt`):
> a 16x2 HD44780 LCD with a PCF8574 I2C backpack works, so a real PCF8574 device has been
> tested. v1.1 has so far run in the simulator only. Please check the wiring carefully and
> report your results.

| Document | Contents |
|---|---|
| [QUICK_START.md](QUICK_START.md) | short step-by-step guide: install the interface, wire it, run the LCD clock demo |
| this file | features, connection, installation, use in BASIC, examples |
| [docs/arduino_tester.md](docs/arduino_tester.md) | test set: Arduino UNO + LCD Keypad Shield as an I2C device, with a test program for the PC-1500 |
| [HELP_arduino_test.md](HELP_arduino_test.md) | help for the test program `pc1500_i2c_arduino_test-v1.0` with I2C v1.4: wiring, start, menu, error messages, protocol |
| [docs/design_notes.md](docs/design_notes.md) | ideas for the next steps: ESP32 as Wi-Fi bridge and program "disk", I2C compared with SPI 3-wire and other 2-wire protocols, speed measurements in the simulator |
| [Software UART: Modification_for_use_with_CE-150_EN.pdf](https://github.com/Ante1000/Sharp_PC-1500_Software_UART/blob/main/Modification_for_use_with_CE-150_EN.pdf) | optional modification for use with the CE-150 (needed only for SDA on PB0) |
| [Software UART: HELP_NEW_memory.md](https://github.com/Ante1000/Sharp_PC-1500_Software_UART/blob/main/HELP_NEW_memory.md) | RAM start for every memory module |

## Repository contents

| File | Contents |
|---|---|
| `Basic/pc1500_i2c_installer-v1.4.txt` | BASIC installer of the I2C master v1.4, with a CPU test, a bus scanner and a read/write test |
| `Basic/pc1500_i2c_arduino_test-v1.0.txt` | PC-1500 test program for the Arduino UNO tester (menu, prompts in English) |
| `Basic/pc1500_i2c_lcd_clock-v1.0.txt` | date and time of the PC-1500 clock on a 16x2 HD44780 LCD with a PCF8574 I2C backpack (address &27 or &3F); tape recording `wav/lcd_clock.wav` (name `LCDCLK`); set the clock first with `TIME = MMDDHH.MMSS` |
| `Basic/pc1500_bmp280-v1.3.txt` | thermometer and barometer with a BMP280 sensor (address &77), forced mode, selectable oversampling and IIR filter; tape recording `wav/bmp280_v1.3.wav` (name `BMP280`); see [the BMP280 example](#example-bmp280-thermometer-and-barometer) |
| `tools/pc1500_bas2wav.py` | converts a BASIC text file into a tape recording for `CLOAD` (and decodes recordings); format notes in the file header. It knows only the keywords of the recorded programs, see the [note](#example-bmp280-thermometer-and-barometer) in the BMP280 example |
| `wav/i2c_v1.4.wav`, `wav/test.wav` | tape recordings of the installer v1.4 (name `I2CV1.4`) and of the test program (name `TEST`) for `CLOAD` |
| `wav/lcd_clock.wav`, `wav/bmp280_v1.3.wav` | tape recordings of the LCD clock (name `LCDCLK`) and of the BMP280 thermometer/barometer (name `BMP280`) |
| `QUICK_START.md`, `HELP_arduino_test.md`, `links.txt` | quick start guide, help for the Arduino test program, links to the videos and to the other repositories |
| `Archive/` | installers of I2C v1.0 to v1.3 (with the recording of v1.3), the stand-alone LH5801 flag test and the Polish version of the test program |
| `asm/` | LH5801 assembler sources and listings of I2C v1.0 to v1.4 |
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
- SCL clock about **17.5 kHz** with v1.3 and v1.4 (measured on a PC-1500: 17.505 kHz on average
  over a 32-byte write; v1.2: 16.6 kHz, v1.0: 9.4 kHz, where single clock periods vary between
  about 60 and 200 µs). I2C has no minimum clock frequency.
- 252 bytes of code in v1.4 (v1.0: 241 bytes, v1.1: 252 bytes, v1.2 and v1.3: 251 bytes). It uses the same TX/RX buffers as the Software UART, so both can be
  installed together but can not operate simultaneously. The UART is not needed for I2C, they are both independent routines.

## Limitations

- Port C of the LH5811 has outputs only, so the PC-1500 cannot read SCL back: **no clock
  stretching** and a single master only. Most RTCs, EEPROMs, port expanders, displays and
  sensors do not stretch the clock. Devices that do (some sensors in "hold master" mode,
  microcontroller-based slaves) may not work; a microcontroller slave must have its answer
  ready in time (see [docs/arduino_tester.md](docs/arduino_tester.md)).
- 10-bit addresses are not supported.
- Interrupts are disabled during the `CALL`, as with SERIN/SEROUT. v1.4 switches them on again
  before it returns; v1.0 to v1.3 leave them off until the ROM switches them on again.

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
- **The diode on SCL** (BAT43, BAT85, 1N5817 or any other Schottky diode, 1N4148 also works) is
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
| A0+&0C5 … A0+&1FF | SEROUT, SERIN and their parameters of the [Software UART](https://github.com/Ante1000/Sharp_PC-1500_Software_UART) UART7.3 (if installed) |
| A0+&200 … A0+&2FE | RX buffer: bytes read from the I2C device |
| A0+&300 … A0+&3FE | TX buffer: bytes to write to the I2C device |
| A0+&400 … A0+&4FB | I2C code (IC, the `CALL` address; v1.4 uses 252 bytes) |
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
2. Load `wav/i2c_v1.4.wav` with `CLOAD` (name on the tape: `I2CV1.4`) or type in
   `Basic/pc1500_i2c_installer-v1.4.txt`, and `RUN` it in RUN mode. It first tests the CPU;
   if it shows "USE I2C V1.2", install `Archive/pc1500_i2c_installer-v1.2.txt` instead. The installers of the older versions are
   in `Archive/`; all versions are used in the same way.
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

A complete example with a PCF8574 as the I2C backpack of a 16x2 HD44780 LCD: the clock
`Basic/pc1500_i2c_lcd_clock-v1.0.txt` (recording `wav/lcd_clock.wav`), see
[QUICK_START.md](QUICK_START.md). It sends a whole line (4 bytes per character) in one `CALL`.

A sensor example, a thermometer and barometer with a BMP280: see
[Example: BMP280 thermometer and barometer](#example-bmp280-thermometer-and-barometer).

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

| Transaction | v1.0 | v1.1 | v1.2 | v1.3, v1.4 |
|---|---|---|---|---|
| presence test (W=0, R=0) | 1.3 ms | 1.0 ms | 1.0 ms | 0.9 ms |
| write one register (W=2) | 3.4 ms | 2.2 ms | 1.9 ms | 1.9 ms |
| read 7 registers (W=1, R=7) | 8.4 ms | 6.6 ms | 6.4 ms | 6.3 ms |
| write 255 bytes | 0.23 s | 0.16 s | 0.14 s | 0.13 s |
| read 255 bytes | 0.18 s | 0.16 s | 0.16 s | 0.16 s |

With v1.0 this is about 1.1 KB/s when writing and 1.4 KB/s when reading; v1.1 reaches about
1.6 KB/s in both directions, v1.2 about 1.8 KB/s when writing. With 32-byte writes (Arduino
tester) a real PC-1500 measured 1011 bytes/s and 9.4 kHz with v1.0, as the address byte and
START/STOP count more; the same test measured **1789 bytes/s and 16.624 kHz with v1.2**
(simulator: 1908 bytes/s, so v1.2 runs about 6.6 % slower than the simulator on the real
PC-1500) and **1884 bytes/s and 17.505 kHz with v1.3** (about 7 % below the simulator). With
the same factor the simulator expects about 1510 bytes/s and 14.0 kHz for v1.1.

### Speed in perspective

With I2C v1.3 (v1.4 has the same timing) a byte takes 9 SCL clocks (8 bits and the ACK), so 17.505 kHz moves 1945 bytes/s
on the wire; the measured 1884 bytes/s include the address byte, START and STOP of a 31-byte
write.

| Way to transfer | Bytes/s | 5697-byte program |
|---|---|---|
| `CLOAD` from cassette (Ben Ryves: about 7 minutes for 5697 bytes) | about 14 | about 7 min |
| Fast Load from cassette, 4000 baud (Ben Ryves) | about 190 | about 30 s |
| Software UART, 9600 bps (8N1: 10 bits per byte) | 960 | 6 s |
| **I2C v1.3 / v1.4**, one 31-byte write (measured with v1.3) | **1884** | **3 s** |

So I2C v1.3 and v1.4 are about 140 times faster than `CLOAD` and about 10 times faster than Fast Load.
In UART terms, 1884 bytes/s × 10 bits = 18 840 bps, about **19 200 baud**: twice the fastest
Software UART setting (9600 bps). Reading is slower (0.62 ms per byte, about 1.6 KB/s, like
16 000 bps).

**Larger files in packets** need pauses between the I2C transactions: the PC-1500 prepares
and checks the data, the device writes to its SD card, and the PC-1500 asks for the status.
Estimates for v1.3 and v1.4 on a real PC-1500 (write of n bytes ≈ 0.45 ms + (n + 1) × 0.514 ms; read of
n bytes ≈ 0.96 ms + n × 0.62 ms; status read ≈ 1.6 ms):

| Driver on the PC-1500 | Packet | Write to the device | Read from the device | 10 KB |
|---|---|---|---|---|
| machine code, device with a buffer of 256 bytes or more (ESP32) | 250 bytes | about 1.75 KB/s | about 1.45 KB/s | 6–7 s |
| machine code, Arduino UNO (32-byte Wire buffer) | 29 bytes | about 1.4 KB/s | about 1.1 KB/s | 7–9 s |
| BASIC, I2C buffer page pointed at the data (no copying) | 255 bytes | about 1.1–1.4 KB/s | about 1.0–1.2 KB/s | 8–11 s |
| BASIC, every byte copied with `POKE`/`PEEK` | 29 bytes | about 60–100 bytes/s | about 60–100 bytes/s | 2–3 min |

The BASIC figures assume 50–100 ms per packet for the BASIC statements and 8–15 ms per byte for
a `POKE`/`PEEK` loop; they are not measured. "Buffer page pointed at the data" means that BASIC
pokes the page of the data into the code (v1.4: TX page at IC+&35, RX page at IC+&52; v1.3:
IC+&35 and IC+&53) and restores it afterwards; a transfer covers bytes 0–254 of that page, so byte 255 has to travel
separately. With a machine-code driver the pauses cost only 5–20 % of the time; with BASIC
copying every byte, BASIC and not the I2C bus sets the speed.

## Example: BMP280 thermometer and barometer

`Basic/pc1500_bmp280-v1.3.txt` (tape recording `wav/bmp280_v1.3.wav`, name `BMP280`) measures the
temperature and the air pressure with a **Bosch BMP280** sensor and shows them in turn on the
display of the PC-1500(A):

```
Temperature = 25.08 C
Pressure = 1006.53 hPa
```

Each text stays for about 2 seconds, then the next measurement starts. Any key stops the program
after the display. The sensor is read in **forced mode** (one measurement on request), and the
temperature and pressure are calculated with the floating-point compensation formulas from the
BMP280 datasheet (appendix 8.1). With the sample values of the datasheet (page 23) the same
calculation in the 10-digit arithmetic of the PC-1500 gives 25.08 °C and 1006.53 hPa.

> **Status:** written from the BMP280 datasheet, tested in a simulation of the calculation and of
> the tape format. Not yet confirmed on a real PC-1500 with v1.3 of the program. Please report
> your results.

### What you need

- the I2C interface v1.4 installed (see [Installation](#installation); `NEW &4500` on a PC-1500A),
  wired as in [Connection](#connection),
- a **BMP280 module** (I2C, not SPI) at address **&77**: the pins CSB and SDO of the sensor go to
  VDDIO (many modules do this already). With SDO connected to GND the address is &76: change
  `D = &77` in line 30 of the program,
- pull-up resistors on SCL and SDA only if the module has none (see [Connection](#connection)).

A bare BMP280 works at 3.3 V (1.71–3.6 V). The diode on SCL keeps 5 V away from it. For SDA see
[How it works](#how-it-works) and [docs/design_notes.md](docs/design_notes.md): if the real
LH5811 behaves like the MAME model, PB2 drives SDA high for about 18 µs a few times per
transaction, and a 3.3 V sensor then gets 2–3 mA through its protection diode (limited by the
470 Ω resistor). A BSS138 level converter on SDA avoids this.

### Use

1. Install the I2C interface as described in [Installation](#installation). The I2C code stays
   in memory after `NEW`.
2. Load the program with `CLOAD` (play `wav/bmp280_v1.3.wav`) or type in
   `Basic/pc1500_bmp280-v1.3.txt`, and `RUN` it in RUN mode.
3. The program shows `Initialising...`: it checks that I2C is installed, reads the chip ID
   (register &D0: &58 for the BMP280; the program also accepts &56, &57 and &60, the BME280),
   makes a soft reset (&E0 ← &B6) and reads the 24 calibration bytes (from &88).
4. It asks three questions. **Answer 1, 2, 4, 8 or 16; ENTER means 1:**

   | Question | Meaning |
   |---|---|
   | `temp oversampling ?` | oversampling of the temperature (×1 … ×16) |
   | `Pres oversampling ?` | oversampling of the pressure (×1 … ×16) |
   | `IR Filtering ?` | coefficient of the IIR filter; 1 = filter off |

   A wrong answer (for example 0 or 3) is asked again. More oversampling and a stronger filter
   mean less noise but a longer measurement (the datasheet gives a maximum of 43.2 ms for
   pressure ×16 with temperature ×2, on top of the time BASIC needs). The datasheet recommends temperature ×1 for pressure ×1…×8 and ×2 for
   pressure ×16 (for example "indoor navigation": pressure ×16, temperature ×2, filter 16;
   "weather monitoring": ×1, ×1, filter off). The IIR filter keeps its memory between the
   forced measurements, so with a strong filter the first readings need several measurements to
   settle.
5. The program writes the filter into the `config` register (&F5), then repeats: it starts a
   measurement by writing `ctrl_meas` (&F4) with mode `01` (forced), waits until the `measuring`
   bit of the `status` register (&F3) is 0, reads the six data bytes &F7…&FC in one burst,
   calculates and displays them.

Codes written to the registers (for reference):

| Answer | 1 | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| `osrs_t`, `osrs_p` (&F4) | 001 | 010 | 011 | 100 | 101 |
| `filter` (&F5, bits 4–2) | 000 (off) | 001 | 010 | 011 | 100 |

Error messages: `I2C NOT INSTALLED` (the I2C code is not in memory), `I2C ERROR E=n` (the
result code of the `CALL`, see the table in [Use in your programs](#use-in-your-programs);
E=1 means that nothing answers at &77), `BAD CHIP ID n` (a different chip) and
`BMP280 TIMEOUT` (the sensor keeps the `measuring` bit set).

### Notes on the program

- The program is about 2.7 KB; it does not fit into a PC-1500 with 2 KB of RAM.
- **No `USING` and no `ABS`:** `tools/pc1500_bas2wav.py` has no tape code for these keywords and
  would write them as plain letters, which gives `ERROR 1`. The program therefore builds the
  text `nn.nn` itself (subroutine at line 850) and takes the absolute value with `IF`. If you
  type the program in by hand you can use `PRINT USING` instead.
- Only the temperature and pressure are used. The BME280 (chip ID &60) has a humidity sensor too,
  which this program does not read.

## Version 1.1: faster

I2C v1.1 is a drop-in replacement for v1.0: the same pins, memory page, parameters (IA, IS),
results, installer questions and test programs, so BASIC programs work unchanged. Its code
fills the page up to the parameters (252 bytes, A0+&400 … A0+&4FB). What makes it faster:

- **Sending a 0 bit is a single instruction.** v1.0 switched SDA to output and then cleared
  the output latch for every 0 bit (with a subroutine call). The latch only has to be cleared
  once: START clears it, and the ACK read after each byte (SDA low) leaves it at 0 in both
  models of the LH5811 latch; after a NACK comes STOP, which clears it itself.
- **SDA is only written when the bit changes.** The send loop has two states, "SDA high" and
  "SDA low"; equal consecutive bits only clock SCL. A byte that ends with a 1 bit does not
  release SDA again before the ACK.
- **Faster receive loop:** while receiving, X points to the port B data register, so reading
  SDA takes 14 cycles instead of 20, and a 0 bit needs no flag instruction (the carry is
  always 0 inside the loop).
- **Shorter paths:** one routine sends the address byte for writing and reading, START
  contains the SDA-low code instead of calling it, and the ACK of the receiver uses absolute
  addresses instead of switching registers.
- **Microcontroller devices keep their time:** after the read address SCL stays low for about
  73 µs, as in v1.0, so an Arduino has time to prepare its answer; after a received byte the
  window is about 51 µs (v1.0: 55 µs).

v1.1 was checked in the same simulator as v1.0: 11264 transactions in all combinations of
latch model, cycle/flag table, SDA port (PB2/PB0), initial port contents, device hold time and
RAM base, plus mutation tests (10 deliberate faults in the code, all detected). Its first test
on hardware is still to do: install v1.1 instead of v1.0 and run the Arduino tester (speed
test about 14 kHz and 1500 B/s expected, echo test without errors).

## Version 1.2: faster writing

v1.2 is again a drop-in replacement (251 bytes, same page, parameters and use). Receiving is
the same as in v1.1; sending is about 1.15 times faster than v1.1:

- **The end-of-byte test only runs after a 1 bit.** The send loop marks the end of a byte with
  a sentinel bit that is shifted out after the 8 data bits. That sentinel is a 1, so after a
  0 bit the test (`BII A,&FF` and a branch, 15–18 cycles) cannot be true and is skipped.
- **No subroutine call per byte.** SEND now sends the address and then all W bytes from the TX
  buffer in one loop; the ACK no longer goes through the carry flag (`SEC`/`REC`), and the
  read address simply ends the loop because UL already equals W.
- **The "SDA low, bit 1" path falls straight into the clock code** instead of jumping to it.
- After START the address byte starts in the "SDA low" state, so SDA is not released and
  pulled low again.

Per data bit the send loop now needs 51–80 cycles (about 66 on average; v1.1: about 77, v1.0:
about 115). The windows for a microcontroller device stay comfortable: about 78 µs after each
written byte, 75 µs after the read address and 51 µs after each received byte. v1.2 passes the
same 11264 simulator transactions, and 13 deliberate faults in its code are all detected.

What is left, and why it was not done:

- **The receive loop is close to its limit:** two port writes for SCL (17 cycles each), one
  read of SDA (14) and the shift. Unrolling it for 2 bits would save about 6 % but needs 13
  bytes, and the page has 1 byte left.
- **Port writes cannot be made cheaper here:** `STA #(Y)` takes 10 cycles instead of 17, but
  only if A already holds the port value, and A holds the data byte.
- **Done in v1.3:** if the real LH5801 sets the Z flag after `SHL`/`ROL`, the two
  `BII A,&FF` tests can go (see below).

## Version 1.3: Z flag from the shifts

v1.3 (251 bytes, same page, parameters and use) is v1.2 with two more changes:

- **The end of a byte is seen directly in the Z flag** set by `SHL`/`ROL` when the sentinel
  leaves A, so the two `BII A,&FF` tests (7 cycles each time a 1 bit is sent) are gone. This
  only works if the CPU sets Z on shifts. MAME's LH5801 does, the LH5801 manual says shifts
  leave Z alone. **The v1.3 installer therefore runs a 47-byte CPU test first** (the same as
  `Archive/pc1500_lh5801_flag_test.txt`) and installs nothing if the result is not 15; then it
  shows "USE I2C V1.2".
- **Byte counters with `CPI UL,n`** (7 cycles) instead of `LDA UL` + `CPA (IP,&FD)` (18
  cycles) when sending and receiving. Every call first writes W and R into the two `CPI`
  instructions (the code changes two of its own bytes, IC+&6B and IC+&EE).

In the simulator v1.3 writes about 7 % faster than v1.2 (255 bytes in 125 ms instead of
134 ms) and reads about 1 % faster. The windows for a microcontroller device: about 69 µs
after each written byte, 74 µs after the read address (two `NOP`s keep it as long as in
v1.2) and 51 µs after each received byte. v1.3 passes all simulator transactions with the
MAME flag behaviour, its installer refuses to install with the manual's behaviour, and 16
deliberate faults in its code are all detected. Extra edge tests (passed by v1.0, v1.2 and
v1.3): device addresses 0 and 127, all 256 byte values written and read, W = R = 255 in one
call, NACK on the first and on the last data byte, absent devices for every W/R combination
and calls with changing W and R (the rewritten `CPI` operands).

**On a real PC-1500** the v1.3 installer accepted the CPU (so the real LH5801 sets Z on
shifts, as in MAME), and the Arduino tester measured **17.505 kHz and 1884 bytes/s**
(v1.2: 16.624 kHz and 1789 bytes/s) with 100 echo transfers without errors. This is about as
fast as I2C gets within one 256-byte page.

## Version 1.4: interrupts on again

v1.4 (252 bytes, same page, parameters and use) is v1.3 with one fix. Like SERIN/SEROUT, the
routine switches the interrupts off with `RIE` at its start, because an interrupt would break
the timing of SCL and SDA. BASIC itself runs with the interrupts on: the PC-1500 ROM switches
them on after reset and after each of its own short `RIE` sections, and waits for a key with
`SIE` + `HLT`. v1.0 to v1.3 returned with the interrupts still off, which did no harm in
practice, because the next ROM routine with `SIE` (e.g. the next display output) switched them
on again and the LH5811 keeps an interrupt request until it is served. v1.4 does it itself:
`SIE` just before the final `RTN`, on every path (OK, NACK and SDA stuck low).

v1.3 had only one free byte in the page, so the two `NOP`s after the read address became one
`DCS (X)`: 1 byte and 13 cycles instead of 2 × 5. It only reads the ROM byte at &F00D into A
and the flags, which are not used at that point. The window for a microcontroller device
after the read address grows from about 74 to about 77 µs; all other times and the speed are
those of v1.3. The two `CPI` operands that every call rewrites are now at IC+&6A and IC+&EF.
In the simulator v1.4 passes all transactions and edge tests of v1.3, plus two new checks on
every call: interrupts off during every port access, and on again after the return (v1.3
fails the second one on every call, as expected).

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

Sources: `asm/i2c_v1.0.asm` to `asm/i2c_v1.4.asm` (LH5801 assembler,
comments in English) and the listings `asm/i2c_v1.x.lst` (with the bytes of every instruction). Symbols that the installer fills in: IP (page
of the code), TP and RP (pages of the TX and RX buffers), BM and DB (bit mask of SDA in port B).

## Author

Alex F4VTS. Contact details are in the
[Software UART repository](https://github.com/Ante1000/Sharp_PC-1500_Software_UART#author).

The I2C master, the Arduino tester and the design notes were written with help from Claude Code.
