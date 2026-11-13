#!/usr/bin/env python
"""Generates the small test fonts in testdata/outline/ used by tests/test_outline.c.

Run from the pxfe directory (needs fontTools, which provides zlib-wrapping, the subsetter and the font builder):

    python -I tests/gen_outline_fonts.py

The generated files are committed; the tests do not run this script.
Every file is derived from assets/fonts/noto-sans (SIL OFL 1.1) or assets/fonts/unifont (GNU GPLv2+ with font embedding
exception / SIL OFL 1.1), see testdata/outline/README.txt.

Files made:

    NotoSub.ttf       Noto Sans Regular subset (TrueType glyf outlines, with the composite glyph eacute)
    NotoSub.woff      the same font as WOFF (zlib-compressed tables, by fontTools)
    NotoSub.eot       the same font in an uncompressed EOT (version 0x00020001, header built here)
    NotoSub.xor.eot   the same with the XOR 0x50 encryption flag set
    NotoSub.ttc       a collection of two faces: Noto Sans Regular subset and Noto Sans Bold subset
    NotoSubCff.otf    OpenType with CFF outlines: the Noto Sans subset glyphs redrawn as cubic Type 2 charstrings (fontTools
                      T2CharStringPen; quadratic curves are raised to cubic exactly)
    UnifontSub.otf    a real CFF font: subset of GNU Unifont 18.0.01 (a handful of glyphs)
    NotoSub.pfb       a Type 1 font written here: the same outlines as cubic charstrings, own Subrs with flex and hint
                      replacement, a custom Encoding, a seac composite (eacute) and three glyphs made by hand
                      (flexed bar, hint-replaced bar, triangle)
"""

import io
import os
import struct
import sys

from fontTools.agl import UV2AGL
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.basePen import BasePen
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTCollection, TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "testdata", "outline")
NOTO = os.path.join(ROOT, "assets", "fonts", "noto-sans")
UNIFONT = os.path.join(ROOT, "assets", "fonts", "unifont", "unifont-18.0.01.otf")

TEXT = "ABCDHOgaeox0123 .,é´"
BOLD_TEXT = "ABCabc0123 "


def subset(path, text, drop_names=False):
    opts = Options()
    opts.layout_features = []
    opts.hinting = False
    opts.notdef_outline = True
    opts.name_IDs = ["*"]
    opts.glyph_names = False
    opts.drop_tables += ["DSIG", "GDEF", "GPOS", "GSUB", "gasp"]
    font = TTFont(path)
    sub = Subsetter(opts)
    sub.populate(text=text)
    sub.subset(font)
    return font


def save(font, name, **kw):
    path = os.path.join(OUT, name)
    for k, v in kw.items():
        setattr(font, k, v)
    font.save(path)
    print(f"{name}: {os.path.getsize(path)} bytes")


def sfnt_bytes(font):
    buf = io.BytesIO()
    font.save(buf)
    return buf.getvalue()


def make_eot(font, xor):
    data = sfnt_bytes(font)
    names = font["name"]

    def s(i):
        n = names.getName(i, 3, 1, 0x409) or names.getName(i, 1, 0, 0)
        return str(n).encode("utf-16-le") if n else b""

    os2, head = font["OS/2"], font["head"]
    panose = os2.panose
    pan = bytes([panose.bFamilyType, panose.bSerifStyle, panose.bWeight, panose.bProportion, panose.bContrast,
                 panose.bStrokeVariation, panose.bArmStyle, panose.bLetterForm, panose.bMidline, panose.bXHeight])
    flags = 0x10000000 if xor else 0
    body = b""
    body += pan + struct.pack("<BBIHH", 1, 1 if os2.fsSelection & 1 else 0, os2.usWeightClass, os2.fsType, 0x504C)
    body += struct.pack("<IIII", os2.ulUnicodeRange1, os2.ulUnicodeRange2, os2.ulUnicodeRange3, os2.ulUnicodeRange4)
    body += struct.pack("<II", getattr(os2, "ulCodePageRange1", 0), getattr(os2, "ulCodePageRange2", 0))
    body += struct.pack("<I", head.checkSumAdjustment) + b"\0" * 16 + struct.pack("<H", 0)   # reserved, padding1
    for i in (1, 2, 5, 4):                                  # family, style, version, full name
        v = s(i)
        body += struct.pack("<H", len(v)) + v + struct.pack("<H", 0)
    body += struct.pack("<H", 0)                            # RootStringSize (no root string)
    font_data = bytes(b ^ 0x50 for b in data) if xor else data
    total = 16 + len(body) + len(font_data)
    header = struct.pack("<IIII", total, len(data), 0x00020001, flags)
    out = header + body + font_data
    assert len(out) == total
    return out


