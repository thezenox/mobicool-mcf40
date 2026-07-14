/*
 * main.c - Mobicool FR34/FR40 compressor cooler alternate PIC16F1829 firmware
 *          (because I wanted to lower the minimum setpoint from -10C to -18C)
 *
 * Copyright (C) 2018 Werner Johansson, wj@unifiedengineering.se
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// Modifications compared to upstream mobicool-fr34:
// - Battery monitor Lo/Med/Hi presets (with 12V/24V auto-detection) replaced by
//   directly adjustable cutout ("L x.x") and restart ("H x.x") voltages in the
//   SET menu, so odd packs like 5S LiFePO4 (~17.3V) don't get misdetected as a
//   drained 24V system
// - Maximum temperature setpoint raised from 10C to 18C
// - Fan over-current error handling implemented (error code Er2)
// - Compressor stall / failed start error reporting implemented (error code Er3)

// This was all that was stored in the EEPROM in original firmware:
// af 2c 00
// 02 04
// This indicates set temperature (+5C I believe), on/off state (likely On) and battery monitor selection (Lo) somehow

#include "mcc_generated_files/mcc.h"
#include "tm1620b.h"
#include "analog.h"
#include "irmcf183.h"

#define MAX_TEMP (18)
#define MIN_TEMP (-18)
#define DEFAULT_TEMP MAX_TEMP

#define DEFAULT_BRIGHT (4)
#define DIM_BRIGHT (0)

#define MAGIC ('F') // Changed from 'W': EEPROM layout differs from upstream

// Battery protection voltages, all in tenths of Volts
#define VOLT_SETTING_MIN (80)  // 8.0V lowest selectable cutout
#define VOLT_SETTING_MAX (280) // 28.0V highest selectable restart (mains adapter feeds ~27V)
#define VOLT_HYST_MIN (3)      // Restart must stay at least 0.3V above cutout
#define DEFAULT_VOLT_CUTOUT (96)
#define DEFAULT_VOLT_RESTART (109)

// Fan over-current handling: 150mOhm shunt on the fan output, a healthy fan
// draws a few hundred mA, so anything above this sustained means jammed/shorted
#define FAN_OC_LIMIT (800)  // mA
#define FAN_OC_SAMPLES (8)  // consecutive main loop iterations (~0.5s)

// Compressor stall detection: commanded to run but drawing almost no power
#define COMP_STALL_POWER (5) // W
#define COMP_STALL_SECS (15) // seconds below COMP_STALL_POWER while in RUN
#define ERR_RETRY_SECS (60)  // lockout before retrying after an error

typedef enum {
    IDLE = 0,

    SET_BEGIN,
    SET_TEMP,
    SET_UNIT,
    SET_VMIN,
    SET_VMAX,
    SET_END,

    DISP_BEGIN,
    DISP_VOLT,
    DISP_COMPPOWER,
    DISP_COMPTIMER,
    DISP_COMPSPEED,
    DISP_FANCURRENT,
    DISP_TEMPRATE,
    DISP_END,
} displaystate_t;

typedef enum {
    EE_MAGIC = 0,
    EE_ONOFF,
    EE_TEMP,
    EE_UNIT,
    EE_VCUT,                    // 16-bit, tenths of Volts
    EE_VRESTART = EE_VCUT + 2,  // 16-bit, tenths of Volts
} eedata_t;

typedef enum {
    COMP_LOCKOUT = 0,
    COMP_OFF,
    COMP_STARTING,
    COMP_RUN,
} comp_state_t;

// Numeric values double as the displayed error code ("Er 2" / "Er 3"),
// mimicking the Danfoss/Secop error numbering
typedef enum {
    ERR_NONE = 0,
    ERR_FAN = 2,  // Fan over-current
    ERR_COMP = 3, // Compressor stall or failure to start
} errcode_t;

static uint16_t EE_ReadWord(uint8_t addr) {
    return (uint16_t)DATAEE_ReadByte(addr) | ((uint16_t)DATAEE_ReadByte(addr + 1) << 8);
}

static void EE_WriteWord(uint8_t addr, uint16_t val) {
    DATAEE_WriteByte(addr, val & 0xff);
    DATAEE_WriteByte(addr + 1, val >> 8);
}

void main(void) {
    // initialize the device
    SYSTEM_Initialize();

    // When using interrupts, you need to set the Global and Peripheral Interrupt Enable bits
    // Use the following macros to:

    // Enable the Global Interrupts
    //INTERRUPT_GlobalInterruptEnable();

    // Enable the Peripheral Interrupts
    //INTERRUPT_PeripheralInterruptEnable();

    // Disable the Global Interrupts
    //INTERRUPT_GlobalInterruptDisable();

    // Disable the Peripheral Interrupts
    //INTERRUPT_PeripheralInterruptDisable();

    IO_LightEna_SetHigh();
    TM1620B_Init();
    TM1620B_Update( (uint8_t[]){0, c_U, c_E, c_o, c_S} );

    __delay_ms(200);
    Compressor_Init();
    __delay_ms(1800);

    displaystate_t cur_state = IDLE;
    uint8_t lastkeys = 0;
    uint8_t flashtimer = 0;
    uint16_t seconds = 0;
    uint8_t comp_timer = 20;
    uint8_t comp_speed = 0;
    comp_state_t compstate = COMP_LOCKOUT;

    bool eeinvalid = false;
    if (DATAEE_ReadByte(EE_MAGIC) != MAGIC) {
        eeinvalid = true;
    }
    bool on = DATAEE_ReadByte(EE_ONOFF);
    int8_t temp_setpoint = DATAEE_ReadByte(EE_TEMP);
    if (temp_setpoint < MIN_TEMP || temp_setpoint > MAX_TEMP) {
        eeinvalid = true;
    }
    bool fahrenheit = DATAEE_ReadByte(EE_UNIT);
    uint16_t volt_cutout = EE_ReadWord(EE_VCUT);
    uint16_t volt_restart = EE_ReadWord(EE_VRESTART);
    if (volt_cutout < VOLT_SETTING_MIN || volt_restart > VOLT_SETTING_MAX ||
        volt_restart < volt_cutout + VOLT_HYST_MIN) {
        eeinvalid = true;
    }
    if (eeinvalid) {
        on = true;
        DATAEE_WriteByte(EE_ONOFF, on);
        temp_setpoint = DEFAULT_TEMP;
        DATAEE_WriteByte(EE_TEMP, temp_setpoint);
        fahrenheit = false;
        DATAEE_WriteByte(EE_UNIT, fahrenheit);
        volt_cutout = DEFAULT_VOLT_CUTOUT;
        EE_WriteWord(EE_VCUT, volt_cutout);
        volt_restart = DEFAULT_VOLT_RESTART;
        EE_WriteWord(EE_VRESTART, volt_restart);
        DATAEE_WriteByte(EE_MAGIC, MAGIC); // Always write magic at the end
    }

    AnalogUpdate();
    uint8_t longpress = 0;
    bool newon = on;
    int8_t newtemp = temp_setpoint;
    bool newfahrenheit = fahrenheit;
    uint16_t newvcut = volt_cutout;
    uint16_t newvrestart = volt_restart;
    int16_t temp_setpoint10 = temp_setpoint * 10;
    int16_t tempacc = 0;
    uint8_t numtemps = 0;
    int16_t temperature10 = AnalogGetTemperature10();
    int16_t temp_rate = 0; // Rate of change per minute in tenths of degrees C
    int16_t last_temp = 0;
    uint8_t temp_rate_tick = 0;
    uint8_t idletimer = 0;
    uint8_t dimtimer = 0;
    uint32_t voltacc = 0;
    uint8_t numvolts = 0;
    bool battlow = false;
    uint8_t keyrepeat = 0;
    errcode_t error = ERR_NONE;
    uint8_t fan_oc_count = 0;
    uint8_t stall_secs = 0;

    while (1) {
        bool compressor_check = false;
        if (TMR1_HasOverflowOccured()) {
            TMR1_Reload();
            PIR1bits.TMR1IF = 0;
            seconds++;
            compressor_check = true;
            if (idletimer < 10) {
                idletimer++;
            } else if (idletimer == 10) {
                cur_state = IDLE;
            }
            if (dimtimer < 20) {
                dimtimer++;
            } else if (dimtimer == 20) {
                TM1620B_SetBrightness(true, DIM_BRIGHT);
            }
        }

        AnalogUpdate();
        // Average temperature a bit more
        tempacc += AnalogGetTemperature10();
        numtemps++;
        if (numtemps == 64) {
            temperature10 = (tempacc + 32) >> 6;
            tempacc = numtemps = 0;
        }
        uint16_t voltage = AnalogGetVoltage();

        // Average voltage some more for battery monitor
        voltacc += voltage;
        numvolts++;
        if (numvolts == 64) {
            uint16_t volt = (voltacc + 32) >> 6;
            volt = (volt + 50) / 100; // Scale to tenths of Volts
            if (volt < volt_cutout && !battlow) {
                battlow = true;
                Compressor_OnOff(false, false, 0);
                comp_timer = 20;
                compstate = COMP_LOCKOUT;
            } else if (volt > volt_restart && battlow) {
                battlow = false;
            }
            voltacc = numvolts = 0;
        }

        if (battlow) compressor_check = false;

        uint16_t fancurrent = AnalogGetFanCurrent();
        uint8_t comppower = AnalogGetCompPower();

        // Fan over-current error handling
        if (Compressor_IsFanOn() && fancurrent > FAN_OC_LIMIT) {
            if (++fan_oc_count >= FAN_OC_SAMPLES) {
                fan_oc_count = 0;
                error = ERR_FAN;
                Compressor_OnOff(false, false, 0);
                comp_timer = ERR_RETRY_SECS;
                compstate = COMP_LOCKOUT;
            }
        } else {
            fan_oc_count = 0;
        }

        uint8_t keys = TM1620B_GetKeys();
        uint8_t pressed_keys = keys & ~lastkeys;

        // Auto-repeat for held +/- keys (temperature and voltage settings)
        uint8_t repeat_keys = pressed_keys;
        if (keys & (KEY_PLUS | KEY_MINUS)) {
            if (keyrepeat < 255) keyrepeat++;
            if (keyrepeat > 12 && (keyrepeat & 1)) {
                repeat_keys |= keys & (KEY_PLUS | KEY_MINUS);
            }
        } else {
            keyrepeat = 0;
        }

        bool comp_on = Compressor_IsOn();
        uint8_t leds = /*orange*/!comp_on << 7 | /*err*/(battlow || error != ERR_NONE) << 6 | /*green*/comp_on << 4;

        if (keys & KEY_ONOFF) {
            if (longpress <= 20) longpress++;
            if (longpress == 20) {
                newon = !newon;
                cur_state = IDLE;
                if (newon) {
                    idletimer = 0;
                    dimtimer = 0;
                    TM1620B_SetBrightness(true, DEFAULT_BRIGHT);
                } else {
                    Compressor_OnOff(false, false, 0);
                    comp_timer = 20;
                    compstate = COMP_LOCKOUT;
                    error = ERR_NONE;
                    fan_oc_count = 0;
                    stall_secs = 0;
                    TM1620B_SetBrightness(true, DIM_BRIGHT);
                }
            }
        } else {
            longpress = 0;
        }

        if (on) {
            IO_LightEna_SetHigh();
        } else {
            IO_LightEna_SetLow();
            leds = 0;
            pressed_keys = 0;
            repeat_keys = 0;
            compressor_check = false;
        }

        if (pressed_keys) {
            flashtimer = 0; // restart flash timer on every keypress
            idletimer = 0;
            dimtimer = 0;
            TM1620B_SetBrightness(true, DEFAULT_BRIGHT);
        }

        if (pressed_keys & KEY_ONOFF) {
            // Implement actual power off/on here some day, on long-press
            // Short presses toggles between different status displays
            if (cur_state < DISP_BEGIN || cur_state > DISP_END) cur_state = DISP_BEGIN;
            cur_state++;
            if (cur_state == DISP_END) cur_state = IDLE;
        }

        if (pressed_keys & KEY_SET) {
            if (cur_state < SET_BEGIN || cur_state > SET_END) {
                cur_state = SET_BEGIN;
                newtemp = temp_setpoint;
                newvcut = volt_cutout;
                newvrestart = volt_restart;
            }
            cur_state++;
            if (cur_state == SET_END) cur_state = IDLE;
        }

        if (cur_state == IDLE) { // Perform housekeeping if we need to update settings
            if (newon != on) {
                on = newon;
                DATAEE_WriteByte(EE_ONOFF, on);
            }
            if (newtemp != temp_setpoint) {
                temp_setpoint = newtemp;
                temp_setpoint10 = newtemp * 10;
                DATAEE_WriteByte(EE_TEMP, temp_setpoint);
            }
            if (newfahrenheit != fahrenheit) {
                fahrenheit = newfahrenheit;
                DATAEE_WriteByte(EE_UNIT, fahrenheit);
            }
            if (newvcut != volt_cutout) {
                volt_cutout = newvcut;
                EE_WriteWord(EE_VCUT, volt_cutout);
            }
            if (newvrestart != volt_restart) {
                volt_restart = newvrestart;
                EE_WriteWord(EE_VRESTART, volt_restart);
            }
        }

        switch (cur_state) {
            case DISP_VOLT: {
                uint8_t buf[5];
                uint16_t dispvolt = (voltage + 50) / 100; // decivolt
                uint8_t num = FormatDigits(NULL, dispvolt, 2);
                buf[0] = leds;
                buf[1] = 0;
                FormatDigits(&buf[4 - num], dispvolt, 2);
                buf[3] |= ADD_DOT;
                buf[4] = c_V;
                TM1620B_Update( buf );
                break;
            }
            case DISP_COMPPOWER: {
                uint8_t buf[3];
                buf[1] = 0;
                FormatDigits(buf, comppower, 0);
                TM1620B_Update( (uint8_t[]){leds, c_C, 0, buf[0], buf[1]} );
                break;
            }
            case DISP_COMPTIMER: {
                uint8_t buf[3];
                buf[1] = 0;
                FormatDigits(buf, comp_timer, 0);
                TM1620B_Update( (uint8_t[]){leds, c_t, 0, buf[0], buf[1]} );
                break;
            }
            case DISP_COMPSPEED: {
                uint8_t buf[3];
                FormatDigits(buf, comp_speed * 5, 3); // In percent
                TM1620B_Update( (uint8_t[]){leds, c_r, buf[0], buf[1], buf[2]} );
                break;
            }
            case DISP_FANCURRENT: {
                uint8_t buf[5];
                uint16_t dispamp = (fancurrent + 50) / 100; // deciamp
                uint8_t num = FormatDigits(NULL, dispamp, 2);
                buf[0] = leds;
                buf[1] = c_F;
                FormatDigits(&buf[4 - num], dispamp, 2);
                buf[3] |= ADD_DOT;
                buf[4] = c_A;
                TM1620B_Update( buf );
                break;
            }
            case DISP_TEMPRATE: {
                uint8_t buf[5];
                uint8_t num = FormatDigits(NULL, temp_rate, 2);
                buf[0] = leds;
                buf[1] = c_d;
                buf[2] = 0;
                FormatDigits(&buf[5 - num], temp_rate, 2);
                buf[4] |= ADD_DOT;
                TM1620B_Update( buf );
                break;
            }
            case SET_TEMP: {
                uint8_t buf[5] = {leds, 0, 0, 0, fahrenheit ? c_F : c_C | ADD_DOT};
                if (repeat_keys & KEY_MINUS && newtemp > MIN_TEMP) newtemp--;
                if (repeat_keys & KEY_PLUS && newtemp < MAX_TEMP) newtemp++;
                if (!(flashtimer & 0x08)) {
                    int8_t disptemp = fahrenheit ? ((((newtemp * 9) + 2) / 5) + 32) : newtemp;
                    uint8_t num = FormatDigits(NULL, disptemp, 0);
                    FormatDigits(&buf[4 - num], disptemp, 0); // Right justified
                }
                TM1620B_Update( buf );
                break;
            }
            case SET_UNIT:
                if (pressed_keys & (KEY_PLUS | KEY_MINUS)) {
                    newfahrenheit = !fahrenheit;
                }
                TM1620B_Update( (uint8_t[]){leds, 0, 0, 0, (flashtimer & 0x08 ? 0 : (newfahrenheit ? c_F : c_C)) | ADD_DOT} );
                break;
            case SET_VMIN: { // Battery monitor cutout voltage, shown as "L xx.x"
                if (repeat_keys & KEY_MINUS && newvcut > VOLT_SETTING_MIN) newvcut--;
                if (repeat_keys & KEY_PLUS && newvcut < VOLT_SETTING_MAX - VOLT_HYST_MIN) {
                    newvcut++;
                    if (newvrestart < newvcut + VOLT_HYST_MIN) {
                        newvrestart = newvcut + VOLT_HYST_MIN; // Keep hysteresis intact
                    }
                }
                uint8_t buf[5] = {leds, c_L, 0, 0, 0};
                if (!(flashtimer & 0x08)) {
                    uint8_t num = FormatDigits(NULL, (int16_t)newvcut, 2);
                    FormatDigits(&buf[5 - num], (int16_t)newvcut, 2);
                    buf[3] |= ADD_DOT;
                }
                TM1620B_Update( buf );
                break;
            }
            case SET_VMAX: { // Battery monitor restart voltage, shown as "H xx.x"
                if (repeat_keys & KEY_MINUS && newvrestart > newvcut + VOLT_HYST_MIN) newvrestart--;
                if (repeat_keys & KEY_PLUS && newvrestart < VOLT_SETTING_MAX) newvrestart++;
                uint8_t buf[5] = {leds, c_H, 0, 0, 0};
                if (!(flashtimer & 0x08)) {
                    uint8_t num = FormatDigits(NULL, (int16_t)newvrestart, 2);
                    FormatDigits(&buf[5 - num], (int16_t)newvrestart, 2);
                    buf[3] |= ADD_DOT;
                }
                TM1620B_Update( buf );
                break;
            }
            case IDLE: {
                uint8_t buf[5] = {leds, 0, 0, 0, fahrenheit ? c_F : c_C | ADD_DOT};
                bool tenths = true; // Maybe customizable in the future?
                if (fahrenheit && temperature10 > 377) tenths = false; // Force tenths off when above 99.9F
                int16_t disptemp;
                if (tenths) {
                    disptemp = fahrenheit ? ((((temperature10 * 9) + 2) / 5) + 320) : temperature10;
                } else {
                    int16_t temperature = (temperature10 + 5) / 10;
                    disptemp = fahrenheit ? ((((temperature * 9) + 2) / 5) + 32) : temperature;
                }
                uint8_t num = FormatDigits(NULL, disptemp, tenths ? 2 : 0);
                FormatDigits(&buf[4 - num], disptemp, tenths ? 2 : 0); // Right justified
                if (tenths) buf[3] |= ADD_DOT;
                if (!on) {
                    if ((flashtimer & 0x0f) < 0xa) {
                        buf[1] = buf[2] = buf[3] = buf[4] = 0;
                    } else if (flashtimer & 0x10) {
                        buf[1] = c_o;
                        buf[2] = c_F;
                        buf[3] = c_F;
                        buf[4] = 0;
                    }
                } else if (battlow) {
                    if ((flashtimer & 0x0f) < 0xa) {
                        buf[1] = buf[2] = buf[3] = buf[4] = 0;
                    } else if (flashtimer & 0x10) {
                        buf[1] = c_b;
                        buf[2] = c_A;
                        buf[3] = buf[4] = c_t;
                    }
                } else if (error != ERR_NONE) {
                    if ((flashtimer & 0x0f) < 0xa) {
                        buf[1] = buf[2] = buf[3] = buf[4] = 0;
                    } else if (flashtimer & 0x10) {
                        buf[1] = c_E;
                        buf[2] = c_r;
                        buf[3] = 0;
                        buf[4] = hexdigits[error];
                    }
                }
                TM1620B_Update( buf );
                break;
            }
            case SET_BEGIN:
            case SET_END:
            case DISP_BEGIN:
            case DISP_END:
                break;
        }

        if (compressor_check) {
            uint8_t min = Compressor_GetMinSpeedIdx();
            uint8_t max = Compressor_GetMaxSpeedIdx();
            uint8_t speedidx = 0;
            static uint8_t fanspin = 0;
            int16_t tempdiff = (temperature10 - temp_setpoint10);
            if (comp_timer > 0) {
                comp_timer--;
                if (comp_timer == 0) compstate++;
            }
            if (fanspin > 0) fanspin--;
            switch (compstate) {
                case COMP_LOCKOUT:
                    // Make sure compressor isn't cycling too fast
                    Compressor_OnOff(false, fanspin > 0, 0); // Stopped
                    break;
                case COMP_OFF:
                    if (tempdiff >= 1 && comp_timer == 0) { // 0.1C above setpoint (which in reality is more because of slow NTC response)
                        comp_timer = 2;
                        fanspin = 2;
                    }
                    Compressor_OnOff(false, fanspin > 0, 0); // Stopped
                    break;
                case COMP_STARTING:
                    speedidx = (temp_setpoint10 > 0) ? Compressor_GetMinSpeedIdx() : Compressor_GetDefaultSpeedIdx();
                    Compressor_OnOff(true, true, speedidx);
                    if (comp_timer == 0) {
                        temp_rate_tick = 0;
                        temp_rate = 0;
                        last_temp = temperature10;
                        stall_secs = 0;
                        comp_timer = 30;
                    }
                    break;
                case COMP_RUN:
                    // Compressor stall / failed start reporting: commanded on
                    // but drawing (almost) no power for a while means the rotor
                    // never started or got stuck - back off and retry later
                    if (comppower < COMP_STALL_POWER) {
                        if (++stall_secs >= COMP_STALL_SECS) {
                            stall_secs = 0;
                            error = ERR_COMP;
                            Compressor_OnOff(false, true, 0);
                            comp_timer = ERR_RETRY_SECS;
                            compstate = COMP_LOCKOUT;
                            fanspin = 5;
                            break;
                        }
                    } else {
                        stall_secs = 0;
                        error = ERR_NONE; // Compressor runs fine again, clear any error
                    }
                    speedidx = comp_speed;
                    temp_rate_tick++;
                    if (temp_rate_tick == 60) {
                        temp_rate = temperature10 - last_temp;
                        // Because the NTC is VERY slow to react to temperature changes
                        // we need to control the rate of temperature change to avoid
                        // undershooting at higher temperatures
                        if (tempdiff > 100 && comppower < 45) { // More than 10C above, max cooling
                            speedidx = max;
                        } else if (tempdiff > 40) { // More than 4C above, try to maintain -0.5C per minute
                            if (temp_rate > -5 && speedidx < max) {
                                speedidx++;
                            } else if (temp_rate < -5 && speedidx > min) {
                                speedidx--;
                            }
                        } else { // When we get closer to the setpoint, try to maintain -0.1C per minute
                            if (temp_rate > -1 && speedidx < max) {
                                speedidx++;
                            } else if (temp_rate < -1 && speedidx > min) {
                                speedidx--;
                            }
                        }
                        temp_rate_tick = 0;
                        last_temp = temperature10;
                    }
                    if (comppower > 45 && speedidx > min) {
                        speedidx--;
                    }
                    if (tempdiff <= 0) { // at setpoint (because NTC is slow, this will decrease a bit more)
                        compstate = COMP_LOCKOUT;
                        comp_timer = 99; // 99s lockout after run
                        fanspin = 120;
                        temp_rate = 0;
                    } else {
                        Compressor_OnOff(true, true, speedidx);
                    }
                    break;
            }
            comp_speed = speedidx;
        }
        lastkeys = keys;
        flashtimer++;
    }
}
