// Pet de agentes em pixel art (pacotes baixados do backend; ver sprites.cpp), animado localmente.
// O backend so manda o estado: {"layout":"pet","mood","bubble","sub",...}.
//
// Pets: os de pets/ no backend + o gato embutido no firmware (reserva). Trocar: arrastar o dedo para o lado sobre o
// pet, ou segurar o dedo para abrir o seletor. Escolha salva na NVS ("cyd"/"pet" = "spr:<nome>").
#include <Preferences.h>
#include <math.h>

#include "app.h"

enum Mood { M_SLEEP, M_IDLE, M_WORK, M_ATTN, M_DONE, M_ERROR };

static constexpr int PET_W = 162, PET_H = 200;
static constexpr int SIDE_W = 138;  // coluna da direita: balao, uso e rodape

static lv_obj_t *petObj = nullptr, *bubbleBox, *bubbleLbl, *usageLbl, *subLbl, *picker = nullptr;
static lv_timer_t* animTimer = nullptr;
static Mood mood = M_SLEEP;
static int skin = 0;  // indice em sprNames
static uint32_t moodSince = 0, petUntil = 0;
static String title = "Agentes";

// pets em pixel art
static constexpr int MAX_SPR = 12;
static String sprNames[MAX_SPR];
static int sprCount = 0;
static uint32_t sprGen = 0;
static SpritePack spr;              // pack do pet escolhido
static String wantSprite;           // nome salvo na NVS (pode ainda nao ter sido baixado)
static SpritePack thumbs[MAX_SPR];  // miniaturas do seletor (so com ele aberto)
static int totalSkins() { return sprCount; }

// --------------------------------------------------------- primitivas

struct Ctx {
  lv_layer_t* L;
  int ox, oy;
};

static void rrect(Ctx& c, int x, int y, int w, int h, int r, uint32_t col, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_color_hex(col);
  d.bg_opa = opa;
  d.radius = r;
  lv_area_t a = {c.ox + x, c.oy + y, c.ox + x + w - 1, c.oy + y + h - 1};
  lv_draw_rect(c.L, &d, &a);
}

static void circle(Ctx& c, int cx, int cy, int r, uint32_t col, lv_opa_t opa = LV_OPA_COVER) {
  rrect(c, cx - r, cy - r, 2 * r + 1, 2 * r + 1, LV_RADIUS_CIRCLE, col, opa);
}

static void tri(Ctx& c, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t col) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.bg_color = lv_color_hex(col);
  d.bg_opa = LV_OPA_COVER;
  d.p[0].x = c.ox + x1;
  d.p[0].y = c.oy + y1;
  d.p[1].x = c.ox + x2;
  d.p[1].y = c.oy + y2;
  d.p[2].x = c.ox + x3;
  d.p[2].y = c.oy + y3;
  lv_draw_triangle(c.L, &d);
}

static void line(Ctx& c, int x1, int y1, int x2, int y2, int w, uint32_t col) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(col);
  d.width = w;
  d.round_start = d.round_end = 1;
  d.p1.x = c.ox + x1;
  d.p1.y = c.oy + y1;
  d.p2.x = c.ox + x2;
  d.p2.y = c.oy + y2;
  lv_draw_line(c.L, &d);
}

// angulos em graus, 0 = direita, sentido horario (90 = baixo)
static void text(Ctx& c, int x, int y, const char* s, const lv_font_t* f, uint32_t col, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = s;  // literais: continuam validos ate o desenho
  d.font = f;
  d.color = lv_color_hex(col);
  d.opa = opa;
  lv_area_t a = {c.ox + x, c.oy + y, c.ox + x + 40, c.oy + y + 30};
  lv_draw_label(c.L, &d, &a);
}

static float wave(uint32_t t, uint32_t periodMs, float phase = 0) {
  return sinf((t % periodMs) * 2 * (float)M_PI / periodMs + phase);
}

static bool blinking(uint32_t t) { return (t % 3700) < 130; }

// ------------------------------------------------------- pixel art