class Rec(BasePen):
    """Records contours as absolute cubic paths: [[start, [("l", p) | ("c", p1, p2, p3)...]]]."""

    def __init__(self, gs):
        super().__init__(gs)
        self.contours = []

    def _moveTo(self, pt):
        self.contours.append([pt, []])

    def _lineTo(self, pt):
        self.contours[-1][1].append(("l", pt))

    def _curveToOne(self, p1, p2, p3):
        self.contours[-1][1].append(("c", p1, p2, p3))

    def _closePath(self):
        pass


def reversed_contour(c):
    start, segs = c
    pts = [start]
    for sg in segs:
        pts.append(sg[-1])
    # close with an explicit line when the last point is not the start
    if pts[-1] != start:
        segs = segs + [("l", start)]
        pts.append(start)
    n = len(segs)
    rsegs = []
    for i in range(n - 1, -1, -1):
        sg = segs[i]
        prev = pts[i]
        if sg[0] == "l":
            rsegs.append(("l", prev))
        else:
            rsegs.append(("c", sg[2], sg[1], prev))
    return [pts[n], rsegs]


# ---------------------------------------------------------------------------------------------------------- Type 1

def t1num(v):
    v = int(round(v))
    if -107 <= v <= 107:
        return bytes([v + 139])
    if 108 <= v <= 1131:
        v -= 108
        return bytes([247 + (v >> 8), v & 255])
    if -1131 <= v <= -108:
        v = -v - 108
        return bytes([251 + (v >> 8), v & 255])
    return b"\xff" + struct.pack(">i", v)


OPS = {"hstem": 1, "vstem": 3, "vmoveto": 4, "rlineto": 5, "hlineto": 6, "vlineto": 7, "rrcurveto": 8, "closepath": 9,
       "callsubr": 10, "return": 11, "hsbw": 13, "endchar": 14, "rmoveto": 21, "hmoveto": 22, "vhcurveto": 30,
       "hvcurveto": 31, "seac": (12, 6), "callothersubr": (12, 16), "pop": (12, 17), "setcurrentpoint": (12, 33),
       "div": (12, 12)}


def cs(*items):
    """items: numbers and operator names -> unencrypted charstring."""
    out = b""
    for it in items:
        if isinstance(it, str):
            op = OPS[it]
            out += bytes(op) if isinstance(op, tuple) else bytes([op])
        else:
            out += t1num(it)
    return out


def encrypt(data, r, n=4):
    data = b"\0" * n + data
    out = bytearray()
    for b in data:
        c = b ^ (r >> 8)
        r = ((c + r) * 52845 + 22719) & 0xFFFF
        out.append(c)
    return bytes(out)


def outline_cs(width, contours):
    """A glyph charstring from absolute cubic contours (hint-less)."""
    xs = [p[0] for c in contours for sg in ([("l", c[0])] + c[1]) for p in sg[1:]]
    sbx = min(xs) if xs else 0
    items = [sbx, width, "hsbw"]
    x, y = sbx, 0
    for start, segs in contours:
        dx, dy = start[0] - x, start[1] - y
        if dy == 0:
            items += [dx, "hmoveto"]
        elif dx == 0:
            items += [dy, "vmoveto"]
        else:
            items += [dx, dy, "rmoveto"]
        x, y = start
        for sg in segs:
            if sg[0] == "l":
                px, py = sg[1]
                dx, dy = px - x, py - y
                if dy == 0:
                    items += [dx, "hlineto"]
                elif dx == 0:
                    items += [dy, "vlineto"]
                else:
                    items += [dx, dy, "rlineto"]
                x, y = px, py
            else:
                (x1, y1), (x2, y2), (x3, y3) = sg[1:]
                d1, d2, d3 = (x1 - x, y1 - y), (x2 - x1, y2 - y1), (x3 - x2, y3 - y2)
                if d1[1] == 0 and d3[0] == 0:
                    items += [d1[0], d2[0], d2[1], d3[1], "hvcurveto"]
                elif d1[0] == 0 and d3[1] == 0:
                    items += [d1[1], d2[0], d2[1], d3[0], "vhcurveto"]
                else:
                    items += [d1[0], d1[1], d2[0], d2[1], d3[0], d3[1], "rrcurveto"]
                x, y = x3, y3
        items += ["closepath"]
    items += ["endchar"]
    return cs(*items), sbx


