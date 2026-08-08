#include "device_hub.h"

#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>

#include "av_ctrl.h"
#include "av_secrets.h"
#include "grid_store.h"

static const int kMaxDev = 48;
static const uint16_t WIZ_PORT = 38899;

struct Dev {
  String id;
  String zoneId;
  String type;
  String name;
  String mac;
  String hostname;
  String hueId;       // Hue light id (bridge)
  String entityId;    // Home Assistant entity_id
  String connector;   // connector id
  String snapshotPath;  // optional HTTP path for camera JPEG
  IPAddress ip;
  uint16_t port = 80;
  bool online = false;
};

static Dev devices[kMaxDev];
static int deviceCount = 0;
static WiFiUDP wizUdp;
static uint32_t lastProbeMs = 0;
static char lastSceneIdBuf[24] = "";
static char lastSceneTagBuf[16] = "";

static IPAddress parseIp(const char *s) {
  IPAddress ip;
  if (!ip.fromString(s)) return INADDR_NONE;
  return ip;
}

static bool httpGetOk(const String &url, int timeoutMs = 1200) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  http.end();
  return code > 0 && code < 500;
}

static bool httpPostJson(const String &url, const char *json, int timeoutMs = 2500) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(timeoutMs > 800 ? 800 : timeoutMs);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  http.end();
  return code > 0 && code < 400;
}

static bool httpPutJson(const String &url, const String &json) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(2500);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(json);
  http.end();
  return code > 0 && code < 400;
}

static bool tcpProbe(const IPAddress &ip, uint16_t port, uint32_t timeoutMs = 400) {
  if (ip == INADDR_NONE || !port) return false;
  WiFiClient c;
  return c.connect(ip, port, timeoutMs);
}

static void rgbToXy(uint8_t r, uint8_t g, uint8_t b, float &x, float &y) {
  float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
  auto gamma = [](float c) {
    return (c > 0.04045f) ? powf((c + 0.055f) / 1.055f, 2.4f) : (c / 12.92f);
  };
  rf = gamma(rf);
  gf = gamma(gf);
  bf = gamma(bf);
  float X = rf * 0.664511f + gf * 0.154324f + bf * 0.162028f;
  float Y = rf * 0.283881f + gf * 0.668433f + bf * 0.047685f;
  float Z = rf * 0.000088f + gf * 0.072310f + bf * 0.986039f;
  float sum = X + Y + Z;
  if (sum <= 0) {
    x = 0.3127f;
    y = 0.3290f;
    return;
  }
  x = X / sum;
  y = Y / sum;
}

// Per-grid Hue credentials on SD; fall back to compile-time av_secrets.
static String gHueIp;
static String gHueUser;
static bool gHueCredLoaded = false;

void deviceHubInvalidateHueCreds() { gHueCredLoaded = false; }

static void loadHueCreds() {
  if (gHueCredLoaded) return;
  gHueCredLoaded = true;
  gHueIp = HUE_BRIDGE_IP;
  gHueUser = HUE_USERNAME;
  String path = gridStorePath("vault/hue.json");
  if (!SD_MMC.exists(path)) return;
  File f = SD_MMC.open(path, "r");
  if (!f) return;
  JsonDocument doc;
  if (!deserializeJson(doc, f)) {
    const char *ip = doc["ip"] | "";
    const char *user = doc["username"] | "";
    if (ip[0]) gHueIp = ip;
    if (user[0]) gHueUser = user;
  }
  f.close();
}

static bool saveHueCreds(const char *ip, const char *user) {
  String dir = gridStorePath("vault");
  if (!SD_MMC.exists(dir)) SD_MMC.mkdir(dir.c_str());
  // gridStorePath uses static buf — rebuild after mkdir
  String path = String("/homes/") + gridStoreActiveId() + "/vault/hue.json";
  JsonDocument doc;
  doc["ip"] = ip;
  doc["username"] = user;
  File f = SD_MMC.open(path, "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  gHueIp = ip;
  gHueUser = user;
  gHueCredLoaded = true;
  return true;
}

static const char *hueIp() {
  loadHueCreds();
  return gHueIp.c_str();
}
static const char *hueUser() {
  loadHueCreds();
  return gHueUser.c_str();
}

static String hueStateUrl(const char *lightId) {
  return String("http://") + hueIp() + "/api/" + hueUser() + "/lights/" + lightId + "/state";
}

static String hueGroupActionUrl(const char *groupId) {
  return String("http://") + hueIp() + "/api/" + hueUser() + "/groups/" + groupId + "/action";
}

static bool hueSetGroup(const char *groupId, bool on, int bri = -1, int ct = -1) {
  if (!groupId || !groupId[0]) return false;
  String body = String("{\"on\":") + (on ? "true" : "false");
  if (on && bri >= 0) body += ",\"bri\":" + String(constrain(bri, 1, 254));
  if (on && ct >= 0) body += ",\"ct\":" + String(constrain(ct, 153, 500));
  body += "}";
  return httpPutJson(hueGroupActionUrl(groupId), body);
}

static bool hueSetLight(const char *lightId, bool on, int bri = -1, int ct = -1) {
  if (!lightId || !lightId[0]) return false;
  String body = String("{\"on\":") + (on ? "true" : "false");
  if (on && bri >= 0) body += ",\"bri\":" + String(constrain(bri, 1, 254));
  if (on && ct >= 0) body += ",\"ct\":" + String(constrain(ct, 153, 500));
  body += "}";
  return httpPutJson(hueStateUrl(lightId), body);
}

static bool hueSetLightRgb(const char *lightId, bool on, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t bri) {
  if (!lightId || !lightId[0]) return false;
  float x, y;
  rgbToXy(r, g, b, x, y);
  char body[128];
  if (on) {
    snprintf(body, sizeof(body), "{\"on\":true,\"bri\":%u,\"xy\":[%.4f,%.4f]}",
             (unsigned)constrain(bri, 1, 254), x, y);
  } else {
    snprintf(body, sizeof(body), "{\"on\":false}");
  }
  return httpPutJson(hueStateUrl(lightId), String(body));
}

static bool hueAlert(const char *lightId, const char *alert) {
  if (!lightId || !lightId[0]) return false;
  String body = String("{\"alert\":\"") + alert + "\"}";
  return httpPutJson(hueStateUrl(lightId), body);
}

static bool hueSetEffect(const char *lightId, const char *effect) {
  if (!lightId || !lightId[0]) return false;
  String body = String("{\"effect\":\"") + effect + "\"}";
  return httpPutJson(hueStateUrl(lightId), body);
}

static void hueRefreshOnline() {
  auto fetchJson = [](const String &url, JsonDocument &doc) -> bool {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(4000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    if (code != 200) {
      http.end();
      return false;
    }
    String body = http.getString();
    http.end();
    return !deserializeJson(doc, body);
  };

  JsonDocument lights;
  bool lightsOk =
      fetchJson(String("http://") + hueIp() + "/api/" + hueUser() + "/lights", lights);
  JsonDocument groups;
  bool groupsOk =
      fetchJson(String("http://") + hueIp() + "/api/" + hueUser() + "/groups", groups);

  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "hue") {
      if (!lightsOk) {
        devices[i].online = false;
        continue;
      }
      const char *hid = devices[i].hueId.c_str();
      JsonObject L = lights[hid].as<JsonObject>();
      devices[i].online = !L.isNull() && (L["state"]["reachable"] | false);
      devices[i].ip = parseIp(hueIp());
    } else if (devices[i].type == "hue_group") {
      if (!groupsOk) {
        devices[i].online = false;
        continue;
      }
      const char *gid = devices[i].hueId.c_str();
      JsonObject G = groups[gid].as<JsonObject>();
      // Group "any_on" / reachable via any light
      devices[i].online = !G.isNull();
      devices[i].ip = parseIp(hueIp());
    }
  }
}

static void hueAll(bool on) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type != "hue") continue;
    hueSetLight(devices[i].hueId.c_str(), on);
    delay(20);
  }
}

static void hueBasementScene(bool on, int bri, int ct, int r = -1, int g = -1, int b = -1) {
  const char *id = HUE_BASEMENT_LAMP;
  // Prefer registered device if present
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "hue" && devices[i].hueId == HUE_BASEMENT_LAMP) {
      id = devices[i].hueId.c_str();
      break;
    }
  }
  hueSetEffect(id, "none");
  if (!on) {
    hueSetLight(id, false);
    return;
  }
  if (r >= 0 && g >= 0 && b >= 0) {
    hueSetLightRgb(id, true, (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)constrain(bri, 1, 254));
  } else {
    hueSetLight(id, true, bri, ct);
  }
}

static void wizSend(const IPAddress &ip, JsonDocument &doc) {
  if (ip == INADDR_NONE) return;
  char payload[256];
  serializeJson(doc, payload, sizeof(payload));
  if (!wizUdp.beginPacket(ip, WIZ_PORT)) return;
  wizUdp.write(reinterpret_cast<uint8_t *>(payload), strlen(payload));
  wizUdp.endPacket();
}

static void normalizeMac(const char *in, char *out, size_t outLen) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < outLen; i++) {
    char c = in[i];
    if (c == ':' || c == '-' || c == ' ') continue;
    if (c >= 'A' && c <= 'F') c = c - 'A' + 'a';
    out[j++] = c;
  }
  out[j] = 0;
}