// Desenha um quadro em blocos SxS, juntando pixels iguais vizinhos; pula linhas fora da faixa sendo pintada.
static void drawPack(Ctx& c, const SpritePack& p, int frame, int x0, int y0, int S) {
  if (!p.data || frame < 0 || frame >= p.nframes) return;
  const int stride = (p.w + 1) / 2;
  const uint8_t* fr = p.data + (size_t)frame * stride * p.h;
  const lv_area_t& clip = c.L->_clip_area;
  auto px = [&](const uint8_t* row, int x) { return (uint8_t)((row[x / 2] >> (x % 2 ? 0 : 4)) & 0x0F); };
  for (int y = 0; y < p.h; y++) {
    int ry = c.oy + y0 + y * S;
    if (ry + S - 1 < clip.y1 || ry > clip.y2) continue;
    const uint8_t* row = fr + y * stride;
    int x = 0;
    while (x < p.w) {
      uint8_t v = px(row, x);
      int x2 = x + 1;
      while (x2 < p.w && px(row, x2) == v) x2++;
      if (v) rrect(c, x0 + x * S, y0 + y * S, (x2 - x) * S, S, 0, p.pal[v]);
      x = x2;
    }
  }
}

static int sprFrame(const SpritePack& p, uint8_t role) {
  int f = sprites_frame_for(p, role);
  if (f >= 0) return f;
  if (role == SPR_SLEEP) return sprFrame(p, SPR_BLINK);  // olhos fechados servem para dormir
  return 0;
}

static void drawSprite(Ctx& c, Mood m, uint32_t t, bool pet, int dy) {
  if (m == M_SLEEP && !pet && sprites_frame_for(spr, SPR_BREATHE) >= 0) dy = 0;  // a respiracao ja mexe
  int S = max(1, min((PET_W - 6) / spr.w, (PET_H - 44) / spr.h));
  int dx = m == M_ERROR ? ((t / 70) % 2 ? 2 : -2) : 0;  // tremendo
  int x0 = (PET_W - spr.w * S) / 2 + dx, y0 = PET_H - 10 - spr.h * S + dy;
  uint8_t role = SPR_NORMAL;
  if (pet) role = sprites_frame_for(spr, SPR_HAPPY) >= 0 ? SPR_HAPPY : SPR_BLINK;
  else if (m == M_SLEEP) role = SPR_SLEEP;
  else if (m == M_ATTN) role = SPR_ALERT;
  else if (m == M_DONE) role = SPR_HAPPY;
  else if (m == M_WORK) role = SPR_WORK;
  int f = sprFrame(spr, role);
  if (f == 0 && m != M_SLEEP && blinking(t)) {
    f = sprFrame(spr, SPR_BLINK);
  } else if (f == 0 && !pet && m != M_SLEEP) {  // abana o rabo: em rajadas quando ocioso, sem parar quando comemora
    int tail = sprites_frame_for(spr, SPR_TAIL);
    uint32_t step = m == M_DONE ? 180 : m == M_ATTN ? 200 : m == M_WORK ? 260 : 320;
    bool burst = m != M_IDLE || (t % 4200) < 1300;
    if (tail >= 0 && burst && (t / step) % 2) f = tail;
  } else if (role == SPR_SLEEP && !pet) {  // respira devagar
    int br = sprites_frame_for(spr, SPR_BREATHE);
    if (br >= 0 && (t % 3600) >= 1800) f = br;
  }
  drawPack(c, spr, f, x0, y0, S);
}

// ----------------------------------------------------- efeitos extras

static void drawExtras(Ctx& c, int cx, int top, Mood m, uint32_t t, bool pet) {
  if (m == M_SLEEP) {  // zZz subindo
    for (int i = 0; i < 3; i++) {
      uint32_t ph = (t + i * 900) % 2700;
      int y = top + 30 - (int)(ph * 40 / 2700);
      lv_opa_t opa = (lv_opa_t)(255 - ph * 255 / 2700);
      text(c, cx + 40 + i * 12, y, i == 1 ? "Z" : "z", i == 1 ? &font_pt_20 : &font_pt_14, 0xB0BEC5, opa);
    }
  } else if (m == M_ATTN) {  // ! piscando
    if (wave(t, 500) > -0.3f) {
      rrect(c, cx + 52, top - 2, 10, 28, 4, 0xFFB300);
      circle(c, cx + 57, top + 34, 5, 0xFFB300);
    }
  } else if (m == M_WORK) {  // ... pensando
    for (int i = 0; i < 3; i++) {
      bool on = (t / 300) % 4 > (uint32_t)i;
      circle(c, cx + 44 + i * 12, top + 10, 4, on ? 0xFFFFFF : 0x555555);
    }
  } else if (m == M_DONE) {  // brilhos
    for (int i = 0; i < 4; i++) {
      float a = i * 1.57f + (t % 4000) * 0.0016f;
      int x = cx + (int)(cosf(a) * 78), y = top + 70 + (int)(sinf(a) * 70);
      int s = 3 + (int)((wave(t, 600, i) + 1) * 2);
      line(c, x - s, y, x + s, y, 2, 0xFFEB3B);
      line(c, x, y - s, x, y + s, 2, 0xFFEB3B);
    }
  }
  if (pet) {  // coracoes subindo
    for (int i = 0; i < 2; i++) {
      uint32_t ph = (t + i * 500) % 1000;
      int x = cx - 30 + i * 60, y = top + 30 - (int)(ph * 30 / 1000);
      circle(c, x - 4, y, 5, 0xFF4D6D);
      circle(c, x + 4, y, 5, 0xFF4D6D);
      tri(c, x - 9, y + 2, x + 9, y + 2, x, y + 12, 0xFF4D6D);
    }
  }
}

