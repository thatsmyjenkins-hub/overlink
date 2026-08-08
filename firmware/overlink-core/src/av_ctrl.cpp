#include "av_ctrl.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <esp_random.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <string.h>

#include "av_secrets.h"
#include "device_hub.h"
#include "firetv_adb_key.h"

// --- helpers ---

static bool deckIr(const char *action) {
  String msg;
  return deviceHubDeckVizio(action, msg);
}

static bool wakeOnLan(const char *mac12) {
  if (!mac12 || strlen(mac12) < 12) return false;
  uint8_t mac[6];
  for (int i = 0; i < 6; ++i) {
    char b[3] = {mac12[i * 2], mac12[i * 2 + 1], 0};
    mac[i] = (uint8_t)strtoul(b, nullptr, 16);
  }
  uint8_t pkt[102];
  memset(pkt, 0xFF, 6);
  for (int i = 0; i < 16; ++i) memcpy(pkt + 6 + i * 6, mac, 6);
  WiFiUDP udp;
  udp.begin(0);
  IPAddress lim = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress subnetBcast((uint32_t)lim | ~(uint32_t)mask);
  const uint16_t ports[] = {9, 7};
  bool ok = false;
  for (uint16_t port : ports) {
    for (int i = 0; i < 2; ++i) {
      if (udp.beginPacket(IPAddress(255, 255, 255, 255), port) && udp.write(pkt, sizeof(pkt)) &&
          udp.endPacket())
        ok = true;
      if (udp.beginPacket(subnetBcast, port) && udp.write(pkt, sizeof(pkt)) && udp.endPacket())
        ok = true;
#ifdef PS5_IP
      if (udp.beginPacket(IPAddress(PS5_IP), port) && udp.write(pkt, sizeof(pkt)) && udp.endPacket())
        ok = true;
#endif
      delay(20);
    }
  }
  udp.stop();
  return ok;
}

// Sony Device Discovery Protocol wake (PS5 Rest Mode). Standard WOL is ignored.
static bool ps5DdpWake(const char *credential) {
  if (!credential || !credential[0]) return false;
  char pkt[220];
  int n = snprintf(pkt, sizeof(pkt),
                   "WAKEUP * HTTP/1.1\n"
                   "client-type:vr\n"
                   "auth-type:R\n"
                   "model:w\n"
                   "app-type:r\n"
                   "user-credential:%s\n"
                   "device-discovery-protocol-version:00030010\n",
                   credential);
  if (n <= 0 || n >= (int)sizeof(pkt)) return false;

  WiFiUDP udp;
  udp.begin(0);
  IPAddress lim = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress subnetBcast((uint32_t)lim | ~(uint32_t)mask);
  IPAddress host;
  host.fromString(PS5_IP);
  bool ok = false;
  for (int i = 0; i < 3; ++i) {
    if (udp.beginPacket(host, 9302) && udp.write((const uint8_t *)pkt, (size_t)n + 1) &&
        udp.endPacket())
      ok = true;
    if (udp.beginPacket(subnetBcast, 9302) && udp.write((const uint8_t *)pkt, (size_t)n + 1) &&
        udp.endPacket())
      ok = true;
    if (udp.beginPacket(IPAddress(255, 255, 255, 255), 9302) &&
        udp.write((const uint8_t *)pkt, (size_t)n + 1) && udp.endPacket())
      ok = true;
    delay(40);
  }
  udp.stop();
  return ok;
}

// --- Sony AVR ---

static bool sonyGetVolume(int &outVol) {
  String soap =
      "<?xml version=\"1.0\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
      "<s:Body><u:GetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
      "<InstanceID>0</InstanceID><Channel>Master</Channel>"
      "</u:GetVolume></s:Body></s:Envelope>";
  HTTPClient http;
  http.setTimeout(1500);
  http.setConnectTimeout(1200);
  if (!http.begin(String("http://") + SONY_IP + ":8080/RenderingControl/ctrl")) return false;
  http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  http.addHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:RenderingControl:1#GetVolume\"");
  int code = http.POST(soap);
  String body = (code > 0) ? http.getString() : String();
  http.end();
  int idx = body.indexOf("<CurrentVolume>");
  if (idx < 0) return false;
  outVol = body.substring(idx + 15).toInt();
  return true;
}

