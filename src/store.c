/* store.c - the SQLite store: open / build / convert / save / queries. See store.h.  Similarity: store_sim.c. */
#include "store.h"
#include "store_int.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "sqlite3.h"

/* ------------------------------------------------------------------ small helpers */

static bool fail(char err[PX_ERR_LEN], const char *fmt, ...) {
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, PX_ERR_LEN, fmt, ap);
        va_end(ap);
    }
    return false;
}

double store_now(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

bool store_hex_to_blob(const char *hex, uint8_t out[16]) {
    if (!hex) return false;
    for (int i = 0; i < 16; i++) {
        int v[2];
        for (int k = 0; k < 2; k++) {
            char c = hex[2 * i + k];
            if (c >= '0' && c <= '9') v[k] = c - '0';
            else if (c >= 'a' && c <= 'f') v[k] = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v[k] = c - 'A' + 10;
            else return false;
        }
        out[i] = (uint8_t)(v[0] * 16 + v[1]);
    }
    return hex[32] == 0;
}

void store_blob_to_hex(const uint8_t *b, char out[33]) {
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
    out[32] = 0;
}

/* A text of the model may hold U+0000 as the two bytes C0 80; the database holds a real NUL. */
static char *to_db_text(const char *s, int *len) {
    size_t n = strlen(s);
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)s[i] == 0xC0 && i + 1 < n && (unsigned char)s[i + 1] == 0x80) { r[o++] = 0; i++; }
        else r[o++] = s[i];
    }
    r[o] = 0;
    *len = (int)o;
    return r;
}

static char *from_db_text(const void *p, int n) {   /* NUL -> C0 80, malloc'd C string */
    const unsigned char *s = (const unsigned char *)p;
    int nul = 0;
    for (int i = 0; i < n; i++) if (s[i] == 0) nul++;
    char *r = (char *)malloc((size_t)n + (size_t)nul + 1);
    if (!r) return NULL;
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == 0) { r[o++] = (char)0xC0; r[o++] = (char)0x80; } else r[o++] = (char)s[i];
    }
    r[o] = 0;
    return r;
}

/* the arena: chunks of strings that live as long as a result list */
typedef struct Arena { char *buf; size_t cap, used; struct Arena *next; } Arena;

static const char *arena_str(Arena **head, const char *s, int n) {
    if (!s) { s = ""; n = 0; }
    if (n < 0) n = (int)strlen(s);
    size_t need = (size_t)n + 1;
    if (!*head || (*head)->cap - (*head)->used < need) {
        Arena *a = (Arena *)calloc(1, sizeof *a);
        a->cap = need > 16384 ? need : 16384;
        a->buf = (char *)malloc(a->cap);
        a->next = *head;
        *head = a;
    }
    char *d = (*head)->buf + (*head)->used;
    memcpy(d, s, (size_t)n);
    d[n] = 0;
    (*head)->used += need;
    return d;
}

static void arena_free(void *p) {
    Arena *a = (Arena *)p;
    while (a) { Arena *n = a->next; free(a->buf); free(a); a = n; }
}

/* run fn(ctx, i) for i in [0,n) on several threads */
typedef struct ParCtx { void (*fn)(void *, int); void *ctx; int n; volatile LONG next; } ParCtx;
static void par_work(ParCtx *c) {
    for (;;) {
        LONG i = InterlockedIncrement(&c->next) - 1;
        if (i >= c->n) break;
        c->fn(c->ctx, (int)i);
    }
}
static DWORD WINAPI par_thread(LPVOID p) { par_work((ParCtx *)p); return 0; }
static void par_run(int n, void (*fn)(void *, int), void *ctx, int nthreads) {
    ParCtx c = {fn, ctx, n, 0};
    if (n < 8 || nthreads < 2) { par_work(&c); return; }
    if (nthreads > 64) nthreads = 64;
    HANDLE th[64];
    int started = 0;
    for (int t = 0; t < nthreads; t++) {
        th[started] = CreateThread(NULL, 0, par_thread, &c, 0, NULL);
        if (th[started]) started++;
    }
    if (started == 0) { par_work(&c); return; }
    WaitForMultipleObjects((DWORD)started, th, TRUE, INFINITE);
    for (int t = 0; t < started; t++) CloseHandle(th[t]);
}

/* ------------------------------------------------------------------ the schema (pxfdb.md, verbatim) */

static const char SCHEMA[] =
"CREATE TABLE meta (\n"
"    key   TEXT PRIMARY KEY,\n"
"    value TEXT NOT NULL\n"
") WITHOUT ROWID;\n"
"CREATE TABLE glyph (\n"
"    hash   BLOB PRIMARY KEY,\n"
"    width  INTEGER NOT NULL,\n"
"    height INTEGER NOT NULL,\n"
"    pixels TEXT NOT NULL,\n"
"    CHECK (length(hash) = 16 AND width > 0 AND height > 0 AND length(pixels) = width * height)\n"
") WITHOUT ROWID;\n"
"CREATE TABLE pxfm (\n"
"    hash                BLOB PRIMARY KEY,\n"
"    ascent              INTEGER CHECK (ascent >= 0),\n"
"    descent             INTEGER CHECK (descent >= 0),\n"
"    line_gap            INTEGER,\n"
"    cap_height          INTEGER,\n"
"    x_height            INTEGER,\n"
"    internal_leading    INTEGER,\n"
"    quad_width          INTEGER,\n"
"    ratio_x             INTEGER CHECK (ratio_x > 0),\n"
"    ratio_y             INTEGER CHECK (ratio_y > 0),\n"
"    underline_position  INTEGER,\n"
"    underline_thickness INTEGER,\n"
"    strikeout_position  INTEGER,\n"
"    strikeout_thickness INTEGER,\n"
"    monospaced          INTEGER NOT NULL CHECK (monospaced IN (0, 1)),\n"
"    symbolic            INTEGER NOT NULL CHECK (symbolic IN (0, 1)),\n"
"    colors              INTEGER CHECK (colors > 1),\n"
"    height              INTEGER,\n"
"    CHECK (length(hash) = 16),\n"
"    CHECK (height IS ascent + descent),\n"
"    CHECK ((ratio_x IS NULL) = (ratio_y IS NULL))\n"
") WITHOUT ROWID;\n"
"CREATE TABLE palette (\n"
"    hash     BLOB PRIMARY KEY,\n"
"    colors   BLOB NOT NULL,\n"
"    colorset BLOB NOT NULL,\n"
"    CHECK (length(hash) = 16 AND length(colors) > 0 AND length(colors) % 4 = 0 AND length(colorset) % 4 = 0)\n"
") WITHOUT ROWID;\n"
"CREATE TABLE pxfm_glyph (\n"
"    pxfm    BLOB NOT NULL REFERENCES pxfm,\n"
"    code    INTEGER NOT NULL CHECK (code >= 0),\n"
"    x       INTEGER NOT NULL,\n"
"    y       INTEGER NOT NULL,\n"
"    advance INTEGER NOT NULL,\n"
"    glyph   BLOB REFERENCES glyph,\n"
"    palette BLOB REFERENCES palette,\n"
"    PRIMARY KEY (pxfm, code)\n"
") WITHOUT ROWID;\n"
"CREATE TABLE pxfm_kerning (\n"
"    pxfm   BLOB NOT NULL REFERENCES pxfm,\n"
"    first  INTEGER NOT NULL,\n"
"    second INTEGER NOT NULL,\n"
"    adjust INTEGER NOT NULL CHECK (adjust <> 0),\n"
"    PRIMARY KEY (pxfm, first, second)\n"
") WITHOUT ROWID;\n"
"CREATE TABLE pxtf (\n"
"    hash   BLOB PRIMARY KEY,\n"
"    source BLOB NOT NULL,\n"
"    CHECK (length(hash) = 16 AND length(source) = 16)\n"
") WITHOUT ROWID;\n"
"CREATE TABLE pxtf_variant (\n"
"    pxtf   BLOB NOT NULL REFERENCES pxtf,\n"
"    number INTEGER NOT NULL CHECK (number > 0),\n"
"    pxfm   BLOB NOT NULL REFERENCES pxfm,\n"
"    format TEXT NOT NULL,\n"
"    type   TEXT NOT NULL,\n"
"    PRIMARY KEY (pxtf, number)\n"
") WITHOUT ROWID;\n"
"CREATE TABLE pxtf_field (\n"
"    pxtf   BLOB NOT NULL,\n"
"    number INTEGER NOT NULL,\n"
"    key    TEXT NOT NULL,\n"
"    value  TEXT NOT NULL,\n"
"    PRIMARY KEY (pxtf, number, key, value),\n"
"    FOREIGN KEY (pxtf, number) REFERENCES pxtf_variant\n"
") WITHOUT ROWID;\n";

static const char INDEXES[] =
"CREATE INDEX glyph_size ON glyph (width, height);\n"
"CREATE INDEX palette_by_colorset ON palette (colorset);\n"
"CREATE INDEX pxfm_glyph_by_glyph ON pxfm_glyph (glyph, pxfm) WHERE glyph IS NOT NULL;\n"
"CREATE INDEX pxfm_glyph_by_code ON pxfm_glyph (code, glyph, pxfm) WHERE code < 1114112;\n";

/* pxfm columns in the order of PxMetric */
static const char *const METRIC_COL[PXM_COUNT] = {
    "ascent", "descent", "line_gap", "cap_height", "x_height", "internal_leading", "quad_width",
    "underline_position", "underline_thickness", "strikeout_position", "strikeout_thickness"};

static bool exec_sql(sqlite3 *db, const char *sql, char err[PX_ERR_LEN]) {
    char *m = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &m) != SQLITE_OK) {
        fail(err, "sqlite: %s", m ? m : "error");
        sqlite3_free(m);
        return false;
    }
    return true;
}

static int one_int(sqlite3 *db, const char *sql) {
    sqlite3_stmt *st;
    int v = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return v;
}

/* ------------------------------------------------------------------ store and connections */

static volatile LONG g_storeSerial = 0;

static Store *store_new(char err[PX_ERR_LEN]) {
    Store *s = (Store *)calloc(1, sizeof *s);
    if (!s) { fail(err, "out of memory"); return NULL; }
    InitializeSRWLock(&s->lock);
    InitializeCriticalSection(&s->simCs);
    snprintf(s->uri, sizeof s->uri, "file:pxfe_store_%ld_%lu?mode=memory&cache=shared", (long)InterlockedIncrement(&g_storeSerial), (unsigned long)GetCurrentProcessId());
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(s->uri, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI | SQLITE_OPEN_SHAREDCACHE, NULL) != SQLITE_OK) {
        fail(err, "cannot create the in-memory database: %s", db ? sqlite3_errmsg(db) : "?");
        if (db) sqlite3_close(db);
        DeleteCriticalSection(&s->simCs);
        free(s);
        return NULL;
    }
    s->main = (StoreConn *)calloc(1, sizeof *s->main);
    s->main->s = s;
    s->main->db = db;
    s->main->isMain = true;
    return s;
}

StoreConn *store_conn_open(Store *s) {
    StoreConn *c = (StoreConn *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->s = s;
    if (sqlite3_open_v2(s->uri, &c->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI | SQLITE_OPEN_SHAREDCACHE, NULL) != SQLITE_OK) {
        if (c->db) sqlite3_close(c->db);
        free(c);
        return NULL;
    }
    return c;
}

static void conn_finalize(StoreConn *c) {
    for (int i = 0; i < STORE_NSTMT; i++) if (c->st[i]) { sqlite3_finalize(c->st[i]); c->st[i] = NULL; }
}

void store_conn_close(StoreConn *c) {
    if (!c || c->isMain) return;
    conn_finalize(c);
    sqlite3_close(c->db);
    free(c);
}

StoreConn *store_main_conn(Store *s) { return s->main; }
Store *store_of(StoreConn *c) { return c->s; }
sqlite3 *store_sqlite(StoreConn *c) { return c->db; }
const StoreInfo *store_info(const Store *s) { return &s->info; }

void store_close(Store *s) {
    if (!s) return;
    store_sim_free(s);
    conn_finalize(s->main);
    sqlite3_close(s->main->db);
    free(s->main);
    DeleteCriticalSection(&s->simCs);
    free(s);
}

/* the cached prepared statement `id` of a connection */
sqlite3_stmt *store_stmt(StoreConn *c, int id, const char *sql) {
    if (!c->st[id]) {
        if (sqlite3_prepare_v3(c->db, sql, -1, SQLITE_PREPARE_PERSISTENT, &c->st[id], NULL) != SQLITE_OK) { c->st[id] = NULL; return NULL; }
    }
    sqlite3_reset(c->st[id]);
    sqlite3_clear_bindings(c->st[id]);
    return c->st[id];
}

#define RLOCK(c) AcquireSRWLockShared(&(c)->s->lock)
#define RUNLOCK(c) ReleaseSRWLockShared(&(c)->s->lock)

enum { ST_FONT_ROW = 0, ST_FONT_PALS, ST_FONT_GLYPHS, ST_FONT_KERN, ST_GLYPH_PIX, ST_HAS_PXFM, ST_HAS_PXTF, ST_TF_ROW, ST_TF_VARIANTS,
       ST_TF_FIELDS, ST_CODE_GLYPHS, ST_GLYPH_ROW, ST_USAGE, ST_PXFM_GLYPHS_ALL, ST_PALETTE, ST_COUNT_ };
#if STORE_NSTMT < ST_COUNT_
#error "STORE_NSTMT too small"
#endif

/* ------------------------------------------------------------------ the pxgl rows */

static int px_used_mask_char(char c) { int i = px_pixel_index(c); return i; }

/* Fills (w, h, pix) from the canonical pxgl text `t` of n bytes; the text must be canonical and valid.  used: bit i set for pixel index i. */
static bool parse_pxgl(const char *t, size_t n, int *w, int *h, char **pix, uint64_t *used, char *msg, size_t msgSize) {
    int width = -1, rows = 0;
    size_t i = 0;
    uint64_t u = 0;
    if (n == 0) { snprintf(msg, msgSize, "empty"); return false; }
    if (t[n - 1] != '\n') { snprintf(msg, msgSize, "last row not ended with LF"); return false; }
    bool ink = false;
    while (i < n) {
        size_t s = i;
        while (t[i] != '\n') {
            int pi = px_used_mask_char(t[i]);
            if (pi < 0) { snprintf(msg, msgSize, "character 0x%02X is not a pixel (or the file is not in canonical form)", (unsigned char)t[i]); return false; }
            u |= 1ull << pi;
            if (pi != 0) ink = true;
            i++;
        }
        int rw = (int)(i - s);
        i++;
        if (rw == 0) { snprintf(msg, msgSize, "empty row"); return false; }
        if (width < 0) width = rw;
        if (rw != width) { snprintf(msg, msgSize, "rows of unequal width"); return false; }
        rows++;
    }
    if (!ink) { snprintf(msg, msgSize, "no inked pixel"); return false; }
    char *p = (char *)malloc((size_t)width * (size_t)rows);
    if (!p) { snprintf(msg, msgSize, "out of memory"); return false; }
    for (int r = 0; r < rows; r++) memcpy(p + (size_t)r * (size_t)width, t + (size_t)r * (size_t)(width + 1), (size_t)width);
    if (!px_pixels_canonical(p, (size_t)width * (size_t)rows)) { free(p); snprintf(msg, msgSize, "the colours are not numbered in reading order from 0"); return false; }
    *w = width; *h = rows; *pix = p; *used = u | 1ull;
    return true;
}

/* the number of colours of a mask of pixel indexes (bit i: PX_PIXELS[i]; '.' is no colour) */
static int mask_colors(uint64_t used) {
    int n = 0;
    for (int i = 1; i < PX_NUM_PIXELS; i++) if ((used >> i) & 1) n++;
    return n;
}

/* the row of a palette: its colours as RGBA bytes in order, and sorted (a palette has no colour twice) */
static int cmp_rgba(const void *a, const void *b) { return memcmp(a, b, 4); }
static void palette_blobs(const uint32_t *c, int n, uint8_t *colors, uint8_t *colorset) {
    for (int i = 0; i < n; i++) {
        colors[4 * i] = (uint8_t)(c[i] >> 24); colors[4 * i + 1] = (uint8_t)(c[i] >> 16);
        colors[4 * i + 2] = (uint8_t)(c[i] >> 8); colors[4 * i + 3] = (uint8_t)c[i];
    }
    memcpy(colorset, colors, (size_t)n * 4);
    qsort(colorset, (size_t)n, 4, cmp_rgba);
}

/* ------------------------------------------------------------------ inserting rows */

typedef struct InsStmts {
    sqlite3_stmt *glyph, *pxfm, *pal, *pg, *kern, *tf, *var, *fld;
} InsStmts;

static void ins_finalize(InsStmts *s) {
    sqlite3_stmt **p[] = {&s->glyph, &s->pxfm, &s->pal, &s->pg, &s->kern, &s->tf, &s->var, &s->fld};
    for (size_t i = 0; i < sizeof p / sizeof *p; i++) if (*p[i]) { sqlite3_finalize(*p[i]); *p[i] = NULL; }
}

static bool ins_prepare(sqlite3 *db, InsStmts *s, bool orIgnore, char err[PX_ERR_LEN]) {
    memset(s, 0, sizeof *s);
    const char *ins = orIgnore ? "INSERT OR IGNORE INTO " : "INSERT INTO ";
    char sql[900];
    struct { sqlite3_stmt **st; const char *tail; } q[] = {
        {&s->glyph, "glyph VALUES (?,?,?,?)"},
        {&s->pxfm, "pxfm VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"},
        {&s->pal, "palette VALUES (?,?,?)"},
        {&s->pg, "pxfm_glyph VALUES (?,?,?,?,?,?,?)"},
        {&s->kern, "pxfm_kerning VALUES (?,?,?,?)"},
        {&s->tf, "pxtf VALUES (?,?)"},
        {&s->var, "pxtf_variant VALUES (?,?,?,?,?)"},
        {&s->fld, "pxtf_field VALUES (?,?,?,?)"}};
    for (size_t i = 0; i < sizeof q / sizeof *q; i++) {
        snprintf(sql, sizeof sql, "%s%s", ins, q[i].tail);
        if (sqlite3_prepare_v2(db, sql, -1, q[i].st, NULL) != SQLITE_OK) { fail(err, "sqlite: %s", sqlite3_errmsg(db)); ins_finalize(s); return false; }
    }
    return true;
}

/* steps a bound statement; returns false on error (not on a constraint skipped by OR IGNORE) */
static bool step_done(sqlite3_stmt *st, sqlite3 *db, char err[PX_ERR_LEN], const char *what) {
    int rc = sqlite3_step(st);
    sqlite3_reset(st);
    if (rc != SQLITE_DONE) return fail(err, "%s: sqlite: %s", what, sqlite3_errmsg(db));
    return true;
}

static void bind_hex(sqlite3_stmt *st, int i, const char *hex) {
    uint8_t b[16];
    if (store_hex_to_blob(hex, b)) sqlite3_bind_blob(st, i, b, 16, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, i);
}

static bool ins_glyph(sqlite3 *db, InsStmts *s, const uint8_t hash[16], int w, int h, const char *pix, char err[PX_ERR_LEN]) {
    sqlite3_bind_blob(s->glyph, 1, hash, 16, SQLITE_STATIC);
    sqlite3_bind_int(s->glyph, 2, w);
    sqlite3_bind_int(s->glyph, 3, h);
    sqlite3_bind_text(s->glyph, 4, pix, w * h, SQLITE_STATIC);
    return step_done(s->glyph, db, err, "glyph");
}

static bool ins_palette(sqlite3 *db, InsStmts *s, const uint8_t hash[16], const uint32_t *c, int n, char err[PX_ERR_LEN]) {
    uint8_t *b = (uint8_t *)malloc((size_t)n * 8 + 8);
    if (!b) return fail(err, "out of memory");
    palette_blobs(c, n, b, b + 4 * n);
    sqlite3_bind_blob(s->pal, 1, hash, 16, SQLITE_STATIC);
    sqlite3_bind_blob(s->pal, 2, b, 4 * n, SQLITE_TRANSIENT);
    sqlite3_bind_blob(s->pal, 3, b + 4 * n, 4 * n, SQLITE_TRANSIENT);
    free(b);
    return step_done(s->pal, db, err, "palette");
}

/* The rows of a pxfm: `f` is a LAZY font as px_parse_pxfm gives it (glyph and palette hashes, offsets as in the file). */
static bool ins_pxfm(sqlite3 *db, InsStmts *s, const uint8_t hash[16], const PxFont *f, char err[PX_ERR_LEN]) {
    sqlite3_stmt *st = s->pxfm;
    int p = 1;
    sqlite3_bind_blob(st, p++, hash, 16, SQLITE_STATIC);
    for (int m = 0; m < PXM_COUNT; m++) {
        if (m == PXM_UNDERLINEPOSITION) {    /* ratio_x, ratio_y sit before the underline columns */
            if (f->hasRatio) { sqlite3_bind_int(st, p++, f->ratioA); sqlite3_bind_int(st, p++, f->ratioB); }
            else { sqlite3_bind_null(st, p++); sqlite3_bind_null(st, p++); }
        }
        if (f->has[m]) sqlite3_bind_int(st, p++, f->m[m]); else sqlite3_bind_null(st, p++);
    }
    sqlite3_bind_int(st, p++, f->monospaced ? 1 : 0);
    sqlite3_bind_int(st, p++, f->symbolic ? 1 : 0);
    {
        int colors = px_font_colors(f);
        if (colors > 1) sqlite3_bind_int(st, p++, colors); else sqlite3_bind_null(st, p++);
    }
    if (f->has[PXM_ASCENT] && f->has[PXM_DESCENT]) sqlite3_bind_int(st, p++, f->m[PXM_ASCENT] + f->m[PXM_DESCENT]); else sqlite3_bind_null(st, p++);
    if (!step_done(st, db, err, "pxfm")) return false;
    if (sqlite3_changes(db) == 0) return true;      /* OR IGNORE: the font was there */
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        sqlite3_bind_blob(s->pg, 1, hash, 16, SQLITE_STATIC);
        sqlite3_bind_int(s->pg, 2, g->code);
        sqlite3_bind_int(s->pg, 3, g->x);
        sqlite3_bind_int(s->pg, 4, g->y);
        sqlite3_bind_int(s->pg, 5, g->advance);
        if (g->hash[0]) bind_hex(s->pg, 6, g->hash); else sqlite3_bind_null(s->pg, 6);
        if (g->hash[0] && g->pal[0]) bind_hex(s->pg, 7, g->pal); else sqlite3_bind_null(s->pg, 7);
        if (!step_done(s->pg, db, err, "pxfm_glyph")) return false;
    }
    for (int i = 0; i < f->kernCount; i++) {
        sqlite3_bind_blob(s->kern, 1, hash, 16, SQLITE_STATIC);
        sqlite3_bind_int(s->kern, 2, f->kern[i].first);
        sqlite3_bind_int(s->kern, 3, f->kern[i].second);
        sqlite3_bind_int(s->kern, 4, f->kern[i].value);
        if (!step_done(s->kern, db, err, "pxfm_kerning")) return false;
    }
    return true;
}

