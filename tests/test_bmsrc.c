/* test_bmsrc.c - plain C tests for src/bmsrc.c (bitmap font sources).  Run from the pxfe directory (build.bat test does).
 * The sample files of testdata/bitmap/ are made by tests/gen_bitmap_fonts.py. */
#include "bmsrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

#define DIR "testdata/bitmap/"
#define TMP "tests/tmp/"

static const char *A = ".###./#...#/#...#/#####/#...#/#...#/#...#";
static const char *B = "####./#...#/#...#/####./#...#/#...#/####.";

/* a glyph as rows joined by '/': '#' ink, '.' none for depth 1; a digit 0..9 of the coverage for depth 8 */
static char *rows_of(const BmGlyph *g, char *buf, size_t cap) {
    size_t o = 0;
    buf[0] = 0;
    if (!g) { snprintf(buf, cap, "(null)"); return buf; }
    for (int y = 0; y < g->h && o + g->w + 2 < cap; y++) {
        if (y) buf[o++] = '/';
        for (int x = 0; x < g->w; x++) {
            unsigned v = g->pix[y * g->w + x];
            buf[o++] = g->depth == 1 ? (v ? '#' : '.') : (char)('0' + (v * 9 + 127) / 255);
        }
    }
    buf[o] = 0;
    return buf;
}

static int is_glyph(const BmGlyph *g, int x, int y, int adv, const char *rows) {
    char buf[1024];
    if (!g) return 0;
    rows_of(g, buf, sizeof buf);
    return g->x == x && g->y == y && g->advance == adv && !strcmp(buf, rows);
}

static void report_glyph(const BmGlyph *g) {
    char buf[1024];
    if (!g) { printf("      glyph is NULL\n"); return; }
    printf("      got x=%d y=%d adv=%d w=%d h=%d rows=%s\n", g->x, g->y, g->advance, g->w, g->h, rows_of(g, buf, sizeof buf));
}

#define CHECK_GLYPH(g, x, y, adv, rows, name) do { \
    const BmGlyph *g__ = (g); \
    int ok__ = is_glyph(g__, x, y, adv, rows); \
    CHECK(ok__, name); if (!ok__) report_glyph(g__); } while (0)

static BmSource *open_ok(const char *file, const char *what) {
    char path[256], err[BM_ERR_LEN];
    snprintf(path, sizeof path, DIR "%s", file);
    BmSource *s = bm_open(path, err, sizeof err);
    char name[300];
    snprintf(name, sizeof name, "%s: opens", what);
    CHECK(s != NULL, name);
    if (!s) printf("      error: %s\n", err);
    return s;
}

static int info_is(const BmSource *s, int i, const char *label, int px, int asc, int desc, int nglyphs, const char *family, const char *style) {
    BmStrikeInfo in;
    if (!bm_strike_info(s, i, &in)) return 0;
    int ok = !strcmp(in.label, label) && in.pixel_size == px && in.ascent == asc && in.descent == desc && in.glyph_count == nglyphs &&
             bm_glyph_count(s, i) == nglyphs && (!family || !strcmp(in.family, family)) && (!style || !strcmp(in.style, style));
    if (!ok) printf("      info: label=%s px=%d asc=%d desc=%d n=%d family=%s style=%s depth=%d\n", in.label, in.pixel_size, in.ascent,
                    in.descent, in.glyph_count, in.family, in.style, in.depth);
    return ok;
}

