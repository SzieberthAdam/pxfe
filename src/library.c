/* library.c - the browser's font index (from the Store), drafts listing and the threaded lazy loader. See library.h. */
#include "library.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* ------------------------------------------------------------------ small helpers */

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static bool fail(char err[PX_ERR_LEN], const char *fmt, ...) {
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, PX_ERR_LEN, fmt, ap);
        va_end(ap);
    }
    return false;
}

typedef struct DirItem { char name[MAX_PATH]; bool isDir; uint64_t size, mtime; } DirItem;

/* all entries of a folder except . and ..; NULL with *n == 0 when it cannot be listed */
static DirItem *list_dir(const char *dir, int *n) {
    char pat[1100];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    *n = 0;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    int cap = 64;
    DirItem *v = (DirItem *)malloc(sizeof(DirItem) * (size_t)cap);
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (*n == cap) {
            cap *= 2;
            DirItem *nv = (DirItem *)realloc(v, sizeof(DirItem) * (size_t)cap);
            if (!nv) break;
            v = nv;
        }
        DirItem *d = &v[(*n)++];
        snprintf(d->name, sizeof d->name, "%s", fd.cFileName);
        d->isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        d->size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        d->mtime = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return v;
}

static bool has_suffix(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static double now_seconds(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* ------------------------------------------------------------------ scan */

typedef struct HRef { const char *h; int i; } HRef;
static int cmp_href(const void *a, const void *b) {
    const HRef *x = (const HRef *)a, *y = (const HRef *)b;
    int r = strcmp(x->h, y->h);
    return r ? r : x->i - y->i;
}

static void add_error(Library *lib, const char *msg) {
    char **ne = (char **)realloc(lib->errors, sizeof(char *) * (size_t)(lib->nerrors + 1));
    if (!ne) return;
    lib->errors = ne;
    lib->errors[lib->nerrors++] = dup_str(msg);
}

static void info_from_store(LibEntry *e, const StoreEntry *se) {
    LibInfo *in = &e->info;
    memset(in, 0, sizeof *in);
    in->defaultCodepoint = PX_NO_CODEPOINT;
    if (!(se->hasAscent && se->hasDescent)) return;      /* not ready: the loader derives the box */
    in->ready = true;
    in->glyphCount = se->glyphCount;
    in->boxStated = true;
    in->boxExact = true;
    in->above = se->ascent;
    in->below = se->descent;
    in->monospaced = se->monospaced;
    in->hasRatio = se->hasRatio;
    in->ratioA = se->ratioA;
    in->ratioB = se->ratioB;
}

static void entry_from_store(LibEntry *e, LibKind kind, const StoreEntry *se) {
    memset(e, 0, sizeof *e);
    e->kind = kind;
    memcpy(e->pxfm, se->pxfm, 33);
    memcpy(e->pxtf, se->pxtf, 33);
    e->variant = se->variant;
    e->family = se->family; e->style = se->style; e->fullname = se->fullname;
    e->designer = se->designer; e->format = se->format;
    e->italic = se->italic; e->struckout = se->struckout; e->underlined = se->underlined;
    info_from_store(e, se);
}

bool library_scan(Store *store, Library *lib, char err[PX_ERR_LEN]) {
    memset(lib, 0, sizeof *lib);
    const StoreInfo *si = store_info(store);
    snprintf(lib->root, sizeof lib->root, "%s", si->path);
    double t0 = now_seconds();
    StoreConn *c = store_conn_open(store);
    if (!c) return fail(err, "cannot open a store connection");
    bool ok = store_list_typefaces(c, &lib->tl, err) && store_list_fonts(c, &lib->fl, err);
    if (!ok) { store_conn_close(c); store_entry_list_free(&lib->tl); store_entry_list_free(&lib->fl); return false; }
    int nt = lib->tl.count, nf = lib->fl.count;
    lib->e = (LibEntry *)calloc((size_t)(nt + nf + 1), sizeof(LibEntry));
    for (int i = 0; i < nt; i++) {
        LibEntry *e = &lib->e[lib->count++];
        entry_from_store(e, LIB_TYPEFACE, &lib->tl.e[i]);
        e->fontPresent = store_has_pxfm(c, e->pxfm);
        if (!e->fontPresent) {
            e->info.ready = e->info.failed = true;
            snprintf(e->info.err, sizeof e->info.err, "the pxfm is missing from the library");
        }
    }
    lib->nTypefaceEntries = nt;
    for (int i = 0; i < nf; i++) {
        LibEntry *e = &lib->e[lib->count++];
        entry_from_store(e, LIB_FONT, &lib->fl.e[i]);
        e->fontPresent = true;
    }
    lib->nFontEntries = nf;
    store_conn_close(c);
    /* index by pxfm hash */
    HRef *h = (HRef *)malloc(sizeof(HRef) * (size_t)(lib->count ? lib->count : 1));
    for (int i = 0; i < lib->count; i++) { h[i].h = lib->e[i].pxfm; h[i].i = i; }
    qsort(h, (size_t)lib->count, sizeof(HRef), cmp_href);
    lib->byPxfm = (int *)malloc(sizeof(int) * (size_t)(lib->count ? lib->count : 1));
    for (int i = 0; i < lib->count; i++) lib->byPxfm[i] = h[i].i;
    lib->nByPxfm = lib->count;
    free(h);
    lib->scanSeconds = now_seconds() - t0;
    return true;
}

void library_free(Library *lib) {
    for (int i = 0; i < lib->count; i++) { free(lib->e[i].path); free(lib->e[i].name); }
    free(lib->e);
    for (int i = 0; i < lib->ntypefaces; i++) { pxtf_free(lib->typefaces[i]); free(lib->typefaces[i]); }
    free(lib->typefaces);
    free(lib->byPxfm);
    for (int i = 0; i < lib->nerrors; i++) free(lib->errors[i]);
    free(lib->errors);
    store_entry_list_free(&lib->tl);
    store_entry_list_free(&lib->fl);
    memset(lib, 0, sizeof *lib);
}

static int lower_bound_pxfm(const Library *lib, const char *hash) {
    int lo = 0, hi = lib->nByPxfm;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(lib->e[lib->byPxfm[mid]].pxfm, hash) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int library_find_by_pxfm(const Library *lib, LibKind kind, const char *pxfmHash) {
    for (int k = lower_bound_pxfm(lib, pxfmHash); k < lib->nByPxfm; k++) {
        const LibEntry *e = &lib->e[lib->byPxfm[k]];
        if (strcmp(e->pxfm, pxfmHash) != 0) break;
        if (e->kind == kind) return lib->byPxfm[k];
    }
    return -1;
}

void library_apply_info(Library *lib, const char *pxfmHash, const LibInfo *info) {
    for (int k = lower_bound_pxfm(lib, pxfmHash); k < lib->nByPxfm; k++) {
        LibEntry *e = &lib->e[lib->byPxfm[k]];
        if (strcmp(e->pxfm, pxfmHash) != 0) break;
        e->info = *info;
    }
}

void library_apply_result(Library *lib, const LibLoadResult *r) { library_apply_info(lib, r->hash, &r->info); }

void library_entry_title(const LibEntry *e, char *buf, size_t size) {
    if (e->kind == LIB_DRAFT && e->name && e->name[0]) snprintf(buf, size, "%s", e->name);
    else if (e->family && e->family[0]) snprintf(buf, size, "%s%s%s", e->family, e->style && e->style[0] ? " " : "", e->style ? e->style : "");
    else if (e->fullname && e->fullname[0]) snprintf(buf, size, "%s", e->fullname);
    else snprintf(buf, size, "%.8s", e->pxfm[0] ? e->pxfm : "draft");
}

/* ------------------------------------------------------------------ drafts */

static int cmp_dir_mtime_desc(const void *a, const void *b) {
    const DirItem *x = (const DirItem *)a, *y = (const DirItem *)b;
    return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : strcmp(x->name, y->name);
}

static void set_meta(LibEntry *e, const PxtfTypeface *c, const PxtfVariant *v) {
    static const char *const none = "";
    e->typeface = v ? c : NULL;
    e->tvariant = v;
#define T(t) ((v && v->text[t]) ? v->text[t] : none)
    e->family = T(PXTF_FAMILY);
    e->style = T(PXTF_STYLE);
    e->fullname = T(PXTF_FULLNAME);
    e->designer = T(PXTF_DESIGNER);
#undef T
    e->format = (v && v->format) ? v->format : none;
    e->italic = v && v->italic;
    e->struckout = v && v->struckout;
    e->underlined = v && v->underlined;
}

static void lower_hash(char *dst, const char *src) {
    for (int i = 0; i < 32; i++) dst[i] = (char)((src[i] >= 'A' && src[i] <= 'F') ? src[i] + 32 : src[i]);
    dst[32] = 0;
}

/* header-only reading of a draft: no bitmap is kept */
static bool draft_quick(const char *path, Library *lib, LibEntry *e, char err[PX_ERR_LEN]) {
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) return fail(err, "cannot read %s", path);
    LibInfo *in = &e->info;
    memset(in, 0, sizeof *in);
    in->defaultCodepoint = PX_NO_CODEPOINT;
    bool hasA = false, hasD = false, header = false;
    int a = 0, d = 0, ga = 0, gd = 0;
    int32_t dflt = PX_NO_CODEPOINT;
    char *ctext = NULL;
    size_t clen = 0, ccap = 0;
    char *p = raw;
    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = 0; p = nl + 1; } else p += strlen(p);
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r') line[--ll] = 0;
        if (!ll || line[0] == '\t') continue;           /* bitmap rows */
        if (!header) {
            if (strcmp(line, "PXFEDRAFT\t3") != 0) {
                bool v2 = strcmp(line, "PXFEDRAFT\t2") == 0;
                free(raw); free(ctext);
                return fail(err, v2 ? "%s: a version 2 draft (made before the pxtf / F-code change); version 3 drafts only" : "%s: not a version 3 draft", path);
            }
            header = true;
            continue;
        }
        if (strncmp(line, "NAME\t", 5) == 0) {
            char *un = NULL;
            char e2[PX_ERR_LEN];
            if (pxtf_unescape(line + 5, &un, e2)) { free(e->name); e->name = dup_str(strcmp(un, "-") == 0 ? "" : un); free(un); }
        } else if (strncmp(line, "ORIGINALHASH\t", 13) == 0 && px_is_hash(line + 13, true, false)) {
            lower_hash(e->pxfm, line + 13);
        } else if (strncmp(line, "ORIGINALPXTF\t", 13) == 0 && strlen(line) > 46 && line[45] == '\t') {
            char hh[33];
            memcpy(hh, line + 13, 32);
            hh[32] = 0;
            if (px_is_hash(hh, true, false)) {
                lower_hash(e->pxtf, hh);
                e->variant = atoi(line + 46);
            }
        } else if (strncmp(line, "PXTF\t", 5) == 0) {
            char *un = NULL;
            char e2[PX_ERR_LEN];
            if (pxtf_unescape(line + 5, &un, e2)) {
                size_t l = strlen(un);
                if (clen + l + 2 > ccap) { ccap = (clen + l + 2) * 2; ctext = (char *)realloc(ctext, ccap); }
                memcpy(ctext + clen, un, l);
                clen += l;
                ctext[clen++] = '\n';
                free(un);
            }
        } else if (strncmp(line, "ASCENT\t", 7) == 0) { hasA = true; a = atoi(line + 7); }
        else if (strncmp(line, "DESCENT\t", 8) == 0) { hasD = true; d = atoi(line + 8); }
        else if (strncmp(line, "RATIO\t", 6) == 0) {
            int x, y;
            if (sscanf(line + 6, "%d:%d", &x, &y) == 2) { in->hasRatio = true; in->ratioA = x; in->ratioB = y; }
        } else if (strcmp(line, "MONOSPACED") == 0) in->monospaced = true;
        else if (strncmp(line, "DEFAULT\t", 8) == 0) dflt = (int32_t)strtol(line + 8, NULL, 16);
        else if (strncmp(line, "GLYPH\t", 6) == 0) {
            int x, y, w, h;
            in->glyphCount++;
            const char *q = strchr(line + 6, '\t');    /* after the code */
            if (q && sscanf(q + 1, "%d,%d\t%*d\t%dx%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0) {
                if (y + h > ga) ga = y + h;
                if (-y > gd) gd = -y;
            }
        }
    }
    free(raw);
    if (!header) { free(ctext); return fail(err, "%s: empty draft", path); }
    in->ready = true;
    in->boxStated = hasA && hasD;
    in->boxExact = true;
    in->above = hasA ? a : ga;
    in->below = hasD ? d : gd;
    in->defaultCodepoint = dflt;
    if (ctext) {
        PxtfTypeface *c = (PxtfTypeface *)malloc(sizeof *c);
        char e2[PX_ERR_LEN];
        if (c && pxtf_parse(ctext, clen, c, e2)) {
            PxtfTypeface **nc = (PxtfTypeface **)realloc(lib->typefaces, sizeof(PxtfTypeface *) * (size_t)(lib->ntypefaces + 1));
            if (nc) {
                lib->typefaces = nc;
                lib->typefaces[lib->ntypefaces++] = c;
                PxtfVariant *v = pxtf_find_variant(c, e->variant);
                if (!v && c->count == 1) v = &c->v[0];
                set_meta(e, c, v);
            } else { pxtf_free(c); free(c); }
        } else free(c);
        free(ctext);
    }
    if (!e->typeface) set_meta(e, NULL, NULL);
    return true;
}

bool library_scan_drafts(const char *dir, Library *lib, char err[PX_ERR_LEN]) {
    memset(lib, 0, sizeof *lib);
    snprintf(lib->root, sizeof lib->root, "%s", dir);
    double t0 = now_seconds();
    int n;
    DirItem *items = list_dir(dir, &n);
    int m = 0;
    for (int i = 0; i < n; i++) if (!items[i].isDir && has_suffix(items[i].name, ".pxfd")) items[m++] = items[i];
    qsort(items, (size_t)m, sizeof(DirItem), cmp_dir_mtime_desc);   /* newest first */
    lib->e = (LibEntry *)calloc((size_t)(m ? m : 1), sizeof(LibEntry));
    for (int i = 0; i < m; i++) {
        LibEntry *e = &lib->e[lib->count];
        char p[1300], e2[PX_ERR_LEN];
        snprintf(p, sizeof p, "%s/%s", dir, items[i].name);
        e->kind = LIB_DRAFT;
        e->path = dup_str(p);
        e->fileSize = items[i].size;
        e->mtime = items[i].mtime;
        if (!draft_quick(p, lib, e, e2)) {
            add_error(lib, e2);
            free(e->path); free(e->name);
            memset(e, 0, sizeof *e);
            continue;
        }
        e->fontPresent = true;
        lib->count++;
    }
    free(items);
    (void)err;
    lib->scanSeconds = now_seconds() - t0;
    return true;
}

/* ------------------------------------------------------------------ loader */

typedef struct Req { char hash[33]; int32_t *cps; int ncps; } Req;
typedef struct CacheEnt { char hash[33]; LibFont *font; int32_t *cps; int ncps; unsigned long used; } CacheEnt;

struct LibLoader {
    Store *store;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    HANDLE *threads; int nthreads;
    bool quit;
    Req *pending; int npending, capPending;
    char inflight[64][33]; int ninflight;
    LibLoadResult *done; int ndone, capDone;
    CacheEnt *cache; int ncache, capCache, maxFonts;
    unsigned long tick;
};

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return x < y ? -1 : x > y;
}

