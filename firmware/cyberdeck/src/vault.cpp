#include "vault.h"
#include "ir_ctrl.h"
#include "rf_ctrl.h"
#include <Preferences.h>

static Preferences prefs;
static const int kMax = 16;

struct Entry {
  int id;
  String name;
  String kind;
  String payload;
};

static Entry entries[kMax];
static int count = 0;
static int nextId = 1;

static void persist() {
  JsonDocument doc;
  JsonArray arr = doc["items"].to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = entries[i].id;
    o["name"] = entries[i].name;
    o["kind"] = entries[i].kind;
    o["payload"] = entries[i].payload;
  }
  doc["nextId"] = nextId;
  String out;
  serializeJson(doc, out);
  prefs.putString("vault", out);
}

static void load() {
  String raw = prefs.getString("vault", "");
  count = 0;
  nextId = 1;
  if (!raw.length()) return;
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return;
  nextId = doc["nextId"] | 1;
  JsonArray arr = doc["items"].as<JsonArray>();
  for (JsonObject o : arr) {
    if (count >= kMax) break;
    entries[count].id = o["id"] | nextId++;
    entries[count].name = o["name"] | "signal";
    entries[count].kind = o["kind"] | "ir";
    entries[count].payload = o["payload"] | "";
    count++;
  }
}

void vaultBegin() {
  prefs.begin("cyberdeck", false);
  load();
}

void vaultList(JsonArray arr) {
  for (int i = 0; i < count; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = entries[i].id;
    o["name"] = entries[i].name;
    o["kind"] = entries[i].kind;
    o["payload"] = entries[i].payload;
  }
}

bool vaultAdd(const String &name, const String &kind, const String &payload,
              String &detail) {
  if (count >= kMax) {
    detail = "Vault full (max 16). Delete one first.";
    return false;
  }
  if (!name.length() || !payload.length()) {
    detail = "Name and payload required";
    return false;
  }
  entries[count].id = nextId++;
  entries[count].name = name;
  entries[count].kind = kind;
  entries[count].payload = payload;
  count++;
  persist();
  detail = "Saved #" + String(entries[count - 1].id);
  return true;
}

bool vaultDelete(int id, String &detail) {
  for (int i = 0; i < count; i++) {
    if (entries[i].id == id) {
      for (int j = i; j < count - 1; j++) entries[j] = entries[j + 1];
      count--;
      persist();
      detail = "Deleted #" + String(id);
      return true;
    }
  }
  detail = "Signal not found";
  return false;
}

bool vaultReplay(int id, String &detail) {
  for (int i = 0; i < count; i++) {
    if (entries[i].id != id) continue;
    if (entries[i].kind == "ir") {
      // payload: NEC:0x20DF906F  or  SONY:0x640C:15
      if (entries[i].payload.startsWith("NEC:")) {
        String hex = entries[i].payload.substring(4);
        uint32_t code = strtoul(hex.c_str(), nullptr, 0);
        irSendNec(code, 1);
        detail = "IR replay " + entries[i].name;
        return true;
      }
      if (entries[i].payload.startsWith("SONY:")) {
        String rest = entries[i].payload.substring(5);
        int colon = rest.indexOf(':');
        String hex = colon >= 0 ? rest.substring(0, colon) : rest;
        uint16_t bits = 15;
        if (colon >= 0) bits = (uint16_t)rest.substring(colon + 1).toInt();
        uint64_t code = strtoull(hex.c_str(), nullptr, 0);
        irSendSony(code, bits, 1);
        detail = "IR replay " + entries[i].name;
        return true;
      }
      detail = "Unsupported IR payload format";
      return false;
    }
    if (entries[i].kind == "rf") {
      return rfTransmitVaultPayload(entries[i].payload, detail);
    }
    detail = "Unknown kind";
    return false;
  }
  detail = "Signal not found";
  return false;
}

int vaultCount() { return count; }

void vaultExport(JsonDocument &doc) {
  doc["ok"] = true;
  doc["source"] = "cyberdeck";
  doc["nextId"] = nextId;
  JsonArray arr = doc["items"].to<JsonArray>();
  vaultList(arr);
}
