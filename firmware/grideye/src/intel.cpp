#include "intel.h"
#include "wifi_forensics.h"
#include "oui_lookup.h"
#include <WiFiClient.h>
#include <LittleFS.h>
#include <algorithm>

extern "C" {
#include "lwip/etharp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
}

const uint16_t IntelScanner::PROBE_PORTS[INTEL_PORT_COUNT] = {
    22, 23, 80, 443, 445, 3389, 8080, 8443
};

void IntelScanner::begin(bool resetDiscovery) {
    loadEventLog();

    if (resetDiscovery) {
        _hostCount = 0;
        _wifiCount = 0;
        _profileQCount = 0;
        _phase = "INIT";
        _progress = 0;
        _lastWifiScan = millis() - 31000;
        _lanSweepStart = millis();
    } else {
        _phase = "MONITOR";
        _lanSweepStart = millis();
        _lanSweepStart = millis();
    }

    logEvent(resetDiscovery ? "CYBERDECK intel engine online"
                            : "WiFi reconnected — log restored");
}

void IntelScanner::loadEventLog() {
    for (int i = 0; i < EVENT_RING_SIZE; i++)
        _events[i] = "";
    _eventHead = 0;

    if (!LittleFS.exists(EVENT_LOG_PATH))
        return;

    File f = LittleFS.open(EVENT_LOG_PATH, "r");
    if (!f) return;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        _events[_eventHead % EVENT_RING_SIZE] = line;
        _eventHead++;
    }
    f.close();
}

void IntelScanner::trimEventLog() {
    if (!LittleFS.exists(EVENT_LOG_PATH)) return;

    File in = LittleFS.open(EVENT_LOG_PATH, "r");
    if (!in) return;
    size_t sz = in.size();
    if (sz <= EVENT_LOG_MAX_BYTES) {
        in.close();
        return;
    }

    size_t keep = (EVENT_LOG_MAX_BYTES * 3) / 4;
    size_t start = sz > keep ? sz - keep : 0;
    if (!in.seek(start)) {
        in.close();
        return;
    }
    if (start > 0)
        in.readStringUntil('\n');

    File out = LittleFS.open("/intel_events.tmp", "w");
    if (!out) {
        in.close();
        return;
    }
    while (in.available())
        out.write(in.read());
    in.close();
    out.close();
    LittleFS.remove(EVENT_LOG_PATH);
    LittleFS.rename("/intel_events.tmp", EVENT_LOG_PATH);
}

void IntelScanner::persistEventLine(const char* line) {
    if (!line || !line[0]) return;
    File f = LittleFS.open(EVENT_LOG_PATH, "a");
    if (!f) {
        Serial.println("[INTEL] event log write failed");
        return;
    }
    f.println(line);
    f.close();
    trimEventLog();
}

void IntelScanner::logEvent(const char* msg) {
    char line[120];
    snprintf(line, sizeof(line), "%lus %s", millis() / 1000, msg);
    _events[_eventHead % EVENT_RING_SIZE] = String(line);
    _eventHead++;
    persistEventLine(line);
    Serial.printf("[INTEL] %s\n", msg);
}

int IntelScanner::discoveryRank(const char* source) {
    if (!source || !source[0]) return 0;
    if (strcmp(source, "tcp") == 0) return 5;
    if (strcmp(source, "arp") == 0) return 4;
    if (strcmp(source, "mdns") == 0) return 3;
    if (strcmp(source, "ssdp") == 0) return 2;
    if (strcmp(source, "priority") == 0) return 1;
    return 0;
}

void IntelScanner::setHostDiscoveryMeta(IntelHost& h, const char* source, const char* name) {
    if (!source) return;
    if (discoveryRank(source) >= discoveryRank(h.discoveredBy)) {
        strncpy(h.discoveredBy, source, sizeof(h.discoveredBy) - 1);
        h.discoveredBy[sizeof(h.discoveredBy) - 1] = '\0';
    }
    if (name && name[0]) {
        strncpy(h.friendlyName, name, sizeof(h.friendlyName) - 1);
        h.friendlyName[sizeof(h.friendlyName) - 1] = '\0';
    }
}

void IntelScanner::ensureMulticastListeners() {
    if (_multicastActive) return;
    _lanDiscovery.begin();
    _multicastActive = _lanDiscovery.isActive();
    if (_multicastActive) logEvent("mDNS/SSDP listeners on");
}

void IntelScanner::stopMulticastListeners() {
    if (!_multicastActive) return;
    _lanDiscovery.stop();
    _multicastActive = false;
    _arpGapReady = false;
}

void IntelScanner::ingestDiscoveryHit(const LanDiscoveryHit& hit) {
    if (hit.ip == 0) return;

    bool created = false;
    int idx = findHost(hit.ip);
    if (idx < 0) {
        markHostAlive(hit.ip, nullptr, 0);
        idx = findHost(hit.ip);
        created = (idx >= 0);
        if (idx < 0) return;
    }

    setHostDiscoveryMeta(_hosts[idx], hit.source, hit.name);
    if (hit.hint[0] && _hosts[idx].deviceType[0] == '\0') {
        strncpy(_hosts[idx].deviceType, hit.hint, sizeof(_hosts[idx].deviceType) - 1);
        _hosts[idx].deviceType[sizeof(_hosts[idx].deviceType) - 1] = '\0';
    }

    if (created) {
        char buf[96];
        snprintf(buf, sizeof(buf), "HOST %s via %s",
                 u32ToAddress(hit.ip).toString().c_str(), hit.source);
        logEvent(buf);
    }
}

void IntelScanner::runArpGapStep() {
    if (_sweepActive || _orchestrationPaused) return;

    unsigned long now = millis();
    if (now - _lastArpGapMs < (unsigned long)LAN_ARP_GAP_MS) return;
    _lastArpGapMs = now;

    if (!_arpGapReady) {
        IPAddress lip = WiFi.localIP();
        IPAddress mask = WiFi.subnetMask();
        uint8_t mb[4] = {mask[0], mask[1], mask[2], mask[3]};
        for (int i = 0; i < 4; i++) {
            _arpGapBase[i] = lip[i] & mb[i];
            _arpGapBcast[i] = _arpGapBase[i] | (uint8_t)(~mb[i]);
            _arpGapAddr[i] = _arpGapBase[i];
        }
        incrementIpBytes(_arpGapAddr, mb);
        _arpGapReady = true;
    }

    IPAddress mask = WiFi.subnetMask();
    uint8_t maskBytes[4] = {mask[0], mask[1], mask[2], mask[3]};

    for (int attempt = 0; attempt < 4; attempt++) {
        if (compareIpBytes(_arpGapAddr, _arpGapBcast) >= 0) {
            memcpy(_arpGapAddr, _arpGapBase, 4);
            incrementIpBytes(_arpGapAddr, maskBytes);
            return;
        }

        uint8_t targetBytes[4];
        memcpy(targetBytes, _arpGapAddr, 4);
        incrementIpBytes(_arpGapAddr, maskBytes);

        if (compareIpBytes(targetBytes, _arpGapBase) == 0) continue;
        if (isSelfIp(targetBytes)) continue;

        uint32_t ip = ipBytesToU32(targetBytes);
        if (findHost(ip) >= 0) continue;

        uint8_t mac[6];
        if (arpResolve(ip, mac)) {
            markHostAlive(ip, mac, 0);
            int idx = findHost(ip);
            if (idx >= 0)
                setHostDiscoveryMeta(_hosts[idx], "arp", "");
            char buf[48];
            snprintf(buf, sizeof(buf), "HOST %s (arp)", u32ToAddress(ip).toString().c_str());
            logEvent(buf);
        }
        return;
    }
}

