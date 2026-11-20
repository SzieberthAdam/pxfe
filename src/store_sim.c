/* store_sim.c - similar fonts and typefaces (DESIGN.md "Similar fonts and typefaces"). See store.h.
 *
 * The cache: for every font, the glyphs of the reference code points (U+0021..U+007E and U+00C0..U+017F, 286 slots)
 * as bit rows (two 64-bit words per row: up to 128 columns, a wider bitmap is clipped), the sorted set of its glyph
 * hashes (first 8 bytes of each), its height, its monospacing and its space / mean advance.
 * One font against the library is then a pass over 286 slots per other font plus a merge of two sorted sets. */
#include "store.h"
#include "store_int.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSLOT 286
#define MIN_SHARED_REF 10
#define W_SHAPE 0.60
#define W_SHARED 0.25
#define W_METRICS 0.15

typedef struct SimGlyph {
    uint64_t id;            /* first 8 bytes of the pxgl hash; 0 = no ink */
    int x, y, w, h;         /* offsets of the bottom-left corner, bitmap size */
    int ink;                /* inked pixels */
    uint64_t *rows;         /* h rows of 2 words, top row first; bit c = column c from the left (clipped at 128) */
} SimGlyph;

typedef struct SimFont {
    uint8_t hash[16];
    int height;             /* ASCENT + DESCENT, else from the glyphs */
    bool mono;
    int spaceAdv;           /* advance of U+0020, -1 when none */
    double meanAdv;         /* mean advance of the code point glyph lines */
    int nrefs;
    SimGlyph *refs;
    int16_t slot[NSLOT];    /* index into refs, -1 */
    int nref;               /* slots filled */
    uint64_t *set; int nset;    /* sorted distinct glyph ids */
} SimFont;

typedef struct SimTf {
    uint8_t hash[16];
    int *fonts; int nfonts;     /* indexes into StoreSim.f */
} SimTf;

typedef struct StoreSim {
    SimFont *f; int nf, capf;       /* sorted by hash */
    SimTf *tf; int ntf;             /* sorted by hash */
    bool ready;
} StoreSim;

static bool fail(char err[PX_ERR_LEN], const char *fmt, ...) {
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, PX_ERR_LEN, fmt, ap);
        va_end(ap);
    }
    return false;
}

static void simfont_free(SimFont *f) {
    for (int i = 0; i < f->nrefs; i++) free(f->refs[i].rows);
    free(f->refs);
    free(f->set);
    memset(f, 0, sizeof *f);
}

static void tf_free_all(StoreSim *m) {
    for (int i = 0; i < m->ntf; i++) free(m->tf[i].fonts);
    free(m->tf);
    m->tf = NULL; m->ntf = 0;
}

static void sim_free(StoreSim *m) {
    if (!m) return;
    for (int i = 0; i < m->nf; i++) simfont_free(&m->f[i]);
    free(m->f);
    tf_free_all(m);
    free(m);
}

void store_sim_free(Store *s) {
    sim_free(s->sim);
    s->sim = NULL;
}

