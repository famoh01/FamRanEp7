#!/usr/bin/env python3
"""Renders the HUD text labels into app/src/main/assets/labels.bin.

Text is shaped with HarfBuzz (Thai vowels and tone marks need it) and each
label is rasterised to an 8-bit alpha bitmap with FreeType. The app uploads
each bitmap as a texture, so it does not shape text at run time.

Requires: pip install uharfbuzz freetype-py
Font: fonts/NotoSansThai-Merged-400.ttf (Noto Sans Thai, SIL OFL 1.1; see
fonts/OFL-NotoSansThai.txt).

Output (little endian): u32 count, then per label: u32 width, u32 height,
width*height alpha bytes, row by row from the top.
The order of LABELS must match enum LabelId in app/src/main/cpp/main.cpp.
"""
import os
import struct
import sys

import freetype
import uharfbuzz as hb

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "fonts", "NotoSansThai-Merged-400.ttf")
OUT = os.path.join(HERE, "..", "app", "src", "main", "assets", "labels.bin")

LABELS = [
    "เมนู", "ตัวละคร", "คลับ", "กระเป๋า", "สกิล",   # menu, character, club, bag, skill
    "วิ่ง", "เดิน", "ยา", "ออโต้", "เลือด", "%",     # run, walk, potion, auto, HP, percent sign
] + ["%d%%" % p for p in range(10, 100, 10)] + [  # 10% .. 90%
    "1", "2", "3", "4", "5", "6",                    # skill grid numbers
]

FONT_PX = 48   # rasterised size; the app scales each label down to the size it needs
PAD = 2


def render(text):
    blob = hb.Blob.from_file_path(FONT)
    font = hb.Font(hb.Face(blob))
    font.scale = (FONT_PX * 64, FONT_PX * 64)
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    hb.shape(font, buf, {"kern": True, "liga": True})

    face = freetype.Face(FONT)
    face.set_char_size(FONT_PX * 64)
    ascender = face.size.ascender / 64.0      # pixels above the baseline (positive)

    # Glyph bitmaps placed on a pen line (26.6 fixed point from HarfBuzz).
    pen = 0
    glyphs = []   # (x0, y0, w, h, alpha_rows) with y0 = top row relative to baseline
    for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
        gx = (pen + pos.x_offset) / 64.0
        gy = pos.y_offset / 64.0               # HarfBuzz y is up; applied as a baseline shift
        pen += pos.x_advance
        face.load_glyph(info.codepoint, freetype.FT_LOAD_NO_HINTING)
        face.glyph.render(freetype.FT_RENDER_MODE_NORMAL)
        bm = face.glyph.bitmap
        if bm.width == 0 or bm.rows == 0:
            continue
        rows = []
        for r in range(bm.rows):
            start = r * bm.pitch
            rows.append(bytes(bm.buffer[start:start + bm.width]))
        left = gx + face.glyph.bitmap_left
        top = -gy + face.glyph.bitmap_top      # distance from baseline up to the top row
        glyphs.append((left, top, bm.width, bm.rows, rows))

    if not glyphs:
        raise SystemExit("no glyphs for %r" % text)

    # Canvas: smallest box that holds every glyph, plus padding.
    min_x = min(g[0] for g in glyphs)
    max_x = max(g[0] + g[2] for g in glyphs)
    max_top = max(g[1] for g in glyphs)
    min_bottom = min(g[1] - g[3] for g in glyphs)
    height_top = max(max_top, ascender)
    height_bottom = min(min_bottom, 0.0)
    W = int(max_x - min_x + 0.999) + 2 * PAD
    H = int(height_top - height_bottom + 0.999) + 2 * PAD
    canvas = bytearray(W * H)
    base_y = PAD + height_top                  # baseline row position on the canvas

    for (x0, top, w, h, rows) in glyphs:
        ox = int(round(PAD + (x0 - min_x)))
        oy = int(round(base_y - top))
        for r in range(h):
            py = oy + r
            if py < 0 or py >= H:
                continue
            row = rows[r]
            for c in range(w):
                px = ox + c
                if 0 <= px < W:
                    i = py * W + px
                    if row[c] > canvas[i]:
                        canvas[i] = row[c]
    return W, H, bytes(canvas)


def main():
    out = os.path.normpath(OUT)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = bytearray(struct.pack("<I", len(LABELS)))
    previews = []
    for text in LABELS:
        w, h, alpha = render(text)
        data += struct.pack("<II", w, h) + alpha
        previews.append((text, w, h, alpha))
    with open(out, "wb") as f:
        f.write(data)
    print("wrote %s (%d labels, %d bytes)" % (out, len(LABELS), len(data)))
    if "--preview" in sys.argv:
        from PIL import Image
        strip_h = sum(p[2] + 6 for p in previews)
        strip_w = max(p[1] for p in previews) + 8
        img = Image.new("L", (strip_w, strip_h), 0)
        y = 0
        for _, w, h, alpha in previews:
            img.paste(Image.frombytes("L", (w, h), alpha), (4, y + 3))
            y += h + 6
        img.save(sys.argv[sys.argv.index("--preview") + 1])


if __name__ == "__main__":
    main()
