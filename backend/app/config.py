"""Configuracao lida de variaveis de ambiente ou de backend/.env (que fica fora do git)."""
import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent


def _load_dotenv() -> None:
    path = BASE_DIR / ".env"
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip())


_load_dotenv()

# Token que os hooks do Claude Code mandam (Authorization: Bearer ...). Vazio = sem checagem.
HOOK_TOKEN = os.environ.get("HOOK_TOKEN", "")
# Token que a placa manda. Vazio = sem checagem.
DEVICE_TOKEN = os.environ.get("DEVICE_TOKEN", "")
DB_PATH = Path(os.environ.get("DB_PATH", BASE_DIR / "data" / "events.db"))
# Pets em pixel art (PNG/WebP); convertidos sozinhos e baixados pela placa
PETS_DIR = Path(os.environ.get("PETS_DIR", BASE_DIR.parent / "pets"))

# Apps da placa: ordem, app padrao e configuracao de cada um (ver app/apps/)
APPS_CONFIG = Path(os.environ.get("APPS_CONFIG", BASE_DIR / "apps.json"))
DEVICES_PATH = Path(os.environ.get("DEVICES_PATH", BASE_DIR / "data" / "devices.json"))
# Nome do pet de agentes (titulo na barra e no menu de apps)
PET_TITLE = os.environ.get("PET_TITLE", "Little Bit Pixel")

# Clima (Open-Meteo): cidade e estado. Vazio = sem cartao de clima.
WEATHER_CITY = os.environ.get("WEATHER_CITY", "São Paulo")
WEATHER_STATE = os.environ.get("WEATHER_STATE", "São Paulo")

# Tempos do pet (segundos)
DONE_SECONDS = int(os.environ.get("PET_DONE_SECONDS", 120))      # comemora apos terminar
SLEEP_SECONDS = int(os.environ.get("PET_SLEEP_SECONDS", 600))    # sem atividade -> dorme
STALE_WORK_SECONDS = int(os.environ.get("PET_STALE_WORK_SECONDS", 900))  # "trabalhando" sem eventos
SESSION_TTL_SECONDS = int(os.environ.get("PET_SESSION_TTL_SECONDS", 6 * 3600))
