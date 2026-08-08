/**
 * CyberDeck — ESP32 IR + CC1101 controller (self-hosted Web UI)
 *
 * Access (no UI password):
 *   SoftAP (open): CyberDeck-IRRF → http://192.168.44.1
 *   After Wi‑Fi join: http://cyberdeck.local  (SoftAP kept as backup)
 *
 * Pins: IR RX=14 TX=4 | CC1101 CS=5 SCK=18 MISO=19 MOSI=23 GDO0=26 GDO2=27
 * Power: JUMP=3.3V; IR LED transistor from dedicated 5V; CC1101 on 3.3V only
 */

#include <Arduino.h>
#include <ArduinoOTA.h>
#include "ir_ctrl.h"
#include "rf_ctrl.h"
#include "vault.h"
#include "web_ui.h"
#include "wifi_net.h"

static bool otaReady = false;

static void otaBeginIfNeeded() {
  if (otaReady || !wifiStaConnected()) return;
  ArduinoOTA.setHostname("cyberdeck");
  ArduinoOTA.onStart([]() { Serial.println(F("OTA start")); });
  ArduinoOTA.onEnd([]() { Serial.println(F("OTA end")); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %u\n", e); });
  ArduinoOTA.begin();
  otaReady = true;
  Serial.println(F("OTA ready (espota @ cyberdeck.local)"));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== CyberDeck IR/RF ==="));

  irBegin();
  vaultBegin();
  rfBegin();
  webBegin();
  otaBeginIfNeeded();

  Serial.print(F("SoftAP: "));
  Serial.println(webApSsid());
  Serial.print(F("Open UI: "));
  Serial.println(webPrimaryUrl());
  Serial.println(F("Serial keys: m/u/d/p/q/h"));
}

void loop() {
  wifiNetLoop();
  otaBeginIfNeeded();
  if (otaReady) ArduinoOTA.handle();
  irLoop();
  rfLoop();
  webLoop();

  if (Serial.available()) {
    char c = Serial.read();
    while (Serial.available()) Serial.read();
    String detail;
    switch (c) {
      case 'm':
      case 'M':
        irSendVizio("MUTE", detail);
        Serial.println(detail);
        break;
      case 'u':
      case 'U':
        irSendVizio("VOL+", detail);
        Serial.println(detail);
        break;
      case 'd':
      case 'D':
        irSendVizio("VOL-", detail);
        Serial.println(detail);
        break;
      case 'p':
      case 'P':
        irSendVizio("POWER", detail);
        Serial.println(detail);
        break;
      case 'q':
      case 'Q':
        Serial.println(irLoopbackQa(detail) ? detail : detail);
        break;
      case 'h':
      case 'H':
        Serial.print(F("UI: "));
        Serial.println(webPrimaryUrl());
        Serial.println(F("SoftAP backup: CyberDeck-IRRF (open) → 192.168.44.1"));
        break;
      default:
        break;
    }
  }
}
