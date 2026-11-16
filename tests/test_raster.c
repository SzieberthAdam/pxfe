/* test_raster.c - tests of src/raster.c (the paths of the glyph editor's line, curve and pencil tools).  build.bat test runs it. */
#include "raster.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

static bool has(const RasPath *r, int x, int y) { for (int i = 0; i < r->n; i++) if (r->p[i].x == x && r->p[i].y == y) return true; return false; }

static bool connected(const RasPath *r)
{
    for (int i = 1; i < r->n; i++)
        if (abs(r->p[i].x - r->p[i - 1].x) > 1 || abs(r->p[i].y - r->p[i - 1].y) > 1 || (r->p[i].x == r->p[i - 1].x && r->p[i].y == r->p[i - 1].y)) return false;
    return true;
}

static bool no_duplicates(const RasPath *r)
{
    for (int i = 0; i < r->n; i++) for (int j = i + 1; j < r->n; j++) if (r->p[i].x == r->p[j].x && r->p[i].y == r->p[j].y) return false;
    return true;
}

static bool no_corners(const RasPath *r)
{
    for (int i = 2; i < r->n; i++) if (ras_is_corner(r->p[i - 2], r->p[i - 1], r->p[i])) return false;
    return true;
}

static bool same_set(const RasPath *a, const RasPath *b)
{
    if (a->n != b->n) return false;
    for (int i = 0; i < a->n; i++) if (!has(b, a->p[i].x, a->p[i].y)) return false;
    return true;
}

static void test_line(void)
{
    RasPath r = {0};
    ras_line(&r, 0, 0, 5, 0);
    CHECK(r.n == 6 && r.p[0].x == 0 && r.p[5].x == 5 && connected(&r), "line: horizontal, 6 cells, ends included");
    ras_free(&r);
    ras_line(&r, 2, 2, 2, -3);
    CHECK(r.n == 6 && r.p[0].y == 2 && r.p[5].y == -3 && connected(&r), "line: vertical upward starts at the first end");
    ras_free(&r);
    ras_line(&r, 0, 0, 4, 4);
    CHECK(r.n == 5 && has(&r, 2, 2) && connected(&r), "line: diagonal, 5 cells");
    ras_free(&r);
    ras_line(&r, 3, 3, 3, 3);
    CHECK(r.n == 1 && r.p[0].x == 3, "line: a single cell");
    ras_free(&r);
    ras_line(&r, 0, 0, 6, 2);
    CHECK(r.n == 7 && r.p[0].x == 0 && r.p[0].y == 0 && r.p[6].x == 6 && r.p[6].y == 2 && connected(&r), "line: 3:1 slope has one cell per column");
    ras_free(&r);

    /* the end points and the order, in every direction */
    int ok = 1, sym = 1, conn = 1, corner = 1, count = 1;
    for (int a = -6; a <= 6; a++)
        for (int b = -6; b <= 6; b++) {
            RasPath f = {0}, g = {0};
            ras_line(&f, 1, -2, 1 + a, -2 + b);
            ras_line(&g, 1 + a, -2 + b, 1, -2);
            ok &= f.p[0].x == 1 && f.p[0].y == -2 && f.p[f.n - 1].x == 1 + a && f.p[f.n - 1].y == -2 + b;
            ok &= g.p[0].x == 1 + a && g.p[0].y == -2 + b && g.p[g.n - 1].x == 1 && g.p[g.n - 1].y == -2;
            sym &= same_set(&f, &g);
            conn &= connected(&f);
            corner &= no_corners(&f);
            count &= f.n == (abs(a) > abs(b) ? abs(a) : abs(b)) + 1;
            ras_free(&f); ras_free(&g);
        }
    CHECK(ok, "line: end points and order for 169 directions");
    CHECK(sym, "line: A to B and B to A cover the same cells (symmetry)");
    CHECK(conn, "line: always 8-connected");
    CHECK(corner, "line: never has an L corner");
    CHECK(count, "line: one cell per step of the long axis");
}

static void snapped(int dx, int dy, int ex, int ey, const char *name)
{
    int ox, oy;
    ras_snap(dx, dy, &ox, &oy);
    CHECK(ox == ex && oy == ey, name);
}

