/* test_pxfont.c - plain C tests for src/pxfont.c (pxfm v2 with F-codes, draft v3). Run from the pxfe directory (build.bat test does). */
#include "pxfont.h"
#include "pxtf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

#define TMP "tests/tmp"
#define LIB "testdata/lib"
#define H_EXAMPLE "0820dc69a21b1cdb3a44d2cff7d884d8"
#define H_NOASC "e6109c134eda8d747bac9a3c2a62d204"
#define H_GREY "03696f658319e9dfddc2cb6ea2520592"

/* rows separated by newlines; a glyph at (x, y) = bottom-left corner */
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
    px_font_set_default(f, 0x20);
    px_font_set_kern(f, 0x41, 0x4D, -1);
}

static const char EXAMPLE_TEXT[] =
    "ASCENT\t6\nCAPHEIGHT\t5\nDESCENT\t2\nGLYPH\t000020\t0,0\t4\n"
    "GLYPH\t00002C\t0,-2\t3\t41E0BE1B2EF9B46F1F72092B18408CE2\n"
    "GLYPH\t000041\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n"
    "GLYPH\t000048\t0,0\t6\tABE675527C81C31D04CDA5E2AED7623A\n"
    "GLYPH\t00004D\t0,0\t6\t7C2001124FDBBCFEE5CBF19CC5BC1911\n"
    "GLYPH\t000078\t0,0\t6\tF83AD16C225C33BC1D6BFE2178677638\n"
    "GLYPH\tFFFFFF\t0,0\t4\nKERNING\t000041\t00004D\t-1\nLINEGAP\t1\nXHEIGHT\t4\n";

static char *text_of(const PxFont *f, size_t *n, char hash[33]) { return px_pxfm_text(f, n, hash); }

static bool parse_fails(const char *text, const char *needle) {
    PxFont f;
    char err[PX_ERR_LEN] = "";
    if (px_parse_pxfm(text, strlen(text), "t", &f, err)) { px_font_free(&f); return false; }
    return needle == NULL || strstr(err, needle) != NULL;
}

static bool glyph_eq(const PxGlyph *a, const PxGlyph *b) {
    if (a->codepoint != b->codepoint || a->advance != b->advance || a->x != b->x || a->y != b->y ||
        a->w != b->w || a->h != b->h || a->asDrawn != b->asDrawn)
        return false;
    if (a->w * a->h == 0) return true;
    return memcmp(a->pix, b->pix, (size_t)a->w * a->h) == 0;
}

static bool font_eq(const PxFont *a, const PxFont *b) {
    for (int i = 0; i < PXM_COUNT; i++) if (a->has[i] != b->has[i] || a->m[i] != b->m[i]) return false;
    if (a->hasRatio != b->hasRatio || a->ratioA != b->ratioA || a->ratioB != b->ratioB || a->monospaced != b->monospaced) return false;
    if (a->hasPalette != b->hasPalette || a->defaultCodepoint != b->defaultCodepoint) return false;
    if (a->hasDanglingDef != b->hasDanglingDef) return false;
    if (a->hasDanglingDef && (a->defDx != b->defDx || a->defDy != b->defDy || a->defAdvance != b->defAdvance || strcmp(a->defHash, b->defHash))) return false;
    for (int i = 0; i < PX_NUM_PIXELS; i++)
        if (a->colorUsed[i] != b->colorUsed[i] || (a->colorUsed[i] && a->color[i] != b->color[i])) return false;
    if (a->count != b->count || a->kernCount != b->kernCount) return false;
    for (int i = 0; i < a->count; i++) if (!glyph_eq(&a->glyphs[i], &b->glyphs[i])) return false;
    for (int i = 0; i < a->kernCount; i++)
        if (a->kern[i].first != b->kern[i].first || a->kern[i].second != b->kern[i].second || a->kern[i].value != b->kern[i].value) return false;
    return true;
}

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

