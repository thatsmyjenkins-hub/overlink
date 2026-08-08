#include "device_registry.h"
#include "power.h"

void DeviceRegistry::normalizeMac(const char *in, char *out, size_t outLen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < outLen; i++) {
        char c = in[i];
        if (c == ':' || c == '-') continue;
        if (c >= 'A' && c <= 'F') c = c - 'A' + 'a';
        out[j++] = c;
    }
    out[j] = '\0';
}

bool DeviceRegistry::macEquals(const char *a, const char *b) {
    char na[18];
    char nb[18];
    normalizeMac(a, na, sizeof(na));
    normalizeMac(b, nb, sizeof(nb));
    return strcmp(na, nb) == 0;
}

void DeviceRegistry::loadWizCache() {
    for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
        char key[8];
        snprintf(key, sizeof(key), "wiz%u", (unsigned)i);
        uint32_t ip = prefs_.getUInt(key, 0);
        if (ip != 0) {
            wizIps_[i] = IPAddress(ip);
        }
    }
}

void DeviceRegistry::saveWizCache(size_t index, const IPAddress &ip) {
    char key[8];
    snprintf(key, sizeof(key), "wiz%u", (unsigned)index);
    prefs_.putUInt(key, static_cast<uint32_t>(ip));
}

bool DeviceRegistry::probeWiz(const IPAddress &ip, size_t matchIndex) {
    if (ip == INADDR_NONE) return false;

    WiFiUDP udp;
    if (!udp.begin(0)) return false;

    const char *payload = "{\"method\":\"getPilot\",\"params\":{}}";
    udp.beginPacket(ip, 38899);
    udp.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
    udp.endPacket();

    unsigned long deadline = millis() + 500;
    while (millis() < deadline) {
        int size = udp.parsePacket();
        if (size <= 0) {
            delay(5);
            yieldUi();
            continue;
        }

        char buf[512];
        int len = udp.read(buf, sizeof(buf) - 1);
        if (len <= 0) continue;
        buf[len] = '\0';

        StaticJsonDocument<384> doc;
        if (deserializeJson(doc, buf)) continue;

        const char *mac = doc["result"]["mac"];
        if (!mac) continue;
        if (!macEquals(mac, WIZ_BULBS[matchIndex].mac)) continue;

        wizIps_[matchIndex] = ip;
        saveWizCache(matchIndex, ip);
        Serial.printf("WiZ '%s' -> %s\n", WIZ_BULBS[matchIndex].name, ip.toString().c_str());
        udp.stop();
        return true;
    }
    udp.stop();
    return false;
}

bool DeviceRegistry::probeWizReliable(const IPAddress &ip, size_t matchIndex) {
    if (probeWiz(ip, matchIndex)) return true;
    yieldUi();
    return probeWiz(ip, matchIndex);
}

void DeviceRegistry::discoverWizBroadcast(WiFiUDP &udp) {
    const char *payload = "{\"method\":\"getPilot\",\"params\":{}}";
    IPAddress broadcast = WiFi.localIP();
    broadcast[3] = 255;

    for (int attempt = 0; attempt < 3; attempt++) {
        udp.beginPacket(broadcast, 38899);
        udp.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
        udp.endPacket();
        udp.beginPacket(IPAddress(255, 255, 255, 255), 38899);
        udp.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
        udp.endPacket();

        unsigned long deadline = millis() + 1500;
        while (millis() < deadline) {
            int size = udp.parsePacket();
            if (size <= 0) {
                delay(5);
                yieldUi();
                continue;
            }

            char buf[512];
            int len = udp.read(buf, sizeof(buf) - 1);
            if (len <= 0) continue;
            buf[len] = '\0';

            StaticJsonDocument<384> doc;
            if (deserializeJson(doc, buf)) continue;

            const char *mac = doc["result"]["mac"];
            if (!mac) continue;

            IPAddress sender = udp.remoteIP();
            for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
                if (wizIps_[i] != INADDR_NONE) continue;
                if (macEquals(mac, WIZ_BULBS[i].mac)) {
                    wizIps_[i] = sender;
                    saveWizCache(i, sender);
                    Serial.printf("WiZ '%s' -> %s (broadcast)\n", WIZ_BULBS[i].name,
                                    sender.toString().c_str());
                }
            }
        }
    }
}

