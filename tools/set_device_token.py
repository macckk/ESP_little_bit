"""Grava na placa (pela serial) o DEVICE_TOKEN de backend/.env, sem exibir o valor.

Uso: python tools/set_device_token.py [--port COM5]
"""
import argparse
import time
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    a = ap.parse_args()
    env = dict(l.split("=", 1) for l in (ROOT / "backend" / ".env").read_text().splitlines()
               if "=" in l and not l.startswith("#"))
    tok = env["DEVICE_TOKEN"].strip()
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 0.2
    ser.dtr = ser.rts = False
    ser.open()
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.write(f"token {tok}\n".encode())
    end, out = time.time() + 2, b""
    while time.time() < end:
        out += ser.read(256)
    ser.close()
    for line in out.decode("utf-8", "replace").splitlines():
        if line.startswith("CFG"):
            print(line.replace(tok, "***"))


if __name__ == "__main__":
    main()
