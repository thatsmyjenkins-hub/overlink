#include "relay_client.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "device_hub.h"
#include "grid_store.h"

static Preferences prefs;
static bool enabled = false;
static String relayUrl;   // e.g. http://relay.example.com:8787
static String enrollToken;
static String sessionId;
static uint32_t lastBeatMs = 0;
static String lastError;
static bool connected = false;

static String configPath() {
  return String("/homes/") + gridStoreActiveId() + "/vault/relay.json";
}

static void loadConfig() {
  prefs.begin("ol-relay", true);
  enabled = prefs.getBool("en", false);
  relayUrl = prefs.getString("url", "");
  enrollToken = prefs.getString("tok", "");
  prefs.end();
  String path = configPath();
  if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path, "r");
    JsonDocument doc;
    if (f && !deserializeJson(doc, f)) {
      if (doc["url"].is<const char *>()) relayUrl = doc["url"].as<const char *>();
      if (doc["token"].is<const char *>()) enrollToken = doc["token"].as<const char *>();
      if (!doc["enabled"].isNull()) enabled = doc["enabled"] | enabled;
    }
    if (f) f.close();
  }
}

static bool saveConfig(String &message) {
  prefs.begin("ol-relay", false);
  prefs.putBool("en", enabled);
  prefs.putString("url", relayUrl);
  prefs.putString("tok", enrollToken);
  prefs.end();
  String dir = String("/homes/") + gridStoreActiveId() + "/vault";
  if (!SD_MMC.exists(dir)) SD_MMC.mkdir(dir.c_str());
  File f = SD_MMC.open(configPath(), "w");
  if (!f) {
    message = "relay vault write fail";
    return false;
  }
  JsonDocument doc;
  doc["url"] = relayUrl;
  doc["token"] = enrollToken;
  doc["enabled"] = enabled;
  serializeJsonPretty(doc, f);
  f.close();
  message = "relay saved";
  return true;
}

static String normalizeBase() {
  String u = relayUrl;
  while (u.endsWith("/")) u.remove(u.length() - 1);
  return u;
}

static bool httpJson(const String &method, const String &path, const String &bodyIn, JsonDocument &out,
                     int timeoutMs = 4000) {
  String base = normalizeBase();
  if (!base.length()) {
    lastError = "no relay url";
    return false;
  }
  String url = base + path;
  bool tls = url.startsWith("https://");
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(2500);
  WiFiClient plain;
  WiFiClientSecure secure;
  if (tls) {
    secure.setInsecure();
    if (!http.begin(secure, url)) {
      lastError = "begin fail";
      return false;
    }
  } else {
    if (!http.begin(plain, url)) {
      lastError = "begin fail";
      return false;
    }
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Overlink-Token", enrollToken);
  http.addHeader("X-Overlink-Grid", gridStoreActiveId());
  int code = -1;
  if (method == "GET")
    code = http.GET();
  else if (method == "POST")
    code = http.POST(bodyIn);
  else
    code = http.sendRequest(method.c_str(), bodyIn);
  String body = code > 0 ? http.getString() : "";
  http.end();
  if (code < 200 || code >= 300) {
    lastError = String("http ") + code;
    connected = false;
    return false;
  }
  if (deserializeJson(out, body)) {
    lastError = "parse fail";
    return false;
  }
  connected = true;
  lastError = "";
  return true;
}

static void handleRemoteCommand(JsonObject cmd) {
  const char *op = cmd["op"] | "";
  String msg;
  JsonDocument result;
  result["id"] = cmd["id"] | "";
  result["ok"] = false;
  if (!strcmp(op, "device.set")) {
    bool on = cmd["on"] | false;
    int dim = cmd["dimming"] | -1;
    bool ok = deviceHubSetDevice(cmd["deviceId"] | "", on, msg, dim, -1);
    result["ok"] = ok;
    result["message"] = msg;
  } else if (!strcmp(op, "scene.run")) {
    bool ok = deviceHubRunScene(cmd["sceneId"] | "", msg);
    result["ok"] = ok;
    result["message"] = msg;
  } else if (!strcmp(op, "ping")) {
    result["ok"] = true;
    result["message"] = "pong";
    result["grid"] = gridStoreActiveId();
  } else {
    result["message"] = "unknown op";
  }
  String payload;
  serializeJson(result, payload);
  JsonDocument ignore;
  httpJson("POST", String("/v1/grids/") + gridStoreActiveId() + "/result", payload, ignore, 3000);
}

void relayClientBegin() {
  loadConfig();
  lastBeatMs = 0;
  sessionId = "";
  connected = false;
}

void relayClientFillStatus(JsonObject out) {
  out["enabled"] = enabled;
  out["url"] = relayUrl;
  out["connected"] = connected && enabled;
  out["hasToken"] = enrollToken.length() > 0;
  out["grid"] = gridStoreActiveId();
  out["error"] = lastError;
  out["session"] = sessionId;
}

bool relayClientConfigure(JsonVariantConst cfg, String &message) {
  if (cfg["url"].is<const char *>()) relayUrl = cfg["url"].as<const char *>();
  if (cfg["token"].is<const char *>()) enrollToken = cfg["token"].as<const char *>();
  if (!cfg["enabled"].isNull()) enabled = cfg["enabled"] | false;
  return saveConfig(message);
}

bool relayClientSetEnabled(bool on, String &message) {
  enabled = on;
  if (!on) {
    connected = false;
    sessionId = "";
  }
  return saveConfig(message);
}

bool relayClientEnroll(const char *code, String &message) {
  if (!code || !code[0]) {
    message = "code required";
    return false;
  }
  if (!relayUrl.length()) {
    message = "set relay url first";
    return false;
  }
  JsonDocument req;
  req["code"] = code;
  req["gridId"] = gridStoreActiveId();
  req["name"] = gridStoreActiveId();
  String body;
  serializeJson(req, body);
  JsonDocument resp;
  if (!httpJson("POST", "/v1/enroll", body, resp, 5000)) {
    message = lastError.length() ? lastError : "enroll fail";
    return false;
  }
  const char *tok = resp["token"] | "";
  if (!tok[0]) {
    message = "no token";
    return false;
  }
  enrollToken = tok;
  enabled = true;
  if (!saveConfig(message)) return false;
  message = "enrolled";
  return true;
}

void relayClientLoop() {
  if (!enabled || !WiFi.isConnected() || !relayUrl.length() || !enrollToken.length()) return;
  if (millis() - lastBeatMs < 2500) return;
  lastBeatMs = millis();

  JsonDocument beat;
  beat["gridId"] = gridStoreActiveId();
  beat["ip"] = WiFi.localIP().toString();
  beat["rssi"] = WiFi.RSSI();
  String body;
  serializeJson(beat, body);
  JsonDocument resp;
  if (!httpJson("POST", String("/v1/grids/") + gridStoreActiveId() + "/heartbeat", body, resp, 3500)) {
    return;
  }
  sessionId = resp["session"] | sessionId;

  // Pull pending commands (dial-out)
  JsonDocument pull;
  if (!httpJson("GET", String("/v1/grids/") + gridStoreActiveId() + "/pull", "", pull, 4000)) return;
  if (!pull["commands"].is<JsonArray>()) return;
  for (JsonObject cmd : pull["commands"].as<JsonArray>()) {
    handleRemoteCommand(cmd);
  }
}