static int slot_of(int32_t code) {
    if (code >= 0x21 && code <= 0x7E) return code - 0x21;
    if (code >= 0xC0 && code <= 0x17F) return 94 + (code - 0xC0);
    return -1;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static uint64_t id_of(const uint8_t *h) {
    uint64_t v;
    memcpy(&v, h, 8);
    return v ? v : 1;       /* 0 is reserved for "no ink" */
}

/* --- reading the rows of fonts (all, or one) */

typedef struct Meta { uint8_t hash[16]; int height; bool hasHeight; bool mono; } Meta;

static bool read_fonts(sqlite3 *db, const uint8_t *only, SimFont **outf, int *outn, char err[PX_ERR_LEN]) {
    sqlite3_stmt *st;
    /* metadata */
    Meta *meta = NULL;
    int nmeta = 0, capm = 2048;
    meta = (Meta *)malloc(sizeof(Meta) * (size_t)capm);
    if (sqlite3_prepare_v2(db, only ? "SELECT hash, height, monospaced FROM pxfm WHERE hash = ?1 ORDER BY hash"
                                    : "SELECT hash, height, monospaced FROM pxfm ORDER BY hash", -1, &st, NULL) != SQLITE_OK) {
        free(meta);
        return fail(err, "sqlite: %s", sqlite3_errmsg(db));
    }
    if (only) sqlite3_bind_blob(st, 1, only, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (nmeta == capm) { capm *= 2; meta = (Meta *)realloc(meta, sizeof(Meta) * (size_t)capm); }
        Meta *m = &meta[nmeta++];
        memcpy(m->hash, sqlite3_column_blob(st, 0), 16);
        m->hasHeight = sqlite3_column_type(st, 1) != SQLITE_NULL;
        m->height = sqlite3_column_int(st, 1);
        m->mono = sqlite3_column_int(st, 2) != 0;
    }
    sqlite3_finalize(st);
    SimFont *fonts = (SimFont *)calloc((size_t)(nmeta ? nmeta : 1), sizeof(SimFont));
    int *setcap = (int *)calloc((size_t)(nmeta ? nmeta : 1), sizeof(int));
    int *topv = (int *)calloc((size_t)(nmeta ? nmeta : 1), sizeof(int));
    int *botv = (int *)calloc((size_t)(nmeta ? nmeta : 1), sizeof(int));
    long *advn = (long *)calloc((size_t)(nmeta ? nmeta : 1), sizeof(long));
    double *advsum = (double *)calloc((size_t)(nmeta ? nmeta : 1), sizeof(double));
    for (int i = 0; i < nmeta; i++) {
        SimFont *f = &fonts[i];
        memcpy(f->hash, meta[i].hash, 16);
        f->mono = meta[i].mono;
        f->height = meta[i].hasHeight ? meta[i].height : -1;
        f->spaceAdv = -1;
        for (int k = 0; k < NSLOT; k++) f->slot[k] = -1;
    }
    const char *sql = only
        ? "SELECT p.pxfm, p.code, p.x, p.y, p.advance, p.glyph, g.width, g.height,"
          " CASE WHEN (p.code BETWEEN 135168 AND 516096) OR (p.code BETWEEN 786432 AND 1568768) THEN g.pixels END"
          " FROM pxfm_glyph AS p LEFT JOIN glyph AS g ON g.hash = p.glyph WHERE p.pxfm = ?1 ORDER BY p.pxfm, p.code"
        : "SELECT p.pxfm, p.code, p.x, p.y, p.advance, p.glyph, g.width, g.height,"
          " CASE WHEN (p.code BETWEEN 135168 AND 516096) OR (p.code BETWEEN 786432 AND 1568768) THEN g.pixels END"
          " FROM pxfm_glyph AS p LEFT JOIN glyph AS g ON g.hash = p.glyph ORDER BY p.pxfm, p.code";
    bool ok = true;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) ok = fail(err, "sqlite: %s", sqlite3_errmsg(db));
    else {
        if (only) sqlite3_bind_blob(st, 1, only, 16, SQLITE_STATIC);
        int fi = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
            const uint8_t *ph = (const uint8_t *)sqlite3_column_blob(st, 0);
            while (fi < nmeta && memcmp(fonts[fi].hash, ph, 16) < 0) fi++;
            if (fi >= nmeta || memcmp(fonts[fi].hash, ph, 16) != 0) continue;
            SimFont *f = &fonts[fi];
            PxCode code = sqlite3_column_int64(st, 1);
            int x = sqlite3_column_int(st, 2), y = sqlite3_column_int(st, 3), adv = sqlite3_column_int(st, 4);
            bool ink = sqlite3_column_type(st, 5) != SQLITE_NULL;
            uint64_t id = 0;
            if (ink) {
                id = id_of((const uint8_t *)sqlite3_column_blob(st, 5));
                if (f->nset == setcap[fi]) { setcap[fi] = setcap[fi] ? setcap[fi] * 2 : 64; f->set = (uint64_t *)realloc(f->set, sizeof(uint64_t) * (size_t)setcap[fi]); }
                f->set[f->nset++] = id;
                int w = sqlite3_column_int(st, 6), h = sqlite3_column_int(st, 7);
                if (y + h > topv[fi]) topv[fi] = y + h;
                if (-y > botv[fi]) botv[fi] = -y;
                (void)w;
            }
            if (code >= PX_CODE_UNICODE_END || px_code_variant(code)) continue;   /* code points of variant 0 only */
            int32_t cp = px_code_codepoint(code);
            advsum[fi] += adv; advn[fi]++;
            if (cp == 0x20) f->spaceAdv = adv;
            int sl = slot_of(cp);
            if (sl < 0) continue;
            SimGlyph g;
            memset(&g, 0, sizeof g);
            g.id = id; g.x = x; g.y = y;
            if (ink) {
                int w = sqlite3_column_int(st, 6), h = sqlite3_column_int(st, 7);
                const char *px = (const char *)sqlite3_column_blob(st, 8);
                if (px && sqlite3_column_bytes(st, 8) == w * h) {
                    g.w = w; g.h = h;
                    g.rows = (uint64_t *)calloc((size_t)h * 2, sizeof(uint64_t));
                    for (int r = 0; r < h; r++)
                        for (int c = 0; c < w && c < 128; c++)
                            if (px[(size_t)r * (size_t)w + (size_t)c] != '.') {
                                g.rows[2 * r + (c >> 6)] |= 1ull << (c & 63);
                                g.ink++;
                            }
                } else g.id = 0;
            }
            f->refs = (SimGlyph *)realloc(f->refs, sizeof(SimGlyph) * (size_t)(f->nrefs + 1));
            f->refs[f->nrefs] = g;
            f->slot[sl] = (int16_t)f->nrefs;
            f->nrefs++;
            f->nref++;
        }
        sqlite3_finalize(st);
    }
    for (int i = 0; i < nmeta; i++) {
        SimFont *f = &fonts[i];
        if (f->nset > 1) {
            qsort(f->set, (size_t)f->nset, sizeof(uint64_t), cmp_u64);
            int o = 0;
            for (int k = 0; k < f->nset; k++) if (o == 0 || f->set[o - 1] != f->set[k]) f->set[o++] = f->set[k];
            f->nset = o;
        }
        if (f->height < 0) f->height = topv[i] + botv[i];
        f->meanAdv = advn[i] ? advsum[i] / (double)advn[i] : 0;
    }
    free(meta); free(setcap); free(topv); free(botv); free(advn); free(advsum);
    if (!ok) { for (int i = 0; i < nmeta; i++) simfont_free(&fonts[i]); free(fonts); return false; }
    *outf = fonts;
    *outn = nmeta;
    return true;
}

