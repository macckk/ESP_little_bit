"""Inicia o backend sem janela (para rodar com pythonw.exe no login do Windows).

Saida vai para backend/data/backend.log (zerado ao passar de 2 MB).
Uso manual:  .venv\\Scripts\\pythonw.exe serve.py     (ou python.exe serve.py para ver no console)
"""
import os
import socket
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
LOG = BASE / "data" / "backend.log"
PORT = int(os.environ.get("CYD_PORT", 8000))


def port_in_use(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(("127.0.0.1", port)) == 0


def main() -> None:
    os.chdir(BASE)
    sys.path.insert(0, str(BASE))
    if sys.stdout is None or sys.executable.lower().endswith("pythonw.exe"):
        LOG.parent.mkdir(parents=True, exist_ok=True)
        if LOG.exists() and LOG.stat().st_size > 2 * 1024 * 1024:
            LOG.unlink()
        f = open(LOG, "a", encoding="utf-8", buffering=1)
        sys.stdout = sys.stderr = f
    if port_in_use(PORT):
        print(f"porta {PORT} ja em uso: outro backend rodando? saindo.", flush=True)
        return

    import logging

    import uvicorn

    logging.basicConfig(level=logging.INFO, stream=sys.stdout,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    # access_log desligado: o long-poll da placa geraria uma linha a cada 25 s
    uvicorn.run("app.main:app", host="0.0.0.0", port=PORT, access_log=False,
                timeout_graceful_shutdown=2, log_config=None)


if __name__ == "__main__":
    main()