static bool is_flag_key(const char *k) { return strcmp(k, "ITALIC") == 0 || strcmp(k, "STRUCKOUT") == 0 || strcmp(k, "UNDERLINED") == 0; }

static bool bind_dbtext(sqlite3_stmt *st, int i, const char *modified) {
    int n;
    char *t = to_db_text(modified, &n);
    if (!t) return false;
    sqlite3_bind_text(st, i, t, n, SQLITE_TRANSIENT);
    free(t);
    return true;
}

static bool ins_field(sqlite3 *db, InsStmts *s, const uint8_t th[16], int number, const char *key, const char *valueModified, char err[PX_ERR_LEN]) {
    sqlite3_bind_blob(s->fld, 1, th, 16, SQLITE_STATIC);
    sqlite3_bind_int(s->fld, 2, number);
    sqlite3_bind_text(s->fld, 3, key, -1, SQLITE_STATIC);
    if (!bind_dbtext(s->fld, 4, valueModified)) return fail(err, "out of memory");
    return step_done(s->fld, db, err, "pxtf_field");
}

static bool ins_typeface(sqlite3 *db, InsStmts *s, const PxtfTypeface *tf, const uint8_t th[16], char err[PX_ERR_LEN]) {
    uint8_t src[16];
    if (!store_hex_to_blob(tf->hash, src)) return fail(err, "typeface without a source hash");
    sqlite3_bind_blob(s->tf, 1, th, 16, SQLITE_STATIC);
    sqlite3_bind_blob(s->tf, 2, src, 16, SQLITE_STATIC);
    if (!step_done(s->tf, db, err, "pxtf")) return false;
    if (sqlite3_changes(db) == 0) return true;
    for (int vi = 0; vi < tf->count; vi++) {
        const PxtfVariant *v = &tf->v[vi];
        sqlite3_bind_blob(s->var, 1, th, 16, SQLITE_STATIC);
        sqlite3_bind_int(s->var, 2, v->number);
        bind_hex(s->var, 3, v->pxfm);
        if (!bind_dbtext(s->var, 4, v->format ? v->format : "") || !bind_dbtext(s->var, 5, v->type ? v->type : "")) return fail(err, "out of memory");
        if (!step_done(s->var, db, err, "pxtf_variant")) return false;
        for (int t = 0; t < PXTF_TEXT_COUNT; t++)
            if (v->text[t] && v->text[t][0] && !ins_field(db, s, th, v->number, pxtf_text_key((PxtfText)t), v->text[t], err)) return false;
        if (v->italic && !ins_field(db, s, th, v->number, "ITALIC", "", err)) return false;
        if (v->struckout && !ins_field(db, s, th, v->number, "STRUCKOUT", "", err)) return false;
        if (v->underlined && !ins_field(db, s, th, v->number, "UNDERLINED", "", err)) return false;
        for (int o = 0; o < v->nopts; o++) {
            const char *line = v->opts[o];
            const char *tab = strchr(line, '\t');
            char key[40];
            size_t kl = tab ? (size_t)(tab - line) : strlen(line);
            if (kl >= sizeof key) return fail(err, "key too long");
            memcpy(key, line, kl);
            key[kl] = 0;
            if (!ins_field(db, s, th, v->number, key, tab ? tab + 1 : "", err)) return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ listing the files of a root */

typedef struct HashList { uint8_t (*h)[16]; int n, cap; int bad; char firstBad[300]; } HashList;

static int cmp16(const void *a, const void *b) { return memcmp(a, b, 16); }

typedef struct ListCtx { const char *root; const char *sub; const char *ext; HashList parts[256]; } ListCtx;

static void list_part(void *vctx, int idx) {
    ListCtx *c = (ListCtx *)vctx;
    HashList *hl = &c->parts[idx];
    char dir[1200];
    snprintf(dir, sizeof dir, "%s/%s/%02x/*", c->root, c->sub, idx);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileExA(dir, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    size_t el = strlen(c->ext);
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        size_t nl = strlen(fd.cFileName);
        uint8_t b[16];
        bool ok = nl == 32 + 1 + el && fd.cFileName[32] == '.' && strcmp(fd.cFileName + 33, c->ext) == 0 &&
                  !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        if (ok) {
            char stem[33];
            memcpy(stem, fd.cFileName, 32);
            stem[32] = 0;
            for (int i = 0; i < 32 && ok; i++) { char ch = stem[i]; if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) ok = false; }
            ok = ok && store_hex_to_blob(stem, b) && b[0] == (uint8_t)idx;
        }
        if (!ok) {
            hl->bad++;
            if (!hl->firstBad[0]) snprintf(hl->firstBad, sizeof hl->firstBad, "%s/%s/%02x/%s", c->root, c->sub, idx, fd.cFileName);
            continue;
        }
        if (hl->n == hl->cap) {
            hl->cap = hl->cap ? hl->cap * 2 : 1024;
            hl->h = (uint8_t(*)[16])realloc(hl->h, (size_t)hl->cap * 16);
        }
        memcpy(hl->h[hl->n++], b, 16);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* names under <root>/<sub>/xx; sorted by hash.  bad: number of entries that are not <32 lower-case hex>.<ext> under their prefix */
static void list_names(const char *root, const char *sub, const char *ext, HashList *out) {
    ListCtx *c = (ListCtx *)calloc(1, sizeof *c);
    c->root = root; c->sub = sub; c->ext = ext;
    par_run(256, list_part, c, 16);
    memset(out, 0, sizeof *out);
    int total = 0;
    for (int i = 0; i < 256; i++) total += c->parts[i].n;
    out->h = (uint8_t(*)[16])malloc((size_t)(total ? total : 1) * 16);
    for (int i = 0; i < 256; i++) {            /* prefixes in order: the concatenation is sorted by first byte */
        HashList *p = &c->parts[i];
        if (p->n > 1) qsort(p->h, (size_t)p->n, 16, cmp16);
        if (p->n) memcpy(out->h + out->n, p->h, (size_t)p->n * 16);
        out->n += p->n;
        out->bad += p->bad;
        if (p->bad && !out->firstBad[0]) memcpy(out->firstBad, p->firstBad, sizeof out->firstBad);
        free(p->h);
    }
    out->cap = out->n;
    free(c);
}

static bool in_list(const HashList *l, const uint8_t h[16]) {
    return l->n > 0 && bsearch(h, l->h, (size_t)l->n, 16, cmp16) != NULL;
}

/* ------------------------------------------------------------------ opening databases */

static bool check_meta(sqlite3 *db, char err[PX_ERR_LEN], const char *path) {
    sqlite3_stmt *st;
    char fmt[64] = "", ver[64] = "";
    if (sqlite3_prepare_v2(db, "SELECT key, value FROM meta", -1, &st, NULL) != SQLITE_OK) return fail(err, "%s: not a pxfdb database (%s)", path, sqlite3_errmsg(db));
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *k = (const char *)sqlite3_column_text(st, 0), *v = (const char *)sqlite3_column_text(st, 1);
        if (k && v && strcmp(k, "format") == 0) snprintf(fmt, sizeof fmt, "%s", v);
        if (k && v && strcmp(k, "format_version") == 0) snprintf(ver, sizeof ver, "%s", v);
    }
    sqlite3_finalize(st);
    if (strcmp(fmt, "PXFDB") != 0 || strcmp(ver, "1") != 0) return fail(err, "%s: not a PXFDB database of format version 1", path);
    return true;
}

static bool open_ro(const char *path, sqlite3 **db, char err[PX_ERR_LEN]) {
    if (sqlite3_open_v2(path, db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        fail(err, "%s: cannot open: %s", path, *db ? sqlite3_errmsg(*db) : "?");
        if (*db) sqlite3_close(*db);
        *db = NULL;
        return false;
    }
    return true;
}

static bool backup_into(sqlite3 *dst, sqlite3 *src, char err[PX_ERR_LEN]) {
    sqlite3_backup *b = sqlite3_backup_init(dst, "main", src, "main");
    if (!b) return fail(err, "backup: %s", sqlite3_errmsg(dst));
    int rc = sqlite3_backup_step(b, -1);
    sqlite3_backup_finish(b);
    if (rc != SQLITE_DONE) return fail(err, "backup failed (%d): %s", rc, sqlite3_errmsg(dst));
    return true;
}

static void count_rows(Store *s) {
    sqlite3 *db = s->main->db;
    s->info.nGlyph = one_int(db, "SELECT COUNT(*) FROM glyph");
    s->info.nPxfm = one_int(db, "SELECT COUNT(*) FROM pxfm");
    s->info.nPxtf = one_int(db, "SELECT COUNT(*) FROM pxtf");
}

/* ------------------------------------------------------------------ building from files */

typedef struct GlyphRec { uint8_t hash[16]; int w, h; char *pix; uint64_t used; } GlyphRec;
typedef struct PalRec { uint8_t hash[16]; uint32_t *c; int n; } PalRec;

typedef struct BuildCtx {
    const char *root;
    HashList *lists;            /* [0] glyph [1] pxfm [2] pxtf [3] palette */
    StoreProgress *prog;
    GlyphRec *glyphs;           /* index = list order */
    PxFont *fonts;
    PxtfTypeface *tfs;
    PalRec *pals;               /* index = list order */
    char **errs[4];             /* per file */
    volatile LONG failed;
    sqlite3 *db;                /* Memory: glyphs and fonts the lists do not hold are looked up there (NULL: a fresh build) */
    CRITICAL_SECTION dbCs;      /* the worker threads share `db` for those lookups */
} BuildCtx;

static bool pixels_of_hash(sqlite3 *db, const uint8_t h[16], uint64_t *used);

/* the ink of a glyph row of Memory (the incremental read of the files) */
static bool ext_glyph_used(BuildCtx *b, const uint8_t h[16], uint64_t *used) {
    if (!b->db) return false;
    EnterCriticalSection(&b->dbCs);
    bool r = pixels_of_hash(b->db, h, used);
    LeaveCriticalSection(&b->dbCs);
    return r;
}

static bool ext_has_pxfm(BuildCtx *b, const uint8_t h[16]) {
    if (!b->db) return false;
    bool has = false;
    EnterCriticalSection(&b->dbCs);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(b->db, "SELECT 1 FROM pxfm WHERE hash = ?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_blob(st, 1, h, 16, SQLITE_STATIC);
        has = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    LeaveCriticalSection(&b->dbCs);
    return has;
}

/* the colours of a palette row of Memory (malloc'd 0xRRGGBBAA) */
static bool palette_of_hash(sqlite3 *db, const uint8_t h[16], uint32_t **c, int *n) {
    sqlite3_stmt *st;
    bool ok = false;
    if (sqlite3_prepare_v2(db, "SELECT colors FROM palette WHERE hash = ?", -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_blob(st, 1, h, 16, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const uint8_t *b = (const uint8_t *)sqlite3_column_blob(st, 0);
        int bl = sqlite3_column_bytes(st, 0);
        if (b && bl > 0 && bl % 4 == 0 && (*c = (uint32_t *)malloc((size_t)bl)) != NULL) {
            *n = bl / 4;
            for (int i = 0; i < *n; i++)
                (*c)[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
            ok = true;
        }
    }
    sqlite3_finalize(st);
    return ok;
}

static bool ext_palette(BuildCtx *b, const uint8_t h[16], uint32_t **c, int *n) {
    if (!b->db) return false;
    EnterCriticalSection(&b->dbCs);
    bool r = palette_of_hash(b->db, h, c, n);
    LeaveCriticalSection(&b->dbCs);
    return r;
}

static void set_err(BuildCtx *b, int kind, int i, const char *fmt, ...) {
    char m[PX_ERR_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    b->errs[kind][i] = dup_str(m);
    InterlockedExchange(&b->failed, 1);
}

static void file_path(const BuildCtx *b, const char *sub, const char *ext, const uint8_t h[16], char *out, size_t n) {
    char hex[33];
    store_blob_to_hex(h, hex);
    snprintf(out, n, "%s/%s/%c%c/%s.%s", b->root, sub, hex[0], hex[1], hex, ext);
}

static void tick(BuildCtx *b) { if (b->prog) InterlockedIncrement(&b->prog->done); }

static void work_glyph(void *vb, int i) {
    BuildCtx *b = (BuildCtx *)vb;
    if (b->failed || (b->prog && b->prog->cancel)) return;
    GlyphRec *g = &b->glyphs[i];
    memcpy(g->hash, b->lists[0].h[i], 16);
    char path[1300], msg[200];
    file_path(b, "pxgl", "pxgl", g->hash, path, sizeof path);
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) { set_err(b, 0, i, "%s: cannot read", path); return; }
    char hh[33], name[33];
    px_hash128_hex(raw, n, hh, false);
    store_blob_to_hex(g->hash, name);
    if (strcmp(hh, name) != 0) { set_err(b, 0, i, "%s: the hash of its content is %s (or it is not in canonical form)", path, hh); free(raw); return; }
    if (!parse_pxgl(raw, n, &g->w, &g->h, &g->pix, &g->used, msg, sizeof msg)) set_err(b, 0, i, "%s: %s", path, msg);
    free(raw);
    tick(b);
}

static void work_palette(void *vb, int i) {
    BuildCtx *b = (BuildCtx *)vb;
    if (b->failed || (b->prog && b->prog->cancel)) return;
    PalRec *pr = &b->pals[i];
    memcpy(pr->hash, b->lists[3].h[i], 16);
    char path[1300], err[PX_ERR_LEN];
    file_path(b, "palette", "hex", pr->hash, path, sizeof path);
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) { set_err(b, 3, i, "%s: cannot read", path); return; }
    char hh[33], name[33];
    px_hash128_hex(raw, n, hh, false);
    store_blob_to_hex(pr->hash, name);
    if (strcmp(hh, name) != 0) set_err(b, 3, i, "%s: the hash of its content is %s (or it is not in canonical form)", path, hh);
    else if (!px_hex_parse(raw, n, &pr->c, &pr->n, err)) set_err(b, 3, i, "%s: %s", path, err);
    free(raw);
    tick(b);
}

static const PalRec *find_palette(const BuildCtx *b, const uint8_t h[16]) {
    int lo = 0, hi = b->lists[3].n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        int c = memcmp(b->pals[mid].hash, h, 16);
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return (lo < b->lists[3].n && memcmp(b->pals[lo].hash, h, 16) == 0) ? &b->pals[lo] : NULL;
}

static const GlyphRec *find_glyph(const BuildCtx *b, const uint8_t h[16]) {
    int lo = 0, hi = b->lists[0].n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        int c = memcmp(b->glyphs[mid].hash, h, 16);
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return (lo < b->lists[0].n && memcmp(b->glyphs[lo].hash, h, 16) == 0) ? &b->glyphs[lo] : NULL;
}

static void work_pxfm(void *vb, int i) {
    BuildCtx *b = (BuildCtx *)vb;
    if (b->failed || (b->prog && b->prog->cancel)) return;
    char path[1300], err[PX_ERR_LEN];
    file_path(b, "pxfm", "pxfm", b->lists[1].h[i], path, sizeof path);
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) { set_err(b, 1, i, "%s: cannot read", path); return; }
    PxFont *f = &b->fonts[i];
    char name[33];
    store_blob_to_hex(b->lists[1].h[i], name);
    if (!px_parse_pxfm(raw, n, path, f, err)) { set_err(b, 1, i, "%s", err); free(raw); return; }
    if (strcmp(f->fileHash, name) != 0) set_err(b, 1, i, "%s: the hash of its content is %s", path, f->fileHash);
    else {
        size_t tl;
        char h2[33];
        char *t = px_pxfm_text(f, &tl, h2);
        if (!t || tl != n || memcmp(t, raw, n) != 0) set_err(b, 1, i, "%s: not in canonical form: its lines are not the lines the writer would write", path);
        free(t);
    }
    free(raw);
    if (b->errs[1][i]) return;
    /* the glyph and palette files it names are in the library; each palette has a colour for each colour of its glyph;
     * COLORS is the number of distinct colours over the first k lines of the palettes (pxfm.md) */
    uint32_t *seen = NULL;           /* any number of colours */
    int nseen = 0, seenCap = 0;
    for (int k = 0; k < f->count && !b->errs[1][i]; k++) {
        const PxGlyph *g = &f->glyphs[k];
        if (!g->hash[0]) continue;
        uint8_t hb[16];
        store_hex_to_blob(g->hash, hb);
        uint64_t used = 0;
        const GlyphRec *gr = find_glyph(b, hb);
        if (gr) used = gr->used;
        else if (!ext_glyph_used(b, hb, &used)) { set_err(b, 1, i, "%s: no glyph file %s", path, g->hash); break; }
        int ncol = mask_colors(used);
        if (!g->pal[0]) {
            if (ncol > 1) set_err(b, 1, i, "%s: code %06X: the glyph %s has %d colours but no palette", path, (unsigned)g->code, g->hash, ncol);
            continue;
        }
        uint8_t pb[16];
        store_hex_to_blob(g->pal, pb);
        const PalRec *pr = find_palette(b, pb);
        uint32_t *ec = NULL;
        int en = 0;
        const uint32_t *pc;
        int pn;
        if (pr) { pc = pr->c; pn = pr->n; }
        else if (ext_palette(b, pb, &ec, &en)) { pc = ec; pn = en; }
        else { set_err(b, 1, i, "%s: no palette file %s", path, g->pal); break; }
        if (pn < ncol) set_err(b, 1, i, "%s: code %06X: the palette %s has %d colours, the glyph %d", path, (unsigned)g->code, g->pal, pn, ncol);
        for (int q = 0; q < ncol && q < pn; q++) {
            int z = 0;
            while (z < nseen && seen[z] != pc[q]) z++;
            if (z < nseen) continue;
            if (nseen == seenCap) {
                int nc = seenCap ? seenCap * 2 : 64;
                uint32_t *ns = (uint32_t *)realloc(seen, sizeof(uint32_t) * (size_t)nc);
                if (!ns) break;
                seen = ns;
                seenCap = nc;
            }
            seen[nseen++] = pc[q];
        }
        free(ec);
    }
    free(seen);
    if (b->errs[1][i]) return;
    if (f->colorsRead > 0 && f->colorsRead != nseen)
        set_err(b, 1, i, "%s: COLORS %d, but the glyphs use %d distinct colours of their palettes", path, f->colorsRead, nseen);
    tick(b);
}

static void work_pxtf(void *vb, int i) {
    BuildCtx *b = (BuildCtx *)vb;
    if (b->failed || (b->prog && b->prog->cancel)) return;
    char path[1300], err[PX_ERR_LEN];
    file_path(b, "pxtf", "pxtf", b->lists[2].h[i], path, sizeof path);
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) { set_err(b, 2, i, "%s: cannot read", path); return; }
    PxtfTypeface *t = &b->tfs[i];
    char name[33];
    store_blob_to_hex(b->lists[2].h[i], name);
    if (!pxtf_parse(raw, n, t, err)) { set_err(b, 2, i, "%s: %s", path, err); free(raw); return; }
    char h2[33];
    size_t tl;
    char *txt = pxtf_text(t, &tl, h2);
    if (!txt || tl != n || memcmp(txt, raw, n) != 0 || strcmp(h2, name) != 0) set_err(b, 2, i, "%s: not in canonical form, or the hash of its content is %s", path, h2);
    free(txt);
    free(raw);
    if (b->errs[2][i]) return;
    for (int v = 0; v < t->count; v++) {
        uint8_t hb[16];
        store_hex_to_blob(t->v[v].pxfm, hb);
        if (!in_list(&b->lists[1], hb) && !ext_has_pxfm(b, hb)) { set_err(b, 2, i, "%s: variant %04d names the pxfm %s, which is not in the library", path, t->v[v].number, t->v[v].pxfm); return; }
    }
    tick(b);
}

static void progress_set(StoreProgress *p, long phase, long total) {
    if (!p) return;
    p->phase = phase;
    p->done = 0;
    p->total = total;
}

/* Reads the files of `lists` (pxgl, pxfm, pxtf, palette) with the strict readers and inserts them into Memory.  fresh: Memory is
 * empty and has no schema yet (the schema, meta and indexes are made); else the rows are added to a loaded database
 * (a pxfm may name glyphs, a pxtf fonts, that Memory has). */
static bool build_from_files(Store *s, const char *root, HashList lists[4], bool fresh, StoreProgress *prog, char err[PX_ERR_LEN]) {
    sqlite3 *db = s->main->db;
    BuildCtx b;
    memset(&b, 0, sizeof b);
    b.root = root; b.lists = lists; b.prog = prog;
    b.db = fresh ? NULL : db;
    InitializeCriticalSection(&b.dbCs);
    int ng = lists[0].n, nm = lists[1].n, nt = lists[2].n, np = lists[3].n;
    b.glyphs = (GlyphRec *)calloc((size_t)(ng ? ng : 1), sizeof(GlyphRec));
    b.fonts = (PxFont *)calloc((size_t)(nm ? nm : 1), sizeof(PxFont));
    b.tfs = (PxtfTypeface *)calloc((size_t)(nt ? nt : 1), sizeof(PxtfTypeface));
    b.errs[0] = (char **)calloc((size_t)(ng ? ng : 1), sizeof(char *));
    b.errs[1] = (char **)calloc((size_t)(nm ? nm : 1), sizeof(char *));
    b.errs[2] = (char **)calloc((size_t)(nt ? nt : 1), sizeof(char *));
    b.pals = (PalRec *)calloc((size_t)(np ? np : 1), sizeof(PalRec));
    b.errs[3] = (char **)calloc((size_t)(np ? np : 1), sizeof(char *));
    bool ok = false;
    InsStmts ins;
    memset(&ins, 0, sizeof ins);
    if (!b.glyphs || !b.fonts || !b.tfs || !b.pals || !b.errs[0] || !b.errs[1] || !b.errs[2] || !b.errs[3]) { fail(err, "out of memory"); goto done; }

    progress_set(prog, 3, (long)ng + nm + nt + np);
    par_run(np, work_palette, &b, 16);
    for (int i = 0; i < np; i++) if (b.errs[3][i]) { fail(err, "%s", b.errs[3][i]); goto done; }
    if (prog && prog->cancel) { fail(err, "cancelled"); goto done; }
    par_run(ng, work_glyph, &b, 32);
    for (int i = 0; i < ng; i++) if (b.errs[0][i]) { fail(err, "%s", b.errs[0][i]); goto done; }
    if (prog && prog->cancel) { fail(err, "cancelled"); goto done; }
    par_run(nm, work_pxfm, &b, 16);
    for (int i = 0; i < nm; i++) if (b.errs[1][i]) { fail(err, "%s", b.errs[1][i]); goto done; }
    if (prog && prog->cancel) { fail(err, "cancelled"); goto done; }
    par_run(nt, work_pxtf, &b, 16);
    for (int i = 0; i < nt; i++) if (b.errs[2][i]) { fail(err, "%s", b.errs[2][i]); goto done; }
    if (prog && prog->cancel) { fail(err, "cancelled"); goto done; }

    progress_set(prog, 4, (long)ng + nm + nt + np);
    if (fresh) {
        if (!exec_sql(db, "PRAGMA synchronous = OFF; PRAGMA journal_mode = OFF;", err)) goto done;
        if (!exec_sql(db, SCHEMA, err)) goto done;
        if (!exec_sql(db, "INSERT INTO meta VALUES ('format', 'PXFDB'); INSERT INTO meta VALUES ('format_version', '1');", err)) goto done;
    }
    if (!exec_sql(db, "BEGIN", err)) goto done;
    if (!ins_prepare(db, &ins, false, err)) goto done;
    for (int i = 0; i < ng; i++) {
        if (!ins_glyph(db, &ins, b.glyphs[i].hash, b.glyphs[i].w, b.glyphs[i].h, b.glyphs[i].pix, err)) goto done;
        if (prog) prog->done++;
    }
    for (int i = 0; i < np; i++) {
        if (!ins_palette(db, &ins, b.pals[i].hash, b.pals[i].c, b.pals[i].n, err)) goto done;
        if (prog) prog->done++;
    }
    for (int i = 0; i < nm; i++) {
        if (!ins_pxfm(db, &ins, lists[1].h[i], &b.fonts[i], err)) goto done;
        if (prog) prog->done++;
    }
    for (int i = 0; i < nt; i++) {
        if (!ins_typeface(db, &ins, &b.tfs[i], lists[2].h[i], err)) goto done;
        if (prog) prog->done++;
    }
    ins_finalize(&ins);
    if (!exec_sql(db, "COMMIT", err)) goto done;
    if (fresh) {
        progress_set(prog, 5, 2);
        if (!exec_sql(db, INDEXES, err)) goto done;
        if (prog) prog->done = 1;
        if (!exec_sql(db, "ANALYZE", err)) goto done;
    }
    ok = true;
done:
    ins_finalize(&ins);
    if (!ok && db) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    for (int i = 0; i < ng && b.glyphs; i++) free(b.glyphs[i].pix);
    for (int i = 0; i < nm && b.fonts; i++) px_font_free(&b.fonts[i]);
    for (int i = 0; i < nt && b.tfs; i++) pxtf_free(&b.tfs[i]);
    for (int i = 0; i < np && b.pals; i++) free(b.pals[i].c);
    for (int k = 0; k < 4; k++) {
        int cnt = k == 0 ? ng : k == 1 ? nm : k == 2 ? nt : np;
        for (int i = 0; i < cnt && b.errs[k]; i++) free(b.errs[k][i]);
        free(b.errs[k]);
    }
    free(b.glyphs); free(b.fonts); free(b.tfs); free(b.pals);
    DeleteCriticalSection(&b.dbCs);
    return ok;
}

/* ------------------------------------------------------------------ writing the database file */

static void dir_of(const char *path, char *out, size_t n);

/* VACUUM INTO a temporary file in the folder of `path`, then replace `path` atomically (write-through).  The old file is untouched
 * when anything fails. */
static bool save_db_file(sqlite3 *mem, const char *path, char err[PX_ERR_LEN]) {
    char tmp[1110];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    DeleteFileA(tmp);
    char dir[1100];
    dir_of(path, dir, sizeof dir);
    px_mkdir_p(dir);
    char sql[2400];
    size_t o = (size_t)snprintf(sql, sizeof sql, "VACUUM INTO '");
    for (const char *p = tmp; *p && o + 4 < sizeof sql; p++) { if (*p == 0x27) sql[o++] = 0x27; sql[o++] = *p; }
    sql[o++] = 0x27; sql[o] = 0;
    if (!exec_sql(mem, sql, err)) { DeleteFileA(tmp); return false; }
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD le = GetLastError();
        DeleteFileA(tmp);
        return fail(err, "%s: cannot replace the file (error %lu); the old file is unchanged", path, (unsigned long)le);
    }
    return true;
}

bool store_write_pxfdb(Store *s, const char *path, char err[PX_ERR_LEN]) {
    AcquireSRWLockShared(&s->lock);
    bool ok = save_db_file(s->main->db, path, err);
    ReleaseSRWLockShared(&s->lock);
    return ok;
}

bool store_save(Store *s, char err[PX_ERR_LEN]) {
    if (!s->info.path[0]) return fail(err, "no database file is chosen: use Save As");
    LONG g = s->gen;
    if (!store_write_pxfdb(s, s->info.path, err)) return false;
    s->savedGen = g;
    return true;
}

bool store_save_as(Store *s, const char *path, char err[PX_ERR_LEN]) {
    LONG g = s->gen;
    if (!store_write_pxfdb(s, path, err)) return false;
    snprintf(s->info.path, sizeof s->info.path, "%s", path);
    s->savedGen = g;
    return true;
}

bool store_modified(const Store *s) { return s->gen != s->savedGen; }

/* ------------------------------------------------------------------ lists */

static const char VARIANT_SQL[] =
    "SELECT v.pxtf, v.number, v.pxfm, v.format,"
    " (SELECT value FROM pxtf_field f WHERE f.pxtf = v.pxtf AND f.number = v.number AND f.key = 'FAMILY'),"
    " (SELECT value FROM pxtf_field f WHERE f.pxtf = v.pxtf AND f.number = v.number AND f.key = 'STYLE'),"
    " (SELECT value FROM pxtf_field f WHERE f.pxtf = v.pxtf AND f.number = v.number AND f.key = 'FULLNAME'),"
    " (SELECT value FROM pxtf_field f WHERE f.pxtf = v.pxtf AND f.number = v.number AND f.key = 'DESIGNER'),"
    " EXISTS (SELECT 1 FROM pxtf_field f WHERE f.pxtf = v.pxtf AND f.number = v.number AND f.key = 'ITALIC'),"
    " EXISTS (SELECT 1 FROM pxtf_field f WHERE f.pxtf = v.pxtf AND f.number = v.number AND f.key = 'STRUCKOUT'),"
    " EXISTS (SELECT 1 FROM pxtf_field f WHERE f.pxtf = v.pxtf AND f.number = v.number AND f.key = 'UNDERLINED')"
    " FROM pxtf_variant v ORDER BY v.pxtf, v.number";

static const char FONT_SQL[] =
    "SELECT hash, ascent, descent, height, monospaced, ratio_x, ratio_y,"
    " (SELECT COUNT(*) FROM pxfm_glyph g WHERE g.pxfm = m.hash AND g.code <> 16777215),"
    " EXISTS (SELECT 1 FROM pxfm_glyph g WHERE g.pxfm = m.hash AND g.code = 16777215),"
    " (SELECT COUNT(*) FROM pxtf_variant v WHERE v.pxfm = m.hash),"
    " colors, symbolic"
    " FROM pxfm m ORDER BY hash";

typedef struct FontFacts {
    uint8_t hash[16];
    bool hasAscent, hasDescent; int ascent, descent;
    bool hasHeight; int height;
    bool monospaced, symbolic;
    bool hasRatio; int ratioA, ratioB;
    int glyphCount; bool hasDefault; int nVariants; int colors;
} FontFacts;

static bool read_facts(sqlite3 *db, FontFacts **out, int *n, char err[PX_ERR_LEN]) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, FONT_SQL, -1, &st, NULL) != SQLITE_OK) return fail(err, "sqlite: %s", sqlite3_errmsg(db));
    int cap = 2048, cnt = 0;
    FontFacts *v = (FontFacts *)malloc(sizeof(FontFacts) * (size_t)cap);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) { cap *= 2; v = (FontFacts *)realloc(v, sizeof(FontFacts) * (size_t)cap); }
        FontFacts *f = &v[cnt++];
        memset(f, 0, sizeof *f);
        memcpy(f->hash, sqlite3_column_blob(st, 0), 16);
        f->hasAscent = sqlite3_column_type(st, 1) != SQLITE_NULL; f->ascent = sqlite3_column_int(st, 1);
        f->hasDescent = sqlite3_column_type(st, 2) != SQLITE_NULL; f->descent = sqlite3_column_int(st, 2);
        f->hasHeight = sqlite3_column_type(st, 3) != SQLITE_NULL; f->height = sqlite3_column_int(st, 3);
        f->monospaced = sqlite3_column_int(st, 4) != 0;
        f->hasRatio = sqlite3_column_type(st, 5) != SQLITE_NULL; f->ratioA = sqlite3_column_int(st, 5); f->ratioB = sqlite3_column_int(st, 6);
        f->glyphCount = sqlite3_column_int(st, 7);
        f->hasDefault = sqlite3_column_int(st, 8) != 0;
        f->nVariants = sqlite3_column_int(st, 9);
        f->colors = sqlite3_column_int(st, 10) > 1 ? sqlite3_column_int(st, 10) : 1;     /* NULL: one colour */
        f->symbolic = sqlite3_column_int(st, 11) != 0;
    }
    sqlite3_finalize(st);
    *out = v; *n = cnt;
    return true;
}

