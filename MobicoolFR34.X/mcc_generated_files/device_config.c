/**
  @Generated PIC10 / PIC12 / PIC16 / PIC18 MCUs Source File

  @Company:
    Microchip Technology Inc.

  @File Name:
    mcc.c

  @Summary:
    This is the mcc.c file generated using PIC10 / PIC12 / PIC16 / PIC18 MCUs

  @Description:
    This header file provides implementations for driver APIs for all modules selected in the GUI.
    Generation Information :
        Product Revision  :  PIC10 / PIC12 / PIC16 / PIC18 MCUs - 1.65.2
        Device            :  PIC16F1829
        Driver Version    :  2.00
    The generated drivers are tested against the following:
        Compiler          :  XC8 1.45 or later
        MPLAB             :  MPLAB X 4.15
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

// Configuration bits for PIC16F18346 (ported from PIC16F1829)

// CONFIG1
#pragma config FEXTOSC = OFF    // External Oscillator not enabled
#pragma config RSTOSC = HFINT1    // Power-up default HFINTOSC 1MHz (matches _XTAL_FREQ, no OSC setup needed)
#pragma config CLKOUTEN = OFF    // CLKOUT function is disabled
#pragma config CSWEN = ON    // Writing to NOSC and NDIV is allowed
#pragma config FCMEN = ON    // Fail-Safe Clock Monitor is enabled

// CONFIG2
#pragma config MCLRE = ON    // MCLR/VPP pin function is MCLR
#pragma config PWRTE = OFF    // PWRT disabled
#pragma config WDTE = OFF    // WDT disabled
#pragma config LPBOREN = OFF    // ULPBOR disabled
#pragma config BOREN = ON    // Brown-out Reset enabled
#pragma config BORV = LOW    // Brown-out voltage (Vbor) set to 2.45V
#pragma config PPS1WAY = OFF    // PPSLOCK can be set and cleared repeatedly
#pragma config STVREN = ON    // Stack Overflow or Underflow will cause a Reset

// CONFIG3
#pragma config WRT = OFF    // Write protection off
#pragma config LVP = ON    // Low Voltage programming enabled (keep ON so the MPLAB Snap keeps working!)

// CONFIG4
#pragma config CP = OFF    // Program memory code protection disabled
#pragma config CPD = OFF    // Data NVM code protection disabled