static bool sonySetVolume(int vol) {
  vol = constrain(vol, 0, 100);
  String soap =
      String("<?xml version=\"1.0\"?>"
             "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
             "<s:Body><u:SetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
             "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredVolume>") +
      String(vol) +
      "</DesiredVolume></u:SetVolume></s:Body></s:Envelope>";
  HTTPClient http;
  http.setTimeout(1500);
  http.setConnectTimeout(1200);
  if (!http.begin(String("http://") + SONY_IP + ":8080/RenderingControl/ctrl")) return false;
  http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  http.addHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:RenderingControl:1#SetVolume\"");
  int code = http.POST(soap);
  http.end();
  return code == 200;
}

static bool sonySetMute(bool mute) {
  String soap =
      String("<?xml version=\"1.0\"?>"
             "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
             "<s:Body><u:SetMute xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
             "<InstanceID>0</InstanceID><Channel>Master</Channel><DesiredMute>") +
      (mute ? "1" : "0") +
      "</DesiredMute></u:SetMute></s:Body></s:Envelope>";
  HTTPClient http;
  http.setTimeout(1500);
  http.setConnectTimeout(1200);
  if (!http.begin(String("http://") + SONY_IP + ":8080/RenderingControl/ctrl")) return false;
  http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  http.addHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:RenderingControl:1#SetMute\"");
  int code = http.POST(soap);
  http.end();
  return code == 200;
}

// --- Vizio SmartCast ---

static bool vizioHttps(const char *path, const char *method, const String &payload, String *outBody) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2000);
  HTTPClient http;
  http.setTimeout(2500);
  http.setConnectTimeout(1800);
  if (!http.begin(client, String("https://") + VIZIO_IP + ":7345" + path)) return false;
  http.addHeader("AUTH", VIZIO_AUTH);
  int code = -1;
  if (!strcmp(method, "GET")) {
    code = http.GET();
  } else {
    http.addHeader("Content-Type", "application/json");
    code = http.PUT(payload);
  }
  if (outBody) *outBody = (code > 0) ? http.getString() : String();
  http.end();
  return code > 0;
}

static bool vizioPower(bool on) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2000);
  HTTPClient http;
  http.setTimeout(2500);
  http.setConnectTimeout(2000);

  bool current = false;
  bool haveState = false;
  if (http.begin(client, String("https://") + VIZIO_IP + ":7345/state/device/power_mode")) {
    http.addHeader("AUTH", VIZIO_AUTH);
    int code = http.GET();
    String body = (code > 0) ? http.getString() : String();
    http.end();
    if (code == 200) {
      int idx = body.indexOf("\"VALUE\":");
      if (idx >= 0) {
        current = body.substring(idx + 8).toInt() == 1;
        haveState = true;
      }
    }
  }
  if (haveState && current == on) return true;
  if (!haveState && !on) {
    // fallback IR
    return deckIr("POWER_OFF");
  }
  if (!http.begin(client, String("https://") + VIZIO_IP + ":7345/key_command/")) {
    return deckIr(on ? "POWER_ON" : "POWER_OFF");
  }
  http.addHeader("AUTH", VIZIO_AUTH);
  http.addHeader("Content-Type", "application/json");
  const String body = "{\"KEYLIST\":[{\"CODESET\":11,\"CODE\":2,\"ACTION\":\"KEYPRESS\"}]}";
  int httpCode = http.PUT(body);
  http.end();
  if (httpCode > 0) return true;
  return deckIr(on ? "POWER_ON" : "POWER_OFF");
}

static bool vizioGetInput(char *out, size_t outLen) {
  if (!out || outLen < 2) return false;
  out[0] = '\0';
  String body;
  if (!vizioHttps("/menu_native/dynamic/tv_settings/devices/current_input", "GET", "", &body)) {
    return false;
  }
  int idx = body.indexOf("\"VALUE\":\"");
  if (idx < 0) return false;
  int start = idx + 9;
  int end = body.indexOf('"', start);
  if (end <= start) return false;
  String val = body.substring(start, end);
  strncpy(out, val.c_str(), outLen - 1);
  out[outLen - 1] = '\0';
  return out[0] != '\0';
}

