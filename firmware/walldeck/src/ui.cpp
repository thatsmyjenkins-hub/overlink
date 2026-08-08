#include "ui.h"
#include "config.h"
#include "power.h"
#include "ui_layout.h"

#include <Preferences.h>
#include <WiFi.h>
#include <lvgl.h>
#include <cstring>

// Canonical palette from ~/cyd-basement-control CYD_UI_V2 (CTRL 3.5)
static const lv_color_t COL_BG = lv_color_hex(0x0A1210);
static const lv_color_t COL_PANEL = lv_color_hex(0x122018);
static const lv_color_t COL_CYAN = lv_color_hex(0x3DDC97);
static const lv_color_t COL_AMBER = lv_color_hex(0xF0A030);
static const lv_color_t COL_DIM = lv_color_hex(0x7A8F80);
static const lv_color_t COL_ACTIVE = lv_color_hex(0xE8F5A0);

#define UI_FONT &lv_font_montserrat_14
#if LV_FONT_MONTSERRAT_12
#define UI_FONT_SM &lv_font_montserrat_12
#else
#define UI_FONT_SM &lv_font_montserrat_14
#endif

// Display order matches CTRL 3.5 screenshot
struct SceneTile {
  const char *id;
  const char *tag;
  bool accent;
  bool confirm;
};

static const SceneTile kScenes[] = {
    {"full", "FULL", false, false},     {"chill", "CHILL", false, false},
    {"movie", "MOVIE", false, false},   {"game", "GAME", false, false},
    {"sports", "SPORTS", false, false}, {"bed", "BED", false, false},
    {"dance", "DANCE", true, false},    {"date", "DATE", true, false},
    {"karaoke", "KARAOKE", true, false}, {"off", "OFF", false, true},
};
static const int kSceneCount = 10;

enum class Screen { Dash, Zones, Basement, Scenes, Devices, Grace, Wled, Room, Party };
static char roomZoneId[14] = "";

static AppContext *g_ctx = nullptr;
static Preferences prefs;
static Screen screen = Screen::Dash;
static int activeScene = -1;
static char logLine[48] = "> GRID READY";
static char summaryScene[16] = "—";
static int summaryOnline = 0;
static int summaryTotal = 0;
static bool summaryDeck = false;
static int wledBriCache = 128;
static int wledFxCache = 0;
static char partyMsg[28] = "OVERLINK SAYS HI";
static int partyMsgPreset = 0;
static const char *const kPartyPresets[] = {
    "OVERLINK SAYS HI",
    "DAD MODE ENABLED",
    "UNLOCK THE SNACKS",
    "OVERLINK ONLINE",
};
static const int kPartyPresetCount = 4;

static void showDash();
static void showZones();
static void showBasement();
static void showHomeScenes();
static void showDevices();
static void showGrace();
static void showWled();
static void showRoom();
static void showParty();
static void drawTabs(lv_obj_t *scr, Screen active);
static void persistNav();
static void resumeNav();

static lv_obj_t *scene_btns[10] = {nullptr};
static lv_obj_t *wifi_label = nullptr;
static lv_obj_t *status_label = nullptr;
static lv_obj_t *now_label = nullptr;
static lv_obj_t *vol_label = nullptr;
static lv_obj_t *link_label = nullptr;
static lv_obj_t *confirmModal = nullptr;
static char pendingSceneId[16] = "";

void ui_log(const char *msg) {
  if (!msg) return;
  strlcpy(logLine, msg, sizeof(logLine));
  if (logLine[0] != '>') {
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "> %s", msg);
    strlcpy(logLine, tmp, sizeof(logLine));
  }
  if (link_label) lv_label_set_text(link_label, logLine);
  Serial.printf("[UI] %s\n", logLine);
}

static void stylePanelBtn(lv_obj_t *btn, bool accent) {
  lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, accent ? COL_AMBER : COL_CYAN, 0);
  lv_obj_set_style_radius(btn, 3, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
}

static void styleSceneBtn(lv_obj_t *btn, bool active, bool accent) {
  lv_obj_set_style_bg_color(btn, active ? COL_ACTIVE : COL_PANEL, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, active ? COL_CYAN : (accent ? COL_AMBER : COL_CYAN), 0);
  lv_obj_set_style_radius(btn, 3, 0);
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  if (lbl) {
    lv_obj_set_style_text_color(lbl, active ? COL_BG : (accent ? COL_AMBER : COL_CYAN), 0);
    lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
  }
}

static void refreshSceneStyles() {
  for (int i = 0; i < kSceneCount; i++) {
    if (!scene_btns[i]) continue;
    styleSceneBtn(scene_btns[i], i == activeScene, kScenes[i].accent);
  }
}

static lv_obj_t *placeBtn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user, int x,
                          int y, int w, int h, bool accent = false) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  stylePanelBtn(btn, accent);
  if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, accent ? COL_AMBER : COL_CYAN, 0);
  lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
  lv_obj_center(lbl);
  return btn;
}

static void runSceneId(const char *id, int index) {
  if (!g_ctx || !g_ctx->core) return;
  String msg;
  bool ok = g_ctx->core->runScene(id, msg);
  if (ok) activeScene = index;
  refreshSceneStyles();
  char line[48];
  snprintf(line, sizeof(line), ok ? "> ENGAGE %s" : "> FAIL %s", kScenes[index].tag);
  ui_log(line);
  if (status_label) lv_label_set_text(status_label, ok ? "READY" : "FAIL");
}

static void confirm_yes_cb(lv_event_t *e) {
  LV_UNUSED(e);
  if (pendingSceneId[0]) {
    int idx = 9;  // OFF
    for (int i = 0; i < kSceneCount; i++) {
      if (!strcmp(kScenes[i].id, pendingSceneId)) {
        idx = i;
        break;
      }
    }
    runSceneId(pendingSceneId, idx);
  }
  pendingSceneId[0] = 0;
  if (confirmModal) lv_obj_add_flag(confirmModal, LV_OBJ_FLAG_HIDDEN);
}

static void confirm_no_cb(lv_event_t *e) {
  LV_UNUSED(e);
  pendingSceneId[0] = 0;
  if (confirmModal) lv_obj_add_flag(confirmModal, LV_OBJ_FLAG_HIDDEN);
}

