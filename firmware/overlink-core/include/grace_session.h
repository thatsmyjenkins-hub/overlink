#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void graceSessionBegin();
void graceSessionFill(JsonObject obj);
bool graceSessionUpdate(JsonVariantConst patch, String &message);
