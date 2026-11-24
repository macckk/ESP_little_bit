"""Linha de status do Claude Code -> backend do CYD (so stdlib).

O Claude Code passa um JSON no stdin a cada atualizacao da linha de status. Daqui
saem para o backend local apenas numeros: limites do plano (5 h e semanal: % e
renovacao), uso da janela de contexto, modelo e id da sessao. Nunca falha: se o
backend estiver fora, so imprime a linha de status e sai.

Tambem grava o ultimo JSON recebido em backend/data/statusline_last.json (local,
para inspecao) e imprime uma linha curta para o proprio Claude Code exibir.
"""
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _token() -> str:
    tok = os.environ.get("CYD_HOOK_TOKEN")
    if tok:
        return tok
    try:
        for line in (ROOT / ".env").read_text(encoding="utf-8").splitlines():
            if line.strip().startswith("HOOK_TOKEN="):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def _num(v):
    return v if isinstance(v, (int, float)) else None


def slim(d: dict) -> dict:
    out = {"ts": time.time(), "session_id": d.get("session_id")}
    rl = d.get("rate_limits") or {}
    for key in ("five_hour", "seven_day"):
        w = rl.get(key)
        if isinstance(w, dict):
            out[key] = {"used_percentage": _num(w.get("used_percentage")), "resets_at": _num(w.get("resets_at"))}
    cw = d.get("context_window")
    if isinstance(cw, dict):
        out["context"] = {k: _num(v) for k, v in cw.items() if _num(v) is not None}
    model = d.get("model")
    if isinstance(model, dict):
        out["model"] = model.get("display_name") or model.get("id")
    return out


def line(s: dict) -> str:
    parts = []
    for key, name in (("five_hour", "5h"), ("seven_day", "sem")):
        p = (s.get(key) or {}).get("used_percentage")
        if p is not None:
            parts.append(f"{name} {p:.0f}%")
    return " · ".join(parts) or "Claude"


def main() -> None:
    raw = sys.stdin.buffer.read()
    try:
        d = json.loads(raw or b"{}")
    except ValueError:
        d = {}
    s = slim(d)
    try:
        (ROOT / "data").mkdir(exist_ok=True)
        (ROOT / "data" / "statusline_last.json").write_bytes(raw)
    except OSError:
        pass
    try:
        url = os.environ.get("CYD_BACKEND_URL", "http://127.0.0.1:8000").rstrip("/") + "/events/statusline"
        headers = {"Content-Type": "application/json"}
        tok = _token()
        if tok:
            headers["Authorization"] = f"Bearer {tok}"
        req = urllib.request.Request(url, data=json.dumps(s).encode(), headers=headers)
        urllib.request.urlopen(req, timeout=0.8).read()
    except Exception:
        pass
    print(line(s))


if __name__ == "__main__":
    main()
