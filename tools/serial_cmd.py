"""Envia comandos ao CYD pela serial e imprime a resposta.

Uso: python tools/serial_cmd.py [--port COM5] [--reset] [--wait 3] [cmds...]
Cada argumento em cmds e enviado como texto (ex.: r i o p), com --wait s de leitura apos cada um.
"""
import argparse
import sys
import time

import serial


def read_for(ser, secs):
    end = time.time() + secs
    while time.time() < end:
        data = ser.read(ser.in_waiting or 1)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--reset", action="store_true", help="pulsa EN antes de ler")
    ap.add_argument("--wait", type=float, default=2.0)
    ap.add_argument("cmds", nargs="*")
    a = ap.parse_args()

    ser = serial.Serial()
    ser.port = a.port
    ser.baudrate = 115200
    ser.timeout = 0.1
    ser.dtr = False
    ser.rts = False
    ser.open()
    if a.reset:
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
    read_for(ser, a.wait)
    for c in a.cmds:
        print(f"\n>>> {c}")
        ser.write(c.encode())
        read_for(ser, a.wait)
    ser.close()


if __name__ == "__main__":
    main()
