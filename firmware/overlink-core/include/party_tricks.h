#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void partyTricksBegin();
void partyTricksLoop();

// Sweep: mDNS services + BLE advertisements → JSON
bool partyTricksSweep(JsonObject out, String &message);

// BLE billboard / name-cycle "spam" (appears in Bluetooth scan lists)
bool partyTricksBleStart(JsonVariantConst cfg, String &message);
bool partyTricksBleStop(String &message);
void partyTricksBleFillStatus(JsonObject out);

// Stampede: identify/blink every light-like device
bool partyTricksStampede(String &message);

// Cast stinger to Chromecast (custom message → big on-TV image)
bool partyTricksCastStinger(JsonVariantConst cfg, String &message);

// Printers: discover + print plain text (JetDirect 9100 + IPP)
bool partyTricksFindPrinters(JsonArray out, String &message);
bool partyTricksPrint(JsonVariantConst cfg, String &message);

void partyTricksFillStatus(JsonObject out);
