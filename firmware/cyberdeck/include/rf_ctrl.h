#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void rfBegin();
void rfLoop();
bool rfOk();
String rfLastError();
bool rfSetFrequency(float mhz, String &detail);
bool rfStartSniff(String &detail);
bool rfStopSniff(String &detail);
bool rfReplayLast(String &detail);
void rfGetStatus(JsonObject obj);
void rfGetLastPacket(JsonObject obj);
bool rfSelfTest(String &detail);
// Sample RSSI for ms milliseconds; writes peak/min/avg into obj.
bool rfWatchRssi(uint32_t ms, JsonObject obj);
bool rfTransmitHex(const String &hex, float mhz, String &detail);
// payload: RF:<mhz>:<hex>  e.g. RF:390.000:AABBCC
bool rfTransmitVaultPayload(const String &payload, String &detail);
float rfFreqMhz();
bool rfHasPacket();
String rfLastPacketHexCompact();  // no spaces
String rfVaultPayloadFromLast();  // RF:<mhz>:<hex> or empty
