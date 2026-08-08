#include "web_portal.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>

#include "automation_engine.h"
#include "connector_store.h"
#include "device_hub.h"
#include "grace_game_page.h"
#include "grace_session.h"
#include "grid_store.h"
#include "grideye_lite.h"
#include "party_tricks.h"
#include "portal_page.h"
#include "relay_client.h"
#include "wifi_manager.h"

static WebServer server(80);
static bool sdMounted = false;
static float sdGb = 0;
static bool seedReady = false;

void webPortalSetSd(bool ok, float gb, bool seed) {
  sdMounted = ok;
  sdGb = gb;
  seedReady = seed;
}

static void bindSsidToHome(const String &ssid) {
  // Create or activate a blank grid for this SSID (new-home arrival path).
  String msg;
  const char *gn = nullptr;
  // Optional name from last create; otherwise use SSID
  if (!gridStoreOnWifiJoin(ssid.c_str(), gn, msg)) {
    Serial.printf("[GRID] wifi join grid fail: %s\n", msg.c_str());
    return;
  }
  Serial.printf("[GRID] %s\n", msg.c_str());
}

static void sendPortal() {
  server.send_P(200, "text/html", PORTAL_HTML);
}

static void handleRoot() { sendPortal(); }

static void handleStatus() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["product"] = "Overlink Core";
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifiFillStatus(wifi);
  JsonObject sd = doc["sd"].to<JsonObject>();
  sd["ok"] = sdMounted;
  sd["sizeGb"] = sdGb;
  sd["seed"] = seedReady;
  JsonObject grid = doc["grid"].to<JsonObject>();
  if (seedReady) {
    grid["id"] = gridStoreActiveId();
    grid["name"] = gridStoreActiveId();
    String hp = gridStorePath("home.json");
    if (SD_MMC.exists(hp)) {
      File f = SD_MMC.open(hp, "r");
      JsonDocument h;
      if (f && !deserializeJson(h, f)) grid["name"] = h["name"] | gridStoreActiveId();
      if (f) f.close();
    }
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleScan() {
  JsonDocument doc;
  JsonArray nets = doc["networks"].to<JsonArray>();
  wifiScanTo(nets);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleWifiSave() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";
  if (!ssid.length()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }
  if (!wifiSaveNetwork(ssid, pass)) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
    return;
  }
  bindSsidToHome(ssid);
  server.send(200, "application/json",
              "{\"ok\":true,\"reboot\":true,\"message\":\"rebooting into STA\"}");
  delay(400);
  ESP.restart();
}

static void sendSdJson(const char *path) {
  if (!sdMounted || !SD_MMC.exists(path)) {
    server.send(404, "application/json", "{\"ok\":false}");
    return;
  }
  File f = SD_MMC.open(path, "r");
  if (!f) {
    server.send(500, "application/json", "{\"ok\":false}");
    return;
  }
  String body = f.readString();
  f.close();
  server.send(200, "application/json", body);
}

static void handleGrid() {
  String p = gridStorePath("devices.json");
  sendSdJson(p.c_str());
}
static void handleHome() {
  String p = gridStorePath("home.json");
  sendSdJson(p.c_str());
}
static void handleZones() {
  String p = gridStorePath("rooms.json");
  sendSdJson(p.c_str());
}
static void handleScenes() {
  String p = gridStorePath("scenes.json");
  sendSdJson(p.c_str());
}