void IntelScanner::requestQuietMs(unsigned ms) {
    unsigned long until = millis() + ms;
    if (until > _quietUntil)
        _quietUntil = until;
}

bool IntelScanner::isCoopQuiet() const {
    return millis() < _quietUntil || _wifiScanAsync;
}

void IntelScanner::loop() {
    static bool wasConnected = false;
    if (WiFi.status() != WL_CONNECTED) {
        wasConnected = false;
        _wifiScanAsync = false;
        if (_phase != nullptr && strcmp(_phase, "NO_LINK") != 0)
            stopMulticastListeners();
        _phase = "NO_LINK";
        _progress = 0;
        return;
    }
    if (!wasConnected) {
        wasConnected = true;
        _wifiUpSince = millis();
    }

    ensureMulticastListeners();
    _lanDiscovery.poll(LAN_MULTICAST_POLL_MAX);
    LanDiscoveryHit hit;
    while (_lanDiscovery.popHit(hit))
        ingestDiscoveryHit(hit);

    if (!_sweepActive && !_orchestrationPaused && !isCoopQuiet())
        runArpGapStep();

    unsigned long now = millis();

    if (_wifiScanAsync) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            _phase = "RF_SCAN";
            _rfInProgress = true;
        } else {
            finishWifiScanResults(n);
            _wifiScanAsync = false;
            _lastWifiScan = now;
        }
    }

    if (!_orchestrationPaused) {
        if (!_wifiScanAsync && !_sweepActive && !isCoopQuiet() &&
            now - _wifiUpSince > LAN_WIFI_SCAN_DEFER_MS &&
            now - _lastWifiScan > 30000) {
            runWifiScan();
        }

        unsigned long sweepInterval = (_hostCount > 0)
            ? (unsigned long)LAN_SWEEP_INTERVAL_FAST_MS
            : (unsigned long)LAN_SWEEP_INTERVAL_MS;
        if (!_sweepActive && now - _lanSweepStart > sweepInterval) {
            _sweepActive = true;
            beginLanSweep();
            _phase = "LAN_SWEEP";
            logEvent("LAN sweep started");
            _lanSweepStart = now;
        }
    }

    if (_sweepActive && !isCoopQuiet()) {
        runLanStep();
    } else if (_profileQCount > 0 && !isCoopQuiet()) {
        runProfileStep();
    } else {
        _phase = "MONITOR";
    }
    recomputeProgress();
}

void IntelScanner::loopPortable() {
    if (_orchestrationPaused)
        return;

    unsigned long now = millis();
    if (now - _lastWifiScan > 30000) {
        runWifiScan();
        _lastWifiScan = now;
    } else if (!_rfInProgress) {
        _phase = "RF_SCAN";
        _progress = 100;
    }
}

int IntelScanner::countProfiledHosts() const {
    int n = 0;
    for (int i = 0; i < _hostCount; i++)
        if (_hosts[i].profiled) n++;
    return n;
}

const char* IntelScanner::progressKind() const {
    if (strcmp(_phase, "LAN_SWEEP") == 0) return "lan";
    if (strcmp(_phase, "RF_SCAN") == 0) return "rf";
    if (strcmp(_phase, "PROFILE") == 0) return "profile";
    if (strcmp(_phase, "NO_LINK") == 0) return "offline";
    return "idle";
}

void IntelScanner::buildProgressLabel(char* buf, size_t len) const {
    if (!buf || len < 4) return;
    if (strcmp(_phase, "LAN_SWEEP") == 0) {
        const char* stage = "TCP";
        if (_lanPhase == LAN_PHASE_PRIORITY) stage = "Priority";
        snprintf(buf, len, "%s %u/%u",
                 stage, (unsigned)_lanSlotsChecked, (unsigned)_lanSlotsTotal);
    } else if (strcmp(_phase, "RF_SCAN") == 0) {
        strncpy(buf, "RF scan (Wi-Fi)", len - 1);
        buf[len - 1] = '\0';
    } else if (strcmp(_phase, "PROFILE") == 0) {
        int prof = countProfiledHosts();
        snprintf(buf, len, "Profiling %d/%d hosts · %d queued",
                 prof, _hostCount, _profileQCount);
    } else if (strcmp(_phase, "MONITOR") == 0) {
        unsigned long lanAge = (millis() - _lanSweepStart) / 1000;
        if (lanAge < 12)
            snprintf(buf, len, "Next LAN sweep in %lus", (unsigned long)(12 - lanAge));
        else
            strncpy(buf, "Between sweeps", len - 1);
        buf[len - 1] = '\0';
    } else if (strcmp(_phase, "NO_LINK") == 0) {
        strncpy(buf, "No WiFi link", len - 1);
        buf[len - 1] = '\0';
    } else {
        snprintf(buf, len, "%s", _phase);
    }
}

void IntelScanner::recomputeProgress() {
    if (WiFi.status() != WL_CONNECTED) {
        _progress = 0;
        return;
    }
    if (strcmp(_phase, "LAN_SWEEP") == 0) {
        if (_lanSlotsTotal > 0)
            _progress = (uint8_t)min(99, (_lanSlotsChecked * 100) / _lanSlotsTotal);
        return;
    }
    if (strcmp(_phase, "RF_SCAN") == 0) {
        _progress = _rfInProgress ? 40 : 100;
        return;
    }
    if (strcmp(_phase, "PROFILE") == 0) {
        if (_hostCount > 0)
            _progress = (uint8_t)((countProfiledHosts() * 100) / _hostCount);
        else
            _progress = 0;
        return;
    }
    if (strcmp(_phase, "MONITOR") == 0) {
        _progress = 0;
        return;
    }
    _progress = 0;
}

uint32_t IntelScanner::ipBytesToU32(const uint8_t b[4]) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}

void IntelScanner::u32ToIpBytes(uint32_t ip, uint8_t b[4]) {
    b[0] = (ip >> 24) & 0xFF;
    b[1] = (ip >> 16) & 0xFF;
    b[2] = (ip >> 8) & 0xFF;
    b[3] = ip & 0xFF;
}

IPAddress IntelScanner::u32ToAddress(uint32_t ip) {
    uint8_t b[4];
    u32ToIpBytes(ip, b);
    return IPAddress(b[0], b[1], b[2], b[3]);
}

