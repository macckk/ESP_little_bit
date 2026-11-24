"""Teste de fumaca contra o backend rodando (localhost:8000): auth, long-poll e o hook real.

Uso (dentro de backend/):  .venv\\Scripts\\python tests\\smoke_longpoll.py
"""
import json
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

BASE = "http://127.0.0.1:8000"
ROOT = Path(__file__).resolve().parent.parent
env = dict(line.split("=", 1) for line in (ROOT / ".env").read_text().splitlines() if "=" in line and not line.startswith("#"))
DEV = {"Authorization": f"Bearer {env['DEVICE_TOKEN'].strip()}"}


def get(path, headers=None, timeout=40):
    req = urllib.request.Request(BASE + path, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.headers.get("ETag"), r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers.get("ETag"), b""


def hook(event: dict):
    # usa o script de hook de verdade, como o Claude Code faria
    subprocess.run([sys.executable, str(ROOT / "hooks" / "claude_hook.py")], input=json.dumps(event).encode(),
                   check=True, timeout=5)


def main():
    assert get("/device/cyd-test/screen")[0] == 401, "sem token deveria dar 401"
    code, etag, body = get("/device/cyd-test/screen", DEV)
    print("inicial:", code, body.decode())

    # long-poll sem mudanca: deve segurar ~3 s e responder 304
    t0 = time.time()
    code, _, _ = get("/device/cyd-test/screen?wait=3", {**DEV, "If-None-Match": etag})
    print(f"long-poll sem mudanca: {code} em {time.time() - t0:.1f} s")
    assert code == 304

    # long-poll com evento no meio: deve voltar logo apos o hook
    result = {}

    def poll():
        t = time.time()
        result["r"] = get("/device/cyd-test/screen?wait=20", {**DEV, "If-None-Match": etag})
        result["dt"] = time.time() - t

    th = threading.Thread(target=poll)
    th.start()
    time.sleep(1.5)
    hook({"hook_event_name": "PreToolUse", "session_id": "smoke", "cwd": "F:\\ESP32_tela",
          "tool_name": "Bash", "tool_input": {"command": "pio run -e cyd2usb"}, "prompt": "nao deve vazar"})
    th.join()
    code, _, body = result["r"]
    print(f"long-poll com evento: {code} em {result['dt']:.1f} s ->", body.decode())
    assert code == 200 and result["dt"] < 3 and b"working" in body

    hook({"hook_event_name": "SessionEnd", "session_id": "smoke"})
    print("OK")


if __name__ == "__main__":
    main()
