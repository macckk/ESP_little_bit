// Interface: barra de status fixa + area de conteudo montada a partir do JSON.
//
// Layouts (campo "layout"):
//   status : {"title","items":[{"label","state":"ok|warn|err|off","value"}]}
//   number : {"title","label","value","sub","progress":0..1,"color":"#RRGGBB"}
//   qr     : {"title","qr","label","value","sub"}
//   message: {"title","text","icon":"ok|warn|err|info"}
//   pet    : {"title","mood":"sleeping|idle|working|attention|done|error","bubble","sub"} (pet.cpp)
//
// Interatividade (todos os layouts menos "pet"):
//   "buttons": [{"id","label"}]  -> fileira de ate 4 botoes embaixo; toque = acao {"action": id}
//   item de "status" com "action" -> a linha vira botao: {"action": item.action, "value": item.id|label}
// Apps: "app" (id do app atual) e "apps": [{"id","name"}] -> tocar no titulo da barra abre o menu de apps.
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include "app.h"

static constexpr int BAR_H = 24;
static constexpr uint32_t C_BG = 0x000000, C_BAR = 0x1A1A1A, C_ROW = 0x1E1E1E, C_DIM = 0x9E9E9E;
static constexpr uint32_t C_OK = 0x43A047, C_WARN = 0xFFB300, C_ERR = 0xE53935, C_OFF = 0x616161;

static lv_obj_t *lblTitle, *lblOffline, *lblWifi, *lblTime, *barRight;
static lv_obj_t* content;
static lv_obj_t *ovBox, *ovTitle, *ovText, *ovBar;

// apps e acoes (recriados a cada tela)
static JsonDocument appsDoc;  // "apps" da ultima tela
static String curApp;
static lv_obj_t* appMenu = nullptr;
static constexpr int MAX_BTN = 4, MAX_ITEMS = 16;
static String btnIds[MAX_BTN], itemAct[MAX_ITEMS], itemVal[MAX_ITEMS];
static int areaH = 0;  // altura util da area do layout (menor com botoes)
static void openAppMenu();

// ------------------------------------------------------------ helpers

static lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, txt ? txt : "");
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

static uint32_t stateColor(const char* s) {
  if (!s) return C_OFF;
  if (!strcmp(s, "ok")) return C_OK;
  if (!strcmp(s, "warn")) return C_WARN;
  if (!strcmp(s, "err")) return C_ERR;
  return C_OFF;
}

static uint32_t parseHex(const char* s, uint32_t def) {
  if (!s || s[0] != '#' || strlen(s) != 7) return def;
  return strtoul(s + 1, nullptr, 16);
}

static const char* iconFor(const char* i) {
  if (!i) return nullptr;
  if (!strcmp(i, "ok")) return LV_SYMBOL_OK;
  if (!strcmp(i, "warn") || !strcmp(i, "err")) return LV_SYMBOL_WARNING;
  if (!strcmp(i, "info")) return LV_SYMBOL_BELL;
  return nullptr;
}

void ui_set_title(const char* t) {
  t = t ? t : "";
  if (strcmp(lv_label_get_text(lblTitle), t)) lv_label_set_text(lblTitle, t);
}
static void setTitle(const char* t) { ui_set_title(t); }

// --------------------------------------------------------------- init

