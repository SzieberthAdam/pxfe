/* test_sheet.c - tests of sheet.h on the pictures of testdata/sheet: the layout found, the scale, the cells, the characters
 * recognised, the colours.  The references are the fonts of assets/fonts.
 * Build: see build.bat (test). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmsrc.h"
#include "sheet.h"
#include "sheetref.h"

static int passed = 0, failed = 0;
#define CHECK(c, ...) do { if (c) passed++; else { failed++; printf("FAIL  "); printf(__VA_ARGS__); printf("  (%s:%d)\n", __FILE__, __LINE__); } } while (0)
#define DIR "testdata/sheet/"

static SheetRefs *refs;

static int load(const char *file, const SheetOpts *o, Sheet *s)
{
    char err[200] = "";
    int w, h;
    unsigned char *rgba = bm_load_rgba(file, &w, &h, err, sizeof err);
    CHECK(rgba != NULL, "%s loads (%s)", file, err);
    if (!rgba) return 0;
    int rc = sheet_analyze(rgba, w, h, o, refs, s, err, sizeof err);
    free(rgba);
    CHECK(rc == 0, "%s is analysed (%s)", file, err);
    return rc == 0;
}

/* how many of the cells with ink have the code `first + their index` (blank cells count as places when `places`) */
static int in_order(const Sheet *s, int first, int places, int *ink)
{
    int good = 0, k = 0;
    *ink = 0;
    for (int i = 0; i < s->ncells; i++) {
        if (!s->cells[i].iw) { if (places) k++; continue; }
        (*ink)++;
        good += s->cells[i].code == first + k;
        k++;
    }
    return good;
}

static void test_marker(void)
{
    Sheet s;
    if (!load(DIR "marker8.png", NULL, &s)) return;
    CHECK(s.layout == SHEET_MARKER && s.markerRow == 0, "marker8: a marker row at the top, got layout %d", s.layout);
    CHECK(s.ncells == 94 && s.glyphs == 94 && s.nrows == 1, "marker8: 94 glyphs, got %d cells, %d glyphs", s.ncells, s.glyphs);
    CHECK(s.scale == 1 && s.npalette == 1 && !s.alphaBg, "marker8: not enlarged, one colour, an opaque background");
    int ink, good = in_order(&s, 33, 0, &ink);
    CHECK(good >= 92, "marker8: the characters are '!' .. '~' in order: %d of %d", good, ink);
    /* the marker's column belongs to no glyph, and the marker is no ink */
    int clean = 1;
    for (int i = 0; i < s.ncells; i++) if (s.ink[s.cells[i].x - 1]) clean = 0;
    CHECK(clean && s.cells[0].x == 1, "marker8: a glyph starts right of its marker, and the markers are not ink");
    CHECK(s.cells[0].ix == 2 && s.cells[0].iw == 2 && s.cells[0].y == 0, "marker8: the ink box of '!', got %d %d %d", s.cells[0].ix, s.cells[0].iw, s.cells[0].ih);
    CHECK(s.ascent + s.descent >= 7 && s.ascent + s.descent <= 8, "marker8: the font box is 7 or 8 rows, got %d + %d", s.ascent, s.descent);
    sheet_free(&s);
    /* a first code given: no recognition */
    SheetOpts o;
    memset(&o, 0, sizeof o);
    o.firstCode = 0x41;
    if (load(DIR "marker8.png", &o, &s)) {
        CHECK(!s.recognised && s.cells[0].code == 0x41 && s.cells[5].code == 0x46, "marker8: counted from a first code");
        sheet_free(&s);
    }
    o.firstCode = 0;
    o.layout = SHEET_ROWS;
    if (load(DIR "marker8.png", &o, &s)) {
        CHECK(s.layout == SHEET_ROWS, "marker8: the layout can be forced to rows");
        sheet_free(&s);
    }
}