static bool macEquals(const char *a, const char *b) {
  char na[32], nb[32];
  normalizeMac(a, na, sizeof(na));
  normalizeMac(b, nb, sizeof(nb));
  return strcmp(na, nb) == 0;
}

static void wizSendRegistrationBroadcast() {
  JsonDocument doc;
  doc["method"] = "registration";
  JsonObject p = doc["params"].to<JsonObject>();
  p["phoneMac"] = "AAAABBABBABA";
  p["register"] = false;
  p["phoneIp"] = "1.2.3.4";
  char payload[256];
  serializeJson(doc, payload, sizeof(payload));

  IPAddress self = WiFi.localIP();
  IPAddress subnetBcast(self[0], self[1], self[2], 255);
  IPAddress bcasts[] = {IPAddress(255, 255, 255, 255), subnetBcast,
                        IPAddress(192, 168, 4, 255), IPAddress(192, 168, 5, 255)};
  for (auto &b : bcasts) {
    if (!wizUdp.beginPacket(b, WIZ_PORT)) continue;
    wizUdp.write(reinterpret_cast<uint8_t *>(payload), strlen(payload));
    wizUdp.endPacket();
  }
}

static void wizDiscoverByMac() {
  // WiZ registration broadcast — bulbs reply with MAC; IPs drift on DHCP
  wizSendRegistrationBroadcast();

  uint32_t t0 = millis();
  while (millis() - t0 < 1200) {
    int n = wizUdp.parsePacket();
    if (n <= 0) {
      delay(10);
      continue;
    }
    char buf[320];
    int len = wizUdp.read(buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = 0;
    JsonDocument resp;
    if (deserializeJson(resp, buf)) continue;
    const char *mac = resp["result"]["mac"] | "";
    if (!mac[0]) continue;
    IPAddress rip = wizUdp.remoteIP();
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].type != "wiz_bulb") continue;
      if (!macEquals(devices[i].mac.c_str(), mac)) continue;
      devices[i].ip = rip;
      devices[i].online = true;
      Serial.printf("[HUB] WiZ %s → %s\n", devices[i].name.c_str(),
                    rip.toString().c_str());
    }
  }
}

static bool wizProbe(const IPAddress &ip) {
  if (ip == INADDR_NONE) return false;
  JsonDocument doc;
  doc["method"] = "getPilot";
  wizSend(ip, doc);
  uint32_t t0 = millis();
  while (millis() - t0 < 500) {
    int n = wizUdp.parsePacket();
    if (n > 0) {
      char buf[256];
      int len = wizUdp.read(buf, sizeof(buf) - 1);
      if (len > 0) return true;
    }
    delay(10);
  }
  return false;
}

static Dev *findDev(const String &id) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].id == id) return &devices[i];
  }
  return nullptr;
}

static void persistDeviceIps() {
  String path = gridStorePath("devices.json");
  if (!SD_MMC.exists(path)) return;
  File f = SD_MMC.open(path, "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return;
  }
  f.close();
  JsonArray arr = doc["devices"].as<JsonArray>();
  for (JsonObject o : arr) {
    const char *id = o["id"] | "";
    Dev *d = findDev(id);
    if (!d || d->ip == INADDR_NONE) continue;
    o["fallbackIp"] = d->ip.toString();
  }
  f = SD_MMC.open(path, "w");
  if (!f) return;
  serializeJsonPretty(doc, f);
  f.close();
}

static bool readDevicesDoc(JsonDocument &doc) {
  String path = gridStorePath("devices.json");
  if (!SD_MMC.exists(path)) {
    doc["devices"].to<JsonArray>();
    return true;
  }
  File f = SD_MMC.open(path, "r");
  if (!f) return false;
  bool ok = !deserializeJson(doc, f);
  f.close();
  if (ok && !doc["devices"].is<JsonArray>()) doc["devices"].to<JsonArray>();
  return ok;
}

static bool writeDevicesDoc(JsonDocument &doc) {
  String path = gridStorePath("devices.json");
  File f = SD_MMC.open(path, "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

static bool reloadAfterWrite(String &message) {
  if (!deviceHubLoadFromSd()) {
    message = "reload fail";
    return false;
  }
  deviceHubRefreshOnline();
  message = String("devices=") + deviceCount;
  return true;
}

bool deviceHubReplaceDevicesJson(const String &json, String &message) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc["devices"].is<JsonArray>()) {
    message = "bad devices json";
    return false;
  }
  if (!writeDevicesDoc(doc)) {
    message = "sd write fail";
    return false;
  }
  return reloadAfterWrite(message);
}

static bool idTakenOnSd(JsonArray arr, const char *id) {
  for (JsonObject o : arr) {
    if (!strcmp(o["id"] | "", id)) return true;
  }
  return false;
}

bool deviceHubAddDevice(JsonVariantConst device, String &message) {
  const char *id = device["id"] | "";
  const char *type = device["type"] | "";
  const char *name = device["name"] | "";
  if (!id[0] || !type[0] || !name[0]) {
    message = "id, type, name required";
    return false;
  }
  JsonDocument doc;
  if (!readDevicesDoc(doc)) {
    message = "sd read fail";
    return false;
  }
  JsonArray arr = doc["devices"].as<JsonArray>();
  if (idTakenOnSd(arr, id)) {
    message = "id exists";
    return false;
  }
  if (arr.size() >= (size_t)kMaxDev) {
    message = "device limit";
    return false;
  }
  JsonObject o = arr.add<JsonObject>();
  o["id"] = id;
  o["type"] = type;
  o["name"] = name;
  o["zoneId"] = device["zoneId"] | "main";
  if (!device["mac"].isNull()) o["mac"] = device["mac"];
  if (!device["hostname"].isNull()) o["hostname"] = device["hostname"];
  if (!device["hueId"].isNull()) o["hueId"] = device["hueId"];
  if (!device["fallbackIp"].isNull()) o["fallbackIp"] = device["fallbackIp"];
  if (!device["port"].isNull()) o["port"] = device["port"];
  if (!device["entityId"].isNull()) o["entityId"] = device["entityId"];
  if (!device["connector"].isNull()) o["connector"] = device["connector"];
  if (!device["snapshotPath"].isNull()) o["snapshotPath"] = device["snapshotPath"];
  if (!writeDevicesDoc(doc)) {
    message = "sd write fail";
    return false;
  }
  return reloadAfterWrite(message);
}

bool deviceHubUpdateDevice(const char *id, JsonVariantConst patch, String &message) {
  if (!id || !id[0]) {
    message = "id required";
    return false;
  }
  JsonDocument doc;
  if (!readDevicesDoc(doc)) {
    message = "sd read fail";
    return false;
  }
  JsonArray arr = doc["devices"].as<JsonArray>();
  JsonObject found;
  for (JsonObject o : arr) {
    if (!strcmp(o["id"] | "", id)) {
      found = o;
      break;
    }
  }
  if (found.isNull()) {
    message = "not found";
    return false;
  }
  if (!patch["name"].isNull()) found["name"] = patch["name"];
  if (!patch["zoneId"].isNull()) found["zoneId"] = patch["zoneId"];
  if (!patch["fallbackIp"].isNull()) found["fallbackIp"] = patch["fallbackIp"];
  if (!patch["hostname"].isNull()) found["hostname"] = patch["hostname"];
  if (!patch["mac"].isNull()) found["mac"] = patch["mac"];
  if (!patch["hueId"].isNull()) found["hueId"] = patch["hueId"];
  if (!patch["port"].isNull()) found["port"] = patch["port"];
  if (!patch["type"].isNull()) found["type"] = patch["type"];
  if (!patch["entityId"].isNull()) found["entityId"] = patch["entityId"];
  if (!patch["connector"].isNull()) found["connector"] = patch["connector"];
  if (!patch["snapshotPath"].isNull()) found["snapshotPath"] = patch["snapshotPath"];
  if (!writeDevicesDoc(doc)) {
    message = "sd write fail";
    return false;
  }
  return reloadAfterWrite(message);
}

bool deviceHubRemoveDevice(const char *id, String &message) {
  if (!id || !id[0]) {
    message = "id required";
    return false;
  }
  JsonDocument doc;
  if (!readDevicesDoc(doc)) {
    message = "sd read fail";
    return false;
  }
  JsonArray arr = doc["devices"].as<JsonArray>();
  for (size_t i = 0; i < arr.size(); i++) {
    if (!strcmp(arr[i]["id"] | "", id)) {
      arr.remove(i);
      if (!writeDevicesDoc(doc)) {
        message = "sd write fail";
        return false;
      }
      return reloadAfterWrite(message);
    }
  }
  message = "not found";
  return false;
}