static bool vizioSetInput(const char *hdmiName) {
  if (!hdmiName || !hdmiName[0]) return false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    String body;
    if (!vizioHttps("/menu_native/dynamic/tv_settings/devices/current_input", "GET", "", &body)) {
      continue;
    }
    int hIdx = body.indexOf("\"HASHVAL\":");
    if (hIdx < 0) continue;
    const char *p = body.c_str() + hIdx + 10;
    while (*p == ' ') ++p;
    uint32_t hash = strtoul(p, nullptr, 10);
    if (hash == 0) continue;

    char cur[24] = "";
    int vIdx = body.indexOf("\"VALUE\":\"");
    if (vIdx >= 0) {
      int start = vIdx + 9;
      int end = body.indexOf('"', start);
      if (end > start) {
        String val = body.substring(start, end);
        strncpy(cur, val.c_str(), sizeof(cur) - 1);
      }
    }
    if (cur[0] && strcasecmp(cur, hdmiName) == 0) return true;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(2500);
    HTTPClient http;
    http.setTimeout(3000);
    http.setConnectTimeout(2500);
    if (!http.begin(client, String("https://") + VIZIO_IP +
                                 ":7345/menu_native/dynamic/tv_settings/devices/current_input")) {
      continue;
    }
    http.addHeader("AUTH", VIZIO_AUTH);
    http.addHeader("Content-Type", "application/json");
    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"REQUEST\":\"MODIFY\",\"HASHVAL\":%u,\"VALUE\":\"%s\"}", (unsigned)hash, hdmiName);
    int code = http.PUT(payload);
    String resp = (code > 0) ? http.getString() : String();
    http.end();
    if (code != 200 || resp.indexOf("SUCCESS") < 0) {
      delay(80);
      continue;
    }
    // PUT SUCCESS is enough — verify GETs often time out on ESP32 even when
    // the TV already switched (was reporting fire=fail with HDMI-3 live).
    delay(80);
    char verify[24] = "";
    if (vizioGetInput(verify, sizeof(verify)) && strcasecmp(verify, hdmiName) == 0) return true;
    return true;
  }
  return false;
}

// --- Fire ADB (trimmed from cyd-basement-control) ---

static int fireAdbRng(void *, unsigned char *out, size_t len) {
  esp_fill_random(out, len);
  return 0;
}

static bool fireAdbWriteMsg(WiFiClient &c, uint32_t cmd, uint32_t a0, uint32_t a1, const uint8_t *data,
                            uint32_t len) {
  uint32_t csum = 0;
  for (uint32_t i = 0; i < len; ++i) csum += data[i];
  uint32_t magic = cmd ^ 0xffffffffu;
  uint8_t hdr[24];
  memcpy(hdr + 0, &cmd, 4);
  memcpy(hdr + 4, &a0, 4);
  memcpy(hdr + 8, &a1, 4);
  memcpy(hdr + 12, &len, 4);
  memcpy(hdr + 16, &csum, 4);
  memcpy(hdr + 20, &magic, 4);
  if (c.write(hdr, 24) != 24) return false;
  if (len && c.write(data, len) != len) return false;
  return true;
}

static bool fireAdbReadExact(WiFiClient &c, uint8_t *buf, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t until = millis() + timeoutMs;
  while (got < n && millis() < until) {
    if (!c.connected()) return false;
    if (c.available() <= 0) {
      delay(2);
      continue;
    }
    int r = c.read(buf + got, n - got);
    if (r <= 0) return false;
    got += (size_t)r;
  }
  return got == n;
}

static bool fireAdbReadMsg(WiFiClient &c, uint32_t &cmd, uint32_t &a0, uint32_t &a1, uint8_t *body,
                           size_t bodyCap, uint32_t &bodyLen, uint32_t timeoutMs) {
  uint8_t hdr[24];
  if (!fireAdbReadExact(c, hdr, 24, timeoutMs)) return false;
  memcpy(&cmd, hdr + 0, 4);
  memcpy(&a0, hdr + 4, 4);
  memcpy(&a1, hdr + 8, 4);
  memcpy(&bodyLen, hdr + 12, 4);
  if (bodyLen == 0) return true;
  if (bodyLen <= bodyCap) return fireAdbReadExact(c, body, bodyLen, timeoutMs);
  if (bodyCap && !fireAdbReadExact(c, body, bodyCap, timeoutMs)) return false;
  uint32_t left = bodyLen - (uint32_t)bodyCap;
  uint8_t junk[64];
  while (left) {
    uint32_t n = left > sizeof(junk) ? (uint32_t)sizeof(junk) : left;
    if (!fireAdbReadExact(c, junk, n, timeoutMs)) return false;
    left -= n;
  }
  bodyLen = (uint32_t)bodyCap;
  return true;
}

