/* test_fontwrite.c - plain C tests for the BDF, PCF, FNT and FON writers of src/fontwrite.c: each file is read back with pxfe's
 * own reader (src/bmsrc.c) and compared pixel by pixel.  The sfnt writers have tests/test_fontwrite_sfnt.c.
 * Run from the pxfe directory (build.bat test does). */
#include "fontwrite.h"
#include "bmsrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

/* rows separated by '/', '#' ink; a glyph at (x, y) = bottom-left corner */
static PxGlyph make_glyph(PxCode code, int adv, int x, int y, const char *rows)
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
    return g;
}

static void add(PxFont *f, PxCode code, int adv, int x, int y, const char *rows)
{
    PxGlyph g = make_glyph(code, adv, x, y, rows);
    px_font_add_glyph(f, &g);
    px_glyph_free(&g);
}

/* The ink of a glyph as "x,y;" pairs from the pen and the baseline (y up), sorted by the drawing order: top row first. */
static void ink_px(const PxGlyph *g, char *out, size_t cap)
{
    size_t o = 0;
    out[0] = 0;
    for (int r = 0; r < g->h; r++)
        for (int c = 0; c < g->w; c++)
            if (g->pix[r * g->w + c] != '.' && o + 16 < cap) o += (size_t)snprintf(out + o, cap - o, "%d,%d;", g->x + c, g->y + g->h - 1 - r);
}
static void ink_bm(const BmGlyph *g, char *out, size_t cap)
{
    size_t o = 0;
    out[0] = 0;
    for (int r = 0; g && r < g->h; r++)
        for (int c = 0; c < g->w; c++)
            if (g->pix[r * g->w + c] && o + 16 < cap) o += (size_t)snprintf(out + o, cap - o, "%d,%d;", g->x + c, g->y + g->h - 1 - r);
}

static bool same_glyph(const PxGlyph *p, const BmGlyph *b, const char *what)
{
    char a[2048], c[2048];
    ink_px(p, a, sizeof a);
    ink_bm(b, c, sizeof c);
    bool ok = b && !strcmp(a, c) && b->advance == p->advance;
    if (!ok) printf("      %s: want adv %d ink %s\n      got  adv %d ink %s\n", what, p->advance, a, b ? b->advance : -1, c);
    return ok;
}

/* the test font: A, a hole (O), two parts (i), a descender (g), a space, an accented letter, a symbol, a variant, a default glyph */
static void make_font(PxFont *f, bool astral)
{
    px_font_init(f);
    f->has[PXM_ASCENT] = true; f->m[PXM_ASCENT] = 6;
    f->has[PXM_DESCENT] = true; f->m[PXM_DESCENT] = 2;
    f->has[PXM_XHEIGHT] = true; f->m[PXM_XHEIGHT] = 4;
    add(f, px_code_cp(' '), 3, 0, 0, NULL);
    add(f, px_code_cp('?'), 5, 0, 0, "###./...#/.##./..../.#..");
    add(f, px_code_cp('A'), 6, 0, 0, ".###./#...#/#...#/#####/#...#/#...#");
    add(f, px_code_cp('O'), 6, 0, 0, ".###./#...#/#...#/#...#/#...#/.###.");
    add(f, px_code_cp('g'), 5, 0, -2, ".###/#..#/#..#/.###/...#/###.");
    add(f, px_code_cp('i'), 2, 0, 0, "#/./#/#/#");
    add(f, px_code_cp(0xE9), 5, 0, 0, ".#../.##./#..#/###./#.../.###");   /* e acute: on cp1252 */
    add(f, px_code_cp(0x0151), 5, 0, 0, ".#.#/#.#./.##./#..#/#..#/.##.");   /* o double acute: cp1250, not cp1252 */
    add(f, px_code_with_variant(px_code_cp('A'), 1), 6, 0, 0, "######");
    add(f, px_code_symbol(0), 4, 1, 1, "##/##");
    add(f, PX_CODE_DEFAULT, 4, 0, 0, "###/#.#/###");
    if (astral) add(f, px_code_cp(0x1F600), 7, 0, 0, "#####/#.#.#/#####");
    px_font_sort(f);
}

static FwFace face_of(const PxFont *f, const char *style)
{
    FwFace fc;
    memset(&fc, 0, sizeof fc);
    fc.font = f;
    fc.meta.family = "Test Sans";
    fc.meta.style = style;
    fc.meta.fullName = "Test Sans Bold";
    fc.meta.copyright = "(c) 2026 Test \xC3\x89va";   /* "Éva": Latin-1 */
    fc.meta.version = "1.2";
    return fc;
}

