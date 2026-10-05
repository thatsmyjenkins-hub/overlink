#include "cloud_bridges.h"

#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>

#include "connector_store.h"
#include "device_hub.h"
#include "grid_store.h"

static String urlEncode(const String &s) {
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~')
      o += c;
    else if (c == ' ')
      o += '+';
    else {
      char buf[5];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      o += buf;
    }
  }
  return o;
}

static bool httpsFormPost(const String &url, const String &form, String &response, int &code) {
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);
  http.setConnectTimeout(8000);
  if (!http.begin(cli, url)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  code = http.POST(form);
  response = code > 0 ? http.getString() : "";
  http.end();
  return code > 0;
}

static bool httpsJson(const char *method, const String &url, const String &bearer, const String &json,
                      String &response, int &code, int timeoutMs = 14000,
                      const char *hostOverride = nullptr) {
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(8000);
  if (!http.begin(cli, url)) return false;
  if (hostOverride && hostOverride[0]) http.addHeader("Host", hostOverride, true, true);
  if (bearer.length()) http.addHeader("Authorization", String("Bearer ") + bearer);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (!strcmp(method, "GET"))
    code = http.GET();
  else if (!strcmp(method, "PUT"))
    code = http.PUT(json);
  else
    code = http.POST(json);
  response = code > 0 ? http.getString() : "";
  http.end();
  return code > 0;
}

static void splitHsEntity(const String &eid, String &id, String &inst) {
  int h = eid.indexOf('#');
  if (h < 0) {
    id = eid;
    inst = "";
    return;
  }
  id = eid.substring(0, h);
  inst = eid.substring(h + 1);
}

static String hsSnippet(const String &body) {
  if (!body.length()) return "";
  String s = body.substring(0, 120);
  s.replace('\n', ' ');
  return s;
}

static bool hubspaceSaveSecret(JsonDocument &secret, String &message) {
  JsonDocument conn;
  conn["id"] = "hubspace";
  conn["name"] = "Hubspace";
  conn["type"] = "hubspace";
  conn["transport"] = "cloud";
  conn["enabled"] = true;
  if (!connectorStoreUpsert(conn.as<JsonVariantConst>(), message)) return false;
  return connectorStoreSetSecret("hubspace", secret.as<JsonVariantConst>(), message);
}

static bool hubspaceParseMe(const String &body, String &accountId) {
  JsonDocument udoc;
  if (deserializeJson(udoc, body)) return false;
  // Top-level accountId is often the *user* id. Metadevices need accountAccess.accountId.
  if (udoc["accountAccess"].is<JsonArray>() && udoc["accountAccess"].size()) {
    JsonObject acc = udoc["accountAccess"][0].as<JsonObject>();
    accountId = acc["account"]["accountId"] | "";
    if (!accountId.length()) accountId = acc["accountId"] | "";
    if (!accountId.length()) accountId = acc["account"]["id"] | "";
  }
  if (!accountId.length()) accountId = udoc["account"]["accountId"] | "";
  if (!accountId.length()) accountId = udoc["accountId"] | "";
  return accountId.length() > 0;
}

static bool hubspaceApplyTokens(JsonDocument &tok, const char *email, const char *password,
                                String &message) {
  const char *refresh = tok["refresh_token"] | "";
  const char *access = tok["access_token"] | "";
  const char *idTok = tok["id_token"] | "";
  int expIn = tok["expires_in"] | 300;
  if (!refresh[0] && !idTok[0] && !access[0]) {
    message = "hubspace no tokens";
    return false;
  }
  String bearer = idTok[0] ? idTok : access;
  String me;
  int meCode = 0;
  httpsJson("GET", "https://api2.afero.net/v1/users/me", bearer, "", me, meCode);
  String accountId;
  if (meCode == 200) hubspaceParseMe(me, accountId);

  JsonDocument secret;
  if (email && email[0]) secret["email"] = email;
  if (password && password[0]) secret["password"] = password;
  if (refresh[0]) secret["refreshToken"] = refresh;
  if (access[0]) secret["accessToken"] = access;
  if (idTok[0]) secret["idToken"] = idTok;
  if (accountId.length()) secret["accountId"] = accountId;
  secret["issuedMs"] = millis();
  secret["expiresIn"] = expIn;
  if (!hubspaceSaveSecret(secret, message)) return false;
  message = accountId.length() ? String("hubspace linked ") + accountId : "hubspace linked";
  return true;
}

