#include "party_tricks.h"

#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <vector>

#include "device_hub.h"

// ─── BLE billboard state ─────────────────────────────────────────────
static bool bleReady = false;
static bool bleAdvertising = false;
static bool bleCycle = false;
static String bleBaseMsg = "OVERLINK SAYS HI";
static String bleLines[8];
static int bleLineCount = 0;
static int bleLineIdx = 0;
static uint32_t bleNextCycleMs = 0;
static const uint32_t kBleCycleMs = 2500;
static BLEAdvertising *bleAdv = nullptr;

static String lastSweepAt = "";
static int lastBleSeen = 0;
static int lastMdnsSeen = 0;
static int lastPrinters = 0;

static const char *kDefaultFunny[] = {
    "OVERLINK IS IN THE HOUSE",
    "🍕 YOUR FRIDGE IS JUDGING YOU",
    "👀 DAD MODE ENABLED",
    "UNLOCK THE SNACKS",
    "WIFI HAS A NEW BOSS",
    "BEAM ME UP SCOTTY",
    "THIS IS FINE 🔥",
    "PLEASE UPDATE YOUR ROUTER",
};

// ─── helpers ─────────────────────────────────────────────────────────
static String urlEncode(const String &s) {
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((int)c) || c == '-' || c == '_' || c == '.' || c == '~')
      o += c;
    else if (c == ' ')
      o += '+';
    else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      o += buf;
    }
  }
  return o;
}

static void ensureBle() {
  if (bleReady) return;
  BLEDevice::init("Overlink");
  bleAdv = BLEDevice::getAdvertising();
  bleReady = true;
}

static void applyBleName(const String &name) {
  ensureBle();
  // BLE Complete Local Name is practically ~20–26 useful chars on many phones
  String n = name;
  if (n.length() > 26) n = n.substring(0, 26);
  if (bleAdv) {
    bleAdv->stop();
    bleAdv->setAdvertisementType(ADV_TYPE_IND);
    bleAdv->setScanResponse(true);
    bleAdv->setMinPreferred(0x06);
    bleAdv->setMinPreferred(0x12);
    BLEAdvertisementData advData;
    advData.setName(n.c_str());
    advData.setFlags(0x06);
    bleAdv->setAdvertisementData(advData);
    BLEAdvertisementData scanData;
    scanData.setName(n.c_str());
    bleAdv->setScanResponseData(scanData);
    bleAdv->start();
  }
  bleAdvertising = true;
}

static void addMdnsService(JsonArray arr, const char *service, const char *proto,
                           const char *kind) {
  int n = MDNS.queryService(service, proto);
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["kind"] = kind;
    o["service"] = String("_") + service + "._" + proto;
    o["name"] = MDNS.hostname(i);
    o["ip"] = MDNS.address(i).toString();
    o["port"] = MDNS.port(i);
  }
}

// ─── Cast protobuf (minimal CastMessage) ─────────────────────────────
static void pbWriteVarint(std::vector<uint8_t> &buf, uint32_t v) {
  while (v >= 0x80) {
    buf.push_back((uint8_t)((v & 0x7F) | 0x80));
    v >>= 7;
  }
  buf.push_back((uint8_t)v);
}

static void pbWriteString(std::vector<uint8_t> &buf, int field, const String &s) {
  pbWriteVarint(buf, (uint32_t)((field << 3) | 2));
  pbWriteVarint(buf, (uint32_t)s.length());
  for (size_t i = 0; i < s.length(); i++) buf.push_back((uint8_t)s[i]);
}

static void pbWriteVarintField(std::vector<uint8_t> &buf, int field, uint32_t v) {
  pbWriteVarint(buf, (uint32_t)((field << 3) | 0));
  pbWriteVarint(buf, v);
}

