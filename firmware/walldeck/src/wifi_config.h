#pragma once

#include <Arduino.h>

void wifi_config_init();
bool wifi_config_has_saved();
bool wifi_config_load(String &ssid, String &password);
void wifi_config_save(const char *ssid, const char *password);
void wifi_config_clear();
bool wifi_config_connect(const char *ssid, const char *password);
bool wifi_config_is_connected();
