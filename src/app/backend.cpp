// Cliente do backend: GET {url}/device/{id}/screen numa task FreeRTOS (core 0),
// para que a rede nunca trave a interface (LVGL roda no loop, core 1).
#include <HTTPClient.h>
#include <WiFi.h>

#include "app.h"

static constexpr size_t MAX_BODY = 8 * 1024;
static constexpr int LONGPOLL_S = 25;

static SemaphoreHandle_t mtx;
static TaskHandle_t task;
static String pending;
static bool hasPending = false;
static volatile bool online = false, paused = false;
static volatile time_t lastOk = 0;
static volatile uint32_t refreshS = 30;
static String etag;
static char lastErr[64] = "";

static void setErr(const char* e) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  strlcpy(lastErr, e, sizeof lastErr);
  xSemaphoreGive(mtx);
}

String backend_last_error() {
  if (!mtx) return String();
  xSemaphoreTake(mtx, portMAX_DELAY);
  String s = lastErr;
  xSemaphoreGive(mtx);
  return s;
}

bool backend_online() { return online; }
time_t backend_last_ok() { return lastOk; }
void backend_pause(bool p) { paused = p; }
uint32_t backend_stack_free() { return task ? uxTaskGetStackHighWaterMark(task) : 0; }

void backend_set_refresh(uint32_t s) { refreshS = s; }

bool backend_take_new(String& body) {
  if (!hasPending) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  body = pending;
  pending = String();
  hasPending = false;
  xSemaphoreGive(mtx);
  return true;
}

// Uma consulta. Retorna segundos ate a proxima.
static uint32_t fetchOnce(uint32_t& failDelay) {
  if (gNet.url.startsWith("https://")) {
    setErr("https ainda nao suportado");
    Serial.println("BACKEND https ainda nao suportado (falta CA); use http por enquanto");
    online = false;
    return 60;
  }
  HTTPClient http;
  // long-poll: o servidor segura ate LONGPOLL_S esperando a tela mudar (responde 304 se nada mudou).
  // Servidores sem suporte ignoram o parametro e respondem na hora.
  String url = gNet.url + "/device/" + gDeviceId + "/screen?wait=" + String(LONGPOLL_S);
  http.setConnectTimeout(5000);
  http.setTimeout((LONGPOLL_S + 10) * 1000);
  if (!http.begin(url)) {
    online = false;
    return 60;
  }
  static const char* hdrs[] = {"ETag"};
  http.collectHeaders(hdrs, 1);
  if (gNet.token.length()) http.addHeader("Authorization", "Bearer " + gNet.token);
  if (online && etag.length()) http.addHeader("If-None-Match", etag);

  uint32_t t0 = millis();
  int code = http.GET();
  uint32_t ms = millis() - t0;
  bool ok = false;
  if (code == 200) {
    int len = http.getSize();
    if (len > (int)MAX_BODY) {
      Serial.printf("BACKEND resposta grande demais (%d B)\n", len);
    } else {
      String body = http.getString();
      if (body.length() <= MAX_BODY) {
        etag = http.header("ETag");
        xSemaphoreTake(mtx, portMAX_DELAY);
        pending = body;
        hasPending = true;
        xSemaphoreGive(mtx);
        ok = true;
        Serial.printf("BACKEND 200 %u B em %lu ms\n", body.length(), ms);
        http.end();
        sprites_sync(body);  // baixa pets novos (outra conexao; poucos KB)
      }
    }
  } else if (code == 304) {
    ok = true;
  } else {
    String e = code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code);
    if (code == 401 || code == 403) e += " (token recusado)";
    setErr(e.c_str());
    Serial.printf("BACKEND falhou: %s (%lu ms)\n", e.c_str(), ms);
  }
  if (ok) setErr("");
  http.end();

  online = ok;
  if (ok) {
    lastOk = time(nullptr);
    failDelay = 5;
    return refreshS;
  }
  uint32_t d = failDelay;
  failDelay = min<uint32_t>(failDelay * 2, 60);
  return d;
}

static void taskFn(void*) {
  uint32_t failDelay = 5;
  for (;;) {
    uint32_t wait = 2;
    bool usesRefresh = false;
    if (!paused && gNet.url.length()) {
      if (WiFi.isConnected()) {
        wait = fetchOnce(failDelay);
        usesRefresh = online;
      } else {
        online = false;
      }
    }
    // espera em fatias de 1 s: se o loop ajustar o refresh (tela nova), vale na hora
    uint32_t start = millis();
    for (;;) {
      uint32_t target = (usesRefresh ? refreshS : wait) * 1000UL;
      if (millis() - start >= target) break;
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000))) break;
    }
  }
}

// ---------------------------------------------------------------- acoes de toque
// POST {url}/device/{id}/action {"action","value"} numa task propria: a task do long-poll pode ficar ate 25 s
// presa esperando a tela mudar, e o toque precisa sair na hora. A resposta (tela nova) chega pelo long-poll.

struct Action {
  char action[33];
  char value[65];
};
static QueueHandle_t actionQ;

static void actionTaskFn(void*) {
  Action a;
  for (;;) {
    if (xQueueReceive(actionQ, &a, portMAX_DELAY) != pdTRUE) continue;
    if (!WiFi.isConnected() || gNet.url.isEmpty() || gNet.url.startsWith("https://")) continue;
    JsonDocument doc;
    doc["action"] = a.action;
    doc["value"] = a.value;
    String body;
    serializeJson(doc, body);
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(8000);
    if (!http.begin(gNet.url + "/device/" + gDeviceId + "/action")) continue;
    http.addHeader("Content-Type", "application/json");
    if (gNet.token.length()) http.addHeader("Authorization", "Bearer " + gNet.token);
    uint32_t t0 = millis();
    int code = http.POST(body);
    http.end();
    Serial.printf("ACAO %s %s -> %d (%lu ms)\n", a.action, a.value, code, millis() - t0);
    if (task) xTaskNotifyGive(task);  // busca a tela de novo ja (sem esperar o "refresh" da tela atual)
  }
}

void backend_send_action(const char* action, const char* value) {
  if (!actionQ) return;
  Action a{};
  strlcpy(a.action, action ? action : "", sizeof a.action);
  strlcpy(a.value, value ? value : "", sizeof a.value);
  if (xQueueSend(actionQ, &a, 0) != pdTRUE) Serial.println("ACAO descartada (fila cheia)");
}

void backend_begin() {
  mtx = xSemaphoreCreateMutex();
  actionQ = xQueueCreate(4, sizeof(Action));
  xTaskCreatePinnedToCore(taskFn, "backend", 8192, nullptr, 1, &task, 0);
  xTaskCreatePinnedToCore(actionTaskFn, "acao", 6144, nullptr, 1, nullptr, 0);
}