static void zoneSlugFromName(const char *name, char *out, size_t outLen) {
  if (!out || outLen < 2) return;
  out[0] = '\0';
  if (!name) {
    strlcpy(out, "home", outLen);
    return;
  }
  // Normalize Emma duplicates + common rooms
  String n = name;
  n.toLowerCase();
  n.replace("’", "'");
  n.replace("'", "");
  if (n.indexOf("emma") >= 0 && n.indexOf("room") >= 0) {
    strlcpy(out, "emmas-room", outLen);
    return;
  }
  if (n.indexOf("living") >= 0) {
    strlcpy(out, "living-room", outLen);
    return;
  }
  if (n.indexOf("kitchen") >= 0) {
    strlcpy(out, "kitchen", outLen);
    return;
  }
  if (n.indexOf("entry") >= 0 || n.indexOf("foyer") >= 0) {
    strlcpy(out, "entry", outLen);
    return;
  }
  if (n.indexOf("bedroom") >= 0 || n.indexOf("master") >= 0) {
    strlcpy(out, "bedroom", outLen);
    return;
  }
  if (n.indexOf("downstairs") >= 0 || n.indexOf("back room") >= 0) {
    strlcpy(out, "downstairs", outLen);
    return;
  }
  if (n.indexOf("basement") >= 0) {
    strlcpy(out, "basement", outLen);
    return;
  }
  // generic slug
  size_t j = 0;
  bool dash = false;
  for (size_t i = 0; name[i] && j + 1 < outLen; i++) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out[j++] = c;
      dash = false;
    } else if (!dash && j > 0) {
      out[j++] = '-';
      dash = true;
    }
  }
  while (j > 0 && out[j - 1] == '-') j--;
  out[j] = '\0';
  if (!out[0]) strlcpy(out, "home", outLen);
}

static const char *prettyZoneName(const char *slug) {
  if (!strcmp(slug, "living-room")) return "Living Room";
  if (!strcmp(slug, "kitchen")) return "Kitchen";
  if (!strcmp(slug, "entry")) return "Entry";
  if (!strcmp(slug, "bedroom")) return "Bedroom";
  if (!strcmp(slug, "emmas-room")) return "Emma's Room";
  if (!strcmp(slug, "downstairs")) return "Downstairs";
  if (!strcmp(slug, "basement")) return "Basement";
  return slug;
}

static bool writeRoomsJson(JsonArray zoneSlugs) {
  JsonDocument rooms;
  JsonArray z = rooms["zones"].to<JsonArray>();
  JsonObject basement = z.add<JsonObject>();
  basement["id"] = "basement";
  basement["name"] = "Basement";
  basement["sort"] = 0;
  int sort = 1;
  for (JsonVariant v : zoneSlugs) {
    const char *slug = v | "";
    if (!slug[0] || !strcmp(slug, "basement") || !strcmp(slug, "home")) continue;
    bool exists = false;
    for (JsonObject o : z) {
      if (!strcmp(o["id"] | "", slug)) {
        exists = true;
        break;
      }
    }
    if (exists) continue;
    JsonObject o = z.add<JsonObject>();
    o["id"] = slug;
    o["name"] = prettyZoneName(slug);
    o["sort"] = sort++;
  }
  String path = gridStorePath("rooms.json");
  File f = SD_MMC.open(path, "w");
  if (!f) return false;
  serializeJsonPretty(rooms, f);
  f.close();
  return true;
}

bool deviceHubHueSync(String &message) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  String base = String("http://") + hueIp() + "/api/" + hueUser();
  if (!http.begin(client, base + "/lights")) {
    message = "hue lights fail";
    return false;
  }
  if (http.GET() != 200) {
    http.end();
    message = "hue lights http";
    return false;
  }
  String lightsBody = http.getString();
  http.end();
  if (!http.begin(client, base + "/groups")) {
    message = "hue groups fail";
    return false;
  }
  if (http.GET() != 200) {
    http.end();
    message = "hue groups http";
    return false;
  }
  String groupsBody = http.getString();
  http.end();

  JsonDocument lights;
  JsonDocument groups;
  if (deserializeJson(lights, lightsBody) || deserializeJson(groups, groupsBody)) {
    message = "hue parse fail";
    return false;
  }

  JsonDocument doc;
  if (!readDevicesDoc(doc)) {
    message = "sd read fail";
    return false;
  }
  JsonArray arr = doc["devices"].as<JsonArray>();
  int added = 0;

  // lightId → zone slug from Hue Room membership
  char lightZone[48][20];
  for (int i = 0; i < 48; i++) lightZone[i][0] = '\0';
  JsonDocument zoneList;
  JsonArray zoneSlugs = zoneList.to<JsonArray>();

  auto findDevObj = [&](const char *type, const char *hid) -> JsonObject {
    for (JsonObject o : arr) {
      if (strcmp(o["type"] | "", type) != 0) continue;
      const char *existing = o["hueId"] | "";
      if (!strcmp(existing, hid)) return o;
      String want = String(type) + "-" + hid;
      if (!strcmp(o["id"] | "", want.c_str())) return o;
    }
    return JsonObject();
  };

  for (JsonPair kv : groups.as<JsonObject>()) {
    JsonObject G = kv.value().as<JsonObject>();
    const char *gtype = G["type"] | "";
    if (strcmp(gtype, "Room") != 0 && strcmp(gtype, "Zone") != 0) continue;
    const char *gid = kv.key().c_str();
    const char *name = G["name"] | gid;
    char slug[24];
    zoneSlugFromName(name, slug, sizeof(slug));
    bool have = false;
    for (JsonVariant v : zoneSlugs) {
      if (!strcmp(v | "", slug)) {
        have = true;
        break;
      }
    }
    if (!have) zoneSlugs.add(slug);

    JsonArray members = G["lights"].as<JsonArray>();
    for (JsonVariant lid : members) {
      int idn = atoi(lid | "0");
      if (idn > 0 && idn < 48) strlcpy(lightZone[idn], slug, sizeof(lightZone[idn]));
    }

    JsonObject existing = findDevObj("hue_group", gid);
    if (!existing.isNull()) {
      existing["zoneId"] = slug;
      existing["name"] = name;
      existing["hueId"] = gid;
      continue;
    }
    if (arr.size() >= (size_t)kMaxDev) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = String("hue_group-") + gid;
    o["type"] = "hue_group";
    o["name"] = name;
    o["hueId"] = gid;
    o["fallbackIp"] = hueIp();
    o["port"] = 80;
    o["zoneId"] = slug;
    added++;
  }

  for (JsonPair kv : lights.as<JsonObject>()) {
    const char *hid = kv.key().c_str();
    int idn = atoi(hid);
    JsonObject L = kv.value().as<JsonObject>();
    const char *name = L["name"] | hid;
    char zoneBuf[24];
    if (idn > 0 && idn < 48 && lightZone[idn][0]) strlcpy(zoneBuf, lightZone[idn], sizeof(zoneBuf));
    else zoneSlugFromName(name, zoneBuf, sizeof(zoneBuf));

    JsonObject existing = findDevObj("hue", hid);
    if (!existing.isNull()) {
      existing["zoneId"] = zoneBuf;
      existing["name"] = name;
      existing["hueId"] = hid;
      continue;
    }
    if (arr.size() >= (size_t)kMaxDev) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = String("hue-") + hid;
    o["type"] = "hue";
    o["name"] = name;
    o["hueId"] = hid;
    o["fallbackIp"] = hueIp();
    o["port"] = 80;
    o["zoneId"] = zoneBuf;
    added++;
  }

  // Amazon Cast downstairs → downstairs zone when present
  for (JsonObject o : arr) {
    if (strcmp(o["type"] | "", "cast") != 0) continue;
    String n = String(o["name"] | "");
    n.toLowerCase();
    String id = String(o["id"] | "");
    if (n.indexOf("downstairs") >= 0 || id.indexOf("downstairs") >= 0) o["zoneId"] = "downstairs";
  }

  if (!writeDevicesDoc(doc)) {
    message = "sd write fail";
    return false;
  }
  if (!writeRoomsJson(zoneSlugs)) {
    message = "rooms write fail";
    return false;
  }
  if (!reloadAfterWrite(message)) return false;
  message = String("hue rooms sync +") + added + " → " + deviceCount;
  return true;
}

static bool registeredIp(const char *ip) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].ip.toString() == ip) return true;
  }
  return false;
}

static bool registeredId(const char *id) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].id == id) return true;
  }
  return false;
}

static const char *catalogForType(const char *type) {
  if (!type) return "other";
  if (!strcmp(type, "wiz_bulb") || !strcmp(type, "wled") || !strcmp(type, "hue") ||
      !strcmp(type, "hue_group") || !strcmp(type, "hue_bridge"))
    return "lights";
  if (!strcmp(type, "cast") || !strcmp(type, "vizio") || !strcmp(type, "firetv") ||
      !strcmp(type, "ps5") || !strcmp(type, "sony") || !strcmp(type, "tcl"))
    return "av";
  if (!strcmp(type, "camera")) return "cameras";
  if (!strcmp(type, "lock") || !strcmp(type, "sensor")) return "locks";
  return "other";
}

static int catalogRank(const char *cat) {
  if (!cat) return 9;
  if (!strcmp(cat, "lights")) return 0;
  if (!strcmp(cat, "av")) return 1;
  if (!strcmp(cat, "cameras")) return 2;
  if (!strcmp(cat, "locks")) return 3;
  return 8;
}

static void suggestOne(JsonArray arr, const char *id, const char *type, const char *name,
                       const char *ip, uint16_t port, const char *why) {
  if (registeredId(id) || (ip && ip[0] && registeredIp(ip))) return;
  JsonObject o = arr.add<JsonObject>();
  o["id"] = id;
  o["type"] = type;
  o["name"] = name;
  o["fallbackIp"] = ip ? ip : "";
  o["port"] = port;
  o["zoneId"] = "main";
  o["reason"] = why;
  o["registered"] = false;
  o["catalog"] = catalogForType(type);
}