// ------------------------------------------------------------ desenho

static void drawCb(lv_event_t* e) {
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(petObj, &a);
  Ctx c{layer, a.x1, a.y1};
  uint32_t t = lv_tick_get();
  bool pet = t < petUntil;
  Mood m = mood;

  // movimento do corpo inteiro
  int dy = 0;
  uint32_t since = t - moodSince;
  switch (m) {
    case M_SLEEP: dy = (int)(wave(t, 4000) * 2); break;
    case M_IDLE: dy = (int)(wave(t, 3000) * 3); break;
    case M_WORK: dy = (int)(fabsf(wave(t, 500)) * -3); break;
    case M_ATTN: dy = -(int)(fabsf(wave(t, since < 3000 ? 450 : 900)) * 16); break;
    case M_DONE: dy = since < 4000 ? -(int)(fabsf(wave(t, 600)) * 12) : (int)(wave(t, 3000) * 2); break;
    default: break;
  }
  if (pet) dy = -(int)(fabsf(wave(t, 400)) * 6);
  if (spr.data) drawSprite(c, m, t, pet, dy);
  drawExtras(c, PET_W / 2, 4, m, t, pet);
}

static void syncSprites();

static void animCb(lv_timer_t*) {
  if (!petObj) return;
  if (sprGen != sprites_generation()) syncSprites();
  // dormindo anima devagar (economiza CPU/SPI)
  lv_timer_set_period(animTimer, mood == M_SLEEP && lv_tick_get() >= petUntil ? 120 : 45);
  lv_obj_invalidate(petObj);
}

// ------------------------------------------------------- troca de skin

// Titulo da barra vem do backend ("Little Bit Pixel"); a direita ficam hora, Wi-Fi e aviso offline.
static void updateTitle() { ui_set_title(title.c_str()); }

static const char* skinName(int s) { return s >= 0 && s < sprCount ? sprNames[s].c_str() : "?"; }

static void setSkin(int s) {
  int n = totalSkins();
  if (!n) return;
  s = ((s % n) + n) % n;
  if (!sprites_load(sprNames[s].c_str(), spr)) return;
  skin = s;
  wantSprite = sprNames[s];
  Preferences p;
  p.begin("cyd", false);
  p.putString("pet", "spr:" + wantSprite);
  p.end();
  updateTitle();
  if (petObj) lv_obj_invalidate(petObj);
  Serial.printf("PET skin=%s\n", skinName(s));
}

// Pasta /sprites mudou (download/remocao): refaz a lista e reposiciona o pet escolhido.
static void syncSprites() {
  sprGen = sprites_generation();
  sprCount = sprites_names(sprNames, MAX_SPR);
  // o escolhido; se sumiu ou ainda nao chegou, o embutido ate la (sem esquecer a escolha salva)
  int idx = -1, builtin = 0;
  for (int i = 0; i < sprCount; i++) {
    if (sprNames[i] == wantSprite) idx = i;
    if (sprNames[i] == "gato") builtin = i;
  }
  skin = idx >= 0 ? idx : builtin;
  if (!sprites_load(sprNames[skin].c_str(), spr)) sprites_free(spr);  // (re)carrega: pode ter versao nova
  Serial.printf("PET %d pet(s); escolhido: %s\n", sprCount, skinName(skin));
  if (petObj) lv_obj_invalidate(petObj);
}

static void closePicker() {
  if (picker) lv_obj_delete(picker);
  picker = nullptr;
  for (auto& t : thumbs) sprites_free(t);
}