static void apply_facts(StoreEntry *e, const FontFacts *f) {
    e->hasAscent = f->hasAscent; e->ascent = f->ascent; e->hasDescent = f->hasDescent; e->descent = f->descent;
    e->hasHeight = f->hasHeight; e->height = f->height; e->monospaced = f->monospaced; e->symbolic = f->symbolic;
    e->hasRatio = f->hasRatio; e->ratioA = f->ratioA; e->ratioB = f->ratioB;
    e->glyphCount = f->glyphCount; e->hasDefault = f->hasDefault; e->nVariants = f->nVariants;
    e->colors = f->colors;
}

static int cmp_facts_key(const void *k, const void *e) { return memcmp(k, ((const FontFacts *)e)->hash, 16); }

static const FontFacts *find_facts(const FontFacts *v, int n, const uint8_t h[16]) {
    return (const FontFacts *)bsearch(h, v, (size_t)n, sizeof(FontFacts), cmp_facts_key);
}

static const char *col_text(sqlite3_stmt *st, int i, Arena **a) {
    const void *p = sqlite3_column_blob(st, i);
    int n = sqlite3_column_bytes(st, i);
    if (!p) return "";
    /* strings with NUL are cut at it (display metadata) */
    return arena_str(a, (const char *)p, (int)strnlen((const char *)p, (size_t)n));
}