static std::vector<uint8_t> castMessage(const char *ns, const char *dest, const String &json) {
  std::vector<uint8_t> body;
  pbWriteVarintField(body, 1, 0);  // protocol_version CASTV2_1_0
  pbWriteString(body, 2, "sender-0");
  pbWriteString(body, 3, dest);
  pbWriteString(body, 4, ns);
  pbWriteVarintField(body, 5, 0);  // STRING payload
  pbWriteString(body, 6, json);

  std::vector<uint8_t> frame;
  uint32_t len = (uint32_t)body.size();
  frame.push_back((len >> 24) & 0xFF);
  frame.push_back((len >> 16) & 0xFF);
  frame.push_back((len >> 8) & 0xFF);
  frame.push_back(len & 0xFF);
  frame.insert(frame.end(), body.begin(), body.end());
  return frame;
}

static bool castWrite(WiFiClientSecure &cli, const std::vector<uint8_t> &frame) {
  return cli.write(frame.data(), frame.size()) == frame.size();
}

static String castReadPayload(WiFiClientSecure &cli, uint32_t timeoutMs = 2500) {
  uint32_t t0 = millis();
  while (cli.available() < 4 && millis() - t0 < timeoutMs) delay(10);
  if (cli.available() < 4) return "";
  uint8_t hdr[4];
  if (cli.readBytes(hdr, 4) != 4) return "";
  uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) |
                 hdr[3];
  if (len == 0 || len > 8192) return "";
  String raw;
  raw.reserve(len);
  uint32_t got = 0;
  t0 = millis();
  while (got < len && millis() - t0 < timeoutMs) {
    if (!cli.available()) {
      delay(5);
      continue;
    }
    char c = (char)cli.read();
    raw += c;
    got++;
  }
  // Extract payload_utf8 heuristically: look for {"type"
  int j = raw.indexOf("{\"type\"");
  if (j < 0) j = raw.indexOf("{\"requestId\"");
  if (j < 0) j = raw.indexOf('{');
  if (j < 0) return "";
  return raw.substring(j);
}

static String castExtractTransportId(const String &json) {
  // "transportId":"xx"
  int k = json.indexOf("\"transportId\"");
  if (k < 0) return "";
  int c = json.indexOf(':', k);
  int q1 = json.indexOf('"', c + 1);
  int q2 = json.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0) return "";
  return json.substring(q1 + 1, q2);
}

static bool castStingerTo(const IPAddress &ip, const String &mediaUrl, String &message) {
  WiFiClientSecure cli;
  cli.setInsecure();
  cli.setTimeout(4000);
  if (!cli.connect(ip, 8009, 4000)) {
    message = "cast tls connect fail";
    return false;
  }
  int req = 1;
  auto send = [&](const char *ns, const char *dest, const String &json) {
    return castWrite(cli, castMessage(ns, dest, json));
  };
  if (!send("urn:x-cast:com.google.cast.tp.connection", "receiver-0",
            "{\"type\":\"CONNECT\"}")) {
    message = "cast connect msg fail";
    return false;
  }
  delay(80);
  send("urn:x-cast:com.google.cast.tp.heartbeat", "receiver-0", "{\"type\":\"PING\"}");
  delay(40);
  String launch = String("{\"type\":\"LAUNCH\",\"appId\":\"CC1AD845\",\"requestId\":") +
                  String(req++) + "}";
  if (!send("urn:x-cast:com.google.cast.receiver", "receiver-0", launch)) {
    message = "cast launch fail";
    return false;
  }
  String transport;
  for (int i = 0; i < 12 && !transport.length(); i++) {
    String payload = castReadPayload(cli, 800);
    if (!payload.length()) {
      send("urn:x-cast:com.google.cast.tp.heartbeat", "receiver-0", "{\"type\":\"PING\"}");
      continue;
    }
    transport = castExtractTransportId(payload);
    if (!transport.length() && payload.indexOf("RECEIVER_STATUS") >= 0) {
      // ask again
      send("urn:x-cast:com.google.cast.receiver", "receiver-0",
           String("{\"type\":\"GET_STATUS\",\"requestId\":") + String(req++) + "}");
    }
  }
  if (!transport.length()) {
    message = "cast no transportId — is TV on?";
    cli.stop();
    return false;
  }
  send("urn:x-cast:com.google.cast.tp.connection", transport.c_str(), "{\"type\":\"CONNECT\"}");
  delay(100);
  String load = String(
                    "{\"type\":\"LOAD\",\"autoplay\":true,\"currentTime\":0,\"requestId\":") +
                String(req++) +
                ",\"media\":{\"contentId\":\"" + mediaUrl +
                "\",\"streamType\":\"BUFFERED\",\"contentType\":\"image/png\",\"metadata\":{"
                "\"metadataType\":0,\"title\":\"OVERLINK\"}}}";
  if (!send("urn:x-cast:com.google.cast.media", transport.c_str(), load)) {
    message = "cast load fail";
    cli.stop();
    return false;
  }
  // Drain a moment so LOAD is accepted
  castReadPayload(cli, 600);
  cli.stop();
  message = String("stinger → ") + ip.toString();
  return true;
}

