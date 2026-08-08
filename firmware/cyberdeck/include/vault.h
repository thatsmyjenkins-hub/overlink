#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void vaultBegin();
void vaultList(JsonArray arr);
bool vaultAdd(const String &name, const String &kind, const String &payload,
              String &detail);
bool vaultDelete(int id, String &detail);
bool vaultReplay(int id, String &detail);
int vaultCount();
