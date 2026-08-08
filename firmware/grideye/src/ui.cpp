#include "ui.h"
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include "vspi_bus.h"
#include "cyd_qrcode.h"

static XPT2046_Touchscreen s_touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

static void drawScrollBtn(TFT_eSPI& tft, int x, int y, int w, int h, bool up, bool enabled);

static int16_t touchBestTwoAvg(int16_t a, int16_t b, int16_t c) {
    int16_t da = a > b ? a - b : b - a;
    int16_t db = a > c ? a - c : c - a;
    int16_t dc = c > b ? c - b : b - c;
    if (da <= db && da <= dc) return (a + b) >> 1;
    if (db <= da && db <= dc) return (a + c) >> 1;
    return (b + c) >> 1;
}

// ── Keyboard character maps ──
static const char* KB_ROWS[][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},     // lower
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},     // upper
    {"1234567890", "-/:;()$&@\"", ".,?!'+#"}     // numbers
};
static const int KB_ROW_LENS[] = {10, 9, 7};

static bool intelPhaseActive(const char* phase) {
    return phase && (strcmp(phase, "LAN_SWEEP") == 0 ||
                     strcmp(phase, "RF_SCAN") == 0 ||
                     strcmp(phase, "PROFILE") == 0);
}

// ────────────────────────────────────────
//  Init
// ────────────────────────────────────────

void UI::begin() {
    pinMode(TFT_BL_PIN, OUTPUT);
    setBacklight(200);

    _tft.init();
    _tft.setRotation(1);
    _tft.fillScreen(COLOR_BG);

    touchBegin();
    loadCalibration();

    Serial.println("[INIT] Display ready");

#if TOUCH_DIAG_BOOT
    Serial.println("[INIT] TOUCH_DIAG_BOOT=1 — touch diagnostic mode");
    Serial.println("[CSV] ms,irq,x,y,z,irqDown,libTouch,pressDown,valid,irqEdge,pressEdge");
    setScreen(SCREEN_TOUCH_DIAG);
#else
    resetTouchState();

    if (_calLoaded) {
        Serial.println("[INIT] Calibration loaded from flash");
        // Post-cal flow runs from main() after net + callbacks are ready.
    } else {
        Serial.println("[INIT] No calibration found, starting calibration");
        _calStep = 0;
        setScreen(SCREEN_CALIBRATE);
    }
#endif
}

// ────────────────────────────────────────
//  Main loop – poll touch
// ────────────────────────────────────────

static const char* screenName(Screen s) {
    switch (s) {
        case SCREEN_TOUCH_DIAG:      return "TOUCH_DIAG";
        case SCREEN_CALIBRATE:       return "CALIBRATE";
        case SCREEN_NET_MODE:        return "NET_MODE";
        case SCREEN_WIFI_SCAN:       return "WIFI_SCAN";
        case SCREEN_WIFI_PASS:       return "WIFI_PASS";
        case SCREEN_WIFI_CONNECTING: return "CONNECTING";
        case SCREEN_MAIN_MENU:       return "HOME";
        case SCREEN_INTEL_LIVE:      return "INTEL_LIVE";
        case SCREEN_SHARE_INTEL:     return "SHARE";
        case SCREEN_STATUS:          return "STATUS";
        case SCREEN_ROAMING:         return "ROAMING";
        default:                     return "???";
    }
}

void UI::loop() {
    if (_screen == SCREEN_TOUCH_DIAG) {
        loopTouchDiag();
        return;
    }

    // Calibration screen uses raw touch directly
    if (_screen == SCREEN_CALIBRATE) {
        uint16_t rawX, rawY;
        bool touched = touchReadRaw(rawX, rawY);

        bool settled = (millis() - _screenChangeTime) >= TOUCH_SETTLE_MS;
        if (!settled) {
            _wasTouched = false;
        } else if (touched && !_wasTouched &&
                   (millis() - _lastTouchTime >= TOUCH_DEBOUNCE_MS)) {
            _lastTouchTime = millis();
            Serial.printf("[CAL] step=%d raw(%d,%d) ACCEPTED\n", _calStep, rawX, rawY);
            handleTouchCalibrate(rawX, rawY);
        }
        if (settled)
            _wasTouched = touched;
        if (!touched) _touchWasDown = false;
        return;
    }

    vspiSelectTouch();

    int tx, ty;
    bool touched = touchRead(tx, ty);
    bool settled = (millis() - _screenChangeTime) >= TOUCH_SETTLE_MS;

    if (_qrOverlayActive) {
        if (!settled) {
            _wasTouched = false;
        } else if (touched && !_wasTouched &&
                   (millis() - _lastTouchTime >= TOUCH_DEBOUNCE_MS)) {
            _lastTouchTime = millis();
            _qrDismissTaps++;
            Serial.printf("[QR] dismiss tap %u/3\n", (unsigned)_qrDismissTaps);
            if (_qrDismissTaps >= 3)
                dismissQrOverlay();
            else
                drawQrOverlay();
            _wasTouched = true;
        } else if (settled) {
            _wasTouched = touched;
        }
        if (_toastExpiry && millis() > _toastExpiry) {
            _toastExpiry = 0;
            if (_qrOverlayActive) drawQrOverlay();
        }
        return;
    }

    if (!settled) {
        _wasTouched = false;
    } else if (_overlayExpiry && touched && !_wasTouched) {
        _overlayExpiry = 0;
        setScreen(_screen);
        _wasTouched = touched;
        return;
    } else if (touched && !_wasTouched &&
               (millis() - _lastTouchTime >= TOUCH_DEBOUNCE_MS)) {
        _lastTouchTime = millis();

        Serial.printf("[TOUCH] screen=%s raw(%u,%u) -> x=%d y=%d cal=%s\n",
                      screenName(_screen), (unsigned)_lastTouchRawX, (unsigned)_lastTouchRawY,
                      tx, ty, calAcceptsRaw(_lastTouchRawX, _lastTouchRawY) ? "saved" : "default");

        switch (_screen) {
            case SCREEN_NET_MODE:    handleTouchNetMode(tx, ty); break;
            case SCREEN_WIFI_SCAN:   handleTouchWiFiScan(tx, ty);  break;
            case SCREEN_WIFI_PASS:   handleTouchWiFiPass(tx, ty);  break;
            case SCREEN_WIFI_CONNECTING:
                Serial.println("[NAV] connecting -> wifi_scan (cancelled)");
                setScreen(SCREEN_WIFI_SCAN);
                break;
            case SCREEN_MAIN_MENU:   handleTouchMainMenu(tx, ty);  break;
            case SCREEN_INTEL_LIVE:  handleTouchIntelLive(tx, ty); break;
            case SCREEN_SHARE_INTEL: handleTouchShareIntel(tx, ty); break;
            case SCREEN_STATUS:      handleTouchStatus(tx, ty);    break;
            case SCREEN_ROAMING:     handleTouchRoaming(tx, ty);   break;
            default: break;
        }
        _wasTouched = true;
    } else if (settled) {
        _wasTouched = touched;
    }

#if TOUCH_DEBUG_POLL
    if (_screen == SCREEN_NET_MODE && settled) {
        static unsigned long lastPoll = 0;
        if (millis() - lastPoll >= 400) {
            lastPoll = millis();
            int z = 0;
            bool irq = (digitalRead(TOUCH_IRQ_PIN) == LOW);
            bool press = touchHasPressure(&z);
            Serial.printf("[TOUCH_POLL] irq=%d z=%d press=%d tap=%d\n",
                          irq ? 1 : 0, z, press ? 1 : 0, touched ? 1 : 0);
        }
    }
#endif

    if (_toastExpiry && millis() > _toastExpiry) {
        _toastExpiry = 0;
        setScreen(_screen);
    }

    if (_overlayExpiry && millis() > _overlayExpiry) {
        _overlayExpiry = 0;
        setScreen(_screen);
    }
}

// ────────────────────────────────────────
//  Touch – raw XPT2046 over VSPI
// ────────────────────────────────────────

void UI::touchBegin() {
    pinMode(TOUCH_IRQ_PIN, INPUT);
    vspiBusInit();          // shared VSPI bus (touch + SD)
    s_touch.begin(vspiBus);
    s_touch.setRotation(1);  // landscape, matches TFT rotation 1
    Serial.println("[TOUCH] XPT2046_Touchscreen library initialized");
}

uint16_t UI::touchReadChannel(uint8_t cmd) {
    vspiSelectTouch();
    digitalWrite(TOUCH_CS_PIN, LOW);
    vspiBus.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    vspiBus.transfer(cmd);
    uint16_t val = vspiBus.transfer16(0) >> 3;
    vspiBus.endTransaction();
    digitalWrite(TOUCH_CS_PIN, HIGH);
    return val;
}

bool UI::touchHasPressure(int *zOut) {
    vspiSelectTouch();
    digitalWrite(TOUCH_CS_PIN, LOW);
    vspiBus.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    vspiBus.transfer(0xB1);
    int16_t z1 = vspiBus.transfer16(0xC1) >> 3;
    int z = z1 + 4095;
    int16_t z2 = vspiBus.transfer16(0x91) >> 3;
    z -= z2;
    digitalWrite(TOUCH_CS_PIN, HIGH);
    vspiBus.endTransaction();
    if (z < 0) z = 0;
    if (zOut) *zOut = z;
    return z >= (int)TOUCH_Z_THRESHOLD;
}

UI::TouchSample UI::touchSample() {
    vspiSelectTouch();
    TouchSample s = {};
    s.irq = digitalRead(TOUCH_IRQ_PIN) ? 1 : 0;
    s.irqSaysDown = (s.irq == 0);

    // Legacy raw SPI (for diagnostic comparison — known broken on many CYDs)
    uint32_t sumX = 0, sumY = 0, sumZ = 0;
    for (int i = 0; i < 2; i++) {
        sumZ += touchReadChannel(0xB0);
        sumX += touchReadChannel(0x90);  // swapped vs our old code
        sumY += touchReadChannel(0xD0);
    }
    uint16_t legX = sumX / 2;
    uint16_t legY = sumY / 2;
    s.rawZ = sumZ / 2;

    int zDiff = 0;
    s.pressDown = touchHasPressure(&zDiff);
    s.rawZ = (uint16_t)zDiff;

    if (s.pressDown || s.irqSaysDown) {
        uint16_t rx = 0, ry = 0;
        if (touchReadRaw(rx, ry)) {
            s.rawX = rx;
            s.rawY = ry;
            s.rawValid = true;
        } else {
            s.rawX = legX;
            s.rawY = legY;
            s.rawValid = (legX > 100 && legY > 100 && legX < 4080 && legY < 4080);
        }
    } else {
        s.rawX = legX;
        s.rawY = legY;
        s.rawValid = (legX > 100 && legY > 100 && legX < 4080 && legY < 4080);
    }
    return s;
}

