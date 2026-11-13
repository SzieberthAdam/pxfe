/* insight.c - "Glyph usage", "Similar fonts" and "Similar typefaces" sections of the right panel (see insight.h).
 *
 * One worker thread with its own StoreConn does every query.  The UI thread only writes the REQUEST (latest wins:
 * generation counters; a job that finished for an older request is thrown away) and swaps in the finished RESULTS
 * under a mutex; results are plain data (masks of bitmaps), the textures are made on the UI thread when drawn. */
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>

#include "insight.h"
#include "theme.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INS_MAXG 256            /* glyph hashes of one request */
#define INS_SIM_N 20            /* rows of a similarity list */
#define INS_USAGE_CAP 8         /* usage rows shown before "Show all" */
#define INS_CODE_LINES 6        /* lines of codes of a usage row */
#define INS_SIM_ROW 64.0f
#define INS_SAMPLE_H 30         /* height of the sample band of a similarity row */
#define INS_SAMPLE_MAXW 230     /* the sample is cut after this many pixels (1x) */

/* ------------------------------------------------------------------ results */

typedef struct UsageRow {
    char pxfm[33], pxtf[33];
    int variant;
    char label[96];
    char codes[640];            /* every code of the row: "U+0041, #3, U+0061" (or, for fonts holding all glyphs, "1,234 glyphs") */
    int ncodes;
    int32_t code0;
    int off;                    /* first of its ncodes codes in UsageRes.codes */
} UsageRow;

typedef struct UsageRes {
    unsigned gen;
    int nGlyphs;
    char first[33];
    bool failed;
    int w, h;                   /* of the single glyph */
    unsigned char *mask;        /* until the texture is made */
    Texture2D tex;
    int nFonts, nTypefaces;
    int nVariants;              /* typeface variants (pxtf rows) that name a font holding the bitmap */
    int useCode, nUse;          /* nUse: fonts that hold the bitmap under the code useCode (cached; useCode -2 = not computed) */
    UsageRow *rows; int nRows;
    int best;                   /* row with the most codes, -1 none */
    int32_t *codes;             /* the codes of every row, grouped (single glyph) */
} UsageRes;

typedef struct SimRow {
    char hash[33];              /* pxfm or pxtf */
    char openPxfm[33];          /* font the sample was made from */
    char name[112];
    double score, shape, shared, metrics;
    bool shapeUsed;
    int sharedRef, nFonts;
    unsigned char *mask; int mw, mh;
    Texture2D tex;
    bool hasSample;
} SimRow;

typedef struct SimRes {
    unsigned gen;
    bool failed;
    SimRow *rows; int n;
} SimRes;

/* ------------------------------------------------------------------ the object */

struct Insight {
    Store *store;
    HANDLE th;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    bool quit;
    /* request (written by the UI thread under cs) */
    char gl[INS_MAXG][33]; int nGl; unsigned usageReq, usageDone;
    char pxfm[33], pxtf[33]; unsigned fontReq, fontsDone, tfDone;
    bool wantFonts, wantTf;
    bool preparing, simReady;
    UsageRes *pendUsage; SimRes *pendFonts, *pendTf;
    /* UI side */
    UsageRes *usage; SimRes *fonts, *tfs;
    bool open[3];
    bool showAll;
    Rectangle vis; bool visSet;
    char tip[200]; Vector2 tipPos;
    int32_t prefer;             /* a click on a row selects this code in the font when the row has it */
};

/* ------------------------------------------------------------------ small helpers */

static void code_text(int32_t c, char *out, size_t cap)
{
    if (px_code_is_uncoded(c)) snprintf(out, cap, "#%d", (int)(c - PX_CODE_UNCODED_FIRST));
    else if (c == PX_CODE_DEFAULT) snprintf(out, cap, "default");
    else snprintf(out, cap, "U+%04X", (unsigned)c);
}

static void make_label(const char *family, const char *style, char *out, size_t cap)
{
    if (family && family[0]) snprintf(out, cap, "%s%s%s", family, style && style[0] ? " " : "", style ? style : "");
    else if (style && style[0]) snprintf(out, cap, "%s", style);
    else out[0] = 0;   /* the view shows the hash instead */
}

static int cmp_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* ------------------------------------------------------------------ worker: usage */

typedef struct UTmp { char label[96]; char pxfm[33], pxtf[33]; int variant; int32_t code; } UTmp;

static int utmp_cmp(const void *pa, const void *pb)
{
    const UTmp *a = pa, *b = pb;
    int d = cmp_ci(a->label, b->label);
    if (d) return d;
    d = strcmp(a->pxfm, b->pxfm);
    if (d) return d;
    if (a->variant != b->variant) return a->variant - b->variant;
    d = strcmp(a->pxtf, b->pxtf);
    if (d) return d;
    return a->code < b->code ? -1 : a->code > b->code;
}

static void free_usage(UsageRes *r)
{
    if (!r) return;
    free(r->mask);
    if (r->tex.id) UnloadTexture(r->tex);
    free(r->rows); free(r->codes);
    free(r);
}

static void usage_mask(StoreConn *c, const char *hash, UsageRes *r)
{
    size_t len = 0;
    char *t = store_pxgl_text(c, hash, &len, NULL);
    if (!t) return;
    int w = 0, h = 0, cur = 0;
    for (size_t i = 0; i < len; i++) {
        if (t[i] == '\n') { if (!w) w = cur; cur = 0; h++; } else cur++;
    }
    if (w > 0 && h > 0) {
        unsigned char *m = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
        int row = 0, col = 0;
        for (size_t i = 0; i < len && m; i++) {
            if (t[i] == '\n') { row++; col = 0; continue; }
            if (row < h && col < w) m[row * w + col] = t[i] != '.';
            col++;
        }
        r->mask = m; r->w = w; r->h = h;
    }
    free(t);
}