bool hubspaceLogin(const char *email, const char *password, String &message) {
  if (!email || !email[0] || !password || !password[0]) {
    message = "email + password required";
    return false;
  }
  String form = String("grant_type=password&client_id=hubspace_android&username=") +
                urlEncode(email) + "&password=" + urlEncode(password) +
                "&scope=" + urlEncode("openid offline_access");
  String resp;
  int code = 0;
  if (!httpsFormPost(
          "https://accounts.hubspaceconnect.com/auth/realms/thd/protocol/openid-connect/token",
          form, resp, code)) {
    message = "hubspace connect fail";
    return false;
  }
  if (code != 200) {
    message = String("hubspace login http ") + code;
    String snip = hsSnippet(resp);
    if (snip.length()) message += String(" ") + snip;
    if (code == 400 || code == 401)
      message += " — Hubspace may require app OTP; HA Hubspace bridge also works";
    return false;
  }
  JsonDocument tok;
  if (deserializeJson(tok, resp)) {
    message = "hubspace token parse fail";
    return false;
  }
  return hubspaceApplyTokens(tok, email, password, message);
}

static bool hubspaceEnsureToken(String &bearer, String &accountId, String &message) {
  JsonDocument secret;
  if (!connectorStoreGetSecret("hubspace", secret)) {
    message = "hubspace login first (Ops → CONN)";
    return false;
  }
  accountId = secret["accountId"] | "";
  String refresh = secret["refreshToken"] | "";
  String idTok = secret["idToken"] | "";
  String access = secret["accessToken"] | "";
  String email = secret["email"] | "";
  String password = secret["password"] | "";
  uint32_t issued = secret["issuedMs"] | 0;
  int expIn = secret["expiresIn"] | 300;
  bearer = idTok.length() ? idTok : access;
  int slack = expIn > 60 ? expIn - 30 : 30;
  bool fresh = issued && bearer.length() && (millis() - issued) < (uint32_t)slack * 1000UL;
  if (fresh && accountId.length()) return true;

  if (refresh.length()) {
    String form = String("grant_type=refresh_token&client_id=hubspace_android&refresh_token=") +
                  urlEncode(refresh);
    String resp;
    int code = 0;
    if (httpsFormPost(
            "https://accounts.hubspaceconnect.com/auth/realms/thd/protocol/openid-connect/token",
            form, resp, code) &&
        code == 200) {
      JsonDocument tok;
      if (!deserializeJson(tok, resp) && hubspaceApplyTokens(tok, email.c_str(), password.c_str(),
                                                             message)) {
        connectorStoreGetSecret("hubspace", secret);
        accountId = secret["accountId"] | accountId;
        idTok = secret["idToken"] | "";
        access = secret["accessToken"] | "";
        bearer = idTok.length() ? idTok : access;
        if (bearer.length()) return true;
      }
    }
  }
  if (email.length() && password.length() && hubspaceLogin(email.c_str(), password.c_str(), message)) {
    connectorStoreGetSecret("hubspace", secret);
    accountId = secret["accountId"] | "";
    idTok = secret["idToken"] | "";
    access = secret["accessToken"] | "";
    bearer = idTok.length() ? idTok : access;
    return bearer.length() > 0;
  }
  if (!bearer.length()) {
    if (!message.length()) message = "hubspace token expired";
    return false;
  }
  if (!accountId.length()) {
    String me;
    int meCode = 0;
    httpsJson("GET", "https://api2.afero.net/v1/users/me", bearer, "", me, meCode);
    if (meCode != 200 || !hubspaceParseMe(me, accountId)) {
      message = "hubspace account id missing";
      return false;
    }
  }
  return true;
}