bool UI::touchReadRaw(uint16_t &rawX, uint16_t &rawY) {
    vspiSelectTouch();
    bool irqDown = (digitalRead(TOUCH_IRQ_PIN) == LOW);
    int z = 0;
    bool press = touchHasPressure(&z);

    if (!press && !irqDown) {
        _touchWasDown = false;
        return false;
    }

    // Library only reads after IRQ wake; force update when Z proves a real press.
    s_touch.isrWake = true;
    if (s_touch.touched()) {
        TS_Point p = s_touch.getPoint();
        if (p.x > 50 && p.y > 50 && p.x < 4080 && p.y < 4080) {
            rawX = p.x;
            rawY = p.y;
            _lastTouchRawX = rawX;
            _lastTouchRawY = rawY;
            _touchWasDown = true;
            return true;
        }
    }

    // Legacy channels (field logs: raw(2104,1281) on JOIN tap)
    uint32_t sumX = 0, sumY = 0;
    for (int i = 0; i < 3; i++) {
        sumX += touchReadChannel(0x90);
        sumY += touchReadChannel(0xD0);
    }
    uint16_t rx = (uint16_t)(sumX / 3);
    uint16_t ry = (uint16_t)(sumY / 3);
    if (rx > 50 && ry > 50 && rx < 4080 && ry < 4080) {
        rawX = rx;
        rawY = ry;
        _lastTouchRawX = rawX;
        _lastTouchRawY = rawY;
        _touchWasDown = true;
        return true;
    }

    _touchWasDown = false;
    return false;
}

bool UI::calAcceptsRaw(uint16_t rawX, uint16_t rawY) const {
    if (!_calLoaded)
        return false;
    const int margin = 200;
    return (int)rawX >= _cal.xMin - margin && (int)rawX <= _cal.xMax + margin &&
           (int)rawY >= _cal.yMin - margin && (int)rawY <= _cal.yMax + margin;
}

void UI::mapTouchPoint(uint16_t rawX, uint16_t rawY, int &x, int &y) const {
    if (_calLoaded && calAcceptsRaw(rawX, rawY)) {
        if (_cal.xInvert)
            x = map(rawX, _cal.xMax, _cal.xMin, 0, SCREEN_W);
        else
            x = map(rawX, _cal.xMin, _cal.xMax, 0, SCREEN_W);
        if (_cal.yInvert)
            y = map(rawY, _cal.yMax, _cal.yMin, 0, SCREEN_H);
        else
            y = map(rawY, _cal.yMin, _cal.yMax, 0, SCREEN_H);
        return;
    }

    // Stale per-unit cal (saved X 3236-3533 but live reads ~2100/1280) pins to corners.
    x = map(rawX, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W);
    y = map(rawY, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H);
}

void UI::probeTouchHealth(const char* tag) {
    vspiSelectTouch();
    for (int i = 0; i < 3; i++) {
        bool irq = (digitalRead(TOUCH_IRQ_PIN) == LOW);
        int z = 0;
        bool press = touchHasPressure(&z);
        uint16_t rx = 0, ry = 0;
        bool ok = touchReadRaw(rx, ry);
        Serial.printf("[TOUCH_PROBE] %s #%d irq=%d z=%d press=%d ok=%d\n",
                      tag, i, irq ? 1 : 0, z, press ? 1 : 0, ok ? 1 : 0);
        delay(20);
    }
}

bool UI::touchRead(int &x, int &y) {
    uint16_t rawX, rawY;
    if (!touchReadRaw(rawX, rawY)) {
        _touchWasDown = false;
        return false;
    }

    bool usedSaved = _calLoaded && calAcceptsRaw(rawX, rawY);
    mapTouchPoint(rawX, rawY, x, y);
    x = constrain(x, 0, SCREEN_W - 1);
    y = constrain(y, 0, SCREEN_H - 1);

    static bool loggedFallback = false;
    if (!usedSaved && !loggedFallback) {
        loggedFallback = true;
        Serial.printf("[CAL] raw(%u,%u) outside saved bounds — using default map\n",
                      (unsigned)rawX, (unsigned)rawY);
    }

    return true;
}

// ────────────────────────────────────────
//  Screen management
// ────────────────────────────────────────

void UI::resetTouchState() {
    _wasTouched = false;
    _touchWasDown = false;
    _lastTouchTime = 0;
}

void UI::setScreen(Screen s) {
    if (_qrOverlayActive) {
        if (s == _screen)
            return;
        _qrOverlayActive = false;
        _qrDismissTaps = 0;
    }
    Serial.printf("[SCREEN] %s -> %s\n", screenName(_screen), screenName(s));
    _screen = s;
    _screenChangeTime = millis();
    resetTouchState();
    _tft.fillScreen(COLOR_BG);
    switch (s) {
        case SCREEN_TOUCH_DIAG:      drawTouchDiag();    break;
        case SCREEN_CALIBRATE:       drawCalibrate();    break;
        case SCREEN_NET_MODE:        drawNetMode();      break;
        case SCREEN_WIFI_SCAN:       drawWiFiScan();     break;
        case SCREEN_WIFI_PASS:       drawWiFiPass();     break;
        case SCREEN_WIFI_CONNECTING: drawConnecting();   break;
        case SCREEN_MAIN_MENU:       drawMainMenu();     break;
        case SCREEN_INTEL_LIVE:      drawIntelLive();    break;
        case SCREEN_SHARE_INTEL:     drawShareIntel();   break;
        case SCREEN_STATUS:          drawStatusScreen();  break;
        case SCREEN_ROAMING:         drawRoaming();      break;
    }
}

// ────────────────────────────────────────
//  Touch diagnostic (raw stream validation)
// ────────────────────────────────────────

void UI::drawTouchDiag() {
    _tft.fillScreen(COLOR_BG);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    _tft.drawString("TOUCH DIAGNOSTIC", 8, 6);
    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("Serial CSV @ 115200 — do not touch yet", 8, 22);
    _tft.drawString("Watch irq / Z / edges in log", 8, 34);
}

void UI::loopTouchDiag() {
    static unsigned long lastLog = 0;
    static unsigned long lastDraw = 0;
    static bool prevIrqDown = false;
    static bool prevPressDown = false;
    static uint32_t irqLowCount = 0;
    static uint32_t irqHighCount = 0;
    static uint32_t sampleCount = 0;
    static uint32_t pressCount = 0;
    static char lineBuf[48];

    bool libTouch = s_touch.touched();
    TouchSample s = touchSample();
    sampleCount++;
    if (s.irqSaysDown) irqLowCount++; else irqHighCount++;
    if (s.pressDown) pressCount++;

    bool irqEdge = (s.irqSaysDown != prevIrqDown);
    bool pressEdge = (s.pressDown != prevPressDown);
    static bool prevLib = false;
    bool libEdge = (libTouch != prevLib);
    prevLib = libTouch;

    unsigned long now = millis();
    bool periodic = (now - lastLog >= 100);
    if (periodic || irqEdge || pressEdge || libEdge) {
        Serial.printf("%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
            now, s.irq, s.rawX, s.rawY, s.rawZ,
            s.irqSaysDown ? 1 : 0, libTouch ? 1 : 0, s.pressDown ? 1 : 0,
            s.rawValid ? 1 : 0, irqEdge ? 1 : 0, pressEdge ? 1 : 0);
        lastLog = now;
    }

    if (irqEdge) {
        Serial.printf("[EDGE] IRQ %s -> %s  (x=%u y=%u z=%u)\n",
            prevIrqDown ? "DOWN" : "UP", s.irqSaysDown ? "DOWN" : "UP",
            s.rawX, s.rawY, s.rawZ);
    }
    if (pressEdge) {
        Serial.printf("[EDGE] PRESS %s -> %s  (z=%u thresh=%d)\n",
            prevPressDown ? "DOWN" : "UP", s.pressDown ? "DOWN" : "UP",
            s.rawZ, TOUCH_Z_THRESHOLD);
    }

    prevIrqDown = s.irqSaysDown;
    prevPressDown = s.pressDown;

    if (now - lastDraw >= 200) {
        lastDraw = now;
        int y = 50;
        const int lh = 16;

        auto row = [&](const char* label, const String& val, uint16_t col = COLOR_TEXT) {
            _tft.fillRect(0, y - 2, SCREEN_W, lh, COLOR_BG);
            _tft.setTextColor(COLOR_DIM);
            _tft.setTextFont(1);
            _tft.drawString(label, 8, y);
            _tft.setTextColor(col);
            _tft.drawString(val, 100, y);
            y += lh;
        };

        row("IRQ pin", String(s.irq) + (s.irq ? " HIGH" : " LOW"),
            s.irqSaysDown ? COLOR_WARNING : COLOR_SUCCESS);
        row("raw X", String(s.rawX));
        row("raw Y", String(s.rawY));
        row("raw Z", String(s.rawZ),
            s.pressDown ? COLOR_WARNING : COLOR_DIM);
        row("irqDown", s.irqSaysDown ? "YES" : "no",
            s.irqSaysDown ? COLOR_WARNING : COLOR_DIM);
        row("libTouch", libTouch ? "YES" : "no",
            libTouch ? COLOR_SUCCESS : COLOR_DIM);
        row("pressDown", s.pressDown ? "YES" : "no",
            s.pressDown ? COLOR_WARNING : COLOR_DIM);

        float irqLowPct = sampleCount ? (100.0f * irqLowCount / sampleCount) : 0;
        snprintf(lineBuf, sizeof(lineBuf), "IRQ low %.0f%%", irqLowPct);
        row("stats", lineBuf,
            irqLowPct > 90 ? COLOR_DANGER : COLOR_DIM);

        if (irqLowPct > 90 && pressCount < sampleCount * 0.05f) {
            _tft.setTextColor(COLOR_DANGER);
            _tft.drawString(">> IRQ likely STUCK LOW <<", 8, 210);
        } else if (pressCount > sampleCount * 0.5f && !s.pressDown) {
            _tft.setTextColor(COLOR_DANGER);
            _tft.drawString(">> Z always high? lower thresh", 8, 210);
        }

        _tft.setTextColor(COLOR_DIM);
        _tft.drawString("Touch screen, watch Z + pressEdge", 8, 225);
    }
}

// ────────────────────────────────────────
//  Calibration save/load
// ────────────────────────────────────────

