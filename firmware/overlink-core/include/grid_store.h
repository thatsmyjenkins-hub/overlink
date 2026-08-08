#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void gridStoreBegin();
void gridStoreFillList(JsonArray arr);
const char *gridStoreActiveId();

// Absolute SD path for a file in the active grid, e.g. gridStorePath("devices.json")
// → "/homes/<activeId>/devices.json". Buffer is static; copy if you need to keep it.
const char *gridStorePath(const char *file);

// Same for an explicit grid id.
const char *gridStorePathFor(const char *gridId, const char *file);

bool gridStoreCreate(const char *name, const char *ssid, String &message);
// Blank template (empty devices/rooms) for a new home — preferred on SoftAP join.
bool gridStoreCreateBlank(const char *name, const char *ssid, String &message);
bool gridStoreRenameHome(const char *name, String &message);
bool gridStoreActivate(const char *id, String &message);
bool gridStoreBindSsid(const char *id, const char *ssid, String &message);

// On Wi‑Fi join: bind/create a blank grid for this SSID and activate it.
bool gridStoreOnWifiJoin(const char *ssid, const char *optionalName, String &message);

// Arrival Wizard: pending until user finishes or skips.
bool gridStoreArrivalPending();
bool gridStoreSetArrivalDone(bool done, String &message);

// Upsert a zone into active grid rooms.json
bool gridStoreUpsertZone(const char *id, const char *name, String &message);