static bool read_typefaces(sqlite3 *db, StoreSim *m, char err[PX_ERR_LEN]) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT pxtf, pxfm FROM pxtf_variant ORDER BY pxtf, pxfm", -1, &st, NULL) != SQLITE_OK) return fail(err, "sqlite: %s", sqlite3_errmsg(db));
    tf_free_all(m);
    int cap = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const uint8_t *th = (const uint8_t *)sqlite3_column_blob(st, 0), *fh = (const uint8_t *)sqlite3_column_blob(st, 1);
        if (m->ntf == 0 || memcmp(m->tf[m->ntf - 1].hash, th, 16) != 0) {
            if (m->ntf == cap) { cap = cap ? cap * 2 : 1024; m->tf = (SimTf *)realloc(m->tf, sizeof(SimTf) * (size_t)cap); }
            SimTf *t = &m->tf[m->ntf++];
            memcpy(t->hash, th, 16);
            t->fonts = NULL; t->nfonts = 0;
        }
        SimTf *t = &m->tf[m->ntf - 1];
        int lo = 0, hi = m->nf;
        while (lo < hi) { int mid = (lo + hi) / 2; if (memcmp(m->f[mid].hash, fh, 16) < 0) lo = mid + 1; else hi = mid; }
        if (lo >= m->nf || memcmp(m->f[lo].hash, fh, 16) != 0) continue;
        bool dup = false;
        for (int k = 0; k < t->nfonts; k++) if (t->fonts[k] == lo) dup = true;
        if (dup) continue;
        t->fonts = (int *)realloc(t->fonts, sizeof(int) * (size_t)(t->nfonts + 1));
        t->fonts[t->nfonts++] = lo;
    }
    sqlite3_finalize(st);
    return true;
}