void UI::loadCalibration() {
    Preferences prefs;
    prefs.begin("cyd_cal", true);
    _calLoaded = prefs.getBool("valid", false);
    if (_calLoaded) {
        _cal.xMin = prefs.getInt("xMin", TOUCH_X_MIN);
        _cal.xMax = prefs.getInt("xMax", TOUCH_X_MAX);
        _cal.yMin = prefs.getInt("yMin", TOUCH_Y_MIN);
        _cal.yMax = prefs.getInt("yMax", TOUCH_Y_MAX);
        _cal.xInvert = prefs.getBool("xInv", true);
        _cal.yInvert = prefs.getBool("yInv", true);
        Serial.printf("[CAL] Loaded: X(%d-%d inv=%d) Y(%d-%d inv=%d)\n",
            _cal.xMin, _cal.xMax, _cal.xInvert,
            _cal.yMin, _cal.yMax, _cal.yInvert);
    } else {
        _cal = {TOUCH_X_MIN, TOUCH_X_MAX, TOUCH_Y_MIN, TOUCH_Y_MAX, true, true};
    }
    prefs.end();
}

void UI::saveCalibration() {
    Preferences prefs;
    prefs.begin("cyd_cal", false);
    prefs.putBool("valid", true);
    prefs.putInt("xMin", _cal.xMin);
    prefs.putInt("xMax", _cal.xMax);
    prefs.putInt("yMin", _cal.yMin);
    prefs.putInt("yMax", _cal.yMax);
    prefs.putBool("xInv", _cal.xInvert);
    prefs.putBool("yInv", _cal.yInvert);
    prefs.end();
    _calLoaded = true;
    Serial.printf("[CAL] Saved: X(%d-%d inv=%d) Y(%d-%d inv=%d)\n",
        _cal.xMin, _cal.xMax, _cal.xInvert,
        _cal.yMin, _cal.yMax, _cal.yInvert);
}

// ────────────────────────────────────────
//  Calibration screen
// ────────────────────────────────────────

// Target screen positions for calibration points
// Top-left, Top-right, Bottom-right, Bottom-left
static const int CAL_TARGETS[][2] = {
    {20, 20}, {300, 20}, {300, 220}, {20, 220}
};

void UI::drawCalTarget(int sx, int sy) {
    // Crosshair
    _tft.drawLine(sx - 12, sy, sx + 12, sy, COLOR_ACCENT);
    _tft.drawLine(sx, sy - 12, sx, sy + 12, COLOR_ACCENT);
    _tft.drawCircle(sx, sy, 8, COLOR_ACCENT);
    _tft.fillCircle(sx, sy, 3, COLOR_WARNING);
}

void UI::drawCalibrate() {
    _tft.fillScreen(COLOR_BG);

    if (_calStep == 0) {
        // Intro screen
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_ACCENT);
        _tft.setTextFont(4);
        _tft.drawString("Touch Calibration", SCREEN_W / 2, 60);

        _tft.setTextColor(COLOR_TEXT);
        _tft.setTextFont(2);
        _tft.drawString("Tap each crosshair precisely.", SCREEN_W / 2, 100);
        _tft.drawString("Lift finger between taps.", SCREEN_W / 2, 120);

        _tft.setTextColor(COLOR_WARNING);
        _tft.setTextFont(4);
        _tft.drawString("Tap to start", SCREEN_W / 2, 180);

    } else if (_calStep <= 4) {
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_TEXT);
        _tft.setTextFont(4);
        _tft.drawString("Touch Calibration", SCREEN_W / 2, 60);

        _tft.setTextFont(2);
        _tft.setTextColor(COLOR_DIM);
        String msg = "Tap the crosshair (" + String(_calStep) + "/4)";
        _tft.drawString(msg, SCREEN_W / 2, 90);

        drawCalTarget(CAL_TARGETS[_calStep - 1][0], CAL_TARGETS[_calStep - 1][1]);

    } else {
        // Step 5: verification / free-draw mode
        _tft.setTextDatum(TL_DATUM);
        _tft.setTextColor(COLOR_ACCENT);
        _tft.setTextFont(2);
        _tft.drawString("Verify: tap anywhere", 10, 5);

        _tft.setTextColor(COLOR_DIM);
        _tft.setTextFont(1);
        _tft.drawString("Dots should appear under your finger", 10, 25);

        // Draw grid reference
        for (int gx = 40; gx < SCREEN_W; gx += 40) {
            for (int gy = 40; gy < SCREEN_H; gy += 40) {
                _tft.drawPixel(gx, gy, COLOR_BORDER);
            }
        }

        // DONE and REDO buttons
        _tft.fillRoundRect(SCREEN_W / 2 - 105, 210, 90, 26, 4, COLOR_DANGER);
        _tft.fillRoundRect(SCREEN_W / 2 + 15, 210, 90, 26, 4, COLOR_SUCCESS);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_BG);
        _tft.setTextFont(2);
        _tft.drawString("REDO", SCREEN_W / 2 - 60, 223);
        _tft.drawString("DONE", SCREEN_W / 2 + 60, 223);
    }
}

void UI::handleTouchCalibrate(uint16_t rawX, uint16_t rawY) {
    if (_calStep == 0) {
        // Intro tap -> advance to first crosshair
        _calStep = 1;
        _screenChangeTime = millis();
        drawCalibrate();
        return;
    }

    if (_calStep >= 1 && _calStep <= 4) {
        int idx = _calStep - 1;
        _calRawX[idx] = rawX;
        _calRawY[idx] = rawY;

        // Visual feedback
        int tx = CAL_TARGETS[idx][0];
        int ty = CAL_TARGETS[idx][1];
        _tft.fillCircle(tx, ty, 10, COLOR_SUCCESS);

        _calStep++;
        _screenChangeTime = millis();

        if (_calStep == 5) {
            // All 4 points collected -- calculate calibration
            // Points: TL(0), TR(1), BR(2), BL(3)
            uint16_t rawLeft  = (_calRawX[0] + _calRawX[3]) / 2;
            uint16_t rawRight = (_calRawX[1] + _calRawX[2]) / 2;
            uint16_t rawTop   = (_calRawY[0] + _calRawY[1]) / 2;
            uint16_t rawBot   = (_calRawY[2] + _calRawY[3]) / 2;

            _cal.xInvert = (rawLeft > rawRight);
            _cal.yInvert = (rawTop > rawBot);

            _cal.xMin = min(rawLeft, rawRight);
            _cal.xMax = max(rawLeft, rawRight);
            _cal.yMin = min(rawTop, rawBot);
            _cal.yMax = max(rawTop, rawBot);

            // Extrapolate from 20px margin to full screen edges
            float xRange = _cal.xMax - _cal.xMin;
            float yRange = _cal.yMax - _cal.yMin;
            float xPerPx = xRange / (300.0f - 20.0f);
            float yPerPx = yRange / (220.0f - 20.0f);

            _cal.xMin = _cal.xMin - (int)(20.0f * xPerPx);
            _cal.xMax = _cal.xMax + (int)(20.0f * xPerPx);
            _cal.yMin = _cal.yMin - (int)(20.0f * yPerPx);
            _cal.yMax = _cal.yMax + (int)(20.0f * yPerPx);

            Serial.printf("[CAL] Computed: X(%d-%d inv=%d) Y(%d-%d inv=%d)\n",
                _cal.xMin, _cal.xMax, _cal.xInvert,
                _cal.yMin, _cal.yMax, _cal.yInvert);

            delay(300);
            drawCalibrate();
        } else {
            delay(300);
            drawCalibrate();
        }
        return;
    }

    // Step 5+: verify mode
    {
        // Verify mode: map the raw touch and draw a dot
        int mx, my;
        if (_cal.xInvert)
            mx = map(rawX, _cal.xMax, _cal.xMin, 0, SCREEN_W);
        else
            mx = map(rawX, _cal.xMin, _cal.xMax, 0, SCREEN_W);

        if (_cal.yInvert)
            my = map(rawY, _cal.yMax, _cal.yMin, 0, SCREEN_H);
        else
            my = map(rawY, _cal.yMin, _cal.yMax, 0, SCREEN_H);

        mx = constrain(mx, 0, SCREEN_W - 1);
        my = constrain(my, 0, SCREEN_H - 1);

        Serial.printf("[CAL] verify raw(%d,%d) -> map(%d,%d)\n", rawX, rawY, mx, my);

        // Check REDO button
        if (my >= 205 && mx < SCREEN_W / 2) {
            Serial.println("[CAL] REDO pressed");
            _calStep = 0;
            drawCalibrate();
            return;
        }

        // Check DONE button
        if (my >= 205 && mx >= SCREEN_W / 2) {
            Serial.println("[CAL] DONE pressed - saving");
            saveCalibration();
            enterPostCalFlow();
            return;
        }

        // Draw dot where the mapped touch lands
        _tft.fillCircle(mx, my, 5, COLOR_ACCENT);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_DIM);
        _tft.setTextFont(1);
        String coords = String(mx) + "," + String(my);
        _tft.drawString(coords, mx, my - 12);
    }
}

// ────────────────────────────────────────
//  Network mode picker
// ────────────────────────────────────────

void UI::enterPostCalFlow() {
    if (_onPostCal)
        _onPostCal();
    else
        Serial.println("[BOOT] enterPostCalFlow: no callback registered");
}

void UI::showNetModePicker(bool askEveryBoot) {
    _askNetEveryBoot = askEveryBoot;
    setScreen(SCREEN_NET_MODE);
}

static void drawNetModeTile(TFT_eSPI& tft, int y, const char* title, const char* hint) {
    tft.fillRoundRect(8, y, SCREEN_W - 16, 52, 5, COLOR_CARD);
    tft.drawRoundRect(8, y, SCREEN_W - 16, 52, 5, COLOR_BORDER);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_ACCENT);
    tft.drawString(title, 14, y + 6);
    tft.setTextFont(1);
    tft.setTextColor(COLOR_DIM);
    tft.drawString(hint, 14, y + 26);
}

void UI::drawNetMode() {
    _tft.fillScreen(COLOR_BG);
    _tft.setTextDatum(TC_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    _tft.drawString("SELECT MODE", SCREEN_W / 2, 10);

    drawNetModeTile(_tft, 36, "JOIN NETWORK",
                    "Home LAN intel or Wi-Fi setup");
    drawNetModeTile(_tft, 108, "FIELD RECON",
                    "Explore nearby Wi-Fi + BLE");

    uint16_t chkColor = _askNetEveryBoot ? COLOR_SUCCESS : COLOR_BORDER;
    _tft.drawRoundRect(10, 178, 16, 16, 3, chkColor);
    if (_askNetEveryBoot) {
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_SUCCESS);
        _tft.drawString("X", 18, 186);
        _tft.setTextDatum(TL_DATUM);
    }
    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("Ask every power-on", 32, 180);
}

