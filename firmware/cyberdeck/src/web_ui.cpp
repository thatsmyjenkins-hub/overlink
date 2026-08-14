#include "web_ui.h"
#include "ir_ctrl.h"
#include "rf_ctrl.h"
#include "vault.h"
#include "wifi_net.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

static WebServer server(80);
static Preferences setupPrefs;
static bool setupDone() { return setupPrefs.getBool("done", false); }
static void setSetupDone(bool v) { setupPrefs.putBool("done", v); }

static void fillHardware(JsonObject hw) {
  // Defaults match the user's verified boost circuit
  hw["ledOhm"] = setupPrefs.getUShort("ledOhm", 47);
  hw["baseOhm"] = setupPrefs.getUShort("baseOhm", 1000);
  hw["notes"] = setupPrefs.getString("hwNotes", "2N2222 + Gikfun LED on dedicated 5V");
}

static void addCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-CyberDeck-Token");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
}

static void sendJson(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  addCors();
  server.send(code, "application/json", out);
}

static void sendMsg(bool ok, const String &msg) {
  JsonDocument doc;
  doc["ok"] = ok;
  doc["message"] = msg;
  sendJson(ok ? 200 : 400, doc);
}

static bool handleFile(const String &path) {
  String p = path;
  if (p.endsWith("/")) p += "index.html";
  if (!LittleFS.exists(p)) return false;
  String ctype = "text/plain";
  if (p.endsWith(".html")) ctype = "text/html";
  else if (p.endsWith(".css")) ctype = "text/css";
  else if (p.endsWith(".js")) ctype = "application/javascript";
  else if (p.endsWith(".svg")) ctype = "image/svg+xml";
  else if (p.endsWith(".json")) ctype = "application/json";
  File f = LittleFS.open(p, "r");
  server.streamFile(f, ctype);
  f.close();
  return true;
}

static void apiStatus() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["device"] = "CyberDeck IR/RF";
  doc["peer"] = true;
  doc["mac"] = WiFi.macAddress();
  doc["setupDone"] = setupDone();
  doc["uptimeMs"] = millis();
  doc["freeHeap"] = ESP.getFreeHeap();
  JsonObject net = doc["wifi"].to<JsonObject>();
  wifiFillStatus(net);
  // Compat fields for older UI bits
  doc["ip"] = net["primaryIp"];
  doc["ssid"] = wifiStaConnected() ? wifiStaSsid() : wifiApSsid();
  JsonObject ir = doc["ir"].to<JsonObject>();
  irGetStatus(ir);
  JsonObject rf = doc["rf"].to<JsonObject>();
  rfGetStatus(rf);
  doc["vaultCount"] = vaultCount();
  JsonObject hw = doc["hardware"].to<JsonObject>();
  fillHardware(hw);
  // next-step guidance (no dead ends)
  if (!wifiHasCreds()) {
    doc["next"] = "Join a home Wi‑Fi so you can leave SoftAP mode";
    doc["nextRoute"] = "/#/network";
  } else if (!setupDone()) {
    doc["next"] = "Open Setup wizard and complete IR + RF checks";
    doc["nextRoute"] = "/#/setup";
  } else if (!rfOk()) {
    doc["next"] = "RF offline — run Diagnostics CC1101 test";
    doc["nextRoute"] = "/#/diag";
  } else {
    doc["next"] = "Ready. Capture IR or sniff RF.";
    doc["nextRoute"] = "/#/ir";
  }
  sendJson(200, doc);
}

static void apiIrLive() {
  JsonDocument doc;
  JsonArray arr = doc["lines"].to<JsonArray>();
  while (irLiveCount()) {
    arr.add(irPopLiveLine());
  }
  JsonObject last = doc["last"].to<JsonObject>();
  irGetLastCapture(last);
  sendJson(200, doc);
}

static void apiIrVizio() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")))
    return sendMsg(false, "Bad JSON");
  String action = body["action"] | "";
  String detail;
  bool ok = irSendVizio(action, detail);
  sendMsg(ok, detail);
}

static void apiIrReplay() {
  String detail;
  bool ok = irReplayLast(detail);
  sendMsg(ok, detail);
}

static void apiIrSave() {
  if (!irHasCapture()) return sendMsg(false, "No capture to save — learn first");
  JsonDocument cap;
  JsonObject o = cap.to<JsonObject>();
  irGetLastCapture(o);
  String name = "IR capture";
  if (server.hasArg("plain")) {
    JsonDocument body;
    deserializeJson(body, server.arg("plain"));
    name = body["name"] | name;
  }
  String button = o["button"] | "";
  if (button.length()) name = button;
  String value = o["value"] | "0x0";
  String proto = o["proto"] | "NEC";
  proto.toUpperCase();
  uint16_t bits = o["bits"] | 0;
  String payload = proto + ":" + value;
  if (proto == "SONY" && bits) payload += ":" + String(bits);
  String detail;
  bool ok = vaultAdd(name, "ir", payload, detail);
  sendMsg(ok, detail);
}