def make_pfb(font):
    gs = font.getGlyphSet()
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    glyphs = {}          # name -> (charstring bytes)
    encoding = {}        # code -> name

    def contours_of(gname):
        pen = Rec(gs)
        gs[gname].draw(pen)
        def r(pt):
            return (int(round(pt[0])), int(round(pt[1])))

        rounded = [[r(c[0]), [(sg[0],) + tuple(r(q) for q in sg[1:]) for sg in c[1]]] for c in pen.contours]
        return [reversed_contour(c) for c in rounded]

    for cp in sorted(cmap):
        if cp in (0xE9,):
            continue                                          # eacute becomes a seac composite below
        gname = cmap[cp]
        if cp == 0x20:
            data = cs(0, hmtx[gname][0], "hsbw", "endchar")
        else:
            data, _ = outline_cs(hmtx[gname][0], contours_of(gname))
        t1name = UV2AGL.get(cp, "uni%04X" % cp)
        glyphs[t1name] = data
        if cp < 256:
            encoding[cp] = t1name
    # seac: eacute = e + acute moved by the offset of the TrueType composite
    comp = font["glyf"]["eacute"].components
    assert [c.glyphName for c in comp] == ["e", "acute"], [c.glyphName for c in comp]
    dx, dy = comp[1].x, comp[1].y
    acute_cs, acute_sbx = outline_cs(hmtx[comp[1].glyphName][0], contours_of(comp[1].glyphName))
    glyphs["acute"] = acute_cs                               # the accent as a glyph of its own (same outline)
    e_xmin = min(p[0] for c in contours_of("e") for sg in ([("l", c[0])] + c[1]) for p in sg[1:])
    comp_sbx = min(e_xmin, acute_sbx + dx)
    glyphs["eacute"] = cs(comp_sbx, hmtx["eacute"][0], "hsbw", acute_sbx, dx - comp_sbx + acute_sbx, dy, ord("e"), 194, "seac")
    encoding[0xE9] = "eacute"

    # glyphs made by hand
    # flexbar: rectangle 100..600 x 0..700 whose bottom edge is a (collinear) flex; hint ops, standard flex Subrs
    flex = [100, 700, "hsbw", 0, 700, "hstem", 100, 500, "vstem", 0, 0, "rmoveto", 1, "callsubr"]
    for d in (250, -150, 100, 50, 50, 100, 100):
        flex += [d, 0, "rmoveto", 2, "callsubr"]
    flex += [50, 600, 0, 0, "callsubr", 700, "vlineto", -500, "hlineto", "closepath", "endchar"]
    glyphs["uni25A0"] = cs(*flex)
    # plain bar with hint replacement (othersubr 3 through Subr 4)
    glyphs["u25A1"] = cs(100, 700, "hsbw", 4, 1, 3, "callothersubr", "pop", "callsubr", 0, 0, "rmoveto",
                         500, "hlineto", 700, "vlineto", -500, "hlineto", "closepath", "endchar")
    # triangle (0,0) (500,0) (250,700), counter-clockwise, code 126 by the Encoding only
    glyphs["foo"] = cs(0, 600, "hsbw", 0, 0, "rmoveto", 500, 0, "rlineto", -250, 700, "rlineto", "closepath", "endchar")
    encoding[126] = "foo"
    glyphs[".notdef"] = cs(50, 500, "hsbw", 0, 0, "rmoveto", 400, "hlineto", 700, "vlineto", -400, "hlineto", "closepath",
                           "endchar")

    subrs = [
        cs(3, 0, "callothersubr", "pop", "pop", "setcurrentpoint", "return"),
        cs(0, 1, "callothersubr", "return"),
        cs(0, 2, "callothersubr", "return"),
        cs("return"),
        cs(0, 700, "hstem", "return"),
    ]

    names = font["name"]
    family = str(names.getName(1, 3, 1, 0x409))
    notice = "Derived from Noto Sans, Copyright 2022 The Noto Project Authors, SIL Open Font License 1.1"

    clear = "%!PS-AdobeFont-1.0: NotoSansSub 1.0\n"
    clear += "11 dict begin\n/FontInfo 5 dict dup begin\n/version (1.0) readonly def\n"
    clear += f"/Notice ({notice}) readonly def\n/FullName ({family} Sub) readonly def\n"
    clear += f"/FamilyName ({family} Sub) readonly def\n/Weight (Regular) readonly def\nend readonly def\n"
    clear += "/FontName /NotoSansSub def\n/PaintType 0 def\n/FontType 1 def\n/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n"
    clear += "/Encoding 256 array\n0 1 255 {1 index exch /.notdef put} for\n"
    for code in sorted(encoding):
        clear += f"dup {code} /{encoding[code]} put\n"
    clear += "readonly def\n/FontBBox {-200 -300 1200 1000} readonly def\ncurrentdict end\ncurrentfile eexec\n"

    priv = b"dup /Private 8 dict dup begin\n"
    priv += b"/RD {string currentfile exch readstring pop} executeonly def\n/ND {noaccess def} executeonly def\n"
    priv += b"/NP {noaccess put} executeonly def\n/BlueValues [] def\n/MinFeature {16 16} def\n/password 5839 def\n"
    priv += b"/lenIV 4 def\n"
    priv += b"/Subrs %d array\n" % len(subrs)
    for i, sdata in enumerate(subrs):
        enc = encrypt(sdata, 4330)
        priv += b"dup %d %d RD " % (i, len(enc)) + enc + b" NP\n"
    priv += b"ND\n"
    order = [".notdef"] + sorted(n for n in glyphs if n != ".notdef")
    priv += b"2 index /CharStrings %d dict dup begin\n" % len(order)
    for n in order:
        enc = encrypt(glyphs[n], 4330)
        priv += b"/%s %d RD " % (n.encode(), len(enc)) + enc + b" ND\n"
    priv += b"end\nend\nreadonly put\nnoaccess put\ndup /FontName get exch definefont pop\nmark currentfile closefile\n"
    enc_priv = encrypt(priv, 55665)
    trailer = (b"0" * 64 + b"\n") * 8 + b"cleartomark\n"

    def seg(t, d):
        return bytes([0x80, t]) + struct.pack("<I", len(d)) + d

    data = seg(1, clear.encode("latin-1")) + seg(2, enc_priv) + seg(1, trailer) + bytes([0x80, 3])
    with open(os.path.join(OUT, "NotoSub.pfb"), "wb") as fh:
        fh.write(data)
    print(f"NotoSub.pfb: {len(data)} bytes")


