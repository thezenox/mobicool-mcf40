# mobicool-fr34 (mobicool_mfc40 fork)

Alternate firmware for Mobicool FR34/FR40 compressor cooler.

**This is a fork of [UnifiedEngineering/mobicool-fr34](https://github.com/UnifiedEngineering/mobicool-fr34).**
It is not intended to be merged upstream. See the next section for everything
that differs from the original project; the original README content follows below.

## Changes in this fork

### Retargeted to PIC16F18346 (from PIC16F1829)

The MCU is swapped for a factory-new **PIC16F18346-I/SO**
([Reichelt](https://www.reichelt.de/de/de/shop/produkt/8-bit-pic-mikrocontroller_28_kb_32_mhz_2_3_-_5_5_v_so-20-354246)),
mainly for availability reasons — and swapping in a new chip means the original
PIC16F1829 with the factory firmware is kept as a backup and can be soldered
back in at any time. The 16F18346 is pin-compatible (same 20-pin SOIC pinout).

A factory-new chip also solves the programming problem: blank parts have all
config bits erased, so **LVP (low-voltage programming) is enabled** and a cheap
MPLAB Snap can program the chip without ever needing 9V on MCLR. (The factory
PIC16F1829 has LVP disabled — a stock Snap cannot even read it, verified: it
only returns an invalid device ID.) This firmware keeps `LVP = ON`, so the
Snap keeps working for all future updates.

Application code is unchanged by the port; only the low-level drivers in
`mcc_generated_files/` were adapted:

- New config word format (`FEXTOSC`/`RSTOSC` instead of `FOSC`; `LVP` moved to
  CONFIG3). `RSTOSC = HFINT1` boots directly at 1 MHz HFINTOSC, so the old
  `OSCCON` setup is gone entirely.
- Peripheral Pin Select: the EUSART is routed to the board pins in
  `pin_manager.c` (`RXPPS = 0x0D` → RB5, `RB7PPS = 0x14` → TX on RB7).
- EUSART registers use the "1"-suffixed names (`RC1STA`, `TX1STA`, `SP1BRGL`,
  `BAUD1CON`, `TX1REG`, `RC1REG`). Still 9600 baud.
- ADC channel selection is port-based on this family (ANA2=0x02, ANC1=0x11,
  ANC3=0x13, ANC6=0x16, ANB4=0x0C) — only the enum values in `adc.h` changed.
- Data EEPROM access goes through the NVM registers (`NVMADR`/`NVMCON1`/
  `NVMDATL`, EEPROM at 0x7000 with `NVMREGS=1`) instead of the old EECON
  registers. The unused FLASH self-write functions were removed.
- Timer1 is identical apart from two bit names (`T1SYNC`, `T1GGO_nDONE`).

### Battery monitor: direct voltage settings instead of Lo/Med/Hi presets

The original firmware auto-detects a 12V or 24V system (>17.0V = 24V) and
offers three preset protection levels. That misdetects a 5S LiFePO4 pack
(~17.3V) as a nearly-dead 24V system and immediately cuts out. This fork
replaces the presets and the auto-detection with two directly adjustable
voltages in the SET menu:

- `L xx.x` — cutout voltage (compressor stops, display flashes `bAt`)
- `H xx.x` — restart voltage (kept at least 0.3V above `L` automatically)

Range 8.0–28.0V in 0.1V steps; hold the up/down key for auto-repeat (also
works in the temperature menu now). Values are stored in EEPROM. Defaults:
9.6V / 10.9V (equivalent to the old "diS" level). Example for 5S LiFePO4:
`L 15.0` / `H 16.0` (~3.0V / 3.2V per cell).

SET menu order (SET key): temperature → °C/°F → `L` cutout → `H` restart.

The EEPROM layout changed accordingly (new magic byte `F`), so settings reset
to defaults the first time this firmware boots.

### Maximum temperature setpoint 18°C

Raised from 10°C to 18°C (minimum stays at -18°C).

### Open TODOs from upstream implemented

- **Fan over-current error handling**: sustained fan current above 800mA
  (jammed/shorted fan) shuts everything down, display flashes `Er 2`,
  automatic retry after 60s.
- **Compressor stall error reporting**: compressor commanded to run but
  drawing less than 5W for 15s (rotor never started or got stuck) shuts down,
  display flashes `Er 3`, automatic retry after 60s.

Error codes follow the Danfoss/Secop numbering; the red error LED is lit while
an error is active, and errors clear themselves once the compressor runs
normally again.

### Toolchain: VS Code + XC8, no MPLAB X required for building

Build/flash tasks for VS Code (`.vscode/tasks.json`) using a plain `xc8-cc`
invocation, plus IntelliSense setup. MPLAB IPE (`ipecmd`) is only needed for
flashing via the Snap. See [BUILD.md](BUILD.md) for details.

## ICSP connector (J2) pinout

J2 is a standard Microchip ICSP header; **pin 1 (MCLR) is the square pad**.
The pin order matches the PICkit/Snap connector 1:1, so the programmer plugs
in directly:

| J2 pin | Signal            | PIC16F18346 pin        |
| ------ | ----------------- | ---------------------- |
| 1      | MCLR/VPP          | 4 (RA3/MCLR)           |
| 2      | VDD (3.3V!)       | 1                      |
| 3      | VSS (GND)         | 20                     |
| 4      | PGD (ICSPDAT)     | 19 (RA0)               |
| 5      | PGC (ICSPCLK)     | 18 (RA1)               |

Notes:

- The board logic runs at **3.3V** (LDO). Note that the **MPLAB Snap cannot
  power the target** (no VDD output, unlike a PICkit) — power the board from
  its normal 12V supply while flashing, or feed 3.3V externally if the chip
  is programmed before soldering. Never feed 5V.
- PGC (RA1) is shared with the load switch for the interior light; that is
  harmless during programming.
- With a factory-new PIC16F18346 (or this firmware, `LVP = ON`) programming
  works via LVP — no high voltage needed. The 9V note below in the original
  README only applies to the factory-programmed PIC16F1829, which has LVP
  disabled (verified: a stock Snap only reads an invalid device ID from it)
  and is code-protected, so its firmware cannot be dumped — keeping the
  desoldered chip is the backup.

---

# Original README (upstream, PIC16F1829)

There are two reasons for this firmware: First I wanted to explore the possibility to run the cooler down to deep-freeze -18C temperature (as the more expensive Waeco/Dometic units can). I also wanted to be able to run the cooler on an 18V Li-Ion battery pack without the battery monitor getting all freaked out.

Mainboard top and bottom (notice the ground plane in the board easily visible by J2, not a simple 2-layer job):
![Main board top](Images/MainBoardTop.JPG "Mainboard Top")
![Main board bot](Images/MainBoardBottom.JPG "Mainboard Bottom")

Display and buttons board top and bottom:
![Disp board top](Images/DisplayBoardTop.JPG "Display board Top")
![Disp board bot](Images/DisplayBoardBottom.JPG "Display board Bottom")

The input is protected from reverse voltage, the 3.3V powering the logic is provided by U3, an LDO (!), yes even from 27+V as it receives when plugged into mains. There's a 12V DC/DC converter powering the cooling fan and some part of the compressor motor driver. The interior LED light is powered directly from the input voltage with a load switch and a series resistor, so intensity will vary depending on input voltage. 

PIC16F1829 pins as used on this board (identical for the PIC16F18346 used in this fork):

Pin | Function
--- | ---
1  VDD |
2  RA5 | TM1620B DIO through ~1k resistor (not used by this firmware, pin 3 is used for both input and output)
3  RA4 | TM1620B DIO
4  MCLR |
5  RC5 | TM1620B CLK
6  RC4 | TM1620B STB
7  RC3 AN7 | Fan current sense (150mOhm to ground)
8  RC6 AN8 | Compressor current analog input (pin 3 of mcp6002e opamp)
9  RC7 | not connected
10 RB7 | TX to IRMCF183, also connected to INT0!
11 RB6 | Output controlling a load switch (fan 12V enable from DC/DC)
12 RB5 | RX from IRMCF183
13 RB4 AN10 | Analog input (1.77V constant, likely 1.8V from IRMCF)
14 RC2 | Output controlling a load switch (12V DC/DC enable)
15 RC1 AN5 | 10k NTC input with 10k to 3v3 (cooler compartment temperature)
16 RC0 | Output controlling a load switch (MMUN2232), needs to be on for compressor to start/run
17 RA2 AN2 | Input voltage monitor (10V in == 598mV)
18 ICSP clk RA1 | (also connected to load switch for internal light)
19 ICSP dat | 
20 GND | 

The display/buttons board uses an interesting chip I've never seen before, the TM1620B from Shenzhen Titan Micro Electronics (http://www.titanmec.com/index.php/en/product/view/id/285.html) with a Chinese-language-only datasheet. Luckily it is a very straight-forward chip to program, take a look at the tm1620b.c code, where the segment mapping is also described for this particular application. 

The motor controller for the brushless DC-motor driving the compressor is an IRMCF183 - this has pre-flashed firmware inside that directly understands very primitive UART commands of 8 bytes: 0xe1, 0xeb, 0x90, motor run (1) or stop(0), then revolutions per second, 0x00, 0x00, checksum (which is a simple addition of the first 7 bytes). The response seems to be 0xe1 (only?). I couldn't find any example project from Infineon matching this packet structure, maybe someone recognizes it from somewhere else? This firmware may very well be used in other Dometic coolers using the Wancool AMV13JZ compressor. I have not tried to access the JTAG port on the IRMCF183 chip, but there's a nice space for an unpopulated connector (J1) right at the board edge :) J4 is the UART interface between PIC and IRMCF183.

The upstream firmware was built using the MPLAB X IDE v4.20 and the free XC8 C compiler v2.00; this fork builds with XC8 v3.00 from VS Code (see [BUILD.md](BUILD.md)).

The ICSP connector (J2) is a standard pinout one where pin 1 (MCLR) being the square one. Note that the system voltage is 3.3V and the LVP program fuse most likely was disabled in the pre-programmed parts, so 9V (not 12V!) has to be applied to MCLR to program.


Here are two final images showing the remaining parts inside and the exterior:
![Interior](Images/WancoolCompressor.JPG "Compressor and power supply")
![Exterior](Images/MobicoolFR34ExteriorOriginalFirmware.JPG "Exterior of Mobicool FR34, with original firmware")
(the leading zero in the exterior shot reveals that it is running the original firmware :))
