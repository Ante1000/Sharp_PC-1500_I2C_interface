# Design notes: next steps for the PC-1500 I2C interface

These notes collect the analyses made while designing I2C v1.0: how an ESP32 could give the
PC-1500 Wi-Fi and a program "disk", and whether another 2-wire protocol would be better than
I2C. **Nothing here is implemented yet**, apart from I2C v1.0 itself and the Arduino tester.
The speeds were measured with test loops in an LH5801 simulator with the instruction timings
of MAME; they do not include the time BASIC needs for the `CALL` and the work on the other side.

## 1. What limits every protocol on the PC-1500

- **PC7 is an output only.** The PC-1500 cannot read this line, so it must always provide the
  clock: protocols in which the other side clocks (PS/2) or that need clock stretching are out.
- **PB2 is the only bidirectional line;** its open-drain output is emulated with the direction
  register DDB. PB0 is a second input/output, but only usable in the CE-150 with the CE-150
  modification.
- **Every change of a line takes at least 13 µs** (a read-modify-write instruction of 17–23
  cycles at 1.3 MHz). This limits any protocol to a few KB/s; the CPU of the PC-1500 is the
  bottleneck, not the protocol.
- **Interrupts are off during the `CALL`.** The timing of the PC-1500 is then deterministic.
- **The other side (Arduino, ESP32) is much faster** and has hardware peripherals, so it should
  take over everything that is time-critical.

## 2. Rules for a microcontroller as I2C device

The master does not see clock stretching. After the ACK clock of a byte it keeps SCL low for
about 148 µs (write), 69 µs (read, after the address), 52 µs (read, after each data byte) or
42 µs (write followed by a repeated START and a read in one `CALL`). From this:

1. **Mailbox protocol:** the command is one write (W = length, R = 0); the answer is read with a
   separate `CALL` (W = 0, R = fixed length). Never combine both in one `CALL`.
2. **The answer starts with a status byte** ("busy", "OK", error). The PC-1500 polls until the
   answer is ready and can do other work in between (e.g. check `INKEY$`).
3. **The answer must already be in the buffer** when the read starts. On an AVR: `onRequest`
   does only a single `Wire.write()` of a prepared buffer. On a classic ESP32: prepare the
   answer in advance with `Wire.slaveWrite()`. On an ESP32-C3: **switch off clock stretching**
   (`stretch_en`), otherwise the slave may stretch and the data are silently wrong; the
   `access_ram_en` mode (the master reads 32 bytes of slave RAM without CPU) might help, still
   to be checked.
4. **No error detection in I2C itself** (ACK does not detect bit errors): add a checksum for
   long transfers. On an 8-bit CPU **Fletcher-16** is cheapest (two additions per byte, a few
   dozen bytes of code); a bitwise CRC-16 would slow the transfer by about 30 %, a table CRC
   needs a 512-byte table.

## 3. ESP32 as Wi-Fi bridge

All "intelligence" goes into the ESP32; the PC-1500 sends short text commands and receives
short answers (the PC-1500 cannot do Wi-Fi, DNS, TLS or redirects, and a string holds 80
characters). Use custom firmware (Arduino), not the AT firmware.

| Function | How (on the ESP32) |
|---|---|
| ThingSpeak | `GET https://api.thingspeak.com/update?api_key=…&field1=…`, up to 8 fields, at most one write per 15 s on a free account |
| Google Sheets | a small Google Apps Script (`doGet` → `appendRow`) published as web app; the ESP32 must follow the 302 redirect |
| Clock (NTP) | `pool.ntp.org` with time zone `CET-1CEST,M3.5.0,M10.5.0/3`; answer `MMDDhh.mmss` for `TIME = VAL …` (the PC-1500 clock has no year) |
| Data from a web page | prefer JSON APIs (e.g. NBP exchange rates, Open-Meteo); the ESP32 extracts the value and sends only the result |
| E-mail to Gmail | easiest via the same Apps Script (`MailApp.sendEmail`); SMTP needs an app password |
| Phone notification | `POST https://ntfy.sh/<topic>` (long random topic name) |
| Another PC-1500 | via an ntfy topic or MQTT; without internet: ESP-NOW between two ESP32 |

