#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "ui.h"
#include "network.h"
UI       ui;
Network  net;

static bool g_roamListBle = false;  // roaming list shows BLE devices vs Wi-Fi APs

static const char* resetReasonStr(esp_reset_reason_t r) {
    switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN";
    case ESP_RST_SW:        return "SW_RESTART";
    case ESP_RST_PANIC:     return "PANIC/CRASH";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT(power)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== CYBERDECK NET INTEL v2.0 ===");
    Serial.printf("[BOOT] reset=%s heap=%u min=%u maxblk=%u\n",
                  resetReasonStr(esp_reset_reason()),
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

    ui.onScanRequest([&]() { net.scanNetworks(ui); });
    ui.onConnectRequest([&](const String& ssid, const String& pass) {
        net.connectWiFi(ssid, pass, ui);
    });
    ui.onReconnect([&]() { net.reconnectSaved(ui); });
    ui.onForgetWifi([&]() {
        net.forgetCredentials();
        net.startSetupPortal(ui);
        ui.setScreen(SCREEN_WIFI_SCAN);
        net.scanNetworks(ui);
    });
    ui.onShareIntel([&]() {
        if (net.writeIntelExport() && net.writeIntelReport())
            ui.setShareStatus("Snapshot saved to flash");
        else
            ui.setShareStatus("Export failed");
    });
    ui.onPostCal([&]() { net.finishBootFlow(ui); });
    ui.onWifiScanBack([&]() { net.returnToModePicker(ui); });
    ui.onNetMode([&](int mode, bool askEveryBoot) {
        net.saveNetPickerPref(askEveryBoot, (NetMode)mode);
        net.applyNetMode((NetMode)mode, ui);
    });
    ui.onRoamCapture([&]() { net.toggleCapture(); });
    ui.onRoamArm([&]() { net.recon().setAuthorized(!net.recon().authorized()); });
    ui.onRoamBle([&]() { net.toggleBle(); });
    ui.onRoamListToggle([&]() { g_roamListBle = !g_roamListBle; });
    ui.onRoamEngage([&](int idx) {
        char ss[24];
        if (net.recon().deauthSortedIndex(idx, ss, sizeof(ss)))
            ui.showToast(String("Deauth -> ") + ss, COLOR_DANGER);
    });

#if !TOUCH_DIAG_BOOT
    ui.begin();
    net.begin(ui);
    if (ui.hasCalibration())
        net.finishBootFlow(ui);
#endif
}

void loop() {
    ui.loop();
#if !TOUCH_DIAG_BOOT
    static unsigned long lastHeapLog = 0;
    if (millis() - lastHeapLog > 5000) {
        lastHeapLog = millis();
        Serial.printf("[HEAP] free=%u min=%u maxblk=%u\n",
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    }
    net.poll(ui);
    net.loop();

    if (net.isRoamingMode()) {
        static unsigned long lastRoam = 0;
        if (millis() - lastRoam > 700) {
            lastRoam = millis();
            auto& rec = net.recon();
            auto& ble = net.ble();
            char buf[8][32];
            const char* lines[8];
            int n = 0;
            for (int i = 0; i < 8; i++) {
                bool ok = g_roamListBle ? ble.getLine(i, buf[i], sizeof(buf[i]))
                                        : rec.getApLine(i, buf[i], sizeof(buf[i]));
                if (!ok) break;
                lines[n++] = buf[i];
            }
            bool cap = net.isCapturing();
            bool armed = rec.authorized();
            ui.setRoamingView(rec.apCount(), rec.clientCount(), rec.probeCount(),
                              rec.handshakeCount(), ble.deviceCount(), rec.channel(),
                              rec.frameCount(), cap, armed, net.bleRunning(),
                              g_roamListBle, lines, n);
            int hs = rec.handshakeCount();
            if (armed)
                net.setLED(80, 0, 0);          // armed: solid red (active engage)
            else if (cap)
                net.setLED(0, 50, 0);          // capturing: green
            else
                net.setLED(hs > 0 ? 60 : 20, 0, hs > 0 ? 0 : 35);  // recon: red/blue
        }
    } else if (net.intelShouldRun()) {
        if (net.isConnected())
            ui.updateLinkState(true, net.getIP(), WiFi.SSID());
        static unsigned long lastSync = 0;
        if (millis() - lastSync > 1500) {
            lastSync = millis();
            auto& intel = net.intel();
            char activity[64];
            intel.formatActivityLine(activity, sizeof(activity));
            ui.setIntelStatus(intel.hostCount(), intel.wifiCount(),
                              intel.phaseName(), intel.scanProgress(), activity);

            const char* hostLines[4];
            char hostBuf[4][28];
            int hn = min(intel.hostCount(), 4);
            for (int i = 0; i < hn; i++) {
                char ip[16], ven[16], typ[12];
                uint8_t risk, ports;
                if (intel.getHostBrief(i, ip, sizeof(ip), ven, sizeof(ven),
                                       typ, sizeof(typ), &risk, &ports)) {
                    snprintf(hostBuf[i], sizeof(hostBuf[i]), "%s %s r%u",
                             ip, typ, (unsigned)risk);
                    hostLines[i] = hostBuf[i];
                }
            }
            const char* wifiLines[3];
            char wifiBuf[3][28];
            int wn = min(intel.wifiCount(), 3);
            for (int i = 0; i < wn; i++) {
                char ssid[20];
                int8_t rssi;
                uint8_t enc;
                if (intel.getWifiBrief(i, ssid, sizeof(ssid), &rssi, &enc)) {
                    const char* e = enc == 0 ? "OPEN" : "enc";
                    snprintf(wifiBuf[i], sizeof(wifiBuf[i]), "%s %ddBm", ssid, (int)rssi);
                    wifiLines[i] = wifiBuf[i];
                }
            }
            ui.setPreviewRows(hostLines, hn, wifiLines, wn);

            static bool scanLed = false;
            if (strcmp(intel.phaseName(), "LAN_SWEEP") == 0 ||
                strcmp(intel.phaseName(), "RF_SCAN") == 0) {
                scanLed = !scanLed;
                net.setLED(0, scanLed ? 50 : 10, scanLed ? 70 : 20);
            }
        }
    } else {
        ui.setConnectionState(false);
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 2000) {
            lastBlink = millis();
            static bool on = false;
            on = !on;
            ui.setLEDColor(on ? 40 : 0, 0, 0);
        }
    }
#endif
}
