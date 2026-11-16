#!/usr/bin/env python
"""Generates testdata/sheet/marker8.png and enlarged3.png from testdata/sheet/font8x8.png (CC0, see testdata/sheet/README.txt).

Run from the pxfe directory (needs Pillow):

    python -I tests/gen_sheet_fonts.py

The generated files are committed; the tests do not run this script.

    marker8.png    the glyphs '!' to '~' of font8x8.png as a strip with a marker row: white glyphs on dark blue, each between two
                   magenta marker pixels of the top row (the marker's column belongs to no glyph), glyphs of their own widths
    enlarged3.png  the first four rows of font8x8.png, every pixel drawn as a block of 3 x 3, on white
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = os.path.join(ROOT, "testdata", "sheet")
src = Image.open(os.path.join(D, "font8x8.png")).convert("RGBA")
px = src.load()


def cell(code):
    i = code - 32
    x0, y0 = (i % 15) * 8, (i // 15) * 8
    cols = [x for x in range(8) if any(px[x0 + x, y0 + y][3] > 127 for y in range(8))]
    a, b = (min(cols), max(cols)) if cols else (0, 2)
    return [[px[x0 + x, y0 + y][3] > 127 for x in range(a, b + 1)] for y in range(8)]


glyphs = [cell(c) for c in range(33, 127)]
w = sum(len(g[0]) + 3 for g in glyphs)   # marker column, a blank column, the glyph, a blank column
im = Image.new("RGB", (w, 9), (0, 0, 136))
x = 0
for g in glyphs:
    im.putpixel((x, 0), (252, 0, 252))
    for yy in range(8):
        for xx, on in enumerate(g[yy]):
            if on:
                im.putpixel((x + 2 + xx, 1 + yy), (252, 252, 252))
    x += len(g[0]) + 3
im.save(os.path.join(D, "marker8.png"))

big = Image.new("RGB", (120 * 3, 32 * 3), (255, 255, 255))
for y in range(32):
    for xx in range(120):
        if px[xx, y][3] > 127:
            for dy in range(3):
                for dx in range(3):
                    big.putpixel((xx * 3 + dx, y * 3 + dy), (20, 20, 60))
big.save(os.path.join(D, "enlarged3.png"))
print("wrote marker8.png and enlarged3.png")
