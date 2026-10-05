#pragma once

#include <Arduino.h>

// Always-on BLE fallback (Nordic UART). Phone can STATUS / SCAN / JOIN
// when SoftAP isn't visible. Billboard names reuse this advertiser.
void bleLinkBegin();
void bleLinkLoop();
void bleLinkEnsure();
void bleLinkAdvertise(const char *name);
void bleLinkPause();   // stop NUS adv (party popups / scan)
void bleLinkResume();  // restore Overlink NUS adv
