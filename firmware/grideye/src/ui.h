#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Preferences.h>
#include <vector>
#include <functional>
#include "config.h"
enum Screen {
    SCREEN_TOUCH_DIAG,
    SCREEN_CALIBRATE,
    SCREEN_NET_MODE,
    SCREEN_WIFI_SCAN,
    SCREEN_WIFI_PASS,
    SCREEN_WIFI_CONNECTING,
    SCREEN_MAIN_MENU,
    SCREEN_INTEL_LIVE,
    SCREEN_SHARE_INTEL,
    SCREEN_STATUS,
    SCREEN_ROAMING
};

enum KBMode { KB_LOWER, KB_UPPER, KB_NUM };

struct WiFiEntry {
    String ssid;
    int rssi;
    bool secure;
};

struct TouchCal {
    int xMin, xMax, yMin, yMax;
    bool xInvert, yInvert;
};

class UI {
public:
    void begin();
    void loop();

    void setScreen(Screen s);
    Screen getScreen() const { return _screen; }

    void setWiFiList(const std::vector<WiFiEntry>& list);
    String getSelectedSSID() const { return _selectedSSID; }
    String getEnteredPassword() const { return _inputBuf; }

    void setConnected(bool ok, const String& ip = "", const String& ssid = "");
    void setConnectionState(bool ok);
    void updateLinkState(bool ok, const String& ip = "", const String& ssid = "");
    bool isConnected() const { return _isConnected; }
    void setIntelStatus(int hosts, int aps, const char* phase, uint8_t progress,
                        const char* activityLine = "");
    void setPreviewRows(const char* hostLines[4], int hostLineCount,
                        const char* wifiLines[3], int wifiLineCount);
    void setStatusText(const String& t) { _statusLine = t; }
    void showToast(const String& msg, uint16_t color = COLOR_ACCENT);

    void setRoamingView(int aps, int clients, int probes, int handshakes, int ble,
                        uint8_t channel, uint32_t frames, bool capturing, bool armed,
                        bool bleOn, bool listIsBle, const char* lines[8], int lineCount);

    void setLEDColor(uint8_t r, uint8_t g, uint8_t b);
    void setBacklight(uint8_t val);
    void showOverlayMessage(const String& msg);
    void showDashboardQr(const String& url);

    using ScanCallback = std::function<void()>;
    using ConnectCallback = std::function<void(const String& ssid, const String& pass)>;
    using AppSelectCallback = std::function<void(int idx)>;
    using ReconnectCallback = std::function<void()>;
    using ForgetWifiCallback = std::function<void()>;
    using ShareIntelCallback = std::function<void()>;
    using NetModeCallback = std::function<void(int mode, bool askEveryBoot)>;
    using RoamCaptureCallback = std::function<void()>;

    void onScanRequest(ScanCallback cb)       { _onScan = cb; }
    void onConnectRequest(ConnectCallback cb) { _onConnect = cb; }
    void onAppSelect(AppSelectCallback cb)    { _onAppSelect = cb; }
    void onReconnect(ReconnectCallback cb)    { _onReconnect = cb; }
    void onForgetWifi(ForgetWifiCallback cb)  { _onForgetWifi = cb; }
    void onShareIntel(ShareIntelCallback cb)  { _onShareIntel = cb; }
    void onNetMode(NetModeCallback cb)        { _onNetMode = cb; }
    void onRoamCapture(RoamCaptureCallback cb) { _onRoamCapture = cb; }
    void onRoamArm(RoamCaptureCallback cb)     { _onRoamArm = cb; }
    void onRoamBle(RoamCaptureCallback cb)     { _onRoamBle = cb; }
    void onRoamEngage(std::function<void(int)> cb) { _onRoamEngage = cb; }
    void onRoamListToggle(RoamCaptureCallback cb)  { _onRoamListToggle = cb; }
    using PostCalCallback = std::function<void()>;
    void onPostCal(PostCalCallback cb) { _onPostCal = cb; }
    void onWifiScanBack(PostCalCallback cb) { _onWifiScanBack = cb; }
    void setShareStatus(const char* msg);
    void showNetModePicker(bool askEveryBoot = true);
    void resetTouchState();
    void probeTouchHealth(const char* tag);
    bool hasCalibration() const { return _calLoaded; }
    void setRoamSdAvailable(bool ok) { _roamSdAvailable = ok; }

private:
    bool _askNetEveryBoot = true;
    TFT_eSPI _tft;
    Screen   _screen = SCREEN_CALIBRATE;
    KBMode   _kbMode = KB_LOWER;

    // Touch state
    unsigned long _lastTouchTime = 0;
    uint16_t _lastTouchRawX = 0;
    uint16_t _lastTouchRawY = 0;
    unsigned long _screenChangeTime = 0;
    bool _wasTouched = false;
    bool _touchWasDown = false;

    // Touch calibration
    TouchCal _cal;
    int  _calStep = 0;        // 0-4: collecting points, 5: verify mode
    uint16_t _calRawX[4];
    uint16_t _calRawY[4];
    bool _calLoaded = false;