void UI::handleTouchNetMode(int x, int y) {
    if (y >= 178 && y <= 196 && x < 200) {
        _askNetEveryBoot = !_askNetEveryBoot;
        drawNetMode();
        return;
    }

    int mode = -1;
    if (y >= 30 && y < 98)
        mode = 0;   // NET_MODE_JOIN
    else if (y >= 100 && y < 168)
        mode = 2;   // NET_MODE_FIELD

    if (mode >= 0 && _onNetMode) {
        Serial.printf("[BTN] net_mode: mode=%d ask=%d\n", mode, _askNetEveryBoot ? 1 : 0);
        _onNetMode(mode, _askNetEveryBoot);
    } else {
        Serial.printf("[BTN] net_mode: unhandled x=%d y=%d\n", x, y);
    }
}

// ────────────────────────────────────────
//  WiFi scan screen
// ────────────────────────────────────────

bool UI::wifiScanPortalBanner() const {
    return _statusLine.indexOf("Portal:") >= 0;
}

int UI::wifiScanListTop() const {
    return wifiScanPortalBanner() ? WIFI_LIST_TOP_PORTAL : WIFI_LIST_TOP;
}

int UI::wifiScanMaxVisible() const {
    int avail = WIFI_LIST_BOTTOM - wifiScanListTop();
    return max(1, avail / WIFI_LIST_ROW_H);
}

void UI::setWiFiList(const std::vector<WiFiEntry>& list) {
    _wifiList = list;
    _wifiScroll = 0;
    if (_screen == SCREEN_WIFI_SCAN) drawWiFiScan();
}

void UI::drawWiFiScan() {
    _tft.fillScreen(COLOR_BG);

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    _tft.drawString("WiFi NETWORKS", 8, 8);

    _tft.fillRoundRect(248, 6, 68, 26, 4, COLOR_ACCENT);
    _tft.setTextColor(COLOR_BG);
    _tft.setTextFont(2);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("SCAN", 282, 19);

    _tft.drawFastHLine(0, UI_HEADER_BOTTOM, SCREEN_W, COLOR_BORDER);

    if (wifiScanPortalBanner()) {
        _tft.fillRoundRect(6, 38, SCREEN_W - 12, 34, 4, COLOR_BG2);
        _tft.drawRoundRect(6, 38, SCREEN_W - 12, 34, 4, COLOR_ACCENT);
        _tft.setTextDatum(TL_DATUM);
        _tft.setTextColor(COLOR_ACCENT);
        _tft.setTextFont(1);
        _tft.drawString("Join Wi-Fi: CYBERDECK (open AP)", 12, 44);
        _tft.setTextColor(COLOR_TEXT);
        _tft.drawString("Phone browser: 192.168.4.1", 12, 56);
    }

    int yStart = wifiScanListTop();
    int rowH = WIFI_LIST_ROW_H;
    int maxVisible = wifiScanMaxVisible();
    int listW = WIFI_SCROLL_BTN_X - 6;

    if (_wifiList.empty()) {
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_DIM);
        _tft.setTextFont(2);
        _tft.drawString("Tap SCAN to find networks", SCREEN_W / 2, (yStart + WIFI_LIST_BOTTOM) / 2);
        drawFooterBack("< BACK");
        return;
    }

    for (int i = _wifiScroll; i < (int)_wifiList.size() && i < _wifiScroll + maxVisible; i++) {
        int row = i - _wifiScroll;
        int y = yStart + row * rowH;

        _tft.fillRoundRect(4, y, listW, rowH - 2, 4, COLOR_CARD);

        _tft.setTextDatum(TL_DATUM);
        _tft.setTextColor(COLOR_TEXT);
        _tft.setTextFont(2);

        String label = _wifiList[i].ssid;
        if (label.length() > 22) label = label.substring(0, 20) + "..";
        _tft.drawString(label, 10, y + 3);

        if (_wifiList[i].secure) {
            _tft.setTextColor(COLOR_WARNING);
            _tft.drawString("*", listW - 42, y + 3);
        }

        int bars = map(constrain(_wifiList[i].rssi, -90, -30), -90, -30, 1, 4);
        for (int b = 0; b < 4; b++) {
            uint16_t col = b < bars ? COLOR_SUCCESS : COLOR_BORDER;
            int bh = 3 + b * 3;
            _tft.fillRect(listW - 34 + b * 7, y + rowH - 8 - bh, 5, bh, col);
        }

        _tft.setTextColor(COLOR_DIM);
        _tft.setTextFont(1);
        _tft.drawString(String(_wifiList[i].rssi) + " dBm", 10, y + 18);
    }

    bool canUp = _wifiScroll > 0;
    bool canDn = _wifiScroll + maxVisible < (int)_wifiList.size();
    int scrollUpY = yStart;
    int scrollDnY = yStart + maxVisible * rowH - 28;
    drawScrollBtn(_tft, WIFI_SCROLL_BTN_X, scrollUpY, WIFI_SCROLL_BTN_W, 26, true, canUp);
    drawScrollBtn(_tft, WIFI_SCROLL_BTN_X, scrollDnY, WIFI_SCROLL_BTN_W, 26, false, canDn);

    if ((int)_wifiList.size() > maxVisible) {
        char page[16];
        snprintf(page, sizeof(page), "%d/%d", _wifiScroll + 1,
                 max(1, (int)_wifiList.size() - maxVisible + 1));
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_DIM);
        _tft.setTextFont(1);
        _tft.drawString(page, WIFI_SCROLL_BTN_X + WIFI_SCROLL_BTN_W / 2, scrollUpY + 36);
    }

    drawFooterBack("< BACK");
}

void UI::handleTouchWiFiScan(int x, int y) {
    if (hitFooterBack(x, y)) {
        Serial.println("[BTN] wifi_scan: BACK -> mode picker");
        if (_onWifiScanBack)
            _onWifiScanBack();
        else
            setScreen(SCREEN_MAIN_MENU);
        return;
    }

    // Scan button (top-right)
    if (x >= 250 && y <= 34) {
        Serial.println("[BTN] wifi_scan: SCAN pressed");
        if (_onScan) _onScan();
        return;
    }

    int yStart = wifiScanListTop();
    int rowH = WIFI_LIST_ROW_H;
    int maxVisible = wifiScanMaxVisible();

    if (x >= WIFI_SCROLL_BTN_X && y >= yStart && y < yStart + 28 && _wifiScroll > 0) {
        Serial.println("[BTN] wifi_scan: scroll UP");
        _wifiScroll--;
        drawWiFiScan();
        return;
    }

    int scrollDnY = yStart + maxVisible * rowH - 28;
    if (x >= WIFI_SCROLL_BTN_X && y >= scrollDnY && y < scrollDnY + 28 &&
        _wifiScroll + maxVisible < (int)_wifiList.size()) {
        Serial.println("[BTN] wifi_scan: scroll DOWN");
        _wifiScroll++;
        drawWiFiScan();
        return;
    }

    if (y >= yStart && y < WIFI_LIST_BOTTOM && x < WIFI_SCROLL_BTN_X) {
        int idx = _wifiScroll + (y - yStart) / rowH;
        if (idx >= 0 && idx < (int)_wifiList.size()) {
            _selectedSSID = _wifiList[idx].ssid;
            Serial.printf("[BTN] wifi_scan: selected '%s' (idx=%d)\n",
                          _selectedSSID.c_str(), idx);

            if (!_wifiList[idx].secure) {
                if (_onConnect) _onConnect(_selectedSSID, "");
            } else {
                clearInput();
                setScreen(SCREEN_WIFI_PASS);
            }
            return;
        }
    }
    Serial.printf("[BTN] wifi_scan: unhandled touch x=%d y=%d\n", x, y);
}

// ────────────────────────────────────────
//  WiFi password screen (with keyboard)
// ────────────────────────────────────────

void UI::drawWiFiPass() {
    _tft.fillScreen(COLOR_BG);

    _tft.fillRoundRect(4, 4, 64, 24, 4, COLOR_CARD);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(COLOR_DANGER);
    _tft.setTextFont(2);
    _tft.drawString("CANCEL", 36, 16);

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(1);
    _tft.drawString("Network", 76, 6);

    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextFont(2);
    String ssidDisplay = _selectedSSID;
    if (ssidDisplay.length() > 24) ssidDisplay = ssidDisplay.substring(0, 22) + "..";
    _tft.drawString(ssidDisplay, 76, 18);

    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("Password", 10, 56);

    drawInputField();
    drawKeyboard();
}