static bool fireAdbSignToken(const uint8_t token[20], uint8_t *sig, size_t *sigLen) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int pret = mbedtls_pk_parse_key(&pk, (const unsigned char *)FIRETV_ADB_KEY,
                                  strlen(FIRETV_ADB_KEY) + 1, nullptr, 0, fireAdbRng, nullptr);
  if (pret != 0 || !mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA)) {
    mbedtls_pk_free(&pk);
    return false;
  }
  mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
  *sigLen = mbedtls_rsa_get_len(rsa);
  if (*sigLen == 0 || *sigLen > 512) {
    mbedtls_pk_free(&pk);
    return false;
  }
  // mbedtls 3.x: no mode arg; private key context implies private op
  int sret = mbedtls_rsa_pkcs1_sign(rsa, fireAdbRng, nullptr, MBEDTLS_MD_SHA1, 20, token, sig);
  mbedtls_pk_free(&pk);
  return sret == 0;
}

static bool fireAdbConnectAuthed(WiFiClient &c) {
  // Single connect — probing first doubled latency and blocked the portal.
  c.setTimeout(1500);
  if (!c.connect(FIRETV_IP, 5555, 1200)) return false;

  const uint8_t hello[] = {'h', 'o', 's', 't', ':', ':', 0};
  if (!fireAdbWriteMsg(c, 0x4e584e43 /*CNXN*/, 0x01000001, 4096, hello, sizeof(hello))) return false;

  uint8_t body[512];
  uint32_t cmd = 0, a0 = 0, a1 = 0, blen = 0;
  if (!fireAdbReadMsg(c, cmd, a0, a1, body, sizeof(body), blen, 1800)) return false;

  if (cmd == 0x48545541 /*AUTH*/ && a0 == 1 /*TOKEN*/) {
    if (blen != 20) return false;
    uint8_t sig[512];
    size_t sigLen = 0;
    if (!fireAdbSignToken(body, sig, &sigLen)) return false;
    if (!fireAdbWriteMsg(c, 0x48545541, 2 /*SIGNATURE*/, 0, sig, (uint32_t)sigLen)) return false;
    if (!fireAdbReadMsg(c, cmd, a0, a1, body, sizeof(body), blen, 1800)) return false;
  }
  return cmd == 0x4e584e43 /*CNXN*/;
}

static bool fireAdbShell(const char *shellCmd) {
  if (!shellCmd || !shellCmd[0]) return false;
  WiFiClient c;
  if (!fireAdbConnectAuthed(c)) {
    c.stop();
    return false;
  }
  char shell[180];
  snprintf(shell, sizeof(shell), "shell:%s", shellCmd);
  const uint32_t localId = 1;
  if (!fireAdbWriteMsg(c, 0x4e45504f /*OPEN*/, localId, 0, (const uint8_t *)shell,
                       (uint32_t)strlen(shell) + 1)) {
    c.stop();
    return false;
  }
  uint8_t body[256];
  uint32_t cmd = 0, a0 = 0, a1 = 0, blen = 0;
  uint32_t remoteId = 0;
  bool ok = false;
  bool sawErr = false;
  uint32_t until = millis() + 2800;
  while (millis() < until) {
    if (!fireAdbReadMsg(c, cmd, a0, a1, body, sizeof(body), blen, 600)) break;
    if (cmd == 0x59414b4f /*OKAY*/) {
      remoteId = a0;
      ok = true;
      continue;
    }
    if (cmd == 0x45545257 /*WRTE*/) {
      remoteId = a0;
      if (blen) {
        // Null-terminate for strstr; body may be partial.
        size_t n = blen < sizeof(body) ? blen : sizeof(body) - 1;
        body[n] = 0;
        if (strstr((char *)body, "Error") || strstr((char *)body, "Exception") ||
            strstr((char *)body, "does not exist"))
          sawErr = true;
      }
      fireAdbWriteMsg(c, 0x59414b4f /*OKAY*/, localId, remoteId, nullptr, 0);
      ok = true;
      continue;
    }
    if (cmd == 0x45534c43 /*CLSE*/) break;
  }
  c.stop();
  return ok && !sawErr;
}

