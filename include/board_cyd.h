#pragma once
// Pinagem ESP32-2432S028 (CYD) - versao 2 USB (micro-USB + USB-C)

// TFT (HSPI)
#define PIN_TFT_SCK   14
#define PIN_TFT_MOSI  13
#define PIN_TFT_MISO  12
#define PIN_TFT_CS    15
#define PIN_TFT_DC     2
#define PIN_TFT_RST   -1   // ligado ao EN
#define PIN_TFT_BL    21

// Touch XPT2046 (barramento proprio, lido por bit-bang)
#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_IRQ  36

// microSD (VSPI)
#define PIN_SD_SCK   18
#define PIN_SD_MISO  19
#define PIN_SD_MOSI  23
#define PIN_SD_CS     5

// Outros
#define PIN_LED_R     4    // LED RGB ativo em LOW
#define PIN_LED_G    16
#define PIN_LED_B    17
#define PIN_LDR      34
#define PIN_SPEAKER  26
#define PIN_BOOT_BTN  0