void ui_init() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // barra de status
  lv_obj_t* bar = plain(scr);
  lv_obj_set_size(bar, LV_PCT(100), BAR_H);
  lv_obj_set_style_bg_color(bar, lv_color_hex(C_BAR), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

  lblTitle = label(bar, "", &font_pt_14, 0xFFFFFF);
  lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lblTitle, 150);
  lv_obj_align(lblTitle, LV_ALIGN_LEFT_MID, 6, 0);
  // tocar no titulo abre o menu de apps (se o backend mandou mais de um)
  lv_obj_add_flag(lblTitle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(lblTitle, 10);
  lv_obj_add_event_cb(lblTitle, [](lv_event_t*) { openAppMenu(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* right = barRight = plain(bar);
  lv_obj_set_size(right, LV_SIZE_CONTENT, BAR_H);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  // START (nao END): com largura LV_SIZE_CONTENT, END faz o bloco medir so o ultimo item
  // e os outros (icone Wi-Fi) ficam fora da area e sao cortados.
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(right, 8, 0);
  lv_obj_align(right, LV_ALIGN_RIGHT_MID, -6, 0);
  lblOffline = label(right, "", &font_pt_14, C_ERR);
  lblWifi = label(right, LV_SYMBOL_WIFI, &font_pt_14, C_OFF);
  lblTime = label(right, "--:--", &font_pt_14, 0xFFFFFF);

  // conteudo
  content = plain(scr);
  lv_obj_set_pos(content, 0, BAR_H);
  lv_obj_set_size(content, LV_PCT(100), lv_display_get_vertical_resolution(nullptr) - BAR_H);
  lv_obj_set_style_pad_all(content, 8, 0);

  // overlay (reset por BOOT / OTA) na camada de cima
  ovBox = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ovBox, 270, 120);
  lv_obj_center(ovBox);
  lv_obj_set_style_bg_color(ovBox, lv_color_hex(0x263238), 0);
  lv_obj_set_style_border_color(ovBox, lv_color_hex(C_WARN), 0);
  lv_obj_set_style_border_width(ovBox, 2, 0);
  lv_obj_remove_flag(ovBox, LV_OBJ_FLAG_SCROLLABLE);
  ovTitle = label(ovBox, "", &font_pt_20, 0xFFFFFF);
  lv_obj_align(ovTitle, LV_ALIGN_TOP_MID, 0, 0);
  ovText = label(ovBox, "", &font_pt_14, C_DIM);
  lv_obj_align(ovText, LV_ALIGN_CENTER, 0, 2);
  ovBar = lv_bar_create(ovBox);
  lv_obj_set_size(ovBar, 220, 12);
  lv_obj_align(ovBar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(ovBox, LV_OBJ_FLAG_HIDDEN);
}

void ui_overlay(const char* title, const char* text, int pct) {
  if (pct < 0) {
    lv_obj_add_flag(ovBox, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(ovTitle, title);
  lv_label_set_text(ovText, text);
  lv_bar_set_value(ovBar, pct, LV_ANIM_OFF);
  lv_obj_remove_flag(ovBox, LV_OBJ_FLAG_HIDDEN);
}

// ------------------------------------------------------ barra de status

void ui_tick_status() {
  struct tm t;
  if (getLocalTime(&t, 0) && t.tm_year > 120)
    lv_label_set_text_fmt(lblTime, "%02d:%02d", t.tm_hour, t.tm_min);
  else
    lv_label_set_text(lblTime, "--:--");

  uint32_t wc = C_OFF;
  if (WiFi.isConnected()) {
    int rssi = WiFi.RSSI();
    wc = rssi > -65 ? C_OK : rssi > -78 ? C_WARN : C_ERR;
  }
  lv_obj_set_style_text_color(lblWifi, lv_color_hex(wc), 0);

  bool offline = !netcfg_portal_active() && gNet.url.length() && !backend_online();
  if (!offline) {
    lv_label_set_text(lblOffline, "");
  } else {
    time_t last = backend_last_ok();
    struct tm lt;
    if (last > 1700000000 && localtime_r(&last, &lt))
      lv_label_set_text_fmt(lblOffline, LV_SYMBOL_WARNING " offline %02d:%02d", lt.tm_hour, lt.tm_min);
    else
      lv_label_set_text(lblOffline, LV_SYMBOL_WARNING " offline");
  }

  static int lastOffline = -1;
  if (offline != lastOffline) {
    lastOffline = offline;
    Serial.printf("BARRA %s: '%s'\n", offline ? "OFFLINE" : "online", lv_label_get_text(lblOffline));
  }

  // titulo ocupa so o que sobra a esquerda dos indicadores (nunca sobrepoe)
  lv_obj_update_layout(barRight);
  int32_t free_w = lv_obj_get_width(lv_obj_get_parent(barRight)) - lv_obj_get_width(barRight) - 6 - 6 - 8;
  lv_obj_set_width(lblTitle, LV_MAX(free_w, 40));
}

void ui_debug() {
  struct {
    const char* n;
    lv_obj_t* o;
  } items[] = {{"titulo", lblTitle}, {"direita", barRight}, {"offline", lblOffline}, {"wifi", lblWifi}, {"hora", lblTime}};
  for (auto& it : items) {
    lv_area_t a;
    lv_obj_get_coords(it.o, &a);
    Serial.printf("UI %-8s x=%d..%d y=%d..%d oculto=%d\n", it.n, (int)a.x1, (int)a.x2, (int)a.y1, (int)a.y2,
                  lv_obj_has_flag(it.o, LV_OBJ_FLAG_HIDDEN));
  }
  lv_font_glyph_dsc_t g;
  bool ok = lv_font_get_glyph_dsc(&font_pt_14, &g, 0xF1EB, 0);
  Serial.printf("UI glifo wifi: ok=%d w=%d h=%d adv=%d\n", ok, g.box_w, g.box_h, g.adv_w);
}

// ------------------------------------------------------------ layouts

static void buildMessage(lv_obj_t* parent, const char* text, const char* icon) {
  lv_obj_t* col = plain(parent);
  lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 10, 0);
  if (icon) label(col, icon, &font_pt_20, C_WARN);
  lv_obj_t* l = label(col, text, &font_pt_20, 0xFFFFFF);
  lv_obj_set_width(l, LV_PCT(95));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

void ui_show_message(const char* title, const char* text, const char* icon) {
  lv_obj_clean(content);
  setTitle(title);
  buildMessage(content, text, icon);
}

// ------------------------------------------------------- menu de apps

static void closeAppMenu() {
  if (appMenu) lv_obj_delete(appMenu);
  appMenu = nullptr;
}

static void appChosen(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  JsonArrayConst apps = appsDoc.as<JsonArrayConst>();
  closeAppMenu();
  if (i < 0 || i >= (int)apps.size()) return;
  const char* id = apps[i]["id"] | "";
  if (curApp != id) {
    backend_send_action("app", id);
    String msg = String("Abrindo ") + (apps[i]["name"] | id) + "…";
    ui_show_message(apps[i]["name"] | "", msg.c_str(), LV_SYMBOL_REFRESH);  // a tela nova chega pelo long-poll
  }
}

// --- Reiniciar (fim do menu, com confirmacao)

static lv_obj_t* menuButton(lv_obj_t* parent, const char* txt, uint32_t bg, int w) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, w, 34);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_t* l = label(b, txt, &font_pt_20, 0xFFFFFF);
  lv_obj_center(l);
  return b;
}

static void restartNow(lv_event_t*) {
  closeAppMenu();
  ui_overlay("Reiniciando…", "volta em alguns segundos", 100);
  // da tempo de a mensagem aparecer na tela antes de reiniciar
  lv_timer_create([](lv_timer_t*) { ESP.restart(); }, 400, nullptr);
}

static void confirmRestart(lv_event_t*) {
  lv_obj_clean(appMenu);  // troca o menu pela pergunta
  label(appMenu, "Reiniciar a placa?", &font_pt_20, 0xFFFFFF);
  lv_obj_t* row = plain(appMenu);
  lv_obj_set_size(row, 260, 34);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(menuButton(row, "Cancelar", 0x37474F, 120), [](lv_event_t*) { closeAppMenu(); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(menuButton(row, "Reiniciar", 0xC62828, 120), restartNow, LV_EVENT_CLICKED, nullptr);
}

static void openAppMenu() {
  JsonArrayConst apps = appsDoc.as<JsonArrayConst>();
  closeAppMenu();
  appMenu = lv_obj_create(lv_layer_top());
  lv_obj_set_size(appMenu, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(appMenu, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(appMenu, LV_OPA_80, 0);
  lv_obj_set_style_border_width(appMenu, 0, 0);
  lv_obj_set_style_radius(appMenu, 0, 0);
  lv_obj_set_flex_flow(appMenu, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(appMenu, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(appMenu, 8, 0);
  lv_obj_add_event_cb(appMenu, [](lv_event_t*) { closeAppMenu(); }, LV_EVENT_CLICKED, nullptr);
  label(appMenu, apps.size() ? "Apps" : "Placa", &font_pt_20, 0xFFFFFF);
  int i = 0;
  for (JsonObjectConst a : apps) {
    if (i >= 4) break;  // cabe com o Reiniciar embaixo
    bool cur = curApp == (a["id"] | "");
    lv_obj_t* b = lv_button_create(appMenu);
    lv_obj_set_size(b, 240, 34);
    lv_obj_set_style_bg_color(b, lv_color_hex(cur ? 0x37474F : 0x263238), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xFFD166), 0);
    lv_obj_set_style_border_width(b, cur ? 2 : 0, 0);
    lv_obj_t* l = label(b, a["name"] | "", &font_pt_20, 0xFFFFFF);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, appChosen, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    i++;
  }
  lv_obj_t* r = menuButton(appMenu, LV_SYMBOL_REFRESH "  Reiniciar", 0x5D1F1F, 160);
  lv_obj_set_style_margin_top(r, 6, 0);
  lv_obj_add_event_cb(r, confirmRestart, LV_EVENT_CLICKED, nullptr);
}

void ui_open_app_menu() {  // serial "apps": abre/fecha
  if (appMenu)
    closeAppMenu();
  else
    openAppMenu();
}

// ---------------------------------------------------------- botoes

static void btnClicked(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= 0 && i < MAX_BTN && btnIds[i].length()) backend_send_action(btnIds[i].c_str());
}

static void itemClicked(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= 0 && i < MAX_ITEMS && itemAct[i].length()) backend_send_action(itemAct[i].c_str(), itemVal[i].c_str());
}

// Com "buttons", reserva a faixa de baixo para eles e devolve a area de cima para o layout.
static lv_obj_t* areaWithButtons(JsonArrayConst buttons) {
  for (auto& s : btnIds) s = String();
  areaH = lv_display_get_vertical_resolution(nullptr) - BAR_H - 16;  // conteudo tem 8 px de margem
  if (buttons.size() == 0) return content;
  areaH -= 46;
  lv_obj_t* body = plain(content);
  lv_obj_set_size(body, LV_PCT(100), areaH);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_t* row = plain(content);
  lv_obj_set_size(row, LV_PCT(100), 40);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  int i = 0;
  for (JsonObjectConst b : buttons) {
    if (i >= MAX_BTN) break;
    btnIds[i] = b["id"] | "";
    lv_obj_t* btn = lv_button_create(row);
    lv_obj_set_height(btn, LV_PCT(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x37474F), 0);
    lv_obj_t* l = label(btn, b["label"] | "", &font_pt_20, 0xFFFFFF);
    lv_obj_center(l);
    lv_obj_add_event_cb(btn, btnClicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    i++;
  }
  return body;
}

static lv_obj_t* makeQr(lv_obj_t* parent, const char* data, int size) {
  lv_obj_t* qr = lv_qrcode_create(parent);
  lv_qrcode_set_size(qr, size);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_update(qr, data, strlen(data));
  // zona de silencio branca
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 6, 0);
  return qr;
}

void ui_show_portal(const char* ap, const char* pass) {
  lv_obj_clean(content);
  setTitle("Configuração");
  char wifiQr[96];
  snprintf(wifiQr, sizeof wifiQr, "WIFI:T:WPA;S:%s;P:%s;;", ap, pass);
  lv_obj_t* qr = makeQr(content, wifiQr, 140);
  lv_obj_align(qr, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* col = plain(content);
  lv_obj_set_size(col, 150, LV_PCT(100));
  lv_obj_align(col, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 2, 0);
  label(col, "1. Conecte no Wi-Fi", &font_pt_14, 0xFFFFFF);
  label(col, ap, &font_pt_14, C_WARN);
  label(col, "senha:", &font_pt_14, 0xFFFFFF);
  label(col, pass, &font_pt_20, C_WARN);
  lv_obj_t* l = label(col, "2. A página abre sozinha (ou acesse 192.168.4.1)", &font_pt_14, C_DIM);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_top(l, 10, 0);
}

static void buildStatus(JsonObjectConst d, lv_obj_t* parent) {
  for (int i = 0; i < MAX_ITEMS; i++) itemAct[i] = itemVal[i] = String();
  lv_obj_t* list = plain(parent);
  lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 4, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  int n = 0;
  for (JsonObjectConst it : d["items"].as<JsonArrayConst>()) {
    if (++n > 16) break;
    uint32_t sc = stateColor(it["state"]);
    lv_obj_t* row = plain(list);
    lv_obj_set_size(row, LV_PCT(100), 30);
    lv_obj_set_style_bg_color(row, lv_color_hex(C_ROW), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    const char* act = it["action"] | "";
    if (*act) {  // linha tocavel: manda a acao com o id (ou nome) do item
      int k = n - 1;
      itemAct[k] = act;
      itemVal[k] = it["id"] | (it["label"] | "");
      lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_color(row, lv_color_hex(0x37474F), LV_STATE_PRESSED);
      lv_obj_add_event_cb(row, itemClicked, LV_EVENT_CLICKED, (void*)(intptr_t)k);
    }

    lv_obj_t* dot = plain(row);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(sc), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* l = label(row, it["label"] | "", &font_pt_20, 0xFFFFFF);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, 170);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 20, 0);

    const char* st = it["state"] | "";
    lv_obj_t* v = label(row, it["value"] | "", &font_pt_14, strcmp(st, "ok") ? sc : C_DIM);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

static void buildNumber(JsonObjectConst d, lv_obj_t* parent) {
  lv_obj_t* l = label(parent, d["label"] | "", &font_pt_20, C_DIM);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 4);

  lv_obj_t* v = label(parent, d["value"] | "-", &font_pt_48, parseHex(d["color"], 0xFFFFFF));
  lv_obj_align(v, LV_ALIGN_CENTER, 0, -14);

  lv_obj_t* s = label(parent, d["sub"] | "", &font_pt_20, 0xFFFFFF);
  lv_obj_align(s, LV_ALIGN_CENTER, 0, 34);

  if (!d["progress"].isNull()) {
    float p = constrain(d["progress"].as<float>(), 0.0f, 1.0f);
    lv_obj_t* b = lv_bar_create(parent);
    lv_obj_set_size(b, 280, 12);
    lv_bar_set_range(b, 0, 1000);
    lv_bar_set_value(b, (int)(p * 1000), LV_ANIM_OFF);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -6);
  }
}

static void buildQr(JsonObjectConst d, lv_obj_t* parent) {
  const char* data = d["qr"] | "";
  int side = min<int>(172, areaH - 12);
  lv_obj_t* qr = makeQr(parent, data, side);
  lv_obj_align(qr, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* col = plain(parent);
  lv_obj_set_size(col, 112, LV_PCT(100));
  lv_obj_align(col, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 6, 0);
  static const struct {
    const char* key;
    const lv_font_t* font;
    uint32_t color;
  } fields[] = {{"label", &font_pt_14, C_DIM}, {"value", &font_pt_20, 0xFFFFFF}, {"sub", &font_pt_14, C_DIM}};
  for (auto& f : fields) {
    const char* t = d[f.key] | "";
    if (!*t) continue;
    lv_obj_t* l = label(col, t, f.font, f.color);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
}

bool ui_render_json(const String& body, uint32_t& refresh, String& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    err = String("JSON invalido: ") + e.c_str();
    return false;
  }
  JsonObjectConst d = doc.as<JsonObjectConst>();
  if (d.isNull()) {
    err = "raiz nao e objeto";
    return false;
  }
  const char* layout = d["layout"] | "";
  refresh = constrain(d["refresh"] | 30, 1, 3600);
  String app = d["app"] | "";
  if (app != curApp) closeAppMenu();  // o pet atualiza a toda hora: so fecha o menu se o app mudou
  curApp = app;
  if (!d["apps"].isNull() && !appMenu) {
    appsDoc.clear();
    appsDoc.set(d["apps"]);
  }

  if (!strcmp(layout, "pet")) {  // atualiza no lugar: nao reinicia a animacao
    if (!pet_is_built()) {
      lv_obj_clean(content);
      pet_build(content);
    }
    pet_update(d);
    return true;
  }

  lv_obj_clean(content);
  setTitle(d["title"] | "");
  lv_obj_t* area = areaWithButtons(d["buttons"].as<JsonArrayConst>());
  if (!strcmp(layout, "status"))
    buildStatus(d, area);
  else if (!strcmp(layout, "number"))
    buildNumber(d, area);
  else if (!strcmp(layout, "qr"))
    buildQr(d, area);
  else if (!strcmp(layout, "message"))
    buildMessage(area, d["text"] | "", iconFor(d["icon"]));
  else {
    String msg = String("Layout desconhecido: ") + layout;
    buildMessage(area, msg.c_str(), LV_SYMBOL_WARNING);
  }
  return true;
}