/* ------------------------------------------------------------------------------------------------ BDF / PCF */
static void test_bdf(void) {
    BmSource *s = open_ok("font.bdf", "bdf");
    if (!s) return;
    CHECK(bm_strike_count(s) == 1, "bdf: one strike");
    {
        BmStrikeInfo si;
        CHECK(bm_strike_info(s, 0, &si) && !si.symbolic, "bdf: microsoft-437 is not symbolic");
        BmSource *y = open_ok("symbol.bdf", "bdf (symbol)");
        CHECK(y && bm_strike_info(y, 0, &si) && si.symbolic, "bdf: Adobe-FontSpecific is symbolic (SYMBOLIC)");
        bm_close(y);
    }
    CHECK(info_is(s, 0, "9px", 9, 7, 2, 6, "Tiny", "Medium"), "bdf: strike info (ascent 7, descent 2, 6 glyphs)");
    CHECK_GLYPH(bm_find(s, 0, 'A'), 0, 0, 6, A, "bdf: A");
    CHECK_GLYPH(bm_find(s, 0, 'B'), 0, 0, 6, B, "bdf: B");
    CHECK_GLYPH(bm_find(s, 0, 0x27), 1, 4, 4, "##/##/#.", "bdf: apostrophe sits 4 rows above the baseline");
    CHECK_GLYPH(bm_find(s, 0, 'p'), 0, -2, 5, "###./#..#/#..#/###./#...", "bdf: p has a descender (y = -2)");
    CHECK_GLYPH(bm_find(s, 0, 0xE9), 1, 0, 5, "#.#/.#./#.#", "bdf: ENCODING 130 of cp437 is U+00E9");
    CHECK(bm_find(s, 0, 130) == NULL, "bdf: the code 130 itself is not a code point");
    CHECK(bm_find(s, 0, 'Z') == NULL, "bdf: missing glyph is NULL");
    CHECK(bm_find(s, 0, -1) == NULL && bm_find(s, 5, 'A') == NULL && bm_find(NULL, 0, 'A') == NULL, "bdf: bad arguments give NULL");
    const BmGlyph *u = bm_glyph_at(s, 0, 5);
    CHECK(u && u->codepoint == -1 && u->code == -1 && u->advance == 1 && u->w == 1 && u->h == 1 && u->name && !strcmp(u->name, "unencoded"),
          "bdf: glyph without encoding (no DWIDTH: advance = width), reachable by index");
    CHECK(bm_glyph_at(s, 0, 6) == NULL && bm_glyph_at(s, 0, -1) == NULL, "bdf: index out of range");
    const BmGlyph *a = bm_find(s, 0, 'A');
    CHECK(a && a->code == 65 && a->codepoint == 'A' && a->depth == 1 && a->name && !strcmp(a->name, "A"), "bdf: code, depth, name");
    BmPlain pl;
    CHECK(a && bm_glyph_to_plain(a, &pl) && pl.w == 5 && pl.h == 7 && pl.x == 0 && pl.y == 0 && pl.advance == 6 && pl.codepoint == 'A' &&
          !strncmp(pl.pix, ".ZZZ.", 5) && !strncmp(pl.pix + 15, "ZZZZZ", 5), "bdf: plain structure for the UI");
    bm_plain_free(&pl);
    CHECK(pl.pix == NULL, "bdf: plain freed");
    CHECK(!bm_glyph_to_plain(NULL, &pl), "bdf: plain of NULL fails");
    bm_close(s);
    bm_close(NULL);
}

static void test_pcf_one(const char *file, const char *what, int unicode_smiley, int have_names, const char *style) {
    char name[160];
    BmSource *s = open_ok(file, what);
    if (!s) return;
    snprintf(name, sizeof name, "%s: strike info", what);
    CHECK(bm_strike_count(s) == 1 && info_is(s, 0, "9px", 9, 7, 2, 6, "Tiny", style), name);
    snprintf(name, sizeof name, "%s: A", what);
    CHECK_GLYPH(bm_find(s, 0, 'A'), 0, 0, 6, A, name);
    snprintf(name, sizeof name, "%s: B", what);
    CHECK_GLYPH(bm_find(s, 0, 'B'), 0, 0, 6, B, name);
    snprintf(name, sizeof name, "%s: apostrophe offset", what);
    CHECK_GLYPH(bm_find(s, 0, 0x27), 1, 4, 4, "##/##/#.", name);
    snprintf(name, sizeof name, "%s: p descender", what);
    CHECK_GLYPH(bm_find(s, 0, 'p'), 0, -2, 5, "###./#..#/#..#/###./#...", name);
    snprintf(name, sizeof name, "%s: %s", what, unicode_smiley ? "U+263A" : "code 0xE9 of ISO8859-1");
    CHECK_GLYPH(bm_find(s, 0, unicode_smiley ? 0x263A : 0xE9), 1, 0, 5, "#.#/.#./#.#", name);
    snprintf(name, sizeof name, "%s: missing glyph", what);
    CHECK(bm_find(s, 0, unicode_smiley ? 0xE9 : 0x263A) == NULL && bm_find(s, 0, 'Z') == NULL, name);
    const BmGlyph *u = bm_glyph_at(s, 0, 5);
    snprintf(name, sizeof name, "%s: unencoded glyph", what);
    CHECK(u && u->codepoint == -1 && u->w == 2 && u->h == 2 && u->advance == 3 && u->x == 0 && u->y == 0 &&
          !strcmp(rows_of(u, (char[64]){0}, 64), "##/#."), name);
    const BmGlyph *a = bm_find(s, 0, 'A');
    snprintf(name, sizeof name, "%s: glyph names", what);
    CHECK(a && (have_names ? (a->name && !strcmp(a->name, "A")) : a->name == NULL) && a->code == 'A', name);
    bm_close(s);
}

