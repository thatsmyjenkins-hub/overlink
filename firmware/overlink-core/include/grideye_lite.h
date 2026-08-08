#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void grideyeLiteBegin();
void grideyeLiteLoop();
void grideyeLiteFillSummary(JsonObject obj);
bool grideyeLiteScanWifi(String &message);
