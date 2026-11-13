/* outline.c - see outline.h.  Portable C99; all reads from font data are bounds-checked. */
#include "outline.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint64_t u64;

#define MAX_FILE   ((size_t)256 << 20)
#define MAX_VERTS  (1 << 20)
#define MAX_DIM    16384
#define MAX_PIXELS (16 << 20)

static void seterr(char *err, int n, const char *fmt, ...) {
    va_list ap;
    if (!err || n <= 0) return;
    va_start(ap, fmt);
    vsnprintf(err, (size_t)n, fmt, ap);
    va_end(ap);
}

/* ===================================================================== path (outline in font units) */

enum { V_MOVE, V_LINE, V_QUAD, V_CUBIC };
typedef struct { u8 t; float x, y, cx, cy, cx1, cy1; } Vert;   /* end point x,y; controls c, c1 */
typedef struct { Vert *v; int n, cap; int oom; } Path;

static void path_free(Path *p) { free(p->v); memset(p, 0, sizeof *p); }

static void path_add(Path *p, int t, double x, double y, double cx, double cy, double cx1, double cy1) {
    Vert *v;
    if (p->oom) return;
    if (p->n >= MAX_VERTS) { p->oom = 1; return; }
    if (p->n >= p->cap) {
        int nc = p->cap ? p->cap * 2 : 64;
        Vert *nv = (Vert *)realloc(p->v, (size_t)nc * sizeof(Vert));
        if (!nv) { p->oom = 1; return; }
        p->v = nv; p->cap = nc;
    }
    v = &p->v[p->n++];
    v->t = (u8)t; v->x = (float)x; v->y = (float)y; v->cx = (float)cx; v->cy = (float)cy; v->cx1 = (float)cx1; v->cy1 = (float)cy1;
}
#define P_MOVE(p, x, y)  path_add(p, V_MOVE, x, y, 0, 0, 0, 0)
#define P_LINE(p, x, y)  path_add(p, V_LINE, x, y, 0, 0, 0, 0)
#define P_QUAD(p, cx, cy, x, y) path_add(p, V_QUAD, x, y, cx, cy, 0, 0)
#define P_CUBIC(p, c1x, c1y, c2x, c2y, x, y) path_add(p, V_CUBIC, x, y, c1x, c1y, c2x, c2y)

/* ===================================================================== inflate (RFC 1950/1951, structured after zlib's puff) */

typedef struct {
    const u8 *in; size_t inlen, incnt;
    long bitbuf; int bitcnt;
    u8 *out; size_t outlen, outcnt;
} Inf;

typedef struct { short count[16]; short symbol[288]; } Huff;

static int inf_bits(Inf *s, int need) {
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (long)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int inf_decode(Inf *s, const Huff *h) {
    int len, code = 0, first = 0, count, index = 0, b;
    for (len = 1; len <= 15; len++) {
        b = inf_bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}

static int inf_construct(Huff *h, const short *length, int n) {
    int symbol, len, left;
    short offs[16];
    for (len = 0; len <= 15; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= 15; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int inf_codes(Inf *s, const Huff *lc, const Huff *dc) {
    static const short lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    int symbol, len, dist, b;
    for (;;) {
        symbol = inf_decode(s, lc);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->outcnt >= s->outlen) return -1;
            s->out[s->outcnt++] = (u8)symbol;
        } else if (symbol == 256) {
            return 0;
        } else {
            symbol -= 257;
            if (symbol >= 29) return -1;
            b = inf_bits(s, lext[symbol]); if (b < 0) return -1;
            len = lens[symbol] + b;
            symbol = inf_decode(s, dc);
            if (symbol < 0 || symbol >= 30) return -1;
            b = inf_bits(s, dext[symbol]); if (b < 0) return -1;
            dist = dists[symbol] + b;
            if ((size_t)dist > s->outcnt || s->outcnt + (size_t)len > s->outlen) return -1;
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - (size_t)dist]; s->outcnt++; }
        }
    }
}

