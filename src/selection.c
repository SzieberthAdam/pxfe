/* selection.c - the cell mask behind the glyph editor's selection tools (see selection.h). */
#include "selection.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ masks */

void sel_free(SelMask *s)
{
    free(s->m);
    memset(s, 0, sizeof *s);
}

bool sel_empty(const SelMask *s) { return s->w <= 0 || s->h <= 0 || !s->m; }

bool sel_get(const SelMask *s, int x, int y)
{
    if (sel_empty(s) || x < s->x || y < s->y || x >= s->x + s->w || y >= s->y + s->h) return false;
    return s->m[(size_t)(y - s->y) * s->w + (x - s->x)] != 0;
}

int sel_count(const SelMask *s)
{
    int n = 0;
    if (sel_empty(s)) return 0;
    for (size_t i = 0, e = (size_t)s->w * s->h; i < e; i++) n += s->m[i] != 0;
    return n;
}

/* a zeroed mask with the box (x, y, w, h); false when the box is empty or too big */
static bool sel_alloc(SelMask *s, int x, int y, int w, int h)
{
    memset(s, 0, sizeof *s);
    if (w <= 0 || h <= 0 || (long long)w * h > 64LL * 1024 * 1024) return false;
    s->m = (unsigned char *)calloc((size_t)w * h, 1);
    if (!s->m) return false;
    s->x = x; s->y = y; s->w = w; s->h = h;
    return true;
}

/* shrinks the box to the cells that are set; frees it when there are none */
static void sel_trim(SelMask *s)
{
    if (!s->m || s->w <= 0 || s->h <= 0) { sel_free(s); return; }
    int x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            if (s->m[(size_t)y * s->w + x]) { x0 = imin(x0, x); x1 = imax(x1, x); y0 = imin(y0, y); y1 = imax(y1, y); }
    if (x1 < 0) { sel_free(s); return; }
    if (x0 == 0 && y0 == 0 && x1 == s->w - 1 && y1 == s->h - 1) return;
    int nw = x1 - x0 + 1, nh = y1 - y0 + 1;
    unsigned char *n = (unsigned char *)malloc((size_t)nw * nh);
    if (!n) return;   /* stays valid, only not tight */
    for (int y = 0; y < nh; y++) memcpy(n + (size_t)y * nw, s->m + (size_t)(y + y0) * s->w + x0, (size_t)nw);
    free(s->m);
    s->m = n; s->x += x0; s->y += y0; s->w = nw; s->h = nh;
}

bool sel_copy(SelMask *dst, const SelMask *src)
{
    if (dst == src) return true;
    sel_free(dst);
    if (sel_empty(src)) return true;
    if (!sel_alloc(dst, src->x, src->y, src->w, src->h)) return false;
    memcpy(dst->m, src->m, (size_t)src->w * src->h);
    return true;
}

void sel_shift(SelMask *s, int dx, int dy)
{
    if (sel_empty(s)) return;
    s->x += dx; s->y += dy;
}

bool sel_set_rect(SelMask *out, int x0, int y0, int x1, int y1)
{
    sel_free(out);
    int ax = imin(x0, x1), bx = imax(x0, x1), ay = imin(y0, y1), by = imax(y0, y1);
    if (!sel_alloc(out, ax, ay, bx - ax + 1, by - ay + 1)) return false;
    memset(out->m, 1, (size_t)out->w * out->h);
    return true;
}

bool sel_combine(SelMask *dst, const SelMask *shape, SelMode mode)
{
    bool de = sel_empty(dst), se = sel_empty(shape);
    if (mode == SEL_REPLACE) return sel_copy(dst, shape);
    if (se) { if (mode == SEL_INTERSECT) sel_free(dst); return true; }
    if (de) { if (mode == SEL_ADD) return sel_copy(dst, shape); return true; }
    int x0, y0, x1, y1;   /* the box of the result, x1 and y1 exclusive */
    if (mode == SEL_ADD) {
        x0 = imin(dst->x, shape->x); y0 = imin(dst->y, shape->y);
        x1 = imax(dst->x + dst->w, shape->x + shape->w); y1 = imax(dst->y + dst->h, shape->y + shape->h);
    } else if (mode == SEL_SUB) {
        x0 = dst->x; y0 = dst->y; x1 = dst->x + dst->w; y1 = dst->y + dst->h;
    } else {
        x0 = imax(dst->x, shape->x); y0 = imax(dst->y, shape->y);
        x1 = imin(dst->x + dst->w, shape->x + shape->w); y1 = imin(dst->y + dst->h, shape->y + shape->h);
        if (x1 <= x0 || y1 <= y0) { sel_free(dst); return true; }
    }
    SelMask r;
    if (!sel_alloc(&r, x0, y0, x1 - x0, y1 - y0)) return false;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            bool a = sel_get(dst, x, y), b = sel_get(shape, x, y), v;
            v = mode == SEL_ADD ? (a || b) : mode == SEL_SUB ? (a && !b) : (a && b);
            r.m[(size_t)(y - y0) * r.w + (x - x0)] = v;
        }
    sel_trim(&r);
    sel_free(dst);
    *dst = r;
    return true;
}

