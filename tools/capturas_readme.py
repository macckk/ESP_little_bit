"""Refaz as capturas de tela usadas no README (docs/img/) direto da placa, pelo serial.

  python tools/capturas_readme.py [pets] [seletor] [cartoes] [apps]     (sem argumentos: tudo)

Use o Python do PlatformIO (tem pyserial). Leva ~20 s por captura. O balao do pet mostra o comando que o Claude
Code estiver rodando: rode a partir da raiz do projeto, com caminho relativo, para nao aparecer nada pessoal.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, "docs", "img")
PY = sys.executable
ENV = dict(os.environ, PYTHONIOENCODING="utf-8")


def cmd(c, wait=1.5):
    r = subprocess.run([PY, os.path.join(ROOT, "tools", "serial_cmd.py"), "--wait", str(wait), c + "\n"],
                       capture_output=True, env=ENV)
    return r.stdout.decode("utf-8", "replace")


def shot(name):
    subprocess.run([PY, os.path.join(ROOT, "tools", "screenshot.py"), "-o", os.path.join(IMG, name + ".png")],
                   capture_output=True, env=ENV)
    print("ok", name, flush=True)


def main():
    which = sys.argv[1:] or ["pets", "seletor", "cartoes", "apps"]
    os.makedirs(IMG, exist_ok=True)
    if "pets" in which:
        for mood, name in (("idle", "pet_ocioso"), ("attention", "pet_alerta"), ("done", "pet_feliz")):
            cmd("mood " + mood)
            shot(name)
        cmd("mood idle")
    if "seletor" in which:
        cmd("picker")
        shot("seletor")
        cmd("picker")
    if "cartoes" in which:  # o cartao 1 (clima) mostra o nome da cidade: fica de fora
        for n, name in ((0, "cartao_relogio"), (2, "cartao_hoje"), (3, "cartao_semana")):
            cmd(f"card {n}")
            shot(name)
        cmd("mood idle")
    if "apps" in which:
        cmd("apps")
        shot("menu_apps")
        cmd("apps")
        cmd("app monitor", 4)
        shot("app_monitor")
        cmd("app contador", 4)
        shot("app_contador")
        cmd("app pet", 4)


if __name__ == "__main__":
    main()
