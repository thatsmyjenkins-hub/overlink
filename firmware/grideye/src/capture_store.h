#pragma once

#include <Arduino.h>

// microSD-backed packet capture for Roaming mode. Frames flow from the Wi-Fi
// RX callback (via rfReconSetCaptureSink) into a FreeRTOS queue; loop() drains
// the queue on the main thread and appends to a pcap file (LINKTYPE 802.11).
//
// CYD2432S028R microSD is on VSPI: SCK18 MISO19 MOSI23 CS5 (verified working).

#define CAP_SNAPLEN   256

class CaptureStore {
public:
    bool begin();                 // mount SD + register capture sink
    bool isMounted() const { return _mounted; }

    bool startCapture();          // open a fresh /captures/cap_NNNN.pcap
    void stopCapture();
    bool isCapturing() const { return _capturing; }

    void loop();                  // drain queue -> SD (call from main loop)

    uint32_t framesWritten() const { return _frames; }
    uint32_t bytesWritten() const { return _bytes; }
    const char* currentFile() const { return _path; }
    uint64_t cardSizeMB() const { return _cardMB; }

private:
    bool _mounted = false;
    bool _capturing = false;
    uint32_t _frames = 0;
    uint32_t _bytes = 0;
    uint32_t _baseMicros = 0;
    unsigned long _lastFlush = 0;
    char _path[40] = "";
    uint64_t _cardMB = 0;

    void writeGlobalHeader();
    int nextIndex();
};
