"""App Contador: numero grande com botoes na placa (layout "number"). Semente do contador de vendas.

Configuracao em backend/apps.json: "contador": {"title": "Contador", "label": "Hoje", "goal": 0}
(goal > 0 mostra barra de progresso). O valor fica em data/apps_state.json e zera a cada dia.
"""
from __future__ import annotations

import json
import time
from pathlib import Path

from .. import config
from .base import App


class CounterApp(App):
    id = "contador"
    name = "Contador"

    def __init__(self, cfg: dict | None = None, state_path: Path | None = None):
        super().__init__(cfg)
        self.name = self.cfg.get("title", "Contador")
        self.label = self.cfg.get("label", "Hoje")
        self.goal = int(self.cfg.get("goal", 0))
        self.path = state_path or config.DB_PATH.parent / "apps_state.json"
        self.value, self.day = 0, time.strftime("%Y-%m-%d")
        try:
            st = json.loads(self.path.read_text(encoding="utf-8")).get(self.id, {})
            if st.get("day") == self.day:
                self.value = int(st.get("value", 0))
        except (OSError, ValueError):
            pass

    def _save(self) -> None:
        try:
            all_ = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            all_ = {}
        all_[self.id] = {"day": self.day, "value": self.value}
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(all_), encoding="utf-8")

    def _rollover(self) -> None:
        today = time.strftime("%Y-%m-%d")
        if today != self.day:
            self.day, self.value = today, 0

    def action(self, device_id: str, action: str, value: str) -> bool:
        self._rollover()
        if action == "inc":
            self.value += 1
        elif action == "dec":
            self.value = max(0, self.value - 1)
        elif action == "reset":
            self.value = 0
        else:
            return False
        self._save()
        return True

    def screen(self) -> dict:
        self._rollover()
        s = {"layout": "number", "title": self.name, "refresh": 60, "label": self.label, "value": str(self.value),
             "sub": time.strftime("%d/%m"),
             "buttons": [{"id": "dec", "label": "-1"}, {"id": "reset", "label": "Zerar"},
                         {"id": "inc", "label": "+1"}]}
        if self.goal > 0:
            s["progress"] = min(1.0, self.value / self.goal)
            s["sub"] = f"meta {self.goal}"
        return s

    def summary(self) -> dict:
        return {"valor": self.value, "dia": self.day}
