#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ui.h"
#include "intel.h"
#include "rf_recon.h"
#include "capture_store.h"
#include "ble_scan.h"
#include "captive_portal.h"

enum NetMode : uint8_t {
    NET_MODE_JOIN   = 0,  // Join LAN (saved creds or CYBERDECK setup portal)
    NET_MODE_REMOTE = 1,  // Legacy pref — treated as JOIN on load
    NET_MODE_FIELD  = 2,  // Off-network recon: promiscuous Wi‑Fi + BLE + capture
};
#define NET_MODE_LOCAL   NET_MODE_JOIN
#define NET_MODE_ROAMING NET_MODE_FIELD

class Network {
    enum StaJoinKind : uint8_t {
        STA_JOIN_DEVICE = 0,
        STA_JOIN_PORTAL_KEEP_AP,
        STA_JOIN_PORTAL_DROP_AP,
    };
    enum ScanJobKind : uint8_t {
        SCAN_JOB_NONE = 0,
        SCAN_JOB_DEVICE_UI,
        SCAN_JOB_REMOTE_CACHE,
    };

public:
    void begin(UI& ui);
    void poll(UI& ui);
    void loop();

    void applyNetMode(NetMode mode, UI& ui);
    void returnToModePicker(UI& ui);
    bool shouldShowNetPicker();
    void finishBootFlow(UI& ui);
    bool isBootFlowDone() const { return _bootFlowDone; }
    void saveNetPickerPref(bool askEveryBoot, NetMode mode);
    NetMode loadSavedNetMode();
    bool isFieldMode() const { return _netMode == NET_MODE_FIELD; }
    bool isRoamingMode() const { return isFieldMode(); }  // legacy alias
    NetMode currentMode() const { return _netMode; }
    bool intelShouldRun() const;

    void scanNetworks(UI& ui);
    void connectWiFi(const String& ssid, const String& pass, UI& ui);

    String getIP() const;
    bool   isConnected() const;
    bool   hasSavedCredentials() const { return _savedSSID.length() > 0; }
    String getSavedSSID() const { return _savedSSID; }
    void   forgetCredentials();
    void   reconnectSaved(UI& ui);

    IntelScanner& intel() { return _intel; }
    RfRecon& recon() { return _recon; }
    CaptureStore& capture() { return _capture; }
    bool toggleCapture();
    bool isCapturing() const { return _capture.isCapturing(); }
    bool sdAvailable() const { return _capture.isMounted(); }
    bool ensureSdReady();

    BleScan& ble() { return _ble; }
    bool toggleBle();
    bool bleRunning() const { return _ble.isRunning(); }

    bool writeIntelExport();
    bool writeIntelReport();
    bool executeControl(const String& action, const String& arg, String& message);

    void setLED(uint8_t r, uint8_t g, uint8_t b);

    bool isPortalActive() const { return _portalActive; }
    String portalUrl() const;
    void startSetupPortal(UI& ui);

private:
    DNSServer _dns;
    CaptivePortalHandler* _captiveHandler = nullptr;
    bool _portalActive = false;
    bool _portalRoutesAdded = false;
    AsyncWebServer _server{80};
    Preferences    _prefs;
    IntelScanner   _intel;
    RfRecon        _recon;
    CaptureStore   _capture;
    BleScan        _ble;
    String _savedSSID;
    String _savedPass;

    uint8_t _ledR = 0, _ledG = 0, _ledB = 0;
    bool _serverStarted = false;
    bool _routesRegistered = false;
    bool _netModeApplied = false;
    NetMode _netMode = NET_MODE_JOIN;
    bool _intelStarted = false;
    bool _bootFlowDone = false;
    bool _sdInitDone = false;
    unsigned long _lastReconnectTry = 0;
    unsigned long _lastUiSync = 0;
    UI* _ui = nullptr;

    // Remote Station web-config job state (set by async HTTP handlers, run on main loop)
    volatile bool _pendingConnect = false;
    volatile bool _pendingRescan = false;
    volatile uint8_t _wifiJobState = 0;  // 0 idle, 1 connecting, 2 connected, 3 failed
    bool _remoteScanned = false;
    String _pendingSsid;
    String _pendingPass;
    bool _pendingKeepAp = true;
    String _apCacheJson = "{\"networks\":[]}";
    unsigned long _lastApScan = 0;

    bool _staJoinActive = false;
    StaJoinKind _staJoinKind = STA_JOIN_DEVICE;
    String _staJoinSsid;
    String _staJoinPass;
    unsigned long _staJoinStart = 0;
    UI* _staJoinUi = nullptr;

    ScanJobKind _scanJob = SCAN_JOB_NONE;
    UI* _scanJobUi = nullptr;

    void startHomeNetwork(UI& ui);
    void handleRemoteJobs();
    void pollWifiJobs(UI& ui);
    void scanNetworksToCache();
    void joinTargetFromPortal(const String& ssid, const String& pass, bool keepAp);

    void beginStaJoin(const String& ssid, const String& pass, UI& ui, StaJoinKind kind);
    void beginWifiScan(ScanJobKind kind, UI* ui = nullptr);
    void ensureIntelStarted(bool resetDiscovery = false);
    void stopSetupPortal();
    void ensureHttpServer();

    void buildHealthJson(JsonDocument& doc);
    void buildControlStatusJson(JsonDocument& doc);
    bool refreshDashCache();
    bool refreshReconCache();
    void markHttpActivity();

    String _dashAllJson;
    bool _dashAllValid = false;
    unsigned long _lastDashCache = 0;
    unsigned long _lastDashCacheOk = 0;

    String _reconAllJson;
    bool _reconAllValid = false;
    unsigned long _lastReconCache = 0;
    unsigned long _lastReconCacheOk = 0;

    void setupLED();
    void setupServer();
    void loadCredentials();
    void saveCredentials(const String& ssid, const String& pass);
    String uptimeString() const;
    static uint32_t parseIp(const String& s);
};