bool sel_invert(SelMask *s, int x0, int y0, int x1, int y1)
{
    int ax = imin(x0, x1), bx = imax(x0, x1), ay = imin(y0, y1), by = imax(y0, y1);
    SelMask r;
    if (!sel_alloc(&r, ax, ay, bx - ax + 1, by - ay + 1)) return false;
    for (int y = ay; y <= by; y++)
        for (int x = ax; x <= bx; x++) r.m[(size_t)(y - ay) * r.w + (x - ax)] = !sel_get(s, x, y);
    sel_trim(&r);
    sel_free(s);
    *s = r;
    return true;
}

bool sel_polygon(SelMask *out, const int *xy, int n)
{
    sel_free(out);
    if (n < 1) return true;
    int x0 = xy[0], x1 = xy[0], y0 = xy[1], y1 = xy[1];
    for (int i = 1; i < n; i++) { x0 = imin(x0, xy[2 * i]); x1 = imax(x1, xy[2 * i]); y0 = imin(y0, xy[2 * i + 1]); y1 = imax(y1, xy[2 * i + 1]); }
    if (!sel_alloc(out, x0, y0, x1 - x0 + 1, y1 - y0 + 1)) return false;
    /* the inside: for each row, where the polygon's edges cross the line through the cell centres */
    double *cr = (double *)malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    if (!cr) { sel_free(out); return false; }
    for (int y = y0; y <= y1; y++) {
        double yc = y + 0.5;
        int k = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double ax = xy[2 * i] + 0.5, ay = xy[2 * i + 1] + 0.5, bx = xy[2 * j] + 0.5, by = xy[2 * j + 1] + 0.5;
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) cr[k++] = ax + (yc - ay) * (bx - ax) / (by - ay);
        }
        for (int a = 1; a < k; a++) {   /* a few entries: insertion sort */
            double v = cr[a];
            int b = a - 1;
            while (b >= 0 && cr[b] > v) { cr[b + 1] = cr[b]; b--; }
            cr[b + 1] = v;
        }
        for (int a = 0; a + 1 < k; a += 2)
            for (int x = x0; x <= x1; x++)
                if (x + 0.5 >= cr[a] && x + 0.5 < cr[a + 1]) out->m[(size_t)(y - y0) * out->w + (x - x0)] = 1;
    }
    free(cr);
    /* the outline */
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * j], by = xy[2 * j + 1];
        int dx = abs(bx - ax), dy = -abs(by - ay), sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1, err = dx + dy;
        for (;;) {
            out->m[(size_t)(ay - y0) * out->w + (ax - x0)] = 1;
            if (ax == bx && ay == by) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; ax += sx; }
            if (e2 <= dx) { err += dx; ay += sy; }
        }
    }
    sel_trim(out);
    return true;
}

bool sel_wand(SelMask *out, const SelBitmap *rg, int sx, int sy, bool diagonals)
{
    sel_free(out);
    if (!rg->pix || rg->w <= 0 || rg->h <= 0 || sx < rg->x || sy < rg->y || sx >= rg->x + rg->w || sy >= rg->y + rg->h) return true;
    if (!sel_alloc(out, rg->x, rg->y, rg->w, rg->h)) return false;
    size_t n = (size_t)rg->w * rg->h;
    int *stack = (int *)malloc(n * sizeof(int));
    if (!stack) { sel_free(out); return false; }
    char target = rg->pix[(size_t)(sy - rg->y) * rg->w + (sx - rg->x)];
    int sp = 0;
    stack[sp++] = (sy - rg->y) * rg->w + (sx - rg->x);
    out->m[stack[0]] = 1;
    while (sp > 0) {
        int idx = stack[--sp];
        int cx = idx % rg->w, cy = idx / rg->w;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if ((dx == 0 && dy == 0) || (!diagonals && dx != 0 && dy != 0)) continue;
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= rg->w || ny >= rg->h) continue;
                int ni = ny * rg->w + nx;
                if (out->m[ni] || rg->pix[ni] != target) continue;
                out->m[ni] = 1;
                stack[sp++] = ni;
            }
    }
    free(stack);
    sel_trim(out);
    return true;
}