static void test_snap(void)
{
    snapped(10, 1, 10, 0, "snap: nearly horizontal goes to 0 degrees");
    snapped(-7, 0, -7, 0, "snap: horizontal to the left");
    snapped(1, 12, 0, 12, "snap: nearly vertical goes to 90 degrees");
    snapped(9, 10, 10, 10, "snap: 45 degrees");
    snapped(-5, -6, -6, -6, "snap: 45 degrees up and to the left");
    snapped(8, 4, 8, 4, "snap: exact 2:1 stays");
    snapped(9, 4, 8, 4, "snap: 2:1 is cut to whole steps");
    snapped(4, 8, 4, 8, "snap: 1:2");
    snapped(-8, 4, -8, 4, "snap: 2:1 to the left, downward");
    snapped(8, 2, 8, 2, "snap: 4:1");
    snapped(2, 8, 2, 8, "snap: 1:4");
    snapped(0, 0, 0, 0, "snap: no vector");
    int bad = 0;
    for (int dx = -20; dx <= 20; dx++)
        for (int dy = -20; dy <= 20; dy++) {
            int ox, oy;
            ras_snap(dx, dy, &ox, &oy);
            int ax = abs(ox), ay = abs(oy);
            bool clean = ay == 0 || ax == 0 || ax == ay || ax == 2 * ay || ay == 2 * ax || ax == 4 * ay || ay == 4 * ax;
            if (!clean) bad++;
            if (ox != 0 && (ox < 0) != (dx < 0)) bad++;
            if (oy != 0 && (oy < 0) != (dy < 0)) bad++;
        }
    CHECK(bad == 0, "snap: every result is one of the clean slopes, in the quadrant of the vector");

    int e[4];
    ras_ends(5, 5, 9, 7, false, false, e);
    CHECK(e[0] == 5 && e[1] == 5 && e[2] == 9 && e[3] == 7, "ends: plain");
    ras_ends(5, 5, 9, 7, false, true, e);
    CHECK(e[0] == 1 && e[1] == 3 && e[2] == 9 && e[3] == 7, "ends: from the centre mirrors the far end");
    ras_ends(5, 5, 17, 6, true, true, e);
    CHECK(e[0] == -7 && e[1] == 5 && e[2] == 17 && e[3] == 5, "ends: constrained and from the centre");
}

static void test_perfect(void)
{
    /* an L: (0,0) (1,0) (1,1): the corner (1,0) goes */
    RasPt l[] = {{0, 0}, {1, 0}, {1, 1}};
    CHECK(ras_is_corner(l[0], l[1], l[2]), "corner: an L is a corner");
    CHECK(ras_pixel_perfect(l, 3) == 2 && l[0].x == 0 && l[1].x == 1 && l[1].y == 1, "perfect: the L loses its middle cell");
    RasPt s[] = {{0, 0}, {1, 0}, {2, 0}};
    CHECK(!ras_is_corner(s[0], s[1], s[2]), "corner: a straight run is not one");
    RasPt d[] = {{0, 0}, {1, 1}, {2, 2}};
    CHECK(!ras_is_corner(d[0], d[1], d[2]), "corner: a diagonal is not one");
    RasPt t[] = {{0, 0}, {1, 0}, {1, 1}, {1, 2}};
    CHECK(ras_pixel_perfect(t, 4) == 3 && t[1].x == 1 && t[1].y == 1, "perfect: an L at the start of a run");
    RasPt h[] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}};
    CHECK(ras_pixel_perfect(h, 5) == 4 && h[2].x == 2 && h[2].y == 1, "perfect: a bend of two runs loses one cell");
    /* a staircase of Ls: (0,0) (1,0) (1,1) (2,1) (2,2) becomes the diagonal (0,0) (1,1) (2,2) */
    RasPt st[] = {{0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}};
    int n = ras_pixel_perfect(st, 5);
    CHECK(n == 3 && st[0].x == 0 && st[1].x == 1 && st[1].y == 1 && st[2].x == 2 && st[2].y == 2, "perfect: a staircase becomes a diagonal");
    RasPt dup[] = {{0, 0}, {0, 0}, {1, 0}, {1, 0}, {1, 1}};
    CHECK(ras_pixel_perfect(dup, 5) == 2, "perfect: repeated cells are dropped first");
    RasPt back[] = {{0, 0}, {1, 0}, {0, 0}};
    CHECK(ras_pixel_perfect(back, 3) == 3, "perfect: a step back is not an L");

    /* any 8-connected random walk: connected, no L, same ends */
    srand(7);
    int okc = 1, okl = 1, oke = 1;
    for (int trial = 0; trial < 200; trial++) {
        RasPath p = {0};
        int x = 0, y = 0;
        for (int m = 0; m < 60; m++) {
            ras_add(&p, x, y);
            x += rand() % 3 - 1;
            y += rand() % 3 - 1;
        }
        int n0x = p.p[0].x, n0y = p.p[0].y, lx = p.p[p.n - 1].x, ly = p.p[p.n - 1].y;
        p.n = ras_pixel_perfect(p.p, p.n);
        okc &= connected(&p);
        okl &= no_corners(&p);
        oke &= p.p[0].x == n0x && p.p[0].y == n0y && p.p[p.n - 1].x == lx && p.p[p.n - 1].y == ly;
        ras_free(&p);
    }
    CHECK(okc, "perfect: random walks stay connected");
    CHECK(okl, "perfect: random walks have no L corner");
    CHECK(oke, "perfect: the ends of a walk stay");
}

