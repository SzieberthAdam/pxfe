/* raster.c - see raster.h. */
#include "raster.h"

#include <math.h>
#include <stdlib.h>

void ras_free(RasPath *r)
{
    free(r->p);
    r->p = NULL;
    r->n = r->cap = 0;
}

bool ras_add(RasPath *r, int x, int y)
{
    if (r->n > 0 && r->p[r->n - 1].x == x && r->p[r->n - 1].y == y) return true;
    if (r->n >= r->cap) {
        int nc = r->cap ? r->cap * 2 : 64;
        RasPt *q = (RasPt *)realloc(r->p, sizeof(RasPt) * (size_t)nc);
        if (!q) return false;
        r->p = q;
        r->cap = nc;
    }
    r->p[r->n].x = x;
    r->p[r->n].y = y;
    r->n++;
    return true;
}

bool ras_line(RasPath *out, int x0, int y0, int x1, int y1)
{
    bool swap = x0 > x1 || (x0 == x1 && y0 > y1);
    int first = out->n;
    if (swap) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    int dx = x1 - x0, dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if (!ras_add(out, x0, y0)) return false;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0++; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
    if (swap)
        for (int i = first, j = out->n - 1; i < j; i++, j--) { RasPt t = out->p[i]; out->p[i] = out->p[j]; out->p[j] = t; }
    return true;
}

void ras_snap(int dx, int dy, int *ox, int *oy)
{
    static const int dir[7][2] = {{1, 0}, {4, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 4}, {0, 1}};
    int ax = abs(dx), ay = abs(dy);
    if (ax == 0 && ay == 0) { *ox = *oy = 0; return; }
    double deg = atan2((double)ay, (double)ax) * 180.0 / 3.14159265358979323846;
    int i = (int)floor(deg / 15.0 + 0.5);
    if (i < 0) i = 0;
    if (i > 6) i = 6;
    int r = dir[i][0], s = dir[i][1];
    int k = (int)floor(((double)(ax * r + ay * s)) / (double)(r * r + s * s) + 0.5);
    *ox = (dx < 0 ? -1 : 1) * k * r;
    *oy = (dy < 0 ? -1 : 1) * k * s;
}

void ras_ends(int sx, int sy, int hx, int hy, bool snap, bool center, int ends[4])
{
    int dx = hx - sx, dy = hy - sy;
    if (snap) ras_snap(dx, dy, &dx, &dy);
    ends[0] = center ? sx - dx : sx;
    ends[1] = center ? sy - dy : sy;
    ends[2] = sx + dx;
    ends[3] = sy + dy;
}

bool ras_is_corner(RasPt a, RasPt b, RasPt c)
{
    return abs(a.x - b.x) + abs(a.y - b.y) == 1 && abs(b.x - c.x) + abs(b.y - c.y) == 1 && abs(a.x - c.x) == 1 && abs(a.y - c.y) == 1;
}

int ras_pixel_perfect(RasPt *p, int n)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (m > 0 && p[m - 1].x == p[i].x && p[m - 1].y == p[i].y) continue;
        if (m >= 2 && ras_is_corner(p[m - 2], p[m - 1], p[i])) m--;
        p[m++] = p[i];
    }
    return m;
}

static int imax3(int a, int b, int c) { int m = a > b ? a : b; return m > c ? m : c; }