static bool registeredMac(const char *mac) {
  if (!mac || !mac[0]) return false;
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].mac.length() && macEquals(devices[i].mac.c_str(), mac)) return true;
  }
  return false;
}

// Broadcast WiZ discover and add suggestions for unknown bulbs (portable LAN).
static void wizSuggestDiscover(JsonArray sug) {
  wizSendRegistrationBroadcast();
  uint32_t t0 = millis();
  while (millis() - t0 < 900) {
    int n = wizUdp.parsePacket();
    if (n <= 0) {
      delay(10);
      continue;
    }
    char buf[320];
    int len = wizUdp.read(buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = 0;
    JsonDocument resp;
    if (deserializeJson(resp, buf)) continue;
    const char *mac = resp["result"]["mac"] | "";
    if (!mac[0] || registeredMac(mac)) continue;
    IPAddress rip = wizUdp.remoteIP();
    char ips[20];
    snprintf(ips, sizeof(ips), "%u.%u.%u.%u", rip[0], rip[1], rip[2], rip[3]);
    if (registeredIp(ips)) continue;
    char macNorm[32];
    normalizeMac(mac, macNorm, sizeof(macNorm));
    String id = String("wiz-") + String(macNorm).substring(0, 8);
    id.replace(":", "");
    if (registeredId(id.c_str())) continue;
    JsonObject o = sug.add<JsonObject>();
    o["id"] = id;
    o["type"] = "wiz_bulb";
    o["name"] = String("WiZ ") + macNorm;
    o["mac"] = macNorm;
    o["fallbackIp"] = ips;
    o["port"] = WIZ_PORT;
    o["zoneId"] = "main";
    o["reason"] = "wiz broadcast";
    o["registered"] = false;
    o["catalog"] = "lights";
  }
}

static bool httpGetBody(const String &url, int timeoutMs, String &body) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(timeoutMs);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  body = code > 0 ? http.getString() : "";
  http.end();
  return code == 200;
}

static void fingerprintHost(JsonArray sug, const IPAddress &ip) {
  char ips[20];
  snprintf(ips, sizeof(ips), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  if (registeredIp(ips)) return;

  String body;
  if (httpGetBody(String("http://") + ips + "/json/info", 350, body)) {
    if (body.indexOf("WLED") >= 0 || body.indexOf("\"ver\"") >= 0) {
      JsonDocument d;
      const char *nm = "WLED";
      if (!deserializeJson(d, body)) nm = d["name"] | "WLED";
      String id = String("wled-") + String(ip[3]);
      suggestOne(sug, id.c_str(), "wled", nm, ips, 80, "wled /json/info");
      return;
    }
  }
  if (httpGetBody(String("http://") + ips + "/api/status", 350, body)) {
    if (body.indexOf("CyberDeck") >= 0 || body.indexOf("cyberdeck") >= 0) {
      String id = String("cyberdeck-") + String(ip[3]);
      suggestOne(sug, id.c_str(), "cyberdeck", "CyberDeck", ips, 80, "api/status");
      return;
    }
  }
  if (httpGetBody(String("http://") + ips + "/api/config", 400, body)) {
    if (body.indexOf("bridgeid") >= 0 || body.indexOf("zigbeechannel") >= 0) {
      JsonDocument d;
      const char *nm = "Hue Bridge";
      if (!deserializeJson(d, body)) nm = d["name"] | "Hue Bridge";
      suggestOne(sug, "hue-bridge", "hue_bridge", nm, ips, 80, "hue /api/config");
      gHueIp = ips;
      gHueCredLoaded = true;
      return;
    }
  }
  if (tcpProbe(ip, 8008, 80)) {
    if (httpGetBody(String("http://") + ips + ":8008/setup/eureka_info", 400, body)) {
      JsonDocument d;
      const char *nm = "Chromecast";
      if (!deserializeJson(d, body)) nm = d["name"] | "Chromecast";
      String id = String("cast-") + String(ip[3]);
      suggestOne(sug, id.c_str(), "cast", nm, ips, 8008, "cast eureka");
      return;
    }
  }
  if (tcpProbe(ip, 7345, 60)) {
    String id = String("vizio-") + String(ip[3]);
    suggestOne(sug, id.c_str(), "vizio", "Vizio TV", ips, 7345, "tcp 7345");
    return;
  }
  if (tcpProbe(ip, 5555, 60)) {
    String id = String("firetv-") + String(ip[3]);
    suggestOne(sug, id.c_str(), "firetv", "Fire TV", ips, 5555, "tcp 5555");
    return;
  }
  if (tcpProbe(ip, 9295, 50) || tcpProbe(ip, 9302, 50)) {
    String id = String("ps5-") + String(ip[3]);
    suggestOne(sug, id.c_str(), "ps5", "PlayStation", ips, 9295, "ps ports");
    return;
  }
  if (tcpProbe(ip, 554, 40)) {
    String id = String("cam-") + String(ip[3]);
    suggestOne(sug, id.c_str(), "camera", "IP Camera", ips, 554, "rtsp 554");
  }
}

void deviceHubFillDiscover(JsonObject out) {
  out["ok"] = true;
  out["deviceCount"] = deviceCount;
  JsonArray sug = out["suggestions"].to<JsonArray>();
  JsonArray catalog = out["catalog"].to<JsonArray>();
  catalog.add("lights");
  catalog.add("av");
  catalog.add("cameras");
  catalog.add("locks");

  IPAddress self = WiFi.localIP();
  out["subnet"] = self.toString() + "/24";

  wizSuggestDiscover(sug);

  int probed = 0;
  const int kMaxHosts = 64;
  // Prefer nearby hosts first, then wrap the /24
  auto consider = [&](uint8_t last) {
    if (probed >= kMaxHosts) return;
    if (last == 0 || last == 255 || last == self[3]) return;
    IPAddress ip(self[0], self[1], self[2], last);
    probed++;
    if (!(tcpProbe(ip, 80, 30) || tcpProbe(ip, 8008, 25) || tcpProbe(ip, 554, 20) ||
          tcpProbe(ip, 7345, 20) || tcpProbe(ip, 5555, 20)))
      return;
    fingerprintHost(sug, ip);
  };
  for (int d = 1; d < 255 && probed < kMaxHosts; d++) {
    int up = (int)self[3] + d;
    int dn = (int)self[3] - d;
    if (up < 255) consider((uint8_t)up);
    if (dn > 0) consider((uint8_t)dn);
  }
  out["probed"] = probed;

  loadHueCreds();
  if (gHueUser.length() && gHueIp.length()) {
    String body;
    String url = String("http://") + hueIp() + "/api/" + hueUser() + "/groups";
    if (httpGetBody(url, 3500, body)) {
      JsonDocument gdoc;
      if (!deserializeJson(gdoc, body)) {
        for (JsonPair kv : gdoc.as<JsonObject>()) {
          JsonObject G = kv.value().as<JsonObject>();
          const char *gt = G["type"] | "";
          if (strcmp(gt, "Room") && strcmp(gt, "Zone")) continue;
          String id = String("hue_group-") + kv.key().c_str();
          if (registeredId(id.c_str())) continue;
          JsonObject o = sug.add<JsonObject>();
          o["id"] = id;
          o["type"] = "hue_group";
          o["name"] = G["name"] | kv.key().c_str();
          o["hueId"] = kv.key().c_str();
          o["fallbackIp"] = hueIp();
          o["port"] = 80;
          o["zoneId"] = "main";
          o["reason"] = "hue room";
          o["registered"] = false;
          o["catalog"] = "lights";
        }
      }
    }
  }

  // Rank suggestions: lights → AV → cameras → locks
  // (ArduinoJson arrays aren't easily sortable in-place; emit ranked index)
  JsonArray ranked = out["ranked"].to<JsonArray>();
  for (int rank = 0; rank <= 8; rank++) {
    for (JsonObject s : sug) {
      if (catalogRank(s["catalog"] | "other") == rank) ranked.add(s["id"]);
    }
  }
  out["suggested"] = (int)sug.size();

  JsonArray reg = out["registered"].to<JsonArray>();
  deviceHubFillDevices(reg);
}

bool deviceHubHuePair(const char *bridgeIp, String &message) {
  String ip = bridgeIp && bridgeIp[0] ? String(bridgeIp) : String(hueIp());
  if (!ip.length()) {
    message = "no bridge ip — run discover first";
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
  String url = String("http://") + ip + "/api";
  if (!http.begin(client, url)) {
    message = "hue connect fail";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{\"devicetype\":\"overlink#core\"}");
  String body = code > 0 ? http.getString() : "";
  http.end();
  if (code != 200) {
    message = "hue http " + String(code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "hue parse fail";
    return false;
  }
  if (doc.is<JsonArray>() && doc.size() > 0) {
    JsonObject o = doc[0].as<JsonObject>();
    if (!o["error"].isNull()) {
      message = o["error"]["description"] | "press hue link button";
      return false;
    }
    const char *user = o["success"]["username"] | "";
    if (!user[0]) {
      message = "no username";
      return false;
    }
    if (!saveHueCreds(ip.c_str(), user)) {
      message = "vault write fail";
      return false;
    }
    message = String("hue linked @ ") + ip;
    return true;
  }
  message = "unexpected hue response";
  return false;
}

bool deviceHubLoadFromSd() {
  deviceCount = 0;
  String path = gridStorePath("devices.json");
  if (!SD_MMC.exists(path)) return false;
  File f = SD_MMC.open(path, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  JsonArray arr = doc["devices"].as<JsonArray>();
  for (JsonObject o : arr) {
    if (deviceCount >= kMaxDev) break;
    Dev &d = devices[deviceCount++];
    d.id = o["id"] | "";
    d.zoneId = o["zoneId"] | "";
    d.type = o["type"] | "";
    d.name = o["name"] | "";
    d.mac = o["mac"] | "";
    d.hostname = o["hostname"] | "";
    d.hueId = o["hueId"] | "";
    d.entityId = o["entityId"] | "";
    d.connector = o["connector"] | "";
    d.snapshotPath = o["snapshotPath"] | "";
    d.port = o["port"] | 80;
    const char *fb = o["fallbackIp"] | "";
    d.ip = parseIp(fb);
    d.online = false;
    // Convenience: hue-29 / hue_group-6 → hueId
    if (!d.hueId.length()) {
      if (d.type == "hue" && d.id.startsWith("hue-")) d.hueId = d.id.substring(4);
      if (d.type == "hue_group" && d.id.startsWith("hue_group-"))
        d.hueId = d.id.substring(10);
    }
  }
  // Migrate known WLED IP if SD still has the old subnet address
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].id == "wled-basement" &&
        devices[i].ip.toString() == "192.168.5.162") {
      devices[i].ip = IPAddress(192, 168, 4, 39);
      Serial.println("[HUB] migrated WLED → 192.168.4.39");
    }
    // CyberDeck drifted off .156 → prefer .50 seed
    if (devices[i].id == "cyberdeck-basement" &&
        devices[i].ip.toString() == "192.168.4.156") {
      devices[i].ip = IPAddress(192, 168, 4, 50);
      Serial.println("[HUB] migrated CyberDeck → 192.168.4.50");
    }
  }
  Serial.printf("[HUB] loaded %d devices from TF\n", deviceCount);
  return deviceCount > 0;
}

void deviceHubBegin() {
  wizUdp.begin(0);
  deviceHubLoadFromSd();
  deviceHubRefreshOnline();
  avCtrlBegin();
}

void deviceHubLoop() {
  if (millis() - lastProbeMs > 30000) {
    lastProbeMs = millis();
    deviceHubRefreshOnline();
  }
}

void deviceHubRefreshOnline() {
  // Reset WiZ online, rediscover by MAC (DHCP moves them)
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "wiz_bulb") devices[i].online = false;
  }
  wizDiscoverByMac();
  hueRefreshOnline();
  for (int i = 0; i < deviceCount; i++) {
    Dev &d = devices[i];
    if (d.type == "wiz_bulb") {
      if (!d.online) d.online = wizProbe(d.ip);
      continue;
    }
    if (d.type == "wled") {
      d.online = httpGetOk("http://" + d.ip.toString() + "/json/info", 1500);
      continue;
    }
    if (d.type == "camera") {
      d.online = tcpProbe(d.ip, 554, 200) || tcpProbe(d.ip, 80, 200);
      continue;
    }
    if (d.type == "ha_entity") {
      d.online = true;  // bridged — assume available when connector present
      continue;
    }
    if (d.type == "cyberdeck") {
      // DHCP drift: try hostname, saved IP, then last-known peer IPs
      const char *alts[] = {"192.168.4.50", "192.168.4.156"};
      d.online = false;
      if (d.hostname.length() &&
          httpGetOk("http://" + d.hostname + ".local/api/status", 1500)) {
        d.online = true;
      } else if (httpGetOk("http://" + d.ip.toString() + "/api/status", 1200)) {
        d.online = true;
      } else {
        for (const char *alt : alts) {
          if (httpGetOk(String("http://") + alt + "/api/status", 900)) {
            d.ip = parseIp(alt);
            d.online = true;
            Serial.printf("[HUB] CyberDeck found at %s\n", alt);
            break;
          }
        }
      }
      continue;
    }
    if (d.type == "hue" || d.type == "hue_group") continue;  // hueRefreshOnline
    if (d.type == "cast") {
      uint16_t port = d.port ? d.port : 8008;
      d.online = httpGetOk("http://" + d.ip.toString() + ":" + String(port) + "/setup/eureka_info",
                           1200);
      continue;
    }
    if (d.type == "ps5") {
      // Rest Mode answers DDP SRCH on UDP 9302 (HTTP 620); fully awake may also answer.
      d.online = false;
      {
        WiFiUDP udp;
        if (udp.begin(0)) {
          const char *srch =
              "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00030010\n";
          if (udp.beginPacket(d.ip, 9302) && udp.write((const uint8_t *)srch, strlen(srch) + 1) &&
              udp.endPacket()) {
            uint32_t until = millis() + 700;
            while (millis() < until) {
              int sz = udp.parsePacket();
              if (sz > 0) {
                d.online = true;
                break;
              }
              delay(10);
            }
          }
          udp.stop();
        }
      }
      if (!d.online)
        d.online = tcpProbe(d.ip, 9295, 300) || tcpProbe(d.ip, 80, 300);
      continue;
    }
    if (d.type == "vizio" || d.type == "firetv" || d.type == "sony" || d.type == "tcl") {
      uint16_t port = d.port ? d.port : 80;
      if (d.type == "vizio" && !d.port) port = 7345;
      if (d.type == "firetv" && !d.port) port = 5555;
      if (d.type == "sony" && !d.port) port = 8080;
      if (d.type == "tcl" && !d.port) port = 6467;
      d.online = tcpProbe(d.ip, port);
      continue;
    }
    d.online = false;
  }
  persistDeviceIps();
}

