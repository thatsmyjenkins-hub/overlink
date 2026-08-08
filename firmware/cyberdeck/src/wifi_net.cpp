#include "wifi_net.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

static Preferences wifiPrefs;
static DNSServer dnsServer;
static const char *kApSsid = "CyberDeck-IRRF";
static const char *kHostname = "cyberdeck";
static const uint32_t kStaTimeoutMs = 15000;

static String savedSsid;
static String savedPass;
static bool apUp = false;
static bool mdnsUp = false;

static void loadCreds() {
  savedSsid = wifiPrefs.getString("ssid", "");
  savedPass = wifiPrefs.getString("pass", "");
}

bool wifiHasCreds() { return savedSsid.length() > 0; }
bool wifiStaConnected() { return WiFi.status() == WL_CONNECTED; }
String wifiStaIp() { return wifiStaConnected() ? WiFi.localIP().toString() : ""; }
String wifiStaSsid() { return wifiStaConnected() ? WiFi.SSID() : savedSsid; }
String wifiApSsid() { return String(kApSsid); }
String wifiApIp() { return apUp ? WiFi.softAPIP().toString() : ""; }
String wifiHostname() { return String(kHostname); }

String wifiModeLabel() {
  if (wifiStaConnected() && apUp) return "AP+STA";
  if (wifiStaConnected()) return "STA";
  if (apUp) return "AP";
  return "DOWN";
}

static void startAp() {
  // Open SoftAP (no password). Use 192.168.44.x — NOT 192.168.4.x —
  // because many home routers (including this LAN) already use 192.168.4.1.
  WiFi.softAPConfig(IPAddress(192, 168, 44, 1), IPAddress(192, 168, 44, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP(kApSsid);
  delay(120);
  apUp = true;
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.print(F("SoftAP open: "));
  Serial.print(kApSsid);
  Serial.print(F(" → http://"));
  Serial.println(WiFi.softAPIP());
}

static void startMdns() {
  if (mdnsUp) return;
  if (MDNS.begin(kHostname)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("cyberdeck", "tcp", 80);
    mdnsUp = true;
    Serial.print(F("mDNS: http://"));
    Serial.print(kHostname);
    Serial.println(F(".local"));
  }
}

void wifiNetBegin() {
  wifiPrefs.begin("wifi", false);
  loadCreds();

  // Case-sensitive SSID fix (was saved as Marmadukebrew)
  if (savedSsid.equalsIgnoreCase("marmadukebrew") &&
      savedSsid != "marmadukebrew") {
    savedSsid = "marmadukebrew";
    wifiPrefs.putString("ssid", savedSsid);
    Serial.println(F("Corrected Wi‑Fi SSID casing → marmadukebrew"));
  }

  WiFi.persistent(false);
  WiFi.setHostname(kHostname);

  if (wifiHasCreds()) {
    WiFi.mode(WIFI_AP_STA);
    startAp();  // always keep recovery AP
    Serial.print(F("Connecting STA to "));
    Serial.println(savedSsid);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kStaTimeoutMs) {
      delay(200);
      Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("STA IP: "));
      Serial.println(WiFi.localIP());
      startMdns();
    } else {
      Serial.println(F("STA failed — SoftAP still available for reconfigure"));
    }
  } else {
    WiFi.mode(WIFI_AP);
    startAp();
    Serial.println(F("No saved Wi‑Fi — SoftAP setup mode"));
  }
}

void wifiNetLoop() {
  if (apUp) dnsServer.processNextRequest();

  // Late mDNS if STA comes up after boot
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck > 3000) {
    lastCheck = millis();
    if (wifiStaConnected() && !mdnsUp) startMdns();
  }
}

void wifiFillStatus(JsonObject obj) {
  obj["mode"] = wifiModeLabel();
  obj["hostname"] = kHostname;
  obj["mdns"] = String("http://") + kHostname + ".local";
  obj["hasCreds"] = wifiHasCreds();
  obj["staConnected"] = wifiStaConnected();
  obj["staSsid"] = wifiStaSsid();
  obj["staIp"] = wifiStaIp();
  obj["apSsid"] = kApSsid;
  obj["apIp"] = wifiApIp();
  obj["apOpen"] = true;
  obj["uiPassword"] = false;
  // Primary URL for the client
  if (wifiStaConnected()) {
    obj["primaryUrl"] = String("http://") + kHostname + ".local";
    obj["primaryIp"] = wifiStaIp();
  } else {
    obj["primaryUrl"] = String("http://") + wifiApIp();
    obj["primaryIp"] = wifiApIp();
  }
  obj["bleNote"] =
      "Full web UI over Bluetooth is not seamless (poor iOS support, needs "
      "HTTPS). SoftAP + home Wi‑Fi + mDNS is the recommended path. BLE "
      "provisioning can be added later if needed.";
}

void wifiScanTo(JsonArray arr) {
  // Scan can briefly disrupt AP clients; keep it short
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
}

bool wifiSaveAndReboot(const String &ssid, const String &pass, String &detail) {
  if (!ssid.length()) {
    detail = "SSID required";
    return false;
  }
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  savedSsid = ssid;
  savedPass = pass;
  detail = "Saved \"" + ssid + "\". Rebooting to join network…";
  return true;
}

bool wifiClearCreds(String &detail) {
  wifiPrefs.remove("ssid");
  wifiPrefs.remove("pass");
  savedSsid = "";
  savedPass = "";
  detail = "Wi‑Fi credentials cleared. Rebooting to SoftAP mode…";
  return true;
}

bool wifiIsCaptiveHost(const String &host) {
  String h = host;
  h.toLowerCase();
  if (!h.length()) return true;
  if (h.indexOf("192.168.44.1") >= 0) return false;
  if (h.indexOf("192.168.4.1") >= 0) return false;
  if (h.indexOf("cyberdeck.local") >= 0) return false;
  // Common OS captive probes → send them to our UI
  return h.indexOf("captive") >= 0 || h.indexOf("connectivitycheck") >= 0 ||
         h.indexOf("msftconnecttest") >= 0 || h.indexOf("msftncsi") >= 0 ||
         h.indexOf("gstatic") >= 0 || h.indexOf("apple.com") >= 0 ||
         h.indexOf("android") >= 0;
}