static void test_pcf(void) {
    test_pcf_one("font_lsb.pcf", "pcf lsb (uncompressed metrics, LSB bits)", 0, 1, "Bold");
    test_pcf_one("font_msb.pcf", "pcf msb (compressed metrics, MSB bytes and bits, pad 2, ISO10646)", 1, 1, "Bold");
    test_pcf_one("font_mix.pcf", "pcf mixed (MSB bits, LSB bytes, pad 4, scan unit 4, no accelerators)", 0, 0, "Bold");
}

/* ------------------------------------------------------------------------------------------------ FNT / FON */
static const char *FIXED_C = "..##..../.#..#.../#....#../#......./#....#../.#..#.../..##..../..#.....";
static const char *D10 = "#######.#/#.......#/#.......#/#.......#/#.......#/#.......#/#######.#/........./........./.........";

static void test_fnt_strikes(const BmSource *s, const char *what, int expect_two) {
    char name[160];
    snprintf(name, sizeof name, "%s: strike count", what);
    CHECK(bm_strike_count(s) == (expect_two ? 2 : 1), name);
    int p = expect_two ? 1 : 0;
    snprintf(name, sizeof name, "%s: proportional strike info", what);
    CHECK(info_is(s, p, "10px", 10, 8, 2, 3, "Tiny Prop", "Regular"), name);
    char a10[256] = "";
    snprintf(a10, sizeof a10, "%s/...../...../.....", A);
    snprintf(name, sizeof name, "%s: A (width 5, 10 rows, y = ascent - height)", what);
    CHECK_GLYPH(bm_find(s, p, 'A'), 0, -2, 5, a10, name);
    snprintf(name, sizeof name, "%s: D is 9 wide (two byte columns)", what);
    CHECK_GLYPH(bm_find(s, p, 'D'), 0, -2, 9, D10, name);
    snprintf(name, sizeof name, "%s: C has width 0 and is left out", what);
    CHECK(bm_find(s, p, 'C') == NULL && bm_glyph_count(s, p) == 3, name);
    if (expect_two) {
        snprintf(name, sizeof name, "%s: fixed strike info 8x8", what);
        CHECK(info_is(s, 0, "8x8", 8, 6, 2, 3, "Tiny Fixed", "Regular"), name);
        snprintf(name, sizeof name, "%s: cp437 0x80 is U+00C7", what);
        CHECK_GLYPH(bm_find(s, 0, 0xC7), 0, -2, 8, FIXED_C, name);
        const BmGlyph *u = bm_find(s, 0, 0xFC), *e = bm_find(s, 0, 0xE9);
        snprintf(name, sizeof name, "%s: cp437 0x81 is U+00FC, 0x82 is U+00E9", what);
        CHECK(u && e && u->code == 0x81 && e->code == 0x82 && u->w == 8 && e->h == 8, name);
        snprintf(name, sizeof name, "%s: byte 0x80 itself is no code point here", what);
        CHECK(bm_find(s, 0, 0x80) == NULL, name);
    }
}

static void test_fnt(void) {
    BmSource *s = open_ok("font.fnt", "fnt");
    if (s) {
        BmStrikeInfo si;
        CHECK(bm_strike_info(s, 0, &si) && !si.symbolic, "fnt: charset 0 is not symbolic");
        test_fnt_strikes(s, "fnt", 0);
        bm_close(s);
    }
    s = open_ok("fixed_prop_ne.fon", "fon (NE)");
    if (s) { test_fnt_strikes(s, "fon NE", 1); bm_close(s); }
    s = open_ok("fixed_prop_pe.fon", "fon (PE)");
    if (s) { test_fnt_strikes(s, "fon PE", 1); bm_close(s); }
}

