#pragma once

#include <Arduino.h>

void power_init();
void power_touch_activity();
void power_network_activity();
void power_loop();
void power_try_deep_sleep(bool allow);
bool power_display_asleep();
bool power_woke_from_touch();

// Runtime setting (NVS). Default false — stay awake for OTA / thin-client use.
bool power_deep_sleep_enabled();
void power_set_deep_sleep(bool enabled);
