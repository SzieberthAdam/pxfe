/* test_fit.c - tests of fit.h: the grid of pixel fonts drawn with outlines, the size of fonts that are none, the drawing of glyphs.
 *
 * The pixel fonts of testdata/outline (tests/gen_pixel_fonts.py) are drawn from the bitmaps in pixel_glyphs.txt, so the test knows
 * every pixel that must come back.
 * Build: see build.bat (test). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fit.h"

static int passed = 0, failed = 0;
#define CHECK(c, ...) do { if (c) passed++; else { failed++; printf("FAIL  "); printf(__VA_ARGS__); printf("  (%s:%d)\n", __FILE__, __LINE__); } } while (0)

#define DIR "testdata/outline/"

typedef struct { int cp; char rows[9][6]; } Ref;
static Ref refs[64];
static int nrefs;

static void load_refs(void)
{
    FILE *f = fopen(DIR "pixel_glyphs.txt", "rb");
    char line[64];
    int row = 9;
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (row >= 9) { if (nrefs >= 64) break; refs[nrefs].cp = (int)strtol(line, NULL, 16); row = 0; nrefs++; }
        else { snprintf(refs[nrefs - 1].rows[row], 6, "%.5s", line); row++; }
    }
    fclose(f);
}

/* the glyph drawn by fit_render equals the reference bitmap (7 rows above the baseline, 2 below, 5 columns from the pen) */
static int same_as_ref(const FitBitmap *b, const Ref *r)
{
    for (int row = 0; row < 9; row++)
        for (int col = 0; col < 5; col++) {
            int want = r->rows[row][col] == '#';
            int y = b->top - (7 - row), x = col - b->left;   /* row of the bitmap that holds the pixels from 7-row-1 to 7-row */
            int have = b->pix && x >= 0 && x < b->w && y >= 0 && y < b->h && b->pix[y * b->w + x];
            if (want != have) return 0;
        }
    for (int i = 0, ink = 0; b->pix && i <= b->w * b->h; i++) {   /* and no ink outside the 5 x 9 box */
        if (i == b->w * b->h) {
            int want = 0;
            for (int row = 0; row < 9; row++) for (int col = 0; col < 5; col++) want += r->rows[row][col] == '#';
            if (ink != want) return 0;
            break;
        }
        ink += b->pix[i] != 0;
    }
    return 1;
}

static void test_pixel_font(const char *file, const char *how, double xp, double yp)
{
    char err[200] = "";
    Outline *ol = outline_open(file, err, sizeof err);
    CHECK(ol != NULL, "%s opens (%s)", file, err);
    if (!ol) return;
    FitGrid g;
    int rc = fit_find_grid(ol, &g, err, sizeof err);
    CHECK(rc == 1 && g.sure, "%s: a grid is found (%s)", file, err);
    CHECK(g.x == 100 && g.y == 100, "%s: the pixel is 100 x 100 units, got %g x %g", file, g.x, g.y);
    CHECK(g.xPhase == xp && g.yPhase == yp, "%s: phases %g %g, got %g %g", file, xp, yp, g.xPhase, g.yPhase);
    CHECK(!strcmp(g.how, how), "%s: found by '%s', got '%s'", file, how, g.how);
    CHECK(!g.perGlyph && g.nphase == 0, "%s: one grid for all glyphs", file);
    FitParams p;
    fit_grid_params(ol, &g, &p);
    CHECK(p.mode == FIT_PIXEL && p.em == 10 && p.aspect == 1, "%s: 10 px per em, square, got %g %g", file, p.em, p.aspect);
    CHECK(fabs(p.dx + xp / 100) < 1e-6 && fabs(p.dy + yp / 100) < 1e-6, "%s: the offset undoes the phase, got %g %g", file, p.dx, p.dy);
    int a, d;
    fit_box(ol, &p, &a, &d);
    CHECK(a == 8 && d == 2, "%s: font box 8 + 2, got %d + %d", file, a, d);
    int bad = 0, adv = 0;
    for (int i = 0; i < nrefs; i++) {
        FitBitmap b;
        int gi = outline_glyph_index(ol, refs[i].cp);
        if (gi <= 0 || fit_render(ol, gi, &p, NULL, &b) != 0) { bad++; continue; }
        if (!same_as_ref(&b, &refs[i])) { bad++; if (bad < 3) printf("      U+%04X differs\n", refs[i].cp); }
        if (b.advance != 6) adv++;
        fit_bitmap_free(&b);
    }
    CHECK(bad == 0, "%s: every glyph comes back pixel by pixel (%d of %d differ)", file, bad, nrefs);
    CHECK(adv == 0, "%s: every advance is 6", file);
    {   /* a space: no ink, an advance */
        FitBitmap b;
        CHECK(fit_render(ol, outline_glyph_index(ol, ' '), &p, NULL, &b) == 0 && b.w == 0 && b.h == 0 && b.pix == NULL && b.advance == 6, "%s: the space is blank", file);
    }
    {   /* a correction moves and scales the glyph */
        FitBitmap b0, b1, b2;
        FitCorr c1 = {2, -1, 1}, c2 = {0, 0, 2};
        int gi = outline_glyph_index(ol, 'H');
        fit_render(ol, gi, &p, NULL, &b0);
        fit_render(ol, gi, &p, &c1, &b1);
        fit_render(ol, gi, &p, &c2, &b2);
        CHECK(b1.w == b0.w && b1.h == b0.h && b1.left == b0.left + 2 && b1.top == b0.top - 1 && !memcmp(b0.pix, b1.pix, (size_t)(b0.w * b0.h)), "%s: a correction of (2, -1) moves the glyph", file);
        if (strcmp(how, "dots")) CHECK(b2.w == 2 * b0.w && b2.h == 2 * b0.h, "%s: a correction of size 2 doubles it, got %d x %d", file, b2.w, b2.h);   /* (the centres of doubled pixels miss dots) */
        CHECK(b2.advance == 12, "%s: a correction of size 2 doubles the advance", file);
        fit_bitmap_free(&b0); fit_bitmap_free(&b1); fit_bitmap_free(&b2);
    }
    fit_grid_free(&g);
    outline_close(ol);
}

