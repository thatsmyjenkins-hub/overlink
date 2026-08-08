#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

struct IntelHost;

// Device profiling + curated CVE matching (MITRE/NVD public data, on-device rules)
class ProfileEngine {
public:
    bool ensureLoaded();
    bool isLoaded() const { return _dbLoaded; }
    int ruleCount() const { return _ruleCount; }
    bool resolveMac(uint32_t ip, uint8_t mac[6]);
    void enrichHost(IntelHost& h);
    void matchVulns(const IntelHost& h, JsonDocument& doc) const;
    void appendHostProfile(const IntelHost& h, JsonObject& o) const;

private:
    bool _dbLoaded = false;
    struct VulnRule {
        char id[20];
        char severity[12];
        float cvss;
        char summary[96];
        char vendors[64];
        char types[48];
        char banners[48];
        uint16_t ports[4];
        uint8_t portCount;
    };
    VulnRule _rules[PROFILE_MAX_RULES];
    int _ruleCount = 0;

    bool loadVulnDb();
    bool hostMatchesRule(const IntelHost& h, const VulnRule& r) const;
    void inferDeviceType(IntelHost& h);
    void inferOsGuess(IntelHost& h);
    bool grabHttpBanner(uint32_t ip, char* banner, size_t bannerLen);
    bool strContains(const char* hay, const char* needle) const;
    bool hasPort(const IntelHost& h, uint16_t port) const;
};
