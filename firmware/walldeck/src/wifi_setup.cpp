#include "wifi_setup.h"
#include "wifi_config.h"
#include "config.h"

#include <WiFi.h>

#define COL_BG     0x070b10
#define COL_CARD   0x0d141c
#define COL_BORDER 0x1e3347
#define COL_TEXT   0xd7e6f0
#define COL_MUTED  0x7f97a8
#define COL_ACCENT 0x39f3ff
#define COL_OK     0x7dff6a
#define COL_ERR    0xff3d6e

enum class SetupPhase { Scanning, PickNetwork, EnterPassword, Connecting, Failed };

static WifiReadyCallback readyCb = nullptr;
static bool active = false;
static SetupPhase phase = SetupPhase::Scanning;

static lv_obj_t *root;
static lv_obj_t *statusLbl;
static lv_obj_t *listPanel;
static lv_obj_t *listScroll;
static lv_obj_t *passPanel;
static lv_obj_t *passSsidLbl;
static lv_obj_t *passInput;
static lv_obj_t *keyboard;
static lv_obj_t *errorLbl;
static lv_obj_t *passActions;

static char selectedSsid[33] = "";
static unsigned long connectStartMs = 0;
static bool scanRequested = false;

static lv_color_t hex(uint32_t c) {
    return lv_color_hex(c);
}

static void show_panel(lv_obj_t *panel) {
    lv_obj_add_flag(listPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(passPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);

    if (panel == passPanel) {
        lv_keyboard_set_textarea(keyboard, passInput);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(passActions, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(passActions, LV_ALIGN_BOTTOM_MID, 0, -lv_obj_get_height(keyboard) - 6);
        lv_obj_move_foreground(passActions);
        lv_obj_move_foreground(keyboard);
    } else {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(passActions, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, nullptr);
    }
}

static void finish_success() {
    active = false;
    lv_obj_clean(lv_screen_active());
    root = nullptr;
    keyboard = nullptr;
    passActions = nullptr;
    if (readyCb) readyCb();
}

static void start_connect(const char *ssid, const char *password) {
    strncpy(selectedSsid, ssid, sizeof(selectedSsid) - 1);
    selectedSsid[sizeof(selectedSsid) - 1] = '\0';
    wifi_config_connect(ssid, password);
    phase = SetupPhase::Connecting;
    connectStartMs = millis();
    show_panel(listPanel);
    lv_label_set_text_fmt(statusLbl, "Connecting to %s…", ssid);
    lv_obj_add_flag(errorLbl, LV_OBJ_FLAG_HIDDEN);
}

static void connect_btn_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (phase == SetupPhase::Connecting) return;
    const char *pass = lv_textarea_get_text(passInput);
    if (strlen(selectedSsid) == 0) return;
    start_connect(selectedSsid, pass ? pass : "");
}

static void submit_password_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        connect_btn_cb(e);
    }
}

static void back_btn_cb(lv_event_t *e) {
    LV_UNUSED(e);
    phase = SetupPhase::PickNetwork;
    show_panel(listPanel);
    lv_label_set_text(statusLbl, "Pick your WiFi network");
}

static void network_row_cb(lv_event_t *e) {
    const char *ssid = static_cast<const char *>(lv_event_get_user_data(e));
    strncpy(selectedSsid, ssid, sizeof(selectedSsid) - 1);
    selectedSsid[sizeof(selectedSsid) - 1] = '\0';
    phase = SetupPhase::EnterPassword;
    lv_label_set_text_fmt(passSsidLbl, LV_SYMBOL_WIFI "  %s", selectedSsid);
    lv_textarea_set_text(passInput, "");
    show_panel(passPanel);
    lv_label_set_text(statusLbl, "Enter password — tap ✓ or Connect");
}

struct NetEntry {
    char ssid[33];
    int32_t rssi;
};

static char ssidPool[24][33];
static int ssidPoolCount = 0;

