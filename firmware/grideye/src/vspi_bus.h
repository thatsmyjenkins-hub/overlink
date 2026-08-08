#pragma once

#include <SPI.h>

// Shared VSPI bus for the XPT2046 touch controller and the microSD card.
//
// The ESP32 only exposes two general-purpose SPI peripherals: the display owns
// HSPI, leaving VSPI for both touch (pins 25/39/32/33) and SD (18/19/23/5).
// They sit on different pins but the same peripheral, so they can't both own it
// at once. Because both are accessed sequentially on the main thread, we fan the
// clock + MOSI outputs out to both pin sets and switch only the single MISO
// input line between them. Call vspiSelectTouch()/vspiSelectSd() before each
// subsystem's transactions. This keeps the existing touch calibration valid.

extern SPIClass vspiBus;

void vspiBusInit();      // begin VSPI on touch pins + fan clock/MOSI to SD pins
void vspiSelectTouch();  // route MISO input from the touch DOUT pin
void vspiSelectSd();     // route MISO input from the SD MISO pin
