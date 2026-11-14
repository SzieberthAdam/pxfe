/* fit.c - fitting an outline font to a pixel grid.  See fit.h.
 *
 * The grid search is a port of pixelglyphlib/readers/ttf.py; the names of the functions and of the constants are kept so that the two
 * can be read side by side.  Where the Python text counts things in a Counter, the order of first insertion decides between equal
 * counts, so Ctr below keeps that order. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fit.h"

#define GRID_TOLERANCE 0.1          /* how far from the grid a coordinate may stray, in cells */
#define GRID_MATCH 0.95             /* how much of the ink a coarser grid must redraw */
#define MAX_EM_CELLS 256            /* a common divisor finer than this many cells per em is no grid by itself */
#define SEARCH_EM_CELLS 128         /* how finely the deep search redraws a font */
#define SEARCH_EDGE_LENGTHS 8       /* how many of the commonest edge lengths the deep search tries as grids */
#define MIN_EM_CELLS 4              /* a coarser grid draws nothing but blocks, however well it matches */
#define SEARCH_MAX_EM_CELLS 64      /* a finer lattice is no pixel grid */
#define SEARCH_EDGE_SLIP 0.06       /* how far an edge may lie from a lattice line, in cells */
#define SEARCH_MIN_ON_LATTICE 0.6   /* how many of the edges a lattice must hold */
#define SEARCH_MAX_PAIRS 30         /* how many pairs of lattices are redrawn */
#define SEARCH_CELL_PIXELS 12       /* how many fine pixels the smaller side of a cell is redrawn with */
#define SEARCH_MAX_ASPECT 1.7       /* how far from square a pixel may be */
#define SEARCH_MAX_BUFFER 0.25      /* how large a pixel may grow on both sides together, in cells */
#define DOT_MIN_COUNT 20            /* so many separate contours make a font of dots */
#define DOT_MIN_SHARE 0.9           /* how many of the contours must be dots of one size */
#define DOT_MAX_HOLES 0.05          /* how many of the contours may be holes */
#define DOT_SIZE_SLIP 0.1           /* how far a dot may differ from the commonest size */
#define DOT_ON_LATTICE 0.97         /* how many of the dots' centres a lattice must hold */
#define SEARCH_SAMPLE_MATCH 0.85    /* how well the probe glyphs must match for the whole font to be tried */
#define SEARCH_MIN_PROBE 20         /* with fewer ASCII glyphs, the probe is filled up with the first glyphs of the font */
#define SEARCH_PROBE_COUNT 94
#define GLYPH_PHASE_GAIN 0.01       /* how much better the probe glyphs must match on cells of their own */
#define CORE_SAMPLES 5              /* how many points of a cell's core are looked at along each axis */
#define CORE_SLIP 0.1               /* how much of a cell's edge a careless hand may slip over, in cells */
#define CORE_MATCH 0.94             /* how much of the cores must agree with the centres of their cells */
#define CORE_PITCH_SLIP 0.25        /* how far the commonest edge may differ from a cell, in cells */
#define GUESS_MATCH 0.80            /* pxfe: a lattice that redraws this much is offered as a guess when nothing passes */

#define INK_MAX_DIM 8192
#define INK_MAX_PIX (48L * 1000 * 1000)

static void setmsg(char *msg, int n, const char *text) { if (msg && n > 0) snprintf(msg, (size_t)n, "%s", text); }

static double pmod(double a, double m) { double r = a - m * floor(a / m); return (r < 0 || r >= m) ? 0 : r; }   /* Python's % */
static double signed_res(double v, double pitch) { v = pmod(v, pitch); return v > pitch / 2 ? v - pitch : v; }
static long igcd(long a, long b) { if (a < 0) a = -a; if (b < 0) b = -b; while (b) { long t = a % b; a = b; b = t; } return a; }
static double round3(double v) { return rint(v * 1000.0) / 1000.0; }
static double round1(double v) { return rint(v * 10.0) / 10.0; }

/* ------------------------------------------------------------------ a counter that remembers the order of insertion */

typedef struct Ctr { int *k; long *c; int n, cap; int *h; int hn; } Ctr;

static void ctr_free(Ctr *t) { free(t->k); free(t->c); free(t->h); memset(t, 0, sizeof *t); }

static int ctr_slot(const Ctr *t, int key)
{
    unsigned u = (unsigned)key * 2654435761u;
    int i = (int)(u % (unsigned)t->hn);
    while (t->h[i] && t->k[t->h[i] - 1] != key) i = (i + 1) % t->hn;
    return i;
}

static int ctr_find(const Ctr *t, int key) { return t->hn ? t->h[ctr_slot(t, key)] - 1 : -1; }
static long ctr_get(const Ctr *t, int key) { int i = ctr_find(t, key); return i < 0 ? 0 : t->c[i]; }

static void ctr_add(Ctr *t, int key, long n)
{
    if ((t->n + 1) * 2 > t->hn) {
        int nh = t->hn ? t->hn * 2 : 64;
        int *h = (int *)calloc((size_t)nh, sizeof(int));
        if (!h) return;
        free(t->h);
        t->h = h;
        t->hn = nh;
        for (int i = 0; i < t->n; i++) t->h[ctr_slot(t, t->k[i])] = i + 1;
    }
    int s = ctr_slot(t, key);
    if (t->h[s]) { t->c[t->h[s] - 1] += n; return; }
    if (t->n >= t->cap) {
        int nc = t->cap ? t->cap * 2 : 32;
        int *k = (int *)realloc(t->k, (size_t)nc * sizeof(int));
        if (k) t->k = k;
        long *c = (long *)realloc(t->c, (size_t)nc * sizeof(long));
        if (c) t->c = c;
        if (!k || !c) return;
        t->cap = nc;
    }
    t->k[t->n] = key;
    t->c[t->n] = n;
    t->h[s] = ++t->n;
}

static long ctr_total(const Ctr *t) { long s = 0; for (int i = 0; i < t->n; i++) s += t->c[i]; return s; }

/* the indices of the `max` commonest keys, the commonest first, equal counts in the order of insertion; returns how many */
static int ctr_top(const Ctr *t, int *idx, int max)
{
    int m = 0;
    for (int i = 0; i < t->n; i++) {
        int j = m < max ? m : max;
        if (j == max && t->c[idx[max - 1]] >= t->c[i]) continue;
        while (j > 0 && t->c[idx[j - 1]] < t->c[i]) { if (j < max) idx[j] = idx[j - 1]; j--; }
        idx[j] = i;
        if (m < max) m++;
    }
    return m;
}

/* ------------------------------------------------------------------ the font: its glyphs with code points */

typedef struct FG {
    int gid, uses, cp0;        /* glyph index, how many code points name it, the first of them */
    OutlineShape s;
    long gcd;                  /* of its coordinates, 0: none (no points, or all 0) */
    int ascii;                 /* a printable ASCII code point names it */
} FG;

typedef struct FF { const Outline *ol; double upem; FG *g; int n; } FF;

static void ff_free(FF *f)
{
    for (int i = 0; i < f->n; i++) outline_shape_free(&f->g[i].s);
    free(f->g);
    memset(f, 0, sizeof *f);
}

int fit_codepoints(const Outline *ol, int **pairs)
{
    int n = 0, cap = 256;
    int *v = (int *)malloc(sizeof(int) * 2 * (size_t)cap);
    *pairs = NULL;
    if (!v) return -1;
    for (int cp = 0; cp <= 0x10FFFF; cp++) {
        if (cp == 0xD800) cp = 0xE000;
        int g = outline_glyph_index(ol, cp);
        if (g <= 0) continue;
        if (n >= cap) {
            cap *= 2;
            int *nv = (int *)realloc(v, sizeof(int) * 2 * (size_t)cap);
            if (!nv) { free(v); return -1; }
            v = nv;
        }
        v[2 * n] = cp;
        v[2 * n + 1] = g;
        n++;
    }
    *pairs = v;
    return n;
}

static int ff_load(FF *f, const Outline *ol)
{
    memset(f, 0, sizeof *f);
    f->ol = ol;
    f->upem = outline_units_per_em(ol);
    if (f->upem <= 0) return -1;
    int *cp = NULL;
    int ncp = fit_codepoints(ol, &cp);
    if (ncp < 0) return -1;
    int ng = outline_glyph_count(ol);
    int *map = (int *)malloc(sizeof(int) * (size_t)(ng > 0 ? ng : 1));
    f->g = (FG *)calloc((size_t)(ncp > 0 ? ncp : 1), sizeof(FG));
    if (!map || !f->g) { free(map); free(cp); return -1; }
    for (int i = 0; i < ng; i++) map[i] = -1;
    for (int i = 0; i < ncp; i++) {
        int c = cp[2 * i], g = cp[2 * i + 1];
        if (g >= ng) continue;
        if (map[g] < 0) {
            FG *e = &f->g[f->n];
            e->gid = g;
            e->cp0 = c;
            if (outline_glyph_shape(ol, g, 8, &e->s, NULL, 0) != OUTLINE_OK) memset(&e->s, 0, sizeof e->s);
            long d = 0;
            for (int k = 0; k < e->s.np; k++) { d = igcd(d, (long)rint(e->s.px[k])); d = igcd(d, (long)rint(e->s.py[k])); }
            e->gcd = d;
            map[g] = f->n++;
        }
        FG *e = &f->g[map[g]];
        e->uses++;
        if (c >= 0x21 && c < 0x7F) e->ascii = 1;
    }
    free(map);
    free(cp);
    return 0;
}

