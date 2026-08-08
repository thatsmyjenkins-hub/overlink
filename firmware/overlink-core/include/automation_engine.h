#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void automationBegin();
void automationLoop();
void automationFillList(JsonArray arr);
bool automationSetEnabled(const char *id, bool enabled, String &message);
bool automationRunNow(const char *id, String &message);
void automationEnsureNtp();
