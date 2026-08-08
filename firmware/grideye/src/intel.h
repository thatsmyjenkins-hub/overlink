#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <vector>

#define INTEL_MAX_PORTS     8
#define INTEL_PORT_COUNT    8

struct IntelHost {
    uint32_t ip;
    uint16_t latencyMs;
    uint16_t ports[INTEL_MAX_PORTS];
    uint8_t  portCount;
    uint8_t  risk;
    bool     alive;
    unsigned long lastSeen;

    uint8_t  mac[6];
    bool     hasMac;
    char     macStr[18];
    char     vendor[40];
    char     deviceType[24];
    char     osGuess[24];
    char     banner[64];
    bool     profiled;
    bool     profileQueued;
    uint8_t  profileConf;
    uint8_t  cveCount;
    char     friendlyName[48];
    char     discoveredBy[12];
};

struct IntelWifi {
    char     ssid[33];
    char     bssid[18];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  encType;
    bool     hidden;
    char     vendor[40];
};

#include "config.h"
#include "profile.h"
#include "lan_discovery.h"

class IntelScanner {
public:
    void begin(bool resetDiscovery = true);
    void loop();
    void loopPortable();  // RF spectrum only (no STA / LAN required)

    void toJsonSummary(JsonDocument& doc) const;
    void toJsonWifi(JsonDocument& doc) const;
    void toJsonHosts(JsonDocument& doc) const;
    void toJsonHostsBrief(JsonDocument& doc) const;
    void toJsonWifiBrief(JsonDocument& doc) const;
    void toJsonProfiles(JsonDocument& doc) const;
    void toJsonProfilesMeta(JsonDocument& doc) const;  // counts only (cheap)
    void toJsonHost(uint32_t ip, JsonDocument& doc) const;
    void toJsonAp(const char* bssid, JsonDocument& doc) const;
    bool runApAction(const char* action, const char* bssid, JsonDocument& result);
    bool triggerRfScan();
    void toJsonEvents(JsonDocument& doc) const;
    void toJsonEvents(JsonDocument& doc, int maxEvents) const;
    void buildTextReport(String& out) const;
    void logEvent(const char* msg);

    ProfileEngine& profile() { return _profile; }

    int hostCount() const { return _hostCount; }
    int wifiCount() const { return _wifiCount; }
    int profileQueueDepth() const { return _profileQCount; }
    const char* phaseName() const { return _phase; }
    uint8_t scanProgress() const { return _progress; }
    void formatActivityLine(char* buf, size_t len) const;
    bool getHostBrief(int idx, char* ipOut, size_t ipLen, char* vendorOut, size_t vLen,
                      char* typeOut, size_t tLen, uint8_t* risk, uint8_t* ports) const;
    bool getWifiBrief(int idx, char* ssidOut, size_t ssidLen, int8_t* rssi, uint8_t* enc) const;

    bool autoScanPaused() const { return _orchestrationPaused; }
    bool autoProfileOnDiscovery() const { return _autoProfileOnDiscovery; }
    void setAutoScanPaused(bool paused);
    void setAutoProfileOnDiscovery(bool enabled);
    void clearProfileQueue();
    bool cancelLanSweep();
    bool controlAction(const char* action, const String& arg, String& message);
    void kickFastDiscovery();
    void requestQuietMs(unsigned ms);
    bool isCoopQuiet() const;

private:
    enum LanPhase : uint8_t {
        LAN_PHASE_PRIORITY = 0,
        LAN_PHASE_TCP = 1,
    };

    LanPhase _lanPhase = LAN_PHASE_PRIORITY;
    uint32_t _priorityTargets[LAN_PRIORITY_MAX];
    int _priorityCount = 0;
    int _priorityIdx = 0;
    uint8_t _tcpPortRound = 0;

    ProfileEngine _profile;
    bool _orchestrationPaused = false;
    bool _autoProfileOnDiscovery = false;
    IntelHost _hosts[INTEL_MAX_HOSTS];
    int _hostCount = 0;
    IntelWifi _wifi[INTEL_MAX_WIFI];
    int _wifiCount = 0;