/* ------------------------------------------------------------------------------------------------ sheets */
static void test_sheets(void) {
    BmSource *s = open_ok("sheet.json", "sheet image (PNG, grey)");
    if (s) {
        CHECK(bm_strike_count(s) == 1 && info_is(s, 0, "6x8", 8, 7, 1, 8, "Tiny Sheet", "Regular"),
              "sheet: info (baseline measured on A-Z: ascent 7, descent 1)");
        CHECK_GLYPH(bm_find(s, 0, 'A'), 0, -1, 6, ".###../#...#./#...#./#####./#...#./#...#./#...#./......", "sheet: A is the whole cell");
        CHECK_GLYPH(bm_find(s, 0, 'F'), 0, -1, 6, "#####./#...../###.../#...../#...../#...../#...../......", "sheet: F in the second row of the grid");
        CHECK(bm_find(s, 0, 'I') == NULL && bm_find(s, 0, '@') == NULL, "sheet: codes outside the grid are missing");
        const BmGlyph *g = bm_find(s, 0, 'H');
        CHECK(g && g->code == 72 && g->codepoint == 'H', "sheet: code of a cell");
        bm_close(s);
    }
    s = open_ok("sheet_alpha.json", "sheet image (RGBA, light ink, cp437-graphic)");
    if (s) {
        CHECK(bm_strike_count(s) == 1 && info_is(s, 0, "4x5", 5, 4, 1, 4, "", ""), "sheet alpha: info (baseline given 3)");
        CHECK_GLYPH(bm_find(s, 0, 0x263A), 0, -1, 4, ".#../####/####/.#../....", "sheet alpha: code 1 is U+263A");
        CHECK(bm_find(s, 0, 0x263B) && bm_find(s, 0, 0x2665) && bm_find(s, 0, 0x2666), "sheet alpha: codes 2, 3, 4 map to U+263B, U+2665, U+2666");
        CHECK(bm_find(s, 0, 1) == NULL, "sheet alpha: control code 1 is not a code point under -graphic");
        bm_close(s);
    }
    s = open_ok("raw.json", "sheet raw dump (cp852-graphic)");
    if (s) {
        CHECK(bm_strike_count(s) == 1 && info_is(s, 0, "8x4", 4, 3, 1, 4, "", ""), "raw: info (baseline 2)");
        CHECK_GLYPH(bm_find(s, 0, '~'), 0, -1, 8, "......../.##..##./#..##..#/........", "raw: 0x7E");
        CHECK(bm_find(s, 0, 0x2302) != NULL, "raw: 0x7F is U+2302 (graphic)");
        CHECK_GLYPH(bm_find(s, 0, 0xC7), 0, -1, 8, "########/#......#/#......#/########", "raw: 0x80 is U+00C7 in cp852");
        CHECK_GLYPH(bm_find(s, 0, 0xFC), 0, -1, 8, ".......#/......##/#......./##......", "raw: 0x81 is U+00FC in cp852 (first and last pixel column)");
        bm_close(s);
    }
    s = open_ok("raw9.json", "sheet raw dump with ninth column");
    if (s) {
        CHECK(info_is(s, 0, "9x4", 4, 3, 1, 4, "", ""), "raw9: info");
        CHECK_GLYPH(bm_find(s, 0, 0xC7), 0, -1, 9, "#########/#......##/#......##/#########",
                    "raw9: code 128 gets a copy of its eighth column");
        CHECK_GLYPH(bm_find(s, 0, 0xFC), 0, -1, 9, ".......#./......##./#......../##.......", "raw9: code 129 gets an empty ninth column");
        bm_close(s);
    }
    s = open_ok("records.json", "sheet records");
    if (s) {
        CHECK(info_is(s, 0, "x4", 4, 4, 0, 4, "", ""), "records: info");
        const BmGlyph *sp = bm_find(s, 0, ' ');
        CHECK(sp && sp->w == 0 && sp->h == 0 && sp->advance == 3 && sp->pix == NULL, "records: space has no pixels and the space advance");
        CHECK_GLYPH(bm_find(s, 0, '!'), 0, 0, 2, "#/#/#/.", "records: '!' (width 1, advance 1 + gap 1)");
        CHECK_GLYPH(bm_find(s, 0, '"'), 0, 0, 4, ".#./#.#/###/#.#", "records: '\"' (width 3)");
        CHECK_GLYPH(bm_find(s, 0, '#'), 0, 0, 3, "##/#./##/#.", "records: '#' (width 2)");
        bm_close(s);
    }
}

