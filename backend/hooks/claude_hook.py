"""Hook do Claude Code -> backend do CYD (so stdlib).

O Claude Code passa o evento em JSON no stdin. Enviamos uma versao ENXUTA (sem prompt,
sem conteudo de arquivo, comando cortado) e saimos sempre com codigo 0 em <= 1 s,
para nunca atrapalhar a sessao, mesmo com o backend fora do ar.

Config: CYD_BACKEND_URL (padrao http://127.0.0.1:8000) e CYD_HOOK_TOKEN
(se ausente, le HOOK_TOKEN de backend/.env).
"""
import json
import os
import sys
import time
import urllib.request
from pathlib import Path

KEEP = ("hook_event_name", "session_id", "cwd", "tool_name", "message", "source", "reason")
KEEP_INPUT = ("file_path", "notebook_path", "pattern")


def _token() -> str:
    tok = os.environ.get("CYD_HOOK_TOKEN")
    if tok:
        return tok
    env = Path(__file__).resolve().parent.parent / ".env"
    try:
        for line in env.read_text(encoding="utf-8").splitlines():
            if line.strip().startswith("HOOK_TOKEN="):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def slim(ev: dict) -> dict:
    out = {k: ev[k] for k in KEEP if k in ev}
    inp = ev.get("tool_input")
    if isinstance(inp, dict):
        small = {k: str(inp[k])[:300] for k in KEEP_INPUT if k in inp}
        if "command" in inp:  # so a primeira linha, curta
            small["command"] = str(inp["command"]).strip().splitlines()[0][:120] if inp["command"] else ""
        out["tool_input"] = small
    return out


def main() -> None:
    ts = time.time()  # momento do evento: o backend descarta eventos que chegam fora de ordem
    try:
        ev = json.loads(sys.stdin.buffer.read() or b"{}")
        out = slim(ev)
        out["ts"] = ts
        data = json.dumps(out).encode()
        url = os.environ.get("CYD_BACKEND_URL", "http://127.0.0.1:8000").rstrip("/") + "/events/claude"
        headers = {"Content-Type": "application/json"}
        tok = _token()
        if tok:
            headers["Authorization"] = f"Bearer {tok}"
        urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers), timeout=1.0).read()
    except Exception:
        pass


if __name__ == "__main__":
    main()
    sys.exit(0)