// ─── IPP / JetDirect print ───────────────────────────────────────────
static bool printRaw9100(const IPAddress &ip, const String &text, String &message) {
  WiFiClient c;
  if (!c.connect(ip, 9100, 2500)) {
    message = "9100 closed";
    return false;
  }
  // Simple text + form feeds so it ejects on many printers
  c.print("\n");
  c.print("================================\n");
  c.print("        O V E R L I N K\n");
  c.print("     party trick / awareness\n");
  c.print("================================\n\n");
  c.print(text);
  c.print("\n\n");
  c.print("--------------------------------\n");
  c.print("If you can read this, an open\n");
  c.print("LAN printer accepted a job with\n");
  c.print("no login. Lock down guest print.\n");
  c.print("--------------------------------\n\n\n\n\n");
  delay(200);
  c.stop();
  message = String("printed via 9100 @ ") + ip.toString();
  return true;
}

static bool printIpp(const IPAddress &ip, uint16_t port, const String &text, String &message) {
  // Minimal IPP Print-Job with document-format text/plain
  // https://www.rfc-editor.org/rfc/rfc8010
  std::vector<uint8_t> body;
  auto put16 = [&](uint16_t v) {
    body.push_back((v >> 8) & 0xFF);
    body.push_back(v & 0xFF);
  };
  auto put32 = [&](uint32_t v) {
    body.push_back((v >> 24) & 0xFF);
    body.push_back((v >> 16) & 0xFF);
    body.push_back((v >> 8) & 0xFF);
    body.push_back(v & 0xFF);
  };
  auto putAttr = [&](uint8_t tag, const char *name, const char *val) {
    body.push_back(tag);
    put16((uint16_t)strlen(name));
    for (const char *p = name; *p; p++) body.push_back((uint8_t)*p);
    put16((uint16_t)strlen(val));
    for (const char *p = val; *p; p++) body.push_back((uint8_t)*p);
  };

  put16(0x0101);             // version 1.1
  put16(0x0002);             // Print-Job
  put32(1);                  // request-id
  body.push_back(0x01);      // operation-attributes-tag
  putAttr(0x47, "attributes-charset", "utf-8");
  putAttr(0x48, "attributes-natural-language", "en");
  String uri = String("ipp://") + ip.toString() + "/ipp/print";
  putAttr(0x45, "printer-uri", uri.c_str());
  putAttr(0x49, "requesting-user-name", "overlink");
  putAttr(0x49, "job-name", "overlink-party");
  putAttr(0x49, "document-format", "text/plain");
  body.push_back(0x03);  // end-of-attributes

  String doc = String("OVERLINK PARTY TRICK\n\n") + text +
               "\n\n(open printer accepted this job — awareness demo)\n";
  for (size_t i = 0; i < doc.length(); i++) body.push_back((uint8_t)doc[i]);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  String url = String("http://") + ip.toString() + ":" + String(port) + "/ipp/print";
  if (!http.begin(client, url)) {
    message = "ipp begin fail";
    return false;
  }
  http.addHeader("Content-Type", "application/ipp");
  http.addHeader("Content-Length", String((int)body.size()));
  int code = http.POST(body.data(), body.size());
  http.end();
  if (code > 0 && code < 500) {
    message = String("ipp http ") + code + " @ " + ip.toString();
    return code >= 200 && code < 300;
  }
  // try alternate path
  url = String("http://") + ip.toString() + ":" + String(port) + "/ipp/port1";
  if (!http.begin(client, url)) {
    message = "ipp fail";
    return false;
  }
  http.addHeader("Content-Type", "application/ipp");
  code = http.POST(body.data(), body.size());
  http.end();
  message = String("ipp alt http ") + code;
  return code >= 200 && code < 300;
}

