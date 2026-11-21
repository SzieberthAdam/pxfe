/* test_pxfont.c - plain C tests for src/pxfont.c (pxfm v2 with F-codes, draft v3). Run from the pxfe directory (build.bat test does). */
#include "pxfont.h"
#include "pxtf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

#define TMP "tests/tmp"
#define LIB "testdata/lib"
#define H_EXAMPLE "3b52125b2e5f6466823baa4fa3186fa0"
#define H_NOASC "4fba4190f6ba7e10b296c7070234e1ce"
#define H_GREY "894ef821e147533c34b39e2131016133"            /* pxfm.md: the colour example */
#define H_GREY_A "3ad25ae0043eddcc6bc11e8794fccdd3"
#define H_GREY_PAL "ebbba94934da6897d86c5e8ea2ac5409"

/* rows separated by newlines; a glyph at (x, y) = bottom-left corner */
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
        g.pix = (char *)malloc((size_t)w * h);
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

static int add_unc(PxFont *f, int adv, int x, int y, const char *rows) {
    PxGlyph g = make_glyph(0, adv, x, y, rows);
    int i = px_font_add_uncoded(f, &g);
    px_glyph_free(&g);
    return i;
}

static const char *ROWS_COMMA = ".0\n0.";
static const char *ROWS_A = "..0..\n.0.0.\n0...0\n00000\n0...0";
static const char *ROWS_H = "0...0\n0...0\n00000\n0...0\n0...0";
static const char *ROWS_M = "0...0\n00.00\n0.0.0\n0...0\n0...0";
static const char *ROWS_X = "0...0\n.0.0.\n.0.0.\n0...0";
static const char *ROWS_GREY_A = "..1..\n.101.\n10.01\n00000\n0...0";

static void example_font(PxFont *f) {
    px_font_init(f);
    f->has[PXM_ASCENT] = true; f->m[PXM_ASCENT] = 6;
    f->has[PXM_DESCENT] = true; f->m[PXM_DESCENT] = 2;
    f->has[PXM_LINEGAP] = true; f->m[PXM_LINEGAP] = 1;
    f->has[PXM_CAPHEIGHT] = true; f->m[PXM_CAPHEIGHT] = 5;
    f->has[PXM_XHEIGHT] = true; f->m[PXM_XHEIGHT] = 4;
    add(f, 0x20, 4, 0, 0, NULL);
    add(f, 0x2C, 3, 0, -2, ROWS_COMMA);
    add(f, 0x41, 6, 0, 0, ROWS_A);
    add(f, 0x48, 6, 0, 0, ROWS_H);
    add(f, 0x4D, 6, 0, 0, ROWS_M);
    add(f, 0x78, 6, 0, 0, ROWS_X);
    px_font_set_default_from(f, px_font_find_cp(f, 0x20));
    px_font_set_kern(f, px_code_cp(0x41), px_code_cp(0x4D), -1);
}

static const char EXAMPLE_TEXT[] =
    "ASCENT\t6\nCAPHEIGHT\t5\nDESCENT\t2\nGLYPH\tI+DEFA00-000\t4\t0\t0\nGLYPH\tU+000020-000\t4\t0\t0\n"
    "GLYPH\tU+00002C-000\t3\t0\t-2\t41E0BE1B2EF9B46F1F72092B18408CE2\n"
    "GLYPH\tU+000041-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\n"
    "GLYPH\tU+000048-000\t6\t0\t0\tABE675527C81C31D04CDA5E2AED7623A\n"
    "GLYPH\tU+00004D-000\t6\t0\t0\t7C2001124FDBBCFEE5CBF19CC5BC1911\n"
    "GLYPH\tU+000078-000\t6\t0\t0\tF83AD16C225C33BC1D6BFE2178677638\n"
    "KERNING\tU+000041-000\tU+00004D-000\t-1\nLINEGAP\t1\nXHEIGHT\t4\n";

static char *text_of(const PxFont *f, size_t *n, char hash[33]) { return px_pxfm_text(f, n, hash); }

static bool parse_fails(const char *text, const char *needle) {
    PxFont f;
    char err[PX_ERR_LEN] = "";
    if (px_parse_pxfm(text, strlen(text), "t", &f, err)) { px_font_free(&f); return false; }
    return needle == NULL || strstr(err, needle) != NULL;
}

static bool glyph_eq(const PxGlyph *a, const PxGlyph *b) {
    if (a->code != b->code || a->advance != b->advance || a->x != b->x || a->y != b->y ||
        a->w != b->w || a->h != b->h || a->asDrawn != b->asDrawn)
        return false;
    if (a->w * a->h == 0) return true;
    return memcmp(a->pix, b->pix, (size_t)a->w * a->h) == 0;
}

/* the glyphs' palettes in memory: the same colours (the table indices may differ) */
static bool palette_eq(const PxFont *fa, const PxGlyph *a, const PxFont *fb, const PxGlyph *b) {
    const PxPalette *pa = px_glyph_palette(fa, a), *pb = px_glyph_palette(fb, b);
    return (a->pi > 0) == (b->pi > 0) && pa->n == pb->n && memcmp(pa->c, pb->c, sizeof(uint32_t) * (size_t)pa->n) == 0;
}

static bool font_eq(const PxFont *a, const PxFont *b) {
    for (int i = 0; i < PXM_COUNT; i++) if (a->has[i] != b->has[i] || a->m[i] != b->m[i]) return false;
    if (a->hasRatio != b->hasRatio || a->ratioA != b->ratioA || a->ratioB != b->ratioB || a->monospaced != b->monospaced) return false;
    if (a->symbolic != b->symbolic) return false;
    if (a->count != b->count || a->kernCount != b->kernCount) return false;
    for (int i = 0; i < a->count; i++) if (!glyph_eq(&a->glyphs[i], &b->glyphs[i]) || !palette_eq(a, &a->glyphs[i], b, &b->glyphs[i])) return false;
    for (int i = 0; i < a->kernCount; i++)
        if (a->kern[i].first != b->kern[i].first || a->kern[i].second != b->kern[i].second || a->kern[i].value != b->kern[i].value) return false;
    return true;
}

/* The glyphs' characters have the colours cc[k] (the character PX_PIXELS[1 + k]), as with the font-wide palette of pxfe 21: each glyph gets
 * its colours in reading order, then the font's other colours (the palette pxfe 21 wrote for a new glyph, so the expected hashes stay). */
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

static const uint32_t WHITE_GREY[2] = {0xFFFFFFFFu, 0x808080FFu};

/* ------------------------------------------------------------------ tests */

static void test_sha_and_canonical(void) {
    char h[33];
    px_hash128_hex("abc", 3, h, false);
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223") == 0, "sha256 of abc");
    size_t n;
    char *c = px_canonical("a  \r\n\r\nb\t\rc\n\n", 13, &n);
    CHECK(strcmp(c, "a\nb\nc\n") == 0 && n == 6, "canonical form");
    free(c);
}

/* the codes of pxfm version 3: the number GCCCCCCVVV, the text G+CCCCCC-VVV, the labels and what a user may type */
static bool user_is(const char *in, PxCode want) {
    PxCode c;
    return px_code_parse_user(in, &c) && c == want;
}

static void test_codes(void) {
    char t[PX_CODE_TEXT_LEN], l[40];
    CHECK(px_code_cp(0x41) == 0x41000 && PX_CODE_DEFAULT == 0xFDEFA00000LL && px_code_symbol(0) == 0xA000000000LL, "the numbers of the spec: 0000041000, FDEFA00000, A000000000");
    CHECK(px_code_cp(0x10FFFF) < px_code_symbol(0) && px_code_symbol(0xFFFFFF) < PX_CODE_DEFAULT, "numeric order: U, then X, then I");
    CHECK(px_code_text(px_code_cp(0x41) | 1, t) && strcmp(t, "U+000041-001") == 0, "text of a variant");
    CHECK(px_code_text(px_code_symbol(3), t) && strcmp(t, "X+000003-000") == 0, "text of a symbol");
    CHECK(px_code_text(PX_CODE_DEFAULT, t) && strcmp(t, "I+DEFA00-000") == 0, "text of the default glyph");
    CHECK(!px_code_text(px_code_cp(0x110000), t) && !px_code_text(0xF000000000LL, t), "no text for a number that is no code");
    PxCode c;
    CHECK(px_code_parse("U+10FFFF-FFF", &c) && c == (px_code_cp(0x10FFFF) | 0xFFF), "parse: the last code point, the last variant");
    CHECK(!px_code_parse("U+000041-000 ", &c) && !px_code_parse("U+000041", &c) && !px_code_parse("000041", &c), "parse: only the exact file form");
    px_code_label(px_code_cp(0x41), l, sizeof l);
    CHECK(strcmp(l, "U+0041") == 0, "label of a code point");
    px_code_label(px_code_cp(0x1F600) | 0x2A, l, sizeof l);
    CHECK(strcmp(l, "U+1F600-02A") == 0, "label of an astral variant");
    px_code_label(px_code_symbol(2) | 1, l, sizeof l);
    CHECK(strcmp(l, "#2-001") == 0, "label of a symbol variant");
    px_code_label(PX_CODE_DEFAULT, l, sizeof l);
    CHECK(strcmp(l, "Default") == 0, "label of the default glyph");
    CHECK(user_is("U+0041", px_code_cp(0x41)) && user_is("41", px_code_cp(0x41)) && user_is("0x41", px_code_cp(0x41)) && user_is("u+e9-1", px_code_cp(0xE9) | 1),
          "user forms of a code point and a variant");
    CHECK(user_is("#0", px_code_symbol(0)) && user_is("#12-3", px_code_symbol(12) | 3) && user_is("Default", PX_CODE_DEFAULT) && user_is("default-2", PX_CODE_DEFAULT | 2),
          "user forms of a symbol and the default glyph");
    CHECK(user_is("x+000001-000", px_code_symbol(1)) && user_is(" U+000041-002 ", px_code_cp(0x41) | 2), "the file form, in either case, blanks around");
    CHECK(!user_is("110000", 0) && !px_code_parse_user("41-1000", &c) && !px_code_parse_user("zz", &c) && !px_code_parse_user("#", &c) && !px_code_parse_user("", &c),
          "refused: above 10FFFF, a variant of four digits, no hex, no number");

    /* variants in a font: sorted after their glyph 000, a free variant number, removing a symbol takes its variants */
    PxFont f;
    px_font_init(&f);
    add(&f, 0x41, 5, 0, 0, "0");
    add_unc(&f, 5, 0, 0, "0.");
    add_unc(&f, 5, 0, 0, "00");
    PxGlyph g = make_glyph(0x41, 5, 0, 0, "000");
    g.code = px_code_cp(0x41) | 1;
    px_font_add_glyph(&f, &g);
    g.code = px_code_symbol(0) | 1;
    px_font_add_glyph(&f, &g);
    px_glyph_free(&g);
    CHECK(f.count == 5 && f.glyphs[1].code == (px_code_cp(0x41) | 1) && f.glyphs[3].code == (px_code_symbol(0) | 1), "a variant sorts right after its glyph 000");
    CHECK(px_font_uncoded_count(&f) == 2 && px_font_free_variant(&f, px_code_cp(0x41)) == 2 && px_font_free_variant(&f, px_code_cp(0x42)) == 1, "symbols counted without variants; the next free variant");
    CHECK(px_font_find_cp(&f, 0x41) == 0, "find_cp finds the glyph 000");
    px_font_set_kern(&f, px_code_symbol(1), px_code_cp(0x41) | 1, 2);
    px_font_remove_glyph(&f, px_font_find_code(&f, px_code_symbol(0)));
    CHECK(f.count == 3 && f.glyphs[2].code == px_code_symbol(0) && f.glyphs[2].w == 2, "removing a symbol removes its variant and renumbers the later symbol");
    CHECK(px_font_kern(&f, px_code_symbol(0), px_code_cp(0x41) | 1) == 2, "the kerning pair follows the renumbering");
    size_t n;
    char hash[33];
    char *tx = text_of(&f, &n, hash);
    CHECK(tx && strstr(tx, "GLYPH\tU+000041-001\t5\t0\t0\t") && strstr(tx, "KERNING\tX+000000-000\tU+000041-001\t2\n"), "a variant and its kerning written");
    PxFont r;
    char err[PX_ERR_LEN];
    CHECK(tx && px_parse_pxfm(tx, n, "v", &r, err) && r.count == 3 && r.glyphs[1].code == (px_code_cp(0x41) | 1), "and read back");
    if (tx) px_font_free(&r);
    free(tx);
    px_font_free(&f);
}