void UI::drawInputField() {
    _tft.fillRoundRect(8, KB_INPUT_Y, SCREEN_W - 50, KB_INPUT_H, 4, COLOR_INPUT_BG);
    _tft.drawRoundRect(8, KB_INPUT_Y, SCREEN_W - 50, KB_INPUT_H, 4, COLOR_BORDER);

    // Show/hide toggle
    _tft.fillRoundRect(SCREEN_W - 38, KB_INPUT_Y, 34, KB_INPUT_H, 4,
                        _showPassword ? COLOR_ACCENT : COLOR_CARD);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(_showPassword ? COLOR_BG : COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("EYE", SCREEN_W - 21, KB_INPUT_Y + KB_INPUT_H / 2);

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextFont(2);
    String display;
    if (_showPassword) {
        display = String(_inputBuf);
    } else {
        for (int i = 0; i < _inputLen; i++) display += '*';
    }
    if (display.length() > 22) display = display.substring(display.length() - 22);
    _tft.drawString(display, 14, KB_INPUT_Y + 7);
}

void UI::drawKeyboard() {
    int modeIdx = (_kbMode == KB_NUM) ? 2 : (_kbMode == KB_UPPER ? 1 : 0);

    for (int r = 0; r < 3; r++) {
        const char* row = KB_ROWS[modeIdx][r];
        int len = KB_ROW_LENS[r];
        int startX = (SCREEN_W - len * KB_PITCH + KB_KEY_GAP) / 2;
        int y = KB_START_Y + r * (KB_KEY_H + KB_KEY_GAP);

        for (int c = 0; c < len; c++) {
            int x = startX + c * KB_PITCH;
            _tft.fillRoundRect(x, y, KB_KEY_W, KB_KEY_H, 3, COLOR_KEY_BG);

            _tft.setTextDatum(MC_DATUM);
            _tft.setTextColor(COLOR_TEXT);
            _tft.setTextFont(2);
            char buf[2] = {row[c], 0};
            _tft.drawString(buf, x + KB_KEY_W / 2, y + KB_KEY_H / 2);
        }
    }

    int bottomY = KB_START_Y + 3 * (KB_KEY_H + KB_KEY_GAP);

    // Shift/Caps key
    int shiftW = 40;
    int shiftX = 4;
    _tft.fillRoundRect(shiftX, KB_START_Y + 2 * (KB_KEY_H + KB_KEY_GAP),
                        shiftW, KB_KEY_H, 3,
                        _kbMode == KB_UPPER ? COLOR_ACCENT : COLOR_CARD);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(_kbMode == KB_UPPER ? COLOR_BG : COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("SHIFT", shiftX + shiftW / 2,
                     KB_START_Y + 2 * (KB_KEY_H + KB_KEY_GAP) + KB_KEY_H / 2);

    // Backspace key
    int bsW = 40;
    int bsX = SCREEN_W - bsW - 4;
    _tft.fillRoundRect(bsX, KB_START_Y + 2 * (KB_KEY_H + KB_KEY_GAP),
                        bsW, KB_KEY_H, 3, COLOR_CARD);
    _tft.setTextColor(COLOR_DANGER);
    _tft.drawString("DEL", bsX + bsW / 2,
                     KB_START_Y + 2 * (KB_KEY_H + KB_KEY_GAP) + KB_KEY_H / 2);

    // Bottom row: mode toggle, space, period, OK
    int modeW = 50;
    _tft.fillRoundRect(4, bottomY, modeW, KB_KEY_H, 3, COLOR_CARD);
    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(2);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(_kbMode == KB_NUM ? "ABC" : "123", 4 + modeW / 2, bottomY + KB_KEY_H / 2);

    int spaceX = 4 + modeW + KB_KEY_GAP;
    int okW = 72;
    int dotW = KB_KEY_W;
    int spaceW = SCREEN_W - spaceX - dotW - okW - 3 * KB_KEY_GAP - 4;

    _tft.fillRoundRect(spaceX, bottomY, spaceW, KB_KEY_H, 3, COLOR_KEY_BG);
    _tft.setTextColor(COLOR_DIM);
    _tft.drawString("SPACE", spaceX + spaceW / 2, bottomY + KB_KEY_H / 2);

    int dotX = spaceX + spaceW + KB_KEY_GAP;
    _tft.fillRoundRect(dotX, bottomY, dotW, KB_KEY_H, 3, COLOR_KEY_BG);
    _tft.setTextColor(COLOR_TEXT);
    _tft.drawString(".", dotX + dotW / 2, bottomY + KB_KEY_H / 2);

    int okX = dotX + dotW + KB_KEY_GAP;
    _tft.fillRoundRect(okX, bottomY, okW, KB_KEY_H, 3, COLOR_SUCCESS);
    _tft.setTextColor(COLOR_BG);
    _tft.drawString("CONNECT", okX + okW / 2, bottomY + KB_KEY_H / 2);
}

char UI::getKeyAt(int x, int y) {
    int modeIdx = (_kbMode == KB_NUM) ? 2 : (_kbMode == KB_UPPER ? 1 : 0);

    for (int r = 0; r < 3; r++) {
        const char* row = KB_ROWS[modeIdx][r];
        int len = KB_ROW_LENS[r];
        int startX = (SCREEN_W - len * KB_PITCH + KB_KEY_GAP) / 2;
        int ky = KB_START_Y + r * (KB_KEY_H + KB_KEY_GAP);

        if (y < ky || y > ky + KB_KEY_H) continue;

        for (int c = 0; c < len; c++) {
            int kx = startX + c * KB_PITCH;
            if (x >= kx && x <= kx + KB_KEY_W) {
                return row[c];
            }
        }
    }
    return 0;
}

void UI::handleTouchWiFiPass(int x, int y) {
    if (x <= 72 && y <= 30) {
        Serial.println("[BTN] wifi_pass: CANCEL -> wifi_scan");
        setScreen(SCREEN_WIFI_SCAN);
        return;
    }

    // Show/hide password toggle
    if (x >= SCREEN_W - 38 && y >= KB_INPUT_Y && y <= KB_INPUT_Y + KB_INPUT_H) {
        _showPassword = !_showPassword;
        drawInputField();
        return;
    }

    int bottomY = KB_START_Y + 3 * (KB_KEY_H + KB_KEY_GAP);

    // Shift key
    int shiftRow = KB_START_Y + 2 * (KB_KEY_H + KB_KEY_GAP);
    if (x < 44 && y >= shiftRow && y <= shiftRow + KB_KEY_H) {
        _kbMode = (_kbMode == KB_UPPER) ? KB_LOWER : KB_UPPER;
        drawKeyboard();
        return;
    }

    // Backspace key
    if (x > SCREEN_W - 44 && y >= shiftRow && y <= shiftRow + KB_KEY_H) {
        backspaceInput();
        drawInputField();
        return;
    }

    // Bottom row
    if (y >= bottomY && y <= bottomY + KB_KEY_H) {
        if (x < 54) {
            _kbMode = (_kbMode == KB_NUM) ? KB_LOWER : KB_NUM;
            drawKeyboard();
            return;
        }

        int modeW = 50;
        int spaceX = 4 + modeW + KB_KEY_GAP;
        int okW = 72;
        int dotW = KB_KEY_W;
        int spaceW = SCREEN_W - spaceX - dotW - okW - 3 * KB_KEY_GAP - 4;
        int dotX = spaceX + spaceW + KB_KEY_GAP;
        int okX = dotX + dotW + KB_KEY_GAP;

        if (x >= spaceX && x <= spaceX + spaceW) {
            appendInput(' ');
            drawInputField();
            return;
        }
        if (x >= dotX && x <= dotX + dotW) {
            appendInput('.');
            drawInputField();
            return;
        }
        if (x >= okX) {
            if (_onConnect) _onConnect(_selectedSSID, String(_inputBuf));
            return;
        }
        return;
    }

    // Character keys
    char c = getKeyAt(x, y);
    if (c) {
        appendInput(c);
        drawInputField();
        if (_kbMode == KB_UPPER) {
            _kbMode = KB_LOWER;
            drawKeyboard();
        }
    }
}

// ────────────────────────────────────────
//  Connecting screen
// ────────────────────────────────────────

void UI::drawConnecting() {
    _tft.fillScreen(COLOR_BG);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(4);
    _tft.drawString("Connecting...", SCREEN_W / 2, 80);

    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(2);
    _tft.drawString(_selectedSSID, SCREEN_W / 2, 120);

    for (int i = 0; i < 3; i++) {
        int cx = SCREEN_W / 2 - 20 + i * 20;
        _tft.fillCircle(cx, 160, 4, COLOR_BORDER);
    }

    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("Tap anywhere or CANCEL below", SCREEN_W / 2, 188);
    drawFooterBack("CANCEL");
}

void UI::setConnectionState(bool ok) {
    _isConnected = ok;
    if (!ok) {
        _connectedIP = "";
    }
}

void UI::updateLinkState(bool ok, const String& ip, const String& ssid) {
    _isConnected = ok;
    if (ip.length() > 0) _connectedIP = ip;
    if (ssid.length() > 0) _selectedSSID = ssid;
    if (!_qrOverlayActive &&
        (_screen == SCREEN_MAIN_MENU || _screen == SCREEN_INTEL_LIVE ||
         _screen == SCREEN_STATUS))
        refreshStatusBar();
}

void UI::setConnected(bool ok, const String& ip, const String& ssid) {
    _isConnected = ok;
    _connectedIP = ip;
    if (ssid.length() > 0)
        _selectedSSID = ssid;
    else if (ok && WiFi.status() == WL_CONNECTED)
        _selectedSSID = WiFi.SSID();

    if (ok) {
        _tft.fillScreen(COLOR_BG);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_SUCCESS);
        _tft.setTextFont(4);
        _tft.drawString("Connected!", SCREEN_W / 2, 80);

        _tft.setTextColor(COLOR_TEXT);
        _tft.setTextFont(2);
        _tft.drawString("IP: " + ip, SCREEN_W / 2, 110);
        if (_selectedSSID.length() > 0)
            _tft.drawString(_selectedSSID, SCREEN_W / 2, 135);

        delay(1500);
        setScreen(SCREEN_MAIN_MENU);
    } else {
        _tft.fillScreen(COLOR_BG);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(COLOR_DANGER);
        _tft.setTextFont(4);
        _tft.drawString("Failed", SCREEN_W / 2, 80);

        _tft.setTextColor(COLOR_DIM);
        _tft.setTextFont(2);
        _tft.drawString("Tap to retry", SCREEN_W / 2, 130);

        delay(2000);
        setScreen(SCREEN_WIFI_SCAN);
    }
}

// ────────────────────────────────────────
//  Main menu
// ────────────────────────────────────────

static bool intelChanged(int h, int a, uint8_t p, const char* phase, const char* act,
                         int& lh, int& la, uint8_t& lp, char* lphase, char* lact) {
    if (h != lh || a != la || p != lp || strcmp(phase, lphase) != 0 || strcmp(act, lact) != 0) {
        lh = h; la = a; lp = p;
        strncpy(lphase, phase, 19);
        lphase[19] = '\0';
        strncpy(lact, act, 63);
        lact[63] = '\0';
        return true;
    }
    return false;
}

void UI::setIntelStatus(int hosts, int aps, const char* phase, uint8_t progress,
                        const char* activityLine) {
    _intelHosts = hosts;
    _intelAps = aps;
    strncpy(_intelPhase, phase ? phase : "?", sizeof(_intelPhase) - 1);
    _intelProgress = progress;
    if (activityLine)
        strncpy(_intelActivity, activityLine, sizeof(_intelActivity) - 1);

    if (!intelChanged(hosts, aps, progress, _intelPhase, _intelActivity,
                      _lastIntelHosts, _lastIntelAps, _lastIntelProgress,
                      _lastIntelPhase, _lastIntelActivity))
        return;

    if (_qrOverlayActive)
        return;

    if (_screen == SCREEN_MAIN_MENU)
        refreshIntelPanel();
    else if (_screen == SCREEN_INTEL_LIVE)
        drawIntelLive();
}

bool UI::hitFooterBack(int x, int y) const {
    (void)x;
    return (y >= UI_FOOTER_TOP && y <= SCREEN_H - 1);
}

void UI::drawFooterBack(const char* label) {
    _tft.fillRect(0, UI_FOOTER_TOP, SCREEN_W, SCREEN_H - UI_FOOTER_TOP, COLOR_BG);
    _tft.drawFastHLine(0, UI_FOOTER_TOP, SCREEN_W, COLOR_BORDER);
    _tft.fillRoundRect(8, UI_FOOTER_DRAW_Y, SCREEN_W - 16, UI_FOOTER_H, 4, COLOR_CARD);
    _tft.drawRoundRect(8, UI_FOOTER_DRAW_Y, SCREEN_W - 16, UI_FOOTER_H, 4, COLOR_ACCENT);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    _tft.drawString(label, SCREEN_W / 2, UI_FOOTER_DRAW_Y + UI_FOOTER_H / 2);
}

static void drawScrollBtn(TFT_eSPI& tft, int x, int y, int w, int h, bool up, bool enabled) {
    uint16_t fill = enabled ? COLOR_CARD : COLOR_BG2;
    uint16_t edge = enabled ? COLOR_ACCENT : COLOR_BORDER;
    tft.fillRoundRect(x, y, w, h, 3, fill);
    tft.drawRoundRect(x, y, w, h, 3, edge);
    if (!enabled) return;
    int cx = x + w / 2;
    int cy = y + h / 2;
    if (up)
        tft.fillTriangle(cx, cy - 5, cx - 7, cy + 4, cx + 7, cy + 4, COLOR_ACCENT);
    else
        tft.fillTriangle(cx, cy + 5, cx - 7, cy - 4, cx + 7, cy - 4, COLOR_ACCENT);
}

// ────────────────────────────────────────
//  Roaming recon screen
// ────────────────────────────────────────

// Fixed layout (320×240) — draw + touch must share these constants.
// Font 2 ≈16px tall; font 1 ≈8px. Leave explicit gaps so rows never overlap labels.
static constexpr int ROAM_TOGGLE_Y   = 26;
static constexpr int ROAM_TOGGLE_H   = 24;
static constexpr int ROAM_STATS_Y    = 52;
static constexpr int ROAM_STAT2_Y    = ROAM_STATS_Y + 18;   // below CH/AP line (font 2)
static constexpr int ROAM_HINT_Y     = ROAM_STAT2_Y + 12;   // below REC/HS line (font 1)
static constexpr int ROAM_LIST_Y     = ROAM_HINT_Y + 14;    // below hint + breathing room
static constexpr int ROAM_ROW_H      = 22;
static constexpr int ROAM_MAX_ROWS   = 5;
static constexpr int ROAM_FOOTER_TOP = UI_FOOTER_TOP;

static int roamVisibleRows() {
    int avail = ROAM_FOOTER_TOP - ROAM_LIST_Y;
    int byH = avail / ROAM_ROW_H;
    return byH < ROAM_MAX_ROWS ? byH : ROAM_MAX_ROWS;
}

void UI::setRoamingView(int aps, int clients, int probes, int handshakes, int ble,
                        uint8_t channel, uint32_t frames, bool capturing, bool armed,
                        bool bleOn, bool listIsBle, const char* lines[8], int lineCount) {
    _roamAps = aps;
    _roamClients = clients;
    _roamProbes = probes;
    _roamHandshakes = handshakes;
    _roamBle = ble;
    _roamChannel = channel;
    _roamFrames = frames;
    _roamCapturing = capturing;
    _roamArmed = armed;
    _roamBleOn = bleOn;
    _roamListIsBle = listIsBle;
    _roamLineN = lineCount < 8 ? lineCount : 8;
    for (int i = 0; i < _roamLineN; i++) {
        strncpy(_roamLines[i], lines[i] ? lines[i] : "", 31);
        _roamLines[i][31] = '\0';
    }
    if (_screen == SCREEN_ROAMING)
        refreshRoamingPanel();
}

void UI::drawRoaming() {
    _tft.fillScreen(COLOR_BG);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_ACCENT2);
    _tft.setTextFont(2);
    _tft.drawString("FIELD RECON", 10, 6);
    refreshRoamingPanel();
}

