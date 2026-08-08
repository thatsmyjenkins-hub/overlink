#include "profile.h"
#include "intel.h"
#include "oui_lookup.h"
#include <LittleFS.h>
#include <WiFiClient.h>

extern "C" {
#include "lwip/etharp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
}

bool ProfileEngine::ensureLoaded() {
#if !ENABLE_CVE_PROFILE
    return false;
#else
    if (_dbLoaded) return true;
    return loadVulnDb();
#endif
}

bool ProfileEngine::strContains(const char* hay, const char* needle) const {
    if (!hay || !needle || !needle[0]) return false;
    String h(hay);
    h.toLowerCase();
    String n(needle);
    n.toLowerCase();
    return h.indexOf(n) >= 0;
}

bool ProfileEngine::hasPort(const IntelHost& h, uint16_t port) const {
    for (int i = 0; i < h.portCount; i++)
        if (h.ports[i] == port) return true;
    return false;
}

bool ProfileEngine::resolveMac(uint32_t ip, uint8_t mac[6]) {
    struct netif* netif = netif_list;
    if (!netif) return false;

    ip4_addr_t addr;
    IPAddress a(ip);
    addr.addr = static_cast<uint32_t>(a);

    etharp_query(netif, &addr, nullptr);
    delay(80);
    yield();

    struct eth_addr* eth_ret = nullptr;
    const ip4_addr_t* ip_ret = nullptr;
    if (etharp_find_addr(netif, &addr, &eth_ret, &ip_ret) >= 0 && eth_ret) {
        memcpy(mac, eth_ret->addr, 6);
        return mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5];
    }
    return false;
}

bool ProfileEngine::grabHttpBanner(uint32_t ip, char* banner, size_t bannerLen) {
    if (!banner || bannerLen < 8) return false;
    WiFiClient client;
    client.setTimeout(200);
    IPAddress addr(ip);
    if (!client.connect(addr, 80, 150)) {
        client.stop();
        return false;
    }
    client.print("GET / HTTP/1.0\r\nHost: ");
    client.print(addr);
    client.print("\r\nConnection: close\r\n\r\n");

    char buf[384];
    size_t n = 0;
    unsigned long t0 = millis();
    while (client.connected() && n < sizeof(buf) - 1 && millis() - t0 < 400) {
        if (client.available()) {
            int c = client.read();
            if (c < 0) break;
            buf[n++] = (char)c;
        }
        yield();
    }
    client.stop();
    buf[n] = '\0';

    const char* server = strstr(buf, "Server:");
    if (server) {
        server += 7;
        while (*server == ' ') server++;
        size_t i = 0;
        while (server[i] && server[i] != '\r' && server[i] != '\n' && i < bannerLen - 1) {
            banner[i] = server[i];
            i++;
        }
        banner[i] = '\0';
        if (i > 0) return true;
    }

    const char* title = strstr(buf, "<title>");
    if (title) {
        title += 7;
        size_t i = 0;
        while (title[i] && title[i] != '<' && i < bannerLen - 1) {
            banner[i] = title[i];
            i++;
        }
        banner[i] = '\0';
        return i > 0;
    }
    strncpy(banner, "HTTP", bannerLen - 1);
    return true;
}