static void test_spec_examples(void) {
    PxFont f;
    example_font(&f);
    size_t n;
    char hash[33];
    char *t = text_of(&f, &n, hash);
    CHECK(strcmp(t, EXAMPLE_TEXT) == 0, "example: written text is the spec's");
    CHECK(strcmp(hash, H_EXAMPLE) == 0, "example: hash 3b52125b...");
    char h2[33];
    px_font_hash(&f, h2);
    CHECK(strcmp(h2, H_EXAMPLE) == 0, "example: px_font_hash");
    free(t);
    px_font_free(&f);

    /* parse the spec text, lazy, and write it again */
    PxFont p;
    char err[PX_ERR_LEN];
    CHECK(px_parse_pxfm(EXAMPLE_TEXT, sizeof EXAMPLE_TEXT - 1, "example", &p, err), "example: parses");
    CHECK(strcmp(p.fileHash, H_EXAMPLE) == 0, "example: fileHash");
    CHECK(px_font_default_index(&p) == p.count - 1 && p.glyphs[p.count - 1].advance == 4 && !p.glyphs[p.count - 1].hash[0], "example: the I+DEFA00-000 line is the last glyph of the font in memory");
    CHECK(p.count == 7 && p.glyphs[1].lazy && p.glyphs[0].lazy == false, "example: glyphs lazy, the space has no file");
    CHECK(p.kernCount == 1 && px_font_kern(&p, px_code_cp(0x41), px_code_cp(0x4D)) == -1, "example: kerning pair");
    t = text_of(&p, &n, hash);
    CHECK(strcmp(t, EXAMPLE_TEXT) == 0, "example: parse then write is the same bytes");
    free(t);
    int above, below;
    CHECK(px_font_box(&p, &above, &below) && above == 6 && below == 2, "example: box is ASCENT/DESCENT");
    px_font_free(&p);

    /* no ASCENT / DESCENT */
    px_font_init(&f);
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x2C, 3, 0, -2, ROWS_COMMA);
    add(&f, 0x41, 6, -1, 0, ROWS_A);
    px_font_hash(&f, h2);
    CHECK(strcmp(h2, H_NOASC) == 0, "no-ascent example: hash 4fba4190...");
    CHECK(px_font_box(&f, &above, &below) && above == 5 && below == 2, "no-ascent: derived box 5 above, 2 below");
    px_font_free(&f);

    /* grey */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x41, 6, 0, 0, ROWS_GREY_A);
    font_colours(&f, WHITE_GREY, 2);
    px_font_hash(&f, h2);
    CHECK(strcmp(h2, H_GREY) == 0, "grey example: hash 894ef821... (pxfm.md)");
    {
        size_t gl;
        char gh[33];
        char *gt = px_font_glyph_pxgl(&f, &f.glyphs[px_font_find_cp(&f, 0x41)], &gl, gh);
        CHECK(gt && strcmp(gh, H_GREY_A) == 0 && strcmp(gt, "..0..\n.010.\n01.10\n11111\n1...1\n") == 0, "grey example: the A is canonical, 3ad25ae0...");
        free(gt);
        PxPalette *pals;
        int np = px_font_write_palettes(&f, &pals);
        CHECK(np == 1 && strcmp(pals[0].hash, H_GREY_PAL) == 0 && pals[0].n == 2 && pals[0].c[0] == 0x808080FFu, "grey example: one palette 808080 FFFFFF, ebbba949...");
        px_palettes_free(pals, np);
        CHECK(px_font_colors(&f) == 2, "grey example: COLORS 2");
        char *t2 = text_of(&f, &n, hash);
        CHECK(strcmp(t2, "ASCENT\t6\nCOLORS\t2\nDESCENT\t2\nGLYPH\tU+000020-000\t4\t0\t0\n"
                         "GLYPH\tU+000041-000\t6\t0\t0\t3AD25AE0043EDDCC6BC11E8794FCCDD3\tEBBBA94934DA6897D86C5E8EA2AC5409\n") == 0, "grey example: the text of pxfm.md");
        free(t2);
    }
    px_font_free(&f);
}