static const char *hsClassify(const char *deviceClass, const char *typeId, const char *name,
                              bool hasLock, bool hasColor, bool hasBri, bool hasPower) {
  String dc = deviceClass ? deviceClass : "";
  String tid = typeId ? typeId : "";
  String n = name ? name : "";
  dc.toLowerCase();
  tid.toLowerCase();
  n.toLowerCase();
  if (tid.indexOf("room") >= 0 || tid.indexOf("zone") >= 0 || tid.indexOf("group") >= 0)
    return nullptr;
  if (hasLock || dc.indexOf("lock") >= 0 || tid.indexOf("lock") >= 0 || n.indexOf("lock") >= 0)
    return "lock";
  if (dc.indexOf("light") >= 0 || dc.indexOf("bulb") >= 0 || dc.indexOf("lamp") >= 0 ||
      tid.indexOf("light") >= 0 || n.indexOf("lamp") >= 0 || n.indexOf("bulb") >= 0 ||
      n.indexOf("light") >= 0 || hasColor || hasBri)
    return "hubspace_light";
  if (dc.indexOf("outlet") >= 0 || dc.indexOf("plug") >= 0 || dc.indexOf("switch") >= 0 ||
      n.indexOf("strip") >= 0 || n.indexOf("outlet") >= 0 || n.indexOf("plug") >= 0 || hasPower)
    return "outlet";
  return nullptr;
}

static const char *hsZoneFor(const char *type, const char *name) {
  String n = name ? name : "";
  n.toLowerCase();
  if (!strcmp(type, "lock")) return "entry";
  if (!strcmp(type, "hubspace_light")) return "entry";
  if (!strcmp(type, "garage")) return "entry";
  if (n.indexOf("front") >= 0 || n.indexOf("entry") >= 0 || n.indexOf("door") >= 0 ||
      n.indexOf("porch") >= 0)
    return "entry";
  return "main";
}

static String hsShortId(const char *uuid) {
  String s;
  if (!uuid) return s;
  for (const char *p = uuid; *p && s.length() < 10; p++) {
    if (*p == '-') continue;
    s += (char)tolower(*p);
  }
  return s;
}

static bool hubspaceFetchMe(const String &access, const String &idTok, String &body, int &code) {
  const String *tokens[] = {&idTok, &access};
  for (const String *tok : tokens) {
    if (!tok->length()) continue;
    if (httpsJson("GET", "https://api2.afero.net/v1/users/me", *tok, "", body, code, 12000) &&
        code == 200)
      return true;
  }
  return false;
}

static bool hubspaceGetMetadevices(const String &accountId, const String &access, const String &idTok,
                                   String &body, int &code) {
  const char *bases[] = {"https://semantics2.afero.net", "https://api2.afero.net"};
  const char *suffixes[] = {"/metadevices?expansions=state", "/metadevices"};
  String tokens[2];
  tokens[0] = access.length() ? access : idTok;
  tokens[1] = (idTok.length() && idTok != tokens[0]) ? idTok : "";
  for (const char *base : bases) {
    for (const char *suf : suffixes) {
      String url = String(base) + "/v1/accounts/" + accountId + suf;
      const char *hostOv = strstr(base, "api2") ? "semantics2.afero.net" : nullptr;
      for (int t = 0; t < 2; t++) {
        if (!tokens[t].length()) continue;
        if (!httpsJson("GET", url, tokens[t], "", body, code, 20000, hostOv)) continue;
        if (code == 200) return true;
      }
    }
  }
  return false;
}