Command set (one ASCII line, answer starting with `OK` or `ER`): `TS` ThingSpeak, `GS` sheet
row, `TM` time, `GW n` web value, `ML` e-mail, `NT` notification, `PS`/`PR` send to / receive
from the other PC-1500, `ST` Wi-Fi status. API keys, the Wi-Fi password and URLs stay in the
ESP32 (e.g. set once with WiFiManager). The PC-1500 always asks first: messages from outside
wait in a queue in the ESP32.

### UART or I2C for the bridge

| | Software UART (SERIN/SEROUT v6.2) | I2C v1.0 |
|---|---|---|
| tested on hardware | yes, also with an ESP32 | not yet |
| who controls the transfer | both sides send; the PC-1500 must be in `CALL SI` | always the PC-1500 |
| lost data | possible if the ESP32 sends at the wrong moment | no, the PC-1500 gives the clock |
| waiting for the internet | SERIN blocks (up to 30 s) | polling of the status, other work in between |
| acknowledge | none | ACK per byte, result code 0–3 |
| speed | 4800 bps: 255 B in about 0.53 s | 255 B in about 0.18 s |
| other devices on the wires | no | yes (RTC, sensors, EEPROM, displays) |
| ESP32 firmware | simple (`Serial2`) | harder (slave mode, rules above) |

Speed hardly matters here, an HTTPS request takes 0.5–3 s anyway.

Mailbox block for the ESP32 (W = 0, R = 82): byte 0 status (0 no data, 1 working, 2 ready),
byte 1 length N, bytes 2–81 the text. Example in BASIC (not tested):

```
10 CLEAR : DIM C$(0)*80, R$(0)*80
20 A0 = PEEK &7863 * 256 : IC = A0 + &400 : IA = A0 + &4FC : TX = A0 + &300 : RX = A0 + &200
30 D = &42 : REM ESP32 ADDRESS
40 C$(0) = "TM" : GOSUB 900 : IF E <> 0 PRINT "I2C ERROR "; E : END
50 TIME = VAL (R$(0)) : PRINT "CLOCK SET" : END
900 REM COMMAND C$(0) -> ANSWER R$(0), E = I2C RESULT
910 L = LEN (C$(0)) : FOR I = 1 TO L : POKE TX + I - 1, ASC (MID$ (C$(0), I, 1)) : NEXT I
920 POKE IA, D, L, 0 : CALL IC, E : IF E <> 0 RETURN
930 POKE IA, D, 0, 82 : CALL IC, E : IF E <> 0 RETURN
940 IF PEEK RX = 1 GOTO 930 : REM ESP32 STILL WORKING
950 N = PEEK (RX + 1) : R$(0) = ""
960 FOR I = 1 TO N : R$(0) = R$(0) + CHR$ (PEEK (RX + 1 + I)) : NEXT I : RETURN
```

**Voltage levels with a 3.3 V ESP32:** in principle the diode on SCL, 470 Ω on SDA and pull-ups
to 3.3 V are enough. But if the real LH5811 behaves like the MAME model (see "How it works" in
the README), PB2 drives SDA to 5 V for about 18 µs a few times per transaction, and 2–3 mA
would flow into the protection diode of the ESP32. A cheap bidirectional **BSS138 level
converter** (HV side 5 V with the PC-1500, LV side 3.3 V with the ESP32) avoids this.
**Power:** the ESP32 needs its own 3.3 V supply (500 mA peaks); common ground.

Suggested order: one ESP32 firmware with the same commands over UART or I2C (the BASIC programs
differ only in the subroutine at line 900); UART first, since it already works with the ESP32;
in parallel test I2C v1.0 on hardware (DS3231, PCF8574 or the Arduino tester); then move to I2C.