bool ras_bezier(RasPath *out, RasPt p0, RasPt p1, RasPt p2, RasPt p3, bool perfect)
{
    int first = out->n;
    int d01 = imax3(abs(p1.x - p0.x), abs(p1.y - p0.y), 0), d12 = imax3(abs(p2.x - p1.x), abs(p2.y - p1.y), 0), d23 = imax3(abs(p3.x - p2.x), abs(p3.y - p2.y), 0);
    int steps = 3 * imax3(d01, d12, d23) + 2;   /* the speed of the curve is at most 3 times the longest side of its control polygon: under a cell a step */
    int px = p0.x, py = p0.y;
    if (!ras_add(out, p0.x, p0.y)) return false;
    for (int i = 1; i <= steps; i++) {
        double t = (double)i / steps, u = 1.0 - t;
        double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
        int x = (int)floor(b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x + 0.5);
        int y = (int)floor(b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y + 0.5);
        if (i == steps) { x = p3.x; y = p3.y; }
        if (abs(x - px) > 1 || abs(y - py) > 1) {   /* never with this step count; the line keeps the path whole all the same */
            RasPath seg = {0};
            if (!ras_line(&seg, px, py, x, y)) { ras_free(&seg); return false; }
            for (int k = 1; k < seg.n; k++) if (!ras_add(out, seg.p[k].x, seg.p[k].y)) { ras_free(&seg); return false; }
            ras_free(&seg);
        } else if (!ras_add(out, x, y)) return false;
        px = x; py = y;
    }
    if (perfect) out->n = first + ras_pixel_perfect(out->p + first, out->n - first);
    return true;
}

void ras_box(int sx, int sy, int hx, int hy, bool square, bool center, int box[4])
{
    int dx = hx - sx, dy = hy - sy;
    if (square) {
        int n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
        dx = (dx < 0 ? -1 : 1) * n;
        dy = (dy < 0 ? -1 : 1) * n;
    }
    int ax = center ? sx - dx : sx, ay = center ? sy - dy : sy, bx = sx + dx, by = sy + dy;
    box[0] = ax < bx ? ax : bx;
    box[1] = ay < by ? ay : by;
    box[2] = ax < bx ? bx : ax;
    box[3] = ay < by ? by : ay;
}

bool ras_rect(RasPath *out, int x0, int y0, int x1, int y1, bool filled)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (filled || x == x0 || x == x1 || y == y0 || y == y1)
                if (!ras_add(out, x, y)) return false;
    return true;
}

/* The ellipse: the midpoint (Bresenham type) ellipse in a bounding box of any size, stepped from the top and bottom middle towards the sides in four
 * mirrored quadrants (so it is symmetric by construction, for even and odd sizes alike).  The cells are sorted and joined (the mirrors meet on the axes),
 * then every L corner is dropped: a cell with exactly two neighbours in the ring, each one straight step away, so the ring stays whole and one cell thick.
 * A filled ellipse fills every row of that ring from its first to its last cell. */
static int cmp_cell(const void *a, const void *b)
{
    const RasPt *p = (const RasPt *)a, *q = (const RasPt *)b;
    return p->y != q->y ? (p->y < q->y ? -1 : 1) : (p->x < q->x ? -1 : p->x > q->x);
}

static bool in_ring(const RasPath *r, int x, int y)
{
    RasPt k = {x, y};
    return bsearch(&k, r->p, (size_t)r->n, sizeof(RasPt), cmp_cell) != NULL;
}

/* the ring of the box 0..a by 0..b (a = width - 1, b = height - 1) shifted to (x0, y0): sorted, no cell twice */
static bool ell_ring(RasPath *ring, int x0, int y0, int a, int b)
{
    RasPath t = {0};
    long b1 = b & 1;
    long dx = 4 * (1 - (long)a) * (long)b * b, dy = 4 * (b1 + 1) * (long)a * a, err = dx + dy + b1 * (long)a * a, e2;
    int xl = x0, xr = x0 + a, yt = y0 + (b + 1) / 2, yb = yt - (int)b1;
    long sa = 8 * (long)a * a, sb = 8 * (long)b * b;
    bool ok = true;
    do {
        ok = ok && ras_add(&t, xr, yt) && ras_add(&t, xl, yt) && ras_add(&t, xl, yb) && ras_add(&t, xr, yb);
        e2 = 2 * err;
        if (e2 <= dy) { yt++; yb--; dy += sa; err += dy; }
        if (e2 >= dx || 2 * err > dy) { xl++; xr--; dx += sb; err += dx; }
    } while (ok && xl <= xr);
    while (ok && yt - yb <= b) {   /* a flat ellipse stops early: finish its tip */
        ok = ras_add(&t, xl - 1, yt) && ras_add(&t, xr + 1, yt) && ras_add(&t, xl - 1, yb) && ras_add(&t, xr + 1, yb);
        yt++; yb--;
    }
    if (ok) {
        qsort(t.p, (size_t)t.n, sizeof(RasPt), cmp_cell);
        for (int i = 0; i < t.n; i++)
            if (i == 0 || cmp_cell(&t.p[i], &t.p[i - 1]) != 0) ok = ras_add(ring, t.p[i].x, t.p[i].y) && ok;
    }
    ras_free(&t);
    return ok;
}

