#pragma once
#include <Arduino.h>

// Calibracao linear raw->tela por eixo, com deteccao de eixos trocados.
// xa0/xa1: raw correspondente a x=m e x=w-m; ya0/ya1: y=m e y=h-m.
// Padrao = calibracao medida nesta placa em rot=1 (paisagem 320x240), 2026-11-23.
struct TouchCal {
  uint8_t valid = 0;
  uint8_t swap = 1;
  int16_t xa0 = 415, xa1 = 3596;
  int16_t ya0 = 599, ya1 = 3613;
  int16_t w = 320, h = 240, m = 20;
};

inline void touchCalMap(const TouchCal& c, uint16_t rx, uint16_t ry, int16_t& sx, int16_t& sy) {
  int a = c.swap ? ry : rx;
  int b = c.swap ? rx : ry;
  int dx = c.xa1 - c.xa0, dy = c.ya1 - c.ya0;
  if (dx == 0) dx = 1;
  if (dy == 0) dy = 1;
  long x = c.m + (long)(a - c.xa0) * (c.w - 2 * c.m) / dx;
  long y = c.m + (long)(b - c.ya0) * (c.h - 2 * c.m) / dy;
  sx = constrain(x, 0, c.w - 1);
  sy = constrain(y, 0, c.h - 1);
}
