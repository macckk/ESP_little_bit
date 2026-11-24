"""Le o LDR da placa repetidamente (comando serial "ldr") e mostra min/max.

Uso: python tools/ldr_watch.py [--port COM5] [--secs 40]
"""
import argparse
import re
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--secs", type=float, default=40)
    a = ap.parse_args()
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 0.1
    ser.dtr = ser.rts = False
    ser.open()
    vals0, vals11 = [], []
    end, buf = time.time() + a.secs, ""
    while time.time() < end:
        ser.write(b"ldr\n")
        t = time.time() + 0.5
        while time.time() < t:
            buf += ser.read(256).decode("utf-8", "replace")
        for m in re.finditer(r"raw0db=(\d+) raw11db=(\d+)", buf):
            vals0.append(int(m.group(1)))
            vals11.append(int(m.group(2)))
            print(time.strftime("%H:%M:%S"), "0dB:", m.group(1), " 11dB:", m.group(2), flush=True)
        buf = buf[buf.rfind("\n") + 1:]
    ser.close()
    if vals0:
        print(f"0dB  min={min(vals0)} max={max(vals0)}")
        print(f"11dB min={min(vals11)} max={max(vals11)}")


if __name__ == "__main__":
    main()