int IntelScanner::compareIpBytes(const uint8_t a[4], const uint8_t b[4]) {
    for (int i = 0; i < 4; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

bool IntelScanner::incrementIpBytes(uint8_t addr[4], const uint8_t mask[4]) {
    for (int i = 3; i >= 0; i--) {
        if (mask[i] == 255) continue;
        addr[i]++;
        return true;
    }
    return false;
}

bool IntelScanner::isSelfIp(const uint8_t addr[4]) const {
    IPAddress self = WiFi.localIP();
    return addr[0] == self[0] && addr[1] == self[1] &&
           addr[2] == self[2] && addr[3] == self[3];
}

void IntelScanner::beginLanSweep() {
    IPAddress lip = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    uint8_t maskBytes[4] = {mask[0], mask[1], mask[2], mask[3]};

    for (int i = 0; i < 4; i++) {
        _scanBase[i] = lip[i] & maskBytes[i];
        _scanBcast[i] = _scanBase[i] | (uint8_t)(~maskBytes[i]);
        _scanAddr[i] = _scanBase[i];
    }

    _lanSlotsTotal = 0;
    uint8_t cursor[4];
    memcpy(cursor, _scanBase, 4);
    while (compareIpBytes(cursor, _scanBcast) < 0) {
        _lanSlotsTotal++;
        if (!incrementIpBytes(cursor, maskBytes)) break;
        if (_lanSlotsTotal >= 1022) break;
    }
    if (_lanSlotsTotal < 1) _lanSlotsTotal = 1;

    _lanSlotsChecked = 0;
    incrementIpBytes(_scanAddr, maskBytes);

    _lanPhase = LAN_PHASE_PRIORITY;
    _priorityIdx = 0;
    _tcpPortRound = 0;
    _lanPortIdx = 0;
    buildPriorityTargets();

    uint16_t subnetSlots = _lanSlotsTotal;
    _lanSlotsTotal = (uint16_t)(_priorityCount + subnetSlots);

    char buf[80];
    snprintf(buf, sizeof(buf), "LAN sweep: %u priority + %u TCP",
             (unsigned)_priorityCount, (unsigned)subnetSlots);
    logEvent(buf);
}

void IntelScanner::kickFastDiscovery() {
    if (WiFi.status() != WL_CONNECTED || _sweepActive) return;
    _arpGapReady = false;
    // Short delay after Wi‑Fi connect so the stack is stable before scanning
    _lanSweepStart = millis() - (LAN_SWEEP_INTERVAL_MS - 4000);
}

void IntelScanner::buildPriorityTargets() {
    _priorityCount = 0;
    auto addBytes = [&](const uint8_t b[4]) {
        if (_priorityCount >= LAN_PRIORITY_MAX) return;
        if (isSelfIp(b)) return;
        uint32_t ip = ipBytesToU32(b);
        for (int i = 0; i < _priorityCount; i++)
            if (_priorityTargets[i] == ip) return;
        _priorityTargets[_priorityCount++] = ip;
    };

    IPAddress gw = WiFi.gatewayIP();
    IPAddress dns = WiFi.dnsIP();
    IPAddress lip = WiFi.localIP();
    uint8_t gb[4] = {gw[0], gw[1], gw[2], gw[3]};
    uint8_t db[4] = {dns[0], dns[1], dns[2], dns[3]};
    uint8_t lb[4] = {lip[0], lip[1], lip[2], lip[3]};

    if (gw[0] | gw[1] | gw[2] | gw[3]) addBytes(gb);
    if ((dns[0] | dns[1] | dns[2] | dns[3]) &&
        (dns[0] != gb[0] || dns[1] != gb[1] || dns[2] != gb[2] || dns[3] != gb[3]))
        addBytes(db);

    uint8_t common[] = {1, 2, 10, 50, 100, 101, 150, 200, 254};
    for (uint8_t last : common) {
        uint8_t t[4] = {lb[0], lb[1], lb[2], last};
        addBytes(t);
    }

    uint8_t mask[4];
    IPAddress m = WiFi.subnetMask();
    mask[0] = m[0]; mask[1] = m[1]; mask[2] = m[2]; mask[3] = m[3];
    uint8_t base[4] = {
        (uint8_t)(lb[0] & mask[0]), (uint8_t)(lb[1] & mask[1]),
        (uint8_t)(lb[2] & mask[2]), (uint8_t)(lb[3] & mask[3])
    };
    uint8_t bcast[4];
    for (int i = 0; i < 4; i++) bcast[i] = base[i] | (uint8_t)(~mask[i]);
    if (bcast[3] > 0) {
        uint8_t t[4] = {bcast[0], bcast[1], bcast[2], (uint8_t)(bcast[3] - 1)};
        addBytes(t);
    }
}

void IntelScanner::runWifiScan() {
    WiFi.scanDelete();
    yield();
    int n = WiFi.scanNetworks(true, true);
    if (n == WIFI_SCAN_RUNNING) {
        _wifiScanAsync = true;
        _phase = "RF_SCAN";
        _rfInProgress = true;
        recomputeProgress();
        logEvent("RF spectrum scan");
        return;
    }
    if (n < 0) {
        _rfInProgress = false;
        _phase = "MONITOR";
        logEvent("RF scan failed");
        WiFi.scanDelete();
        recomputeProgress();
        return;
    }
    finishWifiScanResults(n);
    _lastWifiScan = millis();
}

void IntelScanner::finishWifiScanResults(int n) {
    if (n < 0) {
        _rfInProgress = false;
        _phase = "MONITOR";
        logEvent("RF scan failed");
        WiFi.scanDelete();
        recomputeProgress();
        return;
    }

    _wifiCount = 0;
    for (int i = 0; i < n && _wifiCount < INTEL_MAX_WIFI; i++) {
        if (WiFi.SSID(i).length() == 0 && WiFi.RSSI(i) == 0) continue;

        bool dupe = false;
        const char* bssidStr = WiFi.BSSIDstr(i).c_str();
        for (int j = 0; j < _wifiCount; j++) {
            if (strcmp(_wifi[j].bssid, bssidStr) == 0) { dupe = true; break; }
        }
        if (dupe) continue;

        IntelWifi& w = _wifi[_wifiCount++];
        strncpy(w.ssid, WiFi.SSID(i).c_str(), sizeof(w.ssid) - 1);
        w.hidden = (WiFi.SSID(i).length() == 0);
        if (w.hidden) strncpy(w.ssid, "<HIDDEN>", sizeof(w.ssid) - 1);

        snprintf(w.bssid, sizeof(w.bssid), "%s", WiFi.BSSIDstr(i).c_str());
        w.rssi = WiFi.RSSI(i);
        w.channel = WiFi.channel(i);

        wifi_auth_mode_t auth = WiFi.encryptionType(i);
        if (auth == WIFI_AUTH_OPEN) w.encType = 0;
        else if (auth == WIFI_AUTH_WEP) w.encType = 1;
        else if (auth == WIFI_AUTH_WPA_PSK) w.encType = 2;
        else if (auth == WIFI_AUTH_WPA2_PSK) w.encType = 3;
        else if (auth == WIFI_AUTH_WPA3_PSK) w.encType = 4;
        else w.encType = 5;

        uint8_t bmac[6] = {0};
        const uint8_t* b = WiFi.BSSID(i);
        if (b) {
            memcpy(bmac, b, 6);
            strncpy(w.vendor, ouiLookupVendor(bmac), sizeof(w.vendor) - 1);
        } else {
            strncpy(w.vendor, "Unknown", sizeof(w.vendor) - 1);
        }
    }

    WiFi.scanDelete();
    _rfInProgress = false;
    _phase = "MONITOR";
    char buf[48];
    snprintf(buf, sizeof(buf), "RF: %d networks mapped", _wifiCount);
    logEvent(buf);
    recomputeProgress();
}

bool IntelScanner::probePort(uint32_t ip, uint16_t port, uint16_t& latencyMs, uint16_t timeoutMs) {
    WiFiClient client;
    client.setTimeout(timeoutMs);
    IPAddress addr = u32ToAddress(ip);
    unsigned long t0 = millis();
    if (!client.connect(addr, port, timeoutMs)) {
        client.stop();
        return false;
    }
    latencyMs = (uint16_t)(millis() - t0);
    client.stop();
    return true;
}

bool IntelScanner::arpResolve(uint32_t ip, uint8_t mac[6]) {
    static unsigned long lastArpMs = 0;
    unsigned long now = millis();
    if (now - lastArpMs < LAN_ARP_MIN_INTERVAL_MS)
        return false;
    lastArpMs = now;

    struct netif* netif = netif_list;
    if (!netif) return false;

    ip4_addr_t addr;
    IPAddress a = u32ToAddress(ip);
    addr.addr = static_cast<uint32_t>(a);

    etharp_query(netif, &addr, nullptr);

    for (int attempt = 0; attempt < 4; attempt++) {
        delay(15);
        yield();
        struct eth_addr* eth_ret = nullptr;
        const ip4_addr_t* ip_ret = nullptr;
        if (etharp_find_addr(netif, &addr, &eth_ret, &ip_ret) >= 0 && eth_ret) {
            memcpy(mac, eth_ret->addr, 6);
            if (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])
                return true;
        }
    }
    return false;
}

void IntelScanner::markHostAlive(uint32_t ip, const uint8_t* mac, uint16_t latency) {
    int idx = findHost(ip);
    if (idx < 0) {
        if (_hostCount >= INTEL_MAX_HOSTS) return;
        idx = _hostCount++;
        memset(&_hosts[idx], 0, sizeof(IntelHost));
        _hosts[idx].ip = ip;
        strncpy(_hosts[idx].discoveredBy, "unknown", sizeof(_hosts[idx].discoveredBy) - 1);
    }
    IntelHost& h = _hosts[idx];
    h.alive = true;
    h.lastSeen = millis();
    if (latency > 0 && (h.latencyMs == 0 || latency < h.latencyMs))
        h.latencyMs = latency;
    if (mac) {
        memcpy(h.mac, mac, 6);
        h.hasMac = true;
        snprintf(h.macStr, sizeof(h.macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        strncpy(h.vendor, ouiLookupVendor(mac), sizeof(h.vendor) - 1);
    }
    if (_autoProfileOnDiscovery && !h.profileQueued) {
        enqueueProfile(ip);
        h.profileQueued = true;
    }
}

bool IntelScanner::probeHostQuick(uint32_t ip, uint16_t& latencyMs, uint16_t& openPort) {
    static const uint16_t quick[] = {80, 443, 22, 445};
    for (uint16_t port : quick) {
        if (probePort(ip, port, latencyMs, LAN_TCP_CONNECT_MS)) {
            openPort = port;
            return true;
        }
    }
    return false;
}

bool IntelScanner::shouldSkipTcpTarget(uint32_t ip) const {
    int idx = findHost(ip);
    if (idx < 0) return false;
    const IntelHost& h = _hosts[idx];
    return h.hasMac && h.portCount >= 2;
}

int IntelScanner::findHost(uint32_t ip) const {
    for (int i = 0; i < _hostCount; i++) {
        if (_hosts[i].ip == ip) return i;
    }
    return -1;
}

void IntelScanner::upsertHost(uint32_t ip, uint16_t latency, uint16_t port) {
    int idx = findHost(ip);
    if (idx < 0) {
        if (_hostCount >= INTEL_MAX_HOSTS) return;
        idx = _hostCount++;
        memset(&_hosts[idx], 0, sizeof(IntelHost));
        _hosts[idx].ip = ip;
    }
    IntelHost& h = _hosts[idx];
    h.alive = true;
    h.lastSeen = millis();
    strncpy(h.discoveredBy, "tcp", sizeof(h.discoveredBy) - 1);
    h.discoveredBy[sizeof(h.discoveredBy) - 1] = '\0';
    if (latency > 0 && (h.latencyMs == 0 || latency < h.latencyMs))
        h.latencyMs = latency;

    for (int i = 0; i < h.portCount; i++) {
        if (h.ports[i] == port) return;
    }
    if (h.portCount < INTEL_MAX_PORTS)
        h.ports[h.portCount++] = port;

    if (_autoProfileOnDiscovery && !h.profileQueued) {
        enqueueProfile(ip);
        h.profileQueued = true;
    }
}

void IntelScanner::enqueueProfile(uint32_t ip) {
    if (_profileQCount >= INTEL_MAX_HOSTS) return;
    if (findHost(ip) < 0) return;
    for (int i = 0; i < _profileQCount; i++)
        if (_profileQueue[i] == ip) return;
    _profileQueue[_profileQCount++] = ip;
}

void IntelScanner::updateCveCount(IntelHost& h) {
    JsonDocument doc;
    _profile.matchVulns(h, doc);
    h.cveCount = doc["count"] | 0;
}

void IntelScanner::runProfileStep() {
    if (_profileQCount <= 0) return;
    uint32_t ip = _profileQueue[0];
    for (int i = 1; i < _profileQCount; i++)
        _profileQueue[i - 1] = _profileQueue[i];
    _profileQCount--;

    int idx = findHost(ip);
    if (idx < 0) return;

    _phase = "PROFILE";
    IntelHost& h = _hosts[idx];
    _profile.enrichHost(h);
    updateCveCount(h);
    uint8_t base = calcHostRisk(h);
    uint8_t bump = (uint8_t)min((int)40, (int)h.cveCount * 8);
    h.risk = (uint8_t)min((int)100, (int)base + (int)bump);

    char buf[64];
    snprintf(buf, sizeof(buf), "PROFILE %s %s", u32ToAddress(ip).toString().c_str(), h.deviceType);
    logEvent(buf);
}

void IntelScanner::runPriorityStep() {
    for (int n = 0; n < LAN_PRIORITY_PER_TICK && _priorityIdx < _priorityCount; n++) {
        uint32_t target = _priorityTargets[_priorityIdx++];
        _lanSlotsChecked++;
        _currentTarget = target;

        uint16_t lat = 0, port = 0;
        if (probeHostQuick(target, lat, port)) {
            upsertHost(target, lat, port);
            char buf[40];
            snprintf(buf, sizeof(buf), "HOST %s (priority)", u32ToAddress(target).toString().c_str());
            logEvent(buf);
        }
        yield();
    }
    if (_priorityIdx >= _priorityCount) {
        _lanPhase = LAN_PHASE_TCP;
        memcpy(_scanAddr, _scanBase, 4);
        IPAddress mask = WiFi.subnetMask();
        uint8_t maskBytes[4] = {mask[0], mask[1], mask[2], mask[3]};
        incrementIpBytes(_scanAddr, maskBytes);
        _lanPortIdx = 0;
        logEvent("Priority pass done — TCP sweep");
    }
}

void IntelScanner::runTcpStep() {
    IPAddress mask = WiFi.subnetMask();
    uint8_t maskBytes[4] = {mask[0], mask[1], mask[2], mask[3]};
    static const uint16_t quick[] = {80, 443, 22, 445};

    for (int n = 0; n < LAN_TCP_PROBES_PER_TICK; n++) {
        if (compareIpBytes(_scanAddr, _scanBcast) >= 0)
            break;

        uint8_t targetBytes[4];
        memcpy(targetBytes, _scanAddr, 4);
        incrementIpBytes(_scanAddr, maskBytes);
        _lanSlotsChecked++;

        if (compareIpBytes(targetBytes, _scanBase) == 0) continue;
        if (isSelfIp(targetBytes)) continue;

        uint32_t target = ipBytesToU32(targetBytes);
        if (shouldSkipTcpTarget(target)) continue;

        uint16_t port = quick[_lanPortIdx % 4];
        _lanPortIdx++;
        _currentTarget = target;
        _currentPort = port;

        uint16_t lat = 0;
        uint16_t tmo = (findHost(target) >= 0) ? LAN_TCP_CONNECT_KNOWN_MS : LAN_TCP_CONNECT_MS;
        if (probePort(target, port, lat, tmo)) {
            upsertHost(target, lat, port);
            char buf[32];
            snprintf(buf, sizeof(buf), "HOST %s", u32ToAddress(target).toString().c_str());
            logEvent(buf);
        }
        yield();
    }

    if (compareIpBytes(_scanAddr, _scanBcast) >= 0) {
        finalizeHostRisks();
        _sweepActive = false;
        _sweepCount++;
        _currentTarget = 0;
        _currentPort = 0;
        _phase = "MONITOR";
        char buf[40];
        snprintf(buf, sizeof(buf), "Sweep done: %d hosts", _hostCount);
        logEvent(buf);
    }
}

void IntelScanner::runLanStep() {
    switch (_lanPhase) {
        case LAN_PHASE_PRIORITY: runPriorityStep(); break;
        case LAN_PHASE_TCP:      runTcpStep();      break;
        default:                 runTcpStep();      break;
    }
}

void IntelScanner::finalizeHostRisks() {
    for (int i = 0; i < _hostCount; i++)
        _hosts[i].risk = calcHostRisk(_hosts[i]);
}

uint8_t IntelScanner::calcHostRisk(const IntelHost& h) const {
    uint8_t risk = 10;
    bool has443 = false;
    for (int i = 0; i < h.portCount; i++) {
        uint16_t p = h.ports[i];
        if (p == 443 || p == 8443) has443 = true;
        if (p == 23) risk += 35;
        if (p == 445) risk += 25;
        if (p == 3389) risk += 20;
        if (p == 80) risk += 10;
        if (p == 22) risk += 5;
    }
    if (!has443 && h.portCount > 0) risk += 10;
    if (h.portCount >= 5) risk += 15;
    return min(risk, (uint8_t)100);
}

uint8_t IntelScanner::calcWifiRisk(const IntelWifi& w) const {
    if (w.encType == 0) return 90;
    if (w.encType == 1) return 70;
    if (w.hidden) return 40;
    return 15;
}

const char* IntelScanner::encLabel(uint8_t t) const {
    switch (t) {
        case 0: return "OPEN";
        case 1: return "WEP";
        case 2: return "WPA";
        case 3: return "WPA2";
        case 4: return "WPA3";
        default: return "???";
    }
}

const char* IntelScanner::portService(uint16_t port) const {
    switch (port) {
        case 22: return "SSH";
        case 23: return "TELNET";
        case 80: return "HTTP";
        case 443: return "HTTPS";
        case 445: return "SMB";
        case 3389: return "RDP";
        case 8080: return "HTTP-ALT";
        case 8443: return "HTTPS-ALT";
        default: return "SVC";
    }
}

bool IntelScanner::getHostBrief(int idx, char* ipOut, size_t ipLen, char* vendorOut, size_t vLen,
                              char* typeOut, size_t tLen, uint8_t* risk, uint8_t* ports) const {
    if (idx < 0 || idx >= _hostCount || !ipOut) return false;
    const IntelHost& h = _hosts[idx];
    strncpy(ipOut, u32ToAddress(h.ip).toString().c_str(), ipLen - 1);
    ipOut[ipLen - 1] = '\0';
    if (vendorOut) {
        strncpy(vendorOut, h.vendor[0] ? h.vendor : "—", vLen - 1);
        vendorOut[vLen - 1] = '\0';
    }
    if (typeOut) {
        strncpy(typeOut, h.deviceType[0] ? h.deviceType : "?", tLen - 1);
        typeOut[tLen - 1] = '\0';
    }
    if (risk) *risk = h.risk;
    if (ports) *ports = h.portCount;
    return true;
}

bool IntelScanner::getWifiBrief(int idx, char* ssidOut, size_t ssidLen, int8_t* rssi, uint8_t* enc) const {
    if (idx < 0 || idx >= _wifiCount || !ssidOut) return false;
    strncpy(ssidOut, _wifi[idx].ssid, ssidLen - 1);
    ssidOut[ssidLen - 1] = '\0';
    if (rssi) *rssi = _wifi[idx].rssi;
    if (enc) *enc = _wifi[idx].encType;
    return true;
}

void IntelScanner::formatActivityLine(char* buf, size_t len) const {
    if (!buf || len < 8) return;
    if (strcmp(_phase, "LAN_SWEEP") == 0 && _currentTarget != 0) {
        snprintf(buf, len, "LAN %u%% probe %s:%u",
                 (unsigned)_progress, u32ToAddress(_currentTarget).toString().c_str(),
                 (unsigned)_currentPort);
    } else if (strcmp(_phase, "LAN_SWEEP") == 0) {
        snprintf(buf, len, "LAN %u/%u slots · %d hosts",
                 (unsigned)_lanSlotsChecked, (unsigned)_lanSlotsTotal, _hostCount);
    } else if (strcmp(_phase, "RF_SCAN") == 0) {
        snprintf(buf, len, "RF scan — mapping nearby APs");
    } else if (strcmp(_phase, "PROFILE") == 0) {
        snprintf(buf, len, "Profiling %d/%d · %d queued",
                 countProfiledHosts(), _hostCount, _profileQCount);
    } else if (strcmp(_phase, "MONITOR") == 0) {
        char lbl[48];
        buildProgressLabel(lbl, sizeof(lbl));
        snprintf(buf, len, "%s · %d hosts %d APs", lbl, _hostCount, _wifiCount);
    } else if (strcmp(_phase, "NO_LINK") == 0) {
        snprintf(buf, len, "No WiFi link");
    } else {
        snprintf(buf, len, "%s", _phase);
    }
}

void IntelScanner::toJsonSummary(JsonDocument& doc) const {
    doc["phase"] = _phase;
    doc["progress"] = _progress;
    doc["hostCount"] = _hostCount;
    doc["wifiCount"] = _wifiCount;
    doc["uptime"] = millis() / 1000;
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["gateway"] = WiFi.gatewayIP().toString();
    doc["dns"] = WiFi.dnsIP().toString();
    doc["mask"] = WiFi.subnetMask().toString();
    doc["mac"] = WiFi.macAddress();

    uint8_t maxRisk = 0;
    for (int i = 0; i < _hostCount; i++)
        if (_hosts[i].risk > maxRisk) maxRisk = _hosts[i].risk;
    doc["maxRisk"] = maxRisk;

    int openAps = 0;
    for (int i = 0; i < _wifiCount; i++)
        if (_wifi[i].encType == 0) openAps++;
    doc["openWifi"] = openAps;

    int openPorts = 0;
    int criticalHosts = 0;
    for (int i = 0; i < _hostCount; i++) {
        openPorts += _hosts[i].portCount;
        if (_hosts[i].risk >= 70) criticalHosts++;
    }
    doc["openPorts"] = openPorts;
    doc["criticalHosts"] = criticalHosts;
    doc["sweepActive"] = _sweepActive;
    doc["sweepCount"] = _sweepCount;
    doc["lanIndex"] = _lanSlotsChecked;
    doc["lanTotal"] = _lanSlotsTotal;
    doc["lanChecked"] = _lanSlotsChecked;
    doc["portProbeIdx"] = (_lanPortIdx % 4) + 1;
    doc["portProbeTotal"] = 4;

    char progLbl[64];
    buildProgressLabel(progLbl, sizeof(progLbl));
    doc["progressLabel"] = progLbl;
    doc["progressKind"] = progressKind();

    unsigned long now = millis();
    unsigned long rfAge = (now - _lastWifiScan) / 1000;
    doc["rfAgeSec"] = rfAge;
    doc["nextRfSec"] = (_sweepActive || rfAge >= 30) ? 0 : (unsigned)(30 - rfAge);

    unsigned long lanAge = (now - _lanSweepStart) / 1000;
    doc["nextLanSec"] = _sweepActive ? 0 :
        (lanAge >= 12 ? 0 : (unsigned)(12 - lanAge));

    uint8_t idleCountdownPct = 0;
    if (!_sweepActive && strcmp(_phase, "MONITOR") == 0 && lanAge < 12)
        idleCountdownPct = (uint8_t)((lanAge * 100) / 12);
    doc["idleCountdownPct"] = idleCountdownPct;

    uint8_t hitRatePct = 0;
    if (_lanSlotsChecked > 0)
        hitRatePct = (uint8_t)min(100, (_hostCount * 100) / (int)_lanSlotsChecked);
    doc["hitRatePct"] = hitRatePct;
    doc["discoveryPct"] = hitRatePct;

    if (_sweepActive && _currentTarget != 0) {
        doc["scanTarget"] = u32ToAddress(_currentTarget).toString();
        doc["scanPort"] = _currentPort;
    } else {
        doc["scanTarget"] = "";
        doc["scanPort"] = 0;
    }

    const char* phaseLabel = "Standby";
    if (strcmp(_phase, "RF_SCAN") == 0) phaseLabel = "Wireless spectrum scan";
    else if (strcmp(_phase, "LAN_SWEEP") == 0) phaseLabel = "LAN host sweep";
    else if (strcmp(_phase, "MONITOR") == 0) phaseLabel = "Monitoring (between sweeps)";
    else if (strcmp(_phase, "NO_LINK") == 0) phaseLabel = "No network link";
    else if (strcmp(_phase, "INIT") == 0) phaseLabel = "Initializing";
    else if (strcmp(_phase, "PROFILE") == 0) phaseLabel = "Device profiling";
    doc["phaseLabel"] = phaseLabel;
    doc["orchestrationPaused"] = _orchestrationPaused;
    doc["autoProfile"] = _autoProfileOnDiscovery;
    doc["multicastListen"] = _multicastActive;
    doc["passiveHits"] = _lanDiscovery.hitCount();
    doc["rfInProgress"] = _rfInProgress;

    int profiled = 0, totalCves = 0;
    for (int i = 0; i < _hostCount; i++) {
        if (_hosts[i].profiled) profiled++;
        totalCves += _hosts[i].cveCount;
    }
    doc["profiledHosts"] = profiled;
    doc["totalCves"] = totalCves;
    doc["profileQueue"] = _profileQCount;
}

void IntelScanner::toJsonWifi(JsonDocument& doc) const {
    JsonArray arr = doc["aps"].to<JsonArray>();
    for (int i = 0; i < _wifiCount; i++) {
        const IntelWifi& w = _wifi[i];
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = w.ssid;
        o["bssid"] = w.bssid;
        o["rssi"] = w.rssi;
        o["channel"] = w.channel;
        o["enc"] = encLabel(w.encType);
        o["encType"] = w.encType;
        o["hidden"] = w.hidden;
        o["risk"] = calcWifiRisk(w);
        o["vendor"] = w.vendor;
    }
    doc["count"] = _wifiCount;
    doc["updated"] = millis() / 1000;
}

void IntelScanner::appendHostJson(const IntelHost& h, JsonObject& o) const {
    o["ip"] = u32ToAddress(h.ip).toString();
    o["latency"] = h.latencyMs;
    o["risk"] = h.risk;
    o["ports"] = h.portCount;
    const char* by = h.discoveredBy[0] ? h.discoveredBy : "unknown";
    if (h.portCount > 0) by = "tcp";
    o["discoveredBy"] = by;
    if (h.friendlyName[0]) o["name"] = h.friendlyName;
    o["lastSeen"] = h.lastSeen / 1000;
    JsonArray pa = o["open"].to<JsonArray>();
    for (int j = 0; j < h.portCount; j++)
        pa.add(h.ports[j]);
    _profile.appendHostProfile(h, o);
}

// Dashboard poll — no per-port arrays or heavy strings.
void IntelScanner::appendHostJsonBrief(const IntelHost& h, JsonObject& o) const {
    o["ip"] = u32ToAddress(h.ip).toString();
    o["risk"] = h.risk;
    o["ports"] = h.portCount;
    o["latency"] = h.latencyMs;
    const char* by = h.discoveredBy[0] ? h.discoveredBy : "unknown";
    if (h.portCount > 0) by = "tcp";
    o["discoveredBy"] = by;
    if (h.friendlyName[0]) o["name"] = h.friendlyName;
    o["vendor"] = h.vendor;
    o["deviceType"] = h.deviceType;
    o["osGuess"] = h.osGuess;
    o["profiled"] = h.profiled;
    o["cveCount"] = h.cveCount;
}

void IntelScanner::toJsonHosts(JsonDocument& doc) const {
    JsonArray arr = doc["hosts"].to<JsonArray>();
    for (int i = 0; i < _hostCount; i++) {
        JsonObject o = arr.add<JsonObject>();
        appendHostJson(_hosts[i], o);
    }
    doc["count"] = _hostCount;
}

void IntelScanner::toJsonHostsBrief(JsonDocument& doc) const {
    JsonArray arr = doc["hosts"].to<JsonArray>();
    for (int i = 0; i < _hostCount; i++) {
        JsonObject o = arr.add<JsonObject>();
        appendHostJsonBrief(_hosts[i], o);
    }
    doc["count"] = _hostCount;
}

void IntelScanner::toJsonWifiBrief(JsonDocument& doc) const {
    JsonArray arr = doc["aps"].to<JsonArray>();
    for (int i = 0; i < _wifiCount; i++) {
        const IntelWifi& w = _wifi[i];
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = w.ssid;
        o["bssid"] = w.bssid;
        o["rssi"] = w.rssi;
        o["channel"] = w.channel;
        o["encType"] = w.encType;
        o["enc"] = encLabel(w.encType);
        o["risk"] = calcWifiRisk(w);
        o["vendor"] = w.vendor;
    }
    doc["count"] = _wifiCount;
}

void IntelScanner::toJsonProfiles(JsonDocument& doc) const {
    JsonArray arr = doc["profiles"].to<JsonArray>();
    int totalCves = 0;
    int profiled = 0;
    for (int i = 0; i < _hostCount; i++) {
        const IntelHost& h = _hosts[i];
        JsonObject o = arr.add<JsonObject>();
        appendHostJson(h, o);
        if (h.profiled) profiled++;
        totalCves += h.cveCount;
    }
    doc["count"] = _hostCount;
    doc["profiled"] = profiled;
    doc["totalCves"] = totalCves;
    doc["vulnRules"] = _profile.ruleCount();
    doc["vulnDbLoaded"] = _profile.isLoaded();
}

// Counts only — used by the frequent dashboard poll so it doesn't serialize the
// full per-host profile a second time (the client falls back to the hosts list).
void IntelScanner::toJsonProfilesMeta(JsonDocument& doc) const {
    int totalCves = 0;
    int profiled = 0;
    for (int i = 0; i < _hostCount; i++) {
        if (_hosts[i].profiled) profiled++;
        totalCves += _hosts[i].cveCount;
    }
    doc["count"] = _hostCount;
    doc["profiled"] = profiled;
    doc["totalCves"] = totalCves;
    doc["vulnRules"] = _profile.ruleCount();
    doc["vulnDbLoaded"] = _profile.isLoaded();
}

bool IntelScanner::triggerRfScan() {
    return requestRfScan();
}

void IntelScanner::toJsonAp(const char* bssid, JsonDocument& doc) const {
    int idx = WifiForensics::findApIndex(_wifi, _wifiCount, bssid);
    if (idx < 0) {
        doc["found"] = false;
        if (bssid) doc["bssid"] = bssid;
        return;
    }
    WifiForensics::buildApDetail(_wifi[idx], _wifi, _wifiCount, doc);
    uint8_t risk = calcWifiRisk(_wifi[idx]);
    doc["risk"] = risk;
    if (doc["ap"].is<JsonObject>())
        doc["ap"]["risk"] = risk;
}

bool IntelScanner::runApAction(const char* action, const char* bssid, JsonDocument& result) {
    int idx = WifiForensics::findApIndex(_wifi, _wifiCount, bssid);
    bool ok = WifiForensics::runApAction(action, *this, _wifi, _wifiCount, idx, result);
    if (ok && bssid) {
        char buf[72];
        snprintf(buf, sizeof(buf), "AP %s: %s", action, bssid);
        logEvent(buf);
    }
    return ok;
}

void IntelScanner::toJsonHost(uint32_t ip, JsonDocument& doc) const {
    int idx = findHost(ip);
    if (idx < 0) {
        doc["found"] = false;
        return;
    }
    const IntelHost& h = _hosts[idx];
    doc["found"] = true;
    doc["ip"] = u32ToAddress(h.ip).toString();
    doc["latency"] = h.latencyMs;
    doc["risk"] = h.risk;
    doc["lastSeen"] = h.lastSeen / 1000;
    JsonObject root = doc.as<JsonObject>();
    _profile.appendHostProfile(h, root);
    JsonArray services = doc["services"].to<JsonArray>();
    for (int i = 0; i < h.portCount; i++) {
        JsonObject s = services.add<JsonObject>();
        uint16_t p = h.ports[i];
        s["port"] = p;
        s["name"] = portService(p);
        s["severity"] = (p == 23 || p == 445) ? "CRITICAL" :
                        (p == 3389 || p == 80) ? "WARN" : "INFO";
    }
    _profile.matchVulns(h, doc);
}

void IntelScanner::toJsonEvents(JsonDocument& doc) const {
    toJsonEvents(doc, EVENT_RING_SIZE);
}

void IntelScanner::toJsonEvents(JsonDocument& doc, int maxEvents) const {
    JsonArray arr = doc["events"].to<JsonArray>();
    int n = min(_eventHead, EVENT_RING_SIZE);
    if (maxEvents > 0 && n > maxEvents) n = maxEvents;
    for (int i = 0; i < n; i++) {
        int idx = (_eventHead - n + i + EVENT_RING_SIZE * 2) % EVENT_RING_SIZE;
        if (_events[idx].length() > 0)
            arr.add(_events[idx]);
    }
    doc["count"] = n;
    doc["persisted"] = LittleFS.exists(EVENT_LOG_PATH);
    doc["logPath"] = EVENT_LOG_PATH;
    if (LittleFS.exists(EVENT_LOG_PATH)) {
        File lf = LittleFS.open(EVENT_LOG_PATH, "r");
        if (lf) {
            doc["logBytes"] = lf.size();
            lf.close();
        }
    }
}

void IntelScanner::setAutoScanPaused(bool paused) {
    if (_orchestrationPaused == paused) return;
    _orchestrationPaused = paused;
    logEvent(paused ? "Auto scans paused (web)" : "Auto scans resumed (web)");
}

void IntelScanner::setAutoProfileOnDiscovery(bool enabled) {
    if (_autoProfileOnDiscovery == enabled) return;
    _autoProfileOnDiscovery = enabled;
    logEvent(enabled ? "Auto-profile on discovery enabled" : "Auto-profile on discovery disabled");
}

void IntelScanner::clearProfileQueue() {
    _profileQCount = 0;
    if (strcmp(_phase, "PROFILE") == 0)
        _phase = "MONITOR";
}

bool IntelScanner::cancelLanSweep() {
    if (!_sweepActive) return false;
    _sweepActive = false;
    _lanPhase = LAN_PHASE_PRIORITY;
    _currentTarget = 0;
    _currentPort = 0;
    _phase = "MONITOR";
    _lanSweepStart = millis();
    logEvent("LAN sweep cancelled (web)");
    return true;
}

bool IntelScanner::requestLanSweep() {
    if (WiFi.status() != WL_CONNECTED) return false;
    if (_sweepActive) return false;
    _sweepActive = true;
    beginLanSweep();
    _phase = "LAN_SWEEP";
    _lanSweepStart = millis();
    logEvent("LAN sweep (orchestrated)");
    return true;
}

bool IntelScanner::requestRfScan() {
    if (WiFi.status() != WL_CONNECTED) return false;
    if (_sweepActive) return false;
    runWifiScan();
    _lastWifiScan = millis();
    return true;
}

bool IntelScanner::requestProfileAll(String& message) {
    if (_hostCount == 0) {
        message = "No hosts to profile";
        return false;
    }
    int queued = 0;
    for (int i = 0; i < _hostCount; i++) {
        int before = _profileQCount;
        enqueueProfile(_hosts[i].ip);
        if (_profileQCount > before) queued++;
        _hosts[i].profileQueued = true;
    }
    message = "Queued " + String(queued) + " host(s) for profiling";
    logEvent("Profile-all queued (web)");
    return queued > 0;
}

bool IntelScanner::requestProfileHost(uint32_t ip, String& message) {
    if (ip == 0) {
        message = "Invalid IP";
        return false;
    }
    int idx = findHost(ip);
    if (idx < 0) {
        message = "Host not in discovery table";
        return false;
    }
    for (int i = 0; i < _profileQCount; i++) {
        if (_profileQueue[i] == ip) {
            message = "Already queued for " + u32ToAddress(ip).toString();
            return true;
        }
    }
    if (_hosts[idx].profiled)
        _hosts[idx].profiled = false;
    enqueueProfile(ip);
    _hosts[idx].profileQueued = true;
    message = "Profiling queued for " + u32ToAddress(ip).toString();
    return true;
}

void IntelScanner::clearDiscovery() {
    _hostCount = 0;
    _wifiCount = 0;
    _profileQCount = 0;
    _arpGapReady = false;
    _sweepActive = false;
    _currentTarget = 0;
    _currentPort = 0;
    _lanSlotsChecked = 0;
    _phase = "MONITOR";
    _lanSweepStart = millis();
    logEvent("Discovery cleared (web)");
}

bool IntelScanner::controlAction(const char* action, const String& arg, String& message) {
    if (!action || !action[0]) {
        message = "Missing action";
        return false;
    }

    if (strcmp(action, "lan_sweep") == 0) {
        yield();
        if (requestLanSweep()) {
            message = "LAN sweep started";
            return true;
        }
        message = _sweepActive ? "LAN sweep already running" : "WiFi not connected";
        return false;
    }
    if (strcmp(action, "rf_scan") == 0) {
        if (requestRfScan()) {
            message = "RF scan complete";
            return true;
        }
        message = _sweepActive ? "Finish LAN sweep first" : "WiFi not connected";
        return false;
    }
    if (strcmp(action, "profile_all") == 0)
        return requestProfileAll(message);
    if (strcmp(action, "profile_host") == 0) {
        IPAddress ipAddr;
        if (!ipAddr.fromString(arg)) {
            message = "Invalid ip parameter";
            return false;
        }
        uint8_t b[4] = {(uint8_t)ipAddr[0], (uint8_t)ipAddr[1], (uint8_t)ipAddr[2], (uint8_t)ipAddr[3]};
        return requestProfileHost(ipBytesToU32(b), message);
    }
    if (strcmp(action, "clear_discovery") == 0) {
        clearDiscovery();
        message = "Discovery tables cleared";
        return true;
    }
    if (strcmp(action, "clear_log") == 0) {
        for (int i = 0; i < EVENT_RING_SIZE; i++)
            _events[i] = "";
        _eventHead = 0;
        if (LittleFS.exists(EVENT_LOG_PATH))
            LittleFS.remove(EVENT_LOG_PATH);
        logEvent("Event log cleared (web)");
        message = "Event log cleared";
        return true;
    }
    if (strcmp(action, "pause") == 0) {
        setAutoScanPaused(true);
        message = "Automatic LAN/RF cycles paused";
        return true;
    }
    if (strcmp(action, "resume") == 0) {
        setAutoScanPaused(false);
        message = "Automatic scans resumed";
        return true;
    }
    if (strcmp(action, "cancel_sweep") == 0) {
        if (cancelLanSweep()) {
            message = "LAN sweep stopped";
            return true;
        }
        message = "No LAN sweep running";
        return false;
    }
    if (strcmp(action, "stop_profile") == 0) {
        clearProfileQueue();
        message = "Profile queue cleared";
        return true;
    }
    if (strcmp(action, "auto_profile_on") == 0) {
        setAutoProfileOnDiscovery(true);
        message = "Auto-profile on new hosts enabled";
        return true;
    }
    if (strcmp(action, "auto_profile_off") == 0) {
        setAutoProfileOnDiscovery(false);
        message = "Auto-profile on new hosts disabled";
        return true;
    }

    message = "Unknown action";
    return false;
}

void IntelScanner::buildTextReport(String& out) const {
    out.reserve(4096);
    out = "CYBERDECK NET INTEL REPORT\n";
    out += "==========================\n\n";
    out += "Network: ";
    out += WiFi.SSID();
    out += "  IP: ";
    out += WiFi.localIP().toString();
    out += "  Mask: ";
    out += WiFi.subnetMask().toString();
    out += "\nPhase: ";
    out += _phase;
    out += "  Hosts: ";
    out += String(_hostCount);
    out += "  APs: ";
    out += String(_wifiCount);
    out += "\n\n--- LAN HOSTS ---\n";
    if (_hostCount == 0) {
        out += "(none discovered — ports 80/443/22/445)\n";
    } else {
        for (int i = 0; i < _hostCount; i++) {
            const IntelHost& h = _hosts[i];
            out += u32ToAddress(h.ip).toString();
            out += "  risk=";
            out += String(h.risk);
            out += "  ports=";
            out += String(h.portCount);
            if (h.vendor[0]) {
                out += "  ";
                out += h.vendor;
            }
            if (h.deviceType[0]) {
                out += "  ";
                out += h.deviceType;
            }
            out += "\n";
        }
    }
    out += "\n--- NEARBY WI-FI ---\n";
    if (_wifiCount == 0) {
        out += "(none in last RF scan)\n";
    } else {
        for (int i = 0; i < _wifiCount; i++) {
            const IntelWifi& w = _wifi[i];
            out += w.ssid;
            out += "  ";
            out += String(w.rssi);
            out += " dBm  ch";
            out += String(w.channel);
            out += "  ";
            out += encLabel(w.encType);
            out += "\n";
        }
    }
    out += "\n--- RECENT LOG ---\n";
    int n = min(_eventHead, 16);
    for (int i = 0; i < n; i++) {
        int idx = (_eventHead - n + i + EVENT_RING_SIZE * 2) % EVENT_RING_SIZE;
        if (_events[idx].length() > 0) {
            out += _events[idx];
            out += "\n";
        }
    }
    out += "\nGenerated on CYD2 cyberdeck.\n";
}