    const char* _phase = "STANDBY";
    uint8_t _progress = 0;
    unsigned long _lastWifiScan = 0;
    unsigned long _wifiUpSince = 0;
    unsigned long _quietUntil = 0;
    bool _wifiScanAsync = false;
    unsigned long _lanSweepStart = 0;
    uint8_t _scanAddr[4] = {0};
    uint8_t _scanBase[4] = {0};
    uint8_t _scanBcast[4] = {0};
    uint8_t _lanPortIdx = 0;
    uint16_t _lanSlotsChecked = 0;
    uint16_t _lanSlotsTotal = 253;
    bool _rfInProgress = false;
    bool _sweepActive = false;
    uint16_t _sweepCount = 0;
    uint32_t _currentTarget = 0;
    uint16_t _currentPort = 0;

    uint32_t _profileQueue[INTEL_MAX_HOSTS];
    int _profileQCount = 0;

    LanDiscovery _lanDiscovery;
    bool _multicastActive = false;
    unsigned long _lastArpGapMs = 0;
    uint8_t _arpGapAddr[4] = {0};
    uint8_t _arpGapBase[4] = {0};
    uint8_t _arpGapBcast[4] = {0};
    bool _arpGapReady = false;

    void runWifiScan();
    void finishWifiScanResults(int n);
    void enqueueProfile(uint32_t ip);
    void runProfileStep();

    static const uint16_t PROBE_PORTS[INTEL_PORT_COUNT];

    static const int EVENT_RING_SIZE = 48;
    static const size_t EVENT_LOG_MAX_BYTES = 12288;
    static constexpr const char* EVENT_LOG_PATH = "/intel_events.log";

    String _events[48];
    int _eventHead = 0;

    void loadEventLog();
    void persistEventLine(const char* line);
    void trimEventLog();
    void runLanStep();
    void runPriorityStep();
    void runTcpStep();
    void buildPriorityTargets();
    void markHostAlive(uint32_t ip, const uint8_t* mac, uint16_t latency);
    void ingestDiscoveryHit(const LanDiscoveryHit& hit);
    void setHostDiscoveryMeta(IntelHost& h, const char* source, const char* name);
    void runArpGapStep();
    void ensureMulticastListeners();
    void stopMulticastListeners();
    static int discoveryRank(const char* source);
    bool arpResolve(uint32_t ip, uint8_t mac[6]);
    bool probePort(uint32_t ip, uint16_t port, uint16_t& latencyMs, uint16_t timeoutMs);
    bool probeHostQuick(uint32_t ip, uint16_t& latencyMs, uint16_t& openPort);
    bool shouldSkipTcpTarget(uint32_t ip) const;
    void upsertHost(uint32_t ip, uint16_t latency, uint16_t port);
    void finalizeHostRisks();
    void updateCveCount(IntelHost& h);
    uint8_t calcHostRisk(const IntelHost& h) const;
    uint8_t calcWifiRisk(const IntelWifi& w) const;
    static uint32_t ipBytesToU32(const uint8_t b[4]);
    static void u32ToIpBytes(uint32_t ip, uint8_t b[4]);
    static IPAddress u32ToAddress(uint32_t ip);
    static int compareIpBytes(const uint8_t a[4], const uint8_t b[4]);
    static bool incrementIpBytes(uint8_t addr[4], const uint8_t mask[4]);
    void beginLanSweep();
    bool requestLanSweep();
    bool requestRfScan();
    bool requestProfileAll(String& message);
    bool requestProfileHost(uint32_t ip, String& message);
    void clearDiscovery();
    bool isSelfIp(const uint8_t addr[4]) const;
    int findHost(uint32_t ip) const;
    const char* encLabel(uint8_t t) const;
    const char* portService(uint16_t port) const;
    void appendHostJson(const IntelHost& h, JsonObject& o) const;
    void appendHostJsonBrief(const IntelHost& h, JsonObject& o) const;
    void recomputeProgress();
    int countProfiledHosts() const;
    const char* progressKind() const;
    void buildProgressLabel(char* buf, size_t len) const;
};
