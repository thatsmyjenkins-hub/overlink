#pragma once

#include <ESPAsyncWebServer.h>

// Redirects captive-portal probe URLs to the dashboard on the AP interface.
class CaptivePortalHandler : public AsyncWebHandler {
public:
    explicit CaptivePortalHandler(const char* portalUrl) : _portalUrl(portalUrl) {}

    bool canHandle(AsyncWebServerRequest* request) override;
    void handleRequest(AsyncWebServerRequest* request) override;

private:
    const char* _portalUrl;
};