## 4. ESP32 with SD card as program "disk"

**SAVE:** a machine-code routine reads the start of the BASIC program (`&7865/&7866`, already
used by the installers) and its end (probably `&7867/&7868`, to be confirmed), sends a header
(name, length, type) and then the program in blocks of about 250 bytes with block number and
checksum; the ESP32 stores e.g. `GAME1.BAS`.

**LOAD:** the PC-1500 asks for the file, gets the length, checks the free memory, fetches the
blocks directly into the program area (re-requesting a block on a checksum error), then sets
the end pointer and clears the variables as `CLOAD` does. Like `CLOAD`, it is called from the
keyboard, not from a running program. A BASIC program in memory contains no absolute addresses,
so a program saved on a PC-1500A also loads on a PC-1500 with a CE-161. Machine-code blocks are
loaded to their fixed address.

Estimated time with I2C v1.0: 2 KB about 2 s, 8 KB about 7–8 s, 16 KB (CE-161) about 14–15 s.

**Commands:** first `CALL` with the name in a fixed string variable or in the TX buffer (our
routines pass only numeric variables; whether `CALL` can pass a string is not known). Later real
commands such as `SDSAVE "NAME"`, `SDLOAD`, `SDDIR`, `SDKILL` through the BASIC extension table
in RAM, as LeoBAS does (header &55, `POKE &79D1,&20`); note that this table at the RAM start
conflicts with the RESERVE area, and the ROM routines for string arguments must still be found.

**More from the ESP32:** a file manager in the web browser, conversion between tokenized
programs and plain text (edit on a PC), possibly compatibility with the files of PocketTools
(bin2wav/wav2bin) and existing program archives, backup to the cloud.

**Risks:** I2C v1.0 not yet tested on hardware; every block must be ready in the ESP32 before
the PC-1500 reads it (commands "request block n", "status", "read block"); the driver adds about
0.5 KB of code after the UART and I2C pages (`NEW` moves up again).

## 5. Survey of 2-wire protocols

| Protocol | Fits the PC-1500? | ESP32 support | Estimated speed | Verdict |
|---|---|---|---|---|
| UART (Software UART) | yes, works | hardware | 9600 bps: about 0.95 KB/s | keep for PCs and other pocket computers |
| I2C (v1.0) | yes, without clock stretching | hardware slave, answer prepared in advance | 1.1–1.4 KB/s | standard, many devices on one bus |
| SMBus (I2C variant) | yes, 11 kHz is inside the SMBus range 10–100 kHz | as I2C | as I2C | borrow the PEC byte (CRC-8) and block transfers |
| SPI 3-wire (SIO): clock, shared data line, CS | only with CS emulated by the ESP32 | hardware on ESP32-S2/S3/C3/C6, **not on the classic ESP32** | about 2 KB/s | interesting on an ESP32-S3 |
| SWD (ARM debug): host clock, bidirectional data | ideal shape: host clock, OK/WAIT/FAULT, parity | **no SWD in any ESP32** (ESP32 uses JTAG) | about 1.6 KB/s | best pattern for an own protocol |
| SSI (encoders) | read only | software | fast read | one direction only |
| "Fast loader" (fixed-rhythm packets, as on the C64) | yes, thanks to the deterministic timing | RMT peripheral | read about 3–4 KB/s | fastest, most timing-sensitive |
| 1-Wire | no: sampling within 15 µs is too fast | emulation (OneWireHub), not confirmed on ESP32 | below 1 KB/s | frees PC7, but slower |
| PS/2 | no: the device gives the clock | – | – | out |
| one-wire UART (UPDI, LIN) | as UART, half duplex | hardware | as UART | no gain |
| CAN, RS-485, DALI | need transceivers or too slow | – | – | out |

### Simulator benchmarks (bit loops in LH5801 assembler)

