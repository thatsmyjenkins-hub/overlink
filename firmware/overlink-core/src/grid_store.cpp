#include "grid_store.h"

#include <Preferences.h>
#include <SD_MMC.h>

#include "seed_home.h"
#include "wifi_manager.h"

static char activeId[32] = "home";
static char pathBuf[96];
static Preferences gridPrefs;

static bool writeSeedFile(const char *path, const char *progmemJson) {
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;
  String body = FPSTR(progmemJson);
  size_t n = f.print(body);
  f.close();
  return n > 0;
}

static void sanitizeId(const char *name, char *id, size_t idLen) {
  size_t j = 0;
  for (const char *p = name; *p && j + 1 < idLen; p++) {
    char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      id[j++] = (char)tolower(c);
    else if (c == ' ' || c == '-' || c == '_')
      id[j++] = '-';
  }
  while (j > 0 && id[j - 1] == '-') j--;
  id[j] = '\0';
}

const char *gridStorePathFor(const char *gridId, const char *file) {
  const char *gid = (gridId && gridId[0]) ? gridId : "home";
  const char *fn = (file && file[0]) ? file : "home.json";
  snprintf(pathBuf, sizeof(pathBuf), "/homes/%s/%s", gid, fn);
  return pathBuf;
}

const char *gridStorePath(const char *file) { return gridStorePathFor(activeId, file); }

void gridStoreBegin() {
  if (!SD_MMC.exists("/homes")) SD_MMC.mkdir("/homes");
  gridPrefs.begin("overlink", false);
  String a = gridPrefs.getString("grid", "home");
  strlcpy(activeId, a.c_str(), sizeof(activeId));
  // Ensure active grid directory exists
  String base = String("/homes/") + activeId;
  if (!SD_MMC.exists(base)) {
    strlcpy(activeId, "home", sizeof(activeId));
    gridPrefs.putString("grid", activeId);
  }
}

const char *gridStoreActiveId() { return activeId; }

void gridStoreFillList(JsonArray arr) {
  if (!SD_MMC.exists("/homes/index.json")) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = "home";
    o["name"] = "Home Sprawl";
    o["ssid"] = "";
    o["active"] = !strcmp(activeId, "home");
    return;
  }
  File f = SD_MMC.open("/homes/index.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return;
  }
  f.close();
  for (JsonObject g : doc["grids"].as<JsonArray>()) {
    JsonObject o = arr.add<JsonObject>();
    const char *id = g["id"] | "home";
    o["id"] = id;
    o["name"] = g["name"] | "Grid";
    o["ssid"] = g["ssid"] | "";
    o["active"] = !strcmp(activeId, id);
  }
}

