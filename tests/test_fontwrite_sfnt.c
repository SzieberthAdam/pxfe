/* test_fontwrite_sfnt.c - plain C tests for src/fontwrite_sfnt.c (TrueType, OpenType CFF and WOFF writers).  Run from the pxfe
 * directory.  A small font built in memory is written in each format and read back with pxfe's own outline reader (outline.c):
 * every glyph's pixels at 16 px per em (one font pixel a pixel), advances, names, metrics, the cmap.  The files are also left in
 * tests/tmp/ (fw_sfnt*.ttf/.otf/.woff) for a check with fontTools. */
#include "fontwrite.h"
#include "outline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

#define TMP "tests/tmp/"

/* rows separated by '/', '#' ink; the glyph's bottom-left corner at (x, y) */
static void add(PxFont *f, PxCode code, int adv, int x, int y, const char *rows)
{
    PxGlyph g;
    memset(&g, 0, sizeof g);
    g.code = code; g.advance = adv; g.x = x; g.y = y;
    if (rows && rows[0]) {
        int w = (int)strcspn(rows, "/"), h = 1;
        for (const char *p = rows; *p; p++) h += *p == '/';
        g.w = w; g.h = h;
        g.pix = malloc((size_t)(w * h));
        int o = 0;
        for (const char *p = rows; *p; p++) if (*p != '/') g.pix[o++] = *p == '#' ? '0' : '.';
    }
    px_font_add_glyph(f, &g);
    px_glyph_free(&g);
}

typedef struct TG { PxCode code; int adv, x, y; const char *rows; } TG;
static const TG kGlyphs[] = {
    {0x20, 3, 0, 0, NULL},                                      /* space */
    {0x3D, 4, 0, 1, "###/.../###"},                              /* '=': two parts */
    {0x41, 6, 0, 0, "..#../.#.#./#...#/#####/#...#"},            /* 'A': pixels touching at corners */
    {0x4F, 5, 1, 0, "###/#.#/###"},                              /* 'O': a hole */
    {0x67, 4, 0, -2, "###/#.#/###/..#/###"},                     /* 'g': a descender */
    {0x2591, 4, 0, 0, "#.#/.#./#.#"},                            /* a checker board */
    {0x1F600, 3, 0, 0, "##/##"},                                 /* above FFFF */
};
#define NG ((int)(sizeof kGlyphs / sizeof kGlyphs[0]))
static const TG kSymbol = {0, 4, 0, 0, "#../.##"};
static const TG kDefault = {0, 4, 0, 0, "###/#.#/#.#/###"};

static void build(PxFont *f, bool symbolic, bool withDefault)
{
    px_font_init(f);
    for (int i = 0; i < NG; i++) add(f, px_code_cp((int32_t)kGlyphs[i].code), kGlyphs[i].adv, kGlyphs[i].x, kGlyphs[i].y, kGlyphs[i].rows);
    add(f, px_code_symbol(0), kSymbol.adv, kSymbol.x, kSymbol.y, kSymbol.rows);
    if (withDefault) add(f, PX_CODE_DEFAULT, kDefault.adv, kDefault.x, kDefault.y, kDefault.rows);
    f->has[PXM_ASCENT] = true; f->m[PXM_ASCENT] = 6;
    f->has[PXM_DESCENT] = true; f->m[PXM_DESCENT] = 2;
    f->has[PXM_LINEGAP] = true; f->m[PXM_LINEGAP] = 1;
    f->has[PXM_XHEIGHT] = true; f->m[PXM_XHEIGHT] = 3;
    f->has[PXM_CAPHEIGHT] = true; f->m[PXM_CAPHEIGHT] = 5;
    f->symbolic = symbolic;
    px_font_set_kern(f, px_code_cp(0x41), px_code_cp(0x4F), -1);
    px_font_set_kern(f, px_code_cp(0x4F), px_code_symbol(0), 2);
    px_font_set_kern(f, px_code_cp(0x41), px_code_cp(0x42), 1);   /* names no glyph: left out */
}