static UsageRes *job_usage(StoreConn *c, char (*hashes)[33], int n, unsigned gen)
{
    UsageRes *r = (UsageRes *)calloc(1, sizeof *r);
    char err[PX_ERR_LEN];
    r->gen = gen; r->nGlyphs = n; r->best = -1; r->useCode = -2;
    if (n <= 0) return r;
    memcpy(r->first, hashes[0], 33);
    if (n == 1) {
        StoreUsageList ul = { 0 };
        if (!store_glyph_usage(c, hashes[0], &ul, &r->nFonts, &r->nTypefaces, err)) { r->failed = true; return r; }
        usage_mask(c, hashes[0], r);
        UTmp *tmp = (UTmp *)malloc((size_t)(ul.count + 1) * sizeof *tmp);
        for (int i = 0; i < ul.count; i++) {
            const StoreUsage *u = &ul.u[i];
            UTmp *t = &tmp[i];
            make_label(u->pxtf[0] ? u->family : "", u->pxtf[0] ? u->style : "", t->label, sizeof t->label);
            memcpy(t->pxfm, u->pxfm, 33); memcpy(t->pxtf, u->pxtf, 33);
            t->variant = u->variant; t->code = u->code;
        }
        qsort(tmp, (size_t)ul.count, sizeof *tmp, utmp_cmp);
        r->rows = (UsageRow *)calloc((size_t)ul.count + 1, sizeof *r->rows);
        int nr = 0, bestN = 0;
        for (int i = 0; i < ul.count;) {
            int j = i;
            while (j < ul.count && !strcmp(tmp[j].pxfm, tmp[i].pxfm) && tmp[j].variant == tmp[i].variant && !strcmp(tmp[j].pxtf, tmp[i].pxtf)) j++;
            UsageRow *row = &r->rows[nr++];
            memcpy(row->pxfm, tmp[i].pxfm, 33); memcpy(row->pxtf, tmp[i].pxtf, 33);
            row->variant = tmp[i].variant; row->code0 = tmp[i].code; row->ncodes = j - i; row->off = i;
            snprintf(row->label, sizeof row->label, "%s", tmp[i].label);
            size_t o = 0;
            row->codes[0] = 0;
            int shown = 0;
            for (int k = i; k < j; k++) {
                char ct[16];
                code_text(tmp[k].code, ct, sizeof ct);
                if (o + strlen(ct) + 14 >= sizeof row->codes) break;     /* the rest is counted, not listed */
                o += (size_t)snprintf(row->codes + o, sizeof row->codes - o, "%s%s", k > i ? ", " : "", ct);
                shown++;
            }
            if (shown < j - i) snprintf(row->codes + o, sizeof row->codes - o, " +%d more", j - i - shown);
            if (row->pxtf[0]) r->nVariants++;
            if (row->ncodes > bestN) { bestN = row->ncodes; r->best = nr - 1; }
            i = j;
        }
        r->nRows = nr;
        r->codes = (int32_t *)malloc((size_t)(ul.count + 1) * sizeof(int32_t));
        for (int i = 0; i < ul.count; i++) r->codes[i] = tmp[i].code;
        free(tmp);
        store_usage_list_free(&ul);
    } else {
        StoreEntryList el = { 0 };
        if (!store_fonts_with_glyphs(c, (const char (*)[33])hashes, n, &el, err)) { r->failed = true; return r; }
        r->nFonts = el.count;
        UTmp *tmp = (UTmp *)malloc((size_t)(el.count + 1) * sizeof *tmp);
        int *idx = (int *)malloc((size_t)(el.count + 1) * sizeof *idx);
        for (int i = 0; i < el.count; i++) {
            const StoreEntry *e = &el.e[i];
            make_label(e->family, e->style, tmp[i].label, sizeof tmp[i].label);
            memcpy(tmp[i].pxfm, e->pxfm, 33); memcpy(tmp[i].pxtf, e->pxtf, 33);
            tmp[i].variant = e->variant; tmp[i].code = e->glyphCount;
            idx[i] = i;
        }
        qsort(tmp, (size_t)el.count, sizeof *tmp, utmp_cmp);
        r->rows = (UsageRow *)calloc((size_t)el.count + 1, sizeof *r->rows);
        for (int i = 0; i < el.count; i++) {
            UsageRow *row = &r->rows[i];
            memcpy(row->pxfm, tmp[i].pxfm, 33); memcpy(row->pxtf, tmp[i].pxtf, 33);
            row->variant = tmp[i].variant; row->code0 = -1; row->ncodes = 1;
            snprintf(row->label, sizeof row->label, "%s", tmp[i].label);
            snprintf(row->codes, sizeof row->codes, "%d glyphs", (int)tmp[i].code);
        }
        r->nRows = el.count;
        free(idx); free(tmp);
        store_entry_list_free(&el);
    }
    return r;
}

/* ------------------------------------------------------------------ worker: names and samples */

typedef struct Names {
    StoreEntryList fonts, tfs;
    int *fi, *ti;               /* indices sorted by hash */
    bool have;
} Names;

static const StoreEntryList *g_sortList;
static int idx_cmp_font(const void *a, const void *b)
{
    const StoreEntry *x = &g_sortList->e[*(const int *)a], *y = &g_sortList->e[*(const int *)b];
    return strcmp(x->pxfm, y->pxfm);
}
static int idx_cmp_tf(const void *a, const void *b)
{
    const StoreEntry *x = &g_sortList->e[*(const int *)a], *y = &g_sortList->e[*(const int *)b];
    int d = strcmp(x->pxtf, y->pxtf);
    return d ? d : x->variant - y->variant;
}

static void names_free(Names *n)
{
    if (!n->have) return;
    store_entry_list_free(&n->fonts); store_entry_list_free(&n->tfs);
    free(n->fi); free(n->ti);
    memset(n, 0, sizeof *n);
}

