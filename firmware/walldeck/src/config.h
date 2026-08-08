#pragma once

#include <Arduino.h>

// Overlink Core — WallDeck is a thin client (no direct WiZ/WLED/Deck I/O)
#define CORE_HOSTNAME "overlink"
#define CORE_FALLBACK_IP "192.168.4.55"
#define CORE_PORT 80
#define CORE_POLL_MS 4000UL
#define CORE_OFFLINE_FAILS 2

// Battery / power — deep sleep is opt-in via Settings (default OFF for OTA)
#define TOUCH_IRQ_PIN 36
#define POWER_DEEP_SLEEP_ENABLE 1  // compile-time feature; runtime default off
#define BACKLIGHT_PWM 1
#define BACKLIGHT_BRIGHT 255
#define BACKLIGHT_DIM 48
#define BACKLIGHT_DIM_MS (20UL * 1000UL)
#define BACKLIGHT_OFF_MS (50UL * 1000UL)
#define DEEP_SLEEP_IDLE_MS (70UL * 1000UL)
#define POWER_WIFI_ACTIVE_MS 4000UL
#define POWER_CPU_SCALE 1
#define POWER_CPU_MHZ_ACTIVE 240
#define POWER_CPU_MHZ_IDLE 80
#define POWER_UI_REFRESH_MS 500UL
#define POWER_LOOP_IDLE_MS 100UL

// Layout denser — match BasementController touch targets
#define UI_TOUCH_MIN 40
#define UI_STATUS_H 22
#define UI_LOG_H 64
#define UI_SCENE_MIN_H 48
#define UI_CARD_H 64
#define UI_TOGGLE_SZ 36
#define UI_TOOL_H 36
#define UI_TAB_H 30

#define MAX_ZONES 8
#define MAX_DEVICES 12
#define MAX_SCENES 11
#define UI_THEME_H 34
#define LOG_LINES 4
#define LOG_LINE_LEN 40
