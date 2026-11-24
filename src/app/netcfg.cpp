// Identidade do dispositivo, portal de configuracao (WiFiManager) e reset pelo BOOT.
//
// - Sem Wi-Fi salvo ou sem URL do backend -> abre AP "<id>-setup" com senha aleatoria
//   mostrada na tela (texto + QR de Wi-Fi). Portal pede SSID/senha, URL e token.
// - Credenciais do Wi-Fi: NVS do proprio ESP-IDF. URL/token: NVS namespace "net".
// - BOOT segurado 5 s: apaga Wi-Fi, "net" e /last.json, reinicia no portal.
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "app.h"
#include "board_cyd.h"

NetConfig gNet;
String gDeviceId;

static WiFiManager wm;
static WiFiManagerParameter* pUrl;
static WiFiManagerParameter* pTok;
static bool portal = false;
static uint32_t wifiLostAt = 0;

bool netcfg_portal_active() { return portal; }

static void loadNet() {
  Preferences p;
  p.begin("net", true);
  gNet.url = p.isKey("url") ? p.getString("url") : String();
  gNet.token = p.isKey("token") ? p.getString("token") : String();
  p.end();
}

static void saveParams() {
  String url = pUrl->getValue();
  String tok = pTok->getValue();
  url.trim();
  tok.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  Preferences p;
  p.begin("net", false);
  if (url.length()) p.putString("url", url);
  if (tok.length()) p.putString("token", tok);
  p.end();
  loadNet();
  Serial.printf("NET salvo: url='%s' token=%s\n", gNet.url.c_str(),
                gNet.token.length() ? "(definido)" : "(vazio)");
}

static void startPortal() {
  static char ap[32], pass[12];
  snprintf(ap, sizeof ap, "%s-setup", gDeviceId.c_str());
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (int i = 0; i < 8; i++) pass[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  pass[8] = 0;
  portal = true;
  wm.startConfigPortal(ap, pass);  // nao bloqueante
  Serial.printf("PORTAL ativo: AP '%s' (senha na tela) -> http://192.168.4.1\n", ap);
  ui_show_portal(ap, pass);
}

static void factoryReset() {
  Serial.println("RESET de configuracao: apagando Wi-Fi, URL, token e ultimo estado");
  ui_overlay("Resetando", "apagando configuração...", 100);
  lv_refr_now(nullptr);
  Preferences p;
  p.begin("net", false);
  p.clear();
  p.end();
  LittleFS.remove("/last.json");
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

static void handleBootButton() {
  static uint32_t downAt = 0;
  static bool shown = false;
  bool down = digitalRead(PIN_BOOT_BTN) == LOW;
  uint32_t now = millis();
  if (!down) {
    if (shown) ui_overlay("", "", -1);
    downAt = 0;
    shown = false;
    return;
  }
  if (!downAt) downAt = now;
  uint32_t held = now - downAt;
  if (held < 1000) return;
  if (held >= 5000) factoryReset();
  char txt[48];
  snprintf(txt, sizeof txt, "solte para cancelar (%lu s)", (5000 - held + 999) / 1000);
  ui_overlay("Resetar configuração?", txt, (held - 1000) * 100 / 4000);
  shown = true;
}

void netcfg_serial_line(const String& raw) {
  String line = raw;
  line.trim();
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String val = sp < 0 ? String() : line.substring(sp + 1);
  val.trim();
  if (cmd == "show") {
    Serial.printf("CFG id=%s url='%s' token=%s wifi_salvo=%d ssid='%s' ip=%s portal=%d\n",
                  gDeviceId.c_str(), gNet.url.c_str(), gNet.token.length() ? "(definido)" : "(vazio)",
                  wm.getWiFiIsSaved(), WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), portal);
  } else if ((cmd == "url" || cmd == "token") && val.length()) {
    if (cmd == "url")
      while (val.endsWith("/")) val.remove(val.length() - 1);
    Preferences p;
    p.begin("net", false);
    p.putString(cmd.c_str(), val);
    p.end();
    Serial.printf("CFG %s salvo; reiniciando\n", cmd.c_str());
    delay(100);
    ESP.restart();  // reinicia para a task do backend nao ler a String no meio da troca
  } else if (cmd == "reboot") {
    ESP.restart();
  } else if (line.length()) {
    Serial.println("CFG comandos: show | url <http://host:porta> | token <valor> | reboot");
  }
}

void netcfg_begin() {
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char id[16];
  snprintf(id, sizeof id, "cyd-%02x%02x", mac[4], mac[5]);
  gDeviceId = id;
  loadNet();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(gDeviceId.c_str());
  WiFi.setAutoReconnect(true);

  pUrl = new WiFiManagerParameter("url", "URL do backend (ex. http://192.168.1.10:8000)",
                                  gNet.url.c_str(), 128);
  pTok = new WiFiManagerParameter("token", "Token do dispositivo (vazio = manter o atual)", "", 96);
  wm.addParameter(pUrl);
  wm.addParameter(pTok);
  wm.setSaveParamsCallback(saveParams);
  wm.setBreakAfterConfig(true);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  wm.setHostname(gDeviceId);
  wm.setTitle(gDeviceId);
  wm.setDarkMode(true);
  wm.setShowInfoUpdate(false);  // sem pagina de upload de firmware no portal
  static std::vector<const char*> menu = {"wifi", "exit"};
  wm.setMenu(menu);

  bool configured = wm.getWiFiIsSaved() && gNet.url.length();
  Serial.printf("NET id=%s wifi_salvo=%d url='%s' token=%s\n", id, wm.getWiFiIsSaved(),
                gNet.url.c_str(), gNet.token.length() ? "(definido)" : "(vazio)");
  if (!configured) {
    startPortal();
  } else {
    WiFi.begin();  // credenciais salvas na NVS do Wi-Fi
    wifiLostAt = millis();
  }
}

void netcfg_loop() {
  handleBootButton();
  if (portal) {
    if (wm.process()) {
      // conectou com as credenciais novas: reinicia limpo no modo normal
      Serial.printf("PORTAL concluido, ip=%s. Reiniciando.\n", WiFi.localIP().toString().c_str());
      ui_show_message("Configurado", "Wi-Fi conectado. Reiniciando...", LV_SYMBOL_OK);
      lv_refr_now(nullptr);
      delay(1500);
      ESP.restart();
    }
    return;
  }
  // reconexao: o auto-reconnect cobre quedas; se ficar 30 s fora, forca novo begin
  if (WiFi.isConnected()) {
    wifiLostAt = 0;
  } else if (!wifiLostAt) {
    wifiLostAt = millis();
  } else if (millis() - wifiLostAt > 30000) {
    Serial.printf("WIFI fora ha 30 s (status %d), reconectando\n", WiFi.status());
    WiFi.disconnect();
    WiFi.begin();
    wifiLostAt = millis();
  }
}
