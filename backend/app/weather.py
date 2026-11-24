"""Clima via Open-Meteo (gratuito, sem cadastro). So o nome da cidade/estado sai do PC.

A localizacao (lat/lon) e buscada uma vez e guardada em data/geo.json; a previsao e
atualizada a cada 30 min. Codigos de tempo WMO -> descricao PT-BR + icone simples.
"""
from __future__ import annotations

import json
import logging
import ssl
import time
import urllib.parse
import urllib.request

from . import config

log = logging.getLogger("cyd.weather")
REFRESH_SECONDS = 30 * 60
GEO_FILE = config.BASE_DIR / "data" / "geo.json"

# (descricao, icone) por codigo WMO
WMO = {
    0: ("Céu limpo", "sun"), 1: ("Poucas nuvens", "partly"), 2: ("Parcialmente nublado", "partly"),
    3: ("Nublado", "cloud"), 45: ("Neblina", "fog"), 48: ("Neblina", "fog"),
    51: ("Garoa fraca", "rain"), 53: ("Garoa", "rain"), 55: ("Garoa forte", "rain"),
    56: ("Garoa gelada", "rain"), 57: ("Garoa gelada", "rain"),
    61: ("Chuva fraca", "rain"), 63: ("Chuva", "rain"), 65: ("Chuva forte", "rain"),
    66: ("Chuva gelada", "rain"), 67: ("Chuva gelada", "rain"),
    71: ("Neve fraca", "snow"), 73: ("Neve", "snow"), 75: ("Neve forte", "snow"), 77: ("Neve", "snow"),
    80: ("Pancadas de chuva", "rain"), 81: ("Pancadas de chuva", "rain"), 82: ("Pancadas fortes", "rain"),
    85: ("Neve", "snow"), 86: ("Neve", "snow"),
    95: ("Tempestade", "storm"), 96: ("Tempestade com granizo", "storm"), 99: ("Tempestade com granizo", "storm"),
}


def describe(code) -> tuple[str, str]:
    return WMO.get(int(code) if isinstance(code, (int, float)) else -1, ("—", "cloud"))


def _ssl_context() -> ssl.SSLContext:
    # Verificador do proprio Windows (truststore): o OpenSSL do Python montava a cadeia nova
    # da Let's Encrypt (emissor YE1) por um intermediario expirado e recusava api.open-meteo.com.
    try:
        import truststore
        return truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    except ImportError:
        return ssl.create_default_context()


_CTX = _ssl_context()


def _get(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "cyd-desk-display/1.0"})
    with urllib.request.urlopen(req, timeout=10, context=_CTX) as r:
        return json.loads(r.read())


class Weather:
    def __init__(self, city: str, state: str):
        self.city, self.state = city, state
        self.geo: dict | None = None
        self.data: dict | None = None
        self.fetched_at = 0.0

    def _geocode(self) -> dict | None:
        if self.geo:
            return self.geo
        try:
            cached = json.loads(GEO_FILE.read_text(encoding="utf-8"))
            if cached.get("query") == [self.city, self.state]:
                self.geo = cached
                return self.geo
        except (OSError, ValueError):
            pass
        q = urllib.parse.urlencode({"name": self.city, "count": 10, "language": "pt", "format": "json"})
        res = _get(f"https://geocoding-api.open-meteo.com/v1/search?{q}").get("results") or []
        res = [r for r in res if not self.state or self.state.lower() in (r.get("admin1") or "").lower()] or res
        if not res:
            log.warning("cidade nao encontrada: %s / %s", self.city, self.state)
            return None
        r = res[0]
        self.geo = {"query": [self.city, self.state], "name": r["name"],
                    "lat": r["latitude"], "lon": r["longitude"]}
        GEO_FILE.parent.mkdir(parents=True, exist_ok=True)
        GEO_FILE.write_text(json.dumps(self.geo, ensure_ascii=False), encoding="utf-8")
        return self.geo

    def refresh(self, force: bool = False) -> None:
        """Bloqueante (rodar em thread). Erros so viram log: o cartao some ate voltar."""
        if not self.city or (not force and time.time() - self.fetched_at < REFRESH_SECONDS):
            return
        self.fetched_at = time.time()
        try:
            g = self._geocode()
            if not g:
                return
            q = urllib.parse.urlencode({
                "latitude": g["lat"], "longitude": g["lon"],
                "current": "temperature_2m,relative_humidity_2m,weather_code,is_day",
                "daily": "temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max",
                "timezone": "America/Sao_Paulo", "forecast_days": 2,
            })
            self.data = _get(f"https://api.open-meteo.com/v1/forecast?{q}")
        except Exception as e:  # rede fora, API fora, etc.
            log.warning("clima indisponivel: %s", e)
            self.fetched_at = time.time() - REFRESH_SECONDS + 300  # tenta de novo em 5 min

    def card(self) -> dict | None:
        d = self.data
        if not d or not self.geo or time.time() - self.fetched_at > 3 * 3600:
            return None
        cur, daily = d.get("current") or {}, d.get("daily") or {}
        desc, icon = describe(cur.get("weather_code"))

        def day(i, key):
            v = (daily.get(key) or [None, None])
            return v[i] if i < len(v) else None

        card = {
            "type": "weather",
            "city": self.geo["name"],
            "temp": f"{round(cur['temperature_2m'])}°" if isinstance(cur.get("temperature_2m"), (int, float)) else "--",
            "desc": desc,
            "icon": icon,
            "night": cur.get("is_day") == 0,
            "lines": [],
        }
        tmax, tmin, rain = day(0, "temperature_2m_max"), day(0, "temperature_2m_min"), day(0, "precipitation_probability_max")
        if tmax is not None and tmin is not None:
            card["lines"].append(f"Máx {round(tmax)}° · Mín {round(tmin)}°")
        if rain is not None:
            card["lines"].append(f"Chuva {rain}% · Umidade {cur.get('relative_humidity_2m', '--')}%")
        t_desc, _ = describe(day(1, "weather_code"))
        t_max, t_min = day(1, "temperature_2m_max"), day(1, "temperature_2m_min")
        if t_max is not None and t_min is not None:
            card["lines"].append(f"Amanhã: {t_desc.lower()} · {round(t_min)}–{round(t_max)}°")
        return card
