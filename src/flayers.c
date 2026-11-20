/* flayers.c - font layers: the document's list, the sources (outline.h, bmsrc.h), placement, drawing and the texture cache.
 * See flayers.h. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "flayers.h"

static int nextSerial = 1;

/* ------------------------------------------------------------------ the texture cache (LRU) */

typedef struct CacheEnt {
    int serial, key;
    PxCode code;
    Texture2D tex;        /* id 0: a blank glyph (nothing to draw), kept so that it is not rasterised again */
    int left, top, w, h;
    unsigned long used;
    bool live;
} CacheEnt;

static CacheEnt cache[FL_CACHE_N];
static unsigned long cacheTick;
static long cachePix;

static void ent_free(CacheEnt *c)
{
    if (!c->live) return;
    if (c->tex.id) UnloadTexture(c->tex);
    cachePix -= (long)c->w * c->h;
    memset(c, 0, sizeof *c);
}

int FlCache_Count(void)
{
    int n = 0;
    for (int i = 0; i < FL_CACHE_N; i++) n += cache[i].live;
    return n;
}

void FlCache_Clear(void) { for (int i = 0; i < FL_CACHE_N; i++) ent_free(&cache[i]); }

static void cache_drop(int serial) { for (int i = 0; i < FL_CACHE_N; i++) if (cache[i].live && cache[i].serial == serial) ent_free(&cache[i]); }

static CacheEnt *cache_find(int serial, PxCode code, int key)
{
    for (int i = 0; i < FL_CACHE_N; i++) {
        CacheEnt *c = &cache[i];
        if (c->live && c->serial == serial && c->code == code && c->key == key) { c->used = ++cacheTick; return c; }
    }
    return NULL;
}

/* a free slot, evicting the least recently used entries while the count or the pixel budget is exceeded */
static CacheEnt *cache_slot(long needPix)
{
    for (;;) {
        CacheEnt *oldest = NULL, *freeSlot = NULL;
        for (int i = 0; i < FL_CACHE_N; i++) {
            CacheEnt *c = &cache[i];
            if (!c->live) { if (!freeSlot) freeSlot = c; }
            else if (!oldest || c->used < oldest->used) oldest = c;
        }
        if (freeSlot && cachePix + needPix <= FL_CACHE_PIX) return freeSlot;
        if (!oldest) return freeSlot;
        ent_free(oldest);
    }
}

/* ------------------------------------------------------------------ layers and the document */