bool store_similarity_prepare(Store *s, double *seconds, char err[PX_ERR_LEN]) {
    EnterCriticalSection(&s->simCs);
    if (s->sim && s->sim->ready) { LeaveCriticalSection(&s->simCs); if (seconds) *seconds = 0; return true; }
    double t0 = store_now();
    StoreConn *c = store_conn_open(s);
    if (!c) { LeaveCriticalSection(&s->simCs); return fail(err, "cannot open a connection"); }
    StoreSim *m = (StoreSim *)calloc(1, sizeof *m);
    AcquireSRWLockShared(&s->lock);
    bool ok = read_fonts(c->db, NULL, &m->f, &m->nf, err) && read_typefaces(c->db, m, err);
    ReleaseSRWLockShared(&s->lock);
    store_conn_close(c);
    if (!ok) { sim_free(m); LeaveCriticalSection(&s->simCs); return false; }
    m->capf = m->nf;
    m->ready = true;
    sim_free(s->sim);
    s->sim = m;
    if (seconds) *seconds = store_now() - t0;
    LeaveCriticalSection(&s->simCs);
    return true;
}

void store_sim_font_added(Store *s, const char *hex) {
    EnterCriticalSection(&s->simCs);
    StoreSim *m = s->sim;
    if (!m || !m->ready) { LeaveCriticalSection(&s->simCs); return; }
    uint8_t fh[16];
    char err[PX_ERR_LEN];
    if (store_hex_to_blob(hex, fh)) {
        bool have = false;
        for (int i = 0; i < m->nf; i++) if (memcmp(m->f[i].hash, fh, 16) == 0) have = true;
        StoreConn *c = store_conn_open(s);
        if (c) {
            AcquireSRWLockShared(&s->lock);
            SimFont *nf = NULL;
            int nn = 0;
            if (!have && read_fonts(c->db, fh, &nf, &nn, err) && nn == 1) {
                if (m->nf == m->capf) { m->capf = m->capf ? m->capf * 2 : 16; m->f = (SimFont *)realloc(m->f, sizeof(SimFont) * (size_t)m->capf); }
                int lo = 0;
                while (lo < m->nf && memcmp(m->f[lo].hash, fh, 16) < 0) lo++;
                memmove(&m->f[lo + 1], &m->f[lo], sizeof(SimFont) * (size_t)(m->nf - lo));
                m->f[lo] = nf[0];
                m->nf++;
            }
            free(nf);
            read_typefaces(c->db, m, err);    /* a new typeface or variant: rebuild the small mapping */
            ReleaseSRWLockShared(&s->lock);
            store_conn_close(c);
        }
    }
    LeaveCriticalSection(&s->simCs);
}

/* fonts were deleted from the store (after the deletion): drop them from the cache and rebuild the typeface mapping */
void store_sim_fonts_removed(Store *s, const uint8_t (*hashes)[16], int n) {
    EnterCriticalSection(&s->simCs);
    StoreSim *m = s->sim;
    if (m && m->ready) {
        for (int k = 0; k < n; k++) {
            for (int i = 0; i < m->nf; i++) {
                if (memcmp(m->f[i].hash, hashes[k], 16) != 0) continue;
                simfont_free(&m->f[i]);
                memmove(&m->f[i], &m->f[i + 1], sizeof(SimFont) * (size_t)(m->nf - i - 1));
                m->nf--;
                break;
            }
        }
        StoreConn *c = store_conn_open(s);
        if (c) {
            char err[PX_ERR_LEN];
            AcquireSRWLockShared(&s->lock);
            read_typefaces(c->db, m, err);
            ReleaseSRWLockShared(&s->lock);
            store_conn_close(c);
        }
    }
    LeaveCriticalSection(&s->simCs);
}

/* --- the score */

static int popc(uint64_t v) { return __builtin_popcountll(v); }

static double iou(const SimGlyph *a, const SimGlyph *b) {
    if (a->ink == 0 && b->ink == 0) return 1.0;
    if (a->ink == 0 || b->ink == 0) return 0.0;
    int y0 = a->y > b->y ? a->y : b->y;
    int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    int inter = 0;
    int x0 = a->x < b->x ? a->x : b->x;
    int sa = a->x - x0, sb = b->x - x0;
    for (int yy = y0; yy < y1; yy++) {
        const uint64_t *ra = a->rows + 2 * (a->y + a->h - 1 - yy), *rb = b->rows + 2 * (b->y + b->h - 1 - yy);
        unsigned __int128 ma = ((unsigned __int128)ra[1] << 64 | ra[0]) << sa;
        unsigned __int128 mb = ((unsigned __int128)rb[1] << 64 | rb[0]) << sb;
        unsigned __int128 m = ma & mb;
        inter += popc((uint64_t)m) + popc((uint64_t)(m >> 64));
    }
    int uni = a->ink + b->ink - inter;
    return uni > 0 ? (double)inter / (double)uni : 1.0;
}