/* sorted unique union; returns malloc'd array (never NULL) */
static int32_t *cps_union(const int32_t *a, int na, const int32_t *b, int nb, int *nout) {
    int32_t *r = (int32_t *)malloc(sizeof(int32_t) * (size_t)(na + nb + 1));
    int n = 0;
    if (na) memcpy(r, a, sizeof(int32_t) * (size_t)na);
    if (nb) memcpy(r + na, b, sizeof(int32_t) * (size_t)nb);
    n = na + nb;
    qsort(r, (size_t)n, sizeof(int32_t), cmp_i32);
    int o = 0;
    for (int i = 0; i < n; i++) if (o == 0 || r[o - 1] != r[i]) r[o++] = r[i];
    *nout = o;
    return r;
}

static bool cps_has_all(const int32_t *have, int nh, const int32_t *want, int nw) {
    for (int i = 0; i < nw; i++)
        if (!bsearch(&want[i], have, (size_t)nh, sizeof(int32_t), cmp_i32)) return false;
    return true;
}

static void font_release_locked(LibFont *f) {
    if (!f) return;
    if (--f->refs <= 0) { px_font_free(&f->font); free(f); }
}

static void fill_info(const PxFont *f, LibInfo *in) {
    memset(in, 0, sizeof *in);
    in->ready = true;
    in->glyphCount = f->count;
    in->boxStated = f->has[PXM_ASCENT] && f->has[PXM_DESCENT];
    in->boxExact = px_font_box(f, &in->above, &in->below);
    in->monospaced = f->monospaced;
    in->hasRatio = f->hasRatio;
    in->ratioA = f->ratioA;
    in->ratioB = f->ratioB;
    in->defaultCodepoint = f->defaultCodepoint;
}

