"""App do pet de agentes: reage ao Claude Code (hooks), uso, carrossel e pets em pixel art."""
from __future__ import annotations

import time
from datetime import datetime

from .. import config
from ..cards import build_cards
from ..pet import PetState
from ..plan import PlanState, plan_lines
from ..sprites import SpriteLibrary
from ..store import Store
from ..usage import UsageTracker, local_lines
from ..weather import Weather
from .base import App


def _midnight(now: float) -> float:
    return datetime.fromtimestamp(now).replace(hour=0, minute=0, second=0, microsecond=0).timestamp()


class PetApp(App):
    id = "pet"

    def __init__(self, cfg: dict | None = None):
        super().__init__(cfg)
        self.name = config.PET_TITLE
        self.pet = PetState()
        self.plan = PlanState()
        self.weather = Weather(config.WEATHER_CITY, config.WEATHER_STATE)
        self.store = Store(config.DB_PATH)
        self.usage = UsageTracker()
        self.sprites = SpriteLibrary(config.PETS_DIR, config.DB_PATH.parent / "sprites_cache")

    # --- eventos do Claude Code (endpoints em main.py)
    def handle_event(self, ev: dict) -> bool:
        if not self.pet.handle(ev):
            return False
        s = self.pet.sessions.get(str(ev.get("session_id") or "?"))
        self.store.add(ev, s.project if s else "", s.detail if s else "")
        return True

    def handle_statusline(self, s: dict) -> None:
        self.plan.update(s)

    # --- App
    def refresh(self) -> None:
        self.usage.scan()
        self.weather.refresh()  # so busca de fato a cada 30 min
        self.sprites.refresh()  # converte pets novos/alterados em pets/

    def screen(self) -> dict:
        screen = self.pet.screen()
        screen["title"] = config.PET_TITLE
        sid = self.pet.top_session_id()
        now = time.time()
        midnight = _midnight(now)
        # % do plano quando o Claude Code de terminal manda (linha de status); senao, metricas locais
        if self.plan.has_data():
            screen["usage"] = plan_lines(self.plan, sid)
        else:
            screen["usage"] = local_lines(
                self.usage.context_pct(sid),
                self.store.count_since("UserPromptSubmit", now - 5 * 3600),
                self.store.count_since("UserPromptSubmit", midnight),
                self.usage.active_seconds_today(now),
            )
        # carrossel: vai sempre (fica atualizado); a placa so exibe quando o pet dorme
        screen["cards"] = build_cards(self.weather.card(), self.store.day_stats(midnight),
                                      self.usage.active_by_day(7, now))
        screen["sprites"] = self.sprites.listing()  # a placa baixa o que faltar
        return screen

    def summary(self) -> dict:
        return {
            "sessions": len(self.pet.sessions),
            "usage": self.usage.summary(self.pet.top_session_id()),
            "plan": self.plan.summary(),
            "sprites": self.sprites.summary(),
        }