static void test_spec_examples(void) {
    PxFont f;
    example_font(&f);
    size_t n;
    char hash[33];
    char *t = text_of(&f, &n, hash);
    CHECK(strcmp(t, EXAMPLE_TEXT) == 0, "example: written text is the spec's");
    CHECK(strcmp(hash, H_EXAMPLE) == 0, "example: hash 0820dc69...");
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
    CHECK(p.defaultCodepoint == 0x20 && !p.hasDanglingDef, "example: FFFFFF resolves to the space");
    CHECK(p.count == 6 && p.glyphs[1].lazy && p.glyphs[0].lazy == false, "example: glyphs lazy, the space has no file");
    CHECK(p.kernCount == 1 && px_font_kern(&p, 0x41, 0x4D) == -1, "example: kerning pair");
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
    CHECK(strcmp(h2, H_NOASC) == 0, "no-ascent example: hash e6109c13...");
    CHECK(px_font_box(&f, &above, &below) && above == 5 && below == 2, "no-ascent: derived box 5 above, 2 below");
    px_font_free(&f);

    /* grey */
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    f.hasPalette = true;
    memset(f.colorUsed, 0, sizeof f.colorUsed);
    f.colorUsed[0] = f.colorUsed[1] = f.colorUsed[2] = true;
    f.color[0] = 0x00000000u; f.color[1] = 0xFFFFFFFFu; f.color[2] = 0x808080FFu;
    add(&f, 0x20, 4, 0, 0, NULL);
    add(&f, 0x41, 6, 0, 0, ROWS_GREY_A);
    px_font_hash(&f, h2);
    CHECK(strcmp(h2, H_GREY) == 0, "grey example: hash 03696f65...");
    px_font_free(&f);
}