static const int32_t PROBES[] = {0x48, 0x67, 0x70, 0x79, 0x6A, 0x7C, 0x5B, 0x5D, 0xC5, 0xC9, 0xC7};

static void do_load(LibLoader *L, StoreConn *conn, const Req *r, LibLoadResult *res) {
    memset(res, 0, sizeof *res);
    memcpy(res->hash, r->hash, 33);
    char err[PX_ERR_LEN];
    PxFont f;
    (void)L;
    /* the probe glyphs (eleven small rows) are always asked for: cheaper than a second query to find out
       whether the box is stated */
    int nall = r->ncps;
    int32_t *all = (int32_t *)malloc(sizeof(int32_t) * (size_t)(r->ncps + (int)(sizeof PROBES / sizeof *PROBES) + 1));
    memcpy(all, r->cps, sizeof(int32_t) * (size_t)r->ncps);
    memcpy(all + nall, PROBES, sizeof PROBES);
    nall += (int)(sizeof PROBES / sizeof *PROBES);
    bool ok = store_load_font(conn, r->hash, all, nall, &f, err);
    free(all);
    if (!ok) {
        res->info.ready = true;
        res->info.failed = true;
        snprintf(res->info.err, sizeof res->info.err, "%.159s", err);
        return;
    }
    fill_info(&f, &res->info);
    /* a glyph error cannot happen: the rows are read from the store */
    LibFont *lf = (LibFont *)calloc(1, sizeof *lf);
    memcpy(lf->hash, r->hash, 33);
    lf->font = f;
    lf->info = res->info;
    lf->refs = 1;           /* the loader's; the caller of do_load adds the others */
    res->font = lf;
}

