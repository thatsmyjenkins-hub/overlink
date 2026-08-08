#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void relayClientBegin();
void relayClientLoop();
void relayClientFillStatus(JsonObject out);
bool relayClientConfigure(JsonVariantConst cfg, String &message);
bool relayClientSetEnabled(bool enabled, String &message);
bool relayClientEnroll(const char *code, String &message);