static int inf_dynamic(Inf *s) {
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    short lengths[320];
    Huff lc, dc;
    int nlen, ndist, ncode, i, b, sym, len, err;
    b = inf_bits(s, 5); if (b < 0) return -1; nlen = b + 257;
    b = inf_bits(s, 5); if (b < 0) return -1; ndist = b + 1;
    b = inf_bits(s, 4); if (b < 0) return -1; ncode = b + 4;
    if (nlen > 286 || ndist > 30) return -1;
    for (i = 0; i < ncode; i++) { b = inf_bits(s, 3); if (b < 0) return -1; lengths[order[i]] = (short)b; }
    for (; i < 19; i++) lengths[order[i]] = 0;
    err = inf_construct(&lc, lengths, 19);
    if (err != 0) return -1;
    i = 0;
    while (i < nlen + ndist) {
        sym = inf_decode(s, &lc);
        if (sym < 0) return -1;
        if (sym < 16) lengths[i++] = (short)sym;
        else {
            len = 0;
            if (sym == 16) { if (i == 0) return -1; len = lengths[i - 1]; b = inf_bits(s, 2); if (b < 0) return -1; sym = 3 + b; }
            else if (sym == 17) { b = inf_bits(s, 3); if (b < 0) return -1; sym = 3 + b; }
            else { b = inf_bits(s, 7); if (b < 0) return -1; sym = 11 + b; }
            if (i + sym > nlen + ndist) return -1;
            while (sym--) lengths[i++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;
    err = inf_construct(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return -1;
    err = inf_construct(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return -1;
    return inf_codes(s, &lc, &dc);
}

static int inf_fixed(Inf *s) {
    short lengths[320];
    Huff lc, dc;
    int sym;
    for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < 288; sym++) lengths[sym] = 8;
    inf_construct(&lc, lengths, 288);
    for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
    inf_construct(&dc, lengths, 30);
    return inf_codes(s, &lc, &dc);
}

/* zlib stream -> exactly outlen bytes; 0 on success */
static int inflate_zlib(const u8 *in, size_t inlen, u8 *out, size_t outlen) {
    Inf s;
    int last, type, r;
    if (inlen < 2 || (in[0] & 0x0f) != 8 || ((in[0] << 8) | in[1]) % 31 != 0) return -1;
    memset(&s, 0, sizeof s);
    s.in = in + 2; s.inlen = inlen - 2; s.out = out; s.outlen = outlen;
    do {
        last = inf_bits(&s, 1); type = inf_bits(&s, 2);
        if (last < 0 || type < 0) return -1;
        if (type == 0) {
            unsigned len, nlen;
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.incnt + 4 > s.inlen) return -1;
            len = s.in[s.incnt] | (unsigned)(s.in[s.incnt + 1] << 8);
            nlen = s.in[s.incnt + 2] | (unsigned)(s.in[s.incnt + 3] << 8);
            s.incnt += 4;
            if (len != (~nlen & 0xffff)) return -1;
            if (s.incnt + len > s.inlen || s.outcnt + len > s.outlen) return -1;
            memcpy(s.out + s.outcnt, s.in + s.incnt, len);
            s.incnt += len; s.outcnt += len;
            r = 0;
        } else if (type == 1) r = inf_fixed(&s);
        else if (type == 2) r = inf_dynamic(&s);
        else return -1;
        if (r != 0) return -1;
    } while (!last);
    return s.outcnt == outlen ? 0 : -1;
}

/* ===================================================================== the font object */

typedef struct { size_t off, len; } Tab;
typedef struct T1 T1;
typedef struct { u64 base; unsigned count, osz; size_t offs; } Index;   /* base = file offset of data byte "0" (offsets are 1-based) */

struct Outline {
    u8 *d; size_t n;                 /* the sfnt (or Type 1 file) in memory, owned */
    char fmt[8];
    int nfaces, face, ttc;
    T1 *t1;                          /* Type 1 data, else NULL */
    int isCFF;
    Tab head, maxp, hhea, hmtx, cmap, loca, glyf, name, cff;
    int upem, locfmt, nglyphs, nhm, asc, desc, gap;
    size_t cmapOff; int cmapFmt, cmapSym;
    size_t csIdx, gsIdx, privOff, privSize, fdArray, fdSelect; int isCID;
    char family[160], style[96];
};

static unsigned r8(const Outline *f, size_t o) { return o < f->n ? f->d[o] : 0; }
static unsigned r16(const Outline *f, size_t o) {
    return (o < f->n && f->n - o >= 2) ? (unsigned)((f->d[o] << 8) | f->d[o + 1]) : 0;
}
static int r16s(const Outline *f, size_t o) { unsigned v = r16(f, o); return v >= 0x8000 ? (int)v - 0x10000 : (int)v; }
static uint32_t r32(const Outline *f, size_t o) {
    return (o < f->n && f->n - o >= 4)
        ? ((uint32_t)f->d[o] << 24) | ((uint32_t)f->d[o + 1] << 16) | ((uint32_t)f->d[o + 2] << 8) | f->d[o + 3] : 0;
}
#define TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* ===================================================================== cmap */

static unsigned cmap_sub_lookup(const Outline *f, size_t sub, int fmt, unsigned cp) {
    if (fmt == 0) return cp < 256 ? r8(f, sub + 6 + cp) : 0;
    if (fmt == 6) {
        unsigned first = r16(f, sub + 6), count = r16(f, sub + 8);
        return (cp >= first && cp - first < count) ? r16(f, sub + 10 + 2 * (size_t)(cp - first)) : 0;
    }
    if (fmt == 12) {
        uint32_t ng = r32(f, sub + 12), lo = 0, hi;
        if (ng > f->n / 12) ng = (uint32_t)(f->n / 12);
        hi = ng;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            size_t g = sub + 16 + 12 * (size_t)mid;
            uint32_t s = r32(f, g), e = r32(f, g + 4);
            if (cp < s) hi = mid;
            else if (cp > e) lo = mid + 1;
            else return r32(f, g + 8) + (cp - s);
        }
        return 0;
    }
    if (fmt == 4) {
        unsigned segx2 = r16(f, sub + 6), seg = segx2 / 2, lo = 0, hi = seg;
        size_t endc = sub + 14, startc = sub + 16 + segx2, delta = sub + 16 + 2 * (size_t)segx2, ro = sub + 16 + 3 * (size_t)segx2;
        if (cp > 0xFFFF) return 0;
        while (lo < hi) {   /* first segment with endCode >= cp */
            unsigned mid = lo + (hi - lo) / 2;
            if (r16(f, endc + 2 * (size_t)mid) < cp) lo = mid + 1; else hi = mid;
        }
        if (lo >= seg) return 0;
        {
            unsigned start = r16(f, startc + 2 * (size_t)lo), dl = r16(f, delta + 2 * (size_t)lo), rr = r16(f, ro + 2 * (size_t)lo), g;
            if (start > cp) return 0;
            if (rr == 0) return (cp + dl) & 0xFFFF;
            g = r16(f, ro + 2 * (size_t)lo + rr + 2 * (size_t)(cp - start));
            return g ? ((g + dl) & 0xFFFF) : 0;
        }
    }
    return 0;
}

static void cmap_choose(Outline *f) {
    unsigned n, i;
    int best = 0;
    f->cmapOff = 0; f->cmapFmt = 0; f->cmapSym = 0;
    if (!f->cmap.len) return;
    n = r16(f, f->cmap.off + 2);
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) {
        size_t rec = f->cmap.off + 4 + 8 * (size_t)i, sub;
        unsigned pid = r16(f, rec), eid = r16(f, rec + 2);
        uint32_t off = r32(f, rec + 4);
        int fmt, score = 0;
        if (off >= f->cmap.len) continue;
        sub = f->cmap.off + off;
        fmt = (int)r16(f, sub);
        if (fmt != 0 && fmt != 4 && fmt != 6 && fmt != 12) continue;
        if (pid == 3 && eid == 10 && fmt == 12) score = 100;
        else if (pid == 0 && fmt == 12) score = 90;
        else if (pid == 3 && eid == 1 && fmt == 4) score = 80;
        else if (pid == 0 && fmt == 4) score = 70;
        else if (pid == 3 && eid == 0) score = 50;
        else if (pid == 1) score = 10;
        else score = 5;
        if (score > best) { best = score; f->cmapOff = sub; f->cmapFmt = fmt; f->cmapSym = (pid == 3 && eid == 0); }
    }
}

/* ===================================================================== name table */

static void utf8_put(char *out, int outsz, int *len, unsigned cp) {
    char b[4]; int k = 0, i;
    if (cp < 0x80) b[k++] = (char)cp;
    else if (cp < 0x800) { b[k++] = (char)(0xC0 | (cp >> 6)); b[k++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { b[k++] = (char)(0xE0 | (cp >> 12)); b[k++] = (char)(0x80 | ((cp >> 6) & 63)); b[k++] = (char)(0x80 | (cp & 63)); }
    else { b[k++] = (char)(0xF0 | (cp >> 18)); b[k++] = (char)(0x80 | ((cp >> 12) & 63)); b[k++] = (char)(0x80 | ((cp >> 6) & 63)); b[k++] = (char)(0x80 | (cp & 63)); }
    if (*len + k >= outsz) return;
    for (i = 0; i < k; i++) out[(*len)++] = b[i];
    out[*len] = 0;
}

static int name_string(const Outline *f, unsigned nid, char *out, int outsz) {
    unsigned cnt, i;
    size_t strs;
    int best = 0; size_t bo = 0, bl = 0; unsigned bp = 0;
    int len = 0;
    out[0] = 0;
    if (!f->name.len) return 0;
    cnt = r16(f, f->name.off + 2);
    strs = f->name.off + r16(f, f->name.off + 4);
    if (cnt > 4096) cnt = 4096;
    for (i = 0; i < cnt; i++) {
        size_t rec = f->name.off + 6 + 12 * (size_t)i;
        unsigned pid = r16(f, rec), eid = r16(f, rec + 2), lang = r16(f, rec + 4), id = r16(f, rec + 6), l = r16(f, rec + 8), o = r16(f, rec + 10);
        int score = 0;
        if (id != nid || l == 0 || strs + o + l > f->n) continue;
        if (pid == 3 && (eid == 1 || eid == 10)) score = lang == 0x409 ? 4 : 3;
        else if (pid == 0) score = 2;
        else if (pid == 1 && lang == 0) score = 1;
        if (score > best) { best = score; bo = strs + o; bl = l; bp = pid; }
    }
    if (!best) return 0;
    if (bp == 1) {
        size_t k;
        for (k = 0; k < bl && len + 1 < outsz; k++) { unsigned c = f->d[bo + k]; out[len++] = c < 0x80 ? (char)c : '?'; }
        out[len] = 0;
    } else {
        size_t k;
        for (k = 0; k + 1 < bl; k += 2) {
            unsigned c = (unsigned)((f->d[bo + k] << 8) | f->d[bo + k + 1]);
            if (c >= 0xD800 && c < 0xDC00 && k + 3 < bl) {
                unsigned c2 = (unsigned)((f->d[bo + k + 2] << 8) | f->d[bo + k + 3]);
                if (c2 >= 0xDC00 && c2 < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); k += 2; }
            }
            utf8_put(out, outsz, &len, c);
        }
    }
    return len;
}

/* ===================================================================== TrueType outlines (glyf) */

static int glyf_range(const Outline *f, int g, size_t *off, size_t *len) {
    size_t a, b, sz = f->locfmt ? 4 : 2;
    if (g < 0 || g >= f->nglyphs) return -1;
    if ((u64)(g + 2) * sz > f->loca.len) return -1;
    if (f->locfmt) { a = r32(f, f->loca.off + (size_t)g * 4); b = r32(f, f->loca.off + (size_t)g * 4 + 4); }
    else { a = 2 * (size_t)r16(f, f->loca.off + (size_t)g * 2); b = 2 * (size_t)r16(f, f->loca.off + (size_t)g * 2 + 2); }
    if (b < a || b > f->glyf.len) return -1;
    *off = f->glyf.off + a; *len = b - a;
    return 0;
}

static void path_append_xform(Path *dst, const Path *src, double a, double b, double c, double d, double dx, double dy) {
    int i;
    for (i = 0; i < src->n; i++) {
        const Vert *v = &src->v[i];
        path_add(dst, v->t, a * v->x + c * v->y + dx, b * v->x + d * v->y + dy,
                 a * v->cx + c * v->cy + dx, b * v->cx + d * v->cy + dy, 0, 0);
    }
}

static int glyf_load(const Outline *f, int g, Path *p, int depth, int *budget) {
    size_t off, len, end, pos;
    int nc;
    if (depth > 8 || --*budget < 0) return -1;
    if (glyf_range(f, g, &off, &len) < 0) return -1;
    if (len == 0) return 0;
    if (len < 10) return -1;
    end = off + len;
    nc = r16s(f, off);
    if (nc == 0) return 0;
    if (nc > 0) {
        int *ends = NULL, *xs = NULL, *ys = NULL, i, np, c, x, y;
        u8 *fl = NULL;
        unsigned ins;
        int rc = -1;
        if (10 + 2 * (size_t)nc + 2 > len) return -1;
        ends = (int *)malloc((size_t)nc * sizeof(int));
        if (!ends) return -1;
        for (i = 0; i < nc; i++) {
            ends[i] = (int)r16(f, off + 10 + 2 * (size_t)i);
            if (i > 0 && ends[i] <= ends[i - 1]) goto done;
        }
        np = ends[nc - 1] + 1;
        ins = r16(f, off + 10 + 2 * (size_t)nc);
        pos = off + 12 + 2 * (size_t)nc + ins;
        fl = (u8 *)malloc((size_t)np);
        xs = (int *)malloc((size_t)np * sizeof(int));
        ys = (int *)malloc((size_t)np * sizeof(int));
        if (!fl || !xs || !ys) goto done;
        for (i = 0; i < np;) {
            unsigned flag, rep = 0;
            if (pos >= end) goto done;
            flag = f->d[pos++];
            if (flag & 8) { if (pos >= end) goto done; rep = f->d[pos++]; }
            if (i + (int)rep + 1 > np) goto done;
            fl[i++] = (u8)flag;
            while (rep--) fl[i++] = (u8)flag;
        }
        for (i = 0, x = 0; i < np; i++) {
            if (fl[i] & 2) { if (pos >= end) goto done; x += (fl[i] & 16) ? (int)f->d[pos] : -(int)f->d[pos]; pos++; }
            else if (!(fl[i] & 16)) { if (pos + 2 > end) goto done; x += r16s(f, pos); pos += 2; }
            xs[i] = x;
        }
        for (i = 0, y = 0; i < np; i++) {
            if (fl[i] & 4) { if (pos >= end) goto done; y += (fl[i] & 32) ? (int)f->d[pos] : -(int)f->d[pos]; pos++; }
            else if (!(fl[i] & 32)) { if (pos + 2 > end) goto done; y += r16s(f, pos); pos += 2; }
            ys[i] = y;
        }
        for (c = 0, i = 0; c < nc; c++) {
            int s = i, e = ends[c], m = e - s + 1, k, first, last, have = 0;
            double sx, sy, cx = 0, cy = 0;
            i = e + 1;
            if (m < 2) continue;
            if (fl[s] & 1) { sx = xs[s]; sy = ys[s]; first = s + 1; last = e; }
            else if (fl[e] & 1) { sx = xs[e]; sy = ys[e]; first = s; last = e - 1; }
            else { sx = (xs[s] + xs[e]) * 0.5; sy = (ys[s] + ys[e]) * 0.5; first = s; last = e; }
            P_MOVE(p, sx, sy);
            for (k = first; k <= last; k++) {
                if (fl[k] & 1) {
                    if (have) P_QUAD(p, cx, cy, xs[k], ys[k]); else P_LINE(p, xs[k], ys[k]);
                    have = 0;
                } else {
                    if (have) P_QUAD(p, cx, cy, (cx + xs[k]) * 0.5, (cy + ys[k]) * 0.5);
                    cx = xs[k]; cy = ys[k]; have = 1;
                }
            }
            if (have) P_QUAD(p, cx, cy, sx, sy);
        }
        rc = 0;
    done:
        free(ends); free(fl); free(xs); free(ys);
        return rc;
    }
    /* composite */
    pos = off + 10;
    for (;;) {
        unsigned flags, gi;
        double a = 1, b = 0, c = 0, d = 1, dx = 0, dy = 0;
        Path sub;
        int rc;
        if (pos + 4 > end) return -1;
        flags = r16(f, pos); gi = r16(f, pos + 2); pos += 4;
        if (flags & 1) { if (pos + 4 > end) return -1; if (flags & 2) { dx = r16s(f, pos); dy = r16s(f, pos + 2); } pos += 4; }
        else { if (pos + 2 > end) return -1; if (flags & 2) { dx = (signed char)f->d[pos]; dy = (signed char)f->d[pos + 1]; } pos += 2; }
        if (flags & 8) { if (pos + 2 > end) return -1; a = d = r16s(f, pos) / 16384.0; pos += 2; }
        else if (flags & 0x40) { if (pos + 4 > end) return -1; a = r16s(f, pos) / 16384.0; d = r16s(f, pos + 2) / 16384.0; pos += 4; }
        else if (flags & 0x80) {
            if (pos + 8 > end) return -1;
            a = r16s(f, pos) / 16384.0; b = r16s(f, pos + 2) / 16384.0; c = r16s(f, pos + 4) / 16384.0; d = r16s(f, pos + 6) / 16384.0;
            pos += 8;
        }
        memset(&sub, 0, sizeof sub);
        rc = glyf_load(f, (int)gi, &sub, depth + 1, budget);
        if (rc == 0) path_append_xform(p, &sub, a, b, c, d, dx, dy);
        path_free(&sub);
        if (rc < 0) return -1;
        if (!(flags & 0x20)) break;
    }
    return 0;
}

/* ===================================================================== CFF (Type 2 charstrings) */

static int idx_open(const Outline *f, size_t off, Index *ix, size_t *end) {
    unsigned count = r16(f, off), osz;
    size_t offs;
    u64 base, last;
    memset(ix, 0, sizeof *ix);
    if (off >= f->n) return -1;
    if (count == 0) { if (end) *end = off + 2; return 0; }
    osz = r8(f, off + 2);
    if (osz < 1 || osz > 4) return -1;
    offs = off + 3;
    base = (u64)offs + (u64)(count + 1) * osz - 1;
    if (base >= f->n) return -1;
    ix->count = count; ix->osz = osz; ix->offs = offs; ix->base = base;
    last = 0;
    {
        unsigned k;
        size_t o = offs + (size_t)count * osz;
        for (k = 0; k < osz; k++) last = (last << 8) | r8(f, o + k);
    }
    if (base + last > f->n) return -1;
    if (end) *end = (size_t)(base + last);
    return 0;
}

static int idx_get(const Outline *f, const Index *ix, unsigned i, size_t *start, size_t *len) {
    u64 a = 0, b = 0;
    unsigned k;
    size_t o;
    if (i >= ix->count) return -1;
    o = ix->offs + (size_t)i * ix->osz;
    for (k = 0; k < ix->osz; k++) a = (a << 8) | r8(f, o + k);
    for (k = 0; k < ix->osz; k++) b = (b << 8) | r8(f, o + ix->osz + k);
    if (a < 1 || b < a || ix->base + b > f->n) return -1;
    *start = (size_t)(ix->base + a); *len = (size_t)(b - a);
    return 0;
}

typedef struct { long cs, privsize, privoff, subrs, fdarray, fdselect; int cid; } DictOut;

static void dict_scan(const Outline *f, size_t s, size_t len, DictOut *o) {
    double st[48];
    int sp = 0;
    size_t p = s, e = s + len;
    if (e > f->n) e = f->n;
    while (p < e) {
        unsigned b = r8(f, p);
        double v = 0;
        if (b <= 21) {
            int op = (int)b;
            p++;
            if (b == 12) { op = 1200 + (int)r8(f, p); p++; }
            switch (op) {
            case 17: if (sp >= 1) o->cs = (long)st[sp - 1]; break;
            case 18: if (sp >= 2) { o->privsize = (long)st[sp - 2]; o->privoff = (long)st[sp - 1]; } break;
            case 19: if (sp >= 1) o->subrs = (long)st[sp - 1]; break;
            case 1230: o->cid = 1; break;
            case 1236: if (sp >= 1) o->fdarray = (long)st[sp - 1]; break;
            case 1237: if (sp >= 1) o->fdselect = (long)st[sp - 1]; break;
            default: break;
            }
            sp = 0;
            continue;
        }
        if (b == 28) { v = r16s(f, p + 1); p += 3; }
        else if (b == 29) { v = (double)(int32_t)r32(f, p + 1); p += 5; }
        else if (b == 30) {
            p++;
            while (p < e) { unsigned c = r8(f, p++); if ((c & 15) == 15 || (c >> 4) == 15) break; }
        }
        else if (b >= 32 && b <= 246) { v = (double)b - 139; p++; }
        else if (b >= 247 && b <= 250) { v = ((double)b - 247) * 256 + r8(f, p + 1) + 108; p += 2; }
        else if (b >= 251 && b <= 254) { v = -((double)b - 251) * 256 - r8(f, p + 1) - 108; p += 2; }
        else { p++; continue; }
        if (b != 30 && sp < 48) st[sp++] = v;
        else if (b == 30 && sp < 48) st[sp++] = 0;
    }
}

static int cff_setup(Outline *f) {
    size_t base = f->cff.off, end, ts, tl;
    unsigned hdr = r8(f, base + 2);
    Index ix;
    DictOut o;
    if (r8(f, base) != 1 || hdr < 4) return -1;
    if (idx_open(f, base + hdr, &ix, &end) < 0) return -1;          /* names */
    if (idx_open(f, end, &ix, &end) < 0) return -1;                 /* top dicts */
    if (idx_get(f, &ix, 0, &ts, &tl) < 0) return -1;
    if (idx_open(f, end, &ix, &end) < 0) return -1;                 /* strings */
    f->gsIdx = end;
    if (idx_open(f, end, &ix, NULL) < 0) return -1;                 /* global subrs must be valid */
    memset(&o, 0, sizeof o);
    o.cs = -1;
    dict_scan(f, ts, tl, &o);
    if (o.cs <= 0 || (u64)base + (u64)o.cs >= f->n) return -1;
    f->csIdx = base + (size_t)o.cs;
    if (idx_open(f, f->csIdx, &ix, NULL) < 0) return -1;
    if ((int)ix.count < f->nglyphs || f->nglyphs == 0) f->nglyphs = (int)ix.count;
    f->isCID = o.cid;
    f->privOff = f->privSize = 0;
    if (o.privoff > 0 && o.privsize > 0) { f->privOff = base + (size_t)o.privoff; f->privSize = (size_t)o.privsize; }
    f->fdArray = o.fdarray > 0 ? base + (size_t)o.fdarray : 0;
    f->fdSelect = o.fdselect > 0 ? base + (size_t)o.fdselect : 0;
    return 0;
}

static int cff_local_subrs(const Outline *f, int g, Index *ls) {
    size_t po = f->privOff, ps = f->privSize;
    DictOut o;
    memset(ls, 0, sizeof *ls);
    if (f->isCID && f->fdArray && f->fdSelect) {
        unsigned fmt = r8(f, f->fdSelect), fd = 0;
        Index fa;
        size_t s, l;
        if (fmt == 0) fd = r8(f, f->fdSelect + 1 + (size_t)g);
        else if (fmt == 3) {
            unsigned nr = r16(f, f->fdSelect + 1), i;
            for (i = 0; i < nr; i++) {
                size_t rec = f->fdSelect + 3 + 3 * (size_t)i;
                if ((unsigned)g >= r16(f, rec) && (unsigned)g < r16(f, rec + 3)) { fd = r8(f, rec + 2); break; }
            }
        }
        po = ps = 0;
        if (idx_open(f, f->fdArray, &fa, NULL) < 0 || idx_get(f, &fa, fd, &s, &l) < 0) return 0;
        memset(&o, 0, sizeof o);
        dict_scan(f, s, l, &o);
        if (o.privoff > 0 && o.privsize > 0) { po = f->cff.off + (size_t)o.privoff; ps = (size_t)o.privsize; }
    }
    if (!po || !ps) return 0;
    memset(&o, 0, sizeof o);
    dict_scan(f, po, ps, &o);
    if (o.subrs > 0) idx_open(f, po + (size_t)o.subrs, ls, NULL);
    return 0;
}

typedef struct {
    const Outline *f; Path *p;
    double st[96]; int sp;
    double x, y;
    int nstems, haveW, open;
    Index gs, ls;
    long steps;
} CS;

static int subr_bias(unsigned count) { return count < 1240 ? 107 : count < 33900 ? 1131 : 32768; }

static void cs_move(CS *c, double dx, double dy) { c->x += dx; c->y += dy; P_MOVE(c->p, c->x, c->y); c->open = 1; }
static void cs_line(CS *c, double dx, double dy) {
    if (!c->open) { P_MOVE(c->p, c->x, c->y); c->open = 1; }
    c->x += dx; c->y += dy; P_LINE(c->p, c->x, c->y);
}
static void cs_curve(CS *c, double d1x, double d1y, double d2x, double d2y, double d3x, double d3y) {
    double x1 = c->x + d1x, y1 = c->y + d1y, x2 = x1 + d2x, y2 = y1 + d2y, x3 = x2 + d3x, y3 = y2 + d3y;
    if (!c->open) { P_MOVE(c->p, c->x, c->y); c->open = 1; }
    P_CUBIC(c->p, x1, y1, x2, y2, x3, y3);
    c->x = x3; c->y = y3;
}
/* the optional width argument: drop the first operand when there are `extra` more than expected */
static void cs_width(CS *c, int has) {
    if (!c->haveW) {
        c->haveW = 1;
        if (has && c->sp > 0) { memmove(c->st, c->st + 1, (size_t)(c->sp - 1) * sizeof(double)); c->sp--; }
    }
}

/* returns 1 when endchar was reached, 0 on return / end of data, -1 on error */
static int cs_run(CS *c, size_t pos, size_t len, int depth) {
    const Outline *f = c->f;
    size_t end = pos + len;
    double *st = c->st;
    if (depth > 10) return -1;
    while (pos < end) {
        unsigned b = f->d[pos++];
        int i, sp;
        if (++c->steps > 400000) return -1;
        if (b >= 32 || b == 28) {
            double v;
            if (b == 28) { if (pos + 2 > end) return -1; v = r16s(f, pos); pos += 2; }
            else if (b <= 246) v = (double)b - 139;
            else if (b <= 250) { if (pos >= end) return -1; v = ((double)b - 247) * 256 + f->d[pos++] + 108; }
            else if (b <= 254) { if (pos >= end) return -1; v = -((double)b - 251) * 256 - f->d[pos++] - 108; }
            else { if (pos + 4 > end) return -1; v = (double)(int32_t)r32(f, pos) / 65536.0; pos += 4; }
            if (c->sp >= 96) return -1;
            st[c->sp++] = v;
            continue;
        }
        sp = c->sp;
        switch (b) {
        case 1: case 3: case 18: case 23:
            cs_width(c, c->sp & 1); c->nstems += c->sp / 2; c->sp = 0; break;
        case 19: case 20:
            cs_width(c, c->sp & 1); c->nstems += c->sp / 2; c->sp = 0;
            pos += (size_t)(c->nstems + 7) / 8;
            break;
        case 21: cs_width(c, c->sp > 2); sp = c->sp; if (sp >= 2) cs_move(c, st[sp - 2], st[sp - 1]); c->sp = 0; break;
        case 22: cs_width(c, c->sp > 1); sp = c->sp; if (sp >= 1) cs_move(c, st[sp - 1], 0); c->sp = 0; break;
        case 4:  cs_width(c, c->sp > 1); sp = c->sp; if (sp >= 1) cs_move(c, 0, st[sp - 1]); c->sp = 0; break;
        case 5: for (i = 0; i + 1 < sp; i += 2) cs_line(c, st[i], st[i + 1]); c->sp = 0; break;
        case 6: case 7:
            for (i = 0; i < sp; i++) { int horiz = ((i & 1) == 0) == (b == 6); if (horiz) cs_line(c, st[i], 0); else cs_line(c, 0, st[i]); }
            c->sp = 0; break;
        case 8: for (i = 0; i + 5 < sp; i += 6) cs_curve(c, st[i], st[i + 1], st[i + 2], st[i + 3], st[i + 4], st[i + 5]); c->sp = 0; break;
        case 24:
            for (i = 0; i + 7 < sp; i += 6) cs_curve(c, st[i], st[i + 1], st[i + 2], st[i + 3], st[i + 4], st[i + 5]);
            if (i + 1 < sp) cs_line(c, st[i], st[i + 1]);
            c->sp = 0; break;
        case 25:
            for (i = 0; i + 7 < sp; i += 2) cs_line(c, st[i], st[i + 1]);
            if (i + 5 < sp) cs_curve(c, st[i], st[i + 1], st[i + 2], st[i + 3], st[i + 4], st[i + 5]);
            c->sp = 0; break;
        case 26: {
            double dx1 = 0;
            i = 0;
            if (sp & 1) dx1 = st[i++];
            for (; i + 3 < sp; i += 4) { cs_curve(c, dx1, st[i], st[i + 1], st[i + 2], 0, st[i + 3]); dx1 = 0; }
            c->sp = 0; break;
        }
        case 27: {
            double dy1 = 0;
            i = 0;
            if (sp & 1) dy1 = st[i++];
            for (; i + 3 < sp; i += 4) { cs_curve(c, st[i], dy1, st[i + 1], st[i + 2], st[i + 3], 0); dy1 = 0; }
            c->sp = 0; break;
        }
        case 30: case 31: {
            int horiz = (b == 31);
            for (i = 0; i + 3 < sp; i += 4) {
                int last = (sp - i == 5);
                double extra = last ? st[i + 4] : 0;
                if (horiz) cs_curve(c, st[i], 0, st[i + 1], st[i + 2], extra, st[i + 3]);
                else cs_curve(c, 0, st[i], st[i + 1], st[i + 2], st[i + 3], extra);
                horiz = !horiz;
            }
            c->sp = 0; break;
        }
        case 10: case 29: {
            Index *ix = (b == 10) ? &c->ls : &c->gs;
            size_t s, l;
            int r;
            if (c->sp < 1) return -1;
            i = (int)st[--c->sp] + subr_bias(ix->count);
            if (i < 0 || idx_get(f, ix, (unsigned)i, &s, &l) < 0) return -1;
            r = cs_run(c, s, l, depth + 1);
            if (r != 0) return r;
            break;
        }
        case 11: return 0;
        case 14: cs_width(c, c->sp == 1 || c->sp == 5); c->sp = 0; return 1;
        case 12: {
            unsigned b2 = pos < end ? f->d[pos++] : 0;
            double *s = st;
            switch (b2) {
            case 34: if (sp < 7) return -1;
                cs_curve(c, s[0], 0, s[1], s[2], s[3], 0); cs_curve(c, s[4], 0, s[5], -s[2], s[6], 0); break;
            case 35: if (sp < 12) return -1;
                cs_curve(c, s[0], s[1], s[2], s[3], s[4], s[5]); cs_curve(c, s[6], s[7], s[8], s[9], s[10], s[11]); break;
            case 36: if (sp < 9) return -1;
                cs_curve(c, s[0], s[1], s[2], s[3], s[4], 0); cs_curve(c, s[5], 0, s[6], s[7], s[8], -(s[1] + s[3] + s[7])); break;
            case 37: {
                double dx, dy;
                if (sp < 11) return -1;
                dx = s[0] + s[2] + s[4] + s[6] + s[8]; dy = s[1] + s[3] + s[5] + s[7] + s[9];
                cs_curve(c, s[0], s[1], s[2], s[3], s[4], s[5]);
                if (fabs(dx) > fabs(dy)) cs_curve(c, s[6], s[7], s[8], s[9], s[10], -dy);
                else cs_curve(c, s[6], s[7], s[8], s[9], -dx, s[10]);
                break;
            }
            default: return -1;
            }
            c->sp = 0; break;
        }
        default: return -1;
        }
    }
    return 0;
}

static int cff_load(const Outline *f, int g, Path *p) {
    CS c;
    Index cs;
    size_t s, l;
    int r;
    memset(&c, 0, sizeof c);
    c.f = f; c.p = p;
    if (idx_open(f, f->csIdx, &cs, NULL) < 0 || idx_get(f, &cs, (unsigned)g, &s, &l) < 0) return -1;
    if (idx_open(f, f->gsIdx, &c.gs, NULL) < 0) return -1;
    cff_local_subrs(f, g, &c.ls);
    r = cs_run(&c, s, l, 0);
    return r < 0 ? -1 : 0;
}

/* ===================================================================== Adobe Type 1 (.pfb) */

typedef struct { size_t noff; int nlen; size_t off, len; } T1G;   /* glyph: name and charstring ranges inside priv */

struct T1 {
    u8 *priv; size_t privlen;
    size_t *soff, *slen; int nsubrs;
    T1G *g; int ng;
    int notdef;                      /* index into g of ".notdef" or -1 */
    int upem; int bbox[4];
    int stdenc; char (*enc)[48];     /* font Encoding: NULL / stdenc=1 for StandardEncoding */
    int *cmCp, *cmG, ncm;            /* code point -> glyph (1-based), sorted */
};

typedef struct { const char *name; unsigned short uni; unsigned char std; } AglRow;

static const AglRow AGL[] = {
    {"space",0x20,32},{"exclam",0x21,33},{"quotedbl",0x22,34},{"numbersign",0x23,35},{"dollar",0x24,36},{"percent",0x25,37},
    {"ampersand",0x26,38},{"quoteright",0x2019,39},{"parenleft",0x28,40},{"parenright",0x29,41},{"asterisk",0x2A,42},
    {"plus",0x2B,43},{"comma",0x2C,44},{"hyphen",0x2D,45},{"period",0x2E,46},{"slash",0x2F,47},
    {"zero",0x30,48},{"one",0x31,49},{"two",0x32,50},{"three",0x33,51},{"four",0x34,52},{"five",0x35,53},{"six",0x36,54},
    {"seven",0x37,55},{"eight",0x38,56},{"nine",0x39,57},{"colon",0x3A,58},{"semicolon",0x3B,59},{"less",0x3C,60},
    {"equal",0x3D,61},{"greater",0x3E,62},{"question",0x3F,63},{"at",0x40,64},{"bracketleft",0x5B,91},{"backslash",0x5C,92},
    {"bracketright",0x5D,93},{"asciicircum",0x5E,94},{"underscore",0x5F,95},{"quoteleft",0x2018,96},{"braceleft",0x7B,123},
    {"bar",0x7C,124},{"braceright",0x7D,125},{"asciitilde",0x7E,126},
    {"exclamdown",0xA1,161},{"cent",0xA2,162},{"sterling",0xA3,163},{"fraction",0x2044,164},{"yen",0xA5,165},{"florin",0x192,166},
    {"section",0xA7,167},{"currency",0xA4,168},{"quotesingle",0x27,169},{"quotedblleft",0x201C,170},{"guillemotleft",0xAB,171},
    {"guilsinglleft",0x2039,172},{"guilsinglright",0x203A,173},{"fi",0xFB01,174},{"fl",0xFB02,175},{"endash",0x2013,177},
    {"dagger",0x2020,178},{"daggerdbl",0x2021,179},{"periodcentered",0xB7,180},{"paragraph",0xB6,182},{"bullet",0x2022,183},
    {"quotesinglbase",0x201A,184},{"quotedblbase",0x201E,185},{"quotedblright",0x201D,186},{"guillemotright",0xBB,187},
    {"ellipsis",0x2026,188},{"perthousand",0x2030,189},{"questiondown",0xBF,191},{"grave",0x60,193},{"acute",0xB4,194},
    {"circumflex",0x2C6,195},{"tilde",0x2DC,196},{"macron",0xAF,197},{"breve",0x2D8,198},{"dotaccent",0x2D9,199},
    {"dieresis",0xA8,200},{"ring",0x2DA,202},{"cedilla",0xB8,203},{"hungarumlaut",0x2DD,205},{"ogonek",0x2DB,206},
    {"caron",0x2C7,207},{"emdash",0x2014,208},{"AE",0xC6,225},{"ordfeminine",0xAA,227},{"Lslash",0x141,232},{"Oslash",0xD8,233},
    {"OE",0x152,234},{"ordmasculine",0xBA,235},{"ae",0xE6,241},{"dotlessi",0x131,245},{"lslash",0x142,248},{"oslash",0xF8,249},
    {"oe",0x153,250},{"germandbls",0xDF,251},
    {"Agrave",0xC0,0},{"Aacute",0xC1,0},{"Acircumflex",0xC2,0},{"Atilde",0xC3,0},{"Adieresis",0xC4,0},{"Aring",0xC5,0},
    {"Ccedilla",0xC7,0},{"Egrave",0xC8,0},{"Eacute",0xC9,0},{"Ecircumflex",0xCA,0},{"Edieresis",0xCB,0},{"Igrave",0xCC,0},
    {"Iacute",0xCD,0},{"Icircumflex",0xCE,0},{"Idieresis",0xCF,0},{"Eth",0xD0,0},{"Ntilde",0xD1,0},{"Ograve",0xD2,0},
    {"Oacute",0xD3,0},{"Ocircumflex",0xD4,0},{"Otilde",0xD5,0},{"Odieresis",0xD6,0},{"multiply",0xD7,0},{"Ugrave",0xD9,0},
    {"Uacute",0xDA,0},{"Ucircumflex",0xDB,0},{"Udieresis",0xDC,0},{"Yacute",0xDD,0},{"Thorn",0xDE,0},
    {"agrave",0xE0,0},{"aacute",0xE1,0},{"acircumflex",0xE2,0},{"atilde",0xE3,0},{"adieresis",0xE4,0},{"aring",0xE5,0},
    {"ccedilla",0xE7,0},{"egrave",0xE8,0},{"eacute",0xE9,0},{"ecircumflex",0xEA,0},{"edieresis",0xEB,0},{"igrave",0xEC,0},
    {"iacute",0xED,0},{"icircumflex",0xEE,0},{"idieresis",0xEF,0},{"eth",0xF0,0},{"ntilde",0xF1,0},{"ograve",0xF2,0},
    {"oacute",0xF3,0},{"ocircumflex",0xF4,0},{"otilde",0xF5,0},{"odieresis",0xF6,0},{"divide",0xF7,0},{"ugrave",0xF9,0},
    {"uacute",0xFA,0},{"ucircumflex",0xFB,0},{"udieresis",0xFC,0},{"yacute",0xFD,0},{"thorn",0xFE,0},{"ydieresis",0xFF,0},
    {"copyright",0xA9,0},{"registered",0xAE,0},{"degree",0xB0,0},{"plusminus",0xB1,0},{"mu",0xB5,0},{"onesuperior",0xB9,0},
    {"twosuperior",0xB2,0},{"threesuperior",0xB3,0},{"onequarter",0xBC,0},{"onehalf",0xBD,0},{"threequarters",0xBE,0},
    {"brokenbar",0xA6,0},{"logicalnot",0xAC,0},{"nbspace",0xA0,0},{"Euro",0x20AC,0},{"trademark",0x2122,0},{"Scaron",0x160,0},
    {"scaron",0x161,0},{"Zcaron",0x17D,0},{"zcaron",0x17E,0},{"Ydieresis",0x178,0},{"minus",0x2212,0}
};
#define NAGL ((int)(sizeof AGL / sizeof AGL[0]))

static const unsigned short CP1252[32] = {
    0x20AC,0x81,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,0x2C6,0x2030,0x160,0x2039,0x152,0x8D,0x17D,0x8F,
    0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0x9D,0x17E,0x178 };

/* glyph name -> code point, or -1 */
static int name_to_cp(const char *nm) {
    int i, k = 0, len = (int)strlen(nm);
    if (len >= 7 && nm[0] == 'u' && nm[1] == 'n' && nm[2] == 'i' && len == 7) {
        unsigned v = 0;
        for (i = 3; i < 7; i++) { int c = nm[i]; v <<= 4; if (c >= '0' && c <= '9') v |= (unsigned)(c - '0'); else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10); else goto notuni; }
        return (int)v;
    }
notuni:
    if (len >= 5 && len <= 7 && nm[0] == 'u') {
        unsigned v = 0;
        for (i = 1; i < len; i++) { int c = nm[i]; v <<= 4; if (c >= '0' && c <= '9') v |= (unsigned)(c - '0'); else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10); else { k = -1; break; } }
        if (k == 0 && v <= 0x10FFFF) return (int)v;
    }
    if (len == 1 && ((nm[0] >= 'A' && nm[0] <= 'Z') || (nm[0] >= 'a' && nm[0] <= 'z'))) return nm[0];
    for (i = 0; i < NAGL; i++) if (strcmp(AGL[i].name, nm) == 0) return AGL[i].uni;
    return -1;
}