bool hubspaceImport(String &message) {
  String bearer, accountId;
  if (!hubspaceEnsureToken(bearer, accountId, message)) return false;
  JsonDocument secret;
  connectorStoreGetSecret("hubspace", secret);
  String access = secret["accessToken"] | "";
  String idTok = secret["idToken"] | "";
  if (!access.length()) access = bearer;
  if (!idTok.length()) idTok = bearer;

  String meBody;
  int meCode = 0;
  if (hubspaceFetchMe(access, idTok, meBody, meCode)) {
    String parsed;
    if (hubspaceParseMe(meBody, parsed) && parsed.length()) {
      if (parsed != accountId) {
        accountId = parsed;
        secret["accountId"] = accountId;
        String dummy;
        connectorStoreSetSecret("hubspace", secret.as<JsonVariantConst>(), dummy);
      }
    }
  }

  String body;
  int code = 0;
  if (!hubspaceGetMetadevices(accountId, access, idTok, body, code) || code != 200) {
    message = String("hubspace metadevices http ") + code + " acct=" + accountId.substring(0, 8) +
              " " + hsSnippet(body);
    return false;
  }
  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["deviceId"] = true;
  filter[0]["friendlyName"] = true;
  filter[0]["typeId"] = true;
  filter[0]["description"]["device"]["deviceClass"] = true;
  filter[0]["description"]["device"]["model"] = true;
  filter[0]["description"]["defaultName"] = true;
  filter[0]["state"]["values"][0]["functionClass"] = true;
  filter[0]["state"]["values"][0]["functionInstance"] = true;
  filter["metadevices"][0]["id"] = true;
  filter["metadevices"][0]["deviceId"] = true;
  filter["metadevices"][0]["friendlyName"] = true;
  filter["metadevices"][0]["typeId"] = true;
  filter["metadevices"][0]["description"]["device"]["deviceClass"] = true;
  filter["metadevices"][0]["description"]["defaultName"] = true;
  filter["metadevices"][0]["state"]["values"][0]["functionClass"] = true;
  filter["metadevices"][0]["state"]["values"][0]["functionInstance"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    message = String("hubspace parse fail: ") + err.c_str();
    return false;
  }
  JsonArray arr;
  if (doc.is<JsonArray>())
    arr = doc.as<JsonArray>();
  else
    arr = doc["metadevices"].as<JsonArray>();
  if (arr.isNull()) {
    message = "hubspace no metadevices";
    return false;
  }

  int added = 0;
  int seen = 0;
  for (JsonObject md : arr) {
    const char *mid = md["id"] | md["deviceId"] | "";
    if (!mid[0]) continue;
    const char *typeId = md["typeId"] | "";
    const char *name = md["friendlyName"] | md["description"]["defaultName"] | "";
    const char *dclass = md["description"]["device"]["deviceClass"] | "";
    bool hasLock = false, hasColor = false, hasBri = false, hasPower = false;
    String powerInst[8];
    int nPower = 0;
    if (md["state"]["values"].is<JsonArray>()) {
      for (JsonObject v : md["state"]["values"].as<JsonArray>()) {
        const char *fc = v["functionClass"] | "";
        const char *inst = v["functionInstance"] | "";
        if (!strcmp(fc, "lock-control") || !strcmp(fc, "lock-state")) hasLock = true;
        else if (!strcmp(fc, "color-rgb")) hasColor = true;
        else if (!strcmp(fc, "brightness") || !strcmp(fc, "color-temperature")) hasBri = true;
        else if (!strcmp(fc, "power")) {
          hasPower = true;
          if (inst[0] && nPower < 8) {
            bool dup = false;
            for (int i = 0; i < nPower; i++)
              if (powerInst[i] == inst) dup = true;
            if (!dup) powerInst[nPower++] = inst;
          }
        }
      }
    }
    const char *type = hsClassify(dclass, typeId, name, hasLock, hasColor, hasBri, hasPower);
    if (!type) continue;
    if (!name[0]) name = type;
    seen++;

    auto addOne = [&](const String &entityId, const String &label) {
      String id = String("hs-") + hsShortId(mid);
      int hash = entityId.indexOf('#');
      if (hash >= 0) {
        String inst = entityId.substring(hash + 1);
        inst.replace(" ", "");
        id += "-";
        id += inst.substring(0, 8);
      }
      JsonDocument d;
      d["id"] = id;
      d["type"] = type;
      d["name"] = label;
      d["zoneId"] = hsZoneFor(type, name);
      d["entityId"] = entityId;
      d["connector"] = "hubspace";
      if (!strcmp(type, "hubspace_light")) d["caps"] = hasColor ? "color" : (hasBri ? "ct" : "dim");
      String msg;
      if (deviceHubAddDevice(d.as<JsonVariantConst>(), msg)) added++;
      else if (msg.indexOf("exists") >= 0) added++;
    };

    if (!strcmp(type, "outlet") && nPower > 1) {
      for (int i = 0; i < nPower; i++) {
        String label = String(name) + " · " + powerInst[i];
        addOne(String(mid) + "#" + powerInst[i], label);
      }
    } else {
      addOne(String(mid), String(name));
    }
    if (added >= 20) break;
  }
  message = String("hubspace imported ") + added + " / " + seen;
  return added > 0 || seen == 0;
}