/* all variants, with the facts of their fonts; list owns the arena */
static bool list_variants(sqlite3 *db, StoreEntryList *out, FontFacts *facts, int nfacts, char err[PX_ERR_LEN]) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, VARIANT_SQL, -1, &st, NULL) != SQLITE_OK) return fail(err, "sqlite: %s", sqlite3_errmsg(db));
    int cap = 2048, cnt = 0;
    StoreEntry *v = (StoreEntry *)calloc((size_t)cap, sizeof(StoreEntry));
    Arena *a = (Arena *)out->arena;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (cnt == cap) { cap *= 2; v = (StoreEntry *)realloc(v, sizeof(StoreEntry) * (size_t)cap); memset(v + cnt, 0, sizeof(StoreEntry) * (size_t)(cap - cnt)); }
        StoreEntry *e = &v[cnt++];
        memset(e, 0, sizeof *e);
        store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 0), e->pxtf);
        e->variant = sqlite3_column_int(st, 1);
        const uint8_t *pb = (const uint8_t *)sqlite3_column_blob(st, 2);
        store_blob_to_hex(pb, e->pxfm);
        e->format = col_text(st, 3, &a);
        e->family = col_text(st, 4, &a);
        e->style = col_text(st, 5, &a);
        e->fullname = col_text(st, 6, &a);
        e->designer = col_text(st, 7, &a);
        e->italic = sqlite3_column_int(st, 8) != 0;
        e->struckout = sqlite3_column_int(st, 9) != 0;
        e->underlined = sqlite3_column_int(st, 10) != 0;
        const FontFacts *f = find_facts(facts, nfacts, pb);
        if (f) apply_facts(e, f);
    }
    sqlite3_finalize(st);
    out->arena = a;
    out->e = v;
    out->count = cnt;
    return true;
}

void store_entry_list_free(StoreEntryList *l) {
    if (!l) return;
    free(l->e);
    arena_free(l->arena);
    memset(l, 0, sizeof *l);
}

bool store_list_typefaces(StoreConn *c, StoreEntryList *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    RLOCK(c);
    FontFacts *facts;
    int nf;
    bool ok = read_facts(c->db, &facts, &nf, err) && list_variants(c->db, out, facts, nf, err);
    free(facts);
    RUNLOCK(c);
    if (!ok) store_entry_list_free(out);
    return ok;
}

static int cmp_entry_pxfm(const void *a, const void *b) {
    const StoreEntry *x = (const StoreEntry *)a, *y = (const StoreEntry *)b;
    int r = strcmp(x->pxfm, y->pxfm);
    if (r) return r;
    r = strcmp(x->pxtf, y->pxtf);
    return r ? r : x->variant - y->variant;
}

/* fonts list; `only` (sorted hashes, may be NULL) limits it */
static bool list_fonts_nolock(StoreConn *c, const uint8_t (*only)[16], int nonly, StoreEntryList *out, char err[PX_ERR_LEN]) {
    FontFacts *facts = NULL;
    int nf = 0;
    StoreEntryList vars;
    memset(&vars, 0, sizeof vars);
    if (!read_facts(c->db, &facts, &nf, err)) return false;
    if (!list_variants(c->db, &vars, facts, nf, err)) { free(facts); return false; }
    qsort(vars.e, (size_t)vars.count, sizeof(StoreEntry), cmp_entry_pxfm);
    StoreEntry *res = (StoreEntry *)calloc((size_t)(nf ? nf : 1), sizeof(StoreEntry));
    int n = 0;
    for (int i = 0; i < nf; i++) {
        const FontFacts *f = &facts[i];
        if (only && !(nonly > 0 && bsearch(f->hash, only, (size_t)nonly, 16, cmp16))) continue;
        StoreEntry *e = &res[n++];
        store_blob_to_hex(f->hash, e->pxfm);
        apply_facts(e, f);
        e->family = e->style = e->fullname = e->designer = e->format = "";
        int lo = 0, hi = vars.count;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (strcmp(vars.e[mid].pxfm, e->pxfm) < 0) lo = mid + 1; else hi = mid;
        }
        if (lo < vars.count && strcmp(vars.e[lo].pxfm, e->pxfm) == 0) {
            const StoreEntry *v = &vars.e[lo];
            memcpy(e->pxtf, v->pxtf, 33);
            e->variant = v->variant;
            e->family = v->family; e->style = v->style; e->fullname = v->fullname; e->designer = v->designer; e->format = v->format;
            e->italic = v->italic; e->struckout = v->struckout; e->underlined = v->underlined;
        }
    }
    free(vars.e);
    free(facts);
    out->e = res;
    out->count = n;
    out->arena = vars.arena;
    return true;
}

bool store_list_fonts(StoreConn *c, StoreEntryList *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    RLOCK(c);
    bool ok = list_fonts_nolock(c, NULL, 0, out, err);
    RUNLOCK(c);
    return ok;
}

static bool has_hash(StoreConn *c, int id, const char *sql, const char *hex) {
    uint8_t b[16];
    if (!store_hex_to_blob(hex, b)) return false;
    RLOCK(c);
    sqlite3_stmt *st = store_stmt(c, id, sql);
    bool r = false;
    if (st) {
        sqlite3_bind_blob(st, 1, b, 16, SQLITE_STATIC);
        r = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_reset(st);
    }
    RUNLOCK(c);
    return r;
}

bool store_has_pxfm(StoreConn *c, const char *h) { return has_hash(c, ST_HAS_PXFM, "SELECT 1 FROM pxfm WHERE hash = ?", h); }
bool store_has_pxtf(StoreConn *c, const char *h) { return has_hash(c, ST_HAS_PXTF, "SELECT 1 FROM pxtf WHERE hash = ?", h); }

/* ------------------------------------------------------------------ rows -> model */

static bool tf_add_opt(PxtfVariant *v, const char *line) {
    char **nv = (char **)realloc(v->opts, sizeof(char *) * (size_t)(v->nopts + 1));
    if (!nv) return false;
    v->opts = nv;
    v->opts[v->nopts] = dup_str(line);
    if (!v->opts[v->nopts]) return false;
    v->nopts++;
    return true;
}

static bool load_typeface_nolock(StoreConn *c, const uint8_t th[16], PxtfTypeface *out, char err[PX_ERR_LEN]) {
    pxtf_init(out);
    sqlite3_stmt *st = store_stmt(c, ST_TF_ROW, "SELECT source FROM pxtf WHERE hash = ?");
    if (!st) return fail(err, "sqlite: %s", sqlite3_errmsg(c->db));
    sqlite3_bind_blob(st, 1, th, 16, SQLITE_STATIC);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_reset(st); return fail(err, "no such typeface"); }
    store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 0), out->hash);
    sqlite3_reset(st);
    st = store_stmt(c, ST_TF_VARIANTS, "SELECT number, pxfm, format, type FROM pxtf_variant WHERE pxtf = ? ORDER BY number");
    sqlite3_bind_blob(st, 1, th, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (out->count == out->cap) {
            int nc = out->cap ? out->cap * 2 : 4;
            PxtfVariant *nv = (PxtfVariant *)realloc(out->v, sizeof(PxtfVariant) * (size_t)nc);
            if (!nv) { sqlite3_reset(st); pxtf_free(out); return fail(err, "out of memory"); }
            out->v = nv;
            out->cap = nc;
        }
        PxtfVariant *v = &out->v[out->count++];
        memset(v, 0, sizeof *v);
        v->number = sqlite3_column_int(st, 0);
        store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 1), v->pxfm);
        v->format = from_db_text(sqlite3_column_blob(st, 2), sqlite3_column_bytes(st, 2));
        v->type = from_db_text(sqlite3_column_blob(st, 3), sqlite3_column_bytes(st, 3));
    }
    sqlite3_reset(st);
    st = store_stmt(c, ST_TF_FIELDS, "SELECT number, key, value FROM pxtf_field WHERE pxtf = ? ORDER BY number, key, value");
    sqlite3_bind_blob(st, 1, th, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int number = sqlite3_column_int(st, 0);
        const char *key = (const char *)sqlite3_column_text(st, 1);
        PxtfVariant *v = pxtf_find_variant(out, number);
        if (!v || !key) continue;
        char *val = from_db_text(sqlite3_column_blob(st, 2), sqlite3_column_bytes(st, 2));
        int ti = -1;
        for (int t = 0; t < PXTF_TEXT_COUNT; t++) if (strcmp(key, pxtf_text_key((PxtfText)t)) == 0) ti = t;
        if (ti >= 0) { free(v->text[ti]); v->text[ti] = val; continue; }
        if (is_flag_key(key)) {
            if (key[0] == 'I') v->italic = true; else if (key[0] == 'S') v->struckout = true; else v->underlined = true;
            free(val);
            continue;
        }
        size_t l = strlen(key) + 1 + strlen(val ? val : "") + 1;
        char *line = (char *)malloc(l);
        snprintf(line, l, "%s\t%s", key, val ? val : "");
        tf_add_opt(v, line);
        free(line);
        free(val);
    }
    sqlite3_reset(st);
    return true;
}

bool store_load_typeface(StoreConn *c, const char *hex, PxtfTypeface *out, char err[PX_ERR_LEN]) {
    uint8_t th[16];
    if (!store_hex_to_blob(hex, th)) return fail(err, "'%s' is not a hash", hex);
    RLOCK(c);
    bool ok = load_typeface_nolock(c, th, out, err);
    RUNLOCK(c);
    return ok;
}

static bool glyph_cropped(const char *pix, int w, int h) {
    bool top = false, bottom = false, left = false, right = false;
    for (int x = 0; x < w; x++) { if (pix[x] != '.') top = true; if (pix[(size_t)(h - 1) * (size_t)w + (size_t)x] != '.') bottom = true; }
    for (int y = 0; y < h; y++) { if (pix[(size_t)y * (size_t)w] != '.') left = true; if (pix[(size_t)y * (size_t)w + (size_t)w - 1] != '.') right = true; }
    return top && bottom && left && right;
}

static bool load_bitmaps_nolock(StoreConn *c, PxFont *f, const int32_t *codes, int n, char err[PX_ERR_LEN]) {
    int32_t *want = NULL;
    if (codes) {
        want = (int32_t *)malloc(sizeof(int32_t) * (size_t)(n > 0 ? n : 1));
        memcpy(want, codes, sizeof(int32_t) * (size_t)n);
        /* sorted for bsearch */
        for (int i = 1; i < n; i++) { int32_t v = want[i]; int j = i - 1; while (j >= 0 && want[j] > v) { want[j + 1] = want[j]; j--; } want[j + 1] = v; }
    }
    bool ok = true;
    sqlite3_stmt *st = store_stmt(c, ST_GLYPH_PIX, "SELECT width, height, pixels FROM glyph WHERE hash = ?");
    if (!st) { free(want); return fail(err, "sqlite: %s", sqlite3_errmsg(c->db)); }
    for (int i = 0; i < f->count && ok; i++) {
        PxGlyph *g = &f->glyphs[i];
        if (!g->lazy) continue;
        if (want) {
            int lo = 0, hi = n;
            while (lo < hi) { int mid = (lo + hi) / 2; if (want[mid] < g->code) lo = mid + 1; else hi = mid; }
            if (lo >= n || want[lo] != g->code) continue;
        }
        uint8_t hb[16];
        store_hex_to_blob(g->hash, hb);
        sqlite3_reset(st);
        sqlite3_bind_blob(st, 1, hb, 16, SQLITE_STATIC);
        if (sqlite3_step(st) != SQLITE_ROW) { ok = fail(err, "code %06X: no glyph %s in the store", (unsigned)g->code, g->hash); break; }
        int w = sqlite3_column_int(st, 0), h = sqlite3_column_int(st, 1);
        const char *px = (const char *)sqlite3_column_blob(st, 2);
        if (!px || sqlite3_column_bytes(st, 2) != w * h) { ok = fail(err, "glyph %s is damaged", g->hash); break; }
        g->pix = (char *)malloc((size_t)w * (size_t)h);
        if (!g->pix) { ok = fail(err, "out of memory"); break; }
        memcpy(g->pix, px, (size_t)w * (size_t)h);
        g->w = w; g->h = h;
        g->asDrawn = !glyph_cropped(g->pix, w, h);
        g->lazy = false;
        g->hashValid = true;
        g->cdx = g->cdy = 0;
        if (!px_glyph_from_canonical(f, g, err)) { ok = false; break; }
    }
    sqlite3_reset(st);
    free(want);
    return ok && px_font_finish_palette(f, err);
}

bool store_load_glyph_bitmaps(StoreConn *c, PxFont *f, const int32_t *codes, int n, char err[PX_ERR_LEN]) {
    RLOCK(c);
    bool ok = load_bitmaps_nolock(c, f, codes, n, err);
    RUNLOCK(c);
    return ok;
}