/* does the bitmap at 16 px per em show exactly the glyph's ink? */
static bool same_pixels(const OutlineBitmap *b, const TG *t)
{
    int w = t->rows ? (int)strcspn(t->rows, "/") : 0, h = 0, inkG = 0, inkB = 0;
    if (t->rows) { h = 1; for (const char *p = t->rows; *p; p++) h += *p == '/'; }
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) inkG += t->rows[r * (w + 1) + c] == '#';
    for (int r = 0; r < b->h; r++)
        for (int c = 0; c < b->w; c++) {
            if (b->pix[r * b->w + c] < 128) continue;
            if (b->pix[r * b->w + c] < 250) return false;   /* edges lie on the pixel grid: full or empty */
            inkB++;
            int x = b->left + c, y = b->top - r - 1;              /* the pixel's bottom-left, y up */
            int gc = x - t->x, gr = h - 1 - (y - t->y);
            if (gc < 0 || gc >= w || gr < 0 || gr >= h || t->rows[gr * (w + 1) + gc] != '#') return false;
        }
    return inkB == inkG;
}

static void check_glyph(Outline *o, int gid, const TG *t, const char *what, const char *label)
{
    char msg[200], err[256];
    OutlineBitmap b;
    snprintf(msg, sizeof msg, "%s: %s advance", what, label);
    CHECK(outline_advance(o, gid) == t->adv * 64, msg);
    snprintf(msg, sizeof msg, "%s: %s pixels", what, label);
    int rc = outline_rasterize_glyph(o, gid, 16, 0, 0, &b, err, sizeof err);
    CHECK(rc == OUTLINE_OK && same_pixels(&b, t), msg);
    if (rc == OUTLINE_OK) outline_bitmap_free(&b);
}

static unsigned char *write_font(FwFormat fmt, const PxFont *f, const FwMeta *m, size_t *len, char *warn, size_t wc)
{
    FwFace face = {f, *m};
    char err[PX_ERR_LEN] = "";
    warn[0] = 0;
    unsigned char *d = fw_bytes(fmt, &face, 1, "fw_sfnt", len, warn, wc, err);
    if (!d) printf("      error: %s\n", err);
    return d;
}

static void save(const char *name, const unsigned char *d, size_t n)
{
    char path[256];
    snprintf(path, sizeof path, TMP "%s", name);
    if (!px_write_file(path, d, n)) printf("      cannot write %s\n", path);
}

static unsigned long rd32(const unsigned char *p) { return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3]; }
static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

/* the SVG document of a glyph id in an sfnt's SVG table (malloc'd, NUL-terminated), NULL when it has none */
static char *svg_doc(const unsigned char *d, size_t n, int gid, int *entries)
{
    unsigned nt = rd16(d + 4);
    for (unsigned i = 0; i < nt; i++) {
        const unsigned char *r = d + 12 + 16 * i;
        if (memcmp(r, "SVG ", 4)) continue;
        const unsigned char *t = d + rd32(r + 8), *idx = t + rd32(t + 2);
        unsigned ne = rd16(idx);
        if (entries) *entries = (int)ne;
        for (unsigned e = 0; e < ne; e++) {
            const unsigned char *x = idx + 2 + 12 * e;
            if ((int)rd16(x) > gid || (int)rd16(x + 2) < gid) continue;
            unsigned long off = rd32(x + 4), len = rd32(x + 8);
            if ((size_t)(idx - d) + off + len > n) return NULL;
            char *s = malloc(len + 1);
            memcpy(s, idx + off, len);
            s[len] = 0;
            return s;
        }
        return NULL;
    }
    if (entries) *entries = -1;
    return NULL;
}

/* A colour font: OpenType gets an SVG table with a path per colour of each glyph; the CFF outlines keep all the ink; the other
 * formats refuse it. */
