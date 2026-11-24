"""Estado do pet de agentes: transforma eventos dos hooks do Claude Code em humor + balao.

Humores enviados a placa: sleeping, idle, working, attention, done (e error, reservado
para deploys). A animacao e local na placa; aqui so decidimos o estado.
"""
from __future__ import annotations

import ntpath
import posixpath
import re
import time
from dataclasses import dataclass
from typing import Callable

from . import config

PRIORITY = {"attention": 3, "working": 2, "done": 1, "idle": 0}

# Ferramentas em que o Claude para e espera a pessoa: o pet pede atencao.
ASKS_USER = {
    "AskUserQuestion": "Tenho uma pergunta pra você",
    "ExitPlanMode": "Plano pronto, quer revisar?",
}


@dataclass
class Session:
    id: str
    project: str
    state: str = "idle"  # idle | working | attention | done
    detail: str = ""
    updated: float = 0.0
    last_ts: float = 0.0  # horario (do hook) do ultimo evento aplicado


def _basename(path: str) -> str:
    path = (path or "").rstrip("/\\")
    return ntpath.basename(path) if "\\" in path or ":" in path else posixpath.basename(path)


def _trunc(text: str, n: int) -> str:
    text = " ".join((text or "").split())
    return text if len(text) <= n else text[: n - 1] + "…"


_CD_PREFIX = re.compile(r"^\s*(?:cd|Set-Location|sl|pushd)\s+(?:\"[^\"]*\"|'[^']*'|\S+)\s*(?:;|&&)\s*", re.I)


def _strip_cd(cmd: str) -> str:
    """Resume a linha de comando para o balao: tira trocas de pasta do inicio
    ("cd x && ...", "Set-Location x; ..."), fica so com o primeiro comando e troca
    caminho de programa pelo nome ("& 'C:\\...\\pio.exe' run" -> "pio run")."""
    prev = None
    while prev != cmd:
        prev, cmd = cmd, _CD_PREFIX.sub("", cmd, count=1)
    cmd = (cmd or prev).strip()
    cmd = re.split(r"\s*(?:;|&&|\|\||\|)\s*", cmd, maxsplit=1)[0] or cmd
    cmd = re.sub(r"^&\s*", "", cmd)
    m = re.match(r"^(\"[^\"]+\"|'[^']+'|\S+)(.*)$", cmd)
    if m and re.search(r"[\\/]", m.group(1)):
        exe = re.split(r"[\\/]", m.group(1).strip("\"'"))[-1]
        cmd = re.sub(r"\.(exe|cmd|bat|ps1)$", "", exe, flags=re.I) + m.group(2)
    return cmd


def describe_tool(name: str, tool_input: dict | None) -> str:
    inp = tool_input or {}
    if name in ("Bash", "PowerShell"):
        cmd = (inp.get("command") or "").strip().splitlines()
        return "Rodando " + _trunc(_strip_cd(cmd[0]) if cmd else "comando", 32)
    if name in ("Edit", "MultiEdit", "Write", "NotebookEdit"):
        return "Editando " + _trunc(_basename(inp.get("file_path") or inp.get("notebook_path") or ""), 28)
    if name == "Read":
        return "Lendo " + _trunc(_basename(inp.get("file_path") or ""), 30)
    if name in ("Grep", "Glob"):
        return "Procurando " + _trunc(inp.get("pattern") or "", 26)
    if name in ("WebFetch", "WebSearch"):
        return "Pesquisando na web"
    if name in ("Task", "Agent"):
        return "Chamando um ajudante"
    if name == "TodoWrite" or "plan" in name.lower():
        return "Organizando as tarefas"
    if name.startswith("mcp__"):
        return "Usando " + _trunc(name.split("__")[-1], 28)
    return "Usando " + _trunc(name, 30)


def translate_notification(message: str) -> str:
    m = (message or "").lower()
    if "permission" in m:
        return "Preciso da sua permissão!"
    if "waiting for your input" in m or "waiting for input" in m:
        return "Estou esperando você"
    return _trunc(message or "Preciso de você!", 60)


