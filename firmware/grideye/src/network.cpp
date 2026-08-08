#include "network.h"
#include "vspi_bus.h"
#include <algorithm>

static const char* KEY_NET_MODE = "net_mode";
static const char* KEY_ASK_NET = "ask_net";

static bool heapOkForDashCache();

bool Network::shouldShowNetPicker() {
    return _prefs.getBool(KEY_ASK_NET, true);
}

NetMode Network::loadSavedNetMode() {
    uint8_t v = _prefs.getUChar(KEY_NET_MODE, NET_MODE_JOIN);
    if (v == NET_MODE_REMOTE)
        v = NET_MODE_JOIN;
    if (v != NET_MODE_JOIN && v != NET_MODE_FIELD)
        v = NET_MODE_JOIN;
    return (NetMode)v;
}

void Network::saveNetPickerPref(bool askEveryBoot, NetMode mode) {
    _prefs.putBool(KEY_ASK_NET, askEveryBoot);
    _prefs.putUChar(KEY_NET_MODE, (uint8_t)mode);
}

bool Network::intelShouldRun() const {
    return isConnected();  // roaming uses RfRecon, fed separately in the main loop
}

bool Network::toggleCapture() {
    if (!ensureSdReady()) return false;
    if (_capture.isCapturing()) {
        _capture.stopCapture();
        return false;
    }
    return _capture.startCapture();
}

bool Network::toggleBle() {
    if (_ble.isRunning()) {
        _ble.stop();
        return false;
    }
    _ble.begin();
    return _ble.isRunning();
}

void Network::begin(UI& ui) {
    setupLED();

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }

    _prefs.begin(PREFS_NAMESPACE, false);
    // Older builds forced ask_net=false on every successful WiFi join.
    if (!_prefs.getBool("ask_mig_v2", false)) {
        _prefs.putBool(KEY_ASK_NET, true);
        _prefs.putBool("ask_mig_v2", true);
        Serial.println("[BOOT] Restored ask_net=true (legacy pref migration)");
    }
    loadCredentials();
    if (_savedSSID.length() > 0)
        Serial.printf("[NET] Saved WiFi: %s\n", _savedSSID.c_str());
    else
        Serial.println("[NET] No saved WiFi in flash");
    _ui = &ui;

    bool ask = shouldShowNetPicker();
    Serial.printf("[BOOT] ask_net=%d mode=%u ssid=%s\n",
                  ask ? 1 : 0, (unsigned)loadSavedNetMode(),
                  _savedSSID.length() ? _savedSSID.c_str() : "(none)");
    Serial.println("[NET] Waiting for boot flow");
}

bool Network::ensureSdReady() {
    if (_sdInitDone)
        return _capture.isMounted();
    _sdInitDone = true;
#if ENABLE_SD_CAPTURE
    Serial.println("[NET] SD init (deferred)…");
    if (_capture.begin())
        Serial.printf("[NET] microSD ready (%llu MB) for captures\n", _capture.cardSizeMB());
    else
        Serial.println("[NET] microSD not detected — capture disabled");
#else
    Serial.println("[NET] SD capture disabled (ENABLE_SD_CAPTURE=0)");
#endif
    vspiSelectTouch();
    if (_ui)
        _ui->setRoamSdAvailable(_capture.isMounted());
    return _capture.isMounted();
}

void Network::finishBootFlow(UI& ui) {
    if (_bootFlowDone) return;
    _bootFlowDone = true;
    vspiSelectTouch();
    ui.resetTouchState();
    if (shouldShowNetPicker()) {
        Serial.println("[BOOT] Showing mode picker — waiting for tap");
        ui.showNetModePicker(true);
        ui.probeTouchHealth("mode_picker");
    } else {
        Serial.printf("[BOOT] Auto mode %u (ask_net=0)\n", (unsigned)loadSavedNetMode());
        applyNetMode(loadSavedNetMode(), ui);
    }
    ui.resetTouchState();
}

void Network::ensureIntelStarted(bool resetDiscovery) {
    if (_intelStarted) {
        if (resetDiscovery)
            _intel.begin(true);
        return;
    }
    _intelStarted = true;
    _intel.begin(resetDiscovery);
}

void Network::poll(UI& ui) {
    if (!_bootFlowDone)
        return;
    if (_netModeApplied)
        return;

    Screen s = ui.getScreen();
    if (s == SCREEN_CALIBRATE || s == SCREEN_TOUCH_DIAG)
        return;

    if (s == SCREEN_NET_MODE) {
        if (shouldShowNetPicker())
            return;  // wait for user tap — never auto-override mode selection
        Serial.printf("[NET] Auto-applying saved mode %u\n", (unsigned)loadSavedNetMode());
        applyNetMode(loadSavedNetMode(), ui);
        return;
    }

    if (!shouldShowNetPicker())
        applyNetMode(loadSavedNetMode(), ui);
}

