#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void deviceHubBegin();
void deviceHubLoop();
bool deviceHubLoadFromSd();
bool deviceHubReplaceDevicesJson(const String &json, String &message);
bool deviceHubAddDevice(JsonVariantConst device, String &message);
bool deviceHubUpdateDevice(const char *id, JsonVariantConst patch, String &message);
bool deviceHubRemoveDevice(const char *id, String &message);
bool deviceHubHueSync(String &message);
bool deviceHubHuePair(const char *bridgeIp, String &message);  // press Hue link button first
void deviceHubFillDiscover(JsonObject out);  // suggestions + registered summary
void deviceHubInvalidateHueCreds();  // call on grid switch
void deviceHubFillDevices(JsonArray arr);
void deviceHubFillThemes(JsonArray arr);
bool deviceHubRunScene(const String &sceneId, String &message);
bool deviceHubRunTheme(const String &themeId, String &message);
bool deviceHubSetDevice(const String &id, bool on, String &message, int dimming = -1,
                        int bri = -1);
bool deviceHubIdentify(const String &id, String &message);
void deviceHubRefreshOnline();
bool deviceHubDeckVizio(const char *action, String &message);
bool deviceHubDeckIrReplay(String &message);
bool deviceHubDeckRf(const char *cmd, float mhz, String &message);
bool deviceHubRecovery(const String &action, String &message);
void deviceHubRunLabSmoke(JsonObject out);

// AV Now-panel (Fire / Vizio / Sony)
bool deviceHubAvApp(const char *appId, String &message);
bool deviceHubAvVol(int delta, int &levelOut, String &message);
bool deviceHubAvWatch(String &message);
bool deviceHubAvInput(const char *target, String &message);
bool deviceHubAvKey(const char *name, String &message);

// Dashboard summary
void deviceHubFillSummary(JsonObject obj);
const char *deviceHubLastSceneId();
const char *deviceHubLastSceneTag();

// WLED control (HTTP JSON API proxy)
void deviceHubFillWledDevices(JsonArray arr);
bool deviceHubWledFillState(const char *id, JsonObject out, String &message);
bool deviceHubWledSet(JsonVariantConst patch, String &message);

// Camera snapshot proxy (JPEG into bytes; returns false on fail)
bool deviceHubCameraSnapshot(const char *id, String &contentType, String &bytes, String &message);
