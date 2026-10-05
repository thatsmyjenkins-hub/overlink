#include "wifi_manager.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "device_hub.h"
#include "grid_store.h"

static Preferences prefs;
static DNSServer dns;
static const char *kApSsid = "Overlink-Setup";
static const char *kHostname = "overlink";
static const int kMaxNets = 8;
static const int kApChannel = 6;
static const uint32_t kStaTryMs = 7000;
static const uint32_t kStaPauseMs = 8000;

static bool apUp = false;
static bool mdnsUp = false;
static bool scanBusy = false;
static String lastSsid;

static int staTryIdx = -1;
static int staRoundStart = 0;
static uint32_t staTryMs = 0;
static uint32_t staPauseUntil = 0;
static uint32_t bootMs = 0;
static bool staWasUp = false;

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

static int staChannelOrDefault() {
  if (WiFi.status() != WL_CONNECTED) return kApChannel;
  uint8_t primary = kApChannel;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary >= 1 && primary <= 13)
    return (int)primary;
  return kApChannel;
}

static void startAp() {
  wifi_country_t country = {};
  country.cc[0] = 'U';
  country.cc[1] = 'S';
  country.cc[2] = '\0';
  country.schan = 1;
  country.nchan = 11;
  country.max_tx_power = 84;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  esp_wifi_set_country(&country);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_ps(WIFI_PS_NONE);

  int ch = staChannelOrDefault();
  WiFi.softAPConfig(IPAddress(192, 168, 44, 1), IPAddress(192, 168, 44, 1),
                    IPAddress(255, 255, 255, 0));
  bool ok = WiFi.softAP(kApSsid, nullptr, ch, 0, 4);
  delay(80);
  apUp = ok || (WiFi.softAPIP()[0] != 0);
  dns.stop();
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("[WIFI] SoftAP %s ch%d → http://%s (%s)\n", kApSsid, ch,
                WiFi.softAPIP().toString().c_str(), ok ? "ok" : "retry");
}

static void stopApIfIdle() {
  if (!apUp) return;
  if (WiFi.softAPgetStationNum() > 0) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apUp = false;
  Serial.println("[WIFI] SoftAP down — STA only");
}

static void ensureAp() {
  if (WiFi.status() == WL_CONNECTED) return;
  wifi_mode_t mode = WiFi.getMode();
  IPAddress ip = WiFi.softAPIP();
  if (mode == WIFI_STA || ip[0] == 0 || !apUp) {
    WiFi.mode(WIFI_AP_STA);
    startAp();
  }
}

static void startMdns() {
  if (mdnsUp) return;
  if (MDNS.begin(kHostname)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
    Serial.println("[WIFI] mDNS http://overlink.local");
  }
}

static void beginStaTry(int idx) {
  if (idx < 0 || idx >= netCount) return;
  Serial.printf("[WIFI] STA try %s\n", nets[idx].ssid.c_str());
  WiFi.begin(nets[idx].ssid.c_str(), nets[idx].pass.c_str());
  staTryIdx = idx;
  staTryMs = millis();
}

static void onStaUp() {
  lastSsid = WiFi.SSID();
  prefs.putString("last", lastSsid);
  Serial.printf("[WIFI] STA IP %s ch%d\n", WiFi.localIP().toString().c_str(),
                staChannelOrDefault());
  startMdns();
  String msg;
  if (gridStoreOnWifiJoin(lastSsid.c_str(), nullptr, msg)) {
    Serial.printf("[GRID] %s\n", msg.c_str());
    deviceHubOnGridChanged();
  } else
    Serial.printf("[GRID] wifi join: %s\n", msg.c_str());
}

void wifiManagerBegin() {
  prefs.begin("overlink", false);
  loadNets();
  bootMs = millis();

  WiFi.persistent(false);
  WiFi.setHostname(kHostname);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  startAp();

  if (netCount > 0)
    Serial.println("[WIFI] SoftAP live — STA retries in background");
  else
    Serial.println("[WIFI] No saved nets — SoftAP setup mode");
}

void wifiManagerLoop() {
  if (apUp) dns.processNextRequest();

  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !staWasUp) {
    staWasUp = true;
    onStaUp();
  } else if (!up) {
    staWasUp = false;
  }

  static uint32_t last = 0;
  static uint32_t staUpSince = 0;
  if (millis() - last > 2000) {
    last = millis();
    if (up) {
      if (!staUpSince) staUpSince = millis();
      if (!mdnsUp) startMdns();
      if (apUp && millis() - staUpSince > 4000) stopApIfIdle();
    } else {
      staUpSince = 0;
      ensureAp();
    }
  }

  if (scanBusy || up || netCount == 0) return;
  if (millis() - bootMs < 1500) return;
  if (millis() < staPauseUntil) return;

  if (staTryIdx < 0) {
    int i = lastSsid.length() ? findNet(lastSsid) : 0;
    if (i < 0) i = 0;
    staRoundStart = i;
    beginStaTry(i);
    return;
  }
  if (staTryIdx >= netCount) {
    staTryIdx = -1;
    staPauseUntil = millis() + kStaPauseMs;
    return;
  }

  if (up) return;

  if (millis() - staTryMs > kStaTryMs) {
    Serial.printf("[WIFI] STA timeout %s\n", nets[staTryIdx].ssid.c_str());
    WiFi.disconnect(false, false);
    delay(40);
    ensureAp();
    int next = staTryIdx + 1;
    if (next >= netCount) next = 0;
    if (next == staRoundStart) {
      staTryIdx = -1;
      staPauseUntil = millis() + kStaPauseMs;
      Serial.println("[WIFI] STA paused — SoftAP stays up");
    } else {
      beginStaTry(next);
    }
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
  obj["apClients"] = apUp ? (int)WiFi.softAPgetStationNum() : 0;
  obj["sta"] = wifiStaUp();
  obj["staSsid"] = wifiStaSsid();
  obj["staIp"] = wifiStaIp();
  obj["hostname"] = kHostname;
  obj["savedCount"] = netCount;
  JsonArray saved = obj["saved"].to<JsonArray>();
  for (int i = 0; i < netCount; i++) saved.add(nets[i].ssid);
}

void wifiScanTo(JsonArray arr) {
  scanBusy = true;
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  scanBusy = false;
}
