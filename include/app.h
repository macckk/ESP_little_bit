#pragma once
// Interfaces entre os modulos do app (src/app/).
#include <Arduino.h>
#include <lvgl.h>

#include "lgfx_cyd.h"

// ---- display.cpp: LovyanGFX + LVGL + toque
extern LGFX tft;
void display_init();
void display_screenshot();  // serial "shot": envia a tela em hex (tools/screenshot.py)

// ---- netcfg.cpp: identidade, portal Wi-Fi, NVS "net", reset por BOOT
struct NetConfig {
  String url;    // ex. http://192.168.1.10:8000 (sem / no fim)
  String token;  // Bearer do dispositivo
};
extern NetConfig gNet;
extern String gDeviceId;  // cyd-xxxx (fim do MAC)
void netcfg_begin();
void netcfg_loop();
bool netcfg_portal_active();

// ---- backend.cpp: GET periodico /device/{id}/screen numa task separada
void backend_begin();
bool backend_take_new(String& body);  // true se chegou JSON novo
void backend_set_refresh(uint32_t seconds);
void backend_pause(bool paused);
bool backend_online();
time_t backend_last_ok();  // epoch do ultimo sucesso (0 = nunca / sem NTP)
uint32_t backend_stack_free();
String backend_last_error();  // "" se a ultima consulta deu certo
// Toque na placa -> POST /device/{id}/action (task propria; nao bloqueia). action "app" + value = troca de app.
void backend_send_action(const char* action, const char* value = "");

// netcfg.cpp: console serial de desenvolvimento (show | url X | token X | reboot)
void netcfg_serial_line(const String& line);

// ---- ui.cpp: barra de status + layouts
void ui_init();
void ui_show_message(const char* title, const char* text, const char* icon = nullptr);
void ui_show_portal(const char* ap, const char* pass);
bool ui_render_json(const String& body, uint32_t& refresh, String& err);
void ui_tick_status();                                   // chamar 1x/s
void ui_overlay(const char* title, const char* text, int pct);  // pct<0 esconde

void ui_set_title(const char* title);
void ui_open_app_menu();  // mesmo que tocar no titulo (serial "apps")
void ui_debug();  // serial "uidbg": posicoes da barra de status

// ---- pet.cpp: layout "pet" (animado localmente, 4 modelos trocados pelo toque)
#include <ArduinoJson.h>
enum PetMoodCode { PET_SLEEPING, PET_IDLE, PET_WORKING, PET_ATTENTION, PET_DONE, PET_ERROR };
bool pet_is_built();
int pet_mood();              // PetMoodCode, ou -1 se a tela atual nao e o pet
uint32_t pet_mood_since();   // lv_tick/millis de quando o humor mudou
bool pet_serial_line(const String& line);  // "mood <humor>" para testes
void pet_build(lv_obj_t* parent);
void pet_update(JsonObjectConst d);

// ---- sprites.cpp: pets em pixel art baixados do backend (LittleFS /sprites)
enum SpriteRole : uint8_t { SPR_NORMAL, SPR_BLINK, SPR_SLEEP, SPR_ALERT, SPR_HAPPY, SPR_WORK, SPR_TAIL, SPR_BREATHE };
struct SpritePack {
  char name[13] = "";
  char label[16] = "";
  uint8_t w = 0, h = 0, nframes = 0, ncolors = 0;
  uint32_t pal[16] = {};  // 0xRRGGBB; indice 0 = transparente
  uint8_t roles[8] = {};
  uint8_t* data = nullptr;  // quadros: h linhas de (w+1)/2 bytes, 4 bpp, nibble alto a esquerda
};
void sprites_sync(const String& screenJson);  // task do backend: baixa/remove conforme "sprites"
uint32_t sprites_generation();                // muda quando a pasta muda
int sprites_names(String* out, int max);      // nomes disponiveis (ordem alfabetica)
bool sprites_load(const char* name, SpritePack& p, int maxFrames = 0);  // maxFrames>0: so os primeiros (miniatura)
void sprites_free(SpritePack& p);
int sprites_frame_for(const SpritePack& p, uint8_t role);  // -1 se o pet nao tem esse quadro

// ---- ambient.cpp: LED RGB traseiro + sensor de luz (brilho automatico, modo noite)
void ambient_begin();
void ambient_loop();
bool ambient_serial_line(const String& line);  // "ldr", "ldrcal <escuro> <claro>"
bool ambient_led_enabled();
bool ambient_is_night();  // modo noite ativo (horario e chave "Modo noite")
bool ambient_auto_brightness();
void ambient_set_led(bool on);
void ambient_set_auto_brightness(bool on);

// ---- ota.cpp
void ota_loop();
bool ota_active();
