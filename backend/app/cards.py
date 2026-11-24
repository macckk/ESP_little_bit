"""Cartoes que a placa alterna enquanto o pet dorme (o carrossel roda na placa).

Tipos: clock (desenhado pela placa com o NTP), weather, today, week.
"""
from __future__ import annotations

from .usage import fmt_duration

WEEKDAYS = ["seg", "ter", "qua", "qui", "sex", "sáb", "dom"]


def today_card(stats: dict, active_s: int) -> dict:
    def plural(n, one, many):
        return one if n == 1 else many

    return {
        "type": "today",
        "title": "Hoje com o Claude",
        "items": [
            {"v": str(stats["prompts"]), "l": plural(stats["prompts"], "prompt", "prompts")},
            {"v": fmt_duration(active_s), "l": "ativo"},
            {"v": str(stats["edits"]), "l": plural(stats["edits"], "edição", "edições")},
            {"v": str(stats["commands"]), "l": plural(stats["commands"], "comando", "comandos")},
        ],
    }


def week_card(active_by_day: list) -> dict:
    days = []
    for dt, secs in active_by_day:
        minutes = secs // 60
        days.append({"d": WEEKDAYS[dt.weekday()], "v": minutes,
                     "t": "" if minutes == 0 else fmt_duration(secs).replace(" min", "m")})
    total = sum(s for _, s in active_by_day)
    return {"type": "week", "title": f"Tempo ativo · 7 dias ({fmt_duration(total)})", "days": days}


def build_cards(weather_card: dict | None, stats: dict, active_by_day: list) -> list[dict]:
    cards = [{"type": "clock"}]
    if weather_card:
        cards.append(weather_card)
    cards.append(today_card(stats, active_by_day[-1][1] if active_by_day else 0))
    cards.append(week_card(active_by_day))
    return cards