static void names_load(Names *n, StoreConn *c)
{
    char err[PX_ERR_LEN];
    names_free(n);
    if (!store_list_fonts(c, &n->fonts, err)) memset(&n->fonts, 0, sizeof n->fonts);
    if (!store_list_typefaces(c, &n->tfs, err)) memset(&n->tfs, 0, sizeof n->tfs);
    n->fi = (int *)malloc((size_t)(n->fonts.count + 1) * sizeof(int));
    n->ti = (int *)malloc((size_t)(n->tfs.count + 1) * sizeof(int));
    for (int i = 0; i < n->fonts.count; i++) n->fi[i] = i;
    for (int i = 0; i < n->tfs.count; i++) n->ti[i] = i;
    g_sortList = &n->fonts; qsort(n->fi, (size_t)n->fonts.count, sizeof(int), idx_cmp_font);
    g_sortList = &n->tfs; qsort(n->ti, (size_t)n->tfs.count, sizeof(int), idx_cmp_tf);
    n->have = true;
}

static const StoreEntry *names_find(const StoreEntryList *l, const int *ix, const char *hash, bool typeface)
{
    int lo = 0, hi = l->count - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const StoreEntry *e = &l->e[ix[mid]];
        int d = strcmp(typeface ? e->pxtf : e->pxfm, hash);
        if (d < 0) lo = mid + 1; else { if (d == 0) found = mid; hi = mid - 1; }
    }
    return found < 0 ? NULL : &l->e[ix[found]];
}

/* A sample of the font: "Hamburg", else "AaBbCc", else its first coded glyphs, composed into one mask (1 = ink). */
static bool make_sample(StoreConn *c, const char *pxfm, SimRow *r)
{
    static const char *const kHam = "Hamburg", *const kAa = "AaBbCc";
    int32_t codes[16]; int nc = 0;
    for (const char *p = kHam; *p; p++) codes[nc++] = (unsigned char)*p;
    for (const char *p = kAa; *p; p++) codes[nc++] = (unsigned char)*p;
    PxFont f; char err[PX_ERR_LEN];
    px_font_init(&f);
    if (!store_load_font(c, pxfm, codes, nc, &f, err)) return false;
    int32_t pick[8]; int np = 0;
    int have = 0;
    for (const char *p = kHam; *p; p++) if (px_font_find(&f, (unsigned char)*p) >= 0) have++;
    if (have >= 6) { for (const char *p = kHam; *p; p++) pick[np++] = (unsigned char)*p; }
    else {
        have = 0;
        for (const char *p = kAa; *p; p++) if (px_font_find(&f, (unsigned char)*p) >= 0) have++;
        if (have >= 2) { for (const char *p = kAa; *p; p++) pick[np++] = (unsigned char)*p; }
        else {
            int32_t more[8]; int nm = 0;
            for (int i = 0; i < f.count && nm < 6; i++)
                if (px_code_is_codepoint(f.glyphs[i].code) && f.glyphs[i].code > 0x20 && f.glyphs[i].code != 0xA0) more[nm++] = f.glyphs[i].code;
            if (nm) { store_load_glyph_bitmaps(c, &f, more, nm, err); for (int i = 0; i < nm; i++) pick[np++] = more[i]; }
        }
    }
    /* bounds */
    int pen = 0, minX = 1 << 30, maxX = -(1 << 30), minY = 1 << 30, maxY = -(1 << 30);
    int used = 0;
    int px0[8], pgi[8];
    for (int i = 0; i < np; i++) {
        int gi = px_font_find(&f, pick[i]);
        if (gi < 0) continue;
        const PxGlyph *g = &f.glyphs[gi];
        if (pen > INS_SAMPLE_MAXW) break;
        if (g->w > 0 && g->h > 0 && g->pix) {
            if (pen + g->x < minX) minX = pen + g->x;
            if (pen + g->x + g->w > maxX) maxX = pen + g->x + g->w;
            if (g->y < minY) minY = g->y;
            if (g->y + g->h > maxY) maxY = g->y + g->h;
            px0[used] = pen; pgi[used] = gi; used++;
        }
        pen += g->advance > 0 ? g->advance : g->w + 1;
    }
    bool ok = false;
    if (used > 0 && maxX > minX && maxY > minY && maxX - minX <= 4096 && maxY - minY <= 512) {
        int w = maxX - minX, h = maxY - minY;
        unsigned char *m = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
        for (int i = 0; i < used; i++) {
            const PxGlyph *g = &f.glyphs[pgi[i]];
            int top = maxY - (g->y + g->h);
            for (int ry = 0; ry < g->h; ry++)
                for (int rx = 0; rx < g->w; rx++)
                    if (g->pix[ry * g->w + rx] != '.') m[(top + ry) * w + (px0[i] + g->x - minX + rx)] = 1;
        }
        r->mask = m; r->mw = w; r->mh = h; r->hasSample = true;
        ok = true;
    }
    px_font_free(&f);
    return ok;
}

static void free_sim(SimRes *r)
{
    if (!r) return;
    for (int i = 0; i < r->n; i++) { free(r->rows[i].mask); if (r->rows[i].tex.id) UnloadTexture(r->rows[i].tex); }
    free(r->rows);
    free(r);
}

static SimRes *job_similar(Store *st, StoreConn *c, Names *names, const char *hash, bool typefaces, unsigned gen)
{
    SimRes *r = (SimRes *)calloc(1, sizeof *r);
    StoreSimilar *sim = (StoreSimilar *)calloc(INS_SIM_N, sizeof *sim);
    char err[PX_ERR_LEN];
    int cnt = 0;
    r->gen = gen;
    bool ok = typefaces ? store_similar_typefaces(st, hash, INS_SIM_N, sim, &cnt, err)
                        : store_similar_fonts(st, hash, INS_SIM_N, sim, &cnt, err);
    if (!ok) { r->failed = true; free(sim); return r; }
    r->rows = (SimRow *)calloc((size_t)cnt + 1, sizeof *r->rows);
    r->n = cnt;
    for (int pass = 0; pass < 2; pass++) {
        bool miss = false;
        for (int i = 0; i < cnt; i++) {
            SimRow *row = &r->rows[i];
            const StoreEntry *e = names->have ? names_find(typefaces ? &names->tfs : &names->fonts, typefaces ? names->ti : names->fi, sim[i].hash, typefaces) : NULL;
            if (!e && pass == 0) { miss = true; continue; }
            if (e) make_label(e->family, e->style, row->name, sizeof row->name);
            else row->name[0] = 0;
            if (typefaces && e && e->family[0]) snprintf(row->name, sizeof row->name, "%s", e->family);
            if (e) memcpy(row->openPxfm, e->pxfm, 33);
            else if (!typefaces) memcpy(row->openPxfm, sim[i].hash, 33);
        }
        if (pass == 0 && miss) names_load(names, c); else break;
    }
    for (int i = 0; i < cnt; i++) {
        SimRow *row = &r->rows[i];
        memcpy(row->hash, sim[i].hash, 33);
        row->score = sim[i].score; row->shape = sim[i].shape; row->shared = sim[i].shared; row->metrics = sim[i].metrics;
        row->shapeUsed = sim[i].shapeUsed; row->sharedRef = sim[i].sharedRef; row->nFonts = sim[i].nFonts;
        if (row->openPxfm[0]) make_sample(c, row->openPxfm, row);
    }
    free(sim);
    return r;
}