/* parts (8-connected ink) and holes of a bitmap, by flood fill */
static void topo(const FitBitmap *b, int *parts, int *holes)
{
    int W = b->w + 2, H = b->h + 2, n = W * H, bg = 0;
    unsigned char *m = (unsigned char *)calloc((size_t)n, 1);
    int *st = (int *)malloc(sizeof(int) * (size_t)n);
    *parts = *holes = 0;
    for (int y = 0; y < b->h; y++) for (int x = 0; x < b->w; x++) m[(y + 1) * W + x + 1] = b->pix[y * b->w + x] ? 1 : 0;
    for (int i = 0; i < n; i++) {
        if (m[i] >= 2) continue;
        int ink = m[i], sp = 0;
        m[i] = (unsigned char)(2 + ink);
        st[sp++] = i;
        while (sp) {
            int q = st[--sp];
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int x = q % W + dx, y = q / W + dy;
                    if ((!dx && !dy) || (!ink && dx && dy) || x < 0 || y < 0 || x >= W || y >= H || m[y * W + x] != ink) continue;
                    m[y * W + x] = (unsigned char)(2 + ink);
                    st[sp++] = y * W + x;
                }
        }
        if (ink) (*parts)++; else bg++;
    }
    *holes = bg - 1;
    free(m);
    free(st);
}