/* ------------------------------------------------------------------------------------------------ sbit */
static void dbl_rows(const char *in, char *out) {
    size_t o = 0;
    const char *p = in;
    while (*p) {
        const char *e = strchr(p, '/');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        for (int rep = 0; rep < 2; rep++) {
            if (o) out[o++] = '/';
            for (size_t i = 0; i < l; i++) { out[o++] = p[i]; out[o++] = p[i]; }
        }
        p += l;
        if (*p == '/') p++;
    }
    out[o] = 0;
}

static void test_ttf(void) {
    BmSource *s = open_ok("strikes.ttf", "ttf sbit");
    if (!s) return;
    CHECK(bm_strike_count(s) == 3, "ttf: three strikes");
    {
        BmStrikeInfo si;
        CHECK(bm_strike_info(s, 0, &si) && !si.symbolic, "ttf: no (3,0) cmap subtable, not symbolic");
    }
    CHECK(info_is(s, 0, "8px", 8, 7, 1, 3, "Tiny Strikes", "Regular"), "ttf: strike 0 info (ppem 8)");
    CHECK(info_is(s, 1, "14px", 14, 12, 2, 2, "Tiny Strikes", "Regular"), "ttf: strike 1 info (ppem 14)");
    BmStrikeInfo in;
    bm_strike_info(s, 0, &in);
    CHECK(!strcmp(in.name, "Tiny Strikes Regular") && in.depth == 1, "ttf: full name from the name table");
    bm_strike_info(s, 2, &in);
    CHECK(in.pixel_size == 10 && in.depth == 8 && in.ascent == 9 && in.descent == 1 && in.glyph_count == 1,
          "ttf: strike 2: 2 bits deep -> depth 8, line metrics from hhea scaled to ppem 10 (9, 1)");
    CHECK_GLYPH(bm_find(s, 0, 'A'), 0, 0, 6, A, "ttf: strike 0 A (image format 1, small metrics)");
    CHECK_GLYPH(bm_find(s, 0, 'B'), 0, 0, 6, B, "ttf: strike 0 B (advance 0 -> hmtx 12 scaled to ppem 8 = 6)");
    {   /* the composite: A at (0,0), B at (6,0) */
        char ex[256] = "";
        const char *a = A, *b = B;
        for (int r = 0; r < 7; r++) {
            if (r) strcat(ex, "/");
            strncat(ex, a + r * 6, 5);
            strcat(ex, ".");
            strncat(ex, b + r * 6, 5);
        }
        CHECK_GLYPH(bm_find(s, 0, 'C'), 0, 0, 12, ex, "ttf: strike 0 C (image format 8: components A and B)");
    }
    char d[512];
    dbl_rows(A, d);
    CHECK_GLYPH(bm_find(s, 1, 'A'), 1, -2, 11, d, "ttf: strike 1 A (index format 2, image format 5 bit aligned, doubled)");
    dbl_rows(B, d);
    CHECK_GLYPH(bm_find(s, 1, 'B'), 1, -2, 11, d, "ttf: strike 1 B");
    CHECK(bm_find(s, 1, 'C') == NULL, "ttf: strike 1 has no C");
    const BmGlyph *g = bm_find(s, 2, 'A');
    CHECK(g && g->w == 3 && g->h == 3 && g->x == 0 && g->y == 0 && g->advance == 4 && g->depth == 8 &&
          !memcmp(g->pix, "\x00\x55\xAA\xFF\xAA\x55\x00\xFF\x00", 9), "ttf: strike 2 A: coverage 0, 85, 170, 255 from 2 bits");
    BmPlain pl;
    CHECK(g && bm_glyph_to_plain(g, &pl) && pl.pix[0] == '.' && pl.pix[1] == 'B' && pl.pix[2] == 'N' && pl.pix[3] == 'Z',
          "ttf: plain of a depth-8 glyph uses the 37 levels");
    bm_plain_free(&pl);
    CHECK(bm_find(s, 2, 'B') == NULL, "ttf: strike 2 has only A");
    const BmGlyph *a0 = bm_glyph_at(s, 0, 0);
    CHECK(a0 && a0->code == 1, "ttf: glyph code is the glyph ID");
    bm_close(s);
}

