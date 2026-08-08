#include "lan_discovery.h"
#include "config.h"
#include <cstring>

void LanDiscovery::begin() {
    stop();
    if (WiFi.status() != WL_CONNECTED) return;

    if (!_mdns.beginMulticast(IPAddress(224, 0, 0, 251), 5353)) {
        Serial.println("[MDNS] multicast bind failed");
        return;
    }
    if (!_ssdp.beginMulticast(IPAddress(239, 255, 255, 250), 1900)) {
        Serial.println("[SSDP] multicast bind failed");
        _mdns.stop();
        return;
    }

    _active = true;
    _lastBrowseMs = 0;
    Serial.println("[LAN] mDNS + SSDP listeners on");
}

void LanDiscovery::stop() {
    if (_active) {
        _mdns.stop();
        _ssdp.stop();
        _active = false;
    }
}

bool LanDiscovery::ipOnSubnet(uint32_t ip) const {
    IPAddress lip = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    uint32_t net = ((uint32_t)lip[0] << 24) | ((uint32_t)lip[1] << 16) |
                   ((uint32_t)lip[2] << 8) | lip[3];
    uint32_t m = ((uint32_t)mask[0] << 24) | ((uint32_t)mask[1] << 16) |
                 ((uint32_t)mask[2] << 8) | mask[3];
    return (ip & m) == (net & m) && ip != net;
}

void LanDiscovery::pushHit(uint32_t ip, const char* name, const char* source, const char* hint) {
    if (!ipOnSubnet(ip)) return;

    uint8_t next = (uint8_t)((_qTail + 1) % QUEUE_SIZE);
    if (next == _qHead) return;

    LanDiscoveryHit& h = _queue[_qTail];
    h.ip = ip;
    strncpy(h.name, name ? name : "", sizeof(h.name) - 1);
    h.name[sizeof(h.name) - 1] = '\0';
    strncpy(h.source, source ? source : "?", sizeof(h.source) - 1);
    h.source[sizeof(h.source) - 1] = '\0';
    strncpy(h.hint, hint ? hint : "", sizeof(h.hint) - 1);
    h.hint[sizeof(h.hint) - 1] = '\0';

    _qTail = next;
    if (_hitCount < 0xFFFF) _hitCount++;
}

bool LanDiscovery::popHit(LanDiscoveryHit& out) {
    if (_qHead == _qTail) return false;
    out = _queue[_qHead];
    _qHead = (uint8_t)((_qHead + 1) % QUEUE_SIZE);
    return true;
}

size_t LanDiscovery::dnsSkipName(const uint8_t* p, size_t len, size_t off) {
    while (off < len) {
        uint8_t l = p[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2;
        off += 1 + l;
    }
    return off;
}

size_t LanDiscovery::dnsReadName(const uint8_t* p, size_t len, size_t off, char* out, size_t outLen) {
    if (!out || outLen < 2) return dnsSkipName(p, len, off);
    size_t pos = 0;
    out[0] = '\0';
    size_t start = off;
    bool jumped = false;
    size_t cur = off;

    for (int guard = 0; guard < 64 && cur < len; guard++) {
        uint8_t l = p[cur];
        if (l == 0) {
            if (!jumped) cur++;
            break;
        }
        if ((l & 0xC0) == 0xC0) {
            if (cur + 1 >= len) break;
            size_t ptr = ((size_t)(l & 0x3F) << 8) | p[cur + 1];
            if (!jumped) { start = cur + 2; jumped = true; }
            cur = ptr;
            continue;
        }
        cur++;
        if (cur + l > len) break;
        if (pos > 0 && pos < outLen - 1) out[pos++] = '.';
        for (uint8_t i = 0; i < l && pos < outLen - 1; i++)
            out[pos++] = (char)p[cur + i];
        cur += l;
        if (jumped) break;
    }
    out[pos] = '\0';
    return jumped ? start : cur;
}

void LanDiscovery::parseDnsRecords(const uint8_t* p, size_t len, size_t off, int count, bool isAnswer) {
    (void)isAnswer;
    for (int i = 0; i < count && off + 12 <= len; i++) {
        char name[64];
        off = dnsReadName(p, len, off, name, sizeof(name));
        if (off + 10 > len) break;

        uint16_t type = (uint16_t)((p[off] << 8) | p[off + 1]);
        off += 8;
        uint16_t rdlen = (uint16_t)((p[off] << 8) | p[off + 1]);
        off += 2;
        if (off + rdlen > len) break;

        if (type == 1 && rdlen == 4) {
            uint32_t ip = ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16) |
                          ((uint32_t)p[off + 2] << 8) | p[off + 3];
            const char* label = name[0] ? name : "mdns-host";
            pushHit(ip, label, "mdns", "A");
        }

        off += rdlen;
    }
}

