"""Simula uma sessao do Claude Code (pelo script de hook real) para ver os humores do pet.

Uso (dentro de backend/):  .venv\\Scripts\\python tests\\demo_sequence.py [--step 8]
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

HOOK = Path(__file__).resolve().parent.parent / "hooks" / "claude_hook.py"
CWD = "F:\\ESP32_tela"

STEPS = [
    ("trabalhando (pensando)", {"hook_event_name": "UserPromptSubmit"}),
    ("trabalhando (editando)", {"hook_event_name": "PreToolUse", "tool_name": "Edit",
                                "tool_input": {"file_path": CWD + "\\src\\app\\pet.cpp"}}),
    ("trabalhando (rodando)", {"hook_event_name": "PreToolUse", "tool_name": "Bash",
                               "tool_input": {"command": "pio run -e cyd2usb"}}),
    ("precisa de voce", {"hook_event_name": "Notification",
                         "message": "Claude needs your permission to use Bash"}),
    ("voltou a trabalhar", {"hook_event_name": "PostToolUse", "tool_name": "Bash"}),
    ("terminou", {"hook_event_name": "Stop"}),
]


def send(ev):
    ev = {"session_id": "demo", "cwd": CWD, **ev}
    subprocess.run([sys.executable, str(HOOK)], input=json.dumps(ev).encode(), timeout=5, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--step", type=float, default=8)
    a = ap.parse_args()
    for label, ev in STEPS:
        print(time.strftime("%H:%M:%S"), "->", label, flush=True)
        send(ev)
        time.sleep(a.step)
    print("fim (fica em 'terminou' ~2 min, depois ocioso)")


if __name__ == "__main__":
    main()
