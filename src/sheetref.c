/* sheetref.c - the reference glyphs of sheet.h from outline fonts.  See sheetref.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fit.h"
#include "sheetref.h"

int sheet_refs_add_outline(SheetRefs *r, const char *path, float em)
{
    char err[160];
    Outline *ol = outline_open(path, err, sizeof err);
    if (!ol) return 0;
    FitParams p;
    FitGrid g;
    memset(&g, 0, sizeof g);
    memset(&p, 0, sizeof p);
    if (fit_find_grid(ol, &g, NULL, 0) == 1 && g.sure) fit_grid_params(ol, &g, &p);   /* a pixel font: its own pixels */
    else { p.em = em; p.aspect = 1; p.threshold = 0.45f; p.mode = FIT_SMOOTH; }
    fit_grid_free(&g);
    int codes[512], n = sheet_candidate_codes(codes, 512), added = 0;
    sheet_refs_begin(r);
    for (int i = 0; i < n; i++) {
        FitBitmap b;
        int gi = outline_glyph_index(ol, codes[i]);
        if (gi <= 0 || fit_render(ol, gi, &p, NULL, &b) != 0) continue;
        if (b.w > 0) { sheet_refs_add(r, codes[i], b.pix, b.w, b.h, b.left, b.top); added++; }
        fit_bitmap_free(&b);
    }
    sheet_refs_end(r);
    outline_close(ol);
    return added;
}

int sheet_refs_add_defaults(SheetRefs *r, const char *dir)
{
    static const struct { const char *file; float em; } f[] = {
        {"unifont/unifont-18.0.01.otf", 16}, {"noto-sans/NotoSans-Regular.ttf", 16}, {"noto-sans/NotoSans-Bold.ttf", 16},
        {"noto-sans/NotoSans-Black.ttf", 14}};
    int n = 0;
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
        char p[1200];
        snprintf(p, sizeof p, "%s/%s", dir, f[i].file);
        n += sheet_refs_add_outline(r, p, f[i].em);
    }
    return n;
}