class PetState:
    def __init__(self, now: Callable[[], float] = time.time):
        self.now = now
        self.sessions: dict[str, Session] = {}
        self.last_activity = 0.0

    # ------------------------------------------------------------ eventos
    def handle(self, ev: dict) -> bool:
        """Aplica um evento de hook. Retorna False se o evento for ignorado."""
        name = ev.get("hook_event_name")
        sid = str(ev.get("session_id") or "?")
        cwd = ev.get("cwd") or ""
        t = self.now()

        if name == "SessionEnd":
            self.sessions.pop(sid, None)
            self.last_activity = t
            return True

        s = self.sessions.get(sid) or Session(id=sid, project=_basename(cwd) or "?")
        # hooks assincronos podem chegar fora de ordem: ignora o que for mais antigo
        ts = ev.get("ts")
        if isinstance(ts, (int, float)):
            if ts < s.last_ts:
                return False
            s.last_ts = ts
        if cwd:
            s.project = _basename(cwd) or s.project

        if name == "SessionStart":
            s.state, s.detail = "idle", "Sessão iniciada"
        elif name == "UserPromptSubmit":
            s.state, s.detail = "working", "Pensando…"
        elif name == "PreToolUse" and ev.get("tool_name") in ASKS_USER:
            s.state, s.detail = "attention", ASKS_USER[ev["tool_name"]]
        elif name == "PreToolUse":
            s.state, s.detail = "working", describe_tool(ev.get("tool_name") or "?", ev.get("tool_input"))
        elif name == "PostToolUse":
            if s.state == "attention":  # permissao concedida, ferramenta rodou
                s.detail = "Continuando…"
            s.state = "working"
        elif name == "Notification":
            s.state, s.detail = "attention", translate_notification(ev.get("message") or "")
        elif name == "Stop":
            s.state, s.detail = "done", "Terminei!"
        elif name == "SubagentStop":
            pass
        else:
            return False

        s.updated = t
        self.sessions[sid] = s
        self.last_activity = t
        return True

    # --------------------------------------------------------- tempo passa
    def expire(self) -> None:
        t = self.now()
        for sid, s in list(self.sessions.items()):
            age = t - s.updated
            if age > config.SESSION_TTL_SECONDS:
                del self.sessions[sid]
            elif s.state == "done" and age > config.DONE_SECONDS:
                s.state, s.detail = "idle", "Pronto para a próxima"
            elif s.state == "working" and age > config.STALE_WORK_SECONDS:
                s.state, s.detail = "idle", "Sem notícias da sessão"

    def top_session_id(self) -> str | None:
        """Sessao que o pet esta mostrando (a mais urgente/recente)."""
        if not self.sessions:
            return None
        return max(self.sessions.values(), key=lambda s: (PRIORITY[s.state], s.updated)).id

    # -------------------------------------------------------------- tela
    def screen(self) -> dict:
        self.expire()
        t = self.now()
        sessions = sorted(self.sessions.values(), key=lambda s: (PRIORITY[s.state], s.updated), reverse=True)
        busy = sum(1 for s in sessions if s.state in ("working", "attention"))

        if not sessions or (busy == 0 and all(s.state == "idle" for s in sessions)
                            and t - self.last_activity > config.SLEEP_SECONDS):
            mood, bubble, sub = "sleeping", "zZz…", ""
        else:
            top = sessions[0]
            mood = top.state
            bubble = top.detail or ("Tudo tranquilo" if mood == "idle" else "")
            # rodape so aparece com mais de uma sessao (o nome do projeto nao vai para a tela)
            sub = f"{len(sessions)} sessões" if len(sessions) > 1 else ""
        return {
            "layout": "pet",
            "title": config.PET_TITLE,
            "refresh": 1,
            "mood": mood,
            "bubble": bubble,
            "sub": sub,
            "sessions": len(sessions),
            "busy": busy,
        }
