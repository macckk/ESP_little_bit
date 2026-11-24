"""Backend falso (so stdlib) para testar o firmware antes do FastAPI existir.

GET /device/{id}/screen -> JSON pequeno no formato dos layouts (ver docs/GUIA.md).
Troca de layout a cada --cycle segundos: status -> number -> qr -> message.

Uso: python tools/mock_backend.py [--port 8000] [--token T] [--cycle 20] [--layout qr]
"""
import argparse
import hashlib
import json
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ARGS = None


def screen_status():
    t = int(time.time())
    n8n_ok = (t // 30) % 3 != 0
    return {"layout": "status", "title": "Infra · EasyPanel", "refresh": 5, "items": [
        {"label": "easypanel", "state": "ok", "value": "42 ms"},
        {"label": "api-vendas", "state": "ok", "value": "87 ms"},
        {"label": "postgres", "state": "ok", "value": "3 ms"},
        {"label": "n8n", "state": "ok" if n8n_ok else "err", "value": "120 ms" if n8n_ok else "FORA"},
        {"label": "backup diário", "state": "warn", "value": "atrasado"},
        {"label": "certificado", "state": "ok", "value": "62 dias"},
    ]}


def screen_number():
    pedidos = 37 + (int(time.time()) // 10) % 50
    valor = f"{pedidos * 41.9:,.2f}".replace(",", "X").replace(".", ",").replace("X", ".")
    return {"layout": "number", "title": "Vendas hoje", "refresh": 5, "label": "pedidos (Shopee + ML)",
            "value": str(pedidos), "sub": f"R$ {valor}", "progress": pedidos / 100, "color": "#FFEB3B"}


def screen_qr():
    return {"layout": "qr", "title": "Pix balcão", "refresh": 30,
            "qr": "DEMO-PIX-NAO-PAGAR|cobranca=3|valor=12.50",
            "label": "Valor", "value": "R$ 12,50", "sub": "cobrança #3 · ação de teste"}


def screen_message():
    return {"layout": "message", "title": "Desk pet", "refresh": 10, "icon": "ok",
            "text": "Agente terminou a tarefa. Revisão pendente às 14h."}


SCREENS = {"status": screen_status, "number": screen_number, "qr": screen_qr, "message": screen_message}


def current():
    if ARGS.layout:
        return SCREENS[ARGS.layout]()
    names = list(SCREENS)
    return SCREENS[names[int(time.time() // ARGS.cycle) % len(names)]]()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        m = re.fullmatch(r"/device/([\w-]+)/screen", self.path.split("?")[0])
        if not m:
            self.send_error(404)
            return
        auth = self.headers.get("Authorization", "")
        if ARGS.token and auth != f"Bearer {ARGS.token}":
            self.send_response(401)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        body = json.dumps(current(), ensure_ascii=False, separators=(",", ":")).encode()
        etag = '"' + hashlib.sha1(body).hexdigest()[:16] + '"'
        if self.headers.get("If-None-Match") == etag:
            self.send_response(304)
            self.send_header("ETag", etag)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("ETag", etag)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        auth = "token" if self.headers.get("Authorization") else "sem-token"
        print(time.strftime("%H:%M:%S"), self.client_address[0], auth, fmt % args, flush=True)


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--token", default=None)
    ap.add_argument("--cycle", type=int, default=20)
    ap.add_argument("--layout", choices=list(SCREENS))
    ARGS = ap.parse_args()
    for name, fn in SCREENS.items():
        print(f"{name}: {len(json.dumps(fn(), ensure_ascii=False).encode())} B")
    print(f"mock em 0.0.0.0:{ARGS.port}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", ARGS.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
