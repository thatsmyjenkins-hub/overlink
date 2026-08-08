#include "connector_store.h"

#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "device_hub.h"
#include "grid_store.h"

static String connectorsPath() {
  return String("/homes/") + gridStoreActiveId() + "/connectors/index.json";
}
static String vaultPath(const char *id) {
  return String("/homes/") + gridStoreActiveId() + "/vault/" + id + ".json";
}

void connectorStoreBegin() {
  String base = String("/homes/") + gridStoreActiveId();
  String cdir = base + "/connectors";
  String vdir = base + "/vault";
  if (!SD_MMC.exists(cdir)) SD_MMC.mkdir(cdir.c_str());
  if (!SD_MMC.exists(vdir)) SD_MMC.mkdir(vdir.c_str());
  if (!SD_MMC.exists(connectorsPath())) {
    File f = SD_MMC.open(connectorsPath(), "w");
    if (f) {
      f.print("{\"connectors\":[]}");
      f.close();
    }
  }
}

void connectorStoreFill(JsonArray arr) {
  String path = connectorsPath();
  if (!SD_MMC.exists(path)) return;
  File f = SD_MMC.open(path, "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return;
  }
  f.close();
  for (JsonObject c : doc["connectors"].as<JsonArray>()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = c["id"] | "";
    o["name"] = c["name"] | o["id"];
    o["transport"] = c["transport"] | "lan";
    o["type"] = c["type"] | "";
    o["enabled"] = c["enabled"] | true;
    o["hasSecret"] = SD_MMC.exists(vaultPath(c["id"] | ""));
    if (c["baseUrl"].is<const char *>()) o["baseUrl"] = c["baseUrl"];
  }
}

bool connectorStoreUpsert(JsonVariantConst connector, String &message) {
  const char *id = connector["id"] | "";
  if (!id[0]) {
    message = "id required";
    return false;
  }
  connectorStoreBegin();
  String path = connectorsPath();
  JsonDocument doc;
  if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path, "r");
    deserializeJson(doc, f);
    f.close();
  } else {
    doc["connectors"].to<JsonArray>();
  }
  JsonArray arr = doc["connectors"].as<JsonArray>();
  JsonObject found;
  for (JsonObject c : arr) {
    if (!strcmp(c["id"] | "", id)) {
      found = c;
      break;
    }
  }
  if (found.isNull()) found = arr.add<JsonObject>();
  found["id"] = id;
  found["name"] = connector["name"] | id;
  found["transport"] = connector["transport"] | "lan";
  found["type"] = connector["type"] | "";
  found["enabled"] = connector["enabled"] | true;
  if (connector["baseUrl"].is<const char *>()) found["baseUrl"] = connector["baseUrl"];
  File out = SD_MMC.open(path, "w");
  if (!out) {
    message = "write fail";
    return false;
  }
  serializeJsonPretty(doc, out);
  out.close();
  message = String("connector ") + id;
  return true;
}

bool connectorStoreRemove(const char *id, String &message) {
  if (!id || !id[0]) {
    message = "id required";
    return false;
  }
  String path = connectorsPath();
  if (!SD_MMC.exists(path)) {
    message = "none";
    return false;
  }
  File f = SD_MMC.open(path, "r");
  JsonDocument doc;
  deserializeJson(doc, f);
  f.close();
  JsonArray arr = doc["connectors"].as<JsonArray>();
  JsonDocument next;
  JsonArray outArr = next["connectors"].to<JsonArray>();
  for (JsonObject c : arr) {
    if (!strcmp(c["id"] | "", id)) continue;
    outArr.add(c);
  }
  File out = SD_MMC.open(path, "w");
  serializeJsonPretty(next, out);
  out.close();
  String vp = vaultPath(id);
  if (SD_MMC.exists(vp)) SD_MMC.remove(vp);
  message = "removed";
  return true;
}

bool connectorStoreSetSecret(const char *id, JsonVariantConst secret, String &message) {
  if (!id || !id[0]) {
    message = "id required";
    return false;
  }
  connectorStoreBegin();
  String path = vaultPath(id);
  File f = SD_MMC.open(path, "w");
  if (!f) {
    message = "vault write fail";
    return false;
  }
  serializeJsonPretty(secret, f);
  f.close();
  message = "secret saved";
  return true;
}

bool connectorStoreGetSecret(const char *id, JsonDocument &out) {
  String path = vaultPath(id);
  if (!SD_MMC.exists(path)) return false;
  File f = SD_MMC.open(path, "r");
  if (!f) return false;
  bool ok = !deserializeJson(out, f);
  f.close();
  return ok;
}

bool connectorStoreHaImport(String &message) {
  // Find homeassistant connector with baseUrl + token in vault
  String path = connectorsPath();
  if (!SD_MMC.exists(path)) {
    message = "no connectors";
    return false;
  }
  File f = SD_MMC.open(path, "r");
  JsonDocument doc;
  deserializeJson(doc, f);
  f.close();
  const char *base = nullptr;
  const char *cid = nullptr;
  for (JsonObject c : doc["connectors"].as<JsonArray>()) {
    if (!strcmp(c["type"] | "", "homeassistant")) {
      base = c["baseUrl"] | "";
      cid = c["id"] | "homeassistant";
      break;
    }
  }
  if (!base || !base[0]) {
    message = "add homeassistant connector with baseUrl";
    return false;
  }
  JsonDocument secret;
  if (!connectorStoreGetSecret(cid, secret)) {
    message = "missing HA token in vault";
    return false;
  }
  const char *token = secret["token"] | "";
  if (!token[0]) {
    message = "token required";
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  String url = String(base);
  if (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/api/states";
  if (!http.begin(client, url)) {
    message = "ha connect fail";
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + token);
  http.addHeader("Content-Type", "application/json");
  int code = http.GET();
  String body = code > 0 ? http.getString() : "";
  http.end();
  if (code != 200) {
    message = "ha http " + String(code);
    return false;
  }
  JsonDocument states;
  if (deserializeJson(states, body)) {
    message = "ha parse fail";
    return false;
  }
  int added = 0;
  if (!states.is<JsonArray>()) {
    message = "ha unexpected";
    return false;
  }
  for (JsonObject st : states.as<JsonArray>()) {
    const char *eid = st["entity_id"] | "";
    if (!eid[0]) continue;
    // lights + switches only for v1
    if (strncmp(eid, "light.", 6) && strncmp(eid, "switch.", 7)) continue;
    String id = String("ha-") + eid;
    id.replace(".", "-");
    JsonDocument d;
    d["id"] = id;
    d["type"] = "ha_entity";
    d["name"] = st["attributes"]["friendly_name"] | eid;
    d["zoneId"] = "main";
    d["fallbackIp"] = "";
    d["entityId"] = eid;
    d["connector"] = cid;
    String msg;
    if (deviceHubAddDevice(d.as<JsonVariantConst>(), msg)) added++;
    if (added >= 24) break;
  }
  message = String("ha imported ") + added;
  return added > 0;
}