// Three compact toggles: ARM (engage), CAP (pcap), BLE (bluetooth scan).
static void drawToggleBtn(TFT_eSPI& tft, int bx, int by, int bw, int bh,
                          const char* label, bool on, uint16_t onColor, bool disabled) {
    uint16_t fill = (on && !disabled) ? onColor : COLOR_CARD;
    uint16_t border = disabled ? COLOR_BORDER : (on ? onColor : COLOR_BORDER);
    tft.fillRoundRect(bx, by, bw, bh, 4, fill);
    tft.drawRoundRect(bx, by, bw, bh, 4, border);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(disabled ? COLOR_DIM : (on ? COLOR_BG : COLOR_TEXT));
    tft.drawString(label, bx + bw / 2, by + bh / 2);
}

void UI::drawRoamButtons() {
    const int by = ROAM_TOGGLE_Y, bw = 60, bh = ROAM_TOGGLE_H - 2, gap = 6;
    int bx = 10;
    drawToggleBtn(_tft, bx, by, bw, bh, _roamArmed ? "ARMED" : "ARM",
                  _roamArmed, COLOR_DANGER, false);
    bx += bw + gap;
    const char* capLabel = !_roamSdAvailable ? "NO SD" : (_roamCapturing ? "REC" : "CAP");
    drawToggleBtn(_tft, bx, by, bw, bh, capLabel, _roamCapturing, COLOR_SUCCESS,
                  !_roamSdAvailable);
    bx += bw + gap;
    drawToggleBtn(_tft, bx, by, bw, bh, _roamBleOn ? "BLE*" : "BLE",
                  _roamBleOn, COLOR_ACCENT, false);
    // LIST toggle (Wi-Fi vs BLE) on the far right
    bx = SCREEN_W - bw - 10;
    drawToggleBtn(_tft, bx, by, bw, bh, _roamListIsBle ? "BLE>" : "WIFI>",
                  _roamListIsBle, COLOR_ACCENT2, false);
}

void UI::refreshRoamingPanel() {
    if (_qrOverlayActive) return;
    drawRoamButtons();

    // Content band only — never paint over the footer zone.
    _tft.fillRect(0, ROAM_STATS_Y, SCREEN_W, ROAM_FOOTER_TOP - ROAM_STATS_Y, COLOR_BG);

    char line[44];
    snprintf(line, sizeof(line), "CH%-2u AP:%d CLI:%d PRB:%d BLE:%d",
             (unsigned)_roamChannel, _roamAps, _roamClients, _roamProbes, _roamBle);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    _tft.drawString(line, 10, ROAM_STATS_Y);

    _tft.setTextFont(1);
    _tft.setTextColor(_roamCapturing ? COLOR_SUCCESS : COLOR_DIM);
    snprintf(line, sizeof(line), "REC:%s  HS:%d  frames:%lu",
             _roamCapturing ? "ON" : "off", _roamHandshakes, (unsigned long)_roamFrames);
    _tft.drawString(line, 10, ROAM_STAT2_Y);

    _tft.setTextColor(COLOR_DIM);
    if (_roamListIsBle)
        _tft.drawString("BLE devices", 10, ROAM_HINT_Y);
    else
        _tft.drawString(_roamArmed ? "Tap AP row to deauth (armed)"
                                   : "Nearby access points", 10, ROAM_HINT_Y);

    _tft.drawFastHLine(8, ROAM_LIST_Y - 3, SCREEN_W - 16, COLOR_BORDER);

    int maxRows = roamVisibleRows();
    int n = _roamLineN < maxRows ? _roamLineN : maxRows;

    if (n == 0) {
        _tft.setTextColor(COLOR_DIM);
        _tft.drawString("Listening…", 14, ROAM_LIST_Y + 4);
    } else {
        for (int i = 0; i < n; i++) {
            int ry = ROAM_LIST_Y + i * ROAM_ROW_H;
            bool hs = strstr(_roamLines[i], " HS") != nullptr;
            bool engage = !_roamListIsBle && _roamArmed;
            uint16_t fill = COLOR_CARD;
            uint16_t border = COLOR_BORDER;
            if (engage)
                border = COLOR_DANGER;
            else if (hs)
                border = COLOR_WARNING;
            _tft.fillRoundRect(4, ry, SCREEN_W - 8, ROAM_ROW_H - 2, 3, fill);
            _tft.drawRoundRect(4, ry, SCREEN_W - 8, ROAM_ROW_H - 2, 3, border);

            uint16_t col = COLOR_TEXT;
            if (engage) col = COLOR_DANGER;
            else if (hs) col = COLOR_WARNING;
            _tft.setTextColor(col);
            _tft.setTextFont(1);
            _tft.setTextDatum(TL_DATUM);
            _tft.drawString(_roamLines[i], 12, ry + 5);
        }
    }

    _tft.drawFastHLine(0, ROAM_FOOTER_TOP - 1, SCREEN_W, COLOR_BORDER);
    drawFooterBack("CHANGE MODE");
}

void UI::handleTouchRoaming(int x, int y) {
    if (y >= ROAM_FOOTER_TOP && hitFooterBack(x, y)) {
        Serial.println("[BTN] roam: CHANGE MODE");
        if (_onWifiScanBack)
            _onWifiScanBack();
        else
            showNetModePicker(true);
        return;
    }

    // Toggle row — generous vertical hit slop (±2px).
    if (y >= ROAM_TOGGLE_Y - 2 && y <= ROAM_TOGGLE_Y + ROAM_TOGGLE_H + 2) {
        const int bw = 60, gap = 6;
        int armX = 10, capX = armX + bw + gap, bleX = capX + bw + gap;
        int listX = SCREEN_W - bw - 10;
        if (x >= armX && x < armX + bw) {
            _roamArmed = !_roamArmed;
            if (_onRoamArm) _onRoamArm();
            refreshRoamingPanel();
        } else if (x >= capX && x < capX + bw) {
            if (_roamSdAvailable) {
                _roamCapturing = !_roamCapturing;
                if (_onRoamCapture) _onRoamCapture();
                refreshRoamingPanel();
            }
        } else if (x >= bleX && x < bleX + bw) {
            _roamBleOn = !_roamBleOn;
            if (_onRoamBle) _onRoamBle();
            refreshRoamingPanel();
        } else if (x >= listX && x < listX + bw) {
            _roamListIsBle = !_roamListIsBle;
            if (_onRoamListToggle) _onRoamListToggle();
            refreshRoamingPanel();
        }
        return;
    }

    // AP engage rows (Wi‑Fi list only, when armed).
    if (!_roamListIsBle && _roamArmed && _onRoamEngage) {
        int maxRows = roamVisibleRows();
        int n = _roamLineN < maxRows ? _roamLineN : maxRows;
        if (y >= ROAM_LIST_Y && y < ROAM_LIST_Y + n * ROAM_ROW_H && x >= 4 && x < SCREEN_W - 4) {
            int idx = (y - ROAM_LIST_Y) / ROAM_ROW_H;
            if (idx >= 0 && idx < n) {
                Serial.printf("[BTN] roam: engage row %d\n", idx);
                _onRoamEngage(idx);
            }
        }
    }
}

void UI::refreshStatusBar() {
    if (_qrOverlayActive) return;
    String ip = _isConnected ? _connectedIP : String("");
    if (ip == _lastStatusIp) return;
    strncpy(_lastStatusIp, ip.c_str(), sizeof(_lastStatusIp) - 1);
    _tft.fillRect(0, 0, SCREEN_W, 28, COLOR_BG2);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(_isConnected ? COLOR_SUCCESS : COLOR_DANGER);
    _tft.setTextFont(2);
    _tft.drawString(_isConnected ? "WiFi OK" : "No WiFi", 8, 4);
    if (_isConnected && ip.length() > 0) {
        _tft.setTextDatum(TR_DATUM);
        _tft.setTextColor(COLOR_TEXT);
        String ipShow = ip;
        if (ipShow.length() > 15) ipShow = ipShow.substring(0, 13) + "..";
        _tft.drawString(ipShow, SCREEN_W - 8, 4);
    }
}

