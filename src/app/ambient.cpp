// LED RGB (na traseira: ilumina a parede) + sensor de luz (LDR) -> brilho automatico.
//
// LED por humor do pet: trabalhando = ambar respirando; precisa de voce = vermelho piscando;
// terminou = verde por alguns segundos; erro = vermelho fixo; dormindo/ocioso = apagado.
// Sala escura (modo noite): tela no minimo e LED so para "precisa de voce", fraco.
// O pet NAO dorme por causa do escuro (programar no escuro e comum).
//
// Calibracao do LDR (valores crus do ADC a 0 dB) via serial: "ldrcal <escuro> <claro>".
// Sem calibracao (o LDR desta placa nao responde), o modo noite segue o horario: "noite 22 7".
#include <Preferences.h>
#include <math.h>

#include "app.h"
#include "board_cyd.h"

static constexpr uint8_t CH_R = 2, CH_G = 3, CH_B = 4;  // canal 7 e o backlight (LovyanGFX)
static constexpr uint8_t BRIGHT_MIN = 30, BRIGHT_MAX = 200, BRIGHT_NIGHT = 8;
static constexpr float LED_MAX = 0.6f;  // o LED e forte; limita o maximo

static bool ledEnabled = true, autoBright = true;
static int ldrDark = -1, ldrBright = -1;
static float ldrAvg = -1, level = 1.0f;
static bool night = false;
static uint32_t darkSince = 0;
static int nightStart = 22, nightEnd = 7;  // modo noite por horario (sem LDR calibrado)
static int curBright = BRIGHT_MAX;

// --------------------------------------------------------------- LED

static void ledRaw(float r, float g, float b) {
  auto duty = [](float v) {
    v = constrain(v, 0.0f, 1.0f) * LED_MAX;
    return (uint32_t)(255 - v * v * 255);  // gama ~2 e ativo em LOW
  };
  ledcWrite(CH_R, duty(r));
  ledcWrite(CH_G, duty(g));
  ledcWrite(CH_B, duty(b));
}

static void updateLed(uint32_t now) {
  int mood = pet_mood();
  if (!ledEnabled || ota_active() || mood < 0) {
    ledRaw(0, 0, 0);
    return;
  }
  float scale = night ? 0.35f : 1.0f;
  uint32_t since = now - pet_mood_since();
  switch (mood) {
    case PET_WORKING: {
      if (night) break;
      float v = 0.25f + 0.55f * (sinf(now * 2 * (float)M_PI / 2400) + 1) / 2;
      ledRaw(v, v * 0.45f, 0);
      return;
    }
    case PET_ATTENTION: {
      bool on = (now / 250) % 2 == 0;
      ledRaw(on ? scale : 0, on ? 0.12f * scale : 0, 0);
      return;
    }
    case PET_DONE:
      if (!night && since < 6000) {
        float v = since < 4000 ? 1.0f : 1.0f - (since - 4000) / 2000.0f;
        ledRaw(0, v, 0.1f * v);
        return;
      }
      break;
    case PET_ERROR:
      if (!night) {
        ledRaw(0.7f, 0, 0);
        return;
      }
      break;
    default: break;
  }
  ledRaw(0, 0, 0);
}

// ------------------------------------------------------- luz e brilho

static bool calibrated() { return ldrDark >= 0 && ldrBright >= 0 && ldrDark != ldrBright; }

static void updateLight(uint32_t now) {
  int raw = analogRead(PIN_LDR);
  ldrAvg = ldrAvg < 0 ? raw : ldrAvg * 0.85f + raw * 0.15f;

  if (calibrated()) {
    level = constrain((ldrAvg - ldrDark) / float(ldrBright - ldrDark), 0.0f, 1.0f);
    // modo noite com histerese: entra apos 5 s escuro, sai quando clareia bem
    if (!night) {
      if (level < 0.15f) {
        if (!darkSince) darkSince = now;
        if (now - darkSince > 5000) {
          night = true;
          Serial.printf("LUZ modo noite (raw %.0f)\n", ldrAvg);
        }
      } else {
        darkSince = 0;
      }
    } else if (level > 0.30f) {
      night = false;
      darkSince = 0;
      Serial.printf("LUZ modo dia (raw %.0f)\n", ldrAvg);
    }
  } else {
    // Sem LDR utilizavel (caso desta placa: le ~0 mesmo no escuro, ver HARDWARE.md):
    // modo noite por horario via NTP.
    struct tm t;
    bool wasNight = night;
    if (getLocalTime(&t, 0) && t.tm_year > 120) {
      int h = t.tm_hour;
      night = nightStart > nightEnd ? (h >= nightStart || h < nightEnd) : (h >= nightStart && h < nightEnd);
    } else {
      night = false;
    }
    level = night ? 0.0f : 1.0f;
    if (night != wasNight) Serial.printf("LUZ modo %s (horario %02d-%02d h)\n", night ? "noite" : "dia", nightStart, nightEnd);
  }

  int target = !autoBright ? BRIGHT_MAX
             : night       ? BRIGHT_NIGHT
                           : BRIGHT_MIN + (int)(level * (BRIGHT_MAX - BRIGHT_MIN));
  if (target != curBright) {  // transicao suave
    int step = max(1, abs(target - curBright) / 6);
    curBright += target > curBright ? min(step, target - curBright) : -min(step, curBright - target);
    tft.setBrightness(curBright);
  }
}

