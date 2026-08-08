#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"

// Passive 802.11 reconnaissance: promiscuous capture + channel hopping.
// Catalogs access points, associated clients, and probe requests. The
// promiscuous RX callback runs on the Wi-Fi task and only enqueues compact
// records; all table updates happen on the main loop via loop().

// Optional raw-frame sink used by the capture module. Registered once; invoked
// from the Wi-Fi RX callback for frames of interest while capture is enabled.
typedef void (*ReconCaptureSink)(const uint8_t* data, uint16_t len);
void rfReconSetCaptureSink(ReconCaptureSink sink);
void rfReconEnableCapture(bool enabled);

#define RECON_HOP_MS      280
#define RECON_PRUNE_MS    90000   // drop stations not seen for 90s
#define RECON_CHANNELS    13

struct ReconAp {
    uint8_t  bssid[6];
    char     ssid[33];
    int8_t   rssi;
    uint8_t  channel;
    bool     secure;
    uint16_t beacons;
    uint8_t  clients;
    bool     handshake;   // EAPOL observed for this BSSID
    uint32_t lastSeen;
};

struct ReconClient {
    uint8_t  mac[6];
    uint8_t  bssid[6];    // associated AP (zeros if probe-only)
    int8_t   rssi;
    uint16_t frames;
    bool     hasProbe;
    char     probe[33];
    uint32_t lastSeen;
};

class RfRecon {
public:
    void begin(bool keepAp = false);
    void stop();
    void loop();
    bool isRunning() const { return _running; }

    uint8_t  channel() const { return _channel; }
    int      apCount() const { return _apCount; }
    int      clientCount() const { return _clientCount; }
    int      probeCount() const { return _probeCount; }
    int      handshakeCount() const;
    uint32_t frameCount() const { return _frames; }

    // Formats the idx-th AP (sorted by RSSI) into buf, e.g. "Home  c6 -54 3c*".
    bool getApLine(int idx, char* buf, size_t len) const;

    void toJsonAps(JsonDocument& doc) const;
    void toJsonClients(JsonDocument& doc) const;

    // Active engagement (gated by explicit on-device authorization).
    void setAuthorized(bool a) { _authorized = a; }
    bool authorized() const { return _authorized; }
    // Sends a deauth/disassoc burst to the idx-th AP (RSSI-sorted) and its known
    // clients. Returns false unless authorized + running. Fills ssidOut.
    bool deauthSortedIndex(int idx, char* ssidOut, size_t len);

private:
    bool _running = false;
    bool _authorized = false;
    volatile uint8_t _channel = 1;
    unsigned long _lastHop = 0;
    unsigned long _lastPrune = 0;

    ReconAp     _aps[RECON_MAX_AP];
    int         _apCount = 0;
    ReconClient _clients[RECON_MAX_CLIENT];
    int         _clientCount = 0;
    int         _probeCount = 0;
    uint32_t    _frames = 0;

    void drainQueue();
    int  findAp(const uint8_t bssid[6]) const;
    int  findClient(const uint8_t mac[6]) const;
    void upsertAp(const uint8_t bssid[6], const char* ssid, int8_t rssi,
                  uint8_t ch, bool secure, bool beacon, bool eapol);
    void upsertClient(const uint8_t mac[6], const uint8_t bssid[6],
                      int8_t rssi, const char* probe);
    void prune();
    void recountClients();
    void buildSortedApIndex(uint8_t* order, int& n) const;
};