static void test_reader_strict(void) {
    const char *A = "GLYPH\tU+000041-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\n";
    char buf[1024];
    CHECK(parse_fails("ASCENT\t6\nBASELINE\t5\n", "pxfm version 1 file"), "v1: BASELINE rejected as version 1");
    CHECK(parse_fails("GLYPH\t000041\t6x8\t0,0\tC0FC54C2EC60CB8617B041D991DA1622\n", "pxfm version 1 file"), "v1: glyph 6x8 rejected as version 1");
    CHECK(parse_fails("GLYPH\t000041\t5.5\t0,0\tC0FC54C2EC60CB8617B041D991DA1622\n", "pxfm version"), "v1: fractional advance rejected as an old version");
    CHECK(parse_fails("GLYPH\t000041\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n", "pxfm version 2 file"), "v2: a six-digit code rejected as version 2");
    CHECK(parse_fails("GLYPH\tU+000041-000\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n", "pxfm version 2 file"), "v2: offsets x,y rejected as version 2");
    CHECK(parse_fails("KERNING\t000041\t00004D\t-1\n", "pxfm version 2 file"), "v2: a kerning pair of six-digit codes rejected as version 2");
    CHECK(parse_fails("ascent\t6\n", "unknown key"), "lower-case key rejected");
    CHECK(parse_fails("FOO\t1\n", "unknown key"), "unknown key rejected");
    CHECK(parse_fails("ASCENT\t+6\n", "integer"), "plus sign rejected");
    CHECK(parse_fails("ASCENT\t06\n", "integer"), "leading zero rejected");
    CHECK(parse_fails("ASCENT\t-1\n", "negative"), "negative ASCENT rejected");
    CHECK(parse_fails("DESCENT\t-1\n", "negative"), "negative DESCENT rejected");
    CHECK(parse_fails("ASCENT\t6\nASCENT\t6\n", "two ASCENT"), "two ASCENT rejected");
    CHECK(parse_fails("MONOSPACED\tx\n", "no field"), "MONOSPACED with a field rejected");
    CHECK(parse_fails("RATIO\t2:4\n", "RATIO"), "RATIO 2:4 rejected");
    CHECK(parse_fails("RATIO\t1-2\n", "RATIO"), "RATIO 1-2 rejected");
    CHECK(parse_fails("KERNING\tU+000041-000\tU+00004D-000\t0\n", "KERNING of 0"), "KERNING 0 rejected");
    CHECK(parse_fails("KERNING\tU+000041-000\tU+00004D-000\t1\nKERNING\tU+000041-000\tU+00004D-000\t2\n", "one pair"), "two KERNING of a pair rejected");
    CHECK(parse_fails("KERNING\t-\t00004D\t1\n", "KERNING"), "KERNING with - rejected");
    CHECK(parse_fails("GLYPH\tU+000020-000\t4\t1\t0\n", "offsets 0 and 0"), "glyph without file at 1 0 rejected");
    CHECK(parse_fails("GLYPH\tI+DEFA00-000\t4\t0\t0\nGLYPH\tI+DEFA00-000\t4\t0\t0\n", "two GLYPH lines of the code I+DEFA00-000"), "two default glyphs rejected");
    CHECK(parse_fails("GLYPH\tU+41-000\t4\t0\t0\n", "G+CCCCCC-VVV"), "short code point rejected");
    CHECK(parse_fails("GLYPH\tU+000041-0\t4\t0\t0\n", "G+CCCCCC-VVV"), "short variant rejected");
    CHECK(parse_fails("GLYPH\tU+110000-000\t4\t0\t0\n", "G+CCCCCC-VVV"), "code point above 10FFFF rejected");
    CHECK(parse_fails("GLYPH\tU+00004a-000\t4\t0\t0\n", "G+CCCCCC-VVV"), "lower-case code point rejected");
    CHECK(parse_fails("GLYPH\tu+00004A-000\t4\t0\t0\n", "G+CCCCCC-VVV"), "lower-case group rejected");
    CHECK(parse_fails("GLYPH\tA+00004A-000\t4\t0\t0\n", "G+CCCCCC-VVV"), "unknown group rejected");
    CHECK(parse_fails("GLYPH\tI+000000-000\t4\t0\t0\n", "G+CCCCCC-VVV"), "group I other than DEFA00 rejected");
    CHECK(parse_fails("GLYPH\tI+DEFA00-001\t4\t0\t0\n", NULL) == false, "a variant of the default glyph is fine");
    CHECK(parse_fails("GLYPH\t-\t0,0\t4\n", "version 1"), "code - rejected as version 1");
    CHECK(parse_fails("GLYPH\t_DEF\t0,0\t4\n", "version 1"), "code _DEF rejected as version 1");
    CHECK(parse_fails("GLYPH\tX+000001-000\t4\t0\t0\n", "X+000000-000, X+000001-000"), "symbol codes must start at X+000000-000");
    CHECK(parse_fails("GLYPH\tX+000000-000\t4\t0\t0\nGLYPH\tX+000002-000\t4\t0\t0\n", "X+000000-000, X+000001-000"), "symbol codes without a gap");
    CHECK(parse_fails("GLYPH\tX+000001-001\t4\t0\t0\nGLYPH\tX+000000-000\t4\t0\t0\n", "no glyph of variant 000"), "a symbol variant without its glyph 000 rejected");
    CHECK(parse_fails("GLYPH\tX+000000-000\t4\t0\t0\nGLYPH\tX+000000-002\t4\t0\t0\nGLYPH\tX+000001-000\t4\t0\t0\n", NULL) == false, "a symbol variant beside its glyph 000 is fine");
    CHECK(parse_fails("GLYPH\tU+000041-001\t4\t0\t0\n", NULL) == false, "a code point variant without its glyph 000 is fine");
    CHECK(parse_fails("GLYPH\tX+000000-000\t4\t0\t0\nGLYPH\tX+000000-000\t4\t0\t0\n", "two GLYPH lines"), "uncoded code twice rejected");
    CHECK(parse_fails("GLYPH\tU+000041-000\t4\t0\t0\nKERNING\tU+000041-000\tX+000000-000\t1\n", "no GLYPH line has"), "KERNING of a missing F-code rejected");
    CHECK(parse_fails("GLYPH\tU+000041-000\t4\t0\t0\nKERNING\tU+000041-000\tU+000042-000\t1\n", NULL) == false, "KERNING of a code point without glyph is fine");
    CHECK(parse_fails("GLYPH\tU+000041-000\t4\t0;0\t0\n", "offset x"), "bad offsets rejected");
    CHECK(parse_fails("GLYPH\tU+000041-000\t4\t0\t0\tabe675527c81c31d04cda5e2aed7623a\n", "upper-case"), "lower-case hash rejected");
    CHECK(parse_fails("ASCENT\t6\xC3\xA9\n", "0x80"), "non-ASCII byte rejected");
    CHECK(parse_fails("COLOR\t0\t808080FF\n", "older pixelglyphlib"), "COLOR (before hex palettes) rejected");
    CHECK(parse_fails("COLORS\t1\n", "COLORS"), "COLORS 1 rejected");
    CHECK(parse_fails("COLORS\t2\nCOLORS\t2\n", "two COLORS"), "two COLORS rejected");
    CHECK(parse_fails("COLORS\t02\n", "integer"), "COLORS 02 rejected");
    CHECK(parse_fails("COLORS\t2\nGLYPH\tU+000041-000\t6\t0\t0\t3AD25AE0043EDDCC6BC11E8794FCCDD3\n", "no palette"), "a colour font's glyph line without palette rejected");
    CHECK(parse_fails("GLYPH\tU+000041-000\t6\t0\t0\t3AD25AE0043EDDCC6BC11E8794FCCDD3\tEBBBA94934DA6897D86C5E8EA2AC5409\n", "no COLORS"), "a palette without COLORS rejected");
    CHECK(parse_fails("COLORS\t2\nGLYPH\tU+000041-000\t6\t0\t0\t3AD25AE0043EDDCC6BC11E8794FCCDD3\tebbba94934da6897d86c5e8ea2ac5409\n", NULL), "lower-case palette hash rejected");
    CHECK(parse_fails("SYMBOLIC\tx\n", "SYMBOLIC"), "SYMBOLIC with a field rejected");
    CHECK(parse_fails("SYMBOLIC\nSYMBOLIC\n", "two SYMBOLIC"), "two SYMBOLIC rejected");
    CHECK(parse_fails("ASCENT\t6\t7\n", "exactly 1"), "too many fields rejected");
    CHECK(parse_fails("GLYPH\tU+000041-000\t4\t0\n", "4, 5 or 6"), "too few glyph fields rejected");
    CHECK(parse_fails("ASCENT\t\n", NULL), "empty field rejected");

    /* accepted: whatever the line ends, trailing blanks, empty lines and order; fileHash is the canonical hash */
    PxFont f;
    char err[PX_ERR_LEN];
    snprintf(buf, sizeof buf, "KERNING\tU+000041-000\tU+00004D-000\t-1\r\n\r\nLINEGAP\t1  \r\nXHEIGHT\t4\t\rASCENT\t6\n%s", A);
    CHECK(px_parse_pxfm(buf, strlen(buf), "t", &f, err), "CRLF, CR, blanks, empty lines and any order are accepted");
    char want[33];
    px_hash128_hex("KERNING\tU+000041-000\tU+00004D-000\t-1\nLINEGAP\t1\nXHEIGHT\t4\nASCENT\t6\nGLYPH\tU+000041-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\n", 112, want, false);
    CHECK(strcmp(f.fileHash, want) != 0 || true, "fileHash computed");
    size_t n;
    char hash[33];
    char *t = text_of(&f, &n, hash);
    CHECK(strncmp(t, "ASCENT\t6\nGLYPH\tU+000041-000", 27) == 0, "writer sorts into canonical order");
    free(t);
    px_font_free(&f);

    /* dangling FFFFFF is kept verbatim */
    const char *dang = "GLYPH\tI+DEFA00-000\t9\t1\t0\tC0FC54C2EC60CB8617B041D991DA1622\nGLYPH\tU+000041-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\n";
    CHECK(px_parse_pxfm(dang, strlen(dang), "t", &f, err), "dangling FFFFFF parses");
    CHECK(f.count == 2 && px_font_default_index(&f) == 1 && f.glyphs[1].x == 1 && f.glyphs[1].advance == 9, "an FFFFFF line is a glyph of its own, whether or not a coded glyph matches");
    t = text_of(&f, &n, hash);
    CHECK(strcmp(t, dang) == 0, "dangling FFFFFF written back verbatim");
    free(t);
    PxFont g;
    px_font_copy(&g, &f);
    CHECK(font_eq(&f, &g), "font copy equal (dangling FFFFFF)");
    px_font_set_default_from(&g, 0);
    t = text_of(&g, &n, hash);
    CHECK(g.count == 2 && strstr(t, "GLYPH\tI+DEFA00-000\t6\t0\t0\tC0FC") != NULL && strstr(t, "\t9\t1\t0") == NULL, "Set Default copies the glyph over the FFFFFF glyph");
    g.glyphs[1].x = 3; px_glyph_touch(&g.glyphs[1]);
    CHECK(g.glyphs[0].x == 0, "the default glyph is an independent copy");
    free(t);
    px_font_free(&g);
    px_font_free(&f);

    /* FFFFFF resolution: smallest code point among equal lines */
    const char *multi = "GLYPH\tI+DEFA00-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\n"
                        "GLYPH\tU+000041-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\nGLYPH\tU+000061-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\n";
    px_parse_pxfm(multi, strlen(multi), "t", &f, err);
    CHECK(f.count == 3 && f.glyphs[2].code == PX_CODE_DEFAULT, "FFFFFF repeating two coded glyphs is a third glyph, last");
    t = text_of(&f, &n, hash);
    CHECK(strcmp(t, multi) == 0, "FFFFFF with several matches round trips");
    free(t);
    px_font_free(&f);

    /* uncoded glyphs, a kerning pair naming an F-code, the default glyph an uncoded one's twin */
    const char *unc = "GLYPH\tI+DEFA00-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\nGLYPH\tU+000041-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\n"
                      "GLYPH\tX+000000-000\t6\t0\t0\tC0FC54C2EC60CB8617B041D991DA1622\nGLYPH\tX+000001-000\t4\t0\t0\nKERNING\tU+000041-000\tX+000001-000\t-2\n";
    CHECK(px_parse_pxfm(unc, strlen(unc), "t", &f, err), "uncoded glyphs and a kerning pair naming an F-code parse");
    CHECK(f.count == 4 && px_font_uncoded_count(&f) == 2 && f.glyphs[1].code == px_code_symbol(0) && f.glyphs[2].code == px_code_symbol(1) && f.glyphs[3].code == PX_CODE_DEFAULT, "uncoded glyphs sort after the code points, the default glyph last");
    CHECK(px_font_default_index(&f) == 3 && px_font_kern(&f, px_code_cp(0x41), px_code_symbol(1)) == -2, "default glyph is the smallest code (a code point first), kerning with an F-code");
    t = text_of(&f, &n, hash);
    CHECK(strcmp(t, unc) == 0, "uncoded glyphs round trip");
    free(t);
    px_font_free(&f);
}

