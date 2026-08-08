#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// Lightweight BLE reconnaissance for Roaming mode. Runs an asynchronous NimBLE
// scan; advertisement callbacks (NimBLE task) enqueue compact records that the
// main loop drains into a bounded catalog. Time-shares the 2.4 GHz radio with
// the Wi-Fi promiscuous sweep via the ESP32 coexistence layer.

struct BleDev {
    uint8_t  addr[6];
    char     name[24];
    int8_t   rssi;
    bool     connectable;
    uint8_t  addrType;
    uint16_t seen;
    uint32_t lastSeen;
};

class BleScan {
public:
    void begin();
    void stop();
    void loop();
    bool isRunning() const { return _running; }

    int deviceCount() const { return _count; }
    // Formats the idx-th device (RSSI-sorted) into buf, e.g. "Pixel 7  -61 C".
    bool getLine(int idx, char* buf, size_t len) const;
    void toJson(JsonDocument& doc) const;

private:
    bool _running = false;
    bool _inited = false;
    unsigned long _lastPrune = 0;

    BleDev _dev[BLE_MAX_DEV];
    int _count = 0;

    void drainQueue();
    int  findDev(const uint8_t addr[6]) const;
    void prune();
    void buildSorted(uint8_t* order, int& n) const;
};
