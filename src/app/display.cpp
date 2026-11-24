// LovyanGFX como driver de tela da LVGL + XPT2046 (bit-bang) como entrada.
#include <Preferences.h>

#include "app.h"
#include "board_cyd.h"
#include "touch_cal.h"
#include "xpt2046_bb.h"

LGFX tft;
static XPT2046_BB touch(PIN_TOUCH_CLK, PIN_TOUCH_MOSI, PIN_TOUCH_MISO, PIN_TOUCH_CS, PIN_TOUCH_IRQ);
static TouchCal gCal;

static constexpr int SCREEN_W = 320;
static constexpr int BUF_LINES = 20;
static constexpr size_t BUF_BYTES = SCREEN_W * BUF_LINES * 2;

static bool gShot = false;

// Captura de tela pelo serial: durante um redesenho completo, cada area enviada a tela
// tambem sai em hex ("SHOT x y w h" + linhas). tools/screenshot.py monta o PNG.
static void shotArea(const lv_area_t* a, const uint8_t* px, uint32_t w, uint32_t h) {
  Serial.printf("SHOT %d %d %u %u\n", (int)a->x1, (int)a->y1, w, h);
  static const char hex[] = "0123456789abcdef";
  char line[2 * 2 * 320 + 2];
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t* row = px + y * w * 2;
    for (uint32_t i = 0; i < w * 2; i++) {
      line[2 * i] = hex[row[i] >> 4];
      line[2 * i + 1] = hex[row[i] & 15];
    }
    line[4 * w] = '\n';
    Serial.write((const uint8_t*)line, 4 * w + 1);
  }
}

void display_screenshot() {
  Serial.printf("SHOT_BEGIN %d %d\n", (int)tft.width(), (int)tft.height());
  gShot = true;
  lv_obj_invalidate(lv_screen_active());
  lv_obj_invalidate(lv_layer_top());
  lv_refr_now(nullptr);
  gShot = false;
  Serial.println("SHOT_END");
}

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
  if (gShot) shotArea(area, px, w, h);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels((lgfx::rgb565_t*)px, w * h);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

static void touch_cb(lv_indev_t*, lv_indev_data_t* data) {
  TouchRaw r;
  if (touch.readRaw(r)) {
    int16_t x, y;
    touchCalMap(gCal, r.x, r.y, x, y);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static uint32_t tick_cb() { return millis(); }

static void log_cb(lv_log_level_t, const char* buf) { Serial.print(buf); }

void display_init() {
  // Configuracao confirmada no teste de hardware (ver HARDWARE.md), lida da NVS "cyd"
  Preferences p;
  p.begin("cyd", true);
  uint8_t rot = p.getUChar("rot", 1) & 3;
  bool inv = p.getBool("inv", false);
  bool rgb = p.getBool("rgb", false);

  tft.init();
  tft.setRgbOrder(rgb);
  tft.setRotation(rot);
  tft.invertDisplay(inv);
  tft.setBrightness(200);
  tft.fillScreen(TFT_BLACK);

  gCal = TouchCal{};  // padrao = calibracao medida em rot=1
  gCal.w = tft.width();
  gCal.h = tft.height();
  char key[8];
  snprintf(key, sizeof key, "cal%u", rot);
  if (p.isKey(key) && p.getBytesLength(key) == sizeof(TouchCal)) {
    TouchCal c;
    p.getBytes(key, &c, sizeof c);
    if (c.valid && c.w == gCal.w && c.h == gCal.h) gCal = c;
  }
  p.end();
  touch.begin();

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_log_register_print_cb(log_cb);

  lv_display_t* disp = lv_display_create(tft.width(), tft.height());
  lv_display_set_flush_cb(disp, flush_cb);
  // buffers no heap (a regiao estatica .bss do ESP32 e limitada)
  void* buf1 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, buf1, buf2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);

  lv_theme_t* th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_TEAL),
                                         lv_palette_main(LV_PALETTE_AMBER), true, &font_pt_14);
  lv_display_set_theme(disp, th);
}
