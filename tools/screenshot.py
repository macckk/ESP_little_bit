"""Captura a tela do CYD pelo serial (comando "shot") e salva um PNG.

Uso: python tools/screenshot.py [--port COM5] [-o tela.png]
So precisa de pyserial (use o Python do PlatformIO); o PNG e gravado com a stdlib.
"""
import argparse
import struct
import time
import zlib

import serial


class Image:
    """RGB minimo com gravacao PNG (sem Pillow)."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 3)

    def putpixel(self, xy, rgb):
        x, y = xy
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.px[i:i + 3] = bytes(rgb)

    def save(self, path):
        raw = b"".join(b"\x00" + bytes(self.px[y * self.w * 3:(y + 1) * self.w * 3]) for y in range(self.h))

        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)))
            f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
            f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("-o", "--out", default="tela.png")
    a = ap.parse_args()

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 2
    ser.dtr = ser.rts = False
    ser.open()
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.write(b"shot\n")

    img = None
    area = None  # (x, y, w, h, linha atual)
    deadline = time.time() + 120
    while time.time() < deadline:
        line = ser.readline().decode("ascii", "replace").strip()
        if not line:
            continue
        if line.startswith("SHOT_BEGIN"):
            _, w, h = line.split()
            img = Image(int(w), int(h))
        elif line.startswith("SHOT_END"):
            break
        elif line.startswith("SHOT ") and img is not None:
            x, y, w, h = map(int, line.split()[1:])
            area = [x, y, w, h, 0]
        elif area and len(line) == area[2] * 4:
            x, y, w, h, row = area
            data = bytes.fromhex(line)
            for i in range(w):
                v = data[2 * i] | (data[2 * i + 1] << 8)  # RGB565 little-endian (LVGL)
                r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                img.putpixel((x + i, y + row), (r * 255 // 31, g * 255 // 63, b * 255 // 31))
            area[4] += 1
            if area[4] >= h:
                area = None
    ser.close()
    if img is None:
        raise SystemExit("nenhuma captura recebida (firmware com comando 'shot'?)")
    img.save(a.out)
    print("salvo", a.out)


if __name__ == "__main__":
    main()
