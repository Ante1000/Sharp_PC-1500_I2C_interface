# Help: test program `pc1500_i2c_arduino_test-v1.0` (PC-1500 ↔ Arduino UNO over I2C)

The BASIC program `Basic/pc1500_i2c_arduino_test-v1.0.txt` tests the I2C connection between a
Sharp PC-1500(A) with **I2C v1.3** and an **Arduino UNO + LCD Keypad Shield** running the sketch
`arduino/PC1500_I2C_Slave/PC1500_I2C_Slave.ino`.

**The PC-1500 is the MASTER**, because the I2C routine can only be a master. **The Arduino is
the device (SLAVE) at address &42.** The PC-1500 sends commands: text for the LCD, reading the
ADC and the shield buttons, an echo test and a speed measurement. The Arduino carries them out
and sends the answers back.

Technical description of the tester: [docs/arduino_tester.md](docs/arduino_tester.md).

## Test status

| Item | Status |
|---|---|
| sketch `PC1500_I2C_Slave` | compiles without warnings: 8.9 KB of flash, 722 B of RAM |
| sketch `LCD_Keypad_Test` (test of the shield alone) | tested on hardware, works |
| CPU test of the I2C v1.3 installer on a real PC-1500 | passed: the LH5801 sets the Z flag after `SHL`/`ROL` |
| speed measurement (menu 6) on a real PC-1500 | **works: SCL 17.505 kHz, 1884 B/s** |
| ECHO test (menu 5), 100 transfers | **0 errors** |

## Parts

- Arduino UNO (5 V) with a clone of the "LCD Keypad Shield" (DFRobot): 16×2 display and
  6 buttons
- Schottky diode: BAT43, BAT85 or 1N5817. Not a 1N4148, its voltage drop is too high
- 470 Ω resistor
- optional: two 4.7 kΩ resistors
- plug for the 60-pin connector of the PC-1500: 2 × 30 pin header with 1.27 mm pitch

## Wiring

| PC-1500 (60-pin connector) | In between | Shield / UNO |
|---|---|---|
| PC7, pin 10 | Schottky diode, **cathode (stripe) toward PC7** | **A5** (SCL) |
| PB2, pin 27 | 470 Ω resistor | **A4** (SDA) |
| GND, pins 52–55 | – | GND |

```
PC-1500 (60-pin)                          Arduino UNO / shield
                     BAT43 / BAT85
PC7  pin 10  ──────────|<|──────────────  A5  (SCL)
             (stripe/cathode to PC7)
PB2  pin 27  ─────────[ 470 ]───────────  A4  (SDA)

GND  pin 55  ───────────────────────────  GND

optional:  A5 ──[ 4.7k ]── 5V      A4 ──[ 4.7k ]── 5V
```

- **Pull-up resistors.** `Wire.begin()` in the sketch switches on the internal pull-ups of the
  ATmega on A4 and A5 (20–50 kΩ). With short wires (up to 20–30 cm) this is enough for the
  17.5 kHz clock of I2C v1.3: the lines rise within about 1–7 µs, and every line change of the
  master is a separate instruction of at least 13 µs. So three wires are enough to start. Add
  the external 4.7 kΩ resistors from A4 and A5 to 5 V if the ECHO test shows errors, with
  longer wires or with other modules on the bus. Most modules, for example the DS3231, already
  have 4.7 kΩ resistors on board.
- **Where to take 5 V and GND.** A4 and A5 are on the upper header of the shield. If there are
  V/G rows (5 V and ground) next to A1–A5, use them. If not, use the ICSP header of the UNO:
  pin 2 is 5 V, pin 6 is GND.
- **The diode is needed even though both sides run at 5 V.** After every byte the UNO holds
  SCL low until it has handled its interrupt. Without the diode PC7 would drive SCL high at the
  same time, and two outputs would fight each other.
  - With the diode PC7 can only pull the line low, like a real open-drain I2C output. The pull-up
    resistor makes the high level.
  - So current only flows from the pull-up into PC7 (about 1 mA), never from PC7 into the
    Arduino. This is how it should be.
- **No diode on SDA,** because PB2 must also read this line (ACK bits and data from the
  Arduino). The 470 Ω resistor only limits the current if both sides ever drive the line at the
  same time.
- **Remove the PC-1500 from the CE-150,** because the CE-150 drives PB2.
- **Leave D0 and D1 free:** they are USB and the serial monitor.
- **Free Arduino pins:** D2 and D3 can be set and read by commands from the PC-1500, A1–A3
  measured with the ADC. A0 is the resistor ladder of the shield buttons.

## Start