/* StandardEncoding code -> glyph name (used by seac); 0 if none */
static int std_name(int code, char *out) {
    int i;
    if ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')) { out[0] = (char)code; out[1] = 0; return 1; }
    for (i = 0; i < NAGL; i++) if (AGL[i].std == code && code) { strcpy(out, AGL[i].name); return 1; }
    return 0;
}

static void t1_free(T1 *t) {
    if (!t) return;
    free(t->priv); free(t->soff); free(t->slen); free(t->g); free(t->enc); free(t->cmCp); free(t->cmG);
    free(t);
}

static int t1_name_eq(const T1 *t, int gi, const char *nm) {
    size_t l = strlen(nm);
    return (size_t)t->g[gi].nlen == l && memcmp(t->priv + t->g[gi].noff, nm, l) == 0;
}

static int t1_find(const T1 *t, const char *nm) {
    int i;
    for (i = 0; i < t->ng; i++) if (t1_name_eq(t, i, nm)) return i;
    return -1;
}

/* ---- tokenizer over a byte range ---- */

static int t1_isws(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0; }
static int t1_isdelim(int c) { return c == '/' || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '%'; }

static int t1_tok(const u8 *s, size_t n, size_t *pos, size_t *ts, size_t *tl) {
    size_t p = *pos;
    for (;;) {
        while (p < n && t1_isws(s[p])) p++;
        if (p < n && s[p] == '%') { while (p < n && s[p] != '\n' && s[p] != '\r') p++; continue; }
        break;
    }
    if (p >= n) { *pos = p; return 0; }
    *ts = p;
    if (s[p] == '(') {
        int depth = 0;
        do { if (s[p] == '(') depth++; else if (s[p] == ')') depth--; else if (s[p] == '\\') p++; p++; } while (p < n && depth > 0);
        if (p > n) p = n;
    } else if (s[p] == '/') {
        p++;
        while (p < n && !t1_isws(s[p]) && !t1_isdelim(s[p])) p++;
    } else if (t1_isdelim(s[p])) {
        p++;
    } else {
        while (p < n && !t1_isws(s[p]) && !t1_isdelim(s[p])) p++;
    }
    *tl = p - *ts; *pos = p;
    return 1;
}

