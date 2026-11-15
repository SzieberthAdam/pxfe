/* make_testdata.c - writes testdata/lib (text folders pxgl/ pxfm/ pxtf/) and testdata/lib.pxfdb with pxfe's own code.
 * Run from the pxfe directory after deleting testdata/lib and testdata/lib.pxfdb:  tests\make_testdata.exe
 * The fonts are those of the spec examples plus one with uncoded glyphs, a kerning pair naming an F-code and a
 * default glyph.  The pxfdb is made from the files by the store (store_import_folder). */
#include "pxfont.h"
#include "pxtf.h"
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIB "testdata/lib"

static PxGlyph make_glyph(int cp, int adv, int x, int y, const char *rows) {
    PxGlyph g;
    memset(&g, 0, sizeof g);
    g.codepoint = cp; g.advance = adv; g.x = x; g.y = y;
    if (rows && rows[0]) {
        int w = 0, h = 0;
        const char *p = rows;
        while (*p && *p != '\n') { w++; p++; }
        for (p = rows; *p; p++) if (*p == '\n') h++;
        if (rows[strlen(rows) - 1] != '\n') h++;
        g.w = w; g.h = h;
        g.pix = (char *)malloc((size_t)w * (size_t)h);
        int o = 0;
        for (p = rows; *p; p++) if (*p != '\n') g.pix[o++] = *p;
    }
    return g;
}
static void add(PxFont *f, int cp, int adv, int x, int y, const char *rows) {
    PxGlyph g = make_glyph(cp, adv, x, y, rows);
    px_font_add_glyph(f, &g);
    px_glyph_free(&g);
}
static void add_unc(PxFont *f, int adv, int x, int y, const char *rows) {
    PxGlyph g = make_glyph(0, adv, x, y, rows);
    px_font_add_uncoded(f, &g);
    px_glyph_free(&g);
}

static const char *ROWS_COMMA = ".0\n0.";
static const char *ROWS_A = "..0..\n.0.0.\n0...0\n00000\n0...0";
static const char *ROWS_H = "0...0\n0...0\n00000\n0...0\n0...0";
static const char *ROWS_M = "0...0\n00.00\n0.0.0\n0...0\n0...0";
static const char *ROWS_X = "0...0\n.0.0.\n.0.0.\n0...0";
static const char *ROWS_GREY_A = "..1..\n.101.\n10.01\n00000\n0...0";

static void must(bool ok, const char *what, const char *err) {
    if (!ok) { fprintf(stderr, "%s: %s\n", what, err ? err : ""); exit(1); }
}

static void upper(const char *lower, char *out) {
    for (int i = 0; i < 32; i++) out[i] = (char)((lower[i] >= 'a' && lower[i] <= 'f') ? lower[i] - 32 : lower[i]);
    out[32] = 0;
}

static void save_font(PxFont *f, char hash[33]) {
    char err[PX_ERR_LEN], path[1024];
    must(px_save_final(f, LIB, hash, path, sizeof path, err), "save font", err);
    printf("pxfm %s\n", hash);
    px_font_free(f);
}

static void save_typeface(const char *text) {
    PxtfTypeface c;
    char err[PX_ERR_LEN], hash[33], path[1024];
    must(pxtf_parse(text, strlen(text), &c, err), "parse typeface", err);
    must(pxtf_save(&c, LIB, hash, path, sizeof path, err), "save typeface", err);
    printf("pxtf %s\n", hash);
    pxtf_free(&c);
}