static void test_bezier(void)
{
    RasPath r = {0};
    RasPt a = {0, 0}, b = {10, 0};
    ras_bezier(&r, a, a, b, b, true);
    CHECK(r.n == 11 && r.p[0].x == 0 && r.p[10].x == 10 && connected(&r) && no_duplicates(&r), "bezier: control points on the ends make the straight line");
    ras_free(&r);

    ras_bezier(&r, (RasPt){0, 0}, (RasPt){0, 12}, (RasPt){12, 12}, (RasPt){12, 0}, true);
    CHECK(r.p[0].x == 0 && r.p[0].y == 0 && r.p[r.n - 1].x == 12 && r.p[r.n - 1].y == 0, "bezier: an arch starts and ends on its end points");
    CHECK(connected(&r), "bezier: the arch is 8-connected, no gap");
    CHECK(no_duplicates(&r), "bezier: the arch has no cell twice");
    CHECK(no_corners(&r), "bezier: the arch has no L corner");
    int top = 0;
    for (int i = 0; i < r.n; i++) if (r.p[i].y > top) top = r.p[i].y;
    CHECK(top >= 8 && top <= 10, "bezier: the arch rises to 3/4 of the control height");
    ras_free(&r);

    RasPath raw = {0}, fine = {0};
    ras_bezier(&raw, (RasPt){0, 0}, (RasPt){0, 12}, (RasPt){12, 12}, (RasPt){12, 0}, false);
    ras_bezier(&fine, (RasPt){0, 0}, (RasPt){0, 12}, (RasPt){12, 12}, (RasPt){12, 0}, true);
    CHECK(connected(&raw) && raw.n >= fine.n, "bezier: the raw path is connected and the rule only removes cells");
    ras_free(&raw); ras_free(&fine);

    ras_bezier(&r, (RasPt){5, 5}, (RasPt){-10, 20}, (RasPt){25, -10}, (RasPt){-5, 3}, true);
    CHECK(connected(&r) && no_corners(&r), "bezier: an S curve is connected and has no L corner");
    ras_free(&r);
    ras_bezier(&r, (RasPt){3, 3}, (RasPt){3, 3}, (RasPt){3, 3}, (RasPt){3, 3}, true);
    CHECK(r.n == 1, "bezier: all four points the same is one cell");
    ras_free(&r);

    srand(11);
    int okc = 1, oke = 1, okl = 1, okr = 1;
    for (int trial = 0; trial < 300; trial++) {
        RasPt q[4];
        for (int i = 0; i < 4; i++) { q[i].x = rand() % 61 - 30; q[i].y = rand() % 61 - 30; }
        RasPath f = {0}, g = {0};
        ras_bezier(&f, q[0], q[1], q[2], q[3], true);
        ras_bezier(&g, q[0], q[1], q[2], q[3], false);
        okc &= connected(&f) && connected(&g);
        oke &= f.p[0].x == q[0].x && f.p[0].y == q[0].y && f.p[f.n - 1].x == q[3].x && f.p[f.n - 1].y == q[3].y;
        okl &= no_corners(&f);
        okr &= f.n <= g.n;
        ras_free(&f); ras_free(&g);
    }
    CHECK(okc, "bezier: 300 random curves are 8-connected, perfect or not");
    CHECK(oke, "bezier: 300 random curves keep their end points");
    CHECK(okl, "bezier: 300 random curves have no L corner");
    CHECK(okr, "bezier: the rule never adds cells");
}