// ─── public API ──────────────────────────────────────────────────────
void partyTricksBegin() {
  bleLineCount = 0;
  for (unsigned i = 0; i < sizeof(kDefaultFunny) / sizeof(kDefaultFunny[0]) && i < 8; i++) {
    bleLines[bleLineCount++] = kDefaultFunny[i];
  }
}

void partyTricksLoop() {
  if (!bleAdvertising || !bleCycle) return;
  if (millis() < bleNextCycleMs) return;
  bleNextCycleMs = millis() + kBleCycleMs;
  if (bleLineCount <= 0) return;
  bleLineIdx = (bleLineIdx + 1) % bleLineCount;
  applyBleName(bleLines[bleLineIdx]);
}

void partyTricksBleFillStatus(JsonObject out) {
  out["advertising"] = bleAdvertising;
  out["cycle"] = bleCycle;
  out["message"] = bleBaseMsg;
  out["current"] = bleAdvertising ? (bleCycle ? bleLines[bleLineIdx] : bleBaseMsg) : "";
  JsonArray lines = out["lines"].to<JsonArray>();
  for (int i = 0; i < bleLineCount; i++) lines.add(bleLines[i]);
}

void partyTricksFillStatus(JsonObject out) {
  out["ok"] = true;
  JsonObject ble = out["ble"].to<JsonObject>();
  partyTricksBleFillStatus(ble);
  out["lastBleSeen"] = lastBleSeen;
  out["lastMdnsSeen"] = lastMdnsSeen;
  out["lastPrinters"] = lastPrinters;
  out["note"] =
      "BLE billboard changes the name phones see while scanning Bluetooth. "
      "It does not force pairing popups (Flipper-style Continuity floods). "
      "Use on your own LAN / with household consent.";
}

bool partyTricksBleStart(JsonVariantConst cfg, String &message) {
  bleBaseMsg = cfg["message"] | "OVERLINK SAYS HI";
  bleCycle = cfg["cycle"] | false;
  bleLineCount = 0;
  if (cfg["lines"].is<JsonArrayConst>()) {
    for (JsonVariantConst v : cfg["lines"].as<JsonArrayConst>()) {
      if (bleLineCount >= 8) break;
      const char *s = v.as<const char *>();
      if (s && s[0]) bleLines[bleLineCount++] = s;
    }
  }
  if (bleLineCount == 0) {
    for (unsigned i = 0; i < sizeof(kDefaultFunny) / sizeof(kDefaultFunny[0]) && i < 8; i++)
      bleLines[bleLineCount++] = kDefaultFunny[i];
  }
  // Put custom message first
  if (bleBaseMsg.length()) {
    // shift if needed
    if (bleLineCount < 8) {
      for (int i = bleLineCount; i > 0; i--) bleLines[i] = bleLines[i - 1];
      bleLines[0] = bleBaseMsg;
      bleLineCount++;
    } else {
      bleLines[0] = bleBaseMsg;
    }
  }
  bleLineIdx = 0;
  applyBleName(bleCycle ? bleLines[0] : bleBaseMsg);
  bleNextCycleMs = millis() + kBleCycleMs;
  message = bleCycle ? "BLE spam cycling" : ("BLE billboard: " + bleBaseMsg);
  return true;
}

bool partyTricksBleStop(String &message) {
  if (bleAdv) bleAdv->stop();
  bleAdvertising = false;
  bleCycle = false;
  message = "BLE billboard stopped";
  return true;
}