static int t1_int(const u8 *s, size_t ts, size_t tl, long *v) {
    size_t i = 0;
    long r = 0;
    int neg = 0;
    if (tl == 0) return 0;
    if (s[ts] == '-') { neg = 1; i = 1; }
    if (i >= tl) return 0;
    for (; i < tl; i++) { if (s[ts + i] < '0' || s[ts + i] > '9' || r > 100000000) return 0; r = r * 10 + (s[ts + i] - '0'); }
    *v = neg ? -r : r;
    return 1;
}

static void t1_decrypt(u8 *p, size_t n, unsigned r, int skip) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned c = p[i];
        p[i] = (u8)(c ^ (r >> 8));
        r = ((c + r) * 52845u + 22719u) & 0xFFFFu;
    }
    (void)skip;
}

static int t1_cmp(const void *a, const void *b) {
    const int *x = (const int *)a, *y = (const int *)b;
    if (x[0] != y[0]) return x[0] < y[0] ? -1 : 1;
    return x[1] < y[1] ? -1 : x[1] > y[1];
}

/* find "key" in clear text and return pointer just after it, or NULL */
static const char *t1_key(const char *txt, const char *key) {
    const char *p = strstr(txt, key);
    return p ? p + strlen(key) : NULL;
}

static void t1_string(const char *txt, const char *key, char *out, int outsz) {
    const char *p = t1_key(txt, key);
    int n = 0;
    out[0] = 0;
    if (!p) return;
    while (*p == ' ') p++;
    if (*p != '(') return;
    p++;
    while (*p && *p != ')' && n + 1 < outsz) { if (*p == '\\' && p[1]) p++; out[n++] = *p++; }
    out[n] = 0;
}

