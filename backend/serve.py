"""Inicia o backend sem janela (pythonw.exe no login do Windows) e o mantem de pe.

Este processo e so um "guardiao": roda o servidor num processo filho e, se ele cair (erro, falta de memoria,
fechado por engano), registra no log e sobe de novo. Antes, o backend chegou a morrer em silencio e a placa
ficou sem dados ate alguem reiniciar a mao.

Saida vai para backend/data/backend.log (zerado ao passar de 2 MB).
Uso manual:  .venv\\Scripts\\pythonw.exe serve.py     (ou python.exe serve.py para ver no console)
             python.exe serve.py --worker              (so o servidor, sem guardiao)
"""
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
LOG = BASE / "data" / "backend.log"
PORT = int(os.environ.get("CYD_PORT", 8000))


def port_in_use(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(("127.0.0.1", port)) == 0


def log(msg: str) -> None:
    print(time.strftime("%Y-%m-%d %H:%M:%S ") + "GUARDIAO " + msg, flush=True)


def open_log():
    LOG.parent.mkdir(parents=True, exist_ok=True)
    if LOG.exists() and LOG.stat().st_size > 2 * 1024 * 1024:
        LOG.unlink()
    return open(LOG, "a", encoding="utf-8", buffering=1)


def worker() -> None:
    """O servidor em si (uvicorn)."""
    import faulthandler
    import logging

    import uvicorn

    faulthandler.enable(file=sys.stderr)  # se o Python morrer de vez, o motivo vai para o log
    logging.basicConfig(level=logging.INFO, stream=sys.stdout,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    # access_log desligado: o long-poll da placa geraria uma linha a cada 25 s
    uvicorn.run("app.main:app", host="0.0.0.0", port=PORT, access_log=False,
                timeout_graceful_shutdown=2, log_config=None)


def guardian(out) -> None:
    """Roda o worker e o reinicia se cair; espera mais entre tentativas se ele cair logo apos subir."""
    delay = 5
    while True:
        if port_in_use(PORT):
            log(f"porta {PORT} ja em uso (outro backend rodando?); confiro de novo em 60 s")
            time.sleep(60)
            continue
        started = time.time()
        p = subprocess.Popen([sys.executable, str(BASE / "serve.py"), "--worker"], cwd=BASE,
                             stdout=out, stderr=subprocess.STDOUT,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        log(f"servidor iniciado (pid {p.pid})")
        code = p.wait()
        alive = time.time() - started
        delay = 5 if alive > 120 else min(delay * 2, 60)
        log(f"servidor saiu (codigo {code}) depois de {alive:.0f} s; reinicio em {delay} s")
        time.sleep(delay)


def main() -> None:
    os.chdir(BASE)
    sys.path.insert(0, str(BASE))
    hidden = sys.stdout is None or sys.executable.lower().endswith("pythonw.exe")
    if "--worker" in sys.argv:
        if hidden:
            sys.stdout = sys.stderr = open_log()
        worker()
        return
    out = open_log() if hidden else None
    if hidden:
        sys.stdout = sys.stderr = out
    if port_in_use(PORT):
        log(f"porta {PORT} ja em uso: outro backend rodando? saindo.")
        return
    guardian(out)


if __name__ == "__main__":
    main()