static int has_coords(const FG *g) { return g->s.np > 0; }

/* the probe glyphs: the printable ASCII ones with points, filled up with the first glyphs by code point when there are few */
static int probe_glyphs(const FF *f, int *out)
{
    int n = 0;
    for (int i = 0; i < f->n; i++) if (f->g[i].ascii && has_coords(&f->g[i])) out[n++] = i;
    if (n < SEARCH_MIN_PROBE) {
        for (int i = 0; i < f->n && n < SEARCH_PROBE_COUNT; i++) if (!f->g[i].ascii && has_coords(&f->g[i])) out[n++] = i;
    }
    return n;
}

/* ------------------------------------------------------------------ edges */

typedef void (*EdgeFn)(void *u, double x0, double y0, double x1, double y1);

static void each_edge(const OutlineShape *s, EdgeFn fn, void *u)
{
    for (int c = 0; c < s->nc; c++) {
        int a = s->start[c], b = s->start[c + 1];
        if (b - a < 1) continue;
        for (int i = a; i + 1 < b; i++) fn(u, s->x[i], s->y[i], s->x[i + 1], s->y[i + 1]);
        if (s->x[b - 1] != s->x[a] || s->y[b - 1] != s->y[a]) fn(u, s->x[b - 1], s->y[b - 1], s->x[a], s->y[a]);
    }
}

/* ------------------------------------------------------------------ ink: the pixels whose centre is inside the glyph */

typedef struct Ink { int x0, y0, w, h; unsigned char *b; long n; } Ink;   /* row r of b is the pixel row y0 + r, y up */

static void ink_free(Ink *k) { free(k->b); memset(k, 0, sizeof *k); }
static int ink_at(const Ink *k, long x, long y)
{
    x -= k->x0; y -= k->y0;
    return x >= 0 && y >= 0 && x < k->w && y < k->h && k->b[(size_t)y * (size_t)k->w + (size_t)x];
}

typedef struct Cross { int row; double x; int dir; } Cross;
typedef struct Scan { Cross *v; int n, cap, bad; double ux, uy, ox, oy; } Scan;

static void scan_edge(void *u, double x0, double y0, double x1, double y1)
{
    Scan *s = (Scan *)u;
    if (y0 == y1 || s->bad) return;
    x0 -= s->ox; x1 -= s->ox; y0 -= s->oy; y1 -= s->oy;
    double lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
    double a = ceil(lo / s->uy - 0.5), b = ceil(hi / s->uy - 0.5);
    if (!(b - a <= INK_MAX_DIM) || !(fabs(a) <= 1e8)) { s->bad = 1; return; }   /* written so that NaN fails too */
    for (int row = (int)a; row < (int)b; row++) {
        if (s->n >= s->cap) {
            int nc = s->cap ? s->cap * 2 : 256;
            Cross *nv = (Cross *)realloc(s->v, sizeof(Cross) * (size_t)nc);
            if (!nv) { s->bad = 1; return; }
            s->v = nv;
            s->cap = nc;
        }
        s->v[s->n].row = row;
        s->v[s->n].x = x0 + ((row + 0.5) * s->uy - y0) * (x1 - x0) / (y1 - y0);
        s->v[s->n].dir = y1 > y0 ? 1 : -1;
        s->n++;
    }
}

static int cross_cmp(const void *pa, const void *pb)
{
    const Cross *a = (const Cross *)pa, *b = (const Cross *)pb;
    if (a->row != b->row) return a->row < b->row ? -1 : 1;
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    return a->dir - b->dir;
}

/* The pixels of ux by uy font units whose centre ((c + 0.5) * ux + ox, (r + 0.5) * uy + oy) is inside the glyph (non-zero winding).
 * 0 ok (k->n == 0: no ink), -1 too large or out of memory. */
static int scan_ink(const OutlineShape *sh, double ux, double uy, double ox, double oy, Ink *k)
{
    Scan s;
    memset(&s, 0, sizeof s);
    memset(k, 0, sizeof *k);
    if (!(ux > 0) || !(uy > 0)) return -1;
    s.ux = ux; s.uy = uy; s.ox = ox; s.oy = oy;
    each_edge(sh, scan_edge, &s);
    if (s.bad) { free(s.v); return -1; }
    if (!s.n) { free(s.v); return 0; }
    qsort(s.v, (size_t)s.n, sizeof(Cross), cross_cmp);
    double minx = s.v[0].x, maxx = s.v[0].x;
    for (int i = 1; i < s.n; i++) { if (s.v[i].x < minx) minx = s.v[i].x; if (s.v[i].x > maxx) maxx = s.v[i].x; }
    double c0 = ceil(minx / ux - 0.5), c1 = ceil(maxx / ux - 0.5);
    int r0 = s.v[0].row, r1 = s.v[s.n - 1].row + 1;
    if (c1 - c0 > INK_MAX_DIM || r1 - r0 > INK_MAX_DIM || fabs(c0) > 1e8 || (c1 - c0) * (double)(r1 - r0) > (double)INK_MAX_PIX) { free(s.v); return -1; }
    k->x0 = (int)c0; k->y0 = r0; k->w = (int)(c1 - c0); k->h = r1 - r0;
    if (k->w <= 0) { free(s.v); memset(k, 0, sizeof *k); return 0; }
    k->b = (unsigned char *)calloc((size_t)k->w * (size_t)k->h, 1);
    if (!k->b) { free(s.v); memset(k, 0, sizeof *k); return -1; }
    for (int i = 0; i < s.n;) {
        int row = s.v[i].row, winding = 0;
        double start = 0;
        unsigned char *line = k->b + (size_t)(row - r0) * (size_t)k->w;
        for (; i < s.n && s.v[i].row == row; i++) {
            if (!winding) start = s.v[i].x;
            winding += s.v[i].dir;
            if (!winding) {
                int a = (int)ceil(start / ux - 0.5) - k->x0, b = (int)ceil(s.v[i].x / ux - 0.5) - k->x0;
                if (a < 0) a = 0;
                if (b > k->w) b = k->w;
                for (int c = a; c < b; c++) { if (!line[c]) k->n++; line[c] = 1; }
            }
        }
    }
    free(s.v);
    return 0;
}

/* the cells (fx by fy fine pixels, edges at px, py) an ink touches, each with the value of its centre */
typedef struct Cells { int c0, r0, cw, ch; signed char *v; long on; } Cells;   /* v: 0 untouched, 1 centre inked, -1 not */

static int cells_of(const Ink *k, double fx, double fy, double px, double py, Cells *c)
{
    memset(c, 0, sizeof *c);
    c->c0 = (int)floor((k->x0 + 0.5 - px) / fx);
    c->r0 = (int)floor((k->y0 + 0.5 - py) / fy);
    c->cw = (int)floor((k->x0 + k->w - 0.5 - px) / fx) - c->c0 + 1;
    c->ch = (int)floor((k->y0 + k->h - 0.5 - py) / fy) - c->r0 + 1;
    if (c->cw <= 0 || c->ch <= 0) return -1;
    c->v = (signed char *)calloc((size_t)c->cw * (size_t)c->ch, 1);
    return c->v ? 0 : -1;
}

/* the intersection and the union of an ink and the ink redrawn on cells that take the value of their centre (see grid_match) */
static void match_sums(const Ink *k, double fx, double fy, double px, double py, double *inside, double *uni)
{
    Cells c;
    if (!k->n || cells_of(k, fx, fy, px, py, &c) < 0) return;
    long in = 0;
    for (int y = 0; y < k->h; y++) {
        int cr = (int)floor((k->y0 + y + 0.5 - py) / fy) - c.r0;
        const unsigned char *line = k->b + (size_t)y * (size_t)k->w;
        for (int x = 0; x < k->w; x++) {
            if (!line[x]) continue;
            int cc = (int)floor((k->x0 + x + 0.5 - px) / fx) - c.c0;
            signed char *st = &c.v[(size_t)cr * (size_t)c.cw + (size_t)cc];
            if (!*st) {
                int on = ink_at(k, (long)floor((cc + c.c0 + 0.5) * fx + px), (long)floor((cr + c.r0 + 0.5) * fy + py));
                *st = on ? 1 : -1;
                c.on += on;
            }
            if (*st > 0) in++;
        }
    }
    *inside += (double)in;
    *uni += (double)k->n + (double)c.on * fx * fy - (double)in;
    free(c.v);
}