static void test_grid(void)
{
    Sheet s;
    if (load(DIR "font8x8.png", NULL, &s)) {
        CHECK(s.layout == SHEET_GRID && s.cellW == 8 && s.cellH == 8 && s.offX == 0 && s.offY == 0, "font8x8: a grid of 8 x 8 from the corner, got %d x %d at %d,%d (layout %d)", s.cellW, s.cellH, s.offX, s.offY, s.layout);
        CHECK(s.alphaBg && s.npalette == 1 && s.scale == 1, "font8x8: a transparent background, one colour");
        CHECK(s.nrows == 8 && s.ncells == 120, "font8x8: 8 rows of 15 cells, got %d rows, %d cells", s.nrows, s.ncells);
        int ink, good = in_order(&s, 32, 1, &ink);
        int ascii = 0;
        for (int i = 1; i < 95; i++) ascii += s.cells[i].code == 32 + i;
        CHECK(ascii >= 92, "font8x8: ASCII from the space, cell by cell: %d of 94", ascii);
        CHECK(s.cells[0].iw == 0 && s.cells[0].code == 32, "font8x8: the first cell is the space");
        CHECK(s.cells[96].code == 0xC7 && s.cells[97].code == 0xFC && s.cells[98].code == 0xE9, "font8x8: the accented letters follow in the order of code page 437 (got U+%04X U+%04X U+%04X)", s.cells[96].code, s.cells[97].code, s.cells[98].code);
        (void)good;
        sheet_free(&s);
    }
    if (load(DIR "font-20x20-RPG.png", NULL, &s)) {
        CHECK(s.layout == SHEET_GRID && s.cellW == 20 && s.cellH == 20, "font-20x20-RPG: a grid of 20 x 20, got %d x %d", s.cellW, s.cellH);
        CHECK(s.npalette == 3, "font-20x20-RPG: three colours, got %d", s.npalette);
        int ascii = 0;
        for (int i = 1; i < 95; i++) ascii += s.cells[i].iw == 0 || s.cells[i].code == 32 + i;
        CHECK(ascii >= 92, "font-20x20-RPG: ASCII in order, capitals in the places of the small letters: %d of 94", ascii);
        CHECK(s.cells[65].code == 'a' && s.cells[33].code == 'A', "font-20x20-RPG: the second alphabet is the small letters");
        unsigned char *buf = (unsigned char *)malloc((size_t)(s.cells[33].w * s.cells[33].h));
        sheet_cell_pixels(&s, &s.cells[33], buf);
        int seen[8] = {0}, kinds = 0;
        for (int i = 0; i < s.cells[33].w * s.cells[33].h; i++) if (buf[i] < 8) seen[buf[i]] = 1;
        for (int i = 1; i < 8; i++) kinds += seen[i];
        CHECK(seen[0] && kinds == 3, "font-20x20-RPG: the pixels of A use the three colours, got %d", kinds);
        free(buf);
        sheet_free(&s);
        SheetOpts o;
        memset(&o, 0, sizeof o);
        o.mono = 1;
        if (load(DIR "font-20x20-RPG.png", &o, &s)) { CHECK(s.npalette == 1, "font-20x20-RPG: one colour when asked"); sheet_free(&s); }
        memset(&o, 0, sizeof o);
        o.layout = SHEET_GRID; o.cellW = 10; o.cellH = 20;
        if (load(DIR "font-20x20-RPG.png", &o, &s)) { CHECK(s.cellW == 10 && s.cellH == 20 && s.layout == SHEET_GRID, "font-20x20-RPG: a cell size given is taken"); sheet_free(&s); }
    }
    if (load(DIR "title-font-shmup.png", NULL, &s)) {
        static const char *want = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        CHECK(s.layout == SHEET_GRID && s.cellW == 42 && s.cellH == 33, "title-font-shmup: cells of 42 x 33 although the glyphs touch, got %d x %d", s.cellW, s.cellH);
        int good = 0;
        for (int i = 0; i < 36 && i < s.ncells; i++) good += s.cells[i].code == want[i];
        CHECK(s.ncells == 36 && good >= 34, "title-font-shmup: A-Z then 0-9: %d of 36 (%d cells)", good, s.ncells);
        CHECK(s.npalette == 6, "title-font-shmup: six colours, got %d", s.npalette);
        sheet_free(&s);
    }
}