static int ring_nbrs(const RasPath *r, int x, int y)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if ((dx || dy) && has(r, x + dx, y + dy)) n++;
    return n;
}

/* one 8-connected piece: a flood fill from the first cell reaches all cells */
static bool one_piece(const RasPath *r)
{
    if (r->n == 0) return false;
    char *seen = (char *)calloc((size_t)r->n, 1);
    int *stack = (int *)malloc(sizeof(int) * (size_t)r->n), sp = 0, got = 1;
    seen[0] = 1; stack[sp++] = 0;
    while (sp > 0) {
        int a = stack[--sp];
        for (int b = 0; b < r->n; b++)
            if (!seen[b] && abs(r->p[a].x - r->p[b].x) <= 1 && abs(r->p[a].y - r->p[b].y) <= 1) { seen[b] = 1; got++; stack[sp++] = b; }
    }
    free(seen); free(stack);
    return got == r->n;
}

static void test_box_rect(void)
{
    int b[4];
    ras_box(2, 3, 6, 5, false, false, b);
    CHECK(b[0] == 2 && b[1] == 3 && b[2] == 6 && b[3] == 5, "box: corner to corner");
    ras_box(6, 5, 2, 3, false, false, b);
    CHECK(b[0] == 2 && b[1] == 3 && b[2] == 6 && b[3] == 5, "box: dragged up and to the left is the same box");
    ras_box(2, 3, 6, 5, true, false, b);
    CHECK(b[0] == 2 && b[1] == 3 && b[2] == 6 && b[3] == 7, "box: square takes the longer side (down)");
    ras_box(6, 5, 4, 0, true, false, b);
    CHECK(b[0] == 1 && b[1] == 0 && b[2] == 6 && b[3] == 5, "box: square up and to the left");
    ras_box(4, 4, 4, 4, true, false, b);
    CHECK(b[0] == 4 && b[2] == 4 && b[1] == 4 && b[3] == 4, "box: a click is one cell");
    ras_box(4, 4, 6, 5, false, true, b);
    CHECK(b[0] == 2 && b[1] == 3 && b[2] == 6 && b[3] == 5, "box: from the centre mirrors the far corner");
    ras_box(4, 4, 6, 5, true, true, b);
    CHECK(b[0] == 2 && b[1] == 2 && b[2] == 6 && b[3] == 6, "box: square from the centre");

    RasPath r = {0};
    ras_rect(&r, 0, 0, 4, 3, false);
    CHECK(r.n == 14 && no_duplicates(&r) && !has(&r, 2, 1) && has(&r, 0, 1) && has(&r, 4, 2), "rect: a 5 x 4 outline has 14 cells and an empty inside");
    ras_free(&r);
    ras_rect(&r, 0, 0, 4, 3, true);
    CHECK(r.n == 20 && has(&r, 2, 1), "rect: filled has all 20 cells");
    ras_free(&r);
    ras_rect(&r, 1, 1, 1, 5, false);
    CHECK(r.n == 5 && no_duplicates(&r), "rect: one cell wide is a line");
    ras_free(&r);
    ras_rect(&r, 3, 3, 3, 3, false);
    CHECK(r.n == 1, "rect: one cell");
    ras_free(&r);
}

/* the properties of an outline in the box 0..w-1 by 0..h-1; returns a bit set of the ones that fail: 1 duplicate, 2 not symmetric or outside, 4 misses a side,
 * 8 not one piece, 16 not a ring (from 5 x 5 and up to 3:1: every cell has exactly two neighbours; thinner boxes have two-cell caps), 32 an L corner (from 3 x 3) */
