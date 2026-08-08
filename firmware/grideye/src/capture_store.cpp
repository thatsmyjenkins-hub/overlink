#include "capture_store.h"
#include "config.h"
#include "rf_recon.h"
#include "vspi_bus.h"
#include <SD.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static File s_file;

struct CapRec {
    uint16_t len;     // bytes stored (<= CAP_SNAPLEN)
    uint16_t orig;    // original frame length
    uint32_t ts;      // micros() at capture
    uint8_t  data[CAP_SNAPLEN];
};

static QueueHandle_t s_capQueue = nullptr;

// Runs in the Wi-Fi task — only copies into the queue, never touches SD.
static void captureSink(const uint8_t* d, uint16_t len) {
    if (!s_capQueue) return;
    CapRec r;
    r.orig = len;
    r.len = len > CAP_SNAPLEN ? CAP_SNAPLEN : len;
    r.ts = micros();
    memcpy(r.data, d, r.len);
    xQueueSend(s_capQueue, &r, 0);  // drop if the SD writer falls behind
}

bool CaptureStore::begin() {
    if (_mounted) return true;
    vspiBusInit();      // ensure the shared bus exists (touch may not have run yet)
    vspiSelectSd();     // route MISO to the SD card for card init

    const uint32_t speeds[] = {40000000UL, 25000000UL, 10000000UL, 4000000UL};
    for (uint32_t hz : speeds) {
        if (SD.begin(SD_CS_PIN, vspiBus, hz)) { _mounted = true; break; }
        SD.end();
        delay(30);
    }
    vspiSelectTouch();  // hand the bus back to touch

    if (!_mounted) {
        Serial.println("[CAP] SD mount failed");
        return false;
    }
    _cardMB = SD.cardSize() / (1024ULL * 1024ULL);
    Serial.printf("[CAP] SD mounted, %llu MB\n", _cardMB);

    if (!s_capQueue) s_capQueue = xQueueCreate(24, sizeof(CapRec));
    rfReconSetCaptureSink(&captureSink);
    return true;
}

int CaptureStore::nextIndex() {
    if (!SD.exists("/captures")) SD.mkdir("/captures");
    for (int i = 1; i <= 9999; i++) {
        char p[40];
        snprintf(p, sizeof(p), "/captures/cap_%04d.pcap", i);
        if (!SD.exists(p)) return i;
    }
    return 0;
}

void CaptureStore::writeGlobalHeader() {
    // pcap global header, little-endian, LINKTYPE_IEEE802_11 = 105
    uint8_t h[24];
    uint32_t magic = 0xa1b2c3d4;
    memcpy(h + 0, &magic, 4);
    uint16_t vmaj = 2, vmin = 4;
    memcpy(h + 4, &vmaj, 2);
    memcpy(h + 6, &vmin, 2);
    uint32_t zero = 0;
    memcpy(h + 8, &zero, 4);   // thiszone
    memcpy(h + 12, &zero, 4);  // sigfigs
    uint32_t snap = CAP_SNAPLEN;
    memcpy(h + 16, &snap, 4);
    uint32_t net = 105;
    memcpy(h + 20, &net, 4);
    s_file.write(h, sizeof(h));
}

bool CaptureStore::startCapture() {
    if (!_mounted || _capturing) return _capturing;
    vspiSelectSd();
    int idx = nextIndex();
    if (idx == 0) { vspiSelectTouch(); return false; }
    snprintf(_path, sizeof(_path), "/captures/cap_%04d.pcap", idx);
    s_file = SD.open(_path, FILE_WRITE);
    if (!s_file) {
        Serial.printf("[CAP] open failed: %s\n", _path);
        _path[0] = '\0';
        vspiSelectTouch();
        return false;
    }
    writeGlobalHeader();
    s_file.flush();
    vspiSelectTouch();
    _frames = 0;
    _bytes = 0;
    _baseMicros = micros();
    _lastFlush = millis();
    _capturing = true;
    rfReconEnableCapture(true);
    Serial.printf("[CAP] capturing -> %s\n", _path);
    return true;
}

void CaptureStore::stopCapture() {
    if (!_capturing) return;
    rfReconEnableCapture(false);
    _capturing = false;
    if (s_file) {
        vspiSelectSd();
        s_file.flush();
        s_file.close();
        vspiSelectTouch();
    }
    // Drain any stragglers so they aren't carried into the next session.
    if (s_capQueue) xQueueReset(s_capQueue);
    Serial.printf("[CAP] stopped: %lu frames, %lu bytes -> %s\n",
                  (unsigned long)_frames, (unsigned long)_bytes, _path);
}

void CaptureStore::loop() {
    if (!_capturing || !s_file || !s_capQueue) return;
    if (uxQueueMessagesWaiting(s_capQueue) == 0) return;  // nothing to write

    vspiSelectSd();
    CapRec r;
    int budget = 48;  // bound SD work per loop for UI responsiveness
    while (budget-- > 0 && xQueueReceive(s_capQueue, &r, 0) == pdTRUE) {
        uint32_t rel = r.ts - _baseMicros;
        uint8_t ph[16];
        uint32_t tsSec = rel / 1000000UL;
        uint32_t tsUsec = rel % 1000000UL;
        memcpy(ph + 0, &tsSec, 4);
        memcpy(ph + 4, &tsUsec, 4);
        uint32_t incl = r.len;
        uint32_t orig = r.orig;
        memcpy(ph + 8, &incl, 4);
        memcpy(ph + 12, &orig, 4);
        s_file.write(ph, sizeof(ph));
        s_file.write(r.data, r.len);
        _frames++;
        _bytes += r.len + sizeof(ph);
    }

    if (millis() - _lastFlush > 1000) {
        _lastFlush = millis();
        s_file.flush();
    }
    vspiSelectTouch();  // always hand the bus back so the next touch read works
}
