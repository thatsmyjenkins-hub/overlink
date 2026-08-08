#include "captive_portal.h"

static bool isCaptiveProbePath(const String& url) {
    if (url == "/") return false;
    if (url.startsWith("/api/")) return false;
    if (url == "/app.css" || url == "/app.js" || url == "/index.html") return false;
    if (url == "/generate_204") return true;
    if (url == "/gen_204") return true;
    if (url == "/hotspot-detect.html") return true;
    if (url == "/library/test/success.html") return true;
    if (url == "/success.txt") return true;
    if (url == "/canonical.html") return true;
    if (url == "/ncsi.txt") return true;
    if (url == "/connecttest.txt") return true;
    if (url.indexOf("msftconnecttest") >= 0) return true;
    if (url.indexOf("msftncsi") >= 0) return true;
    if (url.indexOf("gstatic") >= 0) return true;
    if (url.indexOf("apple.com") >= 0) return true;
    if (url.indexOf("miui") >= 0) return true;
    return url.length() > 1;
}

bool CaptivePortalHandler::canHandle(AsyncWebServerRequest* request) {
    if (request->method() != HTTP_GET && request->method() != HTTP_HEAD)
        return false;
    if (!ON_AP_FILTER(request))
        return false;
    String host = request->host();
    if (host == "192.168.4.1" || host == "cyberdeck.local")
        return false;
    return isCaptiveProbePath(request->url());
}

void CaptivePortalHandler::handleRequest(AsyncWebServerRequest* request) {
    request->redirect(_portalUrl);
}