static void test_smooth(void)
{
    char err[200] = "";
    Outline *ol = outline_open(DIR "NotoSub.ttf", err, sizeof err);
    CHECK(ol != NULL, "NotoSub.ttf opens (%s)", err);
    if (!ol) return;
    FitGrid g;
    CHECK(fit_find_grid(ol, &g, err, sizeof err) == 0, "Noto Sans is no pixel font: no grid");
    fit_grid_free(&g);
    FitParams p;
    FitSizeReport rep;
    int rc = fit_find_size(ol, NULL, &p, &rep, err, sizeof err);
    CHECK(rc == 1, "a size is found (%s)", err);
    CHECK(p.mode == FIT_SMOOTH && p.aspect == 1 && p.dx == 0, "the size search gives a smooth placement");
    CHECK(p.em >= 7 && p.em <= 16, "the size is small: 7 to 16 px per em, got %g", p.em);
    CHECK(rep.probes >= 10 && rep.topo >= 0.85f && rep.topo >= rep.bestTopo - 0.04f, "most glyphs keep their parts and holes: %g of %d (best %g)", rep.topo, rep.probes, rep.bestTopo);
    printf("      Noto Sans subset: %g px per em, dy %g, threshold %g, topology %.3f, cover %.3f\n", p.em, p.dy, p.threshold, rep.topo, rep.cover);
    {   /* the counters stay open, the dot of the i stays apart */
        static const struct { int cp, parts, holes; } want[] = {{'A', 1, 1}, {'e', 1, 1}, {'o', 1, 1}, {'i', 2, 0}, {'H', 1, 0}};
        for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
            FitBitmap b;
            int gi = outline_glyph_index(ol, want[i].cp), parts = -1, holes = -1;
            if (gi <= 0) continue;
            CHECK(fit_render(ol, gi, &p, NULL, &b) == 0 && b.w > 0, "'%c' is drawn", want[i].cp);
            if (b.w > 0) topo(&b, &parts, &holes);
            CHECK(parts == want[i].parts && holes == want[i].holes, "'%c' has %d part(s) and %d hole(s), got %d and %d", want[i].cp, want[i].parts, want[i].holes, parts, holes);
            fit_bitmap_free(&b);
        }
    }
    {   /* drawing is a pure function of the placement */
        FitBitmap a, b;
        int gi = outline_glyph_index(ol, 'e');
        fit_render(ol, gi, &p, NULL, &a);
        fit_render(ol, gi, &p, NULL, &b);
        CHECK(a.w == b.w && a.h == b.h && a.left == b.left && a.top == b.top && a.advance == b.advance && !memcmp(a.pix, b.pix, (size_t)(a.w * a.h)), "the same placement draws the same pixels");
        fit_bitmap_free(&a); fit_bitmap_free(&b);
    }
    {   /* the quarter pixel step a glyph was moved by is told, and a correction with an x offset switches it off */
        FitBitmap a, b;
        FitCorr still = {0.25f, 0, 1};
        float s1 = 9, s2 = 9;
        int gi = outline_glyph_index(ol, 'H');
        CHECK(fit_render_shift(ol, gi, &p, NULL, &a, &s1) == 0 && s1 >= -0.5f && s1 <= 0.25f && s1 * 4 == (float)(int)(s1 * 4), "the shift is a quarter pixel step from -0.5 to 0.25, got %g", s1);
        CHECK(fit_render_shift(ol, gi, &p, &still, &b, &s2) == 0 && s2 == 0, "a glyph with an x correction is not moved, got %g", s2);
        fit_bitmap_free(&a); fit_bitmap_free(&b);
        FitParams px = p;
        px.mode = FIT_PIXEL;
        s1 = 9;
        CHECK(fit_render_shift(ol, gi, &px, NULL, &a, &s1) == 0 && s1 == 0, "pixel mode moves nothing");
        fit_bitmap_free(&a);
    }
    {   /* a larger size draws a larger glyph; a lower threshold never removes ink */
        FitParams big = p, low = p, high = p;
        FitBitmap a, b, c, d;
        int gi = outline_glyph_index(ol, 'H'), n1 = 0, n2 = 0;
        big.em = p.em * 2;
        low.threshold = 0.2f;
        high.threshold = 0.8f;
        fit_render(ol, gi, &p, NULL, &a);
        fit_render(ol, gi, &big, NULL, &b);
        CHECK(b.h >= 2 * a.h - 2 && b.h <= 2 * a.h + 2 && b.advance >= 2 * a.advance - 1, "twice the size: H is twice as high (%d -> %d)", a.h, b.h);
        FitCorr still = {0.001f, 0, 1};   /* a correction with an x offset switches the quarter pixel search off: both stand still */
        fit_render(ol, gi, &low, &still, &c);
        fit_render(ol, gi, &high, &still, &d);
        for (int i = 0; i < c.w * c.h; i++) n1 += c.pix[i];
        for (int i = 0; i < d.w * d.h; i++) n2 += d.pix[i];
        CHECK(n1 > n2 && n2 > 0, "threshold 20 %% inks more pixels than 80 %% (%d > %d)", n1, n2);
        fit_bitmap_free(&a); fit_bitmap_free(&b); fit_bitmap_free(&c); fit_bitmap_free(&d);
    }
    {   /* a fixed size is kept */
        FitSizeOpts o = {0, 0, 20};
        FitParams q;
        CHECK(fit_find_size(ol, &o, &q, NULL, err, sizeof err) == 1 && q.em == 20, "a fixed size of 20 is kept, got %g", q.em);
    }
    {   /* the width stretches the glyph across only */
        FitParams wide = p;
        FitBitmap a, b;
        int gi = outline_glyph_index(ol, 'H');
        wide.aspect = 2;
        fit_render(ol, gi, &p, NULL, &a);
        fit_render(ol, gi, &wide, NULL, &b);
        CHECK(b.h == a.h && b.w >= 2 * a.w - 2 && b.w <= 2 * a.w + 2, "width 200 %%: twice as wide, as high (%d x %d -> %d x %d)", a.w, a.h, b.w, b.h);
        fit_bitmap_free(&a); fit_bitmap_free(&b);
    }
    {
        int *cp = NULL, n = fit_codepoints(ol, &cp), sorted = 1;
        for (int i = 1; i < n; i++) if (cp[2 * i] <= cp[2 * i - 2]) sorted = 0;
        CHECK(n >= 15 && sorted && cp[1] > 0, "the code points of the font: %d, ascending", n);
        free(cp);
    }
    outline_close(ol);
}