1. **Upload the sketch** `PC1500_I2C_Slave.ino` to the UNO (Arduino IDE, board "Arduino Uno").
   The LCD shows "I2C slave 0x42". If the screen is blank, adjust the contrast with the blue
   potentiometer on the shield.
2. **Install I2C v1.3 on the PC-1500:**
   - in PRO mode enter `NEW0`, then `NEW &4500` (PC-1500A; other values are in the table in the
     [README](README.md#memory));
   - type in the installer `Basic/pc1500_i2c_installer-v1.3.txt` and `RUN` it. It first tests
     the CPU; if it shows "CPU: …, USE I2C V1.2", your LH5801 cannot run v1.3: use
     `Archive/pc1500_i2c_installer-v1.2.txt` instead, the test program works with it unchanged;
   - when asked for the SDA port, press ENTER (PB2).
3. **Run the scanner of the installer** (`RUN 500`). It must find a device at **&42**.
4. **Enter `NEW` in PRO mode** to remove the installer. The I2C code stays in memory.
5. **Type in the test program** `Basic/pc1500_i2c_arduino_test-v1.0.txt` and `RUN` it.
   At the start it checks:
   - that the I2C code is in memory (first bytes &FD &BE). If not, it shows
     "I2C NOT INSTALLED";
   - that the Arduino answers (presence test). If not, it shows "I2C ERROR E=1 (1=NO ACK)".

The test program needs a few KB, so it fits in a PC-1500A. A PC-1500 with 2 KB of RAM needs a
memory module (for example a CE-155, then `NEW &3D00`).

## Menu on the PC-1500

After the start the program shows the menu and then asks `CHOICE 1-8 (9 MENU)?`:

```
0END 1ID 2TEXT 3ADC 4KEYS
5ECHO 6SPEED 7LIGHT 8CLS
```

`0` ends the program, `9` shows the menu again. A message held on the display (WAIT) is closed
with the ENTER key.

| No. | Test | Questions | What it does and shows |
|---|---|---|---|
| 1 | ID | – | Reads "PC1500 I2C v1.0" (the identification of the Arduino sketch). The simplest write and read test. |
| 2 | TEXT | `LCD ROW (0/1)?`, `TEXT?` | Sends a text (up to 16 characters) to row 0 or 1 of the Arduino display and shows `SENT n CHARACTERS`. |
| 3 | ADC | `CHANNEL A0-A3 (0-3)?` | Continuous measurement of the chosen channel: ADC value (0–1023) and voltage in volts. Channel 0 is the button ladder, so pressing the shield buttons changes the value. Exit: any key of the PC-1500. |
| 4 | KEYS | – | The shield buttons on the PC-1500: `NOW:` the button pressed now, `LAST:` the last one pressed. Exit: any key of the PC-1500. |
| 5 | ECHO | `HOW MANY ECHO TESTS?` (ENTER = 20) | N transfers of 1–29 random bytes. The Arduino sends the data back, the PC-1500 compares them and counts the errors: `ECHO j/N ERRORS b`, and a summary at the end. |
| 6 | SPEED | – | Speed measurement: command `F`, then a write of 31 bytes (`X` + 30 bytes). The Arduino measures the time from the first SCL edge to the STOP condition. The result appears on the LCD (for 4 s) and on the PC-1500, with I2C v1.3: `SCL 17.505 KHZ, 1884 B/S` and `32 B INCL ADDR: 16.452 MS`. Only the transfer itself is measured, without the time BASIC needs. |
| 7 | LIGHT | `LIGHT 1=ON 0=OFF?` | Switches the LCD backlight on and off. |
| 8 | CLS | – | Clears the Arduino LCD. |

**On the Arduino:**
- The **SELECT** button switches the LCD between the text from the PC-1500 and the I2C
  statistics: address, number and letter of the last command, counters of writes (W), reads
  (R) and errors (E).
- The serial monitor (115200 bps) prints every command.

## Error messages

| Message | Meaning | What to check |
|---|---|---|
| `I2C NOT INSTALLED` | there is no I2C code at RAM+&400 | install I2C v1.3 (installer, then `NEW`) |
| `I2C ERROR E=1 (1=NO ACK)` | the Arduino did not acknowledge the address &42 | wires, direction of the diode, power of the UNO, PC-1500 removed from the CE-150. For 1–2 s after a reset of the UNO (bootloader) no answer is normal |
| `I2C ERROR E=2` | the Arduino did not accept a data byte | a write longer than 32 bytes (buffer of the Wire library) |
| `I2C ERROR E=3` | SDA is low all the time | SDA shorted to ground, UNO without power, no pull-up |
| `ARDUINO BUSY` | after 50 reads the answer is still not ready (status 0) | is the sketch running (LCD, serial monitor)? |
| `UNKNOWN COMMAND` | the Arduino does not know the command (status 2) | version of the sketch |
| `BAD PARAMETER` | for example ADC channel above 3 or LCD row other than 0/1 (status 3) | the value entered |
| `MEASUREMENT FAILED` | the Arduino did not record the measured write | repeat test 6 |

## Protocol

Every command is **a separate write** (W = length, R = 0). The answer is read with **a
separate read** of 32 bytes (W = 0, R = 32).

**Answer:** `[status][command number][N][N data bytes]`, N ≤ 29. The data start at RX+3.

| Status | Meaning |
|---|---|
| 0 | the Arduino is still working on the command. BASIC reads again, up to 50 times |
| 1 | OK |
| 2 | unknown command |
| 3 | bad parameter |

| Command (byte 0) | Parameters | Answer data |
|---|---|---|
| `P` (80) identification | – | "PC1500 I2C v1.0" |
| `T` (84) text on the LCD | row 0/1, characters (up to 16) | – |
| `C` (67) clear the LCD | – | – |
| `A` (65) ADC measurement | channel 0–3 (A0–A3) | value 0–1023 (2 B), voltage in mV (2 B) |
| `K` (75) shield buttons | – | button now, last button pressed (then cleared), counter; 0 none, 1 RIGHT, 2 UP, 3 DOWN, 4 LEFT, 5 SELECT |
| `E` (69) echo | up to 29 bytes | the same bytes |
| `F` (70) arm the speed measurement | – | – (do not read the answer) |
| `X` (88) test data for the measurement | any | – |
| `S` (83) result of the measurement | – | valid (1 B), bytes including the address (1 B), time in µs (4 B), SCL in Hz (2 B), bytes/s (2 B) |
| `L` (76) backlight | 1 / 0 | – |
| `D` (68) read inputs D2, D3 | – | D2, D3 (0/1) |
| `O` (79) set D2/D3 | pin 2/3, state 0/1 (output) or 2 (input with pull-up) | – |
| `U` (85) time since the Arduino started | – | ms (4 B) |

Numbers of more than one byte: low byte first. The menu does not use the commands `D`, `O` and
`U`.

**Your own commands from BASIC.** The subroutine at line 920 sends L bytes from the TX buffer,
waits for the answer and returns E (I2C result), Q (status) and N (number of data bytes).
Example to add to the program: set D2 high, then read the time since the Arduino started.
Start it with `RUN 1000`:

```
1000 A0 = (PEEK &7863) * 256 : IC = A0 + &400 : IA = A0 + &4FC
1010 TX = A0 + &300 : RX = A0 + &200 : D = &42
1020 POKE TX, 79, 2, 1 : L = 3 : GOSUB 920 : WAIT : PRINT "E="; E; " Q="; Q
1030 POKE TX, 85 : L = 1 : GOSUB 920 : U = PEEK (RX + 3) + 256 * PEEK (RX + 4)
1040 PRINT U + 65536 * PEEK (RX + 5); " MS" : END
```

Line 1040 uses only the 3 low bytes of the counter, which is enough for 4.6 hours of Arduino
running time.

## Two timing rules

Our master cannot see that the Arduino holds SCL low (PC7 is an output only). So the Arduino
must finish its interrupt before the PC-1500 raises the clock again. The times below come from
the simulator, increased by the 7 % by which the real PC-1500 with I2C v1.3 is slower. After
the ACK clock the master keeps SCL low for about **69 µs** after each written byte, **74 µs**
after the read address and **51 µs** after each received byte.

- **Command and answer are always two separate `CALL`s.**
  - When reading, the master gives the Arduino about 74 µs to prepare the data, and the
    `onRequest` handler takes about 25–30 µs (estimate). That is a comfortable margin.
  - Write and read in one `CALL` (with a repeated START): at the repeated START the Wire
    library calls `onReceive` for the bytes just written, inside the interrupt, and the Arduino
    may hold SCL low meanwhile, which our master cannot see. The read address could then be
    lost.
- **At most 32 bytes per transaction,** the size of the Wire library buffer. A longer write
  ends with E=2.

## Tips

- Unconnected inputs A1–A3 show random values. For the ADC test connect a potentiometer (outer
  pins to 5 V and GND, wiper to A1) or use channel 0.
- Errors in the ECHO test: add the external 4.7 kΩ resistors on A4 and A5, shorten the wires
  and check that the diode is a Schottky diode.
- Speed measurement results: a real PC-1500A with I2C v1.3 measured 17.505 kHz and 1884 B/s. The kHz
  figure is the average over the whole write: 9 clocks per byte (8 bits + ACK) times 32 bytes,
  divided by the time. Single clock periods vary with the data, from about 42 µs for data bits
  to about 110 µs around the ACK.