static void test_colour(void)
{
    PxFont f;
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 4;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 1;
    add(&f, px_code_cp(' '), 2, 0, 0, NULL);
    add(&f, px_code_cp('A'), 4, 0, 0, "###/#.#/###");
    add(&f, px_code_cp('B'), 4, 0, 0, "###/#.#/###");
    add(&f, px_code_cp('C'), 3, 0, 0, "##/##");
    {   /* A red; B a red ring with a half-transparent blue centre line; C stays black (the default palette) */
        uint32_t red = 0xFF0000FFu, two[2] = {0xFF0000FFu, 0x0000FF80u};
        PxGlyph *a = &f.glyphs[px_font_find_cp(&f, 'A')], *b = &f.glyphs[px_font_find_cp(&f, 'B')];
        px_glyph_set_palette(&f, a, &red, 1);
        memcpy(b->pix, "000111000", 9);
        px_glyph_touch(b);
        px_glyph_set_palette(&f, b, two, 2);
    }
    CHECK(px_font_is_colour(&f), "colour: the font is a colour font");
    FwMeta m;
    memset(&m, 0, sizeof m);
    m.family = "Colour Test";
    char warn[1024], err[PX_ERR_LEN] = "";
    size_t len = 0;
    unsigned char *d = write_font(FW_OTF, &f, &m, &len, warn, sizeof warn);
    CHECK(d != NULL, "colour: OpenType written");
    if (d) {
        save("fw_sfnt_colour.otf", d, len);
        int entries = 0;
        char *sa = svg_doc(d, len, 2, &entries), *sb = svg_doc(d, len, 3, NULL), *sc = svg_doc(d, len, 4, NULL), *ssp = svg_doc(d, len, 1, NULL);
        CHECK(entries == 3, "colour: an SVG document for each of the three glyphs with ink");
        CHECK(ssp == NULL, "colour: none for the space");
        CHECK(sa && strstr(sa, "id=\"glyph2\"") && strstr(sa, "fill=\"#FF0000\"") && !strstr(sa, "fill-opacity"), "colour: A is one red path");
        CHECK(sa && strstr(sa, "d=\"M0 0L0 -192L192 -192L192 0ZM64 -64L128 -64L128 -128L64 -128Z\""), "colour: A's path, y flipped, with its hole");
        CHECK(sb && strstr(sb, "fill=\"#FF0000\"") && strstr(sb, "fill=\"#0000FF\" fill-opacity=\"0.502\""), "colour: B has a red and a half-transparent blue path");
        CHECK(sc && strstr(sc, "fill=\"#000000\""), "colour: C, of the default palette, is black");
        free(sa); free(sb); free(sc); free(ssp);
        /* the CFF outlines: every colour is ink */
        char oerr[256];
        Outline *o = outline_open_mem(d, len, "otf", oerr, sizeof oerr);
        CHECK(o != NULL, "colour: outline.c reads the OpenType");
        if (o) {
            static const TG ring = {'B', 4, 0, 0, "###/###/###"};
            check_glyph(o, outline_glyph_index(o, 'B'), &ring, "colour", "B (all ink)");
            outline_close(o);
        }
        free(d);
    }
    FwFace face = {&f, m};
    CHECK(fw_bytes(FW_TTF, &face, 1, "x", &len, warn, sizeof warn, err) == NULL && strstr(err, "one colour"), "colour: TrueType refuses it");
    CHECK(fw_bytes(FW_WOFF, &face, 1, "x", &len, warn, sizeof warn, err) == NULL, "colour: WOFF refuses it");
    /* a font of one colour gets no SVG table */
    PxFont mono;
    build(&mono, false, true);
    d = write_font(FW_OTF, &mono, &m, &len, warn, sizeof warn);
    int none = 0;
    if (d) svg_doc(d, len, 1, &none);
    CHECK(d && none == -1, "colour: a font of one colour has no SVG table");
    free(d);
    px_font_free(&mono);
    px_font_free(&f);
}

