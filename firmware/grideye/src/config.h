#pragma once

// ── Display (ILI9341 on HSPI) ──
// Pin definitions handled via build_flags for TFT_eSPI
#define TFT_BL_PIN        21

// ── Touch (XPT2046 on VSPI) ──
#define TOUCH_CLK_PIN     25
#define TOUCH_DIN_PIN     32
#define TOUCH_DOUT_PIN    39
#define TOUCH_CS_PIN      33
#define TOUCH_IRQ_PIN     36

// ── microSD (shares the VSPI peripheral with touch; see vspi_bus) ──
#define SD_SCK_PIN        18
#define SD_MISO_PIN       19
#define SD_MOSI_PIN       23
#define SD_CS_PIN          5

// ── RGB LED (active LOW / common anode) ──
#define LED_R_PIN          4
#define LED_G_PIN         16
#define LED_B_PIN         17

// ── LDR ──
#define LDR_PIN           34

// ── Screen dimensions (landscape) ──
#define SCREEN_W         320
#define SCREEN_H         240

// ── Touch calibration (adjust per unit) ──
#define TOUCH_X_MIN      200
#define TOUCH_X_MAX     3800
#define TOUCH_Y_MIN      200
#define TOUCH_Y_MAX     3800

// ── UI color palette ──
#define COLOR_BG         0x0861   // #0a0a1a approx
#define COLOR_BG2        0x1928   // #1a1a3e approx
#define COLOR_CARD       0x228A   // #252550 approx
#define COLOR_ACCENT     0x06BF   // #00d4ff
#define COLOR_ACCENT2    0x797F   // #7b2ff7 approx
#define COLOR_TEXT       0xDEDB   // #e0e0e0
#define COLOR_DIM        0x8C51   // #8888aa approx
#define COLOR_SUCCESS    0x07F1   // #00ff88
#define COLOR_WARNING    0xFD40   // #ffaa00
#define COLOR_DANGER     0xF222   // #ff4444
#define COLOR_BORDER     0x3266   // #333366
#define COLOR_KEY_BG     0x2125   // #222244
#define COLOR_KEY_PRESS  0x06BF   // same as accent
#define COLOR_INPUT_BG   0x18C3   // #191930

// ── UI layout grid (320×240) — zones must not overlap ──
// Header/status: y 0–35 | Body: y 36–205 | Footer/nav: y 206–239 (touch y≥206)
#define UI_HEADER_BOTTOM   36
#define UI_BODY_BOTTOM     205
#define UI_FOOTER_TOP      206
#define UI_FOOTER_DRAW_Y   208
#define UI_FOOTER_H        30

// WiFi scan list
#define WIFI_LIST_TOP          40
#define WIFI_LIST_TOP_PORTAL   76
#define WIFI_LIST_BOTTOM       200
#define WIFI_LIST_ROW_H        32
#define WIFI_SCROLL_BTN_X      286
#define WIFI_SCROLL_BTN_W      30

// Keyboard (password screen — no footer; top CANCEL only)
#define KB_KEY_W          29
#define KB_KEY_H          24
#define KB_KEY_GAP         2
#define KB_PITCH          (KB_KEY_W + KB_KEY_GAP)  // 31
#define KB_START_Y       108
#define KB_INPUT_Y        74
#define KB_INPUT_H        26
#define KB_MAX_INPUT      64

// ── App defaults ──
#define MAX_NOTES         15
#define NOTE_MAX_LEN     200
#define WIFI_CONNECT_TIMEOUT_MS 12000
#define STATUS_REFRESH_MS 3000
#define TOUCH_DEBOUNCE_MS 180
#define TOUCH_SETTLE_MS   280   // ignore edges briefly after screen change
#define PREFS_NAMESPACE   "cyd"

// Setup AP when not on home Wi‑Fi (open network + captive portal → dashboard)
#define PORTAL_SSID           "CYBERDECK"
#define PORTAL_AP_IP          192, 168, 4, 1
#define PORTAL_AP_GATEWAY     192, 168, 4, 1
#define PORTAL_AP_SUBNET      255, 255, 255, 0

// LAN discovery tuning
#define LAN_SWEEP_INTERVAL_MS     12000
#define LAN_SWEEP_INTERVAL_FAST_MS 5000
#define LAN_TCP_PROBES_PER_TICK   2
#define LAN_WIFI_SCAN_DEFER_MS    45000
#define LAN_QUIET_AFTER_HTTP_MS   2500
#define LAN_PRIORITY_PER_TICK     2
#define LAN_PRIORITY_MAX          20
#define LAN_ARP_MIN_INTERVAL_MS   120
#define LAN_TCP_CONNECT_MS        50
#define LAN_TCP_CONNECT_KNOWN_MS  35
#define LAN_MULTICAST_POLL_MAX    4
#define LAN_MDNS_BROWSE_MS        45000
#define LAN_ARP_GAP_MS            900

// Set to 1 to boot into touch diagnostic (serial CSV + on-screen). No WiFi/calibration.
#define TOUCH_DIAG_BOOT   0

// XPT2046 differential Z above this = finger down (library default is 300)
#define TOUCH_Z_THRESHOLD 280
// Log irq/z on NET_MODE every N ms — helps serial QA when taps fail
#define TOUCH_DEBUG_POLL  1

// SD shares the VSPI peripheral with touch via vspi_bus (clock/MOSI fan out to
// both pin sets; the MISO input is switched per access). Set 0 to skip SD mount.
#define ENABLE_SD_CAPTURE 1

// Feature gates — set 0 to save flash/RAM on tight builds
#define ENABLE_CVE_PROFILE  1   // on-device CVE rule matching (~12 KB when loaded)
#define ENABLE_DEAUTH       1   // field-mode active engage (deauth burst)

// Bounded catalogs (smaller = less static RAM on ESP32)
#define INTEL_MAX_HOSTS     16
#define INTEL_MAX_WIFI      16
#define RECON_MAX_AP        24
#define RECON_MAX_CLIENT    32
#define BLE_MAX_DEV         24
#define PROFILE_MAX_RULES   32
