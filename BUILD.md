# Build & Flash (VS Code)

Modified firmware for the Mobicool FR34/FR40 compressor cooler, based on
[UnifiedEngineering/mobicool-fr34](https://github.com/UnifiedEngineering/mobicool-fr34).
See [README.md](README.md) for the full list of changes in this fork.

**Target MCU: PIC16F18346-I/SO** (pin-compatible replacement for the original
PIC16F1829, chosen for availability — the factory PIC stays untouched as a
backup).

## Operation summary

SET menu (SET key steps through): temperature → °C/°F → `L` cutout voltage →
`H` restart voltage.

- `L xx.x` — battery cutout voltage: compressor stops, display flashes `bAt`
- `H xx.x` — restart voltage, kept at least 0.3V above `L` automatically
- Range 8.0–28.0V, 0.1V steps, hold up/down for auto-repeat
- Defaults: 9.6V / 10.9V. Example for 5S LiFePO4: `L 15.0` / `H 16.0`
  (~3.0V / 3.2V per cell)
- Temperature setpoint range: -18°C to +18°C

Error display (flashing, red error LED lit, automatic retry after 60s):

- `Er 2` — fan over-current (sustained >800mA through the 150mOhm shunt)
- `Er 3` — compressor stall / failed start (commanded on but drawing <5W
  for 15s)

## Tools

1. **XC8 compiler v3.00**: installed at `C:\Users\schmi\Microchip\xc8\v3.00`
   (free mode, no admin rights needed). The VS Code tasks use the full path,
   so no PATH entry is required.
2. **MPLAB X IDE/IPE** (provides `ipecmd.exe` for flashing with the Snap):
   <https://www.microchip.com/mplabx> — installing just the IPE is enough.
   Adjust the `ipecmd.exe` path in `.vscode/tasks.json` to the installed
   version.
3. VS Code extensions: `C/C++` (Microsoft). Optionally Microchip's official
   **MPLAB Extension for VS Code**, which can import, build and flash the
   MPLAB X project `MobicoolFR34.X` directly instead.

## Build & flash

- **Build:** `Ctrl+Shift+B` (task "Build (XC8)") → produces
  `build/mobicool_mfc40.hex`
- **Flash:** task "Flash (MPLAB Snap)" (builds first automatically)

ICSP connection: J2 on the mainboard, pin 1 (square pad) = MCLR/VPP.
Pinout: 1 MCLR, 2 VDD (3.3V!), 3 GND, 4 PGD, 5 PGC — matches the Snap/PICkit
connector 1:1 (see README for the full table). The **MPLAB Snap cannot power
the target** (no VDD output) — power the board from its normal 12V supply
while flashing, or feed 3.3V externally when programming a chip before
soldering.

## Flashing with the MPLAB Snap

A **factory-new PIC16F18346 can be flashed directly with the Snap**: blank
chips have all config bits in the erased state, so LVP (low-voltage
programming) is enabled. This firmware keeps `LVP = ON`, so the Snap also
works for all future updates.

The desoldered original PIC16F1829 remains untouched as a backup — solder it
back in and the cooler runs the factory firmware again. No dump of it is
possible: it has LVP disabled (a stock Snap only reads an invalid device ID,
verified) and is code-protected, so even an HV programmer reads back blank
flash. Keeping the chip is the backup.

Recommended order: flash the new chip **before soldering it in** (adapter,
externally fed with 3.3V) or right after soldering through J2 with the board
running from its normal 12V supply.

## Port notes PIC16F1829 → PIC16F18346

Functions and pin assignment are unchanged; only the low-level drivers in
`mcc_generated_files/` were adapted:

- **Config bits**: new format (`FEXTOSC`/`RSTOSC` instead of `FOSC`; `LVP`
  now in CONFIG3). `RSTOSC = HFINT1` boots straight into 1 MHz HFINTOSC — the
  old `OSCCON` initialization is gone.
- **PPS**: the 18346 has Peripheral Pin Select; the EUSART is routed to the
  board pins in `pin_manager.c` (`RXPPS = 0x0D` → RB5, `RB7PPS = 0x14` → TX).
- **EUSART**: register names carry a "1" suffix (`RC1STA`, `TX1STA`,
  `SP1BRGL`, `BAUD1CON`, `TX1REG`, `RC1REG`). Baud rate unchanged at 9600
  (BRG16+BRGH, SPBRG 25 @ 1 MHz).
- **ADC**: channel selection is port-based (ANA2=0x02, ANC1=0x11, ANC3=0x13,
  ANC6=0x16, ANB4=0x0C) — only the enum values in `adc.h` changed, the
  ADCON0/1 register layout is identical.
- **Data EEPROM**: accessed through the NVM registers
  (`NVMADR`/`NVMCON1`/`NVMDATL`, EEPROM at 0x7000 with `NVMREGS=1`) instead of
  the old EECON registers. The unused FLASH self-write functions were removed.
- **Timer1**: identical apart from bit names (`T1SYNC`, `T1GGO_nDONE`).

## First power-up checklist

The ADC channel codes and PPS values come from the PIC16F183xx datasheet
conventions ([DS40001839](http://ww1.microchip.com/downloads/en/DeviceDoc/PIC16-L-F18326-18346-Data-Sheet-40001839D.pdf)) —
sanity-check them on first boot:

1. Display shows a plausible compartment temperature (NTC channel OK)
2. Short press of the on/off key → `xx.x V` matches the supply voltage
   (voltage monitor channel OK)
3. Compressor starts when the setpoint is below the compartment temperature
   (UART/PPS routing OK)

If a reading is nonsense, a swapped ADC channel is the first suspect.