static int t1_load(Outline *f, char *err, int errsz) {
    const u8 *d = f->d;
    size_t n = f->n, pos = 0, clen = 0, blen = 0;
    u8 *clear = NULL, *bin = NULL;
    T1 *t = NULL;
    const char *p;
    size_t sp, ts, tl;
    int mode = 0, lenIV = 4, i;
    if (n < 6 || d[0] != 0x80) { seterr(err, errsz, "Type 1 font without PFB segments (PFA) is not supported"); return -1; }
    clear = (u8 *)malloc(n + 1); bin = (u8 *)malloc(n + 1);
    t = (T1 *)calloc(1, sizeof(T1));
    if (!clear || !bin || !t) { seterr(err, errsz, "out of memory"); goto bad; }
    while (pos + 6 <= n && d[pos] == 0x80) {
        unsigned type = d[pos + 1];
        size_t l = (size_t)d[pos + 2] | ((size_t)d[pos + 3] << 8) | ((size_t)d[pos + 4] << 16) | ((size_t)d[pos + 5] << 24);
        if (type == 3) break;
        if (l > n - pos - 6) { seterr(err, errsz, "PFB segment is cut short"); goto bad; }
        if (type == 1 && blen == 0) { memcpy(clear + clen, d + pos + 6, l); clen += l; }
        else if (type == 2) { memcpy(bin + blen, d + pos + 6, l); blen += l; }
        pos += 6 + l;
    }
    clear[clen] = 0;
    if (blen < 5) { seterr(err, errsz, "Type 1 font has no eexec section"); goto bad; }
    t1_decrypt(bin, blen, 55665, 4);
    memmove(bin, bin + 4, blen - 4); blen -= 4;   /* the first 4 bytes are random */
    t->priv = bin; t->privlen = blen; bin = NULL;
    t->upem = 1000; t->notdef = -1;
    {   /* clear text: matrix, bbox, names, encoding */
        const char *txt = (const char *)clear;
        double m = 0.001;
        p = t1_key(txt, "/FontMatrix");
        if (p) { while (*p && *p != '[' && *p != '{') p++; if (*p) { m = atof(p + 1); if (m > 0 && m < 1) t->upem = (int)(1.0 / m + 0.5); } }
        if (t->upem < 16 || t->upem > 16384) t->upem = 1000;
        p = t1_key(txt, "/FontBBox");
        if (p) { while (*p && *p != '[' && *p != '{') p++; if (*p) { char *q = (char *)p + 1; for (i = 0; i < 4; i++) { t->bbox[i] = (int)strtol(q, &q, 10); } } }
        t1_string(txt, "/FamilyName", f->family, (int)sizeof f->family);
        t1_string(txt, "/Weight", f->style, (int)sizeof f->style);
        if (!f->family[0]) {
            p = t1_key(txt, "/FontName");
            if (p) { int k = 0; while (*p == ' ') p++; if (*p == '/') p++; while (*p && !t1_isws((u8)*p) && k + 1 < (int)sizeof f->family) f->family[k++] = *p++; f->family[k] = 0; }
        }
        p = t1_key(txt, "/Encoding");
        if (p) {
            size_t cn = clen - (size_t)(p - txt), q = 0;
            const u8 *cb = (const u8 *)p;
            if (t1_tok(cb, cn, &q, &ts, &tl) && tl == 16 && memcmp(cb + ts, "StandardEncoding", 16) == 0) t->stdenc = 1;
            else {
                t->enc = (char (*)[48])calloc(256, 48);
                if (!t->enc) { seterr(err, errsz, "out of memory"); goto bad; }
                for (;;) {
                    long code; size_t a, al, b, bl, c, cl;
                    if (!t1_tok(cb, cn, &q, &ts, &tl)) break;
                    if (tl == 3 && memcmp(cb + ts, "def", 3) == 0) break;
                    if (tl == 3 && memcmp(cb + ts, "dup", 3) == 0) {
                        size_t save = q;
                        if (t1_tok(cb, cn, &q, &a, &al) && t1_int(cb, a, al, &code) && t1_tok(cb, cn, &q, &b, &bl) && bl > 1 && cb[b] == '/' &&
                            t1_tok(cb, cn, &q, &c, &cl) && cl == 3 && memcmp(cb + c, "put", 3) == 0) {
                            if (code >= 0 && code < 256 && bl - 1 < 48) { memcpy(t->enc[code], cb + b + 1, bl - 1); t->enc[code][bl - 1] = 0; }
                        } else q = save;
                    }
                }
            }
        }
    }
    /* private part: lenIV, Subrs, CharStrings */
    {
        const u8 *s = t->priv;
        size_t cap_s = 0, cap_g = 0;
        long v, len;
        sp = 0;
        while (t1_tok(s, t->privlen, &sp, &ts, &tl)) {
            if (tl == 6 && memcmp(s + ts, "/lenIV", 6) == 0) {
                size_t q = sp, a, al;
                if (t1_tok(s, t->privlen, &q, &a, &al) && t1_int(s, a, al, &v)) lenIV = (int)v;
            } else if (tl == 6 && memcmp(s + ts, "/Subrs", 6) == 0) mode = 1;
            else if (tl == 12 && memcmp(s + ts, "/CharStrings", 12) == 0) mode = 2;
            else if (mode == 1 && tl == 3 && memcmp(s + ts, "dup", 3) == 0) {
                size_t q = sp, a, al, b, bl, c, cl;
                long idx;
                if (t1_tok(s, t->privlen, &q, &a, &al) && t1_int(s, a, al, &idx) && t1_tok(s, t->privlen, &q, &b, &bl) &&
                    t1_int(s, b, bl, &len) && t1_tok(s, t->privlen, &q, &c, &cl) && cl <= 4 && idx >= 0 && idx < 100000 && len >= 0 &&
                    q + 1 + (size_t)len <= t->privlen) {
                    if ((size_t)idx >= cap_s) {
                        size_t nc = (size_t)idx + 16, k;
                        size_t *so = (size_t *)realloc(t->soff, nc * sizeof(size_t)), *sl = (size_t *)realloc(t->slen, nc * sizeof(size_t));
                        if (!so || !sl) { if (so) t->soff = so; if (sl) t->slen = sl; seterr(err, errsz, "out of memory"); goto bad; }
                        t->soff = so; t->slen = sl;
                        for (k = cap_s; k < nc; k++) { so[k] = 0; sl[k] = 0; }
                        cap_s = nc;
                    }
                    t->soff[idx] = q + 1; t->slen[idx] = (size_t)len;
                    if (idx + 1 > t->nsubrs) t->nsubrs = (int)idx + 1;
                    sp = q + 1 + (size_t)len;
                }
            } else if (mode == 2 && tl > 1 && s[ts] == '/') {
                size_t q = sp, a, al, c, cl;
                if (t1_tok(s, t->privlen, &q, &a, &al) && t1_int(s, a, al, &len) && t1_tok(s, t->privlen, &q, &c, &cl) && cl <= 4 &&
                    !(cl == 4 && memcmp(s + c, "dict", 4) == 0) && len >= 0 && q + 1 + (size_t)len <= t->privlen && tl - 1 < 200) {
                    if ((size_t)t->ng >= cap_g) {
                        size_t nc = cap_g ? cap_g * 2 : 64;
                        T1G *ng = (T1G *)realloc(t->g, nc * sizeof(T1G));
                        if (!ng) { seterr(err, errsz, "out of memory"); goto bad; }
                        t->g = ng; cap_g = nc;
                    }
                    t->g[t->ng].noff = ts + 1; t->g[t->ng].nlen = (int)tl - 1;
                    t->g[t->ng].off = q + 1; t->g[t->ng].len = (size_t)len;
                    t->ng++;
                    sp = q + 1 + (size_t)len;
                }
            } else if (mode == 2 && tl == 3 && memcmp(s + ts, "end", 3) == 0 && t->ng > 0) break;
        }
    }
    if (t->ng == 0) { seterr(err, errsz, "Type 1 font has no CharStrings"); goto bad; }
    /* decrypt the charstrings and subrs in place */
    for (i = 0; i < t->ng; i++) {
        T1G *g = &t->g[i];
        if (lenIV >= 0) {
            t1_decrypt(t->priv + g->off, g->len, 4330, 0);
            if (g->len >= (size_t)lenIV) { g->off += (size_t)lenIV; g->len -= (size_t)lenIV; } else g->len = 0;
        }
    }
    for (i = 0; i < t->nsubrs; i++) {
        if (lenIV >= 0 && t->slen[i]) {
            t1_decrypt(t->priv + t->soff[i], t->slen[i], 4330, 0);
            if (t->slen[i] >= (size_t)lenIV) { t->soff[i] += (size_t)lenIV; t->slen[i] -= (size_t)lenIV; } else t->slen[i] = 0;
        }
    }
    t->notdef = t1_find(t, ".notdef");
    /* character map: names, then the encoding */
    t->cmCp = (int *)malloc((size_t)t->ng * 2 * sizeof(int));
    if (!t->cmCp) { seterr(err, errsz, "out of memory"); goto bad; }
    for (i = 0; i < t->ng; i++) {
        char nm[208];
        int cp, code = -1, k;
        if (t->g[i].nlen >= (int)sizeof nm || i == t->notdef) continue;
        memcpy(nm, t->priv + t->g[i].noff, (size_t)t->g[i].nlen); nm[t->g[i].nlen] = 0;
        cp = name_to_cp(nm);
        if (cp < 0) {   /* by the place in the encoding */
            if (t->stdenc) { for (k = 0; k < NAGL; k++) if (strcmp(AGL[k].name, nm) == 0 && AGL[k].std) { code = AGL[k].std; break; } }
            else if (t->enc) { for (k = 0; k < 256; k++) if (strcmp(t->enc[k], nm) == 0) { code = k; break; } }
            if (code >= 0) cp = (code >= 0x80 && code < 0xA0) ? CP1252[code - 0x80] : code;
        }
        if (cp < 0 || cp == 0xFFFD) continue;
        t->cmCp[2 * t->ncm] = cp; t->cmCp[2 * t->ncm + 1] = i + 1; t->ncm++;
    }
    /* the encoding may also map a code to a name that has an AGL code point of its own: names win (as in pixelglyphlib) */
    qsort(t->cmCp, (size_t)t->ncm, 2 * sizeof(int), t1_cmp);
    f->t1 = t;
    free(clear);
    return 0;
bad:
    free(clear); free(bin); t1_free(t);
    return -1;
}

static int t1_lookup(const T1 *t, int cp) {
    int lo = 0, hi = t->ncm;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (t->cmCp[2 * mid] < cp) lo = mid + 1; else hi = mid;
    }
    return (lo < t->ncm && t->cmCp[2 * lo] == cp) ? t->cmCp[2 * lo + 1] : 0;
}

/* ---- charstring interpreter ---- */

typedef struct {
    const T1 *t; Path *p;
    double st[64]; int sp;
    double ps[24]; int psp;
    double x, y, sbx, sby, wx;
    int flex, nflex, open, haveW;
    double fx[8], fy[8];
    double ox, oy;
    long steps;
} T1S;