/* ------------------------------------------------------------------------------------------------ errors and fuzz */
static unsigned g_rng = 12345u;
static int g_fuzz = 1;   /* BMSRC_FUZZ=n in the environment multiplies the iterations of the fuzz tests */
static unsigned rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static unsigned char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = (unsigned char *)malloc((size_t)sz + 1);
    if (d && fread(d, 1, (size_t)sz, f) != (size_t)sz) { free(d); d = NULL; }
    fclose(f);
    *n = d ? (size_t)sz : 0;
    return d;
}

static int spit(const char *path, const unsigned char *d, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(d, 1, n, f);
    fclose(f);
    return 1;
}

/* touches everything a source offers; returns a checksum so the work is not optimised away */
static unsigned long touch(BmSource *s) {
    unsigned long sum = 0;
    for (int i = 0; i < bm_strike_count(s); i++) {
        BmStrikeInfo in;
        bm_strike_info(s, i, &in);
        sum += (unsigned long)(in.ascent + in.descent) + strlen(in.name);
        int n = bm_glyph_count(s, i);
        for (int k = 0; k < n; k++) {
            const BmGlyph *g = bm_glyph_at(s, i, k);
            if (!g) { sum += 1000000; continue; }
            if (g->w < 0 || g->h < 0 || (g->w && g->h && !g->pix)) sum += 1000000007ul;
            for (int p = 0; p < g->w * g->h; p++) sum += g->pix[p];
            if (g->codepoint >= 0 && bm_find(s, i, g->codepoint) == NULL) sum += 1000000009ul;
            BmPlain pl;
            if (bm_glyph_to_plain(g, &pl)) bm_plain_free(&pl);
        }
        bm_find(s, i, 'A');
        bm_find(s, i, 0x10FFFF);
    }
    return sum;
}

static int fuzz_mem(const unsigned char *d, size_t n) {
    char err[BM_ERR_LEN];
    BmSource *s = bm_open_mem(d, n, err, sizeof err);
    if (s) { (void)touch(s); bm_close(s); return 1; }
    return err[0] != 0;   /* a failure must come with a message */
}

static void test_fuzz_binary(const char *file) {
    char path[256], name[300];
    size_t n;
    snprintf(path, sizeof path, DIR "%s", file);
    unsigned char *d = slurp(path, &n);
    snprintf(name, sizeof name, "fuzz %s: read", file);
    CHECK(d != NULL, name);
    if (!d) return;
    int bad = 0, opened = 0;
    unsigned char *m = (unsigned char *)malloc(n + 1);
    char err[BM_ERR_LEN];
    BmSource *whole = bm_open_mem(d, n, err, sizeof err);
    if (whole) { opened = 1; bm_close(whole); }
    /* every truncation (a step for big files) */
    size_t step = n > 700 ? n / 350 : 1;
    for (size_t k = 0; k < n; k += step) if (!fuzz_mem(d, k)) bad++;
    /* bit flips, including whole bytes set to 0x00 / 0xFF and 32-bit values */
    for (int it = 0; it < 1500 * g_fuzz; it++) {
        memcpy(m, d, n);
        int flips = 1 + (int)(rnd() % 6);
        for (int f = 0; f < flips; f++) {
            size_t at = rnd() % n;
            switch (rnd() % 4) {
            case 0: m[at] ^= (unsigned char)(1u << (rnd() % 8)); break;
            case 1: m[at] = 0; break;
            case 2: m[at] = 0xFF; break;
            default: m[at] = (unsigned char)rnd(); break;
            }
        }
        if (it % 5 == 0) { size_t cut = rnd() % (n + 1); if (!fuzz_mem(m, cut)) bad++; }
        else if (!fuzz_mem(m, n)) bad++;
    }
    snprintf(name, sizeof name, "fuzz %s: truncated and bit-flipped copies never crash, failures carry a message", file);
    CHECK(bad == 0 && opened, name);
    if (bad) printf("      %d failures without message\n", bad);
    free(m);
    free(d);
}