void deviceHubFillDevices(JsonArray arr) {
  for (int i = 0; i < deviceCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = devices[i].id;
    o["zoneId"] = devices[i].zoneId;
    o["type"] = devices[i].type;
    o["name"] = devices[i].name;
    o["online"] = devices[i].online;
    o["ip"] = devices[i].ip.toString();
    if (devices[i].hueId.length()) o["hueId"] = devices[i].hueId;
  }
}

static void wizAll(bool on) {
  JsonDocument doc;
  doc["method"] = "setPilot";
  doc["params"]["state"] = on;
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type != "wiz_bulb") continue;
    wizSend(devices[i].ip, doc);
    delay(25);
  }
}

static void wizAllWhite(int dim, int tempK) {
  JsonDocument doc;
  doc["method"] = "setPilot";
  JsonObject p = doc["params"].to<JsonObject>();
  p["state"] = true;
  p["dimming"] = constrain(dim, 10, 100);
  p["temp"] = constrain(tempK, 2200, 6500);
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type != "wiz_bulb") continue;
    wizSend(devices[i].ip, doc);
    delay(25);
  }
}

static void wizAllScene(int sceneId, int dim, int speed) {
  JsonDocument doc;
  doc["method"] = "setPilot";
  JsonObject p = doc["params"].to<JsonObject>();
  p["state"] = true;
  p["sceneId"] = sceneId;
  p["dimming"] = constrain(dim, 10, 100);
  p["speed"] = constrain(speed, 20, 200);
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type != "wiz_bulb") continue;
    wizSend(devices[i].ip, doc);
    delay(25);
  }
}

static Dev *wledDev() {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "wled") return &devices[i];
  }
  return nullptr;
}

static Dev *findWled(const char *id) {
  if (id && id[0]) {
    Dev *d = findDev(String(id));
    if (d && d->type == "wled") return d;
    return nullptr;
  }
  return wledDev();
}

static bool httpGetBody(const String &url, String &body, int timeoutMs = 2000) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  body = http.getString();
  http.end();
  return code == 200;
}

static bool wledPostTo(Dev *d, JsonDocument &doc) {
  if (!d || d->ip == INADDR_NONE) return false;
  char payload[480];
  serializeJson(doc, payload, sizeof(payload));
  String url = "http://" + d->ip.toString();
  if (d->port && d->port != 80) url += ":" + String(d->port);
  url += "/json/state";
  return httpPostJson(url, payload);
}

static bool wledPost(JsonDocument &doc) { return wledPostTo(wledDev(), doc); }

static void wledNormal() {
  JsonDocument doc;
  doc["on"] = true;
  doc["bri"] = 200;
  JsonObject seg = doc["seg"].add<JsonObject>();
  seg["fx"] = 0;
  JsonArray col = seg["col"].to<JsonArray>();
  JsonArray rgb = col.add<JsonArray>();
  rgb.add(255);
  rgb.add(200);
  rgb.add(140);
  wledPost(doc);
}

static void wledParty() {
  JsonDocument doc;
  doc["on"] = true;
  doc["bri"] = 200;
  JsonObject seg = doc["seg"].add<JsonObject>();
  seg["fx"] = 9;
  seg["sx"] = 180;
  seg["ix"] = 128;
  wledPost(doc);
}

static void wledNight() {
  JsonDocument doc;
  doc["on"] = true;
  doc["bri"] = 25;
  JsonObject seg = doc["seg"].add<JsonObject>();
  seg["fx"] = 2;
  seg["sx"] = 40;
  seg["ix"] = 40;
  wledPost(doc);
}

static void wledOff() {
  JsonDocument doc;
  doc["on"] = false;
  wledPost(doc);
}