bool hubspaceControl(const char *entityId, const char *action, bool on, int dimming, int tempK,
                     int r, int g, int b, String &message) {
  if (!entityId || !entityId[0]) {
    message = "hubspace entity missing";
    return false;
  }
  String bearer, accountId;
  if (!hubspaceEnsureToken(bearer, accountId, message)) return false;
  String mid, inst;
  splitHsEntity(entityId, mid, inst);

  JsonDocument body;
  JsonArray values = body["values"].to<JsonArray>();
  auto addStr = [&](const char *fn, const char *val) {
    JsonObject o = values.add<JsonObject>();
    o["functionClass"] = fn;
    if (inst.length()) o["functionInstance"] = inst;
    o["value"] = val;
  };
  auto addInt = [&](const char *fn, int val) {
    JsonObject o = values.add<JsonObject>();
    o["functionClass"] = fn;
    if (inst.length()) o["functionInstance"] = inst;
    o["value"] = val;
  };

  String act = action ? action : "";
  if (act == "lock" || act == "unlock") {
    addStr("lock-control", act.c_str());
  } else if (!on || act == "off") {
    addStr("power", "off");
  } else {
    addStr("power", "on");
    if (r >= 0 && g >= 0 && b >= 0) {
      addStr("color-mode", "color");
      JsonObject o = values.add<JsonObject>();
      o["functionClass"] = "color-rgb";
      if (inst.length()) o["functionInstance"] = inst;
      o["value"]["color-rgb"]["r"] = constrain(r, 0, 255);
      o["value"]["color-rgb"]["g"] = constrain(g, 0, 255);
      o["value"]["color-rgb"]["b"] = constrain(b, 0, 255);
    } else if (tempK >= 2000) {
      addStr("color-mode", "white");
      addInt("color-temperature", constrain(tempK, 2200, 6500));
    }
    if (dimming >= 1) addInt("brightness", constrain(dimming, 1, 100));
  }

  String payload;
  serializeJson(body, payload);
  String url = String("https://semantics2.afero.net/v1/accounts/") + accountId + "/metadevices/" +
               mid + "/state";
  String resp;
  int code = 0;
  if (!httpsJson("PUT", url, bearer, payload, resp, code, 14000) || code < 200 || code >= 300) {
    url = String("https://api2.afero.net/v1/accounts/") + accountId + "/metadevices/" + mid +
          "/state";
    if (!httpsJson("PUT", url, bearer, payload, resp, code, 14000, "semantics2.afero.net")) {
      message = "hubspace control connect fail";
      return false;
    }
  }
  if (code < 200 || code >= 300) {
    message = String("hubspace control http ") + code + " " + hsSnippet(resp);
    return false;
  }
  if (act.length())
    message = String("hubspace ") + act;
  else
    message = on ? "hubspace on" : "hubspace off";
  return true;
}