/* how many samples of the cells touched by ink agree with the centre of their cell; a margin along each edge is not looked at */
static void core_match(const Ink *k, double fx, double fy, double px, double py, double mx, double my, double *agree, double *total)
{
    Cells c;
    if (!k->n || cells_of(k, fx, fy, px, py, &c) < 0) return;
    double sx[CORE_SAMPLES], sy[CORE_SAMPLES];
    for (int i = 0; i < CORE_SAMPLES; i++) {
        sx[i] = mx / fx + (1 - 2 * mx / fx) * i / (CORE_SAMPLES - 1);
        sy[i] = my / fy + (1 - 2 * my / fy) * i / (CORE_SAMPLES - 1);
    }
    for (int y = 0; y < k->h; y++) {
        int cr = (int)floor((k->y0 + y + 0.5 - py) / fy) - c.r0;
        const unsigned char *line = k->b + (size_t)y * (size_t)k->w;
        for (int x = 0; x < k->w; x++) {
            if (!line[x]) continue;
            int cc = (int)floor((k->x0 + x + 0.5 - px) / fx) - c.c0;
            signed char *st = &c.v[(size_t)cr * (size_t)c.cw + (size_t)cc];
            if (*st) continue;
            int col = cc + c.c0, row = cr + c.r0;
            int centre = ink_at(k, (long)floor((col + 0.5) * fx + px), (long)floor((row + 0.5) * fy + py));
            *st = centre ? 1 : -1;
            for (int a = 0; a < CORE_SAMPLES; a++)
                for (int b = 0; b < CORE_SAMPLES; b++) {
                    int in = ink_at(k, (long)floor((col + sx[a]) * fx + px), (long)floor((row + sy[b]) * fy + py));
                    *agree += in == centre;
                    *total += 1;
                }
        }
    }
    free(c.v);
}

/* ------------------------------------------------------------------ lattices fitted to the edges */

typedef struct IE { Ctr widths, heights, xs, xe, ys, ye; double turning; int pass; } IE;

static void ie_free(IE *e) { ctr_free(&e->widths); ctr_free(&e->heights); ctr_free(&e->xs); ctr_free(&e->xe); ctr_free(&e->ys); ctr_free(&e->ye); }

static void ie_edge(void *u, double x0, double y0, double x1, double y1)
{
    IE *e = (IE *)u;
    if (!e->pass) { e->turning += x0 * y1 - x1 * y0; return; }
    if (y0 == y1 && x0 != x1) {
        ctr_add(&e->widths, (int)rint(fabs(x1 - x0)), 1);
        ctr_add((x1 > x0) == (e->turning > 0) ? &e->ys : &e->ye, (int)rint(y0), 1);
    } else if (x0 == x1 && y0 != y1) {
        ctr_add(&e->heights, (int)rint(fabs(y1 - y0)), 1);
        ctr_add((y1 > y0) == (e->turning > 0) ? &e->xe : &e->xs, (int)rint(x0), 1);
    }
}

/* adds the straight edges of a glyph: their lengths and where ink starts and ends on each axis */
static void ink_edges(const OutlineShape *s, IE *e)
{
    e->turning = 0;
    e->pass = 0;
    each_edge(s, ie_edge, e);   /* which way this glyph is wound */
    e->pass = 1;
    each_edge(s, ie_edge, e);
}

typedef struct Lat { double pitch; double phase, buffer, on; } Lat;
typedef struct LN { double k, v; long n; } LN;

static int lat_lines(const Ctr *pos, double phase, double pitch, double limit, LN *out)
{
    int m = 0;
    for (int i = 0; i < pos->n; i++) {
        double k = rint((pos->k[i] - phase) / pitch);
        if (fabs(pos->k[i] - phase - pitch * k) <= limit) { out[m].k = k; out[m].v = pos->k[i]; out[m].n = pos->c[i]; m++; }
    }
    return m;
}

static double lat_commonest(const Ctr *pos, double pitch, double limit)
{
    int idx[200];
    int m = ctr_top(pos, idx, 200);
    double best = 0, bestW = -1;
    for (int i = 0; i < m; i++) {
        double r = pmod(pos->k[idx[i]], pitch), w = 0;
        for (int j = 0; j < m; j++) {
            double s = pmod(pos->k[idx[j]], pitch), d = fabs(r - s);
            if ((d < pitch - d ? d : pitch - d) <= limit) w += (double)pos->c[idx[j]];
        }
        if (w > bestW) { bestW = w; best = r; }
    }
    return best;
}

/* Fits evenly spaced cells to where ink starts and ends on an axis, from a pitch of about `start` (_fit_lattice). */
static int fit_lattice(const Ctr *starts, const Ctr *ends, double start, Lat *out)
{
    const Ctr *side[2] = {starts, ends};
    double pitch = start, limit = fmax(1.5, SEARCH_EDGE_SLIP * start);
    double phase[2], mk[2] = {0, 0}, mv[2] = {0, 0};
    int cnt[2] = {0, 0};
    LN *ln[2];
    ln[0] = (LN *)malloc(sizeof(LN) * (size_t)(starts->n + 1));
    ln[1] = (LN *)malloc(sizeof(LN) * (size_t)(ends->n + 1));
    if (!ln[0] || !ln[1] || !(pitch > 0)) { free(ln[0]); free(ln[1]); return -1; }
    phase[0] = lat_commonest(starts, pitch, limit);
    phase[1] = lat_commonest(ends, pitch, limit);
    for (int it = 0; it < 6; it++) {
        double kmin = 1e300, kmax = -1e300;
        for (int s = 0; s < 2; s++) {
            cnt[s] = lat_lines(side[s], phase[s], pitch, limit, ln[s]);
            for (int i = 0; i < cnt[s]; i++) { if (ln[s][i].k < kmin) kmin = ln[s][i].k; if (ln[s][i].k > kmax) kmax = ln[s][i].k; }
        }
        if (!(kmax > kmin)) break;   /* fewer than two different lines */
        double spread = 0, sum = 0;
        for (int s = 0; s < 2; s++) {
            double tot = 0;
            mk[s] = mv[s] = 0;
            for (int i = 0; i < cnt[s]; i++) { tot += (double)ln[s][i].n; mk[s] += ln[s][i].k * ln[s][i].n; mv[s] += ln[s][i].v * ln[s][i].n; }
            if (tot > 0) { mk[s] /= tot; mv[s] /= tot; }
            for (int i = 0; i < cnt[s]; i++) {
                double k = ln[s][i].k - mk[s], v = ln[s][i].v - mv[s];
                spread += k * k * ln[s][i].n;
                sum += k * v * ln[s][i].n;
            }
        }
        if (!spread) break;
        pitch = sum / spread;
        if (!(pitch > 0)) { free(ln[0]); free(ln[1]); return -1; }
        for (int s = 0; s < 2; s++) if (cnt[s]) phase[s] = mv[s] - pitch * mk[s];
    }
    double reach = 0;
    for (int s = 0; s < 2; s++) {
        cnt[s] = lat_lines(side[s], phase[s], pitch, limit, ln[s]);
        for (int i = 0; i < cnt[s]; i++) if (fabs(ln[s][i].k) > reach) reach = fabs(ln[s][i].k);
    }
    if (reach && fabs(pitch - rint(pitch)) * reach <= 1) {   /* a whole pitch that strays less than a unit across the font */
        pitch = rint(pitch);
        for (int s = 0; s < 2; s++) {
            double tot = 0, k = 0, v = 0;
            for (int i = 0; i < cnt[s]; i++) { tot += (double)ln[s][i].n; k += ln[s][i].k * ln[s][i].n; v += ln[s][i].v * ln[s][i].n; }
            if (tot > 0) phase[s] = v / tot - pitch * (k / tot);
        }
    } else pitch = round3(pitch);
    if (!(pitch > 0)) { free(ln[0]); free(ln[1]); return -1; }
    double on = 0;
    for (int s = 0; s < 2; s++) {
        int m = lat_lines(side[s], phase[s], pitch, limit, ln[s]);
        for (int i = 0; i < m; i++) on += (double)ln[s][i].n;
    }
    double all = (double)(ctr_total(starts) + ctr_total(ends));
    double size = pmod(phase[1] - phase[0], pitch);   /* the drawn size, up to a whole pitch */
    double buffer = size <= pitch * SEARCH_MAX_BUFFER ? size / 2 : (size - pitch) / 2;
    buffer = fabs(buffer) < 0.5 ? 0 : round1(buffer);
    out->pitch = pitch;
    out->phase = rint(signed_res(phase[0] + buffer, pitch));
    out->buffer = buffer;
    out->on = all > 0 ? on / all : 0;
    free(ln[0]);
    free(ln[1]);
    return 0;
}

#define LAT_MAX (SEARCH_EDGE_LENGTHS * 4)

/* the lattices of an axis, the coarsest first, from its commonest edge lengths */
static int lattices(const Ctr *starts, const Ctr *ends, const Ctr *lengths, double upem, Lat *out)
{
    static const int divisors[4] = {1, 2, 3, 4};
    Lat fits[LAT_MAX];
    int nf = 0, n = 0, idx[SEARCH_EDGE_LENGTHS];
    if (!starts->n || !ends->n) return 0;
    int m = ctr_top(lengths, idx, SEARCH_EDGE_LENGTHS);
    for (int i = 0; i < m; i++)
        for (int d = 0; d < 4; d++) {
            double p = (double)lengths->k[idx[i]] / divisors[d];
            Lat l;
            if (!(upem / SEARCH_MAX_EM_CELLS <= p && p <= upem / MIN_EM_CELLS)) continue;
            if (fit_lattice(starts, ends, p, &l) < 0) continue;
            int j = nf++;
            while (j > 0 && fits[j - 1].pitch < l.pitch) { fits[j] = fits[j - 1]; j--; }   /* by pitch, the coarsest first, stable */
            fits[j] = l;
        }
    for (int i = 0; i < nf; i++) {
        int dup = 0;
        if (fits[i].on < SEARCH_MIN_ON_LATTICE) continue;
        for (int j = 0; j < n; j++) if (fabs(fits[i].pitch - out[j].pitch) < 0.01 * fits[i].pitch) dup = 1;
        if (!dup) out[n++] = fits[i];
    }
    return n;
}