static void thumbDraw(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= MAX_SPR || !thumbs[i].data) return;
  lv_obj_t* b = (lv_obj_t*)lv_event_get_target(e);
  lv_area_t a;
  lv_obj_get_coords(b, &a);
  const SpritePack& p = thumbs[i];
  int S = (p.w * 2 <= 64 && p.h * 2 <= 54) ? 2 : 1;
  Ctx c{lv_event_get_layer(e), a.x1, a.y1};
  drawPack(c, p, 0, (lv_area_get_width(&a) - p.w * S) / 2, (lv_area_get_height(&a) - p.h * S) / 2, S);
}

static void pickerBtnCb(lv_event_t* e) {
  setSkin((int)(intptr_t)lv_event_get_user_data(e));
  closePicker();
}

static void setToggleText(lv_obj_t* btn, bool isLed) {
  lv_obj_t* l = lv_obj_get_child(btn, 0);
  if (isLed)
    lv_label_set_text(l, ambient_led_enabled() ? "LED: ligado" : "LED: desligado");
  else
    lv_label_set_text(l, ambient_auto_brightness() ? "Modo noite: sim" : "Modo noite: não");
}

static void toggleCb(lv_event_t* e) {
  bool isLed = (intptr_t)lv_event_get_user_data(e) == 1;
  if (isLed)
    ambient_set_led(!ambient_led_enabled());
  else
    ambient_set_auto_brightness(!ambient_auto_brightness());
  setToggleText((lv_obj_t*)lv_event_get_target(e), isLed);
}

static lv_obj_t* makeToggle(lv_obj_t* parent, bool isLed, int x) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 150, 34);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -6);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x37474F), 0);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &font_pt_14, 0);
  lv_obj_center(l);
  setToggleText(b, isLed);
  lv_obj_add_event_cb(b, toggleCb, LV_EVENT_CLICKED, (void*)(intptr_t)(isLed ? 1 : 0));
  return b;
}

static void openPicker() {
  closePicker();
  picker = lv_obj_create(lv_layer_top());
  lv_obj_set_size(picker, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(picker, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(picker, LV_OPA_80, 0);
  lv_obj_set_style_border_width(picker, 0, 0);
  lv_obj_set_style_radius(picker, 0, 0);
  lv_obj_add_event_cb(picker, [](lv_event_t*) { closePicker(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* l = lv_label_create(picker);
  lv_label_set_text(l, "Escolha o pet");
  lv_obj_set_style_text_font(l, &font_pt_20, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 6);

  // grade de 4 colunas; passando de 2 linhas, rola na vertical
  lv_obj_t* grid = lv_obj_create(picker);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, 308, 136);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, 4, 0);
  lv_obj_set_style_pad_row(grid, 6, 0);
  lv_obj_set_style_pad_left(grid, 2, 0);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* selected = nullptr;
  for (int i = 0; i < totalSkins(); i++) {
    lv_obj_t* b = lv_button_create(grid);
    lv_obj_set_size(b, 72, 62);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(i == skin ? 0x37474F : 0x263238), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xFFD166), 0);
    lv_obj_set_style_border_width(b, i == skin ? 3 : 1, 0);
    lv_obj_add_event_cb(b, pickerBtnCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    if (i == skin) selected = b;
    sprites_load(sprNames[i].c_str(), thumbs[i], 1);  // miniatura: so o quadro normal (~1 KB, nao o pet todo)
    lv_obj_add_event_cb(b, thumbDraw, LV_EVENT_DRAW_MAIN_END, (void*)(intptr_t)i);
  }
  if (selected) {
    lv_obj_update_layout(grid);
    lv_obj_scroll_to_view(selected, LV_ANIM_OFF);
  }
  makeToggle(picker, true, -78);
  makeToggle(picker, false, 78);
}

static void petEventCb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_GESTURE) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
      setSkin(skin + (dir == LV_DIR_LEFT ? 1 : -1));
      lv_indev_wait_release(lv_indev_active());
    }
  } else if (code == LV_EVENT_LONG_PRESSED) {
    openPicker();
    lv_indev_wait_release(lv_indev_active());
  } else if (code == LV_EVENT_SHORT_CLICKED) {
    petUntil = lv_tick_get() + 1800;
  } else if (code == LV_EVENT_DELETE) {
    if (animTimer) lv_timer_delete(animTimer);
    animTimer = nullptr;
    petObj = nullptr;
    closePicker();
  }
}

// ------------------------------------------------ carrossel (pet dormindo)
// Depois de CAR_START_MS dormindo, alterna os cartoes que o backend mandou em "cards".
// Toque volta ao pet por CAR_TOUCH_PAUSE_MS. Modo noite: so o relogio.