/* ------------------------------------------------------------------ the worker thread */

static DWORD WINAPI worker_main(LPVOID p)
{
    Insight *in = (Insight *)p;
    StoreConn *c = store_conn_open(in->store);
    Names names = { 0 };
    bool prepared = false;
    for (;;) {
        int job = 0;
        unsigned gen = 0;
        char hashes[INS_MAXG][33];
        int n = 0;
        char pxfm[33] = "", pxtf[33] = "";
        EnterCriticalSection(&in->cs);
        for (;;) {
            if (in->quit) { LeaveCriticalSection(&in->cs); goto done; }
            if (in->usageReq != in->usageDone) { job = 1; gen = in->usageReq; n = in->nGl; memcpy(hashes, in->gl, (size_t)n * 33); break; }
            if (in->wantFonts && in->fontReq != in->fontsDone) { job = 2; gen = in->fontReq; memcpy(pxfm, in->pxfm, 33); break; }
            if (in->wantTf && in->fontReq != in->tfDone) { job = 3; gen = in->fontReq; memcpy(pxtf, in->pxtf, 33); break; }
            SleepConditionVariableCS(&in->cv, &in->cs, INFINITE);
        }
        LeaveCriticalSection(&in->cs);

        if (job == 1) {
            UsageRes *r = job_usage(c, hashes, n, gen);
            EnterCriticalSection(&in->cs);
            UsageRes *old = in->pendUsage;
            in->pendUsage = r;
            in->usageDone = gen;
            LeaveCriticalSection(&in->cs);
            if (old) { free(old->mask); free(old->rows); free(old->codes); free(old); }   /* never had a texture: the UI thread makes those */
        } else {
            if (!prepared) {
                EnterCriticalSection(&in->cs); in->preparing = true; LeaveCriticalSection(&in->cs);
                char err[PX_ERR_LEN]; double secs = 0;
                store_similarity_prepare(in->store, &secs, err);
                prepared = true;
                EnterCriticalSection(&in->cs); in->preparing = false; in->simReady = true; LeaveCriticalSection(&in->cs);
            }
            if (!names.have) names_load(&names, c);
            bool tf = job == 3;
            SimRes *r = job_similar(in->store, c, &names, tf ? pxtf : pxfm, tf, gen);
            EnterCriticalSection(&in->cs);
            SimRes **slot = tf ? &in->pendTf : &in->pendFonts;
            SimRes *old = *slot;
            *slot = r;
            if (tf) in->tfDone = gen; else in->fontsDone = gen;
            LeaveCriticalSection(&in->cs);
            if (old) { for (int i = 0; i < old->n; i++) free(old->rows[i].mask); free(old->rows); free(old); }
        }
    }
done:
    names_free(&names);
    store_conn_close(c);
    return 0;
}

/* ------------------------------------------------------------------ lifecycle and requests */

Insight *Insight_Create(Store *store)
{
    Insight *in = (Insight *)calloc(1, sizeof *in);
    if (!in) return NULL;
    in->store = store;
    in->open[0] = in->open[1] = in->open[2] = true;
    in->prefer = -1;
    in->wantFonts = in->wantTf = true;
    InitializeCriticalSection(&in->cs);
    InitializeConditionVariable(&in->cv);
    in->th = CreateThread(NULL, 0, worker_main, in, 0, NULL);
    return in;
}

void Insight_Destroy(Insight *in)
{
    if (!in) return;
    EnterCriticalSection(&in->cs);
    in->quit = true;
    WakeAllConditionVariable(&in->cv);
    LeaveCriticalSection(&in->cs);
    if (in->th) { WaitForSingleObject(in->th, INFINITE); CloseHandle(in->th); }
    free_usage(in->usage); free_usage(in->pendUsage);
    free_sim(in->fonts); free_sim(in->pendFonts); free_sim(in->tfs); free_sim(in->pendTf);
    DeleteCriticalSection(&in->cs);
    free(in);
}

void Insight_SetGlyphs(Insight *in, const char (*glyphHashes)[33], int n)
{
    if (!in) return;
    if (n < 0) n = 0;
    if (n > INS_MAXG) n = INS_MAXG;
    char tmp[INS_MAXG][33];
    for (int i = 0; i < n; i++) {
        int k = 0;
        for (; k < 32 && glyphHashes[i][k]; k++) tmp[i][k] = (char)tolower((unsigned char)glyphHashes[i][k]);
        tmp[i][k] = 0;
    }
    if (n == in->nGl && (n == 0 || memcmp(tmp, in->gl, (size_t)n * 33) == 0)) return;
    EnterCriticalSection(&in->cs);
    memcpy(in->gl, tmp, (size_t)n * 33);
    in->nGl = n;
    in->usageReq++;
    WakeAllConditionVariable(&in->cv);
    LeaveCriticalSection(&in->cs);
}