typedef struct Grid { double x, y, xp, yp; int per; double match; const char *how; } Grid;
typedef struct Pair { Lat x, y; } Pair;

static int pair_cmp(const Pair *a, const Pair *b)
{
    double pa = a->x.pitch * a->y.pitch, pb = b->x.pitch * b->y.pitch;
    return pa > pb;
}

static void pairs_sort(Pair *p, int n)   /* by the size of the cell, the largest first, stable */
{
    for (int i = 1; i < n; i++) {
        Pair t = p[i];
        int j = i;
        while (j > 0 && pair_cmp(&t, &p[j - 1])) { p[j] = p[j - 1]; j--; }
        p[j] = t;
    }
}

static int aspect_ok(double x, double y) { return 1 / SEARCH_MAX_ASPECT <= x / y && x / y <= SEARCH_MAX_ASPECT; }

static void all_edges(const FF *f, IE *e)
{
    memset(e, 0, sizeof *e);
    for (int i = 0; i < f->n; i++) if (has_coords(&f->g[i])) ink_edges(&f->g[i].s, e);
}

/* {glyph: where the cells of `pitch` start} for a font whose glyphs are shifted one by one; has[i] = 0: no vertical edges */
static void glyph_phases(const FF *f, const int *which, int nwhich, double pitch, double *phase, char *has)
{
    double limit = fmax(1.5, SEARCH_EDGE_SLIP * pitch);
    for (int w = 0; w < (which ? nwhich : f->n); w++) {
        int i = which ? which[w] : w;
        IE e;
        memset(&e, 0, sizeof e);
        has[i] = 0;
        phase[i] = 0;
        ink_edges(&f->g[i].s, &e);
        if (e.xs.n) {
            Ctr both;
            memset(&both, 0, sizeof both);
            for (int k = 0; k < e.xs.n; k++) ctr_add(&both, e.xs.k[k], e.xs.c[k]);
            for (int k = 0; k < e.xe.n; k++) ctr_add(&both, e.xe.k[k], e.xe.c[k]);
            double best = 0, bestW = -1;
            for (int a = 0; a < both.n; a++) {
                double r = pmod(both.k[a], pitch), wsum = 0;
                for (int b = 0; b < both.n; b++) {
                    double s = pmod(both.k[b], pitch), d = fabs(r - s);
                    if ((d < pitch - d ? d : pitch - d) <= limit) wsum += (double)both.c[b];
                }
                if (wsum > bestW) { bestW = wsum; best = r; }
            }
            phase[i] = rint(signed_res(best, pitch));
            has[i] = 1;
            ctr_free(&both);
        }
        ie_free(&e);
    }
}

/* a grid that may be shifted or have pixels that are not square (shifted_grid); *guess gets the best pair that failed */
static int shifted_grid(const FF *f, Grid *out, Grid *guess)
{
    IE e;
    Lat lx[LAT_MAX], ly[LAT_MAX];
    Pair *pairs;
    int np = 0, found = 0;
    all_edges(f, &e);
    int nx = lattices(&e.xs, &e.xe, &e.widths, f->upem, lx), ny = lattices(&e.ys, &e.ye, &e.heights, f->upem, ly);
    ie_free(&e);
    pairs = (Pair *)malloc(sizeof(Pair) * (size_t)(nx * ny + 1));
    int *probe = (int *)malloc(sizeof(int) * (size_t)(f->n + 1));
    double *pin = (double *)calloc((size_t)(f->n + 1), sizeof(double)), *pun = (double *)calloc((size_t)(f->n + 1), sizeof(double));
    char *have = (char *)calloc((size_t)(f->n + 1), 1);
    if (!pairs || !probe || !pin || !pun || !have) { free(pairs); free(probe); free(pin); free(pun); free(have); return 0; }
    for (int i = 0; i < nx; i++) for (int j = 0; j < ny; j++) if (aspect_ok(lx[i].pitch, ly[j].pitch)) { pairs[np].x = lx[i]; pairs[np].y = ly[j]; np++; }
    pairs_sort(pairs, np);
    int nprobe = probe_glyphs(f, probe);
    for (int p = 0; p < np && p < SEARCH_MAX_PAIRS && !found; p++) {   /* the coarsest first */
        double x = pairs[p].x.pitch, y = pairs[p].y.pitch, xp = pairs[p].x.phase, yp = pairs[p].y.phase;
        double fine = (x < y ? x : y) / SEARCH_CELL_PIXELS;
        double in = 0, un = 0;
        memset(have, 0, (size_t)f->n);
        for (int k = 0; k < nprobe; k++) {
            Ink ink;
            int i = probe[k];
            pin[i] = pun[i] = 0;
            if (scan_ink(&f->g[i].s, fine, fine, 0, 0, &ink) < 0 || !ink.n) { ink_free(&ink); continue; }
            match_sums(&ink, x / fine, y / fine, xp / fine, yp / fine, &pin[i], &pun[i]);
            have[i] = 1;
            in += pin[i];
            un += pun[i];
            ink_free(&ink);
        }
        if (!(un > 0) || in / un < SEARCH_SAMPLE_MATCH) continue;
        double shared = 0, uni = 0;
        for (int i = 0; i < f->n; i++) {   /* one glyph at a time; a glyph of several code points counts as many */
            if (!has_coords(&f->g[i])) continue;
            double a = 0, b = 0;
            if (have[i]) { a = pin[i]; b = pun[i]; }
            else {
                Ink ink;
                if (scan_ink(&f->g[i].s, fine, fine, 0, 0, &ink) < 0 || !ink.n) { ink_free(&ink); continue; }
                match_sums(&ink, x / fine, y / fine, xp / fine, yp / fine, &a, &b);
                ink_free(&ink);
            }
            shared += f->g[i].uses * a;
            uni += f->g[i].uses * b;
        }
        double m = uni > 0 ? shared / uni : 0;
        Grid g = {x, y, xp, yp, 0, m, "lattice"};
        if (m >= GRID_MATCH) { *out = g; found = 1; }
        else if (guess && m > guess->match) *guess = g;
    }
    free(pairs); free(probe); free(pin); free(pun); free(have);
    return found;
}

/* the coarsest candidate close to every coordinate or redrawing the font well, 0 when none (coarsest_grid) */
static double coarsest_grid(const FF *f, const double *cand, int nc, double fine, double *match)
{
    if (nc <= 0) return 0;
    double *in = (double *)calloc((size_t)nc, sizeof(double)), *un = (double *)calloc((size_t)nc, sizeof(double));
    char *close = (char *)malloc((size_t)nc);
    if (!in || !un || !close) { free(in); free(un); free(close); return 0; }
    memset(close, 1, (size_t)nc);
    for (int i = 0; i < f->n; i++) {
        const FG *g = &f->g[i];
        if (!has_coords(g)) continue;
        for (int c = 0; c < nc; c++) {
            if (!close[c]) continue;
            for (int k = 0; k < g->s.np && close[c]; k++) {
                double vx = pmod(rint(g->s.px[k]), cand[c]), vy = pmod(rint(g->s.py[k]), cand[c]);
                if (fmin(vx, cand[c] - vx) > GRID_TOLERANCE * cand[c] || fmin(vy, cand[c] - vy) > GRID_TOLERANCE * cand[c]) close[c] = 0;
            }
        }
        Ink ink;
        if (scan_ink(&g->s, fine, fine, 0, 0, &ink) < 0 || !ink.n) { ink_free(&ink); continue; }
        for (int c = 0; c < nc; c++) {
            double a = 0, b = 0;
            match_sums(&ink, cand[c] / fine, cand[c] / fine, 0, 0, &a, &b);
            in[c] += g->uses * a;
            un[c] += g->uses * b;
        }
        ink_free(&ink);
    }
    double best = 0;
    for (int c = 0; c < nc; c++) {
        double m = un[c] > 0 ? in[c] / un[c] : 0;
        if ((close[c] || m >= GRID_MATCH) && cand[c] > best) { best = cand[c]; *match = m; }
    }
    free(in); free(un); free(close);
    return best;
}

typedef struct LenU { Ctr *c; } LenU;
static void len_edge(void *u, double x0, double y0, double x1, double y1)
{
    if (x0 == x1 || y0 == y1) ctr_add(((LenU *)u)->c, (int)rint(fabs(x1 - x0) + fabs(y1 - y0)), 1);
}

