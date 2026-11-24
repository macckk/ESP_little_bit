"""Gera fonte LVGL (lv_font_fmt_txt, bpp 4, sem compressao) a partir de TTF/WOFF usando Pillow.

Substitui o lv_font_conv (npm) para nao depender de Node. Sem kerning.

Uso:
  python tools/gen_lvgl_font.py --name font_pt_14 --size 14 \
      --font Montserrat-Medium.ttf 0x20-0x7E,0xA0-0xFF \
      --font FontAwesome5.woff 0xF00C,0xF1EB \
      -o src/fonts/font_pt_14.c
"""
import argparse

from PIL import Image, ImageDraw, ImageFont


def parse_ranges(spec):
    cps = []
    for part in spec.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            cps.extend(range(int(a, 0), int(b, 0) + 1))
        else:
            cps.append(int(part, 0))
    return cps


def render_glyph(font, cp, size):
    ch = chr(cp)
    adv = font.getlength(ch)
    pad = size * 2
    base = size * 3
    im = Image.new("L", (size * 5, size * 5), 0)
    ImageDraw.Draw(im).text((pad, base), ch, font=font, fill=255, anchor="ls")
    bbox = im.getbbox()
    if not bbox:
        return dict(cp=cp, adv=adv, w=0, h=0, ox=0, oy=0, px=[])
    x0, y0, x1, y1 = bbox
    crop = im.crop(bbox)
    return dict(cp=cp, adv=adv, w=x1 - x0, h=y1 - y0, ox=x0 - pad, oy=base - y1,
                px=list(crop.tobytes()))


def pack4(px):
    out = bytearray()
    for i in range(0, len(px), 2):
        hi = px[i] >> 4
        lo = (px[i + 1] >> 4) if i + 1 < len(px) else 0
        out.append((hi << 4) | lo)
    return out


def contiguous_runs(cps):
    runs, start, prev = [], cps[0], cps[0]
    for c in cps[1:]:
        if c != prev + 1:
            runs.append((start, prev))
            start = c
        prev = c
    runs.append((start, prev))
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--size", type=int, required=True)
    ap.add_argument("--font", nargs=2, action="append", metavar=("ARQ", "FAIXAS"), required=True)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()

    glyphs = {}
    groups = []  # (lista de cps, compacta?) - cada --font vira 1+ cmaps
    for path, spec in a.font:
        font = ImageFont.truetype(path, a.size)
        cps = sorted(set(parse_ranges(spec)))
        for cp in cps:
            glyphs[cp] = render_glyph(font, cp, a.size)
        groups.append(cps)

    order = sorted(glyphs)
    bitmap = bytearray()
    dsc = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id 0 reservado */"]
    gid = {}
    for cp in order:
        g = glyphs[cp]
        gid[cp] = len(dsc)
        adv16 = int(round(g["adv"] * 16))
        assert adv16 < 4096 and -128 <= g["ox"] < 128 and -128 <= g["oy"] < 128 and g["w"] < 256 and g["h"] < 256
        dsc.append(f"    {{.bitmap_index = {len(bitmap)}, .adv_w = {adv16}, .box_w = {g['w']}, "
                   f".box_h = {g['h']}, .ofs_x = {g['ox']}, .ofs_y = {g['oy']}}} /* U+{cp:04X} */")
        bitmap += pack4(g["px"])
    assert len(bitmap) < (1 << 20)

    # metricas de linha a partir das caixas reais (como o lv_font_conv)
    tops = [g["oy"] + g["h"] for g in glyphs.values() if g["h"]]
    bottoms = [g["oy"] for g in glyphs.values() if g["h"]]
    line_height = max(tops) - min(bottoms)
    base_line = -min(bottoms)

    # cmaps: faixas continuas viram FORMAT0_TINY; o resto de cada grupo, SPARSE_TINY
    cmaps, lists = [], []
    for cps in groups:
        runs = contiguous_runs(cps)
        singles = [c for r in runs if r[1] - r[0] < 3 for c in range(r[0], r[1] + 1)]
        for r0, r1 in runs:
            if r1 - r0 >= 3:
                cmaps.append(f"    {{.range_start = {r0}, .range_length = {r1 - r0 + 1}, .glyph_id_start = {gid[r0]}, "
                             f".unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
                             f".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}}")
        if singles:
            # SPARSE_TINY exige ids consecutivos na ordem de unicode_list
            ids = [gid[c] for c in singles]
            assert ids == list(range(ids[0], ids[0] + len(ids))), "ids nao consecutivos"
            n = len(lists)
            lists.append(f"static const uint16_t unicode_list_{n}[] = {{"
                         + ", ".join(str(c - singles[0]) for c in singles) + "};")
            cmaps.append(f"    {{.range_start = {singles[0]}, .range_length = {singles[-1] - singles[0] + 1}, "
                         f".glyph_id_start = {ids[0]}, .unicode_list = unicode_list_{n}, "
                         f".glyph_id_ofs_list = NULL, .list_length = {len(singles)}, "
                         f".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}}")

    lines = ["    " + ", ".join(f"0x{b:02x}" for b in bitmap[i:i + 16]) + ","
             for i in range(0, len(bitmap), 16)]
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"""/* Gerado por tools/gen_lvgl_font.py - nao editar.
 * {a.name}: {a.size}px, bpp 4, {len(glyphs)} glifos, {len(bitmap)} bytes de bitmap
 * Fontes: {'; '.join(p.replace(chr(92), '/').split('/')[-1] + ' ' + s for p, s in a.font)}
 */
#include "lvgl.h"

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {{
{chr(10).join(lines)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{(',' + chr(10)).join(dsc)}
}};

{chr(10).join(lists)}

static const lv_font_fmt_txt_cmap_t cmaps[] = {{
{(',' + chr(10)).join(cmaps)}
}};

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = {len(cmaps)},
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
}};

const lv_font_t {a.name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line_height},
    .base_line = {base_line},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
}};
""")
    print(f"{a.out}: {len(glyphs)} glifos, bitmap {len(bitmap)} B, line_height {line_height}, base_line {base_line}")


if __name__ == "__main__":
    main()
