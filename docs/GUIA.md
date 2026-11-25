# Guia técnico: Little Bit Pixel e base de apps para CYD

Placa "burra": só desenha telas a partir de JSON pequeno. O **backend** (FastAPI) roda **na rede local**, no PC onde o
Claude Code roda (um Raspberry Pi também serviria; **não usamos VPS**), faz o trabalho pesado e guarda as credenciais.

Base comum para várias ideias na **mesma placa**: cada ideia é um **app** do backend, e a placa troca de app pelo toque
(menu no título) ou pelo PC, sem regravar nada. Hoje: **pet de agentes de IA** (Little Bit Pixel), **Monitor** de serviços
(semente do painel EasyPanel) e **Contador** com botões (semente do contador de vendas); planejado: QR Pix de balcão.
Ver [Apps](#apps-a-mesma-placa-várias-ideias).

- Hardware medido e calibrado: **[HARDWARE.md](HARDWARE.md)**
- Como criar pets novos no ChatGPT: **[pets/PROMPTS.md](../pets/PROMPTS.md)**

```
 Claude Code (hooks) ──POST /events/claude──▶ ┌──────────────────────────┐
 Open-Meteo (clima)  ◀──── a cada 30 min ──── │ backend FastAPI          │
 ~/.claude/projects (transcripts, uso) ─────▶ │ PC 192.168.1.10:8000     │
 pets/*.webp (desenhos) ── converte ────────▶ │ (inicia com o Windows)   │
                                              └────────────┬─────────────┘
                              long-poll GET /device/{id}/screen?wait=25 (JSON)
                              GET /device/{id}/sprite/{nome} (pets, binário)
                                                           │ Wi-Fi
                                              ┌────────────▼─────────────┐
                                              │ CYD 192.168.1.50         │
                                              │ LVGL: pet, carrossel,    │
                                              │ barra, LED traseiro      │
                                              └──────────────────────────┘
```

| Item | Valor |
|---|---|
| Placa | ESP32-2432S028 (2 USB), ST7789 320×240, toque resistivo XPT2046, microSD, 4MB flash, sem PSRAM |
| IP da placa (exemplo) | **192.168.1.50** (reserve no roteador; MAC `xx:xx:xx:xx:xx:xx`, id `cyd-xxxx`) |
| IP do PC/backend (exemplo) | **192.168.1.10**, porta 8000 (reserve no roteador) |
| Serial | COM5, 115200 (CH340) |
| Depois do desenvolvimento | só alimentação USB: tudo (dados, pets, firmware via OTA) chega pela rede |

---

## Apps: a mesma placa, várias ideias

A placa não sabe de pet, servidor ou vendas: ela só desenha **layouts genéricos** (`status`, `number`, `qr`, `message`
e o `pet`) e manda de volta os **toques**. Quem decide o conteúdo é o **app ativo** no backend. Trocar de ideia = trocar
de app (ou escrever um novo em Python); o firmware não muda.

| App | id | Layout | Interação |
|---|---|---|---|
| Little Bit Pixel (pet de agentes) | `pet` | `pet` | arrastar/segurar para trocar o pet, toque = carinho |
| Monitor de serviços | `monitor` | `status` | botão **Atualizar** refaz as checagens |
| Contador | `contador` | `number` | botões **-1**, **Zerar**, **+1** (valor do dia, zera à meia-noite) |

### Trocar de app

- **Na placa:** toque no **título da barra** → menu com os apps (o atual destacado) → toque no app.
  No fim do menu fica o botão **Reiniciar** (pede confirmação; a placa volta sozinha ao Wi-Fi, ao backend e ao
  mesmo app). O menu mostra até 4 apps.
- **Pelo PC:** `POST /device/cyd-xxxx/app/<id>` com o `HOOK_TOKEN`.
- **Serial:** `app monitor`, ou `apps` para abrir o menu.

O app escolhido fica salvo por placa em `backend/data/devices.json` (vale para várias placas). Os apps continuam
rodando por trás: o pet acompanha o Claude mesmo com o Monitor na tela.

### Configurar (`backend/apps.json`)

```json
{
  "apps": ["pet", "monitor", "contador"],
  "default": "pet",
  "monitor": {"title": "Serviços", "interval": 60, "slow_ms": 1500,
              "checks": [{"name": "Backend", "url": "http://127.0.0.1:8000/apps"}, ...]},
  "contador": {"title": "Contador", "label": "Hoje", "goal": 0}
}
```

`apps` = quais aparecem no menu e em que ordem; cada app lê a sua seção. `goal > 0` no contador mostra barra de meta.
O Monitor marca amarelo quem responde acima de `slow_ms` ou com HTTP 4xx, e vermelho quem não responde ou dá 5xx.

### Criar um app novo

1. Arquivo em `backend/app/apps/`, com uma subclasse de `App` (`apps/base.py`):

   ```python
   from .base import App

   class VendasApp(App):
       id = "vendas"
       name = "Vendas"

       def refresh(self):            # lento (rede/arquivos), numa thread a cada ~20 s
           self.total = buscar_total()

       def screen(self):             # rápido: devolve a tela
           return {"layout": "number", "title": self.name, "label": "Hoje", "value": f"R$ {self.total}",
                   "buttons": [{"id": "atualizar", "label": "Atualizar"}]}

       def action(self, device_id, action, value):   # toque na placa; True = tela mudou
           if action == "atualizar":
               self.refresh()
               return True
           return False
   ```

2. Registrar em `backend/app/apps/__init__.py` (`CATALOG`).
3. Pôr o id em `"apps"` no `apps.json` (e uma seção com a configuração dele, se precisar) e reiniciar o backend.

Erros dentro de um app viram uma tela de erro só daquele app; os outros seguem funcionando.

### Interatividade no JSON

- `"buttons": [{"id", "label"}]`: até 4 botões numa fileira embaixo (qualquer layout menos `pet`). Toque →
  `POST /device/{id}/action {"action": <id>}`.
- Item de `status` com `"action"` (e `"id"` opcional): a linha vira botão → `{"action": ..., "value": id ou label}`.
- A resposta não precisa voltar no POST: o backend atualiza a tela e ela chega pelo long-poll (a placa busca na hora
  depois de cada toque).

---

## Projeto 1: Little Bit Pixel, o pet de agentes

Um bichinho em pixel art que reage às sessões do Claude Code. Com várias sessões, segue a mais urgente e mostra
"N sessões" no rodapé.

### Tela

- **Barra:** "Little Bit Pixel" à esquerda; à direita aviso offline, ícone Wi-Fi (cor por RSSI) e hora (NTP).
- **Esquerda:** o pet. **Direita:** balão de fala (o que o Claude está fazendo: "Editando ui.cpp", "Rodando pio…",
  "Tenho uma pergunta pra você"…) e, abaixo dele, o **uso do Claude**.

### Humores

| Humor | Quando | Pet | LED traseiro |
|---|---|---|---|
| dormindo | 10 min sem atividade | quadro "dormindo", respirando devagar, "zZz"; depois de 30 s, carrossel | apagado |
| ocioso | entre tarefas | normal, pisca sozinho, abana o rabo em rajadas | apagado |
| trabalhando | ferramentas rodando | quadro "trabalhando", pulinhos, "…" | âmbar respirando |
| precisa de você | `Notification`, `AskUserQuestion`, `ExitPlanMode` | quadro "alerta", pulos altos, "!", balão amarelo | vermelho piscando |
| terminou | `Stop` (por 2 min) | quadro "feliz", quica, brilhos | verde por ~6 s |
| erro | reservado (ex. deploy falhou; nenhum evento do Claude dispara hoje) | treme, balão vermelho | vermelho fixo |

Toque rápido no pet = carinho (quadro "feliz" + corações).

### Pets (8, em pixel art)

**Gato, Blob, Robô, Fantasminha, Cachorro, TV retrô, Pinguim e Dino**, gerados no ChatGPT e convertidos pelo backend.

- **Trocar:** arraste o dedo para o lado sobre o pet, ou **segure o dedo** para abrir o seletor (grade com miniaturas
  + botões **LED ligado/desligado** e **Modo noite sim/não**). A escolha fica salva na NVS pelo nome (`"spr:gato"`).
- **Pet novo:** salve o desenho em `pets/` e em até 20 s ele aparece na placa, sem regravar o firmware
  (ver [Pets em pixel art](#pets-em-pixel-art)).
- **Pet reserva:** o gato também vai embutido no firmware (`src/app/pet_builtin.h`). Placa nova, sem backend ou com a
  memória apagada nunca fica sem pet. Se o pet escolhido sumir da pasta, a placa mostra o gato.
- Os 8 pets vetoriais desenhados por código (primeira versão) foram removidos; os pixel art os substituíram
  (o Polvo virou Fantasminha).

### Uso do Claude (abaixo do balão)

- **App desktop (o normal):** métricas medidas no PC:
  `Contexto 63%` (tokens da última resposta ÷ 1M, calculado dos transcripts; bateu com o anel do app:
  616.333 × 616.802), `5 h: N prompts`, `Hoje: N prompts` (dos hooks) e `Ativo hoje: 1h24`.
- **Claude Code de terminal:** a linha de status (`backend/hooks/statusline.py`) envia os **% oficiais do plano**, e a
  tela troca para `5 h 31% · 14:10`, `Semana 57% · sáb 15:00` e `Contexto`. O app desktop **não executa** a linha de
  status (testado em 2026-11-24), por isso lá ficam as métricas locais. Plano Pro não tem limite mensal.
- O custo equivalente em US$ continua calculado (`/health` → `usage`), mas não vai para a tela.

### Carrossel com o pet dormindo

Depois de 30 s dormindo, a tela alterna a cada 10 s:

1. **Relógio grande e data** ("terça-feira, 24 de novembro").
2. **Clima da sua cidade** (`WEATHER_CITY`/`WEATHER_STATE` no `backend/.env`; Open-Meteo, a cada 30 min): ícone animado, temperatura, máx/mín, chuva, umidade e
   amanhã. Cidade em `WEATHER_CITY`/`WEATHER_STATE`. O clima usa o verificador de certificados do Windows
   (`truststore`): o OpenSSL do Python recusava a cadeia nova da Let's Encrypt (YE1) do Open-Meteo.
3. **Hoje com o Claude:** prompts, tempo ativo, edições, comandos.
4. **Tempo ativo nos últimos 7 dias:** barras, hoje destacado.

Tocar na tela volta ao pet por 1 min; qualquer atividade do Claude acorda o pet na hora. No modo noite só o relógio.

### LED e modo noite

- **LED traseiro** ilumina a parede; cores na tabela de humores. Desliga pelo seletor.
- **Modo noite por horário** (padrão 22h–7h, via NTP): tela no mínimo, LED só para "precisa de você", fraco.
  O sensor de luz (LDR) da placa não funciona (ver HARDWARE.md), por isso o horário. Serial `noite 22 7` muda o horário.
- Sem alto-falante neste projeto.

---

## Pets em pixel art

### Criar um pet

Passo a passo e prompts prontos em **[pets/PROMPTS.md](../pets/PROMPTS.md)**. Resumo:

1. ChatGPT, conversa nova, anexando o gato como referência de estilo → **prompt 1** (o personagem).
2. **Prompt 2** → folha com `normal`, `piscando`, `dormindo` → salvar como `pets/<nome>_base.webp`.
3. **Prompt 3** → folha com `alerta`, `feliz`, `trabalhando` → salvar como `pets/<nome>_alerta.webp`.
4. (Opcional) rabo abanando: `pets/<nome>.json` (abaixo).

Nome do arquivo sem acento (`robo`, não `robô`): o nome do pet é o que vem antes do `_`, só `a-z0-9`, até 12 letras.

### Regras dos arquivos

- **Fundo magenta liso (#FF00FF)** ou transparente; quadros **lado a lado**.
- **Papéis, nesta ordem:** `normal`, `piscando`, `dormindo`, `alerta`, `feliz`, `trabalhando`, `rabo`, `respirando`.
  Só o `normal` é obrigatório; o que faltar usa o normal (dormir sem quadro próprio usa o "piscando").
- **Vários arquivos do mesmo pet se juntam.** Se o sufixo for um papel (`gato_alerta.webp`), os quadros daquele arquivo
  começam nesse papel; senão começam no `normal`. Papel repetido: vale o arquivo com mais quadros.
- **Gerados por script** quando faltam:
  - `respirando`: o quadro de dormir com tudo acima de uma linha do corpo 1 px mais baixo (a placa alterna devagar).
  - `rabo`: se `pets/<nome>.json` disser onde fica o rabo no quadro normal. Exemplo (gato):
    `{"rabo": {"x": 31, "y0": 28, "y1": 38, "desloca": 2}}`: da coluna `x` em diante, as linhas `y0`–`y1` se
    inclinam (`desloca` px no topo, 0 na base). Coordenadas: `pet_convert.py <nome> --grade` gera a prévia numerada.
    Configurados: gato, cachorro e dino.
- Grade final de até 64×64 blocos; os atuais têm de 33×28 (blob) a 48×46 (cachorro).

### Conversão (`backend/app/sprites.py`)

A "pixel art" da IA não é pixel art de verdade: são imagens de ~2000 px com blocos de 12–16 px, de tamanhos
irregulares, centenas de cores e compressão WebP. O conversor:

1. Separa os quadros pelas colunas de fundo e mede o **tamanho do bloco** (Fourier sobre as bordas). Se o bicho ficar
   com menos de 28 blocos no maior lado, o bloco foi medido em dobro (contorno sempre em pares, caso do robô) e é dividido.
2. **Grade que gruda nas bordas reais:** cada linha da grade se ajusta à borda mais forte perto de onde era esperada
   (com grade fixa, linhas finas como o sorriso do robô sumiam).
3. Amostra 9 pontos no centro de cada bloco e fica com o **medoide** (um pixel real; a mediana por canal inventava cores).
4. **Fundo:** magenta, e também as bordas que o WebP mistura com o magenta (rosadas, roxas e, só na borda da silhueta,
   vinho). Sem isso apareciam pontinhos rosa no contorno.
5. Arquivos extras do mesmo pet são **reamostrados na escala do arquivo do normal** (cada imagem da IA sai com blocos
   um pouco diferentes; sem isso o pet encolhia ao mudar de humor).
6. Junta cores quase iguais, limita a **15 cores + transparente** e **alinha os quadros** ao normal (até 3 px), para
   piscar sem "pular".

Pacote final: 4 bits por pixel, ~1 KB por quadro, 3–9 KB por pet. Formato em `sprites.py` (cabeçalho `CYDP`).

### Prévia e diagnóstico

```powershell
backend\.venv\Scripts\python tools\pet_convert.py gato
```

Gera `pets\gato_previa.png` (todos os quadros ampliados; `*_previa.png` é ignorado pelo backend). `--grade` numera
linhas e colunas. No backend, `/health` → `sprites` mostra quadros, grade, cores e erros de conversão de cada pet.

### Na placa

- A placa recebe em cada tela `"sprites": [{"n": "gato", "v": "5a3bd9a6"}, …]` e a task do backend (core 0) baixa o
  que falta para LittleFS (`/sprites/<nome>-<versão>.bin`). Some da pasta → some da placa.
- **Ordem segura:** apaga as versões antigas primeiro, confere o espaço livre e só então baixa (o LittleFS cheio chegou
  a travar a placa em loop; ver [Problemas resolvidos](#problemas-resolvidos)).
- Desenho com ampliação inteira (3× ou 4×), em blocos `lv_draw_rect` juntando pixels vizinhos iguais e pulando
  linhas fora da faixa sendo pintada. Efeitos (zZz, "!", "…", brilhos, corações) desenhados por cima.
- Mudar o pet reserva: `backend\.venv\Scripts\python tools\pet_embed.py gato` e regravar o firmware.

---

## Backend (`backend/`)

| Arquivo | O que faz |
|---|---|
| `app/main.py` | FastAPI: endpoints, app ativo por placa, telas com ETag por app, long-poll, ações, ticker de 5 s |
| `app/apps/` | os apps: `base.py` (contrato), `pet_app.py`, `monitor.py`, `counter.py`; `__init__.py` (catálogo) |
| `apps.json` | apps no menu, app padrão e configuração de cada um |
| `app/pet.py` | estado do pet a partir dos eventos dos hooks (sessões, humor, balão, ordem por `ts`) |
| `app/usage.py` | lê `~/.claude/projects/**/*.jsonl`: contexto %, tempo ativo, custo equivalente |
| `app/plan.py` | % do plano vindos da linha de status (CLI) |
| `app/store.py` | SQLite enxuto de eventos (`data/events.db`): contagens do dia e da semana |
| `app/cards.py` | cartões do carrossel |
| `app/weather.py` | Open-Meteo (geocodificação em cache em `data/geo.json`, previsão a cada 30 min, `truststore`) |
| `app/sprites.py` | conversor e catálogo de pets em pixel art |
| `hooks/claude_hook.py` | hook global do Claude Code → `POST /events/claude` (1 s de timeout, sempre sai 0) |
| `hooks/statusline.py` | linha de status do CLI → `POST /events/statusline` |
| `serve.py` | lançador com **guardião**: roda o servidor num processo filho e o reinicia se cair (log em `data/backend.log`) |

### Endpoints

| Método e caminho | Token | O que é |
|---|---|---|
| `POST /events/claude` | `HOOK_TOKEN` | evento dos hooks |
| `POST /events/statusline` | `HOOK_TOKEN` | % do plano (CLI) |
| `GET /device/{id}/screen?wait=N` | `DEVICE_TOKEN` | JSON da tela do app ativo; long-poll até 30 s com `If-None-Match`/ETag |
| `POST /device/{id}/action` | `DEVICE_TOKEN` | toque: `{"action", "value"}`; `action: "app"` troca o app |
| `GET /device/{id}/sprite/{nome}` | `DEVICE_TOKEN` | pacote binário de um pet |
| `POST /device/{id}/app/{app}` | `HOOK_TOKEN` | troca o app da placa pelo PC |
| `GET /apps` | — | apps disponíveis e o padrão |
| `GET /events/recent?limit=N` | `HOOK_TOKEN` | últimos eventos do Claude |
| `GET /health` | — | placas (app ativo de cada), telas de todos os apps, diagnóstico de cada app |

### Configuração (`backend/.env`, fora do git; modelo em `.env.example`)

| Variável | Padrão | O que é |
|---|---|---|
| `HOOK_TOKEN`, `DEVICE_TOKEN` | — | tokens (gerados aleatórios, nunca exibidos) |
| `PET_TITLE` | Little Bit Pixel | nome do pet (barra e menu de apps) |
| `APPS_CONFIG` | `backend/apps.json` | configuração dos apps |
| `DEVICES_PATH` | `backend/data/devices.json` | app ativo de cada placa |
| `WEATHER_CITY`, `WEATHER_STATE` | São Paulo, São Paulo | cidade do clima (vazio = sem cartão) |
| `PETS_DIR` | `pets/` | pasta dos desenhos |
| `DB_PATH` | `backend/data/events.db` | histórico |
| `PET_DONE_SECONDS` | 120 | tempo comemorando |
| `PET_SLEEP_SECONDS` | 600 | sem atividade → dorme |
| `PET_STALE_WORK_SECONDS` | 900 | "trabalhando" sem eventos → ocioso |
| `PET_SESSION_TTL_SECONDS` | 21600 | sessão esquecida |
| `CLAUDE_PROJECTS_DIR` | `~/.claude/projects` | transcripts do Claude Code |

O token da placa é gravado com `python tools/set_device_token.py` (lê do `.env`, não exibe o valor).

### Rodar

**Inicia sozinho no login do Windows**, sem janela: atalho `CYD backend` na pasta Inicializar →
`backend\.venv\Scripts\pythonw.exe backend\serve.py`. Log em `backend\data\backend.log`.

Com console (depuração), dentro de `backend/`:

```powershell
& .\.venv\Scripts\python.exe serve.py
```

Reiniciar o que roda escondido (depois de mudar o código):

```powershell
Get-CimInstance Win32_Process -Filter "Name like 'python%'" | Where-Object { $_.CommandLine -match 'serve\.py' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }; Start-Process "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup\CYD backend.lnk"
```

Instalar do zero (dentro de `backend/`): `python -m venv .venv` e `.venv\Scripts\pip install -r requirements.txt`
(FastAPI, uvicorn, truststore, Pillow).

### Hooks do Claude Code

Globais em `~/.claude/settings.json` (SessionStart, UserPromptSubmit, PreToolUse, PostToolUse, Notification, Stop,
SessionEnd), **assíncronos** (não atrasam as ferramentas), rodando `backend/hooks/claude_hook.py`.
**Só saem do PC:** evento, ferramenta, nome do arquivo e a 1ª linha do comando (sem `cd`/`Set-Location`, caminho do
executável reduzido ao nome). **Nunca** o prompt nem o conteúdo de arquivos. Backend fora do ar → o hook sai em silêncio.
A linha de status também está em `~/.claude/settings.json` (`refreshInterval` 60).

### Testes

```powershell
cd backend; .\.venv\Scripts\python -m unittest discover -s tests
```

33 testes (pet, uso, plano, cartões, conversor e cache de pets, apps Monitor e Contador, catálogo de apps). Também: `tests\smoke_longpoll.py` (com o servidor rodando) e
`tests\demo_sequence.py` (simula uma sessão para ver todos os humores na placa).

---

## Firmware

### Módulos (`src/app/`)

| Arquivo | O que faz |
|---|---|
| `main.cpp` | setup/loop, console serial, salva o último JSON, log de heap a cada 10 s |
| `display.cpp` | LovyanGFX + LVGL 9.2 + toque calibrado; captura de tela pelo serial |
| `ui.cpp` | barra de status, layouts genéricos (`status`, `number`, `qr`, `message`, `pet`), botões e menu de apps |
| `pet.cpp` | pet em pixel art, humores, efeitos, seletor, carrossel |
| `sprites.cpp` | download/validação dos pets (LittleFS) e pet reserva embutido |
| `backend.cpp` | task (core 0) com long-poll, ETag, novas tentativas 5→10→15 s; sync de pets; task de ações (toques) |
| `netcfg.cpp` | portal Wi-Fi (WiFiManager), URL/token na NVS, reset por BOOT |
| `ambient.cpp` | LED RGB traseiro, modo noite por horário, brilho |
| `ota.cpp` | ArduinoOTA com barra de progresso |
| `fonts/` | Montserrat PT-BR 14/20/48 + ícones |

### Ambientes PlatformIO

| Ambiente | O que é |
|---|---|
| `cyd2usb` (padrão) | app, gravação por USB (COM5) |
| `cyd2usb_ota` | mesmo app, gravação pela rede (ArduinoOTA, IP em `secrets.ini`) |
| `hwtest` | teste de hardware: cores/rotação, toque + calibração, microSD |

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e cyd2usb -t upload
```

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e cyd2usb_ota -t upload
```

`secrets.ini` (fora do git, modelo em `secrets.ini.example`): senha do OTA (aleatória) e `ota_host = 192.168.1.50`
(o Windows não resolve `cyd-xxxx.local`).

### Partições (`partitions_cyd.csv`)

2 apps de **1,75 MB** (OTA) + **LittleFS de 384 KB** (pets e último estado) + NVS em 0x9000 (calibração preservada).
O app usa ~1,63 MB (89%). Antes era `min_spiffs.csv` (apps de 1,9 MB, LittleFS de 128 KB, pequeno demais para 8 pets).
Trocar a partição exige gravar por USB e reformata o LittleFS (os pets são baixados de novo).

### NVS

| Namespace | Chaves |
|---|---|
| `cyd` | `rot`, `inv`, `rgb`, `cal1` (tela e toque), `pet` (pet escolhido, `"spr:<nome>"`), `led`, `autob` (modo noite), `nini`/`nfim` (horário da noite), `ldrd`/`ldrb` (calibração do LDR, sem uso) |
| `net` | `url`, `token` (backend) |

Chaves antigas `skin`, `skin2` e `sprn` só servem para migrar a escolha de versões anteriores.

### Console serial (115200)

| Comando | O que faz |
|---|---|
| `show` · `url <http://host:porta>` · `token <valor>` · `reboot` | configuração de rede (url/token gravam e reiniciam) |
| `apps` · `app <id>` · `acao <id>` | abre/fecha o menu de apps, troca de app, simula um botão |
| `pets` · `skin N` · `picker` | lista os pets, escolhe pelo número, abre/fecha o seletor |
| `mood sleeping\|idle\|working\|attention\|done\|error` | força um humor (até o backend mandar outro) |
| `cards` · `card N` | inicia o carrossel já / mostra o cartão N |
| `noite <início> <fim>` | horário do modo noite |
| `ldr` · `ldrcal <escuro> <claro>` | leitura/calibração do sensor de luz (inútil nesta placa) |
| `shot` | captura de tela (use `tools/screenshot.py`) |
| `uidbg` | posições da barra de status |

### Memória (sem PSRAM)

Log a cada 10 s: heap livre, mínimo, maior bloco, heap da LVGL (48 KB) e pilha da task do backend.
Medido: ~136 KB livres em operação, LVGL ~21–28%, pilha do backend ~4–7 KB livres de 8 KB.
O pet carregado ocupa ~1 KB por quadro; as miniaturas do seletor só existem com ele aberto.

---

## Primeira inicialização (portal)

Sem Wi-Fi salvo ou sem URL do backend, a placa abre o AP **`cyd-xxxx-setup`** com senha aleatória mostrada na tela
(texto + QR de Wi-Fi). No celular: conectar → a página abre sozinha (ou 192.168.4.1) → *Configure WiFi* → rede
2,4 GHz, senha, **URL do backend** (`http://192.168.1.10:8000`) e **token** → Save. A placa reinicia.

- Wi-Fi na NVS do ESP-IDF; URL/token na NVS `net`. Depois de 30 s offline, tenta reconectar.
- **BOOT segurado 5 s** = apaga Wi-Fi, URL, token e último estado e volta ao portal (barra na tela; soltar cancela).
- URL errada no portal: corrija pelo serial (`url http://192.168.1.10:8000`); a tela de boot mostra o erro e a URL.

## Protocolo com o backend

`GET {url}/device/{id}/screen?wait=25` com `Authorization: Bearer <token>` e `If-None-Match`. O backend segura até a
tela mudar (reação em < 1 s) e responde `200` com JSON (até 8 KB) ou `304`. URLs `https://` são recusadas (não precisa:
tudo na rede local).

| `layout` | Campos |
|---|---|
| `status` | `items: [{label, state: ok\|warn\|err\|off, value}]` (lista rolável, até 16) |
| `number` | `label`, `value` (fonte 48px), `sub`, `progress` 0..1, `color` `#RRGGBB` |
| `qr` | `qr` (texto do QR, ex. Pix copia-e-cola), `label`, `value`, `sub` |
| `message` | `text`, `icon: ok\|warn\|err\|info` |
| `pet` | `mood`, `bubble`, `usage` (linhas), `sub`, `cards` (carrossel), `sprites` (pets disponíveis); atualiza sem reconstruir a tela |

Campos comuns: `title` (barra de status), `refresh` (segundos entre consultas, além do long-poll), `app` e `apps`
(menu de apps) e `buttons` (ver [Interatividade](#interatividade-no-json)). Toques voltam por
`POST /device/{id}/action` (task própria na placa, não espera o long-poll).

```json
{"layout":"status","title":"Infra · EasyPanel","refresh":30,
 "items":[{"label":"postgres","state":"ok","value":"3 ms"},{"label":"n8n","state":"err","value":"FORA"}]}
```

**Ao ligar:** "Ligando…" → "Conectando ao Wi-Fi" → "Conectado à rede X, IP …" e "Buscando dados no PC…" (o IP fica
pelo menos 3 s na tela) → a primeira tela do backend. Se o PC não responder: "O PC não respondeu (motivo). Tentando de
novo…". A placa **não** mostra mais a última tela salva (antigo `/last.json`): ela trazia um estado velho, como o pet
"trabalhando", até o PC responder.

**Offline:** a tela atual continua, a barra mostra `⚠ offline HH:MM` e as tentativas seguem em 5→10→15 s (o PC volta →
a tela volta em até 15 s). No pet, depois de 45 s sem o PC ele **dorme** com o balão "Sem conexão com o PC" e sem as
linhas de uso (não fica congelado no último humor).

## Fontes PT-BR

`src/app/fonts/font_pt_{14,20,48}.c`: Montserrat Medium, ASCII + Latin-1 (á, ç, ã, õ, º…), pontuação tipográfica
(– — ' ' " " • … € ≈) e ícones (Wi-Fi, alerta, ok…) nas de 14/20. Geradas com `tools/gen_lvgl_font.py` (Pillow, sem Node):

```powershell
python tools/gen_lvgl_font.py --name font_pt_20 --size 20 --font .pio/libdeps/cyd2usb/lvgl/scripts/built_in_font/Montserrat-Medium.ttf 0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x20AC,0x2248 --font ".pio/libdeps/cyd2usb/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff" 0xF00C,0xF00D,0xF011,0xF013,0xF019,0xF021,0xF071,0xF093,0xF0F3,0xF1EB -o src/app/fonts/font_pt_20.c
```

Caractere fora dessas faixas aparece como quadradinho: adicione-o à lista e gere de novo.

## Ferramentas (`tools/`)

| Ferramenta | O que faz |
|---|---|
| `screenshot.py` | **captura a tela da placa** pelo serial e salva PNG (~20 s; use o Python do PlatformIO) |
| `serial_cmd.py` | envia comandos e lê o serial sem resetar a placa |
| `capturas_readme.py` | refaz as imagens do README (`docs/img/`) direto da placa |
| `pet_convert.py <nome> [--grade]` | converte um pet e gera a prévia |
| `pet_embed.py <nome>` | regrava o pet reserva embutido no firmware |
| `set_device_token.py` | grava o token do `.env` na placa pelo serial, sem exibir |
| `mock_backend.py` | backend falso (stdlib) que alterna os 4 layouts genéricos |
| `gen_lvgl_font.py` | gera as fontes LVGL |
| `ldr_watch.py` | acompanha o sensor de luz (diagnóstico) |

Dica: no Git Bash, rode as ferramentas de serial com `PYTHONIOENCODING=utf-8` (os ícones quebram o console cp1252).

---

## Problemas resolvidos

| Sintoma | Causa | Solução |
|---|---|---|
| Placa reiniciando em loop ao receber pets | LittleFS de 128 KB lotado (versões novas baixadas antes de apagar as antigas); a LittleFS divide por zero quando cheia | partição de 384 KB, apagar antes de baixar, conferir espaço livre |
| Clima "certificate has expired" | OpenSSL do Python não monta a cadeia nova da Let's Encrypt (YE1) | `truststore` (verificador do Windows; a verificação continua ligada) |
| % do plano não aparece | app desktop não executa a linha de status | métricas locais (contexto, prompts, tempo ativo) |
| Ícone Wi-Fi sumia da barra | flex com `END` + `SIZE_CONTENT` media só o último filho | `LV_FLEX_ALIGN_START` |
| Balão mostrava "Set-Location …" | comandos começando com `cd` | o backend remove `cd`/`Set-Location` e reduz caminho do executável |
| Quadradinho no lugar de "…" e "≈" | glifos fora da fonte | pontuação tipográfica nas fontes |
| `.bss` estourou | buffers de desenho estáticos + heap LVGL | buffers no heap |
| OTA "Host Not Found" | Windows não resolve mDNS | IP em `secrets.ini` |
| Desligar o backend demorava | uvicorn esperando os long-polls | `timeout_graceful_shutdown=2` |
| LDR sempre 0 | divisor abaixo do piso do ADC nesta placa | modo noite por horário |
| Tela nova demorava ~30 s após trocar de app | depois de cada tela a placa esperava o `refresh` dela antes de consultar de novo | depois de cada toque a placa consulta na hora |
| Backend levava ~57 s para subir | reconvertia os 8 pets a cada início | cache em disco por pet (`data/sprites_cache/`): ~8 s |
| Ao ligar, a placa mostrava o pet "trabalhando" de antes até o PC responder | restaurava a última tela salva | sequência de boot (Wi-Fi, IP, PC) e pet "sem conexão" após 45 s offline |
| Backend morreu em silêncio e a placa ficou sem dados | causa desconhecida (nada no log nem no Windows) | `serve.py` com guardião que reinicia o servidor |
| Pets da IA: tamanho variando, sorriso sumindo, pontinhos rosa | blocos irregulares, compressão WebP | ver [Conversão](#conversão-backendappspritespy) |

## Pendências e ideias

- Depois de reiniciar o backend, o pet só mostra a sessão no próximo evento (o estado fica em memória).
- EasyPanel: sem webhook configurado ainda (projeto 2).
- Próximos apps: **EasyPanel** (evoluir o Monitor com a API/webhook do EasyPanel), **Vendas** (evoluir o Contador
  com a API do marketplace) e **Pix** (layout `qr` com o copia-e-cola gerado no backend).
- Ideia: o pet pedir atenção mesmo com outro app na tela (ex. trocar para o pet sozinho em "precisa de você").
- **microSD não é usado.** Ideias: histórico de longo prazo, pets/imagens maiores, fotos.
- Raspberry Pi como backend sempre ligado, quando um projeto precisar.
- Firmware em 89% do slot de 1,75 MB: há folga, mas vigiar o crescimento.
