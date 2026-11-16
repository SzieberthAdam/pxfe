/* sheet_tool.c - a command line look at sheet.h: what the analysis finds in a picture of a font.
 *
 *   sheet_tool <picture> [-v] [-refs <font file> ...]
 *
 * One line: the layout, the scale, the cells and the characters found (the code points as text, '?' for a glyph without a code,
 * '_' for a blank cell).  -v adds one line per cell (x y w h, ink box, code, score).
 * The references are GNU Unifont and Noto Sans Regular and Bold of assets/fonts unless given.
 *
 * Build: gcc tests\sheet_tool.c src\sheet.c src\sheetref.c src\fit.c src\outline.c src\bmsrc.c -o tests\sheet_tool.exe -O2 -I src -I vendor */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmsrc.h"
#include "sheet.h"
#include "sheetref.h"

static void put_utf8(int c)
{
    if (c < 0x80) putchar(c);
    else if (c < 0x800) { putchar(0xC0 | (c >> 6)); putchar(0x80 | (c & 63)); }
    else { putchar(0xE0 | (c >> 12)); putchar(0x80 | ((c >> 6) & 63)); putchar(0x80 | (c & 63)); }
}

int main(int argc, char **argv)
{
    int verbose = 0, nref = 0;
    const char *refs[8], *pic = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-refs")) { while (i + 1 < argc && argv[i + 1][0] != '-' && nref < 8) refs[nref++] = argv[++i]; }
        else pic = argv[i];
    }
    if (!pic) { fprintf(stderr, "usage: sheet_tool <picture> [-v] [-refs <font> ...]\n"); return 2; }
    SheetRefs *r = sheet_refs_new();
    if (!nref) sheet_refs_add_defaults(r, "assets/fonts");
    for (int i = 0; i < nref; i++) sheet_refs_add_outline(r, refs[i], 16);
    char err[256] = "";
    int w, h;
    unsigned char *rgba = bm_load_rgba(pic, &w, &h, err, sizeof err);
    if (!rgba) { printf("error %s\n", err); return 1; }
    Sheet s;
    if (sheet_analyze(rgba, w, h, NULL, r, &s, err, sizeof err) < 0) { printf("error %s\n", err); return 1; }
    printf("layout=%s scale=%d size=%dx%d cell=%dx%d off=%d,%d rows=%d cells=%d glyphs=%d box=%d+%d colours=%d conf=%.2f refs=%d\n",
           s.layout == SHEET_MARKER ? "marker" : s.layout == SHEET_GRID ? "grid" : "rows", s.scale, s.w, s.h, s.cellW, s.cellH, s.offX, s.offY,
           s.nrows, s.ncells, s.glyphs, s.ascent, s.descent, s.npalette, s.confidence, sheet_refs_count(r));
    int row = -1;
    for (int i = 0; i < s.ncells; i++) {
        const SheetCell *c = &s.cells[i];
        if (c->row != row) { if (row >= 0) putchar('\n'); row = c->row; printf("  "); }
        if (!c->iw) putchar(c->code == 32 ? ' ' : '_');
        else if (c->code < 0) putchar('?');
        else put_utf8(c->code);
    }
    putchar('\n');
    if (verbose)
        for (int i = 0; i < s.ncells; i++) {
            const SheetCell *c = &s.cells[i];
            printf("cell %d row %d: %d %d %d %d ink %d %d %d %d code %d score %.2f\n", i, c->row, c->x, c->y, c->w, c->h, c->ix, c->iy, c->iw, c->ih, c->code, c->score);
        }
    sheet_free(&s);
    free(rgba);
    sheet_refs_free(r);
    return 0;
}
