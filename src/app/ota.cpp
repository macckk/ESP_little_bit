// ArduinoOTA (desenvolvimento): gravar pela rede com `pio run -e cyd2usb_ota -t upload`.
// Senha vem de secrets.ini (OTA_PASSWORD); vazia = OTA desligado.
// OTA via HTTPS puxando do backend fica para quando o backend existir.
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "app.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

static bool started = false, active = false;

bool ota_active() { return active; }

static void begin() {
  started = true;
  if (strlen(OTA_PASSWORD) < 8) {
    Serial.println("OTA desligado: defina ota_password (8+ caracteres) em secrets.ini");
    return;
  }
  ArduinoOTA.setHostname(gDeviceId.c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] {
    active = true;
    backend_pause(true);
    Serial.println("OTA inicio");
    ui_overlay("Atualizando firmware", "não desligue", 0);
    lv_refr_now(nullptr);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static int last = -1;
    int pct = total ? done * 100 / total : 0;
    if (pct / 2 != last / 2) {
      last = pct;
      ui_overlay("Atualizando firmware", "não desligue", pct);
      lv_refr_now(nullptr);
    }
  });
  ArduinoOTA.onEnd([] {
    Serial.println("OTA fim, reiniciando");
    ui_overlay("Atualizado", "reiniciando...", 100);
    lv_refr_now(nullptr);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("OTA erro %u\n", e);
    active = false;
    backend_pause(false);
    ui_overlay("", "", -1);
  });
  ArduinoOTA.begin();
  Serial.printf("OTA pronto: %s.local / %s\n", gDeviceId.c_str(), WiFi.localIP().toString().c_str());
}

void ota_loop() {
  if (!WiFi.isConnected() || netcfg_portal_active()) return;
  if (!started) begin();
  ArduinoOTA.handle();
}