static bool fireLaunchPkg(const char *package) {
  if (!package || !package[0]) return false;
  // am start is more reliable than monkey on Fire Cube (fewer false OKAY/timeouts).
  char cmd[160];
  snprintf(cmd, sizeof(cmd),
           "am start -a android.intent.action.MAIN -c android.intent.category.LEANBACK_LAUNCHER -p %s",
           package);
  if (fireAdbShell(cmd)) return true;
  // Fallback for picky packages
  snprintf(cmd, sizeof(cmd), "monkey -p %s -c android.intent.category.LEANBACK_LAUNCHER 1", package);
  return fireAdbShell(cmd);
}

static bool fireLaunchComponent(const char *component) {
  if (!component || !component[0]) return false;
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "am start -n %s", component);
  return fireAdbShell(cmd);
}

static bool fireOpenUrl(const char *url) {
  if (!url || !url[0]) return false;
  char cmd[160];
  snprintf(cmd, sizeof(cmd), "am start -a android.intent.action.VIEW -d '%s'", url);
  return fireAdbShell(cmd);
}

// --- public ---

void avCtrlBegin() { Serial.println("[AV] Vizio/Fire/Sony bridge ready"); }

bool avEnsureWatching(String &detail) {
  bool pwr = vizioPower(true);
  // Soft-fail input quickly so WATCH doesn't freeze the portal for 8s+.
  bool inp = vizioSetInput(FIRETV_HDMI);
  if (!inp) deckIr("INPUT");  // one IR nudge; user can press again if needed
  bool mute = sonySetMute(false);
  detail = String("watch pwr=") + (pwr ? "ok" : "fail") + " fire=" + (inp ? "ok" : "fail") +
           " unmute=" + (mute ? "ok" : "fail");
  return pwr || inp || mute;
}

bool avEnsurePs5(String &detail) {
  // PS5 Rest Mode ignores Ethernet WOL — needs DDP WAKEUP + Remote Play credential.
  const char *cred = "";
#ifdef PS5_USER_CREDENTIAL
  cred = PS5_USER_CREDENTIAL;
#endif
  bool ddp = false;
  if (cred[0]) ddp = ps5DdpWake(cred);

  // Best-effort extras (harmless if ignored)
  wakeOnLan(PS5_MAC);
#ifdef PS5_MAC_WIFI
  wakeOnLan(PS5_MAC_WIFI);
#endif

  bool pwr = vizioPower(true);
  bool inp = vizioSetInput(PS5_HDMI);
  if (!inp) deckIr("INPUT");
  sonySetMute(false);

  // Second DDP burst after TV/HDMI settle — console sometimes misses the first.
  if (cred[0]) {
    delay(200);
    ddp = ps5DdpWake(cred) || ddp;
  }

  if (!cred[0]) {
    detail = "ps5 need credential — run tools/ps5_capture_cred.sh";
    return pwr || inp;  // still switch TV to PS5 HDMI
  }
  detail = String("ps5 wake=") + (ddp ? "ok" : "fail") + " pwr=" + (pwr ? "ok" : "fail") +
           " hdmi=" + (inp ? "ok" : "fail");
  return ddp || pwr || inp;
}