static constexpr uint32_t CAR_START_MS = 30000, CAR_CARD_MS = 10000, CAR_TOUCH_PAUSE_MS = 60000;
static JsonDocument cardsDoc;
static lv_obj_t *carousel = nullptr, *clockTime = nullptr, *clockDate = nullptr;
static lv_timer_t* carTimer = nullptr;
static int carIndex = -1;
static uint32_t carSwitchAt = 0, carSuppressUntil = 0;
static String wxIcon;
static bool wxNight = false;

static const char* const WEEKDAY_FULL[] = {"domingo", "segunda-feira", "terça-feira", "quarta-feira",
                                           "quinta-feira", "sexta-feira", "sábado"};
static const char* const MONTHS[] = {"janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho",
                                     "agosto", "setembro", "outubro", "novembro", "dezembro"};

static void cloudShape(Ctx& c, int x, int y, uint32_t col) {
  circle(c, x - 18, y + 4, 14, col);
  circle(c, x + 2, y - 6, 20, col);
  circle(c, x + 22, y + 4, 14, col);
  rrect(c, x - 32, y + 2, 68, 18, 9, col);
}

static void wxIconDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  Ctx c{lv_event_get_layer(e), a.x1, a.y1};
  int x = 48, y = 44;
  uint32_t t = lv_tick_get();
  const String& ic = wxIcon;
  bool sunny = ic == "sun" || ic == "partly";
  if (sunny) {
    int sx = ic == "partly" ? x - 14 : x, sy = ic == "partly" ? y - 12 : y;
    if (wxNight) {
      circle(c, sx, sy, 22, 0xECEFF1);
      circle(c, sx + 10, sy - 8, 20, 0x000000);  // lua crescente
    } else {
      float rot = (t % 12000) * 2 * (float)M_PI / 12000;
      for (int i = 0; i < 8; i++) {
        float ang = rot + i * (float)M_PI / 4;
        line(c, sx + (int)(cosf(ang) * 30), sy + (int)(sinf(ang) * 30), sx + (int)(cosf(ang) * 40),
             sy + (int)(sinf(ang) * 40), 4, 0xFFC107);
      }
      circle(c, sx, sy, 22, 0xFFC107);
    }
  }
  if (ic == "sun") return;
  uint32_t cloud = ic == "storm" ? 0x78909C : 0xCFD8DC;
  cloudShape(c, x + (ic == "partly" ? 8 : 0), y + (ic == "partly" ? 8 : -6), cloud);
  if (ic == "rain" || ic == "storm") {
    for (int i = 0; i < 3; i++) {
      int dy = (int)((t / 8 + i * 37) % 22);
      line(c, x - 18 + i * 18, y + 22 + dy, x - 22 + i * 18, y + 32 + dy, 3, 0x4FC3F7);
    }
  }
  if (ic == "storm") tri(c, x + 2, y + 18, x - 10, y + 42, x + 8, y + 40, 0xFFEB3B);
  if (ic == "snow")
    for (int i = 0; i < 3; i++) circle(c, x - 18 + i * 18, y + 30 + (int)((t / 20 + i * 9) % 14), 3, 0xFFFFFF);
  if (ic == "fog")
    for (int i = 0; i < 3; i++) rrect(c, x - 34 + i * 4, y + 24 + i * 9, 64, 4, 2, 0xB0BEC5);
}

static lv_obj_t* carLabel(lv_obj_t* parent, const char* txt, const lv_font_t* f, uint32_t col) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, txt ? txt : "");
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  return l;
}

