#include "wled_controller.h"
#include "device_registry.h"
#include "power.h"

void WledController::begin() {}

void WledController::setRegistry(DeviceRegistry *registry) {
    registry_ = registry;
}

void WledController::postState(JsonDocument &doc) {
    if (!registry_ || !registry_->wledFound()) return;
    power_network_activity();

    char payload[384];
    serializeJson(doc, payload, sizeof(payload));

    client_.stop();
    HTTPClient http;
    String url = String("http://") + registry_->wledIp().toString() + "/json/state";
    http.setTimeout(3000);
    http.begin(client_, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    if (code <= 0) {
        Serial.printf("WLED POST failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();
}

void WledController::setPower(bool on) {
    StaticJsonDocument<64> doc;
    doc["on"] = on;
    postState(doc);
}

void WledController::setNormal() {
    StaticJsonDocument<256> doc;
    doc["on"] = true;
    doc["bri"] = 200;
    JsonObject seg = doc["seg"].add<JsonObject>();
    seg["fx"] = 0;
    JsonArray col = seg["col"].to<JsonArray>();
    JsonArray rgb = col.add<JsonArray>();
    rgb.add(255);
    rgb.add(200);
    rgb.add(140);
    postState(doc);
}

void WledController::setParty() {
    setEffect(WLED_FX_RAINBOW, WLED_PARTY_SPEED, 128, WLED_PARTY_BRIGHTNESS);
}

void WledController::setNightPulse() {
    setEffect(WLED_FX_BREATHE, WLED_NIGHT_SPEED, WLED_NIGHT_INTENSITY, WLED_NIGHT_BRIGHTNESS);
}

void WledController::setSolid(uint8_t r, uint8_t g, uint8_t b, int bri) {
    StaticJsonDocument<256> doc;
    doc["on"] = true;
    doc["bri"] = constrain(bri, 1, 255);
    JsonObject seg = doc["seg"].add<JsonObject>();
    seg["fx"] = 0;
    JsonArray col = seg["col"].to<JsonArray>();
    JsonArray rgb = col.add<JsonArray>();
    rgb.add(r);
    rgb.add(g);
    rgb.add(b);
    postState(doc);
}

void WledController::setBrightness(int bri) {
    StaticJsonDocument<64> doc;
    doc["on"] = true;
    doc["bri"] = constrain(bri, 1, 255);
    postState(doc);
}

void WledController::setEffect(int fx, int speed, int intensity, int brightness) {
    StaticJsonDocument<256> doc;
    doc["on"] = true;
    doc["bri"] = constrain(brightness, 1, 255);
    JsonObject seg = doc["seg"].add<JsonObject>();
    seg["fx"] = fx;
    seg["sx"] = constrain(speed, 0, 255);
    seg["ix"] = constrain(intensity, 0, 255);
    postState(doc);
}
