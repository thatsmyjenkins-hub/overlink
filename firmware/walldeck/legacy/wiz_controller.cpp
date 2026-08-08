#include "wiz_controller.h"
#include "device_registry.h"
#include "power.h"

void WizController::begin() {
    udp_.begin(0);
}

void WizController::setRegistry(DeviceRegistry *registry) {
    registry_ = registry;
}

void WizController::sendIp(const IPAddress &ip, JsonDocument &doc) {
    if (ip == INADDR_NONE) return;
    power_network_activity();

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    if (!udp_.beginPacket(ip, WIZ_PORT)) {
        Serial.println("WiZ: beginPacket failed");
        return;
    }
    udp_.write(reinterpret_cast<const uint8_t *>(payload), strlen(payload));
    udp_.endPacket();
}

void WizController::send(size_t index, JsonDocument &doc) {
    if (!registry_ || index >= WIZ_BULB_COUNT) return;
    sendIp(registry_->wizIp(index), doc);
}

void WizController::sendAll(JsonDocument &doc) {
    if (!registry_) return;
    for (size_t i = 0; i < WIZ_BULB_COUNT; i++) {
        if (registry_->wizFound(i)) {
            send(i, doc);
            delay(20);
        }
    }
}

void WizController::setAllState(bool on) {
    StaticJsonDocument<128> doc;
    doc["method"] = "setPilot";
    doc["params"]["state"] = on;
    sendAll(doc);
}

void WizController::setAllWhite(int dimming, int tempK) {
    StaticJsonDocument<128> doc;
    doc["method"] = "setPilot";
    JsonObject params = doc["params"].to<JsonObject>();
    params["state"] = true;
    params["dimming"] = constrain(dimming, 10, 100);
    params["temp"] = constrain(tempK, 2200, 6500);
    sendAll(doc);
}

void WizController::setAllScene(int sceneId, int dimming, int speed) {
    StaticJsonDocument<128> doc;
    doc["method"] = "setPilot";
    JsonObject params = doc["params"].to<JsonObject>();
    params["state"] = true;
    params["sceneId"] = sceneId;
    params["dimming"] = constrain(dimming, 10, 100);
    params["speed"] = constrain(speed, 20, 200);
    sendAll(doc);
}

void WizController::setBulbState(size_t index, bool on) {
    StaticJsonDocument<128> doc;
    doc["method"] = "setPilot";
    doc["params"]["state"] = on;
    send(index, doc);
}

void WizController::setBulbDimming(size_t index, int dimming) {
    StaticJsonDocument<128> doc;
    doc["method"] = "setPilot";
    JsonObject params = doc["params"].to<JsonObject>();
    params["state"] = true;
    params["dimming"] = constrain(dimming, 10, 100);
    params["temp"] = 3000;
    send(index, doc);
}