void DeviceRegistry::discoverWizFallback() {
    for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
        if (wizIps_[i] != INADDR_NONE) continue;
        const uint8_t *ip = WIZ_BULBS[i].fallbackIp;
        IPAddress addr(ip[0], ip[1], ip[2], ip[3]);
        probeWizReliable(addr, i);
        yieldUi();
    }
}

void DeviceRegistry::discoverWiz() {
    for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
        if (wizIps_[i] != INADDR_NONE) {
            if (!probeWizReliable(wizIps_[i], i)) {
                const uint8_t *fb = WIZ_BULBS[i].fallbackIp;
                IPAddress addr(fb[0], fb[1], fb[2], fb[3]);
                if (!probeWizReliable(addr, i)) {
                    wizIps_[i] = INADDR_NONE;
                }
            }
            yieldUi();
        }
    }

    if (wizFoundCount() >= WIZ_BULB_COUNT) return;

    discoverWizFallback();
    if (wizFoundCount() >= WIZ_BULB_COUNT) return;

    WiFiUDP udp;
    if (!udp.begin(DISCOVERY_PORT)) {
        Serial.println("WiZ discovery: failed to bind UDP port");
        return;
    }
    discoverWizBroadcast(udp);
    udp.stop();
    yieldUi();
}

void DeviceRegistry::loadWledCache() {
    uint32_t ip = prefs_.getUInt("wled_ip", 0);
    if (ip != 0) {
        wledIp_ = IPAddress(ip);
    }
}

void DeviceRegistry::saveWledCache(const IPAddress &ip) {
    prefs_.putUInt("wled_ip", static_cast<uint32_t>(ip));
}

bool DeviceRegistry::probeWled(const IPAddress &ip) {
    if (ip == INADDR_NONE) return false;

    HTTPClient http;
    WiFiClient client;
    String url = String("http://") + ip.toString() + "/json/info";
    http.setTimeout(2000);
    http.begin(client, url);

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, http.getStream())) {
        http.end();
        return false;
    }
    http.end();

    const char *mac = doc["mac"] | "";
    if (!mac[0] || !macEquals(mac, WLED_MAC)) return false;

    wledIp_ = ip;
    saveWledCache(ip);
    Serial.printf("WLED -> %s\n", ip.toString().c_str());
    return true;
}

bool DeviceRegistry::probeWledReliable(const IPAddress &ip) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (probeWled(ip)) return true;
        yieldUi();
        delay(80);
    }
    return false;
}

void DeviceRegistry::tryCachedWled() {
    if (wledIp_ == INADDR_NONE) return;
    if (probeWledReliable(wledIp_)) return;
    wledIp_ = INADDR_NONE;
    prefs_.remove("wled_ip");
}

void DeviceRegistry::tryFallbackWled() {
    IPAddress ip;
    if (!ip.fromString(WLED_FALLBACK_IP)) return;
    probeWledReliable(ip);
}

void DeviceRegistry::tryMdnsWled() {
    int count = MDNS.queryService("wled", "tcp");
    for (int i = 0; i < count; i++) {
        if (probeWled(MDNS.IP(i))) return;
    }
}

void DeviceRegistry::startWledScan() {
    IPAddress fb;
    if (fb.fromString(WLED_FALLBACK_IP) && probeWledReliable(fb)) {
        return;
    }

    scanSubnetIdx_ = 0;
    scanHost_ = 1;
    if (fb != INADDR_NONE) {
        for (size_t i = 0; i < WLED_SCAN_SUBNET_COUNT; i++) {
            if (WLED_SCAN_SUBNETS[i] == fb[2]) {
                scanSubnetIdx_ = i;
                scanHost_ = static_cast<uint8_t>(fb[3] + 1);
                if (scanHost_ == 0) scanHost_ = 1;
                break;
            }
        }
    }
    scanActive_ = true;
    nextScanMs_ = 0;
}

bool DeviceRegistry::scanWledStep() {
    const uint8_t batch = 16;
    for (uint8_t n = 0; n < batch; n++) {
        if (millis() < nextScanMs_) return false;

        if (scanSubnetIdx_ >= WLED_SCAN_SUBNET_COUNT) {
            return true;
        }

        uint8_t subnet = WLED_SCAN_SUBNETS[scanSubnetIdx_];
        IPAddress ip(192, 168, subnet, scanHost_);

        if (ip != WiFi.localIP()) {
            probeWled(ip);
            if (wledFound()) {
                return true;
            }
        }

        scanHost_++;
        if (scanHost_ == 0) {
            scanSubnetIdx_++;
            scanHost_ = 1;
        }
        nextScanMs_ = millis() + 30;
    }
    return false;
}