bool avLaunchApp(const char *appId, String &detail) {
  if (!appId) {
    detail = "no app";
    return false;
  }
  String id = appId;
  id.toLowerCase();

  if (id == "ps5") return avEnsurePs5(detail);

  // Fast path: launch on Fire Cube via ADB. Then nudge Vizio to HDMI-3 so the
  // TV actually shows the Cube (launch alone looked "broken" on wrong input).
  bool ok = false;
  const char *label = appId;
  if (id == "nflx" || id == "netflix") {
    label = "NETFLIX";
    ok = fireLaunchComponent("com.netflix.ninja/com.netflix.ninja.MainActivity") ||
         fireLaunchPkg("com.netflix.ninja");
  } else if (id == "yt" || id == "youtube") {
    label = "YOUTUBE";
    // Prefer classic YT; .tv variant also present on this Cube.
    ok = fireLaunchComponent("com.amazon.firetv.youtube/dev.cobalt.app.MainActivity") ||
         fireLaunchComponent("com.amazon.firetv.youtube.tv/dev.cobalt.app.MainActivity") ||
         fireLaunchPkg("com.amazon.firetv.youtube");
  } else if (id == "disney" || id == "dsn+" || id == "dsn") {
    label = "DISNEY";
    ok = fireLaunchComponent(
             "com.disney.disneyplus/com.bamtechmedia.dominguez.main.MainActivity") ||
         fireLaunchPkg("com.disney.disneyplus");
  } else if (id == "prime") {
    label = "PRIME";
    // Prime Video on Cube is firebat — NOT a browser URL (that opened Silk).
    ok = fireLaunchPkg("com.amazon.firebat") ||
         fireLaunchComponent("com.amazon.firebat/com.amazon.pyrocore.IgnitionActivity");
  } else {
    detail = "unknown app";
    return false;
  }

  if (!ok) {
    // Fire ADB dark: wake TV/input, then retry once.
    String wake;
    avEnsureWatching(wake);
    if (id == "prime")
      ok = fireLaunchPkg("com.amazon.firebat");
    else if (id == "nflx" || id == "netflix")
      ok = fireLaunchPkg("com.netflix.ninja");
    else if (id == "yt" || id == "youtube")
      ok = fireLaunchPkg("com.amazon.firetv.youtube");
    else if (id == "disney" || id == "dsn+" || id == "dsn")
      ok = fireLaunchPkg("com.disney.disneyplus");
  } else {
    // Don't block the portal on full watch-ensure; just flip to Fire HDMI.
    if (!vizioSetInput(FIRETV_HDMI)) deckIr("INPUT");
  }

  detail = String(label) + (ok ? " launched" : " launch fail");
  return ok;
}

bool avVolDelta(int delta, int &levelOut, String &detail) {
  int vol = 18;
  if (!sonyGetVolume(vol)) {
    // IR fallback
    bool ok = deckIr(delta >= 0 ? "VOL+" : "VOL-");
    levelOut = -1;
    detail = ok ? "vol ir" : "vol fail";
    return ok;
  }
  vol = constrain(vol + delta, 0, 100);
  bool ok = sonySetVolume(vol);
  if (ok) sonySetMute(false);
  levelOut = vol;
  detail = String("vol ") + vol;
  return ok;
}

bool avSetMute(bool mute, String &detail) {
  bool ok = sonySetMute(mute);
  if (!ok) ok = deckIr("MUTE");
  detail = mute ? "muted" : "unmuted";
  return ok;
}

bool avFireKey(int keycode, String &detail) {
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "input keyevent %d", keycode);
  bool ok = fireAdbShell(cmd);
  detail = ok ? String("key ") + keycode : "key fail";
  if (!ok) {
    // map common nav to Vizio IR
    if (keycode == 3) ok = deckIr("HOME");
    else if (keycode == 4) ok = deckIr("BACK");
    else if (keycode == 23) ok = deckIr("OK");
    detail = ok ? "ir fallback" : detail;
  }
  return ok;
}

bool avSceneApply(const String &sceneId, String &detail) {
  String id = sceneId;
  id.toLowerCase();

  auto vol = [&](int level) {
    sonySetMute(false);
    sonySetVolume(level);
  };

  if (id == "movie") {
    bool ok = avEnsureWatching(detail);
    vol(38);
    detail = "MOVIE av";
    return ok;
  }
  if (id == "sports" || id == "karaoke") {
    bool ok = avEnsureWatching(detail);
    vol(id == "sports" ? 34 : 36);
    detail = String(id) + " av";
    return ok;
  }
  if (id == "game") {
    bool ok = avEnsurePs5(detail);
    vol(28);
    detail = "GAME av";
    return ok;
  }
  if (id == "full") {
    vol(26);
    detail = "FULL av vol";
    return true;
  }
  if (id == "chill") {
    vol(18);
    detail = "CHILL av vol";
    return true;
  }
  if (id == "dance") {
    vol(38);
    detail = "DANCE av vol";
    return true;
  }
  if (id == "date") {
    vol(12);
    detail = "DATE av vol";
    return true;
  }
  if (id == "bed") {
    bool ok = sonySetMute(true);
    detail = "BED mute";
    return ok || deckIr("MUTE");
  }
  if (id == "off") {
    sonySetMute(true);
    bool pwr = vizioPower(false);
    detail = pwr ? "OFF av" : "OFF av partial";
    return true;
  }
  detail = "no av";
  return true;
}