void Network::markHttpActivity() {
    _intel.requestQuietMs(LAN_QUIET_AFTER_HTTP_MS);
}

bool Network::refreshReconCache() {
    if (!heapOkForDashCache()) {
        if (_reconAllValid && millis() - _lastReconCacheOk < 60000)
            return true;
        _reconAllValid = false;
        return false;
    }

    JsonDocument d;
    String wifi, ble, stats;
    d.clear(); _recon.toJsonAps(d);
    if (serializeJson(d, wifi) == 0) { _reconAllValid = false; return false; }

    d.clear(); _ble.toJson(d);
    if (serializeJson(d, ble) == 0) { _reconAllValid = false; return false; }

    d.clear();
    d["channel"] = _recon.channel();
    d["apCount"] = _recon.apCount();
    d["clientCount"] = _recon.clientCount();
    d["probeCount"] = _recon.probeCount();
    d["handshakeCount"] = _recon.handshakeCount();
    d["frameCount"] = _recon.frameCount();
    d["capturing"] = _capture.isCapturing();
    d["sdMounted"] = _capture.isMounted();
    d["bleRunning"] = _ble.isRunning();
    d["armed"] = _recon.authorized();
    if (serializeJson(d, stats) == 0) { _reconAllValid = false; return false; }

    String out;
    out.reserve(2048);
    out = "{\"wifi\":" + wifi + ",\"ble\":" + ble + ",\"stats\":" + stats + "}";
    if (out.length() < 12) {
        _reconAllValid = false;
        return false;
    }
    _reconAllJson = out;
    _reconAllValid = true;
    _lastReconCacheOk = millis();
    return true;
}

void Network::beginStaJoin(const String& ssid, const String& pass, UI& ui, StaJoinKind kind) {
    _staJoinActive = true;
    _staJoinKind = kind;
    _staJoinSsid = ssid;
    _staJoinPass = pass;
    _staJoinUi = &ui;
    _staJoinStart = millis();
    _wifiJobState = 1;

    ui.setScreen(SCREEN_WIFI_CONNECTING);

    if (kind == STA_JOIN_PORTAL_DROP_AP)
        stopSetupPortal();

    if (kind == STA_JOIN_PORTAL_KEEP_AP)
        WiFi.mode(WIFI_AP_STA);
    else
        WiFi.mode(WIFI_STA);

    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[NET] STA join started → %s\n", ssid.c_str());
}

static void fillWifiListFromScan(std::vector<WiFiEntry>& list) {
    int n = WiFi.scanComplete();
    if (n < 0) return;
    list.clear();
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        bool dupe = false;
        for (auto& e : list) {
            if (e.ssid == ssid) { dupe = true; break; }
        }
        if (dupe) continue;
        WiFiEntry entry;
        entry.ssid = ssid;
        entry.rssi = WiFi.RSSI(i);
        entry.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        list.push_back(entry);
    }
    std::sort(list.begin(), list.end(), [](const WiFiEntry& a, const WiFiEntry& b) {
        return a.rssi > b.rssi;
    });
}

void Network::beginWifiScan(ScanJobKind kind, UI* ui) {
    if (_scanJob != SCAN_JOB_NONE) return;
    _scanJob = kind;
    _scanJobUi = ui;
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    Serial.println("[NET] WiFi scan started (async)");
}

