// Configuracao LVGL 9.2 para CYD (ESP32 sem PSRAM). O que nao esta aqui usa o padrao.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Memoria: heap proprio da LVGL (interno, sem PSRAM)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (48 * 1024U)

#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0

// Fontes: so as geradas com acentos PT-BR (tools/gen_lvgl_font.py)
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE \
  LV_FONT_DECLARE(font_pt_14)  \
  LV_FONT_DECLARE(font_pt_20)  \
  LV_FONT_DECLARE(font_pt_48)
#define LV_FONT_DEFAULT &font_pt_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#define LV_USE_QRCODE 1

#endif  // LV_CONF_H
