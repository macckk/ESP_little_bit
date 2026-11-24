"""Uso do Claude Code lido dos transcripts locais (~/.claude/projects/**/*.jsonl).

Soma o custo equivalente da API (US$) por resposta, sem repetir a mesma resposta
(o transcript grava uma copia por bloco). Leitura incremental: so o que foi
acrescentado desde a ultima varredura. Nada sai do PC; so numeros vao para a placa.

Validado contra o "totalCostUSD" do proprio Claude Code: fica ~6% abaixo (chamadas
internas que nao aparecem no transcript), por isso a tela mostra "≈".
"""
from __future__ import annotations

import json
import os
import re
import threading
import time
from datetime import datetime
from pathlib import Path

# US$ por milhao de tokens: (entrada, saida, leitura de cache).
# Escrita de cache = 1,25x a entrada (5 min) ou 2x (1 h). Fonte: tabela de precos da API (2026-11).
PRICES = {
    "claude-fable-5-2": (8.0, 40.0, 0.20),
    "claude-mythos-5-2": (8.0, 40.0, 0.20),
    "claude-fable-5-1": (10.0, 50.0, 1.00),
    "claude-fable-5": (10.0, 50.0, 1.00),
    "claude-opus-5-5": (4.0, 20.0, 0.20),
    "claude-opus-5": (5.0, 25.0, 0.50),
    "claude-opus-4-8": (5.0, 25.0, 0.50),
    "claude-opus-4-7": (5.0, 25.0, 0.50),
    "claude-opus-4-6": (5.0, 25.0, 0.50),
    "claude-sonnet-5-5": (2.0, 10.0, 0.10),
    "claude-sonnet-5": (2.0, 10.0, 0.20),
    "claude-sonnet-4-6": (3.0, 15.0, 0.30),
    "claude-haiku-5-5": (0.10, 0.50, 0.01),
    "claude-haiku-4-5": (1.0, 5.0, 0.10),
}
KEEP_HOURS = 8 * 24  # guarda 8 dias: "hoje", "ultimas 5 h" e o grafico de 7 dias
ACTIVE_GAP = 300  # intervalos maiores que 5 min entre respostas nao contam como tempo ativo

# Janela de contexto por modelo (tokens). Modelos atuais: 1M; mais antigos: 200K.
CONTEXT_WINDOW = {"claude-haiku-4-5": 200_000}
DEFAULT_CONTEXT_WINDOW = 1_000_000


def context_window_for(model: str | None) -> int:
    m = re.sub(r"-\d{8}$", "", (model or "").lower()).split("[")[0]
    return CONTEXT_WINDOW.get(m, DEFAULT_CONTEXT_WINDOW)


def price_for(model: str | None):
    if not model:
        return None
    m = re.sub(r"-\d{8}$", "", model.lower())  # tira sufixo de data
    m = m.split("[")[0]
    if m in PRICES:
        return PRICES[m]
    for key in sorted(PRICES, key=len, reverse=True):
        if m.startswith(key):
            return PRICES[key]
    return None


def response_cost(usage: dict, model: str | None) -> float:
    p = price_for(model)
    if not p:
        return 0.0
    inp, out, read = p
    cc = usage.get("cache_creation") or {}
    if cc:
        w5, w1 = cc.get("ephemeral_5m_input_tokens", 0) or 0, cc.get("ephemeral_1h_input_tokens", 0) or 0
    else:
        w5, w1 = usage.get("cache_creation_input_tokens", 0) or 0, 0
    return (
        (usage.get("input_tokens", 0) or 0) * inp
        + (usage.get("output_tokens", 0) or 0) * out
        + (usage.get("cache_read_input_tokens", 0) or 0) * read
        + w5 * inp * 1.25
        + w1 * inp * 2.0
    ) / 1e6