static void apiIrQa() {
  String detail;
  bool ok = irLoopbackQa(detail);
  JsonDocument doc;
  doc["ok"] = ok;
  doc["message"] = detail;
  sendJson(ok ? 200 : 422, doc);
}

static void apiRfFreq() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")))
    return sendMsg(false, "Bad JSON");
  float mhz = body["mhz"] | 433.92f;
  String detail;
  bool ok = rfSetFrequency(mhz, detail);
  sendMsg(ok, detail);
}

static void apiRfSniff() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  deserializeJson(body, server.arg("plain"));
  bool on = body["on"] | false;
  String detail;
  bool ok = on ? rfStartSniff(detail) : rfStopSniff(detail);
  sendMsg(ok, detail);
}

static void apiRfReplay() {
  String detail;
  bool ok = rfReplayLast(detail);
  sendMsg(ok, detail);
}

static void apiRfPacket() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  rfGetLastPacket(o);
  sendJson(200, doc);
}

static void apiRfTest() {
  String detail;
  bool ok = rfSelfTest(detail);
  JsonDocument doc;
  doc["ok"] = ok;
  doc["message"] = detail;
  sendJson(ok ? 200 : 422, doc);
}

static void apiRfWatch() {
  uint32_t ms = 4000;
  if (server.hasArg("plain")) {
    JsonDocument body;
    if (!deserializeJson(body, server.arg("plain"))) {
      ms = body["ms"] | 4000;
    }
  }
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  bool ok = rfWatchRssi(ms, o);
  sendJson(ok ? 200 : 422, doc);
}

static void apiIrSend() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")))
    return sendMsg(false, "Bad JSON");
  String detail;
  const char *action = body["action"] | "";
  if (action[0]) {
    return sendMsg(irSendVizio(String(action), detail), detail);
  }
  const char *sony = body["sony"] | "";
  const char *proto = body["proto"] | "";
  String protoU = String(proto);
  protoU.toUpperCase();
  if (sony[0] || protoU == "SONY") {
    const char *val = sony[0] ? sony : (body["value"] | "");
    if (!val[0]) return sendMsg(false, "Need sony/value (e.g. 0x640C)");
    uint64_t code = strtoull(val, nullptr, 0);
    uint16_t bits = body["bits"] | 15;
    uint8_t frames = body["frames"] | 1;
    irSendSony(code, bits, frames);
    detail = "Sent SONY 0x" + String((uint32_t)code, HEX) + " /" + String(bits) +
             " x" + String(frames);
    return sendMsg(true, detail);
  }
  const char *nec = body["nec"] | "";
  if (!nec[0]) return sendMsg(false, "Need action, nec, or sony (e.g. 0x640C)");
  uint32_t code = strtoul(nec, nullptr, 0);
  uint8_t frames = body["frames"] | 1;
  irSendNec(code, frames);
  detail = "Sent NEC 0x" + String(code, HEX) + " x" + String(frames);
  sendMsg(true, detail);
}

static void apiRfTx() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")))
    return sendMsg(false, "Bad JSON");
  const char *hex = body["hex"] | "";
  if (!hex[0]) return sendMsg(false, "hex required");
  float mhz = body["mhz"] | 0.0f;
  String detail;
  sendMsg(rfTransmitHex(String(hex), mhz, detail), detail);
}

static void apiRfSave() {
  String name;
  if (server.hasArg("plain")) {
    JsonDocument body;
    if (!deserializeJson(body, server.arg("plain"))) {
      name = body["name"] | "";
    }
  }
  String payload = rfVaultPayloadFromLast();
  if (!payload.length()) return sendMsg(false, "No RF packet captured yet");
  if (!name.length()) name = "RF " + String(rfFreqMhz(), 3) + " MHz";
  String detail;
  sendMsg(vaultAdd(name, "rf", payload, detail), detail);
}

static void apiVaultList() {
  JsonDocument doc;
  JsonArray arr = doc["items"].to<JsonArray>();
  vaultList(arr);
  sendJson(200, doc);
}

static void apiVaultExport() {
  JsonDocument doc;
  vaultExport(doc);
  sendJson(200, doc);
}

static void apiVaultPush() {
  String core = "http://overlink.local";
  if (server.hasArg("plain") && server.arg("plain").length()) {
    JsonDocument body;
    if (!deserializeJson(body, server.arg("plain")) && body["core"].is<const char *>()) {
      core = body["core"].as<const char *>();
    }
  }
  while (core.endsWith("/")) core.remove(core.length() - 1);
  JsonDocument doc;
  vaultExport(doc);
  String payload;
  serializeJson(doc, payload);
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(core + "/api/deck/vault")) {
    sendMsg(false, "cannot reach Core");
    return;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  String resp = http.getString();
  http.end();
  sendMsg(code >= 200 && code < 300, String("push ") + code + " " + resp);
}