static int ring_faults(int w, int h)
{
    RasPath r = {0};
    int bad = 0;
    ras_ellipse(&r, 0, 0, w - 1, h - 1, false);
    bool sym = true, tl = false, tr = false, tt = false, tb = false;
    for (int i = 0; i < r.n; i++) {
        int x = r.p[i].x, y = r.p[i].y;
        if (x < 0 || x >= w || y < 0 || y >= h || !has(&r, w - 1 - x, y) || !has(&r, x, h - 1 - y)) sym = false;
        tl |= x == 0; tr |= x == w - 1; tt |= y == 0; tb |= y == h - 1;
    }
    if (!no_duplicates(&r)) bad |= 1;
    if (!sym) bad |= 2;
    if (!(tl && tr && tt && tb)) bad |= 4;
    if (!one_piece(&r)) bad |= 8;
    int mn = w < h ? w : h, mx = w < h ? h : w;
    for (int i = 0; i < r.n && mn >= 3; i++) {
        int x = r.p[i].x, y = r.p[i].y, nb = ring_nbrs(&r, x, y);
        bool corner = nb == 2 && (has(&r, x - 1, y) || has(&r, x + 1, y)) && (has(&r, x, y - 1) || has(&r, x, y + 1));
        if (corner) bad |= 32;
        if (mn >= 5 && mx <= 3 * mn && nb != 2) bad |= 16;
    }
    ras_free(&r);
    return bad;
}

static void test_ellipse(void)
{
    int bad = 0, bad_line = 0, bad_subset = 0, bad_hole = 0, tried = 0, first = 0;
    for (int w = 1; w <= 12; w++) {
        for (int h = 1; h <= 12; h++) {
            RasPath r = {0}, f = {0};
            int x0 = -3, y0 = 5, x1 = x0 + w - 1, y1 = y0 + h - 1;
            int rf = ring_faults(w, h);
            if (rf && !first) { first = rf; printf("      first fault %d at %d x %d\n", rf, w, h); }
            bad |= rf;
            tried++;
            ras_ellipse(&r, x0, y0, x1, y1, false);
            ras_ellipse(&f, x0, y0, x1, y1, true);
            if (w == 1 || h == 1) { if (r.n != w * h || f.n != w * h) bad_line++; }
            bool sub = f.n >= r.n && no_duplicates(&f);
            for (int i = 0; i < r.n; i++) if (!has(&f, r.p[i].x, r.p[i].y)) sub = false;
            if (!sub) bad_subset++;
            for (int y = y0; y <= y1; y++) {   /* filled: every row is one run, so no holes */
                int lo = 1 << 30, hi = -(1 << 30), cnt = 0;
                for (int i = 0; i < f.n; i++) if (f.p[i].y == y) { cnt++; if (f.p[i].x < lo) lo = f.p[i].x; if (f.p[i].x > hi) hi = f.p[i].x; }
                if (cnt == 0 || cnt != hi - lo + 1) bad_hole++;
            }
            ras_free(&r); ras_free(&f);
        }
    }
    CHECK(tried == 144, "ellipse: sizes 1..12 by 1..12 were all tried");
    CHECK(!(bad & 1), "ellipse: no cell twice (all sizes 1..12 in both axes)");
    CHECK(!(bad & 2), "ellipse: symmetric in both axes and inside the box (all sizes)");
    CHECK(!(bad & 4), "ellipse: the ring touches the box on all four sides (all sizes)");
    CHECK(!(bad & 8), "ellipse: one 8-connected piece (all sizes)");
    CHECK(!(bad & 16), "ellipse: from 5 x 5 up to 3:1 every ring cell has exactly two neighbours (closed, one cell thick)");
    CHECK(!(bad & 32), "ellipse: from 3 x 3 up no cell is the middle of an L corner");
    CHECK(bad_line == 0, "ellipse: a box one cell wide or high is a line");
    CHECK(bad_subset == 0, "ellipse: filled contains the ring");
    CHECK(bad_hole == 0, "ellipse: filled has no holes: every row is one run");

    RasPath r = {0};
    ras_ellipse(&r, 0, 0, 1, 1, false);
    CHECK(r.n == 4, "ellipse: 2 x 2 is the four cells");
    ras_free(&r);
    ras_ellipse(&r, 0, 0, 2, 2, false);
    CHECK(r.n == 4 && !has(&r, 0, 0) && has(&r, 1, 0) && has(&r, 0, 1) && has(&r, 2, 1) && has(&r, 1, 2), "ellipse: 3 x 3 is the diamond");
    ras_free(&r);
    ras_ellipse(&r, 0, 0, 2, 2, true);
    CHECK(r.n == 5 && has(&r, 1, 1), "ellipse: 3 x 3 filled is the diamond and its middle");
    ras_free(&r);
    ras_ellipse(&r, 0, 0, 7, 1, false);
    CHECK(r.n == 16, "ellipse: 8 x 2 is a solid band");
    ras_free(&r);

    /* a circle: round, one cell thick */
    ras_ellipse(&r, 0, 0, 20, 20, false);
    int ok = no_duplicates(&r) && one_piece(&r);
    for (int i = 0; i < r.n; i++) {
        double dx = r.p[i].x - 10.0, dy = r.p[i].y - 10.0, d = sqrt(dx * dx + dy * dy);
        if (d < 9.0 || d > 11.0) ok = 0;
    }
    CHECK(ok && has(&r, 10, 0) && has(&r, 10, 20) && has(&r, 0, 10) && has(&r, 20, 10) && !has(&r, 10, 10), "ellipse: a 21 x 21 circle is round and touches all four sides");
    ras_free(&r);

    srand(5);   /* random boxes up to 60 cells: the same properties */
    int okr = 1;
    for (int t = 0; t < 200; t++) if (ring_faults(1 + rand() % 60, 1 + rand() % 60)) okr = 0;
    CHECK(okr, "ellipse: 200 random boxes up to 60 x 60 are symmetric, connected, without L corners and touch the box");
}