void Network::pollWifiJobs(UI& ui) {
    if (_staJoinActive && _staJoinUi) {
        if (WiFi.status() == WL_CONNECTED) {
            _staJoinActive = false;
            _wifiJobState = 2;
            saveCredentials(_staJoinSsid, _staJoinPass);
            if (_staJoinKind == STA_JOIN_PORTAL_DROP_AP)
                WiFi.mode(WIFI_STA);
            if (_recon.isRunning())
                _recon.stop();
            if (_staJoinKind == STA_JOIN_DEVICE)
                stopSetupPortal();
            ensureIntelStarted(false);
            _intel.kickFastDiscovery();
            ensureHttpServer();
            _staJoinUi->setConnected(true, WiFi.localIP().toString(), _staJoinSsid);
            _staJoinUi->showToast("Dashboard: http://" + WiFi.localIP().toString() + "/", COLOR_SUCCESS);
            Serial.printf("[NET] Joined %s\n", WiFi.localIP().toString().c_str());
            _staJoinUi = nullptr;
        } else if (millis() - _staJoinStart >= WIFI_CONNECT_TIMEOUT_MS) {
            _staJoinActive = false;
            _wifiJobState = 3;
            Serial.println("[NET] STA join timed out");
            WiFi.disconnect();
            if (_staJoinKind == STA_JOIN_DEVICE) {
                startSetupPortal(*_staJoinUi);
                _staJoinUi->setScreen(SCREEN_WIFI_SCAN);
                beginWifiScan(SCAN_JOB_DEVICE_UI, _staJoinUi);
            } else if (_staJoinKind == STA_JOIN_PORTAL_DROP_AP && !_portalActive) {
                startSetupPortal(*_staJoinUi);
            }
            if (_staJoinKind != STA_JOIN_DEVICE && _ui)
                _ui->setStatusText("Join failed — retry from web UI");
            _staJoinUi = nullptr;
        }
    }

    if (_scanJob == SCAN_JOB_NONE) return;
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n == WIFI_SCAN_FAILED) {
        Serial.println("[NET] WiFi scan failed");
        WiFi.scanDelete();
        _scanJob = SCAN_JOB_NONE;
        _scanJobUi = nullptr;
        return;
    }

    if (_scanJob == SCAN_JOB_DEVICE_UI && _scanJobUi) {
        std::vector<WiFiEntry> list;
        fillWifiListFromScan(list);
        _scanJobUi->setWiFiList(list);
        _scanJobUi->resetTouchState();
        Serial.printf("[NET] Device scan: %u networks\n", (unsigned)list.size());
    } else if (_scanJob == SCAN_JOB_REMOTE_CACHE) {
        JsonDocument doc;
        JsonArray arr = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            bool dupe = false;
            for (JsonObject o : arr) {
                if (ssid == o["ssid"].as<const char*>()) { dupe = true; break; }
            }
            if (dupe) continue;
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = ssid;
            o["rssi"] = WiFi.RSSI(i);
            o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        _apCacheJson = "";
        serializeJson(doc, _apCacheJson);
        _lastApScan = millis();
        _remoteScanned = true;
        Serial.printf("[NET] Remote scan cached %u nets\n", (unsigned)arr.size());
    }

    WiFi.scanDelete();
    _scanJob = SCAN_JOB_NONE;
    _scanJobUi = nullptr;
}

void Network::startHomeNetwork(UI& ui) {
    if (_savedSSID.length() > 0) {
        Serial.printf("[NET] Joining saved %s\n", _savedSSID.c_str());
        beginStaJoin(_savedSSID, _savedPass, ui, STA_JOIN_DEVICE);
        return;
    }

    startSetupPortal(ui);
    ui.setScreen(SCREEN_WIFI_SCAN);
    ui.resetTouchState();
    beginWifiScan(SCAN_JOB_DEVICE_UI, &ui);
}

void Network::returnToModePicker(UI& ui) {
    _netModeApplied = false;
    _staJoinActive = false;
    _scanJob = SCAN_JOB_NONE;
    _staJoinUi = nullptr;
    _scanJobUi = nullptr;
    ui.showNetModePicker(true);
}

void Network::applyNetMode(NetMode mode, UI& ui) {
    if (mode == NET_MODE_REMOTE)
        mode = NET_MODE_JOIN;

    if (_recon.isRunning() && mode != NET_MODE_FIELD) {
        _capture.stopCapture();
        if (_ble.isRunning()) _ble.stop();
        _recon.stop();
    }

    _netMode = mode;
    _netModeApplied = true;

    switch (mode) {
    case NET_MODE_JOIN:
        Serial.println("[NET] Mode: JOIN (LAN intel or setup portal)");
        _lastDashCache = 0;
        _dashAllValid = false;
        startHomeNetwork(ui);
        break;

    case NET_MODE_FIELD:
        Serial.println("[NET] Mode: FIELD RECON (AP + RF/BLE)");
        startSetupPortal(ui);  // keep CYBERDECK AP so phone can reach field dashboard
        _recon.begin(true);
        ensureSdReady();
        ui.setConnectionState(false);
        ui.setStatusText("Field: http://192.168.4.1/");
        ui.setScreen(SCREEN_ROAMING);
        break;
    }
}

String Network::portalUrl() const {
    if (_portalActive)
        return "http://192.168.4.1/";
    return "";
}

// Remote Station: process web-driven scan/connect requests on the main thread
// (async HTTP handlers only set flags, so Wi‑Fi + TFT calls stay off the AsyncTCP task).
void Network::handleRemoteJobs() {
    if (_pendingConnect) {
        _pendingConnect = false;
        joinTargetFromPortal(_pendingSsid, _pendingPass, _pendingKeepAp);
        return;
    }

    if (WiFi.status() == WL_CONNECTED)
        return;  // deployed — dashboard takes over

    if (_pendingRescan) {
        _pendingRescan = false;
        scanNetworksToCache();
        return;
    }
    if (!_remoteScanned && _scanJob == SCAN_JOB_NONE)
        scanNetworksToCache();
}

void Network::scanNetworksToCache() {
    beginWifiScan(SCAN_JOB_REMOTE_CACHE, nullptr);
}