void DeviceRegistry::loadCyberDeckCache() {
    uint32_t ip = prefs_.getUInt("deck_ip", 0);
    if (ip != 0) {
        cyberdeckIp_ = IPAddress(ip);
    }
}

void DeviceRegistry::saveCyberDeckCache(const IPAddress &ip) {
    prefs_.putUInt("deck_ip", static_cast<uint32_t>(ip));
}

bool DeviceRegistry::probeCyberDeck(const IPAddress &ip) {
    if (ip == INADDR_NONE) return false;

    HTTPClient http;
    WiFiClient client;
    String url = String("http://") + ip.toString() + "/api/status";
    http.setTimeout(2000);
    http.begin(client, url);

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    // Only need the device identity field — keep this small for DRAM.
    JsonDocument doc;
    if (deserializeJson(doc, http.getStream())) {
        http.end();
        return false;
    }
    http.end();

    const char *device = doc["device"] | "";
    if (strcmp(device, "CyberDeck IR/RF") != 0) return false;

    cyberdeckIp_ = ip;
    saveCyberDeckCache(ip);
    Serial.printf("CyberDeck -> %s\n", ip.toString().c_str());
    return true;
}

bool DeviceRegistry::probeCyberDeckReliable(const IPAddress &ip) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (probeCyberDeck(ip)) return true;
        yieldUi();
        delay(80);
    }
    return false;
}

void DeviceRegistry::tryCachedCyberDeck() {
    if (cyberdeckIp_ == INADDR_NONE) return;
    if (probeCyberDeckReliable(cyberdeckIp_)) return;
    cyberdeckIp_ = INADDR_NONE;
    prefs_.remove("deck_ip");
}

void DeviceRegistry::tryFallbackCyberDeck() {
    IPAddress ip;
    if (!ip.fromString(CYBERDECK_FALLBACK_IP)) return;
    probeCyberDeckReliable(ip);
}

void DeviceRegistry::tryMdnsCyberDeck() {
    int count = MDNS.queryService("cyberdeck", "tcp");
    for (int i = 0; i < count; i++) {
        if (probeCyberDeck(MDNS.IP(i))) return;
    }
    // Hostname lookup (cyberdeck.local)
    IPAddress ip = MDNS.queryHost(CYBERDECK_HOSTNAME, 1500);
    if (ip != INADDR_NONE) probeCyberDeck(ip);
}

void DeviceRegistry::startCyberDeckScan() {
    IPAddress fb;
    if (fb.fromString(CYBERDECK_FALLBACK_IP) && probeCyberDeckReliable(fb)) {
        return;
    }

    deckScanSubnetIdx_ = 0;
    deckScanHost_ = 1;
    if (fb != INADDR_NONE) {
        for (size_t i = 0; i < CYBERDECK_SCAN_SUBNET_COUNT; i++) {
            if (CYBERDECK_SCAN_SUBNETS[i] == fb[2]) {
                deckScanSubnetIdx_ = i;
                deckScanHost_ = static_cast<uint8_t>(fb[3] + 1);
                if (deckScanHost_ == 0) deckScanHost_ = 1;
                break;
            }
        }
    }
    deckScanActive_ = true;
    nextDeckScanMs_ = 0;
}

bool DeviceRegistry::scanCyberDeckStep() {
    const uint8_t batch = 12;
    for (uint8_t n = 0; n < batch; n++) {
        if (millis() < nextDeckScanMs_) return false;

        if (deckScanSubnetIdx_ >= CYBERDECK_SCAN_SUBNET_COUNT) {
            return true;
        }

        uint8_t subnet = CYBERDECK_SCAN_SUBNETS[deckScanSubnetIdx_];
        IPAddress ip(192, 168, subnet, deckScanHost_);

        if (ip != WiFi.localIP()) {
            probeCyberDeck(ip);
            if (cyberdeckFound()) {
                return true;
            }
        }

        deckScanHost_++;
        if (deckScanHost_ == 0) {
            deckScanSubnetIdx_++;
            deckScanHost_ = 1;
        }
        nextDeckScanMs_ = millis() + 30;
    }
    return false;
}