static bool winkSaveSecret(JsonDocument &secret, String &message) {
  JsonDocument conn;
  conn["id"] = "wink";
  conn["name"] = "Wink";
  conn["type"] = "wink";
  conn["transport"] = "cloud";
  conn["enabled"] = true;
  if (!connectorStoreUpsert(conn.as<JsonVariantConst>(), message)) return false;
  return connectorStoreSetSecret("wink", secret.as<JsonVariantConst>(), message);
}

bool winkLogin(const char *email, const char *password, const char *clientId,
               const char *clientSecret, const char *token, String &message) {
  String access = token ? token : "";
  if (!access.length()) {
    if (!email || !email[0] || !password || !password[0]) {
      message = "Wink needs email+password+client_id or a pasted access token";
      return false;
    }
    if (!clientId || !clientId[0]) {
      message = "Wink cloud needs a developer client_id (or paste an access token)";
      return false;
    }
    JsonDocument req;
    req["client_id"] = clientId;
    if (clientSecret && clientSecret[0]) req["client_secret"] = clientSecret;
    req["username"] = email;
    req["password"] = password;
    req["grant_type"] = "password";
    String payload, resp;
    serializeJson(req, payload);
    int code = 0;
    if (!httpsJson("POST", "https://api.wink.com/oauth2/token", "", payload, resp, code)) {
      message = "wink connect fail";
      return false;
    }
    if (code != 200) {
      message = String("wink login http ") + code + " " + hsSnippet(resp) +
                " — Wink subscriptions are mostly retired";
      return false;
    }
    JsonDocument tok;
    if (deserializeJson(tok, resp)) {
      message = "wink token parse fail";
      return false;
    }
    access = tok["access_token"] | tok["data"]["access_token"] | "";
    if (!access.length()) {
      message = "wink no access_token";
      return false;
    }
  }
  String me;
  int meCode = 0;
  if (!httpsJson("GET", "https://api.wink.com/users/me", access, "", me, meCode) || meCode != 200) {
    message = String("wink token rejected http ") + meCode +
              " — local hub :8888 still needs this cloud token";
    return false;
  }
  JsonDocument secret;
  if (email && email[0]) secret["email"] = email;
  if (password && password[0]) secret["password"] = password;
  if (clientId && clientId[0]) secret["clientId"] = clientId;
  if (clientSecret && clientSecret[0]) secret["clientSecret"] = clientSecret;
  secret["accessToken"] = access;
  secret["issuedMs"] = millis();
  if (!winkSaveSecret(secret, message)) return false;
  message = "wink linked";
  return true;
}

