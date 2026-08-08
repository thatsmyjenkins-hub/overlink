#include "power.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>

#define BACKLIGHT_PIN 21
#define BACKLIGHT_LEDC_CH 0

static unsigned long lastTouchMs = 0;
static unsigned long lastNetworkActiveMs = 0;
static uint8_t backlightLevel = BACKLIGHT_BRIGHT;
static bool displayAsleep = false;
static bool deepSleepEnabled = false;  // default OFF

static void set_backlight(uint8_t level) {
  if (level == backlightLevel) return;
  backlightLevel = level;
  displayAsleep = (level == 0);
  if (BACKLIGHT_PWM) {
    ledcWrite(BACKLIGHT_PIN, level);
  } else if (level == 0) {
    digitalWrite(BACKLIGHT_PIN, LOW);
  } else {
    digitalWrite(BACKLIGHT_PIN, HIGH);
  }
}

static void apply_wifi_sleep_policy() {
  unsigned long now = millis();
  bool active = (now - lastTouchMs < POWER_WIFI_ACTIVE_MS) ||
                (now - lastNetworkActiveMs < POWER_WIFI_ACTIVE_MS);
  // When deep sleep is disabled, keep Wi‑Fi fully awake for OTA
  if (!deepSleepEnabled) {
    WiFi.setSleep(WIFI_PS_NONE);
    return;
  }
  WiFi.setSleep(active ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
}

static void apply_cpu_speed() {
#if POWER_CPU_SCALE
  if (displayAsleep) {
    setCpuFrequencyMhz(POWER_CPU_MHZ_IDLE);
  } else {
    setCpuFrequencyMhz(POWER_CPU_MHZ_ACTIVE);
  }
#endif
}

static void enter_deep_sleep() {
  set_backlight(0);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // XPT2046 TIRQ: HIGH idle, LOW when touched
  pinMode(TOUCH_IRQ_PIN, INPUT);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_IRQ_PIN, 0);

  Serial.println("Deep sleep — touch to wake");
  Serial.flush();
  delay(10);

  esp_deep_sleep_start();
}

bool power_deep_sleep_enabled() { return deepSleepEnabled; }

void power_set_deep_sleep(bool enabled) {
  deepSleepEnabled = enabled;
  Preferences p;
  p.begin("walldeck", false);
  p.putBool("deep_sleep", enabled);
  p.end();
  Serial.printf("Deep sleep %s\n", enabled ? "ON" : "OFF");
  apply_wifi_sleep_policy();
}

void power_init() {
  lastTouchMs = millis();
  lastNetworkActiveMs = millis();

  Preferences p;
  p.begin("walldeck", true);
  // Default OFF if key missing — never brick OTA by surprise sleep
  deepSleepEnabled = p.getBool("deep_sleep", false);
  p.end();
  Serial.printf("Deep sleep setting: %s\n", deepSleepEnabled ? "ON" : "OFF");

  pinMode(BACKLIGHT_PIN, OUTPUT);
  if (BACKLIGHT_PWM) {
    // Arduino-ESP32 3.x LEDC API (pin-based)
    ledcAttach(BACKLIGHT_PIN, 5000, 8);
    ledcWrite(BACKLIGHT_PIN, BACKLIGHT_BRIGHT);
  } else {
    digitalWrite(BACKLIGHT_PIN, HIGH);
  }
  displayAsleep = false;
  backlightLevel = BACKLIGHT_BRIGHT;

  apply_wifi_sleep_policy();
  apply_cpu_speed();
}

void power_touch_activity() {
  lastTouchMs = millis();
  if (backlightLevel != BACKLIGHT_BRIGHT) {
    set_backlight(BACKLIGHT_BRIGHT);
    apply_cpu_speed();
  }
  apply_wifi_sleep_policy();
}

void power_network_activity() {
  lastNetworkActiveMs = millis();
  apply_wifi_sleep_policy();
}

void power_loop() {
  unsigned long idle = millis() - lastTouchMs;

  if (idle >= BACKLIGHT_OFF_MS) {
    set_backlight(0);
  } else if (idle >= BACKLIGHT_DIM_MS) {
    set_backlight(BACKLIGHT_DIM);
  } else if (backlightLevel != BACKLIGHT_BRIGHT) {
    set_backlight(BACKLIGHT_BRIGHT);
  }

  apply_wifi_sleep_policy();
  apply_cpu_speed();
}

void power_try_deep_sleep(bool allow) {
#if !POWER_DEEP_SLEEP_ENABLE
  (void)allow;
  return;
#else
  if (!allow || !deepSleepEnabled) return;

  unsigned long now = millis();
  if (now - lastTouchMs < DEEP_SLEEP_IDLE_MS) return;
  if (now - lastNetworkActiveMs < POWER_WIFI_ACTIVE_MS) return;

  enter_deep_sleep();
#endif
}

bool power_display_asleep() { return displayAsleep; }

bool power_woke_from_touch() {
#if POWER_DEEP_SLEEP_ENABLE
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
#else
  return false;
#endif
}
