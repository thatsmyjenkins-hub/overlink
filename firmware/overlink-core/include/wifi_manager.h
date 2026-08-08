#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void wifiManagerBegin();
void wifiManagerLoop();

bool wifiStaUp();
bool wifiApUp();
String wifiStaIp();
String wifiStaSsid();
String wifiApSsid();
String wifiModeLabel();

bool wifiSaveNetwork(const String &ssid, const String &pass);
void wifiPreferSsid(const String &ssid);
void wifiClearNetworks();
void wifiFillStatus(JsonObject obj);
void wifiScanTo(JsonArray arr);
