// Teste de hardware CYD ESP32-2432S028 (2 USB): tela, toque e microSD.
//
// Botao BOOT: toque curto = proxima tela; segurar 1,5 s = calibrar toque.
// Comandos serial (115200): h = ajuda.

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

#include "board_cyd.h"
#include "lgfx_cyd.h"
#include "touch_cal.h"
#include "xpt2046_bb.h"

static LGFX tft;
static XPT2046_BB touch(PIN_TOUCH_CLK, PIN_TOUCH_MOSI, PIN_TOUCH_MISO, PIN_TOUCH_CS, PIN_TOUCH_IRQ);
static Preferences prefs;
static SPIClass sdSPI(VSPI);

static uint8_t gRot = 1;
static bool gInvert = false;
static bool gRgbOrder = false;
static TouchCal gCal;

enum Screen : uint8_t { SCR_DISPLAY = 0, SCR_TOUCH, SCR_SD, SCR_COUNT };
static Screen gScreen = SCR_DISPLAY;

// ---------------------------------------------------------------- config

static const char* calKey(uint8_t r) {
  static char k[8];
  snprintf(k, sizeof k, "cal%u", r);
  return k;
}

static void loadCal() {
  gCal = TouchCal{};
  gCal.w = tft.width();
  gCal.h = tft.height();
  const char* k = calKey(gRot);
  if (prefs.isKey(k) && prefs.getBytesLength(k) == sizeof(TouchCal)) {
    TouchCal c;
    prefs.getBytes(k, &c, sizeof c);
    if (c.valid && c.w == gCal.w && c.h == gCal.h) gCal = c;
  }
}

static void saveSettings() {
  prefs.putUChar("rot", gRot);
  prefs.putBool("inv", gInvert);
  prefs.putBool("rgb", gRgbOrder);
}

static void applyDisplay() {
  tft.setRgbOrder(gRgbOrder);
  tft.setRotation(gRot);
  tft.invertDisplay(gInvert);
  loadCal();
}

static void printConfig() {
  Serial.printf(
      "CONFIG panel=%s rot=%u invert=%d rgb_order=%d size=%dx%d | touch cal=%s swap=%u "
      "xa=%d..%d ya=%d..%d\n",
      CYD_PANEL_NAME, gRot, gInvert, gRgbOrder, tft.width(), tft.height(),
      gCal.valid ? "OK" : "padrao", gCal.swap, gCal.xa0, gCal.xa1, gCal.ya0, gCal.ya1);
}

static void printHelp() {
  Serial.println(
      "\nComandos:\n"
      "  1/2/3  tela: cores / toque / microSD\n"
      "  n      proxima tela (= BOOT)\n"
      "  r      rotacao +1\n"
      "  i      inverte cores (invertDisplay)\n"
      "  o      alterna rgb_order (RGB<->BGR)\n"
      "  c      calibrar toque (= segurar BOOT)\n"
      "  s      repetir teste microSD\n"
      "  l      limpar tela de toque\n"
      "  z      diagnostico bruto do XPT2046\n"
      "  d      ler ID do controlador da tela\n"
      "  p      mostrar configuracao\n"
      "  x      apagar configuracao salva\n");
}

// ---------------------------------------------------------------- telas

static void footer(const char* txt) {
  tft.setFont(&fonts::Font0);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(textdatum_t::bottom_center);
  tft.drawString(txt, tft.width() / 2, tft.height() - 3);
}

