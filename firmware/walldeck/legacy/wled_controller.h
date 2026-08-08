#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "config.h"

class DeviceRegistry;

class WledController {
public:
    void begin();
    void setRegistry(DeviceRegistry *registry);
    void setPower(bool on);
    void setNormal();
    void setParty();
    void setNightPulse();
    void setBrightness(int bri);
    void setSolid(uint8_t r, uint8_t g, uint8_t b, int bri);
    void setEffect(int fx, int speed, int intensity, int brightness);

private:
    WiFiClient client_;
    DeviceRegistry *registry_ = nullptr;
    void postState(JsonDocument &doc);
};
