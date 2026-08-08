#include "ble_scan.h"
#include <NimBLEDevice.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

struct BleRec {
    uint8_t addr[6];
    int8_t  rssi;
    uint8_t addrType;
    bool    connectable;
    char    name[24];
};

static QueueHandle_t s_bleQueue = nullptr;

class AdvCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* d) override {
        if (!s_bleQueue) return;
        BleRec r;
        memset(&r, 0, sizeof(r));
        const uint8_t* a = d->getAddress().getNative();
        memcpy(r.addr, a, 6);
        r.rssi = d->getRSSI();
        r.addrType = d->getAddress().getType();
        r.connectable = d->isConnectable();
        if (d->haveName()) {
            std::string n = d->getName();
            strncpy(r.name, n.c_str(), sizeof(r.name) - 1);
        }
        xQueueSend(s_bleQueue, &r, 0);
    }
};

static AdvCallbacks* s_cb = nullptr;

static bool macEq6(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

void BleScan::begin() {
    if (_running) return;
    _count = 0;
    memset(_dev, 0, sizeof(_dev));

    if (!_inited) {
        NimBLEDevice::init("");
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
        _inited = true;
    }
    if (!s_bleQueue) s_bleQueue = xQueueCreate(32, sizeof(BleRec));
    if (!s_cb) s_cb = new AdvCallbacks();

    NimBLEScan* sc = NimBLEDevice::getScan();
    sc->setAdvertisedDeviceCallbacks(s_cb, true);  // report duplicates for live RSSI
    sc->setActiveScan(true);
    sc->setInterval(80);
    sc->setWindow(40);
    sc->start(0, nullptr, false);  // continuous

    _running = true;
    _lastPrune = millis();
    Serial.println("[BLE] Scan started");
}

void BleScan::stop() {
    if (!_running) return;
    NimBLEScan* sc = NimBLEDevice::getScan();
    if (sc) sc->stop();
    // Release the BT controller so Wi-Fi gets the full radio while BLE is off.
    NimBLEDevice::deinit(true);
    _inited = false;
    _running = false;
    Serial.println("[BLE] Scan stopped");
}

void BleScan::loop() {
    if (!_running) return;
    drainQueue();
    if (millis() - _lastPrune >= 5000) {
        _lastPrune = millis();
        prune();
    }
}

int BleScan::findDev(const uint8_t addr[6]) const {
    for (int i = 0; i < _count; i++)
        if (macEq6(_dev[i].addr, addr)) return i;
    return -1;
}

void BleScan::drainQueue() {
    BleRec r;
    int budget = 48;
    while (budget-- > 0 && s_bleQueue && xQueueReceive(s_bleQueue, &r, 0) == pdTRUE) {
        int idx = findDev(r.addr);
        if (idx < 0) {
            if (_count >= BLE_MAX_DEV) {
                idx = 0;
                for (int i = 1; i < _count; i++)
                    if (_dev[i].lastSeen < _dev[idx].lastSeen) idx = i;
            } else {
                idx = _count++;
                memset(&_dev[idx], 0, sizeof(BleDev));
            }
            memcpy(_dev[idx].addr, r.addr, 6);
        }
        BleDev& d = _dev[idx];
        d.rssi = r.rssi;
        d.addrType = r.addrType;
        d.connectable = r.connectable;
        d.seen++;
        if (r.name[0] && d.name[0] == '\0') strncpy(d.name, r.name, sizeof(d.name) - 1);
        d.lastSeen = millis();
    }
}

void BleScan::prune() {
    unsigned long now = millis();
    int w = 0;
    for (int i = 0; i < _count; i++)
        if (now - _dev[i].lastSeen < 60000) _dev[w++] = _dev[i];
    _count = w;
}

void BleScan::buildSorted(uint8_t* order, int& n) const {
    n = _count;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {
        uint8_t key = order[i];
        int j = i - 1;
        while (j >= 0 && _dev[order[j]].rssi < _dev[key].rssi) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
}

bool BleScan::getLine(int idx, char* buf, size_t len) const {
    uint8_t order[BLE_MAX_DEV];
    int n = 0;
    buildSorted(order, n);
    if (idx < 0 || idx >= n) return false;
    const BleDev& d = _dev[order[idx]];
    char label[18];
    if (d.name[0]) {
        strncpy(label, d.name, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
    } else {
        snprintf(label, sizeof(label), "%02X:%02X:%02X:%02X:%02X:%02X",
                 d.addr[5], d.addr[4], d.addr[3], d.addr[2], d.addr[1], d.addr[0]);
    }
    snprintf(buf, len, "%-17s %d%s", label, (int)d.rssi, d.connectable ? " C" : "");
    return true;
}

void BleScan::toJson(JsonDocument& doc) const {
    JsonArray arr = doc["ble"].to<JsonArray>();
    uint8_t order[BLE_MAX_DEV];
    int n = 0;
    buildSorted(order, n);
    for (int i = 0; i < n; i++) {
        const BleDev& d = _dev[order[i]];
        JsonObject o = arr.add<JsonObject>();
        char m[18];
        snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
                 d.addr[5], d.addr[4], d.addr[3], d.addr[2], d.addr[1], d.addr[0]);
        o["addr"] = m;
        o["name"] = d.name;
        o["rssi"] = d.rssi;
        o["connectable"] = d.connectable;
        o["seen"] = d.seen;
    }
}