static void score(const SimFont *a, const SimFont *b, StoreSimilar *r) {
    double sum = 0;
    int n = 0;
    for (int sl = 0; sl < NSLOT; sl++) {
        if (a->slot[sl] < 0 || b->slot[sl] < 0) continue;
        const SimGlyph *ga = &a->refs[a->slot[sl]], *gb = &b->refs[b->slot[sl]];
        n++;
        if (ga->id == gb->id && ga->x == gb->x && ga->y == gb->y) sum += 1.0;     /* the same bitmap at the same place (two empties too) */
        else sum += iou(ga, gb);
    }
    double shape = n > 0 ? sum / n : 0;
    /* shared glyphs: the Jaccard index of the two sets */
    int i = 0, j = 0, inter = 0;
    while (i < a->nset && j < b->nset) {
        if (a->set[i] < b->set[j]) i++;
        else if (a->set[i] > b->set[j]) j++;
        else { inter++; i++; j++; }
    }
    int uni = a->nset + b->nset - inter;
    double shared = uni > 0 ? (double)inter / (double)uni : 0;
    /* metrics: heights, advance (of the space when both have one, else the mean), monospacing */
    double hmax = a->height > b->height ? a->height : b->height;
    if (hmax < 1) hmax = 1;
    double hs = 1.0 - fabs((double)(a->height - b->height)) / hmax;
    double xa, xb;
    if (a->spaceAdv >= 0 && b->spaceAdv >= 0) { xa = a->spaceAdv; xb = b->spaceAdv; } else { xa = a->meanAdv; xb = b->meanAdv; }
    double xm = xa > xb ? xa : xb;
    if (xm < 1) xm = 1;
    double as = 1.0 - fabs(xa - xb) / xm;
    double ms = a->mono == b->mono ? 1.0 : 0.0;
    double metrics = (hs + as + ms) / 3.0;
    r->sharedRef = n;
    r->shapeUsed = n >= MIN_SHARED_REF;
    r->shape = r->shapeUsed ? 100.0 * shape : -1.0;
    r->shared = 100.0 * shared;
    r->metrics = 100.0 * metrics;
    if (r->shapeUsed) r->score = 100.0 * (W_SHAPE * shape + W_SHARED * shared + W_METRICS * metrics);
    else r->score = 100.0 * (W_SHARED * shared + W_METRICS * metrics) / (W_SHARED + W_METRICS);
}

static int find_font(const StoreSim *m, const uint8_t h[16]) {
    int lo = 0, hi = m->nf;
    while (lo < hi) { int mid = (lo + hi) / 2; if (memcmp(m->f[mid].hash, h, 16) < 0) lo = mid + 1; else hi = mid; }
    return (lo < m->nf && memcmp(m->f[lo].hash, h, 16) == 0) ? lo : -1;
}

static int cmp_sim(const void *a, const void *b) {
    const StoreSimilar *x = (const StoreSimilar *)a, *y = (const StoreSimilar *)b;
    if (x->score != y->score) return x->score > y->score ? -1 : 1;
    return strcmp(x->hash, y->hash);
}

bool store_font_similarity(Store *s, const char *pa, const char *pb, StoreSimilar *out, char err[PX_ERR_LEN]) {
    if (!store_similarity_prepare(s, NULL, err)) return false;
    uint8_t ha[16], hb[16];
    if (!store_hex_to_blob(pa, ha) || !store_hex_to_blob(pb, hb)) return fail(err, "not a hash");
    EnterCriticalSection(&s->simCs);
    StoreSim *m = s->sim;
    int ia = find_font(m, ha), ib = find_font(m, hb);
    bool ok = ia >= 0 && ib >= 0;
    if (ok) {
        memset(out, 0, sizeof *out);
        score(&m->f[ia], &m->f[ib], out);
        store_blob_to_hex(hb, out->hash);
    } else fail(err, "font not in the store");
    LeaveCriticalSection(&s->simCs);
    return ok;
}