void LanDiscovery::parseMdnsPacket(const uint8_t* data, size_t len) {
    if (len < 12) return;

    int qd = (data[4] << 8) | data[5];
    int an = (data[6] << 8) | data[7];
    int ns = (data[8] << 8) | data[9];
    int ar = (data[10] << 8) | data[11];

    size_t off = 12;
    for (int i = 0; i < qd && off < len; i++) {
        off = dnsSkipName(data, len, off);
        if (off + 4 > len) return;
        off += 4;
    }

    parseDnsRecords(data, len, off, an, true);

    for (int i = 0; i < an && off < len; i++) {
        off = dnsSkipName(data, len, off);
        if (off + 10 > len) break;
        off += 8;
        if (off + 2 > len) break;
        uint16_t rdlen = (uint16_t)((data[off] << 8) | data[off + 1]);
        off += 2 + rdlen;
    }

    parseDnsRecords(data, len, off, ns, true);

    for (int i = 0; i < ns && off < len; i++) {
        off = dnsSkipName(data, len, off);
        if (off + 10 > len) break;
        off += 8;
        if (off + 2 > len) break;
        uint16_t rdlen = (uint16_t)((data[off] << 8) | data[off + 1]);
        off += 2 + rdlen;
    }

    if (ar > 0 && off < len)
        parseDnsRecords(data, len, off, ar, true);
}

void LanDiscovery::parseSsdpPacket(const uint8_t* data, size_t len) {
    if (len < 16) return;

    char buf[512];
    size_t copy = min(len, sizeof(buf) - 1);
    memcpy(buf, data, copy);
    buf[copy] = '\0';

    const char* loc = strstr(buf, "LOCATION:");
    if (!loc) loc = strstr(buf, "Location:");
    if (loc) {
        loc += 9;
        while (*loc == ' ') loc++;
        unsigned int a, b, c, d, port = 0;
        if (sscanf(loc, "http://%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) >= 4) {
            uint32_t ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
            const char* st = strstr(buf, "ST:");
            char hint[32] = "UPnP";
            if (st) {
                st += 3;
                while (*st == ' ') st++;
                size_t n = 0;
                while (st[n] && st[n] != '\r' && st[n] != '\n' && n < sizeof(hint) - 1) {
                    hint[n] = st[n];
                    n++;
                }
                hint[n] = '\0';
            }
            pushHit(ip, hint, "ssdp", "location");
        }
    }

    const char* usn = strstr(buf, "USN:");
    if (usn) {
        usn += 4;
        while (*usn == ' ') usn++;
        if (strncmp(usn, "uuid:", 5) == 0) {
            char uuid[40];
            size_t n = 0;
            while (usn[n] && usn[n] != '\r' && usn[n] != '\n' && n < sizeof(uuid) - 1) {
                uuid[n] = usn[n];
                n++;
            }
            uuid[n] = '\0';
            (void)uuid;
        }
    }
}

void LanDiscovery::sendBrowseIfDue() {
    if (!_active || WiFi.status() != WL_CONNECTED) return;
    unsigned long now = millis();
    if (now - _lastBrowseMs < (unsigned long)LAN_MDNS_BROWSE_MS) return;
    _lastBrowseMs = now;

    static const uint8_t query[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x09, '_', 's', 'e', 'r', 'v', 'i', 'c', 'e', 's',
        0x07, '_', 'd', 'n', 's', '-', 's', 'd',
        0x04, '_', 'u', 'd', 'p',
        0x05, 'l', 'o', 'c', 'a', 'l',
        0x00,
        0x00, 0x0c, 0x00, 0x01
    };

    if (_mdns.beginPacket(IPAddress(224, 0, 0, 251), 5353)) {
        _mdns.write(query, sizeof(query));
        _mdns.endPacket();
    }
}

void LanDiscovery::poll(unsigned maxPackets) {
    if (!_active) return;

    sendBrowseIfDue();

    uint8_t pkt[1536];
    unsigned budget = maxPackets;

    while (budget > 0) {
        int n = _mdns.parsePacket();
        if (n <= 0) break;
        int r = _mdns.read(pkt, min(n, (int)sizeof(pkt)));
        if (r > 0) parseMdnsPacket(pkt, (size_t)r);
        budget--;
        yield();
    }

    while (budget > 0) {
        int n = _ssdp.parsePacket();
        if (n <= 0) break;
        int r = _ssdp.read(pkt, min(n, (int)sizeof(pkt)));
        if (r > 0) parseSsdpPacket(pkt, (size_t)r);
        budget--;
        yield();
    }
}