/* a font whose common divisor is no grid: among its glyph grids and commonest edge lengths (deep_grid) */
static double deep_grid(const FF *f, double *match)
{
    Ctr lengths;
    LenU u = {&lengths};
    memset(&lengths, 0, sizeof lengths);
    for (int i = 0; i < f->n; i++) if (has_coords(&f->g[i])) each_edge(&f->g[i].s, len_edge, &u);
    double fine = f->upem / SEARCH_EM_CELLS;
    int cap = f->n + SEARCH_EDGE_LENGTHS, n = 0, idx[SEARCH_EDGE_LENGTHS];
    double *cand = (double *)malloc(sizeof(double) * (size_t)cap), *keep = (double *)malloc(sizeof(double) * (size_t)cap);
    if (!cand || !keep) { free(cand); free(keep); ctr_free(&lengths); return 0; }
    for (int i = 0; i < f->n; i++) if (f->g[i].gcd) cand[n++] = (double)f->g[i].gcd;
    int m = ctr_top(&lengths, idx, SEARCH_EDGE_LENGTHS);
    for (int i = 0; i < m; i++) cand[n++] = (double)lengths.k[idx[i]];
    int nk = 0;
    for (int i = 0; i < n; i++) {
        int dup = 0;
        if (!(2 * fine <= cand[i] && cand[i] <= f->upem / MIN_EM_CELLS)) continue;
        for (int j = 0; j < nk; j++) if (keep[j] == cand[i]) dup = 1;
        if (!dup) keep[nk++] = cand[i];
    }
    n = 0;
    for (int i = 0; i < nk; i++) {   /* of two nearly equal lengths, the rarer one is a slip of the other */
        int slip = 0;
        for (int j = 0; j < nk; j++)
            if (fabs(keep[i] - keep[j]) <= GRID_TOLERANCE * keep[i] && ctr_get(&lengths, (int)keep[j]) > ctr_get(&lengths, (int)keep[i])) slip = 1;
        if (!slip) cand[n++] = keep[i];
    }
    double unit = coarsest_grid(f, cand, n, fine, match);
    free(cand); free(keep);
    ctr_free(&lengths);
    return unit;
}

/* a grid whose columns start anew in each glyph (glyph_phase_grid) */
static int glyph_phase_grid(const FF *f, Grid *out)
{
    IE e;
    Lat ly[LAT_MAX];
    Pair pairs[SEARCH_EDGE_LENGTHS * LAT_MAX];
    int np = 0, idx[SEARCH_EDGE_LENGTHS], found = 0;
    all_edges(f, &e);
    int ny = lattices(&e.ys, &e.ye, &e.heights, f->upem, ly);
    int m = ctr_top(&e.widths, idx, SEARCH_EDGE_LENGTHS);
    for (int i = 0; i < m; i++) {
        double p = (double)e.widths.k[idx[i]];
        if (!(f->upem / SEARCH_MAX_EM_CELLS <= p && p <= f->upem / MIN_EM_CELLS)) continue;
        for (int j = 0; j < ny; j++) if (aspect_ok(p, ly[j].pitch)) { pairs[np].x.pitch = p; pairs[np].x.phase = 0; pairs[np].y = ly[j]; np++; }
    }
    ie_free(&e);
    pairs_sort(pairs, np);
    double *phase = (double *)malloc(sizeof(double) * (size_t)(f->n + 1));
    char *has = (char *)malloc((size_t)(f->n + 1));
    if (!phase || !has) { free(phase); free(has); return 0; }
    for (int p = 0; p < np && p < SEARCH_MAX_PAIRS && !found; p++) {
        double x = pairs[p].x.pitch, y = pairs[p].y.pitch, yp = pairs[p].y.phase;
        double fine = (x < y ? x : y) / SEARCH_CELL_PIXELS, shared = 0, inked = 0;
        glyph_phases(f, NULL, 0, x, phase, has);
        for (int i = 0; i < f->n; i++) {
            Ink ink;
            if (!has_coords(&f->g[i]) || !has[i]) continue;
            if (scan_ink(&f->g[i].s, fine, fine, 0, 0, &ink) < 0 || !ink.n) { ink_free(&ink); continue; }
            double a = 0, b = 0;
            match_sums(&ink, x / fine, y / fine, phase[i] / fine, yp / fine, &a, &b);
            if (b > 0) shared += a / b * (double)ink.n;
            inked += (double)ink.n;
            ink_free(&ink);
        }
        if (inked > 0 && shared / inked >= GRID_MATCH) { *out = (Grid){x, y, 0, yp, 1, shared / inked, "glyph columns"}; found = 1; }
    }
    free(phase); free(has);
    return found;
}

static int is_pixel_size(double pitch, const Ctr *lengths)
{
    int idx[1];
    return ctr_top(lengths, idx, 1) && fabs(lengths->k[idx[0]] - pitch) <= CORE_PITCH_SLIP * pitch;
}

static double core_margin(double pitch, double buffer) { return fmax(2 * buffer, CORE_SLIP * pitch); }

/* a grid that the ink does not follow closely, as pixels were drawn too large or carelessly (loose_grid) */
static int loose_grid(const FF *f, Grid *out)
{
    IE e;
    Lat cols[LAT_MAX], rows0[LAT_MAX], rows[LAT_MAX];
    Pair pairs[LAT_MAX * LAT_MAX];
    int np = 0, nr = 0, found = 0;
    all_edges(f, &e);
    int nc = lattices(&e.xs, &e.xe, &e.widths, f->upem, cols), nr0 = lattices(&e.ys, &e.ye, &e.heights, f->upem, rows0);
    for (int j = 0; j < nr0; j++) if (is_pixel_size(rows0[j].pitch, &e.heights)) rows[nr++] = rows0[j];
    for (int i = 0; i < nc; i++)
        for (int j = 0; j < nr; j++)
            if (is_pixel_size(cols[i].pitch, &e.widths) && aspect_ok(cols[i].pitch, rows[j].pitch)) { pairs[np].x = cols[i]; pairs[np].y = rows[j]; np++; }
    pairs_sort(pairs, np);
    for (int p = 0; p < np && p < SEARCH_MAX_PAIRS && !found; p++) {
        double x = pairs[p].x.pitch, y = pairs[p].y.pitch;
        double fine = (x < y ? x : y) / SEARCH_CELL_PIXELS, agree = 0, total = 0;
        for (int i = 0; i < f->n; i++) {
            Ink ink;
            if (!has_coords(&f->g[i])) continue;
            if (scan_ink(&f->g[i].s, fine, fine, 0, 0, &ink) < 0 || !ink.n) { ink_free(&ink); continue; }
            double a = 0, t = 0;
            core_match(&ink, x / fine, y / fine, pairs[p].x.phase / fine, pairs[p].y.phase / fine,
                       core_margin(x, pairs[p].x.buffer) / fine, core_margin(y, pairs[p].y.buffer) / fine, &a, &t);
            agree += f->g[i].uses * a;
            total += f->g[i].uses * t;
            ink_free(&ink);
        }
        if (total > 0 && agree / total >= CORE_MATCH) { *out = (Grid){x, y, pairs[p].x.phase, pairs[p].y.phase, 0, agree / total, "loose"}; found = 1; }
    }
    if (!found) {   /* square pixels, whose columns start anew in each glyph */
        double *phase = (double *)malloc(sizeof(double) * (size_t)(f->n + 1));
        char *has = (char *)malloc((size_t)(f->n + 1));
        for (int j = 0; phase && has && j < nr && !found; j++) {   /* rows are sorted the coarsest first */
            double y = rows[j].pitch;
            if (!is_pixel_size(y, &e.widths)) continue;
            double fine = y / SEARCH_CELL_PIXELS, agree = 0, inked = 0;
            glyph_phases(f, NULL, 0, y, phase, has);
            for (int i = 0; i < f->n; i++) {
                Ink ink;
                if (!has_coords(&f->g[i]) || !has[i]) continue;
                if (scan_ink(&f->g[i].s, fine, fine, 0, 0, &ink) < 0 || !ink.n) { ink_free(&ink); continue; }
                double a = 0, t = 0;
                core_match(&ink, y / fine, y / fine, phase[i] / fine, rows[j].phase / fine, CORE_SLIP * y / fine, core_margin(y, rows[j].buffer) / fine, &a, &t);
                if (t > 0) agree += a / t * (double)ink.n;
                inked += (double)ink.n;
                ink_free(&ink);
            }
            if (inked > 0 && agree / inked >= CORE_MATCH) { *out = (Grid){y, y, 0, rows[j].phase, 1, agree / inked, "loose"}; found = 1; }
        }
        free(phase); free(has);
    }
    ie_free(&e);
    return found;
}

/* ---- a font drawn as separate dots of one size ---- */

typedef struct Dot { double w, h, cx, cy; } Dot;

static int dot_lattice(const Ctr *centres, double size, double upem, double *pitch, double *phase)
{
    static const int divisors[4] = {1, 2, 3, 4};
    int n = centres->n, idx[SEARCH_EDGE_LENGTHS], found = 0;
    int *v = (int *)malloc(sizeof(int) * (size_t)(n + 1));
    Ctr gaps;
    memset(&gaps, 0, sizeof gaps);
    if (!v) return 0;
    memcpy(v, centres->k, sizeof(int) * (size_t)n);
    for (int i = 1; i < n; i++) { int t = v[i], j = i; while (j > 0 && v[j - 1] > t) { v[j] = v[j - 1]; j--; } v[j] = t; }
    for (int i = 0; i + 1 < n; i++) if (v[i + 1] - v[i] >= size * (1 - DOT_SIZE_SLIP)) ctr_add(&gaps, v[i + 1] - v[i], 1);
    free(v);
    int m = ctr_top(&gaps, idx, SEARCH_EDGE_LENGTHS);
    Lat best = {0, 0, 0, 0};
    for (int i = 0; i < m; i++)
        for (int d = 0; d < 4; d++) {
            double p = (double)gaps.k[idx[i]] / divisors[d];
            Lat l;
            if (!(p >= size * (1 - DOT_SIZE_SLIP) && upem / SEARCH_MAX_EM_CELLS <= p && p <= upem / MIN_EM_CELLS)) continue;
            if (fit_lattice(centres, centres, p, &l) < 0 || l.on < DOT_ON_LATTICE) continue;
            if (!found || l.pitch > best.pitch || (l.pitch == best.pitch && (l.phase > best.phase || (l.phase == best.phase && l.on > best.on)))) best = l;
            found = 1;
        }
    ctr_free(&gaps);
    if (!found) return 0;
    *pitch = best.pitch;
    *phase = rint(signed_res(best.phase - best.pitch / 2, best.pitch));
    return 1;
}

