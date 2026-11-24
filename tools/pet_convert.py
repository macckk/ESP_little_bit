"""Converte um pet (todos os arquivos pets/<nome>*) e salva a previa (como ficara na placa), sem precisar da placa.

  backend\\.venv\\Scripts\\python tools\\pet_convert.py gato [--grade] [--escala 6]

--grade numera linhas e colunas (para achar as coordenadas do rabo em pets/<nome>.json).
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))
from PIL import ImageDraw  # noqa: E402

from app.sprites import EXTS, ROLES, build, is_preview, load_config, pet_name  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pet", help="nome do pet (ex. gato)")
    ap.add_argument("--pasta", type=Path, default=ROOT / "pets")
    ap.add_argument("-o", "--saida", type=Path)
    ap.add_argument("--escala", type=int, default=6)
    ap.add_argument("--grade", action="store_true")
    a = ap.parse_args()
    paths = sorted(p for p in a.pasta.iterdir() if p.suffix.lower() in EXTS and not is_preview(p) and pet_name(p) == a.pet)
    if not paths:
        sys.exit(f"nenhum arquivo de '{a.pet}' em {a.pasta}")
    pk = build(a.pet, paths, load_config(a.pasta, a.pet))
    print(f"pet '{pk.name}' de {pk.source}: grade {pk.w}x{pk.h}, {len(pk.palette) - 1} cores, "
          f"bloco de {pk.cell:.1f} px, pacote {len(pk.to_bytes())} B")
    print("quadros:", ", ".join(ROLES[r] + (" (gerado)" if ROLES[r] in pk.generated else "") for r in pk.roles))
    im = pk.preview(a.escala)
    if a.grade:
        d = ImageDraw.Draw(im)
        s = a.escala
        for x in range(0, pk.w, 5):
            d.line([(x * s, 0), (x * s, im.height)], fill=(80, 80, 80))
            d.text((x * s + 1, 0), str(x), fill=(255, 255, 0))
        for y in range(0, pk.h, 5):
            d.line([(0, y * s), (pk.w * s, y * s)], fill=(80, 80, 80))
            d.text((1, y * s + 1), str(y), fill=(255, 255, 0))
    out = a.saida or a.pasta / f"{a.pet}_previa.png"
    im.save(out)
    print("previa:", out)


if __name__ == "__main__":
    main()
