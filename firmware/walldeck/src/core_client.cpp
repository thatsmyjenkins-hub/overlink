#include "core_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

void CoreClient::begin() {
  coreIp_.fromString(CORE_FALLBACK_IP);
  lastPollMs_ = 0;
  failStreak_ = 0;
  coreOnline_ = false;
}

bool CoreClient::httpGet(const String &path, String &body, int timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = "http://" + coreIp_.toString() + path;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) return false;
  int code = http.GET();
  body = http.getString();
  http.end();
  return code == 200;
}

bool CoreClient::httpPost(const String &path, const String &json, String &body, int timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = "http://" + coreIp_.toString() + path;
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  body = http.getString();
  http.end();
  return code == 200;
}

bool CoreClient::ensureCore() {
  IPAddress resolved;
  if (WiFi.hostByName(CORE_HOSTNAME ".local", resolved) && resolved) {
    coreIp_ = resolved;
  }

  String body;
  if (!httpGet("/api/status", body, 2000)) {
    IPAddress fb;
    fb.fromString(CORE_FALLBACK_IP);
    if (coreIp_ != fb) {
      coreIp_ = fb;
      if (!httpGet("/api/status", body, 2000)) return false;
    } else {
      return false;
    }
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  if (!(doc["ok"] | false)) return false;
  const char *name = doc["grid"]["name"] | "OVERLINK";
  strlcpy(homeName_, name, sizeof(homeName_));
  return true;
}

bool CoreClient::refreshCatalog() {
  String body;
  if (httpGet("/api/zones", body, 2500)) {
    JsonDocument doc;
    if (!deserializeJson(doc, body)) {
      zoneCount_ = 0;
      JsonArray arr = doc["zones"].as<JsonArray>();
      for (JsonObject z : arr) {
        if (zoneCount_ >= MAX_ZONES) break;
        CoreZone &dst = zones_[zoneCount_++];
        strlcpy(dst.id, z["id"] | "", sizeof(dst.id));
        strlcpy(dst.name, z["name"] | dst.id, sizeof(dst.name));
      }
    }
  }

  if (httpGet("/api/scenes", body, 2500)) {
    JsonDocument doc;
    if (!deserializeJson(doc, body)) {
      sceneCount_ = 0;
      JsonArray arr = doc["scenes"].as<JsonArray>();
      for (JsonObject s : arr) {
        if (sceneCount_ >= MAX_SCENES) break;
        CoreScene &dst = scenes_[sceneCount_++];
        strlcpy(dst.id, s["id"] | "", sizeof(dst.id));
        strlcpy(dst.name, s["name"] | dst.id, sizeof(dst.name));
        strlcpy(dst.scope, s["scope"] | "zone", sizeof(dst.scope));
        strlcpy(dst.zoneId, s["zoneId"] | "", sizeof(dst.zoneId));
        dst.confirm = s["confirm"] | false;
      }
    }
  }

  if (httpGet("/api/themes", body, 2500)) {
    JsonDocument doc;
    if (!deserializeJson(doc, body)) {
      themeCount_ = 0;
      JsonArray arr = doc["themes"].as<JsonArray>();
      for (JsonObject t : arr) {
        if (themeCount_ >= 12) break;
        CoreTheme &dst = themes_[themeCount_++];
        strlcpy(dst.id, t["id"] | "", sizeof(dst.id));
        strlcpy(dst.name, t["name"] | dst.id, sizeof(dst.name));
        dst.color = t["color"] | 0x39f3ff;
      }
    }
  }

  // Zones fallback from devices if rooms.json empty
  if (zoneCount_ == 0) {
    for (size_t i = 0; i < deviceCount_; i++) {
      bool seen = false;
      for (size_t z = 0; z < zoneCount_; z++) {
        if (!strcmp(zones_[z].id, devices_[i].zoneId)) {
          seen = true;
          break;
        }
      }
      if (seen || !devices_[i].zoneId[0] || zoneCount_ >= MAX_ZONES) continue;
      CoreZone &dst = zones_[zoneCount_++];
      strlcpy(dst.id, devices_[i].zoneId, sizeof(dst.id));
      // Title-case-ish: use id
      strlcpy(dst.name, devices_[i].zoneId, sizeof(dst.name));
      if (dst.name[0] >= 'a' && dst.name[0] <= 'z') dst.name[0] -= 32;
    }
  }
  return zoneCount_ > 0 || sceneCount_ > 0;
}

bool CoreClient::refreshDevices() {
  String body;
  if (!httpGet("/api/devices", body, 2500)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  deviceCount_ = 0;
  JsonArray arr = doc["devices"].as<JsonArray>();
  // Prefer room controls (groups/AV) over individual bulbs — RAM-limited.
  auto rank = [](const char *type) -> int {
    if (!type) return 9;
    if (!strcmp(type, "hue_group")) return 0;
    if (!strcmp(type, "firetv") || !strcmp(type, "cast")) return 1;
    if (!strcmp(type, "cyberdeck") || !strcmp(type, "wled") || !strcmp(type, "wiz_bulb")) return 2;
    if (!strcmp(type, "vizio") || !strcmp(type, "sony") || !strcmp(type, "ps5")) return 3;
    if (!strcmp(type, "hue")) return 4;
    return 5;
  };
  auto take = [&](int wantRank) {
    for (JsonObject d : arr) {
      if (deviceCount_ >= MAX_DEVICES) return;
      if (rank(d["type"] | "") != wantRank) continue;
      CoreDevice &dst = devices_[deviceCount_++];
      strlcpy(dst.id, d["id"] | "", sizeof(dst.id));
      strlcpy(dst.zoneId, d["zoneId"] | "", sizeof(dst.zoneId));
      strlcpy(dst.type, d["type"] | "", sizeof(dst.type));
      strlcpy(dst.name, d["name"] | dst.id, sizeof(dst.name));
      dst.online = d["online"] | false;
    }
  };
  for (int r = 0; r <= 5; r++) take(r);
  return true;
}

void CoreClient::loop() {
  unsigned long now = millis();
  if (lastPollMs_ && now - lastPollMs_ < CORE_POLL_MS) return;
  lastPollMs_ = now;

  if (!ensureCore()) {
    failStreak_++;
    if (failStreak_ >= CORE_OFFLINE_FAILS) coreOnline_ = false;
    return;
  }

  bool okDev = refreshDevices();
  // Catalog less often: every successful online transition + first poll
  static bool catalogLoaded = false;
  if (!catalogLoaded || !coreOnline_) {
    refreshCatalog();
    catalogLoaded = zoneCount_ > 0 || sceneCount_ > 0;
  }

  if (okDev) {
    failStreak_ = 0;
    coreOnline_ = true;
  } else {
    failStreak_++;
    if (failStreak_ >= CORE_OFFLINE_FAILS) coreOnline_ = false;
  }
}

bool CoreClient::runScene(const char *id, String &message) {
  JsonDocument req;
  req["id"] = id;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/scenes/run", json, body, 15000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::runTheme(const char *id, String &message) {
  JsonDocument req;
  req["id"] = id;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/themes/run", json, body, 8000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::setDevice(const char *id, bool on, String &message, int dimming) {
  JsonDocument req;
  req["id"] = id;
  req["on"] = on;
  if (dimming >= 10) req["dimming"] = dimming;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/devices/set", json, body, 5000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::identify(const char *id, String &message) {
  JsonDocument req;
  req["id"] = id;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/devices/identify", json, body, 6000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::deckIr(const char *action, String &message) {
  JsonDocument req;
  req["action"] = action;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/deck/ir", json, body, 5000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::deckRf(const char *cmd, String &message) {
  JsonDocument req;
  req["cmd"] = cmd;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/deck/rf", json, body, 5000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::recovery(const char *action, String &message) {
  JsonDocument req;
  req["action"] = action;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/tools/recovery", json, body, 8000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::probe() {
  String body;
  return httpPost("/api/devices/probe", "{}", body, 8000);
}

bool CoreClient::avApp(const char *id, String &message) {
  JsonDocument req;
  req["id"] = id;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/av/app", json, body, 12000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::avVol(int delta, int &levelOut, String &message) {
  JsonDocument req;
  req["delta"] = delta;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/av/vol", json, body, 5000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  levelOut = doc["level"] | -1;
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::avWatch(String &message) {
  String body;
  if (!httpPost("/api/av/watch", "{}", body, 10000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::avInput(const char *target, String &message) {
  JsonDocument req;
  req["target"] = target;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/av/input", json, body, 10000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::fetchSummary(int &online, int &total, char *sceneTag, size_t tagLen,
                              bool &deckOnline) {
  String body;
  if (!httpGet("/api/home/summary", body, 3000)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObject sum = doc["summary"];
  online = sum["deviceOnline"] | 0;
  total = sum["deviceTotal"] | 0;
  deckOnline = sum["deckOnline"] | false;
  if (sceneTag && tagLen) {
    strlcpy(sceneTag, sum["lastSceneTag"] | "—", tagLen);
  }
  const char *hn = doc["gridName"] | "OVERLINK";
  strlcpy(homeName_, hn, sizeof(homeName_));
  return true;
}

bool CoreClient::fetchGraceState(char *gameName, size_t gameLen, char *screen, size_t screenLen,
                                 char *prompt, size_t promptLen, char *detail, size_t detailLen,
                                 char *team, size_t teamLen, int &score0, int &score1,
                                 int &remainSec, uint32_t *updatedMs) {
  String body;
  if (!httpGet("/api/games/grace/state", body, 1200)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  if (gameName && gameLen) strlcpy(gameName, doc["gameName"] | "Grace", gameLen);
  if (screen && screenLen) strlcpy(screen, doc["screen"] | "menu", screenLen);
  if (prompt && promptLen) strlcpy(prompt, doc["prompt"] | "", promptLen);
  if (detail && detailLen) strlcpy(detail, doc["detail"] | "", detailLen);
  if (team && teamLen) strlcpy(team, doc["teamLabel"] | "", teamLen);
  score0 = doc["scores0"] | 0;
  score1 = doc["scores1"] | 0;
  remainSec = doc["remainSec"] | doc["timerSec"] | 0;
  if (updatedMs) *updatedMs = doc["updatedMs"] | 0;
  return doc["ok"] | true;
}

bool CoreClient::fetchWledState(bool &on, int &bri, int &fx, char *name, size_t nameLen) {
  String body;
  if (!httpGet("/api/wled/state", body, 2500)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  on = doc["on"] | false;
  bri = doc["bri"] | 0;
  fx = doc["fx"] | 0;
  if (name && nameLen) strlcpy(name, doc["name"] | "WLED", nameLen);
  return doc["ok"] | doc["online"] | false;
}

bool CoreClient::wledSetJson(const char *json, String &message) {
  if (!json) {
    message = "no body";
    return false;
  }
  String body;
  if (!httpPost("/api/wled/set", String(json), body, 4000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::partySweep(String &message, int &mdnsCount, int &bleCount) {
  mdnsCount = 0;
  bleCount = 0;
  String body;
  if (!httpPost("/api/party/sweep", "{}", body, 12000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  mdnsCount = doc["mdnsCount"] | 0;
  bleCount = doc["bleCount"] | 0;
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::partyBleStart(const char *message, bool cycle, String &outMsg) {
  JsonDocument req;
  req["message"] = message && message[0] ? message : "OVERLINK SAYS HI";
  req["cycle"] = cycle;
  // Empty lines → Core uses its funny defaults (+ custom message first)
  req["lines"].to<JsonArray>();
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/party/ble/start", json, body, 5000)) {
    outMsg = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    outMsg = "bad response";
    return false;
  }
  outMsg = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::partyBleStop(String &message) {
  String body;
  if (!httpPost("/api/party/ble/stop", "{}", body, 4000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::partyStampede(String &message) {
  String body;
  if (!httpPost("/api/party/stampede", "{}", body, 20000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::partyCast(const char *message, String &outMsg) {
  JsonDocument req;
  req["message"] = message && message[0] ? message : "OVERLINK ONLINE";
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/party/cast", json, body, 15000)) {
    outMsg = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    outMsg = "bad response";
    return false;
  }
  outMsg = doc["message"] | "";
  return doc["ok"] | false;
}

bool CoreClient::partyFindPrinters(String &message, int &count) {
  count = 0;
  String body;
  if (!httpGet("/api/party/printers", body, 12000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  JsonArray arr = doc["printers"].as<JsonArray>();
  count = arr.isNull() ? 0 : (int)arr.size();
  if (doc["message"].is<const char *>())
    message = doc["message"].as<const char *>();
  else
    message = String("printers ") + count;
  return true;
}

bool CoreClient::partyPrintFirst(const char *message, String &outMsg) {
  String body;
  if (!httpGet("/api/party/printers", body, 12000)) {
    outMsg = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    outMsg = "bad response";
    return false;
  }
  JsonArray arr = doc["printers"].as<JsonArray>();
  if (arr.isNull() || !arr.size()) {
    outMsg = "no printers";
    return false;
  }
  JsonObject p = arr[0].as<JsonObject>();
  const char *ip = p["ip"] | "";
  uint16_t port = (uint16_t)(p["port"] | 9100);
  if (!ip[0]) {
    outMsg = "bad printer";
    return false;
  }
  JsonDocument req;
  req["ip"] = ip;
  req["port"] = port;
  req["message"] = message && message[0] ? message : "Hello from Overlink";
  String json;
  serializeJson(req, json);
  String resp;
  if (!httpPost("/api/party/print", json, resp, 10000)) {
    outMsg = "core unreachable";
    return false;
  }
  JsonDocument out;
  if (deserializeJson(out, resp)) {
    outMsg = "bad response";
    return false;
  }
  outMsg = out["message"] | "";
  return out["ok"] | false;
}

bool CoreClient::partyStatus(bool &advertising, bool &cycle, char *current, size_t currentLen) {
  String body;
  if (!httpGet("/api/party/status", body, 3000)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  advertising = doc["ble"]["advertising"] | false;
  cycle = doc["ble"]["cycle"] | false;
  if (current && currentLen)
    strlcpy(current, doc["ble"]["current"] | "", currentLen);
  return true;
}

bool CoreClient::avKey(const char *name, String &message) {
  JsonDocument req;
  req["name"] = name;
  String json;
  serializeJson(req, json);
  String body;
  if (!httpPost("/api/av/key", json, body, 6000)) {
    message = "core unreachable";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad response";
    return false;
  }
  message = doc["message"] | "";
  return doc["ok"] | false;
}

uint32_t CoreClient::stateHash() const {
  uint32_t h = coreOnline_ ? 1 : 0;
  h = h * 33 + (uint32_t)deviceCount_;
  h = h * 33 + (uint32_t)zoneCount_;
  h = h * 33 + (uint32_t)sceneCount_;
  for (size_t i = 0; i < deviceCount_; i++) {
    h = h * 33 + (devices_[i].online ? 1 : 0);
    h = h * 33 + (uint8_t)devices_[i].id[0];
  }
  return h;
}