static void drawDisplayTest() {
  const int W = tft.width(), H = tft.height();
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, W, H, TFT_WHITE);
  // marcadores de canto: SupEsq=vermelho SupDir=verde InfDir=azul InfEsq=amarelo
  tft.fillRect(2, 2, 10, 10, TFT_RED);
  tft.fillRect(W - 12, 2, 10, 10, TFT_GREEN);
  tft.fillRect(W - 12, H - 12, 10, 10, TFT_BLUE);
  tft.fillRect(2, H - 12, 10, 10, TFT_YELLOW);

  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(textdatum_t::top_center);
  tft.drawString("CYD teste de tela", W / 2, 6);
  char buf[64];
  snprintf(buf, sizeof buf, "%s rot=%u inv=%d rgb=%d %dx%d", CYD_PANEL_NAME, gRot, gInvert,
           gRgbOrder, W, H);
  tft.drawString(buf, W / 2, 24);

  struct Bar {
    uint16_t c;
    const char* n;
  };
  const Bar bars[] = {{TFT_RED, "R"},     {TFT_GREEN, "G"},   {TFT_BLUE, "B"},
                      {TFT_CYAN, "C"},    {TFT_MAGENTA, "M"}, {TFT_YELLOW, "Y"},
                      {TFT_WHITE, "W"}};
  const int n = sizeof(bars) / sizeof(bars[0]);
  const int y0 = 46, bh = H / 3, bw = (W - 16) / n;
  for (int i = 0; i < n; i++) {
    int x = 8 + i * bw;
    tft.fillRect(x, y0, bw - 3, bh, bars[i].c);
    tft.drawString(bars[i].n, x + (bw - 3) / 2, y0 + bh + 2);
  }
  // degrade cinza
  const int gy = y0 + bh + 22, gx0 = 8, gw = W - 16;
  for (int x = 0; x < gw; x++) {
    uint8_t v = x * 255 / (gw - 1);
    tft.drawFastVLine(gx0 + x, gy, 18, tft.color565(v, v, v));
  }
  tft.setTextDatum(textdatum_t::top_left);
  tft.drawString("preto", gx0, gy + 20);
  tft.setTextDatum(textdatum_t::top_right);
  tft.drawString("branco", gx0 + gw, gy + 20);
  footer("BOOT: prox. tela | segurar: calibrar");
}

static void drawTargets() {
  const int W = tft.width(), H = tft.height(), m = 20;
  const int pts[5][2] = {{m, m}, {W - m, m}, {W - m, H - m}, {m, H - m}, {W / 2, H / 2}};
  for (auto& p : pts) {
    tft.drawCircle(p[0], p[1], 8, TFT_DARKGREY);
    tft.drawFastHLine(p[0] - 12, p[1], 25, TFT_DARKGREY);
    tft.drawFastVLine(p[0], p[1] - 12, 25, TFT_DARKGREY);
  }
}

static void drawTouchScreen() {
  tft.fillScreen(TFT_BLACK);
  drawTargets();
  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(textdatum_t::top_center);
  tft.drawString(gCal.valid ? "Toque (calibrado)" : "Toque (SEM calibracao)", tft.width() / 2, 36);
  footer("toque/desenhe | segurar BOOT: calibrar");
}

static void showTouchInfo(const TouchRaw& r, int sx, int sy) {
  char buf[48];
  const int W = tft.width(), cy = tft.height() / 2 + 30;
  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(textdatum_t::top_center);
  tft.fillRect(30, cy, W - 60, 34, TFT_BLACK);
  snprintf(buf, sizeof buf, "raw x=%4u y=%4u z=%4u", r.x, r.y, r.z);
  tft.drawString(buf, W / 2, cy);
  snprintf(buf, sizeof buf, "tela x=%3d y=%3d", sx, sy);
  tft.drawString(buf, W / 2, cy + 17);
}

// --------------------------------------------------------- calibracao

static bool waitTouchSample(uint32_t timeoutMs, long& ax, long& ay) {
  TouchRaw r;
  uint32_t t0 = millis();
  // espera soltar
  while (touch.readRaw(r)) {
    if (millis() - t0 > timeoutMs) return false;
    delay(10);
  }
  // espera tocar
  while (!touch.readRaw(r)) {
    if (millis() - t0 > timeoutMs) return false;
    delay(10);
  }
  delay(40);  // estabiliza
  long sx = 0, sy = 0;
  int n = 0;
  while (n < 25 && touch.readRaw(r)) {
    sx += r.x;
    sy += r.y;
    n++;
    delay(10);
  }
  while (touch.readRaw(r)) delay(10);  // espera soltar
  if (n < 4) return false;
  ax = sx / n;
  ay = sy / n;
  return true;
}

