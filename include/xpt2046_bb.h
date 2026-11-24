#pragma once
#include <Arduino.h>

struct TouchRaw {
  uint16_t x, y, z;
};

// Driver XPT2046 por bit-bang (nao ocupa HSPI/VSPI).
class XPT2046_BB {
 public:
  XPT2046_BB(int clk, int mosi, int miso, int cs, int irq)
      : _clk(clk), _mosi(mosi), _miso(miso), _cs(cs), _irq(irq) {}

  void begin();
  // true se ha toque com pressao >= zThreshold. Valores 12 bits.
  bool readRaw(TouchRaw& out);

  uint16_t zThreshold = 350;

 private:
  uint16_t xfer(uint8_t cmd);
  int _clk, _mosi, _miso, _cs, _irq;
};
