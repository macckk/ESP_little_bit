"""App Monitor: lista de servicos com bolinha verde/amarela/vermelha (layout "status").

Semente do painel EasyPanel. Configuracao em backend/apps.json:
  "monitor": {"title": "Serviços", "interval": 60, "slow_ms": 1500,
              "checks": [{"name": "Backend", "url": "http://127.0.0.1:8000/health"}, ...]}
Botao "Atualizar" na placa refaz as checagens na hora.
"""
from __future__ import annotations

import ssl
import time
import urllib.error
import urllib.request

import truststore

from .base import App

_CTX = truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)


def check(url: str, timeout: float = 4.0) -> tuple[str, str]:
    """(state, value) de uma URL: ok + latencia, err + motivo."""
    t0 = time.monotonic()
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "cyd-monitor"})
        with urllib.request.urlopen(req, timeout=timeout, context=_CTX if url.startswith("https") else None) as r:
            r.read(256)
            code = r.status
    except urllib.error.HTTPError as e:
        code = e.code
    except Exception:  # timeout, DNS, recusado...
        return "err", "FORA"
    ms = int((time.monotonic() - t0) * 1000)
    if code >= 500:
        return "err", f"HTTP {code}"
    if code >= 400:
        return "warn", f"HTTP {code}"
    return "ok", f"{ms} ms"


class MonitorApp(App):
    id = "monitor"
    name = "Monitor"

    def __init__(self, cfg: dict | None = None):
        super().__init__(cfg)
        self.name = self.cfg.get("title", "Serviços")
        self.checks = [c for c in self.cfg.get("checks", []) if c.get("name") and c.get("url")]
        self.interval = int(self.cfg.get("interval", 60))
        self.slow_ms = int(self.cfg.get("slow_ms", 1500))
        self.results: dict[str, tuple[str, str]] = {}
        self.last_run = 0.0

    def run_checks(self) -> None:
        res = {}
        for c in self.checks:
            state, value = check(c["url"])
            if state == "ok" and int(value.split()[0]) > self.slow_ms:
                state = "warn"  # respondeu, mas devagar
            res[c["name"]] = (state, value)
        self.results = res
        self.last_run = time.time()

    def refresh(self) -> None:
        if time.time() - self.last_run >= self.interval:
            self.run_checks()

    def action(self, device_id: str, action: str, value: str) -> bool:
        if action == "refresh":
            self.run_checks()
            return True
        return False

    def screen(self) -> dict:
        items = []
        for c in self.checks:
            state, value = self.results.get(c["name"], ("off", "…"))
            items.append({"label": c["name"], "state": state, "value": value})
        if not items:
            return {"layout": "message", "title": self.name, "icon": "info",
                    "text": "Nenhum serviço configurado (backend/apps.json)"}
        hora = time.strftime("%H:%M", time.localtime(self.last_run)) if self.last_run else "--:--"
        return {"layout": "status", "title": f"{self.name} · {hora}", "refresh": 30, "items": items,
                "buttons": [{"id": "refresh", "label": "Atualizar"}]}

    def summary(self) -> dict:
        return {"ultima": self.last_run, "resultados": self.results}