void Insight_SetFont(Insight *in, const char *pxfmHash, const char *pxtfHash)
{
    if (!in) return;
    char a[33] = "", b[33] = "";
    for (int k = 0; pxfmHash && k < 32 && pxfmHash[k]; k++) a[k] = (char)tolower((unsigned char)pxfmHash[k]);
    for (int k = 0; pxtfHash && k < 32 && pxtfHash[k]; k++) b[k] = (char)tolower((unsigned char)pxtfHash[k]);
    if (!strcmp(a, in->pxfm) && !strcmp(b, in->pxtf)) return;
    EnterCriticalSection(&in->cs);
    memcpy(in->pxfm, a, 33); memcpy(in->pxtf, b, 33);
    in->fontReq++;
    if (!a[0]) in->fontsDone = in->fontReq;
    if (!b[0]) in->tfDone = in->fontReq;
    WakeAllConditionVariable(&in->cv);
    LeaveCriticalSection(&in->cs);
}

void Insight_SetVisible(Insight *in, Rectangle visible) { if (in) { in->vis = visible; in->visSet = true; } }

/* UI thread: take the results the worker finished. */
static void pull(Insight *in)
{
    UsageRes *u; SimRes *f, *t;
    EnterCriticalSection(&in->cs);
    u = in->pendUsage; in->pendUsage = NULL;
    f = in->pendFonts; in->pendFonts = NULL;
    t = in->pendTf; in->pendTf = NULL;
    LeaveCriticalSection(&in->cs);
    if (u) { free_usage(in->usage); in->usage = u; }
    if (f) { free_sim(in->fonts); in->fonts = f; }
    if (t) { free_sim(in->tfs); in->tfs = t; }
}

static bool usage_loading(const Insight *in) { return in->usageReq != in->usageDone || !in->usage || in->usage->gen != in->usageReq; }
static bool fonts_loading(const Insight *in) { return in->pxfm[0] && (in->fontReq != in->fontsDone || !in->fonts || in->fonts->gen != in->fontReq); }
static bool tfs_loading(const Insight *in) { return in->pxtf[0] && (in->fontReq != in->tfDone || !in->tfs || in->tfs->gen != in->fontReq); }

int Insight_UsageFontCount(const Insight *in)
{
    if (!in) return 0;
    pull((Insight *)in);
    if (usage_loading(in)) return in->nGl == 0 && in->usageReq == in->usageDone ? 0 : -1;
    return in->usage->failed ? 0 : in->usage->nFonts;
}

int Insight_SimilarCount(const Insight *in)
{
    if (!in) return 0;
    pull((Insight *)in);
    if (!in->pxfm[0]) return 0;
    if (in->wantTf && !in->wantFonts) {      /* only the typeface list is shown (the Typefaces tab of the browser) */
        if (tfs_loading(in)) return -1;
        return in->tfs->failed ? 0 : in->tfs->n;
    }
    if (fonts_loading(in)) return -1;
    return in->fonts->failed ? 0 : in->fonts->n;
}

bool Insight_Busy(const Insight *in)
{
    if (!in) return false;
    pull((Insight *)in);
    bool b;
    EnterCriticalSection((CRITICAL_SECTION *)&in->cs);
    b = in->usageReq != in->usageDone || in->preparing ||
        (in->wantFonts && in->fontReq != in->fontsDone) || (in->wantTf && in->fontReq != in->tfDone);
    LeaveCriticalSection((CRITICAL_SECTION *)&in->cs);
    return b;
}

void Insight_SetPreferredCode(Insight *in, int32_t code) { if (in) in->prefer = code; }

static int cmp_pstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* Fonts that hold the single glyph under the preferred code (the number the finder shows for the bitmap); cached in the result. */
static int usage_code_fonts(Insight *in)
{
    UsageRes *u = in->usage;
    if (in->prefer < 0 || usage_loading(in) || u->failed || u->nGlyphs != 1 || !u->codes) return -1;
    if (u->useCode == in->prefer) return u->nUse;
    const char **p = (const char **)malloc(sizeof(char *) * (size_t)(u->nRows + 1));
    int n = 0;
    for (int i = 0; i < u->nRows; i++) {
        const UsageRow *r = &u->rows[i];
        for (int k = 0; k < r->ncodes; k++) if (u->codes[r->off + k] == in->prefer) { p[n++] = r->pxfm; break; }
    }
    qsort(p, (size_t)n, sizeof *p, cmp_pstr);
    int d = 0;
    for (int i = 0; i < n; i++) if (i == 0 || strcmp(p[i], p[i - 1])) d++;
    free(p);
    u->useCode = in->prefer; u->nUse = d;
    return d;
}

int Insight_UsageVariantCount(const Insight *in)
{
    if (!in) return -2;
    pull((Insight *)in);
    if (usage_loading(in)) return -1;
    return in->usage->failed ? 0 : in->usage->nVariants;
}

int Insight_UsageCodeFonts(const Insight *in)
{
    if (!in) return -2;
    pull((Insight *)in);
    return usage_code_fonts((Insight *)in);
}

/* The code a click on the row selects: the preferred one when the row has the glyph under it, else the first. */
static int32_t row_code(const Insight *in, const UsageRow *r)
{
    const UsageRes *u = in->usage;
    if (in->prefer >= 0 && u && u->codes && u->nGlyphs == 1)
        for (int k = 0; k < r->ncodes; k++) if (u->codes[r->off + k] == in->prefer) return in->prefer;
    return r->code0;
}

bool Insight_BestUsage(Insight *in, const char *glyphHash, InsightTarget *t)
{
    if (!in) return false;
    pull(in);
    if (usage_loading(in) || in->usage->failed || in->usage->nGlyphs != 1 || in->usage->best < 0) return false;
    if (glyphHash && strcmp(glyphHash, in->usage->first)) return false;
    int bi = in->usage->best;
    if (in->prefer >= 0) {   /* a font that has the glyph under the preferred code first, then the one with most codes */
        int bestScore = -1;
        for (int i = 0; i < in->usage->nRows; i++) {
            const UsageRow *q = &in->usage->rows[i];
            int sc = q->ncodes + (row_code(in, q) == in->prefer ? 1000000 : 0);
            if (sc > bestScore) { bestScore = sc; bi = i; }
        }
    }
    const UsageRow *r = &in->usage->rows[bi];
    memset(t, 0, sizeof *t);
    t->action = INSIGHT_OPEN_FONT;
    memcpy(t->pxfm, r->pxfm, 33); memcpy(t->pxtf, r->pxtf, 33);
    t->variant = r->variant; t->code = row_code(in, r);
    return true;
}