/* ------------------------------------------------------------------ fill and polygon (version 20) */

static int count_in(const RasPath *r, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int i = 0; i < r->n; i++) if (r->p[i].x >= x0 && r->p[i].x <= x1 && r->p[i].y >= y0 && r->p[i].y <= y1) n++;
    return n;
}

static void test_flood(void)
{
    /* 6 x 4 cells with the top-left cell at (-1, 2): columns -1..4, rows 2..5; '#' is a wall of another character */
    const char *pix =
        "..#..."
        "..#.#."
        "###.#."
        "..#.#.";
    RasPath r = {0};
    CHECK(ras_flood(&r, pix, -1, 2, 6, 4, -1, 2, true, false, NULL) && r.n == 4 && has(&r, -1, 2) && has(&r, 0, 3) && !has(&r, 1, 2), "flood: contiguous fills the room of the seed");
    CHECK(r.p[0].x == -1 && r.p[0].y == 2 && r.p[3].x == 0 && r.p[3].y == 3, "flood: the cells come in row order");
    ras_free(&r);
    ras_flood(&r, pix, -1, 2, 6, 4, 2, 2, true, false, NULL);
    CHECK(r.n == 9 && has(&r, 4, 2) && has(&r, 4, 5) && has(&r, 2, 5) && !has(&r, 3, 3) && !has(&r, -1, 5), "flood: the other room is a path of 9 cells around the wall");
    ras_free(&r);

    /* a diagonal wall: 4-way stops, 8-way slips through */
    const char *dg =
        "..#"
        ".#."
        "#..";
    ras_flood(&r, dg, 0, 0, 3, 3, 0, 0, true, false, NULL);
    CHECK(r.n == 3 && has(&r, 0, 0) && has(&r, 1, 0) && has(&r, 0, 1) && !has(&r, 2, 1), "flood: 4-way keeps to one side of a diagonal wall");
    ras_free(&r);
    ras_flood(&r, dg, 0, 0, 3, 3, 0, 0, true, true, NULL);
    CHECK(r.n == 6 && has(&r, 2, 1) && has(&r, 2, 2) && has(&r, 1, 2), "flood: 8-way slips through the diagonal gap (6 cells)");
    ras_free(&r);

    /* not contiguous: every cell of that character */
    ras_flood(&r, pix, -1, 2, 6, 4, -1, 2, false, false, NULL);
    CHECK(r.n == 15 && has(&r, 4, 5) && has(&r, -1, 5) && !has(&r, 1, 2), "flood: global fill takes every '.' of the region (15)");
    ras_free(&r);
    ras_flood(&r, pix, -1, 2, 6, 4, 1, 2, false, false, NULL);
    CHECK(r.n == 9 && has(&r, -1, 4) && has(&r, 3, 5), "flood: global fill of a wall character takes all 9");
    ras_free(&r);

    /* the clip mask (columns -1..1) is a wall and a limit */
    unsigned char clip[24];
    memset(clip, 0, sizeof clip);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 3; x++) clip[y * 6 + x] = 1;
    ras_flood(&r, pix, -1, 2, 6, 4, -1, 2, true, false, clip);
    CHECK(r.n == 4, "flood: a clip that holds the room changes nothing");
    ras_free(&r);
    clip[0 * 6 + 1] = 0;   /* the cell (0, 2) leaves the clip */
    clip[1 * 6 + 1] = 0;   /* and (0, 3) */
    ras_flood(&r, pix, -1, 2, 6, 4, -1, 2, true, false, clip);
    CHECK(r.n == 2 && has(&r, -1, 2) && has(&r, -1, 3), "flood: contiguous, the clip is a wall");
    ras_free(&r);
    ras_flood(&r, pix, -1, 2, 6, 4, 0, 2, true, false, clip);
    CHECK(r.n == 0, "flood: a seed outside the clip fills nothing");
    ras_free(&r);
    ras_flood(&r, pix, -1, 2, 6, 4, -1, 2, false, false, clip);
    CHECK(r.n == 4 && has(&r, -1, 5) && has(&r, 0, 5) && count_in(&r, 2, 2, 4, 5) == 0, "flood: global fill is limited to the clip too (4 cells)");
    ras_free(&r);

    /* the seed outside the region fills nothing; an empty region (the growth region of the editor: the box and the rows above the baseline) fills whole */
    CHECK(ras_flood(&r, pix, -1, 2, 6, 4, 9, 9, true, false, NULL) && r.n == 0, "flood: a seed outside the region fills nothing");
    ras_free(&r);
    char empty[5 * 3];
    memset(empty, '.', sizeof empty);
    ras_flood(&r, empty, 0, -2, 5, 3, 2, -1, true, false, NULL);
    CHECK(r.n == 15 && has(&r, 0, -2) && has(&r, 4, 0), "flood: an empty region is filled whole, rows above the baseline included");
    ras_free(&r);
    ras_flood(&r, empty, 0, -2, 5, 3, 2, -1, false, true, NULL);
    CHECK(r.n == 15, "flood: global and 8-way fill an empty region whole");
    ras_free(&r);
}