static void updateClock() {
  if (!clockTime) return;
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 120) {
    lv_label_set_text(clockTime, "--:--");
    return;
  }
  lv_label_set_text_fmt(clockTime, "%02d:%02d", t.tm_hour, t.tm_min);
  lv_label_set_text_fmt(clockDate, "%s, %d de %s", WEEKDAY_FULL[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
}

static void buildCard(JsonObjectConst card) {
  lv_obj_clean(carousel);
  clockTime = clockDate = nullptr;
  const char* type = card["type"] | "";
  bool night = ambient_is_night();
  uint32_t fg = night ? 0x607D8B : 0xFFFFFF, dim = night ? 0x455A64 : 0x9E9E9E;

  if (!strcmp(type, "clock")) {
    clockTime = carLabel(carousel, "", &font_pt_48, fg);
    lv_obj_align(clockTime, LV_ALIGN_CENTER, 0, -22);
    clockDate = carLabel(carousel, "", &font_pt_20, dim);
    lv_obj_align(clockDate, LV_ALIGN_CENTER, 0, 30);
    updateClock();

  } else if (!strcmp(type, "weather")) {
    wxIcon = card["icon"] | "cloud";
    wxNight = card["night"] | false;
    carLabel(carousel, card["city"] | "", &font_pt_14, dim);
    lv_obj_t* icon = lv_obj_create(carousel);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 96, 96);
    lv_obj_set_pos(icon, 0, 20);
    lv_obj_add_event_cb(icon, wxIconDraw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_t* temp = carLabel(carousel, card["temp"] | "--", &font_pt_48, fg);
    lv_obj_set_pos(temp, 112, 22);
    lv_obj_t* desc = carLabel(carousel, card["desc"] | "", &font_pt_20, fg);
    lv_obj_set_pos(desc, 112, 82);
    lv_obj_set_width(desc, 190);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_DOT);
    int y = 124;
    for (JsonVariantConst l : card["lines"].as<JsonArrayConst>()) {
      lv_obj_t* lb = carLabel(carousel, l.as<const char*>(), &font_pt_14, dim);
      lv_obj_set_pos(lb, 4, y);
      y += 22;
    }

  } else if (!strcmp(type, "today")) {
    carLabel(carousel, card["title"] | "Hoje", &font_pt_20, dim);
    int i = 0;
    for (JsonObjectConst it : card["items"].as<JsonArrayConst>()) {
      int x = (i % 2) * 152, y = 30 + (i / 2) * 84;
      lv_obj_t* v = carLabel(carousel, it["v"] | "", &font_pt_48, fg);
      lv_obj_set_pos(v, x + 4, y);
      lv_obj_t* l = carLabel(carousel, it["l"] | "", &font_pt_14, dim);
      lv_obj_set_pos(l, x + 6, y + 58);
      if (++i >= 4) break;
    }

  } else if (!strcmp(type, "week")) {
    carLabel(carousel, card["title"] | "", &font_pt_14, dim);
    JsonArrayConst days = card["days"].as<JsonArrayConst>();
    int n = days.size(), maxv = 1;
    for (JsonObjectConst d : days) maxv = max(maxv, (int)(d["v"] | 0));
    const int base = 176, maxh = 118, bw = 30, step = 304 / max(n, 1);
    int i = 0;
    for (JsonObjectConst d : days) {
      int v = d["v"] | 0, h = v ? max(4, v * maxh / maxv) : 2;
      int x = i * step + (step - bw) / 2;
      bool today = i == n - 1;
      lv_obj_t* bar = lv_obj_create(carousel);
      lv_obj_remove_style_all(bar);
      lv_obj_set_style_bg_color(bar, lv_color_hex(today ? 0x26A69A : 0x546E7A), 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(bar, 4, 0);
      lv_obj_set_pos(bar, x, base - h);
      lv_obj_set_size(bar, bw, h);
      const char* txt = d["t"] | "";
      if (*txt) {
        lv_obj_t* tl = carLabel(carousel, txt, &font_pt_14, fg);
        lv_obj_align_to(tl, bar, LV_ALIGN_OUT_TOP_MID, 0, -2);
      }
      lv_obj_t* dl = carLabel(carousel, d["d"] | "", &font_pt_14, today ? fg : dim);
      lv_obj_set_pos(dl, i * step, base + 4);  // largura da coluna inteira ("dom", "sáb")
      lv_obj_set_width(dl, step);
      lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_CENTER, 0);
      i++;
    }
  }
}

static void carouselHide() {
  if (carousel) lv_obj_delete(carousel);  // DELETE zera os ponteiros
}

static void carouselTouched(lv_event_t*) {
  carSuppressUntil = lv_tick_get() + CAR_TOUCH_PAUSE_MS;
  carouselHide();
}

