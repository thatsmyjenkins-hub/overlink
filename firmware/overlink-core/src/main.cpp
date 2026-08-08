#include <Arduino.h>
#include <math.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <FS.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_NeoPixel.h>

#include "board_pins.h"
#include "seed_home.h"
#include "device_hub.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "automation_engine.h"
#include "grideye_lite.h"
#include "connector_store.h"
#include "grid_store.h"
#include "party_tricks.h"
#include "relay_client.h"

static SPIClass lcdSPI(FSPI);
static Adafruit_ST7789 tft(&lcdSPI, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);
static Adafruit_NeoPixel rgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);

static bool sdOk = false;
static bool seedOk = false;
static bool otaReady = false;
static bool otaBusy = false;
static uint64_t sdBytes = 0;
static String sdLabel = "NO CARD";
static String statusLine = "booting";
static uint32_t lastUiMs = 0;

static bool writeTextFile(const char *path, const char *progmemJson) {
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SEED] open fail %s\n", path);
    return false;
  }
  String body = FPSTR(progmemJson);
  size_t n = f.print(body);
  f.close();
  Serial.printf("[SEED] wrote %s (%u bytes)\n", path, (unsigned)n);
  return n > 0;
}

static bool ensureSeedHome() {
  if (!SD_MMC.exists("/homes")) SD_MMC.mkdir("/homes");
  if (!SD_MMC.exists("/homes/index.json")) {
    writeTextFile("/homes/index.json", SEED_INDEX);
  }
  if (!SD_MMC.exists("/homes/home")) SD_MMC.mkdir("/homes/home");

  bool wrote = false;
  auto maybe = [&](const char *path, const char *json) {
    if (!SD_MMC.exists(path)) {
      writeTextFile(path, json);
      wrote = true;
    }
  };
  maybe("/homes/home/home.json", SEED_HOME);
  maybe("/homes/home/rooms.json", SEED_ROOMS);
  maybe("/homes/home/devices.json", SEED_DEVICES);
  maybe("/homes/home/scenes.json", SEED_SCENES);
  maybe("/homes/home/automations.json", SEED_AUTOMATIONS);

  bool ok = SD_MMC.exists("/homes/home/devices.json") &&
            SD_MMC.exists("/homes/index.json");
  if (ok) {
    Serial.println(wrote ? "[SEED] Home Sprawl planted on TF"
                         : "[SEED] Home Sprawl already on TF");
  }
  return ok;
}

static void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

// CTRL palette on 172×320 deckface (fits width; short labels only)
static const uint16_t kTeal = 0x3BE2;    // ~#3DDC97
static const uint16_t kAmber = 0xF540;   // ~#F0A030
static const uint16_t kActive = 0xEF55;  // ~#E8F5A0
static const uint16_t kDim = 0x7BEF;

static void drawScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  tft.setTextColor(kTeal);
  tft.setTextSize(2);
  tft.setCursor(4, 6);
  tft.println("OVERLINK");

  tft.setTextSize(1);
  tft.setTextColor(kAmber);
  tft.setCursor(4, 30);
  tft.println("CORE // CTRL");

  tft.setTextColor(kActive);
  tft.setCursor(4, 52);
  tft.println(statusLine);

  tft.setCursor(4, 72);
  tft.setTextColor(kDim);
  tft.print("TF ");
  tft.setTextColor(sdOk ? kTeal : ST77XX_RED);
  tft.println(sdLabel);

  if (sdOk) {
    tft.setTextColor(kDim);
    tft.setCursor(4, 90);
    tft.printf("%.1fGB", sdBytes / (1024.0f * 1024.0f * 1024.0f));
    tft.setCursor(4, 106);
    tft.setTextColor(seedOk ? kTeal : kAmber);
    tft.println(seedOk ? "Home Sprawl" : "Grid pending");
  }

  tft.setTextColor(kDim);
  tft.setCursor(4, 140);
  tft.printf("WiFi %s", wifiModeLabel().c_str());
  tft.setCursor(4, 156);
  if (wifiStaUp()) {
    tft.setTextColor(kTeal);
    tft.println(wifiStaSsid());
    tft.setTextColor(kActive);
    tft.setCursor(4, 172);
    tft.println(wifiStaIp());
  } else {
    tft.setTextColor(kAmber);
    tft.println("SoftAP setup");
    tft.setTextColor(kActive);
    tft.setCursor(4, 172);
    tft.println(wifiApSsid());
    tft.setCursor(4, 188);
    tft.println("192.168.44.1");
  }

  tft.setTextColor(kDim);
  tft.setCursor(4, 300);
  tft.println("overlink.local");
}