bool partyTricksSweep(JsonObject out, String &message) {
  out["ok"] = true;
  JsonArray mdns = out["mdns"].to<JsonArray>();
  JsonArray ble = out["ble"].to<JsonArray>();

  // mDNS service browse (home awareness map)
  addMdnsService(mdns, "googlecast", "tcp", "chromecast");
  addMdnsService(mdns, "ipp", "tcp", "printer");
  addMdnsService(mdns, "printer", "tcp", "printer");
  addMdnsService(mdns, "pdl-datastream", "tcp", "printer");
  addMdnsService(mdns, "http", "tcp", "http");
  addMdnsService(mdns, "hap", "tcp", "homekit");
  addMdnsService(mdns, "homekit", "tcp", "homekit");
  addMdnsService(mdns, "spotify-connect", "tcp", "spotify");
  addMdnsService(mdns, "sonos", "tcp", "sonos");
  addMdnsService(mdns, "androidtvremote2", "tcp", "androidtv");
  addMdnsService(mdns, "nvstream", "tcp", "gamestream");

  lastMdnsSeen = (int)mdns.size();

  // BLE scan
  ensureBle();
  if (bleAdvertising && bleAdv) bleAdv->stop();
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  BLEScanResults *results = scan->start(4, false);
  int count = results ? results->getCount() : 0;
  lastBleSeen = count;
  for (int i = 0; i < count && i < 40; i++) {
    BLEAdvertisedDevice d = results->getDevice(i);
    JsonObject o = ble.add<JsonObject>();
    String name = d.haveName() ? String(d.getName().c_str()) : "(no name)";
    o["name"] = name;
    o["addr"] = d.getAddress().toString().c_str();
    o["rssi"] = d.getRSSI();
    if (d.haveManufacturerData()) o["mfg"] = true;
  }
  scan->clearResults();
  if (bleAdvertising) applyBleName(bleCycle ? bleLines[bleLineIdx] : bleBaseMsg);

  out["mdnsCount"] = lastMdnsSeen;
  out["bleCount"] = lastBleSeen;
  message = String("sweep mdns=") + lastMdnsSeen + " ble=" + lastBleSeen;
  return true;
}

bool partyTricksStampede(String &message) {
  JsonDocument wrap;
  JsonArray devices = wrap["devices"].to<JsonArray>();
  deviceHubFillDevices(devices);
  int n = 0;
  String fail;
  for (JsonObject d : devices) {
    const char *type = d["type"] | "";
    const char *id = d["id"] | "";
    if (!id[0]) continue;
    bool light = !strcmp(type, "wiz_bulb") || !strcmp(type, "wled") || !strcmp(type, "hue") ||
                 !strcmp(type, "hue_group");
    if (!light) continue;
    String msg;
    if (deviceHubIdentify(id, msg))
      n++;
    else
      fail = msg;
    delay(180);
  }
  // Also try WLED on/off pulse for strips that reject identify
  for (JsonObject d : devices) {
    if (strcmp(d["type"] | "", "wled")) continue;
    JsonDocument patch;
    patch["id"] = d["id"];
    patch["on"] = false;
    String msg;
    deviceHubWledSet(patch.as<JsonVariantConst>(), msg);
    delay(120);
    patch["on"] = true;
    patch["bri"] = 200;
    deviceHubWledSet(patch.as<JsonVariantConst>(), msg);
    delay(120);
  }
  message = String("stampede blinked ") + n + " lights";
  if (!n && fail.length()) message += String(" (") + fail + ")";
  return n > 0;
}

