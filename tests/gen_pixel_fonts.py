#!/usr/bin/env python
"""Generates the pixel fonts drawn with outlines in testdata/outline/ used by tests/test_fit.c and the end-to-end scripts.

Run from the pxfe directory (needs fontTools):

    python -I tests/gen_pixel_fonts.py

The generated files are committed; the tests do not run this script.
The glyphs are a tiny hand-made 5 x 7 design (with two rows below the baseline), public domain (CC0 1.0).
The same bitmaps are written to testdata/outline/pixel_glyphs.txt, which the tests compare the results with.

Files made:

    PixelSquare.ttf   every pixel a square of 100 units on the grid (runs of pixels are one rectangle); 1000 units per em
    PixelShift.ttf    the same, the grid shifted by (30, -20) units and the pixels 104 x 96 units large: no common divisor
                      tells the grid, the lattice search must find it (100 x 100, phases 30 and -20)
    PixelDots.ttf     every pixel a separate diamond of 80 units on a lattice of 100: a font of dots
"""

import os
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "testdata", "outline")

# 9 rows: 7 above the baseline, 2 below; '#' is ink
GLYPHS = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#", ".....", "....."],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####.", ".....", "....."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###.", ".....", "....."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####.", ".....", "....."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####", ".....", "....."],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#....", ".....", "....."],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#", ".....", "....."],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###.", ".....", "....."],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####", ".....", "....."],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###.", ".....", "....."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "....."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###.", ".....", "....."],
    "a": [".....", ".....", ".###.", "....#", ".####", "#...#", ".####", ".....", "....."],
    "b": ["#....", "#....", "####.", "#...#", "#...#", "#...#", "####.", ".....", "....."],
    "c": [".....", ".....", ".###.", "#...#", "#....", "#...#", ".###.", ".....", "....."],
    "d": ["....#", "....#", ".####", "#...#", "#...#", "#...#", ".####", ".....", "....."],
    "e": [".....", ".....", ".###.", "#...#", "#####", "#....", ".###.", ".....", "....."],
    "g": [".....", ".....", ".####", "#...#", "#...#", "#...#", ".####", "....#", ".###."],
    "h": ["#....", "#....", "####.", "#...#", "#...#", "#...#", "#...#", ".....", "....."],
    "i": ["..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###.", ".....", "....."],
    "l": [".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###.", ".....", "....."],
    "n": [".....", ".....", "####.", "#...#", "#...#", "#...#", "#...#", ".....", "....."],
    "o": [".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###.", ".....", "....."],
    "p": [".....", ".....", "####.", "#...#", "#...#", "#...#", "####.", "#....", "#...."],
    "t": [".#...", ".#...", "####.", ".#...", ".#...", ".#..#", "..##.", ".....", "....."],
    "u": [".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####", ".....", "....."],
    "x": [".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", ".....", "....."],
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###.", ".....", "....."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###.", ".....", "....."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####", ".....", "....."],
    "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####.", ".....", "....."],
    ".": [".....", ".....", ".....", ".....", ".....", ".##..", ".##..", ".....", "....."],
    "-": [".....", ".....", ".....", "#####", ".....", ".....", ".....", ".....", "....."],
}
UNIT = 100
ABOVE = 7  # rows above the baseline


def name_of(ch):
    return {".": "period", "-": "hyphen", "0": "zero", "1": "one", "2": "two", "3": "three"}.get(ch, ch)


def squares(rows, dx=0, dy=0, grow_x=0, grow_y=0):
    """One rectangle per horizontal run of pixels, wound clockwise (TrueType)."""
    out = []
    for r, row in enumerate(rows):
        y1 = (ABOVE - r) * UNIT + dy + grow_y / 2
        y0 = (ABOVE - r - 1) * UNIT + dy - grow_y / 2
        c = 0
        while c < len(row):
            if row[c] != "#":
                c += 1
                continue
            e = c
            while e < len(row) and row[e] == "#":
                e += 1
            x0, x1 = c * UNIT + dx - grow_x / 2, e * UNIT + dx + grow_x / 2
            out.append([(x0, y0), (x0, y1), (x1, y1), (x1, y0)])
            c = e
    return out


def diamonds(rows, size=80):
    out = []
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch == "#":
                cx, cy, h = c * UNIT + UNIT / 2, (ABOVE - r - 1) * UNIT + UNIT / 2, size / 2
                out.append([(cx - h, cy), (cx, cy + h), (cx + h, cy), (cx, cy - h)])
    return out


def build(path, family, contours_of):
    order = [".notdef", "space"] + [name_of(c) for c in GLYPHS]
    cmap = {0x20: "space"}
    cmap.update({ord(c): name_of(c) for c in GLYPHS})
    glyphs, metrics = {}, {}
    for name in (".notdef", "space"):
        glyphs[name] = TTGlyphPen(None).glyph()
        metrics[name] = (6 * UNIT, 0)
    for ch, rows in GLYPHS.items():
        pen = TTGlyphPen(None)
        xmin = None
        for contour in contours_of(rows):
            pts = [(int(round(x)), int(round(y))) for x, y in contour]
            pen.moveTo(pts[0])
            for p in pts[1:]:
                pen.lineTo(p)
            pen.closePath()
            m = min(p[0] for p in pts)
            xmin = m if xmin is None else min(xmin, m)
        glyphs[name_of(ch)] = pen.glyph()
        metrics[name_of(ch)] = (6 * UNIT, xmin or 0)
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=8 * UNIT, descent=-2 * UNIT)
    fb.setupNameTable({"familyName": family, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=8 * UNIT, sTypoDescender=-2 * UNIT, usWinAscent=8 * UNIT, usWinDescent=2 * UNIT)
    fb.setupPost()
    fb.save(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    build(os.path.join(OUT, "PixelSquare.ttf"), "Pixel Square", lambda rows: squares(rows))
    build(os.path.join(OUT, "PixelShift.ttf"), "Pixel Shift", lambda rows: squares(rows, 30, -20, 4, -4))
    build(os.path.join(OUT, "PixelDots.ttf"), "Pixel Dots", lambda rows: diamonds(rows))
    with open(os.path.join(OUT, "pixel_glyphs.txt"), "w", newline="\n") as f:
        for ch, rows in GLYPHS.items():
            f.write("%04X\n" % ord(ch))
            for row in rows:
                f.write(row + "\n")
    print("wrote 3 fonts and pixel_glyphs.txt to", OUT)


if __name__ == "__main__":
    sys.exit(main())