static bool load_font_nolock(StoreConn *c, const uint8_t fh[16], const char *hex, const int32_t *codes, int ncodes, PxFont *out, char err[PX_ERR_LEN]) {
    char sql[700];
    int p = 0;
    p += snprintf(sql + p, sizeof sql - (size_t)p, "SELECT ");
    for (int m = 0; m < PXM_COUNT; m++) p += snprintf(sql + p, sizeof sql - (size_t)p, "%s, ", METRIC_COL[m]);
    snprintf(sql + p, sizeof sql - (size_t)p, "ratio_x, ratio_y, monospaced, symbolic, colors FROM pxfm WHERE hash = ?");
    sqlite3_stmt *st = store_stmt(c, ST_FONT_ROW, sql);
    if (!st) return fail(err, "sqlite: %s", sqlite3_errmsg(c->db));
    sqlite3_bind_blob(st, 1, fh, 16, SQLITE_STATIC);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_reset(st); return fail(err, "pxfm %s is not in the library", hex); }
    PxFont f;
    px_font_init(&f);
    for (int m = 0; m < PXM_COUNT; m++)
        if (sqlite3_column_type(st, m) != SQLITE_NULL) { f.has[m] = true; f.m[m] = sqlite3_column_int(st, m); }
    if (sqlite3_column_type(st, PXM_COUNT) != SQLITE_NULL) { f.hasRatio = true; f.ratioA = sqlite3_column_int(st, PXM_COUNT); f.ratioB = sqlite3_column_int(st, PXM_COUNT + 1); }
    f.monospaced = sqlite3_column_int(st, PXM_COUNT + 2) != 0;
    f.symbolic = sqlite3_column_int(st, PXM_COUNT + 3) != 0;
    f.colorsRead = sqlite3_column_type(st, PXM_COUNT + 4) != SQLITE_NULL ? sqlite3_column_int(st, PXM_COUNT + 4) : 0;
    sqlite3_reset(st);
    snprintf(f.fileHash, sizeof f.fileHash, "%s", hex);

    st = store_stmt(c, ST_FONT_GLYPHS, "SELECT code, x, y, advance, glyph, palette FROM pxfm_glyph WHERE pxfm = ? ORDER BY code");
    sqlite3_bind_blob(st, 1, fh, 16, SQLITE_STATIC);
    int cap = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        int32_t code = sqlite3_column_int(st, 0);
        PxGlyph g;
        memset(&g, 0, sizeof g);
        g.x = sqlite3_column_int(st, 1);
        g.y = sqlite3_column_int(st, 2);
        g.advance = sqlite3_column_int(st, 3);
        if (sqlite3_column_type(st, 4) != SQLITE_NULL) {
            store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 4), g.hash);
            g.lazy = true;
            if (sqlite3_column_bytes(st, 5) == 16) store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 5), g.pal);
        }
        g.hashValid = true;
        g.code = code;
        if (f.count == cap) {
            cap = cap ? cap * 2 : 64;
            PxGlyph *ng = (PxGlyph *)realloc(f.glyphs, sizeof(PxGlyph) * (size_t)cap);
            if (!ng) { sqlite3_reset(st); px_font_free(&f); return fail(err, "out of memory"); }
            f.glyphs = ng;
            f.capacity = cap;
        }
        f.glyphs[f.count++] = g;
    }
    sqlite3_reset(st);
    st = store_stmt(c, ST_FONT_KERN, "SELECT first, second, adjust FROM pxfm_kerning WHERE pxfm = ? ORDER BY first, second");
    sqlite3_bind_blob(st, 1, fh, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {     /* (pxfm, first, second) is the key: no pair twice, so append */
        if (f.kernCount == f.kernCap) {
            int nc = f.kernCap ? f.kernCap * 2 : 64;
            PxKern *nk = (PxKern *)realloc(f.kern, sizeof(PxKern) * (size_t)nc);
            if (!nk) { sqlite3_reset(st); px_font_free(&f); return fail(err, "out of memory"); }
            f.kern = nk;
            f.kernCap = nc;
        }
        f.kern[f.kernCount].first = sqlite3_column_int(st, 0);
        f.kern[f.kernCount].second = sqlite3_column_int(st, 1);
        f.kern[f.kernCount].value = sqlite3_column_int(st, 2);
        f.kernCount++;
    }
    sqlite3_reset(st);
    if (px_font_has_glyph_palettes(&f)) {
        /* a colour font: its palettes into the font's table, then every bitmap */
        st = store_stmt(c, ST_PALETTE, "SELECT colors FROM palette WHERE hash = ?");
        for (int i = 0; i < f.count; i++) {
            const PxGlyph *g = &f.glyphs[i];
            if (!g->pal[0] || px_font_find_palette(&f, g->pal)) continue;
            uint8_t pb[16];
            store_hex_to_blob(g->pal, pb);
            sqlite3_reset(st);
            sqlite3_bind_blob(st, 1, pb, 16, SQLITE_STATIC);
            const uint8_t *b = sqlite3_step(st) == SQLITE_ROW ? (const uint8_t *)sqlite3_column_blob(st, 0) : NULL;
            int bl = b ? sqlite3_column_bytes(st, 0) : 0;
            if (!b || bl <= 0 || bl % 4 || bl > 4 * 4096) {
                sqlite3_reset(st); px_font_free(&f);
                return fail(err, "code %06X: no palette %s in the store", (unsigned)g->code, g->pal);
            }
            uint32_t cv[4096];
            for (int k = 0; k < bl / 4; k++) cv[k] = (uint32_t)b[4 * k] << 24 | (uint32_t)b[4 * k + 1] << 16 | (uint32_t)b[4 * k + 2] << 8 | b[4 * k + 3];
            if (px_font_add_palette(&f, g->pal, cv, bl / 4) <= 0) { sqlite3_reset(st); px_font_free(&f); return fail(err, "out of memory"); }
        }
        sqlite3_reset(st);
        codes = NULL;
        ncodes = 0;
    }
    if (!px_font_adopt_palettes(&f, err) || !load_bitmaps_nolock(c, &f, codes, ncodes, err)) { px_font_free(&f); return false; }
    *out = f;
    return true;
}

bool store_load_font(StoreConn *c, const char *hex, const int32_t *codes, int ncodes, PxFont *out, char err[PX_ERR_LEN]) {
    uint8_t fh[16];
    if (!store_hex_to_blob(hex, fh)) return fail(err, "'%s' is not a hash", hex);
    char low[33];
    store_blob_to_hex(fh, low);
    RLOCK(c);
    bool ok = load_font_nolock(c, fh, low, codes, ncodes, out, err);
    RUNLOCK(c);
    return ok;
}

/* ------------------------------------------------------------------ rows -> text (pxfdb.md, independent of PxFont) */

typedef struct SBuf { char *p; size_t n, cap; } SBuf;
static void sb_add(SBuf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 1024;
        while (nc < b->n + n + 1) nc *= 2;
        b->p = (char *)realloc(b->p, nc);
        b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

typedef struct LineSet { char **v; int n, cap; } LineSet;
static void ls_add(LineSet *l, const char *s, size_t n) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->v = (char **)realloc(l->v, sizeof(char *) * (size_t)l->cap); }
    char *d = (char *)malloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    l->v[l->n++] = d;
}
static void ls_addf(LineSet *l, const char *fmt, ...) {
    char t[512];
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (n < (int)sizeof t) { ls_add(l, t, (size_t)n); va_end(ap2); return; }
    char *big = (char *)malloc((size_t)n + 1);
    vsnprintf(big, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    ls_add(l, big, (size_t)n);
    free(big);
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void ls_free(LineSet *l) { for (int i = 0; i < l->n; i++) free(l->v[i]); free(l->v); memset(l, 0, sizeof *l); }

/* sorted, each once, LF-ended; optional leading prefix for each line */
static void ls_emit(LineSet *l, SBuf *out, const char *prefix) {
    qsort(l->v, (size_t)l->n, sizeof(char *), cmp_str);
    for (int i = 0; i < l->n; i++) {
        if (i > 0 && strcmp(l->v[i], l->v[i - 1]) == 0) continue;
        if (prefix) sb_add(out, prefix, strlen(prefix));
        sb_add(out, l->v[i], strlen(l->v[i]));
        sb_add(out, "\n", 1);
    }
}

static const char *const METRIC_KEYS_FILE[PXM_COUNT] = {
    "ASCENT", "DESCENT", "LINEGAP", "CAPHEIGHT", "XHEIGHT", "INTERNALLEADING", "QUADWIDTH",
    "UNDERLINEPOSITION", "UNDERLINETHICKNESS", "STRIKEOUTPOSITION", "STRIKEOUTTHICKNESS"};

char *store_pxfm_text(StoreConn *c, const char *hex, size_t *len, char hashOut[33]) {
    uint8_t fh[16];
    if (!store_hex_to_blob(hex, fh)) return NULL;
    RLOCK(c);
    LineSet L;
    memset(&L, 0, sizeof L);
    char sql[600];
    int p = snprintf(sql, sizeof sql, "SELECT ");
    for (int m = 0; m < PXM_COUNT; m++) p += snprintf(sql + p, sizeof sql - (size_t)p, "%s, ", METRIC_COL[m]);
    snprintf(sql + p, sizeof sql - (size_t)p, "ratio_x, ratio_y, monospaced, symbolic, colors FROM pxfm WHERE hash = ?");
    sqlite3_stmt *st = store_stmt(c, ST_FONT_ROW, sql);
    bool found = false;
    if (st) {
        sqlite3_bind_blob(st, 1, fh, 16, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            found = true;
            for (int m = 0; m < PXM_COUNT; m++)
                if (sqlite3_column_type(st, m) != SQLITE_NULL) ls_addf(&L, "%s\t%d", METRIC_KEYS_FILE[m], sqlite3_column_int(st, m));
            if (sqlite3_column_type(st, PXM_COUNT) != SQLITE_NULL) ls_addf(&L, "RATIO\t%d:%d", sqlite3_column_int(st, PXM_COUNT), sqlite3_column_int(st, PXM_COUNT + 1));
            if (sqlite3_column_int(st, PXM_COUNT + 2)) ls_add(&L, "MONOSPACED", 10);
            if (sqlite3_column_int(st, PXM_COUNT + 3)) ls_add(&L, "SYMBOLIC", 8);
            if (sqlite3_column_type(st, PXM_COUNT + 4) != SQLITE_NULL) ls_addf(&L, "COLORS\t%d", sqlite3_column_int(st, PXM_COUNT + 4));
        }
        sqlite3_reset(st);
    }
    if (!found) { RUNLOCK(c); ls_free(&L); return NULL; }
    st = store_stmt(c, ST_PXFM_GLYPHS_ALL, "SELECT code, x, y, advance, glyph, palette FROM pxfm_glyph WHERE pxfm = ?");
    sqlite3_bind_blob(st, 1, fh, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        char h[33], up[33];
        if (sqlite3_column_type(st, 4) != SQLITE_NULL) {
            store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 4), h);
            for (int i = 0; i < 33; i++) up[i] = (char)toupper((unsigned char)h[i]);
            char ph[33] = "", pu[34] = "";
            if (sqlite3_column_bytes(st, 5) == 16) {
                store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 5), ph);
                pu[0] = '\t';
                for (int i = 0; i < 33; i++) pu[1 + i] = (char)toupper((unsigned char)ph[i]);
            }
            ls_addf(&L, "GLYPH\t%06X\t%d,%d\t%d\t%s%s", (unsigned)sqlite3_column_int(st, 0), sqlite3_column_int(st, 1), sqlite3_column_int(st, 2), sqlite3_column_int(st, 3), up, pu);
        } else {
            ls_addf(&L, "GLYPH\t%06X\t%d,%d\t%d", (unsigned)sqlite3_column_int(st, 0), sqlite3_column_int(st, 1), sqlite3_column_int(st, 2), sqlite3_column_int(st, 3));
        }
    }
    sqlite3_reset(st);
    st = store_stmt(c, ST_FONT_KERN, "SELECT first, second, adjust FROM pxfm_kerning WHERE pxfm = ? ORDER BY first, second");
    sqlite3_bind_blob(st, 1, fh, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) ls_addf(&L, "KERNING\t%06X\t%06X\t%d", (unsigned)sqlite3_column_int(st, 0), (unsigned)sqlite3_column_int(st, 1), sqlite3_column_int(st, 2));
    sqlite3_reset(st);
    RUNLOCK(c);
    SBuf b = {0};
    ls_emit(&L, &b, NULL);
    ls_free(&L);
    if (!b.p) { b.p = (char *)malloc(1); b.p[0] = 0; }
    if (len) *len = b.n;
    if (hashOut) px_hash128_hex(b.p, b.n, hashOut, false);
    return b.p;
}

char *store_palette_text(StoreConn *c, const char *hex, size_t *len, char hashOut[33]) {
    uint8_t ph[16];
    if (!store_hex_to_blob(hex, ph)) return NULL;
    RLOCK(c);
    char *t = NULL;
    sqlite3_stmt *st = store_stmt(c, ST_PALETTE, "SELECT colors FROM palette WHERE hash = ?");
    if (st) {
        sqlite3_bind_blob(st, 1, ph, 16, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const uint8_t *b = (const uint8_t *)sqlite3_column_blob(st, 0);
            int bl = sqlite3_column_bytes(st, 0);
            uint32_t *cv = b && bl > 0 && bl % 4 == 0 ? (uint32_t *)malloc((size_t)bl) : NULL;
            if (cv) {
                for (int k = 0; k < bl / 4; k++) cv[k] = (uint32_t)b[4 * k] << 24 | (uint32_t)b[4 * k + 1] << 16 | (uint32_t)b[4 * k + 2] << 8 | b[4 * k + 3];
                char h[33];
                t = px_hex_text(cv, bl / 4, len, h);
                if (t && hashOut) memcpy(hashOut, h, 33);
                free(cv);
            }
        }
        sqlite3_reset(st);
    }
    RUNLOCK(c);
    return t;
}

char *store_pxgl_text(StoreConn *c, const char *hex, size_t *len, char hashOut[33]) {
    uint8_t gh[16];
    if (!store_hex_to_blob(hex, gh)) return NULL;
    RLOCK(c);
    sqlite3_stmt *st = store_stmt(c, ST_GLYPH_ROW, "SELECT width, height, pixels FROM glyph WHERE hash = ?");
    char *t = NULL;
    if (st) {
        sqlite3_bind_blob(st, 1, gh, 16, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            int w = sqlite3_column_int(st, 0), h = sqlite3_column_int(st, 1);
            const char *px = (const char *)sqlite3_column_blob(st, 2);
            t = (char *)malloc((size_t)(w + 1) * (size_t)h + 1);
            size_t o = 0;
            for (int y = 0; y < h; y++) { memcpy(t + o, px + (size_t)y * (size_t)w, (size_t)w); o += (size_t)w; t[o++] = '\n'; }
            t[o] = 0;
            if (len) *len = o;
            if (hashOut) px_hash128_hex(t, o, hashOut, false);
        }
        sqlite3_reset(st);
    }
    RUNLOCK(c);
    return t;
}

static bool is_text_key(const char *k) {
    if (strcmp(k, "CODEPAGE") == 0) return true;
    for (int t = 0; t < PXTF_TEXT_COUNT; t++) if (strcmp(k, pxtf_text_key((PxtfText)t)) == 0) return true;
    return false;
}

char *store_pxtf_text(StoreConn *c, const char *hex, size_t *len, char hashOut[33]) {
    uint8_t th[16];
    if (!store_hex_to_blob(hex, th)) return NULL;
    RLOCK(c);
    sqlite3_stmt *st = store_stmt(c, ST_TF_ROW, "SELECT source FROM pxtf WHERE hash = ?");
    char source[33] = "";
    if (st) {
        sqlite3_bind_blob(st, 1, th, 16, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 0), source);
        sqlite3_reset(st);
    }
    if (!source[0]) { RUNLOCK(c); return NULL; }
    typedef struct V { int number; char pxfm[33]; char *format, *type; LineSet fields; } V;
    V *vs = NULL;
    int nv = 0;
    st = store_stmt(c, ST_TF_VARIANTS, "SELECT number, pxfm, format, type FROM pxtf_variant WHERE pxtf = ? ORDER BY number");
    sqlite3_bind_blob(st, 1, th, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        vs = (V *)realloc(vs, sizeof(V) * (size_t)(nv + 1));
        V *v = &vs[nv++];
        memset(v, 0, sizeof *v);
        v->number = sqlite3_column_int(st, 0);
        store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 1), v->pxfm);
        v->format = from_db_text(sqlite3_column_blob(st, 2), sqlite3_column_bytes(st, 2));
        v->type = from_db_text(sqlite3_column_blob(st, 3), sqlite3_column_bytes(st, 3));
    }
    sqlite3_reset(st);
    st = store_stmt(c, ST_TF_FIELDS, "SELECT number, key, value FROM pxtf_field WHERE pxtf = ? ORDER BY number, key, value");
    sqlite3_bind_blob(st, 1, th, 16, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int number = sqlite3_column_int(st, 0);
        const char *key = (const char *)sqlite3_column_text(st, 1);
        V *v = NULL;
        for (int i = 0; i < nv; i++) if (vs[i].number == number) v = &vs[i];
        if (!v || !key) continue;
        char *val = from_db_text(sqlite3_column_blob(st, 2), sqlite3_column_bytes(st, 2));
        if (is_flag_key(key)) ls_add(&v->fields, key, strlen(key));
        else if (is_text_key(key)) { char *e = pxtf_escape(val); ls_addf(&v->fields, "%s\t%s", key, e); free(e); }
        else ls_addf(&v->fields, "%s\t%s", key, val);
        free(val);
    }
    sqlite3_reset(st);
    RUNLOCK(c);
    bool sameF = true, sameT = true;
    for (int i = 1; i < nv; i++) { if (strcmp(vs[i].format, vs[0].format) != 0) sameF = false; if (strcmp(vs[i].type, vs[0].type) != 0) sameT = false; }
    SBuf b = {0};
    LineSet top;
    memset(&top, 0, sizeof top);
    char up[33];
    for (int i = 0; i < 33; i++) up[i] = (char)toupper((unsigned char)source[i]);
    ls_addf(&top, "HASH\t%s", up);
    if (nv > 0 && sameF) ls_addf(&top, "FORMAT\t%s", vs[0].format);
    if (nv > 0 && sameT) { char *e = pxtf_escape(vs[0].type); ls_addf(&top, "TYPE\t%s", e); free(e); }
    ls_emit(&top, &b, NULL);
    ls_free(&top);
    for (int i = 0; i < nv; i++) {
        V *v = &vs[i];
        char num[16], pu[33];
        snprintf(num, sizeof num, "%04d\n", v->number);
        sb_add(&b, num, strlen(num));
        for (int k = 0; k < 33; k++) pu[k] = (char)toupper((unsigned char)v->pxfm[k]);
        ls_addf(&v->fields, "PXFM\t%s", pu);
        if (!sameF) ls_addf(&v->fields, "FORMAT\t%s", v->format);
        if (!sameT) { char *e = pxtf_escape(v->type); ls_addf(&v->fields, "TYPE\t%s", e); free(e); }
        ls_emit(&v->fields, &b, "\t");
        ls_free(&v->fields);
        free(v->format); free(v->type);
    }
    free(vs);
    if (len) *len = b.n;
    if (hashOut) px_hash128_hex(b.p, b.n, hashOut, false);
    return b.p;
}

/* ------------------------------------------------------------------ glyph usage and the glyph finder */

void store_usage_list_free(StoreUsageList *l) {
    if (!l) return;
    free(l->u);
    arena_free(l->arena);
    memset(l, 0, sizeof *l);
}

