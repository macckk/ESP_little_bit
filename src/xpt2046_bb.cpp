#include "xpt2046_bb.h"

// Comandos (12 bits, diferencial). PD=01 mantem ADC ligado entre leituras;
// o ultimo comando com PD=00 desliga e reabilita o PENIRQ.
static constexpr uint8_t CMD_Z1 = 0xB1;
static constexpr uint8_t CMD_Z2 = 0xC1;
static constexpr uint8_t CMD_Y = 0x91;
static constexpr uint8_t CMD_X = 0xD1;
static constexpr uint8_t CMD_PWRDN = 0xD0;

void XPT2046_BB::begin() {
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH);
  pinMode(_clk, OUTPUT);
  digitalWrite(_clk, LOW);
  pinMode(_mosi, OUTPUT);
  digitalWrite(_mosi, LOW);
  pinMode(_miso, INPUT);
  if (_irq >= 0) pinMode(_irq, INPUT);
  // Deixa o chip em power-down com IRQ habilitado
  digitalWrite(_cs, LOW);
  xfer(CMD_PWRDN);
  digitalWrite(_cs, HIGH);
}

uint16_t XPT2046_BB::xfer(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(_mosi, (cmd >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(_clk, HIGH);
    delayMicroseconds(1);
    digitalWrite(_clk, LOW);
  }
  digitalWrite(_mosi, LOW);
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(_clk, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | (digitalRead(_miso) & 1);
    digitalWrite(_clk, LOW);
    delayMicroseconds(1);
  }
  return (v >> 3) & 0x0FFF;
}

static uint16_t median5(uint16_t* a) {
  for (int i = 1; i < 5; i++)
    for (int j = i; j > 0 && a[j - 1] > a[j]; j--) {
      uint16_t t = a[j];
      a[j] = a[j - 1];
      a[j - 1] = t;
    }
  return a[2];
}

bool XPT2046_BB::readRaw(TouchRaw& out) {
  digitalWrite(_cs, LOW);
  uint16_t z1 = xfer(CMD_Z1);
  uint16_t z2 = xfer(CMD_Z2);
  int z = (int)z1 + 4095 - (int)z2;
  if (z < 0) z = 0;
  if (z1 == 0 && z2 == 0) z = 0;  // MISO preso em 0: chip nao responde
  if (z < zThreshold) {
    xfer(CMD_PWRDN);
    digitalWrite(_cs, HIGH);
    out.z = z;
    return false;
  }
  uint16_t xs[5], ys[5];
  xfer(CMD_Y);  // descarta primeira leitura (assentamento)
  for (int i = 0; i < 5; i++) {
    ys[i] = xfer(CMD_Y);
    xs[i] = xfer(CMD_X);
  }
  xfer(CMD_PWRDN);
  digitalWrite(_cs, HIGH);
  out.x = median5(xs);
  out.y = median5(ys);
  out.z = z;
  return true;
}