bool ras_ellipse(RasPath *out, int x0, int y0, int x1, int y1, bool filled)
{
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w <= 2 || h <= 2) return ras_rect(out, x0, y0, x1, y1, true);   /* a line or a solid band */
    RasPath ring = {0};
    if (!ell_ring(&ring, x0, y0, w - 1, h - 1)) { ras_free(&ring); return false; }
    bool ok = true;
    char *drop = (char *)calloc((size_t)ring.n, 1);
    if (!drop) { ras_free(&ring); return false; }
    for (int i = 0; i < ring.n; i++) {
        int x = ring.p[i].x, y = ring.p[i].y, n = 0, ax = 0, ay = 0, bx = 0, by = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if ((dx || dy) && in_ring(&ring, x + dx, y + dy)) { if (n == 0) ax = dx, ay = dy; else bx = dx, by = dy; n++; }
        drop[i] = n == 2 && abs(ax) + abs(ay) == 1 && abs(bx) + abs(by) == 1 && abs(ax - bx) == 1 && abs(ay - by) == 1;
    }
    for (int i = 0; i < ring.n && ok; i++) {   /* the cells are sorted by row: a row's first and last kept cell bound its fill */
        if (drop[i]) continue;
        if (!filled) { ok = ras_add(out, ring.p[i].x, ring.p[i].y); continue; }
        int j = i;
        while (j + 1 < ring.n && ring.p[j + 1].y == ring.p[i].y) j++;
        int last = j;
        while (last > i && drop[last]) last--;
        for (int x = ring.p[i].x; x <= ring.p[last].x && ok; x++) ok = ras_add(out, x, ring.p[i].y);
        i = j;
    }
    free(drop);
    ras_free(&ring);
    return ok;
}

/* ------------------------------------------------------------------ fill and polygon */

bool ras_flood(RasPath *out, const char *pix, int rx, int ry, int rw, int rh, int sx, int sy, bool contiguous, bool diagonals, const unsigned char *clip)
{
    if (!pix || rw <= 0 || rh <= 0 || sx < rx || sy < ry || sx >= rx + rw || sy >= ry + rh) return true;
    size_t n = (size_t)rw * rh;
    size_t seed = (size_t)(sy - ry) * rw + (size_t)(sx - rx);
    if (clip && !clip[seed]) return true;
    char target = pix[seed];
    unsigned char *mark = (unsigned char *)calloc(n, 1);
    size_t *stack = contiguous ? (size_t *)malloc(n * sizeof(size_t)) : NULL;
    if (!mark || (contiguous && !stack)) { free(mark); free(stack); return false; }
    if (!contiguous) {
        for (size_t i = 0; i < n; i++) mark[i] = pix[i] == target && (!clip || clip[i]);
    } else {
        size_t sp = 0;
        stack[sp++] = seed;
        mark[seed] = 1;
        while (sp > 0) {   /* a stack of cells: each cell is pushed once */
            size_t idx = stack[--sp];
            int cx = (int)(idx % (size_t)rw), cy = (int)(idx / (size_t)rw);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if ((!dx && !dy) || (!diagonals && dx && dy)) continue;
                    int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= rw || ny >= rh) continue;
                    size_t ni = (size_t)ny * rw + (size_t)nx;
                    if (mark[ni] || pix[ni] != target || (clip && !clip[ni])) continue;
                    mark[ni] = 1;
                    stack[sp++] = ni;
                }
        }
    }
    bool ok = true;
    for (size_t i = 0; i < n && ok; i++)
        if (mark[i]) ok = ras_add(out, rx + (int)(i % (size_t)rw), ry + (int)(i / (size_t)rw));
    free(mark);
    free(stack);
    return ok;
}

