#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"
#include "wifi_config.h"
#include "wifi_setup.h"
#include "core_client.h"
#include "ui.h"
#include "power.h"

static bool otaReady = false;
static bool otaBusy = false;

static void otaBeginIfNeeded() {
  if (otaReady || WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname("walldeck");
  ArduinoOTA.onStart([]() {
    otaBusy = true;
    power_network_activity();
    Serial.println(F("OTA start"));
  });
  ArduinoOTA.onEnd([]() {
    otaBusy = false;
    Serial.println(F("OTA end"));
  });
  ArduinoOTA.onError([](ota_error_t e) {
    otaBusy = false;
    Serial.printf("OTA error %u\n", e);
  });
  ArduinoOTA.begin();
  otaReady = true;
  Serial.println(F("OTA ready (espota @ walldeck.local)"));
}

#define XPT2046_IRQ TOUCH_IRQ_PIN
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Portrait (USB edge at bottom) — 240×320
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
// Smaller partial buffer frees DRAM for ArduinoOTA (~2.5KB vs /40)
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 80 * (LV_COLOR_DEPTH / 8))

TFT_eSPI tft = TFT_eSPI();
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

uint32_t draw_buf[DRAW_BUF_SIZE / 4];

CoreClient core;
AppContext appCtx;

unsigned long lastUiRefreshMs = 0;
unsigned long lastPumpMs = 0;
uint32_t lastCoreHash = 0;
bool mainAppReady = false;

void pump_ui(int times = 1) {
  for (int i = 0; i < times; i++) {
    lv_timer_handler();
    lv_tick_inc(5);
    delay(5);
  }
}

void touchscreen_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    data->state = LV_INDEV_STATE_PRESSED;
    // Portrait mapping (matches TFT rotation 0)
    data->point.x = constrain(map(p.x, 200, 3700, 0, SCREEN_WIDTH - 1), 0, SCREEN_WIDTH - 1);
    data->point.y = constrain(map(p.y, 240, 3800, 0, SCREEN_HEIGHT - 1), 0, SCREEN_HEIGHT - 1);
    power_touch_activity();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void show_connecting_saved(const char *ssid) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0d0d18), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "WiFi");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xf0f0f8), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

  lv_obj_t *msg = lv_label_create(screen);
  lv_label_set_text_fmt(msg, "Connecting to\n%s", ssid);
  lv_obj_set_style_text_color(msg, lv_color_hex(0x8888a8), 0);
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *spin = lv_spinner_create(screen);
  lv_obj_set_size(spin, 40, 40);
  lv_obj_align(spin, LV_ALIGN_CENTER, 0, 50);
}

bool try_saved_wifi() {
  if (!wifi_config_has_saved()) return false;

  String ssid, pass;
  wifi_config_load(ssid, pass);
  show_connecting_saved(ssid.c_str());
  wifi_config_connect(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (!wifi_config_is_connected() && millis() - start < 18000) {
    pump_ui(5);
  }
  return wifi_config_is_connected();
}

void on_wifi_ready() {
  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());

  otaBeginIfNeeded();

  core.begin();
  appCtx.core = &core;
  // First sync before UI so cold-start can land on last zone
  for (int i = 0; i < 3 && !core.online(); i++) {
    core.loop();
    pump_ui(2);
  }

  ui_init(&appCtx);
  mainAppReady = true;
  Serial.println("WallDeck thin client ready");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  if (power_woke_from_touch()) {
    Serial.println("Woke from deep sleep (touch)");
  } else {
    Serial.println("Overlink WallDeck starting...");
  }

  pinMode(21, OUTPUT);
  power_init();

  lv_init();

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);

  lv_display_t *disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);

  wifi_config_init();

  if (try_saved_wifi()) {
    on_wifi_ready();
  } else {
    if (wifi_config_has_saved()) {
      Serial.println("Saved WiFi failed — opening setup");
      wifi_config_clear();
    }
    wifi_setup_start(on_wifi_ready);
  }
}

void loop() {
  if (!mainAppReady) {
    wifi_setup_loop();
    otaBeginIfNeeded();
    if (otaReady) ArduinoOTA.handle();
    pump_ui(1);
    return;
  }

  otaBeginIfNeeded();
  if (otaReady) ArduinoOTA.handle();

  power_loop();
  power_try_deep_sleep(!otaBusy);

  unsigned long now = millis();
  unsigned long pumpGap = power_display_asleep() ? POWER_LOOP_IDLE_MS : 10UL;
  if (now - lastPumpMs < pumpGap) {
    delay(1);
    return;
  }
  lastPumpMs = now;

  core.loop();

  uint32_t hash = core.stateHash();
  if (power_display_asleep()) {
    lastCoreHash = hash;
  } else if (hash != lastCoreHash || now - lastUiRefreshMs > POWER_UI_REFRESH_MS) {
    lastCoreHash = hash;
    ui_refresh(&appCtx);
    lastUiRefreshMs = now;
  }

  pump_ui(1);
}