void Network::joinTargetFromPortal(const String& ssid, const String& pass, bool keepAp) {
    if (!_ui) return;
    if (_ui) _ui->setStatusText("Joining " + ssid + "…");
    beginStaJoin(ssid, pass, *_ui,
                 keepAp ? STA_JOIN_PORTAL_KEEP_AP : STA_JOIN_PORTAL_DROP_AP);
}

void Network::ensureHttpServer() {
    if (!_routesRegistered) {
        setupServer();
        _routesRegistered = true;
    }
    if (!_portalRoutesAdded && _portalActive) {
        if (!_captiveHandler)
            _captiveHandler = new CaptivePortalHandler("http://192.168.4.1/");
        _server.addHandler(_captiveHandler).setFilter(ON_AP_FILTER);
        _portalRoutesAdded = true;
        Serial.println("[NET] Captive portal redirects enabled");
    }
    if (!_serverStarted) {
        _serverStarted = true;
        setLED(0, 40, 60);
    }
}

void Network::startSetupPortal(UI& ui) {
    if (_portalActive) {
        ensureHttpServer();
        return;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);

    IPAddress apIp(PORTAL_AP_IP);
    IPAddress gateway(PORTAL_AP_GATEWAY);
    IPAddress subnet(PORTAL_AP_SUBNET);
    WiFi.softAPConfig(apIp, gateway, subnet);

    String ssid = PORTAL_SSID;
    if (!WiFi.softAP(ssid.c_str(), nullptr, 1, 0, 4)) {
        Serial.println("[NET] softAP failed");
        return;
    }

    _portalActive = true;
    _dns.start(53, "*", apIp);
    ensureHttpServer();

    Serial.printf("[NET] Setup AP \"%s\" — open %s\n", ssid.c_str(), portalUrl().c_str());
    Serial.println("[NET] Phones may auto-open the dashboard (captive portal)");

    ui.showToast("Join Wi‑Fi: CYBERDECK", COLOR_ACCENT);
    ui.setStatusText("Portal: " + portalUrl());
}

void Network::stopSetupPortal() {
    if (!_portalActive) return;
    _dns.stop();
    WiFi.softAPdisconnect(true);
    _portalActive = false;
    Serial.println("[NET] Setup AP stopped");
}