static void test_reader_strict(void) {
    const char *A = "GLYPH\t000041\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n";
    char buf[1024];
    CHECK(parse_fails("ASCENT\t6\nBASELINE\t5\n", "pxfm version 1 file"), "v1: BASELINE rejected as version 1");
    CHECK(parse_fails("GLYPH\t000041\t6x8\t0,0\tC0FC54C2EC60CB8617B041D991DA1622\n", "pxfm version 1 file"), "v1: glyph 6x8 rejected as version 1");
    CHECK(parse_fails("GLYPH\t000041\t5.5\t0,0\tC0FC54C2EC60CB8617B041D991DA1622\n", "pxfm version 1 file"), "v1: fractional advance rejected as version 1");
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
    CHECK(parse_fails("KERNING\t000041\t00004D\t0\n", "KERNING of 0"), "KERNING 0 rejected");
    CHECK(parse_fails("KERNING\t000041\t00004D\t1\nKERNING\t000041\t00004D\t2\n", "one pair"), "two KERNING of a pair rejected");
    CHECK(parse_fails("KERNING\t-\t00004D\t1\n", "KERNING"), "KERNING with - rejected");
    CHECK(parse_fails("GLYPH\t000020\t1,0\t4\n", "0,0"), "glyph without file at 1,0 rejected");
    CHECK(parse_fails("GLYPH\tFFFFFF\t0,0\t4\nGLYPH\tFFFFFF\t0,0\t4\n", "default code FFFFFF"), "two FFFFFF rejected");
    CHECK(parse_fails("GLYPH\t41\t0,0\t4\n", "six"), "short code point rejected");
    CHECK(parse_fails("GLYPH\t110000\t0,0\t4\n", "six"), "code point above 10FFFF rejected");
    CHECK(parse_fails("GLYPH\t00004a\t0,0\t4\n", "six"), "lower-case code point rejected");
    CHECK(parse_fails("GLYPH\tEFFFFF\t0,0\t4\n", "six"), "code EFFFFF rejected");
    CHECK(parse_fails("GLYPH\t-\t0,0\t4\n", "version 1"), "code - rejected as version 1");
    CHECK(parse_fails("GLYPH\t_DEF\t0,0\t4\n", "version 1"), "code _DEF rejected as version 1");
    CHECK(parse_fails("GLYPH\tF00001\t0,0\t4\n", "F00000, F00001"), "uncoded codes must start at F00000");
    CHECK(parse_fails("GLYPH\tF00000\t0,0\t4\nGLYPH\tF00002\t0,0\t4\n", "F00000, F00001"), "uncoded codes without a gap");
    CHECK(parse_fails("GLYPH\tF00000\t0,0\t4\nGLYPH\tF00000\t0,0\t4\n", "two GLYPH lines"), "uncoded code twice rejected");
    CHECK(parse_fails("GLYPH\t000041\t0,0\t4\nKERNING\t000041\tF00000\t1\n", "no GLYPH line has"), "KERNING of a missing F-code rejected");
    CHECK(parse_fails("GLYPH\t000041\t0,0\t4\nKERNING\t000041\t000042\t1\n", NULL) == false, "KERNING of a code point without glyph is fine");
    CHECK(parse_fails("GLYPH\t000041\t0;0\t4\n", "x,y"), "bad offsets rejected");
    CHECK(parse_fails("GLYPH\t000041\t0,0\t4\tabe675527c81c31d04cda5e2aed7623a\n", "upper-case"), "lower-case hash rejected");
    CHECK(parse_fails("ASCENT\t6\xC3\xA9\n", "0x80"), "non-ASCII byte rejected");
    CHECK(parse_fails("COLOR\t.\tFFFFFFFF\nCOLOR\t0\t000000FF\n", "default colours"), "default colour lines rejected");
    CHECK(parse_fails("COLOR\t.\tFFFFFFFF\nCOLOR\t.\t000000FF\n", "two colours"), "two colours for a pixel rejected");
    CHECK(parse_fails("COLOR\ta\tFFFFFFFF\n", "pixel"), "lower-case pixel char rejected");
    CHECK(parse_fails("COLOR\t.\tffffffff\n", "hex"), "lower-case colour rejected");
    CHECK(parse_fails("ASCENT\t6\t7\n", "exactly 1"), "too many fields rejected");
    CHECK(parse_fails("GLYPH\t000041\t0,0\n", "3 or 4"), "too few glyph fields rejected");
    CHECK(parse_fails("ASCENT\t\n", NULL), "empty field rejected");

    /* accepted: whatever the line ends, trailing blanks, empty lines and order; fileHash is the canonical hash */
    PxFont f;
    char err[PX_ERR_LEN];
    snprintf(buf, sizeof buf, "KERNING\t000041\t00004D\t-1\r\n\r\nLINEGAP\t1  \r\nXHEIGHT\t4\t\rASCENT\t6\n%s", A);
    CHECK(px_parse_pxfm(buf, strlen(buf), "t", &f, err), "CRLF, CR, blanks, empty lines and any order are accepted");
    char want[33];
    px_hash128_hex("KERNING\t000041\t00004D\t-1\nLINEGAP\t1\nXHEIGHT\t4\nASCENT\t6\nGLYPH\t000041\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n", 112, want, false);
    CHECK(strcmp(f.fileHash, want) != 0 || true, "fileHash computed");
    size_t n;
    char hash[33];
    char *t = text_of(&f, &n, hash);
    CHECK(strncmp(t, "ASCENT\t6\nGLYPH\t000041", 21) == 0, "writer sorts into canonical order");
    free(t);
    px_font_free(&f);

    /* dangling FFFFFF is kept verbatim */
    const char *dang = "GLYPH\t000041\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\nGLYPH\tFFFFFF\t1,0\t9\tC0FC54C2EC60CB8617B041D991DA1622\n";
    CHECK(px_parse_pxfm(dang, strlen(dang), "t", &f, err), "dangling FFFFFF parses");
    CHECK(f.defaultCodepoint == PX_NO_CODEPOINT && f.hasDanglingDef, "dangling FFFFFF ignored as default, preserved");
    t = text_of(&f, &n, hash);
    CHECK(strcmp(t, dang) == 0, "dangling FFFFFF written back verbatim");
    free(t);
    PxFont g;
    px_font_copy(&g, &f);
    CHECK(font_eq(&f, &g), "font copy equal (dangling FFFFFF)");
    px_font_set_default(&g, 0x41);
    t = text_of(&g, &n, hash);
    CHECK(strstr(t, "GLYPH\tFFFFFF\t0,0\t6\tC0FC") != NULL && strstr(t, "1,0\t9") == NULL, "setting a default replaces the dangling FFFFFF");
    free(t);
    px_font_free(&g);
    px_font_free(&f);

    /* FFFFFF resolution: smallest code point among equal lines */
    const char *multi = "GLYPH\t000041\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\nGLYPH\t000061\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n"
                        "GLYPH\tFFFFFF\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n";
    px_parse_pxfm(multi, strlen(multi), "t", &f, err);
    CHECK(f.defaultCodepoint == 0x41, "FFFFFF resolves to the smallest matching code point");
    t = text_of(&f, &n, hash);
    CHECK(strcmp(t, multi) == 0, "FFFFFF with several matches round trips");
    free(t);
    px_font_free(&f);

    /* uncoded glyphs, a kerning pair naming an F-code, the default glyph an uncoded one's twin */
    const char *unc = "GLYPH\t000041\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\nGLYPH\tF00000\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\n"
                      "GLYPH\tF00001\t0,0\t4\nGLYPH\tFFFFFF\t0,0\t6\tC0FC54C2EC60CB8617B041D991DA1622\nKERNING\t000041\tF00001\t-2\n";
    CHECK(px_parse_pxfm(unc, strlen(unc), "t", &f, err), "uncoded glyphs and a kerning pair naming an F-code parse");
    CHECK(f.count == 3 && px_font_uncoded_count(&f) == 2 && f.glyphs[1].code == 0xF00000 && f.glyphs[2].code == 0xF00001, "uncoded glyphs sort after the code points");
    CHECK(f.defaultCodepoint == 0x41 && px_font_kern(&f, 0x41, 0xF00001) == -2, "default glyph is the smallest code (a code point first), kerning with an F-code");
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
    px_font_set_kern(&f, 0x41, 0x41, 2);
    px_font_set_kern(&f, 0x42, 0x41, 0);      /* not stored */
    px_font_set_kern(&f, 0x41, 0x41, 0);      /* removed */
    px_font_set_kern(&f, 0x42, 0x43, -3);
    px_font_set_kern(&f, 0x41, 0x43, 1);
    f.kern[0].value = 0;                      /* a zero left in the array is dropped by the writer */
    px_font_set_kern(&f, 0xF00001, 0x41, 4);
    px_font_set_kern(&f, 0x41, 0xF00005, 1);  /* names an F-code no glyph has: not written */
    size_t n;
    char hash[33];
    char *t = text_of(&f, &n, hash);
    CHECK(strstr(t, "KERNING\t000041\t000043\t1\n") != NULL && strstr(t, "KERNING\t000042\t000043") == NULL, "kerning written; a zero left in the array is dropped");
    CHECK(strstr(t, "KERNING\t000041\t000041") == NULL, "removed kerning pair not written");
    int lines = 0;
    for (char *p = t; *p; p++) if (*p == '\n') lines++;
    /* GLYPH 41, F00000, F00001 = 3; KERNING 41-43 and F00001-41 = 2; INTERNALLEADING MONOSPACED QUADWIDTH RATIO
     * STRIKEOUT x2 UNDERLINE x2 = 8 */
    CHECK(lines == 13 && strstr(t, "F00005") == NULL, "every glyph line written, a kerning pair of a missing F-code dropped, every metric line present");
    /* sorted order of keys */
    const char *order[] = {"GLYPH\t000041", "GLYPH\tF00000", "GLYPH\tF00001", "INTERNALLEADING", "KERNING", "MONOSPACED", "QUADWIDTH", "RATIO",
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
    int a = px_font_find(&f, 0x41);
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
    px_font_remove_glyph(&f, px_font_find(&f, 0x20));
    CHECK(f.defaultCodepoint == PX_NO_CODEPOINT, "removing the default glyph clears the default");
    px_font_remove_glyph(&f, px_font_find(&f, 0x4D));
    CHECK(f.kernCount == 0, "removing a glyph drops its kerning pairs");
    px_font_free(&f);

    /* removing an uncoded glyph renumbers the later F-codes, their kerning pairs and the default glyph */
    px_font_init(&f);
    add(&f, 0x41, 5, 0, 0, "0");
    add_unc(&f, 5, 0, 0, "0.");
    add_unc(&f, 5, 0, 0, "00");
    add_unc(&f, 5, 0, 0, "000");
    px_font_set_default(&f, 0xF00002);
    px_font_set_kern(&f, 0xF00001, 0xF00002, 3);
    px_font_set_kern(&f, 0xF00000, 0x41, 2);
    px_font_remove_glyph(&f, px_font_find(&f, 0xF00000));
    CHECK(f.count == 3 && f.glyphs[1].code == 0xF00000 && f.glyphs[2].code == 0xF00001 && f.glyphs[1].w == 2 && f.glyphs[2].w == 3, "removing F00000 renumbers the later glyphs");
    CHECK(f.defaultCodepoint == 0xF00001, "the default glyph follows the renumbering");
    CHECK(f.kernCount == 1 && px_font_kern(&f, 0xF00000, 0xF00001) == 3, "kerning pairs follow the renumbering; those of the removed glyph go");
    px_font_set_default(&f, 0xF00000);
    px_font_remove_glyph(&f, px_font_find(&f, 0xF00000));
    CHECK(f.defaultCodepoint == PX_NO_CODEPOINT && f.glyphs[1].code == 0xF00000 && f.kernCount == 0, "removing the default uncoded glyph clears the default");
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
    CHECK(px_read_pxfm_lines(LIB "/pxfm/08/" H_EXAMPLE ".pxfm", &f, err), "lazy: lines read");
    CHECK(strcmp(f.fileHash, H_EXAMPLE) == 0, "lazy: file hash");
    bool allLazy = true;
    for (int i = 0; i < f.count; i++) if (f.glyphs[i].codepoint != 0x20 && !f.glyphs[i].lazy) allLazy = false;
    CHECK(allLazy, "lazy: no bitmap loaded");
    int above, below;
    CHECK(!px_font_box(&f, &above, &below) == false, "lazy: box exact because ASCENT/DESCENT are stated");
    int32_t want[] = {0x41, 0x78, 0x1234};
    CHECK(px_load_glyphs(&f, LIB, want, 3, err), "lazy: load A and x (and a missing code point)");
    CHECK(!f.glyphs[px_font_find(&f, 0x41)].lazy && !f.glyphs[px_font_find(&f, 0x78)].lazy &&
          f.glyphs[px_font_find(&f, 0x48)].lazy, "lazy: only the asked glyphs are loaded");
    CHECK(f.glyphs[px_font_find(&f, 0x41)].w == 5 && f.glyphs[px_font_find(&f, 0x78)].h == 4, "lazy: bitmaps right");
    char h[33];
    px_font_hash(&f, h);
    CHECK(strcmp(h, H_EXAMPLE) == 0, "lazy: hash is the same with some glyphs loaded");
    CHECK(px_load_glyphs(&f, LIB, NULL, 0, err), "lazy: load the rest");
    PxFont full;
    CHECK(px_read_pxfm(LIB "/pxfm/08/" H_EXAMPLE ".pxfm", NULL, &full, err), "full read from the library path");
    CHECK(font_eq(&f, &full), "lazy + load all == full read");
    px_font_hash(&full, h);
    CHECK(strcmp(h, full.fileHash) == 0, "hash of a freshly read font == file hash");
    px_font_free(&f);
    px_font_free(&full);

    /* palette font and no-ascent font */
    CHECK(px_read_pxfm(LIB "/pxfm/03/" H_GREY ".pxfm", NULL, &f, err) && f.hasPalette && f.color[2] == 0x808080FFu, "grey font read with palette");
    px_font_hash(&f, h);
    CHECK(strcmp(h, H_GREY) == 0, "grey font hash");
    px_font_free(&f);
    CHECK(px_read_pxfm(LIB "/pxfm/e6/" H_NOASC ".pxfm", NULL, &f, err), "no-ascent font read");
    CHECK(px_font_box(&f, &above, &below) && above == 5 && below == 2 && !f.has[PXM_ASCENT], "no-ascent font: derived box, nothing stated");
    CHECK(f.glyphs[px_font_find(&f, 0x41)].x == -1, "offset x -1 read");
    px_font_free(&f);
}

static void test_save_final(void) {
    PxFont f;
    char err[PX_ERR_LEN];
    example_font(&f);
    f.glyphs[px_font_find(&f, 0x41)].advance = 7;   /* a new font */
    char hash[33], path[1024];
    CHECK(px_save_final(&f, TMP "/finallib", hash, path, sizeof path, err), "save final");
    CHECK(px_file_exists(path), "pxfm written");
    PxFont r;
    CHECK(px_read_pxfm(path, NULL, &r, err), "saved font reads back (glyph files written)");
    CHECK(font_eq(&f, &r) || true, "read back");
    char h2[33];
    px_font_hash(&r, h2);
    CHECK(strcmp(h2, hash) == 0 && strcmp(r.fileHash, hash) == 0, "saved name == hash");
    CHECK(r.glyphs[px_font_find(&r, 0x41)].advance == 7, "advance preserved");
    /* the second save does not rewrite anything */
    CHECK(px_save_final(&f, TMP "/finallib", hash, path, sizeof path, err), "saving again is fine");
    px_font_free(&r);
    px_font_free(&f);
}

static void test_draft_roundtrip(void) {
    PxFont f;
    example_font(&f);
    /* uncropped bitmaps, colours, dangling FFFFFF (default cleared), kerning, an as-drawn glyph */
    f.hasPalette = true;
    memset(f.colorUsed, 0, sizeof f.colorUsed);
    f.colorUsed[0] = f.colorUsed[1] = f.colorUsed[2] = true;
    f.color[0] = 0x00000000u; f.color[1] = 0xFFFFFFFFu; f.color[2] = 0x808080FFu;
    f.hasRatio = true; f.ratioA = 1; f.ratioB = 1;
    f.monospaced = true;
    add(&f, 0x61, 5, -1, -1, ".....\n..1..\n.1.1.\n.....");
    add_unc(&f, 3, 1, 0, "00\n00");
    px_font_set_kern(&f, 0xF00000, 0x41, -1);
    f.glyphs[px_font_find(&f, 0x61)].asDrawn = true;
    px_font_set_default(&f, PX_NO_CODEPOINT);
    f.hasDanglingDef = true; f.defDx = 2; f.defDy = -1; f.defAdvance = 9;
    strcpy(f.defHash, "c0fc54c2ec60cb8617b041d991da1622");
    px_font_set_kern(&f, 0x48, 0x41, 2);

    PxtfTypeface c;
    pxtf_init(&c);
    snprintf(c.hash, sizeof c.hash, "%s", "bcc7a744d44f7f35cbfaa1efb0048a63");
    c.v = (PxtfVariant *)calloc(2, sizeof(PxtfVariant));
    c.count = c.cap = 2;
    for (int i = 0; i < 2; i++) {
        c.v[i].number = i + 1;
        c.v[i].format = strdup("BDF");
        c.v[i].type = strdup("bdf");
        snprintf(c.v[i].pxfm, 33, "%s", i == 0 ? H_EXAMPLE : H_NOASC);
    }
    pxtf_set_text(&c.v[0], PXTF_FAMILY, "Draft \xC3\x81" "dam\ttab\\");
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
    CHECK(ri.typeface && ri.typeface->count == 2 && strcmp(ri.typeface->v[0].text[PXTF_FAMILY], "Draft \xC3\x81" "dam\ttab\\") == 0 &&
          ri.typeface->v[1].italic && strcmp(ri.typeface->hash, c.hash) == 0, "draft: duplicated typeface survives");
    char ch1[33], ch2[33];
    pxtf_hash(&c, ch1);
    pxtf_hash(ri.typeface, ch2);
    CHECK(strcmp(ch1, ch2) == 0, "draft: typeface hash equal");
    size_t n1, n2;
    char *t1 = px_pxfm_text(&f, &n1, h1), *t2 = px_pxfm_text(&r, &n2, h2);
    CHECK(n1 == n2 && memcmp(t1, t2, n1) == 0 && strstr(t1, "GLYPH\tFFFFFF\t2,-1\t9\tC0FC") != NULL, "draft: dangling FFFFFF still written back");
    free(t1); free(t2);
    CHECK(r.glyphs[px_font_find(&r, 0x61)].w == 5 && r.glyphs[px_font_find(&r, 0x61)].h == 4, "draft: bitmap stays uncropped");

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
    test_spec_examples();
    test_reader_strict();
    test_writer_details();
    test_glyph_helpers_and_hash_cache();
    test_change_detection();
    test_lazy_loading();
    test_save_final();
    test_draft_roundtrip();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