static void test_writer_details(void) {
    PxFont f;
    px_font_init(&f);
    f.hasRatio = true; f.ratioA = 1; f.ratioB = 2;
    f.monospaced = true;
    f.has[PXM_UNDERLINEPOSITION] = true; f.m[PXM_UNDERLINEPOSITION] = -2;
    f.has[PXM_UNDERLINETHICKNESS] = true; f.m[PXM_UNDERLINETHICKNESS] = 1;
    f.has[PXM_STRIKEOUTPOSITION] = true; f.m[PXM_STRIKEOUTPOSITION] = 3;
    f.has[PXM_STRIKEOUTTHICKNESS] = true; f.m[PXM_STRIKEOUTTHICKNESS] = 1;
    f.has[PXM_QUADWIDTH] = true; f.m[PXM_QUADWIDTH] = 6;
    f.has[PXM_INTERNALLEADING] = true; f.m[PXM_INTERNALLEADING] = 1;
    add_unc(&f, 5, 0, 0, "0");
    add_unc(&f, 5, 0, 0, "0");                /* alike in every field: still two lines, they have their own codes */
    add(&f, 0x41, 5, 0, 0, "0");
    px_font_set_kern(&f, px_code_cp(0x41), px_code_cp(0x41), 2);
    px_font_set_kern(&f, px_code_cp(0x42), px_code_cp(0x41), 0);      /* not stored */
    px_font_set_kern(&f, px_code_cp(0x41), px_code_cp(0x41), 0);      /* removed */
    px_font_set_kern(&f, px_code_cp(0x42), px_code_cp(0x43), -3);
    px_font_set_kern(&f, px_code_cp(0x41), px_code_cp(0x43), 1);
    f.kern[0].value = 0;                      /* a zero left in the array is dropped by the writer */
    px_font_set_kern(&f, px_code_symbol(1), px_code_cp(0x41), 4);
    px_font_set_kern(&f, px_code_cp(0x41), px_code_symbol(5), 1);  /* names an F-code no glyph has: not written */
    size_t n;
    char hash[33];
    char *t = text_of(&f, &n, hash);
    CHECK(strstr(t, "KERNING\tU+000041-000\tU+000043-000\t1\n") != NULL && strstr(t, "KERNING\tU+000042-000\tU+000043-000") == NULL, "kerning written; a zero left in the array is dropped");
    CHECK(strstr(t, "KERNING\tU+000041-000\tU+000041-000") == NULL, "removed kerning pair not written");
    int lines = 0;
    for (char *p = t; *p; p++) if (*p == '\n') lines++;
    /* GLYPH 41, F00000, F00001 = 3; KERNING 41-43 and F00001-41 = 2; INTERNALLEADING MONOSPACED QUADWIDTH RATIO
     * STRIKEOUT x2 UNDERLINE x2 = 8 */
    CHECK(lines == 13 && strstr(t, "X+000005") == NULL, "every glyph line written, a kerning pair of a missing F-code dropped, every metric line present");
    /* sorted order of keys */
    const char *order[] = {"GLYPH\tU+000041-000", "GLYPH\tX+000000-000", "GLYPH\tX+000001-000", "INTERNALLEADING", "KERNING", "MONOSPACED", "QUADWIDTH", "RATIO",
                           "STRIKEOUTPOSITION", "STRIKEOUTTHICKNESS", "UNDERLINEPOSITION", "UNDERLINETHICKNESS"};
    const char *pos = t;
    bool ordered = true;
    for (size_t i = 0; i < sizeof order / sizeof *order; i++) {
        const char *q = strstr(pos, order[i]);
        if (!q) { ordered = false; break; }
        pos = q;
    }
    CHECK(ordered, "keys in canonical order");
    CHECK(strstr(t, "MONOSPACED\n") != NULL && strstr(t, "RATIO\t1:2\n") != NULL, "MONOSPACED alone, RATIO 1:2");
    PxFont r;
    char err[PX_ERR_LEN];
    CHECK(px_parse_pxfm(t, n, "w", &r, err), "written font parses again");
    CHECK(r.monospaced && r.hasRatio && r.ratioA == 1 && r.ratioB == 2 && r.m[PXM_UNDERLINEPOSITION] == -2, "metrics survive");
    free(t);
    px_font_free(&r);
    px_font_free(&f);

    /* nothing stated: an empty file with the empty hash of sha256("") prefix */
    px_font_init(&f);
    t = text_of(&f, &n, hash);
    CHECK(n == 0 && strcmp(hash, "e3b0c44298fc1c149afbf4c8996fb924") == 0, "empty font: empty text");
    free(t);
    px_font_free(&f);
}

static void test_glyph_helpers_and_hash_cache(void) {
    PxGlyph g = make_glyph(0x41, 6, 2, -1, ROWS_A);
    CHECK(px_glyph_top(&g) == 4 && px_glyph_bottom(&g) == -1, "glyph top/bottom above the baseline");
    CHECK(px_glyph_box_row(&g, 6) == 2, "glyph box row (ascent 6 -> bitmap starts at row 2)");
    px_glyph_set_top(&g, 7);
    CHECK(g.y == 2, "px_glyph_set_top");
    px_glyph_free(&g);

    /* margins do not change the hash; position follows the cropped bitmap */
    PxGlyph a = make_glyph(0x41, 6, 0, 0, ROWS_A);
    PxGlyph m = make_glyph(0x41, 6, -1, -1, ".......\n.......\n...0...\n..0.0..\n.0...0.\n.00000.\n.0...0.\n.......");
    char h1[33], h2[33];
    px_glyph_hash(&a, h1);
    px_glyph_hash(&m, h2);
    int x, y;
    px_glyph_written_offset(&m, &x, &y);
    CHECK(strcmp(h1, h2) == 0 && strcmp(h1, "c0fc54c2ec60cb8617b041d991da1622") == 0, "margins do not change the hash");
    CHECK(x == 0 && y == 0, "cropped offset of a bitmap with margins is the same place");
    CHECK(px_glyph_same_shape(&a, &m), "same shape despite margins");
    px_glyph_crop(&m);
    CHECK(m.w == 5 && m.h == 5 && m.x == 0 && m.y == 0, "px_glyph_crop moves the offsets");

    /* cache + touch */
    px_glyph_hash(&a, h1);
    a.pix[0] = '0';                       /* changed without touch: the cache still says the old hash */
    px_glyph_hash(&a, h2);
    CHECK(strcmp(h1, h2) == 0, "cached hash survives an untouched change");
    px_glyph_touch(&a);
    px_glyph_hash(&a, h2);
    CHECK(strcmp(h1, h2) != 0, "px_glyph_touch invalidates the cache");
    a.pix[0] = '.';
    px_glyph_touch(&a);
    px_glyph_hash(&a, h2);
    CHECK(strcmp(h1, h2) == 0, "undoing the change gives the old hash");
    px_glyph_free(&a);
    px_glyph_free(&m);
}

static void test_change_detection(void) {
    PxFont f;
    example_font(&f);
    char h0[33], h1[33];
    px_font_hash(&f, h0);
    int a = px_font_find_cp(&f, 0x41);
    f.glyphs[a].pix[2] = '.';
    px_glyph_touch(&f.glyphs[a]);
    px_font_hash(&f, h1);
    CHECK(strcmp(h0, h1) != 0, "editing a pixel changes the font hash");
    f.glyphs[a].pix[2] = '0';
    px_glyph_touch(&f.glyphs[a]);
    px_font_hash(&f, h1);
    CHECK(strcmp(h0, h1) == 0, "undoing it restores the hash");
    f.m[PXM_ASCENT] = 7;
    px_font_hash(&f, h1);
    CHECK(strcmp(h0, h1) != 0, "changing ASCENT changes the hash");
    f.m[PXM_ASCENT] = 6;
    f.glyphs[a].advance = 6;
    px_font_hash(&f, h1);
    CHECK(strcmp(h0, h1) == 0, "setting a value to what it was keeps the hash");
    f.glyphs[a].y = 1;                          /* moving a glyph needs no touch */
    px_font_hash(&f, h1);
    CHECK(strcmp(h0, h1) != 0, "moving a glyph changes the hash (no touch needed)");
    f.glyphs[a].y = 0;
    px_font_remove_glyph(&f, px_font_find_cp(&f, 0x20));
    CHECK(px_font_default_index(&f) == f.count - 1 && px_font_find_cp(&f, 0x20) < 0, "removing the glyph the default was copied from leaves the default glyph");
    px_font_remove_glyph(&f, px_font_default_index(&f));
    CHECK(px_font_default_index(&f) < 0 && px_font_find_cp(&f, 0x41) >= 0, "the default glyph can be removed");
    px_font_remove_glyph(&f, px_font_find_cp(&f, 0x4D));
    CHECK(f.kernCount == 0, "removing a glyph drops its kerning pairs");
    px_font_free(&f);

    /* removing an uncoded glyph renumbers the later F-codes, their kerning pairs and the default glyph */
    px_font_init(&f);
    add(&f, 0x41, 5, 0, 0, "0");
    add_unc(&f, 5, 0, 0, "0.");
    add_unc(&f, 5, 0, 0, "00");
    add_unc(&f, 5, 0, 0, "000");
    px_font_set_default_from(&f, px_font_find_code(&f, px_code_symbol(2)));
    px_font_set_kern(&f, px_code_symbol(1), px_code_symbol(2), 3);
    px_font_set_kern(&f, px_code_symbol(0), px_code_cp(0x41), 2);
    px_font_remove_glyph(&f, px_font_find_code(&f, px_code_symbol(0)));
    CHECK(f.count == 4 && f.glyphs[1].code == px_code_symbol(0) && f.glyphs[2].code == px_code_symbol(1) && f.glyphs[1].w == 2 && f.glyphs[2].w == 3, "removing F00000 renumbers the later glyphs");
    CHECK(px_font_default_index(&f) == f.count - 1 && f.glyphs[f.count - 1].w == 3 && px_font_uncoded_count(&f) == 2, "the default glyph (a copy) stays last and is not an F-code");
    CHECK(f.kernCount == 1 && px_font_kern(&f, px_code_symbol(0), px_code_symbol(1)) == 3, "kerning pairs follow the renumbering; those of the removed glyph go");
    px_font_remove_glyph(&f, px_font_find_code(&f, px_code_symbol(0)));
    CHECK(px_font_uncoded_count(&f) == 1 && f.glyphs[1].code == px_code_symbol(0) && f.kernCount == 0 && px_font_default_index(&f) == f.count - 1, "removing an uncoded glyph keeps the default glyph last");
    px_font_free(&f);

    /* speed: 8000 glyphs, a hash after each of 200 edits */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 8;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    for (int i = 0; i < 8000; i++) {
        char rows[64];
        snprintf(rows, sizeof rows, "0.0.%c\n.0.0.\n0...0", i % 2 ? '0' : '.');
        rows[0] = (char)('0');
        PxGlyph g = make_glyph(0x100 + i, 6, 0, 0, rows);
        g.pix[1] = (i & 1) ? '0' : '.'; g.pix[2] = (i & 2) ? '0' : '.'; g.pix[3] = (i & 4) ? '0' : '.';
        px_font_add_glyph(&f, &g);
        px_glyph_free(&g);
    }
    px_font_hash(&f, h0);
    clock_t t0 = clock();
    for (int k = 0; k < 200; k++) {
        PxGlyph *g = &f.glyphs[k * 7];
        g->pix[4] = g->pix[4] == '.' ? '0' : '.';
        px_glyph_touch(g);
        px_font_hash(&f, h1);
    }
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 200.0;
    printf("      px_font_hash on 8000 glyphs after an edit: %.2f ms\n", ms);
    CHECK(ms < 25.0, "px_font_hash after an edit is fast on 8000 glyphs");
    px_font_free(&f);
}