/* ------------------------------------------------------------------ drawing */

typedef struct Cx {
    Insight *in;
    Rectangle area, vis;
    bool *clicked;
    InsightTarget *target;
} Cx;

static bool cx_hover(const Cx *c, Rectangle r)
{
    if (Th_InputBlocked()) return false;
    Vector2 m = GetMousePosition();
    return CheckCollisionPointRec(m, c->vis) && CheckCollisionPointRec(m, r);
}

static bool cx_visible(const Cx *c, float y, float h) { return y + h >= c->vis.y && y <= c->vis.y + c->vis.height; }

static void cx_tip(Cx *c, const char *text)
{
    snprintf(c->in->tip, sizeof c->in->tip, "%s", text);
    c->in->tipPos = GetMousePosition();
}

/* Section header: the fold caption of Th_Section with an optional right-aligned count. Returns true when open. */
static bool sec_header(Cx *c, float *y, int idx, const char *label, const char *right, bool first)
{
    Insight *in = c->in;
    *y += first ? 4 : TH_GAP;
    float sy = *y;
    if (cx_visible(c, sy, TH_ROW_H) && Th_Section(label, c->area.x + 16, y, c->area.width - 32, in->open[idx] ? TH_SEC_OPEN : TH_SEC_CLOSED, right)) in->open[idx] = !in->open[idx];
    *y = sy + TH_ROW_H;
    return in->open[idx];
}

static void note(Cx *c, float *y, const char *text, bool spinner)
{
    if (cx_visible(c, *y, TH_ROW_H)) {
        float x = c->area.x + 16;
        if (spinner) { Th_Spinner((Vector2){ x + 6, *y + 12 }, 6, TH_TEXT_DIM); x += 20; }
        Th_TextClip(text, x, *y + 3, c->area.width - 32 - (x - c->area.x - 16), TH_TEXT_DIM, TH_F_BODY);
    }
    *y += TH_ROW_H;
}

static void ensure_tex(Texture2D *t, unsigned char **mask, int w, int h)
{
    if (!t->id && *mask) {
        *t = Th_MaskTexture(*mask, w, h, TH_TEXT);
        free(*mask);
        *mask = NULL;
    }
}

static void click_target(Cx *c, const UsageRow *r)
{
    if (!c->clicked || !c->target) return;
    memset(c->target, 0, sizeof *c->target);
    c->target->action = INSIGHT_OPEN_FONT;
    memcpy(c->target->pxfm, r->pxfm, 33); memcpy(c->target->pxtf, r->pxtf, 33);
    c->target->variant = r->variant; c->target->code = row_code(c->in, r);
    *c->clicked = true;
}

/* Breaks "U+0041, U+0042, #3" after the commas so that every line fits maxw (small font); at most INS_CODE_LINES lines,
 * the last one cut with an ellipsis when the codes do not fit.  Returns the number of lines (at least 1). */
static int wrap_codes(const char *text, float maxw, char lines[INS_CODE_LINES][160])
{
    int n = 0;
    lines[0][0] = 0;
    if (Th_MeasureRich(text, TH_F_SMALL) <= maxw) { snprintf(lines[0], 160, "%.159s", text); return 1; }
    const char *p = text;
    while (*p && n < INS_CODE_LINES) {
        char *ln = lines[n];
        ln[0] = 0;
        size_t len = 0;
        while (*p) {
            const char *e = strstr(p, ", ");
            size_t tl = e ? (size_t)(e - p) + 1 : strlen(p);          /* the token with its comma */
            char cand[160];
            if (len + tl + 2 >= sizeof cand) break;
            size_t cl = len;
            memcpy(cand, ln, len);
            if (len) cand[cl++] = ' ';
            memcpy(cand + cl, p, tl);
            cl += tl;
            cand[cl] = 0;
            if (len && Th_MeasureRich(cand, TH_F_SMALL) > maxw) break;
            memcpy(ln, cand, cl + 1);
            len = cl;
            p += tl;
            while (*p == ' ') p++;
        }
        if (!len) { snprintf(ln, 160, "%.159s", p); p += strlen(p); }      /* a single token wider than the row */
        n++;
    }
    if (*p && n > 0) {   /* more text than lines: end the last one with an ellipsis */
        char *ln = lines[n - 1];
        size_t l = strlen(ln);
        if (l + 4 < 160) memcpy(ln + l, "\xE2\x80\xA6", 4);
    }
    return n;
}

