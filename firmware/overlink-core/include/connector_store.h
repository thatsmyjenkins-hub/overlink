#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Per-grid connector registry + auth vault under /homes/<grid>/connectors|vault
void connectorStoreBegin();
void connectorStoreFill(JsonArray arr);
bool connectorStoreUpsert(JsonVariantConst connector, String &message);
bool connectorStoreRemove(const char *id, String &message);
bool connectorStoreSetSecret(const char *id, JsonVariantConst secret, String &message);
bool connectorStoreGetSecret(const char *id, JsonDocument &out);
// Import entities from a local Home Assistant bridge if configured
bool connectorStoreHaImport(String &message);