static void otaBeginIfNeeded() {
  if (otaReady || !wifiStaUp()) return;
  ArduinoOTA.setHostname("overlink");
  ArduinoOTA.onStart([]() {
    otaBusy = true;
    Serial.println(F("OTA start"));
    statusLine = "OTA flashing";
    setRgb(180, 100, 0);
    drawScreen();
  });
  ArduinoOTA.onEnd([]() {
    otaBusy = false;
    Serial.println(F("OTA end"));
    statusLine = "OTA done";
    drawScreen();
  });
  ArduinoOTA.onError([](ota_error_t e) {
    otaBusy = false;
    Serial.printf("OTA error %u\n", e);
  });
  ArduinoOTA.begin();
  otaReady = true;
  Serial.println(F("OTA ready (espota @ overlink.local / 192.168.4.55)"));
}

static bool mountSd() {
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2,
                      PIN_SD_D3)) {
    Serial.println("[SD] setPins failed");
    return false;
  }
  if (!SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("[SD] begin failed");
    return false;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) return false;

  const char *type = "UNKNOWN";
  if (cardType == CARD_MMC) type = "MMC";
  else if (cardType == CARD_SD) type = "SD";
  else if (cardType == CARD_SDHC) type = "SDHC";

  sdBytes = SD_MMC.cardSize();
  sdLabel = String(type) + " OK";
  Serial.printf("[SD] type=%s size=%.2f GB\n", type,
                sdBytes / (1024.0 * 1024.0 * 1024.0));
  seedOk = ensureSeedHome();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== OVERLINK CORE ===");
  Serial.printf("chip=%s flash=%uMB psram=%uKB\n", ESP.getChipModel(),
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned)(ESP.getPsramSize() / 1024));

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  rgb.begin();
  rgb.setBrightness(48);
  setRgb(0, 40, 80);

  statusLine = "mounting TF";
  sdOk = mountSd();

  lcdSPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, PIN_LCD_CS);
  tft.init(LCD_WIDTH, LCD_HEIGHT);
  tft.setRotation(0);

  statusLine = sdOk ? "TF uplink OK" : "TF FAIL";
  drawScreen();

  webPortalSetSd(sdOk, sdBytes / (1024.0f * 1024.0f * 1024.0f), seedOk);

  statusLine = "raising SoftAP";
  drawScreen();
  wifiManagerBegin();
  gridStoreBegin();
  webPortalBegin();

  if (wifiStaUp()) {
    statusLine = "grid online";
    setRgb(0, 140, 60);
  } else {
    statusLine = "join Overlink-Setup";
    setRgb(0, 80, 160);
  }
  otaBeginIfNeeded();
  drawScreen();
  Serial.println("[BOOT] ready");
}

void loop() {
  wifiManagerLoop();
  otaBeginIfNeeded();
  if (otaReady) ArduinoOTA.handle();
  webPortalLoop();
  if (wifiStaUp()) {
    static bool servicesUp = false;
    if (!servicesUp) {
      deviceHubBegin();
      connectorStoreBegin();
      relayClientBegin();
      partyTricksBegin();
      automationBegin();
      grideyeLiteBegin();
      servicesUp = true;
    }
    deviceHubLoop();
    relayClientLoop();
    partyTricksLoop();
    automationLoop();
    grideyeLiteLoop();
  }

  // RGB pulse: cyan in setup, green when STA (hold amber during OTA)
  static uint32_t t0 = 0;
  if (!otaBusy && millis() - t0 > 40) {
    t0 = millis();
    static float phase = 0;
    phase += 0.08f;
    uint8_t v = (uint8_t)(24 + 50 * (sinf(phase) * 0.5f + 0.5f));
    if (wifiStaUp()) setRgb(0, v, (uint8_t)(v / 2));
    else setRgb(0, (uint8_t)(v / 2), v);
  }

  if (!otaBusy && millis() - lastUiMs > 3000) {
    lastUiMs = millis();
    if (wifiStaUp()) statusLine = otaReady ? "grid online / OTA" : "grid online";
    else statusLine = "join Overlink-Setup";
    drawScreen();
  }
}
