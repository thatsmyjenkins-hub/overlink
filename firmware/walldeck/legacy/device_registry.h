#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "config.h"

class DeviceRegistry {
public:
    typedef void (*UiPumpFn)();

    void begin(UiPumpFn pump = nullptr);
    void loop();
    void requestDiscovery(bool fullScan = false);

    IPAddress wizIp(size_t index) const;
    IPAddress wledIp() const;
    IPAddress cyberdeckIp() const;
    bool wizFound(size_t index) const;
    bool wledFound() const;
    bool cyberdeckFound() const;
    uint8_t wizFoundCount() const;
    bool isDiscovering() const { return discovering_; }
    bool isWledScanning() const { return scanActive_; }
    bool hasAnyDevice() const;
    uint32_t stateHash() const;

private:
    static constexpr uint16_t DISCOVERY_PORT = 47100;

    IPAddress wizIps_[WIZ_BULB_COUNT];
    IPAddress wledIp_;
    IPAddress cyberdeckIp_;
    Preferences prefs_;
    bool discovering_ = false;
    bool pendingDiscovery_ = false;
    unsigned long lastDiscoveryMs_ = 0;
    unsigned long nextScanMs_ = 0;

    bool scanActive_ = false;
    size_t scanSubnetIdx_ = 0;
    uint8_t scanHost_ = 1;
    bool deckScanActive_ = false;
    size_t deckScanSubnetIdx_ = 0;
    uint8_t deckScanHost_ = 1;
    unsigned long nextDeckScanMs_ = 0;
    UiPumpFn pump_ = nullptr;

    void yieldUi() {
        if (pump_) pump_();
    }

    void normalizeMac(const char *in, char *out, size_t outLen);
    bool macEquals(const char *a, const char *b);
    void loadWizCache();
    void saveWizCache(size_t index, const IPAddress &ip);
    bool probeWiz(const IPAddress &ip, size_t matchIndex);
    bool probeWizReliable(const IPAddress &ip, size_t matchIndex);
    void discoverWizBroadcast(WiFiUDP &udp);
    void discoverWizFallback();
    void discoverWiz();
    void tryCachedWled();
    void tryFallbackWled();
    void tryMdnsWled();
    void startWledScan();
    bool scanWledStep();
    bool probeWled(const IPAddress &ip);
    bool probeWledReliable(const IPAddress &ip);
    void saveWledCache(const IPAddress &ip);
    void loadWledCache();
    void tryCachedCyberDeck();
    void tryFallbackCyberDeck();
    void tryMdnsCyberDeck();
    void startCyberDeckScan();
    bool scanCyberDeckStep();
    bool probeCyberDeck(const IPAddress &ip);
    bool probeCyberDeckReliable(const IPAddress &ip);
    void saveCyberDeckCache(const IPAddress &ip);
    void loadCyberDeckCache();
    void runDiscovery(bool fullScan = false);
};