static void handleHomeSummary() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["gridId"] = gridStoreActiveId();
  doc["gridName"] = gridStoreActiveId();
  {
    String hp = gridStorePath("home.json");
    if (SD_MMC.exists(hp)) {
      File f = SD_MMC.open(hp, "r");
      JsonDocument h;
      if (f && !deserializeJson(h, f)) doc["gridName"] = h["name"] | gridStoreActiveId();
      if (f) f.close();
    }
  }
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifiFillStatus(wifi);
  JsonObject sum = doc["summary"].to<JsonObject>();
  deviceHubFillSummary(sum);
  JsonArray zones = doc["zones"].to<JsonArray>();
  String rp = gridStorePath("rooms.json");
  if (SD_MMC.exists(rp)) {
    File f = SD_MMC.open(rp, "r");
    if (f) {
      JsonDocument rooms;
      if (!deserializeJson(rooms, f)) {
        for (JsonObject rz : rooms["zones"].as<JsonArray>()) {
          JsonObject z = zones.add<JsonObject>();
          z["id"] = rz["id"] | "";
          z["name"] = rz["name"] | z["id"];
        }
      }
      f.close();
    }
  }
  if (!zones.size()) {
    JsonObject z = zones.add<JsonObject>();
    z["id"] = "main";
    z["name"] = "Main";
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleDevices() {
  JsonDocument doc;
  doc["ok"] = true;
  JsonArray arr = doc["devices"].to<JsonArray>();
  deviceHubFillDevices(arr);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleProbe() {
  deviceHubRefreshOnline();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleSceneRun() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String id = doc["id"] | "";
  String msg;
  bool ok = deviceHubRunScene(id, msg);
  JsonDocument out;
  out["ok"] = ok;
  out["message"] = msg;
  String body;
  serializeJson(out, body);
  server.send(ok ? 200 : 400, "application/json", body);
}

static void sendJsonOk(bool ok, const String &msg) {
  JsonDocument out;
  out["ok"] = ok;
  out["message"] = msg;
  String body;
  serializeJson(out, body);
  server.send(ok ? 200 : 400, "application/json", body);
}

static void handleDeviceSet() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String id = doc["id"] | "";
  bool on = doc["on"] | false;
  int dimming = doc["dimming"] | -1;
  int bri = doc["bri"] | -1;
  String msg;
  bool ok = deviceHubSetDevice(id, on, msg, dimming, bri);
  sendJsonOk(ok, msg);
}

static void handleThemes() {
  JsonDocument doc;
  doc["ok"] = true;
  JsonArray arr = doc["themes"].to<JsonArray>();
  deviceHubFillThemes(arr);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleThemeRun() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String id = doc["id"] | "";
  String msg;
  sendJsonOk(deviceHubRunTheme(id, msg), msg);
}

static void handleRecovery() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String action = doc["action"] | "";
  String msg;
  sendJsonOk(deviceHubRecovery(action, msg), msg);
}

static void handleDeckRf() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  const char *cmd = doc["cmd"] | "";
  float mhz = doc["mhz"] | 0.0f;
  String msg;
  sendJsonOk(deviceHubDeckRf(cmd, mhz, msg), msg);
}

static void handleIdentify() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String id = doc["id"] | "";
  String msg;
  bool ok = deviceHubIdentify(id, msg);
  JsonDocument out;
  out["ok"] = ok;
  out["message"] = msg;
  String body;
  serializeJson(out, body);
  server.send(ok ? 200 : 400, "application/json", body);
}

static void ensureGraceDirs() {
  if (!sdMounted) return;
  if (!SD_MMC.exists("/games")) SD_MMC.mkdir("/games");
  if (!SD_MMC.exists("/games/grace")) SD_MMC.mkdir("/games/grace");
  if (!SD_MMC.exists("/games/grace/decks")) SD_MMC.mkdir("/games/grace/decks");
}

static void sendGraceIndex() { server.send_P(200, "text/html", GRACE_GAME_HTML); }

static bool serveGraceSdFile(const String &uri) {
  if (!uri.startsWith("/games/grace/")) return false;
  if (uri == "/games/grace/" || uri == "/games/grace") {
    sendGraceIndex();
    return true;
  }
  if (!sdMounted || !SD_MMC.exists(uri)) {
    server.send(404, "text/plain", "missing file — run tools/push_grace_games.sh");
    return true;
  }
  File f = SD_MMC.open(uri, "r");
  if (!f) {
    server.send(500, "text/plain", "open fail");
    return true;
  }
  const char *ctype = "application/octet-stream";
  if (uri.endsWith(".json")) ctype = "application/json";
  else if (uri.endsWith(".html")) ctype = "text/html";
  server.streamFile(f, ctype);
  f.close();
  return true;
}