static bool calibrate() {
  const int W = tft.width(), H = tft.height(), m = 20;
  const int pts[4][2] = {{m, m}, {W - m, m}, {W - m, H - m}, {m, H - m}};
  long rx[4], ry[4];
  Serial.println("CAL inicio");
  for (int i = 0; i < 4; i++) {
    tft.fillScreen(TFT_BLACK);
    tft.drawCircle(pts[i][0], pts[i][1], 10, TFT_RED);
    tft.drawFastHLine(pts[i][0] - 15, pts[i][1], 31, TFT_WHITE);
    tft.drawFastVLine(pts[i][0], pts[i][1] - 15, 31, TFT_WHITE);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(textdatum_t::middle_center);
    char buf[40];
    snprintf(buf, sizeof buf, "Toque no alvo %d/4", i + 1);
    tft.drawString(buf, W / 2, H / 2);
    tft.drawString("(caneta, firme, no centro)", W / 2, H / 2 + 18);
    if (!waitTouchSample(30000, rx[i], ry[i])) {
      Serial.println("CAL falhou/timeout");
      return false;
    }
    Serial.printf("CAL ponto %d tela=(%d,%d) raw=(%ld,%ld)\n", i + 1, pts[i][0], pts[i][1], rx[i],
                  ry[i]);
  }
  long dxr = labs((rx[1] + rx[2]) - (rx[0] + rx[3]));
  long dyr = labs((ry[1] + ry[2]) - (ry[0] + ry[3]));
  TouchCal c;
  c.swap = dyr > dxr;
  auto A = [&](int i) { return c.swap ? ry[i] : rx[i]; };
  auto B = [&](int i) { return c.swap ? rx[i] : ry[i]; };
  c.xa0 = (A(0) + A(3)) / 2;
  c.xa1 = (A(1) + A(2)) / 2;
  c.ya0 = (B(0) + B(1)) / 2;
  c.ya1 = (B(2) + B(3)) / 2;
  c.w = W;
  c.h = H;
  c.m = m;
  if (abs(c.xa1 - c.xa0) < 500 || abs(c.ya1 - c.ya0) < 500) {
    Serial.println("CAL rejeitada: variacao raw pequena demais");
    return false;
  }
  c.valid = 1;
  gCal = c;
  prefs.putBytes(calKey(gRot), &c, sizeof c);
  Serial.println("CAL salva");
  printConfig();
  return true;
}

// ------------------------------------------------------------ microSD

static int sdLineY = 0;
static void sdLog(uint16_t color, const char* fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.printf("SD %s\n", buf);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(textdatum_t::top_left);
  tft.drawString(buf, 6, sdLineY);
  sdLineY += 16;
}

