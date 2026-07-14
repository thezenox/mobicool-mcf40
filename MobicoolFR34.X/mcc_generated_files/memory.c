/**
  MEMORY Generated Driver File

  @Company
    Microchip Technology Inc.

  @File Name
    memory.c

  @Summary
    Data EEPROM access for the PIC16F18346 (ported from the PIC16F1829
    EECON-based driver; the 16F18346 accesses its 256 byte data EEPROM
    through the NVM registers at address 0x7000 with NVMREGS=1).

  @Description
    This source file provides the DATAEE read/write APIs used to persist
    the cooler settings. The unused FLASH_* self-write functions from the
    original MCC driver were dropped in the port.
*/

/*
    (c) 2018 Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip software and any
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party
    license terms applicable to your use of third party software (including open source software) that
    may accompany Microchip software.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
    FOR A PARTICULAR PURPOSE.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
    SOFTWARE.
*/

/**
  Section: Included Files
*/

#include <xc.h>
#include "memory.h"

#define EEPROM_BASE_HIGH (0x70) // Data EEPROM lives at 0x7000-0x70FF in NVM space

/**
  Section: Data EEPROM Module APIs
*/

void DATAEE_WriteByte(uint8_t bAdd, uint8_t bData)
{
    uint8_t GIEBitValue = INTCONbits.GIE;

    NVMADRL = bAdd;                 // Data EEPROM offset to write
    NVMADRH = EEPROM_BASE_HIGH;
    NVMDATL = bData;                // Data value to write
    NVMCON1bits.NVMREGS = 1;        // Point to DATA EEPROM
    NVMCON1bits.WREN = 1;           // Enable writes

    INTCONbits.GIE = 0;     // Disable INTs
    NVMCON2 = 0x55;
    NVMCON2 = 0xAA;
    NVMCON1bits.WR = 1;     // Set WR bit to begin write
    // Wait for write to complete
    while (NVMCON1bits.WR)
    {
    }

    NVMCON1bits.WREN = 0;   // Disable writes
    INTCONbits.GIE = GIEBitValue;
}

uint8_t DATAEE_ReadByte(uint8_t bAdd)
{
    NVMADRL = bAdd;                 // Data EEPROM offset to read
    NVMADRH = EEPROM_BASE_HIGH;
    NVMCON1bits.NVMREGS = 1;        // Point to DATA EEPROM
    NVMCON1bits.RD = 1;             // Initiate read
    NOP();  // NOPs may be required for latency at high frequencies
    NOP();

    return (NVMDATL);
}
/**
 End of File
*/