static File graceUploadFile;
static void handleGraceUploadWrite() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    ensureGraceDirs();
    String name = server.arg("name");
    // allow only safe relative paths under /games/grace/
    if (!name.length() || name.indexOf("..") >= 0 || name[0] == '/') {
      return;
    }
    String path = String("/games/grace/") + name;
    // create parent for decks/*
    graceUploadFile = SD_MMC.open(path, FILE_WRITE);
    Serial.printf("[GRACE] upload start %s\n", path.c_str());
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (graceUploadFile) graceUploadFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (graceUploadFile) {
      graceUploadFile.close();
      Serial.printf("[GRACE] upload end %u bytes\n", (unsigned)u.totalSize);
    }
  }
}

static void handleNotFound() {
  if (server.method() == HTTP_GET && serveGraceSdFile(server.uri())) return;
  if (server.method() == HTTP_GET) {
    sendPortal();
    return;
  }
  server.send(404, "text/plain", "not found");
}

void webPortalBegin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/canonical.html", HTTP_GET, handleRoot);
  server.on("/ncsi.txt", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/api/grid", HTTP_GET, handleGrid);
  server.on("/api/home", HTTP_GET, handleHome);
  server.on("/api/home/summary", HTTP_GET, handleHomeSummary);
  server.on("/api/zones", HTTP_GET, handleZones);
  server.on("/api/scenes", HTTP_GET, handleScenes);
  server.on("/api/themes", HTTP_GET, handleThemes);
  server.on("/api/themes/run", HTTP_POST, handleThemeRun);
  server.on("/api/devices", HTTP_GET, handleDevices);
  server.on("/api/devices/probe", HTTP_POST, handleProbe);
  server.on("/api/scenes/run", HTTP_POST, handleSceneRun);
  server.on("/api/devices/set", HTTP_POST, handleDeviceSet);
  server.on("/api/devices/identify", HTTP_POST, handleIdentify);
  server.on("/api/devices/replace", HTTP_POST, []() {
    String msg;
    bool ok = deviceHubReplaceDevicesJson(server.arg("plain"), msg);
    sendJsonOk(ok, msg);
  });
  server.on("/api/devices/add", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(deviceHubAddDevice(doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on("/api/devices/update", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(deviceHubUpdateDevice(doc["id"] | "", doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on("/api/devices/remove", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(deviceHubRemoveDevice(doc["id"] | "", msg), msg);
  });
  server.on("/api/hue/sync", HTTP_POST, []() {
    String msg;
    sendJsonOk(deviceHubHueSync(msg), msg);
  });
  server.on("/api/hue/pair", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg;
    sendJsonOk(deviceHubHuePair(doc["ip"] | "", msg), msg);
  });
  server.on("/api/arrival", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    doc["pending"] = gridStoreArrivalPending();
    doc["grid"] = gridStoreActiveId();
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/arrival/done", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg;
    bool done = doc["done"].isNull() ? true : (doc["done"] | true);
    sendJsonOk(gridStoreSetArrivalDone(done, msg), msg);
  });
  server.on("/api/zones/upsert", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(gridStoreUpsertZone(doc["id"] | "", doc["name"] | "", msg), msg);
  });
  server.on("/api/connectors", HTTP_GET, []() {
    connectorStoreBegin();
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray arr = doc["connectors"].to<JsonArray>();
    connectorStoreFill(arr);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/connectors/upsert", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(connectorStoreUpsert(doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on("/api/connectors/remove", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg;
    sendJsonOk(connectorStoreRemove(doc["id"] | "", msg), msg);
  });
  server.on("/api/connectors/secret", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(connectorStoreSetSecret(doc["id"] | "", doc["secret"], msg), msg);
  });
  server.on("/api/connectors/ha/import", HTTP_POST, []() {
    String msg;
    sendJsonOk(connectorStoreHaImport(msg), msg);
  });
  server.on("/api/relay/status", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject st = doc["relay"].to<JsonObject>();
    relayClientFillStatus(st);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/relay/configure", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(relayClientConfigure(doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on("/api/relay/enroll", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg;
    sendJsonOk(relayClientEnroll(doc["code"] | "", msg), msg);
  });
  server.on("/api/relay/expose", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg;
    sendJsonOk(relayClientSetEnabled(doc["enabled"] | false, msg), msg);
  });
  server.on("/api/party/status", HTTP_GET, []() {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    partyTricksFillStatus(o);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/party/sweep", HTTP_POST, []() {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    String msg;
    bool ok = partyTricksSweep(o, msg);
    o["ok"] = ok;
    o["message"] = msg;
    String body;
    serializeJson(doc, body);
    server.send(ok ? 200 : 500, "application/json", body);
  });
  server.on("/api/party/ble/start", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg;
    sendJsonOk(partyTricksBleStart(doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on("/api/party/ble/stop", HTTP_POST, []() {
    String msg;
    sendJsonOk(partyTricksBleStop(msg), msg);
  });
  server.on("/api/party/stampede", HTTP_POST, []() {
    String msg;
    sendJsonOk(partyTricksStampede(msg), msg);
  });
  server.on("/api/party/cast", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String msg;
    sendJsonOk(partyTricksCastStinger(doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on("/api/party/printers", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray arr = doc["printers"].to<JsonArray>();
    String msg;
    partyTricksFindPrinters(arr, msg);
    doc["message"] = msg;
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/party/print", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(partyTricksPrint(doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on("/api/cameras/snapshot", HTTP_GET, []() {
    String id = server.hasArg("id") ? server.arg("id") : "";
    String ct, bytes, msg;
    if (!deviceHubCameraSnapshot(id.c_str(), ct, bytes, msg)) {
      sendJsonOk(false, msg);
      return;
    }
    server.send(200, ct.c_str(), bytes);
  });
  server.on("/api/discover/devices", HTTP_GET, []() {
    JsonDocument doc;
    JsonObject out = doc.to<JsonObject>();
    deviceHubFillDiscover(out);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/tools/recovery", HTTP_POST, handleRecovery);
  server.on("/api/deck/rf", HTTP_POST, handleDeckRf);
  server.on("/api/deck/ir", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    const char *action = doc["action"] | "MUTE";
    String msg;
    sendJsonOk(deviceHubDeckVizio(action, msg), msg);
  });
  server.on("/api/av/watch", HTTP_POST, []() {
    String msg;
    sendJsonOk(deviceHubAvWatch(msg), msg);
  });
  server.on("/api/av/input", HTTP_POST, []() {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    const char *target = doc["target"] | "fire";
    String msg;
    sendJsonOk(deviceHubAvInput(target, msg), msg);
  });
  server.on("/api/av/app", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    const char *id = doc["id"] | "";
    String msg;
    sendJsonOk(deviceHubAvApp(id, msg), msg);
  });
  server.on("/api/av/vol", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    int delta = doc["delta"] | 0;
    int level = -1;
    String msg;
    bool ok = deviceHubAvVol(delta, level, msg);
    JsonDocument out;
    out["ok"] = ok;
    out["message"] = msg;
    if (level >= 0) out["level"] = level;
    String body;
    serializeJson(out, body);
    server.send(ok ? 200 : 500, "application/json", body);
  });
  server.on("/api/av/key", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    const char *name = doc["name"] | "";
    String msg;
    sendJsonOk(deviceHubAvKey(name, msg), msg);
  });
  server.on("/api/lab/smoke", HTTP_POST, []() {
    JsonDocument doc;
    JsonObject out = doc.to<JsonObject>();
    deviceHubRunLabSmoke(out);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/grids", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray arr = doc["grids"].to<JsonArray>();
    gridStoreFillList(arr);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/grids/create", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    bool ok = gridStoreCreate(doc["name"] | "", doc["ssid"] | "", msg);
    sendJsonOk(ok, msg);
  });
  server.on("/api/grids/rename", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    bool ok = gridStoreRenameHome(doc["name"] | "", msg);
    sendJsonOk(ok, msg);
  });
  server.on("/api/grids/activate", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    bool ok = gridStoreActivate(doc["id"] | "", msg);
    if (ok) {
      deviceHubInvalidateHueCreds();
      deviceHubLoadFromSd();
      deviceHubRefreshOnline();
    }
    sendJsonOk(ok, msg);
  });
  server.on("/api/grids/bind", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    bool ok = gridStoreBindSsid(doc["id"] | "", doc["ssid"] | "", msg);
    sendJsonOk(ok, msg);
  });
  server.on("/api/discover", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray known = doc["known"].to<JsonArray>();
    deviceHubFillDevices(known);
    JsonArray nets = doc["networks"].to<JsonArray>();
    wifiScanTo(nets);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/automations", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray arr = doc["automations"].to<JsonArray>();
    automationFillList(arr);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/automations/enable", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    bool ok = automationSetEnabled(doc["id"] | "", doc["enabled"] | false, msg);
    sendJsonOk(ok, msg);
  });
  server.on("/api/automations/run", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    bool ok = automationRunNow(doc["id"] | "", msg);
    sendJsonOk(ok, msg);
  });
  server.on("/api/grideye/summary", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject sum = doc["summary"].to<JsonObject>();
    grideyeLiteFillSummary(sum);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/grideye/scan", HTTP_POST, []() {
    String msg;
    bool ok = grideyeLiteScanWifi(msg);
    sendJsonOk(ok, msg);
  });
  server.on("/api/wled/devices", HTTP_GET, []() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray arr = doc["devices"].to<JsonArray>();
    deviceHubFillWledDevices(arr);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/wled/state", HTTP_GET, []() {
    String id = server.hasArg("id") ? server.arg("id") : "";
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    String msg;
    bool ok = deviceHubWledFillState(id.c_str(), o, msg);
    if (!o["ok"].is<bool>()) o["ok"] = ok;
    o["message"] = msg;
    String body;
    serializeJson(doc, body);
    server.send(ok ? 200 : 502, "application/json", body);
  });
  server.on("/api/wled/set", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false,\"message\":\"bad json\"}");
      return;
    }
    String msg;
    bool ok = deviceHubWledSet(doc.as<JsonVariantConst>(), msg);
    JsonDocument out;
    out["ok"] = ok;
    out["message"] = msg;
    // echo live state when possible
    JsonObject st = out["state"].to<JsonObject>();
    String ignore;
    deviceHubWledFillState(doc["id"] | "", st, ignore);
    String body;
    serializeJson(out, body);
    server.send(ok ? 200 : 400, "application/json", body);
  });
  server.on("/games/grace", HTTP_GET, sendGraceIndex);
  server.on("/games/grace/", HTTP_GET, sendGraceIndex);
  server.on("/api/games/grace/state", HTTP_GET, []() {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    graceSessionFill(o);
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
  });
  server.on("/api/games/grace/state", HTTP_POST, []() {
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    String msg;
    sendJsonOk(graceSessionUpdate(doc.as<JsonVariantConst>(), msg), msg);
  });
  server.on(
      "/api/games/grace/upload", HTTP_POST,
      []() {
        ensureGraceDirs();
        server.send(200, "application/json", "{\"ok\":true}");
      },
      handleGraceUploadWrite);
  ensureGraceDirs();
  graceSessionBegin();
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[WEB] portal on :80");
}

void webPortalLoop() { server.handleClient(); }