static void apiVaultAdd() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")))
    return sendMsg(false, "Bad JSON");
  String detail;
  bool ok = vaultAdd(body["name"] | "", body["kind"] | "ir",
                     body["payload"] | "", detail);
  sendMsg(ok, detail);
}

static void apiVaultDel() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  deserializeJson(body, server.arg("plain"));
  String detail;
  bool ok = vaultDelete(body["id"] | -1, detail);
  sendMsg(ok, detail);
}

static void apiVaultReplay() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  deserializeJson(body, server.arg("plain"));
  String detail;
  bool ok = vaultReplay(body["id"] | -1, detail);
  sendMsg(ok, detail);
}

static void apiSetupGetFixed() {
  JsonDocument doc;
  doc["done"] = setupDone();
  JsonArray steps = doc["steps"].to<JsonArray>();
  steps.add("power");
  steps.add("ir_wiring");
  steps.add("ir_loopback");
  steps.add("rf_wiring");
  steps.add("rf_test");
  steps.add("finish");
  JsonObject check = doc["checklist"].to<JsonObject>();
  check["jump33"] =
      "JUMP must be 3.3V (CC1101). IR LED uses dedicated 5V + transistor.";
  check["ir"] = "RX D14; TX D4→1k→2N2222; 5V→47Ω→LED→collector";
  check["rf"] = "CC1101 VCC=3.3V only; SPI 18/19/23/5; GDO0=26";
  JsonObject hw = doc["hardware"].to<JsonObject>();
  fillHardware(hw);
  sendJson(200, doc);
}

static void apiSetupHardware() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")))
    return sendMsg(false, "Bad JSON");
  uint16_t ledOhm = body["ledOhm"] | 47;
  uint16_t baseOhm = body["baseOhm"] | 1000;
  if (ledOhm < 10 || ledOhm > 220) return sendMsg(false, "ledOhm must be 10–220");
  if (baseOhm < 220 || baseOhm > 4700)
    return sendMsg(false, "baseOhm must be 220–4700");
  setupPrefs.putUShort("ledOhm", ledOhm);
  setupPrefs.putUShort("baseOhm", baseOhm);
  if (body["notes"].is<const char *>()) {
    setupPrefs.putString("hwNotes", body["notes"].as<const char *>());
  }
  sendMsg(true, "Hardware config saved (LED " + String(ledOhm) + "Ω)");
}

static void apiSetupComplete() {
  setSetupDone(true);
  sendMsg(true, "Setup marked complete. You can re-run it anytime from Setup.");
}

static void apiSetupReset() {
  setSetupDone(false);
  sendMsg(true, "Setup reset — wizard unlocked");
}

static void apiUseCases() {
  JsonDocument doc;
  JsonArray arr = doc["cases"].to<JsonArray>();
  auto add = [&](const char *id, const char *title, const char *goal,
                 const char *route, const char *ifStuck) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = id;
    o["title"] = title;
    o["goal"] = goal;
    o["route"] = route;
    o["ifStuck"] = ifStuck;
  };
  add("uc1", "First boot setup", "Complete wizard so IR+RF are verified",
      "/#/setup", "Diagnostics → run IR loopback + CC1101 test");
  add("uc2", "Control Vizio TV", "Mute/Vol/Power from IR console", "/#/ir",
      "Learn from real remote, Save, Replay; check TX aim");
  add("uc3", "Learn unknown remote", "Capture button and save to vault", "/#/ir",
      "Point remote at RX dome; watch Live feed");
  add("uc4", "Sniff sub-GHz fob", "Tune band, sniff, replay last packet",
      "/#/rf", "Confirm CC1101 3.3V + antenna; try 315/433/868/915");
  add("uc5", "Replay saved signal", "Pick vault item and transmit", "/#/vault",
      "If empty, capture on IR/RF first — CTAs always offered");
  add("uc6", "Troubleshoot no TX", "Loopback QA + wiring map", "/#/diag",
      "Phone camera on IR LED during blast; swap E/C on 2N2222");
  add("uc7", "Troubleshoot RF dead", "SPI self-test + pin map", "/#/diag",
      "Never 5V on CC1101; re-seat SPI jumpers");
  add("uc8", "Reconfigure after move", "Reset setup and re-verify", "/#/setup",
      "Setup → Reset wizard");
  add("uc9", "Join home Wi‑Fi", "Save SSID/pass, reboot, use cyberdeck.local",
      "/#/network", "If STA fails, reconnect SoftAP CyberDeck-IRRF (open)");
  sendJson(200, doc);
}

static void apiWifiStatus() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  wifiFillStatus(o);
  sendJson(200, doc);
}