static void t1_moveto(T1S *s, double dx, double dy) {
    s->x += dx; s->y += dy;
    if (s->flex) { if (s->nflex < 8) { s->fx[s->nflex] = s->x; s->fy[s->nflex] = s->y; s->nflex++; } return; }
    P_MOVE(s->p, s->x, s->y); s->open = 1;
}
static void t1_lineto(T1S *s, double dx, double dy) {
    if (!s->open) { P_MOVE(s->p, s->x, s->y); s->open = 1; }
    s->x += dx; s->y += dy; P_LINE(s->p, s->x, s->y);
}
static void t1_curveto(T1S *s, double a, double b, double c, double d, double e, double f) {
    double x1 = s->x + a, y1 = s->y + b, x2 = x1 + c, y2 = y1 + d, x3 = x2 + e, y3 = y2 + f;
    if (!s->open) { P_MOVE(s->p, s->x, s->y); s->open = 1; }
    P_CUBIC(s->p, x1, y1, x2, y2, x3, y3);
    s->x = x3; s->y = y3;
}

static int t1_glyph_run(T1S *s, int gi, int depth);

/* 1 = endchar, 0 = return/end, -1 = error */
static int t1_cs(T1S *s, const u8 *cs, size_t len, int depth) {
    size_t pos = 0;
    double *st = s->st;
    if (depth > 10) return -1;
    while (pos < len) {
        unsigned b = cs[pos++];
        if (++s->steps > 400000) return -1;
        if (b >= 32) {
            double v;
            if (b <= 246) v = (double)b - 139;
            else if (b <= 250) { if (pos >= len) return -1; v = ((double)b - 247) * 256 + cs[pos++] + 108; }
            else if (b <= 254) { if (pos >= len) return -1; v = -((double)b - 251) * 256 - cs[pos++] - 108; }
            else { if (pos + 4 > len) return -1; v = (double)(int32_t)(((uint32_t)cs[pos] << 24) | ((uint32_t)cs[pos + 1] << 16) | ((uint32_t)cs[pos + 2] << 8) | cs[pos + 3]); pos += 4; }
            if (s->sp >= 64) return -1;
            st[s->sp++] = v;
            continue;
        }
        switch (b) {
        case 1: case 3: s->sp = 0; break;                                         /* hstem vstem */
        case 4: if (s->sp >= 1) t1_moveto(s, 0, st[s->sp - 1]); s->sp = 0; break;
        case 5: if (s->sp >= 2) t1_lineto(s, st[0], st[1]); s->sp = 0; break;
        case 6: if (s->sp >= 1) t1_lineto(s, st[0], 0); s->sp = 0; break;
        case 7: if (s->sp >= 1) t1_lineto(s, 0, st[0]); s->sp = 0; break;
        case 8: if (s->sp >= 6) t1_curveto(s, st[0], st[1], st[2], st[3], st[4], st[5]); s->sp = 0; break;
        case 9: s->open = 0; s->sp = 0; break;                                    /* closepath */
        case 10: {
            long i; size_t so, sl; int r;
            if (s->sp < 1) return -1;
            i = (long)st[--s->sp];
            if (i < 0 || i >= s->t->nsubrs) return -1;
            so = s->t->soff[i]; sl = s->t->slen[i];
            if (so + sl > s->t->privlen) return -1;
            r = t1_cs(s, s->t->priv + so, sl, depth + 1);
            if (r != 0) return r;
            break;
        }
        case 11: return 0;
        case 13:                                                                  /* hsbw */
            if (s->sp >= 2) {
                s->sbx = st[0]; s->sby = 0;
                if (!s->haveW) { s->wx = st[1]; s->haveW = 1; }
                s->x = s->sbx + s->ox; s->y = s->oy;
            }
            s->sp = 0; break;
        case 14: s->sp = 0; return 1;                                             /* endchar */
        case 21: if (s->sp >= 2) t1_moveto(s, st[0], st[1]); s->sp = 0; break;
        case 22: if (s->sp >= 1) t1_moveto(s, st[0], 0); s->sp = 0; break;
        case 30: if (s->sp >= 4) t1_curveto(s, 0, st[0], st[1], st[2], st[3], 0); s->sp = 0; break;
        case 31: if (s->sp >= 4) t1_curveto(s, st[0], 0, st[1], st[2], 0, st[3]); s->sp = 0; break;
        case 12: {
            unsigned b2 = pos < len ? cs[pos++] : 255;
            switch (b2) {
            case 0: case 1: case 2: s->sp = 0; break;                             /* dotsection vstem3 hstem3 */
            case 6: {                                                              /* seac: asb adx ady bchar achar */
                char nm[32];
                int bi, ai, r;
                double asb, adx, ady, ox = s->ox, oy = s->oy, sbx = s->sbx;
                int bch, ach;
                if (s->sp < 5 || depth > 6) return -1;
                asb = st[0]; adx = st[1]; ady = st[2]; bch = (int)st[3]; ach = (int)st[4];
                bi = std_name(bch, nm) ? t1_find(s->t, nm) : -1;
                s->sp = 0;
                if (bi >= 0) { r = t1_glyph_run(s, bi, depth + 1); if (r < 0) return -1; }
                ai = std_name(ach, nm) ? t1_find(s->t, nm) : -1;
                if (ai >= 0) {
                    s->ox = ox + sbx - asb + adx; s->oy = oy + ady;
                    r = t1_glyph_run(s, ai, depth + 1);
                    s->ox = ox; s->oy = oy;
                    if (r < 0) return -1;
                }
                s->sp = 0;
                return 1;
            }
            case 7:                                                                /* sbw */
                if (s->sp >= 4) { s->sbx = st[0]; s->sby = st[1]; if (!s->haveW) { s->wx = st[2]; s->haveW = 1; } s->x = s->sbx + s->ox; s->y = s->sby + s->oy; }
                s->sp = 0; break;
            case 12: if (s->sp >= 2) { st[s->sp - 2] = st[s->sp - 1] != 0 ? st[s->sp - 2] / st[s->sp - 1] : 0; s->sp--; } break;   /* div */
            case 16: {                                                             /* callothersubr */
                int idx, n, j;
                if (s->sp < 2) return -1;
                idx = (int)st[s->sp - 1]; n = (int)st[s->sp - 2]; s->sp -= 2;
                if (n < 0 || n > s->sp) return -1;
                if (idx == 1) { s->flex = 1; s->nflex = 0; }
                else if (idx == 2) { /* point recorded by rmoveto */ }
                else if (idx == 0 && n == 3) {
                    double ex = st[s->sp - 2], ey = st[s->sp - 1];
                    s->sp -= 3;
                    if (s->nflex >= 7) {
                        if (!s->open) { P_MOVE(s->p, s->fx[0], s->fy[0]); s->open = 1; }
                        P_CUBIC(s->p, s->fx[1], s->fy[1], s->fx[2], s->fy[2], s->fx[3], s->fy[3]);
                        P_CUBIC(s->p, s->fx[4], s->fy[4], s->fx[5], s->fy[5], s->fx[6], s->fy[6]);
                    }
                    s->flex = 0;
                    s->psp = 0; s->ps[s->psp++] = ey + s->oy; s->ps[s->psp++] = ex + s->ox;
                    if (s->nflex >= 7) { s->x = s->fx[6]; s->y = s->fy[6]; }
                } else {
                    s->psp = 0;
                    for (j = 0; j < n && s->psp < 24; j++) s->ps[s->psp++] = st[--s->sp];
                }
                break;
            }
            case 17: if (s->sp >= 64) return -1; st[s->sp++] = s->psp > 0 ? s->ps[--s->psp] : 0; break;   /* pop */
            case 33: if (s->sp >= 2) { s->x = st[s->sp - 2]; s->y = st[s->sp - 1]; } s->sp = 0; break; /* setcurrentpoint */
            default: return -1;
            }
            break;
        }
        default: return -1;
        }
    }
    return 0;
}

static int t1_glyph_run(T1S *s, int gi, int depth) {
    const T1G *g;
    if (gi < 0 || gi >= s->t->ng) return -1;
    g = &s->t->g[gi];
    if (g->off + g->len > s->t->privlen) return -1;
    s->sp = 0; s->open = 0; s->flex = 0;
    return t1_cs(s, s->t->priv + g->off, g->len, depth);
}

/* gid 0 is .notdef (or empty), gid k+1 is the k-th charstring of the file */
static int t1_load_glyph(const Outline *f, int gid, Path *p, double *wx) {
    T1S s;
    int gi;
    memset(&s, 0, sizeof s);
    s.t = f->t1; s.p = p;
    *wx = 0;
    gi = gid == 0 ? f->t1->notdef : gid - 1;
    if (gi < 0) return 0;
    if (gid < 0 || gid > f->t1->ng) return -1;
    if (t1_glyph_run(&s, gi, 0) < 0) return -1;
    *wx = s.wx;
    return 0;
}

/* ===================================================================== opening */

static void clear_face(Outline *f) {
    memset(&f->head, 0, sizeof f->head); memset(&f->maxp, 0, sizeof f->maxp); memset(&f->hhea, 0, sizeof f->hhea);
    memset(&f->hmtx, 0, sizeof f->hmtx); memset(&f->cmap, 0, sizeof f->cmap); memset(&f->loca, 0, sizeof f->loca);
    memset(&f->glyf, 0, sizeof f->glyf); memset(&f->name, 0, sizeof f->name); memset(&f->cff, 0, sizeof f->cff);
    f->upem = f->locfmt = f->nglyphs = f->nhm = f->asc = f->desc = f->gap = 0;
    f->isCFF = 0; f->isCID = 0;
    f->family[0] = f->style[0] = 0;
}

static int face_setup(Outline *f, int idx, char *err, int errsz) {
    size_t base = 0;
    uint32_t ver;
    unsigned nt, i;
    int cff2 = 0;
    clear_face(f);
    if (f->ttc) {
        if (idx < 0 || idx >= f->nfaces) { seterr(err, errsz, "no such face"); return -1; }
        base = r32(f, 12 + 4 * (size_t)idx);
    } else if (idx != 0) { seterr(err, errsz, "no such face"); return -1; }
    if (base >= f->n) { seterr(err, errsz, "face offset is outside the file"); return -1; }
    ver = r32(f, base);
    if (ver != 0x00010000 && ver != TAG('t', 'r', 'u', 'e') && ver != TAG('O', 'T', 'T', 'O') && ver != TAG('t', 'y', 'p', '1')) {
        seterr(err, errsz, "not an sfnt font"); return -1;
    }
    nt = r16(f, base + 4);
    if (nt == 0 || nt > 256) { seterr(err, errsz, "bad table count"); return -1; }
    for (i = 0; i < nt; i++) {
        size_t rec = base + 12 + 16 * (size_t)i;
        uint32_t tag = r32(f, rec), off = r32(f, rec + 8), len = r32(f, rec + 12);
        Tab t;
        if (rec + 16 > f->n) break;
        if (off >= f->n) continue;
        if ((u64)off + len > f->n) len = (uint32_t)(f->n - off);
        t.off = off; t.len = len;
        switch (tag) {
        case TAG('h', 'e', 'a', 'd'): f->head = t; break;
        case TAG('m', 'a', 'x', 'p'): f->maxp = t; break;
        case TAG('h', 'h', 'e', 'a'): f->hhea = t; break;
        case TAG('h', 'm', 't', 'x'): f->hmtx = t; break;
        case TAG('c', 'm', 'a', 'p'): f->cmap = t; break;
        case TAG('l', 'o', 'c', 'a'): f->loca = t; break;
        case TAG('g', 'l', 'y', 'f'): f->glyf = t; break;
        case TAG('n', 'a', 'm', 'e'): f->name = t; break;
        case TAG('C', 'F', 'F', ' '): f->cff = t; break;
        case TAG('C', 'F', 'F', '2'): cff2 = 1; break;
        default: break;
        }
    }
    if (f->head.len < 54) { seterr(err, errsz, "no head table"); return -1; }
    f->upem = (int)r16(f, f->head.off + 18);
    if (f->upem < 16 || f->upem > 16384) { seterr(err, errsz, "bad units per em"); return -1; }
    f->locfmt = r16s(f, f->head.off + 50) ? 1 : 0;
    f->nglyphs = f->maxp.len >= 6 ? (int)r16(f, f->maxp.off + 4) : 0;
    f->isCFF = f->cff.len > 0;
    if (f->isCFF) {
        if (cff_setup(f) < 0) { seterr(err, errsz, "bad CFF table"); return -1; }
    } else {
        int maxg;
        if (!f->glyf.len || !f->loca.len) {
            seterr(err, errsz, cff2 ? "CFF2 (variable CFF) outlines are not supported" : "font has no outlines (glyf/CFF)");
            return -1;
        }
        maxg = (int)(f->loca.len / (f->locfmt ? 4 : 2)) - 1;
        if (f->nglyphs > maxg) f->nglyphs = maxg;
    }
    if (f->nglyphs <= 0) { seterr(err, errsz, "font has no glyphs"); return -1; }
    if (f->hhea.len >= 36) {
        f->asc = r16s(f, f->hhea.off + 4); f->desc = r16s(f, f->hhea.off + 6); f->gap = r16s(f, f->hhea.off + 8);
        f->nhm = (int)r16(f, f->hhea.off + 34);
        if ((size_t)f->nhm > f->hmtx.len / 4) f->nhm = (int)(f->hmtx.len / 4);
    }
    cmap_choose(f);
    if (!name_string(f, 16, f->family, (int)sizeof f->family)) name_string(f, 1, f->family, (int)sizeof f->family);
    if (!name_string(f, 17, f->style, (int)sizeof f->style)) name_string(f, 2, f->style, (int)sizeof f->style);
    return 0;
}

