#include "cyberdeck_controller.h"
#include "device_registry.h"
#include "power.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

void CyberDeckController::begin() {}

void CyberDeckController::setRegistry(DeviceRegistry *registry) {
    registry_ = registry;
}

bool CyberDeckController::found() const {
    return registry_ && registry_->cyberdeckFound();
}

IPAddress CyberDeckController::ip() const {
    if (!registry_) return INADDR_NONE;
    return registry_->cyberdeckIp();
}

String CyberDeckController::baseUrl() const {
    return String("http://") + ip().toString();
}

bool CyberDeckController::postJson(const char *path, const char *json, String *message) {
    if (!found() || !json) return false;
    power_network_activity();

    WiFiClient client;
    HTTPClient http;
    String url = baseUrl() + path;
    http.setTimeout(5000);
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(json);
    if (code <= 0) {
        Serial.printf("CyberDeck POST %s failed: %s\n", path, http.errorToString(code).c_str());
        http.end();
        return false;
    }
    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (!deserializeJson(doc, resp)) {
        if (message) *message = doc["message"] | "";
        return doc["ok"] | (code >= 200 && code < 300);
    }
    return code >= 200 && code < 300;
}

bool CyberDeckController::getJson(const char *path, JsonDocument &out) {
    if (!found()) return false;
    power_network_activity();

    WiFiClient client;
    HTTPClient http;
    String url = baseUrl() + path;
    http.setTimeout(4000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }
    DeserializationError err = deserializeJson(out, http.getStream());
    http.end();
    return !err;
}

bool CyberDeckController::irVizio(const char *action) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"action\":\"%s\"}", action ? action : "");
    return postJson("/api/ir/send", buf);
}

bool CyberDeckController::irSendNec(uint32_t code, uint8_t frames) {
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"nec\":\"0x%08lX\",\"frames\":%u}",
             (unsigned long)code, (unsigned)frames);
    return postJson("/api/ir/send", buf);
}

bool CyberDeckController::irReplayLast() {
    return postJson("/api/ir/replay", "{}");
}

bool CyberDeckController::irSave(const char *name) {
    if (name && name[0]) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"name\":\"%s\"}", name);
        return postJson("/api/ir/save", buf);
    }
    return postJson("/api/ir/save", "{}");
}

bool CyberDeckController::rfSetFreq(float mhz) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"mhz\":%.3f}", mhz);
    return postJson("/api/rf/freq", buf);
}

bool CyberDeckController::rfSniff(bool on) {
    return postJson("/api/rf/sniff", on ? "{\"on\":true}" : "{\"on\":false}");
}

bool CyberDeckController::rfReplayLast() {
    return postJson("/api/rf/replay", "{}");
}

bool CyberDeckController::rfTxHex(const char *hex, float mhz) {
    char buf[256];
    if (mhz > 0) {
        snprintf(buf, sizeof(buf), "{\"hex\":\"%s\",\"mhz\":%.3f}", hex ? hex : "", mhz);
    } else {
        snprintf(buf, sizeof(buf), "{\"hex\":\"%s\"}", hex ? hex : "");
    }
    return postJson("/api/rf/tx", buf);
}

bool CyberDeckController::rfSave(const char *name) {
    if (name && name[0]) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"name\":\"%s\"}", name);
        return postJson("/api/rf/save", buf);
    }
    return postJson("/api/rf/save", "{}");
}

bool CyberDeckController::rfTest() {
    return postJson("/api/rf/test", "{}");
}

bool CyberDeckController::vaultReplay(int id) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"id\":%d}", id);
    return postJson("/api/vault/replay", buf);
}

bool CyberDeckController::getStatus(JsonDocument &out) {
    return getJson("/api/status", out);
}