/* The inside by the even-odd rule: for each row, the edges that cross the line through the cell centres, sorted, and the cells whose centres lie between
 * the 1st and 2nd, the 3rd and 4th ... crossing; the vertices are cell centres. */
static bool poly_inside(RasPath *out, const RasPt *v, int n)
{
    int y0 = v[0].y, y1 = v[0].y;
    for (int i = 1; i < n; i++) { if (v[i].y < y0) y0 = v[i].y; if (v[i].y > y1) y1 = v[i].y; }
    double *cr = (double *)malloc(sizeof(double) * (size_t)n);
    if (!cr) return false;
    bool ok = true;
    for (int y = y0; y <= y1 && ok; y++) {
        double yc = y + 0.5;
        int k = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double ax = v[i].x + 0.5, ay = v[i].y + 0.5, bx = v[j].x + 0.5, by = v[j].y + 0.5;
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) cr[k++] = ax + (yc - ay) * (bx - ax) / (by - ay);
        }
        for (int a = 1; a < k; a++) {
            double t = cr[a];
            int b = a - 1;
            while (b >= 0 && cr[b] > t) { cr[b + 1] = cr[b]; b--; }
            cr[b + 1] = t;
        }
        for (int a = 0; a + 1 < k && ok; a += 2) {
            int xa = (int)ceil(cr[a] - 0.5), xb = (int)ceil(cr[a + 1] - 0.5) - 1;   /* cells with cr[a] <= x + 0.5 < cr[a + 1] */
            for (int x = xa; x <= xb && ok; x++) ok = ras_add(out, x, y);
        }
    }
    free(cr);
    return ok;
}

static int cmp_cell_yx(const void *a, const void *b)
{
    const RasPt *p = (const RasPt *)a, *q = (const RasPt *)b;
    return p->y != q->y ? (p->y < q->y ? -1 : 1) : p->x != q->x ? (p->x < q->x ? -1 : 1) : 0;
}

bool ras_polygon(RasPath *out, const RasPt *v, int n, bool closed, bool filled)
{
    if (n < 1) return true;
    RasPath all = {0};
    bool ok = true;
    for (int i = 0; i + 1 < n + (closed && n > 2 ? 1 : 0) && ok; i++) ok = ras_line(&all, v[i].x, v[i].y, v[(i + 1) % n].x, v[(i + 1) % n].y);
    if (ok && n == 1) ok = ras_add(&all, v[0].x, v[0].y);
    if (ok && closed && n >= 3 && all.n > 1 && all.p[all.n - 1].x == all.p[0].x && all.p[all.n - 1].y == all.p[0].y) all.n--;   /* the last edge ends where the first began */
    if (ok && filled && closed && n >= 3) ok = poly_inside(&all, v, n);
    if (ok && filled && closed && n >= 3) {   /* outline and inside together: sorted by row, no cell twice */
        qsort(all.p, (size_t)all.n, sizeof(RasPt), cmp_cell_yx);
        for (int i = 0; i < all.n && ok; i++)
            if (i == 0 || cmp_cell_yx(&all.p[i], &all.p[i - 1]) != 0) ok = ras_add(out, all.p[i].x, all.p[i].y);
    } else if (ok) {
        for (int i = 0; i < all.n && ok; i++) ok = ras_add(out, all.p[i].x, all.p[i].y);   /* the outline in drawing order */
    }
    ras_free(&all);
    return ok;
}