static bool is_inflight(const LibLoader *L, const char *hash) {
    for (int i = 0; i < L->ninflight; i++) if (strcmp(L->inflight[i], hash) == 0) return true;
    return false;
}

static CacheEnt *cache_find(LibLoader *L, const char *hash) {
    for (int i = 0; i < L->ncache; i++) if (strcmp(L->cache[i].hash, hash) == 0) return &L->cache[i];
    return NULL;
}

static void install(LibLoader *L, const Req *r, LibLoadResult *res) {
    if (res->font) {
        CacheEnt *ce = cache_find(L, r->hash);
        int nun;
        if (ce) {
            int32_t *un = cps_union(ce->cps, ce->ncps, r->cps, r->ncps, &nun);
            free(ce->cps);
            ce->cps = un;
            ce->ncps = nun;
            font_release_locked(ce->font);
        } else {
            if (L->ncache == L->capCache) {
                L->capCache = L->capCache ? L->capCache * 2 : 64;
                L->cache = (CacheEnt *)realloc(L->cache, sizeof(CacheEnt) * (size_t)L->capCache);
            }
            ce = &L->cache[L->ncache++];
            memcpy(ce->hash, r->hash, 33);
            ce->cps = cps_union(NULL, 0, r->cps, r->ncps, &nun);
            ce->ncps = nun;
        }
        ce->font = res->font;           /* the cache's reference */
        ce->used = ++L->tick;
        res->font->refs++;              /* the result's reference */
        int ci = (int)(ce - L->cache);
        while (L->ncache > L->maxFonts) {   /* LRU eviction, never the one just installed */
            int victim = -1;
            for (int i = 0; i < L->ncache; i++)
                if (i != ci && (victim < 0 || L->cache[i].used < L->cache[victim].used)) victim = i;
            if (victim < 0) break;
            font_release_locked(L->cache[victim].font);
            free(L->cache[victim].cps);
            L->cache[victim] = L->cache[L->ncache - 1];
            if (ci == L->ncache - 1) ci = victim;
            L->ncache--;
        }
    }
    if (L->ndone == L->capDone) {
        L->capDone = L->capDone ? L->capDone * 2 : 64;
        L->done = (LibLoadResult *)realloc(L->done, sizeof(LibLoadResult) * (size_t)L->capDone);
    }
    L->done[L->ndone++] = *res;
}