static int woff_decode(Outline *f, char *err, int errsz) {
    unsigned nt = r16(f, 12), i;
    uint32_t flavor = r32(f, 4);
    u64 total;
    u8 *out;
    size_t pos, sr = 1, es = 0;
    if (nt == 0 || nt > 256 || 44 + 20 * (size_t)nt > f->n) { seterr(err, errsz, "bad WOFF header"); return -1; }
    total = 12 + 16 * (u64)nt;
    for (i = 0; i < nt; i++) {
        uint32_t orig = r32(f, 44 + 20 * (size_t)i + 12);
        total += ((u64)orig + 3) & ~(u64)3;
        if (total > MAX_FILE) { seterr(err, errsz, "WOFF is too large"); return -1; }
    }
    out = (u8 *)calloc(1, (size_t)total + 1);
    if (!out) { seterr(err, errsz, "out of memory"); return -1; }
    out[0] = (u8)(flavor >> 24); out[1] = (u8)(flavor >> 16); out[2] = (u8)(flavor >> 8); out[3] = (u8)flavor;
    out[4] = (u8)(nt >> 8); out[5] = (u8)nt;
    while (sr * 2 <= nt) { sr *= 2; es++; }
    out[6] = (u8)((sr * 16) >> 8); out[7] = (u8)(sr * 16);
    out[8] = (u8)(es >> 8); out[9] = (u8)es;
    out[10] = (u8)((nt * 16 - sr * 16) >> 8); out[11] = (u8)(nt * 16 - sr * 16);
    pos = 12 + 16 * (size_t)nt;
    for (i = 0; i < nt; i++) {
        size_t e = 44 + 20 * (size_t)i, od = 12 + 16 * (size_t)i;
        uint32_t off = r32(f, e + 4), comp = r32(f, e + 8), orig = r32(f, e + 12), csum = r32(f, e + 16);
        if ((u64)off + comp > f->n || comp > orig) { free(out); seterr(err, errsz, "bad WOFF table entry"); return -1; }
        memcpy(out + od, f->d + e, 4);
        out[od + 4] = (u8)(csum >> 24); out[od + 5] = (u8)(csum >> 16); out[od + 6] = (u8)(csum >> 8); out[od + 7] = (u8)csum;
        out[od + 8] = (u8)(pos >> 24); out[od + 9] = (u8)(pos >> 16); out[od + 10] = (u8)(pos >> 8); out[od + 11] = (u8)pos;
        out[od + 12] = (u8)(orig >> 24); out[od + 13] = (u8)(orig >> 16); out[od + 14] = (u8)(orig >> 8); out[od + 15] = (u8)orig;
        if (comp == orig) memcpy(out + pos, f->d + off, orig);
        else if (inflate_zlib(f->d + off, comp, out + pos, orig) != 0) { free(out); seterr(err, errsz, "corrupt WOFF table data"); return -1; }
        pos += ((size_t)orig + 3) & ~(size_t)3;
    }
    free(f->d); f->d = out; f->n = (size_t)total;
    return 0;
}

static unsigned le32(const u8 *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }

static int eot_check(const Outline *f) {
    return f->n >= 36 && le32(f->d) == f->n && (f->d[34] | (f->d[35] << 8)) == 0x504C;
}

static int eot_decode(Outline *f, char *err, int errsz) {
    unsigned fsz = le32(f->d + 4), flags = le32(f->d + 12);
    u8 *out;
    size_t i;
    if (flags & 0x4) { seterr(err, errsz, "EOT with MicroType Express compression is unsupported"); return -1; }
    if (fsz > f->n || fsz < 12) { seterr(err, errsz, "the font data of the EOT is cut short"); return -1; }
    out = (u8 *)malloc((size_t)fsz + 1);
    if (!out) { seterr(err, errsz, "out of memory"); return -1; }
    memcpy(out, f->d + (f->n - fsz), fsz);
    if (flags & 0x10000000u) for (i = 0; i < fsz; i++) out[i] ^= 0x50;
    free(f->d); f->d = out; f->n = fsz;
    return 0;
}

Outline *outline_open_mem(const void *data, size_t size, const char *ext, char *err, int errsz) {
    Outline *f;
    uint32_t m;
    const char *kind = NULL;
    if (!data || size < 12) { seterr(err, errsz, "file is too short to be a font"); return NULL; }
    if (size > MAX_FILE) { seterr(err, errsz, "file is too large"); return NULL; }
    f = (Outline *)calloc(1, sizeof *f);
    if (!f) { seterr(err, errsz, "out of memory"); return NULL; }
    f->d = (u8 *)malloc(size + 1);
    if (!f->d) { free(f); seterr(err, errsz, "out of memory"); return NULL; }
    memcpy(f->d, data, size); f->d[size] = 0; f->n = size;
    m = r32(f, 0);
    if (m == TAG('w', 'O', 'F', '2')) { seterr(err, errsz, "WOFF2 is not supported"); goto bad; }
    if (m == TAG('w', 'O', 'F', 'F')) { if (woff_decode(f, err, errsz) < 0) goto bad; kind = "woff"; }
    else if (eot_check(f)) { if (eot_decode(f, err, errsz) < 0) goto bad; kind = "eot"; }
    if (kind) m = r32(f, 0);
    if (!kind && f->d[0] == 0x80 && f->d[1] == 1) {
        if (t1_load(f, err, errsz) < 0) goto bad;
        strcpy(f->fmt, "pfb");
        f->nfaces = 1;
        if (!f->style[0]) strcpy(f->style, "Regular");
        return f;
    }
    if (m == TAG('t', 't', 'c', 'f')) {
        f->ttc = 1; f->nfaces = (int)r32(f, 8);
        if (f->nfaces <= 0 || f->nfaces > 256 || 12 + 4 * (size_t)f->nfaces > f->n) { seterr(err, errsz, "bad collection header"); goto bad; }
    } else if (m == 0x00010000 || m == TAG('t', 'r', 'u', 'e') || m == TAG('O', 'T', 'T', 'O') || m == TAG('t', 'y', 'p', '1')) {
        f->nfaces = 1;
    } else {
        seterr(err, errsz, "unrecognised font format%s%s", ext && *ext ? " for " : "", ext && *ext ? ext : "");
        goto bad;
    }
    if (face_setup(f, 0, err, errsz) < 0) goto bad;
    strcpy(f->fmt, kind ? kind : f->ttc ? "ttc" : f->isCFF ? "otf" : "ttf");
    return f;
bad:
    outline_close(f);
    return NULL;
}

Outline *outline_open(const char *path, char *err, int errsz) {
    FILE *fp;
    long sz;
    u8 *buf;
    Outline *f;
    const char *ext;
    if (!path) { seterr(err, errsz, "no path"); return NULL; }
    fp = fopen(path, "rb");
    if (!fp) { seterr(err, errsz, "cannot open %s", path); return NULL; }
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 0 || (size_t)sz > MAX_FILE) { fclose(fp); seterr(err, errsz, "file is empty or too large"); return NULL; }
    buf = (u8 *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); seterr(err, errsz, "out of memory"); return NULL; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); free(buf); seterr(err, errsz, "read error on %s", path); return NULL; }
    fclose(fp);
    ext = strrchr(path, '.');
    f = outline_open_mem(buf, (size_t)sz, ext, err, errsz);
    free(buf);
    return f;
}

void outline_close(Outline *f) {
    if (!f) return;
    t1_free(f->t1);
    free(f->d);
    free(f);
}

int outline_face_count(const Outline *f) { return f ? f->nfaces : 0; }
int outline_face(const Outline *f) { return f ? f->face : 0; }

int outline_select_face(Outline *f, int index, char *err, int errsz) {
    Outline tmp;
    if (!f) return -1;
    if (f->t1 || !f->ttc) {
        if (index != 0) { seterr(err, errsz, "no such face"); return -1; }
        return 0;
    }
    tmp = *f;
    if (face_setup(&tmp, index, err, errsz) < 0) return -1;
    tmp.face = index;
    *f = tmp;
    return 0;
}

const char *outline_format(const Outline *f) { return f ? f->fmt : ""; }
const char *outline_family(const Outline *f) { return f ? f->family : ""; }
const char *outline_style(const Outline *f) { return f ? f->style : ""; }
int outline_units_per_em(const Outline *f) { return f ? (f->t1 ? f->t1->upem : f->upem) : 0; }
int outline_glyph_count(const Outline *f) { return !f ? 0 : f->t1 ? f->t1->ng + 1 : f->nglyphs; }

void outline_metrics(const Outline *f, int *asc, int *desc, int *gap) {
    int a = 0, d = 0, g = 0;
    if (f) {
        if (f->t1) { a = f->t1->bbox[3]; d = f->t1->bbox[1]; }
        else { a = f->asc; d = f->desc; g = f->gap; }
    }
    if (asc) *asc = a;
    if (desc) *desc = d;
    if (gap) *gap = g;
}

/* ===================================================================== lookup */

int outline_glyph_index(const Outline *f, int cp) {
    unsigned g;
    if (!f || cp < 0 || cp > 0x10FFFF) return 0;
    if (f->t1) return t1_lookup(f->t1, cp);
    if (!f->cmapFmt) return 0;
    g = cmap_sub_lookup(f, f->cmapOff, f->cmapFmt, (unsigned)cp);
    if (g == 0 && f->cmapSym && cp < 0x100) g = cmap_sub_lookup(f, f->cmapOff, f->cmapFmt, 0xF000u + (unsigned)cp);
    return g < (unsigned)f->nglyphs ? (int)g : 0;
}

int outline_advance(const Outline *f, int g) {
    if (!f) return 0;
    if (f->t1) {
        Path p; double wx = 0;
        memset(&p, 0, sizeof p);
        if (g < 0 || g > f->t1->ng || t1_load_glyph(f, g, &p, &wx) < 0) wx = 0;
        path_free(&p);
        return (int)(wx + (wx < 0 ? -0.5 : 0.5));
    }
    if (g < 0 || g >= f->nglyphs || f->nhm <= 0) return 0;
    return (int)r16(f, f->hmtx.off + 4 * (size_t)(g < f->nhm ? g : f->nhm - 1));
}

/* ===================================================================== rasteriser */

typedef struct { float *x, *y; int n, cap; int *cs; int nc, ccap; int bad; double minx, miny, maxx, maxy; } Poly;

static void poly_free(Poly *q) { free(q->x); free(q->y); free(q->cs); memset(q, 0, sizeof *q); }

