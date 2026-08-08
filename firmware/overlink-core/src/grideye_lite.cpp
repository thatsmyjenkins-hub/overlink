#include "grideye_lite.h"

#include <WiFi.h>

#include "device_hub.h"
#include "wifi_manager.h"

struct ApHit {
  char ssid[33];
  int32_t rssi;
  bool secure;
};

static ApHit aps[16];
static int apCount = 0;
static char phase[24] = "IDLE";
static uint32_t lastScanMs = 0;

void grideyeLiteBegin() { strncpy(phase, "IDLE", sizeof(phase) - 1); }

bool grideyeLiteScanWifi(String &message) {
  strncpy(phase, "RF_SCAN", sizeof(phase) - 1);
  JsonDocument tmp;
  JsonArray arr = tmp.to<JsonArray>();
  wifiScanTo(arr);
  apCount = 0;
  for (JsonObject o : arr) {
    if (apCount >= 16) break;
    strncpy(aps[apCount].ssid, o["ssid"] | "", sizeof(aps[apCount].ssid) - 1);
    aps[apCount].rssi = o["rssi"] | -100;
    aps[apCount].secure = o["secure"] | true;
    apCount++;
  }
  lastScanMs = millis();
  strncpy(phase, "MONITOR", sizeof(phase) - 1);
  message = String("scanned ") + apCount + " APs";
  return true;
}

void grideyeLiteFillSummary(JsonObject obj) {
  JsonDocument sumDoc;
  JsonObject hub = sumDoc.to<JsonObject>();
  deviceHubFillSummary(hub);

  obj["phase"] = phase;
  obj["phaseLabel"] = phase;
  obj["wifiCount"] = apCount;
  obj["hostCount"] = hub["deviceTotal"] | 0;
  obj["deviceOnline"] = hub["deviceOnline"] | 0;
  obj["ssid"] = wifiStaSsid();
  obj["ip"] = wifiStaIp();
  obj["lastScanMs"] = lastScanMs;

  JsonArray apArr = obj["aps"].to<JsonArray>();
  for (int i = 0; i < apCount && i < 8; i++) {
    JsonObject o = apArr.add<JsonObject>();
    o["ssid"] = aps[i].ssid;
    o["rssi"] = aps[i].rssi;
    o["enc"] = aps[i].secure ? "SEC" : "OPEN";
  }

  JsonArray hosts = obj["hosts"].to<JsonArray>();
  // Reuse device registry as LAN host brief
  JsonDocument ddoc;
  JsonArray darr = ddoc.to<JsonArray>();
  deviceHubFillDevices(darr);
  int n = 0;
  for (JsonObject d : darr) {
    if (n >= 8) break;
    JsonObject o = hosts.add<JsonObject>();
    o["name"] = d["name"];
    o["ip"] = d["ip"];
    o["type"] = d["type"];
    o["online"] = d["online"];
    n++;
  }
}

void grideyeLiteLoop() {
  // optional idle refresh every 10 min when STA up
  if (wifiStaUp() && apCount == 0 && millis() > 20000 && millis() - lastScanMs > 600000) {
    String msg;
    grideyeLiteScanWifi(msg);
  }
}
