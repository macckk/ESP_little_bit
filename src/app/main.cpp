// App base do CYD: placa "burra" que desenha telas JSON vindas do backend.
// Modulos: display (LVGL), netcfg (portal/NVS/reset), backend (task HTTP), ui (layouts),
// ota (ArduinoOTA). Ver README.md e docs/GUIA.md.
//
// Ligar: "Ligando…" -> "Conectando ao Wi-Fi…" -> "Conectado, IP …" + "Buscando dados no PC…" -> tela do backend.
// A tela antiga NAO volta no boot (mostrava um estado velho, ex. o pet "trabalhando", ate o PC responder).
// PC fora do ar por mais de OFFLINE_PET_MS: o pet dorme e avisa (nao fica congelado no ultimo humor).
#include <LittleFS.h>
#include <WiFi.h>
#include <unistd.h>

#include "app.h"
#include "board_cyd.h"

static constexpr uint32_t SHOW_IP_MS = 3000;        // o IP fica na tela pelo menos isso antes da 1a tela
static constexpr uint32_t OFFLINE_PET_MS = 45000;   // backend sem responder por isso -> pet "sem conexao"

static bool haveScreen = false;    // ja desenhou alguma tela do backend
static uint32_t wifiUpAt = 0;      // quando o Wi-Fi conectou (0 = ainda nao)
static String heldBody;            // 1a tela segurada ate o IP ficar visivel por SHOW_IP_MS
static uint32_t offlineSince = 0;  // backend fora desde (0 = online)
static bool petOffline = false;

// Antes da primeira tela, a area de conteudo conta o que esta acontecendo.
static void updateBootMessage() {
  if (haveScreen || netcfg_portal_active()) return;
  static String shown;
  String key, text;
  const char* icon = LV_SYMBOL_WIFI;
  String ssid = WiFi.SSID();
  if (!WiFi.isConnected()) {
    key = "wifi";
    text = "Conectando ao Wi-Fi";
    if (ssid.length()) text += "\n" + ssid;
    text += "…";
  } else {
    String net = "Conectado" + (ssid.length() ? " à rede " + ssid : String()) + "\nIP " + WiFi.localIP().toString();
    String err = backend_last_error();
    if (gNet.url.isEmpty()) {
      key = "nourl";
      text = net + "\n\nSem endereço do PC.\nSegure BOOT 5 s para configurar.";
      icon = LV_SYMBOL_WARNING;
    } else if (err.length()) {
      key = "err:" + err;
      text = net + "\n\nO PC não respondeu (" + err + ").\nTentando de novo…";
      icon = LV_SYMBOL_WARNING;
    } else {
      key = "fetch";
      text = net + "\n\nBuscando dados no PC…";
      icon = LV_SYMBOL_OK;
    }
  }
  if (key == shown) return;
  shown = key;
  ui_show_message("Iniciando", text.c_str(), icon);
  Serial.printf("BOOT %s\n", key.c_str());
}

static void handleSerial() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (buf == "shot")
        display_screenshot();
      else if (buf == "uidbg")
        ui_debug();
      else if (buf == "apps")  // menu de apps (o mesmo do toque no titulo)
        ui_open_app_menu();
      else if (buf.startsWith("app "))  // troca de app: "app monitor"
        backend_send_action("app", buf.substring(4).c_str());
      else if (buf.startsWith("acao "))  // simula um botao: "acao inc"
        backend_send_action(buf.substring(5).c_str());
      else if (!ambient_serial_line(buf) && !pet_serial_line(buf))
        netcfg_serial_line(buf);
      buf = "";
    } else if (buf.length() < 160) {
      buf += c;
    }
  }
}

static void logHeap() {
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  Serial.printf(
      "HEAP livre=%u min=%u maior_bloco=%u | LVGL usado=%u%% (%u/%u B) frag=%u%% | "
      "pilha backend livre=%u | wifi=%s rssi=%d\n",
      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), m.used_pct,
      m.total_size - m.free_size, m.total_size, m.frag_pct, backend_stack_free(),
      WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "--", WiFi.isConnected() ? WiFi.RSSI() : 0);
}

static void render(const String& body) {
  uint32_t refresh;
  String err;
  if (ui_render_json(body, refresh, err)) {
    haveScreen = true;
    petOffline = false;
    backend_set_refresh(refresh);
  } else {
    Serial.printf("TELA rejeitada: %s\n", err.c_str());
  }
}

// PC/backend fora do ar: o pet nao pode ficar congelado "trabalhando". Depois de OFFLINE_PET_MS ele dorme e avisa;
// a tela volta sozinha quando o backend responder.
static void checkOffline(uint32_t now) {
  if (!haveScreen) return;
  if (backend_online()) {
    offlineSince = 0;
    return;
  }
  if (!offlineSince) offlineSince = now;
  if (!petOffline && now - offlineSince >= OFFLINE_PET_MS && pet_is_built()) {
    pet_offline("Sem conexão com o PC");
    petOffline = true;
    Serial.println("PET sem conexao com o PC: dormindo");
  }
}

void setup() {
  Serial.begin(115200);
  for (int p : {PIN_LED_R, PIN_LED_G, PIN_LED_B}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);  // LED RGB apagado
  }
  Serial.printf("\nCYD app | %s | heap %u\n", CYD_PANEL_NAME, ESP.getFreeHeap());

  if (!LittleFS.begin(true)) Serial.println("LittleFS indisponivel");
  ::unlink("/littlefs/last.json");  // versoes antigas guardavam a ultima tela aqui (unlink: sem log se nao existe)
  display_init();
  ui_init();
  ui_show_message("Iniciando", "Ligando…", LV_SYMBOL_POWER);
  lv_timer_handler();  // desenha ja, antes do Wi-Fi
  ambient_begin();

  netcfg_begin();
  configTzTime("<-03>3", "pool.ntp.org", "a.st1.ntp.br");  // America/Sao_Paulo (sem horario de verao)
  backend_begin();
  logHeap();
}

void loop() {
  lv_timer_handler();
  handleSerial();
  netcfg_loop();
  ambient_loop();
  ota_loop();
  if (ota_active()) return;

  uint32_t now = millis();
  if (WiFi.isConnected()) {
    if (!wifiUpAt) wifiUpAt = now ? now : 1;
  } else {
    wifiUpAt = 0;
  }

  String body;
  if (backend_take_new(body)) {
    if (!haveScreen && wifiUpAt && now - wifiUpAt < SHOW_IP_MS)
      heldBody = body;  // deixa o IP visivel um pouco antes da primeira tela
    else
      render(body);
  }
  if (heldBody.length() && wifiUpAt && now - wifiUpAt >= SHOW_IP_MS) {
    render(heldBody);
    heldBody = String();
  }

  static uint32_t lastStatus = 0, lastHeap = 0;
  if (now - lastStatus >= 1000) {
    lastStatus = now;
    ui_tick_status();
    updateBootMessage();
    checkOffline(now);
  }
  if (now - lastHeap >= 10000) {
    lastHeap = now;
    logHeap();
  }
  delay(5);
}