static void wledSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t bri) {
  JsonDocument doc;
  doc["on"] = true;
  doc["bri"] = bri;
  JsonObject seg = doc["seg"].add<JsonObject>();
  seg["fx"] = 0;
  JsonArray col = seg["col"].to<JsonArray>();
  JsonArray rgb = col.add<JsonArray>();
  rgb.add(r);
  rgb.add(g);
  rgb.add(b);
  wledPost(doc);
}

static void wledFx(uint8_t fx, uint8_t sx, uint8_t ix, uint8_t bri) {
  JsonDocument doc;
  doc["on"] = true;
  doc["bri"] = bri;
  JsonObject seg = doc["seg"].add<JsonObject>();
  seg["fx"] = fx;
  seg["sx"] = sx;
  seg["ix"] = ix;
  wledPost(doc);
}

static void wledFxColor(uint8_t fx, uint8_t sx, uint8_t ix, uint8_t bri, uint8_t r, uint8_t g,
                        uint8_t b) {
  JsonDocument doc;
  doc["on"] = true;
  doc["bri"] = bri;
  JsonObject seg = doc["seg"].add<JsonObject>();
  seg["fx"] = fx;
  seg["sx"] = sx;
  seg["ix"] = ix;
  JsonArray col = seg["col"].to<JsonArray>();
  JsonArray rgb = col.add<JsonArray>();
  rgb.add(r);
  rgb.add(g);
  rgb.add(b);
  wledPost(doc);
}

// CTRL scene lighting (from cyd-basement-control) — WiZ + WLED; AV via av_ctrl
static constexpr int kFxAurora = 38;
static constexpr int kFxCandle = 88;
static constexpr int kFxMultiComet = 59;

struct ThemePreset {
  const char *id;
  const char *name;
  uint32_t color;
  bool wizScene;
  uint8_t wizSceneId;
  uint8_t wizDimming;
  uint16_t wizTempK;
  uint8_t wizSpeed;
  uint8_t wledFx;
  uint8_t wledBri;
  uint8_t wledSpeed;
  uint8_t wledIx;
  uint8_t wledR;
  uint8_t wledG;
  uint8_t wledB;
};

static const ThemePreset THEMES[] = {
    {"cozy", "Cozy", 0x6d4c41, true, 6, 80, 0, 80, 0, 180, 0, 0, 255, 160, 100},
    {"fireplace", "Fireplace", 0xc0392b, true, 5, 90, 0, 60, 0, 200, 0, 0, 255, 80, 20},
    {"ocean", "Ocean", 0x1565c0, true, 1, 85, 0, 70, 0, 180, 0, 0, 20, 80, 200},
    {"forest", "Forest", 0x2e7d32, true, 7, 85, 0, 70, 0, 160, 0, 0, 40, 180, 60},
    {"sunset", "Sunset", 0xe65100, true, 3, 90, 0, 80, 0, 200, 0, 0, 255, 100, 40},
    {"relax", "Relax", 0x5e35b1, true, 16, 70, 0, 50, 2, 120, 40, 40, 0, 0, 0},
    {"romance", "Romance", 0xad1457, true, 2, 80, 0, 60, 2, 150, 50, 60, 0, 0, 0},
    {"tv-time", "TV Time", 0x37474f, true, 18, 60, 0, 0, 0, 80, 0, 0, 200, 200, 210},
    {"warm", "Warm", 0xc47f0a, false, 0, 100, 2700, 0, 0, 200, 0, 0, 255, 200, 140},
    {"cool", "Cool", 0x0277bd, false, 0, 100, 5000, 0, 0, 200, 0, 0, 220, 230, 255},
};
static const size_t THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

static Dev *deckDev() {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "cyberdeck") return &devices[i];
  }
  return nullptr;
}

static bool deckPost(const char *path, const char *json) {
  Dev *d = deckDev();
  if (!d) return false;
  // Prefer numeric IP — mDNS (.local) often stalls the whole web portal.
  if (d->ip != INADDR_NONE) {
    String url = "http://" + d->ip.toString() + path;
    if (httpPostJson(url, json, 1800)) return true;
  }
  if (d->hostname.length()) {
    String url = "http://" + d->hostname + ".local" + path;
    if (httpPostJson(url, json, 1200)) return true;
  }
  return false;
}

void deviceHubFillThemes(JsonArray arr) {
  for (size_t i = 0; i < THEME_COUNT; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = THEMES[i].id;
    o["name"] = THEMES[i].name;
    o["color"] = THEMES[i].color;
  }
}

bool deviceHubRunTheme(const String &themeId, String &message) {
  const ThemePreset *t = nullptr;
  for (size_t i = 0; i < THEME_COUNT; i++) {
    if (themeId == THEMES[i].id) {
      t = &THEMES[i];
      break;
    }
  }
  if (!t) {
    message = "unknown theme";
    return false;
  }
  if (t->wizScene) {
    wizAllScene(t->wizSceneId, t->wizDimming, t->wizSpeed);
  } else {
    wizAllWhite(t->wizDimming, t->wizTempK);
  }
  if (t->wledFx == 0) {
    wledSolid(t->wledR, t->wledG, t->wledB, t->wledBri);
  } else {
    wledFx(t->wledFx, t->wledSpeed, t->wledIx, t->wledBri);
  }
  message = String(t->name) + " armed";
  return true;
}

bool deviceHubRunScene(const String &sceneId, String &message) {
  String id = sceneId;
  id.toLowerCase();
  // legacy aliases → CTRL scenes
  if (id.startsWith("home-")) id = id.substring(5);
  if (id.startsWith("basement-")) id = id.substring(9);
  if (id == "normal") id = "chill";
  if (id == "party") id = "dance";
  if (id == "night") id = "movie";
  if (id == "all-off" || id == "all_off" || id == "house-off") id = "off";

  auto lightsThenAv = [&](const char *tag) {
    String avDetail;
    avSceneApply(id, avDetail);
    message = String(tag) + " armed";
    strncpy(lastSceneIdBuf, id.c_str(), sizeof(lastSceneIdBuf) - 1);
    lastSceneIdBuf[sizeof(lastSceneIdBuf) - 1] = '\0';
    strncpy(lastSceneTagBuf, tag, sizeof(lastSceneTagBuf) - 1);
    lastSceneTagBuf[sizeof(lastSceneTagBuf) - 1] = '\0';
    Serial.printf("[SCENE] %s | %s\n", tag, avDetail.c_str());
    return true;
  };

  if (id == "movie") {
    wizAllWhite(1, 2000);
    wledOff();
    hueBasementScene(true, 1, 500);
    return lightsThenAv("MOVIE");
  }
  if (id == "full") {
    wizAllWhite(100, 4500);
    wledFxColor(kFxMultiComet, 120, 160, 160, 255, 220, 200);
    hueBasementScene(true, 254, 250);
    return lightsThenAv("FULL");
  }
  if (id == "chill") {
    wizAllWhite(55, 2700);
    wledFxColor(kFxCandle, 40, 180, 90, 255, 140, 40);
    hueBasementScene(true, 120, -1, 255, 120, 40);
    return lightsThenAv("CHILL");
  }
  if (id == "dance") {
    wizAllScene(4, 100, 200);
    wledFxColor(kFxAurora, 110, 160, 140, 255, 0, 180);
    hueBasementScene(true, 254, -1, 255, 0, 220);
    hueSetEffect(HUE_BASEMENT_LAMP, "colorloop");
    return lightsThenAv("DANCE");
  }
  if (id == "game") {
    wizAllWhite(85, 4000);
    wledFxColor(kFxMultiComet, 100, 150, 130, 0, 255, 120);
    hueBasementScene(true, 200, 250);
    return lightsThenAv("GAME");
  }
  if (id == "sports") {
    wizAllWhite(90, 5000);
    wledSolid(20, 80, 255, 120);
    hueBasementScene(true, 220, 200);
    return lightsThenAv("SPORTS");
  }
  if (id == "date") {
    wizAllWhite(12, 2200);
    wledFxColor(kFxCandle, 30, 160, 50, 255, 100, 30);
    hueBasementScene(true, 40, -1, 255, 90, 40);
    return lightsThenAv("DATE");
  }
  if (id == "karaoke") {
    wizAllScene(4, 100, 180);
    wledFxColor(kFxAurora, 100, 160, 150, 255, 0, 200);
    hueBasementScene(true, 254, -1, 255, 0, 160);
    return lightsThenAv("KARAOKE");
  }
  if (id == "bed") {
    wizAll(false);
    wledOff();
    hueBasementScene(false, 0, 0);
    return lightsThenAv("BED");
  }
  if (id == "off") {
    wizAll(false);
    wledOff();
    hueAll(false);
    String avDetail;
    avSceneApply(id, avDetail);
    message = "OFF";
    strncpy(lastSceneIdBuf, "off", sizeof(lastSceneIdBuf) - 1);
    strncpy(lastSceneTagBuf, "OFF", sizeof(lastSceneTagBuf) - 1);
    Serial.printf("[SCENE] OFF | %s\n", avDetail.c_str());
    return true;
  }
  message = "unknown scene";
  return false;
}

const char *deviceHubLastSceneId() { return lastSceneIdBuf; }
const char *deviceHubLastSceneTag() { return lastSceneTagBuf; }