static void draw_usage(Cx *c, float *y, bool first)
{
    Insight *in = c->in;
    UsageRes *u = in->usage;
    bool loading = usage_loading(in);
    char right[48] = "";
    if (!loading && !u->failed && u->nGlyphs >= 1) snprintf(right, sizeof right, "%d font%s", u->nFonts, u->nFonts == 1 ? "" : "s");
    if (!sec_header(c, y, 0, "GLYPH USAGE", right, first)) return;
    float x = c->area.x, w = c->area.width, x0 = x + 16, x1 = x + w - 16;

    if (in->nGl == 0 && in->usageReq == in->usageDone) { note(c, y, "Select a glyph to see where it is used.", false); return; }
    if (loading) { note(c, y, "Searching the library\xE2\x80\xA6", true); return; }
    if (u->failed) { note(c, y, "The usage could not be read.", false); return; }

    if (u->nGlyphs == 1) {
        int useN = usage_code_fonts(in);
        float top = *y + 2;
        if (cx_visible(c, top, 56)) {
            Rectangle box = { x0, top, 56, 56 };
            DrawRectangleRec(box, TH_PANEL);
            ensure_tex(&u->tex, &u->mask, u->w, u->h);
            if (u->tex.id) {
                int z = (int)fminf(52.0f / (float)u->w, 52.0f / (float)u->h);
                if (z < 1) z = 1;
                float gw = (float)(u->w * z), gh = (float)(u->h * z);
                float gx = box.x + floorf((56 - gw) / 2), gy = box.y + floorf((56 - gh) / 2);
                if (gx < box.x) gx = box.x;
                if (gy < box.y) gy = box.y;
                Th_DrawTextureClipped(u->tex, gx, gy, z, box, WHITE);
            }
            Th_RectLines(box, TH_LINE);
            char t[96];
            Th_HashShort("Glyph", u->first, x0 + 66, top, 0, NULL);
            Rectangle hr = { x0 + 66, top, 90, 16 };
            if (cx_hover(c, hr)) cx_tip(c, u->first);
            snprintf(t, sizeof t, "%d \xC3\x97 %d px", u->w, u->h);
            Th_Text(t, x0 + 66, top + 16, TH_TEXT);
            snprintf(t, sizeof t, "In %d font%s \xC2\xB7 %d typeface variant%s", u->nFonts, u->nFonts == 1 ? "" : "s", u->nVariants, u->nVariants == 1 ? "" : "s");
            Th_TextClip(t, x0 + 66, top + 36, x1 - x0 - 66, TH_TEXT_DIM, TH_F_BODY);
            if (useN >= 0) {
                char cc[24];
                code_text(in->prefer, cc, sizeof cc);
                snprintf(t, sizeof t, "%d use it for %s", useN, cc);
                Th_TextClip(t, x0, top + 62, x1 - x0, TH_TEXT_DIM, TH_F_BODY);
            }
        }
        *y += useN >= 0 ? 92 : 64;
    } else {
        char t[96];
        snprintf(t, sizeof t, "Fonts holding all %d selected glyphs", u->nGlyphs);
        if (cx_visible(c, *y, TH_ROW_H)) {
            Th_TextClip(t, x0, *y + 3, x1 - x0, TH_TEXT, TH_F_BODY);
        }
        *y += TH_ROW_H;
    }

    if (u->nRows == 0) {
        note(c, y, u->nGlyphs == 1 ? "No font uses this glyph." : "No font holds all of them.", false);
        return;
    }
    int shown = in->showAll ? u->nRows : (u->nRows < INS_USAGE_CAP ? u->nRows : INS_USAGE_CAP);
    for (int i = 0; i < shown; i++) {
        const UsageRow *r = &u->rows[i];
        char lines[INS_CODE_LINES][160];
        int nl = wrap_codes(r->codes, x1 - x0, lines);
        Rectangle rr = { x + 8, *y, w - 16, 30.0f + 18.0f * (float)nl };
        if (cx_visible(c, rr.y, rr.height)) {
            char nm[32];
            snprintf(nm, sizeof nm, "usage_row_%d", i);
            Th_Reg(nm, (Rectangle){ rr.x, fmaxf(rr.y, c->vis.y), rr.width, fminf(rr.y + rr.height, c->vis.y + c->vis.height) - fmaxf(rr.y, c->vis.y) });
            bool hv = cx_hover(c, rr);
            Th_ListRow(rr, hv ? TH_LR_HOVER : 0);
            if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) click_target(c, r);
            float hw = Th_HashShort("Font", r->pxfm, x1, rr.y + 3, TH_HASH_RIGHT, NULL);
            Th_TextClip(r->label, x0, rr.y + 3, x1 - x0 - hw - 8, TH_TEXT, TH_F_BODY);
            for (int k = 0; k < nl; k++) Th_TextClip(lines[k], x0, rr.y + 26 + 18.0f * (float)k, x1 - x0, TH_TEXT_DIM, TH_F_SMALL | TH_F_RICH);
        }
        *y += rr.height;
    }
    if (u->nRows > INS_USAGE_CAP) {
        Rectangle rr = { x0, *y + 2, 0, TH_ROW_H };
        if (cx_visible(c, rr.y, rr.height)) {
            char t[48];
            snprintf(t, sizeof t, in->showAll ? "Show fewer" : "Show all %d", u->nRows);
            rr.width = Th_ButtonW(t);
            bool pb = Th_InputBlocked();
            Th_SetInputBlocked(pb || !CheckCollisionPointRec(GetMousePosition(), c->vis));   /* a scrolled-out button does not react */
            if (Th_Button(rr, t, 0, NULL)) in->showAll = !in->showAll;
            Th_SetInputBlocked(pb);
        }
        *y += TH_ROW_H + 4;
    }
}