static void onSceneClick(lv_event_t *e) {
  int i = (int)(uintptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= kSceneCount) return;
  if (kScenes[i].confirm) {
    strlcpy(pendingSceneId, kScenes[i].id, sizeof(pendingSceneId));
    if (confirmModal) {
      lv_obj_clear_flag(confirmModal, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(confirmModal);
    }
    return;
  }
  runSceneId(kScenes[i].id, i);
}

static void onDeckIr(lv_event_t *e) {
  const char *a = static_cast<const char *>(lv_event_get_user_data(e));
  if (!a || !g_ctx || !g_ctx->core) return;
  String msg;
  bool ok = g_ctx->core->deckIr(a, msg);
  char line[48];
  snprintf(line, sizeof(line), ok ? "> IR %s" : "> IR FAIL", a);
  ui_log(line);
}

static void onAvWatch(lv_event_t *e) {
  LV_UNUSED(e);
  if (!g_ctx || !g_ctx->core) return;
  String msg;
  bool ok = g_ctx->core->avWatch(msg);
  ui_log(ok ? "> WATCH" : "> WATCH FAIL");
}

static void onAvInput(lv_event_t *e) {
  const char *t = static_cast<const char *>(lv_event_get_user_data(e));
  if (!g_ctx || !g_ctx->core) return;
  String msg;
  bool ok = g_ctx->core->avInput(t ? t : "fire", msg);
  ui_log(ok ? "> IN" : "> IN FAIL");
}

static void onAvApp(lv_event_t *e) {
  const char *id = static_cast<const char *>(lv_event_get_user_data(e));
  if (!id || !g_ctx || !g_ctx->core) return;
  String msg;
  bool ok = g_ctx->core->avApp(id, msg);
  char line[48];
  snprintf(line, sizeof(line), ok ? "> %s" : "> %s FAIL", id);
  ui_log(line);
}

static void onAvKey(lv_event_t *e) {
  const char *name = static_cast<const char *>(lv_event_get_user_data(e));
  if (!name || !g_ctx || !g_ctx->core) return;
  String msg;
  bool ok = g_ctx->core->avKey(name, msg);
  char line[48];
  snprintf(line, sizeof(line), ok ? "> %s" : "> %s FAIL", name);
  ui_log(line);
}

static void persistNav() {
  prefs.begin("walldeck", false);
  prefs.putInt("tab", (int)screen);
  prefs.end();
}

static void drawTabs(lv_obj_t *scr, Screen active) {
  constexpr int tabH = 26;
  const int y = kScreenH - tabH;
  static const char *labels[] = {"HOME", "ZONES", "SCENES"};
  const int w = kScreenW / 3;
  for (int i = 0; i < 3; i++) {
    Screen target = i == 0 ? Screen::Dash : (i == 1 ? Screen::Zones : Screen::Scenes);
    bool on = (active == target) || (active == Screen::Basement && target == Screen::Zones) ||
              (active == Screen::Devices && target == Screen::Zones) ||
              (active == Screen::Wled && target == Screen::Zones) ||
              (active == Screen::Room && target == Screen::Zones) ||
              (active == Screen::Grace && target == Screen::Dash) ||
              (active == Screen::Party && target == Screen::Dash);
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_pos(btn, i * w, y);
    lv_obj_set_size(btn, w - 1, tabH - 1);
    lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
    lv_obj_set_style_border_color(btn, on ? COL_CYAN : COL_DIM, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_add_event_cb(
        btn,
        [](lv_event_t *e) {
          Screen t = (Screen)(uintptr_t)lv_event_get_user_data(e);
          if (t == Screen::Dash) showDash();
          else if (t == Screen::Zones) showZones();
          else showHomeScenes();
        },
        LV_EVENT_CLICKED, (void *)(uintptr_t)target);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, labels[i]);
    lv_obj_set_style_text_color(lbl, on ? COL_CYAN : COL_DIM, 0);
    lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
    lv_obj_center(lbl);
  }
}

static void onGotoDevices(lv_event_t *e) {
  LV_UNUSED(e);
  showDevices();
}

static void onGotoWled(lv_event_t *e) {
  LV_UNUSED(e);
  showWled();
}

static void onGotoBasement(lv_event_t *e) {
  LV_UNUSED(e);
  showBasement();
}

static void wledPost(const char *json) {
  if (!g_ctx || !g_ctx->core) return;
  String msg;
  bool ok = g_ctx->core->wledSetJson(json, msg);
  ui_log(ok ? "> WLED" : "> WLED FAIL");
}

static void onWledOn(lv_event_t *e) {
  const char *v = static_cast<const char *>(lv_event_get_user_data(e));
  if (v && v[0] == '1') wledPost("{\"on\":true}");
  else wledPost("{\"on\":false}");
  showWled();
}

static void onWledBri(lv_event_t *e) {
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  wledBriCache = constrain(wledBriCache + d, 1, 255);
  char buf[40];
  snprintf(buf, sizeof(buf), "{\"on\":true,\"bri\":%d}", wledBriCache);
  wledPost(buf);
  showWled();
}

static void onWledFx(lv_event_t *e) {
  int fx = (int)(intptr_t)lv_event_get_user_data(e);
  wledFxCache = fx;
  char buf[48];
  snprintf(buf, sizeof(buf), "{\"on\":true,\"fx\":%d}", fx);
  wledPost(buf);
  showWled();
}

static void onWledColor(lv_event_t *e) {
  const char *rgb = static_cast<const char *>(lv_event_get_user_data(e));
  if (!rgb) return;
  int r = 255, g = 180, b = 90;
  sscanf(rgb, "%d,%d,%d", &r, &g, &b);
  char buf[72];
  snprintf(buf, sizeof(buf), "{\"on\":true,\"solid\":true,\"r\":%d,\"g\":%d,\"b\":%d}", r, g, b);
  wledPost(buf);
  showWled();
}

static void onWledPs(lv_event_t *e) {
  int ps = (int)(intptr_t)lv_event_get_user_data(e);
  char buf[40];
  snprintf(buf, sizeof(buf), "{\"on\":true,\"ps\":%d}", ps);
  wledPost(buf);
  showWled();
}

static void onGotoZones(lv_event_t *e) {
  LV_UNUSED(e);
  showZones();
}

static void onToggleDeepSleep(lv_event_t *e) {
  LV_UNUSED(e);
  bool next = !power_deep_sleep_enabled();
  power_set_deep_sleep(next);
  ui_log(next ? "> SLEEP ON" : "> SLEEP OFF");
  showDevices();
}

static void onVol(lv_event_t *e) {
  const char *a = static_cast<const char *>(lv_event_get_user_data(e));
  if (!a || !g_ctx || !g_ctx->core) return;
  int delta = !strcmp(a, "VOL+") ? 1 : -1;
  int level = -1;
  String msg;
  bool ok = g_ctx->core->avVol(delta, level, msg);
  if (vol_label) {
    static int vol = 18;
    if (level >= 0) vol = level;
    else vol = constrain(vol + delta, 0, 99);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", vol);
    lv_label_set_text(vol_label, buf);
    lv_obj_set_style_text_color(vol_label, ok ? COL_ACTIVE : COL_AMBER, 0);
  }
  ui_log(ok ? "> VOL" : "> VOL FAIL");
}

static void light_toggle_cb(lv_event_t *e) {
  size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
  if (!g_ctx || !g_ctx->core || idx >= g_ctx->core->deviceCount()) return;
  static bool on[MAX_DEVICES];
  on[idx] = !on[idx];
  String msg;
  g_ctx->core->setDevice(g_ctx->core->device(idx).id, on[idx], msg);
  ui_log(on[idx] ? "> DEV ON" : "> DEV OFF");
  lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
  stylePanelBtn(btn, false);
  lv_obj_set_style_border_color(btn, on[idx] ? COL_ACTIVE : COL_CYAN, 0);
}

static lv_obj_t *grace_prompt = nullptr;
static lv_obj_t *grace_detail = nullptr;
static lv_obj_t *grace_score = nullptr;
static lv_obj_t *grace_hdr = nullptr;
static lv_obj_t *grace_meta = nullptr;
static char grace_content_key[240];

static void clearRoot() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  for (auto &b : scene_btns) b = nullptr;
  wifi_label = status_label = now_label = vol_label = link_label = confirmModal = nullptr;
  grace_prompt = grace_detail = grace_score = grace_hdr = grace_meta = nullptr;
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void makeConfirm(lv_obj_t *scr) {
  confirmModal = lv_obj_create(scr);
  lv_obj_set_size(confirmModal, 200, 110);
  lv_obj_center(confirmModal);
  lv_obj_set_style_bg_color(confirmModal, COL_PANEL, 0);
  lv_obj_set_style_border_color(confirmModal, COL_AMBER, 0);
  lv_obj_set_style_border_width(confirmModal, 1, 0);
  lv_obj_set_style_radius(confirmModal, 3, 0);
  lv_obj_set_flex_flow(confirmModal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(confirmModal, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(confirmModal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *cl = lv_label_create(confirmModal);
  lv_label_set_text(cl, "ALL OFF?");
  lv_obj_set_style_text_color(cl, COL_AMBER, 0);
  lv_obj_set_style_text_font(cl, UI_FONT, 0);
  lv_obj_t *row = lv_obj_create(confirmModal);
  lv_obj_set_size(row, 180, 36);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *no = lv_button_create(row);
  lv_obj_set_size(no, 70, 32);
  stylePanelBtn(no, false);
  lv_obj_add_event_cb(no, confirm_no_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *nl = lv_label_create(no);
  lv_label_set_text(nl, "CANCEL");
  lv_obj_set_style_text_color(nl, COL_CYAN, 0);
  lv_obj_set_style_text_font(nl, UI_FONT, 0);
  lv_obj_center(nl);
  lv_obj_t *yes = lv_button_create(row);
  lv_obj_set_size(yes, 70, 32);
  stylePanelBtn(yes, true);
  lv_obj_add_event_cb(yes, confirm_yes_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *yl = lv_label_create(yes);
  lv_label_set_text(yl, "OFF");
  lv_obj_set_style_text_color(yl, COL_AMBER, 0);
  lv_obj_set_style_text_font(yl, UI_FONT, 0);
  lv_obj_center(yl);
}

static void showDash() {
  screen = Screen::Dash;
  persistNav();
  clearRoot();
  lv_obj_t *scr = lv_screen_active();
  if (g_ctx && g_ctx->core) {
    g_ctx->core->fetchSummary(summaryOnline, summaryTotal, summaryScene, sizeof(summaryScene),
                              summaryDeck);
  }
  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, "OVERLINK");
  lv_obj_set_style_text_color(hdr, COL_CYAN, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT, 0);
  lv_obj_set_pos(hdr, kMargin, 4);

  wifi_label = lv_label_create(scr);
  bool wifiOk = WiFi.status() == WL_CONNECTED && g_ctx && g_ctx->core && g_ctx->core->online();
  lv_label_set_text(wifi_label, wifiOk ? "WIFI OK" : "WIFI --");
  lv_obj_set_style_text_color(wifi_label, wifiOk ? COL_ACTIVE : COL_AMBER, 0);
  lv_obj_set_style_text_font(wifi_label, UI_FONT_SM, 0);
  lv_obj_align(wifi_label, LV_ALIGN_TOP_RIGHT, -kMargin, 4);

  auto stat = [&](const char *k, const char *v, int y) {
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_pos(box, kMargin, y);
    lv_obj_set_size(box, kScreenW - 2 * kMargin, 44);
    lv_obj_set_style_bg_color(box, COL_PANEL, 0);
    lv_obj_set_style_border_color(box, COL_CYAN, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 3, 0);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *mk = lv_label_create(box);
    lv_label_set_text(mk, k);
    lv_obj_set_style_text_color(mk, COL_DIM, 0);
    lv_obj_set_style_text_font(mk, UI_FONT_SM, 0);
    lv_obj_t *mv = lv_label_create(box);
    lv_label_set_text(mv, v);
    lv_obj_set_style_text_color(mv, COL_ACTIVE, 0);
    lv_obj_set_style_text_font(mv, UI_FONT, 0);
    lv_obj_set_pos(mv, 0, 16);
  };
  char buf[32];
  snprintf(buf, sizeof(buf), "%d/%d ONLINE", summaryOnline, summaryTotal);
  stat("DEVICES", buf, 26);
  stat("RECENT SCENE", summaryScene, 72);
  placeBtn(scr, "BASEMENT CTRL >", onGotoBasement, nullptr, kMargin, 122,
           kScreenW - 2 * kMargin, 28);
  placeBtn(
      scr, "GRACE PARTY >",
      [](lv_event_t *e) {
        LV_UNUSED(e);
        showGrace();
      },
      nullptr, kMargin, 154, kScreenW - 2 * kMargin, 28);
  placeBtn(
      scr, "PARTY TRICKS >",
      [](lv_event_t *e) {
        LV_UNUSED(e);
        showParty();
      },
      nullptr, kMargin, 186, kScreenW - 2 * kMargin, 28, true);
  if (summaryDeck) {
    lv_obj_t *deck = lv_label_create(scr);
    lv_label_set_text(deck, "CYBERDECK UP");
    lv_obj_set_style_text_color(deck, COL_DIM, 0);
    lv_obj_set_style_text_font(deck, UI_FONT_SM, 0);
    lv_obj_set_pos(deck, kMargin, 218);
  }
  link_label = lv_label_create(scr);
  lv_label_set_text(link_label, logLine);
  lv_obj_set_style_text_color(link_label, COL_CYAN, 0);
  lv_obj_set_style_text_font(link_label, UI_FONT_SM, 0);
  lv_obj_set_pos(link_label, kMargin, 236);
  drawTabs(scr, Screen::Dash);
}

static void showGrace() {
  screen = Screen::Grace;
  persistNav();

  char gameName[28] = "Grace";
  char gscreen[12] = "menu";
  char prompt[160] = "Open phone Games";
  char detail[200] = "";
  char team[12] = "";
  int s0 = 0, s1 = 0, remain = 0;
  if (g_ctx && g_ctx->core) {
    g_ctx->core->fetchGraceState(gameName, sizeof(gameName), gscreen, sizeof(gscreen), prompt,
                                 sizeof(prompt), detail, sizeof(detail), team, sizeof(team), s0, s1,
                                 remain, nullptr);
  }

  char scoreBuf[40];
  snprintf(scoreBuf, sizeof(scoreBuf), "R %d · B %d", s0, s1);
  if (remain > 0 && !strcmp(gscreen, "play")) {
    snprintf(scoreBuf, sizeof(scoreBuf), "R %d · B %d · %ds", s0, s1, remain);
  }

  char contentKey[240];
  snprintf(contentKey, sizeof(contentKey), "%s|%s|%s|%s|%d|%d", gscreen, prompt, detail, team, s0,
           s1);

  // Soft update: same card — only refresh timer/score (no flicker).
  if (grace_prompt && lv_obj_is_valid(grace_prompt) && grace_content_key[0] &&
      !strcmp(contentKey, grace_content_key)) {
    if (grace_score) lv_label_set_text(grace_score, scoreBuf);
    return;
  }

  clearRoot();
  strlcpy(grace_content_key, contentKey, sizeof(grace_content_key));
  lv_obj_t *scr = lv_screen_active();

  grace_hdr = lv_label_create(scr);
  lv_label_set_text(grace_hdr, gameName);
  lv_obj_set_style_text_color(grace_hdr, COL_AMBER, 0);
  lv_obj_set_style_text_font(grace_hdr, UI_FONT_SM, 0);
  lv_obj_set_pos(grace_hdr, kMargin, 4);

  grace_meta = lv_label_create(scr);
  char metaBuf[48];
  snprintf(metaBuf, sizeof(metaBuf), "%s · %s", gscreen, team[0] ? team : "—");
  lv_label_set_text(grace_meta, metaBuf);
  lv_obj_set_style_text_color(grace_meta, COL_DIM, 0);
  lv_obj_set_style_text_font(grace_meta, UI_FONT_SM, 0);
  lv_obj_align(grace_meta, LV_ALIGN_TOP_RIGHT, -kMargin, 4);

  grace_prompt = lv_label_create(scr);
  lv_label_set_long_mode(grace_prompt, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(grace_prompt, kScreenW - 2 * kMargin);
  lv_label_set_text(grace_prompt, prompt[0] ? prompt : "…");
  lv_obj_set_style_text_color(grace_prompt, COL_ACTIVE, 0);
  lv_obj_set_style_text_font(grace_prompt, UI_FONT, 0);
  lv_obj_set_style_text_align(grace_prompt, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(grace_prompt, kMargin, 36);

  if (detail[0]) {
    grace_detail = lv_label_create(scr);
    lv_label_set_long_mode(grace_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(grace_detail, kScreenW - 2 * kMargin);
    lv_label_set_text(grace_detail, detail);
    lv_obj_set_style_text_color(grace_detail, COL_CYAN, 0);
    lv_obj_set_style_text_font(grace_detail, UI_FONT_SM, 0);
    lv_obj_set_style_text_align(grace_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(grace_detail, kMargin, 130);
  }

  grace_score = lv_label_create(scr);
  lv_label_set_text(grace_score, scoreBuf);
  lv_obj_set_style_text_color(grace_score, COL_AMBER, 0);
  lv_obj_set_style_text_font(grace_score, UI_FONT, 0);
  lv_obj_set_pos(grace_score, kMargin, kScreenH - 56);

  placeBtn(
      scr, "REFRESH",
      [](lv_event_t *e) {
        LV_UNUSED(e);
        grace_content_key[0] = 0;
        grace_prompt = nullptr;
        showGrace();
      },
      nullptr, kScreenW - 84, kScreenH - 58, 78, 24);
  drawTabs(scr, Screen::Grace);
}

static void onPartyMsgCycle(lv_event_t *e) {
  LV_UNUSED(e);
  partyMsgPreset = (partyMsgPreset + 1) % kPartyPresetCount;
  strlcpy(partyMsg, kPartyPresets[partyMsgPreset], sizeof(partyMsg));
  showParty();
}

static void onPartySweep(lv_event_t *e) {
  LV_UNUSED(e);
  if (!g_ctx || !g_ctx->core) return;
  ui_log("> SWEEP…");
  String msg;
  int mdns = 0, ble = 0;
  bool ok = g_ctx->core->partySweep(msg, mdns, ble);
  char line[48];
  if (ok)
    snprintf(line, sizeof(line), "> SWEEP m%d b%d", mdns, ble);
  else
    snprintf(line, sizeof(line), "> SWEEP FAIL");
  ui_log(line);
}

static void onPartyBle(lv_event_t *e) {
  const char *mode = static_cast<const char *>(lv_event_get_user_data(e));
  if (!g_ctx || !g_ctx->core || !mode) return;
  String msg;
  bool ok = false;
  if (!strcmp(mode, "stop")) {
    ok = g_ctx->core->partyBleStop(msg);
    ui_log(ok ? "> BLE STOP" : "> BLE FAIL");
  } else {
    bool cycle = !strcmp(mode, "cycle");
    ok = g_ctx->core->partyBleStart(partyMsg, cycle, msg);
    ui_log(ok ? (cycle ? "> BLE SPAM" : "> BLE ON") : "> BLE FAIL");
  }
}

static void onPartyStampede(lv_event_t *e) {
  LV_UNUSED(e);
  if (!g_ctx || !g_ctx->core) return;
  ui_log("> STAMPEDE…");
  String msg;
  bool ok = g_ctx->core->partyStampede(msg);
  ui_log(ok ? "> STAMPEDE OK" : "> STAMPEDE FAIL");
}

static void onPartyCast(lv_event_t *e) {
  LV_UNUSED(e);
  if (!g_ctx || !g_ctx->core) return;
  ui_log("> CAST…");
  String msg;
  // Prefer cast-friendly preset text
  const char *castMsg =
      (partyMsgPreset == 3) ? partyMsg : "OVERLINK ONLINE";
  bool ok = g_ctx->core->partyCast(castMsg, msg);
  ui_log(ok ? "> CAST OK" : "> CAST FAIL");
}

static void onPartyFindPrinters(lv_event_t *e) {
  LV_UNUSED(e);
  if (!g_ctx || !g_ctx->core) return;
  ui_log("> FIND PRINT…");
  String msg;
  int count = 0;
  bool ok = g_ctx->core->partyFindPrinters(msg, count);
  char line[48];
  if (ok)
    snprintf(line, sizeof(line), "> PRINTERS %d", count);
  else
    snprintf(line, sizeof(line), "> PRINT FIND FAIL");
  ui_log(line);
}

static void onPartyPrint(lv_event_t *e) {
  LV_UNUSED(e);
  if (!g_ctx || !g_ctx->core) return;
  ui_log("> PRINT…");
  String msg;
  bool ok = g_ctx->core->partyPrintFirst(partyMsg, msg);
  ui_log(ok ? "> PRINTED" : "> PRINT FAIL");
}

static void showParty() {
  screen = Screen::Party;
  persistNav();
  clearRoot();
  lv_obj_t *scr = lv_screen_active();

  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, "PARTY TRICKS");
  lv_obj_set_style_text_color(hdr, COL_AMBER, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT, 0);
  lv_obj_set_pos(hdr, kMargin, 4);

  placeBtn(
      scr, "< BACK",
      [](lv_event_t *e) {
        LV_UNUSED(e);
        showDash();
      },
      nullptr, kScreenW - 72, 2, 68, 22);

  bool bleOn = false, bleCycle = false;
  char bleCur[28] = "";
  if (g_ctx && g_ctx->core) g_ctx->core->partyStatus(bleOn, bleCycle, bleCur, sizeof(bleCur));

  lv_obj_t *msgLbl = lv_label_create(scr);
  char msgLine[56];
  snprintf(msgLine, sizeof(msgLine), "MSG: %s", partyMsg);
  lv_label_set_long_mode(msgLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(msgLbl, kScreenW - 2 * kMargin - 56);
  lv_label_set_text(msgLbl, msgLine);
  lv_obj_set_style_text_color(msgLbl, COL_ACTIVE, 0);
  lv_obj_set_style_text_font(msgLbl, UI_FONT_SM, 0);
  lv_obj_set_pos(msgLbl, kMargin, 28);
  placeBtn(scr, "MSG", onPartyMsgCycle, nullptr, kScreenW - 56, 26, 50, 22, true);

  if (bleOn) {
    lv_obj_t *bleLbl = lv_label_create(scr);
    char bleLine[48];
    snprintf(bleLine, sizeof(bleLine), "BLE %s: %s", bleCycle ? "SPAM" : "ON",
             bleCur[0] ? bleCur : partyMsg);
    lv_label_set_long_mode(bleLbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(bleLbl, kScreenW - 2 * kMargin);
    lv_label_set_text(bleLbl, bleLine);
    lv_obj_set_style_text_color(bleLbl, COL_AMBER, 0);
    lv_obj_set_style_text_font(bleLbl, UI_FONT_SM, 0);
    lv_obj_set_pos(bleLbl, kMargin, 48);
  }

  lv_obj_t *list = lv_obj_create(scr);
  lv_obj_set_pos(list, kMargin, bleOn ? 66 : 52);
  lv_obj_set_size(list, kScreenW - 2 * kMargin, kScreenH - (bleOn ? 66 : 52) - 30);
  lv_obj_set_style_bg_color(list, COL_BG, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_gap(list, 4, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  auto flexChip = [&](lv_obj_t *row, const char *text, lv_event_cb_t cb, void *ud, bool accent,
                      int w) {
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, w, 28);
    stylePanelBtn(btn, accent);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, accent ? COL_AMBER : COL_CYAN, 0);
    lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
    lv_obj_center(lbl);
  };
  auto actionRow = [&](const char *a, const char *b, lv_event_cb_t ca, void *ua, lv_event_cb_t cb,
                       void *ub) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(row, 4, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    const int bw = b ? (kScreenW - 2 * kMargin - 8) / 2 : (kScreenW - 2 * kMargin - 4);
    flexChip(row, a, ca, ua, false, bw);
    if (b && cb) flexChip(row, b, cb, ub, false, bw);
  };

  actionRow("SWEEP", "STAMPEDE", onPartySweep, nullptr, onPartyStampede, nullptr);
  actionRow("BLE ON", "SPAM", onPartyBle, (void *)"on", onPartyBle, (void *)"cycle");
  actionRow("BLE STOP", "CAST TV", onPartyBle, (void *)"stop", onPartyCast, nullptr);
  actionRow("FIND PRINT", "PRINT 1st", onPartyFindPrinters, nullptr, onPartyPrint, nullptr);

  lv_obj_t *hint = lv_label_create(list);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, LV_PCT(100));
  lv_label_set_text(hint, "MSG cycles presets. PRINT 1st = first open printer.");
  lv_obj_set_style_text_color(hint, COL_DIM, 0);
  lv_obj_set_style_text_font(hint, UI_FONT_SM, 0);

  link_label = lv_label_create(scr);
  lv_label_set_text(link_label, logLine);
  lv_obj_set_style_text_color(link_label, COL_CYAN, 0);
  lv_obj_set_style_text_font(link_label, UI_FONT_SM, 0);
  lv_obj_set_pos(link_label, kMargin, kScreenH - 52);

  drawTabs(scr, Screen::Party);
}

static void onGotoRoom(lv_event_t *e) {
  const char *id = static_cast<const char *>(lv_event_get_user_data(e));
  if (!id || !id[0]) return;
  if (!strcmp(id, "basement")) {
    showBasement();
    return;
  }
  strlcpy(roomZoneId, id, sizeof(roomZoneId));
  showRoom();
}

static void onRoomGroup(lv_event_t *e) {
  bool on = (bool)(uintptr_t)lv_event_get_user_data(e);
  if (!g_ctx || !g_ctx->core || !roomZoneId[0]) return;
  // Prefer hue_group in this zone
  const char *devId = nullptr;
  for (size_t i = 0; i < g_ctx->core->deviceCount(); i++) {
    const CoreDevice &d = g_ctx->core->device(i);
    if (strcmp(d.zoneId, roomZoneId)) continue;
    if (!strcmp(d.type, "hue_group")) {
      devId = d.id;
      break;
    }
  }
  if (!devId) {
    ui_log("> NO GROUP");
    return;
  }
  String msg;
  bool ok = g_ctx->core->setDevice(devId, on, msg);
  ui_log(ok ? (on ? "> ROOM ON" : "> ROOM OFF") : "> ROOM FAIL");
}

static void showRoom() {
  screen = Screen::Room;
  persistNav();
  clearRoot();
  lv_obj_t *scr = lv_screen_active();

  const char *title = roomZoneId;
  if (g_ctx && g_ctx->core) {
    for (size_t i = 0; i < g_ctx->core->zoneCount(); i++) {
      if (!strcmp(g_ctx->core->zone(i).id, roomZoneId)) {
        title = g_ctx->core->zone(i).name;
        break;
      }
    }
  }

  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, title);
  lv_obj_set_style_text_color(hdr, COL_AMBER, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT_SM, 0);
  lv_obj_set_pos(hdr, kMargin, 4);

  placeBtn(scr, "< ZONES", [](lv_event_t *e) { LV_UNUSED(e); showZones(); }, nullptr, kScreenW - 78,
           2, 72, 22);

  placeBtn(scr, "ROOM ON", onRoomGroup, (void *)(uintptr_t)1, kMargin, 32,
           (kScreenW - 2 * kMargin - 4) / 2, 36);
  placeBtn(scr, "ROOM OFF", onRoomGroup, (void *)(uintptr_t)0,
           kMargin + (kScreenW - 2 * kMargin - 4) / 2 + 4, 32, (kScreenW - 2 * kMargin - 4) / 2, 36);

  int y = 78;
  int shown = 0;
  if (g_ctx && g_ctx->core) {
    for (size_t i = 0; i < g_ctx->core->deviceCount() && shown < 5; i++) {
      const CoreDevice &d = g_ctx->core->device(i);
      if (strcmp(d.zoneId, roomZoneId)) continue;
      if (strcmp(d.type, "hue") && strcmp(d.type, "cast") && strcmp(d.type, "firetv")) continue;
      char line[40];
      snprintf(line, sizeof(line), "%s %s", d.online ? "*" : "-", d.name);
      lv_obj_t *lbl = lv_label_create(scr);
      lv_label_set_text(lbl, line);
      lv_obj_set_style_text_color(lbl, d.online ? COL_CYAN : COL_DIM, 0);
      lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
      lv_obj_set_pos(lbl, kMargin, y);
      y += 18;
      shown++;
    }
  }
  if (!shown) {
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Hue lights via phone CTRL");
    lv_obj_set_style_text_color(hint, COL_DIM, 0);
    lv_obj_set_style_text_font(hint, UI_FONT_SM, 0);
    lv_obj_set_pos(hint, kMargin, y);
  }
  drawTabs(scr, Screen::Room);
}

static void showZones() {
  screen = Screen::Zones;
  persistNav();
  clearRoot();
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, "ZONES");
  lv_obj_set_style_text_color(hdr, COL_AMBER, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT, 0);
  lv_obj_set_pos(hdr, kMargin, 4);

  int y = 28;
  bool any = false;
  if (g_ctx && g_ctx->core) {
    for (size_t i = 0; i < g_ctx->core->zoneCount(); i++) {
      const CoreZone &z = g_ctx->core->zone(i);
      any = true;
      placeBtn(scr, z.name, onGotoRoom, (void *)z.id, kMargin, y, kScreenW - 2 * kMargin, 28);
      y += 32;
      if (y > kScreenH - 40) break;
    }
  }
  if (!any) {
    placeBtn(scr, "BASEMENT", onGotoBasement, nullptr, kMargin, 36, kScreenW - 2 * kMargin, 40);
  }
  drawTabs(scr, Screen::Zones);
}

static void showHomeScenes() {
  screen = Screen::Scenes;
  persistNav();
  clearRoot();
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, "HOME SCENES");
  lv_obj_set_style_text_color(hdr, COL_AMBER, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT, 0);
  lv_obj_set_pos(hdr, kMargin, 4);

  int y = 32;
  bool any = false;
  static char homeIds[4][24];
  int homeN = 0;
  if (g_ctx && g_ctx->core) {
    for (size_t i = 0; i < g_ctx->core->sceneCount(); i++) {
      const CoreScene &s = g_ctx->core->scene(i);
      if (strcmp(s.scope, "home") != 0) continue;
      any = true;
      if (homeN >= 4) break;
      strlcpy(homeIds[homeN], s.id, sizeof(homeIds[0]));
      placeBtn(
          scr, s.name[0] ? s.name : s.id,
          [](lv_event_t *e) {
            const char *id = (const char *)lv_event_get_user_data(e);
            if (!id || !g_ctx || !g_ctx->core) return;
            String msg;
            bool ok = g_ctx->core->runScene(id, msg);
            ui_log(ok ? "> HOME SCENE" : "> SCENE FAIL");
          },
          (void *)homeIds[homeN], kMargin, y, kScreenW - 2 * kMargin, 36, s.confirm);
      homeN++;
      y += 42;
    }
  }
  if (!any) {
    lv_obj_t *m = lv_label_create(scr);
    lv_label_set_text(m, "No home scenes.\nOpen Basement CTRL.");
    lv_obj_set_style_text_color(m, COL_DIM, 0);
    lv_obj_set_style_text_font(m, UI_FONT_SM, 0);
    lv_obj_set_pos(m, kMargin, 40);
    placeBtn(scr, "BASEMENT CTRL >", onGotoBasement, nullptr, kMargin, 90,
             kScreenW - 2 * kMargin, 32);
  }
  drawTabs(scr, Screen::Scenes);
}

static void showBasement() {
  screen = Screen::Basement;
  persistNav();
  clearRoot();
  lv_obj_t *scr = lv_screen_active();

  // CTRL geometry — leave room for bottom tabs (26px)
#ifdef WALLDECK_UI_V2
  constexpr int tabH = 26;
  constexpr int innerW = kScreenW - 2 * kMargin;
  constexpr int gap = 6;
  constexpr int sceneH = 38;
  constexpr int sceneRows = 5;
  constexpr int sceneW = (innerW - gap) / 2;
  constexpr int gridH = sceneRows * sceneH + (sceneRows - 1) * gap;
  constexpr int gridY = 28;
  constexpr int nowY = gridY + gridH + 6;
  constexpr int nowH = kScreenH - tabH - kMargin - nowY;
  constexpr int rowH = 32;
  constexpr int y1 = 26, y2 = 62, y3 = 98, y4 = 134;
  constexpr int linkY = nowH - 20;
  constexpr int devW = 200, ps5W = 80, ps5X = 208;
  const char *title = "BASEMENT CTRL 3.5";
#else
  constexpr int tabH = 26;
  constexpr int innerW = kScreenW - 2 * kMargin;
  constexpr int gap = 3;
  constexpr int sceneH = 22;
  constexpr int sceneRows = 5;
  constexpr int sceneW = (innerW - gap) / 2;
  constexpr int gridH = sceneRows * sceneH + (sceneRows - 1) * gap;
  constexpr int gridY = 18;
  constexpr int nowY = gridY + gridH + 4;
  constexpr int nowH = kScreenH - tabH - 2 - nowY;
  constexpr int rowH = 24;
  constexpr int y1 = 16, y2 = 44, y3 = 72, y4 = 100;
  constexpr int linkY = nowH - 18;
  constexpr int devW = 100, ps5W = 48, ps5X = 104;
  const char *title = "BASEMENT CTRL";
#endif
  static_assert(nowY + nowH + 26 <= kScreenH + 4, "now panel overflows screen");

  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, title);
  lv_obj_set_style_text_color(hdr, COL_CYAN, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT_SM, 0);
  lv_obj_set_pos(hdr, kMargin, 3);

  wifi_label = lv_label_create(scr);
  bool wifiOk = WiFi.status() == WL_CONNECTED && g_ctx && g_ctx->core && g_ctx->core->online();
  lv_label_set_text(wifi_label, wifiOk ? "WIFI OK" : "WIFI --");
  lv_obj_set_style_text_color(wifi_label, wifiOk ? COL_ACTIVE : COL_AMBER, 0);
  lv_obj_set_style_text_font(wifi_label, UI_FONT_SM, 0);
  lv_obj_align(wifi_label, LV_ALIGN_TOP_RIGHT, -kMargin, 4);

  for (int i = 0; i < kSceneCount; i++) {
    const int col = i % 2;
    const int row = i / 2;
    const int x = kMargin + col * (sceneW + gap);
    const int y = gridY + row * (sceneH + gap);
    lv_obj_t *btn = lv_button_create(scr);
    scene_btns[i] = btn;
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, sceneW, sceneH);
    styleSceneBtn(btn, i == activeScene, kScenes[i].accent);
    lv_obj_add_event_cb(btn, onSceneClick, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, kScenes[i].tag);
    lv_obj_set_style_text_font(lbl, UI_FONT_SM, 0);
    lv_obj_center(lbl);
  }
  refreshSceneStyles();

  lv_obj_t *now = lv_obj_create(scr);
  lv_obj_remove_style_all(now);
  lv_obj_set_pos(now, kMargin, nowY);
  lv_obj_set_size(now, innerW, nowH);
  lv_obj_set_style_bg_color(now, COL_PANEL, 0);
  lv_obj_set_style_bg_opa(now, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(now, 1, 0);
  lv_obj_set_style_border_color(now, COL_CYAN, 0);
  lv_obj_set_style_radius(now, 3, 0);
  lv_obj_set_style_pad_all(now, 0, 0);
  lv_obj_clear_flag(now, LV_OBJ_FLAG_SCROLLABLE);

  constexpr int ix = 4;
  constexpr int iw = innerW - 8;

  now_label = lv_label_create(now);
  lv_label_set_long_mode(now_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(now_label, iw - 76);
  lv_label_set_text(now_label, "MAIN | READY");
  lv_obj_set_style_text_color(now_label, COL_ACTIVE, 0);
  lv_obj_set_style_text_font(now_label, UI_FONT_SM, 0);
  lv_obj_set_pos(now_label, ix, 3);

  status_label = lv_label_create(now);
  lv_label_set_text(status_label, "READY");
  lv_obj_set_width(status_label, 70);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(status_label, COL_AMBER, 0);
  lv_obj_set_style_text_font(status_label, UI_FONT_SM, 0);
  lv_obj_set_pos(status_label, ix + iw - 70, 3);

  int x = ix;
#ifdef WALLDECK_UI_V2
  placeBtn(now, "MAIN", onAvKey, (void *)"HOME", x, y1, 52, rowH);
  x += 57;
  placeBtn(now, "-", onVol, (void *)"VOL-", x, y1, 36, rowH);
  x += 40;
  vol_label = lv_label_create(now);
  lv_obj_set_width(vol_label, 32);
  lv_obj_set_style_text_align(vol_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(vol_label, COL_DIM, 0);
  lv_obj_set_style_text_font(vol_label, UI_FONT, 0);
  lv_label_set_text(vol_label, "18");
  lv_obj_set_pos(vol_label, x, y1 + 8);
  x += 36;
  placeBtn(now, "+", onVol, (void *)"VOL+", x, y1, 36, rowH);
  x += 41;
  placeBtn(now, "WATCH", onAvWatch, nullptr, x, y1, 58, rowH);
  x += 63;
  placeBtn(now, "IN", onAvInput, (void *)"fire", x, y1, 42, rowH);
#else
  placeBtn(now, "MAIN", onAvKey, (void *)"HOME", x, y1, 36, rowH);
  x += 39;
  placeBtn(now, "-", onVol, (void *)"VOL-", x, y1, 28, rowH);
  x += 31;
  vol_label = lv_label_create(now);
  lv_obj_set_width(vol_label, 32);
  lv_obj_set_style_text_align(vol_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(vol_label, COL_DIM, 0);
  lv_obj_set_style_text_font(vol_label, UI_FONT, 0);
  lv_label_set_text(vol_label, "18");
  lv_obj_set_pos(vol_label, x, y1 + 5);
  x += 34;
  placeBtn(now, "+", onVol, (void *)"VOL+", x, y1, 28, rowH);
  x += 31;
  placeBtn(now, "WATCH", onAvWatch, nullptr, x, y1, 40, rowH);
  x += 43;
  placeBtn(now, "IN", onAvInput, (void *)"fire", x, y1, 32, rowH);
#endif

  constexpr int chipW = (iw - 3 * gap) / 4;
  placeBtn(now, "NFLX", onAvApp, (void *)"nflx", ix, y2, chipW, rowH);
  placeBtn(now, "YT", onAvApp, (void *)"yt", ix + chipW + gap, y2, chipW, rowH);
  placeBtn(now, "DSN+", onAvApp, (void *)"disney", ix + 2 * (chipW + gap), y2, chipW, rowH);
  placeBtn(now, "PRIME", onAvApp, (void *)"prime", ix + 3 * (chipW + gap), y2, chipW, rowH);

  constexpr int navW = (iw - 3 * gap) / 4;
  placeBtn(now, "HOME", onAvKey, (void *)"HOME", ix, y3, navW, rowH);
  placeBtn(now, "BACK", onAvKey, (void *)"BACK", ix + navW + gap, y3, navW, rowH);
  placeBtn(now, "OK", onAvKey, (void *)"OK", ix + 2 * (navW + gap), y3, navW, rowH);
  placeBtn(now, "MORE", onAvKey, (void *)"MORE", ix + 3 * (navW + gap), y3, navW, rowH);

#ifdef WALLDECK_UI_V2
  placeBtn(now, "DEVICES", onGotoDevices, nullptr, ix, y4, 96, 24);
  placeBtn(now, "WLED >", onGotoWled, nullptr, ix + 102, y4, 90, 24, true);
  placeBtn(now, "PS5", onAvApp, (void *)"ps5", ix + 200, y4, 70, 24);
#else
  placeBtn(now, "DEV", onGotoDevices, nullptr, ix, y4, 52, 24);
  placeBtn(now, "WLED", onGotoWled, nullptr, ix + 56, y4, 56, 24, true);
  placeBtn(now, "PS5", onAvApp, (void *)"ps5", ix + 116, y4, 48, 24);
#endif
  (void)devW;
  (void)ps5W;
  (void)ps5X;

  link_label = lv_label_create(now);
  lv_label_set_text(link_label, logLine);
  lv_obj_set_style_text_color(link_label, COL_CYAN, 0);
  lv_obj_set_style_text_font(link_label, UI_FONT_SM, 0);
  lv_obj_set_width(link_label, iw);
  lv_label_set_long_mode(link_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(link_label, ix, linkY);

  makeConfirm(scr);
  drawTabs(scr, Screen::Basement);
}

static void showWled() {
  screen = Screen::Wled;
  persistNav();
  clearRoot();
  lv_obj_t *scr = lv_screen_active();

  bool on = false;
  int bri = wledBriCache;
  int fx = wledFxCache;
  char name[28] = "WLED";
  if (g_ctx && g_ctx->core) {
    g_ctx->core->fetchWledState(on, bri, fx, name, sizeof(name));
    wledBriCache = bri > 0 ? bri : wledBriCache;
    wledFxCache = fx;
  }

  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, name);
  lv_obj_set_style_text_color(hdr, COL_AMBER, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT_SM, 0);
  lv_obj_set_pos(hdr, kMargin, 4);

  lv_obj_t *meta = lv_label_create(scr);
  char metaBuf[40];
  snprintf(metaBuf, sizeof(metaBuf), "%s · %d", on ? "ON" : "OFF", wledBriCache);
  lv_label_set_text(meta, metaBuf);
  lv_obj_set_style_text_color(meta, on ? COL_ACTIVE : COL_AMBER, 0);
  lv_obj_set_style_text_font(meta, UI_FONT_SM, 0);
  lv_obj_align(meta, LV_ALIGN_TOP_RIGHT, -kMargin, 4);

  placeBtn(scr, "< BACK", onGotoBasement, nullptr, kMargin, 24, 64, 24);

  const int y0 = 54;
  const int gap = 4;
  const int bw = (kScreenW - 2 * kMargin - gap) / 2;
  placeBtn(scr, "ON", onWledOn, (void *)"1", kMargin, y0, bw, 28);
  placeBtn(scr, "OFF", onWledOn, (void *)"0", kMargin + bw + gap, y0, bw, 28);

  placeBtn(scr, "BRI -", onWledBri, (void *)(intptr_t)-32, kMargin, y0 + 34, bw, 28);
  placeBtn(scr, "BRI +", onWledBri, (void *)(intptr_t)32, kMargin + bw + gap, y0 + 34, bw, 28);

  static const char *warm = "255,180,90";
  static const char *cool = "180,210,255";
  static const char *red = "255,40,40";
  static const char *blue = "40,100,255";
  placeBtn(scr, "WARM", onWledColor, (void *)warm, kMargin, y0 + 68, bw, 26, true);
  placeBtn(scr, "COOL", onWledColor, (void *)cool, kMargin + bw + gap, y0 + 68, bw, 26);
  placeBtn(scr, "RED", onWledColor, (void *)red, kMargin, y0 + 98, bw, 26);
  placeBtn(scr, "BLUE", onWledColor, (void *)blue, kMargin + bw + gap, y0 + 98, bw, 26);

  const int fw = (kScreenW - 2 * kMargin - 3 * gap) / 4;
  placeBtn(scr, "SOLID", onWledFx, (void *)(intptr_t)0, kMargin, y0 + 132, fw, 26);
  placeBtn(scr, "RAIN", onWledFx, (void *)(intptr_t)9, kMargin + fw + gap, y0 + 132, fw, 26);
  placeBtn(scr, "AUR", onWledFx, (void *)(intptr_t)38, kMargin + 2 * (fw + gap), y0 + 132, fw, 26);
  placeBtn(scr, "FIRE", onWledFx, (void *)(intptr_t)115, kMargin + 3 * (fw + gap), y0 + 132, fw, 26);

  placeBtn(scr, "P1", onWledPs, (void *)(intptr_t)1, kMargin, y0 + 164, fw, 26);
  placeBtn(scr, "P2", onWledPs, (void *)(intptr_t)2, kMargin + fw + gap, y0 + 164, fw, 26);
  placeBtn(scr, "P3", onWledPs, (void *)(intptr_t)3, kMargin + 2 * (fw + gap), y0 + 164, fw, 26);
  placeBtn(scr, "P4", onWledPs, (void *)(intptr_t)4, kMargin + 3 * (fw + gap), y0 + 164, fw, 26);

  lv_obj_t *hint = lv_label_create(scr);
  lv_label_set_text(hint, "presets = WLED saved slots");
  lv_obj_set_style_text_color(hint, COL_DIM, 0);
  lv_obj_set_style_text_font(hint, UI_FONT_SM, 0);
  lv_obj_set_pos(hint, kMargin, y0 + 198);

  drawTabs(scr, Screen::Wled);
}

static void showDevices() {
  screen = Screen::Devices;
  clearRoot();
  lv_obj_t *scr = lv_screen_active();

  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, "DEVICES");
  lv_obj_set_style_text_color(hdr, COL_CYAN, 0);
  lv_obj_set_style_text_font(hdr, UI_FONT, 0);
  lv_obj_set_pos(hdr, kMargin, 4);

  placeBtn(scr, "< BACK", onGotoBasement, nullptr, kScreenW - 72, 2, 68, 22);

  lv_obj_t *list = lv_obj_create(scr);
  lv_obj_set_pos(list, kMargin, 28);
  lv_obj_set_size(list, kScreenW - 2 * kMargin, kScreenH - 28 - 36);
  lv_obj_set_style_bg_color(list, COL_BG, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_gap(list, 3, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  // Settings: deep sleep (default OFF so OTA always works)
  {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 36);
    lv_obj_set_style_bg_color(row, COL_PANEL, 0);
    lv_obj_set_style_border_color(row, COL_AMBER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 3, 0);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, "DEEP SLEEP");
    lv_obj_set_style_text_color(name, COL_AMBER, 0);
    lv_obj_set_style_text_font(name, UI_FONT, 0);

    const bool on = power_deep_sleep_enabled();
    lv_obj_t *tog = lv_button_create(row);
    lv_obj_set_size(tog, 52, 26);
    stylePanelBtn(tog, on);
    lv_obj_add_event_cb(tog, onToggleDeepSleep, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *tl = lv_label_create(tog);
    lv_label_set_text(tl, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(tl, on ? COL_ACTIVE : COL_CYAN, 0);
    lv_obj_set_style_text_font(tl, UI_FONT, 0);
    lv_obj_center(tl);
  }

  if (!g_ctx || !g_ctx->core) return;
  for (size_t i = 0; i < g_ctx->core->deviceCount(); i++) {
    const CoreDevice &d = g_ctx->core->device(i);
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 36);
    lv_obj_set_style_bg_color(row, COL_PANEL, 0);
    lv_obj_set_style_border_color(row, d.online ? COL_CYAN : COL_DIM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 3, 0);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text_fmt(name, "%s", d.name);
    lv_obj_set_style_text_color(name, d.online ? COL_CYAN : COL_DIM, 0);
    lv_obj_set_style_text_font(name, UI_FONT, 0);

    if (strcmp(d.type, "wiz_bulb") == 0 || strcmp(d.type, "wled") == 0 ||
        strcmp(d.type, "hue") == 0) {
      lv_obj_t *tog = lv_button_create(row);
      lv_obj_set_size(tog, 44, 26);
      stylePanelBtn(tog, false);
      lv_obj_add_event_cb(tog, light_toggle_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
      lv_obj_t *tl = lv_label_create(tog);
      lv_label_set_text(tl, "PWR");
      lv_obj_set_style_text_color(tl, COL_CYAN, 0);
      lv_obj_set_style_text_font(tl, UI_FONT, 0);
      lv_obj_center(tl);
    } else {
      lv_obj_t *st = lv_label_create(row);
      lv_label_set_text(st, d.online ? "UP" : "DARK");
      lv_obj_set_style_text_color(st, d.online ? COL_ACTIVE : COL_AMBER, 0);
      lv_obj_set_style_text_font(st, UI_FONT, 0);
    }
  }
  drawTabs(scr, Screen::Devices);
}

static void resumeNav() {
  prefs.begin("walldeck", true);
  activeScene = prefs.getInt("scene", -1);
  int tab = prefs.getInt("tab", (int)Screen::Dash);
  prefs.end();
  Screen s = (Screen)tab;
  if (s == Screen::Basement) showBasement();
  else if (s == Screen::Zones) showZones();
  else if (s == Screen::Scenes) showHomeScenes();
  else if (s == Screen::Devices) showDevices();
  else if (s == Screen::Grace) showGrace();
  else if (s == Screen::Wled) showWled();
  else if (s == Screen::Room) showRoom();
  else if (s == Screen::Party) showParty();
  else showDash();
}

void ui_init(AppContext *ctx) {
  g_ctx = ctx;
  resumeNav();
  ui_log("> GRID READY");
}

void ui_refresh(AppContext *ctx) {
  g_ctx = ctx;
  if (screen == Screen::Grace) {
    static uint32_t lastGrace = 0;
    if (millis() - lastGrace > 450) {
      lastGrace = millis();
      showGrace();
    }
    return;
  }
  if (!wifi_label) return;
  bool wifiOk = WiFi.status() == WL_CONNECTED && g_ctx && g_ctx->core && g_ctx->core->online();
  lv_label_set_text(wifi_label, wifiOk ? "WIFI OK" : "WIFI --");
  lv_obj_set_style_text_color(wifi_label, wifiOk ? COL_ACTIVE : COL_AMBER, 0);
  if (!wifiOk && screen == Screen::Basement && status_label) {
    lv_label_set_text(status_label, "CORE?");
  }
}