/* the grid on the centres of a font drawn as dots of one size, whatever their shape (dot_grid) */
static int dot_grid(const FF *f, Grid *out)
{
    Dot *dots = NULL;
    int nd = 0, cap = 0, holes = 0, found = 0;
    Ctr sizes;
    memset(&sizes, 0, sizeof sizes);
    for (int i = 0; i < f->n; i++) {
        const OutlineShape *s = &f->g[i].s;
        double winding = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int c = 0; c < s->nc; c++) {
                int a = s->pstart[c], b = s->pstart[c + 1];
                double area = 0, minx = 1e30, maxx = -1e30, miny = 1e30, maxy = -1e30;
                for (int k = a; k < b; k++) {
                    int k2 = k + 1 < b ? k + 1 : a;
                    area += (double)s->px[k] * s->py[k2] - (double)s->px[k2] * s->py[k];
                    if (s->px[k] < minx) minx = s->px[k];
                    if (s->px[k] > maxx) maxx = s->px[k];
                    if (s->py[k] < miny) miny = s->py[k];
                    if (s->py[k] > maxy) maxy = s->py[k];
                }
                area /= 2;
                if (!pass) { winding += area; continue; }
                if (area * winding < 0) holes++;
                else if (area) {
                    if (nd >= cap) {
                        cap = cap ? cap * 2 : 256;
                        Dot *nv = (Dot *)realloc(dots, sizeof(Dot) * (size_t)cap);
                        if (!nv) { free(dots); ctr_free(&sizes); return 0; }
                        dots = nv;
                    }
                    dots[nd++] = (Dot){maxx - minx, maxy - miny, (minx + maxx) / 2, (miny + maxy) / 2};
                }
            }
    }
    if (nd >= DOT_MIN_COUNT && holes <= DOT_MAX_HOLES * nd) {
        int idx[1];
        for (int i = 0; i < nd; i++) ctr_add(&sizes, (int)fmin(rint(dots[i].w), 32767) * 65536 + (int)fmin(rint(dots[i].h), 32767), 1);
        ctr_top(&sizes, idx, 1);
        double w = sizes.k[idx[0]] / 65536, h = sizes.k[idx[0]] % 65536;
        if (h < 0) { h += 65536; w -= 1; }
        Ctr cx, cy;
        memset(&cx, 0, sizeof cx);
        memset(&cy, 0, sizeof cy);
        int good = 0;
        double mw = 0, mh = 0;
        for (int i = 0; i < nd; i++)
            if (fabs(dots[i].w - w) <= DOT_SIZE_SLIP * w && fabs(dots[i].h - h) <= DOT_SIZE_SLIP * h) {
                good++;
                if (dots[i].w > mw) mw = dots[i].w;
                if (dots[i].h > mh) mh = dots[i].h;
                ctr_add(&cx, (int)rint(dots[i].cx), 1);
                ctr_add(&cy, (int)rint(dots[i].cy), 1);
            }
        double px, py, phx, phy;
        if (w > 0 && h > 0 && good >= DOT_MIN_SHARE * nd && dot_lattice(&cx, mw, f->upem, &px, &phx) && dot_lattice(&cy, mh, f->upem, &py, &phy)
            && aspect_ok(px, py)) {
            *out = (Grid){px, py, phx, phy, 0, 0, "dots"};
            found = 1;
        }
        ctr_free(&cx);
        ctr_free(&cy);
    }
    free(dots);
    ctr_free(&sizes);
    return found;
}

/* the grid with its columns started anew in each glyph if the probe glyphs match so clearly better (phases_by_glyph) */
static void phases_by_glyph(const FF *f, Grid *g)
{
    double fine = (g->x < g->y ? g->x : g->y) / SEARCH_CELL_PIXELS;
    int *probe = (int *)malloc(sizeof(int) * (size_t)(f->n + 1));
    double *phase = (double *)calloc((size_t)(f->n + 1), sizeof(double));
    char *has = (char *)calloc((size_t)(f->n + 1), 1);
    if (!probe || !phase || !has) { free(probe); free(phase); free(has); return; }
    int np = probe_glyphs(f, probe);
    glyph_phases(f, probe, np, g->x, phase, has);
    double one[2] = {0, 0}, own[2] = {0, 0};
    for (int k = 0; k < np; k++) {
        Ink ink;
        int i = probe[k];
        if (scan_ink(&f->g[i].s, fine, fine, 0, 0, &ink) < 0 || !ink.n) { ink_free(&ink); continue; }
        match_sums(&ink, g->x / fine, g->y / fine, g->xp / fine, g->yp / fine, &one[0], &one[1]);
        match_sums(&ink, g->x / fine, g->y / fine, (has[i] ? phase[i] : 0) / fine, g->yp / fine, &own[0], &own[1]);
        ink_free(&ink);
    }
    if (one[1] > 0 && own[1] > 0 && own[0] / own[1] >= one[0] / one[1] + GLYPH_PHASE_GAIN) { g->xp = 0; g->per = 1; g->match = own[0] / own[1]; }
    free(probe); free(phase); free(has);
}

void fit_grid_free(FitGrid *g)
{
    if (!g) return;
    free(g->phaseCp);
    free(g->phase);
    memset(g, 0, sizeof *g);
}

int fit_find_grid(const Outline *ol, FitGrid *out, char *msg, int msgsz)
{
    FF f;
    Grid g = {0, 0, 0, 0, 0, 0, ""}, guess = {0, 0, 0, 0, 0, 0, "guess"};
    int found = 0;
    memset(out, 0, sizeof *out);
    if (!ol) { setmsg(msg, msgsz, "no font"); return -1; }
    if (ff_load(&f, ol) < 0) { ff_free(&f); setmsg(msg, msgsz, "cannot read the glyphs"); return -1; }
    long exact = 0;
    for (int i = 0; i < f.n; i++) exact = igcd(exact, f.g[i].gcd);
    if (!exact) { ff_free(&f); setmsg(msg, msgsz, "the font has no outlines"); return 0; }
    found = dot_grid(&f, &g);
    if (!found) {
        double unit = 0, m = 0;
        if (f.upem / (double)exact <= MAX_EM_CELLS) {
            double *cand = (double *)malloc(sizeof(double) * (size_t)(f.n + 1));
            int nc = 0;
            for (int i = 0; cand && i < f.n; i++) {
                int dup = 0;
                if (f.g[i].gcd <= exact) continue;
                for (int j = 0; j < nc; j++) if (cand[j] == (double)f.g[i].gcd) dup = 1;
                if (!dup) cand[nc++] = (double)f.g[i].gcd;
            }
            unit = cand ? coarsest_grid(&f, cand, nc, (double)exact, &m) : 0;
            free(cand);
            g = (Grid){unit ? unit : (double)exact, unit ? unit : (double)exact, 0, 0, 0, unit ? m : 1, unit ? "coarser" : "exact"};
            found = 1;
            if (f.upem / g.x > SEARCH_MAX_EM_CELLS) {   /* too fine for pixels: the pixels may be neither square nor whole */
                Grid s;
                if (shifted_grid(&f, &s, &guess)) g = s;
            }
        } else {
            found = shifted_grid(&f, &g, &guess);
            if (!found) {
                unit = deep_grid(&f, &m);
                if (unit) { g = (Grid){unit, unit, 0, 0, 0, m, "edges"}; found = 1; }
            }
            if (!found) found = glyph_phase_grid(&f, &g) || loose_grid(&f, &g);
        }
        if (found && !g.per && strcmp(g.how, "exact") && strcmp(g.how, "coarser") && strcmp(g.how, "edges")) phases_by_glyph(&f, &g);
    }
    int sure = found;
    if (!found && guess.match >= GUESS_MATCH) { g = guess; found = 1; }   /* pxfe: the best lattice, for the user to judge */
    if (!found) { ff_free(&f); setmsg(msg, msgsz, "no pixel grid found"); return 0; }
    out->x = g.x; out->y = g.y; out->xPhase = g.xp + 0.0; out->yPhase = g.yp + 0.0;   /* + 0.0: no negative zero */
    out->perGlyph = g.per;
    out->sure = sure;
    out->match = g.match;
    snprintf(out->how, sizeof out->how, "%s", g.how);
    if (g.per) {
        double *phase = (double *)calloc((size_t)(f.n + 1), sizeof(double));
        char *has = (char *)calloc((size_t)(f.n + 1), 1);
        int *cp = NULL, ncp = fit_codepoints(ol, &cp);
        int ng = outline_glyph_count(ol);
        int *map = (int *)malloc(sizeof(int) * (size_t)(ng + 1));
        out->phaseCp = (int *)malloc(sizeof(int) * (size_t)(ncp > 0 ? ncp : 1));
        out->phase = (double *)malloc(sizeof(double) * (size_t)(ncp > 0 ? ncp : 1));
        if (!phase || !has || ncp < 0 || !map || !out->phaseCp || !out->phase) {
            free(phase); free(has); free(cp); free(map); fit_grid_free(out); ff_free(&f);
            setmsg(msg, msgsz, "out of memory");
            return -1;
        }
        glyph_phases(&f, NULL, 0, g.x, phase, has);
        for (int i = 0; i < ng; i++) map[i] = -1;
        for (int i = 0; i < f.n; i++) map[f.g[i].gid] = i;
        for (int i = 0; i < ncp; i++) {
            int gi = cp[2 * i + 1] < ng ? map[cp[2 * i + 1]] : -1;
            if (gi < 0 || !has[gi] || !phase[gi]) continue;
            out->phaseCp[out->nphase] = cp[2 * i];
            out->phase[out->nphase++] = phase[gi];
        }
        free(phase); free(has); free(cp); free(map);
    }
    ff_free(&f);
    return 1;
}

