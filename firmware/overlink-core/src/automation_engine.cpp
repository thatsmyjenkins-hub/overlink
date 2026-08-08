#include "automation_engine.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <time.h>

#include "device_hub.h"
#include "grid_store.h"
#include "wifi_manager.h"

struct AutoRule {
  char id[24];
  char name[40];
  bool enabled;
  char triggerType[12];  // time
  int hour;
  int minute;
  char actionType[12];  // scene
  char actionId[24];
  int lastFiredDay;  // yday to debounce
};

static AutoRule rules[8];
static int ruleCount = 0;
static bool ntpOk = false;
static uint32_t lastCheckMs = 0;

static bool loadFromSd() {
  ruleCount = 0;
  String path = gridStorePath("automations.json");
  if (!SD_MMC.exists(path)) return false;
  File f = SD_MMC.open(path, "r");
  if (!f) return false;
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return false;
  }
  f.close();
  JsonArray arr = doc["automations"].as<JsonArray>();
  for (JsonObject o : arr) {
    if (ruleCount >= 8) break;
    AutoRule &r = rules[ruleCount];
    memset(&r, 0, sizeof(r));
    strncpy(r.id, o["id"] | "", sizeof(r.id) - 1);
    strncpy(r.name, o["name"] | r.id, sizeof(r.name) - 1);
    r.enabled = o["enabled"] | true;
    JsonObject t = o["trigger"];
    strncpy(r.triggerType, t["type"] | "time", sizeof(r.triggerType) - 1);
    r.hour = t["hour"] | 0;
    r.minute = t["minute"] | 0;
    JsonObject a = o["action"];
    strncpy(r.actionType, a["type"] | "scene", sizeof(r.actionType) - 1);
    strncpy(r.actionId, a["id"] | "", sizeof(r.actionId) - 1);
    r.lastFiredDay = -1;
    if (r.id[0]) ruleCount++;
  }
  return true;
}

static bool saveToSd() {
  JsonDocument doc;
  JsonArray arr = doc["automations"].to<JsonArray>();
  for (int i = 0; i < ruleCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = rules[i].id;
    o["name"] = rules[i].name;
    o["enabled"] = rules[i].enabled;
    JsonObject t = o["trigger"].to<JsonObject>();
    t["type"] = rules[i].triggerType;
    t["hour"] = rules[i].hour;
    t["minute"] = rules[i].minute;
    JsonObject a = o["action"].to<JsonObject>();
    a["type"] = rules[i].actionType;
    a["id"] = rules[i].actionId;
  }
  String path = gridStorePath("automations.json");
  File f = SD_MMC.open(path, "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

static void seedTemplatesIfEmpty() {
  if (ruleCount > 0) return;
  // Starter templates (disabled until user enables)
  AutoRule r;
  memset(&r, 0, sizeof(r));
  strncpy(r.id, "midnight-off", sizeof(r.id) - 1);
  strncpy(r.name, "House Off @ 00:30", sizeof(r.name) - 1);
  r.enabled = false;
  strncpy(r.triggerType, "time", sizeof(r.triggerType) - 1);
  r.hour = 0;
  r.minute = 30;
  strncpy(r.actionType, "scene", sizeof(r.actionType) - 1);
  strncpy(r.actionId, "house-off", sizeof(r.actionId) - 1);
  r.lastFiredDay = -1;
  rules[ruleCount++] = r;

  memset(&r, 0, sizeof(r));
  strncpy(r.id, "bed-2300", sizeof(r.id) - 1);
  strncpy(r.name, "Bed @ 23:00", sizeof(r.name) - 1);
  r.enabled = false;
  strncpy(r.triggerType, "time", sizeof(r.triggerType) - 1);
  r.hour = 23;
  r.minute = 0;
  strncpy(r.actionType, "scene", sizeof(r.actionType) - 1);
  strncpy(r.actionId, "bed", sizeof(r.actionId) - 1);
  r.lastFiredDay = -1;
  rules[ruleCount++] = r;
  saveToSd();
}

void automationEnsureNtp() {
  if (ntpOk || !wifiStaUp()) return;
  configTime(-6 * 3600, 3600, "pool.ntp.org", "time.nist.gov");
  ntpOk = true;
  Serial.println("[AUTO] NTP requested (America/Chicago approx)");
}

void automationBegin() {
  loadFromSd();
  seedTemplatesIfEmpty();
  automationEnsureNtp();
}

void automationFillList(JsonArray arr) {
  for (int i = 0; i < ruleCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = rules[i].id;
    o["name"] = rules[i].name;
    o["enabled"] = rules[i].enabled;
    o["hour"] = rules[i].hour;
    o["minute"] = rules[i].minute;
    o["actionId"] = rules[i].actionId;
  }
}

bool automationSetEnabled(const char *id, bool enabled, String &message) {
  for (int i = 0; i < ruleCount; i++) {
    if (!strcmp(rules[i].id, id)) {
      rules[i].enabled = enabled;
      saveToSd();
      message = enabled ? "enabled" : "disabled";
      return true;
    }
  }
  message = "not found";
  return false;
}

bool automationRunNow(const char *id, String &message) {
  for (int i = 0; i < ruleCount; i++) {
    if (!strcmp(rules[i].id, id)) {
      if (!strcmp(rules[i].actionType, "scene")) {
        return deviceHubRunScene(rules[i].actionId, message);
      }
      message = "bad action";
      return false;
    }
  }
  message = "not found";
  return false;
}

void automationLoop() {
  if (!wifiStaUp()) return;
  automationEnsureNtp();
  if (millis() - lastCheckMs < 15000) return;
  lastCheckMs = millis();

  time_t now = time(nullptr);
  if (now < 1700000000) return;  // not synced yet
  struct tm t;
  localtime_r(&now, &t);
  for (int i = 0; i < ruleCount; i++) {
    AutoRule &r = rules[i];
    if (!r.enabled) continue;
    if (strcmp(r.triggerType, "time") != 0) continue;
    if (t.tm_hour == r.hour && t.tm_min == r.minute && r.lastFiredDay != t.tm_yday) {
      String msg;
      if (!strcmp(r.actionType, "scene")) {
        deviceHubRunScene(r.actionId, msg);
        Serial.printf("[AUTO] fired %s → %s\n", r.id, msg.c_str());
      }
      r.lastFiredDay = t.tm_yday;
    }
  }
}