    // WiFi scan results
    std::vector<WiFiEntry> _wifiList;
    int _wifiScroll = 0;
    String _selectedSSID;

    // Keyboard input
    char _inputBuf[KB_MAX_INPUT + 1] = {};
    int  _inputLen = 0;
    bool _showPassword = false;

    // Main menu
    String _connectedIP;
    String _statusLine;
    int _intelHosts = 0;
    int _intelAps = 0;
    char _intelPhase[20] = "STANDBY";
    char _intelActivity[64] = "";
    uint8_t _intelProgress = 0;
    bool   _isConnected = false;

    int _lastIntelHosts = -1;
    int _lastIntelAps = -1;
    uint8_t _lastIntelProgress = 255;
    char _lastIntelPhase[20] = "";
    char _lastIntelActivity[64] = "";
    char _lastStatusIp[20] = "";
    char _previewHosts[4][28];
    char _previewWifi[3][28];
    int _previewHostN = 0;
    int _previewWifiN = 0;

    // Toast
    String _toastMsg;
    unsigned long _toastExpiry = 0;

    // Overlay
    String _overlayMsg;
    unsigned long _overlayExpiry = 0;
    bool _qrOverlayActive = false;
    uint8_t _qrDismissTaps = 0;
    String _qrUrl;

    void drawQrOverlay();
    void dismissQrOverlay();

    // Callbacks
    ScanCallback    _onScan;
    ConnectCallback _onConnect;
    AppSelectCallback _onAppSelect;
    ReconnectCallback _onReconnect;
    ForgetWifiCallback _onForgetWifi;
    ShareIntelCallback _onShareIntel;
    NetModeCallback _onNetMode;
    PostCalCallback _onPostCal;
    PostCalCallback _onWifiScanBack;
    RoamCaptureCallback _onRoamCapture;
    RoamCaptureCallback _onRoamArm;
    RoamCaptureCallback _onRoamBle;
    RoamCaptureCallback _onRoamListToggle;
    std::function<void(int)> _onRoamEngage;

    char _shareStatus[48] = "";

    // Roaming recon
    int _roamAps = 0, _roamClients = 0, _roamProbes = 0, _roamHandshakes = 0, _roamBle = 0;
    uint8_t _roamChannel = 0;
    uint32_t _roamFrames = 0;
    bool _roamCapturing = false;
    bool _roamArmed = false;
    bool _roamBleOn = false;
    bool _roamListIsBle = false;
    bool _roamSdAvailable = false;
    char _roamLines[8][32];
    int _roamLineN = 0;

    // Touch helpers
    void touchBegin();
    struct TouchSample {
        uint8_t  irq;       // pin level: 0=LOW 1=HIGH
        uint16_t rawX;
        uint16_t rawY;
        uint16_t rawZ;
        bool irqSaysDown;   // irq==LOW (typical XPT2046)
        bool pressDown;     // Z above threshold
        bool rawValid;      // X/Y in plausible range
    };
    TouchSample touchSample();
    bool touchHasPressure(int *zOut = nullptr);
    bool touchReadRaw(uint16_t &rawX, uint16_t &rawY);
    bool touchRead(int &x, int &y);
    bool calAcceptsRaw(uint16_t rawX, uint16_t rawY) const;
    void mapTouchPoint(uint16_t rawX, uint16_t rawY, int &x, int &y) const;
    uint16_t touchReadChannel(uint8_t cmd);
    void loadCalibration();
    void saveCalibration();

    // Drawing
    void enterPostCalFlow();
    void drawNetMode();
    bool wifiScanPortalBanner() const;
    int wifiScanListTop() const;
    int wifiScanMaxVisible() const;
    void handleTouchNetMode(int x, int y);
    void drawTouchDiag();
    void loopTouchDiag();
    void drawCalibrate();
    void drawCalTarget(int sx, int sy);
    void drawWiFiScan();
    void drawWiFiPass();
    void drawConnecting();
    void drawMainMenu();
    void drawShareIntel();
    void drawIntelLive();
    void refreshIntelPanel();
    void refreshPreviewPanel();
    void refreshStatusBar();
    void drawFooterBack(const char* label = "BACK");
    bool hitFooterBack(int x, int y) const;
    void drawStatusScreen();
    void drawRoaming();
    void drawRoamButtons();
    void refreshRoamingPanel();
    void handleTouchRoaming(int x, int y);
    void drawKeyboard();
    void drawInputField();
    void drawStatusBar();
    void drawToast();

    // Touch handlers
    void handleTouchCalibrate(uint16_t rawX, uint16_t rawY);
    void handleTouchWiFiScan(int x, int y);
    void handleTouchWiFiPass(int x, int y);
    void handleTouchMainMenu(int x, int y);
    void handleTouchIntelLive(int x, int y);
    void handleTouchShareIntel(int x, int y);
    void handleTouchStatus(int x, int y);
    char  getKeyAt(int x, int y);

    void clearInput();
    void appendInput(char c);
    void backspaceInput();
};