static void populate_network_list() {
    lv_obj_clean(listScroll);
    ssidPoolCount = 0;

    int count = WiFi.scanComplete();
    if (count <= 0) {
        lv_obj_t *lbl = lv_label_create(listScroll);
        lv_label_set_text(lbl, "No networks found.\nTap Rescan.");
        lv_obj_set_style_text_color(lbl, hex(COL_MUTED), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl, LV_PCT(100));
        return;
    }

    NetEntry entries[24];
    int unique = 0;

    for (int i = 0; i < count && unique < 24; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;

        bool dup = false;
        for (int u = 0; u < unique; u++) {
            if (strcmp(entries[u].ssid, ssid.c_str()) == 0) {
                if (WiFi.RSSI(i) > entries[u].rssi) entries[u].rssi = WiFi.RSSI(i);
                dup = true;
                break;
            }
        }
        if (dup) continue;

        strncpy(entries[unique].ssid, ssid.c_str(), sizeof(entries[unique].ssid) - 1);
        entries[unique].rssi = WiFi.RSSI(i);
        unique++;
    }

    for (int i = 0; i < unique - 1; i++) {
        for (int j = i + 1; j < unique; j++) {
            if (entries[j].rssi > entries[i].rssi) {
                NetEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }

    for (int i = 0; i < unique; i++) {
        if (ssidPoolCount >= 24) break;
        strncpy(ssidPool[ssidPoolCount], entries[i].ssid, sizeof(ssidPool[ssidPoolCount]) - 1);

        lv_obj_t *btn = lv_button_create(listScroll);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, UI_TOUCH_MIN);
        lv_obj_set_ext_click_area(btn, 6);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, hex(COL_CARD), 0);
        lv_obj_add_event_cb(btn, network_row_cb, LV_EVENT_CLICKED, ssidPool[ssidPoolCount]);

        lv_obj_t *lbl = lv_label_create(btn);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s\n%d dBm", entries[i].ssid, entries[i].rssi);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, hex(COL_TEXT), 0);
        lv_obj_center(lbl);
        ssidPoolCount++;
    }
}

static void rescan_cb(lv_event_t *e) {
    LV_UNUSED(e);
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    scanRequested = true;
    phase = SetupPhase::Scanning;
    show_panel(listPanel);
    lv_label_set_text(statusLbl, "Scanning for networks…");
    lv_obj_clean(listScroll);
    lv_obj_t *lbl = lv_label_create(listScroll);
    lv_label_set_text(lbl, "Please wait…");
    lv_obj_set_style_text_color(lbl, hex(COL_MUTED), 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *make_action_btn(lv_obj_t *parent, const char *text, lv_color_t bg,
                                 lv_event_cb_t cb) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, UI_TOUCH_MIN);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_ext_click_area(btn, 6);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, hex(COL_TEXT), 0);
    lv_obj_center(lbl);
    return btn;
}