static void apiWifiScan() {
  JsonDocument doc;
  JsonArray arr = doc["networks"].to<JsonArray>();
  wifiScanTo(arr);
  sendJson(200, doc);
}

static void apiWifiSave() {
  if (!server.hasArg("plain")) return sendMsg(false, "JSON body required");
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")))
    return sendMsg(false, "Bad JSON");
  String ssid = body["ssid"] | "";
  String pass = body["pass"] | "";
  String detail;
  if (!wifiSaveAndReboot(ssid, pass, detail)) {
    sendMsg(false, detail);
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = detail;
  doc["rebooting"] = true;
  sendJson(200, doc);
  delay(900);
  ESP.restart();
}

static void apiWifiClear() {
  String detail;
  wifiClearCreds(detail);
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = detail;
  doc["rebooting"] = true;
  sendJson(200, doc);
  delay(900);
  ESP.restart();
}

void webBegin() {
  setupPrefs.begin("setup", false);
  LittleFS.begin(true);
  wifiNetBegin();

  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/ir/live", HTTP_GET, apiIrLive);
  server.on("/api/ir/vizio", HTTP_POST, apiIrVizio);
  server.on("/api/ir/send", HTTP_POST, apiIrSend);
  server.on("/api/ir/replay", HTTP_POST, apiIrReplay);
  server.on("/api/ir/save", HTTP_POST, apiIrSave);
  server.on("/api/ir/qa", HTTP_POST, apiIrQa);
  server.on("/api/rf/freq", HTTP_POST, apiRfFreq);
  server.on("/api/rf/sniff", HTTP_POST, apiRfSniff);
  server.on("/api/rf/replay", HTTP_POST, apiRfReplay);
  server.on("/api/rf/tx", HTTP_POST, apiRfTx);
  server.on("/api/rf/save", HTTP_POST, apiRfSave);
  server.on("/api/rf/packet", HTTP_GET, apiRfPacket);
  server.on("/api/rf/test", HTTP_POST, apiRfTest);
  server.on("/api/rf/watch", HTTP_POST, apiRfWatch);
  server.on("/api/vault", HTTP_GET, apiVaultList);
  server.on("/api/vault/export", HTTP_GET, apiVaultExport);
  server.on("/api/vault/push", HTTP_POST, apiVaultPush);
  server.on("/api/vault/add", HTTP_POST, apiVaultAdd);
  server.on("/api/vault/delete", HTTP_POST, apiVaultDel);
  server.on("/api/vault/replay", HTTP_POST, apiVaultReplay);
  server.on("/api/setup", HTTP_GET, apiSetupGetFixed);
  server.on("/api/setup/hardware", HTTP_POST, apiSetupHardware);
  server.on("/api/setup/complete", HTTP_POST, apiSetupComplete);
  server.on("/api/setup/reset", HTTP_POST, apiSetupReset);
  server.on("/api/usecases", HTTP_GET, apiUseCases);
  server.on("/api/wifi", HTTP_GET, apiWifiStatus);
  server.on("/api/wifi/scan", HTTP_GET, apiWifiScan);
  server.on("/api/wifi/save", HTTP_POST, apiWifiSave);
  server.on("/api/wifi/clear", HTTP_POST, apiWifiClear);

  // Captive portal probes (Android/iOS/Windows)
  server.on("/generate_204", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.44.1/#/network", true);
    server.send(302, "text/plain", "");
  });
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.44.1/#/network", true);
    server.send(302, "text/plain", "");
  });
  server.on("/ncsi.txt", HTTP_GET, []() {
    server.send(200, "text/plain", "Microsoft NCSI");
  });
  server.on("/connecttest.txt", HTTP_GET, []() {
    server.send(200, "text/plain", "Microsoft Connect Test");
  });

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      addCors();
      server.send(204);
      return;
    }
    String uri = server.uri();
    String host = server.hostHeader();
    if (!uri.startsWith("/api/") && wifiIsCaptiveHost(host) &&
        !wifiStaConnected()) {
      server.sendHeader("Location", "http://192.168.44.1/#/network", true);
      server.send(302, "text/plain", "");
      return;
    }
    if (uri.startsWith("/api/")) {
      sendMsg(false, "API route not found");
      return;
    }
    if (!handleFile(uri)) {
      if (!handleFile("/index.html")) {
        server.send(500, "text/plain",
                    "LittleFS missing UI. Run: pio run -t uploadfs");
      }
    }
  });

  server.begin();
}

void webLoop() {
  wifiNetLoop();
  server.handleClient();
}

String webApSsid() { return wifiApSsid(); }
String webApIp() { return wifiApIp(); }
String webPrimaryUrl() {
  return wifiStaConnected() ? (String("http://") + wifiHostname() + ".local")
                            : (String("http://") + wifiApIp());
}