def _iso_to_epoch(ts: str | None) -> float | None:
    if not ts:
        return None
    try:
        return datetime.fromisoformat(ts.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None


class UsageTracker:
    def __init__(self, root: Path | None = None):
        self.root = root or Path(os.environ.get("CLAUDE_PROJECTS_DIR", Path.home() / ".claude" / "projects"))
        self._offsets: dict[Path, int] = {}
        self._records: dict[str, tuple[float, str, float]] = {}  # id -> (epoch, session, custo)
        # sessao -> (epoch, tokens de entrada da ultima resposta, modelo): tamanho atual do contexto.
        # Conferido contra o app: 616.333 (transcript) x 616.802 (app) tokens.
        self._context: dict[str, tuple[float, int, str | None]] = {}
        self._lock = threading.Lock()

    def scan(self) -> None:
        if not self.root.exists():
            return
        cutoff = time.time() - KEEP_HOURS * 3600
        for path in self.root.rglob("*.jsonl"):
            try:
                st = path.stat()
            except OSError:
                continue
            if st.st_mtime < cutoff:
                continue
            start = self._offsets.get(path, 0)
            if st.st_size < start:  # arquivo truncado/reescrito
                start = 0
            if st.st_size == start:
                continue
            with open(path, "rb") as f:
                f.seek(start)
                data = f.read()
            end = data.rfind(b"\n")
            if end < 0:
                continue  # linha ainda incompleta
            self._offsets[path] = start + end + 1
            self._ingest(data[: end + 1], cutoff)
        with self._lock:
            for k in [k for k, r in self._records.items() if r[0] < cutoff]:
                del self._records[k]

    def _ingest(self, chunk: bytes, cutoff: float) -> None:
        new = {}
        ctx = {}
        for line in chunk.splitlines():
            if b'"usage"' not in line:
                continue
            try:
                e = json.loads(line)
            except ValueError:
                continue
            m = e.get("message")
            if e.get("type") != "assistant" or not isinstance(m, dict) or not m.get("usage"):
                continue
            ts = _iso_to_epoch(e.get("timestamp"))
            if ts is None or ts < cutoff:
                continue
            key = m.get("id") or e.get("requestId") or e.get("uuid")
            sid = str(e.get("sessionId") or "")
            u = m["usage"]
            new[key] = (ts, sid, response_cost(u, m.get("model")))
            if not e.get("isSidechain"):  # subagentes tem contexto proprio
                tokens = sum(int(u.get(k, 0) or 0) for k in
                             ("input_tokens", "cache_read_input_tokens", "cache_creation_input_tokens"))
                if sid not in ctx or ts >= ctx[sid][0]:
                    ctx[sid] = (ts, tokens, m.get("model"))
        with self._lock:
            self._records.update(new)
            for sid, c in ctx.items():
                if sid not in self._context or c[0] >= self._context[sid][0]:
                    self._context[sid] = c

    def context_pct(self, session_id: str | None, now: float | None = None) -> float | None:
        """% de contexto da sessao; sem sessao conhecida, a mais recente da ultima hora."""
        now = now or time.time()
        with self._lock:
            c = self._context.get(session_id or "")
            if c is None and not session_id and self._context:
                recent = max(self._context.values(), key=lambda v: v[0])
                c = recent if now - recent[0] < 3600 else None
        if not c:
            return None
        return 100.0 * c[1] / context_window_for(c[2])

    def active_seconds_today(self, now: float | None = None) -> int:
        return self.active_by_day(1, now)[-1][1]

    def active_by_day(self, days: int = 7, now: float | None = None) -> list[tuple[datetime, int]]:
        """[(dia, segundos ativos)] dos ultimos `days` dias, do mais antigo para hoje."""
        now = now or time.time()
        today = datetime.fromtimestamp(now).replace(hour=0, minute=0, second=0, microsecond=0)
        starts = [today.timestamp() - 86400 * i for i in range(days - 1, -1, -1)]
        with self._lock:
            ts = sorted(t for t, _, _ in self._records.values() if t >= starts[0])
        out = []
        for i, start in enumerate(starts):
            end = starts[i + 1] if i + 1 < len(starts) else now + 1
            day_ts = [t for t in ts if start <= t < end]
            out.append((datetime.fromtimestamp(start), int(sum(min(b - a, ACTIVE_GAP)
                                                               for a, b in zip(day_ts, day_ts[1:])))))
        return out

    def summary(self, session_id: str | None = None, now: float | None = None) -> dict:
        now = now or time.time()
        midnight = datetime.fromtimestamp(now).replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
        with self._lock:
            recs = list(self._records.values())
        return {
            "today": sum(c for t, _, c in recs if t >= midnight),
            "last5h": sum(c for t, _, c in recs if t >= now - 5 * 3600),
            "session": sum(c for _, s, c in recs if session_id and s == session_id),
            "responses_today": sum(1 for t, _, _ in recs if t >= midnight),
        }


def brl_number(v: float) -> str:
    """1234.5 -> '1.234,50'"""
    return f"{v:,.2f}".replace(",", "X").replace(".", ",").replace("X", ".")


def fmt_duration(seconds: int) -> str:
    h, m = divmod(max(0, seconds) // 60, 60)
    return f"{h}h{m:02d}" if h else f"{m} min"


def local_lines(context_pct: float | None, prompts_5h: int, prompts_today: int, active_s: int) -> list[str]:
    """Metricas medidas no PC (o app desktop nao expoe o % do plano)."""
    lines = []
    if context_pct is not None:
        lines.append(f"Contexto {context_pct:.0f}%")
    lines.append(f"5 h: {prompts_5h} prompt{'s' if prompts_5h != 1 else ''}")
    lines.append(f"Hoje: {prompts_today} prompt{'s' if prompts_today != 1 else ''}")
    lines.append(f"Ativo hoje: {fmt_duration(active_s)}")
    return lines


def usage_lines(s: dict, has_session: bool) -> list[str]:
    lines = [f"Hoje ≈ US$ {brl_number(s['today'])}", f"5 h ≈ US$ {brl_number(s['last5h'])}"]
    if has_session:
        lines.append(f"Sessão ≈ US$ {brl_number(s['session'])}")
    return lines
