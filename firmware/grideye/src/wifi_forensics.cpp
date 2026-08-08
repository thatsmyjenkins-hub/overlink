#include "wifi_forensics.h"
#include "oui_lookup.h"
#include <WiFi.h>

namespace WifiForensics {

static int channelBand(int ch) {
    if (ch >= 1 && ch <= 14) return 2;
    if (ch >= 36) return 5;
    return 0;
}

bool parseBssid(const char* str, uint8_t mac[6]) {
    if (!str) return false;
    unsigned int a[6];
    if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6)
        return false;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)a[i];
    return true;
}

bool bssidEqual(const uint8_t a[6], const uint8_t b[6]) {
    return memcmp(a, b, 6) == 0;
}

int findApIndex(const IntelWifi* aps, int count, const char* bssid) {
    uint8_t want[6];
    if (!parseBssid(bssid, want)) return -1;
    for (int i = 0; i < count; i++) {
        uint8_t got[6];
        if (!parseBssid(aps[i].bssid, got)) continue;
        if (bssidEqual(want, got)) return i;
    }
    return -1;
}

static void appendFinding(JsonArray& arr, const char* severity, const char* title, const char* detail) {
    JsonObject f = arr.add<JsonObject>();
    f["severity"] = severity;
    f["title"] = title;
    f["detail"] = detail;
}

void appendApJson(const IntelWifi& ap, JsonObject& o) {
    o["ssid"] = ap.ssid;
    o["bssid"] = ap.bssid;
    o["rssi"] = ap.rssi;
    o["channel"] = ap.channel;
    o["hidden"] = ap.hidden;
    o["vendor"] = ap.vendor;
    o["encType"] = ap.encType;
    const char* encLabels[] = {"OPEN", "WEP", "WPA", "WPA2", "WPA3", "OTHER"};
    o["enc"] = ap.encType <= 5 ? encLabels[ap.encType] : "???";
}

static int scanRssiForBssid(const uint8_t want[6], int samples, int8_t& outMin, int8_t& outMax, int& outAvg) {
    int8_t best = -128;
    outMin = 0;
    outMax = -128;
    long sum = 0;
    int hits = 0;

    for (int s = 0; s < samples; s++) {
        if (s > 0) {
            delay(180);
            yield();
        }
        WiFi.scanDelete();
        yield();
        int n = WiFi.scanNetworks(false, true);
        if (n < 0) continue;

        for (int i = 0; i < n; i++) {
            const uint8_t* b = WiFi.BSSID(i);
            if (!b) continue;
            if (!bssidEqual(want, b)) continue;
            int8_t r = WiFi.RSSI(i);
            if (hits == 0) {
                outMin = outMax = r;
            } else {
                if (r < outMin) outMin = r;
                if (r > outMax) outMax = r;
            }
            sum += r;
            hits++;
            if (r > best) best = r;
        }
        WiFi.scanDelete();
    }

    outAvg = hits ? (int)(sum / hits) : 0;
    return hits;
}

void buildApDetail(const IntelWifi& target, const IntelWifi* aps, int apCount,
                   JsonDocument& doc) {
    doc["found"] = true;
    JsonObject ap = doc["ap"].to<JsonObject>();
    appendApJson(target, ap);
    ap["risk"] = 0;

    JsonArray scenarios = doc["scenarios"].to<JsonArray>();
    const char* list[] = {
        "signal_profile", "channel_audit", "ssid_collision",
        "security_audit", "rogue_check", "pentest_recon"
    };
    for (const char* s : list)
        scenarios.add(s);

    if (WiFi.status() == WL_CONNECTED) {
        doc["connectedSsid"] = WiFi.SSID();
        doc["connectedBssid"] = WiFi.BSSIDstr();
        uint8_t tb[6], wb[6];
        bool haveTarget = parseBssid(target.bssid, tb);
        bool haveConn = parseBssid(WiFi.BSSIDstr().c_str(), wb);
        doc["isConnectedAp"] = haveTarget && haveConn && bssidEqual(tb, wb);
    } else {
        doc["isConnectedAp"] = false;
    }

    int coChannel = 0;
    for (int i = 0; i < apCount; i++) {
        if (&aps[i] == &target) continue;
        if (aps[i].channel == target.channel) coChannel++;
    }
    doc["coChannelCount"] = coChannel;
    doc["band"] = channelBand(target.channel) == 5 ? "5GHz" :
                    (channelBand(target.channel) == 2 ? "2.4GHz" : "unknown");
}