static DWORD WINAPI loader_thread(LPVOID p) {
    LibLoader *L = (LibLoader *)p;
    StoreConn *conn = store_conn_open(L->store);
    for (;;) {
        EnterCriticalSection(&L->cs);
        int ri;
        for (;;) {
            if (L->quit) { LeaveCriticalSection(&L->cs); store_conn_close(conn); return 0; }
            ri = -1;
            for (int i = 0; i < L->npending; i++) if (!is_inflight(L, L->pending[i].hash)) { ri = i; break; }
            if (ri >= 0 && L->ninflight < 64) break;
            SleepConditionVariableCS(&L->cv, &L->cs, INFINITE);
        }
        Req r = L->pending[ri];
        memmove(&L->pending[ri], &L->pending[ri + 1], sizeof(Req) * (size_t)(L->npending - ri - 1));
        L->npending--;
        memcpy(L->inflight[L->ninflight++], r.hash, 33);
        LeaveCriticalSection(&L->cs);

        LibLoadResult res;
        do_load(L, conn, &r, &res);

        EnterCriticalSection(&L->cs);
        for (int i = 0; i < L->ninflight; i++)
            if (strcmp(L->inflight[i], r.hash) == 0) { memcpy(L->inflight[i], L->inflight[L->ninflight - 1], 33); L->ninflight--; break; }
        install(L, &r, &res);
        WakeAllConditionVariable(&L->cv);    /* a pending request of the same font may be runnable now */
        LeaveCriticalSection(&L->cs);
        free(r.cps);
    }
}

