#pragma once

#ifndef IR_RX_PIN
#define IR_RX_PIN 14
#endif
#ifndef IR_TX_PIN
#define IR_TX_PIN 4
#endif
#ifndef CC1101_CS
#define CC1101_CS 5
#endif
#ifndef CC1101_GDO0
#define CC1101_GDO0 26
#endif
#ifndef CC1101_GDO2
#define CC1101_GDO2 27
#endif

// ESP32 VSPI defaults used by RadioLib
static const int PIN_SCK = 18;
static const int PIN_MISO = 19;
static const int PIN_MOSI = 23;