static bool writeBlankHomeJson(const char *path, const char *id, const char *name,
                               const char *ssid) {
  JsonDocument doc;
  doc["id"] = id;
  doc["name"] = name;
  if (ssid && ssid[0])
    doc["ssid"] = ssid;
  else
    doc["ssid"] = nullptr;
  doc["timezone"] = "UTC";
  doc["notes"] = "Blank grid — Arrival Wizard will discover devices";
  doc["arrivalDone"] = false;
  File f = SD_MMC.open(path, "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

static bool appendIndex(const char *id, const char *name, const char *ssid) {
  JsonDocument idoc;
  if (SD_MMC.exists("/homes/index.json")) {
    File idx = SD_MMC.open("/homes/index.json", "r");
    deserializeJson(idoc, idx);
    idx.close();
  } else {
    idoc["version"] = 1;
    idoc["grids"].to<JsonArray>();
  }
  JsonArray grids = idoc["grids"].as<JsonArray>();
  if (grids.isNull()) grids = idoc["grids"].to<JsonArray>();
  for (JsonObject g : grids) {
    if (!strcmp(g["id"] | "", id)) {
      g["name"] = name;
      if (ssid) g["ssid"] = ssid;
      File out = SD_MMC.open("/homes/index.json", "w");
      if (!out) return false;
      serializeJsonPretty(idoc, out);
      out.close();
      return true;
    }
  }
  JsonObject g = grids.add<JsonObject>();
  g["id"] = id;
  g["name"] = name;
  if (ssid) g["ssid"] = ssid;
  g["path"] = id;
  File out = SD_MMC.open("/homes/index.json", "w");
  if (!out) return false;
  serializeJsonPretty(idoc, out);
  out.close();
  return true;
}

bool gridStoreCreateBlank(const char *name, const char *ssid, String &message) {
  if (!name || !name[0]) {
    message = "name required";
    return false;
  }
  char id[32];
  sanitizeId(name, id, sizeof(id));
  if (!id[0]) {
    message = "bad id";
    return false;
  }

  String base = String("/homes/") + id;
  if (SD_MMC.exists(base + "/devices.json")) {
    // Already exists — activate + bind ssid
    if (ssid && ssid[0]) gridStoreBindSsid(id, ssid, message);
    gridStoreActivate(id, message);
    message = String("exists ") + id;
    return true;
  }
  SD_MMC.mkdir(base.c_str());
  if (!writeBlankHomeJson((base + "/home.json").c_str(), id, name, ssid)) {
    message = "home write fail";
    return false;
  }
  writeSeedFile((base + "/rooms.json").c_str(),
                "{\"zones\":[{\"id\":\"main\",\"name\":\"Main\",\"sort\":0}]}");
  writeSeedFile((base + "/devices.json").c_str(), "{\"devices\":[]}");
  writeSeedFile((base + "/scenes.json").c_str(), "{\"scenes\":[]}");
  writeSeedFile((base + "/automations.json").c_str(), "{\"automations\":[]}");
  SD_MMC.mkdir((base + "/vault").c_str());
  SD_MMC.mkdir((base + "/connectors").c_str());
  appendIndex(id, name, ssid);
  strlcpy(activeId, id, sizeof(activeId));
  gridPrefs.putString("grid", activeId);
  message = String("blank ") + id;
  return true;
}

bool gridStoreCreate(const char *name, const char *ssid, String &message) {
  // Prefer blank for new homes; keep seed only when explicitly creating "home" first boot.
  return gridStoreCreateBlank(name, ssid, message);
}

bool gridStoreRenameHome(const char *name, String &message) {
  if (!name || !name[0]) {
    message = "name required";
    return false;
  }
  const char *path = gridStorePath("home.json");
  if (!SD_MMC.exists(path)) {
    message = "no home";
    return false;
  }
  File f = SD_MMC.open(path, "r");
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    message = "bad home.json";
    return false;
  }
  f.close();
  doc["name"] = name;
  f = SD_MMC.open(gridStorePath("home.json"), "w");
  if (!f) {
    message = "write fail";
    return false;
  }
  serializeJsonPretty(doc, f);
  f.close();

  if (SD_MMC.exists("/homes/index.json")) {
    File idx = SD_MMC.open("/homes/index.json", "r");
    JsonDocument idoc;
    if (!deserializeJson(idoc, idx)) {
      idx.close();
      for (JsonObject g : idoc["grids"].as<JsonArray>()) {
        if (!strcmp(g["id"] | "", activeId)) {
          g["name"] = name;
          break;
        }
      }
      File out = SD_MMC.open("/homes/index.json", "w");
      if (out) {
        serializeJsonPretty(idoc, out);
        out.close();
      }
    } else {
      idx.close();
    }
  }
  message = "renamed";
  return true;
}

bool gridStoreBindSsid(const char *id, const char *ssid, String &message) {
  if (!id || !id[0] || !ssid || !ssid[0]) {
    message = "id+ssid required";
    return false;
  }
  if (!SD_MMC.exists("/homes/index.json")) {
    // Create index entry
    appendIndex(id, id, ssid);
  } else {
    File idx = SD_MMC.open("/homes/index.json", "r");
    JsonDocument idoc;
    if (deserializeJson(idoc, idx)) {
      idx.close();
      message = "bad index";
      return false;
    }
    idx.close();
    bool found = false;
    for (JsonObject g : idoc["grids"].as<JsonArray>()) {
      if (!strcmp(g["id"] | "", id)) {
        g["ssid"] = ssid;
        found = true;
        break;
      }
    }
    if (!found) {
      JsonArray grids = idoc["grids"].as<JsonArray>();
      JsonObject g = grids.add<JsonObject>();
      g["id"] = id;
      g["name"] = id;
      g["ssid"] = ssid;
    }
    File out = SD_MMC.open("/homes/index.json", "w");
    if (!out) {
      message = "write fail";
      return false;
    }
    serializeJsonPretty(idoc, out);
    out.close();
  }

  String path = String("/homes/") + id + "/home.json";
  if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path, "r");
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
      f.close();
      doc["ssid"] = ssid;
      f = SD_MMC.open(path, "w");
      if (f) {
        serializeJsonPretty(doc, f);
        f.close();
      }
    } else {
      f.close();
    }
  }
  message = "bound";
  return true;
}