bool store_similar_fonts(Store *s, const char *hex, int n, StoreSimilar *out, int *count, char err[PX_ERR_LEN]) {
    if (!store_similarity_prepare(s, NULL, err)) return false;
    uint8_t h[16];
    if (!store_hex_to_blob(hex, h)) return fail(err, "'%s' is not a hash", hex);
    EnterCriticalSection(&s->simCs);
    StoreSim *m = s->sim;
    int ia = find_font(m, h);
    if (ia < 0) { LeaveCriticalSection(&s->simCs); return fail(err, "font not in the store"); }
    StoreSimilar *all = (StoreSimilar *)malloc(sizeof(StoreSimilar) * (size_t)(m->nf ? m->nf : 1));
    int na = 0;
    for (int i = 0; i < m->nf; i++) {
        if (i == ia) continue;
        StoreSimilar *r = &all[na++];
        memset(r, 0, sizeof *r);
        score(&m->f[ia], &m->f[i], r);
        store_blob_to_hex(m->f[i].hash, r->hash);
    }
    LeaveCriticalSection(&s->simCs);
    qsort(all, (size_t)na, sizeof(StoreSimilar), cmp_sim);
    int k = na < n ? na : n;
    memcpy(out, all, sizeof(StoreSimilar) * (size_t)k);
    *count = k;
    free(all);
    return true;
}

bool store_similar_typefaces(Store *s, const char *hex, int n, StoreSimilar *out, int *count, char err[PX_ERR_LEN]) {
    if (!store_similarity_prepare(s, NULL, err)) return false;
    uint8_t h[16];
    if (!store_hex_to_blob(hex, h)) return fail(err, "'%s' is not a hash", hex);
    EnterCriticalSection(&s->simCs);
    StoreSim *m = s->sim;
    int qi = -1;
    for (int i = 0; i < m->ntf; i++) if (memcmp(m->tf[i].hash, h, 16) == 0) qi = i;
    if (qi < 0) { LeaveCriticalSection(&s->simCs); return fail(err, "typeface not in the store"); }
    const SimTf *q = &m->tf[qi];
    StoreSimilar *all = (StoreSimilar *)calloc((size_t)(m->ntf ? m->ntf : 1), sizeof(StoreSimilar));
    int na = 0;
    if (q->nfonts > 0) {
        /* score of each query font against every font */
        StoreSimilar *sc = (StoreSimilar *)malloc(sizeof(StoreSimilar) * (size_t)q->nfonts * (size_t)(m->nf ? m->nf : 1));
        for (int k = 0; k < q->nfonts; k++)
            for (int i = 0; i < m->nf; i++) score(&m->f[q->fonts[k]], &m->f[i], &sc[(size_t)k * (size_t)m->nf + (size_t)i]);
        for (int ti = 0; ti < m->ntf; ti++) {
            if (ti == qi || m->tf[ti].nfonts == 0) continue;
            StoreSimilar *r = &all[na++];
            memset(r, 0, sizeof *r);
            store_blob_to_hex(m->tf[ti].hash, r->hash);
            r->nFonts = m->tf[ti].nfonts;
            r->shapeUsed = true;
            for (int k = 0; k < q->nfonts; k++) {
                const StoreSimilar *best = NULL;
                for (int j = 0; j < m->tf[ti].nfonts; j++) {
                    const StoreSimilar *c = &sc[(size_t)k * (size_t)m->nf + (size_t)m->tf[ti].fonts[j]];
                    if (!best || c->score > best->score) best = c;
                }
                r->score += best->score; r->shared += best->shared; r->metrics += best->metrics;
                r->shape += best->shapeUsed ? best->shape : 0;
                if (!best->shapeUsed) r->shapeUsed = false;
                if (best->sharedRef > r->sharedRef) r->sharedRef = best->sharedRef;
            }
            r->score /= q->nfonts; r->shared /= q->nfonts; r->metrics /= q->nfonts; r->shape /= q->nfonts;
            if (!r->shapeUsed) r->shape = -1;
        }
        free(sc);
    }
    LeaveCriticalSection(&s->simCs);
    qsort(all, (size_t)na, sizeof(StoreSimilar), cmp_sim);
    int k = na < n ? na : n;
    memcpy(out, all, sizeof(StoreSimilar) * (size_t)k);
    *count = k;
    free(all);
    return true;
}
