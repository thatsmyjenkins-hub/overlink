#include "wifi_config.h"
#include <Preferences.h>
#include <WiFi.h>

static Preferences wifiPrefs;

void wifi_config_init() {
    wifiPrefs.begin("wifi", false);
}

bool wifi_config_has_saved() {
    return wifiPrefs.getString("ssid", "").length() > 0;
}

bool wifi_config_load(String &ssid, String &password) {
    ssid = wifiPrefs.getString("ssid", "");
    password = wifiPrefs.getString("pass", "");
    return ssid.length() > 0;
}

void wifi_config_save(const char *ssid, const char *password) {
    wifiPrefs.putString("ssid", ssid);
    wifiPrefs.putString("pass", password);
}

void wifi_config_clear() {
    wifiPrefs.remove("ssid");
    wifiPrefs.remove("pass");
}

bool wifi_config_connect(const char *ssid, const char *password) {
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
    WiFi.begin(ssid, password);
    return true;
}

bool wifi_config_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}
