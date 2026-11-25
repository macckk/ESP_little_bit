# CYD ESP32-2432S028 (2 USB) — configurações descobertas

Tudo medido/confirmado nesta placa em 2026-11-23. Fonte da verdade para os projetos futuros.

## Placa

| Item | Valor |
|---|---|
| Chip | ESP32-D0WD-V3 rev 3.1, dual core 240 MHz, cristal 40 MHz |
| Flash | 4MB (fabricante 0x5E, device 0x4016), modo DIO; sem PSRAM |
| MAC | `xx:xx:xx:xx:xx:xx` → id curto sugerido `cyd-xxxx` |
| USB-serial | CH340 (`VID_1A86&PID_7523`), driver já instalado → **COM5** |
| Upload | 921600 baud funciona sem erro |
| Heap livre no boot (só tela+toque) | ~327KB |

## Tela — ST7789 240x320 (HSPI)

| Parâmetro | Valor confirmado |
|---|---|
| Driver | **ST7789** (`lgfx::Panel_ST7789`) |
| Pinos | SCK 14, MOSI 13, MISO 12, CS 15, DC 2, RST -1 (ligado ao EN), BL 21 (PWM) |
| SPI | modo 0, escrita 40 MHz |
| Rotação | **1** → paisagem 320x240, quadrado vermelho de teste no canto superior esquerdo, texto não espelhado |
| Inversão | **invert = false** (fundo preto aparece preto) |
| Ordem de cor | **rgb_order = false** (R/G/B corretos) |
| Offsets | 0 / 0 (borda de 1px visível nos 4 lados) |
| Leitura (MISO) | **não responde**: RDDID volta lixo (`81 81 81`) e 0xD3 volta 0 → não dá para ler pixels/ID; tratar a tela como só-escrita |

Config completa em [include/lgfx_cyd.h](../include/lgfx_cyd.h).

## Toque — XPT2046 resistivo

| Parâmetro | Valor |
|---|---|
| Pinos | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 (barramento próprio) |
| Driver | bit-bang em [src/xpt2046_bb.cpp](../src/xpt2046_bb.cpp): HSPI é da tela e VSPI do SD |
| Repouso | IRQ = 1, pressão z ≈ 0–20 |
| Limiar de toque | z ≥ 350 |
| **Calibração (rot=1, 320x240)** | **swap = 1** (raw Y → tela X), **xa = 415..3596**, **ya = 599..3613**, margem 20px |
| Precisão | confirmada visualmente: traço sai embaixo da caneta |

Mapeamento: `x = 20 + (rawY - 415) * 280 / (3596 - 415)`, `y = 20 + (rawX - 599) * 200 / (3613 - 599)`.
Valores também gravados como padrão em [include/touch_cal.h](../include/touch_cal.h).

## microSD (VSPI)

| Parâmetro | Valor |
|---|---|
| Pinos | SCK 18, MISO 19, MOSI 23, CS 5 |
| Cartão de teste | SDHC 32GB, FAT32, vazio (o app ainda não usa o SD) |
| Clock | monta a 20 MHz |
| Velocidade (256KB) | escrita ~970KB/s, leitura ~1,4MB/s; R/W verificado OK |

## Outros pinos

LED RGB (ativo em LOW): R 4, G 16, B 17 · LDR 34 · alto-falante 26 · botão BOOT 0.

| Item | Resultado (2026-11-23) |
|---|---|
| LED RGB | **funciona**; fica na traseira e ilumina a parede. PWM LEDC canais 2/3/4 (o 7 é o backlight); confirmado vermelho, verde e âmbar |
| LDR (GPIO 34) | **inutilizável nesta placa**: lê 0 com sala clara, coberto com o dedo e com a placa de tela para baixo, em 0 dB e 11 dB (~90 leituras, só 3 picos de ruído). A tensão do divisor fica abaixo do piso do ADC. Alternativa: sensor I2C (BH1750) no conector de expansão |
| Alto-falante | conector SPEAK presente; **sem alto-falante** (comprar 8 Ω 0,5–2 W, JST 1,25 mm 2 pinos) |

## NVS em uso

- `cyd`: `rot` (u8), `inv` (bool), `rgb` (bool), `cal1` (blob `TouchCal`, calibração da rotação 1); e do app:
  `pet`, `led`, `autob`, `nini`, `nfim`, `ldrd`, `ldrb` (ver [GUIA.md](GUIA.md#nvs)).
- `net`: `url`, `token` do backend. O Wi-Fi fica na NVS do próprio ESP-IDF.

A NVS fica em 0x9000 em `default.csv`, `min_spiffs.csv` e `partitions_cyd.csv`: trocar a partição preserva a
calibração; `pio run -t erase` apaga.

## Partições (4MB)

| Partição | Offset | Tamanho |
|---|---|---|
| nvs | 0x9000 | 20KB |
| otadata | 0xE000 | 8KB |
| app0 / app1 (OTA) | 0x10000 / 0x1D0000 | 1,75MB cada |
| spiffs (LittleFS) | 0x390000 | 384KB (pets em pixel art) |
| coredump | 0x3F0000 | 64KB |

Arquivo: [partitions_cyd.csv](../partitions_cyd.csv). Com 128KB de LittleFS (`min_spiffs.csv`) os 8 pets não cabiam e a
LittleFS chegou a travar a placa em loop (divisão por zero com a partição cheia).

## Rede

| Item | Valor |
|---|---|
| IP da placa | **192.168.1.50** (exemplo; reserve um IP fixo no roteador: pelo DHCP ele muda), RSSI −63 a −73 dBm |
| mDNS `cyd-xxxx.local` | anunciado pela placa, mas **não resolve no Windows** → usar IP no OTA |
| ArduinoOTA | funciona (~1,6MB em ~90 s) |
| PC de desenvolvimento / backend | **192.168.1.10** (reservado no roteador), porta 8000 acessível pela placa |

## Lições para os próximos passos

- Fontes embutidas da LovyanGFX (Font0/2/4/7, DejaVu*) só têm ASCII 0x20–0x7E. Acentos PT-BR exigem fonte gerada.
- Um firmware com Wi-Fi + HTTPClient + LovyanGFX + ArduinoJson ocupou ~1,08MB; o app completo (LVGL, pets) ~1,63MB.
  Partição atual: `partitions_cyd.csv` (2 apps de 1,75MB para OTA + LittleFS de 384KB para os pets). Trocar a partição
  exige gravar por USB e reformata o LittleFS (os pets são baixados de novo).
- Rede 2,4 GHz apenas.
- App com LVGL 9.2 + WiFiManager + HTTP + ArduinoOTA + 3 fontes PT-BR: 1,56MB; com os pets e o pet reserva
  embutido (8KB): **1,63MB** (89% do slot de 1,75MB).
- A região estática `.bss` (DRAM) estoura com heap LVGL de 48KB + 2 buffers estáticos de 12,8KB: buffers de desenho vão no heap (`heap_caps_malloc`).
- Heap em operação com Wi-Fi + LVGL: ~136KB livres, maior bloco ~86–90KB; no portal ~140KB.
- O LED RGB é forte: o firmware limita a 60% e aplica correção de gama; ele é ativo em LOW.