static void test_format(FwFormat fmt, const char *ext)
{
    PxFont f;
    build(&f, false, true);
    FwMeta m = {0};
    m.family = "Test Pixel"; m.style = "Bold"; m.version = "Version 2.500";
    m.copyright = "(c) Ádám 2026"; m.license = "Free \xF0\x9F\x98\x80 licence"; m.designer = "Somebody";
    char warn[1024], what[64], msg[600], err[256], name[256];
    size_t n = 0, n2 = 0;
    snprintf(what, sizeof what, "%s", ext);
    unsigned char *d = write_font(fmt, &f, &m, &n, warn, sizeof warn);
    CHECK(d != NULL, "writes");
    if (!d) { px_font_free(&f); return; }
    snprintf(msg, sizeof msg, "%s: the warnings tell of the kerning pair left out", what);
    CHECK(strstr(warn, "1 kerning pair(s)") && !strchr(strchr(warn, '\n') + 1, '\n'), msg);
    unsigned char *d2 = write_font(fmt, &f, &m, &n2, warn, sizeof warn);
    snprintf(msg, sizeof msg, "%s: the same bytes twice", what);
    CHECK(d2 && n2 == n && !memcmp(d, d2, n), msg);
    free(d2);
    char file[64];
    snprintf(file, sizeof file, "fw_sfnt.%s", ext);
    save(file, d, n);

    Outline *o = outline_open_mem(d, n, ext, err, sizeof err);
    snprintf(msg, sizeof msg, "%s: outline.c opens it (%s)", what, o ? "ok" : err);
    CHECK(o != NULL, msg);
    if (!o) { free(d); px_font_free(&f); return; }
    snprintf(msg, sizeof msg, "%s: format %s", what, outline_format(o));
    CHECK(!strcmp(outline_format(o), fmt == FW_OTF ? "otf" : fmt == FW_WOFF ? "woff" : "ttf"), msg);
    snprintf(msg, sizeof msg, "%s: family \"%s\", style \"%s\"", what, outline_family(o), outline_style(o));
    CHECK(!strcmp(outline_family(o), "Test Pixel") && !strcmp(outline_style(o), "Bold"), msg);
    outline_name(o, 0, name, sizeof name);
    snprintf(msg, sizeof msg, "%s: copyright \"%s\"", what, name);
    CHECK(!strcmp(name, m.copyright), msg);
    outline_name(o, 13, name, sizeof name);
    snprintf(msg, sizeof msg, "%s: licence with a surrogate pair \"%s\"", what, name);
    CHECK(!strcmp(name, m.license), msg);
    outline_name(o, 6, name, sizeof name);
    snprintf(msg, sizeof msg, "%s: PostScript name \"%s\"", what, name);
    CHECK(!strcmp(name, "TestPixel-Bold"), msg);
    outline_name(o, 4, name, sizeof name);
    snprintf(msg, sizeof msg, "%s: full name \"%s\"", what, name);
    CHECK(!strcmp(name, "Test Pixel Bold"), msg);
    outline_name(o, 5, name, sizeof name);
    CHECK(!strcmp(name, "Version 2.500"), "version");
    int asc, desc, gap;
    outline_metrics(o, &asc, &desc, &gap);
    snprintf(msg, sizeof msg, "%s: units per em %d, ascender %d, descender %d, line gap %d", what, outline_units_per_em(o), asc, desc, gap);
    CHECK(outline_units_per_em(o) == 1024 && asc == 384 && desc == -128 && gap == 64, msg);
    snprintf(msg, sizeof msg, "%s: %d glyphs", what, outline_glyph_count(o));
    CHECK(outline_glyph_count(o) == NG + 2, msg);
    CHECK(!outline_symbolic(o), "not symbolic");
    for (int i = 0; i < NG; i++) {
        char label[32];
        snprintf(label, sizeof label, "U+%04X", (unsigned)kGlyphs[i].code);
        int gid = outline_glyph_index(o, (int)kGlyphs[i].code);
        snprintf(msg, sizeof msg, "%s: %s is glyph %d", what, label, gid);
        CHECK(gid == i + 1, msg);
        check_glyph(o, gid, &kGlyphs[i], what, label);
    }
    check_glyph(o, NG + 1, &kSymbol, what, "the symbol");
    check_glyph(o, 0, &kDefault, what, "the default glyph (.notdef)");
    snprintf(msg, sizeof msg, "%s: a code point not in the font gives .notdef", what);
    CHECK(outline_glyph_index(o, 0x42) == 0, msg);
    outline_close(o);
    free(d);
    px_font_free(&f);
}