int main(void) {
    char err[PX_ERR_LEN], hex[4][33], up[4][33];
    PxFont f;

    /* 1. the pxfm example of the spec */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    f.has[PXM_LINEGAP] = true; f.m[PXM_LINEGAP] = 1;
    f.has[PXM_CAPHEIGHT] = true; f.m[PXM_CAPHEIGHT] = 5;
    f.has[PXM_XHEIGHT] = true; f.m[PXM_XHEIGHT] = 4;
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x2C, 3, 0, -2, ROWS_COMMA);
    add(&f, 0x41, 6, 0, 0, ROWS_A);
    add(&f, 0x48, 6, 0, 0, ROWS_H);
    add(&f, 0x4D, 6, 0, 0, ROWS_M);
    add(&f, 0x78, 6, 0, 0, ROWS_X);
    px_font_set_default_from(&f, px_font_find(&f, 0x20));
    px_font_set_kern(&f, 0x41, 0x4D, -1);
    save_font(&f, hex[0]);

    /* 2. the grey font of the spec (a palette) */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    f.hasPalette = true;
    memset(f.colorUsed, 0, sizeof f.colorUsed);
    f.colorUsed[0] = f.colorUsed[1] = f.colorUsed[2] = true;
    f.color[0] = 0x00000000u; f.color[1] = 0xFFFFFFFFu; f.color[2] = 0x808080FFu;
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x41, 6, 0, 0, ROWS_GREY_A);
    save_font(&f, hex[1]);

    /* 3. no ASCENT / DESCENT, an offset of -1 */
    px_font_init(&f);
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x2C, 3, 0, -2, ROWS_COMMA);
    add(&f, 0x41, 6, -1, 0, ROWS_A);
    save_font(&f, hex[2]);

    /* 4. uncoded glyphs F00000 and F00001, kerning that names F-codes, a default glyph (the one of U+0048) */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    f.hasRatio = true; f.ratioA = 1; f.ratioB = 1;
    f.monospaced = true;
    add(&f, 0x20, 6, 0, 0, NULL);
    add(&f, 0x41, 6, 0, 0, ROWS_A);
    add(&f, 0x48, 6, 0, 0, ROWS_H);
    add_unc(&f, 6, 0, 0, ROWS_X);
    add_unc(&f, 6, 0, 0, ROWS_M);
    px_font_set_default_from(&f, px_font_find(&f, 0x48));
    px_font_set_kern(&f, 0x41, 0xF00000, -1);
    px_font_set_kern(&f, 0xF00001, 0xF00000, 1);
    save_font(&f, hex[3]);
    for (int i = 0; i < 4; i++) upper(hex[i], up[i]);

    /* typefaces */
    char text[2048];
    snprintf(text, sizeof text,
             "FORMAT\tBDF\nHASH\tBCC7A744D44F7F35CBFAA1EFB0048A63\nTYPE\tbdf\n"
             "0001\n\tCOPYRIGHT\t(c) pxfe tests\n\tDESIGNER\t\xC3\x81" "d\xC3\xA1m Test\n\tFAMILY\tTest Sans\n\tFULLNAME\tTest Sans Regular\n"
             "\tPXFM\t%s\n\tSLANT\tR\n\tSTYLE\tRegular\n\tWEIGHTNAME\tMedium\n"
             "0002\n\tFAMILY\tTest Sans\n\tITALIC\n\tPXFM\t%s\n\tSLANT\tI\n\tSTYLE\tItalic\n",
             up[0], up[2]);
    save_typeface(text);
    snprintf(text, sizeof text,
             "FORMAT\tRAW\nHASH\t0123456789ABCDEF0123456789ABCDEF\nTYPE\traw-msb\n"
             "0001\n\tCELL\t6x8\n\tCODEPAGE\tcp437-graphic\n\tFAMILY\tUncoded Mono\n\tFULLNAME\tUncoded Mono Regular\n"
             "\tPXFM\t%s\n\tSTYLE\tRegular\n",
             up[3]);
    save_typeface(text);

    /* the pxfdb, built from the files by the store */
    Store *s = NULL;
    must(store_import_folder(LIB, "testdata/lib.pxfdb", NULL, &s, err), "import the folder into testdata/lib.pxfdb", err);
    store_close(s);
    printf("testdata/lib.pxfdb written\n");
    return 0;
}
