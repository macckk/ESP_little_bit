"""Pets em pixel art: converte imagens (PNG/WebP, inclusive geradas por IA) no pacote que a placa desenha.

Entrada: pets/<nome>[_sufixo].png|webp com fundo liso magenta (#FF00FF) ou transparente.
Quadros lado a lado (folha), na ordem de ROLES: normal, piscando, dormindo, alerta, feliz, trabalhando, rabo, respirando
(so o normal e obrigatorio; a placa usa o que existir).
Varios arquivos do mesmo pet se juntam: se o sufixo for um papel (gato_alerta.webp), os quadros daquele arquivo
comecam nesse papel; senao comecam no normal. Papel repetido: vale o arquivo com mais quadros.
Gerados quando faltam: "respirando" (quadro de dormir com o corpo 1 px mais baixo) e "rabo" (se pets/<nome>.json
disser onde fica o rabo: {"rabo": {"x": 31, "y0": 28, "y1": 38, "desloca": 2}}, coordenadas do quadro normal
na previa; as linhas y0..y1 a partir da coluna x se inclinam, desloca px no topo e 0 na base).

O conversor descobre o tamanho real dos "pixels" da imagem (a IA desenha blocos de ~10-20 px),
amostra o centro de cada bloco, junta cores quase iguais (ruido do WebP) e limita a 15 cores + transparente.

Pacote (little-endian), servido em GET /device/{id}/sprite/{nome}:
  "CYDP" | ver=1 | w | h | nframes | ncolors | label[16] (UTF-8, \\0) | paleta ncolors*3 (RGB; indice 0 = transparente)
  | papeis[nframes] | quadros: h linhas de ceil(w/2) bytes, 4 bpp, nibble alto = pixel da esquerda
"""
from __future__ import annotations

import cmath
import hashlib
import json
import math
import pickle
import re
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

ROLES = ["normal", "piscando", "dormindo", "alerta", "feliz", "trabalhando", "rabo", "respirando"]
MAX_SIDE = 64  # maior lado do pet na grade real
MIN_SIDE = 28  # menos que isso no maior lado = bloco medido em dobro
MAX_COLORS = 15  # + transparente
EXTS = {".png", ".webp", ".gif", ".bmp"}


