#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class DeviceRegistry;

// Full-permission HTTP client for CyberDeck IR/RF peer.
class CyberDeckController {
public:
    void begin();
    void setRegistry(DeviceRegistry *registry);

    bool found() const;
    IPAddress ip() const;

    bool irVizio(const char *action);
    bool irSendNec(uint32_t code, uint8_t frames = 12);
    bool irReplayLast();
    bool irSave(const char *name = nullptr);

    bool rfSetFreq(float mhz);
    bool rfSniff(bool on);
    bool rfReplayLast();
    bool rfTxHex(const char *hex, float mhz = 0);
    bool rfSave(const char *name = nullptr);
    bool rfTest();

    bool vaultReplay(int id);
    bool getStatus(JsonDocument &out);

private:
    DeviceRegistry *registry_ = nullptr;

    bool postJson(const char *path, const char *json, String *message = nullptr);
    bool getJson(const char *path, JsonDocument &out);
    String baseUrl() const;
};
