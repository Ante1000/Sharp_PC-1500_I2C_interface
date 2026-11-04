# Quick start: I2C for the Sharp PC-1500(A)

Two steps: **1.** put the I2C interface into the pocket computer, **2.** (optional) run the demo
clock on a 16x2 LCD. Details, timing and all options are in [README.md](README.md).

> **Remove the PC-1500(A) from the CE-150 while I2C is in use.** PB2 (SDA) is the cassette
> input of the CE-150. Load the tape recordings with the computer docked in the CE-150
> (audio output of your computer or player to the EAR jack), then take it out and connect your
> I2C device.

---

## 1. Install the I2C interface

You need a bare **PC-1500A** (a PC-1500 needs a RAM module). Download the file 
[`wav/i2c_v1.4.wav`](wav/i2c_v1.4.wav) from this repository.

| Step | Action |
|---|---|
| 1 | Switch to **PRO** mode. Type `NEW0` [ENTER], then `NEW &4500` [ENTER]. |
| 2 | Type `CLOAD` [ENTER] and play `wav/i2c_v1.4.wav` until it has loaded. |
| 3 | Switch to **RUN** mode and type `RUN` [ENTER]. The installer tests the CPU, then asks for the SDA port: press [ENTER] for **PB2** (pin 27). |
| 4 | Switch to **PRO** mode and type `NEW` [ENTER]. The I2C code stays in memory. |

The interface is ready to use. The README.md file describes how to use this I2C interface. 

Other memory configurations (RAM modules): use the `NEW` value from the
table in [README.md](README.md#memory) instead of `NEW &4500`.
If the installer shows "USE I2C V1.2", install [`Archive/pc1500_i2c_installer-v1.2.txt`](Archive/pc1500_i2c_installer-v1.2.txt) instead.

### Wiring

Pins of the 60-pin connector on the left side of the PC-1500(A):

| Signal | Pin | I2C line |
|---|---|---|
| PC7 | **10** | **SCL**, through a Schottky diode (BAT43, BAT85, 1N5817; 1N4148 also works), cathode (stripe) toward PC7. Often this diode can be omitted - check with your I2C module. |
| PB2 | **27** | **SDA**, through 470 ohm. This resistor is only for protection. |
| GND | **52-55** | GND of the I2C device |

* Pull-ups of 4.7-10 kohm on SCL and SDA are needed only if the module has none (many modules
  already have them on board: then add no more).
* **3.3 V modules** work with the same wiring: the PC-1500 reads 2.4 V or more as high. Use
  pull-ups to 3.3 V.
* The connector drawing is in the
  [Software UART repository](https://github.com/Ante1000/Sharp_PC-1500_Software_UART/blob/main/img/pc1500_60pin_connector.png).

---

## 2. Demo: date and time on a 16x2 LCD

### What you need

* an HD44780 **LCD 2x16** module **with the I2C converter** (PCF8574 "backpack", address `&27`
  or `&3F`; the program tries both),
* the I2C interface installed as above (still in memory, `NEW &4500`),
* the recording [`wav/lcd_clock.wav`](wav/lcd_clock.wav) (tape name `LCDCLK`).

### Connection

| LCD module | PC-1500 connector |
|---|---|
| GND | pin 52-55 |
| SCL | pin 10 (diode as above, usually can be ommited) |
| SDA | pin 27 (470 ohm as above, usually can be ommited) |
| VCC | pin 44-48 (battery line, Vbat) **or** pin 11-12 (Vcc line), see the note below |

> **Power supply.** The backlight draws a lot of current, so the LCD should be supplied
> directly from the battery line. If you replace the backlight jumper on the I2C converter by a
> resistor of 470-680 ohm, the backlight current falls to about 3 mA and the module can be
> supplied from the regulated Vcc line (pin 11 or 12).

### Run it

| Step | Action |
|---|---|
| 1 | In **RUN** mode set the clock of the PC-1500: `TIME = MMDDHH.MMSS` [ENTER] (for example `TIME = 110314.3000` for 3 November, 14:30:00). |
| 2 | Dock the computer in the CE-150. In **PRO** mode type `CLOAD` [ENTER] (do **not** type `NEW0` first: that would remove the I2C code) and play `wav/lcd_clock.wav`. |
| 3 | Take the computer out of the CE-150 and connect the LCD. Switch it on: the backlight must be on. Adjust the contrast with the potentiometer on the converter. |
| 4 | Switch to **RUN** mode and type `RUN` [ENTER]. |

The LCD shows `INITIALISING...`, then the title `* I2C CLOCK *` , then the 
date (`DATE: MM/DD`) and the time (`TIME: HH:MM:SS`). 
**Any key** stops the program.

### Troubleshooting

| Message / symptom | Cause |
|---|---|
| `I2C NOT INSTALLED` | the I2C code is not in memory: do part 1 again and use `NEW &4500` (not a plain `NEW`) before `CLOAD` |
| `NO LCD AT &27 OR &3F` | wiring, supply, or the backpack has another address (load the installer again and use its bus scanner, `RUN 500`) |
| blank LCD, backlight on | contrast potentiometer |
| wrong time | set `TIME = MMDDHH.MMSS` (step 1) |