static void test_fuzz_sheets(void) {
    static const char *const files[] = {"sheet.png", "sheet.json", "sheet_alpha.png", "sheet_alpha.json", "raw.bin", "raw.json", "raw9.json",
                                        "records.bin", "records.json"};
    char a[256], b[256];
    int copied = 1;
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {   /* the sheets and their sources side by side in tests/tmp */
        size_t n;
        snprintf(a, sizeof a, DIR "%s", files[i]);
        snprintf(b, sizeof b, TMP "%s", files[i]);
        unsigned char *d = slurp(a, &n);
        if (!d || !spit(b, d, n)) copied = 0;
        free(d);
    }
    if (!copied) { printf("SKIP  fuzz sheets: tests/tmp is missing\n"); return; }
    static const char *const descs[] = {"sheet.json", "sheet_alpha.json", "raw.json", "raw9.json", "records.json"};
    static const char *const srcs[] = {"sheet.png", "sheet_alpha.png", "raw.bin", "records.bin"};
    int bad = 0;
    char err[BM_ERR_LEN];
    for (size_t i = 0; i < sizeof descs / sizeof descs[0]; i++) {   /* the descriptor mutated, the source intact */
        size_t n;
        snprintf(a, sizeof a, DIR "%s", descs[i]);
        unsigned char *d = slurp(a, &n);
        unsigned char *m = (unsigned char *)malloc(n + 1);
        for (int it = 0; it < 120 * g_fuzz; it++) {
            memcpy(m, d, n);
            size_t len = n;
            if (it < 80) { size_t cut = it < (int)n ? (size_t)it : rnd() % (n + 1); len = cut; }
            else if (it >= 80) for (int f = 0; f < 3; f++) m[rnd() % n] = (unsigned char)(rnd() % 3 ? (32 + rnd() % 95) : rnd());
            snprintf(b, sizeof b, TMP "fz_desc.json");
            if (it >= 80 || it % 2) { unsigned char *t = (unsigned char *)malloc(len + 1); memcpy(t, m, len); spit(b, t, len); free(t); }
            else spit(b, m, len);
            /* the mutated descriptor lives in tests/tmp next to the copied sources */
            BmSource *s = bm_open(b, err, sizeof err);
            if (s) { (void)touch(s); bm_close(s); } else if (!err[0]) bad++;
        }
        free(m);
        free(d);
    }
    for (size_t i = 0; i < sizeof srcs / sizeof srcs[0]; i++) {   /* the source mutated, the descriptor intact */
        size_t n;
        snprintf(a, sizeof a, DIR "%s", srcs[i]);
        unsigned char *d = slurp(a, &n);
        unsigned char *m = (unsigned char *)malloc(n + 1);
        const char *desc = i == 0 ? "sheet.json" : i == 1 ? "sheet_alpha.json" : i == 2 ? "raw.json" : "records.json";
        snprintf(a, sizeof a, TMP "%s", desc);
        for (int it = 0; it < 150 * g_fuzz; it++) {
            memcpy(m, d, n);
            size_t len = n;
            if (it % 4 == 0) len = rnd() % (n + 1);
            else for (int f = 0; f < 1 + (int)(rnd() % 4); f++) m[rnd() % n] ^= (unsigned char)(1u << (rnd() % 8));
            snprintf(b, sizeof b, TMP "%s", srcs[i]);
            spit(b, m, len);
            BmSource *s = bm_open(a, err, sizeof err);
            if (s) { (void)touch(s); bm_close(s); } else if (!err[0]) bad++;
        }
        spit(b, d, n);   /* restore */
        free(m);
        free(d);
    }
    CHECK(bad == 0, "fuzz sheets: mutated descriptors and mutated sources never crash, failures carry a message");
}