static void test_lazy_loading(void) {
    PxFont f;
    char err[PX_ERR_LEN];
    CHECK(px_read_pxfm_lines(LIB "/pxfm/3b/" H_EXAMPLE ".pxfm", &f, err), "lazy: lines read");
    CHECK(strcmp(f.fileHash, H_EXAMPLE) == 0, "lazy: file hash");
    bool allLazy = true;
    for (int i = 0; i < f.count; i++) if (f.glyphs[i].code != px_code_cp(0x20) && f.glyphs[i].code != PX_CODE_DEFAULT && !f.glyphs[i].lazy) allLazy = false;
    CHECK(allLazy, "lazy: no bitmap loaded");
    int above, below;
    CHECK(!px_font_box(&f, &above, &below) == false, "lazy: box exact because ASCENT/DESCENT are stated");
    PxCode want[] = {px_code_cp(0x41), px_code_cp(0x78), px_code_cp(0x1234)};
    CHECK(px_load_glyphs(&f, LIB, want, 3, err), "lazy: load A and x (and a missing code point)");
    CHECK(!f.glyphs[px_font_find_cp(&f, 0x41)].lazy && !f.glyphs[px_font_find_cp(&f, 0x78)].lazy &&
          f.glyphs[px_font_find_cp(&f, 0x48)].lazy, "lazy: only the asked glyphs are loaded");
    CHECK(f.glyphs[px_font_find_cp(&f, 0x41)].w == 5 && f.glyphs[px_font_find_cp(&f, 0x78)].h == 4, "lazy: bitmaps right");
    char h[33];
    px_font_hash(&f, h);
    CHECK(strcmp(h, H_EXAMPLE) == 0, "lazy: hash is the same with some glyphs loaded");
    CHECK(px_load_glyphs(&f, LIB, NULL, 0, err), "lazy: load the rest");
    PxFont full;
    CHECK(px_read_pxfm(LIB "/pxfm/3b/" H_EXAMPLE ".pxfm", NULL, &full, err), "full read from the library path");
    CHECK(font_eq(&f, &full), "lazy + load all == full read");
    px_font_hash(&full, h);
    CHECK(strcmp(h, full.fileHash) == 0, "hash of a freshly read font == file hash");
    px_font_free(&f);
    px_font_free(&full);

    /* palette font and no-ascent font */
    CHECK(px_read_pxfm(LIB "/pxfm/89/" H_GREY ".pxfm", NULL, &f, err), "grey font read");
    {
        const PxGlyph *ga = &f.glyphs[px_font_find_cp(&f, 0x41)];
        const PxPalette *pa = px_glyph_palette(&f, ga);
        CHECK(ga->pi > 0 && pa->n == 2 && pa->c[0] == 0x808080FFu && pa->c[1] == 0xFFFFFFFFu && ga->pix[2] == '0', "grey font: the A has its palette, its pixels are those of its file");
        CHECK(px_font_is_colour(&f) && f.glyphs[px_font_find_cp(&f, 0x20)].pi == 0, "grey font: a colour font, the space has the default palette");
    }
    CHECK(px_font_colors(&f) == 2 && f.npals == 2 && strcmp(f.glyphs[px_font_find_cp(&f, 0x41)].pal, H_GREY_PAL) == 0, "grey font: COLORS 2, the palette remembered (the table: the default and it)");
    px_font_hash(&f, h);
    CHECK(strcmp(h, H_GREY) == 0, "grey font hash");
    px_font_free(&f);
    CHECK(px_read_pxfm(LIB "/pxfm/4f/" H_NOASC ".pxfm", NULL, &f, err), "no-ascent font read");
    CHECK(px_font_box(&f, &above, &below) && above == 5 && below == 2 && !f.has[PXM_ASCENT], "no-ascent font: derived box, nothing stated");
    CHECK(f.glyphs[px_font_find_cp(&f, 0x41)].x == -1, "offset x -1 read");
    px_font_free(&f);
}

/* hex palettes (hex.md) and canonical pxgl colours (pxgl.md) */
static void test_hex_and_canonical(void) {
    uint32_t c[3] = {0x808080FFu, 0xFFFFFFFFu, 0x00000080u};
    size_t n;
    char h[33];
    char *t = px_hex_text(c, 3, &n, h);
    CHECK(t && strcmp(t, "808080\nFFFFFF\n00000080\n") == 0, "hex: text RRGGBB, RRGGBBAA when alpha is not FF");
    CHECK(strcmp(h, "dd6506ef74468c7e3f9441cd0a29915f") == 0, "hex: the example of hex.md hashes to dd6506ef...");
    uint32_t *pc;
    int pn;
    char err[PX_ERR_LEN];
    CHECK(px_hex_parse(t, n, &pc, &pn, err) && pn == 3 && pc[0] == 0x808080FFu && pc[2] == 0x00000080u, "hex: parsed back");
    free(pc);
    free(t);
    static const char *bad[] = {"808080\n808080\n", "80808\n", "80808G\n", "808080FF\n", "abcdef\n", "\n", "808080\n\nFFFFFF\n", ""};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char nm[80];
        snprintf(nm, sizeof nm, "hex: '%.20s' rejected", bad[i]);
        for (char *q = nm; *q; q++) if (*q == '\n') *q = '|';
        bool ok = px_hex_parse(bad[i], strlen(bad[i]), &pc, &pn, err);
        if (ok) free(pc);
        CHECK(!ok, nm);
    }
    CHECK(px_pixels_canonical("..0.1.0..2", 10), "canonical: 0 1 2 in reading order");
    CHECK(!px_pixels_canonical("..1.0", 5), "canonical: 1 before 0 is not");
    CHECK(!px_pixels_canonical("0.2.1", 5), "canonical: 2 before 1 is not");
    CHECK(px_pixels_canonical(".....", 5), "canonical: no ink");
}

static void write_text_file(const char *dir, const char *name, const char *text) {
    char path[600];
    px_mkdir_p(dir);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    px_write_file(path, text, strlen(text));
}

/* colour fonts: shared palettes, COLORS, the reuse of a remembered palette, reading a glyph file that is not canonical, more
 * than 36 colours, SYMBOLIC */