static void carouselShow(int index) {
  JsonArrayConst cards = cardsDoc.as<JsonArrayConst>();
  if (!carousel) {
    carousel = lv_obj_create(lv_obj_get_parent(petObj));  // ultimo filho: fica por cima
    lv_obj_remove_style_all(carousel);
    lv_obj_set_size(carousel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(carousel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(carousel, LV_OPA_COVER, 0);
    lv_obj_add_flag(carousel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(carousel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(carousel, carouselTouched, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(carousel, [](lv_event_t*) {
      carousel = clockTime = clockDate = nullptr;
      carIndex = -1;
    }, LV_EVENT_DELETE, nullptr);
  }
  carIndex = index;
  carSwitchAt = lv_tick_get();
  buildCard(cards[index]);
}

static void carouselTick(lv_timer_t*) {
  if (!petObj) {  // tela do pet foi desmontada: o timer se remove
    lv_timer_delete(carTimer);
    carTimer = nullptr;
    return;
  }
  uint32_t now = lv_tick_get();
  JsonArrayConst cards = cardsDoc.as<JsonArrayConst>();
  bool want = mood == M_SLEEP && now - moodSince > CAR_START_MS && now > carSuppressUntil && !picker &&
              cards.size() > 0;
  if (!want) {
    carouselHide();
    return;
  }
  int clockIdx = 0;
  for (size_t i = 0; i < cards.size(); i++)
    if (!strcmp(cards[i]["type"] | "", "clock")) clockIdx = i;
  if (ambient_is_night()) {  // a noite so o relogio
    if (carIndex != clockIdx) carouselShow(clockIdx);
  } else if (carIndex < 0) {
    carouselShow(0);
  } else if (now - carSwitchAt >= CAR_CARD_MS) {
    carouselShow((carIndex + 1) % cards.size());
  }
  updateClock();
}

// -------------------------------------------------------------- API

bool pet_is_built() { return petObj != nullptr; }
int pet_mood() { return petObj ? (int)mood : -1; }  // ordem de Mood == PetMoodCode

// Teste local (serial "mood working"): vale ate o backend mandar outro estado.
bool pet_serial_line(const String& line) {
  // testes: "skin N" | "picker" | "cards" (inicia o carrossel ja) | "card N" (mostra o cartao N)
  if (line.startsWith("skin ")) {
    setSkin(line.substring(5).toInt());
    return true;
  }
  if (line == "pets") {
    for (int i = 0; i < totalSkins(); i++) Serial.printf("PET %d %s%s\n", i, skinName(i), i == skin ? " *" : "");
    return true;
  }
  if (line == "picker") {  // abre/fecha (capturas de tela)
    if (picker) closePicker();
    else if (petObj) openPicker();
    return true;
  }
  if (line == "cards" || line.startsWith("card ")) {
    if (!petObj || cardsDoc.as<JsonArrayConst>().size() == 0) {
      Serial.println("PET sem pet na tela ou sem cartoes (o backend so manda com o pet dormindo)");
      return true;
    }
    closePicker();
    mood = M_SLEEP;
    moodSince = lv_tick_get() - CAR_START_MS - 1;
    carSuppressUntil = 0;
    if (line.startsWith("card ")) {
      int n = line.substring(5).toInt() % cardsDoc.as<JsonArrayConst>().size();
      carouselShow(n);
    }
    Serial.printf("PET carrossel: %u cartoes\n", (unsigned)cardsDoc.as<JsonArrayConst>().size());
    return true;
  }
  if (!line.startsWith("mood ")) return false;
  static const char* const names[] = {"sleeping", "idle", "working", "attention", "done", "error"};
  String n = line.substring(5);
  n.trim();
  for (int i = 0; i < 6; i++) {
    if (n == names[i] && petObj) {
      mood = (Mood)i;
      moodSince = lv_tick_get();
      Serial.printf("PET humor forcado: %s\n", names[i]);
      return true;
    }
  }
  Serial.println("PET uso: mood sleeping|idle|working|attention|done|error (com o pet na tela)");
  return true;
}
uint32_t pet_mood_since() { return moodSince; }

void pet_build(lv_obj_t* parent) {
  Preferences p;
  p.begin("cyd", true);
  // "pet" = "spr:<nome>"; versoes antigas: "sprn". Sem escolha (ou era um desenhado que saiu): o gato.
  String saved = p.isKey("pet") ? p.getString("pet", "") : "spr:" + p.getString("sprn", "");
  p.end();
  wantSprite = saved.startsWith("spr:") && saved.length() > 4 ? saved.substring(4) : String("gato");
  syncSprites();  // se o pet salvo e de pixel art e ja esta na placa, volta a ele

  petObj = lv_obj_create(parent);
  lv_obj_remove_style_all(petObj);
  lv_obj_set_size(petObj, PET_W, PET_H);
  lv_obj_align(petObj, LV_ALIGN_LEFT_MID, -4, 0);
  lv_obj_add_flag(petObj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(petObj, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(petObj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(petObj, drawCb, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(petObj, petEventCb, LV_EVENT_ALL, nullptr);

  bubbleBox = lv_obj_create(parent);
  lv_obj_set_size(bubbleBox, SIDE_W, LV_SIZE_CONTENT);
  lv_obj_align(bubbleBox, LV_ALIGN_TOP_RIGHT, 0, 16);
  lv_obj_set_style_radius(bubbleBox, 12, 0);
  lv_obj_set_style_pad_all(bubbleBox, 8, 0);
  lv_obj_set_style_border_width(bubbleBox, 0, 0);
  lv_obj_remove_flag(bubbleBox, LV_OBJ_FLAG_SCROLLABLE);
  bubbleLbl = lv_label_create(bubbleBox);
  lv_obj_set_width(bubbleLbl, LV_PCT(100));
  lv_label_set_long_mode(bubbleLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(bubbleLbl, &font_pt_14, 0);
  lv_obj_set_style_text_color(bubbleLbl, lv_color_hex(0x111111), 0);

  // uso do Claude (linhas prontas do backend), logo abaixo do balao
  usageLbl = lv_label_create(parent);
  lv_obj_set_width(usageLbl, SIDE_W);
  lv_label_set_long_mode(usageLbl, LV_LABEL_LONG_WRAP);
  lv_label_set_text(usageLbl, "");
  lv_obj_set_style_text_font(usageLbl, &font_pt_14, 0);
  lv_obj_set_style_text_color(usageLbl, lv_color_hex(0xCFD8DC), 0);
  lv_obj_set_style_text_line_space(usageLbl, 2, 0);

  subLbl = lv_label_create(parent);
  lv_obj_set_width(subLbl, SIDE_W);
  lv_label_set_long_mode(subLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(subLbl, &font_pt_14, 0);
  lv_obj_set_style_text_color(subLbl, lv_color_hex(0x9E9E9E), 0);
  lv_obj_align(subLbl, LV_ALIGN_BOTTOM_RIGHT, 0, -4);

  animTimer = lv_timer_create(animCb, 45, nullptr);
  carTimer = lv_timer_create(carouselTick, 500, nullptr);
}

static Mood parseMood(const char* s) {
  if (!s) return M_IDLE;
  if (!strcmp(s, "sleeping")) return M_SLEEP;
  if (!strcmp(s, "working")) return M_WORK;
  if (!strcmp(s, "attention")) return M_ATTN;
  if (!strcmp(s, "done")) return M_DONE;
  if (!strcmp(s, "error")) return M_ERROR;
  return M_IDLE;
}

void pet_update(JsonObjectConst d) {
  Mood m = parseMood(d["mood"]);
  if (m != mood) {
    mood = m;
    moodSince = lv_tick_get();
    if (m != M_SLEEP) carouselHide();  // acordou: volta ao pet na hora
  }
  if (!d["cards"].isNull()) {
    cardsDoc.clear();
    cardsDoc.set(d["cards"]);
  }
  title = d["title"] | "";
  updateTitle();

  const char* bubble = d["bubble"] | "";
  if (strcmp(lv_label_get_text(bubbleLbl), bubble)) lv_label_set_text(bubbleLbl, bubble);
  uint32_t bg = m == M_ATTN ? 0xFFCA28 : m == M_ERROR ? 0xEF9A9A : m == M_DONE ? 0xC8E6C9 : 0xFFFFFF;
  lv_obj_set_style_bg_color(bubbleBox, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(bubbleBox, *bubble ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

  const char* sub = d["sub"] | "";
  if (strcmp(lv_label_get_text(subLbl), sub)) lv_label_set_text(subLbl, sub);

  String usage;
  for (JsonVariantConst l : d["usage"].as<JsonArrayConst>()) {
    if (usage.length()) usage += "\n";
    usage += l.as<const char*>();
  }
  if (strcmp(lv_label_get_text(usageLbl), usage.c_str())) lv_label_set_text(usageLbl, usage.c_str());
  // reposiciona sob o balao (a altura do balao muda com o texto)
  lv_obj_update_layout(bubbleBox);
  lv_obj_align_to(usageLbl, bubbleBox, LV_ALIGN_OUT_BOTTOM_LEFT, 2, *bubble ? 8 : 0);
}

// Backend fora do ar: dorme e avisa no balao (sem uso velho). Volta sozinho na proxima tela do backend.
void pet_offline(const char* msg) {
  if (!petObj) return;
  JsonDocument d;
  d["mood"] = "sleeping";
  d["title"] = title;
  d["bubble"] = msg;
  d["sub"] = "";
  d["usage"].to<JsonArray>();
  pet_update(d.as<JsonObjectConst>());
}