static void test_symbolic(void)
{
    PxFont f;
    build(&f, true, false);
    add(&f, px_code_cp(0xF041), 2, 0, 0, "#");   /* a glyph of its own at F041: it keeps that code in (3,0) */
    FwMeta m = {0};
    char warn[1024], err[256], msg[200];
    size_t n = 0;
    unsigned char *d = write_font(FW_TTF, &f, &m, &n, warn, sizeof warn);
    CHECK(d != NULL, "symbolic: writes");
    if (!d) { px_font_free(&f); return; }
    save("fw_sfnt_sym.ttf", d, n);
    Outline *o = outline_open_mem(d, n, "ttf", err, sizeof err);
    CHECK(o != NULL, "symbolic: opens");
    if (o) {
        CHECK(outline_symbolic(o), "symbolic: a (3,0) subtable");
        snprintf(msg, sizeof msg, "symbolic: family is the stem \"%s\", style \"%s\"", outline_family(o), outline_style(o));
        CHECK(!strcmp(outline_family(o), "fw_sfnt") && !strcmp(outline_style(o), "Regular"), msg);
        CHECK(outline_glyph_count(o) == NG + 3, "symbolic: an empty .notdef inserted");
        CHECK(outline_advance(o, 0) == 0, "symbolic: .notdef has advance 0");
        int gid = outline_glyph_index(o, 0x41);
        CHECK(gid == 3, "symbolic: U+0041 reads back");
        CHECK(outline_glyph_index(o, 0x1F600) == 8, "symbolic: U+1F600 reads back");
        CHECK(outline_glyph_index(o, 0xF041) == 7, "symbolic: U+F041 reads back");
        if (gid > 0) check_glyph(o, gid, &kGlyphs[2], "symbolic", "U+0041");
        outline_close(o);
    }
    /* the (3,0) subtable itself: F000 + code below 100, F041 the glyph of its own */
    {
        const unsigned char *cm = NULL;
        unsigned nt = (unsigned)(d[4] << 8 | d[5]);
        for (unsigned i = 0; i < nt; i++) {
            const unsigned char *r = d + 12 + 16 * i;
            if (!memcmp(r, "cmap", 4)) cm = d + ((size_t)r[8] << 24 | (size_t)r[9] << 16 | (size_t)r[10] << 8 | r[11]);
        }
        int found = 0, own = 0, plain = 0;
        if (cm) {
            unsigned ns = (unsigned)(cm[2] << 8 | cm[3]);
            for (unsigned i = 0; i < ns; i++) {
                const unsigned char *r = cm + 4 + 8 * i;
                if (r[1] != 3 || r[3] != 0) continue;
                const unsigned char *s = cm + ((size_t)r[4] << 24 | (size_t)r[5] << 16 | (size_t)r[6] << 8 | r[7]);
                unsigned segs = (unsigned)(s[6] << 8 | s[7]) / 2;
                for (unsigned k = 0; k < segs; k++) {
                    unsigned end = (unsigned)(s[14 + 2 * k] << 8 | s[15 + 2 * k]);
                    unsigned start = (unsigned)(s[16 + 2 * segs + 2 * k] << 8 | s[17 + 2 * segs + 2 * k]);
                    unsigned delta = (unsigned)(s[16 + 4 * segs + 2 * k] << 8 | s[17 + 4 * segs + 2 * k]);
                    for (unsigned c = start; c <= end && c < 0xFFFF; c++) {
                        unsigned g = (c + delta) & 0xFFFF;
                        if (c == 0xF020 && g == 1) found++;
                        if (c == 0xF03D && g == 2) found++;
                        if (c == 0xF041 && g == 7) own++;
                        if (c == 0x2591 && g == 6) found++;
                        if (c < 0x100) plain++;
                    }
                }
            }
        }
        snprintf(msg, sizeof msg, "symbolic: (3,0) maps F020, F03D, 2591 (%d of 3), F041 to its own glyph (%d), nothing below 100 (%d)", found, own, plain);
        CHECK(found == 3 && own == 1 && plain == 0, msg);
    }
    free(d);
    px_font_free(&f);
}

static void test_errors(void)
{
    PxFont f;
    px_font_init(&f);
    PxGlyph g;
    memset(&g, 0, sizeof g);
    g.code = px_code_cp(0x41); g.advance = 3; g.lazy = true; g.hashValid = true;
    strcpy(g.hash, "0123456789abcdef0123456789abcdef");
    px_font_add_glyph(&f, &g);
    FwFace face = {&f, {0}};
    char err[PX_ERR_LEN] = "", warn[256] = "", msg[700];
    size_t n;
    unsigned char *d = fw_bytes(FW_TTF, &face, 1, "x", &n, warn, sizeof warn, err);
    CHECK(!d && strstr(err, "not loaded"), "a lazy glyph: refused");
    f.has[PXM_ASCENT] = f.has[PXM_DESCENT] = true;
    d = fw_bytes(FW_OTF, &face, 1, "x", &n, warn, sizeof warn, err);
    CHECK(!d && strstr(err, "not loaded"), "a lazy glyph with the box stated: refused");
    free(d);
    px_font_free(&f);
    /* an empty font is still a font */
    px_font_init(&f);
    for (int k = 0; k < 3; k++) {
        FwFormat fm = k == 0 ? FW_TTF : k == 1 ? FW_OTF : FW_WOFF;
        d = fw_bytes(fm, &face, 1, "empty", &n, warn, sizeof warn, err);
        Outline *o = d ? outline_open_mem(d, n, NULL, err, sizeof err) : NULL;

        snprintf(msg, sizeof msg, "an empty font as %s: written and read (%s)", fw_label(fm), o ? "ok" : err);
        CHECK(o && outline_glyph_count(o) == 1, msg);
        if (o) outline_close(o);
        free(d);
    }
    px_font_free(&f);
}