void DeviceRegistry::runDiscovery(bool fullScan) {
    discovering_ = true;
    discoverWiz();
    discovering_ = false;

    tryFallbackWled();
    if (!wledFound()) tryCachedWled();
    if (!wledFound()) tryMdnsWled();
    if (!wledFound() && (fullScan || !BATTERY_SKIP_WLED_SUBNET_SCAN)) {
        startWledScan();
    }

    tryFallbackCyberDeck();
    if (!cyberdeckFound()) tryCachedCyberDeck();
    if (!cyberdeckFound()) tryMdnsCyberDeck();
    if (!cyberdeckFound() && fullScan) {
        startCyberDeckScan();
    }
    lastDiscoveryMs_ = millis();
}

void DeviceRegistry::begin(UiPumpFn pump) {
    pump_ = pump;
    for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
        wizIps_[i] = INADDR_NONE;
    }
    wledIp_ = INADDR_NONE;
    cyberdeckIp_ = INADDR_NONE;

    prefs_.begin("basement", false);
    loadWizCache();
    loadWledCache();
    loadCyberDeckCache();

    if (WiFi.status() == WL_CONNECTED) {
        MDNS.begin("basement-cyd");
#if BATTERY_WAKE_SKIP_DISCOVERY
        // Lights cache is enough to skip a full scan; CyberDeck is probed on the next
        // incomplete-interval pass if missing.
        if (power_woke_from_touch() && wizFoundCount() >= WIZ_BULB_COUNT && wledFound()) {
            lastDiscoveryMs_ = millis();
            Serial.println("Wake: cached device IPs");
        } else {
            runDiscovery();
        }
#else
        runDiscovery();
#endif
    }
}

void DeviceRegistry::requestDiscovery(bool fullScan) {
    if (WiFi.status() != WL_CONNECTED) return;
    scanActive_ = false;
    deckScanActive_ = false;
    if (fullScan) {
        runDiscovery(true);
    } else {
        pendingDiscovery_ = true;
    }
}

void DeviceRegistry::loop() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (pendingDiscovery_) {
        pendingDiscovery_ = false;
        runDiscovery(false);
    }

    if (scanActive_) {
        if (scanWledStep()) {
            scanActive_ = false;
            lastDiscoveryMs_ = millis();
        }
        return;
    }

    if (deckScanActive_) {
        if (scanCyberDeckStep()) {
            deckScanActive_ = false;
            lastDiscoveryMs_ = millis();
        }
        return;
    }

    bool incomplete =
        wizFoundCount() < WIZ_BULB_COUNT || !wledFound() || !cyberdeckFound();
    unsigned long interval = incomplete ? DISCOVERY_INCOMPLETE_INTERVAL_MS : DISCOVERY_INTERVAL_MS;

    if (millis() - lastDiscoveryMs_ >= interval) {
        pendingDiscovery_ = true;
    }
}

IPAddress DeviceRegistry::wizIp(size_t index) const {
    if (index >= WIZ_BULB_COUNT) return INADDR_NONE;
    return wizIps_[index];
}

IPAddress DeviceRegistry::wledIp() const {
    return wledIp_;
}

IPAddress DeviceRegistry::cyberdeckIp() const {
    return cyberdeckIp_;
}

bool DeviceRegistry::wizFound(size_t index) const {
    return index < WIZ_BULB_COUNT && wizIps_[index] != INADDR_NONE;
}

bool DeviceRegistry::wledFound() const {
    return wledIp_ != INADDR_NONE;
}

bool DeviceRegistry::cyberdeckFound() const {
    return cyberdeckIp_ != INADDR_NONE;
}

uint8_t DeviceRegistry::wizFoundCount() const {
    uint8_t n = 0;
    for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
        if (wizIps_[i] != INADDR_NONE) n++;
    }
    return n;
}

bool DeviceRegistry::hasAnyDevice() const {
    return wizFoundCount() > 0 || wledFound() || cyberdeckFound();
}

uint32_t DeviceRegistry::stateHash() const {
    uint32_t h = wizFoundCount();
    h |= (wledFound() ? 0x100u : 0);
    h |= (cyberdeckFound() ? 0x200u : 0);
    h |= (discovering_ ? 0x10000u : 0);
    h |= (scanActive_ ? 0x20000u : 0);
    h |= (deckScanActive_ ? 0x80000u : 0);
    h |= (pendingDiscovery_ ? 0x40000u : 0);
    for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
        h = h * 31u + static_cast<uint32_t>(wizIps_[i]);
    }
    h = h * 31u + static_cast<uint32_t>(wledIp_);
    h = h * 31u + static_cast<uint32_t>(cyberdeckIp_);
    return h;
}