| Variant | PC-1500 lines | Write | Read | 16 KB |
|---|---|---|---|---|
| I2C v1.0 (loops) | PC7, PB2 | 1.13 KB/s | 1.38 KB/s | 12–14 s |
| I2C with unrolled loops (estimate, not measured) | PC7, PB2 | about 1.5–1.8 KB/s | about 1.7–1.8 KB/s | about 9–11 s |
| SPI, classic (SCK = PC7, MOSI = PB0, MISO = PB2), loop | PC7, PB0, PB2 | 1.62 KB/s | 1.76 KB/s | about 10 s |
| SPI, classic, unrolled | PC7, PB0, PB2 | 2.09 KB/s | 2.18 KB/s | about 7.5 s |
| SPI 3-wire, open-drain write, unrolled | PC7, PB2 | 1.61–2.24 KB/s (mean 1.86, depends on the data) | 2.18 KB/s | 7.5–10 s |
| SWD to an ARM target (estimate) | PC7, PB2 | about 1.6 KB/s | about 1.6 KB/s | about 10 s |
| own protocol, 2 bits per clock (both edges), loop | PC7, PB2 | 2.20 KB/s | 2.35 KB/s | about 7 s |
| own protocol, 2 bits per clock, unrolled | PC7, PB2 | **2.70 KB/s** | **2.85 KB/s** | **5.6–5.9 s** |

Part of the SPI gain comes from unrolling the loops, which would also help I2C; against unrolled
loops SPI is only about 1.2 times faster than I2C.

### SPI, SPI 3-wire, SWD in short

- **Classic SPI** needs a third line (PB0, so the CE-150 modification), two 5 V outputs need
  dividers or a converter, there are no acknowledges and no busy signal, and CS must be
  generated by the ESP32 itself (a jumper from one of its GPIOs to its CS input, frame detected
  by a pause of the clock). Every ESP32 has a hardware SPI slave with DMA. Not recommended
  because of PB0.
- **SPI 3-wire** uses exactly the I2C wiring (PC7 through the diode, PB2 through 470 Ω). The
  ESP32-S2/S3/C3/C6 support it in hardware with the *SPI Slave Half Duplex* driver
  (`SPI_SLAVE_HD_3WIRE_MODE`); the transaction format is fixed by Espressif (8-bit command,
  address and dummy cycles for turning the line around, then data from shared registers or by
  DMA). Problems: CS must be emulated, few examples, and if both sides get out of step the
  ESP32 drives 3.3 V against PB2 (about 7 mA through 470 Ω: no damage, but wrong data). In SPI
  mode 0 the data change only while the clock is low, so I2C chips on the same wires would see
  no START/STOP; this needs careful testing.
- **SWD** is only a pattern: its OK/WAIT/FAULT answer replaces clock stretching. As a real
  interface (e.g. to a Raspberry Pi Pico W, whose RAM the PC-1500 could read and write directly)
  it would need more than 500 bytes of code on the PC-1500 and gives no speed gain.

### Own protocol on the I2C wiring

START and STOP as in I2C, a header with command and length (no address byte, no ACK per byte),
an answer code OK / WAIT / FAULT as in SWD (WAIT replaces clock stretching, FAULT means a
checksum error and the block is sent again), up to 256 data bytes with a checksum. When the
ESP32 is alone on the wires, data can be sampled on both clock edges (2 port operations per bit
instead of 3–4), which gives the 2.7–2.85 KB/s above. The ESP32 handles it in software, with
about 45 µs per bit, in an interrupt pinned to core 1 (dual-core ESP32 or ESP32-S3). Sampling on
both edges breaks the I2C rules (data change while the clock is high, which other chips read as
START or STOP), so this mode is only for a bus with the ESP32 alone.

## 6. ESP32-C3: I2C or SPI 3-wire?

The ESP32-C3 has one RISC-V core shared with Wi-Fi (interrupts can be delayed), one I2C
controller, one general SPI (SPI2) with half-duplex 3-wire mode, few GPIOs, and the strapping
pins 2, 8 and 9 should be avoided.