bool winkImport(String &message) {
  JsonDocument secret;
  if (!connectorStoreGetSecret("wink", secret)) {
    message = "wink login first";
    return false;
  }
  String access = secret["accessToken"] | "";
  if (!access.length()) {
    message = "wink token missing";
    return false;
  }
  String body;
  int code = 0;
  if (!httpsJson("GET", "https://api.wink.com/users/me/wink_devices", access, "", body, code,
                 16000) ||
      code != 200) {
    message = String("wink devices http ") + code + " " + hsSnippet(body);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    message = "wink parse fail";
    return false;
  }
  JsonArray arr = doc["data"].as<JsonArray>();
  if (arr.isNull() && doc.is<JsonArray>()) arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    message = "wink no devices";
    return false;
  }
  int added = 0;
  for (JsonObject d : arr) {
    const char *otype = d["object_type"] | "";
    const char *name = d["name"] | "";
    const char *oid = "";
    if (d["object_id"].is<int>()) {
      static char idbuf[24];
      snprintf(idbuf, sizeof(idbuf), "%d", (int)d["object_id"]);
      oid = idbuf;
    } else {
      oid = d["object_id"] | d["uuid"] | "";
    }
    if (!oid[0] || !name[0]) continue;
    const char *type = nullptr;
    const char *zone = "main";
    if (!strcmp(otype, "lock")) {
      type = "lock";
      zone = "entry";
    } else if (!strcmp(otype, "light_bulb")) {
      type = "hubspace_light";
      zone = "main";
    } else if (!strcmp(otype, "binary_switch") || !strcmp(otype, "powerstrip")) {
      type = "outlet";
    } else if (!strcmp(otype, "garage_door") || !strcmp(otype, "shade")) {
      type = "garage";
      zone = "entry";
    } else
      continue;
    JsonDocument nd;
    nd["id"] = String("wink-") + hsShortId(oid);
    nd["type"] = type;
    nd["name"] = name;
    nd["zoneId"] = zone;
    nd["entityId"] = String(otype) + "s/" + oid;
    if (!strcmp(otype, "binary_switch")) nd["entityId"] = String("binary_switches/") + oid;
    else if (!strcmp(otype, "garage_door")) nd["entityId"] = String("garage_doors/") + oid;
    nd["connector"] = "wink";
    if (!strcmp(type, "hubspace_light")) nd["caps"] = "color";
    String msg;
    if (deviceHubAddDevice(nd.as<JsonVariantConst>(), msg) || msg.indexOf("exists") >= 0) added++;
    if (added >= 16) break;
  }
  message = String("wink imported ") + added;
  return added > 0;
}

bool winkControl(const char *entityId, const char *action, bool on, int dimming, int r, int g,
                 int b, String &message) {
  if (!entityId || !entityId[0]) {
    message = "wink entity missing";
    return false;
  }
  JsonDocument secret;
  if (!connectorStoreGetSecret("wink", secret)) {
    message = "wink login first";
    return false;
  }
  String access = secret["accessToken"] | "";
  if (!access.length()) {
    message = "wink token missing";
    return false;
  }
  JsonDocument desired;
  String act = action ? action : "";
  String path = entityId;
  if (path.startsWith("lock")) {
    desired["desired_state"]["locked"] = (act != "unlock");
  } else if (path.startsWith("garage")) {
    if (act != "open" && act != "close") {
      message = "wink garage needs open or close";
      return false;
    }
    desired["desired_state"]["position"] = (act == "open") ? 1.0 : 0.0;
  } else {
    desired["desired_state"]["powered"] = on && act != "off";
    if (on && dimming >= 1) desired["desired_state"]["brightness"] = constrain(dimming, 1, 100) / 100.0;
    if (on && r >= 0 && g >= 0 && b >= 0) {
      desired["desired_state"]["color_model"] = "rgb";
      desired["desired_state"]["color"][0] = constrain(r, 0, 255);
      desired["desired_state"]["color"][1] = constrain(g, 0, 255);
      desired["desired_state"]["color"][2] = constrain(b, 0, 255);
    }
  }
  String payload, resp;
  serializeJson(desired, payload);
  int code = 0;
  String url = String("https://api.wink.com/") + path;
  if (!httpsJson("PUT", url, access, payload, resp, code) || code < 200 || code >= 300) {
    message = String("wink control http ") + code + " " + hsSnippet(resp);
    return false;
  }
  message = act.length() ? String("wink ") + act : (on ? "wink on" : "wink off");
  return true;
}

