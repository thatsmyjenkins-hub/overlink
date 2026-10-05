#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

bool hubspaceLogin(const char *email, const char *password, String &message);
bool hubspaceImport(String &message);
bool hubspaceControl(const char *entityId, const char *action, bool on, int dimming, int tempK,
                     int r, int g, int b, String &message);

bool winkLogin(const char *email, const char *password, const char *clientId,
               const char *clientSecret, const char *token, String &message);
bool winkImport(String &message);
bool winkControl(const char *entityId, const char *action, bool on, int dimming, int r, int g,
                 int b, String &message);
bool winkFillInfo(JsonObject out);

bool ratgdoControl(const char *ip, const char *action, String &message);
bool haCallService(const char *connectorId, const char *domain, const char *service,
                   const char *entityId, String &message);