void fit_grid_params(const Outline *ol, const FitGrid *g, FitParams *p)
{
    double upem = outline_units_per_em(ol);
    memset(p, 0, sizeof *p);
    p->mode = FIT_PIXEL;
    p->threshold = 0.5f;
    p->em = (float)(upem / g->y);
    p->aspect = (float)(g->y / g->x);
    p->dx = (float)(-g->xPhase / g->x) + 0.0f;
    p->dy = (float)(-g->yPhase / g->y) + 0.0f;
}

int fit_grid_corr(const FitGrid *g, int cp, FitCorr *c)
{
    int lo = 0, hi = g->nphase;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (g->phaseCp[mid] < cp) lo = mid + 1; else hi = mid;
    }
    if (lo >= g->nphase || g->phaseCp[lo] != cp) return 0;
    c->dx = (float)(-g->phase[lo] / g->x + g->xPhase / g->x);
    c->dy = 0;
    c->dsize = 1;
    return c->dx != 0;
}

/* ------------------------------------------------------------------ rendering */

void fit_bitmap_free(FitBitmap *b)
{
    if (!b) return;
    free(b->pix);
    memset(b, 0, sizeof *b);
}

/* crops a w*h 0/1 bitmap (top row first; left column and top edge given) into out */
static int crop_into(const unsigned char *b, int w, int h, int left, int top, FitBitmap *out)
{
    int x0 = w, x1 = -1, y0 = h, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (b[(size_t)y * (size_t)w + (size_t)x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    out->w = out->h = out->left = out->top = 0;
    out->pix = NULL;
    if (x1 < 0) return 0;
    out->w = x1 - x0 + 1;
    out->h = y1 - y0 + 1;
    out->left = left + x0;
    out->top = top - y0;
    out->pix = (unsigned char *)malloc((size_t)out->w * (size_t)out->h);
    if (!out->pix) { out->w = out->h = 0; return -1; }
    for (int y = 0; y < out->h; y++) memcpy(out->pix + (size_t)y * (size_t)out->w, b + (size_t)(y + y0) * (size_t)w + (size_t)x0, (size_t)out->w);
    return 0;
}

/* The coverage of a glyph with four columns per pixel (so that it can be moved by quarter pixels afterwards). */
typedef struct Cov { OutlineBitmap b; } Cov;

static int cov_raster(const Outline *ol, int glyph, double emx, double emy, double dx, double dy, Cov *c)
{
    memset(c, 0, sizeof *c);
    if (!(emx >= 0.5) || !(emy >= 0.5) || emx * 4 > 8192 || emy > 8192) return -1;
    return outline_rasterize_glyph_xy(ol, glyph, (float)(emx * 4), (float)emy, (float)(dx * 4), (float)dy, &c->b, NULL, 0) == OUTLINE_OK ? 0 : -1;
}

static int fdiv4(int a) { return a >= 0 ? a / 4 : -((3 - a) / 4); }

/* Turns the coverage into pixels at a threshold.  snap: the glyph may move by a quarter pixel step to where the pixels are closest
 * to the coverage.  err and uni (may be NULL) get the sums of |coverage - pixel| and of max(coverage, pixel). */
static int cov_pixels(const Cov *c, double thr, int snap, FitBitmap *out, double *err, double *uni, int *kOut)
{
    static const int ks[4] = {0, -1, 1, 2};
    const OutlineBitmap *b = &c->b;
    out->w = out->h = out->left = out->top = 0;
    out->pix = NULL;
    if (err) *err = 0;
    if (uni) *uni = 0;
    if (kOut) *kOut = 0;
    if (b->w <= 0 || b->h <= 0 || !b->pix) return 0;
    int ncol = b->w / 4 + 3;
    float *cv = (float *)malloc(sizeof(float) * (size_t)ncol * (size_t)b->h);
    unsigned char *bits = (unsigned char *)malloc((size_t)ncol * (size_t)b->h);
    if (!cv || !bits) { free(cv); free(bits); return -1; }
    int bestK = 0, bestC0 = 0;
    double bestE = 1e300, bestU = 0;
    for (int t = 0; t < (snap ? 4 : 1); t++) {
        int k = ks[t], c0 = fdiv4(b->left - k);
        double e = 0, u = 0;
        memset(cv, 0, sizeof(float) * (size_t)ncol * (size_t)b->h);
        for (int y = 0; y < b->h; y++) {
            const unsigned char *row = b->pix + (size_t)y * (size_t)b->w;
            float *crow = cv + (size_t)y * (size_t)ncol;
            for (int x = 0; x < b->w; x++) if (row[x]) crow[fdiv4(b->left + x - k) - c0] += row[x];
        }
        for (size_t i = 0; i < (size_t)ncol * (size_t)b->h; i++) {
            double v = cv[i] / (4.0 * 255.0);
            int on = v >= thr;
            e += on ? 1 - v : v;
            u += on ? 1 : v;
        }
        if (e < bestE - 1e-6) { bestE = e; bestU = u; bestK = k; bestC0 = c0; }
    }
    {
        int k = bestK;
        memset(cv, 0, sizeof(float) * (size_t)ncol * (size_t)b->h);
        for (int y = 0; y < b->h; y++) {
            const unsigned char *row = b->pix + (size_t)y * (size_t)b->w;
            float *crow = cv + (size_t)y * (size_t)ncol;
            for (int x = 0; x < b->w; x++) if (row[x]) crow[fdiv4(b->left + x - k) - bestC0] += row[x];
        }
        for (size_t i = 0; i < (size_t)ncol * (size_t)b->h; i++) bits[i] = cv[i] / (4.0 * 255.0) >= thr;
    }
    if (err) *err = bestE;
    if (uni) *uni = bestU;
    if (kOut) *kOut = bestK;
    int rc = crop_into(bits, ncol, b->h, bestC0, b->top, out);
    free(cv);
    free(bits);
    return rc;
}

int fit_render(const Outline *ol, int glyph, const FitParams *p, const FitCorr *corr, FitBitmap *out)
{
    return fit_render_shift(ol, glyph, p, corr, out, NULL);
}

int fit_render_shift(const Outline *ol, int glyph, const FitParams *p, const FitCorr *corr, FitBitmap *out, float *shift)
{
    if (shift) *shift = 0;
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (!ol || !p || !(p->em > 0)) return -1;
    double upem = outline_units_per_em(ol);
    double ds = corr && corr->dsize > 0 ? corr->dsize : 1.0, aspect = p->aspect > 0 ? p->aspect : 1.0;
    double emy = (double)p->em * ds, emx = emy * aspect;
    double dx = (double)p->dx + (corr ? corr->dx : 0), dy = (double)p->dy + (corr ? corr->dy : 0);
    if (upem <= 0) return -1;
    {
        int adv = outline_advance(ol, glyph);
        if (adv > 0x7FFF) adv = 0;   /* a negative advance written where only unsigned numbers belong */
        out->advance = (int)lround(adv * emx / upem);
    }
    if (p->mode == FIT_PIXEL) {
        OutlineShape s;
        Ink ink;
        double ux = upem / emx, uy = upem / emy;
        if (outline_glyph_shape(ol, glyph, 8, &s, NULL, 0) != OUTLINE_OK) return -1;
        int rc = scan_ink(&s, ux, uy, -dx * ux, -dy * uy, &ink);
        outline_shape_free(&s);
        if (rc < 0) return -1;
        if (!ink.n) { ink_free(&ink); return 0; }
        unsigned char *flip = (unsigned char *)malloc((size_t)ink.w * (size_t)ink.h);
        if (!flip) { ink_free(&ink); return -1; }
        for (int y = 0; y < ink.h; y++) memcpy(flip + (size_t)y * (size_t)ink.w, ink.b + (size_t)(ink.h - 1 - y) * (size_t)ink.w, (size_t)ink.w);
        rc = crop_into(flip, ink.w, ink.h, ink.x0, ink.y0 + ink.h, out);
        free(flip);
        ink_free(&ink);
        return rc;
    }
    Cov c;
    double thr = p->threshold;
    if (!(thr >= 0.05)) thr = 0.05;
    if (thr > 0.95) thr = 0.95;
    if (cov_raster(ol, glyph, emx, emy, dx, dy, &c) < 0) return -1;
    int k = 0;
    int rc = cov_pixels(&c, thr, !(corr && corr->dx != 0), out, NULL, NULL, &k);
    if (shift) *shift = (float)(-k / 4.0);
    outline_bitmap_free(&c.b);
    return rc;
}

void fit_box(const Outline *ol, const FitParams *p, int *ascent, int *descent)
{
    int a = 0, d = 0;
    double upem = outline_units_per_em(ol);
    outline_metrics(ol, &a, &d, NULL);
    if (d > 0) d = -d;   /* the descent lies below the baseline, whatever sign the font gives it */
    long asc = upem > 0 ? lround(a * (double)p->em / upem + p->dy) : 0, desc = upem > 0 ? lround(-d * (double)p->em / upem - p->dy) : 0;
    if (ascent) *ascent = asc < 0 ? 0 : (int)asc;
    if (descent) *descent = desc < 0 ? 0 : (int)desc;
}

/* ------------------------------------------------------------------ the size of a font that is no pixel font */

/* the parts (8-connected ink) and the holes (4-connected background that does not reach the border) of a w*h 0/1 bitmap */
static int topology(const unsigned char *b, int w, int h, int *parts, int *holes)
{
    int W = w + 2, H = h + 2;
    unsigned char *m = (unsigned char *)calloc((size_t)W * (size_t)H, 1);
    int *st = (int *)malloc(sizeof(int) * (size_t)W * (size_t)H);
    *parts = *holes = 0;
    if (!m || !st) { free(m); free(st); return -1; }
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) m[(size_t)(y + 1) * (size_t)W + (size_t)(x + 1)] = b[(size_t)y * (size_t)w + (size_t)x] ? 1 : 0;
    int bg = 0;
    for (int i = 0; i < W * H; i++) {
        if (m[i] >= 2) continue;
        int ink = m[i], sp = 0;
        m[i] = (unsigned char)(ink ? 3 : 2);
        st[sp++] = i;
        while (sp) {
            int q = st[--sp], qx = q % W, qy = q / W;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if ((!dx && !dy) || (!ink && dx && dy)) continue;
                    int nx = qx + dx, ny = qy + dy;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    int n = ny * W + nx;
                    if (m[n] != ink) continue;
                    m[n] = (unsigned char)(ink ? 3 : 2);
                    st[sp++] = n;
                }
        }
        if (ink) (*parts)++; else bg++;
    }
    *holes = bg - 1;   /* the first background region is the outside (the padding) */
    free(m);
    free(st);
    return 0;
}