FlLayer *FlLayer_New(const FsLayer *init)
{
    FlLayer *l = (FlLayer *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->s = *init;
    l->s.id = 0;
    if (!(l->s.scale > 0)) l->s.scale = 1.0f;
    if (!(l->s.aspect > 0)) l->s.aspect = 1.0f;
    if (!(l->s.threshold > 0)) l->s.threshold = 0.5f;
    l->serial = nextSerial++;
    return l;
}

static void close_src(FlLayer *l)
{
    l->snap.valid = false;
    if (l->ol) { outline_close(l->ol); l->ol = NULL; }
    if (l->bm) { bm_close(l->bm); l->bm = NULL; }
    cache_drop(l->serial);
}

void FlLayer_Free(FlLayer *l)
{
    if (!l) return;
    close_src(l);
    free(l->corr);
    free(l);
}

FlLayer *FlLayer_Copy(const FlLayer *l)
{
    FlLayer *c = FlLayer_New(&l->s);
    if (!c) return NULL;
    c->origin = l->origin ? l->origin : l->serial;
    if (l->ncorr) {
        c->corr = (FlCorr *)malloc(sizeof(FlCorr) * (size_t)l->ncorr);
        if (!c->corr) { FlLayer_Free(c); return NULL; }
        memcpy(c->corr, l->corr, sizeof(FlCorr) * (size_t)l->ncorr);
        c->ncorr = c->capcorr = l->ncorr;
    }
    return c;
}

FlDoc *FlDoc_New(void) { return (FlDoc *)calloc(1, sizeof(FlDoc)); }

void FlDoc_Clear(FlDoc *d)
{
    for (int i = 0; i < d->n; i++) { FlLayer_Free(d->v[i]); d->v[i] = NULL; }
    d->n = 0;
}

void FlDoc_Free(FlDoc *d)
{
    if (!d) return;
    FlDoc_Clear(d);
    free(d);
}

bool FlDoc_Add(FlDoc *d, FlLayer *l, int pos)
{
    if (!l || d->n >= FL_MAX) return false;
    if (pos < 0 || pos > d->n) pos = d->n;
    memmove(&d->v[pos + 1], &d->v[pos], sizeof(FlLayer *) * (size_t)(d->n - pos));
    d->v[pos] = l;
    d->n++;
    return true;
}

bool FlDoc_Load(FlDoc *d, FSettings *fs, const char *hash)
{
    FlDoc_Clear(d);
    d->rev++;
    if (!fs || !hash || !hash[0]) return true;
    FsLayer all[FL_MAX];
    int n = fs_list_layers(fs, hash, all, FL_MAX);
    if (n < 0) return false;
    if (n > FL_MAX) n = FL_MAX;
    for (int i = 0; i < n; i++) {
        FlLayer *l = FlLayer_New(&all[i]);
        if (!l) return false;
        int nc = fs_list_corrections(fs, all[i].id, NULL, 0);
        PxCode *codes = nc > 0 ? (PxCode *)malloc(sizeof(PxCode) * (size_t)nc) : NULL;
        if (nc > 0 && !codes) { FlLayer_Free(l); return false; }
        if (nc > 0) { int got = fs_list_corrections(fs, all[i].id, codes, nc); if (got < nc) nc = got; }
        for (int k = 0; k < nc; k++) {
            FsCorrection c;
            if (!fs_get_correction(fs, all[i].id, codes[k], &c)) continue;
            FlCorr fc = {codes[k], c.dx, c.dy, c.dsize, c.hidden};
            FlLayer_SetCorr(l, codes[k], &fc);
        }
        free(codes);
        FlDoc_Add(d, l, -1);
    }
    return true;
}

bool FlDoc_Store(const FlDoc *d, FSettings *fs, const char *hash)
{
    if (!fs || !hash || !hash[0]) return false;
    bool had = fs_has_font(fs, hash);
    if (!had && d->n == 0) return true;
    if (had && !fs_remove_font(fs, hash)) return false;
    for (int i = 0; i < d->n; i++) {
        const FlLayer *l = d->v[i];
        FsLayer s = l->s;
        s.position = i;
        int id = fs_add_layer(fs, hash, &s);
        if (id < 0) return false;
        for (int k = 0; k < l->ncorr; k++) {
            FsCorrection c = {l->corr[k].dx, l->corr[k].dy, l->corr[k].dsize, l->corr[k].hidden};
            if (!fs_set_correction(fs, id, l->corr[k].code, &c)) return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ corrections and placement */

const FlCorr *FlLayer_Corr(const FlLayer *l, PxCode code)
{
    int lo = 0, hi = l->ncorr;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (l->corr[mid].code < code) lo = mid + 1; else hi = mid;
    }
    return (lo < l->ncorr && l->corr[lo].code == code) ? &l->corr[lo] : NULL;
}

void FlLayer_SetCorr(FlLayer *l, PxCode code, const FlCorr *c)
{
    bool def = c->dx == 0 && c->dy == 0 && c->dsize == 1.0f && !c->hidden;
    int lo = 0, hi = l->ncorr;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (l->corr[mid].code < code) lo = mid + 1; else hi = mid;
    }
    bool have = lo < l->ncorr && l->corr[lo].code == code;
    if (def) {
        if (have) { memmove(&l->corr[lo], &l->corr[lo + 1], sizeof(FlCorr) * (size_t)(l->ncorr - lo - 1)); l->ncorr--; }
        return;
    }
    if (!have) {
        if (l->ncorr == l->capcorr) {
            int nc = l->capcorr ? l->capcorr * 2 : 8;
            FlCorr *p = (FlCorr *)realloc(l->corr, sizeof(FlCorr) * (size_t)nc);
            if (!p) return;
            l->corr = p;
            l->capcorr = nc;
        }
        memmove(&l->corr[lo + 1], &l->corr[lo], sizeof(FlCorr) * (size_t)(l->ncorr - lo));
        l->ncorr++;
    }
    l->corr[lo] = *c;
    l->corr[lo].code = code;
}

int fl_bitmap_k(float scale)
{
    int k = (int)lroundf(scale);
    return k < 1 ? 1 : (k > FL_MAX_K ? FL_MAX_K : k);
}

void FlLayer_Place(const FlLayer *l, PxCode code, FlPlace *out)
{
    const FlCorr *c = FlLayer_Corr(l, code);
    out->dx = l->s.dx + (c ? c->dx : 0);
    out->dy = l->s.dy + (c ? c->dy : 0);
    out->hidden = c && c->hidden;
    out->dsize = c ? c->dsize : 1.0f;
    float s = l->s.scale * (c ? c->dsize : 1.0f);
    bool bitmap = l->state == FL_OK && !l->outline && l->s.kind == 1;
    out->scale = bitmap ? (float)fl_bitmap_k(s) : s;
}

float FlLayer_Em(const FlLayer *l, int fontPx, const FlPlace *p)
{
    float em = l->s.em > 0 ? l->s.em : (float)fontPx * l->s.scale;
    return em * (p && p->dsize > 0 ? p->dsize : 1.0f);
}

void FlLayer_FitParams(const FlLayer *l, int fontPx, FitParams *out)
{
    out->em = FlLayer_Em(l, fontPx, NULL);
    out->aspect = l->s.aspect > 0 ? l->s.aspect : 1.0f;
    out->dx = l->s.dx;
    out->dy = l->s.dy;
    out->threshold = l->s.threshold > 0 ? l->s.threshold : 0.5f;
    out->mode = l->s.mode ? FIT_PIXEL : FIT_SMOOTH;
}

bool FlLayer_FitCorr(const FlLayer *l, PxCode code, FitCorr *out)
{
    const FlCorr *c = FlLayer_Corr(l, code);
    out->dx = c ? c->dx : 0;
    out->dy = c ? c->dy : 0;
    out->dsize = c ? c->dsize : 1.0f;
    return c != NULL;
}

float FlLayer_Shift(FlLayer *l, PxCode code, int fontPx)
{
    if (!l->s.follow || l->s.mode || l->s.kind != 1 || l->state != FL_OK || !l->outline || !(l->s.em > 0)) return 0;
    FitParams p;
    FitCorr c;
    FlLayer_FitParams(l, fontPx, &p);
    FlLayer_FitCorr(l, code, &c);
    if (l->snap.valid && l->snap.code == code && !memcmp(&l->snap.p, &p, sizeof p) && !memcmp(&l->snap.c, &c, sizeof c)) return l->snap.shift;
    FitBitmap b;
    float shift = 0;
    int gi = outline_glyph_index(l->ol, px_code_codepoint(code));
    if (gi > 0 && fit_render_shift(l->ol, gi, &p, &c, &b, &shift) == 0) fit_bitmap_free(&b);
    else shift = 0;
    l->snap.valid = true;
    l->snap.code = code;
    l->snap.p = p;
    l->snap.c = c;
    l->snap.shift = shift;
    return shift;
}

/* ------------------------------------------------------------------ opening the file */

static bool file_stat(const char *path, int64_t *size, int64_t *mtime)
{
    struct stat st;
    if (stat(path, &st) != 0 || (st.st_mode & S_IFMT) == S_IFDIR) return false;
    *size = (int64_t)st.st_size;
    *mtime = (int64_t)st.st_mtime;
    return true;
}

static const char *ext_of(const char *p)
{
    const char *dot = strrchr(p, '.'), *s1 = strrchr(p, '/'), *s2 = strrchr(p, 0x5C);
    const char *sl = s1 > s2 ? s1 : s2;
    return (dot && (!sl || dot > sl)) ? dot + 1 : "";
}

static bool ext_in(const char *e, const char *list)   /* list: ".a.b." */
{
    char t[16];
    snprintf(t, sizeof t, ".%s.", e);
    for (char *q = t; *q; q++) *q = (char)tolower((unsigned char)*q);
    return strstr(list, t) != NULL;
}

/* does the outline font draw ink for one of the first code points of strike 0 of the bitmap source? */
static bool outline_inks(Outline *ol, BmSource *bm)
{
    int n = bm_glyph_count(bm, 0);
    for (int i = 0; i < n && i < 32; i++) {
        const BmGlyph *g = bm_glyph_at(bm, 0, i);
        if (!g || g->codepoint < 0 || g->w * g->h <= 0) continue;
        int gi = outline_glyph_index(ol, g->codepoint);
        if (gi <= 0) continue;
        int left, top, w, h;
        float adv;
        if (outline_glyph_bbox(ol, gi, 16.0f, 0, 0, &left, &top, &w, &h, &adv, NULL, 0) == 0 && w * h > 0) return true;
    }
    return false;
}

static void open_src(FlLayer *l)
{
    close_src(l);
    l->state = FL_FAILED;
    l->checked = GetTime();
    l->canOutline = l->canBitmap = false;
    l->nFaces = 0;
    int64_t size, mtime;
    if (!file_stat(l->s.source, &size, &mtime)) { snprintf(l->err, sizeof l->err, "file missing"); return; }
    const char *e = ext_of(l->s.source);
    bool known = ext_in(e, ".ttf.otf.ttc.otc.woff.eot.pfb.bdf.pcf.fnt.fon.json.");
    bool tryOl = !known || ext_in(e, ".ttf.otf.ttc.otc.woff.eot.pfb.");
    bool tryBm = !known || ext_in(e, ".ttf.otf.bdf.pcf.fnt.fon.json.");
    char e1[256] = "", e2[BM_ERR_LEN] = "";
    Outline *ol = tryOl ? outline_open(l->s.source, e1, sizeof e1) : NULL;
    BmSource *bm = tryBm ? bm_open(l->s.source, e2, sizeof e2) : NULL;
    if (!ol && !bm) { snprintf(l->err, sizeof l->err, "cannot read: %.130s", tryOl && e1[0] ? e1 : (e2[0] ? e2 : "unknown format")); return; }
    bool flag = l->s.face >= FL_BM_FLAG;
    int idx = flag ? l->s.face - FL_BM_FLAG : l->s.face;
    if (idx < 0) idx = 0;
    bool useBm;
    l->canOutline = ol != NULL;
    l->canBitmap = bm != NULL;
    if (ol && bm) useBm = flag || !outline_inks(ol, bm);
    else useBm = bm != NULL;
    if (useBm) {
        if (ol) { outline_close(ol); ol = NULL; }
        int n = bm_strike_count(bm);
        if (n < 1) { bm_close(bm); snprintf(l->err, sizeof l->err, "no bitmap strike"); return; }
        l->bm = bm;
        l->outline = false;
        l->nFaces = n;
        l->strike = idx < n ? idx : n - 1;
    } else {
        if (bm) { bm_close(bm); bm = NULL; }
        char er[160] = "";
        if (outline_face_count(ol) > 1 && idx > 0 && outline_select_face(ol, idx < outline_face_count(ol) ? idx : outline_face_count(ol) - 1, er, sizeof er) != 0) {
            outline_close(ol);
            snprintf(l->err, sizeof l->err, "cannot read: %.130s", er);
            return;
        }
        l->ol = ol;
        l->outline = true;
        l->nFaces = outline_face_count(ol);
    }
    if (l->s.fileSize != size || l->s.fileMtime != mtime) { l->s.fileSize = size; l->s.fileMtime = mtime; l->metaChanged = true; }
    l->err[0] = 0;
    l->state = FL_OK;
}

bool FlLayer_Ready(FlLayer *l)
{
    if (l->s.kind != 1) return true;
    double now = GetTime();
    if (l->state != FL_UNOPENED && now - l->checked >= 2.0) {   /* the file may have changed (or appeared) */
        l->checked = now;
        int64_t sz, mt;
        bool there = file_stat(l->s.source, &sz, &mt);
        if (l->state == FL_FAILED ? there : (!there || sz != l->s.fileSize || mt != l->s.fileMtime)) {
            close_src(l);
            l->state = FL_UNOPENED;
        }
    }
    if (l->state == FL_UNOPENED) open_src(l);
    return l->state == FL_OK;
}

bool FlLayer_SetFace(FlLayer *l, int face)
{
    l->s.face = face;
    close_src(l);
    l->state = FL_UNOPENED;
    return FlLayer_Ready(l);
}

static const char *base_of(const char *p)
{
    const char *s1 = strrchr(p, '/'), *s2 = strrchr(p, 0x5C);
    const char *s = s1 > s2 ? s1 : s2;
    return s ? s + 1 : p;
}

void FlLayer_Label(FlLayer *l, char *out, int n)
{
    out[0] = 0;
    if (l->s.kind != 1) { snprintf(out, (size_t)n, "%s", l->s.label); return; }
    if (FlLayer_Ready(l)) {
        if (l->outline) {
            const char *fam = outline_family(l->ol), *sty = outline_style(l->ol);
            if (fam[0] && sty[0]) snprintf(out, (size_t)n, "%s %s", fam, sty);
            else if (fam[0]) snprintf(out, (size_t)n, "%s", fam);
        } else {
            BmStrikeInfo si;
            if (bm_strike_info(l->bm, l->strike, &si)) {
                char nm[200];
                if (si.family[0]) snprintf(nm, sizeof nm, "%s%s%s", si.family, si.style[0] ? " " : "", si.style);
                else snprintf(nm, sizeof nm, "%s", si.name);
                if (nm[0] && l->nFaces > 1 && si.label[0]) snprintf(out, (size_t)n, "%s %s", nm, si.label);
                else if (nm[0]) snprintf(out, (size_t)n, "%s", nm);
                else if (si.label[0]) snprintf(out, (size_t)n, "%s %s", base_of(l->s.source), si.label);
            }
        }
    }
    if (!out[0]) snprintf(out, (size_t)n, "%s", base_of(l->s.source));
}

/* ------------------------------------------------------------------ glyph questions */

static bool gate(FlLayer *l) { return l->s.kind == 1 && FlLayer_Ready(l); }

int FlLayer_Has(FlLayer *l, PxCode code, int fontPx)
{
    (void)fontPx;
    if (!gate(l)) return -1;
    if (l->outline) {
        int gi = outline_glyph_index(l->ol, px_code_codepoint(code));
        if (gi <= 0) return -1;
        int left, top, w, h;
        float adv;
        if (outline_glyph_bbox(l->ol, gi, 16.0f, 0, 0, &left, &top, &w, &h, &adv, NULL, 0) != 0) return 0;
        return w * h > 0;
    }
    const BmGlyph *g = bm_find(l->bm, l->strike, px_code_codepoint(code));
    if (!g) return -1;
    return g->w * g->h > 0;
}

int FlLayer_InkCount(FlLayer *l, PxCode code, int fontPx)
{
    if (!gate(l)) return -1;
    int n = 0;
    if (l->outline) {
        if (outline_glyph_index(l->ol, px_code_codepoint(code)) <= 0) return -1;
        OutlineBitmap b;
        if (outline_rasterize(l->ol, px_code_codepoint(code), (float)(fontPx < 1 ? 16 : fontPx), 0, 0, &b, NULL, 0) != OUTLINE_OK) return -1;
        for (int i = 0; b.pix && i < b.w * b.h; i++) n += b.pix[i] >= 128;
        outline_bitmap_free(&b);
        return n;
    }
    const BmGlyph *g = bm_find(l->bm, l->strike, px_code_codepoint(code));
    if (!g) return -1;
    for (int i = 0; g->pix && i < g->w * g->h; i++) n += g->pix[i] != 0;
    return n;
}

/* ------------------------------------------------------------------ drawing */

static Texture2D make_texture(const OutlineBitmap *b)
{
    Texture2D none = {0};
    unsigned char *rgba = (unsigned char *)malloc((size_t)b->w * (size_t)b->h * 4);
    if (!rgba) return none;
    for (int i = 0; i < b->w * b->h; i++) { rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = 255; rgba[4 * i + 3] = b->pix[i]; }
    Image img = {rgba, b->w, b->h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(img);
    free(rgba);
    return t;
}

static void draw_outline(FlLayer *l, PxCode code, int ox, int oy, int z, int fontPx, const FlPlace *p, Color tint)
{
    float em = FlLayer_Em(l, fontPx, p) * (float)z, aspect = l->s.aspect > 0 ? l->s.aspect : 1.0f;
    if (em < 1.0f || em > FL_MAX_EM || em * aspect < 1.0f || em * aspect > FL_MAX_EM) return;
    int emKey = (int)lroundf(em * 8.0f);
    int key = emKey + 20000 * (int)lroundf(aspect * 512.0f);   /* a stretched layer has its own textures */
    CacheEnt *c = cache_find(l->serial, code, key);
    if (!c) {
        OutlineBitmap b;
        memset(&b, 0, sizeof b);
        int gi = outline_glyph_index(l->ol, px_code_codepoint(code));
        int rc = gi > 0 ? outline_rasterize_glyph_xy(l->ol, gi, (float)emKey / 8.0f * aspect, (float)emKey / 8.0f, 0, 0, &b, NULL, 0) : OUTLINE_MISSING;
        long pix = rc == OUTLINE_OK ? (long)b.w * b.h : 0;
        c = cache_slot(pix);
        if (!c) { outline_bitmap_free(&b); return; }
        memset(c, 0, sizeof *c);
        c->live = true;
        c->serial = l->serial;
        c->code = code;
        c->key = key;
        c->used = ++cacheTick;
        if (pix > 0) {
            c->tex = make_texture(&b);
            c->left = b.left; c->top = b.top; c->w = b.w; c->h = b.h;
            cachePix += pix;
        }
        outline_bitmap_free(&b);
    }
    if (!c->tex.id) return;
    float shift = FlLayer_Shift(l, code, fontPx);   /* where the pixels of the glyph were made from */
    DrawTexture(c->tex, ox + (int)lroundf((p->dx + shift) * (float)z) + c->left, oy - (int)lroundf(p->dy * (float)z) - c->top, tint);
}

static void draw_bitmap(FlLayer *l, PxCode code, int ox, int oy, int z, const FlPlace *p, Color tint)
{
    const BmGlyph *g = bm_find(l->bm, l->strike, px_code_codepoint(code));
    if (!g || !g->pix || g->w <= 0 || g->h <= 0) return;
    int k = (int)p->scale, cell = k * z;
    int x0 = ox + (int)lroundf(p->dx) * z + g->x * cell;
    int yTop = oy - (int)lroundf(p->dy) * z - (g->y + g->h) * cell;
    for (int r = 0; r < g->h; r++)
        for (int c = 0; c < g->w;) {
            unsigned char v = g->pix[r * g->w + c];
            if (!v) { c++; continue; }
            int run = 1;
            if (g->depth == 1) while (c + run < g->w && g->pix[r * g->w + c + run]) run++;
            Color col = tint;
            if (g->depth != 1) col.a = (unsigned char)(tint.a * v / 255);
            DrawRectangle(x0 + c * cell, yTop + r * cell, run * cell, cell, col);
            c += run;
        }
}

void FlLayer_Draw(FlLayer *l, PxCode code, int ox, int oy, int z, int fontPx, const FlPlace *p, Color tint)
{
    if (!gate(l) || p->hidden || px_code_codepoint(code) < 0) return;   /* a variant is drawn from the glyph of its code point */
    if (l->outline) draw_outline(l, code, ox, oy, z, fontPx, p, tint);
    else draw_bitmap(l, code, ox, oy, z, p, tint);
}