void wifi_setup_start(WifiReadyCallback on_ready) {
    readyCb = on_ready;
    active = true;
    phase = SetupPhase::Scanning;
    scanRequested = true;
    selectedSsid[0] = '\0';

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, hex(COL_BG), 0);

    root = lv_obj_create(screen);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, hex(COL_TEXT), 0);

    statusLbl = lv_label_create(root);
    lv_label_set_text(statusLbl, "Scanning for networks…");
    lv_obj_set_style_text_color(statusLbl, hex(COL_MUTED), 0);
    lv_obj_set_width(statusLbl, LV_PCT(100));

    listPanel = lv_obj_create(root);
    lv_obj_set_width(listPanel, LV_PCT(100));
    lv_obj_set_flex_grow(listPanel, 1);
    lv_obj_set_style_bg_opa(listPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listPanel, 0, 0);
    lv_obj_set_style_pad_all(listPanel, 0, 0);
    lv_obj_set_flex_flow(listPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(listPanel, LV_OBJ_FLAG_SCROLLABLE);

    listScroll = lv_obj_create(listPanel);
    lv_obj_set_width(listScroll, LV_PCT(100));
    lv_obj_set_flex_grow(listScroll, 1);
    lv_obj_set_flex_flow(listScroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(listScroll, 8, 0);
    lv_obj_set_style_bg_opa(listScroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listScroll, 0, 0);
    lv_obj_add_flag(listScroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(listScroll, LV_DIR_VER);

    lv_obj_t *listActions = lv_obj_create(listPanel);
    lv_obj_set_width(listActions, LV_PCT(100));
    lv_obj_set_height(listActions, UI_TOUCH_MIN + 4);
    lv_obj_set_flex_flow(listActions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(listActions, 8, 0);
    lv_obj_set_style_bg_opa(listActions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listActions, 0, 0);
    lv_obj_set_style_pad_all(listActions, 0, 0);
    lv_obj_remove_flag(listActions, LV_OBJ_FLAG_SCROLLABLE);
    make_action_btn(listActions, LV_SYMBOL_REFRESH " Rescan", hex(COL_ACCENT), rescan_cb);

    passPanel = lv_obj_create(root);
    lv_obj_set_width(passPanel, LV_PCT(100));
    lv_obj_set_flex_grow(passPanel, 1);
    lv_obj_set_style_bg_opa(passPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passPanel, 0, 0);
    lv_obj_set_flex_flow(passPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(passPanel, 8, 0);
    lv_obj_add_flag(passPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(passPanel, LV_OBJ_FLAG_SCROLLABLE);

    passSsidLbl = lv_label_create(passPanel);
    lv_label_set_text(passSsidLbl, "");
    lv_obj_set_style_text_color(passSsidLbl, hex(COL_TEXT), 0);
    lv_obj_set_width(passSsidLbl, LV_PCT(100));

    passInput = lv_textarea_create(passPanel);
    lv_obj_set_width(passInput, LV_PCT(100));
    lv_obj_set_height(passInput, UI_TOUCH_MIN);
    lv_textarea_set_one_line(passInput, true);
    lv_textarea_set_password_mode(passInput, true);
    lv_textarea_set_placeholder_text(passInput, "Password");
    lv_obj_add_event_cb(passInput, submit_password_cb, LV_EVENT_READY, nullptr);

    passActions = lv_obj_create(screen);
    lv_obj_set_width(passActions, LV_PCT(96));
    lv_obj_set_height(passActions, UI_TOUCH_MIN + 4);
    lv_obj_set_flex_flow(passActions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(passActions, 8, 0);
    lv_obj_set_style_bg_opa(passActions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passActions, 0, 0);
    lv_obj_set_style_pad_all(passActions, 0, 0);
    lv_obj_remove_flag(passActions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(passActions, LV_OBJ_FLAG_HIDDEN);
    make_action_btn(passActions, LV_SYMBOL_LEFT " Back", hex(COL_BORDER), back_btn_cb);
    make_action_btn(passActions, LV_SYMBOL_OK " Connect", hex(COL_OK), connect_btn_cb);

    errorLbl = lv_label_create(root);
    lv_label_set_text(errorLbl, "");
    lv_obj_set_style_text_color(errorLbl, hex(COL_ERR), 0);
    lv_obj_set_width(errorLbl, LV_PCT(100));
    lv_obj_add_flag(errorLbl, LV_OBJ_FLAG_HIDDEN);

    keyboard = lv_keyboard_create(screen);
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(40));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, nullptr);
    lv_obj_add_event_cb(keyboard, submit_password_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true);
}

void wifi_setup_loop() {
    if (!active) return;

    if (phase == SetupPhase::Scanning && scanRequested) {
        int result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) return;
        scanRequested = false;
        phase = SetupPhase::PickNetwork;
        populate_network_list();
        lv_label_set_text(statusLbl, "Pick your WiFi network");
        return;
    }

    if (phase == SetupPhase::Connecting) {
        if (wifi_config_is_connected()) {
            const char *pass = lv_textarea_get_text(passInput);
            wifi_config_save(selectedSsid, pass ? pass : "");
            finish_success();
            return;
        }
        if (millis() - connectStartMs > 20000) {
            phase = SetupPhase::Failed;
            lv_label_set_text(statusLbl, "Connection failed");
            lv_label_set_text(errorLbl, "Wrong password or weak signal.\nTry again.");
            lv_obj_remove_flag(errorLbl, LV_OBJ_FLAG_HIDDEN);
            show_panel(passPanel);
        }
    }
}

bool wifi_setup_is_active() {
    return active;
}