static void poly_new(Poly *q) {
    if (q->nc >= q->ccap) {
        int nc = q->ccap ? q->ccap * 2 : 16;
        int *ns = (int *)realloc(q->cs, (size_t)nc * sizeof(int));
        if (!ns) { q->bad = 1; return; }
        q->cs = ns; q->ccap = nc;
    }
    q->cs[q->nc++] = q->n;
}

static void poly_pt(Poly *q, double x, double y) {
    if (q->bad) return;
    if (!(fabs(x) < 1e7 && fabs(y) < 1e7)) { q->bad = 1; return; }
    if (q->n >= q->cap) {
        int nc = q->cap ? q->cap * 2 : 256;
        float *nx, *ny;
        if (nc > (4 << 20)) { q->bad = 1; return; }
        nx = (float *)realloc(q->x, (size_t)nc * sizeof(float));
        if (nx) q->x = nx;
        ny = (float *)realloc(q->y, (size_t)nc * sizeof(float));
        if (ny) q->y = ny;
        if (!nx || !ny) { q->bad = 1; return; }
        q->cap = nc;
    }
    q->x[q->n] = (float)x; q->y[q->n] = (float)y; q->n++;
    if (x < q->minx) q->minx = x;
    if (x > q->maxx) q->maxx = x;
    if (y < q->miny) q->miny = y;
    if (y > q->maxy) q->maxy = y;
}

static int steps_for(double dd, double k) {
    int n;
    if (!(dd < 1e7)) return 1;
    n = (int)ceil(sqrt(dd * k)) ;
    return n < 1 ? 1 : n > 100 ? 100 : n;
}

static void flatten(const Path *p, double s, double fx, double fy, Poly *q) {
    double cx = fx, cy = fy;   /* current point in px */
    int i;
    q->minx = q->miny = 1e30; q->maxx = q->maxy = -1e30;
    for (i = 0; i < p->n && !q->bad; i++) {
        const Vert *v = &p->v[i];
        double X = v->x * s + fx, Y = v->y * s + fy;
        if (v->t == V_MOVE) { poly_new(q); poly_pt(q, X, Y); }
        else {
            int k, n;
            if (q->nc == 0) { poly_new(q); poly_pt(q, cx, cy); }
            if (v->t == V_LINE) poly_pt(q, X, Y);
            else if (v->t == V_QUAD) {
                double qx = v->cx * s + fx, qy = v->cy * s + fy;
                double ddx = cx - 2 * qx + X, ddy = cy - 2 * qy + Y;
                n = steps_for(sqrt(ddx * ddx + ddy * ddy), 2.5);
                for (k = 1; k <= n; k++) {
                    double t = (double)k / n, u = 1 - t;
                    poly_pt(q, u * u * cx + 2 * u * t * qx + t * t * X, u * u * cy + 2 * u * t * qy + t * t * Y);
                }
            } else {
                double ax = v->cx * s + fx, ay = v->cy * s + fy, bx = v->cx1 * s + fx, by = v->cy1 * s + fy;
                double d1x = cx - 2 * ax + bx, d1y = cy - 2 * ay + by, d2x = ax - 2 * bx + X, d2y = ay - 2 * by + Y;
                double dd = fmax(sqrt(d1x * d1x + d1y * d1y), sqrt(d2x * d2x + d2y * d2y));
                n = steps_for(dd, 7.5);
                for (k = 1; k <= n; k++) {
                    double t = (double)k / n, u = 1 - t;
                    poly_pt(q, u * u * u * cx + 3 * u * u * t * ax + 3 * u * t * t * bx + t * t * t * X,
                               u * u * u * cy + 3 * u * u * t * ay + 3 * u * t * t * by + t * t * t * Y);
                }
            }
        }
        cx = X; cy = Y;
    }
}

/* exact-area accumulation of one edge (after the "font-rs" method): a holds signed area deltas, stride = w + 2 */
static void acc_line(float *a, int stride, int w, int h, float x0, float y0, float x1, float y1) {
    float dir, dxdy, x;
    int y, ystart, yend;
    if (y0 == y1) return;
    if (y0 < y1) dir = 1;
    else { float t; dir = -1; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    dxdy = (x1 - x0) / (y1 - y0);
    if (!(fabsf(dxdy) < 1e9f)) dxdy = 0;   /* nearly horizontal edge: no area worth the trouble (and no inf/NaN) */
    x = x0;
    if (y0 < 0) x -= y0 * dxdy;
    ystart = y0 < 0 ? 0 : (int)y0;
    yend = (int)ceilf(y1);
    if (yend > h) yend = h;
    for (y = ystart; y < yend; y++) {
        float *row = a + (size_t)y * (size_t)stride;
        float dy = fminf((float)(y + 1), y1) - fmaxf((float)y, y0);
        float xnext = x + dxdy * dy, d = dy * dir;
        float xa = x < xnext ? x : xnext, xb = x < xnext ? xnext : x;
        float fl, ce;
        int x0i, x1i;
        if (!(xa >= 0.0f)) xa = 0;   /* NaN-safe clamps: the indices below must stay inside the row */
        if (!(xb >= 0.0f)) xb = 0;
        if (!(xa <= (float)w)) xa = (float)w;
        if (!(xb <= (float)w)) xb = (float)w;
        fl = floorf(xa); ce = ceilf(xb);
        x0i = (int)fl; x1i = (int)ce;
        if (x1i <= x0i + 1) {
            float xmf = 0.5f * (xa + xb) - fl;
            row[x0i] += d - d * xmf;
            row[x0i + 1] += d * xmf;
        } else {
            float s = 1.0f / (xb - xa);
            float x0f = xa - fl, a0 = 0.5f * s * (1 - x0f) * (1 - x0f);
            float x1f = xb - ce + 1, am = 0.5f * s * x1f * x1f;
            row[x0i] += d * a0;
            if (x1i == x0i + 2) row[x0i + 1] += d * (1 - a0 - am);
            else {
                int xi;
                float a1 = s * (1.5f - x0f), a2;
                row[x0i + 1] += d * (a1 - a0);
                for (xi = x0i + 2; xi < x1i - 1; xi++) row[xi] += d * s;
                a2 = a1 + (float)(x1i - x0i - 3) * s;
                row[x1i - 1] += d * (1 - a2 - am);
            }
            row[x1i] += d * am;
        }
        x = xnext;
    }
}

static int fill(const Poly *q, int left, int top, int w, int h, u8 *out) {
    int stride = w + 2, c, y, x;
    float *a = (float *)calloc((size_t)stride * (size_t)h, sizeof(float));
    if (!a) return -1;
    for (c = 0; c < q->nc; c++) {
        int s = q->cs[c], e = (c + 1 < q->nc) ? q->cs[c + 1] : q->n, k;
        if (e - s < 2) continue;
        for (k = s; k < e; k++) {
            int k2 = (k + 1 < e) ? k + 1 : s;
            acc_line(a, stride, w, h, q->x[k] - (float)left, (float)top - q->y[k], q->x[k2] - (float)left, (float)top - q->y[k2]);
        }
    }
    for (y = 0; y < h; y++) {
        const float *row = a + (size_t)y * (size_t)stride;
        float acc = 0;
        for (x = 0; x < w; x++) {
            float v;
            acc += row[x];
            v = fabsf(acc);
            if (v > 1) v = 1;
            out[(size_t)y * (size_t)w + (size_t)x] = (u8)(v * 255.0f + 0.5f);
        }
    }
    free(a);
    return 0;
}

static int glyph_poly(const Outline *f, int g, float px, float fx, float fy, Poly *q, double *adv, char *err, int errsz) {
    Path p;
    double wx = 0, s;
    int rc;
    memset(q, 0, sizeof *q);
    memset(&p, 0, sizeof p);
    if (!f) { seterr(err, errsz, "no font"); return -1; }
    if (!(px >= 0.5f && px <= 8192.0f) || !(fabsf(fx) < 1e4f) || !(fabsf(fy) < 1e4f)) { seterr(err, errsz, "bad size or offset"); return -1; }
    if (g < 0 || g >= outline_glyph_count(f)) { seterr(err, errsz, "bad glyph index"); return -1; }
    if (f->t1) rc = t1_load_glyph(f, g, &p, &wx);
    else if (f->isCFF) { rc = cff_load(f, g, &p); wx = outline_advance(f, g); }
    else { int budget = 256; rc = glyf_load(f, g, &p, 0, &budget); wx = outline_advance(f, g); }
    if (rc < 0 || p.oom) { path_free(&p); seterr(err, errsz, "bad glyph data (glyph %d)", g); return -1; }
    s = (double)px / (double)outline_units_per_em(f);
    *adv = wx * s;
    flatten(&p, s, fx, fy, q);
    path_free(&p);
    if (q->bad) { poly_free(q); seterr(err, errsz, "glyph is too large or out of memory"); return -1; }
    return 0;
}

/* pixel box of the polygon: left, top (y up), w, h; 0 when the glyph has no ink */
static int poly_box(const Poly *q, int *left, int *top, int *w, int *h, char *err, int errsz) {
    int r, b;
    *left = *top = *w = *h = 0;
    if (q->n < 2 || q->minx > q->maxx) return 0;
    *left = (int)floor(q->minx); r = (int)ceil(q->maxx);
    b = (int)floor(q->miny); *top = (int)ceil(q->maxy);
    *w = r - *left; *h = *top - b;
    if (*w <= 0 || *h <= 0) { *w = *h = 0; return 0; }
    if (*w > MAX_DIM || *h > MAX_DIM || (long long)*w * *h > MAX_PIXELS) {
        seterr(err, errsz, "bitmap would be too large (%d x %d)", *w, *h);
        *w = *h = 0;
        return -1;
    }
    return 0;
}

int outline_glyph_bbox(const Outline *f, int g, float px, float fx, float fy, int *left, int *top, int *w, int *h,
                       float *advance, char *err, int errsz) {
    Poly q;
    double adv = 0;
    int l, t, ww, hh, rc;
    if (glyph_poly(f, g, px, fx, fy, &q, &adv, err, errsz) < 0) return OUTLINE_ERROR;
    rc = poly_box(&q, &l, &t, &ww, &hh, err, errsz);
    poly_free(&q);
    if (rc < 0) return OUTLINE_ERROR;
    if (left) *left = l;
    if (top) *top = t;
    if (w) *w = ww;
    if (h) *h = hh;
    if (advance) *advance = (float)adv;
    return OUTLINE_OK;
}

int outline_rasterize_glyph(const Outline *f, int g, float px, float fx, float fy, OutlineBitmap *out, char *err, int errsz) {
    Poly q;
    double adv = 0;
    int rc;
    if (!out) { seterr(err, errsz, "no output"); return OUTLINE_ERROR; }
    memset(out, 0, sizeof *out);
    if (glyph_poly(f, g, px, fx, fy, &q, &adv, err, errsz) < 0) return OUTLINE_ERROR;
    out->advance = (float)adv;
    rc = poly_box(&q, &out->left, &out->top, &out->w, &out->h, err, errsz);
    if (rc < 0) { poly_free(&q); out->advance = 0; return OUTLINE_ERROR; }
    if (out->w > 0) {
        out->pix = (u8 *)malloc((size_t)out->w * (size_t)out->h);
        if (!out->pix || fill(&q, out->left, out->top, out->w, out->h, out->pix) < 0) {
            free(out->pix);
            memset(out, 0, sizeof *out);
            poly_free(&q);
            seterr(err, errsz, "out of memory");
            return OUTLINE_ERROR;
        }
    }
    poly_free(&q);
    return OUTLINE_OK;
}

int outline_rasterize(const Outline *f, int cp, float px, float fx, float fy, OutlineBitmap *out, char *err, int errsz) {
    int g = outline_glyph_index(f, cp);
    if (out) memset(out, 0, sizeof *out);
    if (g == 0) return OUTLINE_MISSING;
    return outline_rasterize_glyph(f, g, px, fx, fy, out, err, errsz);
}

void outline_bitmap_free(OutlineBitmap *b) {
    if (!b) return;
    free(b->pix);
    memset(b, 0, sizeof *b);
}