/* random glyphs (corner contacts, holes, offsets): every one comes back pixel for pixel, each contour a simple polygon */
static void test_random(void)
{
    enum { N = 300 };
    static char rows[N][100];
    static TG tg[N];
    unsigned seed = 12345;
#define RND(k) ((seed = seed * 1103515245u + 12345u) >> 16) % (unsigned)(k)
    PxFont f;
    px_font_init(&f);
    for (int i = 0; i < N; i++) {
        int w = 1 + (int)RND(9), h = 1 + (int)RND(9), o = 0;
        for (int r = 0; r < h; r++) {
            if (r) rows[i][o++] = '/';
            for (int c = 0; c < w; c++) rows[i][o++] = RND(2) ? '#' : '.';
        }
        rows[i][o] = 0;
        tg[i] = (TG){0x100 + i, (int)RND(12), (int)RND(7) - 3, (int)RND(7) - 3, rows[i]};
        if (!strchr(rows[i], '#')) tg[i].rows = NULL, tg[i].x = tg[i].y = 0;
        add(&f, px_code_cp(0x100 + i), tg[i].adv, tg[i].x, tg[i].y, tg[i].rows);
    }
#undef RND
    for (int k = 0; k < 3; k++) {
        FwFormat fmt = k == 0 ? FW_TTF : k == 1 ? FW_OTF : FW_WOFF;
        FwMeta m = {0};
        char warn[256], err[256], msg[200];
        size_t n = 0;
        unsigned char *d = write_font(fmt, &f, &m, &n, warn, sizeof warn);
        Outline *o = d ? outline_open_mem(d, n, NULL, err, sizeof err) : NULL;
        snprintf(msg, sizeof msg, "random %s: written and opened", fw_ext(fmt));
        CHECK(o != NULL, msg);
        if (o) {
            int bad = 0, notSimple = 0;
            for (int i = 0; i < N; i++) {
                int gid = outline_glyph_index(o, 0x100 + i);
                OutlineBitmap b;
                if (gid != i + 1 || outline_advance(o, gid) != tg[i].adv * 64) { bad++; continue; }
                if (outline_rasterize_glyph(o, gid, 16, 0, 0, &b, err, sizeof err) != OUTLINE_OK) { bad++; continue; }
                if (!same_pixels(&b, &tg[i])) bad++;
                outline_bitmap_free(&b);
                OutlineShape sh;
                if (outline_glyph_shape(o, gid, 1, &sh, err, sizeof err) != OUTLINE_OK) { bad++; continue; }
                for (int c = 0; c < sh.nc; c++)
                    for (int p = sh.pstart[c]; p < sh.pstart[c + 1]; p++)
                        for (int q = p + 1; q < sh.pstart[c + 1]; q++)
                            if (sh.px[p] == sh.px[q] && sh.py[p] == sh.py[q]) notSimple++;
                outline_shape_free(&sh);
            }
            snprintf(msg, sizeof msg, "random %s: %d of %d glyphs differ", fw_ext(fmt), bad, N);
            CHECK(!bad, msg);
            snprintf(msg, sizeof msg, "random %s: %d contour points repeated", fw_ext(fmt), notSimple);
            CHECK(!notSimple, msg);
            outline_close(o);
        }
        if (d) { char file[64]; snprintf(file, sizeof file, "fw_sfnt_random.%s", fw_ext(fmt)); save(file, d, n); }
        free(d);
    }
    px_font_free(&f);
}

int main(void)
{
    px_mkdir_p("tests/tmp");
    test_format(FW_TTF, "ttf");
    test_format(FW_OTF, "otf");
    test_format(FW_WOFF, "woff");
    test_symbolic();
    test_random();
    test_errors();
    test_colour();
    printf("\ntest_fontwrite_sfnt: %d checks, %d failed\n", g_pass + g_fail, g_fail);
    return g_fail ? 1 : 0;
}