/* the textbook even-odd test by a ray to the right of the cell centre */
static bool inside_naive(const RasPt *v, int n, int x, int y)
{
    double px = x + 0.5, py = y + 0.5;
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        double ax = v[i].x + 0.5, ay = v[i].y + 0.5, bx = v[j].x + 0.5, by = v[j].y + 0.5;
        if ((ay > py) != (by > py) && px < (bx - ax) * (py - ay) / (by - ay) + ax) in = !in;
    }
    return in;
}

static int inside_missing(const RasPath *r, const RasPt *v, int n, int w, int h)
{
    int bad = 0;
    for (int y = -10; y <= h; y++) for (int x = -10; x <= w; x++) if (inside_naive(v, n, x, y) && !has(r, x, y)) bad++;
    return bad;
}

static void test_polygon(void)
{
    RasPath r = {0};
    RasPt tri[3] = {{0, 0}, {8, 0}, {0, 8}};
    ras_polygon(&r, tri, 3, true, false);
    CHECK(r.n == 24 && has(&r, 0, 0) && has(&r, 8, 0) && has(&r, 0, 8) && has(&r, 4, 4) && !has(&r, 2, 2) && connected(&r), "polygon: a triangle's outline is three joined lines (24 cells)");
    ras_free(&r);
    ras_polygon(&r, tri, 3, false, false);
    CHECK(has(&r, 8, 0) && has(&r, 0, 8) && !has(&r, 0, 4) && connected(&r), "polygon: open, the closing edge is not drawn");
    ras_free(&r);
    ras_polygon(&r, tri, 3, true, true);
    CHECK(has(&r, 2, 2) && has(&r, 1, 1) && has(&r, 4, 3) && !has(&r, 7, 7) && !has(&r, 5, 5) && no_duplicates(&r), "polygon: a filled triangle holds its inside, no cell twice");
    CHECK(inside_missing(&r, tri, 3, 12, 12) == 0, "polygon: every cell whose centre is inside is filled");
    ras_free(&r);

    /* convex: the vertices of a rectangle fill its box, with sharp corners */
    RasPt sq[4] = {{1, 1}, {5, 1}, {5, 4}, {1, 4}};
    ras_polygon(&r, sq, 4, true, true);
    CHECK(r.n == 5 * 4 && has(&r, 1, 1) && has(&r, 5, 4) && has(&r, 5, 1) && has(&r, 1, 4), "polygon: a rectangle's vertices fill its whole box (20 cells)");
    ras_free(&r);
    ras_polygon(&r, sq, 4, true, false);
    CHECK(r.n == 14 && !has(&r, 3, 2), "polygon: its outline is the 14 border cells");
    ras_free(&r);

    /* concave: a U is not filled across its dent */
    RasPt u[8] = {{0, 0}, {2, 0}, {2, 3}, {4, 3}, {4, 0}, {6, 0}, {6, 5}, {0, 5}};
    ras_polygon(&r, u, 8, true, true);
    CHECK(!has(&r, 3, 1) && !has(&r, 3, 2) && has(&r, 3, 3) && has(&r, 1, 1) && has(&r, 5, 1) && has(&r, 3, 4), "polygon: a concave outline is not filled across the dent");
    CHECK(inside_missing(&r, u, 8, 12, 12) == 0, "polygon: the concave inside is complete");
    ras_free(&r);

    /* self-intersecting: the bow tie fills two wings, the pentagram leaves its middle empty (even-odd) */
    RasPt bow[4] = {{0, 0}, {8, 8}, {8, 0}, {0, 8}};
    ras_polygon(&r, bow, 4, true, true);
    CHECK(has(&r, 1, 4) && has(&r, 7, 4) && has(&r, 7, 1) && has(&r, 0, 6) && has(&r, 4, 4) && !has(&r, 4, 1) && !has(&r, 4, 2) && !has(&r, 4, 7), "polygon: a bow tie fills its two wings and not the gaps above and below");
    ras_free(&r);
    RasPt star[5] = {{10, 0}, {16, 18}, {1, 7}, {19, 7}, {4, 18}};
    ras_polygon(&r, star, 5, true, true);
    CHECK(!has(&r, 10, 9) && has(&r, 10, 3) && has(&r, 5, 8) && has(&r, 15, 8) && has(&r, 7, 15), "polygon: a pentagram's middle stays empty (even-odd), its points are filled");
    CHECK(inside_missing(&r, star, 5, 20, 20) == 0, "polygon: the pentagram agrees with the even-odd count");
    ras_free(&r);

    /* degenerate: a point, two points, collinear, repeated, none */
    RasPt one[1] = {{3, 3}};
    ras_polygon(&r, one, 1, true, true);
    CHECK(r.n == 1 && has(&r, 3, 3), "polygon: one vertex is one cell");
    ras_free(&r);
    RasPt two[2] = {{0, 0}, {5, 2}};
    RasPath ln = {0};
    ras_polygon(&r, two, 2, true, true);
    ras_line(&ln, 0, 0, 5, 2);
    CHECK(r.n == ln.n && same_set(&r, &ln), "polygon: two vertices are a line, closed and filled or not");
    ras_free(&r); ras_free(&ln);
    RasPt col[4] = {{0, 0}, {3, 3}, {6, 6}, {2, 2}};
    ras_polygon(&r, col, 4, true, true);
    CHECK(r.n == 7 && has(&r, 0, 0) && has(&r, 6, 6) && has(&r, 3, 3), "polygon: collinear vertices are a line (7 cells), nothing is filled");
    ras_free(&r);
    RasPt rep[3] = {{2, 2}, {2, 2}, {2, 2}};
    ras_polygon(&r, rep, 3, true, true);
    CHECK(r.n == 1, "polygon: repeated vertices are one cell");
    ras_free(&r);
    CHECK(ras_polygon(&r, one, 0, true, true) && r.n == 0, "polygon: no vertices, no cells");

    /* a sliver keeps its outline; negative coordinates (above the baseline, left of the pen) work */
    RasPt sl[3] = {{0, 0}, {9, 1}, {0, 1}};
    ras_polygon(&r, sl, 3, true, true);
    CHECK(has(&r, 0, 0) && has(&r, 9, 1) && has(&r, 0, 1) && r.n >= 12, "polygon: a thin sliver keeps its outline");
    ras_free(&r);
    RasPt neg[3] = {{-4, -6}, {2, -6}, {-1, 0}};
    ras_polygon(&r, neg, 3, true, true);
    CHECK(has(&r, -4, -6) && has(&r, 2, -6) && has(&r, -1, 0) && has(&r, -1, -4) && !has(&r, -4, -1), "polygon: negative coordinates work");
    ras_free(&r);
}

int main(void)
{
    test_line();
    test_snap();
    test_perfect();
    test_bezier();
    test_box_rect();
    test_ellipse();
    test_flood();
    test_polygon();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
