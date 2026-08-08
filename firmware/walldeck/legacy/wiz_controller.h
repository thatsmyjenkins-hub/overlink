#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "config.h"

class DeviceRegistry;

class WizController {
public:
    void begin();
    void setRegistry(DeviceRegistry *registry);
    void setAllState(bool on);
    void setAllWhite(int dimming, int tempK);
    void setAllScene(int sceneId, int dimming = 100, int speed = 100);
    void setBulbState(size_t index, bool on);
    void setBulbDimming(size_t index, int dimming);

private:
    WiFiUDP udp_;
    DeviceRegistry *registry_ = nullptr;
    static constexpr uint16_t WIZ_PORT = 38899;

    void sendIp(const IPAddress &ip, JsonDocument &doc);
    void send(size_t index, JsonDocument &doc);
    void sendAll(JsonDocument &doc);
};