LibLoader *libloader_create(Store *store, int threads, int maxFonts) {
    LibLoader *L = (LibLoader *)calloc(1, sizeof *L);
    if (!L) return NULL;
    L->store = store;
    L->maxFonts = maxFonts > 0 ? maxFonts : 256;
    InitializeCriticalSection(&L->cs);
    InitializeConditionVariable(&L->cv);
    if (threads <= 0) threads = 6;
    if (threads > 32) threads = 32;
    L->threads = (HANDLE *)calloc((size_t)threads, sizeof(HANDLE));
    for (int i = 0; i < threads; i++) {
        HANDLE h = CreateThread(NULL, 0, loader_thread, L, 0, NULL);
        if (h) L->threads[L->nthreads++] = h;
    }
    return L;
}

void libloader_destroy(LibLoader *L) {
    if (!L) return;
    EnterCriticalSection(&L->cs);
    L->quit = true;
    WakeAllConditionVariable(&L->cv);
    LeaveCriticalSection(&L->cs);
    for (int i = 0; i < L->nthreads; i++) {
        WaitForSingleObject(L->threads[i], INFINITE);
        CloseHandle(L->threads[i]);
    }
    free(L->threads);
    for (int i = 0; i < L->npending; i++) free(L->pending[i].cps);
    free(L->pending);
    for (int i = 0; i < L->ndone; i++) font_release_locked(L->done[i].font);
    free(L->done);
    for (int i = 0; i < L->ncache; i++) { font_release_locked(L->cache[i].font); free(L->cache[i].cps); }
    free(L->cache);
    DeleteCriticalSection(&L->cs);
    free(L);
}