| | I2C | SPI 3-wire (HD mode) |
|---|---|---|
| line driver on the PC-1500 | **exists**: 241 B, simulator-tested | new: about 200–250 B with loops, about 450 B unrolled |
| transport (command, status, block, checksum, retry) | about 120–200 B (BASIC is enough for the internet functions) | about 100–150 B (status from the shared registers) |
| "disk" (SAVE, LOAD, DIR) | about 300–400 B | about 300–400 B |
| frame start | ✅ START/STOP detected by the C3 hardware | ⚠️ no CS line: an interrupt must detect START and set CS through a jumper; if it is late the bits shift |
| noise, slow clock edges | ✅ digital glitch filter on SCL and SDA | ⚠️ no filter; a slow rising edge through diode and pull-up may count twice: better a divider from PC7 and short wires |
| collision on the data line | ✅ impossible, both sides open drain | ⚠️ possible when out of step (about 7 mA, wrong data) |
| ESP32 not ready | ⚠️ `stretch_en` off, answer prepared in advance | ✅ status registers readable by hardware any time, blocks by DMA |
| Wi-Fi load on the single core | ✅ the 32-byte FIFO needs refilling only every ~20 ms | ✅ DMA; ⚠️ CS emulation depends on the interrupt latency |
| ESP32 off or hung | ✅ no ACK, E = 1 at once | ⚠️ the PC-1500 reads &FF or garbage: a fixed "magic" byte in the status is needed |
| other chips on the wires | ✅ | ❌ |
| software maturity | standard driver; ESP-IDF 5.x moves from slave driver v1 to v2 (v1 removed in IDF 6.0); Arduino `Wire` works | ESP-IDF only, few examples |
| speed | 1.1–1.4 KB/s | 1.6–2.2 KB/s |

**Verdict:** build everything, including the "disk", on I2C: the low-level code exists, the
hardware handles the framing, open drain excludes collisions, a missing ESP32 is detected at
once, and other chips can share the bus. The price is about 1.5 times slower transfers, which
only matters for loading large programs. If SPI 3-wire is the goal from the start, an
**ESP32-S3** is better: the interrupt for the CS emulation can run on the second core, apart
from Wi-Fi.

## 7. Plan

1. Test I2C v1.0 on a real PC-1500: Arduino UNO tester ([arduino_tester.md](arduino_tester.md)),
   DS3231 or PCF8574. This also shows how the PB2 latch really behaves.
2. ESP32 firmware: I2C slave (optionally UART) with the mailbox protocol; internet functions;
   SD card with file protocol; web file manager.
3. Machine-code "disk" driver for the PC-1500 (SAVE, LOAD, DIR, DEL with checksum and retry),
   tested in the simulator first.
4. Optionally a faster transfer mode (own protocol or SPI 3-wire on an ESP32-S3) and real BASIC
   commands through the extension table.

## Sources

- [ESP-IDF: SPI Slave Half Duplex (`SPI_SLAVE_HD_3WIRE_MODE`)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/peripherals/spi_slave_hd.html)
- [ESP SPI Slave HD Mode Protocol (transaction format; not on the classic ESP32)](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/protocols/esp_spi_slave_protocol.html)
- [ESP-IDF: I2C Slave v1 on ESP32-C3 (`stretch_en`, `access_ram_en`, move to driver v2)](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/peripherals/i2c_slave_v1.html)
- [Espressif FAQ: I2C](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/i2c.html)
- [Arduino forum: ESP32 WireSlave and `slaveWrite`](https://forum.arduino.cc/t/understanding-esp32-wireslave-example/1000680)
- [esp32.com: ESP32 as I2C slave](https://esp32.com/viewtopic.php?p=37749)
- [OneWireHub: 1-Wire device emulation](https://reference.arduino.cc/reference/en/libraries/onewirehub)
