/* sheet.c - finding a font in a picture.  See sheet.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sheet.h"

#define FW 8                    /* the grid a glyph's shape is compared on */
#define FH 10
#define MAX_SCALE 24
#define NOISE_COLORS 300        /* more distinct colours than this: the picture has noise */
#define NOISE_TOL 60            /* how far from the background a noisy pixel must be to be ink (largest channel difference) */
#define MAX_CELLS 4096

static void setmsg(char *msg, int n, const char *t) { if (msg && n > 0) snprintf(msg, (size_t)n, "%s", t); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ code page 437, upper half */

static const unsigned short CP437[128] = {
    0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
    0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xA2, 0xA3, 0xA5, 0x20A7, 0x192,
    0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0x2310, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x3B1, 0xDF, 0x393, 0x3C0, 0x3A3, 0x3C3, 0xB5, 0x3C4, 0x3A6, 0x398, 0x3A9, 0x3B4, 0x221E, 0x3C6, 0x3B5, 0x2229,
    0x2261, 0xB1, 0x2265, 0x2264, 0x2320, 0x2321, 0xF7, 0x2248, 0xB0, 0x2219, 0xB7, 0x221A, 0x207F, 0xB2, 0x25A0, 0xA0};

static const int EXTRA[] = {0x152, 0x153, 0x20AC, 0x2190, 0x2191, 0x2192, 0x2193};

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int sheet_candidate_codes(int *out, int max)
{
    int tmp[512], n = 0;
    for (int c = 32; c < 127; c++) tmp[n++] = c;
    for (int c = 0xA0; c < 0x100; c++) tmp[n++] = c;
    for (int i = 0; i < 128; i++) tmp[n++] = CP437[i];
    for (size_t i = 0; i < sizeof EXTRA / sizeof EXTRA[0]; i++) tmp[n++] = EXTRA[i];
    qsort(tmp, (size_t)n, sizeof(int), cmp_int);
    int m = 0;
    for (int i = 0; i < n; i++) if (!i || tmp[i] != tmp[i - 1]) { if (m < max) out[m] = tmp[i]; m++; }
    return m < max ? m : max;
}

/* ------------------------------------------------------------------ reference glyphs */

typedef struct Ref { int code; float f[FW * FH]; float t, b, rw; } Ref;   /* shape; top, bottom and width in line heights */
typedef struct RefRaw { int code, w, h, left, top; unsigned char *pix; } RefRaw;
struct SheetRefs { Ref *v; int n, cap; RefRaw *raw; int nraw, rawcap; };

SheetRefs *sheet_refs_new(void) { return (SheetRefs *)calloc(1, sizeof(SheetRefs)); }

void sheet_refs_free(SheetRefs *r)
{
    if (!r) return;
    for (int i = 0; i < r->nraw; i++) free(r->raw[i].pix);
    free(r->raw);
    free(r->v);
    free(r);
}

int sheet_refs_count(const SheetRefs *r) { return r ? r->n : 0; }

void sheet_refs_begin(SheetRefs *r)
{
    for (int i = 0; i < r->nraw; i++) free(r->raw[i].pix);
    r->nraw = 0;
}

void sheet_refs_add(SheetRefs *r, int code, const unsigned char *pix, int w, int h, int left, int top)
{
    if (w <= 0 || h <= 0 || !pix) return;
    if (r->nraw >= r->rawcap) {
        int nc = r->rawcap ? r->rawcap * 2 : 256;
        RefRaw *nv = (RefRaw *)realloc(r->raw, sizeof(RefRaw) * (size_t)nc);
        if (!nv) return;
        r->raw = nv;
        r->rawcap = nc;
    }
    RefRaw *e = &r->raw[r->nraw];
    e->pix = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (!e->pix) return;
    for (int i = 0; i < w * h; i++) e->pix[i] = pix[i] ? 1 : 0;
    e->code = code; e->w = w; e->h = h; e->left = left; e->top = top;
    r->nraw++;
}

/* the shape of an ink mask (stride given, box x0,y0,w,h) on the FW x FH grid: how much of each grid cell is inked */
static void shape_of(const unsigned char *m, int stride, int x0, int y0, int w, int h, float *f)
{
    float acc[FW * FH], area[FW * FH];
    memset(acc, 0, sizeof acc);
    memset(area, 0, sizeof area);
    for (int y = 0; y < h; y++) {
        /* a source pixel covers [y, y+1) of h: spread it over the grid rows it overlaps */
        float gy0 = (float)y * FH / (float)h, gy1 = (float)(y + 1) * FH / (float)h;
        for (int gy = (int)gy0; gy < FH && (float)gy < gy1; gy++) {
            float oy = fminf(gy1, (float)gy + 1) - fmaxf(gy0, (float)gy);
            if (oy <= 0) continue;
            for (int x = 0; x < w; x++) {
                float gx0 = (float)x * FW / (float)w, gx1 = (float)(x + 1) * FW / (float)w;
                int on = m[(size_t)(y0 + y) * (size_t)stride + (size_t)(x0 + x)] != 0;
                for (int gx = (int)gx0; gx < FW && (float)gx < gx1; gx++) {
                    float ox = fminf(gx1, (float)gx + 1) - fmaxf(gx0, (float)gx);
                    if (ox <= 0) continue;
                    area[gy * FW + gx] += ox * oy;
                    if (on) acc[gy * FW + gx] += ox * oy;
                }
            }
        }
    }
    for (int i = 0; i < FW * FH; i++) f[i] = area[i] > 0 ? acc[i] / area[i] : 0;
}

static int median_int(int *v, int n)
{
    if (n <= 0) return 0;
    qsort(v, (size_t)n, sizeof(int), cmp_int);
    return v[n / 2];
}

void sheet_refs_end(SheetRefs *r)
{
    int tops[128], nt = 0;
    for (int i = 0; i < r->nraw && nt < 128; i++) {
        int c = r->raw[i].code;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) tops[nt++] = r->raw[i].top;
    }
    int line = nt ? median_int(tops, nt) : 0;
    if (line <= 0) { for (int i = 0; i < r->nraw; i++) line = imax(line, r->raw[i].top); }
    if (line <= 0) line = 1;
    for (int i = 0; i < r->nraw; i++) {
        RefRaw *e = &r->raw[i];
        if (r->n >= r->cap) {
            int nc = r->cap ? r->cap * 2 : 512;
            Ref *nv = (Ref *)realloc(r->v, sizeof(Ref) * (size_t)nc);
            if (!nv) break;
            r->v = nv;
            r->cap = nc;
        }
        Ref *o = &r->v[r->n++];
        o->code = e->code;
        shape_of(e->pix, e->w, 0, 0, e->w, e->h, o->f);
        o->t = (float)e->top / (float)line;
        o->b = (float)(e->top - e->h) / (float)line;
        o->rw = (float)e->w / (float)line;
    }
    sheet_refs_begin(r);
}

/* ------------------------------------------------------------------ pixels */

static uint32_t rgba_at(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int cdist(uint32_t a, uint32_t b)
{
    int d = abs((int)(a >> 24) - (int)(b >> 24)), e = abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255)), f = abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255));
    return imax(d, imax(e, f));
}

typedef struct CEnt { uint32_t c; long n; } CEnt;
typedef struct CMap { CEnt *v; int n, cap; int *h; int hn; } CMap;

static void cmap_free(CMap *m) { free(m->v); free(m->h); memset(m, 0, sizeof *m); }