bool libloader_request(LibLoader *L, const char *hash, const int32_t *cps, int n) {
    EnterCriticalSection(&L->cs);
    CacheEnt *ce = cache_find(L, hash);
    if (ce && cps_has_all(ce->cps, ce->ncps, cps, n)) {
        ce->used = ++L->tick;
        LeaveCriticalSection(&L->cs);
        return true;
    }
    int found = -1;
    for (int i = 0; i < L->npending; i++) if (strcmp(L->pending[i].hash, hash) == 0) { found = i; break; }
    if (found >= 0) {
        int nun;
        int32_t *un = cps_union(L->pending[found].cps, L->pending[found].ncps, cps, n, &nun);
        free(L->pending[found].cps);
        L->pending[found].cps = un;
        L->pending[found].ncps = nun;
    } else {
        if (L->npending == L->capPending) {
            L->capPending = L->capPending ? L->capPending * 2 : 64;
            L->pending = (Req *)realloc(L->pending, sizeof(Req) * (size_t)L->capPending);
        }
        Req *r = &L->pending[L->npending++];
        memcpy(r->hash, hash, 33);
        int nun;
        r->cps = cps_union(ce ? ce->cps : NULL, ce ? ce->ncps : 0, cps, n, &nun);   /* keep what is loaded */
        r->ncps = nun;
    }
    WakeAllConditionVariable(&L->cv);
    LeaveCriticalSection(&L->cs);
    return false;
}

void libloader_set_wanted(LibLoader *L, const char (*hashes)[33], int n) {
    EnterCriticalSection(&L->cs);
    Req *neu = (Req *)malloc(sizeof(Req) * (size_t)(L->npending > 0 ? L->npending : 1));
    bool *taken = (bool *)calloc((size_t)(L->npending > 0 ? L->npending : 1), sizeof(bool));
    int m = 0;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < L->npending; i++)
            if (!taken[i] && strcmp(L->pending[i].hash, hashes[k]) == 0) { neu[m++] = L->pending[i]; taken[i] = true; break; }
    for (int i = 0; i < L->npending; i++) if (!taken[i]) free(L->pending[i].cps);
    memcpy(L->pending, neu, sizeof(Req) * (size_t)m);
    L->npending = m;
    free(neu);
    free(taken);
    LeaveCriticalSection(&L->cs);
}

bool libloader_poll(LibLoader *L, LibLoadResult *r) {
    EnterCriticalSection(&L->cs);
    if (L->ndone == 0) { LeaveCriticalSection(&L->cs); return false; }
    *r = L->done[0];
    memmove(&L->done[0], &L->done[1], sizeof(LibLoadResult) * (size_t)(L->ndone - 1));
    L->ndone--;
    LeaveCriticalSection(&L->cs);
    return true;
}

LibFont *libloader_get(LibLoader *L, const char *hash) {
    EnterCriticalSection(&L->cs);
    CacheEnt *ce = cache_find(L, hash);
    LibFont *f = NULL;
    if (ce) { f = ce->font; f->refs++; ce->used = ++L->tick; }
    LeaveCriticalSection(&L->cs);
    return f;
}

void libloader_release(LibLoader *L, LibFont *f) {
    if (!f) return;
    EnterCriticalSection(&L->cs);
    font_release_locked(f);
    LeaveCriticalSection(&L->cs);
}

int libloader_pending(LibLoader *L) {
    EnterCriticalSection(&L->cs);
    int n = L->npending + L->ninflight;
    LeaveCriticalSection(&L->cs);
    return n;
}
