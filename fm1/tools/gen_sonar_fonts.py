#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Stéphane Faure (CDT Ambiance)
"""Génère firmware/sonar/sonar_fonts.h : les polices de l'écran du sonar, lissées sur 16 niveaux (4 bits).

Polices (SIL Open Font License 1.1, voir assets/fonts/) :
  Barlow Condensed Bold (chiffres), Barlow Bold et SemiBold (libellés), Cinzel Bold (nom du preset).

    python3 tools/gen_sonar_fonts.py FONTS_DIR     (FONTS_DIR : barlow/, barlowcondensed/, cinzel/ de google/fonts ofl/)
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent / "firmware" / "sonar" / "sonar_fonts.h"
ACCENTS = "ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜàâäçéèêëîïôöùûü−±·’…«»—"
TEXT = "".join(chr(c) for c in range(32, 127)) + ACCENTS
BIG = "0123456789:+−-% "

def fonts(d):
    d = Path(d)
    def cinzel_bold(size):
        f = ImageFont.truetype(str(d / "cinzel" / "Cinzel[wght].ttf"), size)
        try:
            f.set_variation_by_name("Bold")
        except Exception:
            pass
        return f
    cinzel = cinzel_bold(13)
    return [
        # nom C, police, caractères
        ("SN_F_BIG", ImageFont.truetype(str(d / "barlowcondensed" / "BarlowCondensed-Bold.ttf"), 84), BIG),
        ("SN_F_VAL", ImageFont.truetype(str(d / "barlowcondensed" / "BarlowCondensed-Bold.ttf"), 22), TEXT),
        ("SN_F_LAB", ImageFont.truetype(str(d / "barlow" / "Barlow-Bold.ttf"), 12), TEXT),
        ("SN_F_LAB14", ImageFont.truetype(str(d / "barlow" / "Barlow-Bold.ttf"), 14), TEXT),
        ("SN_F_SUB", ImageFont.truetype(str(d / "barlow" / "Barlow-SemiBold.ttf"), 15), TEXT),
        ("SN_F_HDR", cinzel, TEXT),
        ("SN_F_TITLE", cinzel_bold(24), TEXT),
    ]

def render(font, chars):
    asc, desc = font.getmetrics()
    glyphs, data = [], bytearray()
    for ch in sorted(set(chars), key=ord):
        box = font.getbbox(ch, anchor="ls")          # par rapport à l'origine sur la ligne de base
        adv = int(round(font.getlength(ch)))
        x0, y0, x1, y1 = box
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        off = len(data)
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            px = img.load()
            for y in range(h):
                row = []
                for x in range(w):
                    row.append((px[x, y] + 8) * 15 // 255 if px[x, y] else 0)
                if len(row) % 2:
                    row.append(0)
                for i in range(0, len(row), 2):
                    data.append(row[i] << 4 | row[i + 1])
        glyphs.append((ord(ch), w, h, x0, y0, adv, off))
    return asc, desc, glyphs, data

def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    out = ["/* SPDX-License-Identifier: GPL-3.0-only (les dessins de caractères : SIL OFL 1.1, assets/fonts/) */",
           "/* Généré par tools/gen_sonar_fonts.py : ne pas modifier à la main. */",
           "#ifndef SONAR_FONTS_H", "#define SONAR_FONTS_H", "",
           "typedef struct { uint16_t cp; uint8_t w, h; int8_t x, y; uint8_t adv; uint32_t off; } sn_glyph_t;",
           "typedef struct { uint8_t ascent, descent; uint16_t n; const sn_glyph_t *g; const uint8_t *data; } sn_font_t;",
           ""]
    total = 0
    for cname, font, chars in fonts(sys.argv[1]):
        asc, desc, glyphs, data = render(font, chars)
        total += len(data) + len(glyphs) * 12
        low = cname.lower()
        out.append(f"static const uint8_t {low}_data[{max(1, len(data))}] = {{")
        for i in range(0, len(data), 24):
            out.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        if not data:
            out.append("    0")
        out.append("};")
        out.append(f"static const sn_glyph_t {low}_g[{len(glyphs)}] = {{")
        for cp, w, h, x, y, adv, off in glyphs:
            out.append(f"    {{{cp}, {w}, {h}, {x}, {y}, {adv}, {off}}},")
        out.append("};")
        out.append(f"static const sn_font_t {cname} = {{{asc}, {desc}, {len(glyphs)}, {low}_g, {low}_data}};")
        out.append("")
    out += ["#endif", ""]
    OUT.write_text("\n".join(out), encoding="utf-8")
    print(f"-> {OUT}  (~{total // 1024} Ko en flash)")

if __name__ == "__main__":
    main()
