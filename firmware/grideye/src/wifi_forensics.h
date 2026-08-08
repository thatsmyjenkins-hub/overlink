#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "intel.h"

namespace WifiForensics {

bool parseBssid(const char* str, uint8_t mac[6]);
bool bssidEqual(const uint8_t a[6], const uint8_t b[6]);
int findApIndex(const IntelWifi* aps, int count, const char* bssid);

void appendApJson(const IntelWifi& ap, JsonObject& o);
void buildApDetail(const IntelWifi& target, const IntelWifi* aps, int apCount,
                   JsonDocument& doc);
bool runApAction(const char* action, IntelScanner& scanner,
                 const IntelWifi* aps, int apCount, int targetIdx,
                 JsonDocument& result);

} // namespace WifiForensics