int fit_find_size(const Outline *ol, const FitSizeOpts *opts, FitParams *p, FitSizeReport *rep, char *msg, int msgsz)
{
    static const double dys[4] = {0, 0.25, 0.5, 0.75}, thrs[2] = {0.5, 0.38};
    enum { MAXP = 80 };
    int probe[MAXP], rparts[MAXP], rholes[MAXP], np = 0;
    if (rep) memset(rep, 0, sizeof *rep);
    if (!ol || !p) { setmsg(msg, msgsz, "no font"); return -1; }
    double upem = outline_units_per_em(ol);
    if (upem <= 0) { setmsg(msg, msgsz, "no font"); return -1; }
    double minEm = opts && opts->minEm > 0 ? opts->minEm : 5, maxEm = opts && opts->maxEm > 0 ? opts->maxEm : 40;
    if (opts && opts->fixedEm > 0) minEm = maxEm = opts->fixedEm;
    if (minEm < 2) minEm = 2;
    if (maxEm > 400) maxEm = 400;
    if (maxEm < minEm) maxEm = minEm;

    /* the probe: letters and digits, else the first glyphs with ink; each with the parts and holes of its true shape */
    for (int pass = 0; pass < 2 && np < 10; pass++) {
        int *cp = NULL, ncp = pass ? fit_codepoints(ol, &cp) : 62;
        np = 0;
        for (int i = 0; i < ncp && np < MAXP; i++) {
            int c = pass ? cp[2 * i] : (i < 26 ? 'A' + i : i < 52 ? 'a' + i - 26 : '0' + i - 52);
            int g = outline_glyph_index(ol, c);
            OutlineShape s;
            Ink ink;
            if (g <= 0 || outline_glyph_shape(ol, g, 8, &s, NULL, 0) != OUTLINE_OK) continue;
            int rc = scan_ink(&s, upem / 200, upem / 200, 0, 0, &ink);
            outline_shape_free(&s);
            if (rc == 0 && ink.n && topology(ink.b, ink.w, ink.h, &rparts[np], &rholes[np]) == 0) probe[np++] = g;
            ink_free(&ink);
        }
        free(cp);
    }
    if (!np) { setmsg(msg, msgsz, "the font has no glyph with ink"); return 0; }

    int steps = (int)floor((maxEm - minEm) * 2 + 1e-6) + 1;
    double *sTopo = (double *)calloc((size_t)steps, sizeof(double)), *sCover = (double *)calloc((size_t)steps, sizeof(double));
    double *sDy = (double *)calloc((size_t)steps, sizeof(double)), *sThr = (double *)calloc((size_t)steps, sizeof(double));
    if (!sTopo || !sCover || !sDy || !sThr) { free(sTopo); free(sCover); free(sDy); free(sThr); setmsg(msg, msgsz, "out of memory"); return -1; }
    for (int s = 0; s < steps; s++) {
        double em = minEm + 0.5 * s, bestScore = -1;
        for (int d = 0; d < 4; d++) {
            double ok[2] = {0, 0}, err[2] = {0, 0}, uni[2] = {0, 0};
            for (int k = 0; k < np; k++) {
                Cov c;
                if (cov_raster(ol, probe[k], em, em, 0, dys[d], &c) < 0) continue;
                for (int t = 0; t < 2; t++) {
                    FitBitmap b;
                    double e = 0, u = 0;
                    int parts = 0, holes = 0;
                    if (cov_pixels(&c, thrs[t], 1, &b, &e, &u, NULL) < 0) continue;
                    err[t] += e;
                    uni[t] += u;
                    if (b.w > 0 && topology(b.pix, b.w, b.h, &parts, &holes) == 0 && parts == rparts[k] && holes == rholes[k]) ok[t] += 1;
                    fit_bitmap_free(&b);
                }
                outline_bitmap_free(&c.b);
            }
            for (int t = 0; t < 2; t++) {
                double topo = ok[t] / np, cover = uni[t] > 0 ? 1 - err[t] / uni[t] : 0;
                double score = topo + 0.5 * cover;
                if (score > bestScore + 1e-9) { bestScore = score; sTopo[s] = topo; sCover[s] = cover; sDy[s] = dys[d]; sThr[s] = thrs[t]; }
            }
        }
    }
    /* the smallest size at which (nearly) as many glyphs keep their parts and holes as at any size, then the best of the sizes
     * up to two pixels above it: there the stems and the heights tend to fall on whole pixels */
    double bestTopo = 0;
    for (int s = 0; s < steps; s++) if (sTopo[s] > bestTopo) bestTopo = sTopo[s];
    int first = 0;
    double slack = fmax(0.035, 1.5 / np);   /* two glyphs of 62; with a small sample at least one */
    while (first < steps - 1 && sTopo[first] < bestTopo - slack) first++;
    int pick = first;
    for (int s = first; s < steps && s <= first + 4; s++)
        if (sTopo[s] + 0.5 * sCover[s] > sTopo[pick] + 0.5 * sCover[pick] + 1e-9) pick = s;
    {   /* at the size chosen: the position in sixteenths of a pixel and more thresholds; the coarse answer stays on a tie */
        static const double fthr[7] = {0.5, 0.46, 0.42, 0.38, 0.34, 0.3, 0.55};
        double em = minEm + 0.5 * pick, best = sTopo[pick] + 0.5 * sCover[pick];
        for (int d = 0; d < 16; d++) {
            double ok[7] = {0}, err[7] = {0}, uni[7] = {0};
            for (int k = 0; k < np; k++) {
                Cov c;
                if (cov_raster(ol, probe[k], em, em, 0, d / 16.0, &c) < 0) continue;
                for (int t = 0; t < 7; t++) {
                    FitBitmap b;
                    double e = 0, u = 0;
                    int parts = 0, holes = 0;
                    if (cov_pixels(&c, fthr[t], 1, &b, &e, &u, NULL) < 0) continue;
                    err[t] += e;
                    uni[t] += u;
                    if (b.w > 0 && topology(b.pix, b.w, b.h, &parts, &holes) == 0 && parts == rparts[k] && holes == rholes[k]) ok[t] += 1;
                    fit_bitmap_free(&b);
                }
                outline_bitmap_free(&c.b);
            }
            for (int t = 0; t < 7; t++) {
                double topo = ok[t] / np, cover = uni[t] > 0 ? 1 - err[t] / uni[t] : 0;
                if (topo + 0.5 * cover > best + 1e-6) { best = topo + 0.5 * cover; sTopo[pick] = topo; sCover[pick] = cover; sDy[pick] = d / 16.0; sThr[pick] = fthr[t]; }
            }
        }
    }
    memset(p, 0, sizeof *p);
    p->mode = FIT_SMOOTH;
    p->aspect = 1;
    p->em = (float)(minEm + 0.5 * pick);
    p->dy = (float)sDy[pick];
    p->threshold = (float)sThr[pick];
    if (sTopo[pick] > bestTopo) bestTopo = sTopo[pick];
    if (rep) { rep->probes = np; rep->topo = (float)sTopo[pick]; rep->cover = (float)sCover[pick]; rep->bestTopo = (float)bestTopo; }
    free(sTopo); free(sCover); free(sDy); free(sThr);
    return 1;
}