bool runApAction(const char* action, IntelScanner& scanner,
                 const IntelWifi* aps, int apCount, int targetIdx,
                 JsonDocument& result) {
    if (!action || targetIdx < 0 || targetIdx >= apCount) {
        result["ok"] = false;
        result["message"] = "AP not found";
        return false;
    }

    const IntelWifi& target = aps[targetIdx];
    uint8_t want[6];
    if (!parseBssid(target.bssid, want)) {
        result["ok"] = false;
        result["message"] = "Invalid BSSID";
        return false;
    }

    result["action"] = action;
    result["bssid"] = target.bssid;
    result["ssid"] = target.ssid;
    JsonArray findings = result["findings"].to<JsonArray>();

    auto finish = [&](bool ok, const char* msg) {
        result["ok"] = ok;
        result["message"] = msg;
        return ok;
    };

    if (strcmp(action, "rescan") == 0) {
        if (scanner.triggerRfScan()) {
            appendFinding(findings, "info", "RF scan started",
                          "Refresh this page in ~8s for updated RSSI/channel.");
            return finish(true, "RF scan triggered");
        }
        return finish(false, "RF scan already running or offline");
    }

    if (strcmp(action, "signal_profile") == 0 || strcmp(action, "pentest_recon") == 0) {
        int8_t rmin, rmax;
        int avg;
        int hits = scanRssiForBssid(want, 5, rmin, rmax, avg);
        if (hits > 0) {
            result["rssiMin"] = rmin;
            result["rssiMax"] = rmax;
            result["rssiAvg"] = avg;
            result["rssiSamples"] = hits;
            int swing = rmax - rmin;
            if (swing > 12)
                appendFinding(findings, "warn", "Unstable RSSI",
                              "Signal swing >12 dB — possible mobility, power save, or rogue AP.");
            else
                appendFinding(findings, "ok", "Stable RSSI",
                              "Signal strength consistent across samples.");
        } else {
            appendFinding(findings, "warn", "AP not seen",
                          "BSSID absent during 5 quick scans — may be hidden or out of range.");
        }
        if (strcmp(action, "signal_profile") == 0)
            return finish(hits > 0, hits > 0 ? "Signal profile complete" : "AP not visible");
    }

    if (strcmp(action, "channel_audit") == 0 || strcmp(action, "pentest_recon") == 0) {
        JsonArray co = result["coChannelAps"].to<JsonArray>();
        JsonArray adj = result["adjacentChannelAps"].to<JsonArray>();
        int coCount = 0, adjCount = 0;
        for (int i = 0; i < apCount; i++) {
            if (i == targetIdx) continue;
            const IntelWifi& o = aps[i];
            int dch = abs((int)o.channel - (int)target.channel);
            if (o.channel == target.channel) {
                coCount++;
                JsonObject x = co.add<JsonObject>();
                x["ssid"] = o.ssid;
                x["bssid"] = o.bssid;
                x["rssi"] = o.rssi;
            } else if (dch == 1) {
                adjCount++;
                JsonObject x = adj.add<JsonObject>();
                x["ssid"] = o.ssid;
                x["bssid"] = o.bssid;
                x["channel"] = o.channel;
                x["rssi"] = o.rssi;
            }
        }
        if (coCount >= 3)
            appendFinding(findings, "high", "Channel congestion",
                          "Many APs share this channel — interference likely.");
        else if (coCount > 0)
            appendFinding(findings, "med", "Co-channel APs",
                          "Other networks on same channel — monitor for collisions.");
        else
            appendFinding(findings, "ok", "Clean channel",
                          "No other mapped APs on this channel.");

        if (strcmp(action, "channel_audit") == 0)
            return finish(true, "Channel audit complete");
    }

    if (strcmp(action, "ssid_collision") == 0 || strcmp(action, "pentest_recon") == 0) {
        JsonArray twins = result["ssidMatches"].to<JsonArray>();
        int twinsCount = 0;
        for (int i = 0; i < apCount; i++) {
            if (i == targetIdx) continue;
            if (strcmp(aps[i].ssid, target.ssid) != 0) continue;
            uint8_t ob[6];
            if (!parseBssid(aps[i].bssid, ob)) continue;
            if (bssidEqual(want, ob)) continue;
            twinsCount++;
            JsonObject t = twins.add<JsonObject>();
            t["bssid"] = aps[i].bssid;
            t["rssi"] = aps[i].rssi;
            t["channel"] = aps[i].channel;
            t["encType"] = aps[i].encType;
        }
        if (twinsCount > 0)
            appendFinding(findings, "high", "SSID collision / evil-twin risk",
                          "Same SSID, different BSSID — verify authenticity before connecting.");
        else
            appendFinding(findings, "ok", "Unique SSID+BSSID",
                          "No duplicate SSID with other BSSIDs in last scan.");
        if (strcmp(action, "ssid_collision") == 0)
            return finish(true, twinsCount ? "Collisions found" : "No SSID collisions");
    }

    if (strcmp(action, "security_audit") == 0 || strcmp(action, "pentest_recon") == 0) {
        switch (target.encType) {
            case 0:
                appendFinding(findings, "critical", "Open network",
                              "No encryption — traffic readable on this WLAN.");
                break;
            case 1:
                appendFinding(findings, "critical", "WEP",
                              "WEP is broken — treat as no security.");
                break;
            case 2:
                appendFinding(findings, "high", "WPA (TKIP era)",
                              "Legacy WPA — prefer WPA2/WPA3; crackable under attack.");
                break;
            case 3:
                appendFinding(findings, "med", "WPA2-PSK",
                              "Common home standard — weak passphrases remain the risk.");
                break;
            case 4:
                appendFinding(findings, "ok", "WPA3",
                              "Modern encryption — still verify passphrase policy.");
                break;
            default:
                appendFinding(findings, "med", "Unknown encryption",
                              "Could not classify auth mode from scan.");
                break;
        }
        if (target.hidden)
            appendFinding(findings, "med", "Hidden SSID",
                          "SSID not broadcast — clients may probe, revealing network name.");

        if (strcmp(action, "security_audit") == 0)
            return finish(true, "Security audit complete");
    }

    if (strcmp(action, "rogue_check") == 0 || strcmp(action, "pentest_recon") == 0) {
        if (WiFi.status() == WL_CONNECTED) {
            String connSsid = WiFi.SSID();
            String connBssid = WiFi.BSSIDstr();
            uint8_t cb[6];
            bool sameBssid = parseBssid(connBssid.c_str(), cb) && bssidEqual(want, cb);
            bool sameSsid = connSsid.equals(target.ssid);

            result["connectedSsid"] = connSsid;
            result["connectedBssid"] = connBssid;
            result["isConnectedAp"] = sameBssid;

            if (sameSsid && !sameBssid)
                appendFinding(findings, "high", "Possible rogue AP",
                              "You are connected to same SSID but different BSSID than this AP.");
            else if (sameBssid)
                appendFinding(findings, "ok", "This is your AP",
                              "Deck is associated to this BSSID.");
            else
                appendFinding(findings, "info", "Not associated",
                              "Deck uses a different network than this AP.");
        } else {
            appendFinding(findings, "info", "Offline",
                          "Deck not on Wi-Fi — rogue check compares scan data only.");
        }
        if (strcmp(action, "rogue_check") == 0)
            return finish(true, "Rogue check complete");
    }

    if (strcmp(action, "pentest_recon") == 0) {
        return finish(true, "Passive recon bundle complete");
    }

    result["ok"] = false;
    result["message"] = "Unknown action";
    return false;
}

} // namespace WifiForensics