void ProfileEngine::inferDeviceType(IntelHost& h) {
    if (strContains(h.vendor, "Hikvision") || strContains(h.banner, "hikvision")) {
        strncpy(h.deviceType, "Camera", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Sonos")) {
        strncpy(h.deviceType, "Speaker", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Espressif") || strContains(h.vendor, "Tuya")) {
        strncpy(h.deviceType, "IoT", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Raspberry Pi")) {
        strncpy(h.deviceType, "Embedded", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Apple") && hasPort(h, 62078)) {
        strncpy(h.deviceType, "Phone", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Apple")) {
        strncpy(h.deviceType, "Apple Device", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Samsung") || strContains(h.vendor, "Google")) {
        strncpy(h.deviceType, "Mobile", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Cisco") || strContains(h.vendor, "Ubiquiti") ||
        strContains(h.vendor, "TP-Link") || strContains(h.vendor, "NETGEAR") ||
        strContains(h.vendor, "Netgear") || strContains(h.vendor, "ASUSTek") ||
        strContains(h.vendor, "Aruba")) {
        if (hasPort(h, 22) && !hasPort(h, 80))
            strncpy(h.deviceType, "Network Gear", sizeof(h.deviceType) - 1);
        else
            strncpy(h.deviceType, "Router", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Hewlett Packard") || strContains(h.vendor, "Dell")) {
        strncpy(h.deviceType, "PC", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Qnap")) {
        strncpy(h.deviceType, "NAS", sizeof(h.deviceType) - 1);
        return;
    }
    if (strContains(h.vendor, "Amazon") || strContains(h.vendor, "Roku") ||
        strContains(h.vendor, "Wyze") || strContains(h.vendor, "Nest")) {
        strncpy(h.deviceType, "IoT", sizeof(h.deviceType) - 1);
        return;
    }
    if (hasPort(h, 3389) && hasPort(h, 445)) {
        strncpy(h.deviceType, "PC", sizeof(h.deviceType) - 1);
        return;
    }
    if (hasPort(h, 23)) {
        strncpy(h.deviceType, "IoT", sizeof(h.deviceType) - 1);
        return;
    }
    if (hasPort(h, 80) || hasPort(h, 443)) {
        strncpy(h.deviceType, "Web Device", sizeof(h.deviceType) - 1);
        return;
    }
    strncpy(h.deviceType, "Unknown", sizeof(h.deviceType) - 1);
}

void ProfileEngine::inferOsGuess(IntelHost& h) {
    if (strContains(h.banner, "microsoft-iis") || strContains(h.banner, "win32")) {
        strncpy(h.osGuess, "Windows", sizeof(h.osGuess) - 1);
        return;
    }
    if (hasPort(h, 445) || hasPort(h, 3389)) {
        strncpy(h.osGuess, "Windows", sizeof(h.osGuess) - 1);
        return;
    }
    if (strContains(h.banner, "ubuntu") || strContains(h.banner, "debian")) {
        strncpy(h.osGuess, "Linux", sizeof(h.osGuess) - 1);
        return;
    }
    if (strContains(h.banner, "nginx") || strContains(h.banner, "apache") ||
        hasPort(h, 22)) {
        strncpy(h.osGuess, "Linux/Unix", sizeof(h.osGuess) - 1);
        return;
    }
    if (strContains(h.vendor, "Apple")) {
        strncpy(h.osGuess, "Apple OS", sizeof(h.osGuess) - 1);
        return;
    }
    if (strContains(h.vendor, "Espressif")) {
        strncpy(h.osGuess, "FreeRTOS", sizeof(h.osGuess) - 1);
        return;
    }
    strncpy(h.osGuess, "Unknown", sizeof(h.osGuess) - 1);
}

void ProfileEngine::enrichHost(IntelHost& h) {
    uint8_t mac[6] = {0};
    if (resolveMac(h.ip, mac)) {
        h.hasMac = true;
        memcpy(h.mac, mac, 6);
        snprintf(h.macStr, sizeof(h.macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        strncpy(h.vendor, ouiLookupVendor(mac), sizeof(h.vendor) - 1);
    } else if (h.ip == (uint32_t)WiFi.gatewayIP()) {
        strncpy(h.vendor, "Gateway", sizeof(h.vendor) - 1);
        strncpy(h.deviceType, "Router", sizeof(h.deviceType) - 1);
    }

    if (hasPort(h, 80) && h.banner[0] == '\0')
        grabHttpBanner(h.ip, h.banner, sizeof(h.banner));

    if (h.deviceType[0] == '\0')
        inferDeviceType(h);
    inferOsGuess(h);

    uint8_t conf = 25;
    if (h.hasMac && strcmp(h.vendor, "Unknown") != 0) conf += 30;
    if (h.banner[0]) conf += 20;
    if (h.portCount > 0) conf += 15;
    if (strcmp(h.deviceType, "Unknown") != 0) conf += 10;
    h.profileConf = min(conf, (uint8_t)100);
    h.profiled = true;
}

bool ProfileEngine::loadVulnDb() {
    _ruleCount = 0;
    File f = LittleFS.open("/vuln_db.json", "r");
    if (!f) {
        Serial.println("[PROFILE] vuln_db.json missing");
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, f)) {
        f.close();
        Serial.println("[PROFILE] vuln_db parse error");
        return false;
    }
    f.close();

    JsonArray rules = doc["rules"].as<JsonArray>();
    if (rules.isNull()) return false;

    for (JsonObject r : rules) {
        if (_ruleCount >= PROFILE_MAX_RULES) break;
        VulnRule& vr = _rules[_ruleCount++];
        strncpy(vr.id, r["id"] | "?", sizeof(vr.id) - 1);
        strncpy(vr.severity, r["severity"] | "INFO", sizeof(vr.severity) - 1);
        vr.cvss = r["cvss"] | 0.0f;
        strncpy(vr.summary, r["summary"] | "", sizeof(vr.summary) - 1);
        vr.vendors[0] = '\0';
        if (!r["vendors"].isNull()) {
            String v;
            if (r["vendors"].is<const char*>())
                v = r["vendors"].as<const char*>();
            else if (r["vendors"].is<JsonArray>())
                for (JsonVariant x : r["vendors"].as<JsonArray>()) {
                    if (v.length()) v += "|";
                    v += x.as<const char*>();
                }
            strncpy(vr.vendors, v.c_str(), sizeof(vr.vendors) - 1);
        }
        strncpy(vr.types, "", sizeof(vr.types) - 1);
        if (!r["types"].isNull()) {
            String t;
            for (JsonVariant x : r["types"].as<JsonArray>())
                t += String(x.as<const char*>()) + "|";
            strncpy(vr.types, t.c_str(), sizeof(vr.types) - 1);
        }
        strncpy(vr.banners, "", sizeof(vr.banners) - 1);
        if (!r["banners"].isNull()) {
            String b;
            for (JsonVariant x : r["banners"].as<JsonArray>())
                b += String(x.as<const char*>()) + "|";
            strncpy(vr.banners, b.c_str(), sizeof(vr.banners) - 1);
        }
        vr.portCount = 0;
        if (!r["ports"].isNull()) {
            for (JsonVariant p : r["ports"].as<JsonArray>()) {
                if (vr.portCount < 4)
                    vr.ports[vr.portCount++] = (uint16_t)p.as<int>();
            }
        }
    }

    _dbLoaded = true;
    Serial.printf("[PROFILE] %d vuln rules loaded\n", _ruleCount);
    return true;
}

bool ProfileEngine::hostMatchesRule(const IntelHost& h, const VulnRule& r) const {
    bool portMatch = (r.portCount == 0);
    for (int i = 0; i < r.portCount; i++) {
        if (hasPort(h, r.ports[i])) { portMatch = true; break; }
    }
    if (r.portCount > 0 && !portMatch) return false;

    if (r.vendors[0]) {
        bool vm = false;
        String vl(r.vendors);
        int start = 0;
        while (start < (int)vl.length()) {
            int sep = vl.indexOf('|', start);
            String token = sep < 0 ? vl.substring(start) : vl.substring(start, sep);
            token.trim();
            if (token.length() && strContains(h.vendor, token.c_str())) vm = true;
            if (sep < 0) break;
            start = sep + 1;
        }
        if (!vm) return false;
    }

    if (r.types[0]) {
        bool tm = false;
        String tl(r.types);
        int start = 0;
        while (start < (int)tl.length()) {
            int sep = tl.indexOf('|', start);
            String token = sep < 0 ? tl.substring(start) : tl.substring(start, sep);
            token.trim();
            if (token.length() && strContains(h.deviceType, token.c_str())) tm = true;
            if (sep < 0) break;
            start = sep + 1;
        }
        if (!tm) return false;
    }

    if (r.banners[0]) {
        bool bm = false;
        String bl(r.banners);
        int start = 0;
        while (start < (int)bl.length()) {
            int sep = bl.indexOf('|', start);
            String token = sep < 0 ? bl.substring(start) : bl.substring(start, sep);
            token.trim();
            if (token.length() && strContains(h.banner, token.c_str())) bm = true;
            if (sep < 0) break;
            start = sep + 1;
        }
        if (!bm) return false;
    }

    return true;
}

void ProfileEngine::matchVulns(const IntelHost& h, JsonDocument& doc) const {
#if !ENABLE_CVE_PROFILE
    doc["cves"] = JsonArray();
    doc["count"] = 0;
    doc["dbLoaded"] = false;
    return;
#else
    if (!_dbLoaded) {
        const_cast<ProfileEngine*>(this)->ensureLoaded();
    }
    JsonArray arr = doc["cves"].to<JsonArray>();
    int count = 0;
    for (int i = 0; i < _ruleCount; i++) {
        if (!hostMatchesRule(h, _rules[i])) continue;
        JsonObject o = arr.add<JsonObject>();
        o["id"] = _rules[i].id;
        o["severity"] = _rules[i].severity;
        o["cvss"] = _rules[i].cvss;
        o["summary"] = _rules[i].summary;
        o["source"] = "curated-db";
        count++;
    }
    doc["count"] = count;
    doc["dbLoaded"] = _dbLoaded;
#endif
}

void ProfileEngine::appendHostProfile(const IntelHost& h, JsonObject& o) const {
    o["mac"] = h.hasMac ? h.macStr : "";
    o["vendor"] = h.vendor;
    o["deviceType"] = h.deviceType;
    o["osGuess"] = h.osGuess;
    o["banner"] = h.banner;
    o["profileConf"] = h.profileConf;
    o["profiled"] = h.profiled;
    o["cveCount"] = h.cveCount;
}