static int cmp_u32(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return x < y ? -1 : x > y; }
static int cmp_str_ptr(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

bool store_glyph_usage(StoreConn *c, const char *hex, StoreUsageList *out, int *nFonts, int *nTypefaces, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    uint8_t gh[16];
    if (!store_hex_to_blob(hex, gh)) return fail(err, "'%s' is not a hash", hex);
    RLOCK(c);
    FontFacts *facts = NULL;
    int nf = 0;
    StoreEntryList vars;
    memset(&vars, 0, sizeof vars);
    bool ok = false;
    sqlite3_stmt *st = NULL;
    if (!read_facts(c->db, &facts, &nf, err) || !list_variants(c->db, &vars, facts, nf, err)) goto done;
    qsort(vars.e, (size_t)vars.count, sizeof(StoreEntry), cmp_entry_pxfm);
    st = store_stmt(c, ST_USAGE,
        "SELECT p.pxfm, p.code FROM pxfm_glyph AS p WHERE p.glyph = ?1 AND (p.code <> 16777215 OR NOT EXISTS"
        " (SELECT 1 FROM pxfm_glyph AS o WHERE o.pxfm = p.pxfm AND o.glyph = ?1 AND o.code <> 16777215)) ORDER BY 1, 2");
    if (!st) { fail(err, "sqlite: %s", sqlite3_errmsg(c->db)); goto done; }
    sqlite3_bind_blob(st, 1, gh, 16, SQLITE_STATIC);
    int cap = 256, cnt = 0;
    StoreUsage *u = (StoreUsage *)malloc(sizeof(StoreUsage) * (size_t)cap);
    Arena *a = NULL;
    char (*fonts)[33] = (char (*)[33])malloc(33 * 4096);
    int nfonts = 0, fcap = 4096;
    char (*tfs)[33] = (char (*)[33])malloc(33 * 4096);
    int ntfs = 0, tcap = 4096;
    while (sqlite3_step(st) == SQLITE_ROW) {
        char pxfm[33];
        store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 0), pxfm);
        int32_t code = sqlite3_column_int(st, 1);
        if (nfonts == 0 || strcmp(fonts[nfonts - 1], pxfm) != 0) {
            if (nfonts == fcap) { fcap *= 2; fonts = (char (*)[33])realloc(fonts, 33 * (size_t)fcap); }
            memcpy(fonts[nfonts++], pxfm, 33);
        }
        int lo = 0, hi = vars.count;
        while (lo < hi) { int mid = (lo + hi) / 2; if (strcmp(vars.e[mid].pxfm, pxfm) < 0) lo = mid + 1; else hi = mid; }
        bool any = false;
        for (int k = lo; k < vars.count && strcmp(vars.e[k].pxfm, pxfm) == 0; k++) {
            any = true;
            if (cnt == cap) { cap *= 2; u = (StoreUsage *)realloc(u, sizeof(StoreUsage) * (size_t)cap); }
            StoreUsage *r = &u[cnt++];
            memcpy(r->pxfm, pxfm, 33);
            r->code = code;
            memcpy(r->pxtf, vars.e[k].pxtf, 33);
            r->variant = vars.e[k].variant;
            r->family = arena_str(&a, vars.e[k].family, -1);
            r->style = arena_str(&a, vars.e[k].style, -1);
            if (ntfs == tcap) { tcap *= 2; tfs = (char (*)[33])realloc(tfs, 33 * (size_t)tcap); }
            memcpy(tfs[ntfs++], vars.e[k].pxtf, 33);
        }
        if (!any) {
            if (cnt == cap) { cap *= 2; u = (StoreUsage *)realloc(u, sizeof(StoreUsage) * (size_t)cap); }
            StoreUsage *r = &u[cnt++];
            memcpy(r->pxfm, pxfm, 33);
            r->code = code;
            r->pxtf[0] = 0;
            r->variant = 0;
            r->family = r->style = "";
        }
    }
    sqlite3_reset(st);
    if (nFonts) *nFonts = nfonts;
    if (nTypefaces) {
        qsort(tfs, (size_t)ntfs, 33, cmp_str_ptr);
        int d = 0;
        for (int i = 0; i < ntfs; i++) if (i == 0 || strcmp(tfs[i], tfs[i - 1]) != 0) d++;
        *nTypefaces = d;
    }
    free(fonts); free(tfs);
    out->u = u; out->count = cnt; out->arena = a;
    ok = true;
    (void)cmp_u32;
done:
    store_entry_list_free(&vars);
    free(facts);
    RUNLOCK(c);
    return ok;
}

bool store_fonts_with_glyphs(StoreConn *c, const char (*hashes)[33], int n, StoreEntryList *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    if (n <= 0) return true;
    uint8_t (*hb)[16] = (uint8_t(*)[16])malloc((size_t)n * 16);
    for (int i = 0; i < n; i++) if (!store_hex_to_blob(hashes[i], hb[i])) { free(hb); return fail(err, "'%s' is not a hash", hashes[i]); }
    /* distinct */
    qsort(hb, (size_t)n, 16, cmp16);
    int m = 0;
    for (int i = 0; i < n; i++) if (i == 0 || memcmp(hb[i], hb[i - 1], 16) != 0) memcpy(hb[m++], hb[i], 16);
    size_t sl = 200 + (size_t)m * 2;
    char *sql = (char *)malloc(sl);
    int p = snprintf(sql, sl, "SELECT pxfm FROM pxfm_glyph WHERE glyph IN (");
    for (int i = 0; i < m; i++) p += snprintf(sql + p, sl - (size_t)p, "%s?", i ? "," : "");
    snprintf(sql + p, sl - (size_t)p, ") GROUP BY pxfm HAVING COUNT(DISTINCT glyph) = %d ORDER BY pxfm", m);
    RLOCK(c);
    sqlite3_stmt *st;
    bool ok = false;
    uint8_t (*found)[16] = NULL;
    int nfound = 0;
    if (m > 900) { fail(err, "too many glyphs"); goto done; }
    if (sqlite3_prepare_v2(c->db, sql, -1, &st, NULL) != SQLITE_OK) { fail(err, "sqlite: %s", sqlite3_errmsg(c->db)); goto done; }
    for (int i = 0; i < m; i++) sqlite3_bind_blob(st, i + 1, hb[i], 16, SQLITE_STATIC);
    int fcap = 64;
    found = (uint8_t(*)[16])malloc((size_t)fcap * 16);
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (nfound == fcap) { fcap *= 2; found = (uint8_t(*)[16])realloc(found, (size_t)fcap * 16); }
        memcpy(found[nfound++], sqlite3_column_blob(st, 0), 16);
    }
    sqlite3_finalize(st);
    qsort(found, (size_t)nfound, 16, cmp16);
    ok = list_fonts_nolock(c, (const uint8_t(*)[16])found, nfound, out, err);
done:
    RUNLOCK(c);
    free(found); free(sql); free(hb);
    return ok;
}

void store_code_glyph_list_free(StoreCodeGlyphList *l) { if (l) { free(l->g); memset(l, 0, sizeof *l); } }

static int cmp_codeglyph(const void *a, const void *b) {
    const StoreCodeGlyph *x = (const StoreCodeGlyph *)a, *y = (const StoreCodeGlyph *)b;
    if (x->nFonts != y->nFonts) return x->nFonts > y->nFonts ? -1 : 1;
    return strcmp(x->glyph, y->glyph);
}

bool store_glyphs_for_code(StoreConn *c, int32_t code, StoreCodeGlyphList *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    if (code < 0 || code >= 0x110000) return true;
    RLOCK(c);
    sqlite3_stmt *st = store_stmt(c, ST_CODE_GLYPHS,
        "SELECT p.glyph, g.width, g.height, p.x, p.y, COUNT(*) FROM pxfm_glyph AS p JOIN glyph AS g ON g.hash = p.glyph"
        " WHERE p.code = ?1 AND p.code < 1114112 GROUP BY p.glyph, p.x, p.y ORDER BY p.glyph");
    if (!st) { RUNLOCK(c); return fail(err, "sqlite: %s", sqlite3_errmsg(c->db)); }
    sqlite3_bind_int(st, 1, code);
    int cap = 128, cnt = 0, bestN = 0, bestXY = 0;
    StoreCodeGlyph *g = (StoreCodeGlyph *)calloc((size_t)cap, sizeof(StoreCodeGlyph));
    uint8_t last[16];
    bool haveLast = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const uint8_t *hb = (const uint8_t *)sqlite3_column_blob(st, 0);
        int x = sqlite3_column_int(st, 3), y = sqlite3_column_int(st, 4), n = sqlite3_column_int(st, 5);
        if (!haveLast || memcmp(last, hb, 16) != 0) {
            if (cnt == cap) { cap *= 2; g = (StoreCodeGlyph *)realloc(g, sizeof(StoreCodeGlyph) * (size_t)cap); }
            StoreCodeGlyph *e = &g[cnt++];
            memset(e, 0, sizeof *e);
            store_blob_to_hex(hb, e->glyph);
            e->width = sqlite3_column_int(st, 1);
            e->height = sqlite3_column_int(st, 2);
            e->yMin = y;
            memcpy(last, hb, 16);
            haveLast = true;
            bestN = 0;
            (void)bestXY;
        }
        StoreCodeGlyph *e = &g[cnt - 1];
        if (y < e->yMin) e->yMin = y;
        e->nFonts += n;
        if (n > bestN) { bestN = n; e->yTypical = y; e->xTypical = x; }
    }
    sqlite3_reset(st);
    RUNLOCK(c);
    qsort(g, (size_t)cnt, sizeof(StoreCodeGlyph), cmp_codeglyph);
    out->g = g;
    out->count = cnt;
    return true;
}

/* ------------------------------------------------------------------ saving */

typedef struct NewGlyph { uint8_t hash[16]; int w, h; char *pix; } NewGlyph;

/* the rows of one font and typeface into `db` (OR IGNORE: what is there stays) inside the caller's transaction */
static bool insert_all(sqlite3 *db, const NewGlyph *ng, int nng, const PxPalette *pals, int npals, const uint8_t fh[16], const PxFont *parsed,
                       const PxtfTypeface *tf, const uint8_t th[16], int *newGlyphs, bool *newPxfm, bool *newPxtf, char err[PX_ERR_LEN]) {
    InsStmts ins;
    if (!ins_prepare(db, &ins, true, err)) return false;
    bool ok = true;
    int added = 0;
    for (int i = 0; i < npals && ok; i++) {
        uint8_t pb[16];
        store_hex_to_blob(pals[i].hash, pb);
        ok = ins_palette(db, &ins, pb, pals[i].c, pals[i].n, err);
    }
    for (int i = 0; i < nng && ok; i++) {
        ok = ins_glyph(db, &ins, ng[i].hash, ng[i].w, ng[i].h, ng[i].pix, err);
        if (ok && sqlite3_changes(db) > 0) added++;
    }
    if (ok) {
        sqlite3_int64 before = sqlite3_total_changes(db);
        ok = ins_pxfm(db, &ins, fh, parsed, err);
        if (newPxfm) *newPxfm = sqlite3_total_changes(db) > before;
    }
    if (ok && tf) {
        sqlite3_int64 before = sqlite3_total_changes(db);
        ok = ins_typeface(db, &ins, tf, th, err);
        if (newPxtf) *newPxtf = sqlite3_total_changes(db) > before;
    }
    ins_finalize(&ins);
    if (newGlyphs) *newGlyphs = added;
    return ok;
}

static bool pixels_of_hash(sqlite3 *db, const uint8_t h[16], uint64_t *used) {
    sqlite3_stmt *st;
    bool r = false;
    if (sqlite3_prepare_v2(db, "SELECT pixels FROM glyph WHERE hash = ?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_blob(st, 1, h, 16, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *p = (const char *)sqlite3_column_blob(st, 0);
            int n = sqlite3_column_bytes(st, 0);
            for (int i = 0; i < n; i++) { int pi = px_pixel_index(p[i]); if (pi >= 0) *used |= 1ull << pi; }
            r = true;
        }
        sqlite3_finalize(st);
    }
    return r;
}

bool store_save_to_memory(Store *s, const PxFont *font, const PxtfTypeface *tfIn, int variant, StoreSaveResult *res, char err[PX_ERR_LEN]) {
    StoreSaveResult r;
    memset(&r, 0, sizeof r);
    bool ok = false;
    NewGlyph *ng = NULL;
    int nng = 0;
    PxFont parsed;
    memset(&parsed, 0, sizeof parsed);
    PxtfTypeface tf;
    memset(&tf, 0, sizeof tf);
    bool haveTf = false, locked = false, inTx = false;
    char *text = NULL;
    uint8_t fh[16], th[16];
    sqlite3 *db = s->main->db;
    PxPalette *pals = NULL;
    int npals = 0;

    for (int i = 1; i < font->count; i++)
        if (font->glyphs[i].code == font->glyphs[i - 1].code) return fail(err, "two glyphs have the code %06X", (unsigned)font->glyphs[i].code);
    size_t tl;
    text = px_pxfm_text(font, &tl, r.pxfm);
    if (!text) return fail(err, "out of memory");
    store_hex_to_blob(r.pxfm, fh);
    if (!px_parse_pxfm(text, tl, "the font", &parsed, err)) { free(text); return false; }
    ng = (NewGlyph *)calloc((size_t)(font->count + 1), sizeof(NewGlyph));
    if (!ng) { fail(err, "out of memory"); goto done; }
    npals = px_font_write_palettes(font, &pals);
    if (npals < 0) { npals = 0; pals = NULL; fail(err, "out of memory"); goto done; }
    for (int i = 0; i < font->count; i++) {
        const PxGlyph *g = &font->glyphs[i];
        if (g->lazy) continue;
        size_t n;
        char gh[33];
        char *t = px_font_glyph_pxgl(font, g, &n, gh);
        if (!t) continue;
        uint8_t hb[16];
        store_hex_to_blob(gh, hb);
        bool dup = false;
        for (int k = 0; k < nng && !dup; k++) if (memcmp(ng[k].hash, hb, 16) == 0) dup = true;
        if (!dup) {
            int w = (int)(strchr(t, '\n') - t);
            int h = (int)(n / (size_t)(w + 1));
            char *pix = (char *)malloc((size_t)w * (size_t)h);
            size_t o = 0;
            for (size_t k = 0; k < n; k++) if (t[k] != '\n') pix[o++] = t[k];
            memcpy(ng[nng].hash, hb, 16);
            ng[nng].w = w; ng[nng].h = h; ng[nng].pix = pix;
            nng++;
        }
        free(t);
    }
    if (tfIn) {
        if (!pxtf_copy(&tf, tfIn)) { fail(err, "out of memory"); goto done; }
        haveTf = true;
        if (variant > 0) {
            PxtfVariant *v = pxtf_find_variant(&tf, variant);
            if (!v) { fail(err, "the typeface has no variant %d", variant); goto done; }
            memcpy(v->pxfm, r.pxfm, 33);
        }
        pxtf_hash(&tf, r.pxtf);
        store_hex_to_blob(r.pxtf, th);
    }

    AcquireSRWLockExclusive(&s->lock);
    locked = true;
    /* lazy glyphs (and their palettes) and the typeface's other fonts must be in the store */
    for (int i = 0; i < font->count; i++) {
        const PxGlyph *g = &font->glyphs[i];
        if (!g->lazy || !g->hash[0]) continue;
        uint8_t hb[16];
        uint64_t used = 0;
        store_hex_to_blob(g->hash, hb);
        if (!pixels_of_hash(db, hb, &used)) { fail(err, "the glyph %s of code %06X is not in the store", g->hash, (unsigned)g->code); goto done; }
        if (g->pal[0]) {
            uint32_t *pc;
            int pn;
            store_hex_to_blob(g->pal, hb);
            if (!palette_of_hash(db, hb, &pc, &pn)) { fail(err, "the palette %s of code %06X is not in the store", g->pal, (unsigned)g->code); goto done; }
            free(pc);
        }
    }
    if (haveTf) {
        for (int v = 0; v < tf.count; v++) {
            uint8_t pb[16];
            store_hex_to_blob(tf.v[v].pxfm, pb);
            if (memcmp(pb, fh, 16) != 0) {
                sqlite3_stmt *st;
                bool has = false;
                if (sqlite3_prepare_v2(db, "SELECT 1 FROM pxfm WHERE hash = ?", -1, &st, NULL) == SQLITE_OK) {
                    sqlite3_bind_blob(st, 1, pb, 16, SQLITE_STATIC);
                    has = sqlite3_step(st) == SQLITE_ROW;
                    sqlite3_finalize(st);
                }
                if (!has) { fail(err, "variant %04d names the font %s, which is not in the library", tf.v[v].number, tf.v[v].pxfm); goto done; }
            }
        }
    }
    if (!exec_sql(db, "BEGIN IMMEDIATE", err)) goto done;
    inTx = true;
    if (!insert_all(db, ng, nng, pals, npals, fh, &parsed, haveTf ? &tf : NULL, th, &r.newGlyphs, &r.newPxfm, &r.newPxtf, err)) goto done;

    if (!exec_sql(db, "COMMIT", err)) goto done;
    inTx = false;
    ok = true;
done:
    if (inTx) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    if (locked) ReleaseSRWLockExclusive(&s->lock);
    if (ok) {
        if (r.newPxfm || r.newPxtf || r.newGlyphs) InterlockedIncrement(&s->gen);
        store_sim_font_added(s, r.pxfm); count_rows(s); if (res) *res = r;
    }
    for (int i = 0; i < nng && ng; i++) free(ng[i].pix);
    free(ng);
    px_palettes_free(pals, npals);
    px_font_free(&parsed);
    if (haveTf) pxtf_free(&tf);
    free(text);
    return ok;
}

/* ================================================================== opening, importing, exporting */

enum { K_GLYPH = 0, K_PXFM = 1, K_PXTF = 2, K_PALETTE = 3 };
#define NKIND 4
static const char *const KIND_TABLE[NKIND] = {"glyph", "pxfm", "pxtf", "palette"};
static const char *const KIND_SUB[NKIND] = {"pxgl", "pxfm", "pxtf", "palette"};
static const char *const KIND_EXT[NKIND] = {"pxgl", "pxfm", "pxtf", "hex"};

static int *count_of(StoreCounts *c, int k) {
    return k == K_GLYPH ? &c->glyphs : k == K_PXFM ? &c->fonts : k == K_PXTF ? &c->typefaces : &c->palettes;
}

