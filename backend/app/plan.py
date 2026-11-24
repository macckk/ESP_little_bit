"""Limites do plano Claude (janela de 5 h e semanal) e contexto das sessoes,
recebidos da linha de status do Claude Code (hooks/statusline.py)."""
from __future__ import annotations

import time
from datetime import datetime

WEEKDAYS = ["seg", "ter", "qua", "qui", "sex", "sáb", "dom"]
STALE_SECONDS = 6 * 3600  # sem atualizacao ha mais que isso: nao confia mais


class PlanState:
    def __init__(self):
        self.windows: dict[str, dict] = {}   # "five_hour"/"seven_day" -> {used_percentage, resets_at}
        self.received_at = 0.0
        self.context: dict[str, dict] = {}   # session_id -> {"pct": float, "ts": float}
        self.model: dict[str, str] = {}

    def update(self, s: dict, now: float | None = None) -> None:
        now = now or time.time()
        got = False
        for key in ("five_hour", "seven_day"):
            w = s.get(key)
            if isinstance(w, dict) and isinstance(w.get("used_percentage"), (int, float)):
                self.windows[key] = {"used_percentage": float(w["used_percentage"]),
                                     "resets_at": w.get("resets_at")}
                got = True
        if got:
            self.received_at = now
        sid = s.get("session_id")
        ctx = s.get("context") or {}
        pct = ctx.get("used_percentage")
        if sid and isinstance(pct, (int, float)):
            self.context[sid] = {"pct": float(pct), "ts": now}
        if sid and s.get("model"):
            self.model[sid] = str(s["model"])

    def has_data(self, now: float | None = None) -> bool:
        now = now or time.time()
        return bool(self.windows) and now - self.received_at < STALE_SECONDS

    def window(self, key: str, now: float | None = None) -> dict | None:
        """Janela valida; se ja renovou, considera 0% ate chegar dado novo."""
        now = now or time.time()
        w = self.windows.get(key)
        if not w:
            return None
        r = w.get("resets_at")
        if isinstance(r, (int, float)) and r <= now:
            return {"used_percentage": 0.0, "resets_at": None}
        return w

    def summary(self, now: float | None = None) -> dict:
        now = now or time.time()
        return {k: self.window(k, now) for k in ("five_hour", "seven_day")} | {
            "age_s": round(now - self.received_at) if self.received_at else None}


def fmt_reset(epoch, now: float | None = None) -> str:
    if not isinstance(epoch, (int, float)):
        return ""
    now = now or time.time()
    dt = datetime.fromtimestamp(epoch)
    if epoch - now < 20 * 3600:
        return dt.strftime("%H:%M")
    return f"{WEEKDAYS[dt.weekday()]} {dt:%H:%M}"


def plan_lines(plan: PlanState, session_id: str | None, now: float | None = None) -> list[str]:
    now = now or time.time()
    lines = []
    for key, name in (("five_hour", "5 h"), ("seven_day", "Semana")):
        w = plan.window(key, now)
        if w is None:
            continue
        reset = fmt_reset(w.get("resets_at"), now)
        lines.append(f"{name} {w['used_percentage']:.0f}%" + (f" · {reset}" if reset else ""))
    ctx = plan.context.get(session_id or "")
    if ctx and now - ctx["ts"] < STALE_SECONDS:
        lines.append(f"Contexto {ctx['pct']:.0f}%")
    return lines