void UI::setPreviewRows(const char* hostLines[4], int hostLineCount,
                        const char* wifiLines[3], int wifiLineCount) {
    _previewHostN = min(hostLineCount, 4);
    _previewWifiN = min(wifiLineCount, 3);
    for (int i = 0; i < _previewHostN; i++) {
        strncpy(_previewHosts[i], hostLines[i] ? hostLines[i] : "", 27);
        _previewHosts[i][27] = '\0';
    }
    for (int i = 0; i < _previewWifiN; i++) {
        strncpy(_previewWifi[i], wifiLines[i] ? wifiLines[i] : "", 27);
        _previewWifi[i][27] = '\0';
    }
    if (_screen == SCREEN_MAIN_MENU && !_qrOverlayActive)
        refreshPreviewPanel();
}

void UI::refreshPreviewPanel() {
    if (_qrOverlayActive) return;
    const int y0 = 94;
    const int h = 72;
    _tft.fillRect(6, y0, SCREEN_W - 12, h, COLOR_BG);
    _tft.drawRoundRect(6, y0, SCREEN_W - 12, h, 4, COLOR_BORDER);

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("LAN / RF preview", 10, y0 + 2);

    int y = y0 + 12;
    int hostShow = min(_previewHostN, 2);
    if (hostShow == 0) {
        const char* wait = intelPhaseActive(_intelPhase) ? "  scanning hosts..." : "  no hosts yet";
        _tft.setTextColor(COLOR_TEXT);
        _tft.drawString(wait, 10, y);
        y += 11;
    } else {
        for (int i = 0; i < hostShow; i++) {
            _tft.setTextColor(COLOR_TEXT);
            _tft.drawString(_previewHosts[i], 10, y);
            y += 11;
        }
    }

    _tft.setTextColor(COLOR_DIM);
    _tft.drawString("Wi-Fi:", 10, y);
    y += 11;
    int wifiShow = min(_previewWifiN, 2);
    if (wifiShow == 0) {
        _tft.setTextColor(COLOR_TEXT);
        _tft.drawString("  waiting for scan", 10, y);
    } else {
        for (int i = 0; i < wifiShow; i++) {
            _tft.setTextColor(COLOR_TEXT);
            _tft.drawString(_previewWifi[i], 10, y);
            y += 11;
        }
    }

    if (_isConnected && _connectedIP.length() > 0) {
        _tft.fillRoundRect(8, y0 + h - 16, SCREEN_W - 16, 14, 3, COLOR_CARD);
        _tft.setTextColor(COLOR_ACCENT);
        _tft.setTextFont(1);
        String url = _connectedIP + " (tap preview)";
        if (url.length() > 28) url = url.substring(0, 26) + "..";
        _tft.drawString(url, 12, y0 + h - 12);
    }
}

void UI::refreshIntelPanel() {
    if (_qrOverlayActive) return;
    const int y0 = 50;
    const int h = 40;
    _tft.fillRect(6, y0, SCREEN_W - 12, h, COLOR_BG);

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_WARNING);
    _tft.setTextFont(1);
    _tft.drawString("SCAN ENGINE (tap)", 10, y0 + 2);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    String live = String(_intelHosts) + " hosts | " + String(_intelAps) + " APs";
    _tft.drawString(live, 10, y0 + 14);
    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString(_intelActivity[0] ? _intelActivity : _intelPhase, 10, y0 + 28);

    _tft.fillRoundRect(10, y0 + 40, SCREEN_W - 20, 6, 2, COLOR_BORDER);
    bool active = intelPhaseActive(_intelPhase);
    if (active) {
        int pw = map(_intelProgress, 0, 100, 0, SCREEN_W - 20);
        if (pw > 0)
            _tft.fillRoundRect(10, y0 + 40, pw, 6, 2, COLOR_ACCENT);
        char pct[8];
        snprintf(pct, sizeof(pct), "%u%%", (unsigned)_intelProgress);
        _tft.setTextDatum(TR_DATUM);
        _tft.setTextColor(COLOR_TEXT);
        _tft.drawString(pct, SCREEN_W - 10, y0 + 38);
    } else {
        _tft.setTextDatum(TR_DATUM);
        _tft.setTextColor(COLOR_DIM);
        _tft.drawString("idle", SCREEN_W - 10, y0 + 38);
    }
}

void UI::setShareStatus(const char* msg) {
    if (msg) strncpy(_shareStatus, msg, sizeof(_shareStatus) - 1);
    _shareStatus[sizeof(_shareStatus) - 1] = '\0';
    if (_screen == SCREEN_SHARE_INTEL)
        drawShareIntel();
}

void UI::drawMainMenu() {
    _tft.fillScreen(COLOR_BG);
    _lastStatusIp[0] = '\0';
    _lastIntelHosts = -1;
    drawStatusBar();

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    _tft.drawString("CYBERDECK HOME", 10, 30);
    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("Network intelligence hub", 10, 42);

    refreshIntelPanel();
    refreshPreviewPanel();

    _tft.setTextDatum(MC_DATUM);
    _tft.setTextFont(1);
    const int btnH = 22;
    const int row1 = 172;
    const int row2 = 198;
    const int w = 152;

    if (_isConnected && _connectedIP.length() > 0) {
        _tft.fillRoundRect(6, row1, w, btnH, 3, COLOR_ACCENT);
        _tft.setTextColor(COLOR_BG);
        _tft.drawString("WEB DASH", 82, row1 + btnH / 2);

        _tft.fillRoundRect(162, row1, w, btnH, 3, COLOR_CARD);
        _tft.setTextColor(COLOR_WARNING);
        _tft.drawString("SCAN", 238, row1 + btnH / 2);

        _tft.fillRoundRect(6, row2, w, btnH, 3, COLOR_SUCCESS);
        _tft.setTextColor(COLOR_BG);
        _tft.drawString("SHARE", 82, row2 + btnH / 2);

        _tft.fillRoundRect(162, row2, w, btnH, 3, COLOR_CARD);
        _tft.setTextColor(COLOR_ACCENT);
        _tft.drawString("STATUS", 238, row2 + btnH / 2);
    } else {
        _tft.fillRoundRect(6, row1, w, btnH, 3, COLOR_ACCENT);
        _tft.setTextColor(COLOR_BG);
        _tft.drawString("WiFi SETUP", 82, row1 + btnH / 2);

        _tft.fillRoundRect(162, row1, w, btnH, 3, COLOR_CARD);
        _tft.setTextColor(COLOR_ACCENT);
        _tft.drawString("STATUS", 238, row1 + btnH / 2);
    }
}

void UI::drawShareIntel() {
    _tft.fillScreen(COLOR_BG);
    drawStatusBar();

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_SUCCESS);
    _tft.setTextFont(2);
    _tft.drawString("SHARE INTEL", 10, 36);

    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextFont(1);
    int y = 58;
    _tft.drawString("Snapshot hosts + Wi-Fi to flash", 10, y);
    y += 14;
    _tft.drawString("Open on phone / laptop:", 10, y);
    y += 16;

    if (_isConnected && _connectedIP.length() > 0) {
        _tft.setTextColor(COLOR_ACCENT);
        String url = "http://" + _connectedIP + "/";
        _tft.drawString(url, 10, y);
        y += 14;
        _tft.setTextColor(COLOR_DIM);
        _tft.drawString("/api/intel/export  JSON", 10, y);
        y += 12;
        _tft.drawString("/api/intel/report  text", 10, y);
        y += 12;
        _tft.drawString("/api/intel/log      events", 10, y);
    }

    y += 10;
    _tft.setTextColor(COLOR_TEXT);
    String stats = String(_intelHosts) + " hosts  " + String(_intelAps) + " APs";
    _tft.drawString(stats, 10, y);
    y += 14;
    if (_shareStatus[0]) {
        _tft.setTextColor(COLOR_SUCCESS);
        _tft.drawString(_shareStatus, 10, y);
    }

    _tft.fillRoundRect(20, 158, SCREEN_W - 40, 28, 4, COLOR_ACCENT);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(COLOR_BG);
    _tft.setTextFont(2);
    _tft.drawString("SAVE SNAPSHOT", SCREEN_W / 2, 172);

    drawFooterBack("< HOME");
}

void UI::drawIntelLive() {
    _tft.fillScreen(COLOR_BG);
    drawStatusBar();

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_WARNING);
    _tft.setTextFont(2);
    _tft.drawString("SCAN ENGINE", 10, 36);

    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(4);
    _tft.drawString(_intelPhase, 10, 58);

    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextFont(2);
    String counts = String(_intelHosts) + " hosts  " + String(_intelAps) + " APs";
    _tft.drawString(counts, 10, 88);

    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString(_intelActivity, 10, 108);

    _tft.drawString("Progress:", 10, 128);
    _tft.fillRoundRect(10, 142, SCREEN_W - 20, 10, 2, COLOR_BORDER);
    bool active = intelPhaseActive(_intelPhase);
    if (active) {
        int pw = map(_intelProgress, 0, 100, 0, SCREEN_W - 20);
        if (pw > 0)
            _tft.fillRoundRect(10, 142, pw, 10, 2, COLOR_ACCENT);
        char pct[12];
        snprintf(pct, sizeof(pct), "%u%%", (unsigned)_intelProgress);
        _tft.setTextColor(COLOR_ACCENT);
        _tft.setTextFont(2);
        _tft.drawString(pct, 10, 156);
    } else {
        _tft.setTextColor(COLOR_DIM);
        _tft.setTextFont(2);
        _tft.drawString("idle — bar only moves during scan", 10, 156);
    }

    _tft.setTextColor(COLOR_DIM);
    _tft.setTextFont(1);
    _tft.drawString("Full dashboard on phone browser", 10, 172);
    if (_isConnected && _connectedIP.length() > 0) {
        _tft.setTextColor(COLOR_ACCENT);
        String url = "http://" + _connectedIP + "/";
        if (url.length() > 36) url = url.substring(0, 34) + "..";
        _tft.drawString(url, 10, 184);
    }

    drawFooterBack("< BACK");
}

void UI::drawStatusBar() {
    refreshStatusBar();
}