static int cmap_add(CMap *m, uint32_t c, long n)
{
    if ((m->n + 1) * 2 > m->hn) {
        int nh = m->hn ? m->hn * 2 : 1024;
        int *h = (int *)calloc((size_t)nh, sizeof(int));
        if (!h) return -1;
        free(m->h);
        m->h = h;
        m->hn = nh;
        for (int i = 0; i < m->n; i++) {
            int s = (int)((m->v[i].c * 2654435761u) % (unsigned)nh);
            while (m->h[s]) s = (s + 1) % nh;
            m->h[s] = i + 1;
        }
    }
    int s = (int)((c * 2654435761u) % (unsigned)m->hn);
    while (m->h[s] && m->v[m->h[s] - 1].c != c) s = (s + 1) % m->hn;
    if (m->h[s]) { m->v[m->h[s] - 1].n += n; return m->h[s] - 1; }
    if (m->n >= m->cap) {
        int nc = m->cap ? m->cap * 2 : 256;
        CEnt *nv = (CEnt *)realloc(m->v, sizeof(CEnt) * (size_t)nc);
        if (!nv) return -1;
        m->v = nv;
        m->cap = nc;
    }
    m->v[m->n].c = c;
    m->v[m->n].n = n;
    m->h[s] = ++m->n;
    return m->n - 1;
}

static int cent_cmp(const void *a, const void *b)
{
    const CEnt *x = (const CEnt *)a, *y = (const CEnt *)b;
    return x->n != y->n ? (x->n < y->n ? 1 : -1) : (x->c < y->c ? -1 : x->c > y->c);
}

/* ------------------------------------------------------------------ scale */

/* how much of `t` (transition counts per position, n of them) lies on a lattice of pitch s; *phase gets the best phase */
static double lattice_share(const long *t, int n, int s, int *phase)
{
    long tot = 0, best = -1;
    long sum[MAX_SCALE + 1];
    memset(sum, 0, sizeof sum);
    for (int i = 1; i < n; i++) { tot += t[i]; sum[i % s] += t[i]; }
    for (int p = 0; p < s; p++) if (sum[p] > best) { best = sum[p]; *phase = p; }
    return tot > 0 ? (double)best / (double)tot : 0;
}

/* picture pixels per font pixel: the largest s for which (nearly) every change of colour lies on a lattice of pitch s */
static int find_scale(const uint32_t *px, const unsigned char *ink, int w, int h, int noisy, int *phx, int *phy)
{
    long *tx = (long *)calloc((size_t)w + 1, sizeof(long)), *ty = (long *)calloc((size_t)h + 1, sizeof(long));
    int found = 1;
    *phx = *phy = 0;
    if (!tx || !ty) { free(tx); free(ty); return 1; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * (size_t)w + (size_t)x;
            if (x && (noisy ? ink[i] != ink[i - 1] : px[i] != px[i - 1])) tx[x]++;
            if (y && (noisy ? ink[i] != ink[i - (size_t)w] : px[i] != px[i - (size_t)w])) ty[y]++;
        }
    double need = noisy ? 0.86 : 0.985;
    for (int s = imin(MAX_SCALE, imin(w, h) / 4); s >= 2; s--) {
        int px_ = 0, py_ = 0;
        if (lattice_share(tx, w, s, &px_) >= need && lattice_share(ty, h, s, &py_) >= need) { found = s; *phx = px_; *phy = py_; break; }
    }
    free(tx);
    free(ty);
    return found;
}

/* ------------------------------------------------------------------ the working picture */

typedef struct Seg { int a, b; } Seg;       /* inclusive */
typedef struct Band { int y0, y1; Seg *seg; int nseg; } Band;

static int row_has_ink(const Sheet *s, int y, int x0, int x1)
{
    const unsigned char *r = s->ink + (size_t)y * (size_t)s->w;
    for (int x = x0; x <= x1; x++) if (r[x]) return 1;
    return 0;
}

static int col_has_ink(const Sheet *s, int x, int y0, int y1)
{
    for (int y = y0; y <= y1; y++) if (s->ink[(size_t)y * (size_t)s->w + (size_t)x]) return 1;
    return 0;
}