static void draw_sim(Cx *c, float *y, bool typefaces, bool first)
{
    Insight *in = c->in;
    SimRes *s = typefaces ? in->tfs : in->fonts;
    const char *hash = typefaces ? in->pxtf : in->pxfm;
    bool loading = typefaces ? tfs_loading(in) : fonts_loading(in);
    const char *label = typefaces ? "SIMILAR TYPEFACES" : "SIMILAR FONTS";
    if (!sec_header(c, y, typefaces ? 2 : 1, label, NULL, first)) return;
    float x = c->area.x, w = c->area.width, x0 = x + 16, x1 = x + w - 16;
    if (!hash[0]) { note(c, y, typefaces ? "The font is not part of a typeface." : "Open a font to see similar ones.", false); return; }
    if (loading) {
        bool prep;
        EnterCriticalSection(&in->cs); prep = in->preparing || !in->simReady; LeaveCriticalSection(&in->cs);
        note(c, y, prep ? "Preparing similarity index\xE2\x80\xA6" : (typefaces ? "Finding similar typefaces\xE2\x80\xA6" : "Finding similar fonts\xE2\x80\xA6"), true);
        return;
    }
    if (s->failed) { note(c, y, "Similarity is not available for this entry.", false); return; }
    if (s->n == 0) { note(c, y, "Nothing similar found.", false); return; }

    const float colW = 52;
    for (int i = 0; i < s->n; i++) {
        SimRow *r = &s->rows[i];
        Rectangle rr = { x + 8, *y, w - 16, INS_SIM_ROW };
        if (cx_visible(c, rr.y, rr.height)) {
            char nm[32];
            snprintf(nm, sizeof nm, "similar_row_%d", i);
            Th_Reg(nm, (Rectangle){ rr.x, fmaxf(rr.y, c->vis.y), rr.width, fminf(rr.y + rr.height, c->vis.y + c->vis.height) - fmaxf(rr.y, c->vis.y) });
            bool hv = cx_hover(c, rr);
            Th_ListRow(rr, hv ? TH_LR_HOVER : 0);
            if (hv) {
                char tip[200];
                if (r->shapeUsed) snprintf(tip, sizeof tip, "shape %.0f \xC2\xB7 shared %.0f \xC2\xB7 metrics %.0f", r->shape, r->shared, r->metrics);
                else if (typefaces) snprintf(tip, sizeof tip, "shape not used for some font pairs \xC2\xB7 shared %.0f \xC2\xB7 metrics %.0f", r->shared, r->metrics);
                else snprintf(tip, sizeof tip, "shape not used (%d shared code points, 10 needed) \xC2\xB7 shared %.0f \xC2\xB7 metrics %.0f", r->sharedRef, r->shared, r->metrics);
                cx_tip(c, tip);
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && c->clicked && c->target) {
                    memset(c->target, 0, sizeof *c->target);
                    if (typefaces) { c->target->action = INSIGHT_OPEN_TYPEFACE; memcpy(c->target->pxtf, r->hash, 33); c->target->variant = 0; }
                    else { c->target->action = INSIGHT_OPEN_FONT; memcpy(c->target->pxfm, r->hash, 33); }
                    c->target->code = -1;
                    *c->clicked = true;
                }
            }
            /* score: number and a small bar */
            char sc[16];
            snprintf(sc, sizeof sc, "%.0f", r->score);
            Th_Text(sc, x0, rr.y + 3, TH_TEXT);
            Rectangle bar = { x0, rr.y + 28, colW - 12, 4 };
            DrawRectangleRec(bar, TH_LINE_SOFT);
            float fw = floorf(bar.width * (float)(r->score / 100.0));
            if (fw < 1 && r->score > 0) fw = 1;
            Color bc = r->shapeUsed ? TH_ACCENT : TH_HOVER_BORDER;
            DrawRectangle((int)bar.x, (int)bar.y, (int)fw, 4, bc);
            /* name, extras */
            float tx = x0 + colW;
            float right = x1;
            char ex[40] = "";
            if (typefaces && r->nFonts > 0) snprintf(ex, sizeof ex, "%d font%s", r->nFonts, r->nFonts == 1 ? "" : "s");
            else if (!r->shapeUsed) snprintf(ex, sizeof ex, "no shape");
            if (ex[0]) {
                float ew = Th_MeasureSmall(ex).x;
                Th_TextSmall(ex, x1 - ew, rr.y + 6, TH_TEXT_DIM);
                right = x1 - ew - 8;
            }
            if (r->name[0]) Th_TextClip(r->name, tx, rr.y + 3, right - tx, TH_TEXT, TH_F_BODY);
            else Th_HashShort(typefaces ? "Typeface" : "Font", r->hash, tx, rr.y + 3, 0, NULL);
            /* sample, pixel-perfect at an integer scale */
            Rectangle band = { tx, rr.y + 28, x1 - tx, INS_SAMPLE_H };
            if (r->hasSample) {
                ensure_tex(&r->tex, &r->mask, r->mw, r->mh);
                if (r->tex.id) {
                    int z = (int)fminf(band.width / (float)r->mw, (float)INS_SAMPLE_H / (float)r->mh);
                    if (z < 1) z = 1;
                    if (z > 2) z = 2;
                    float gh = (float)(r->mh * z);
                    float gy = band.y + (gh < band.height ? floorf((band.height - gh) / 2) : 0);
                    Th_DrawTextureClipped(r->tex, band.x, gy, z, band, WHITE);
                }
            } else {
                Th_TextSmall("no sample", band.x, band.y + 6, TH_TEXT_HINT);
            }
        }
        *y += INS_SIM_ROW;
    }
}

float Insight_Draw(Insight *in, Rectangle area, int sections, bool *clicked, InsightTarget *target)
{
    if (!in) return 0;
    pull(in);
    bool wf = (sections & INSIGHT_SECTION_SIMILAR_FONTS) != 0, wt = (sections & INSIGHT_SECTION_SIMILAR_TYPEFACES) != 0;
    if (wf != in->wantFonts || wt != in->wantTf) {
        EnterCriticalSection(&in->cs);
        in->wantFonts = wf; in->wantTf = wt;
        WakeAllConditionVariable(&in->cv);
        LeaveCriticalSection(&in->cs);
    }
    Cx c = { in, area, in->visSet ? in->vis : area, clicked, target };
    in->visSet = false;
    in->tip[0] = 0;
    float y = area.y;
    bool first = true;
    if (sections & INSIGHT_SECTION_USAGE) { draw_usage(&c, &y, first); first = false; }
    if (wf) { draw_sim(&c, &y, false, first); first = false; }
    if (wt) { draw_sim(&c, &y, true, first); first = false; }
    y += 8;
    if (in->tip[0]) {
        Vector2 m = Th_Measure(in->tip);
        float tw = m.x + 16, th = m.y + 10;
        float tx = in->tipPos.x + 14, ty = in->tipPos.y + 18;
        if (tx + tw > (float)GetScreenWidth() - 4) tx = (float)GetScreenWidth() - 4 - tw;
        if (ty + th > (float)GetScreenHeight() - 4) ty = in->tipPos.y - th - 6;
        Rectangle tr = { floorf(tx), floorf(ty), tw, th };
        DrawRectangleRounded(tr, 0.2f, 6, TH_TIP_BG);
        Th_Text(in->tip, tr.x + 8, tr.y + 5, TH_TEXT_ON);
    }
    return y - area.y;
}