# ------------------------------------------------------------------------------------------------------------- CFF

def make_cff_otf(font):
    gs = font.getGlyphSet()
    order = font.getGlyphOrder()
    cmap = font.getBestCmap()
    charstrings = {}
    for name in order:
        pen = T2CharStringPen(gs[name].width, gs)
        gs[name].draw(pen)
        charstrings[name] = pen.getCharString()
    upem = font["head"].unitsPerEm
    fb = FontBuilder(upem, isTTF=False)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupCFF("NotoSansSubCff-Regular", {"FullName": "Noto Sans Sub CFF Regular", "FamilyName": "Noto Sans Sub CFF"},
                charstrings, {})
    fb.setupHorizontalMetrics({n: (font["hmtx"][n][0], 0) for n in order})
    fb.setupHorizontalHeader(ascent=font["hhea"].ascent, descent=font["hhea"].descent)
    fb.setupNameTable({"familyName": "Noto Sans Sub CFF", "styleName": "Regular",
                       "copyright": "Derived from Noto Sans, Copyright 2022 The Noto Project Authors",
                       "licenseDescription": "SIL Open Font License, Version 1.1"})
    fb.setupOS2(sTypoAscender=font["hhea"].ascent, sTypoDescender=font["hhea"].descent, usWinAscent=1069, usWinDescent=293)
    fb.setupPost()
    path = os.path.join(OUT, "NotoSubCff.otf")
    fb.save(path)
    print(f"NotoSubCff.otf: {os.path.getsize(path)} bytes")


def main():
    os.makedirs(OUT, exist_ok=True)
    regular = subset(os.path.join(NOTO, "NotoSans-Regular.ttf"), TEXT)
    save(regular, "NotoSub.ttf")
    reg = TTFont(os.path.join(OUT, "NotoSub.ttf"))
    w = TTFont(os.path.join(OUT, "NotoSub.ttf"))
    w.flavor = "woff"
    w.save(os.path.join(OUT, "NotoSub.woff"))
    print("NotoSub.woff:", os.path.getsize(os.path.join(OUT, "NotoSub.woff")), "bytes")
    for xor, name in ((False, "NotoSub.eot"), (True, "NotoSub.xor.eot")):
        blob = make_eot(reg, xor)
        with open(os.path.join(OUT, name), "wb") as fh:
            fh.write(blob)
        print(f"{name}: {len(blob)} bytes")
    bold = subset(os.path.join(NOTO, "NotoSans-Bold.ttf"), BOLD_TEXT)
    ttc = TTCollection()
    ttc.fonts = [TTFont(os.path.join(OUT, "NotoSub.ttf")), bold]
    ttc.save(os.path.join(OUT, "NotoSub.ttc"))
    print("NotoSub.ttc:", os.path.getsize(os.path.join(OUT, "NotoSub.ttc")), "bytes")
    make_cff_otf(reg)
    uni = subset(UNIFONT, "ABab0")
    save(uni, "UnifontSub.otf")
    make_pfb(reg)


if __name__ == "__main__":
    sys.exit(main())