bool gridStoreActivate(const char *id, String &message) {
  if (!id || !id[0]) {
    message = "id required";
    return false;
  }
  String ssid;
  bool found = false;
  if (SD_MMC.exists("/homes/index.json")) {
    File idx = SD_MMC.open("/homes/index.json", "r");
    JsonDocument idoc;
    if (!deserializeJson(idoc, idx)) {
      for (JsonObject g : idoc["grids"].as<JsonArray>()) {
        if (!strcmp(g["id"] | "", id)) {
          found = true;
          ssid = g["ssid"] | "";
          break;
        }
      }
    }
    idx.close();
  }
  String path = String("/homes/") + id;
  if (!found && !SD_MMC.exists(path)) {
    message = "unknown grid";
    return false;
  }
  strlcpy(activeId, id, sizeof(activeId));
  gridPrefs.putString("grid", activeId);
  if (ssid.length()) wifiPreferSsid(ssid);
  message = String("active ") + activeId;
  return true;
}

bool gridStoreOnWifiJoin(const char *ssid, const char *optionalName, String &message) {
  if (!ssid || !ssid[0]) {
    message = "ssid required";
    return false;
  }
  // Prefer existing grid bound to this SSID
  if (SD_MMC.exists("/homes/index.json")) {
    File idx = SD_MMC.open("/homes/index.json", "r");
    JsonDocument idoc;
    if (!deserializeJson(idoc, idx)) {
      for (JsonObject g : idoc["grids"].as<JsonArray>()) {
        const char *gssid = g["ssid"] | "";
        if (gssid[0] && !strcmp(gssid, ssid)) {
          idx.close();
          return gridStoreActivate(g["id"] | "home", message);
        }
      }
    }
    idx.close();
  }
  // Create blank grid named after SSID (or optional name)
  char nameBuf[40];
  if (optionalName && optionalName[0])
    strlcpy(nameBuf, optionalName, sizeof(nameBuf));
  else
    snprintf(nameBuf, sizeof(nameBuf), "%s", ssid);
  return gridStoreCreateBlank(nameBuf, ssid, message);
}

bool gridStoreArrivalPending() {
  String path = gridStorePath("home.json");
  if (!SD_MMC.exists(path)) return false;
  File f = SD_MMC.open(path, "r");
  if (!f) return false;
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return false;
  }
  f.close();
  // Missing flag = already-established home (seeded sprawl); only blank grids force wizard
  if (doc["arrivalDone"].isNull()) return false;
  return !(doc["arrivalDone"] | true);
}

bool gridStoreSetArrivalDone(bool done, String &message) {
  String path = gridStorePath("home.json");
  if (!SD_MMC.exists(path)) {
    message = "no home.json";
    return false;
  }
  File f = SD_MMC.open(path, "r");
  if (!f) {
    message = "read fail";
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    message = "parse fail";
    return false;
  }
  f.close();
  doc["arrivalDone"] = done;
  File out = SD_MMC.open(path, "w");
  if (!out) {
    message = "write fail";
    return false;
  }
  serializeJsonPretty(doc, out);
  out.close();
  message = done ? "arrival done" : "arrival reset";
  return true;
}

bool gridStoreUpsertZone(const char *id, const char *name, String &message) {
  if (!id || !id[0]) {
    message = "id required";
    return false;
  }
  String path = gridStorePath("rooms.json");
  JsonDocument doc;
  if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path, "r");
    deserializeJson(doc, f);
    f.close();
  } else {
    doc["zones"].to<JsonArray>();
  }
  JsonArray zones = doc["zones"].as<JsonArray>();
  if (zones.isNull()) zones = doc["zones"].to<JsonArray>();
  JsonObject found;
  for (JsonObject z : zones) {
    if (!strcmp(z["id"] | "", id)) {
      found = z;
      break;
    }
  }
  if (found.isNull()) {
    found = zones.add<JsonObject>();
    found["id"] = id;
    found["sort"] = (int)zones.size() - 1;
  }
  found["name"] = (name && name[0]) ? name : id;
  File out = SD_MMC.open(path, "w");
  if (!out) {
    message = "write fail";
    return false;
  }
  serializeJsonPretty(doc, out);
  out.close();
  message = String("zone ") + id;
  return true;
}