static BmSource *write_read(FwFormat fmt, const FwFace *faces, int n, char *warn, size_t wc, const char *what)
{
    char err[PX_ERR_LEN] = "", berr[BM_ERR_LEN] = "", name[200];
    size_t len = 0;
    warn[0] = 0;
    unsigned char *data = fw_bytes(fmt, faces, n, "testsans", &len, warn, wc, err);
    snprintf(name, sizeof name, "%s: written", what);
    CHECK(data && len > 0, name);
    if (!data) { printf("      %s\n", err); return NULL; }
    BmSource *s = bm_open_mem(data, len, berr, sizeof berr);
    snprintf(name, sizeof name, "%s: read back by bmsrc", what);
    CHECK(s != NULL, name);
    if (!s) printf("      %s\n", berr);
    /* deterministic: the same bytes again */
    size_t len2 = 0;
    char w2[1024] = "";
    unsigned char *again = fw_bytes(fmt, faces, n, "testsans", &len2, w2, sizeof w2, err);
    snprintf(name, sizeof name, "%s: the same bytes twice", what);
    CHECK(again && len2 == len && !memcmp(again, data, len), name);
    free(again);
    {   /* a copy for a look with other readers */
        char path[300], *p;
        snprintf(path, sizeof path, "tests/tmp/fw_%s.%s", what, fw_ext(fmt));
        for (p = path + 10; *p; p++) if (*p == ' ') *p = '_';
        px_write_file(path, data, len);
    }
    free(data);
    return s;
}

static void test_bdf_pcf(FwFormat fmt)
{
    const char *fn = fw_label(fmt);
    char name[200], warn[2048];
    PxFont f;
    make_font(&f, true);
    FwFace fc = face_of(&f, "Bold");
    BmSource *s = write_read(fmt, &fc, 1, warn, sizeof warn, fn);
    if (!s) { px_font_free(&f); return; }
    BmStrikeInfo in;
    bm_strike_info(s, 0, &in);
    snprintf(name, sizeof name, "%s: ascent 6, descent 2", fn);
    CHECK(in.ascent == 6 && in.descent == 2, name);
    snprintf(name, sizeof name, "%s: family and style", fn);
    CHECK(!strcmp(in.family, "Test Sans") && !strcmp(in.style, "Bold"), name);
    if (strcmp(in.family, "Test Sans") || strcmp(in.style, "Bold")) printf("      got '%s' '%s'\n", in.family, in.style);
    /* every coded glyph of variant 0, the symbol, no default glyph, no variant */
    int want = 0;
    bool all = true;
    for (int i = 0; i < f.count; i++) {
        const PxGlyph *g = &f.glyphs[i];
        int32_t cp = px_code_codepoint(g->code);
        if (px_code_variant(g->code) || px_code_is_default(g->code)) continue;
        if (fmt == FW_PCF && cp > 0xFFFF) continue;
        want++;
        if (cp < 0) continue;
        char w[40];
        snprintf(w, sizeof w, "U+%04X", (unsigned)cp);
        all &= same_glyph(g, bm_find(s, 0, cp), w);
    }
    snprintf(name, sizeof name, "%s: every glyph, pixel by pixel", fn);
    CHECK(all, name);
    snprintf(name, sizeof name, "%s: %d glyphs (no variant, no default glyph)", fn, want);
    CHECK(bm_glyph_count(s, 0) == want, name);
    if (bm_glyph_count(s, 0) != want) printf("      got %d\n", bm_glyph_count(s, 0));
    {   /* the symbol: no code point */
        bool found = false;
        for (int i = 0; i < bm_glyph_count(s, 0); i++) {
            const BmGlyph *g = bm_glyph_at(s, 0, i);
            if (g->codepoint < 0) found = same_glyph(&f.glyphs[px_font_find_code(&f, px_code_symbol(0))], g, "symbol");
        }
        snprintf(name, sizeof name, "%s: the symbol, uncoded", fn);
        CHECK(found, name);
    }
    if (fmt == FW_PCF) {
        snprintf(name, sizeof name, "%s: U+1F600 left out with a warning", fn);
        CHECK(bm_find(s, 0, 0x1F600) == NULL && strstr(warn, "U+1F600"), name);
    }
    bm_close(s);
    /* a symbolic font keeps its codes */
    f.symbolic = true;
    s = write_read(fmt, &fc, 1, warn, sizeof warn, fn);
    if (s) {
        bm_strike_info(s, 0, &in);
        snprintf(name, sizeof name, "%s: symbolic font read as symbolic, codes kept", fn);
        CHECK(in.symbolic && bm_find(s, 0, 'A') != NULL, name);
        bm_close(s);
    }
    px_font_free(&f);
}