bool winkFillInfo(JsonObject out) {
  out["ip"] = "192.168.6.9";
  out["upnp"] = "urn:wink-com:device:hub:2";
  out["localControl"] = "401 no token — :8888 needs Wink cloud local_control";
  out["note"] =
      "Hub is online on LAN. Cloud subscriptions are mostly retired. If you still have Wink login, "
      "add it in Ops → CONN. Otherwise inventory only.";
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setTimeout(2500);
  if (http.begin(cli, "https://192.168.6.9:8888/")) {
    int code = http.GET();
    String body = code > 0 ? http.getString() : "";
    http.end();
    out["upnpHttp"] = code;
    int fn = body.indexOf("<friendlyName>");
    int fn2 = body.indexOf("</friendlyName>");
    if (fn >= 0 && fn2 > fn) out["friendlyName"] = body.substring(fn + 14, fn2);
    int mn = body.indexOf("<modelName>");
    int mn2 = body.indexOf("</modelName>");
    if (mn >= 0 && mn2 > mn) out["modelName"] = body.substring(mn + 11, mn2);
  }
  WiFiClient plain;
  HTTPClient http2;
  http2.setTimeout(1200);
  if (http2.begin(plain, "http://192.168.6.9/")) {
    int code = http2.GET();
    out["port80"] = code;
    out["port80Body"] = hsSnippet(http2.getString());
    http2.end();
  }
  JsonDocument secret;
  out["hasCloudToken"] = connectorStoreGetSecret("wink", secret);
  return true;
}

bool ratgdoControl(const char *ip, const char *action, String &message) {
  if (!ip || !ip[0] || !action || !action[0]) {
    message = "ratgdo ip + open/close required";
    return false;
  }
  if (strcmp(action, "open") && strcmp(action, "close")) {
    message = "garage action must be open or close";
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
  http.setConnectTimeout(1500);
  auto tryUrl = [&](const String &url, bool post) -> int {
    if (!http.begin(client, url)) return -1;
    int code = post ? http.POST("") : http.GET();
    http.end();
    return code;
  };
  String base = String("http://") + ip;
  const String pathsPost[] = {base + "/door/" + action, base + "/button/" + action + "/press",
                              base + "/button/" + action + "_door/press"};
  for (const String &u : pathsPost) {
    int code = tryUrl(u, true);
    if (code > 0 && code < 400) {
      message = String("ratgdo ") + action;
      return true;
    }
  }
  for (const String &u : pathsPost) {
    int code = tryUrl(u, false);
    if (code > 0 && code < 400) {
      message = String("ratgdo ") + action;
      return true;
    }
  }
  message = "ratgdo no door endpoint — check IP / ESPHome entity names";
  return false;
}

bool haCallService(const char *connectorId, const char *domain, const char *service,
                   const char *entityId, String &message) {
  if (!connectorId || !connectorId[0] || !domain || !service || !entityId || !entityId[0]) {
    message = "ha service args missing";
    return false;
  }
  String cpath = String("/homes/") + gridStoreActiveId() + "/connectors/index.json";
  String baseUrl;
  if (SD_MMC.exists(cpath)) {
    File f = SD_MMC.open(cpath, "r");
    JsonDocument cdoc;
    if (f && !deserializeJson(cdoc, f)) {
      for (JsonObject c : cdoc["connectors"].as<JsonArray>()) {
        if (!strcmp(c["id"] | "", connectorId)) {
          baseUrl = c["baseUrl"] | "";
          break;
        }
      }
    }
    if (f) f.close();
  }
  JsonDocument secret;
  if (!connectorStoreGetSecret(connectorId, secret)) {
    message = "ha token missing";
    return false;
  }
  String token = secret["token"] | "";
  if (!baseUrl.length() || !token.length()) {
    message = "ha connector/token missing";
    return false;
  }
  if (baseUrl.endsWith("/")) baseUrl.remove(baseUrl.length() - 1);
  String url = baseUrl + "/api/services/" + domain + "/" + service;
  JsonDocument body;
  body["entity_id"] = entityId;
  String payload;
  serializeJson(body, payload);
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(client, url)) {
    message = "ha connect fail";
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + token);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  http.end();
  bool ok = code > 0 && code < 300;
  message = ok ? String("ha ") + service : String("ha http ") + code;
  return ok;
}