/* ------------------------------------------------------------------ bitmaps */

char selbm_get(const SelBitmap *b, int x, int y)
{
    if (!b->pix || x < b->x || y < b->y || x >= b->x + b->w || y >= b->y + b->h) return '.';
    return b->pix[(size_t)(y - b->y) * b->w + (x - b->x)];
}

int selbm_ink_in(const SelBitmap *b, const SelMask *m)
{
    int n = 0;
    if (sel_empty(m)) return 0;
    for (int y = m->y; y < m->y + m->h; y++)
        for (int x = m->x; x < m->x + m->w; x++)
            if (sel_get(m, x, y) && selbm_get(b, x, y) != '.') n++;
    return n;
}

int selbm_clear(SelBitmap *b, const SelMask *m)
{
    int n = 0;
    if (sel_empty(m) || !b->pix) return 0;
    for (int y = m->y; y < m->y + m->h; y++)
        for (int x = m->x; x < m->x + m->w; x++) {
            if (!sel_get(m, x, y) || selbm_get(b, x, y) == '.') continue;
            b->pix[(size_t)(y - b->y) * b->w + (x - b->x)] = '.';
            n++;
        }
    return n;
}

int selbm_stamp(SelBitmap *b, const SelFloat *f)
{
    int n = 0;
    if (!f->pix || sel_empty(&f->shape) || !b->pix) return 0;
    for (int y = 0; y < f->shape.h; y++)
        for (int x = 0; x < f->shape.w; x++) {
            char c = f->pix[(size_t)y * f->shape.w + x];
            int gx = f->shape.x + x, gy = f->shape.y + y;
            if (c == '.' || gx < b->x || gy < b->y || gx >= b->x + b->w || gy >= b->y + b->h) continue;
            char *p = &b->pix[(size_t)(gy - b->y) * b->w + (gx - b->x)];
            if (*p == c) continue;
            *p = c;
            n++;
        }
    return n;
}

/* ------------------------------------------------------------------ floating pixels */

void selfloat_free(SelFloat *f)
{
    sel_free(&f->shape);
    free(f->pix);
    f->pix = NULL;
}

bool selfloat_lift(SelFloat *f, const SelMask *m, const SelBitmap *b)
{
    selfloat_free(f);
    if (sel_empty(m)) return true;
    if (!sel_copy(&f->shape, m)) return false;
    f->pix = (char *)malloc((size_t)m->w * m->h);
    if (!f->pix) { sel_free(&f->shape); return false; }
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            f->pix[(size_t)y * m->w + x] = m->m[(size_t)y * m->w + x] ? selbm_get(b, m->x + x, m->y + y) : '.';
    return true;
}

bool selfloat_copy(SelFloat *dst, const SelFloat *src)
{
    if (dst == src) return true;
    selfloat_free(dst);
    if (sel_empty(&src->shape)) return true;
    if (!sel_copy(&dst->shape, &src->shape)) return false;
    size_t n = (size_t)src->shape.w * src->shape.h;
    dst->pix = (char *)malloc(n);
    if (!dst->pix) { sel_free(&dst->shape); return false; }
    memcpy(dst->pix, src->pix, n);
    return true;
}

void selfloat_shift(SelFloat *f, int dx, int dy) { sel_shift(&f->shape, dx, dy); }

bool selfloat_ink_bounds(const SelFloat *f, int *x0, int *y0, int *x1, int *y1)
{
    bool any = false;
    if (!f->pix || sel_empty(&f->shape)) return false;
    for (int y = 0; y < f->shape.h; y++)
        for (int x = 0; x < f->shape.w; x++) {
            if (f->pix[(size_t)y * f->shape.w + x] == '.') continue;
            int gx = f->shape.x + x, gy = f->shape.y + y;
            if (!any) { *x0 = *x1 = gx; *y0 = *y1 = gy; any = true; }
            else { *x0 = imin(*x0, gx); *x1 = imax(*x1, gx); *y0 = imin(*y0, gy); *y1 = imax(*y1, gy); }
        }
    return any;
}