static void ink_box(const Sheet *s, SheetCell *c)
{
    int x0 = c->x + c->w, x1 = -1, y0 = c->y + c->h, y1 = -1;
    for (int y = c->y; y < c->y + c->h; y++)
        for (int x = c->x; x < c->x + c->w; x++)
            if (s->ink[(size_t)y * (size_t)s->w + (size_t)x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < 0) { c->ix = c->x; c->iy = c->y; c->iw = c->ih = 0; return; }
    c->ix = x0; c->iy = y0; c->iw = x1 - x0 + 1; c->ih = y1 - y0 + 1;
}

static int add_cell(Sheet *s, int x, int y, int w, int h, int row)
{
    if (s->ncells >= MAX_CELLS || w <= 0 || h <= 0) return -1;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return -1;
    SheetCell *nv = (SheetCell *)realloc(s->cells, sizeof(SheetCell) * (size_t)(s->ncells + 1));
    if (!nv) return -1;
    s->cells = nv;
    SheetCell *c = &s->cells[s->ncells++];
    memset(c, 0, sizeof *c);
    c->x = x; c->y = y; c->w = w; c->h = h; c->row = row; c->code = -1;
    ink_box(s, c);
    return 0;
}

/* ---- the marker row ---- */

/* A strip with a marker row: single pixels of one colour in the top (or bottom) row, each above (below) a column that separates two
 * glyphs.  A glyph is the columns between two markers; the marker's own column belongs to no glyph.  A pixel of another colour in
 * that row is ink of a glyph (a tall glyph may reach it).  Returns the number of markers. */
static int find_marker(Sheet *s, int *starts, int max)
{
    for (int pass = 0; pass < 2; pass++) {
        int y = pass ? s->h - 1 : 0, n = 0, single = 0, runs = 0;
        const unsigned char *r = s->ink + (size_t)y * (size_t)s->w;
        const uint32_t *pr = s->px + (size_t)y * (size_t)s->w;
        if (s->h < 4) return 0;
        /* the colour of the markers: the commonest among the single pixels of the row */
        uint32_t cols[8];
        int cnt[8], nc = 0;
        for (int x = 0; x < s->w; x++) {
            if (!r[x]) continue;
            if (x > 0 && r[x - 1]) continue;
            runs++;
            if (x + 1 < s->w && r[x + 1]) continue;
            single++;
            int k = 0;
            while (k < nc && cols[k] != pr[x]) k++;
            if (k == nc) { if (nc == 8) continue; cols[nc] = pr[x]; cnt[nc++] = 0; }
            cnt[k]++;
        }
        if (single < 6 || single * 10 < runs * 9) continue;   /* (nearly) nothing but single pixels */
        int best = 0;
        for (int k = 1; k < nc; k++) if (cnt[k] > cnt[best]) best = k;
        if (cnt[best] * 10 < single * 9) continue;
        for (int x = 0; x < s->w; x++)
            if (r[x] && pr[x] == cols[best] && !(x > 0 && r[x - 1]) && !(x + 1 < s->w && r[x + 1])) { if (n < max) starts[n] = x; n++; }
        if (n < 6 || n >= max || starts[0] > 1) continue;
        /* the columns under the markers are (nearly) empty, and the stretches between them hold the glyphs */
        long under = 0, inked = 0;
        for (int i = 0; i < n; i++) {
            for (int yy = 0; yy < s->h; yy++) if (yy != y && s->ink[(size_t)yy * (size_t)s->w + (size_t)starts[i]]) under++;
            int a = starts[i] + 1, b = (i + 1 < n ? starts[i + 1] : s->w) - 1, any = 0;
            for (int x = a; x <= b && !any; x++) any = col_has_ink(s, x, pass ? 0 : 1, pass ? s->h - 2 : s->h - 1);
            inked += any;
        }
        if (inked * 10 < (long)n * 6 || under > (long)n) continue;
        s->markerRow = y;
        for (int i = 0; i < n; i++) s->ink[(size_t)y * (size_t)s->w + (size_t)starts[i]] = 0;   /* a marker is no ink */
        return n;
    }
    return 0;
}

/* ---- bands (text rows) and segments ---- */

static int find_bands(const Sheet *s, Band *bands, int max)
{
    int n = 0, y = 0;
    while (y < s->h && n < max) {
        while (y < s->h && !row_has_ink(s, y, 0, s->w - 1)) y++;
        if (y >= s->h) break;
        int y0 = y;
        while (y < s->h && row_has_ink(s, y, 0, s->w - 1)) y++;
        bands[n].y0 = y0; bands[n].y1 = y - 1; bands[n].seg = NULL; bands[n].nseg = 0;
        n++;
    }
    if (n >= 3) {   /* a band far lower than the others (dots, accents) belongs to its closest neighbour */
        int hs[256], k = 0;
        for (int i = 0; i < n && k < 256; i++) hs[k++] = bands[i].y1 - bands[i].y0 + 1;
        int med = median_int(hs, k);
        for (int i = 0; i < n; i++) {
            int hgt = bands[i].y1 - bands[i].y0 + 1;
            if (hgt * 2 > med) continue;
            int up = i > 0 ? bands[i].y0 - bands[i - 1].y1 - 1 : 1 << 20, dn = i + 1 < n ? bands[i + 1].y0 - bands[i].y1 - 1 : 1 << 20;
            if (imin(up, dn) * 3 > med) continue;
            if (up <= dn) { bands[i - 1].y1 = bands[i].y1; memmove(&bands[i], &bands[i + 1], sizeof(Band) * (size_t)(n - i - 1)); }
            else { bands[i + 1].y0 = bands[i].y0; memmove(&bands[i], &bands[i + 1], sizeof(Band) * (size_t)(n - i - 1)); }
            n--;
            i--;
        }
    }
    return n;
}

static int find_segs(const Sheet *s, Band *b)
{
    int x = 0, cap = 0;
    b->nseg = 0;
    while (x < s->w) {
        while (x < s->w && !col_has_ink(s, x, b->y0, b->y1)) x++;
        if (x >= s->w) break;
        int a = x;
        while (x < s->w && col_has_ink(s, x, b->y0, b->y1)) x++;
        if (b->nseg >= cap) {
            cap = cap ? cap * 2 : 64;
            Seg *nv = (Seg *)realloc(b->seg, sizeof(Seg) * (size_t)cap);
            if (!nv) return -1;
            b->seg = nv;
        }
        b->seg[b->nseg].a = a;
        b->seg[b->nseg].b = x - 1;
        b->nseg++;
    }
    return 0;
}

/* how many of the intervals (those not far longer than most: such have run together) do not lie inside one cell */
static int pitch_bad(const Seg *v, int n, int cw, int o, int longest, int stop)
{
    int bad = 0;
    for (int i = 0; i < n && bad <= stop; i++) {
        if (v[i].b - v[i].a + 1 > longest) continue;
        if ((v[i].a - o + cw) / cw != (v[i].b - o + cw) / cw) bad++;
    }
    return bad;
}

/* The smallest pitch for which every interval lies inside one cell; a pitch a little larger that divides the length, with no margin,
 * is taken instead when there is one.  1 when found. */
static int infer_pitch(const Seg *v, int n, const Band *bands, int nb, int len, int *pitch, int *off)
{
    if (n < 4) return 0;
    int *ls = (int *)malloc(sizeof(int) * (size_t)n), maxLen = 0;
    if (!ls) return 0;
    for (int i = 0; i < n; i++) ls[i] = v[i].b - v[i].a + 1;
    int med = median_int(ls, n), longest = med + med * 3 / 4 + 1, counted = 0;
    free(ls);
    for (int i = 0; i < n; i++) { int l = v[i].b - v[i].a + 1; if (l <= longest) { maxLen = imax(maxLen, l); counted++; } }
    if (counted < 4) return 0;
    int allow = counted / 40;
    for (int cw = imax(maxLen, 3); cw <= len / 3; cw++) {
        int bestO = -1;
        for (int o = 0; o < cw && bestO < 0; o++) if (pitch_bad(v, n, cw, o, longest, allow) <= allow) bestO = o;   /* no margin first, else the smallest */
        if (bestO < 0) continue;
        /* how many cells hold more than one interval of a row: glyphs of several parts are few */
        int multi = 0, occ = 0;
        for (int r = 0; r < nb; r++) {
            int last = -(1 << 20), cnt = 0;
            for (int i = 0; i < bands[r].nseg; i++) {
                int cell = (bands[r].seg[i].a - bestO + cw) / cw;
                if (cell == last) cnt++;
                else { if (cnt > 1) multi++; occ++; cnt = 1; last = cell; }
            }
            if (cnt > 1) multi++;
        }
        if (multi * 100 > occ * 22) continue;
        *pitch = cw;
        *off = bestO;
        for (int c2 = cw; c2 <= cw + cw / 2 + 1; c2++)   /* cells that divide the picture, from its edge */
            if ((len % c2 == 0 || len % c2 == 1) && pitch_bad(v, n, c2, 0, longest, allow) <= allow) { *pitch = c2; *off = 0; break; }
        return 1;
    }
    return 0;
}

/* The period of a profile, among the divisors of its length.  hint 0: the smallest that repeats nearly as well as the best.
 * hint > 0 (the other side of the cell): of those that repeat well, the one closest to it. */
static int find_period(const double *p, int n, int minP, int hint)
{
    double bestA = -2, a[1024];
    int cand[1024], nc = 0;
    for (int d = imax(minP, 3); d <= n / 2 && nc < 1024; d++) {
        if (n % d) continue;
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        int m = n - d;
        for (int i = 0; i < m; i++) { double x = p[i], y = p[i + d]; sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; }
        double vx = sxx - sx * sx / m, vy = syy - sy * sy / m, cov = sxy - sx * sy / m;
        double r = vx > 1e-9 && vy > 1e-9 ? cov / sqrt(vx * vy) : 0;
        cand[nc] = d;
        a[nc++] = r;
        if (r > bestA) bestA = r;
    }
    if (bestA < 0.4) return 0;
    if (hint <= 0) {
        for (int i = 0; i < nc; i++) if (a[i] >= bestA - 0.06) return cand[i];
        return 0;
    }
    int best = 0;
    double bd = 1e9;
    for (int i = 0; i < nc; i++) {
        double q = (double)cand[i] / hint;
        if (a[i] < fmax(0.3, bestA - 0.3) || q < 0.45 || q > 2.6) continue;
        double d = fabs(log(q));
        if (d < bd) { bd = d; best = cand[i]; }
    }
    return best;
}

/* The height of the cells from the text rows: the smallest pitch for which every row lies inside one cell (rows that have run
 * together, far taller than the others, do not count); cells that start at the top and divide the picture are preferred. */
static int infer_rows(const Band *b, int nb, int h, int *pitch, int *off)
{
    int hs[256], n = 0, maxH = 0;
    if (nb < 2) return 0;
    for (int i = 0; i < nb && n < 256; i++) hs[n++] = b[i].y1 - b[i].y0 + 1;
    int med = median_int(hs, n);
    for (int i = 0; i < nb; i++) { int hh = b[i].y1 - b[i].y0 + 1; if (hh * 2 <= med * 3) maxH = imax(maxH, hh); }
    int found = 0;
    for (int pass = 0; pass < 2 && !found; pass++)      /* first: no margin and a whole number of cells */
        for (int ch = imax(maxH, 3); ch <= h / 2 && !found; ch++) {
            if (!pass && h % ch) continue;
            for (int o = 0; o < (pass ? ch : 1) && !found; o++) {
                int ok = 1, counted = 0;
                for (int i = 0; i < nb && ok; i++) {
                    int hh = b[i].y1 - b[i].y0 + 1;
                    if (hh * 2 > med * 3) continue;
                    counted++;
                    if ((b[i].y0 - o + ch) / ch != (b[i].y1 - o + ch) / ch) ok = 0;
                }
                if (ok && counted >= 2) { *pitch = ch; *off = o; found = 1; }
            }
        }
    return found;
}

static void free_bands(Band *b, int n) { for (int i = 0; i < n; i++) free(b[i].seg); }

static int layout_marker(Sheet *s)
{
    int starts[MAX_CELLS];
    int n = find_marker(s, starts, MAX_CELLS);
    if (!n) return 0;
    for (int i = 0; i < n; i++) {
        int a = starts[i] + 1, b = (i + 1 < n ? starts[i + 1] : s->w);   /* the columns between two markers */
        if (b <= a) continue;
        /* ink under the next marker (a shadow that spills over) stays with the glyph it is joined to: this one */
        if (i + 1 < n && col_has_ink(s, b, 0, s->h - 1) && col_has_ink(s, b - 1, 0, s->h - 1)) b++;
        add_cell(s, a, 0, b - a, s->h, 0);
    }
    while (s->ncells > 1 && s->cells[s->ncells - 1].iw == 0) s->ncells--;   /* what follows the last glyph */
    s->layout = SHEET_MARKER;
    s->nrows = 1;
    return 1;
}

static void grid_cells(Sheet *s)
{
    int row = 0;
    s->ncells = 0;
    for (int y = s->offY; y + s->cellH <= s->h + s->cellH / 3 && y < s->h; y += s->cellH, row++)
        for (int x = s->offX; x + s->cellW <= s->w + s->cellW / 3 && x < s->w; x += s->cellW) add_cell(s, x, y, s->cellW, s->cellH, row);
    s->nrows = row;
    s->layout = SHEET_GRID;
}

static int layout_auto(Sheet *s, const SheetOpts *o)
{
    Band bands[256];
    int nb = find_bands(s, bands, 256);
    if (!nb) return -1;
    int total = 0;
    for (int i = 0; i < nb; i++) { if (find_segs(s, &bands[i]) < 0) { free_bands(bands, nb); return -1; } total += bands[i].nseg; }
    int want = o ? o->layout : SHEET_AUTO;
    int cw = 0, ox = 0, ch = 0, oy = 0;
    if (want != SHEET_ROWS && (!s->noisy || want == SHEET_GRID)) {   /* (a photograph has no cells to trust) */
        double *pc = (double *)calloc((size_t)s->w, sizeof(double)), *pr = (double *)calloc((size_t)s->h, sizeof(double));
        Seg *all = (Seg *)malloc(sizeof(Seg) * (size_t)(total + 1));
        if (pc && pr && all) {
            for (int y = 0; y < s->h; y++)
                for (int x = 0; x < s->w; x++) {
                    uint32_t c = s->px[(size_t)y * (size_t)s->w + (size_t)x];
                    double v = s->ink[(size_t)y * (size_t)s->w + (size_t)x] ? 64 + ((c >> 24) + ((c >> 16) & 255) + ((c >> 8) & 255)) / 3.0 : 0;
                    pc[x] += v;
                    pr[y] += v;
                }
            int n = 0, hs[256], k = 0, widest = 0;
            for (int i = 0; i < nb; i++) {
                if (k < 256) hs[k++] = bands[i].y1 - bands[i].y0 + 1;
                for (int j = 0; j < bands[i].nseg; j++) { all[n++] = bands[i].seg[j]; widest = imax(widest, bands[i].seg[j].b - bands[i].seg[j].a + 1); }
            }
            int medH = median_int(hs, k);
            /* sorted by start, so that the parts of one cell are neighbours */
            for (int i = 1; i < n; i++) { Seg t = all[i]; int j = i; while (j > 0 && all[j - 1].a > t.a) { all[j] = all[j - 1]; j--; } all[j] = t; }
            /* across: from the gaps between the glyphs; glyphs that fill their cells leave none, then the period tells */
            int merged = total < nb * 3 || widest * 3 > s->w;
            if (!merged && infer_pitch(all, n, bands, nb, s->w, &cw, &ox) && cw * 4 < medH) cw = 0;   /* slivers are no cells */
            if (!cw && (merged || want == SHEET_GRID)) { cw = find_period(pc, s->w, 4, 0); ox = 0; }
            if (cw) {
                /* down: from the text rows; rows that touch run together, then the period tells (with the width as a hint) */
                if (!infer_rows(bands, nb, s->h, &ch, &oy)) { ch = nb == 1 ? s->h : 0; oy = 0; }
                if (!ch || ch * 10 > cw * 16) {
                    int per = find_period(pr, s->h, 4, cw);
                    if (per && (!ch || per < ch)) { ch = per; oy = 0; }
                }
            }
        }
        free(pc);
        free(pr);
        free(all);
    }
    int grid = cw > 0 && ch > 0;
    if (grid) {
        s->cellW = cw; s->cellH = ch; s->offX = ox; s->offY = oy;
        grid_cells(s);
        /* a grid of mostly blank cells is no grid (a photograph cut into slivers) */
        int last = -1, blank = 0;
        for (int i = 0; i < s->ncells; i++) if (s->cells[i].iw) last = i;
        for (int i = 0; i <= last; i++) blank += !s->cells[i].iw;
        if (want != SHEET_GRID && (last < 0 || blank * 100 > (last + 1) * 45)) { grid = 0; s->ncells = 0; s->cellW = s->cellH = s->offX = s->offY = 0; }
    }
    if (!grid) {
        int *ws = (int *)malloc(sizeof(int) * (size_t)(total + 1)), nw = 0;
        for (int i = 0; ws && i < nb; i++) for (int k = 0; k < bands[i].nseg; k++) ws[nw++] = bands[i].seg[k].b - bands[i].seg[k].a + 1;
        int medW = ws && nw ? median_int(ws, nw) : 0;
        free(ws);
        for (int i = 0; i < nb; i++)
            for (int k = 0; k < bands[i].nseg; k++) {
                int a = bands[i].seg[k].a, b = bands[i].seg[k].b, wd = b - a + 1;
                int parts = medW >= 6 && nw >= 8 && wd * 10 >= medW * 19 ? (wd + medW / 2) / medW : 1;
                /* glyphs that touch: cut near the even division, where the fewest pixels are inked */
                int from = a;
                for (int q = 1; q < parts; q++) {
                    int mid = a + wd * q / parts, bestX = mid, bestN = 1 << 30;
                    for (int x = imax(from + 2, mid - medW / 4); x <= imin(b - 2, mid + medW / 4); x++) {
                        int cnt = 0;
                        for (int y = bands[i].y0; y <= bands[i].y1; y++) cnt += s->ink[(size_t)y * (size_t)s->w + (size_t)x];
                        if (cnt < bestN || (cnt == bestN && abs(x - mid) < abs(bestX - mid))) { bestN = cnt; bestX = x; }
                    }
                    add_cell(s, from, bands[i].y0, bestX - from, bands[i].y1 - bands[i].y0 + 1, i);
                    from = bestX;
                }
                add_cell(s, from, bands[i].y0, b - from + 1, bands[i].y1 - bands[i].y0 + 1, i);
            }
        s->layout = SHEET_ROWS;
        s->nrows = nb;
    }
    free_bands(bands, nb);
    return 0;
}

/* ------------------------------------------------------------------ metrics */

static int mode_of(const int *v, int n, int lo, int hi)
{
    int best = lo, bestN = -1;
    for (int c = lo; c <= hi; c++) {
        int k = 0;
        for (int i = 0; i < n; i++) k += v[i] == c;
        if (k > bestN) { bestN = k; best = c; }
    }
    return best;
}

/* baseline and top of the capitals, as rows from the top of a cell; ascent, descent */
static void find_metrics(Sheet *s, int *baseline, int *capTop)
{
    int n = 0, maxH = 1;
    int *bot = (int *)malloc(sizeof(int) * (size_t)(s->ncells + 1)), *top = (int *)malloc(sizeof(int) * (size_t)(s->ncells + 1));
    *baseline = 1;
    *capTop = 0;
    if (!bot || !top) { free(bot); free(top); return; }
    for (int i = 0; i < s->ncells; i++) {
        const SheetCell *c = &s->cells[i];
        maxH = imax(maxH, c->h);
        if (!c->iw) continue;
        bot[n] = c->iy + c->ih - c->y;
        top[n] = c->iy - c->y;
        n++;
    }
    if (n) {
        *baseline = mode_of(bot, n, 0, maxH);
        int m = 0;
        for (int i = 0; i < n; i++) if (bot[i] == *baseline) top[m++] = top[i];
        *capTop = m ? mode_of(top, m, 0, maxH) : 0;
        if (*capTop >= *baseline) *capTop = 0;
        int minTop = maxH, maxBot = 0;
        for (int i = 0; i < s->ncells; i++) {
            const SheetCell *c = &s->cells[i];
            if (!c->iw) continue;
            minTop = imin(minTop, c->iy - c->y);
            maxBot = imax(maxBot, c->iy + c->ih - c->y);
        }
        s->lineTop = minTop;
        s->ascent = *baseline - minTop;
        s->descent = maxBot - *baseline;
        if (s->descent < 0) s->descent = 0;
    }
    free(bot);
    free(top);
}

/* ------------------------------------------------------------------ recognising the characters */

typedef struct Cand { int code; int succA, succB, succC; int upper; } Cand;   /* successors: code + 1, the next of CP437, 0 after 9 */
typedef struct Box { int ix, iy, iw, ih; } Box;

static int cand_index(const Cand *c, int n, int code)
{
    int lo = 0, hi = n;
    while (lo < hi) { int mid = (lo + hi) / 2; if (c[mid].code < code) lo = mid + 1; else hi = mid; }
    return lo < n && c[lo].code == code ? lo : -1;
}

#define RUN_BONUS 0.5f         /* what continuing the run of the glyph before is worth */
#define UP_BONUS 0.04f       /* what a higher code than the glyph before is worth */
#define BLANK_SKIP 0.6f      /* a blank cell that holds no place (the end of a row, a gap in the sheet) */
#define SCORE_FLOOR 0.2f     /* a glyph that looks like nothing still takes a place in a run */

/* How well each cell matches each code when its glyph is read from one mask (the ink, or one colour of it); em keeps the best of
 * the masks it has seen (N * K values, -1e9 where no mask said anything yet). */
static void emissions(const Sheet *s, const SheetRefs *refs, const unsigned char *mask, const Cand *cd, int K, float *em)
{
    int N = s->ncells, n = 0, maxH = 1;
    Box *bx = (Box *)calloc((size_t)N, sizeof(Box));
    int *bot = (int *)malloc(sizeof(int) * (size_t)N), *top = (int *)malloc(sizeof(int) * (size_t)N);
    float *e = (float *)malloc(sizeof(float) * (size_t)K);
    if (!bx || !bot || !top || !e) goto done;
    /* the boxes of this mask, and its line: where most glyphs end and where most of those start */
    for (int i = 0; i < N; i++) {
        const SheetCell *c = &s->cells[i];
        int x0 = c->x + c->w, x1 = -1, y0 = c->y + c->h, y1 = -1;
        for (int y = c->y; y < c->y + c->h; y++)
            for (int x = c->x; x < c->x + c->w; x++)
                if (mask[(size_t)y * (size_t)s->w + (size_t)x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        maxH = imax(maxH, c->h);
        if (x1 < 0) continue;
        bx[i].ix = x0; bx[i].iy = y0; bx[i].iw = x1 - x0 + 1; bx[i].ih = y1 - y0 + 1;
        bot[n] = y1 + 1 - c->y;
        top[n] = y0 - c->y;
        n++;
    }
    if (!n) goto done;
    int baseline = mode_of(bot, n, 0, maxH), m = 0;
    for (int i = 0; i < n; i++) if (bot[i] == baseline) top[m++] = top[i];
    int capTop = m ? mode_of(top, m, 0, maxH) : 0;
    if (capTop >= baseline) capTop = 0;
    float line = (float)imax(baseline - capTop, 1);
    for (int i = 0; i < N; i++) {
        const SheetCell *c = &s->cells[i];
        float *out = em + (size_t)i * (size_t)K;
        if (!bx[i].iw) continue;   /* this mask has nothing of the glyph */
        float f[FW * FH];
        shape_of(mask, s->w, bx[i].ix, bx[i].iy, bx[i].iw, bx[i].ih, f);
        float t = (float)(baseline - (bx[i].iy - c->y)) / line, b = (float)(baseline - (bx[i].iy + bx[i].ih - c->y)) / line, rw = (float)bx[i].iw / line;
        for (int k = 0; k < K; k++) e[k] = cd[k].code == 32 ? -1.0f : SCORE_FLOOR;
        for (int r = 0; r < refs->n; r++) {
            const Ref *q = &refs->v[r];
            int k = cand_index(cd, K, q->code);
            if (k < 0 || q->code == 32) continue;
            float d = 0;
            for (int j = 0; j < FW * FH; j++) d += fabsf(f[j] - q->f[j]);
            float sc = 1.0f - d / (float)(FW * FH) - 0.45f * fminf(fabsf(t - q->t) + fabsf(b - q->b) + 0.5f * fabsf(rw - q->rw), 1.5f);
            if (sc > e[k]) e[k] = sc;
        }
        for (int k = 0; k < K; k++)   /* capitals in the places of the small letters */
            if (cd[k].upper >= 0 && e[cd[k].upper] * 0.97f > e[k]) e[k] = e[cd[k].upper] * 0.97f;
        for (int k = 0; k < K; k++) if (e[k] > out[k]) out[k] = e[k];
    }
done:
    free(bx); free(bot); free(top); free(e);
}

static int follows(const Cand *cd, int a, int b) { return a >= 0 && b >= 0 && (cd[a].succA == b || cd[a].succB == b || cd[a].succC == b); }

/* The best sequence of codes for the cells: a glyph that continues the run of the one before it is favoured; a blank cell holds the
 * place of a missing character or no place at all. */
static void decode(Sheet *s, const Cand *cd, int K, const float *em)
{
    int N = s->ncells;
    float *V = (float *)malloc(sizeof(float) * (size_t)K), *W = (float *)malloc(sizeof(float) * (size_t)K);
    short *back = (short *)malloc(sizeof(short) * (size_t)K * (size_t)N);
    char *skip = (char *)calloc((size_t)K * (size_t)N, 1), *used = (char *)calloc((size_t)K, 1);
    int *ki = (int *)malloc(sizeof(int) * (size_t)N);
    if (!V || !W || !back || !skip || !used || !ki) goto done;
    for (int k = 0; k < K; k++) V[k] = em[k];
    for (int i = 1; i < N; i++) {
        const float *e = em + (size_t)i * (size_t)K;
        short *bk = back + (size_t)i * (size_t)K;
        int bestAny = 0;
        for (int k = 1; k < K; k++) if (V[k] > V[bestAny]) bestAny = k;
        for (int k = 0; k < K; k++) { W[k] = V[bestAny]; bk[k] = (short)bestAny; }
        int pre = -1;   /* the best of the codes below k */
        for (int k = 0; k < K; k++) {
            if (pre >= 0 && V[pre] + UP_BONUS > W[k]) { W[k] = V[pre] + UP_BONUS; bk[k] = (short)pre; }
            if (pre < 0 || V[k] > V[pre]) pre = k;
        }
        for (int k = 0; k < K; k++) {
            int nx[3] = {cd[k].succA, cd[k].succB, cd[k].succC};
            for (int q = 0; q < 3; q++) if (nx[q] >= 0 && V[k] + RUN_BONUS > W[nx[q]]) { W[nx[q]] = V[k] + RUN_BONUS; bk[nx[q]] = (short)k; }
        }
        for (int k = 0; k < K; k++) {
            float take = W[k] + e[k];
            if (!s->cells[i].iw && V[k] + BLANK_SKIP > take + 1e-4f) { skip[(size_t)i * (size_t)K + (size_t)k] = 1; bk[k] = (short)k; W[k] = V[k] + BLANK_SKIP; }
            else W[k] = take;
        }
        for (int k = 0; k < K; k++) V[k] = W[k];
    }
    int k = 0;
    for (int j = 1; j < K; j++) if (V[j] > V[k]) k = j;
    for (int i = N - 1; i >= 0; i--) { ki[i] = skip[(size_t)i * (size_t)K + (size_t)k] ? -1 : k; if (i) k = back[(size_t)i * (size_t)K + (size_t)k]; }
    /* a code given twice: the glyph that stands in a run keeps it, the other takes its best code that is still free */
    for (int i = 0; i < N; i++) if (s->cells[i].iw && ki[i] >= 0) used[ki[i]]++;
    for (int i = 0; i < N; i++) {
        if (!s->cells[i].iw || ki[i] < 0 || used[ki[i]] < 2) continue;
        int kk = ki[i], keep = -1;
        float bestV = -1e9f;
        for (int j = 0; j < N; j++) {
            if (ki[j] != kk || !s->cells[j].iw) continue;
            float v = em[(size_t)j * (size_t)K + (size_t)kk];
            int pj = j - 1, nj = j + 1;
            while (pj >= 0 && ki[pj] < 0) pj--;
            while (nj < N && ki[nj] < 0) nj++;
            if (pj >= 0 && follows(cd, ki[pj], kk)) v += RUN_BONUS;
            if (nj < N && follows(cd, kk, ki[nj])) v += RUN_BONUS;
            if (v > bestV) { bestV = v; keep = j; }
        }
        for (int j = 0; j < N; j++) {
            if (ki[j] != kk || !s->cells[j].iw || j == keep) continue;
            int alt = -1;
            for (int q = 0; q < K; q++)
                if (!used[q] && cd[q].code != 32 && em[(size_t)j * (size_t)K + (size_t)q] >= 0.32f && (alt < 0 || em[(size_t)j * (size_t)K + (size_t)q] > em[(size_t)j * (size_t)K + (size_t)alt])) alt = q;
            used[kk]--;
            ki[j] = alt;
            if (alt >= 0) used[alt]++;
        }
    }
    for (int i = 0; i < N; i++) {
        s->cells[i].code = ki[i] >= 0 ? cd[ki[i]].code : -1;
        s->cells[i].score = ki[i] >= 0 && s->cells[i].iw ? em[(size_t)i * (size_t)K + (size_t)ki[i]] : 0;
    }
done:
    free(V); free(W); free(back); free(skip); free(used); free(ki);
}

/* The characters of the cells.  A glyph of several colours (an outline, a shadow, a bevel) is read from all its ink and from single
 * colours of it, and each glyph keeps its best reading. */
static void recognise(Sheet *s, const SheetRefs *refs)
{
    int codes[512];
    int K = sheet_candidate_codes(codes, 512), N = s->ncells;
    size_t np = (size_t)s->w * (size_t)s->h;
    Cand *cd = (Cand *)calloc((size_t)K, sizeof(Cand));
    float *em = (float *)malloc(sizeof(float) * (size_t)K * (size_t)imax(N, 1));
    unsigned char *mask = (unsigned char *)malloc(np);
    CMap m;
    memset(&m, 0, sizeof m);
    if (!cd || !em || !mask || !N) goto done;
    for (int k = 0; k < K; k++) { cd[k].code = codes[k]; cd[k].succA = cd[k].succB = cd[k].succC = -1; cd[k].upper = -1; }
    for (int k = 0; k < K; k++) {
        cd[k].succA = cand_index(cd, K, codes[k] + 1);
        if ((codes[k] >= 'a' && codes[k] <= 'z') || (codes[k] >= 0xE0 && codes[k] <= 0xFE && codes[k] != 0xF7)) cd[k].upper = cand_index(cd, K, codes[k] - 32);
    }
    for (int i = 0; i + 1 < 128; i++) { int a = cand_index(cd, K, CP437[i]), b = cand_index(cd, K, CP437[i + 1]); if (a >= 0) cd[a].succB = b; }
    { int a = cand_index(cd, K, '~'); if (a >= 0) cd[a].succB = cand_index(cd, K, CP437[0]); }
    { int a = cand_index(cd, K, '9'); if (a >= 0) cd[a].succC = cand_index(cd, K, '0'); }   /* 1 2 ... 9 0 */

    for (size_t i = 0; i < (size_t)K * (size_t)N; i++) em[i] = -1e9f;
    emissions(s, refs, s->ink, cd, K, em);
    /* the commonest colours of the ink, each alone and left out */
    long inkN = 0;
    for (int i = 0; i < N; i++) {
        const SheetCell *c = &s->cells[i];
        for (int y = c->iy; y < c->iy + c->ih; y++)
            for (int x = c->ix; x < c->ix + c->iw; x++) {
                size_t p = (size_t)y * (size_t)s->w + (size_t)x;
                if (!s->ink[p]) continue;
                uint32_t col = s->px[p] | 0xFFu;
                if (s->noisy) col = (col & 0xC0C0C0FFu) | 0x20202000u;
                cmap_add(&m, col, 1);
                inkN++;
            }
    }
    if (m.n >= 2 && inkN > 0) {
        qsort(m.v, (size_t)m.n, sizeof(CEnt), cent_cmp);
        for (int v = 0; v < imin(m.n, 3) * 2; v++) {
            const CEnt *e = &m.v[v / 2];
            if (e->n * 100 < inkN * 12 || e->n * 100 > inkN * 88) continue;
            for (size_t p = 0; p < np; p++) {
                uint32_t col = s->px[p] | 0xFFu;
                if (s->noisy) col = (col & 0xC0C0C0FFu) | 0x20202000u;
                mask[p] = s->ink[p] && ((col == e->c) == !(v & 1));
            }
            emissions(s, refs, mask, cd, K, em);
        }
    }
    for (int i = 0; i < N; i++) {
        float *e = em + (size_t)i * (size_t)K;
        if (s->cells[i].iw) { for (int k = 0; k < K; k++) if (e[k] < -1e8f) e[k] = cd[k].code == 32 ? -1.0f : SCORE_FLOOR; }
        else for (int k = 0; k < K; k++) e[k] = cd[k].code == 32 ? 0.95f : BLANK_SKIP - RUN_BONUS;   /* a place held is worth what a place not held is, once the run goes on */
    }
    decode(s, cd, K, em);
    s->recognised = 1;
done:
    cmap_free(&m);
    free(cd); free(em); free(mask);
}

/* ------------------------------------------------------------------ several fonts on one sheet */

static int same_glyph(const Sheet *s, const SheetCell *a, const SheetCell *b)
{
    if (a->iw != b->iw || a->ih != b->ih || a->ix - a->x != b->ix - b->x || a->iy - a->y != b->iy - b->y) return 0;
    for (int y = 0; y < a->ih; y++)
        for (int x = 0; x < a->iw; x++)
            if (s->ink[(size_t)(a->iy + y) * (size_t)s->w + (size_t)(a->ix + x)] != s->ink[(size_t)(b->iy + y) * (size_t)s->w + (size_t)(b->ix + x)]) return 0;
    return 1;
}

/* The same glyphs again and again (one font in several colours): the length of one copy in cells, 0 when there is one font. */
static int find_blocks(const Sheet *s)
{
    int N = s->ncells;
    for (int P = 16; P <= N / 2; P++) {
        int same = 0, cmp = 0;
        for (int i = 0; i + P < N; i++) {
            if (!s->cells[i].iw && !s->cells[i + P].iw) continue;
            cmp++;
            same += same_glyph(s, &s->cells[i], &s->cells[i + P]);
        }
        if (cmp >= 16 && same * 100 >= cmp * 92) return P;
    }
    return 0;
}

/* ------------------------------------------------------------------ palette */

static int build_palette(Sheet *s, int mono)
{
    CMap m;
    memset(&m, 0, sizeof m);
    if (mono) { s->palette[0] = 0x000000FFu; s->npalette = 1; return 0; }
    for (int i = 0; i < s->ncells; i++) {
        const SheetCell *c = &s->cells[i];
        for (int y = c->iy; y < c->iy + c->ih; y++)
            for (int x = c->ix; x < c->ix + c->iw; x++) {
                size_t p = (size_t)y * (size_t)s->w + (size_t)x;
                if (!s->ink[p]) continue;
                uint32_t col = s->px[p] | 0xFFu;
                if (s->noisy) col = (col & 0xF0F0F0FFu) | 0x08080800u;   /* noise: 16 levels a channel */
                if (cmap_add(&m, col, 1) < 0) { cmap_free(&m); return -1; }
            }
    }
    if (!m.n) { s->palette[0] = 0x000000FFu; s->npalette = 1; cmap_free(&m); return 0; }
    qsort(m.v, (size_t)m.n, sizeof(CEnt), cent_cmp);
    int n = imin(m.n, 256);
    typedef struct { double r, g, b; double n; } Cl;
    Cl cl[256];
    for (int i = 0; i < n; i++) { cl[i].r = m.v[i].c >> 24; cl[i].g = (m.v[i].c >> 16) & 255; cl[i].b = (m.v[i].c >> 8) & 255; cl[i].n = (double)m.v[i].n; }
    for (int i = n; i < m.n; i++) {   /* the rare colours join their closest */
        int best = 0, bd = 1 << 30;
        for (int j = 0; j < n; j++) { uint32_t c2 = ((uint32_t)cl[j].r << 24) | ((uint32_t)cl[j].g << 16) | ((uint32_t)cl[j].b << 8) | 255; int d = cdist(m.v[i].c, c2); if (d < bd) { bd = d; best = j; } }
        cl[best].n += (double)m.v[i].n;
    }
    while (n > SHEET_MAX_COLORS) {   /* merge the pair that costs least: close colours, the rarer one small */
        int bi = 0, bj = 1;
        double bc = 1e300;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                double dr = cl[i].r - cl[j].r, dg = cl[i].g - cl[j].g, db = cl[i].b - cl[j].b;
                double cost = (dr * dr + dg * dg + db * db) * fmin(cl[i].n, cl[j].n);
                if (cost < bc) { bc = cost; bi = i; bj = j; }
            }
        double tn = cl[bi].n + cl[bj].n;
        cl[bi].r = (cl[bi].r * cl[bi].n + cl[bj].r * cl[bj].n) / tn;
        cl[bi].g = (cl[bi].g * cl[bi].n + cl[bj].g * cl[bj].n) / tn;
        cl[bi].b = (cl[bi].b * cl[bi].n + cl[bj].b * cl[bj].n) / tn;
        cl[bi].n = tn;
        cl[bj] = cl[n - 1];
        n--;
    }
    for (int i = 1; i < n; i++) { Cl t = cl[i]; int j = i; while (j > 0 && cl[j - 1].n < t.n) { cl[j] = cl[j - 1]; j--; } cl[j] = t; }
    for (int i = 0; i < n; i++) s->palette[i] = ((uint32_t)lround(cl[i].r) << 24) | ((uint32_t)lround(cl[i].g) << 16) | ((uint32_t)lround(cl[i].b) << 8) | 255u;
    s->npalette = n;
    cmap_free(&m);
    return 0;
}

void sheet_cell_pixels(const Sheet *s, const SheetCell *c, unsigned char *out)
{
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            size_t p = (size_t)(c->y + y) * (size_t)s->w + (size_t)(c->x + x);
            int v = 0;
            if (s->ink[p]) {
                int best = 0, bd = 1 << 30;
                for (int k = 0; k < s->npalette; k++) { int d = cdist(s->px[p] | 0xFFu, s->palette[k]); if (d < bd) { bd = d; best = k; } }
                v = best + 1;
            }
            out[(size_t)y * (size_t)c->w + (size_t)x] = (unsigned char)v;
        }
}

/* ------------------------------------------------------------------ the analysis */

void sheet_free(Sheet *s)
{
    if (!s) return;
    free(s->px);
    free(s->ink);
    free(s->cells);
    memset(s, 0, sizeof *s);
}

/* background and ink of the picture as it is (before the scale) */
static int classify(const uint32_t *px, int w, int h, Sheet *s, unsigned char *ink)
{
    size_t n = (size_t)w * (size_t)h;
    long clear = 0;
    CMap m;
    memset(&m, 0, sizeof m);
    for (size_t i = 0; i < n; i++) {
        if ((px[i] & 255) < 128) clear++;
        else if (m.n < 5000 && cmap_add(&m, px[i] | 0xFFu, 1) < 0) { cmap_free(&m); return -1; }
    }
    s->noisy = m.n > NOISE_COLORS;
    s->alphaBg = clear * 50 > (long)n;
    if (!s->alphaBg) {
        if (s->noisy) {   /* the commonest colour, counted coarsely */
            CMap q;
            memset(&q, 0, sizeof q);
            for (size_t i = 0; i < n; i++) cmap_add(&q, (px[i] & 0xE0E0E000u) | 0x101010FFu, 1);
            int best = 0;
            for (int i = 1; i < q.n; i++) if (q.v[i].n > q.v[best].n) best = i;
            s->bg = q.n ? q.v[best].c : 0xFFFFFFFFu;
            cmap_free(&q);
        } else {
            int best = 0;
            for (int i = 1; i < m.n; i++) if (m.v[i].n > m.v[best].n) best = i;
            s->bg = m.n ? m.v[best].c : 0xFFFFFFFFu;
        }
    }
    cmap_free(&m);
    for (size_t i = 0; i < n; i++) {
        if (s->alphaBg) ink[i] = (px[i] & 255) >= 128;
        else if ((px[i] & 255) < 128) ink[i] = 0;
        else ink[i] = s->noisy ? cdist(px[i], s->bg) > NOISE_TOL : (px[i] | 0xFFu) != s->bg;
    }
    return 0;
}

int sheet_analyze(const unsigned char *rgba, int w, int h, const SheetOpts *opts, const SheetRefs *refs, Sheet *s, char *msg, int msgsz)
{
    memset(s, 0, sizeof *s);
    if (!rgba || w < 2 || h < 2 || w > 8192 || h > 8192) { setmsg(msg, msgsz, "no picture, or one that is too small or too large"); return -1; }
    size_t n = (size_t)w * (size_t)h;
    uint32_t *px = (uint32_t *)malloc(sizeof(uint32_t) * n);
    unsigned char *ink = (unsigned char *)malloc(n);
    if (!px || !ink) { free(px); free(ink); setmsg(msg, msgsz, "out of memory"); return -1; }
    for (size_t i = 0; i < n; i++) px[i] = rgba_at(rgba + 4 * i);
    if (classify(px, w, h, s, ink) < 0) { free(px); free(ink); setmsg(msg, msgsz, "out of memory"); return -1; }

    /* the scale */
    int phx = 0, phy = 0;
    int sc = opts && opts->scale > 0 ? opts->scale : find_scale(px, ink, w, h, s->noisy, &phx, &phy);
    if (opts && opts->scale > 0) { int a, b; find_scale(px, ink, w, h, s->noisy, &a, &b); phx = a % sc; phy = b % sc; }
    if (sc > 1) {
        int x0 = phx > 0 ? phx - sc : 0, y0 = phy > 0 ? phy - sc : 0;
        int nw = (w - x0 + sc - 1) / sc, nh = (h - y0 + sc - 1) / sc;
        uint32_t *np = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)nw * (size_t)nh);
        unsigned char *ni = (unsigned char *)malloc((size_t)nw * (size_t)nh);
        if (!np || !ni) { free(np); free(ni); free(px); free(ink); setmsg(msg, msgsz, "out of memory"); return -1; }
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                long r = 0, g = 0, b = 0, a = 0, k = 0, on = 0;
                for (int dy = 0; dy < sc; dy++)
                    for (int dx = 0; dx < sc; dx++) {
                        int sx = x0 + x * sc + dx, sy = y0 + y * sc + dy;
                        if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
                        uint32_t c = px[(size_t)sy * (size_t)w + (size_t)sx];
                        r += c >> 24; g += (c >> 16) & 255; b += (c >> 8) & 255; a += c & 255; k++;
                        on += ink[(size_t)sy * (size_t)w + (size_t)sx];
                    }
                size_t o = (size_t)y * (size_t)nw + (size_t)x;
                if (s->noisy && k) np[o] = ((uint32_t)(r / k) << 24) | ((uint32_t)(g / k) << 16) | ((uint32_t)(b / k) << 8) | (uint32_t)(a / k);
                else {   /* the pixel in the middle of the block */
                    int sx = imin(w - 1, imax(0, x0 + x * sc + sc / 2)), sy = imin(h - 1, imax(0, y0 + y * sc + sc / 2));
                    np[o] = px[(size_t)sy * (size_t)w + (size_t)sx];
                    on = ink[(size_t)sy * (size_t)w + (size_t)sx] ? k : 0;
                }
                ni[o] = k && on * 2 >= k;
            }
        free(px);
        free(ink);
        px = np; ink = ni; w = nw; h = nh;
    }
    s->scale = sc;
    s->px = px;
    s->ink = ink;
    s->w = w;
    s->h = h;

    /* the layout */
    int want = opts ? opts->layout : SHEET_AUTO;
    int done = 0;
    if (want == SHEET_GRID && opts->cellW > 0 && opts->cellH > 0) {
        s->cellW = opts->cellW; s->cellH = opts->cellH; s->offX = imax(0, opts->offX); s->offY = imax(0, opts->offY);
        grid_cells(s);
        done = 1;
    }
    if (!done && (want == SHEET_AUTO || want == SHEET_MARKER)) done = layout_marker(s);
    if (!done && want == SHEET_MARKER) { setmsg(msg, msgsz, "the picture has no marker row"); sheet_free(s); return -1; }
    if (!done && layout_auto(s, opts) < 0) { setmsg(msg, msgsz, "the picture has no ink"); sheet_free(s); return -1; }
    if (s->layout == SHEET_GRID) {   /* blank cells after the last glyph hold no place */
        while (s->ncells > 1 && s->cells[s->ncells - 1].iw == 0) s->ncells--;
    }
    if (!s->ncells) { setmsg(msg, msgsz, "no glyph found"); sheet_free(s); return -1; }

    /* one font in several colours: the copy wanted stays */
    {
        int P = find_blocks(s);
        if (P > 0) {
            s->blocks = (s->ncells + P - 1) / P;
            s->block = opts && opts->block > 0 && opts->block < s->blocks ? opts->block : 0;
            int a = s->block * P, n2 = imin(P, s->ncells - a);
            memmove(s->cells, s->cells + a, sizeof(SheetCell) * (size_t)n2);
            s->ncells = n2;
            while (s->ncells > 1 && s->cells[s->ncells - 1].iw == 0) s->ncells--;
        } else s->blocks = 1;
    }

    /* metrics, characters, colours */
    int baseline = 1, capTop = 0;
    find_metrics(s, &baseline, &capTop);
    if (opts && opts->codes && opts->ncodes > 0) {
        for (int i = 0; i < s->ncells; i++) s->cells[i].code = i < opts->ncodes ? opts->codes[i] : -1;
    } else if ((opts && opts->firstCode > 0) || !refs || !refs->n) {
        int c = opts && opts->firstCode > 0 ? opts->firstCode : (s->cells[0].iw ? 33 : 32);
        for (int i = 0; i < s->ncells; i++) s->cells[i].code = c + i;
    } else recognise(s, refs);
    double sum = 0;
    for (int i = 0; i < s->ncells; i++) {
        SheetCell *c = &s->cells[i];
        if (!c->iw && c->code != 32) c->code = -1;   /* a blank cell only held the place of a character */
        if (c->iw && c->code >= 0) { s->glyphs++; sum += c->score; }
    }
    s->confidence = s->glyphs ? (float)(sum / s->glyphs) : 0;
    if (build_palette(s, opts && opts->mono) < 0) { setmsg(msg, msgsz, "out of memory"); sheet_free(s); return -1; }
    snprintf(s->note, sizeof s->note, "%s, %d glyphs%s", s->layout == SHEET_MARKER ? "a strip with a marker row" : s->layout == SHEET_GRID ? "a grid of cells" : "rows of glyphs",
             s->glyphs, s->scale > 1 ? ", enlarged" : "");
    return 0;
}