// --------------------------------------------------------------- API

void ambient_begin() {
  Preferences p;
  p.begin("cyd", true);
  ledEnabled = p.getBool("led", true);
  autoBright = p.getBool("autob", true);
  ldrDark = p.isKey("ldrd") ? p.getInt("ldrd") : -1;
  ldrBright = p.isKey("ldrb") ? p.getInt("ldrb") : -1;
  nightStart = p.getUChar("nini", 22) % 24;
  nightEnd = p.getUChar("nfim", 7) % 24;
  p.end();

  const uint8_t pins[] = {PIN_LED_R, PIN_LED_G, PIN_LED_B};
  const uint8_t chans[] = {CH_R, CH_G, CH_B};
  for (int i = 0; i < 3; i++) {
    ledcSetup(chans[i], 5000, 8);
    ledcAttachPin(pins[i], chans[i]);
  }
  ledRaw(0, 0, 0);
  analogSetPinAttenuation(PIN_LDR, ADC_0db);  // faixa 0-1,1 V: mais sensivel
  Serial.printf("LUZ led=%d auto=%d calib=%s (escuro=%d claro=%d)\n", ledEnabled, autoBright,
                calibrated() ? "OK" : "falta", ldrDark, ldrBright);
}

void ambient_loop() {
  static uint32_t lastLight = 0, lastLed = 0;
  uint32_t now = millis();
  if (now - lastLight >= 200) {
    lastLight = now;
    updateLight(now);
  }
  if (now - lastLed >= 30) {
    lastLed = now;
    updateLed(now);
  }
}

bool ambient_led_enabled() { return ledEnabled; }
bool ambient_is_night() { return night && autoBright; }
bool ambient_auto_brightness() { return autoBright; }

static void saveBool(const char* k, bool v) {
  Preferences p;
  p.begin("cyd", false);
  p.putBool(k, v);
  p.end();
}

void ambient_set_led(bool on) {
  ledEnabled = on;
  saveBool("led", on);
}

void ambient_set_auto_brightness(bool on) {
  autoBright = on;
  saveBool("autob", on);
}

bool ambient_serial_line(const String& line) {
  if (line == "ldr") {
    // leitura nas duas atenuacoes, para diagnostico
    int r0 = analogRead(PIN_LDR);
    analogSetPinAttenuation(PIN_LDR, ADC_11db);
    delay(2);
    int r11 = analogRead(PIN_LDR);
    analogSetPinAttenuation(PIN_LDR, ADC_0db);
    Serial.printf("LDR raw0db=%d raw11db=%d media=%.0f nivel=%.2f noite=%d brilho=%d\n", r0, r11, ldrAvg,
                  level, night, curBright);
    return true;
  }
  if (line.startsWith("noite ")) {  // "noite 22 7" = modo noite das 22h as 7h
    int s, e;
    if (sscanf(line.c_str() + 6, "%d %d", &s, &e) == 2 && s >= 0 && s < 24 && e >= 0 && e < 24) {
      Preferences p;
      p.begin("cyd", false);
      p.putUChar("nini", s);
      p.putUChar("nfim", e);
      p.end();
      nightStart = s;
      nightEnd = e;
      Serial.printf("LUZ modo noite das %02d h as %02d h\n", s, e);
    } else {
      Serial.println("LUZ uso: noite <hora_inicio> <hora_fim>");
    }
    return true;
  }
  if (line.startsWith("ldrcal ")) {
    int d, b;
    if (sscanf(line.c_str() + 7, "%d %d", &d, &b) == 2 && d != b) {
      Preferences p;
      p.begin("cyd", false);
      p.putInt("ldrd", d);
      p.putInt("ldrb", b);
      p.end();
      ldrDark = d;
      ldrBright = b;
      Serial.printf("LDR calibrado: escuro=%d claro=%d\n", d, b);
    } else {
      Serial.println("LDR uso: ldrcal <escuro> <claro>");
    }
    return true;
  }
  return false;
}
