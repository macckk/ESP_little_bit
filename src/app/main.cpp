// App base do CYD: placa "burra" que desenha telas JSON vindas do backend.
// Modulos: display (LVGL), netcfg (portal/NVS/reset), backend (task HTTP), ui (layouts),
// ota (ArduinoOTA). Ver README.md e docs/GUIA.md.
#include <LittleFS.h>
#include <WiFi.h>

#include "app.h"
#include "board_cyd.h"

static constexpr const char* LAST_PATH = "/last.json";
static constexpr uint32_t PERSIST_MIN_MS = 5 * 60 * 1000;  // poupa a flash

static String lastSavedBody, unsavedBody;
static uint32_t lastPersistAt = 0;
static bool fsOk = false;
static bool haveScreen = false;  // ja desenhou alguma tela do backend (ou a salva)

// Enquanto nao ha tela nenhuma, a area de conteudo mostra o que esta acontecendo.
static void updateBootMessage() {
  if (haveScreen || netcfg_portal_active()) return;
  static String shown;
  String key, text;
  const char* icon = LV_SYMBOL_WIFI;
  String err = backend_last_error();
  if (!WiFi.isConnected()) {
    key = "wifi";
    text = "Conectando ao Wi-Fi...";
  } else if (gNet.url.isEmpty()) {
    key = "nourl";
    text = "Sem URL do backend.\nSegure BOOT 5 s para reconfigurar.";
    icon = LV_SYMBOL_WARNING;
  } else if (err.length()) {
    key = "err:" + err;
    text = "Backend não respondeu:\n" + err + "\n" + gNet.url;
    icon = LV_SYMBOL_WARNING;
  } else {
    key = "fetch";
    text = "Buscando tela...\n" + gNet.url;
    icon = LV_SYMBOL_REFRESH;
  }
  if (key == shown) return;
  shown = key;
  ui_show_message("", text.c_str(), icon);
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

static void persist(const String& body, bool force) {
  if (!fsOk || body == lastSavedBody) return;
  if (!force && millis() - lastPersistAt < PERSIST_MIN_MS && lastPersistAt) {
    unsavedBody = body;  // grava depois
    return;
  }
  File f = LittleFS.open(LAST_PATH, "w");
  if (f) {
    f.print(body);
    f.close();
    lastSavedBody = body;
    unsavedBody = String();
    lastPersistAt = millis();
  }
}

static void showLastSaved() {
  if (!fsOk || !LittleFS.exists(LAST_PATH)) return;
  File f = LittleFS.open(LAST_PATH, "r");
  String body = f.readString();
  f.close();
  uint32_t refresh;
  String err;
  if (ui_render_json(body, refresh, err)) {
    lastSavedBody = body;
    haveScreen = true;
    Serial.printf("ESTADO anterior restaurado (%u B)\n", body.length());
  }
}

void setup() {
  Serial.begin(115200);
  for (int p : {PIN_LED_R, PIN_LED_G, PIN_LED_B}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);  // LED RGB apagado
  }
  Serial.printf("\nCYD app | %s | heap %u\n", CYD_PANEL_NAME, ESP.getFreeHeap());

  fsOk = LittleFS.begin(true);
  if (!fsOk) Serial.println("LittleFS indisponivel");
  display_init();
  ui_init();
  ambient_begin();

  netcfg_begin();
  if (!netcfg_portal_active()) {
    showLastSaved();  // ultimo estado aparece (com aviso offline) ate o backend responder
    updateBootMessage();
  }
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

  String body;
  if (backend_take_new(body)) {
    uint32_t refresh;
    String err;
    if (ui_render_json(body, refresh, err)) {
      haveScreen = true;
      backend_set_refresh(refresh);
      persist(body, false);
    } else {
      Serial.printf("TELA rejeitada: %s\n", err.c_str());
    }
  }
  if (unsavedBody.length() && millis() - lastPersistAt >= PERSIST_MIN_MS) persist(unsavedBody, true);

  static uint32_t lastStatus = 0, lastHeap = 0;
  uint32_t now = millis();
  if (now - lastStatus >= 1000) {
    lastStatus = now;
    ui_tick_status();
    updateBootMessage();
  }
  if (now - lastHeap >= 10000) {
    lastHeap = now;
    logHeap();
  }
  delay(5);
}