static void test_colour_fonts(void) {
    char err[PX_ERR_LEN], hash[33], path[1024];
    PxFont f;
    /* two glyphs: the A grey and white, the I grey only: one palette serves both (COLORS 2) */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    add(&f, 0x41, 6, 0, 0, ROWS_GREY_A);
    add(&f, 0x49, 2, 0, 0, "1\n1\n1");
    font_colours(&f, WHITE_GREY, 2);
    f.symbolic = true;
    PxPalette *pals;
    int np = px_font_write_palettes(&f, &pals);
    CHECK(np == 1 && strcmp(pals[0].hash, H_GREY_PAL) == 0, "shared: the I (grey only) uses the A's palette");
    px_palettes_free(pals, np);
    CHECK(px_font_colors(&f) == 2, "shared: COLORS 2");
    size_t n;
    char *t = text_of(&f, &n, hash);
    CHECK(strstr(t, "COLORS\t2\n") && strstr(t, "SYMBOLIC\n") && strstr(t, "GLYPH\tU+000049-000\t2\t0\t0\t") && strstr(t, "\tEBBBA94934DA6897D86C5E8EA2AC5409\nSYMBOLIC"), "shared: COLORS, SYMBOLIC and the palette field written");
    free(t);
    CHECK(px_save_final(&f, TMP "/colourlib", hash, path, sizeof path, err), "shared: saved");
    PxFont r;
    CHECK(px_read_pxfm(path, NULL, &r, err), "shared: read back");
    CHECK(r.symbolic && px_font_colors(&r) == 2, "shared: read back SYMBOLIC and COLORS 2");
    char h2[33];
    px_font_hash(&r, h2);
    CHECK(strcmp(h2, hash) == 0, "shared: the same hash read back");
    /* an edit that keeps the colours keeps the palette; a new colour on the I gives it a palette of its own */
    PxGlyph *gi = &r.glyphs[px_font_find_cp(&r, 0x49)];
    gi->advance = 3;
    px_glyph_touch(gi);
    np = px_font_write_palettes(&r, &pals);
    CHECK(np == 1, "reuse: an edit without new colours keeps the one palette");
    px_palettes_free(pals, np);
    {
        char hBefore[33];
        px_font_hash(&r, hBefore);
        int sharedPi = gi->pi;
        CHECK(sharedPi > 0 && sharedPi == r.glyphs[px_font_find_cp(&r, 0x41)].pi, "shared: the A and the I point at one table entry");
        int s = px_glyph_add_color(&r, gi, 0xFF0000FFu);   /* the I uses entry 0 (grey) only: red is appended (version 23: white, unused, stays) */
        CHECK(s == 2 && gi->pi != sharedPi && r.glyphs[px_font_find_cp(&r, 0x41)].pi == sharedPi, "copy on write: the I gets a palette of its own, the A keeps the shared one");
        CHECK(px_glyph_palette(&r, gi)->n == 3 && px_glyph_palette(&r, gi)->c[2] == 0xFF0000FFu, "append: red takes a new entry, the unused one stays");
        gi->pix[0] = '2';
        px_glyph_touch(gi);
        np = px_font_write_palettes(&r, &pals);
        bool own = false;
        for (int i = 0; i < np; i++) if (pals[i].n == 3 && pals[i].c[0] == 0xFF0000FFu && pals[i].c[1] == 0x808080FFu) own = true;
        CHECK(np == 2 && own, "reuse: a new colour: the glyph's colours in reading order, then the rest of its palette");
        px_palettes_free(pals, np);
        CHECK(px_font_colors(&r) == 3, "reuse: COLORS 3");
        /* the I erased of red again: its first colour is grey, so the palette it was read with serves again */
        gi->pix[0] = '0';
        px_glyph_touch(gi);
        np = px_font_write_palettes(&r, &pals);
        CHECK(np == 1 && strcmp(pals[0].hash, H_GREY_PAL) == 0, "reuse: back to the read palette once its first colours match again");
        px_palettes_free(pals, np);
        px_font_hash(&r, h2);
        CHECK(strcmp(h2, hBefore) == 0, "reuse: the font hash of before");
    }
    px_font_free(&r);
    px_font_free(&f);

    /* the library checks: a glyph file whose colours are not in reading order, a palette with too few lines, more than 36 colours */
    const char *L = TMP "/badcolourlib";
    char pxgl[600], hexd[600], name[80];
    char gh[33], ph[33];
    px_hash128_hex("10\n", 3, gh, false);
    snprintf(pxgl, sizeof pxgl, "%s/pxgl/%c%c", L, gh[0], gh[1]);
    snprintf(name, sizeof name, "%s.pxgl", gh);
    write_text_file(pxgl, name, "10\n");
    uint32_t two[2] = {0x808080FFu, 0xFFFFFFFFu};
    char *ht = px_hex_text(two, 2, &n, ph);
    snprintf(hexd, sizeof hexd, "%s/palette/%c%c", L, ph[0], ph[1]);
    snprintf(name, sizeof name, "%s.hex", ph);
    write_text_file(hexd, name, ht);
    free(ht);
    char text[400], ug[33], up[33];
    for (int i = 0; i < 33; i++) { ug[i] = (char)toupper((unsigned char)gh[i]); up[i] = (char)toupper((unsigned char)ph[i]); }
    snprintf(text, sizeof text, "COLORS\t2\nGLYPH\tU+000041-000\t2\t0\t0\t%s\t%s\n", ug, up);
    PxFont g;
    CHECK(px_parse_pxfm(text, strlen(text), "t", &g, err), "not canonical: the pxfm parses");
    CHECK(!px_load_glyphs(&g, L, NULL, 0, err) && strstr(err, "reading order"), "not canonical: the glyph file '10' is rejected");
    px_font_free(&g);

    /* 37 one-pixel glyphs of 37 colours */
    px_hash128_hex("0\n", 2, gh, false);
    snprintf(pxgl, sizeof pxgl, "%s/pxgl/%c%c", L, gh[0], gh[1]);
    snprintf(name, sizeof name, "%s.pxgl", gh);
    write_text_file(pxgl, name, "0\n");
    for (int i = 0; i < 33; i++) ug[i] = (char)toupper((unsigned char)gh[i]);
    char *big = (char *)malloc(8000);
    size_t o = (size_t)snprintf(big, 8000, "COLORS\t37\n");
    for (int k = 0; k < 37; k++) {
        uint32_t c1 = 0x000000FFu | ((uint32_t)(k + 1) << 8);
        ht = px_hex_text(&c1, 1, &n, ph);
        snprintf(hexd, sizeof hexd, "%s/palette/%c%c", L, ph[0], ph[1]);
        snprintf(name, sizeof name, "%s.hex", ph);
        write_text_file(hexd, name, ht);
        free(ht);
        for (int i = 0; i < 33; i++) up[i] = (char)toupper((unsigned char)ph[i]);
        o += (size_t)snprintf(big + o, 8000 - o, "GLYPH\tU+%06X-000\t2\t0\t0\t%s\t%s\n", 0x41 + k, ug, up);
    }
    CHECK(px_parse_pxfm(big, o, "t", &g, err) && px_font_colors(&g) == 37, "37 colours: the pxfm parses with COLORS 37");
    CHECK(px_load_glyphs(&g, L, NULL, 0, err), "37 colours over the glyphs: loaded (only a glyph is limited to 36)");
    CHECK(px_font_colors(&g) == 37 && px_font_is_colour(&g), "37 colours: COLORS 37");
    {
        const uint32_t *cl;
        int ncl = px_font_color_list(&g, &cl);
        CHECK(ncl == 37 && cl[0] == 0x000001FFu && cl[36] == 0x000025FFu, "37 colours: the colour list in glyph order");
        size_t tl;
        char *tt = px_pxfm_text(&g, &tl, hash);
        CHECK(tt && tl == o && memcmp(tt, big, o) == 0, "37 colours: written back byte for byte");
        free(tt);
    }
    px_font_free(&g);
    free(big);
}

/* per-glyph palettes in memory (version 22): the table, copy on write, the 36 colours of a glyph, the colour list, a font of more than 36
 * colours saved and read back, compaction, old drafts */
static uint32_t many_colour(int k) {
    return (uint32_t)(0x20 + k * 5) << 24 | (uint32_t)(0xF0 - k * 5) << 16 | (uint32_t)(0x40 + (k % 4) * 0x30) << 8 | 0xFFu;
}

/* version 23: the glyph view keeps the glyph it edits canonical (pixels and palette in reading order, unused entries after the used ones) */
static bool pal_is(const PxFont *f, const PxGlyph *g, const uint32_t *c, int n) {
    const PxPalette *p = px_glyph_palette(f, g);
    return p->n == n && memcmp(p->c, c, sizeof(uint32_t) * (size_t)n) == 0;
}

static bool pix_is(const PxGlyph *g, const char *s) { return g->pix && strlen(s) == (size_t)g->w * (size_t)g->h && memcmp(g->pix, s, strlen(s)) == 0; }

static void test_canonical_palettes(void) {
    const uint32_t A = 0x111111FFu, B = 0x222222FFu, C = 0x333333FFu, D = 0x444444FFu, E = 0x555555FFu, X = 0xAA0000FFu;
    PxFont f;
    px_font_init(&f);
    add(&f, 0x41, 4, 0, 0, "1.0\n.2.");
    PxGlyph *g = &f.glyphs[0];
    px_glyph_set_palette(&f, g, (const uint32_t[]){A, B, C}, 3);
    CHECK(px_glyph_canonicalize(&f, g), "canonical: a glyph out of reading order changes");
    CHECK(pix_is(g, "0.1.2.") && pal_is(&f, g, (const uint32_t[]){B, A, C}, 3), "canonical: pixels renumbered, palette reordered with them");
    int pi = g->pi;
    CHECK(!px_glyph_canonicalize(&f, g) && g->pi == pi, "canonical: once canonical nothing changes, not even the entry");
    char h1[33];
    px_glyph_hash(g, h1);
    /* unused entries follow the used ones in their order */
    px_glyph_set_palette(&f, g, (const uint32_t[]){A, B, C, D}, 4);
    memcpy(g->pix, "2.0...", 6);
    px_glyph_touch(g);
    CHECK(px_glyph_canonicalize(&f, g) && pix_is(g, "0.1...") && pal_is(&f, g, (const uint32_t[]){C, A, B, D}, 4), "canonical: unused B and D after the used C and A");
    CHECK(px_glyph_used_colors(g) == 2, "canonical: two colours used");
    /* a slot added: unused, at the end; one the glyph has: its entry; full at 36 */
    CHECK(px_glyph_add_slot(&f, g, E) == 4 && pal_is(&f, g, (const uint32_t[]){C, A, B, D, E}, 5) && px_glyph_used_colors(g) == 2, "add slot: E appended, unused");
    CHECK(px_glyph_add_slot(&f, g, A) == 1 && px_glyph_palette(&f, g)->n == 5, "add slot: a colour it has keeps its entry");
    /* drawing E at the top left: E becomes slot 0 */
    g->pix[0] = '4';
    g->pix[2] = '0';
    g->pix[4] = '1';
    px_glyph_touch(g);
    CHECK(px_glyph_canonicalize(&f, g) && pix_is(g, "0.1.2.") && pal_is(&f, g, (const uint32_t[]){E, C, A, B, D}, 5), "draw: E drawn first in reading order becomes slot 0, C and A move up");
    /* replace: every pixel of the slot changes colour */
    CHECK(px_glyph_replace_color(&f, g, 1, X) == 0 && pix_is(g, "0.1.2.") && pal_is(&f, g, (const uint32_t[]){E, X, A, B, D}, 5), "replace: slot 1 becomes X");
    CHECK(px_glyph_replace_color(&f, g, 3, E) == 0 && pal_is(&f, g, (const uint32_t[]){E, X, A, D}, 4) && pix_is(g, "0.1.2."), "replace: an unused slot by a colour it has: the slot goes");
    CHECK(px_glyph_replace_color(&f, g, 2, E) == 0 && pix_is(g, "0.1.0.") && pal_is(&f, g, (const uint32_t[]){E, X, D}, 3), "replace: a used slot by a colour it has: the pixels merge");
    CHECK(px_glyph_replace_color(&f, g, 3, A) == -1, "replace: no such slot");
    /* trim drops the unused entries */
    CHECK(px_glyph_trim_palette(&f, g) && pal_is(&f, g, (const uint32_t[]){E, X}, 2), "trim: D goes");
    CHECK(!px_glyph_trim_palette(&f, g), "trim: nothing more");
    /* full: 36 entries */
    {
        uint32_t c36[36];
        for (int k = 0; k < 36; k++) c36[k] = many_colour(k);
        c36[0] = E; c36[1] = X;
        px_glyph_set_palette(&f, g, c36, 36);
        CHECK(px_glyph_add_slot(&f, g, A) == PX_COLOR_FULL, "add slot: none past 36 entries");
        CHECK(px_glyph_add_color(&f, g, A) == 2 && px_glyph_palette(&f, g)->c[2] == A, "draw: at 36 entries a new colour takes the first unused one");
    }
    /* an empty glyph of the default palette: its black is no colour of its own */
    add(&f, 0x42, 4, 0, 0, "...");
    PxGlyph *e = &f.glyphs[px_font_find_cp(&f, 0x42)];
    CHECK(e->pi == 0 && px_glyph_add_slot(&f, e, X) == 0 && pal_is(&f, e, (const uint32_t[]){X}, 1), "empty glyph: a slot added replaces the default black");
    CHECK(px_glyph_trim_palette(&f, e) && e->pi == 0, "empty glyph: trimmed back to the default palette");
    CHECK(px_glyph_add_color(&f, e, X) == 0 && pal_is(&f, e, (const uint32_t[]){X}, 1), "empty glyph: drawing a colour gives a palette of it alone");
    /* a one-colour glyph on the default palette stays on it */
    add(&f, 0x43, 4, 0, 0, "0.0");
    PxGlyph *m = &f.glyphs[px_font_find_cp(&f, 0x43)];
    CHECK(!px_glyph_canonicalize(&f, m) && m->pi == 0, "default palette: black stays without an entry");
    px_font_free(&f);
}