static void test_garbage(void) {
    char err[BM_ERR_LEN];
    int bad = 0;
    unsigned char buf[4096];
    static const char *const magics[] = {"\x01" "fcp", "STARTFONT 2.1\n", "MZ", "\x00\x01\x00\x00", "OTTO", "ttcf", "\x00\x02\x00\x00\x00\x00", "{\"cell\":",
                                         "true", ""};
    size_t sizes[] = {0, 1, 2, 3, 7, 16, 64, 66, 109, 148, 200, 1000, 4096};
    for (size_t m = 0; m < sizeof magics / sizeof magics[0]; m++)
        for (size_t z = 0; z < sizeof sizes / sizeof sizes[0]; z++)
            for (int it = 0; it < 60; it++) {
                size_t n = sizes[z];
                for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)(it % 3 == 0 ? 0 : it % 3 == 1 ? rnd() : (rnd() % 8 == 0 ? 0xFF : rnd() % 64));
                size_t ml = strlen(magics[m]);
                if (m == 3) ml = 4;
                if (m == 6) ml = 6;
                for (size_t i = 0; i < ml && i < n; i++) buf[i] = (unsigned char)magics[m][i];
                if (m == 2 && n > 0x44) { buf[0x3C] = 0x40; buf[0x3D] = buf[0x3E] = buf[0x3F] = 0; buf[0x40] = it & 1 ? 'N' : 'P'; buf[0x41] = it & 1 ? 'E' : 'E'; }
                BmSource *s = bm_open_mem(buf, n, err, sizeof err);
                if (s) { (void)touch(s); bm_close(s); } else if (!err[0]) bad++;
            }
    CHECK(bad == 0, "garbage: random bytes behind every magic never crash");
    CHECK(bm_open("testdata/bitmap/does-not-exist.bdf", err, sizeof err) == NULL && err[0], "missing file: NULL with a message");
    CHECK(bm_open(NULL, err, sizeof err) == NULL, "NULL path");
    CHECK(bm_open_mem(NULL, 0, err, sizeof err) == NULL, "NULL data");
    CHECK(bm_open_mem((const unsigned char *)"x", 1, NULL, 0) == NULL, "no error buffer is fine");
    CHECK(bm_strike_count(NULL) == 0 && bm_glyph_count(NULL, 0) == 0 && bm_glyph_at(NULL, 0, 0) == NULL, "NULL source in the accessors");
    BmStrikeInfo in;
    CHECK(!bm_strike_info(NULL, 0, &in), "NULL source: strike info fails");
    /* a JSON sheet in memory needs its file */
    CHECK(bm_open_mem((const unsigned char *)"{\"cell\":{}}", 11, err, sizeof err) == NULL && err[0], "JSON in memory: refused with a message");
    /* a descriptor that is not a sheet */
    static const char notsheet[] = "{\"a\": [1, 2, {\"b\": null}], \"c\": \"\\u00e9\"}";
    spit(TMP "notsheet.json", (const unsigned char *)notsheet, sizeof notsheet - 1);
    BmSource *s = bm_open(TMP "notsheet.json", err, sizeof err);
    CHECK(s == NULL && strstr(err, "cell") != NULL, "JSON without \"cell\": not a sheet descriptor");
    spit(TMP "deep.json", (const unsigned char *)"{\"cell\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[", 50);
    s = bm_open(TMP "deep.json", err, sizeof err);
    CHECK(s == NULL && err[0], "JSON nested too deep or truncated: error, no crash");
}

int main(void) {
    const char *fz = getenv("BMSRC_FUZZ");
    if (fz && atoi(fz) > 0) g_fuzz = atoi(fz);
    printf("test_bmsrc\n");
    test_bdf();
    test_pcf();
    test_fnt();
    test_sheets();
    test_ttf();
    static const char *const bins[] = {"font.bdf", "font_lsb.pcf", "font_msb.pcf", "font_mix.pcf", "font.fnt", "fixed_prop_ne.fon",
                                       "fixed_prop_pe.fon", "strikes.ttf"};
    for (size_t i = 0; i < sizeof bins / sizeof bins[0]; i++) test_fuzz_binary(bins[i]);
    test_fuzz_sheets();
    test_garbage();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
