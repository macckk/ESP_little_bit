"""Gera src/app/pet_builtin.h com o pacote de um pet embutido no firmware (pet reserva: a placa sempre tem
pelo menos ele, mesmo nova ou sem backend).

  backend\\.venv\\Scripts\\python tools\\pet_embed.py gato
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))
from app.sprites import EXTS, build, is_preview, load_config, pet_name  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pet")
    ap.add_argument("--pasta", type=Path, default=ROOT / "pets")
    a = ap.parse_args()
    paths = sorted(p for p in a.pasta.iterdir() if p.suffix.lower() in EXTS and not is_preview(p) and pet_name(p) == a.pet)
    pk = build(a.pet, paths, load_config(a.pasta, a.pet))
    data = pk.to_bytes()
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    out = ROOT / "src" / "app" / "pet_builtin.h"
    out.write_text(
        "// Gerado por tools/pet_embed.py -- pet reserva embutido no firmware (nao editar a mao).\n"
        "#pragma once\n#include <stdint.h>\n\n"
        f'static const char BUILTIN_PET_NAME[] = "{pk.name}";\n'
        f"static const uint8_t BUILTIN_PET[{len(data)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n",
        encoding="utf-8")
    print(f"{out}: {pk.name}, {len(data)} B, versao {pk.version()}")


if __name__ == "__main__":
    main()