void Network::loop() {
    if (_ui)
        pollWifiJobs(*_ui);

    if (_portalActive)
        _dns.processNextRequest();

    if (_portalActive && !isFieldMode() && WiFi.status() != WL_CONNECTED)
        handleRemoteJobs();

    if (isFieldMode()) {
        ensureHttpServer();
        static bool loggedField = false;
        if (!loggedField) {
            loggedField = true;
            Serial.printf("[NET] Field dashboard http://%s/\n",
                          WiFi.softAPIP().toString().c_str());
        }
        if (!_recon.isRunning())
            _recon.begin(_portalActive);
        if (_lastReconCache == 0 || millis() - _lastReconCache >= 3500) {
            refreshReconCache();
            _lastReconCache = millis();
        }
        _recon.loop();
        _capture.loop();
        if (isFieldMode())
            _ble.loop();
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (_recon.isRunning())
            _recon.stop();
        ensureIntelStarted(false);
        ensureHttpServer();
        static bool loggedSta = false;
        if (!loggedSta) {
            loggedSta = true;
            Serial.printf("[NET] Dashboard http://%s/\n",
                          WiFi.localIP().toString().c_str());
        }
        if (_lastDashCache == 0 || millis() - _lastDashCache >= 3500) {
            if (!refreshDashCache())
                Serial.printf("[DASH] cache skip heap=%u blk=%u\n",
                              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            _lastDashCache = millis();
        }
        _intel.loop();
        return;
    }

    if (_portalActive) {
        ensureHttpServer();
        return;
    }

    _serverStarted = false;

    // Don't hammer STA reconnect while the boot mode picker is waiting for a tap.
    if (_ui && _ui->getScreen() == SCREEN_NET_MODE && !_netModeApplied && shouldShowNetPicker())
        return;

    if (_savedSSID.length() > 0 && millis() - _lastReconnectTry > 15000) {
        _lastReconnectTry = millis();
        Serial.printf("[NET] Reconnecting to saved %s\n", _savedSSID.c_str());
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(_savedSSID.c_str(), _savedPass.c_str());
    }
}

void Network::forgetCredentials() {
    _prefs.remove("ssid");
    _prefs.remove("pass");
    _savedSSID = "";
    _savedPass = "";
    WiFi.disconnect(true);
    Serial.println("[NET] Saved WiFi cleared");
}

void Network::reconnectSaved(UI& ui) {
    if (_savedSSID.length() > 0)
        connectWiFi(_savedSSID, _savedPass, ui);
    else {
        ui.setScreen(SCREEN_WIFI_SCAN);
        scanNetworks(ui);
    }
}

uint32_t Network::parseIp(const String& s) {
    IPAddress ip;
    if (!ip.fromString(s)) return 0;
    return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
           ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}

static const char* INTEL_EXPORT_JSON = "/intel_export.json";
static const char* INTEL_REPORT_TXT = "/intel_report.txt";

bool Network::writeIntelExport() {
    if (!_intelStarted) return false;
    File f = LittleFS.open(INTEL_EXPORT_JSON, "w");
    if (!f) return false;

    f.print("{\"schema\":\"cyd2-intel-export-v1\",\"exportedAt\":");
    f.print(millis() / 1000);
    f.print(",\"dashboard\":\"http://");
    f.print(WiFi.localIP().toString());
    f.print("/\"");

    f.print(",\"summary\":");
    {
        JsonDocument doc;
        _intel.toJsonSummary(doc);
        if (serializeJson(doc, f) == 0) { f.close(); return false; }
    }
    f.print(",\"wifi\":");
    {
        JsonDocument doc;
        _intel.toJsonWifi(doc);
        if (serializeJson(doc, f) == 0) { f.close(); return false; }
    }
    f.print(",\"hosts\":");
    {
        JsonDocument doc;
        _intel.toJsonHosts(doc);
        if (serializeJson(doc, f) == 0) { f.close(); return false; }
    }
    f.print(",\"profiles\":");
    {
        JsonDocument doc;
        _intel.toJsonProfiles(doc);
        if (serializeJson(doc, f) == 0) { f.close(); return false; }
    }
    f.print(",\"events\":");
    {
        JsonDocument doc;
        _intel.toJsonEvents(doc);
        if (serializeJson(doc, f) == 0) { f.close(); return false; }
    }
    f.print("}");

    f.close();
    _intel.logEvent("Intel snapshot exported");
    return true;
}

bool Network::executeControl(const String& action, const String& arg, String& message) {
    if (action == "export") {
        bool ok = writeIntelExport() && writeIntelReport();
        message = ok ? "Export and report written to flash" : "Export failed";
        return ok;
    }
    if (!_intelStarted) {
        message = "Intel not started — join a network first";
        return false;
    }
    return _intel.controlAction(action.c_str(), arg, message);
}

bool Network::writeIntelReport() {
    if (!_intelStarted) return false;
    String report;
    _intel.buildTextReport(report);
    File f = LittleFS.open(INTEL_REPORT_TXT, "w");
    if (!f) return false;
    f.print(report);
    f.close();
    return true;
}

void Network::scanNetworks(UI& ui) {
    beginWifiScan(SCAN_JOB_DEVICE_UI, &ui);
}

void Network::connectWiFi(const String& ssid, const String& pass, UI& ui) {
    beginStaJoin(ssid, pass, ui, STA_JOIN_DEVICE);
}

bool Network::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String Network::getIP() const {
    return WiFi.localIP().toString();
}

void Network::loadCredentials() {
    _savedSSID = _prefs.getString("ssid", "");
    _savedPass = _prefs.getString("pass", "");
}

void Network::saveCredentials(const String& ssid, const String& pass) {
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", pass);
    _savedSSID = ssid;
    _savedPass = pass;
}

void Network::setupLED() {
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    setLED(0, 0, 0);
}

void Network::setLED(uint8_t r, uint8_t g, uint8_t b) {
    _ledR = r; _ledG = g; _ledB = b;
    analogWrite(LED_R_PIN, 255 - r);
    analogWrite(LED_G_PIN, 255 - g);
    analogWrite(LED_B_PIN, 255 - b);
}

// Serves a LittleFS asset, preferring a pre-compressed ".gz" twin (saves ~70% of
// the transfer over the deck's AP) and optionally tagging it cacheable.
//
// We open the ".gz" file but hand the library the *clean* path (without ".gz").
// That makes AsyncFileResponse (a) auto-add Content-Encoding: gzip, and (b) emit a
// Content-Disposition filename of "index.html" instead of "index.html.gz" — iOS
// WebKit treats a ".gz" filename as a forced download, which broke rendering there.
static void sendFsFile(AsyncWebServerRequest* req, const char* path,
                       const char* contentType, bool cacheable = false) {
    String gz = String(path) + ".gz";
    bool useGz = LittleFS.exists(gz);
    String served = useGz ? gz : String(path);
    if (!LittleFS.exists(served)) {
        Serial.printf("[HTTP] missing %s\n", path);
        req->send(503, "text/plain", "Filesystem asset missing — run uploadfs");
        return;
    }
    File f = LittleFS.open(served, "r");
    AsyncWebServerResponse* res = req->beginResponse(f, String(path), contentType);
    if (cacheable) res->addHeader("Cache-Control", "public, max-age=86400");
    req->send(res);
}

// Graceful back-pressure. Building a JSON response needs a contiguous heap block;
// if the heap is too low/fragmented (e.g. mid LAN-sweep), return 503 instead of
// risking an out-of-memory panic + reboot. The dashboard retries on its next poll.
static const size_t kJsonMinBlock = 16000;
static const size_t kJsonMinFree  = 24000;
static const size_t kDashMinBlock = 9000;   // brief snapshot is ~4–8 KB
static const size_t kDashMinFree  = 18000;
static bool heapOkForJson() {
    return ESP.getMaxAllocHeap() >= kJsonMinBlock && ESP.getFreeHeap() >= kJsonMinFree;
}
static bool heapOkForDashCache() {
    return ESP.getMaxAllocHeap() >= kDashMinBlock && ESP.getFreeHeap() >= kDashMinFree;
}
static bool deckBusy(AsyncWebServerRequest* req) {
    if (heapOkForJson()) return false;
    req->send(503, "application/json", "{\"busy\":1}");
    return true;
}

// Build the dashboard poll JSON on the main loop (never on the AsyncTCP task).
// Streaming via AsyncResponseStream was crashing: cbuf::resizeAdd → failed new[] → abort().
bool Network::refreshDashCache() {
    if (!_intelStarted)
        return false;
    if (!heapOkForDashCache()) {
        // Serve the last good snapshot for a few seconds so the dashboard stays
        // responsive during brief heap dips (e.g. mid LAN-sweep).
        if (_dashAllValid && millis() - _lastDashCacheOk < 60000)
            return true;
        _dashAllValid = false;
        return false;
    }

    JsonDocument d;
    String parts[7];
    const char* keys[] = {"summary", "wifi", "hosts", "profiles", "events", "health", "control"};

    d.clear(); _intel.toJsonSummary(d);
    if (serializeJson(d, parts[0]) == 0) { _dashAllValid = false; return false; }

    d.clear(); _intel.toJsonWifiBrief(d);
    if (serializeJson(d, parts[1]) == 0) { _dashAllValid = false; return false; }

    d.clear(); _intel.toJsonHostsBrief(d);
    if (serializeJson(d, parts[2]) == 0) { _dashAllValid = false; return false; }

    d.clear(); _intel.toJsonProfilesMeta(d);
    if (serializeJson(d, parts[3]) == 0) { _dashAllValid = false; return false; }

    d.clear(); _intel.toJsonEvents(d, 12);
    if (serializeJson(d, parts[4]) == 0) { _dashAllValid = false; return false; }

    d.clear(); buildHealthJson(d);
    if (serializeJson(d, parts[5]) == 0) { _dashAllValid = false; return false; }

    d.clear(); buildControlStatusJson(d);
    if (serializeJson(d, parts[6]) == 0) { _dashAllValid = false; return false; }

    String out;
    out.reserve(4096);
    out = '{';
    for (int i = 0; i < 7; i++) {
        if (i) out += ',';
        out += '"';
        out += keys[i];
        out += "\":";
        out += parts[i];
    }
    out += '}';

    if (out.length() < 10) {
        _dashAllValid = false;
        return false;
    }

    _dashAllJson = out;
    _dashAllValid = true;
    _lastDashCacheOk = millis();
    return true;
}

void Network::buildHealthJson(JsonDocument& doc) {
    doc["ok"] = true;
    bool sta = (WiFi.status() == WL_CONNECTED);
    doc["mode"] = sta ? "station" : (_portalActive ? "portal" : "offline");
    doc["ip"] = sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["ssid"] = sta ? WiFi.SSID() : String(PORTAL_SSID);
    doc["rssi"] = sta ? WiFi.RSSI() : 0;
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    doc["fs"] = LittleFS.exists("/index.html") || LittleFS.exists("/index.html.gz");
    doc["hosts"] = _intel.hostCount();
    doc["portalActive"] = _portalActive;
    if (_portalActive) {
        doc["portalSsid"] = PORTAL_SSID;
        doc["portalUrl"] = portalUrl();
        doc["portalIp"] = WiFi.softAPIP().toString();
    }
    doc["scanPipeline"] = "priority,tcp";
}

void Network::buildControlStatusJson(JsonDocument& doc) {
    doc["ok"] = true;
    doc["phase"] = _intel.phaseName();
    doc["sweepActive"] = strcmp(_intel.phaseName(), "LAN_SWEEP") == 0;
    doc["orchestrationPaused"] = _intel.autoScanPaused();
    doc["autoProfile"] = _intel.autoProfileOnDiscovery();
    doc["hostCount"] = _intel.hostCount();
    doc["profileQueue"] = _intel.profileQueueDepth();
    doc["wifiCount"] = _intel.wifiCount();
    JsonArray actions = doc["actions"].to<JsonArray>();
    const char* list[] = {
        "lan_sweep", "rf_scan", "profile_all", "profile_host",
        "export", "clear_discovery", "clear_log", "pause", "resume",
        "cancel_sweep", "stop_profile", "auto_profile_on", "auto_profile_off"
    };
    for (const char* a : list)
        actions.add(a);
}

void Network::setupServer() {
    _server.on("/api/health", HTTP_GET, [this](AsyncWebServerRequest* req) {
        markHttpActivity();
        char buf[256];
        bool sta = (WiFi.status() == WL_CONNECTED);
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"mode\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"heap\":%u,"
                 "\"uptime\":%lu,\"fs\":true,\"hosts\":%d,\"portalActive\":%s}",
                 sta ? "station" : "offline",
                 sta ? WiFi.localIP().toString().c_str() : WiFi.softAPIP().toString().c_str(),
                 sta ? WiFi.RSSI() : 0,
                 ESP.getFreeHeap(), millis() / 1000, _intel.hostCount(),
                 _portalActive ? "true" : "false");
        req->send(200, "application/json", buf);
    });

    // Combined dashboard poll — serves a snapshot built on the main loop.
    // Never build/serialize JSON here (AsyncTCP task); that was crashing via
    // AsyncResponseStream buffer growth when heap was fragmented mid-sweep.
    _server.on("/api/intel/all", HTTP_GET, [this](AsyncWebServerRequest* req) {
        markHttpActivity();
        if (!_dashAllValid || _dashAllJson.length() == 0 ||
            millis() - _lastDashCacheOk > 60000) {
            req->send(503, "application/json", "{\"busy\":1}");
            return;
        }
        req->send(200, "application/json", _dashAllJson);
    });

    _server.on("/api/recon/all", HTTP_GET, [this](AsyncWebServerRequest* req) {
        markHttpActivity();
        if (!_reconAllValid || _reconAllJson.length() == 0 ||
            millis() - _lastReconCacheOk > 60000) {
            req->send(503, "application/json", "{\"busy\":1}");
            return;
        }
        req->send(200, "application/json", _reconAllJson);
    });

    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
        markHttpActivity();
        if (isFieldMode() &&
            (LittleFS.exists("/field.html") || LittleFS.exists("/field.html.gz"))) {
            sendFsFile(req, "/field.html", "text/html");
            return;
        }
        if (_portalActive && WiFi.status() != WL_CONNECTED &&
            (LittleFS.exists("/config.html") || LittleFS.exists("/config.html.gz"))) {
            sendFsFile(req, "/config.html", "text/html");
            return;
        }
        sendFsFile(req, "/index.html", "text/html");
    });

    _server.on("/field.html", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendFsFile(req, "/field.html", "text/html");
    });

    _server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendFsFile(req, "/config.html", "text/html");
    });

    _server.on("/app.css", HTTP_GET, [this](AsyncWebServerRequest* req) {
        markHttpActivity();
        sendFsFile(req, "/app.css", "text/css", true);
    });

    _server.on("/app.js", HTTP_GET, [this](AsyncWebServerRequest* req) {
        markHttpActivity();
        sendFsFile(req, "/app.js", "application/javascript", true);
    });

    auto sendControlJson = [](AsyncWebServerRequest* req, bool ok, const char* action,
                              const String& message, const char* phase = nullptr) {
        JsonDocument doc;
        doc["ok"] = ok;
        doc["action"] = action;
        doc["message"] = message;
        if (phase) doc["phase"] = phase;
        String out;
        serializeJson(doc, out);
        req->send(ok ? 200 : 409, "application/json", out);
    };

    _server.on("/api/control/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        markHttpActivity();
        JsonDocument doc;
        buildControlStatusJson(doc);
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    auto handleControl = [this, sendControlJson](AsyncWebServerRequest* req) {
        markHttpActivity();
        if (!req->hasParam("action")) {
            req->send(400, "application/json", "{\"ok\":false,\"message\":\"action required\"}");
            return;
        }
        String action = req->getParam("action")->value();
        action.toLowerCase();
        String arg = req->hasParam("ip") ? req->getParam("ip")->value() : "";
        String message;
        bool ok = executeControl(action, arg, message);
        sendControlJson(req, ok, action.c_str(), message, _intel.phaseName());
    };

    _server.on("/api/control", HTTP_GET, handleControl);
    _server.on("/api/control", HTTP_POST, handleControl);

    // ── Remote Station Wi‑Fi config endpoints ──
    _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (req->hasParam("refresh"))
            _pendingRescan = true;
        JsonDocument doc;
        deserializeJson(doc, _apCacheJson);
        doc["scanning"] = _pendingRescan;
        doc["ageSec"] = _lastApScan ? (millis() - _lastApScan) / 1000 : -1;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/wifi/connect", HTTP_POST, [this](AsyncWebServerRequest* req) {
        String ssid = req->hasParam("ssid", true) ? req->getParam("ssid", true)->value() : "";
        String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
        if (ssid.length() == 0) {
            req->send(400, "application/json", "{\"ok\":false,\"message\":\"ssid required\"}");
            return;
        }
        _pendingSsid = ssid;
        _pendingPass = pass;
        _pendingKeepAp = true;
        if (req->hasParam("keepAp", true)) {
            String v = req->getParam("keepAp", true)->value();
            v.toLowerCase();
            _pendingKeepAp = (v != "0" && v != "false" && v != "off");
        }
        _wifiJobState = 1;
        _pendingConnect = true;  // set last so the main loop sees a complete request
        req->send(200, "application/json", "{\"ok\":true,\"state\":\"connecting\"}");
    });

    _server.on("/api/wifi/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        JsonDocument doc;
        const char* st = "idle";
        switch (_wifiJobState) {
            case 1: st = "connecting"; break;
            case 2: st = "connected"; break;
            case 3: st = "failed"; break;
        }
        bool sta = (WiFi.status() == WL_CONNECTED);
        doc["state"] = st;
        doc["connected"] = sta;
        doc["ssid"] = sta ? WiFi.SSID() : _pendingSsid;
        doc["ip"] = sta ? WiFi.localIP().toString() : String("");
        doc["apIp"] = WiFi.softAPIP().toString();
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // On-demand detail endpoints (loaded when user drills in — not part of /all poll)
    _server.on("/api/intel/host", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (deckBusy(req)) return;
        if (!_intelStarted) {
            req->send(503, "application/json", "{\"busy\":1}");
            return;
        }
        if (deckBusy(req)) return;
        String ipStr = req->hasParam("ip") ? req->getParam("ip")->value() : "";
        JsonDocument doc;
        _intel.toJsonHost(parseIp(ipStr), doc);
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    auto handleApAction = [this](AsyncWebServerRequest* req) {
        if (!_intelStarted) {
            req->send(503, "application/json", "{\"ok\":false,\"message\":\"intel not ready\"}");
            return;
        }
        String bssid = req->hasParam("bssid") ? req->getParam("bssid")->value() : "";
        String action = req->hasParam("action") ? req->getParam("action")->value() : "";
        JsonDocument doc;
        bool ok = _intel.runApAction(action.c_str(), bssid.c_str(), doc);
        String out;
        serializeJson(doc, out);
        req->send(ok ? 200 : 400, "application/json", out);
    };

    _server.on("/api/intel/ap", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (deckBusy(req)) return;
        if (!_intelStarted) {
            req->send(503, "application/json", "{\"busy\":1}");
            return;
        }
        String bssid = req->hasParam("bssid") ? req->getParam("bssid")->value() : "";
        JsonDocument doc;
        _intel.toJsonAp(bssid.c_str(), doc);
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    _server.on("/api/intel/ap/action", HTTP_GET, handleApAction);
    _server.on("/api/intel/ap/action", HTTP_POST, handleApAction);

    _server.on("/api/intel/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!LittleFS.exists("/intel_events.log")) {
            req->send(404, "text/plain", "No event log on device yet");
            return;
        }
        req->send(LittleFS, "/intel_events.log", "text/plain");
    });

    _server.on("/api/intel/export", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (deckBusy(req)) return;
        if (req->hasParam("refresh") || !LittleFS.exists(INTEL_EXPORT_JSON))
            writeIntelExport();
        if (LittleFS.exists(INTEL_EXPORT_JSON))
            req->send(LittleFS, INTEL_EXPORT_JSON, "application/json");
        else
            req->send(503, "application/json", "{\"error\":\"export failed\"}");
    });

    _server.on("/api/intel/report", HTTP_GET, [this](AsyncWebServerRequest* req) {
        if (deckBusy(req)) return;
        if (req->hasParam("refresh") || !LittleFS.exists(INTEL_REPORT_TXT))
            writeIntelReport();
        if (LittleFS.exists(INTEL_REPORT_TXT))
            req->send(LittleFS, INTEL_REPORT_TXT, "text/plain");
        else
            req->send(503, "text/plain", "Report generation failed");
    });

    _server.onNotFound([](AsyncWebServerRequest* req) {
        Serial.printf("[HTTP] 404 %s\n", req->url().c_str());
        req->send(404, "text/plain", "Not found");
    });

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    _server.begin();
    Serial.printf("[CYBERDECK] http://%s/\n", WiFi.localIP().toString().c_str());
}

String Network::uptimeString() const {
    unsigned long sec = millis() / 1000;
    int h = (sec % 86400) / 3600;
    int m = (sec % 3600) / 60;
    char buf[24];
    snprintf(buf, sizeof(buf), "%dh %dm", h, m);
    return String(buf);
}
