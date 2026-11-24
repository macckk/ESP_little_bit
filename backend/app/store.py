"""Historico enxuto de eventos em SQLite (sem prompts nem conteudo de arquivos)."""
import sqlite3
import threading
import time
from pathlib import Path


class Store:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(path, check_same_thread=False)
        self._lock = threading.Lock()
        with self._lock:
            self._db.execute(
                "CREATE TABLE IF NOT EXISTS events ("
                " id INTEGER PRIMARY KEY, ts REAL, session TEXT, event TEXT, tool TEXT, project TEXT, detail TEXT)"
            )
            self._db.commit()

    def add(self, ev: dict, project: str, detail: str) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO events (ts, session, event, tool, project, detail) VALUES (?,?,?,?,?,?)",
                (time.time(), str(ev.get("session_id") or ""), ev.get("hook_event_name") or "",
                 ev.get("tool_name") or "", project, detail),
            )
            self._db.commit()

    def count_since(self, event: str, since: float) -> int:
        with self._lock:
            return self._db.execute("SELECT COUNT(*) FROM events WHERE event = ? AND ts >= ?",
                                    (event, since)).fetchone()[0]

    def day_stats(self, since: float) -> dict:
        """Contadores para o cartao "Hoje"."""
        edit_tools = ("Edit", "MultiEdit", "Write", "NotebookEdit")
        cmd_tools = ("Bash", "PowerShell")
        with self._lock:
            q = self._db.execute
            prompts = q("SELECT COUNT(*) FROM events WHERE event='UserPromptSubmit' AND ts>=?", (since,)).fetchone()[0]
            edits = q(f"SELECT COUNT(*) FROM events WHERE event='PreToolUse' AND ts>=? AND tool IN "
                      f"({','.join('?' * len(edit_tools))})", (since, *edit_tools)).fetchone()[0]
            cmds = q(f"SELECT COUNT(*) FROM events WHERE event='PreToolUse' AND ts>=? AND tool IN "
                     f"({','.join('?' * len(cmd_tools))})", (since, *cmd_tools)).fetchone()[0]
            projects = q("SELECT COUNT(DISTINCT project) FROM events WHERE ts>=? AND project!=''",
                         (since,)).fetchone()[0]
        return {"prompts": prompts, "edits": edits, "commands": cmds, "projects": projects}

    def recent(self, limit: int = 50) -> list[dict]:
        with self._lock:
            rows = self._db.execute(
                "SELECT ts, session, event, tool, project, detail FROM events ORDER BY id DESC LIMIT ?", (limit,)
            ).fetchall()
        keys = ("ts", "session", "event", "tool", "project", "detail")
        return [dict(zip(keys, r)) for r in rows]
