#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void wifiNetBegin();
void wifiNetLoop();

bool wifiHasCreds();
bool wifiStaConnected();
String wifiStaIp();
String wifiStaSsid();
String wifiApSsid();
String wifiApIp();
String wifiHostname();
String wifiModeLabel();

void wifiFillStatus(JsonObject obj);
void wifiScanTo(JsonArray arr);
bool wifiSaveAndReboot(const String &ssid, const String &pass, String &detail);
bool wifiClearCreds(String &detail);
bool wifiIsCaptiveHost(const String &host);
