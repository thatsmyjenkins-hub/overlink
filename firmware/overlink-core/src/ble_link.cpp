#include "ble_link.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "grid_store.h"
#include "wifi_manager.h"

// Nordic UART — nRF Connect / Serial BLE / Web Bluetooth
static const char *kNusService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *kNusRx = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // write
static const char *kNusTx = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // notify

static BLEServer *server = nullptr;
static BLECharacteristic *rxChar = nullptr;
static BLECharacteristic *txChar = nullptr;
static BLEAdvertising *adv = nullptr;
static bool ready = false;
static bool pending = false;
static bool rebootSoon = false;
static bool paused = false;
static uint32_t rebootAt = 0;
static String rxBuf;
static String pendingCmd;
static String advName = "Overlink";

static void txChunk(const String &s) {
  if (!txChar) return;
  const int chunk = 160;
  for (int i = 0; i < (int)s.length(); i += chunk) {
    String part = s.substring(i, min((int)s.length(), i + chunk));
    txChar->setValue((uint8_t *)part.c_str(), part.length());
    txChar->notify();
    delay(15);
  }
  uint8_t nl = '\n';
  txChar->setValue(&nl, 1);
  txChar->notify();
}

static void queueCmd(const String &raw) {
  String c = raw;
  c.trim();
  if (!c.length()) return;
  pendingCmd = c;
  pending = true;
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (!v.length()) return;
    rxBuf += v;
    int nl;
    while ((nl = rxBuf.indexOf('\n')) >= 0 || (nl = rxBuf.indexOf('\r')) >= 0) {
      String line = rxBuf.substring(0, nl);
      rxBuf = rxBuf.substring(nl + 1);
      while (rxBuf.startsWith("\n") || rxBuf.startsWith("\r")) rxBuf.remove(0, 1);
      queueCmd(line);
    }
    if (rxBuf.length() > 8 && rxBuf.startsWith("{") && rxBuf.endsWith("}")) {
      queueCmd(rxBuf);
      rxBuf = "";
    } else if (rxBuf.length() > 0 && rxBuf.length() < 200 &&
               rxBuf.indexOf('{') < 0 && !rxBuf.startsWith("JOIN")) {
      // single-word commands without newline (STATUS / SCAN / HELP)
      String u = rxBuf;
      u.trim();
      u.toUpperCase();
      if (u == "STATUS" || u == "SCAN" || u == "HELP") {
        queueCmd(rxBuf);
        rxBuf = "";
      }
    }
    if (rxBuf.length() > 400) rxBuf = "";
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onDisconnect(BLEServer *s) override {
    delay(80);
    if (adv && !paused) adv->start();
  }
};

static void handleHelp() {
  txChunk(
      "Overlink BLE\n"
      "STATUS\n"
      "SCAN\n"
      "JOIN ssid\n"
      "JOIN ssid|pass\n"
      "JSON {\"cmd\":\"status\"|\"scan\"|\"join\",\"ssid\":\"\",\"pass\":\"\"}\n"
      "Wi-Fi: Overlink-Setup  http://192.168.44.1");
}

static void handleStatus() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["product"] = "Overlink Core";
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifiFillStatus(wifi);
  doc["hint"] = "Wi-Fi Overlink-Setup · http://192.168.44.1";
  String out;
  serializeJson(doc, out);
  txChunk(out);
}

static void handleScan() {
  JsonDocument doc;
  JsonArray nets = doc["networks"].to<JsonArray>();
  wifiScanTo(nets);
  doc["ok"] = true;
  doc["count"] = nets.size();
  String out;
  serializeJson(doc, out);
  txChunk(out);
}

static bool handleJoin(const String &ssid, const String &pass) {
  if (!ssid.length()) {
    txChunk("{\"ok\":false,\"error\":\"ssid required\"}");
    return false;
  }
  if (!wifiSaveNetwork(ssid, pass)) {
    txChunk("{\"ok\":false,\"error\":\"save failed\"}");
    return false;
  }
  String msg;
  gridStoreOnWifiJoin(ssid.c_str(), nullptr, msg);
  txChunk("{\"ok\":true,\"reboot\":true,\"message\":\"rebooting into STA\"}");
  rebootSoon = true;
  rebootAt = millis() + 600;
  return true;
}

static void handleCmd(String cmd) {
  String trim = cmd;
  trim.trim();
  if (!trim.length()) return;

  if (trim.startsWith("{")) {
    JsonDocument doc;
    if (deserializeJson(doc, trim)) {
      txChunk("{\"ok\":false,\"error\":\"bad json\"}");
      return;
    }
    const char *c = doc["cmd"] | "";
    String cs = c;
    cs.toLowerCase();
    if (cs == "status") handleStatus();
    else if (cs == "scan") handleScan();
    else if (cs == "help") handleHelp();
    else if (cs == "join")
      handleJoin(doc["ssid"] | "", doc["pass"] | "");
    else
      txChunk("{\"ok\":false,\"error\":\"unknown cmd\"}");
    return;
  }

  String upper = trim;
  upper.toUpperCase();
  if (upper == "HELP" || upper == "?" ) {
    handleHelp();
    return;
  }
  if (upper == "STATUS") {
    handleStatus();
    return;
  }
  if (upper == "SCAN") {
    handleScan();
    return;
  }
  if (upper.startsWith("JOIN ") || upper == "JOIN") {
    String rest = trim.substring(trim.indexOf(' ') >= 0 ? trim.indexOf(' ') + 1 : 4);
    rest.trim();
    String ssid = rest;
    String pass = "";
    int bar = rest.indexOf('|');
    if (bar < 0) bar = rest.indexOf('\t');
    if (bar >= 0) {
      ssid = rest.substring(0, bar);
      pass = rest.substring(bar + 1);
      ssid.trim();
      pass.trim();
    }
    handleJoin(ssid, pass);
    return;
  }
  txChunk("ERR unknown — send HELP");
}

void bleLinkPause() {
  paused = true;
  if (adv) adv->stop();
}

void bleLinkResume() {
  paused = false;
  bleLinkAdvertise(advName.length() ? advName.c_str() : "Overlink");
}

void bleLinkAdvertise(const char *name) {
  bleLinkEnsure();
  if (!adv) return;
  String n = name && name[0] ? name : "Overlink";
  if (n.length() > 24) n = n.substring(0, 24);
  advName = n;

  BLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setName(n.c_str());
  BLEAdvertisementData scanData;
  scanData.setCompleteServices(BLEUUID(kNusService));
  adv->stop();
  delay(20);
  adv->setAdvertisementType(ADV_TYPE_IND);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->start();
  Serial.printf("[BLE] advertise '%s'\n", n.c_str());
}

void bleLinkEnsure() {
  if (ready) return;
  BLEDevice::init("Overlink");
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *svc = server->createService(kNusService);
  txChar = svc->createCharacteristic(
      kNusTx, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  txChar->addDescriptor(new BLE2902());
  rxChar = svc->createCharacteristic(
      kNusRx, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());
  svc->start();
  adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(kNusService);
  ready = true;
  Serial.println("[BLE] NUS fallback ready");
}

void bleLinkBegin() {
  bleLinkEnsure();
  bleLinkAdvertise("Overlink");
}

void bleLinkLoop() {
  if (pending) {
    pending = false;
    String cmd = pendingCmd;
    pendingCmd = "";
    handleCmd(cmd);
  }
  if (rebootSoon && millis() > rebootAt) {
    rebootSoon = false;
    ESP.restart();
  }
}
