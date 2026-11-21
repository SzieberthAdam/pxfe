/* make_testdata.c - writes testdata/lib (text folders pxgl/ pxfm/ pxtf/ palette/), testdata/lib.pxfdb, testdata/lib_colors.pxfdb
 * and testdata/lib_many.pxfdb with pxfe's own code.
 * Run from the pxfe directory after deleting testdata/lib, testdata/lib.pxfdb, testdata/lib_colors.pxfdb and testdata/lib_many.pxfdb:
 * tests\make_testdata.exe
 * The fonts of lib are those of the spec examples (the grey one is the colour example of pxfm.md) plus one with uncoded glyphs,
 * a kerning pair naming an F-code and a default glyph.  lib_colors (for the Colours filter) has four fonts of 1, 2, 2 and 3
 * colours: typeface 1 holds the 3-colour font, typeface 2 the 1-colour example and an italic 2-colour font without ASCENT,
 * and the grey font belongs to no typeface.  Each pxfdb is made from its files by the store (store_import_folder). */
#include "pxfont.h"
#include "pxtf.h"
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIB "testdata/lib"
#define LIBC "tests/tmp/lib_colors"
#define LIBM "tests/tmp/lib_many"

static PxGlyph make_glyph(int cp, int adv, int x, int y, const char *rows) {
    PxGlyph g;
    memset(&g, 0, sizeof g);
    g.code = px_code_cp(cp); g.advance = adv; g.x = x; g.y = y;
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

/* The glyphs' characters have the colours cc[k] (the character PX_PIXELS[1 + k]), as in pxfe 21, which had one palette for the font:
 * each glyph gets its colours in reading order, then the font's other colours (the palette pxfe 21 wrote, so the files stay the same). */
static void font_colours(PxFont *f, const uint32_t *cc, int n) {
    uint32_t map[PX_NUM_PIXELS];
    for (int k = 0; k < PX_NUM_PIXELS; k++) map[k] = k >= 1 && k - 1 < n ? cc[k - 1] : 0x000000FFu;
    for (int i = 0; i < f->count; i++) {
        PxGlyph *g = &f->glyphs[i];
        if (!g->pix) continue;
        px_glyph_set_colors(f, g, map);
        const PxPalette *p = px_glyph_palette(f, g);
        uint32_t ext[64];
        int m = p->n;
        memcpy(ext, p->c, sizeof(uint32_t) * (size_t)m);
        for (int k = 0; k < n; k++) {
            bool have = false;
            for (int j = 0; j < m; j++) if (ext[j] == cc[k]) have = true;
            if (!have) ext[m++] = cc[k];
        }
        px_glyph_set_palette(f, g, ext, m);
    }
}

/* the 40 colours of the many-colour font */
static uint32_t many_colour(int k) {
    return (uint32_t)(0x20 + k * 5) << 24 | (uint32_t)(0xF0 - k * 5) << 16 | (uint32_t)(0x40 + (k % 4) * 0x30) << 8 | 0xFFu;
}

static void must(bool ok, const char *what, const char *err) {
    if (!ok) { fprintf(stderr, "%s: %s\n", what, err ? err : ""); exit(1); }
}

static void upper(const char *lower, char *out) {
    for (int i = 0; i < 32; i++) out[i] = (char)((lower[i] >= 'a' && lower[i] <= 'f') ? lower[i] - 32 : lower[i]);
    out[32] = 0;
}

static const char *g_lib = LIB;

static void save_font(PxFont *f, char hash[33]) {
    char err[PX_ERR_LEN], path[1024];
    must(px_save_final(f, g_lib, hash, path, sizeof path, err), "save font", err);
    printf("pxfm %s\n", hash);
    px_font_free(f);
}

/* a licence text (license.md) under <lib>/license/; its hash in upper case */
static void save_license(const char *text, char up[33]) {
    char err[PX_ERR_LEN] = "", h[33], dir[600], path[700];
    size_t n = 0;
    char *can = pxtf_license_canonical(text, strlen(text), &n, err);
    must(can != NULL, "licence text", err);
    px_hash128_hex(can, n, h, false);
    snprintf(dir, sizeof dir, "%s/license/%c%c", g_lib, h[0], h[1]);
    snprintf(path, sizeof path, "%s/%s.txt", dir, h);
    must(px_mkdir_p(dir) && px_write_file(path, can, n), "write the licence text", path);
    printf("license %s\n", h);
    upper(h, up);
    free(can);
}

static void save_typeface(const char *text) {
    PxtfTypeface c;
    char err[PX_ERR_LEN], hash[33], path[1024];
    must(pxtf_parse(text, strlen(text), &c, err), "parse typeface", err);
    must(pxtf_save(&c, g_lib, hash, path, sizeof path, err), "save typeface", err);
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
    px_font_set_default_from(&f, px_font_find_cp(&f, 0x20));
    px_font_set_kern(&f, px_code_cp(0x41), px_code_cp(0x4D), -1);
    save_font(&f, hex[0]);

    /* 2. the grey font of the spec (a palette) */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x41, 6, 0, 0, ROWS_GREY_A);
    font_colours(&f, (const uint32_t[]){0xFFFFFFFFu, 0x808080FFu}, 2);
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
    px_font_set_default_from(&f, px_font_find_cp(&f, 0x48));
    px_font_set_kern(&f, px_code_cp(0x41), px_code_symbol(0), -1);
    px_font_set_kern(&f, px_code_symbol(1), px_code_symbol(0), 1);
    save_font(&f, hex[3]);
    for (int i = 0; i < 4; i++) upper(hex[i], up[i]);

    /* typefaces: Test Sans with licence terms (0001 free with credit and a licence text, 0002 personal use, derived), Uncoded Mono with none */
    char lic[33];
    save_license("Test Licence\r\n\r\n\r\nYou may use, change and pass on the pxfe test fonts,   \n"
                 "if you credit pxfe tests and pass this text on with them.\n\n", lic);
    char text[4096];
    snprintf(text, sizeof text,
             "NAME\tTest Sans\n"
             "0001\n\tCOPYRIGHT\t(c) pxfe tests\n\tDESIGNER\t\xC3\x81" "d\xC3\xA1m Test\n\tNAME\tTest Sans Regular\n"
             "\tPXFM\t%s\n\tSLANT\tR\n\tSOURCE-FORMAT\tBDF\n\tSOURCE-HASH\tBCC7A744D44F7F35CBFAA1EFB0048A63\n\tSOURCE-TYPE\tbdf\n\tSTYLE\tRegular\n"
             "\tTERMS-CLASS\tcc-by\n\tTERMS-COMMERCIAL\tyes\n\tTERMS-CONFIDENCE\thigh\n\tTERMS-DATE\t2026-11-21\n"
             "\tTERMS-EVIDENCE\tYou may use, change and pass on the pxfe test fonts, if you credit pxfe tests.\n\tTERMS-LICENSE\tCC BY 4.0\n"
             "\tTERMS-MODIFY\tyes\n\tTERMS-OBLIGATION\tattribution\n\tTERMS-OBLIGATION\tinclude-license\n\tTERMS-PROVENANCE\toriginal\n"
             "\tTERMS-REDISTRIBUTE\tyes\n\tTERMS-REDISTRIBUTE-MODIFIED\tyes\n\tTERMS-SOURCE\treadme\n\tTERMS-SPDX\tCC-BY-4.0\n\tTERMS-TEXT\t%s\n"
             "\tWEIGHTNAME\tMedium\n"
             "0002\n\tITALIC\n\tPXFM\t%s\n\tSLANT\tI\n\tSOURCE-FORMAT\tBDF\n\tSOURCE-HASH\tBCC7A744D44F7F35CBFAA1EFB0048A63\n\tSOURCE-TYPE\tbdf\n\tSTYLE\tItalic\n"
             "\tTERMS-CLASS\tfree-personal\n\tTERMS-COMMERCIAL\tno\n\tTERMS-CONFIDENCE\tlow\n\tTERMS-DERIVED-FROM\tIBM VGA ROM font\n"
             "\tTERMS-MODIFY\task\n\tTERMS-NOTE\tFree for personal use; ask the author for a commercial licence.\n\tTERMS-PROVENANCE\tderived\n"
             "\tTERMS-REDISTRIBUTE\tnot-stated\n\tTERMS-REDISTRIBUTE-MODIFIED\tno\n\tTERMS-UPSTREAM-HOLDER\tIBM\n\tTERMS-UPSTREAM-STATUS\tunknown\n"
             "\tTERMS-USE-LIMIT\tpersonal-only\n",
             up[0], lic, up[2]);
    save_typeface(text);
    snprintf(text, sizeof text,
             "NAME\tUncoded Mono\n"
             "0001\n\tCELL\t6x8\n\tCODEPAGE\tcp437-graphic\n\tNAME\tUncoded Mono Regular\n"
             "\tPXFM\t%s\n\tSOURCE-FORMAT\tRAW\n\tSOURCE-HASH\t0123456789ABCDEF0123456789ABCDEF\n\tSOURCE-TYPE\traw-msb\n\tSTYLE\tRegular\n",
             up[3]);
    save_typeface(text);

    /* the pxfdb, built from the files by the store */
    Store *s = NULL;
    must(store_import_folder(LIB, "testdata/lib.pxfdb", NULL, &s, err), "import the folder into testdata/lib.pxfdb", err);
    store_close(s);
    printf("testdata/lib.pxfdb written\n");

    /* lib_colors: its files in tests/tmp/lib_colors, then the pxfdb */
    g_lib = LIBC;
    char hc[4][33], uc[4][33];
    px_font_init(&f);           /* 1 colour: the pxfm example (as in lib) */
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
    px_font_set_default_from(&f, px_font_find_cp(&f, 0x20));
    px_font_set_kern(&f, px_code_cp(0x41), px_code_cp(0x4D), -1);
    save_font(&f, hc[0]);
    px_font_init(&f);           /* 2 colours: the grey font, in no typeface */
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x41, 6, 0, 0, ROWS_GREY_A);
    font_colours(&f, (const uint32_t[]){0xFFFFFFFFu, 0x808080FFu}, 2);
    save_font(&f, hc[1]);
    px_font_init(&f);           /* 2 colours, no ASCENT / DESCENT: black and grey */
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x2C, 3, 0, -2, ROWS_COMMA);
    add(&f, 0x41, 6, -1, 0, ROWS_GREY_A);
    font_colours(&f, (const uint32_t[]){0x000000FFu, 0x808080FFu}, 2);
    save_font(&f, hc[2]);
    px_font_init(&f);           /* 3 colours: black, grey and red, monospaced */
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    f.monospaced = true;
    add(&f, 0x20, 6, 0, 0, NULL);
    add(&f, 0x41, 6, 0, 0, ROWS_GREY_A);
    add(&f, 0x48, 6, 0, 0, "0...0\n0...0\n02220\n0...0\n0...0");
    font_colours(&f, (const uint32_t[]){0x000000FFu, 0x808080FFu, 0xC04040FFu}, 3);
    save_font(&f, hc[3]);
    for (int i = 0; i < 4; i++) upper(hc[i], uc[i]);
    snprintf(text, sizeof text,
             "NAME\tThree Colours\n"
             "0001\n\tCELL\t6x8\n\tPXFM\t%s\n\tSOURCE-FORMAT\tRAW\n\tSOURCE-HASH\t0123456789ABCDEF0123456789ABCDEF\n\tSOURCE-TYPE\traw-msb\n\tSTYLE\tRegular\n", uc[3]);
    save_typeface(text);
    snprintf(text, sizeof text,
             "NAME\tTest Sans\n"
             "0001\n\tNAME\tTest Sans Regular\n\tPXFM\t%s\n\tSOURCE-FORMAT\tBDF\n\tSOURCE-HASH\tBCC7A744D44F7F35CBFAA1EFB0048A63\n\tSOURCE-TYPE\tbdf\n\tSTYLE\tRegular\n"
             "0002\n\tITALIC\n\tPXFM\t%s\n\tSOURCE-FORMAT\tBDF\n\tSOURCE-HASH\tBCC7A744D44F7F35CBFAA1EFB0048A63\n\tSOURCE-TYPE\tbdf\n\tSTYLE\tItalic\n", uc[0], uc[2]);
    save_typeface(text);
    must(store_import_folder(LIBC, "testdata/lib_colors.pxfdb", NULL, &s, err), "import the folder into testdata/lib_colors.pxfdb", err);
    store_close(s);
    printf("testdata/lib_colors.pxfdb written\n");

    /* lib_many: one font of 40 colours (more than a glyph may have, as the spec allows): A..T two colours each (the colours 2i and 2i+1),
     * U the colours of A (a shared palette), V the first colour of A only (the palette of A, longer than it needs), W a 6x6 block of the
     * colours 0..35 (a glyph with all its 36 colours), and the space.  Its files are in tests/tmp/lib_many. */
    g_lib = LIBM;
    char hm[33];
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    add(&f, 0x20, 4, 0, 0, NULL);
    for (int i = 0; i < 20; i++) {
        add(&f, 0x41 + i, 6, 0, 0, "0000\n0..0\n1111\n1..1\n1..1");
        uint32_t pc[2] = {many_colour(2 * i), many_colour(2 * i + 1)};
        px_glyph_set_palette(&f, &f.glyphs[px_font_find_cp(&f, 0x41 + i)], pc, 2);
    }
    add(&f, 0x55, 6, 0, 0, "0..0\n0..0\n0..0\n0..0\n.11.");
    add(&f, 0x56, 6, 0, 0, "0...0\n0...0\n.0.0.\n.0.0.\n..0..");
    {
        uint32_t pa[2] = {many_colour(0), many_colour(1)};
        px_glyph_set_palette(&f, &f.glyphs[px_font_find_cp(&f, 0x55)], pa, 2);
        px_glyph_set_palette(&f, &f.glyphs[px_font_find_cp(&f, 0x56)], pa, 2);
    }
    {
        char rows[64];
        int o = 0;
        for (int y = 0; y < 6; y++) { for (int x = 0; x < 6; x++) rows[o++] = PX_PIXELS[1 + y * 6 + x]; rows[o++] = y < 5 ? '\n' : 0; }
        add(&f, 0x57, 7, 0, 0, rows);
        uint32_t pw[36];
        for (int k = 0; k < 36; k++) pw[k] = many_colour(k);
        px_glyph_set_palette(&f, &f.glyphs[px_font_find_cp(&f, 0x57)], pw, 36);
    }
    must(px_font_colors(&f) == 40, "the many-colour font", "has not 40 colours");
    save_font(&f, hm);
    must(store_import_folder(LIBM, "testdata/lib_many.pxfdb", NULL, &s, err), "import the folder into testdata/lib_many.pxfdb", err);
    store_close(s);
    printf("testdata/lib_many.pxfdb written\n");
    return 0;
}