void deviceHubFillSummary(JsonObject obj) {
  int online = 0;
  int total = deviceCount;
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].online) online++;
  }
  obj["deviceOnline"] = online;
  obj["deviceTotal"] = total;
  obj["lastSceneId"] = lastSceneIdBuf;
  obj["lastSceneTag"] = lastSceneTagBuf[0] ? lastSceneTagBuf : "—";
  obj["coreOnline"] = true;
  bool deck = false;
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "cyberdeck" && devices[i].online) {
      deck = true;
      break;
    }
  }
  obj["deckOnline"] = deck;
}

bool deviceHubSetDevice(const String &id, bool on, String &message, int dimming, int bri) {
  Dev *d = findDev(id);
  if (!d) {
    message = "device not found";
    return false;
  }
  if (d->type == "wiz_bulb") {
    JsonDocument doc;
    doc["method"] = "setPilot";
    JsonObject p = doc["params"].to<JsonObject>();
    p["state"] = on;
    if (on && dimming >= 10) p["dimming"] = constrain(dimming, 10, 100);
    wizSend(d->ip, doc);
    message = on ? "bulb on" : "bulb off";
    return true;
  }
  if (d->type == "wled") {
    JsonDocument doc;
    doc["on"] = on;
    if (on && bri >= 0) doc["bri"] = constrain(bri, 1, 255);
    else if (on && dimming >= 10) doc["bri"] = map(constrain(dimming, 10, 100), 10, 100, 26, 255);
    bool ok = wledPostTo(d, doc);
    message = ok ? (on ? "strip on" : "strip off") : "wled fail";
    return ok;
  }
  if (d->type == "hue") {
    int b = 200;
    if (bri >= 0) b = constrain(bri, 1, 254);
    else if (dimming >= 10) b = map(constrain(dimming, 10, 100), 10, 100, 26, 254);
    bool ok = hueSetLight(d->hueId.c_str(), on, on ? b : -1, on ? 370 : -1);
    message = ok ? (on ? "hue on" : "hue off") : "hue fail";
    return ok;
  }
  if (d->type == "hue_group") {
    int b = 200;
    if (bri >= 0) b = constrain(bri, 1, 254);
    else if (dimming >= 10) b = map(constrain(dimming, 10, 100), 10, 100, 26, 254);
    bool ok = hueSetGroup(d->hueId.c_str(), on, on ? b : -1, on ? 370 : -1);
    message = ok ? (on ? "group on" : "group off") : "group fail";
    return ok;
  }
  if (d->type == "ha_entity") {
    if (!d->entityId.length() || !d->connector.length()) {
      message = "ha entity incomplete";
      return false;
    }
    String cpath = String("/homes/") + gridStoreActiveId() + "/connectors/index.json";
    String baseUrl;
    if (SD_MMC.exists(cpath)) {
      File f = SD_MMC.open(cpath, "r");
      JsonDocument cdoc;
      if (f && !deserializeJson(cdoc, f)) {
        for (JsonObject c : cdoc["connectors"].as<JsonArray>()) {
          if (!strcmp(c["id"] | "", d->connector.c_str())) {
            baseUrl = c["baseUrl"] | "";
            break;
          }
        }
      }
      if (f) f.close();
    }
    String vpath = String("/homes/") + gridStoreActiveId() + "/vault/" + d->connector + ".json";
    String token;
    if (SD_MMC.exists(vpath)) {
      File f = SD_MMC.open(vpath, "r");
      JsonDocument sdoc;
      if (f && !deserializeJson(sdoc, f)) token = sdoc["token"] | "";
      if (f) f.close();
    }
    if (!baseUrl.length() || !token.length()) {
      message = "ha connector/token missing";
      return false;
    }
    if (baseUrl.endsWith("/")) baseUrl.remove(baseUrl.length() - 1);
    String domain = d->entityId.substring(0, d->entityId.indexOf('.'));
    String url = baseUrl + "/api/services/" + domain + "/" + (on ? "turn_on" : "turn_off");
    JsonDocument body;
    body["entity_id"] = d->entityId;
    String payload;
    serializeJson(body, payload);
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(4000);
    if (!http.begin(client, url)) {
      message = "ha connect fail";
      return false;
    }
    http.addHeader("Authorization", String("Bearer ") + token);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    http.end();
    bool ok = code > 0 && code < 300;
    message = ok ? (on ? "ha on" : "ha off") : ("ha http " + String(code));
    return ok;
  }
  message = "unsupported type";
  return false;
}

bool deviceHubCameraSnapshot(const char *id, String &contentType, String &bytes, String &message) {
  Dev *d = findDev(id);
  if (!d || d->type != "camera") {
    message = "camera not found";
    return false;
  }
  if (d->ip == INADDR_NONE) {
    message = "no camera ip";
    return false;
  }
  const char *paths[] = {
      nullptr,  // filled with device snapshotPath
      "/image/jpeg.cgi",
      "/cgi-bin/snapshot.cgi",
      "/snapshot.jpg",
      "/jpg/image.jpg",
      "/onvif-http/snapshot",
  };
  String custom = d->snapshotPath;
  contentType = "image/jpeg";
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    String path;
    if (i == 0) {
      if (!custom.length()) continue;
      path = custom.startsWith("/") ? custom : ("/" + custom);
    } else {
      path = paths[i];
    }
    String url = "http://" + d->ip.toString() + path;
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(3500);
    if (!http.begin(client, url)) continue;
    int code = http.GET();
    if (code == 200) {
      bytes = http.getString();
      String ct = http.header("Content-Type");
      if (ct.length()) contentType = ct;
      http.end();
      if (bytes.length() > 100) {
        d->online = true;
        message = "ok";
        return true;
      }
    } else {
      http.end();
    }
  }
  message = "no snapshot path worked";
  return false;
}

bool deviceHubIdentify(const String &id, String &message) {
  Dev *d = findDev(id);
  if (!d) {
    message = "device not found";
    return false;
  }
  if (d->type == "hue") {
    bool ok = hueAlert(d->hueId.c_str(), "select");
    message = ok ? ("blinked " + d->name) : "hue identify fail";
    return ok;
  }
  if (d->type == "hue_group") {
    bool ok = hueSetGroup(d->hueId.c_str(), true, 200, 370);
    delay(400);
    ok = hueSetGroup(d->hueId.c_str(), false) && ok;
    message = ok ? ("pulsed " + d->name) : "group identify fail";
    return ok;
  }
  if (d->type == "wled") {
    JsonDocument off;
    off["on"] = false;
    bool ok = wledPostTo(d, off);
    delay(200);
    JsonDocument on;
    on["on"] = true;
    on["bri"] = 220;
    ok = wledPostTo(d, on) && ok;
    delay(200);
    on["bri"] = 80;
    ok = wledPostTo(d, on) && ok;
    message = ok ? ("pulsed " + d->name) : "wled identify fail";
    return ok;
  }
  if (d->type != "wiz_bulb") {
    message = "identify only for wiz/hue/wled";
    return false;
  }
  // blink: off-on-off-on
  for (int i = 0; i < 2; i++) {
    JsonDocument off;
    off["method"] = "setPilot";
    off["params"]["state"] = false;
    wizSend(d->ip, off);
    delay(250);
    JsonDocument on;
    on["method"] = "setPilot";
    on["params"]["state"] = true;
    on["params"]["dimming"] = 100;
    on["params"]["temp"] = 4000;
    wizSend(d->ip, on);
    delay(350);
  }
  message = "blinked " + d->name;
  return true;
}

bool deviceHubDeckIrReplay(String &message);

bool deviceHubDeckVizio(const char *action, String &message) {
  if (!deckDev()) {
    message = "no cyberdeck";
    return false;
  }
  if (action && !strcmp(action, "REPLAY")) return deviceHubDeckIrReplay(message);
  char payload[96];
  snprintf(payload, sizeof(payload), "{\"action\":\"%s\"}", action ? action : "MUTE");
  bool ok = deckPost("/api/ir/vizio", payload);
  message = ok ? String("deck ") + action : "deck ir fail";
  return ok;
}

bool deviceHubDeckIrReplay(String &message) {
  if (!deckDev()) {
    message = "no cyberdeck";
    return false;
  }
  bool ok = deckPost("/api/ir/replay", "{}");
  message = ok ? "ir replay" : "ir replay fail";
  return ok;
}

bool deviceHubDeckRf(const char *cmd, float mhz, String &message) {
  if (!deckDev()) {
    message = "no cyberdeck";
    return false;
  }
  if (!cmd) {
    message = "cmd required";
    return false;
  }
  if (!strcmp(cmd, "freq") || !strcmp(cmd, "sniff_315") || !strcmp(cmd, "sniff_390") ||
      !strcmp(cmd, "sniff_433")) {
    float f = mhz;
    if (!strcmp(cmd, "sniff_315")) f = 315.0f;
    else if (!strcmp(cmd, "sniff_390")) f = 390.0f;
    else if (!strcmp(cmd, "sniff_433")) f = 433.92f;
    char payload[48];
    snprintf(payload, sizeof(payload), "{\"mhz\":%.3f}", f);
    if (!deckPost("/api/rf/freq", payload)) {
      message = "rf freq fail";
      return false;
    }
    if (!strcmp(cmd, "freq")) {
      message = "rf freq set";
      return true;
    }
    bool ok = deckPost("/api/rf/sniff", "{\"on\":true}");
    message = ok ? "rf sniff on" : "rf sniff fail";
    return ok;
  }
  if (!strcmp(cmd, "sniff_off")) {
    bool ok = deckPost("/api/rf/sniff", "{\"on\":false}");
    message = ok ? "rf sniff off" : "rf sniff fail";
    return ok;
  }
  if (!strcmp(cmd, "replay")) {
    bool ok = deckPost("/api/rf/replay", "{}");
    message = ok ? "rf replay" : "rf replay fail";
    return ok;
  }
  if (!strcmp(cmd, "test")) {
    bool ok = deckPost("/api/rf/test", "{}");
    message = ok ? "rf test" : "rf test fail";
    return ok;
  }
  message = "unknown rf cmd";
  return false;
}

