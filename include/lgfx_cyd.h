#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board_cyd.h"

#if defined(CYD_PANEL_ILI9341)
using CydPanel = lgfx::Panel_ILI9341;
#define CYD_PANEL_NAME "ILI9341"
#else
using CydPanel = lgfx::Panel_ST7789;
#define CYD_PANEL_NAME "ST7789"
#endif

class LGFX : public lgfx::LGFX_Device {
  CydPanel _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_TFT_SCK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_dc = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = false;      // ajustado em runtime via invertDisplay()
      cfg.rgb_order = false;   // ajustado em runtime via setRgbOrder()
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_TFT_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }

  // Muda o bit RGB/BGR; tem efeito no proximo setRotation().
  void setRgbOrder(bool rgb) {
    auto cfg = _panel.config();
    cfg.rgb_order = rgb;
    _panel.config(cfg);
  }
};
