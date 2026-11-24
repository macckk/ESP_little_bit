import tempfile
import unittest
from unittest import mock
from pathlib import Path

from PIL import Image, ImageDraw

from app.sprites import ROLES, SpriteLibrary, convert

MAGENTA = (255, 0, 255)


def _sheet(path: Path, frames: int, cell: int = 11, grid=(30, 34)):
    """Folha falsa: cada quadro e um boneco em grade de `cell` px, com ruido leve de compressao."""
    gw, gh = grid
    im = Image.new("RGB", (frames * (gw + 6) * cell, (gh + 6) * cell), MAGENTA)
    d = ImageDraw.Draw(im)
    for f in range(frames):
        ox, oy = (f * (gw + 6) + 3) * cell, 3 * cell
        for gy in range(gh):
            for gx in range(gw):
                edge = gx in (0, gw - 1) or gy in (0, gh - 1)
                stripe = (gx * 7 + gy * 3) % 5 == 0  # listras: bordas em quase toda a grade, como numa arte real
                col = (90, 50, 30) if edge else (224, 112, 32) if stripe else (244, 162, 97)
                if gy == 4 and gx in (3, gw - 4):
                    col = (0, 0, 0) if f == 0 else (244, 162, 97)  # olhos fecham no 2o quadro
                d.rectangle([ox + gx * cell, oy + gy * cell, ox + (gx + 1) * cell - 1, oy + (gy + 1) * cell - 1],
                            fill=(col[0], max(0, col[1] - (gx % 2)), col[2]))
    im.save(path)


class SpriteTest(unittest.TestCase):
    def test_converte_folha(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "Bichinho_folha.png"
            _sheet(p, 2)
            pk = convert(p)
            self.assertEqual(pk.name, "bichinho")
            self.assertEqual((pk.w, pk.h), (30, 34))
            self.assertEqual([ROLES[r] for r in pk.roles], ["normal", "piscando", "respirando"])
            self.assertEqual(pk.generated, ("respirando",))
            self.assertEqual(len(pk.palette) - 1, 4)  # contorno, pelo, listra, olho (ruido juntado)
            self.assertNotEqual(pk.frames[0], pk.frames[1])
            # respirando: linha de cima vazia (tudo desceu 1 px), base igual
            fw = pk.w
            self.assertFalse(any(pk.frames[2][:fw]))
            self.assertEqual(pk.frames[2][-fw:], pk.frames[1][-fw:])
            data = pk.to_bytes()
            self.assertEqual(data[:4], b"CYDP")
            stride = (30 + 1) // 2
            self.assertEqual(len(data), 4 + 5 + 16 + 3 * len(pk.palette) + 3 + 3 * 34 * stride)
            self.assertEqual(list(data[25 + 3 * len(pk.palette):][:3]), [0, 1, 7])

    def test_biblioteca_prefere_mais_quadros(self):
        with tempfile.TemporaryDirectory() as tmp:
            _sheet(Path(tmp) / "gato_base.png", 1)
            _sheet(Path(tmp) / "gato_folha.png", 3)
            lib = SpriteLibrary(Path(tmp))
            self.assertTrue(lib.refresh())
            self.assertFalse(lib.refresh())  # nada mudou
            self.assertEqual([x["n"] for x in lib.listing()], ["gato"])
            self.assertEqual(lib.summary()["pets"]["gato"]["quadros"],
                             ["normal", "piscando", "dormindo", "respirando"])
            # pasta some e volta igual: some da lista e reaparece
            moved = Path(tmp) / "x"
            lib.folder = moved
            self.assertTrue(lib.refresh())
            self.assertEqual(lib.listing(), [])
            lib.folder = Path(tmp)
            self.assertTrue(lib.refresh())
            self.assertEqual([x["n"] for x in lib.listing()], ["gato"])

    def test_junta_por_sufixo_e_gera_rabo(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            _sheet(d / "gato_base.png", 2)    # normal, piscando
            _sheet(d / "gato_alerta.png", 2)  # alerta, feliz
            (d / "gato.json").write_text('{"rabo": {"x": 20, "y0": 2, "y1": 30, "desloca": 2}}', encoding="utf-8")
            lib = SpriteLibrary(d)
            lib.refresh()
            info = lib.summary()["pets"]["gato"]
            self.assertEqual(info["quadros"], ["normal", "piscando", "alerta", "feliz", "rabo", "respirando"])
            pk = lib.packs["gato"][0]
            self.assertEqual(pk.w, 32)  # abriu 2 colunas para o rabo
            rabo = pk.frames[4]
            self.assertTrue(any(rabo[2 * pk.w + 30:3 * pk.w]))     # topo do rabo foi 2 px para a direita
            self.assertEqual(rabo[30 * pk.w:], pk.frames[0][30 * pk.w:])  # base nao mexe

    def test_cache_em_disco(self):
        with tempfile.TemporaryDirectory() as tmp:
            d, cache = Path(tmp) / "pets", Path(tmp) / "cache"
            d.mkdir()
            _sheet(d / "gato_base.png", 2)
            lib = SpriteLibrary(d, cache)
            lib.refresh()
            v = lib.listing()
            # backend reiniciado: le do cache, sem converter
            with mock.patch("app.sprites.build", side_effect=AssertionError("reconverteu")):
                lib2 = SpriteLibrary(d, cache)
                lib2.refresh()
            self.assertEqual(lib2.listing(), v)
            # arquivo mudou: converte de novo
            _sheet(d / "gato_base.png", 3)
            lib2.refresh()
            self.assertEqual(lib2.summary()["pets"]["gato"]["quadros"][:3], ["normal", "piscando", "dormindo"])


if __name__ == "__main__":
    unittest.main()