void store_counts_text(const StoreCounts *c, char *out, size_t n) {
    char t[160] = "";
    size_t o = 0;
    if (c->fonts) o += (size_t)snprintf(t + o, sizeof t - o, "%s%d font%s", o ? ", " : "", c->fonts, c->fonts == 1 ? "" : "s");
    if (c->typefaces) o += (size_t)snprintf(t + o, sizeof t - o, "%s%d typeface%s", o ? ", " : "", c->typefaces, c->typefaces == 1 ? "" : "s");
    if (c->glyphs) o += (size_t)snprintf(t + o, sizeof t - o, "%s%d glyph%s", o ? ", " : "", c->glyphs, c->glyphs == 1 ? "" : "s");
    if (c->palettes) o += (size_t)snprintf(t + o, sizeof t - o, "%s%d palette%s", o ? ", " : "", c->palettes, c->palettes == 1 ? "" : "s");
    snprintf(out, n, "%s", o ? t : "nothing");
}

static void dir_of(const char *path, char *out, size_t n) {
    snprintf(out, n, "%s", path);
    size_t l = strlen(out);
    while (l > 0 && out[l - 1] != '/' && out[l - 1] != '\\') l--;
    if (l == 0) snprintf(out, n, ".");
    else { out[l - 1] = 0; if (l == 1) snprintf(out, n, "/"); }
}

static bool dir_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void trim_slashes(char *r) {
    size_t rl = strlen(r);
    while (rl > 1 && (r[rl - 1] == '/' || r[rl - 1] == '\\')) r[--rl] = 0;
}

/* A pxfdb of format version 1 made before hex palettes (pxfm_color, no palette table, no pxfm.symbolic) cannot be read: it is
 * rebuilt from its files by a newer pixelglyphlib, never migrated. */
static bool check_schema(sqlite3 *db, char err[PX_ERR_LEN], const char *path) {
    bool old = one_int(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'pxfm_color'") > 0 ||
               one_int(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'palette'") == 0 ||
               one_int(db, "SELECT COUNT(*) FROM pragma_table_info('pxfm') WHERE name = 'symbolic'") == 0 ||
               one_int(db, "SELECT COUNT(*) FROM pragma_table_info('pxfm_glyph') WHERE name = 'palette'") == 0;
    if (old) return fail(err, "%s: made by an older pixelglyphlib (before hex palettes: colour lines, no palette table): rebuild it from its files with the current pixelglyphlib", path);
    return true;
}

bool store_open_pxfdb(const char *path, StoreProgress *prog, Store **out, char err[PX_ERR_LEN]) {
    double t0 = store_now();
    sqlite3 *src;
    if (!open_ro(path, &src, err)) return false;
    if (!check_meta(src, err, path) || !check_schema(src, err, path)) { sqlite3_close(src); return false; }
    Store *s = store_new(err);
    if (!s) { sqlite3_close(src); return false; }
    progress_set(prog, 7, 1);
    if (!backup_into(s->main->db, src, err)) { sqlite3_close(src); store_close(s); return false; }
    sqlite3_close(src);
    snprintf(s->info.path, sizeof s->info.path, "%s", path);
    count_rows(s);
    s->info.openSeconds = store_now() - t0;
    progress_set(prog, 0, 0);
    *out = s;
    return true;
}

/* lists the four folders of `root`, reads every file with the strict readers and builds the database in the empty store */
static bool build_store_from_folder(Store *s, const char *root, StoreProgress *prog, char err[PX_ERR_LEN]) {
    HashList lists[NKIND];
    progress_set(prog, 1, NKIND);
    for (int k = 0; k < NKIND; k++) { list_names(root, KIND_SUB[k], KIND_EXT[k], &lists[k]); if (prog) prog->done = k + 1; }
    bool ok = false;
    int bad = 0;
    const char *fb = NULL;
    for (int k = 0; k < NKIND; k++) { bad += lists[k].bad; if (lists[k].bad && !fb) fb = lists[k].firstBad; }
    if (bad) { fail(err, "%s: not a file named <hash>.<kind> under its prefix folder (%d such entries)", fb, bad); goto done; }
    if (lists[0].n == 0 && lists[1].n == 0 && lists[2].n == 0) { fail(err, "%s: no pxgl, pxfm or pxtf files", root); goto done; }
    {
        double tb = store_now();
        if (!build_from_files(s, root, lists, true, prog, err)) goto done;
        s->info.buildSeconds = store_now() - tb;
    }
    ok = true;
done:
    for (int k = 0; k < NKIND; k++) free(lists[k].h);
    return ok;
}

static Store *import_build(const char *folder, StoreProgress *prog, char err[PX_ERR_LEN]) {
    char r[1100];
    snprintf(r, sizeof r, "%s", folder);
    trim_slashes(r);
    if (!dir_exists(r)) { fail(err, "%s: no such folder", r); return NULL; }
    Store *s = store_new(err);
    if (!s) return NULL;
    if (!build_store_from_folder(s, r, prog, err)) { store_close(s); return NULL; }
    return s;
}

bool store_import_folder(const char *folder, const char *pxfdbPath, StoreProgress *prog, Store **out, char err[PX_ERR_LEN]) {
    double t0 = store_now();
    Store *s = import_build(folder, prog, err);
    if (!s) return false;
    progress_set(prog, 8, 1);
    if (!save_db_file(s->main->db, pxfdbPath, err)) { store_close(s); return false; }
    snprintf(s->info.path, sizeof s->info.path, "%s", pxfdbPath);
    s->info.imported = true;
    count_rows(s);
    s->info.openSeconds = store_now() - t0;
    progress_set(prog, 0, 0);
    *out = s;
    return true;
}

bool store_import_to_file(const char *folder, const char *pxfdbPath, StoreProgress *prog, char err[PX_ERR_LEN]) {
    Store *s = import_build(folder, prog, err);
    if (!s) return false;
    progress_set(prog, 8, 1);
    bool ok = save_db_file(s->main->db, pxfdbPath, err);
    store_close(s);
    return ok;
}

void store_scan_folder(const char *folderIn, StoreFolderScan *out) {
    char folder[1100];
    snprintf(folder, sizeof folder, "%s", folderIn);
    trim_slashes(folder);
    memset(out, 0, sizeof *out);
    for (int k = 0; k < NKIND; k++) {
        HashList l;
        list_names(folder, KIND_SUB[k], KIND_EXT[k], &l);
        *count_of(&out->found, k) = l.n;
        out->other += l.bad;
        free(l.h);
    }
}

bool store_scan_has_data(const StoreFolderScan *s) {
    return s->found.fonts + s->found.typefaces + s->found.glyphs + s->found.palettes + s->other > 0;
}

/* 1 written, 0 it was there already, -1 error.  Never replaces a file. */
static int write_new_file(const char *path, const char *data, size_t len, char err[PX_ERR_LEN]) {
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 0;
    char dir[1300], tmp[1420];
    dir_of(path, dir, sizeof dir);
    px_mkdir_p(dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { fail(err, "%s: cannot write", path); return -1; }
    bool w = fwrite(data, 1, len, f) == len;
    w = fclose(f) == 0 && w;
    if (!w) { DeleteFileA(tmp); fail(err, "%s: write failed", path); return -1; }
    if (!MoveFileExA(tmp, path, 0)) {
        DeleteFileA(tmp);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 0;
        fail(err, "%s: cannot write", path);
        return -1;
    }
    return 1;
}

/* deletes everything below `dir` (not `dir` itself); returns the number of files deleted, -1 on an error */
static int delete_contents(const char *dir, char err[PX_ERR_LEN]) {
    char pat[1400];
    snprintf(pat, sizeof pat, "%s/*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0, rc = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char p[1500];
        snprintf(p, sizeof p, "%s/%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            int k = delete_contents(p, err);
            if (k < 0) { rc = -1; break; }
            n += k;
            if (!RemoveDirectoryA(p)) { fail(err, "%s: cannot delete the folder", p); rc = -1; break; }
        } else {
            SetFileAttributesA(p, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileA(p)) { fail(err, "%s: cannot delete", p); rc = -1; break; }
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return rc < 0 ? -1 : n;
}

bool store_export_folder(Store *s, const char *folderIn, StoreExportMode mode, StoreProgress *prog, StoreExportResult *resOut, char err[PX_ERR_LEN]) {
    StoreExportResult res;
    memset(&res, 0, sizeof res);
    char folder[1100];
    snprintf(folder, sizeof folder, "%s", folderIn);
    trim_slashes(folder);
    px_mkdir_p(folder);
    if (!dir_exists(folder)) return fail(err, "%s: no such folder", folder);
    if (mode == STORE_EXPORT_OVERWRITE) {
        progress_set(prog, 10, NKIND);
        for (int k = 0; k < NKIND; k++) {
            char d[1200];
            snprintf(d, sizeof d, "%s/%s", folder, KIND_SUB[k]);
            if (dir_exists(d)) {
                int n = delete_contents(d, err);
                if (n < 0) return false;
                *count_of(&res.deleted, k) = n;
            }
            if (prog) prog->done = k + 1;
        }
    }
    StoreConn *c = store_conn_open(s);
    if (!c) return fail(err, "cannot open a store connection");
    uint8_t (*hs[NKIND])[16] = {NULL, NULL, NULL, NULL};
    int nh[NKIND] = {0, 0, 0, 0};
    bool ok = true;
    long total = 0;
    for (int k = 0; k < NKIND && ok; k++) {
        char sql[80];
        snprintf(sql, sizeof sql, "SELECT hash FROM %s ORDER BY hash", KIND_TABLE[k]);
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(c->db, sql, -1, &st, NULL) != SQLITE_OK) { fail(err, "sqlite: %s", sqlite3_errmsg(c->db)); ok = false; break; }
        int cap = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (sqlite3_column_bytes(st, 0) != 16) continue;
            if (nh[k] == cap) { cap = cap ? cap * 2 : 1024; hs[k] = (uint8_t(*)[16])realloc(hs[k], (size_t)cap * 16); }
            memcpy(hs[k][nh[k]++], sqlite3_column_blob(st, 0), 16);
        }
        sqlite3_finalize(st);
        total += nh[k];
    }
    progress_set(prog, 11, total);
    for (int k = NKIND - 1; k >= 0 && ok; k--) {      /* palettes, typefaces, fonts, glyphs */
        for (int i = 0; i < nh[k] && ok; i++) {
            if (prog && prog->cancel) { fail(err, "cancelled"); ok = false; break; }
            char hex[33], got[33], path[1400];
            store_blob_to_hex(hs[k][i], hex);
            size_t len = 0;
            char *t = k == K_GLYPH ? store_pxgl_text(c, hex, &len, got) : k == K_PXFM ? store_pxfm_text(c, hex, &len, got)
                    : k == K_PXTF ? store_pxtf_text(c, hex, &len, got) : store_palette_text(c, hex, &len, got);
            if (!t) { fail(err, "the %s %s cannot be made from the rows", KIND_SUB[k], hex); ok = false; break; }
            if (strcmp(got, hex) != 0) { fail(err, "the %s %s made from the rows has the hash %s: not written", KIND_SUB[k], hex, got); free(t); ok = false; break; }
            snprintf(path, sizeof path, "%s/%s/%c%c/%s.%s", folder, KIND_SUB[k], hex[0], hex[1], hex, KIND_EXT[k]);
            int w = write_new_file(path, t, len, err);
            free(t);
            if (w < 0) { ok = false; break; }
            (*count_of(w ? &res.written : &res.skipped, k))++;
            if (prog) prog->done++;
        }
    }
    for (int k = 0; k < NKIND; k++) free(hs[k]);
    store_conn_close(c);
    progress_set(prog, 0, 0);
    if (resOut) *resOut = res;
    return ok;
}

/* ================================================================== deleting fonts and typefaces */

static int q_hashes(sqlite3 *db, const char *sql, const char *hex, char (**out)[33]) {
    sqlite3_stmt *st;
    *out = NULL;
    uint8_t b[16];
    if (!store_hex_to_blob(hex, b) || sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_blob(st, 1, b, 16, SQLITE_STATIC);
    int n = 0, cap = 0;
    char (*r)[33] = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (sqlite3_column_bytes(st, 0) != 16) continue;
        if (n == cap) { cap = cap ? cap * 2 : 16; r = (char(*)[33])realloc(r, (size_t)cap * 33); }
        store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 0), r[n++]);
    }
    sqlite3_finalize(st);
    *out = r;
    return n;
}

static bool row_exists(sqlite3 *db, const char *sql, const char *hex) {
    sqlite3_stmt *st;
    uint8_t b[16];
    bool r = false;
    if (!store_hex_to_blob(hex, b) || sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_blob(st, 1, b, 16, SQLITE_STATIC);
    r = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return r;
}

void store_plan_free(StoreDeletePlan *p) {
    free(p->fonts); free(p->gone); free(p->rw); free(p->glyphs); free(p->palettes);
    if (p->newTf) { for (int i = 0; i < p->nRw; i++) pxtf_free(&p->newTf[i]); free(p->newTf); }
    memset(p, 0, sizeof *p);
}

/* a small sorted set of 16-byte hashes (the glyphs of a deletion plan) */
typedef struct HashSet { uint8_t (*h)[16]; int n, cap; } HashSet;

static void hs_free(HashSet *h) { free(h->h); memset(h, 0, sizeof *h); }

static bool hs_has(const HashSet *s, const uint8_t h[16]) {
    return s->n > 0 && bsearch(h, s->h, (size_t)s->n, 16, cmp16) != NULL;
}

static void hs_add_n(HashSet *s, const uint8_t (*a)[16], int n) {
    if (n <= 0) return;
    if (s->n + n > s->cap) {
        s->cap = (s->n + n) * 2;
        s->h = (uint8_t(*)[16])realloc(s->h, (size_t)s->cap * 16);
    }
    memcpy(s->h + s->n, a, (size_t)n * 16);
    s->n += n;
    qsort(s->h, (size_t)s->n, 16, cmp16);
    int o = 0;
    for (int i = 0; i < s->n; i++) if (o == 0 || memcmp(s->h[o - 1], s->h[i], 16) != 0) { if (o != i) memcpy(s->h[o], s->h[i], 16); o++; }
    s->n = o;
}

static void plan_push33(char (**v)[33], int *n, const char *hex) {
    for (int i = 0; i < *n; i++) if (strcmp((*v)[i], hex) == 0) return;
    *v = (char(*)[33])realloc(*v, (size_t)(*n + 1) * 33);
    snprintf((*v)[(*n)++], 33, "%s", hex);
}

/* the pxgl and palettes that only the deleted fonts name (rows that nobody named before are not candidates) */
static bool plan_orphans(sqlite3 *db, StoreDeletePlan *p, char err[PX_ERR_LEN]) {
    HashSet D = {0};
    for (int i = 0; i < p->nFonts; i++) { uint8_t b[16]; store_hex_to_blob(p->fonts[i], b); hs_add_n(&D, (const uint8_t(*)[16])b, 1); }
    static const char *const CAND[2] = {"SELECT DISTINCT glyph FROM pxfm_glyph WHERE pxfm = ? AND glyph IS NOT NULL",
                                        "SELECT DISTINCT palette FROM pxfm_glyph WHERE pxfm = ? AND palette IS NOT NULL"};
    static const char *const USERS[2] = {"SELECT pxfm FROM pxfm_glyph WHERE glyph = ?", "SELECT pxfm FROM pxfm_glyph WHERE palette = ?"};
    bool ok = true;
    for (int kind = 0; kind < 2 && ok; kind++) {
        HashSet C = {0};
        for (int i = 0; i < p->nFonts; i++) {
            char (*gl)[33];
            int n = q_hashes(db, CAND[kind], p->fonts[i], &gl);
            for (int j = 0; j < n; j++) { uint8_t b[16]; store_hex_to_blob(gl[j], b); hs_add_n(&C, (const uint8_t(*)[16])b, 1); }
            free(gl);
        }
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, USERS[kind], -1, &st, NULL) != SQLITE_OK) { fail(err, "sqlite: %s", sqlite3_errmsg(db)); ok = false; }
        for (int i = 0; ok && i < C.n; i++) {
            sqlite3_bind_blob(st, 1, C.h[i], 16, SQLITE_STATIC);
            bool used = false;
            while (sqlite3_step(st) == SQLITE_ROW) {
                if (sqlite3_column_bytes(st, 0) == 16 && !hs_has(&D, (const uint8_t *)sqlite3_column_blob(st, 0))) { used = true; break; }
            }
            sqlite3_reset(st);
            if (!used) {
                char hex[33];
                store_blob_to_hex(C.h[i], hex);
                if (kind == 0) plan_push33(&p->glyphs, &p->nGlyphs, hex); else plan_push33(&p->palettes, &p->nPalettes, hex);
            }
        }
        if (st) sqlite3_finalize(st);
        hs_free(&C);
    }
    hs_free(&D);
    return ok;
}

static void plan_summary(StoreDeletePlan *p) {
    char rw[500] = "", t[900] = "";
    size_t o = 0;
    for (int i = 0; i < p->nRw && i < 3; i++) {
        StoreRewrite *r = &p->rw[i];
        char nums[160] = "";
        size_t no = 0;
        for (int j = 0; j < r->nLost && j < 6; j++) no += (size_t)snprintf(nums + no, sizeof nums - no, "%s%04d", j ? ", " : "", r->lost[j]);
        o += (size_t)snprintf(rw + o, sizeof rw - o, "%sTypeface %.8s loses variant%s %s and is rewritten", i ? "; " : "", r->oldPxtf, r->nLost == 1 ? "" : "s", nums);
    }
    if (p->nRw > 3) snprintf(rw + o, sizeof rw - o, "; and %d more rewritten", p->nRw - 3);
    size_t n = 0;
    if (p->nFonts) n += (size_t)snprintf(t + n, sizeof t - n, "%d font%s", p->nFonts, p->nFonts == 1 ? "" : "s");
    if (p->nWhole) n += (size_t)snprintf(t + n, sizeof t - n, "%s%d typeface%s", n ? ", " : "", p->nWhole, p->nWhole == 1 ? "" : "s");
    if (p->nRw) n += (size_t)snprintf(t + n, sizeof t - n, "%s(%s)", n ? " " : "", rw);
    if (p->nGlyphs) n += (size_t)snprintf(t + n, sizeof t - n, "%s%d glyph%s", n ? ", " : "", p->nGlyphs, p->nGlyphs == 1 ? "" : "s");
    if (p->nPalettes) n += (size_t)snprintf(t + n, sizeof t - n, "%s%d palette%s", n ? ", " : "", p->nPalettes, p->nPalettes == 1 ? "" : "s");
    snprintf(p->summary, sizeof p->summary, "%s", n ? t : "nothing");
}