static void test_rows(void)
{
    Sheet s;
    if (!load(DIR "font-1-tds.png", NULL, &s)) return;
    CHECK(s.layout == SHEET_ROWS && s.nrows == 4, "font-1-tds: four rows of glyphs, got layout %d, %d rows", s.layout, s.nrows);
    CHECK(s.ncells == 93, "font-1-tds: 93 glyphs, got %d", s.ncells);
    int ink, good = in_order(&s, 33, 0, &ink);
    CHECK(good >= 88, "font-1-tds: '!' .. '}' in order (read through the outline): %d of %d", good, ink);
    CHECK(s.npalette == 3, "font-1-tds: three colours, got %d", s.npalette);
    sheet_free(&s);
}

static void test_enlarged(void)
{
    Sheet s;
    if (!load(DIR "enlarged3.png", NULL, &s)) return;
    CHECK(s.scale == 3 && s.w == 120 && s.h == 32, "enlarged3: every pixel is 3 x 3, the sheet is 120 x 32; got scale %d, %d x %d", s.scale, s.w, s.h);
    CHECK(s.layout == SHEET_GRID && s.cellW == 8 && s.cellH == 8, "enlarged3: a grid of 8 x 8, got %d x %d (layout %d)", s.cellW, s.cellH, s.layout);
    int ascii = 0;
    for (int i = 1; i < 60 && i < s.ncells; i++) ascii += s.cells[i].code == 32 + i;
    CHECK(ascii >= 57, "enlarged3: ASCII from the space: %d of 59", ascii);
    CHECK(!s.alphaBg && s.bg == 0xFFFFFFFFu, "enlarged3: a white background");
    sheet_free(&s);
}

static void test_bad(void)
{
    Sheet s;
    char err[100] = "";
    unsigned char blank[16 * 16 * 4];
    memset(blank, 255, sizeof blank);
    CHECK(sheet_analyze(blank, 16, 16, NULL, refs, &s, err, sizeof err) < 0 && err[0], "a blank picture is an error (%s)", err);
    CHECK(sheet_analyze(NULL, 16, 16, NULL, refs, &s, err, sizeof err) < 0, "no picture is an error");
    CHECK(sheet_analyze(blank, 1, 1, NULL, refs, &s, err, sizeof err) < 0, "a picture of one pixel is an error");
    int w, h;
    CHECK(bm_load_rgba(DIR "README.txt", &w, &h, err, sizeof err) == NULL && err[0], "a text file is no picture (%s)", err);
    CHECK(bm_load_rgba(DIR "nothere.png", &w, &h, err, sizeof err) == NULL, "a missing file is an error");
    /* one dot: a sheet of one glyph */
    blank[(5 * 16 + 5) * 4] = 0; blank[(5 * 16 + 5) * 4 + 1] = 0; blank[(5 * 16 + 5) * 4 + 2] = 0;
    CHECK(sheet_analyze(blank, 16, 16, NULL, refs, &s, err, sizeof err) == 0 && s.ncells == 1, "a single dot is one glyph");
    sheet_free(&s);
    /* without references the glyphs are counted */
    CHECK(sheet_analyze(blank, 16, 16, NULL, NULL, &s, err, sizeof err) == 0 && !s.recognised && s.cells[0].code == 33, "without references the glyphs are counted from '!'");
    sheet_free(&s);
}

int main(void)
{
    refs = sheet_refs_new();
    int n = sheet_refs_add_defaults(refs, "assets/fonts");
    CHECK(n > 500 && sheet_refs_count(refs) == n, "the reference glyphs are made from assets/fonts: %d", n);
    int codes[512], k = sheet_candidate_codes(codes, 512), sorted = 1;
    for (int i = 1; i < k; i++) if (codes[i] <= codes[i - 1]) sorted = 0;
    CHECK(k > 250 && sorted && codes[0] == 32, "the candidate codes: %d, ascending from the space", k);
    test_marker();
    test_grid();
    test_rows();
    test_enlarged();
    test_bad();
    sheet_refs_free(refs);
    printf("\ntest_sheet: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
