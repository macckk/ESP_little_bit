"""Apps disponiveis na placa. Ordem e configuracao em backend/apps.json."""
from __future__ import annotations

import json
from pathlib import Path

from .base import App
from .counter import CounterApp
from .monitor import MonitorApp
from .pet_app import PetApp

# id -> classe. App novo: importar aqui e acrescentar.
CATALOG: dict[str, type[App]] = {
    PetApp.id: PetApp,
    MonitorApp.id: MonitorApp,
    CounterApp.id: CounterApp,
}


def load_config(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}


def build_apps(cfg: dict) -> dict[str, App]:
    """Instancia os apps na ordem de cfg["apps"] (padrao: todos do catalogo)."""
    order = [a for a in cfg.get("apps", list(CATALOG)) if a in CATALOG]
    return {a: CATALOG[a](cfg.get(a, {})) for a in order}