static void test_fnt(void)
{
    char warn[2048];
    PxFont f;
    make_font(&f, false);
    add(&f, px_code_cp(0x2603), 3, 0, 0, "###");   /* a snowman: on no code page of FNT */
    FwFace fc = face_of(&f, "Bold Italic");
    BmSource *s = write_read(FW_FNT, &fc, 1, warn, sizeof warn, "FNT");
    if (s) {
        BmStrikeInfo in;
        bm_strike_info(s, 0, &in);
        CHECK(in.ascent == 6 && in.descent == 2, "FNT: ascent 6, descent 2");
        CHECK(!strcmp(in.family, "Test Sans"), "FNT: face name");
        CHECK(!strcmp(in.style, "Bold Italic"), "FNT: weight 700 and italic");
        if (strcmp(in.style, "Bold Italic")) printf("      got '%s'\n", in.style);
        /* cp1250 covers the most (e acute and o double acute) */
        bool all = true;
        const int cps[] = {' ', '?', 'A', 'O', 'g', 'i', 0xE9, 0x0151};
        for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++) {
            char w[40];
            snprintf(w, sizeof w, "U+%04X", (unsigned)cps[i]);
            all &= same_glyph(&f.glyphs[px_font_find_cp(&f, cps[i])], bm_find(s, 0, cps[i]), w);
        }
        CHECK(all, "FNT: every glyph of cp1250 (the code page covering most), pixel by pixel");
        CHECK(bm_find(s, 0, 0x2603) == NULL && strstr(warn, "not on code page cp1250"), "FNT: U+2603 left out with a warning");
        CHECK(bm_glyph_count(s, 0) == 8, "FNT: 8 glyphs");
        bm_close(s);
    }
    /* the CODEPAGE text chooses cp1252 */
    fc.meta.codepage = "1252";
    s = write_read(FW_FNT, &fc, 1, warn, sizeof warn, "FNT cp1252");
    if (s) {
        CHECK(bm_find(s, 0, 0xE9) != NULL && bm_find(s, 0, 0x0151) == NULL && strstr(warn, "2 glyph(s) left out, not on code page cp1252"),
              "FNT: CODEPAGE 1252 keeps U+00E9 and leaves out U+0151");
        bm_close(s);
    }
    fc.meta.codepage = NULL;
    /* a glyph outside its cell is left out */
    add(&f, px_code_cp('j'), 2, -1, -2, ".#/../.#/.#/.#/##");
    s = write_read(FW_FNT, &fc, 1, warn, sizeof warn, "FNT unfit");
    if (s) {
        CHECK(bm_find(s, 0, 'j') == NULL && strstr(warn, "not fitting"), "FNT: a glyph left of its pen left out with a warning");
        bm_close(s);
    }
    px_font_free(&f);
}

static void test_fon(void)
{
    char name[200], warn[2048];
    PxFont a, b;
    make_font(&a, false);
    make_font(&b, false);
    for (int i = 0; i < b.count; i++) b.glyphs[i].advance += 1;   /* the second face is wider */
    FwFace fc[2] = {face_of(&a, "Regular"), face_of(&b, "Bold")};
    BmSource *s = write_read(FW_FON, fc, 2, warn, sizeof warn, "FON");
    if (!s) { px_font_free(&a); px_font_free(&b); return; }
    CHECK(bm_strike_count(s) == 2, "FON: two fonts in one file");
    for (int k = 0; k < 2 && k < bm_strike_count(s); k++) {
        BmStrikeInfo in;
        bm_strike_info(s, k, &in);
        snprintf(name, sizeof name, "FON: font %d is %s", k + 1, k ? "Bold" : "Regular");
        CHECK(!strcmp(in.style, k ? "Bold" : "Regular"), name);
        const PxFont *f = k ? &b : &a;
        bool all = true;
        const int cps[] = {'A', 'O', 'g', 'i'};
        for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++) all &= same_glyph(&f->glyphs[px_font_find_cp(f, cps[i])], bm_find(s, k, cps[i]), "glyph");
        snprintf(name, sizeof name, "FON: font %d, glyphs pixel by pixel", k + 1);
        CHECK(all, name);
    }
    bm_close(s);
    px_font_free(&a);
    px_font_free(&b);
}

static void test_refusals(void)
{
    char err[PX_ERR_LEN], warn[256] = "";
    size_t len;
    PxFont f;
    make_font(&f, false);
    FwFace fc[2] = {face_of(&f, "Regular"), face_of(&f, "Bold")};
    CHECK(fw_bytes(FW_BDF, fc, 2, "x", &len, warn, sizeof warn, err) == NULL && err[0], "two faces in a BDF: refused");
    uint32_t red = 0xFF0000FF, blue = 0x0000FFFF;
    uint32_t two[2] = {red, blue};
    PxGlyph *g = &f.glyphs[px_font_find_cp(&f, 'A')];
    g->pix[0] = '1';
    px_glyph_touch(g);
    px_glyph_set_palette(&f, g, two, 2);
    CHECK(px_font_is_colour(&f), "colour font made");
    CHECK(fw_bytes(FW_BDF, fc, 1, "x", &len, warn, sizeof warn, err) == NULL && strstr(err, "one colour"), "a colour font: refused");
    px_font_free(&f);
    FwFormat ff;
    CHECK(fw_format_of_ext(".WOFF", &ff) && ff == FW_WOFF && fw_format_of_ext("bdf", &ff) && ff == FW_BDF && !fw_format_of_ext("pxtp", &ff),
          "formats by extension");
    char nm[64];
    fw_ascii_name("Test Sans \xC3\x89va-2!", nm, sizeof nm, false);
    CHECK(!strcmp(nm, "TestSansva-2"), "an ASCII name");
}

int main(void)
{
    printf("test_fontwrite\n");
    test_bdf_pcf(FW_BDF);
    test_bdf_pcf(FW_PCF);
    test_fnt();
    test_fon();
    test_refusals();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