static void free_variant_fields(PxtfVariant *v) {
    free(v->format); free(v->type);
    for (int i = 0; i < PXTF_TEXT_COUNT; i++) free(v->text[i]);
    for (int i = 0; i < v->nopts; i++) free(v->opts[i]);
    free(v->opts);
    memset(v, 0, sizeof *v);
}

bool store_plan_delete_font(Store *s, const char *pxfm, StoreDeletePlan *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    StoreConn *c = s->main;
    bool ok = false;
    char (*tfs)[33] = NULL;
    int nt;
    AcquireSRWLockShared(&s->lock);
    if (!row_exists(c->db, "SELECT 1 FROM pxfm WHERE hash = ?", pxfm)) { fail(err, "the font %s is not in Memory", pxfm); goto done; }
    plan_push33(&out->fonts, &out->nFonts, pxfm);
    nt = q_hashes(c->db, "SELECT DISTINCT pxtf FROM pxtf_variant WHERE pxfm = ?", pxfm, &tfs);
    for (int i = 0; i < nt; i++) {
        uint8_t th[16];
        store_hex_to_blob(tfs[i], th);
        PxtfTypeface tf;
        pxtf_init(&tf);
        if (!load_typeface_nolock(c, th, &tf, err)) { pxtf_free(&tf); goto done; }
        int keep = 0;
        StoreRewrite rw;
        memset(&rw, 0, sizeof rw);
        for (int j = 0; j < tf.count; j++) {
            if (strcmp(tf.v[j].pxfm, pxfm) == 0) { if (rw.nLost < 64) rw.lost[rw.nLost++] = tf.v[j].number; }
            else keep++;
        }
        plan_push33(&out->gone, &out->nGone, tfs[i]);
        if (keep == 0) { out->nWhole++; pxtf_free(&tf); continue; }
        int o = 0;
        for (int j = 0; j < tf.count; j++) {
            if (strcmp(tf.v[j].pxfm, pxfm) == 0) { free_variant_fields(&tf.v[j]); continue; }
            if (o != j) tf.v[o] = tf.v[j];
            o++;
        }
        tf.count = o;
        snprintf(rw.oldPxtf, sizeof rw.oldPxtf, "%s", tfs[i]);
        pxtf_hash(&tf, rw.newPxtf);
        out->rw = (StoreRewrite *)realloc(out->rw, sizeof(StoreRewrite) * (size_t)(out->nRw + 1));
        out->newTf = (PxtfTypeface *)realloc(out->newTf, sizeof(PxtfTypeface) * (size_t)(out->nRw + 1));
        out->rw[out->nRw] = rw;
        out->newTf[out->nRw] = tf;
        out->nRw++;
    }
    if (!plan_orphans(c->db, out, err)) goto done;
    plan_summary(out);
    ok = true;
done:
    ReleaseSRWLockShared(&s->lock);
    free(tfs);
    if (!ok) store_plan_free(out);
    return ok;
}

bool store_plan_delete_typeface(Store *s, const char *pxtf, StoreDeletePlan *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    StoreConn *c = s->main;
    bool ok = false;
    char (*fl)[33] = NULL;
    int n;
    AcquireSRWLockShared(&s->lock);
    if (!row_exists(c->db, "SELECT 1 FROM pxtf WHERE hash = ?", pxtf)) { fail(err, "the typeface %s is not in Memory", pxtf); goto done; }
    plan_push33(&out->gone, &out->nGone, pxtf);
    out->nWhole = 1;
    n = q_hashes(c->db,
        "SELECT DISTINCT v.pxfm FROM pxtf_variant v WHERE v.pxtf = ?1 AND v.pxfm IN (SELECT hash FROM pxfm)"
        " AND NOT EXISTS (SELECT 1 FROM pxtf_variant w WHERE w.pxfm = v.pxfm AND w.pxtf <> ?1)", pxtf, &fl);
    for (int i = 0; i < n; i++) plan_push33(&out->fonts, &out->nFonts, fl[i]);
    if (!plan_orphans(c->db, out, err)) goto done;
    plan_summary(out);
    ok = true;
done:
    ReleaseSRWLockShared(&s->lock);
    free(fl);
    if (!ok) store_plan_free(out);
    return ok;
}

static bool del_where(sqlite3 *db, const char *sql, const uint8_t h[16], char err[PX_ERR_LEN]) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return fail(err, "sqlite: %s", sqlite3_errmsg(db));
    sqlite3_bind_blob(st, 1, h, 16, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? true : fail(err, "sqlite: %s", sqlite3_errmsg(db));
}

bool store_apply_delete(Store *s, const StoreDeletePlan *p, char err[PX_ERR_LEN]) {
    sqlite3 *db = s->main->db;
    bool ok = false, inTx = false, locked = false;
    uint8_t (*fb)[16] = (uint8_t(*)[16])malloc((size_t)(p->nFonts + 1) * 16);
    uint8_t (*gb)[16] = (uint8_t(*)[16])malloc((size_t)(p->nGone + 1) * 16);
    uint8_t (*lb)[16] = (uint8_t(*)[16])malloc((size_t)(p->nGlyphs + 1) * 16);
    uint8_t (*nb)[16] = (uint8_t(*)[16])malloc((size_t)(p->nRw + 1) * 16);
    uint8_t (*pb)[16] = (uint8_t(*)[16])malloc((size_t)(p->nPalettes + 1) * 16);
    for (int i = 0; i < p->nPalettes; i++) store_hex_to_blob(p->palettes[i], pb[i]);
    for (int i = 0; i < p->nFonts; i++) store_hex_to_blob(p->fonts[i], fb[i]);
    for (int i = 0; i < p->nGone; i++) store_hex_to_blob(p->gone[i], gb[i]);
    for (int i = 0; i < p->nGlyphs; i++) store_hex_to_blob(p->glyphs[i], lb[i]);
    for (int i = 0; i < p->nRw; i++) store_hex_to_blob(p->rw[i].newPxtf, nb[i]);
    InsStmts ins;
    memset(&ins, 0, sizeof ins);
    AcquireSRWLockExclusive(&s->lock);
    locked = true;
    if (!exec_sql(db, "BEGIN IMMEDIATE", err)) goto done;
    inTx = true;
    for (int i = 0; i < p->nFonts; i++) {
        if (!del_where(db, "DELETE FROM pxfm_glyph WHERE pxfm = ?", fb[i], err) ||
            !del_where(db, "DELETE FROM pxfm_kerning WHERE pxfm = ?", fb[i], err) || !del_where(db, "DELETE FROM pxfm WHERE hash = ?", fb[i], err)) goto done;
    }
    for (int i = 0; i < p->nGone; i++) {
        if (!del_where(db, "DELETE FROM pxtf_field WHERE pxtf = ?", gb[i], err) || !del_where(db, "DELETE FROM pxtf_variant WHERE pxtf = ?", gb[i], err) ||
            !del_where(db, "DELETE FROM pxtf WHERE hash = ?", gb[i], err)) goto done;
    }
    if (p->nRw) {
        if (!ins_prepare(db, &ins, true, err)) goto done;
        for (int i = 0; i < p->nRw; i++) if (!ins_typeface(db, &ins, &p->newTf[i], nb[i], err)) goto done;
        ins_finalize(&ins);
        memset(&ins, 0, sizeof ins);
    }
    for (int i = 0; i < p->nGlyphs; i++) if (!del_where(db, "DELETE FROM glyph WHERE hash = ?", lb[i], err)) goto done;
    for (int i = 0; i < p->nPalettes; i++) if (!del_where(db, "DELETE FROM palette WHERE hash = ?", pb[i], err)) goto done;
    if (!exec_sql(db, "COMMIT", err)) goto done;
    inTx = false;
    ok = true;
done:
    if (inTx) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    if (locked) ReleaseSRWLockExclusive(&s->lock);
    if (ok) {
        InterlockedIncrement(&s->gen);
        store_sim_fonts_removed(s, fb, p->nFonts);
        count_rows(s);
    }
    free(fb); free(gb); free(lb); free(nb); free(pb);
    return ok;
}

bool store_edit_in_place(Store *s, const char *oldPxfm, const PxFont *font, const char *oldPxtf, const PxtfTypeface *tf, StoreEditResult *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    snprintf(out->pxfm, sizeof out->pxfm, "%s", oldPxfm);
    if (oldPxtf) snprintf(out->pxtf, sizeof out->pxtf, "%s", oldPxtf);
    bool fontCh = false;
    if (font) {
        StoreSaveResult sr;
        if (!store_save_to_memory(s, font, NULL, 0, &sr, err)) return false;
        snprintf(out->pxfm, sizeof out->pxfm, "%s", sr.pxfm);
        fontCh = strcmp(sr.pxfm, oldPxfm) != 0;
    }
    StoreDeletePlan p;
    memset(&p, 0, sizeof p);
    StoreConn *c = s->main;
    char (*tfs)[33] = NULL;
    int nt = 0;
    bool ok = false;
    AcquireSRWLockShared(&s->lock);
    if (fontCh && row_exists(c->db, "SELECT 1 FROM pxfm WHERE hash = ?", oldPxfm)) plan_push33(&p.fonts, &p.nFonts, oldPxfm);
    if (fontCh) nt = q_hashes(c->db, "SELECT DISTINCT pxtf FROM pxtf_variant WHERE pxfm = ?", oldPxfm, &tfs);
    if (nt < 0) { fail(err, "sqlite: %s", sqlite3_errmsg(c->db)); goto done; }
    if (tf && oldPxtf) {
        bool have = false;
        for (int i = 0; i < nt; i++) if (strcmp(tfs[i], oldPxtf) == 0) have = true;
        if (!have) { tfs = (char(*)[33])realloc(tfs, (size_t)(nt + 1) * 33); snprintf(tfs[nt++], 33, "%s", oldPxtf); }
    }
    for (int i = 0; i < nt; i++) {
        PxtfTypeface t;
        pxtf_init(&t);
        if (tf && oldPxtf && strcmp(tfs[i], oldPxtf) == 0) { if (!pxtf_copy(&t, tf)) { pxtf_free(&t); fail(err, "out of memory"); goto done; } }
        else {
            uint8_t th[16];
            store_hex_to_blob(tfs[i], th);
            if (!load_typeface_nolock(c, th, &t, err)) { pxtf_free(&t); goto done; }
        }
        bool given = tf && oldPxtf && strcmp(tfs[i], oldPxtf) == 0;   /* the caller's typeface names the fonts it wants: a clone keeps the old font */
        for (int j = 0; j < t.count; j++)
            if (fontCh && strcmp(t.v[j].pxfm, oldPxfm) == 0) { if (given) p.nFonts = 0; else memcpy(t.v[j].pxfm, out->pxfm, 33); }
        StoreRewrite rw;
        memset(&rw, 0, sizeof rw);
        snprintf(rw.oldPxtf, sizeof rw.oldPxtf, "%s", tfs[i]);
        pxtf_hash(&t, rw.newPxtf);
        if (strcmp(rw.oldPxtf, rw.newPxtf) == 0) { pxtf_free(&t); continue; }
        plan_push33(&p.gone, &p.nGone, tfs[i]);
        p.rw = (StoreRewrite *)realloc(p.rw, sizeof(StoreRewrite) * (size_t)(p.nRw + 1));
        p.newTf = (PxtfTypeface *)realloc(p.newTf, sizeof(PxtfTypeface) * (size_t)(p.nRw + 1));
        p.rw[p.nRw] = rw;
        p.newTf[p.nRw] = t;
        p.nRw++;
        if (out->nMoved < 32) { memcpy(out->movedFrom[out->nMoved], rw.oldPxtf, 33); memcpy(out->movedTo[out->nMoved], rw.newPxtf, 33); out->nMoved++; }
        if (oldPxtf && strcmp(tfs[i], oldPxtf) == 0) memcpy(out->pxtf, rw.newPxtf, 33);
    }
    ok = p.nFonts == 0 || plan_orphans(c->db, &p, err);
done:
    ReleaseSRWLockShared(&s->lock);
    free(tfs);
    if (ok && (p.nFonts || p.nGone || p.nRw)) ok = store_apply_delete(s, &p, err);
    store_plan_free(&p);
    return ok;
}

/* ================================================================== adding files given in memory (packages: src/package.c)
 * The files are written to a temporary folder at their library paths, read there by the folder import's strict readers as a
 * library of their own (build_store_from_folder into a scratch store), and the new ones are added to memory by the
 * incremental build (build_from_files, fresh = false).  Nothing above is changed for it. */

static volatile LONG g_addSerial = 0;

static char *file_text_of(StoreConn *c, int kind, const char *hex, size_t *len) {
    char got[33];
    return kind == K_GLYPH ? store_pxgl_text(c, hex, len, got) : kind == K_PXFM ? store_pxfm_text(c, hex, len, got)
         : kind == K_PXTF ? store_pxtf_text(c, hex, len, got) : store_palette_text(c, hex, len, got);
}

bool store_add_files(Store *s, const StoreFile *files, int n, StoreCounts *addedOut, StoreCounts *skippedOut, char err[PX_ERR_LEN]) {
    StoreCounts added = {0, 0, 0, 0}, skipped = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) {
        if (files[i].kind < 0 || files[i].kind >= NKIND || !px_is_hash(files[i].hash, false, true)) return fail(err, "store_add_files: file %d has no kind or no lower-case hash", i);
    }
    char base[MAX_PATH], root[MAX_PATH + 80];
    DWORD bl = GetTempPathA(sizeof base, base);
    if (bl == 0 || bl >= sizeof base) return fail(err, "no temporary folder");
    snprintf(root, sizeof root, "%spxfe-files-%lu-%lu-%ld", base, (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), (long)InterlockedIncrement(&g_addSerial));
    trim_slashes(root);
    if (!px_mkdir_p(root)) return fail(err, "%s: cannot make the temporary folder", root);
    bool ok = false;
    bool *isNew = (bool *)calloc((size_t)(n ? n : 1), sizeof(bool));
    HashList lists[NKIND];
    memset(lists, 0, sizeof lists);
    if (!isNew) { fail(err, "out of memory"); goto done; }
    for (int i = 0; i < n; i++) {
        const StoreFile *f = &files[i];
        char path[MAX_PATH + 300], dir[MAX_PATH + 200];
        snprintf(dir, sizeof dir, "%s/%s/%c%c", root, KIND_SUB[f->kind], f->hash[0], f->hash[1]);
        snprintf(path, sizeof path, "%s/%s.%s", dir, f->hash, KIND_EXT[f->kind]);
        if (!px_mkdir_p(dir) || !px_write_file(path, f->data, f->len)) { fail(err, "%s: cannot write", path); goto done; }
    }
    {   /* the files alone, with the strict readers */
        Store *chk = store_new(err);
        if (!chk) goto done;
        bool good = build_store_from_folder(chk, root, NULL, err);
        store_close(chk);
        if (!good) {
            size_t rl = strlen(root);   /* the messages name the temporary paths: keep the part inside the library */
            char *p = strstr(err, root);
            while (p) { memmove(p, p + rl + 1, strlen(p + rl + 1) + 1); p = strstr(err, root); }
            goto done;
        }
    }
    if (!s) { ok = true; goto done; }
    {   /* what memory has: the same text is skipped, another text is a clash */
        StoreConn *c = store_conn_open(s);
        if (!c) { fail(err, "cannot open a store connection"); goto done; }
        bool clash = false;
        for (int i = 0; i < n && !clash; i++) {
            size_t len = 0;
            char *t = file_text_of(c, files[i].kind, files[i].hash, &len);
            if (!t) isNew[i] = true;
            else if (len == files[i].len && memcmp(t, files[i].data, len) == 0) (*count_of(&skipped, files[i].kind))++;
            else { fail(err, "the library has the %s file %s with another content: nothing was imported", KIND_SUB[files[i].kind], files[i].hash); clash = true; }
            free(t);
        }
        store_conn_close(c);
        if (clash) goto done;
    }
    for (int i = 0; i < n; i++) {
        if (!isNew[i]) continue;
        HashList *l = &lists[files[i].kind];
        if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->h = (uint8_t(*)[16])realloc(l->h, (size_t)l->cap * 16); }
        store_hex_to_blob(files[i].hash, l->h[l->n++]);
    }
    for (int k = 0; k < NKIND; k++) {            /* sorted (build_from_files searches them), each once */
        HashList *l = &lists[k];
        if (l->n > 1) qsort(l->h, (size_t)l->n, 16, cmp16);
        int m = 0;
        for (int i = 0; i < l->n; i++) if (m == 0 || memcmp(l->h[m - 1], l->h[i], 16) != 0) memcpy(l->h[m++], l->h[i], 16);
        l->n = m;
        *count_of(&added, k) = m;
    }
    if (lists[0].n + lists[1].n + lists[2].n + lists[3].n > 0) {
        AcquireSRWLockExclusive(&s->lock);
        bool good = build_from_files(s, root, lists, false, NULL, err);
        ReleaseSRWLockExclusive(&s->lock);
        if (!good) goto done;
        InterlockedIncrement(&s->gen);
        for (int i = 0; i < lists[K_PXFM].n; i++) { char hex[33]; store_blob_to_hex(lists[K_PXFM].h[i], hex); store_sim_font_added(s, hex); }
        count_rows(s);
    }
    ok = true;
done:
    {
        char e2[PX_ERR_LEN];
        delete_contents(root, e2);
        RemoveDirectoryA(root);
    }
    for (int k = 0; k < NKIND; k++) free(lists[k].h);
    free(isNew);
    if (ok) {
        if (addedOut) *addedOut = added;
        if (skippedOut) *skippedOut = skipped;
    }
    return ok;
}