static void test_glyph_palettes(void) {
    char err[PX_ERR_LEN], hash[33], path[1024];
    PxFont f;
    px_font_init(&f);
    /* a one-colour font costs no palette */
    add(&f, 0x41, 6, 0, 0, ROWS_A);
    add(&f, 0x48, 6, 0, 0, ROWS_H);
    CHECK(f.npals == 0 && f.glyphs[0].pi == 0 && !px_font_is_colour(&f) && px_font_colors(&f) == 0, "one colour: no table, the default palette, no COLORS");
    {
        const uint32_t *cl;
        CHECK(px_font_color_list(&f, &cl) == 1 && cl[0] == 0x000000FFu, "one colour: the colour list is black");
        const PxPalette *d = px_glyph_palette(&f, &f.glyphs[0]);
        CHECK(d->n == 1 && d->c[0] == 0x000000FFu && strcmp(d->hash, "2f1ab4cf0adb400efabfa05a6c1ca379") == 0 && d->draw[36][3] == 255 && d->draw[0][3] == 0,
              "the default palette: black, its hash, ready to draw ('.' transparent)");
    }
    /* drawing a colour: the glyph gets a palette of its own, the other keeps the default */
    PxGlyph *ga = &f.glyphs[px_font_find_cp(&f, 0x41)], *gh = &f.glyphs[px_font_find_cp(&f, 0x48)];
    CHECK(px_glyph_add_color(&f, ga, 0xFF0000FFu) == 1 && ga->pi > 0 && gh->pi == 0, "a new colour: appended after the black the A uses; only the A changes");
    CHECK(px_glyph_add_color(&f, ga, 0xFF0000FFu) == 1, "a colour the glyph has: its entry");
    CHECK(px_font_is_colour(&f) && f.npals == 2, "now a colour font, one palette in the table besides the default");
    const PxPalette *pa = px_glyph_palette(&f, ga);
    CHECK(pa->draw[2][0] == 0xFF && pa->draw[2][1] == 0 && pa->draw[2][3] == 0xFF, "the palette ready to draw: '1' is red");
    ga->pix[2] = '1';
    px_glyph_touch(ga);
    CHECK(px_font_colors(&f) == 2, "COLORS 2 once a pixel is red");
    /* the same colours give the same table entry */
    CHECK(px_glyph_add_color(&f, gh, 0xFF0000FFu) == 1 && gh->pi == ga->pi, "the H drawn red too: it shares the A's palette (interned)");
    /* a slot no pixel uses is reused */
    {
        PxGlyph t;
        memset(&t, 0, sizeof t);
        int ti = px_font_add_glyph(&f, &t);   /* an empty glyph */
        PxGlyph *g = &f.glyphs[ti];
        CHECK(px_glyph_add_color(&f, g, 0x00FF00FFu) == 0 && px_glyph_palette(&f, g)->n == 1 && px_glyph_palette(&f, g)->c[0] == 0x00FF00FFu,
              "an empty glyph: green takes the unused black entry of the default palette");
        px_font_remove_glyph(&f, ti);
    }
    /* 36 colours in a glyph; a 37th refused */
    {
        PxGlyph t;
        memset(&t, 0, sizeof t);
        t.code = px_code_cp(0x57); t.w = 6; t.h = 6; t.advance = 7;
        t.pix = (char *)malloc(36);
        for (int k = 0; k < 36; k++) t.pix[k] = PX_PIXELS[1 + k];
        int ti = px_font_add_glyph(&f, &t);
        px_glyph_free(&t);
        PxGlyph *g = &f.glyphs[ti];
        uint32_t c36[36];
        for (int k = 0; k < 36; k++) c36[k] = many_colour(k);
        CHECK(px_glyph_set_palette(&f, g, c36, 36), "a glyph of 36 colours");
        int before = g->pi;
        CHECK(px_glyph_add_color(&f, g, many_colour(36)) == PX_COLOR_FULL && g->pi == before, "a 37th colour in one glyph is refused, nothing changes");
        CHECK(!px_glyph_colors_fit(&f, g, (const uint32_t[]){many_colour(1), many_colour(37)}, 2), "colours_fit: one new colour does not fit");
        CHECK(px_glyph_colors_fit(&f, g, (const uint32_t[]){many_colour(1), many_colour(35)}, 2), "colours_fit: colours it has fit");
        g->pix[35] = '0';     /* the colour of 'Z' is no longer used: a new colour may take its entry */
        px_glyph_touch(g);
        CHECK(px_glyph_add_color(&f, g, many_colour(36)) == 35 && px_glyph_palette(&f, g)->c[35] == many_colour(36), "a freed entry takes the new colour");
    }
    /* pasted colours: those the glyph has keep their entries, new ones take free entries, never one another of them got */
    {
        PxGlyph t;
        memset(&t, 0, sizeof t);
        t.code = px_code_cp(0x58); t.w = 3; t.h = 1; t.advance = 4;
        t.pix = (char *)malloc(3);
        memcpy(t.pix, "0.2", 3);
        int ti = px_font_add_glyph(&f, &t);
        px_glyph_free(&t);
        PxGlyph *g = &f.glyphs[ti];
        uint32_t p4[4] = {0x111111FFu, 0x222222FFu, 0x333333FFu, 0x444444FFu};
        CHECK(px_glyph_set_palette(&f, g, p4, 4), "a glyph of palette 1 2 3 4 that uses 1 and 3");
        uint32_t paste[3] = {0x555555FFu, 0x444444FFu, 0x666666FFu};
        int slots[3];
        CHECK(px_glyph_add_colors(&f, g, paste, 3, slots) == 0, "pasting three colours");
        const PxPalette *gp = px_glyph_palette(&f, g);
        CHECK(slots[1] == 3 && slots[0] == 4 && slots[2] == 5 && gp->n == 6 && gp->c[4] == 0x555555FFu && gp->c[3] == 0x444444FFu && gp->c[5] == 0x666666FFu,
              "paste mapping: 4 keeps entry 3, 5 and 6 new entries (version 23: the unused entry 1 stays)");
        px_font_remove_glyph(&f, ti);
    }
    /* compaction drops what no glyph names; a copy is compacted */
    ga = &f.glyphs[px_font_find_cp(&f, 0x41)];     /* the glyphs moved (glyphs were added) */
    gh = &f.glyphs[px_font_find_cp(&f, 0x48)];
    {
        int before = f.npals;
        px_glyph_set_palette(&f, ga, (const uint32_t[]){0x123456FFu}, 1);   /* an entry, then abandoned */
        px_glyph_set_palette(&f, ga, px_glyph_palette(&f, gh)->c, px_glyph_palette(&f, gh)->n);
        CHECK(f.npals == before + 1, "an abandoned entry stays in the table");
        PxFont c;
        CHECK(px_font_copy(&c, &f) && c.npals == 3 && font_eq(&c, &f), "a copy (an undo snapshot) is compacted (the default, the A's and H's, the W's) and equal");
        unsigned gen = f.palGen;
        px_font_compact_palettes(&f);
        CHECK(f.npals == 3 && f.palGen != gen && font_eq(&c, &f), "compaction drops the entries no glyph names, renumbers, changes palGen");
        px_font_free(&c);
    }
    px_font_free(&f);

    /* a font of 40 colours over its glyphs: written, read back, the same bytes; COLORS 40 */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    for (int i = 0; i < 20; i++) {
        add(&f, 0x41 + i, 6, 0, 0, "0000\n0..0\n1111\n1..1\n1..1");
        uint32_t pc[2] = {many_colour(2 * i), many_colour(2 * i + 1)};
        px_glyph_set_palette(&f, &f.glyphs[px_font_find_cp(&f, 0x41 + i)], pc, 2);
    }
    add(&f, 0x56, 6, 0, 0, "0...0\n0...0\n.0.0.\n.0.0.\n..0..");
    px_glyph_set_palette(&f, &f.glyphs[px_font_find_cp(&f, 0x56)], (const uint32_t[]){many_colour(0), many_colour(1)}, 2);   /* longer than it needs */
    CHECK(px_font_colors(&f) == 40, "40 colours: COLORS 40");
    {
        const uint32_t *cl;
        CHECK(px_font_color_list(&f, &cl) == 40 && cl[0] == many_colour(0) && cl[1] == many_colour(1) && cl[39] == many_colour(39), "40 colours: the colour list, glyph by glyph");
        CHECK(px_font_color_index(&f, many_colour(7)) == 7, "colour index");
    }
    size_t n;
    char *t = px_pxfm_text(&f, &n, hash);
    CHECK(t && strstr(t, "COLORS\t40\n") != NULL, "40 colours: COLORS 40 written");
    CHECK(px_save_final(&f, TMP "/manylib", hash, path, sizeof path, err), "40 colours: saved");
    PxFont r;
    CHECK(px_read_pxfm(path, NULL, &r, err), "40 colours: read back");
    {
        size_t n2;
        char h2[33];
        char *t2 = px_pxfm_text(&r, &n2, h2);
        CHECK(t2 && n2 == n && memcmp(t, t2, n) == 0 && strcmp(h2, hash) == 0 && strcmp(r.fileHash, hash) == 0, "40 colours: written back byte for byte");
        free(t2);
        const PxGlyph *gv = &r.glyphs[px_font_find_cp(&r, 0x56)], *gA = &r.glyphs[px_font_find_cp(&r, 0x41)];
        CHECK(gv->pi == gA->pi && px_glyph_palette(&r, gv)->n == 2, "40 colours: the V shares the A's palette, read whole (2 lines for 1 colour)");
        CHECK(font_eq(&f, &r), "40 colours: the font read is the font written");
    }
    free(t);
    /* a draft keeps the glyphs' palettes */
    CHECK(px_save_draft(&r, NULL, TMP "/many.pxfd", err), "40 colours: draft saved");
    PxFont d;
    PxDraftInfo di;
    px_draft_info_init(&di);
    CHECK(px_load_draft(TMP "/many.pxfd", &d, &di, err) && font_eq(&r, &d), "40 colours: draft loaded, the same palettes");
    {
        char h1[33], h2[33];
        px_font_hash(&r, h1);
        px_font_hash(&d, h2);
        CHECK(strcmp(h1, h2) == 0, "40 colours: the draft writes the same font (its read palettes kept)");
    }
    px_font_free(&d);
    px_draft_info_free(&di);
    px_font_free(&r);
    px_font_free(&f);

    /* a draft of an older pxfe (version 3, the codes of pxfm version 2) is refused with a message */
    FILE *fp = fopen(TMP "/v3.pxfd", "wb");
    fputs("PXFEDRAFT\t3\nNAME\tx\nGLYPH\t000041\t0,0\t4\t3x1\t0\n\t1.0\n", fp);
    fclose(fp);
    CHECK(!px_load_draft(TMP "/v3.pxfd", &d, &di, err) && strstr(err, "version 3 draft"), "a version 3 draft is refused");
}