@dataclass
class Pack:
    name: str
    label: str
    w: int
    h: int
    frames: list[list[int]]  # indices de paleta, linha a linha (0 = transparente)
    palette: list[tuple[int, int, int]]  # palette[0] e o transparente (ignorado)
    source: str
    cell: float
    roles: list[int] | None = None  # papel (indice em ROLES) de cada quadro
    generated: tuple[str, ...] = ()

    def __post_init__(self):
        if self.roles is None:
            self.roles = list(range(len(self.frames)))

    def to_bytes(self) -> bytes:
        out = bytearray(b"CYDP")
        out += bytes([1, self.w, self.h, len(self.frames), len(self.palette)])
        out += self.label.encode()[:15].ljust(16, b"\0")
        for r, g, b in self.palette:
            out += bytes([r, g, b])
        out += bytes(self.roles)
        stride = (self.w + 1) // 2
        for fr in self.frames:
            for y in range(self.h):
                row = bytearray(stride)
                for x in range(self.w):
                    v = fr[y * self.w + x]
                    row[x // 2] |= v << 4 if x % 2 == 0 else v
                out += row
        return bytes(out)

    def version(self) -> str:
        return hashlib.sha1(self.to_bytes()).hexdigest()[:8]

    def preview(self, scale: int = 4, bg=(24, 24, 24)) -> Image.Image:
        """Quadros lado a lado, ampliados, sobre fundo escuro (como na placa)."""
        gap = 4
        im = Image.new("RGB", (len(self.frames) * (self.w + gap) - gap, self.h), bg)
        px = im.load()
        for i, fr in enumerate(self.frames):
            for y in range(self.h):
                for x in range(self.w):
                    v = fr[y * self.w + x]
                    if v:
                        px[i * (self.w + gap) + x, y] = self.palette[v]
        return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


# ----------------------------------------------------------------- analise


def _is_bg(p) -> bool:
    if len(p) == 4 and p[3] < 128:
        return True
    r, g, b = p[0], p[1], p[2]
    if r > 170 and b > 170 and g < 110 and abs(r - b) < 90:  # magenta com ruido de compressao
        return True
    # borda: o WebP mistura o magenta com o contorno/pelo (rosa-arroxeado); verde bem abaixo de vermelho e azul
    if r > 120 and b > 120 and g < min(r, b) - 60:
        return True
    # magenta misturado com contorno escuro (roxo): verde bem menor que vermelho e azul
    return min(r, b) > 80 and g < 0.55 * min(r, b) and abs(r - b) < 110


def _dist2(a, b) -> int:
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _segments(occupied: list[bool], min_gap: int) -> list[tuple[int, int]]:
    segs, start, gap = [], None, 0
    for i, o in enumerate(occupied):
        if o:
            if start is None:
                start = i
            gap, end = 0, i
        elif start is not None:
            gap += 1
            if gap >= min_gap:
                segs.append((start, end + 1))
                start = None
    if start is not None:
        segs.append((start, end + 1))
    return segs


def _period(hist: list[float]) -> float:
    """Periodo da grade a partir do histograma de bordas (componente de Fourier).
    Divisores do periodo real tambem pontuam alto; o maior periodo com nota alta e o certo
    (as vezes e o dobro: ver MIN_SIDE em analyze)."""
    total = sum(hist) or 1.0
    pts = [(x, h) for x, h in enumerate(hist) if h]
    scores = []
    p = 4.0
    while p <= 48.0:
        z = sum(h * cmath.exp(2j * math.pi * x / p) for x, h in pts)
        scores.append((p, abs(z) / total))
        p += 0.05
    best = max(sc for _, sc in scores)
    return max(p for p, sc in scores if sc >= 0.75 * best)


def _edges(img: Image.Image, box) -> tuple[list[float], list[float]]:
    x0, y0, x1, y1 = box
    px = img.load()
    hx = [0.0] * (x1 - x0 + 1)
    hy = [0.0] * (y1 - y0 + 1)
    for y in range(y0, y1):
        prev = px[x0, y]
        for x in range(x0 + 1, x1):
            cur = px[x, y]
            if _dist2(cur, prev) > 2500:
                hx[x - x0] += 1
            prev = cur
    for x in range(x0, x1):
        prev = px[x, y0]
        for y in range(y0 + 1, y1):
            cur = px[x, y]
            if _dist2(cur, prev) > 2500:
                hy[y - y0] += 1
            prev = cur
    return hx, hy


def _grid_lines(hist: list[float], cell: float, off: float, length: int) -> list[float]:
    """Linhas da grade (relativas ao inicio da caixa). A IA nao desenha blocos todos iguais (variam ~10%),
    entao cada linha "gruda" na borda real mais forte perto de onde era esperada; sem borda, segue o passo."""
    strong = 0.12 * (max(hist) or 1)
    lines = [off - cell * math.ceil(off / cell)]  # primeira linha <= 0
    while lines[-1] < length:
        exp = lines[-1] + cell
        lo, hi = int(exp - cell * 0.3), int(exp + cell * 0.3)
        win = [(hist[x], x) for x in range(max(0, lo), min(len(hist), hi + 1))]
        h, x = max(win) if win else (0, 0)
        lines.append(float(x) if h >= strong else exp)
    return lines


def _sample(img: Image.Image, box, xs: list[float], ys: list[float]):
    """Amostra o centro de cada bloco da grade (linhas xs/ys). Devolve (w, h, pixels RGB ou None)."""
    x0, y0, x1, y1 = box
    px = img.load()
    out = []
    for r in range(len(ys) - 1):
        for c in range(len(xs) - 1):
            bx0, bx1 = x0 + xs[c], x0 + xs[c + 1]
            by0, by1 = y0 + ys[r], y0 + ys[r + 1]
            vals = []
            for fy in (0.3, 0.5, 0.7):
                for fx in (0.3, 0.5, 0.7):
                    x, y = int(bx0 + fx * (bx1 - bx0)), int(by0 + fy * (by1 - by0))
                    if 0 <= x < img.width and 0 <= y < img.height:
                        vals.append(px[x, y])
            fg = [v[:3] for v in vals if not _is_bg(v)]
            if len(fg) * 2 <= len(vals):
                out.append(None)
            else:  # medoide: um pixel real do bloco (a mediana por canal inventava cores nas bordas)
                out.append(min(fg, key=lambda a: sum(_dist2(a, b) for b in fg)))
    return len(xs) - 1, len(ys) - 1, out


def _clean_edges(w, h, pix):
    """Pixels da borda da silhueta com tom de magenta (contorno escuro misturado com o fundo pelo WebP):
    viram fundo. So na borda, para nao apagar boca/lingua/bochecha."""
    def tinted(c):
        r, g, b = c
        return r > 80 and g < 60 and b > g + 10
    for _ in range(2):
        drop = []
        for y in range(h):
            for x in range(w):
                c = pix[y * w + x]
                if c is None or not tinted(c):
                    continue
                edge = any(not (0 <= x + dx < w and 0 <= y + dy < h) or pix[(y + dy) * w + x + dx] is None
                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if edge:
                    drop.append(y * w + x)
        for i in drop:
            pix[i] = None
    return pix


def _trim(w, h, pix):
    ys = [y for y in range(h) if any(pix[y * w + x] for x in range(w))]
    xs = [x for x in range(w) if any(pix[y * w + x] for y in range(h))]
    if not xs:
        return 0, 0, []
    x0, x1, y0, y1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1
    return x1 - x0, y1 - y0, [pix[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]


def _palette(frames) -> tuple[list[tuple[int, int, int]], dict]:
    """Junta cores quase iguais (ruido) e limita a MAX_COLORS. Devolve paleta e mapa cor->indice."""
    counts: dict[tuple, int] = {}
    for _, _, pix in frames:
        for p in pix:
            if p:
                counts[p] = counts.get(p, 0) + 1
    clusters: list[list] = []  # [cor_representante, soma_r, soma_g, soma_b, n]
    for col, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        for cl in clusters:
            if _dist2(col, cl[0]) < 24 ** 2:
                cl[1] += col[0] * n
                cl[2] += col[1] * n
                cl[3] += col[2] * n
                cl[4] += n
                break
        else:
            clusters.append([col, col[0] * n, col[1] * n, col[2] * n, n])
    clusters.sort(key=lambda cl: -cl[4])
    pal = [(cl[1] // cl[4], cl[2] // cl[4], cl[3] // cl[4]) for cl in clusters[:MAX_COLORS]]
    mapping = {col: 1 + min(range(len(pal)), key=lambda i: _dist2(col, pal[i])) for col in counts}
    return [(0, 0, 0)] + pal, mapping


def analyze(path: Path, cell: float | None = None) -> tuple[list, float, int]:
    """Le uma imagem e devolve os quadros na grade real [(w, h, pixels RGB ou None)], o tamanho do bloco
    (medido, ou o informado) e a altura do maior quadro em pixels da imagem."""
    img = Image.open(path)
    img = img.convert("RGBA" if img.mode in ("RGBA", "LA", "P") else "RGB")
    W, H = img.size
    px = img.load()
    colocc = [any(not _is_bg(px[x, y]) for y in range(0, H, 2)) for x in range(W)]
    segs = _segments(colocc, max(4, W // 50))
    segs = [s for s in segs if s[1] - s[0] > W // 40]  # descarta sujeira
    if not segs:
        raise ValueError(f"{path.name}: nenhum desenho encontrado (o fundo precisa ser magenta liso ou transparente)")

    boxes = []
    for sx0, sx1 in segs:
        rows = [y for y in range(H) if any(not _is_bg(px[x, y]) for x in range(sx0, sx1, 2))]
        boxes.append((sx0, rows[0], sx1, rows[-1] + 1))

    # tamanho do bloco: medido no maior quadro (todos vieram da mesma imagem)
    rgb = img.convert("RGB")
    if cell is None:
        big = max(boxes, key=lambda b: (b[2] - b[0]) * (b[3] - b[1]))
        hx, hy = _edges(rgb, big)
        cell = (_period(hx) + _period(hy)) / 2
        # desenho com contorno/detalhes sempre em pares engana a medida (da o dobro do bloco real):
        # se o bicho ficaria com menos de MIN_SIDE blocos no maior lado, o bloco real e a metade
        side = max(max(b[2] - b[0], b[3] - b[1]) for b in boxes)
        while side / cell < MIN_SIDE and cell / 2 >= 4:
            cell /= 2

    frames = []
    for box in boxes:
        bhx, bhy = _edges(rgb, box)
        xs = _grid_lines(bhx, cell, _phase(bhx, cell), box[2] - box[0])
        ys = _grid_lines(bhy, cell, _phase(bhy, cell), box[3] - box[1])
        w, h, pix = _sample(img, box, xs, ys)
        pix = _clean_edges(w, h, pix)
        frames.append(_trim(w, h, pix))
    return frames, cell, max(b[3] - b[1] for b in boxes)


def first_role(path: Path) -> int:
    """gato_alerta.webp -> quadros comecam em "alerta"; outros sufixos -> "normal"."""
    parts = path.stem.lower().split("_", 1)
    return ROLES.index(parts[1]) if len(parts) == 2 and parts[1] in ROLES else 0


def build(name: str, paths: list[Path], config: dict | None = None) -> Pack:
    """Junta os arquivos de um pet num pacote (paleta e alinhamento comuns) e gera os quadros que faltam."""
    raw = []
    for p in paths:
        frames, cell, phys_h = analyze(p)
        raw.append((p, frames, cell, phys_h))
    raw.sort(key=lambda a: (first_role(a[0]) != 0, -len(a[1])))  # referencia: o arquivo do normal com mais quadros
    # Cada imagem da IA pode ter blocos de tamanho um pouco diferente. Se o bicho tem o mesmo tamanho real nas
    # imagens, ele deve ter o mesmo tamanho na grade: reamostra as outras na escala da referencia.
    _, _, ref_cell, ref_h = raw[0]
    analyzed = [raw[0][:3]]
    for p, frames, cell, phys_h in raw[1:]:
        want = ref_cell * phys_h / ref_h
        if abs(want - cell) / cell > 0.02:
            frames, cell, _ = analyze(p, want)
        analyzed.append((p, frames, cell))
    analyzed.sort(key=lambda a: -len(a[1]))  # papel repetido: vale o arquivo com mais quadros
    by_role: dict[int, tuple] = {}
    for p, frames, cell in analyzed:
        for i, fr in enumerate(frames):
            r = first_role(p) + i
            if r < len(ROLES) and r not in by_role:
                by_role[r] = fr
    if 0 not in by_role:
        raise ValueError(f"{name}: falta o quadro normal (um arquivo sem sufixo de papel, ex. {name}_base.webp)")
    roles = sorted(by_role)
    frames = [by_role[r] for r in roles]

    fw = max(f[0] for f in frames)
    fh = max(f[1] for f in frames)
    if max(fw, fh) > MAX_SIDE:
        raise ValueError(f"{name}: grade de {fw}x{fh} e grande demais (max {MAX_SIDE})")

    pal, mapping = _palette(frames)
    out = []
    for i, (w, h, pix) in enumerate(frames):  # alinha pela base, centralizado
        ox, oy = (fw - w) // 2, fh - h
        if i:  # e depois ajusta ate 3 px para casar com o normal (trocar de quadro sem "pular")
            ref = out[0]
            ox, oy = min(((ox + dx, oy + dy) for dx in range(-3, 4) for dy in range(-3, 4)
                          if 0 <= ox + dx <= fw - w and 0 <= oy + dy <= fh - h),
                         key=lambda o: _mismatch(ref, fw, pix, w, h, *o))
        canvas = [0] * (fw * fh)
        for y in range(h):
            for x in range(w):
                p = pix[y * w + x]
                if p:
                    canvas[(y + oy) * fw + x + ox] = mapping[p]
        out.append(canvas)

    generated = []
    config = config or {}
    # rabo: inclina a regiao do rabo do quadro normal (alternando, ele abana)
    tail = config.get("rabo")
    if tail and ROLES.index("rabo") not in roles:
        x0 = int(tail["x"])  # coordenadas do normal na previa = coordenadas do canvas
        shift = int(tail.get("desloca", 2))
        need = max(x for y in range(fh) for x in range(fw) if out[0][y * fw + x]) + 1 + max(0, shift)
        if need > fw:  # abre espaco a direita para o rabo nao sair do quadro
            pad = need - fw
            out = [[row for y in range(fh) for row in fr[y * fw:(y + 1) * fw] + [0] * pad] for fr in out]
            fw = need
        out.append(_shear(out[0], fw, fh, x0, int(tail["y0"]), int(tail["y1"]), shift))
        roles.append(ROLES.index("rabo"))
        generated.append("rabo")
    # respirando: o quadro de dormir com tudo acima de uma linha do corpo 1 px mais baixo
    if ROLES.index("respirando") not in roles:
        base = next((out[roles.index(r)] for r in (2, 1, 0) if r in roles))
        seam = config.get("respira", {}).get("linha") or _seam(base, fw, fh)
        out.append(_breathe(base, fw, fh, seam))
        roles.append(ROLES.index("respirando"))
        generated.append("respirando")

    srcs = ", ".join(p.name for p, _, _ in analyzed)
    return Pack(name, name.capitalize(), fw, fh, out, pal, srcs, analyzed[0][2], roles, tuple(generated))


def _shear(fr, fw, fh, x0, y0, y1, shift):
    """Pixels da coluna x0 em diante, linhas y0..y1: deslocados para a direita, `shift` no topo e 0 na base."""
    out = list(fr)
    for y in range(y0, y1 + 1):
        k = round(shift * (y1 - y) / max(1, y1 - y0))
        if not k:
            continue
        row = fr[y * fw:(y + 1) * fw]
        for x in range(x0, fw):
            out[y * fw + x] = 0
        for x in range(x0, fw):
            if row[x] and 0 <= x + k < fw:
                out[y * fw + x + k] = row[x]
    return out


def _seam(fr, fw, fh) -> int:
    """Linha do corpo (55-80% da altura) mais parecida com a de cima: some sem deixar marca."""
    def diff(y):
        return sum(fr[y * fw + x] != fr[(y - 1) * fw + x] for x in range(fw))
    return min(range(int(fh * 0.55), int(fh * 0.8)), key=diff)


def _breathe(fr, fw, fh, seam):
    out = list(fr)
    for y in range(seam, 0, -1):  # linha seam some; tudo acima desce 1
        out[y * fw:(y + 1) * fw] = fr[(y - 1) * fw:y * fw]
    out[0:fw] = [0] * fw
    return out


def convert(path: Path, name: str | None = None) -> Pack:
    """Um arquivo so (previa rapida)."""
    name = name or pet_name(path)
    return build(name, [path], load_config(path.parent, name))


def load_config(folder: Path, name: str) -> dict:
    p = folder / f"{name}.json"
    if not p.exists():
        return {}
    return json.loads(p.read_text(encoding="utf-8"))


def _mismatch(ref, fw, pix, w, h, ox, oy) -> int:
    """Pixels cuja silhueta (opaco/transparente) difere do quadro de referencia."""
    n = 0
    for y in range(h):
        for x in range(w):
            n += (pix[y * w + x] is not None) != (ref[(y + oy) * fw + x + ox] != 0)
    return n


def _phase(hist: list[float], p: float) -> float:
    z = sum(h * cmath.exp(2j * math.pi * x / p) for x, h in enumerate(hist) if h)
    return (cmath.phase(z) / (2 * math.pi) * p) % p


def is_preview(path: Path) -> bool:
    """tools/pet_convert.py salva <nome>_previa.png na mesma pasta; nao e desenho."""
    return path.stem.lower().endswith("_previa")


def pet_name(path: Path) -> str:
    stem = path.stem.split("_")[0].lower()
    return re.sub(r"[^a-z0-9]", "", stem)[:12] or "pet"


# ---------------------------------------------------------------- catalogo


CONVERTER_VERSION = 2  # mude quando o conversor mudar: invalida o cache em disco


class SpriteLibrary:
    """Pasta pets/: converte o que mudou e guarda os pacotes em memoria.

    Converter um pet leva ~5 s. Cada pet tem uma chave (arquivos + tamanho + data, o .json dele e
    CONVERTER_VERSION); com a chave igual reaproveita a conversao anterior: da memoria, ou do cache em disco
    (cache_dir/<nome>.pkl), que deixa o backend subir em segundos em vez de reconverter tudo."""

    def __init__(self, folder: Path, cache_dir: Path | None = None):
        self.folder = folder
        self.cache_dir = cache_dir
        self.packs: dict[str, tuple[Pack, bytes, str]] = {}  # nome -> (pack, bytes, versao)
        self._mtimes: dict[str, float] = {}
        self._keys: dict[str, tuple] = {}
        self.errors: dict[str, str] = {}

    def _key(self, name: str, paths: list[Path]) -> tuple:
        cfg = self.folder / f"{name}.json"
        parts = [(p.name, p.stat().st_size, p.stat().st_mtime_ns) for p in paths]
        if cfg.exists():
            parts.append((cfg.name, cfg.stat().st_size, cfg.stat().st_mtime_ns))
        return (CONVERTER_VERSION, tuple(parts))

    def _cached(self, name: str, key: tuple) -> Pack | None:
        if self._keys.get(name) == key and name in self.packs:
            return self.packs[name][0]
        if not self.cache_dir:
            return None
        try:
            with open(self.cache_dir / f"{name}.pkl", "rb") as f:
                k, pk = pickle.load(f)
            return pk if k == key else None
        except (OSError, pickle.PickleError, EOFError, ValueError, AttributeError):
            return None

    def _store(self, name: str, key: tuple, pk: Pack) -> None:
        if not self.cache_dir:
            return
        try:
            self.cache_dir.mkdir(parents=True, exist_ok=True)
            with open(self.cache_dir / f"{name}.pkl", "wb") as f:
                pickle.dump((key, pk), f)
        except OSError:
            pass

    def refresh(self) -> bool:
        """True se algo mudou."""
        if not self.folder.is_dir():
            changed = bool(self.packs)
            self.packs = {}
            self._mtimes = {}  # se a pasta voltar com os mesmos arquivos, reconverte
            return changed
        files = sorted(p for p in self.folder.iterdir() if p.suffix.lower() in EXTS | {".json"})
        mt = {str(p): p.stat().st_mtime for p in files}
        if mt == self._mtimes:
            return False
        self._mtimes = mt
        groups: dict[str, list[Path]] = {}
        for p in files:
            if p.suffix.lower() in EXTS and not is_preview(p):
                groups.setdefault(pet_name(p), []).append(p)
        best: dict[str, Pack] = {}
        self.errors = {}
        keys = {}
        for name, paths in groups.items():
            try:
                key = keys[name] = self._key(name, paths)
                pk = self._cached(name, key)
                if pk is None:
                    pk = build(name, paths, load_config(self.folder, name))
                    self._store(name, key, pk)
                best[name] = pk
            except Exception as e:  # imagem ruim nao derruba o backend
                self.errors[name] = str(e)
        self._keys = keys
        new = {}
        for n, pk in best.items():
            data = pk.to_bytes()
            new[n] = (pk, data, hashlib.sha1(data).hexdigest()[:8])
        changed = {n: v for n, (_, _, v) in new.items()} != {n: v for n, (_, _, v) in self.packs.items()}
        self.packs = new
        return changed

    def listing(self) -> list[dict]:
        return [{"n": n, "v": v} for n, (_, _, v) in sorted(self.packs.items())]

    def get(self, name: str) -> bytes | None:
        e = self.packs.get(name)
        return e[1] if e else None

    def summary(self) -> dict:
        return {
            "pets": {n: {"arquivos": pk.source, "quadros": [ROLES[r] for r in pk.roles],
                         "gerados": list(pk.generated), "grade": f"{pk.w}x{pk.h}",
                         "cores": len(pk.palette) - 1, "bloco_px": round(pk.cell, 1), "versao": v}
                     for n, (pk, _, v) in self.packs.items()},
            "erros": self.errors,
        }