void UI::handleTouchMainMenu(int x, int y) {
    if (y >= 48 && y <= 92) {
        Serial.println("[BTN] home: SCAN panel");
        setScreen(SCREEN_INTEL_LIVE);
        return;
    }

    if (y >= 168) {
        bool topRow = y < 192;
        bool left = x < 160;
        if (_isConnected && _connectedIP.length() > 0) {
            if (topRow && left) {
                String url = "http://" + _connectedIP + "/";
                showOverlayMessage("Open in phone browser:\n" + url);
                return;
            }
            if (topRow && !left) {
                setScreen(SCREEN_INTEL_LIVE);
                return;
            }
            if (!topRow && left) {
                showDashboardQr("http://" + _connectedIP + "/");
                return;
            }
            setScreen(SCREEN_STATUS);
            return;
        }
        if (left) {
            if (_onReconnect) _onReconnect();
            else if (_onScan) { setScreen(SCREEN_WIFI_SCAN); _onScan(); }
            return;
        }
        setScreen(SCREEN_STATUS);
        return;
    }

    if (y >= 92 && y <= 168 && _isConnected && _connectedIP.length() > 0) {
        String url = "http://" + _connectedIP + "/";
        showOverlayMessage("Open in phone browser:\n" + url);
        return;
    }

    Serial.printf("[BTN] home: touch x=%d y=%d\n", x, y);
}

void UI::handleTouchShareIntel(int x, int y) {
    if (hitFooterBack(x, y)) {
        setScreen(SCREEN_MAIN_MENU);
        return;
    }
    if (y >= 152 && y < UI_FOOTER_TOP && x >= 16 && x <= SCREEN_W - 16) {
        setShareStatus("Saving…");
        if (_onShareIntel) _onShareIntel();
        else setShareStatus("Share not wired");
        return;
    }
    if (_isConnected && _connectedIP.length() > 0) {
        String url = "http://" + _connectedIP + "/";
        showOverlayMessage("Share from any browser:\n" + url +
                          "\n/api/intel/export\n/api/intel/report");
    }
}

void UI::handleTouchIntelLive(int x, int y) {
    if (hitFooterBack(x, y)) {
        Serial.println("[BTN] intel_live: BACK -> main_menu");
        setScreen(SCREEN_MAIN_MENU);
        return;
    }
    Serial.printf("[BTN] intel_live: touch x=%d y=%d\n", x, y);
}

// ────────────────────────────────────────
//  Status screen
// ────────────────────────────────────────

void UI::drawStatusScreen() {
    _tft.fillScreen(COLOR_BG);
    drawStatusBar();

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextFont(2);
    _tft.drawString("DEVICE STATUS", 10, 32);
    _tft.drawFastHLine(0, 48, SCREEN_W, COLOR_BORDER);

    _tft.setTextFont(1);
    const int lineH = 13;
    const int valX = 52;
    int y = 52;

    auto truncStr = [](const String& s, int maxLen) -> String {
        if ((int)s.length() <= maxLen) return s;
        return s.substring(0, maxLen - 2) + "..";
    };

    auto row = [&](const char* label, const String& val, uint16_t valColor = COLOR_TEXT) {
        _tft.setTextColor(COLOR_DIM);
        _tft.drawString(label, 8, y);
        _tft.setTextColor(valColor);
        _tft.drawString(truncStr(val, 40), valX, y);
        y += lineH;
    };

    row("Link:", _isConnected ? "Connected" : "Disconnected",
        _isConnected ? COLOR_SUCCESS : COLOR_DANGER);

    String ssid = _selectedSSID.length() > 0 ? _selectedSSID :
                  (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("—"));
    row("SSID:", ssid);

    String ip = _connectedIP.length() > 0 ? _connectedIP :
                (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("—"));
    row("IP:", ip);

    if (_isConnected && ip.length() > 0 && ip != "—") {
        row("Web:", truncStr("http://" + ip + "/", 40), COLOR_ACCENT);
    }

    row("Scan:", _intelPhase);
    row("Intel:", String(_intelHosts) + "h " + String(_intelAps) + " APs");
    row("Mem:", String(ESP.getFreeHeap() / 1024) + "KB  Up:" + String(millis() / 60000) + "m");

    _tft.fillRoundRect(8, 132, SCREEN_W - 16, 24, 4, COLOR_ACCENT2);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextFont(2);
    _tft.drawString("CHANGE NETWORK", SCREEN_W / 2, 144);

    const int btnY = 160;
    const int btnH = 24;
    _tft.fillRoundRect(8, btnY, 148, btnH, 4, COLOR_DANGER);
    _tft.fillRoundRect(164, btnY, 148, btnH, 4, COLOR_WARNING);

    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(COLOR_BG);
    _tft.setTextFont(2);
    _tft.drawString("FORGET", 82, btnY + btnH / 2);
    _tft.drawString("RE-CAL", 238, btnY + btnH / 2);

    drawFooterBack("< BACK");
}

void UI::handleTouchStatus(int x, int y) {
    if (hitFooterBack(x, y)) {
        Serial.println("[BTN] status: BACK -> main_menu");
        setScreen(SCREEN_MAIN_MENU);
        return;
    }
    if (y >= 130 && y <= 158) {
        Serial.println("[BTN] status: NETWORK MODE");
        showNetModePicker();
        return;
    }
    if (y >= 158 && y <= 186 && x < 160) {
        Serial.println("[BTN] status: FORGET WiFi");
        if (_onForgetWifi) _onForgetWifi();
        showToast("WiFi forgotten", COLOR_WARNING);
        setConnectionState(false);
        setScreen(SCREEN_WIFI_SCAN);
        if (_onScan) _onScan();
        return;
    }
    if (y >= 158 && y <= 186 && x >= 160) {
        Serial.println("[BTN] status: RECALIBRATE");
        _calStep = 0;
        _calLoaded = false;
        setScreen(SCREEN_CALIBRATE);
        return;
    }
    {
        Serial.printf("[BTN] status: unhandled touch x=%d y=%d\n", x, y);
    }
}

// ────────────────────────────────────────
//  Toast & Overlay
// ────────────────────────────────────────

void UI::showToast(const String& msg, uint16_t color) {
    _tft.fillRoundRect(20, 200, SCREEN_W - 40, 30, 6, color);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(COLOR_BG);
    _tft.setTextFont(2);
    _tft.drawString(msg, SCREEN_W / 2, 215);
    _toastExpiry = millis() + 2000;
}

void UI::showDashboardQr(const String& url) {
    if (url.length() == 0 || !_isConnected) {
        showToast("Connect WiFi first", COLOR_WARNING);
        return;
    }
    _qrUrl = url;
    _qrOverlayActive = true;
    _qrDismissTaps = 0;
    drawQrOverlay();
    Serial.printf("[QR] dashboard %s\n", url.c_str());
}

void UI::dismissQrOverlay() {
    _qrOverlayActive = false;
    _qrDismissTaps = 0;
    // Force full redraw — scan lines may have changed while overlay was up.
    Screen s = _screen;
    _screen = (Screen)-1;
    setScreen(s);
}

void UI::drawQrOverlay() {
    const char* url = _qrUrl.c_str();
    static const uint8_t kVersions[] = {3, 4, 5, 6, 0};

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(6)];
    bool ok = false;

    for (int i = 0; kVersions[i]; i++) {
        if (qrcode_initText(&qrcode, qrcodeData, kVersions[i], ECC_LOW, url) == 0) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        _qrOverlayActive = false;
        showToast("QR encode failed", COLOR_DANGER);
        setScreen(_screen);
        return;
    }

    _tft.fillScreen(TFT_WHITE);

    const int hintH = 26;
    const int modules = qrcode.size;
    const int availH = SCREEN_H - hintH - 6;
    const int availW = SCREEN_W - 6;
    int scale = min(availW / modules, availH / modules);
    if (scale < 2) scale = 2;

    const int pix = modules * scale;
    const int ox = (SCREEN_W - pix) / 2;
    const int oy = (availH - pix) / 2 + 2;

    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            if (qrcode_getModule(&qrcode, x, y))
                _tft.fillRect(ox + x * scale, oy + y * scale, scale, scale, TFT_BLACK);
        }
    }

    _tft.fillRect(0, SCREEN_H - hintH, SCREEN_W, hintH, COLOR_BG);
    _tft.drawFastHLine(0, SCREEN_H - hintH, SCREEN_W, COLOR_BORDER);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextFont(1);
    _tft.setTextColor(COLOR_TEXT);
    char hint[48];
    snprintf(hint, sizeof(hint), "Scan for dashboard · tap %u/3 to close",
             (unsigned)_qrDismissTaps);
    _tft.drawString(hint, SCREEN_W / 2, SCREEN_H - hintH / 2);
}

void UI::showOverlayMessage(const String& msg) {
    _tft.fillRoundRect(16, 72, SCREEN_W - 32, 96, 8, COLOR_BG2);
    _tft.drawRoundRect(16, 72, SCREEN_W - 32, 96, 8, COLOR_ACCENT);
    _tft.setTextDatum(TC_DATUM);
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextFont(2);

    int lineY = 88;
    int start = 0;
    const int maxChars = 28;
    while (start < (int)msg.length() && lineY < 158) {
        int end = start + maxChars;
        if (end > (int)msg.length()) end = msg.length();
        else if (msg.charAt(end) != '\n') {
            int brk = msg.lastIndexOf('\n', end);
            if (brk > start) end = brk;
            else if (msg.charAt(end) != ' ' && end < (int)msg.length()) {
                int sp = msg.lastIndexOf(' ', end);
                if (sp > start) end = sp;
            }
        }
        String line = msg.substring(start, end);
        line.trim();
        if (line.length() > 0) {
            _tft.drawString(line, SCREEN_W / 2, lineY);
            lineY += 18;
        }
        start = end;
        if (start < (int)msg.length() && msg.charAt(start) == '\n') start++;
        while (start < (int)msg.length() && msg.charAt(start) == ' ') start++;
    }
    _tft.setTextFont(1);
    _tft.setTextColor(COLOR_DIM);
    _tft.drawString("tap anywhere to dismiss", SCREEN_W / 2, 158);
    _overlayExpiry = millis() + 8000;
}

// ────────────────────────────────────────
//  LED & Backlight
// ────────────────────────────────────────

void UI::setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
    analogWrite(LED_R_PIN, 255 - r);
    analogWrite(LED_G_PIN, 255 - g);
    analogWrite(LED_B_PIN, 255 - b);
}

void UI::setBacklight(uint8_t val) {
    analogWrite(TFT_BL_PIN, val);
}

// ────────────────────────────────────────
//  Input helpers
// ────────────────────────────────────────

void UI::clearInput() {
    memset(_inputBuf, 0, sizeof(_inputBuf));
    _inputLen = 0;
    _kbMode = KB_LOWER;
    _showPassword = false;
}

void UI::appendInput(char c) {
    if (_inputLen < KB_MAX_INPUT) {
        _inputBuf[_inputLen++] = c;
        _inputBuf[_inputLen] = 0;
    }
}

void UI::backspaceInput() {
    if (_inputLen > 0) {
        _inputBuf[--_inputLen] = 0;
    }
}