bool partyTricksCastStinger(JsonVariantConst cfg, String &message) {
  String msg = cfg["message"] | "OVERLINK ONLINE";
  String ipStr = cfg["ip"] | "";
  IPAddress ip;
  if (ipStr.length()) {
    if (!ip.fromString(ipStr)) {
      message = "bad cast ip";
      return false;
    }
  } else {
    // first online cast device
    JsonDocument wrap;
    JsonArray devices = wrap["devices"].to<JsonArray>();
    deviceHubFillDevices(devices);
    bool found = false;
    for (JsonObject d : devices) {
      if (strcmp(d["type"] | "", "cast")) continue;
      const char *dip = d["ip"] | "";
      if (dip[0] && ip.fromString(dip)) {
        found = true;
        break;
      }
    }
    if (!found) {
      // mDNS fallback
      int n = MDNS.queryService("googlecast", "tcp");
      if (n > 0) {
        ip = MDNS.address(0);
        found = true;
      }
    }
    if (!found) {
      message = "no Chromecast found — add cast device or run Sweep";
      return false;
    }
  }
  // Big on-TV PNG via placehold (Chromecast pulls from internet)
  String media = String("https://placehold.co/1920x1080/07100e/E8F5A0/png?text=") +
                 urlEncode(msg.substring(0, 48));
  // Also try a local HTML-less fallback image host if placehold blocked — still attempt cast
  return castStingerTo(ip, media, message);
}

bool partyTricksFindPrinters(JsonArray out, String &message) {
  int before = (int)out.size();
  auto addPrinter = [&](const char *name, const char *ip, uint16_t port, const char *via) {
    for (JsonObject o : out) {
      if (!strcmp(o["ip"] | "", ip) && (int)(o["port"] | 0) == port) return;
    }
    JsonObject o = out.add<JsonObject>();
    o["name"] = name;
    o["ip"] = ip;
    o["port"] = port;
    o["via"] = via;
    o["openGuess"] = (port == 9100 || port == 631);
  };

  int n = MDNS.queryService("ipp", "tcp");
  for (int i = 0; i < n; i++) {
    addPrinter(MDNS.hostname(i).c_str(), MDNS.address(i).toString().c_str(), MDNS.port(i),
               "mdns-ipp");
  }
  n = MDNS.queryService("printer", "tcp");
  for (int i = 0; i < n; i++) {
    addPrinter(MDNS.hostname(i).c_str(), MDNS.address(i).toString().c_str(), MDNS.port(i),
               "mdns-printer");
  }
  n = MDNS.queryService("pdl-datastream", "tcp");
  for (int i = 0; i < n; i++) {
    addPrinter(MDNS.hostname(i).c_str(), MDNS.address(i).toString().c_str(), MDNS.port(i),
               "mdns-pdl");
  }

  // Subnet probe common printer ports near gateway (bounded)
  IPAddress self = WiFi.localIP();
  int probed = 0;
  for (int d = 1; d < 40 && probed < 32; d++) {
    for (int sign = -1; sign <= 1; sign += 2) {
      int last = (int)self[3] + sign * d;
      if (last <= 0 || last >= 255) continue;
      IPAddress ip(self[0], self[1], self[2], (uint8_t)last);
      probed++;
      WiFiClient c;
      if (c.connect(ip, 9100, 40)) {
        c.stop();
        addPrinter((String("jetdirect-") + last).c_str(), ip.toString().c_str(), 9100, "tcp-9100");
      } else if (c.connect(ip, 631, 40)) {
        c.stop();
        addPrinter((String("ipp-") + last).c_str(), ip.toString().c_str(), 631, "tcp-631");
      }
    }
  }
  lastPrinters = (int)out.size() - before;
  message = String("printers ") + lastPrinters;
  return true;
}

bool partyTricksPrint(JsonVariantConst cfg, String &message) {
  String text = cfg["message"] | "Hello from Overlink";
  String ipStr = cfg["ip"] | "";
  int port = cfg["port"] | 9100;
  if (!ipStr.length()) {
    message = "printer ip required";
    return false;
  }
  IPAddress ip;
  if (!ip.fromString(ipStr)) {
    message = "bad printer ip";
    return false;
  }
  // Prefer raw 9100 — most reliable "open printer" demo
  if (port == 9100 || port == 0) {
    if (printRaw9100(ip, text, message)) return true;
    // fall through to IPP on 631
    port = 631;
  }
  if (printIpp(ip, (uint16_t)port, text, message)) return true;
  // last chance 9100 if user picked IPP port but 9100 open
  if (port != 9100 && printRaw9100(ip, text, message)) return true;
  return false;
}