static void runSdTest() {
  tft.fillScreen(TFT_BLACK);
  tft.setFont(&fonts::Font2);
  sdLineY = 6;
  sdLog(TFT_WHITE, "Teste microSD");

  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  uint32_t freq = 20000000;
  bool ok = SD.begin(PIN_SD_CS, sdSPI, freq);
  if (!ok) {
    SD.end();
    freq = 4000000;
    ok = SD.begin(PIN_SD_CS, sdSPI, freq);
  }
  if (!ok) {
    sdLog(TFT_RED, "FALHA ao montar (cartao? FAT32?)");
    SD.end();
    sdSPI.end();
    footer("BOOT: prox. tela | serial s: repetir");
    return;
  }
  const char* types[] = {"NENHUM", "MMC", "SD", "SDHC", "?"};
  uint8_t t = SD.cardType();
  sdLog(TFT_GREEN, "Montado @ %u MHz, tipo %s", freq / 1000000, types[t > 4 ? 4 : t]);
  sdLog(TFT_WHITE, "Cartao: %llu MB", SD.cardSize() / (1024ULL * 1024ULL));
  sdLog(TFT_WHITE, "FS: %llu / %llu MB usados", SD.usedBytes() / (1024ULL * 1024ULL),
        SD.totalBytes() / (1024ULL * 1024ULL));

  File root = SD.open("/");
  int shown = 0, total = 0;
  if (root) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      if (shown < 5) {
        sdLog(TFT_LIGHTGREY, " %s %s", f.isDirectory() ? "[d]" : "   ", f.name());
        shown++;
      } else {
        Serial.printf("SD  %s %s\n", f.isDirectory() ? "[d]" : "   ", f.name());
      }
      total++;
      f.close();
    }
    root.close();
  }
  sdLog(TFT_LIGHTGREY, " (%d itens na raiz)", total);

  // escrita/leitura
  const char* path = "/cyd_test.bin";
  static uint8_t buf[4096];
  for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i * 7 + 3);
  const int blocks = 64;  // 256 KB
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    sdLog(TFT_RED, "FALHA ao criar arquivo de teste");
  } else {
    uint32_t t0 = millis();
    for (int b = 0; b < blocks; b++) f.write(buf, sizeof buf);
    f.close();
    uint32_t tw = millis() - t0;
    f = SD.open(path, FILE_READ);
    static uint8_t rb[4096];
    bool same = f && f.size() == blocks * sizeof buf;
    t0 = millis();
    for (int b = 0; same && b < blocks; b++) {
      if (f.read(rb, sizeof rb) != (int)sizeof rb || memcmp(rb, buf, sizeof rb) != 0) same = false;
    }
    uint32_t tr = millis() - t0;
    if (f) f.close();
    SD.remove(path);
    const float kb = blocks * sizeof(buf) / 1024.0f;
    sdLog(same ? TFT_GREEN : TFT_RED, "R/W 256KB: %s", same ? "OK" : "ERRO");
    sdLog(TFT_WHITE, "esc %.0f KB/s  leit %.0f KB/s", kb * 1000 / (tw ? tw : 1),
          kb * 1000 / (tr ? tr : 1));
  }
  SD.end();
  sdSPI.end();
  footer("BOOT: prox. tela | serial s: repetir");
}

// -------------------------------------------------------------- fluxo

static void showScreen(Screen s) {
  gScreen = s;
  switch (s) {
    case SCR_DISPLAY: drawDisplayTest(); break;
    case SCR_TOUCH: drawTouchScreen(); break;
    case SCR_SD: runSdTest(); break;
    default: break;
  }
}

static void doCalibrate() {
  bool ok = calibrate();
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(ok ? "Calibracao salva" : "Calibracao falhou", tft.width() / 2, tft.height() / 2);
  delay(1000);
  showScreen(SCR_TOUCH);
}

static void touchDiag() {
  // le z1/z2/x/y crus por 3 s
  Serial.println("DIAG XPT2046 (3 s) - toque e solte durante o teste");
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    TouchRaw r;
    bool p = touch.readRaw(r);
    Serial.printf("DIAG irq=%d pressed=%d z=%u x=%u y=%u\n", digitalRead(PIN_TOUCH_IRQ), p, r.z,
                  p ? r.x : 0, p ? r.y : 0);
    delay(150);
  }
}