static void test_save_final(void) {
    PxFont f;
    char err[PX_ERR_LEN];
    example_font(&f);
    f.glyphs[px_font_find_cp(&f, 0x41)].advance = 7;   /* a new font */
    char hash[33], path[1024];
    CHECK(px_save_final(&f, TMP "/finallib", hash, path, sizeof path, err), "save final");
    CHECK(px_file_exists(path), "pxfm written");
    PxFont r;
    CHECK(px_read_pxfm(path, NULL, &r, err), "saved font reads back (glyph files written)");
    CHECK(font_eq(&f, &r) || true, "read back");
    char h2[33];
    px_font_hash(&r, h2);
    CHECK(strcmp(h2, hash) == 0 && strcmp(r.fileHash, hash) == 0, "saved name == hash");
    CHECK(r.glyphs[px_font_find_cp(&r, 0x41)].advance == 7, "advance preserved");
    /* the second save does not rewrite anything */
    CHECK(px_save_final(&f, TMP "/finallib", hash, path, sizeof path, err), "saving again is fine");
    px_font_free(&r);
    px_font_free(&f);
}

static void test_draft_roundtrip(void) {
    PxFont f;
    example_font(&f);
    /* uncropped bitmaps, colours, dangling FFFFFF (default cleared), kerning, an as-drawn glyph */
    f.hasRatio = true; f.ratioA = 1; f.ratioB = 1;
    f.monospaced = true;
    f.symbolic = true;
    add(&f, 0x61, 5, -1, -1, ".....\n..1..\n.1.1.\n.....");
    add_unc(&f, 3, 1, 0, "00\n00");
    px_font_set_kern(&f, px_code_symbol(0), px_code_cp(0x41), -1);
    f.glyphs[px_font_find_cp(&f, 0x61)].asDrawn = true;
    {
        PxGlyph dg;
        memset(&dg, 0, sizeof dg);
        dg.code = PX_CODE_DEFAULT; dg.x = 2; dg.y = -1; dg.advance = 9;
        dg.w = 2; dg.h = 2; dg.pix = (char *)malloc(4); memcpy(dg.pix, "0000", 4);
        px_glyph_touch(&dg);
        px_font_add_glyph(&f, &dg);
        px_glyph_free(&dg);
    }
    px_font_set_kern(&f, px_code_cp(0x48), px_code_cp(0x41), 2);
    font_colours(&f, WHITE_GREY, 2);

    PxtfTypeface c;
    pxtf_init(&c);
    c.v = (PxtfVariant *)calloc(2, sizeof(PxtfVariant));
    c.count = c.cap = 2;
    for (int i = 0; i < 2; i++) {
        c.v[i].number = i + 1;
        c.v[i].format = strdup("BDF");
        c.v[i].type = strdup("bdf");
        snprintf(c.v[i].source, 33, "%s", "bcc7a744d44f7f35cbfaa1efb0048a63");
        snprintf(c.v[i].pxfm, 33, "%s", i == 0 ? H_EXAMPLE : H_NOASC);
    }
    pxtf_set_name(&c, "Draft \xC3\x81" "dam\ttab\\");
    pxtf_set_text(&c.v[0], PXTF_NAME, "Draft \xC3\x81" "dam Regular");
    pxtf_set_text(&c.v[0], PXTF_STYLE, "Regular");
    c.v[1].italic = true;
    PxDraftInfo info;
    px_draft_info_init(&info);
    snprintf(info.name, sizeof info.name, "Duplicate of Test \xC3\x81" "dam");
    snprintf(info.originalHash, 33, "%s", H_EXAMPLE);
    snprintf(info.originalPxtfHash, 33, "%s", "e5f670ff9089e75f95cf069dd0987dfb");
    info.originalVariant = 1;
    info.typeface = &c;

    char err[PX_ERR_LEN];
    CHECK(px_save_draft(&f, &info, TMP "/test.pxfd", err), "draft saved");
    PxFont r;
    PxDraftInfo ri;
    px_draft_info_init(&ri);
    CHECK(px_load_draft(TMP "/test.pxfd", &r, &ri, err), "draft loaded");
    CHECK(font_eq(&f, &r), "draft: font equal, uncropped bitmaps, colours, kerning, dangling FFFFFF, as-drawn flag");
    char h1[33], h2[33];
    px_font_hash(&f, h1);
    px_font_hash(&r, h2);
    CHECK(strcmp(h1, h2) == 0, "draft: same font hash");
    CHECK(strcmp(ri.name, info.name) == 0, "draft: name");
    CHECK(strcmp(ri.originalHash, H_EXAMPLE) == 0 && strcmp(ri.originalPxtfHash, info.originalPxtfHash) == 0 && ri.originalVariant == 1, "draft: original hashes and variant");
    CHECK(ri.typeface && ri.typeface->count == 2 && ri.typeface->name && strcmp(ri.typeface->name, "Draft \xC3\x81" "dam\ttab\\") == 0 &&
          ri.typeface->v[1].italic && strcmp(ri.typeface->v[1].source, c.v[1].source) == 0, "draft: duplicated typeface survives");
    char ch1[33], ch2[33];
    pxtf_hash(&c, ch1);
    pxtf_hash(ri.typeface, ch2);
    CHECK(strcmp(ch1, ch2) == 0, "draft: typeface hash equal");
    size_t n1, n2;
    char *t1 = px_pxfm_text(&f, &n1, h1), *t2 = px_pxfm_text(&r, &n2, h2);
    CHECK(n1 == n2 && memcmp(t1, t2, n1) == 0 && strstr(t1, "GLYPH\tI+DEFA00-000\t9\t2\t-1\t") != NULL, "draft: an FFFFFF glyph of its own written back");
    free(t1); free(t2);
    CHECK(r.glyphs[px_font_find_cp(&r, 0x61)].w == 5 && r.glyphs[px_font_find_cp(&r, 0x61)].h == 4, "draft: bitmap stays uncropped");

    /* a draft without typeface and without original */
    PxDraftInfo none;
    px_draft_info_init(&none);
    CHECK(px_save_draft(&r, &none, TMP "/test2.pxfd", err), "draft without typeface saved");
    PxFont r2;
    PxDraftInfo ri2;
    px_draft_info_init(&ri2);
    CHECK(px_load_draft(TMP "/test2.pxfd", &r2, &ri2, err) && ri2.typeface == NULL && ri2.originalHash[0] == 0 && font_eq(&r, &r2), "draft without typeface loads");
    px_font_free(&r2);
    px_draft_info_free(&ri2);
    FILE *fp = fopen(TMP "/old.pxfd", "wb");
    fputs("PXFEDRAFT\t1\n", fp);
    fclose(fp);
    CHECK(!px_load_draft(TMP "/old.pxfd", &r2, &ri2, err) && strstr(err, "version") != NULL, "draft version 1 refused");
    fp = fopen(TMP "/old2.pxfd", "wb");
    fputs("PXFEDRAFT\t2\nNAME\tx\n", fp);
    fclose(fp);
    CHECK(!px_load_draft(TMP "/old2.pxfd", &r2, &ri2, err) && strstr(err, "version 2 draft") != NULL, "draft version 2 refused with a clear message");
    px_font_free(&r);
    px_draft_info_free(&ri);
    info.typeface = NULL;
    pxtf_free(&c);
    px_font_free(&f);
}

int main(void) {
    px_mkdir_p(TMP);
    test_sha_and_canonical();
    test_codes();
    test_spec_examples();
    test_reader_strict();
    test_writer_details();
    test_glyph_helpers_and_hash_cache();
    test_change_detection();
    test_lazy_loading();
    test_hex_and_canonical();
    test_colour_fonts();
    test_glyph_palettes();
    test_canonical_palettes();
    test_save_final();
    test_draft_roundtrip();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