bool deviceHubRecovery(const String &action, String &message) {
  if (action == "reset_normal" || action == "resync") {
    if (action == "resync") deviceHubRefreshOnline();
    return deviceHubRunScene("basement-normal", message);
  }
  if (action == "power_cycle") {
    deviceHubRunScene("basement-all-off", message);
    delay(500);
    return deviceHubRunScene("basement-normal", message);
  }
  if (action == "fix_bulbs") {
    wizAllWhite(100, 3000);
    message = "bulbs normal";
    return true;
  }
  if (action == "fix_strip") {
    wledNormal();
    message = "strip normal";
    return true;
  }
  if (action == "replay_party") return deviceHubRunScene("basement-party", message);
  message = "unknown recovery";
  return false;
}

void deviceHubRunLabSmoke(JsonObject out) {
  deviceHubRefreshOnline();
  JsonArray steps = out["steps"].to<JsonArray>();
  int pass = 0, fail = 0;

  auto step = [&](const char *name, bool ok, const char *detail) {
    JsonObject s = steps.add<JsonObject>();
    s["name"] = name;
    s["ok"] = ok;
    s["detail"] = detail;
    if (ok) pass++;
    else fail++;
  };

  int wizOnline = 0;
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "wiz_bulb" && devices[i].online) wizOnline++;
  }
  step("wiz_discovery", wizOnline > 0, String(wizOnline).c_str());

  Dev *wled = wledDev();
  step("wled_reach", wled && wled->online, wled ? wled->ip.toString().c_str() : "missing");

  bool deck = false;
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type == "cyberdeck" && devices[i].online) deck = true;
  }
  step("cyberdeck_reach", deck, deck ? "uplink" : "dark");

  String msg;
  step("scene_night", deviceHubRunScene("basement-night", msg), msg.c_str());
  delay(800);
  step("scene_normal", deviceHubRunScene("basement-normal", msg), msg.c_str());
  delay(800);
  step("scene_all_off", deviceHubRunScene("basement-all-off", msg), msg.c_str());

  if (wizOnline > 0) {
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].type == "wiz_bulb" && devices[i].online) {
        step("identify_wiz", deviceHubIdentify(devices[i].id, msg), msg.c_str());
        break;
      }
    }
  }

  out["pass"] = pass;
  out["fail"] = fail;
  out["ok"] = fail == 0;
}

bool deviceHubAvApp(const char *appId, String &message) { return avLaunchApp(appId, message); }

bool deviceHubAvVol(int delta, int &levelOut, String &message) {
  return avVolDelta(delta, levelOut, message);
}

bool deviceHubAvWatch(String &message) { return avEnsureWatching(message); }

bool deviceHubAvInput(const char *target, String &message) {
  if (!target) target = "fire";
  String t = target;
  t.toLowerCase();
  if (t == "ps5") return avEnsurePs5(message);
  return avEnsureWatching(message);
}

bool deviceHubAvKey(const char *name, String &message) {
  if (!name) {
    message = "no key";
    return false;
  }
  String n = name;
  n.toUpperCase();
  // Fire TV nav via ADB first. Older CyberDeck builds reject HOME/BACK/OK as
  // "Unknown Vizio action", and that failed IR attempt alone cost ~1s.
  if (n == "HOME") return avFireKey(3, message);
  if (n == "BACK") return avFireKey(4, message);
  if (n == "OK" || n == "ENTER") return avFireKey(23, message);
  if (n == "UP") return avFireKey(19, message);
  if (n == "DOWN") return avFireKey(20, message);
  if (n == "LEFT") return avFireKey(21, message);
  if (n == "RIGHT") return avFireKey(22, message);
  if (n == "MORE" || n == "MENU") return avFireKey(82, message);
  // Power / vol / input still go through CyberDeck IR when available.
  return deviceHubDeckVizio(name, message);
}

void deviceHubFillWledDevices(JsonArray arr) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].type != "wled") continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = devices[i].id;
    o["name"] = devices[i].name;
    o["ip"] = devices[i].ip.toString();
    o["online"] = devices[i].online;
    o["zoneId"] = devices[i].zoneId;
  }
}

bool deviceHubWledFillState(const char *id, JsonObject out, String &message) {
  Dev *d = findWled(id);
  if (!d) {
    message = "no wled device";
    return false;
  }
  String url = "http://" + d->ip.toString();
  if (d->port && d->port != 80) url += ":" + String(d->port);
  url += "/json/state";
  String body;
  if (!httpGetBody(url, body, 2200)) {
    d->online = false;
    message = "wled unreachable";
    out["ok"] = false;
    out["id"] = d->id;
    out["name"] = d->name;
    out["ip"] = d->ip.toString();
    out["online"] = false;
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "bad wled json";
    return false;
  }
  d->online = true;
  out["ok"] = true;
  out["id"] = d->id;
  out["name"] = d->name;
  out["ip"] = d->ip.toString();
  out["online"] = true;
  out["on"] = doc["on"] | false;
  out["bri"] = doc["bri"] | 0;
  out["ps"] = doc["ps"] | -1;
  JsonObject seg0;
  if (doc["seg"].is<JsonArray>() && doc["seg"].as<JsonArray>().size() > 0) {
    seg0 = doc["seg"][0].as<JsonObject>();
  }
  out["fx"] = seg0["fx"] | 0;
  out["sx"] = seg0["sx"] | 128;
  out["ix"] = seg0["ix"] | 128;
  out["pal"] = seg0["pal"] | 0;
  uint8_t r = 255, g = 200, b = 140;
  if (seg0["col"].is<JsonArray>() && seg0["col"].as<JsonArray>().size() > 0) {
    JsonVariant c0 = seg0["col"][0];
    if (c0.is<JsonArray>() && c0.as<JsonArray>().size() >= 3) {
      r = c0[0] | 255;
      g = c0[1] | 200;
      b = c0[2] | 140;
    }
  }
  out["r"] = r;
  out["g"] = g;
  out["b"] = b;
  message = "ok";
  return true;
}

bool deviceHubWledSet(JsonVariantConst patch, String &message) {
  const char *id = patch["id"] | "";
  Dev *d = findWled(id);
  if (!d) {
    message = "no wled device";
    return false;
  }

  JsonDocument doc;
  bool touchSeg = false;
  JsonObject seg;

  if (!patch["on"].isNull()) doc["on"] = patch["on"] | false;
  if (!patch["bri"].isNull()) {
    doc["bri"] = constrain((int)(patch["bri"] | 128), 1, 255);
    if (patch["on"].isNull()) doc["on"] = true;
  }
  if (!patch["ps"].isNull()) {
    int ps = patch["ps"] | 0;
    doc["ps"] = constrain(ps, 0, 250);
    if (patch["on"].isNull()) doc["on"] = true;
  }

  auto needSeg = [&]() {
    if (!touchSeg) {
      touchSeg = true;
      seg = doc["seg"].add<JsonObject>();
      seg["id"] = 0;
    }
  };

  if (!patch["fx"].isNull()) {
    needSeg();
    seg["fx"] = constrain((int)(patch["fx"] | 0), 0, 255);
    if (patch["on"].isNull()) doc["on"] = true;
  }
  if (!patch["sx"].isNull()) {
    needSeg();
    seg["sx"] = constrain((int)(patch["sx"] | 128), 0, 255);
  }
  if (!patch["ix"].isNull()) {
    needSeg();
    seg["ix"] = constrain((int)(patch["ix"] | 128), 0, 255);
  }
  if (!patch["pal"].isNull()) {
    needSeg();
    seg["pal"] = constrain((int)(patch["pal"] | 0), 0, 255);
  }

  bool hasColor = !patch["r"].isNull() || !patch["g"].isNull() || !patch["b"].isNull() ||
                  (patch["solid"] | false);
  if (hasColor) {
    needSeg();
    uint8_t r = patch["r"] | 255;
    uint8_t g = patch["g"] | 200;
    uint8_t b = patch["b"] | 140;
    if (patch["solid"] | false) seg["fx"] = 0;
    JsonArray col = seg["col"].to<JsonArray>();
    JsonArray rgb = col.add<JsonArray>();
    rgb.add(r);
    rgb.add(g);
    rgb.add(b);
    if (patch["on"].isNull()) doc["on"] = true;
  }

  if (doc.as<JsonObject>().size() == 0) {
    message = "empty patch";
    return false;
  }

  bool ok = wledPostTo(d, doc);
  d->online = ok;
  message = ok ? ("wled " + d->name) : "wled fail";
  return ok;
}