static void handleSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    switch (ch) {
      case '1': showScreen(SCR_DISPLAY); break;
      case '2': showScreen(SCR_TOUCH); break;
      case '3':
      case 's': showScreen(SCR_SD); break;
      case 'n': showScreen((Screen)((gScreen + 1) % SCR_COUNT)); break;
      case 'r':
        gRot = (gRot + 1) & 3;
        saveSettings();
        applyDisplay();
        printConfig();
        showScreen(gScreen == SCR_SD ? SCR_DISPLAY : gScreen);
        break;
      case 'i':
        gInvert = !gInvert;
        saveSettings();
        applyDisplay();
        printConfig();
        showScreen(gScreen == SCR_SD ? SCR_DISPLAY : gScreen);
        break;
      case 'o':
        gRgbOrder = !gRgbOrder;
        saveSettings();
        applyDisplay();
        printConfig();
        showScreen(gScreen == SCR_SD ? SCR_DISPLAY : gScreen);
        break;
      case 'c': doCalibrate(); break;
      case 'l':
        if (gScreen == SCR_TOUCH) drawTouchScreen();
        break;
      case 'z': touchDiag(); break;
      case 'd':
        // RDDID (0x04): ST7789 -> 85 85 52 ; ILI9341 -> ID4 (0xD3) = 00 93 41
        Serial.printf("PANEL RDDID(0x04)=%08X  RDID4(0xD3)=%08X (bytes crus, 1o = dummy)\n",
                      (unsigned)tft.getPanel()->readCommand(0x04, 0, 4),
                      (unsigned)tft.getPanel()->readCommand(0xD3, 0, 4));
        break;
      case 'p': printConfig(); break;
      case 'x':
        prefs.clear();
        gRot = 1;
        gInvert = false;
        gRgbOrder = false;
        applyDisplay();
        printConfig();
        showScreen(SCR_DISPLAY);
        break;
      case 'h':
      case '?': printHelp(); break;
      default: break;
    }
  }
}

static void handleButton() {
  static bool prev = false, longFired = false;
  static uint32_t downAt = 0;
  bool b = digitalRead(PIN_BOOT_BTN) == LOW;
  uint32_t now = millis();
  if (b && !prev) {
    downAt = now;
    longFired = false;
  }
  if (b && !longFired && now - downAt > 1500) {
    longFired = true;
    doCalibrate();
  }
  if (!b && prev && !longFired && now - downAt > 30) {
    showScreen((Screen)((gScreen + 1) % SCR_COUNT));
  }
  prev = b;
}

static void handleTouch() {
  static uint32_t lastPoll = 0, lastLog = 0;
  static bool wasDown = false;
  static int lastX = -1, lastY = -1;
  uint32_t now = millis();
  if (now - lastPoll < 15) return;
  lastPoll = now;

  TouchRaw r;
  if (touch.readRaw(r)) {
    int16_t sx, sy;
    touchCalMap(gCal, r.x, r.y, sx, sy);
    if (gScreen == SCR_TOUCH) {
      if (wasDown && lastX >= 0)
        tft.drawLine(lastX, lastY, sx, sy, TFT_GREEN);
      else
        tft.fillCircle(sx, sy, 2, TFT_GREEN);
      if (now - lastLog > 100) showTouchInfo(r, sx, sy);
    }
    if (now - lastLog > 100) {
      Serial.printf("TOUCH raw x=%u y=%u z=%u -> tela x=%d y=%d\n", r.x, r.y, r.z, sx, sy);
      lastLog = now;
    }
    lastX = sx;
    lastY = sy;
    wasDown = true;
  } else if (wasDown) {
    Serial.println("TOUCH up");
    wasDown = false;
    lastX = lastY = -1;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  // LED RGB apagado (ativo em LOW)
  for (int p : {PIN_LED_R, PIN_LED_G, PIN_LED_B}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  prefs.begin("cyd", false);
  gRot = prefs.getUChar("rot", 1) & 3;
  gInvert = prefs.getBool("inv", false);
  gRgbOrder = prefs.getBool("rgb", false);

  tft.init();
  tft.setBrightness(200);
  applyDisplay();
  touch.begin();

  Serial.printf("\nCYD hw test | chip %s rev%d | flash %u MB | heap %u\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getFlashChipSize() / (1024 * 1024), ESP.getFreeHeap());
  printConfig();
  printHelp();
  showScreen(SCR_DISPLAY);
}

void loop() {
  handleSerial();
  handleButton();
  handleTouch();
}
