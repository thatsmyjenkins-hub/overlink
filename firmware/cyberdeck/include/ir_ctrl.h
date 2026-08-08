#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void irBegin();
void irLoop();
bool irLoopbackQa(String &detail);
bool irSendNec(uint32_t code, uint8_t frames = 1);
bool irSendSony(uint64_t data, uint16_t nbits, uint8_t frames = 1);
bool irSendVizio(const String &action, String &detail);
bool irReplayLast(String &detail);
void irGetStatus(JsonObject obj);
void irGetLastCapture(JsonObject obj);
bool irHasCapture();
void irClearLiveLog();
String irPopLiveLine();  // empty if none
size_t irLiveCount();
