#include "wifi_manager.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

static Preferences prefs;
static DNSServer dns;
static const char *kApSsid = "Overlink-Setup";
static const char *kHostname = "overlink";
static const uint32_t kStaTimeoutMs = 18000;
static const int kMaxNets = 8;

static bool apUp = false;
static bool mdnsUp = false;
static String lastSsid;

struct Cred {
  String ssid;
  String pass;
};

static Cred nets[kMaxNets];
static int netCount = 0;

static void loadNets() {
  netCount = prefs.getUChar("n", 0);
  if (netCount > kMaxNets) netCount = kMaxNets;
  for (int i = 0; i < netCount; i++) {
    char sk[8], pk[8];
    snprintf(sk, sizeof(sk), "s%d", i);
    snprintf(pk, sizeof(pk), "p%d", i);
    nets[i].ssid = prefs.getString(sk, "");
    nets[i].pass = prefs.getString(pk, "");
  }
  if (prefs.isKey("last")) lastSsid = prefs.getString("last", "");
  else lastSsid = "";
}

static void persistNets() {
  prefs.putUChar("n", (uint8_t)netCount);
  for (int i = 0; i < netCount; i++) {
    char sk[8], pk[8];
    snprintf(sk, sizeof(sk), "s%d", i);
    snprintf(pk, sizeof(pk), "p%d", i);
    prefs.putString(sk, nets[i].ssid);
    prefs.putString(pk, nets[i].pass);
  }
  prefs.putString("last", lastSsid);
}

static int findNet(const String &ssid) {
  for (int i = 0; i < netCount; i++) {
    if (nets[i].ssid == ssid) return i;
  }
  return -1;
}

static void startAp() {
  WiFi.softAPConfig(IPAddress(192, 168, 44, 1), IPAddress(192, 168, 44, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP(kApSsid);  // open
  delay(120);
  apUp = true;
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("[WIFI] SoftAP %s → http://%s\n", kApSsid,
                WiFi.softAPIP().toString().c_str());
}

static void startMdns() {
  if (mdnsUp) return;
  if (MDNS.begin(kHostname)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
    Serial.println("[WIFI] mDNS http://overlink.local");
  }
}

static bool tryConnect(const String &ssid, const String &pass, uint32_t timeoutMs) {
  Serial.printf("[WIFI] STA → %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    lastSsid = ssid;
    prefs.putString("last", lastSsid);
    Serial.printf("[WIFI] STA IP %s\n", WiFi.localIP().toString().c_str());
    startMdns();
    return true;
  }
  return false;
}

// Sticky: last SSID first; else strongest saved in scan
static bool connectSaved() {
  if (netCount == 0) return false;

  if (lastSsid.length()) {
    int i = findNet(lastSsid);
    if (i >= 0 && tryConnect(nets[i].ssid, nets[i].pass, kStaTimeoutMs)) {
      return true;
    }
  }

  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  int bestIdx = -1;
  int bestRssi = -999;
  for (int s = 0; s < n; s++) {
    String ssid = WiFi.SSID(s);
    int ni = findNet(ssid);
    if (ni < 0) continue;
    int rssi = WiFi.RSSI(s);
    if (rssi > bestRssi) {
      bestRssi = rssi;
      bestIdx = ni;
    }
  }
  WiFi.scanDelete();

  if (bestIdx >= 0) {
    return tryConnect(nets[bestIdx].ssid, nets[bestIdx].pass, kStaTimeoutMs);
  }

  // Fall back: try each saved in order
  for (int i = 0; i < netCount; i++) {
    if (nets[i].ssid == lastSsid) continue;
    if (tryConnect(nets[i].ssid, nets[i].pass, 10000)) return true;
  }
  return false;
}

void wifiManagerBegin() {
  prefs.begin("overlink", false);
  loadNets();

  WiFi.persistent(false);
  WiFi.setHostname(kHostname);
  WiFi.mode(WIFI_AP_STA);
  startAp();

  if (netCount > 0) {
    if (!connectSaved()) {
      Serial.println("[WIFI] STA failed — SoftAP stays up for reconfigure");
    }
  } else {
    Serial.println("[WIFI] No saved nets — SoftAP setup mode");
  }
}

void wifiManagerLoop() {
  if (apUp) dns.processNextRequest();

  static uint32_t last = 0;
  if (millis() - last > 4000) {
    last = millis();
    if (wifiStaUp() && !mdnsUp) startMdns();
  }
}

bool wifiStaUp() { return WiFi.status() == WL_CONNECTED; }
bool wifiApUp() { return apUp; }
String wifiStaIp() { return wifiStaUp() ? WiFi.localIP().toString() : ""; }
String wifiStaSsid() { return wifiStaUp() ? WiFi.SSID() : lastSsid; }
String wifiApSsid() { return String(kApSsid); }

String wifiModeLabel() {
  if (wifiStaUp() && apUp) return "AP+STA";
  if (wifiStaUp()) return "STA";
  if (apUp) return "AP";
  return "DOWN";
}

bool wifiSaveNetwork(const String &ssid, const String &pass) {
  if (!ssid.length()) return false;
  int i = findNet(ssid);
  if (i < 0) {
    if (netCount >= kMaxNets) {
      // drop oldest
      for (int j = 1; j < netCount; j++) nets[j - 1] = nets[j];
      netCount--;
    }
    i = netCount++;
  }
  nets[i].ssid = ssid;
  nets[i].pass = pass;
  lastSsid = ssid;
  persistNets();
  Serial.printf("[WIFI] saved net '%s' (count=%d)\n", ssid.c_str(), netCount);
  return true;
}

void wifiPreferSsid(const String &ssid) {
  if (!ssid.length()) return;
  lastSsid = ssid;
  prefs.putString("last", lastSsid);
  Serial.printf("[WIFI] prefer SSID '%s'\n", ssid.c_str());
}

void wifiClearNetworks() {
  netCount = 0;
  lastSsid = "";
  persistNets();
}

void wifiFillStatus(JsonObject obj) {
  obj["mode"] = wifiModeLabel();
  obj["ap"] = apUp;
  obj["apSsid"] = kApSsid;
  obj["apIp"] = apUp ? WiFi.softAPIP().toString() : "";
  obj["sta"] = wifiStaUp();
  obj["staSsid"] = wifiStaSsid();
  obj["staIp"] = wifiStaIp();
  obj["hostname"] = kHostname;
  obj["savedCount"] = netCount;
  JsonArray saved = obj["saved"].to<JsonArray>();
  for (int i = 0; i < netCount; i++) saved.add(nets[i].ssid);
}

void wifiScanTo(JsonArray arr) {
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
}