static void test_other_formats(void)
{
    static const char *files[] = {DIR "NotoSubCff.otf", DIR "NotoSub.woff", DIR "NotoSub.pfb", DIR "UnifontSub.otf"};
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char err[200] = "";
        Outline *ol = outline_open(files[i], err, sizeof err);
        CHECK(ol != NULL, "%s opens (%s)", files[i], err);
        if (!ol) continue;
        FitGrid g;
        FitParams p;
        int rc = fit_find_grid(ol, &g, err, sizeof err);
        if (strstr(files[i], "Unifont")) {
            CHECK(rc == 1 && g.sure && g.x == g.y, "GNU Unifont is a pixel font: a square grid (%g x %g, %s)", g.x, g.y, g.how);
            fit_grid_params(ol, &g, &p);
            CHECK(p.em == 16, "GNU Unifont is 16 px per em, got %g", p.em);
        } else {
            CHECK(rc == 0, "%s is no pixel font", files[i]);
            CHECK(fit_find_size(ol, NULL, &p, NULL, err, sizeof err) == 1 && p.em >= 6 && p.em <= 20, "%s: a small size is found (%g)", files[i], p.em);
        }
        if (rc == 1) fit_grid_free(&g);
        outline_close(ol);
    }
}

static void test_shape(void)
{
    char err[200] = "";
    Outline *ol = outline_open(DIR "PixelSquare.ttf", err, sizeof err);
    if (!ol) { CHECK(0, "PixelSquare.ttf opens"); return; }
    OutlineShape s;
    int gi = outline_glyph_index(ol, 'I');   /* three runs: the top bar, the stem (5 rows, one rectangle each), the bottom bar */
    CHECK(outline_glyph_shape(ol, gi, 8, &s, err, sizeof err) == OUTLINE_OK, "the shape of I (%s)", err);
    CHECK(s.nc == 7 && s.n == 28 && s.np == 28 && s.advance == 600, "I: 7 rectangles, 28 points, advance 600; got %d, %d, %d, %d", s.nc, s.n, s.np, s.advance);
    int curved = 0;
    for (int c = 0; c < s.nc; c++) curved += s.curved[c];
    CHECK(curved == 0 && s.start[0] == 0 && s.start[s.nc] == s.n && s.pstart[s.nc] == s.np, "I: no curves, the contours are told apart");
    outline_shape_free(&s);
    OutlineBitmap a, b;
    outline_rasterize_glyph(ol, gi, 10, 0, 0, &a, NULL, 0);
    outline_rasterize_glyph_xy(ol, gi, 20, 10, 0, 0, &b, NULL, 0);
    CHECK(b.w == 2 * a.w && b.h == a.h && fabsf(b.advance - 2 * a.advance) < 0.01f, "a stretched raster is twice as wide (%d x %d -> %d x %d)", a.w, a.h, b.w, b.h);
    outline_bitmap_free(&a); outline_bitmap_free(&b);
    char nm[64];
    CHECK(outline_name(ol, 1, nm, sizeof nm) > 0 && !strcmp(nm, "Pixel Square"), "the family name is read: '%s'", nm);
    CHECK(outline_name(ol, 13, nm, sizeof nm) == 0 && nm[0] == 0, "a name the font does not have is empty");
    outline_close(ol);
}

int main(void)
{
    load_refs();
    CHECK(nrefs == 33, "pixel_glyphs.txt holds 33 glyphs, got %d", nrefs);
    test_pixel_font(DIR "PixelSquare.ttf", "exact", 0, 0);
    test_pixel_font(DIR "PixelShift.ttf", "loose", 30, -20);
    test_pixel_font(DIR "PixelDots.ttf", "dots", 0, 0);
    test_smooth();
    test_other_formats();
    test_shape();
    printf("\ntest_fit: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
