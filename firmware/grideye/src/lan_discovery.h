#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

struct LanDiscoveryHit {
    uint32_t ip;
    char     name[64];
    char     source[8];
    char     hint[32];
};

class LanDiscovery {
public:
    void begin();
    void stop();
    void poll(unsigned maxPackets = 4);
    bool popHit(LanDiscoveryHit& out);
    void sendBrowseIfDue();
    uint16_t hitCount() const { return _hitCount; }
    bool isActive() const { return _active; }

private:
    WiFiUDP _mdns;
    WiFiUDP _ssdp;
    bool    _active = false;
    unsigned long _lastBrowseMs = 0;

    static const int QUEUE_SIZE = 16;
    LanDiscoveryHit _queue[QUEUE_SIZE];
    uint8_t _qHead = 0;
    uint8_t _qTail = 0;
    uint16_t _hitCount = 0;

    void pushHit(uint32_t ip, const char* name, const char* source, const char* hint = nullptr);
    bool ipOnSubnet(uint32_t ip) const;
    void parseMdnsPacket(const uint8_t* data, size_t len);
    void parseSsdpPacket(const uint8_t* data, size_t len);
    static size_t dnsSkipName(const uint8_t* p, size_t len, size_t off);
    static size_t dnsReadName(const uint8_t* p, size_t len, size_t off, char* out, size_t outLen);
    void parseDnsRecords(const uint8_t* p, size_t len, size_t off, int count, bool isAnswer);
};
