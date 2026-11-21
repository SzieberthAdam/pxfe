/* pxtf.c - pixel typeface (pxtf version 2) model, reader and writer. See pxtf.h. */
#include "pxtf.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const char *const TEXT_KEY[PXTF_TEXT_COUNT] = {
    "CODEPAGE", "COPYRIGHT", "DESCRIPTION", "DESIGNER", "DESIGNERURL", "LICENSE", "LICENSEURL", "NAME",
    "SLANT", "STYLE", "TRADEMARK", "VENDOR", "VENDORURL", "VERSION", "WEIGHTNAME",
    "TERMS-DERIVED-FROM", "TERMS-EVIDENCE", "TERMS-LICENSE", "TERMS-NOTE", "TERMS-SOURCE", "TERMS-SPDX", "TERMS-UPSTREAM-HOLDER"};
static const char *const TEXT_LABEL[PXTF_TEXT_COUNT] = {
    "Code page", "Copyright", "Description", "Designer", "Designer URL", "License", "License URL", "Full name",
    "Slant", "Style", "Trademark", "Vendor", "Vendor URL", "Version", "Weight name",
    "Derived from", "Evidence", "Licence", "Note", "Read from", "SPDX", "Upstream holder"};

const char *pxtf_text_key(PxtfText t) { return (t >= 0 && t < PXTF_TEXT_COUNT) ? TEXT_KEY[t] : ""; }
const char *pxtf_text_label(PxtfText t) { return (t >= 0 && t < PXTF_TEXT_COUNT) ? TEXT_LABEL[t] : ""; }

static bool fail(char err[PX_ERR_LEN], const char *fmt, ...) {
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, PX_ERR_LEN, fmt, ap);
        va_end(ap);
    }
    return false;
}

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

/* ------------------------------------------------------------------ model */

void pxtf_init(PxtfTypeface *c) { memset(c, 0, sizeof *c); }

static void variant_free(PxtfVariant *v) {
    free(v->format);
    free(v->type);
    for (int i = 0; i < PXTF_TEXT_COUNT; i++) free(v->text[i]);
    for (int i = 0; i < v->nopts; i++) free(v->opts[i]);
    free(v->opts);
    memset(v, 0, sizeof *v);
}

void pxtf_free(PxtfTypeface *c) {
    free(c->name);
    for (int i = 0; i < c->count; i++) variant_free(&c->v[i]);
    free(c->v);
    memset(c, 0, sizeof *c);
}

static bool variant_copy(PxtfVariant *d, const PxtfVariant *s) {
    *d = *s;
    d->format = s->format ? dup_str(s->format) : NULL;
    d->type = s->type ? dup_str(s->type) : NULL;
    for (int i = 0; i < PXTF_TEXT_COUNT; i++) d->text[i] = s->text[i] ? dup_str(s->text[i]) : NULL;
    d->opts = NULL;
    d->nopts = 0;
    if (s->nopts > 0) {
        d->opts = (char **)calloc((size_t)s->nopts, sizeof(char *));
        if (!d->opts) return false;
        for (int i = 0; i < s->nopts; i++) { d->opts[i] = dup_str(s->opts[i]); d->nopts++; }
    }
    return true;
}

bool pxtf_copy(PxtfTypeface *dst, const PxtfTypeface *src) {
    pxtf_init(dst);
    if (src->name && !(dst->name = dup_str(src->name))) return false;
    if (src->count > 0) {
        dst->v = (PxtfVariant *)calloc((size_t)src->count, sizeof(PxtfVariant));
        if (!dst->v) return false;
        dst->cap = src->count;
        for (int i = 0; i < src->count; i++) {
            if (!variant_copy(&dst->v[i], &src->v[i])) { dst->count = i + 1; pxtf_free(dst); return false; }
            dst->count++;
        }
    }
    return true;
}

PxtfVariant *pxtf_clone_variant(PxtfTypeface *c, int number) {
    PxtfVariant *src = pxtf_find_variant(c, number);
    if (!src) return NULL;
    if (c->count == c->cap) {
        int cap = c->cap ? c->cap * 2 : 4;
        PxtfVariant *nv = (PxtfVariant *)realloc(c->v, sizeof(PxtfVariant) * (size_t)cap);
        if (!nv) return NULL;
        c->v = nv;
        c->cap = cap;
        src = pxtf_find_variant(c, number);
    }
    PxtfVariant *d = &c->v[c->count];
    memset(d, 0, sizeof *d);
    if (!variant_copy(d, src)) return NULL;
    d->number = c->v[c->count - 1].number + 1;   /* the variants are in ascending order: the new one is last */
    c->count++;
    return d;
}

PxtfVariant *pxtf_add_variant(PxtfTypeface *c) {
    if (c->count == c->cap) {
        int cap = c->cap ? c->cap * 2 : 4;
        PxtfVariant *nv = (PxtfVariant *)realloc(c->v, sizeof(PxtfVariant) * (size_t)cap);
        if (!nv) return NULL;
        c->v = nv;
        c->cap = cap;
    }
    PxtfVariant *d = &c->v[c->count];
    memset(d, 0, sizeof *d);
    d->number = c->count ? c->v[c->count - 1].number + 1 : 1;   /* ascending: the new one is last */
    c->count++;
    return d;
}

PxtfVariant *pxtf_find_variant(PxtfTypeface *c, int number) {
    for (int i = 0; i < c->count; i++) if (c->v[i].number == number) return &c->v[i];
    return NULL;
}

bool pxtf_set_name(PxtfTypeface *c, const char *utf8) {
    free(c->name);
    c->name = NULL;
    if (utf8 && utf8[0]) {
        c->name = dup_str(utf8);
        return c->name != NULL;
    }
    return true;
}

void pxtf_random_hash(char out[33]) {
    static volatile LONG serial = 0;
    struct { LARGE_INTEGER qpc; FILETIME ft; DWORD pid, tid; LONG n; void *p; } seed;
    memset(&seed, 0, sizeof seed);
    QueryPerformanceCounter(&seed.qpc);
    GetSystemTimeAsFileTime(&seed.ft);
    seed.pid = GetCurrentProcessId();
    seed.tid = GetCurrentThreadId();
    seed.n = InterlockedIncrement(&serial);
    seed.p = (void *)&seed;
    uint8_t d[32];
    px_sha256(&seed, sizeof seed, d);
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) { out[2 * i] = hx[d[i] >> 4]; out[2 * i + 1] = hx[d[i] & 15]; }
    out[32] = 0;
}

bool pxtf_set_editor_source(PxtfVariant *v) {
    free(v->format);
    free(v->type);
    v->format = dup_str(PXTF_EDITOR_FORMAT);
    v->type = dup_str(PXTF_EDITOR_TYPE);
    pxtf_random_hash(v->source);
    return v->format && v->type;
}

bool pxtf_set_text(PxtfVariant *v, PxtfText t, const char *utf8) {
    if (t < 0 || t >= PXTF_TEXT_COUNT) return false;
    free(v->text[t]);
    v->text[t] = NULL;
    if (utf8 && utf8[0]) {
        v->text[t] = dup_str(utf8);
        return v->text[t] != NULL;
    }
    return true;
}

/* ------------------------------------------------------------------ text: escapes, UTF-8, NFC */

char *pxtf_escape(const char *utf8) {
    size_t n = strlen(utf8);
    char *out = (char *)malloc(n * 4 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)utf8[i];
        if (c == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\r') {
            out[o++] = '\\'; out[o++] = 'n';
            if (i + 1 < n && utf8[i + 1] == '\n') i++;
        }
        else if (c == 0xC0 && i + 1 < n && (unsigned char)utf8[i + 1] == 0x80) { o += (size_t)sprintf(out + o, "\\x00"); i++; }
        else if (c < 0x20 || c == 0x7F) o += (size_t)sprintf(out + o, "\\x%02X", c);
        else out[o++] = (char)c;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
    return out;
}

bool pxtf_unescape(const char *e, char **out, char err[PX_ERR_LEN]) {
    size_t n = strlen(e);
    char *r = (char *)malloc(n + 1);
    if (!r) return fail(err, "out of memory");
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i] != '\\') { r[o++] = e[i]; continue; }
        char c = e[i + 1];
        if (c == '\\') { r[o++] = '\\'; i++; }
        else if (c == 't') { r[o++] = '\t'; i++; }
        else if (c == 'n') { r[o++] = '\n'; i++; }
        else if (c == 'x' && ((e[i + 2] >= '0' && e[i + 2] <= '9') || (e[i + 2] >= 'A' && e[i + 2] <= 'F')) &&
                 ((e[i + 3] >= '0' && e[i + 3] <= '9') || (e[i + 3] >= 'A' && e[i + 3] <= 'F'))) {
            int hi = e[i + 2] <= '9' ? e[i + 2] - '0' : e[i + 2] - 'A' + 10;
            int lo = e[i + 3] <= '9' ? e[i + 3] - '0' : e[i + 3] - 'A' + 10;
            if (hi * 16 + lo == 0) { r[o++] = (char)0xC0; r[o++] = (char)0x80; } else r[o++] = (char)(hi * 16 + lo);  /* U+0000 is kept as the two bytes C0 80 */
            i += 3;
        } else {
            free(r);
            return fail(err, "'%.12s' has a backslash that is no escape", e);
        }
    }
    r[o] = 0;
    *out = r;
    return true;
}

/* decodes one code point at s[*i]; false on invalid UTF-8 (strict) */
static bool utf8_next(const unsigned char *s, size_t len, size_t *i, uint32_t *cp) {
    unsigned char c = s[*i];
    if (c < 0x80) { *cp = c; (*i)++; return true; }
    int n;
    uint32_t v, min;
    if (c >= 0xC2 && c <= 0xDF) { n = 1; v = c & 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 2; v = c & 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 3; v = c & 0x07; min = 0x10000; }
    else return false;
    if (*i + (size_t)n >= len) return false;
    for (int k = 1; k <= n; k++) {
        unsigned char d = s[*i + (size_t)k];
        if ((d & 0xC0) != 0x80) return false;
        v = (v << 6) | (d & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return false;
    *cp = v;
    *i += (size_t)n + 1;
    return true;
}

bool pxtf_utf8_valid(const char *s, size_t len) {
    size_t i = 0;
    uint32_t cp;
    while (i < len) if (!utf8_next((const unsigned char *)s, len, &i, &cp)) return false;
    return true;
}

static bool composes(uint32_t base, uint32_t mark) {
    const char *set;
    switch (mark) {
    case 0x300: set = "AEIOUaeiouNnWwYy"; break;
    case 0x301: set = "AEIOUYaeiouyCcLlNnRrSsZzGgKkMmPpWw"; break;
    case 0x302: set = "AEIOUaeiouCcGgHhJjSsWwYyZz"; break;
    case 0x303: set = "AEIOUaeiouNnYyVv"; break;
    case 0x308: set = "AEIOUYaeiouyHhWwXxt"; break;
    case 0x30A: set = "AaUuWwYy"; break;
    case 0x30C: set = "AaCcDdEeGgHhIiKkLlNnOoRrSsTtUuZz"; break;
    case 0x327: set = "CcDdEeGgHhKkLlNnRrSsTt"; break;
    default: return false;
    }
    return base < 0x80 && base != 0 && strchr(set, (int)base) != NULL;
}

bool pxtf_nfc_ok(const char *s, size_t len) {
    size_t i = 0;
    uint32_t cp, prev = 0;
    bool ascii = true;
    for (size_t k = 0; k < len; k++) if ((unsigned char)s[k] >= 0x80) { ascii = false; break; }
    if (ascii) return true;
    while (i < len) {
        if (!utf8_next((const unsigned char *)s, len, &i, &cp)) return false;
        if (composes(prev, cp)) return false;
        prev = cp;
    }
    return true;
}

/* ------------------------------------------------------------------ writer */

typedef struct Buf { char *p; size_t n, cap; bool bad; } Buf;

static void buf_add(Buf *b, const char *s, size_t n) {
    if (b->bad) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 1024;
        while (nc < b->n + n + 1) nc *= 2;
        char *np = (char *)realloc(b->p, nc);
        if (!np) { b->bad = true; return; }
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void buf_str(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

static void upper_hash(const char *lower, char *out) {
    int i = 0;
    for (; lower[i] && i < 32; i++) out[i] = (char)((lower[i] >= 'a' && lower[i] <= 'f') ? lower[i] - 32 : lower[i]);
    out[i] = 0;
}

static int cmp_ptr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* the sorted, deduped lines of a variant (malloc'd strings, without the leading tab) */
typedef struct Lines { char **v; int n, cap; } Lines;

static bool lines_push(Lines *L, char *s) { /* takes ownership */
    if (!s) return false;
    if (L->n == L->cap) {
        int nc = L->cap ? L->cap * 2 : 32;
        char **nv = (char **)realloc(L->v, sizeof(char *) * (size_t)nc);
        if (!nv) { free(s); return false; }
        L->v = nv;
        L->cap = nc;
    }
    L->v[L->n++] = s;
    return true;
}

static char *join2(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    char *r = (char *)malloc(la + 1 + lb + 1);
    if (!r) return NULL;
    memcpy(r, a, la);
    r[la] = '\t';
    memcpy(r + la + 1, b, lb + 1);
    return r;
}

static void lines_free(Lines *L) {
    for (int i = 0; i < L->n; i++) free(L->v[i]);
    free(L->v);
    memset(L, 0, sizeof *L);
}

static bool variant_lines(const PxtfVariant *v, Lines *L) {
    for (int t = 0; t < PXTF_TEXT_COUNT; t++) {
        if (!v->text[t]) continue;
        char *e = pxtf_escape(v->text[t]);
        if (!e) return false;
        if (e[0]) { if (!lines_push(L, join2(TEXT_KEY[t], e))) { free(e); return false; } }
        free(e);
    }
    if (v->italic && !lines_push(L, dup_str("ITALIC"))) return false;
    if (v->struckout && !lines_push(L, dup_str("STRUCKOUT"))) return false;
    if (v->underlined && !lines_push(L, dup_str("UNDERLINED"))) return false;
    char up[33];
    upper_hash(v->pxfm, up);
    if (!lines_push(L, join2("PXFM", up))) return false;
    for (int i = 0; i < v->nopts; i++) if (!lines_push(L, dup_str(v->opts[i]))) return false;
    if (v->format && !lines_push(L, join2("SOURCE-FORMAT", v->format))) return false;
    if (v->source[0]) { upper_hash(v->source, up); if (!lines_push(L, join2("SOURCE-HASH", up))) return false; }
    if (v->type) {
        char *e = pxtf_escape(v->type);
        if (!e) return false;
        bool ok = lines_push(L, join2("SOURCE-TYPE", e));
        free(e);
        if (!ok) return false;
    }
    qsort(L->v, (size_t)L->n, sizeof(char *), cmp_ptr);
    int w = 0;   /* a line that would occur twice is written once */
    for (int i = 0; i < L->n; i++) {
        if (w > 0 && strcmp(L->v[w - 1], L->v[i]) == 0) { free(L->v[i]); continue; }
        L->v[w++] = L->v[i];
    }
    L->n = w;
    return true;
}

char *pxtf_text(const PxtfTypeface *c, size_t *len, char hashOut[33]) {
    Buf b = {0};
    if (c->name) {   /* the top line */
        char *e = pxtf_escape(c->name);
        if (!e) return NULL;
        if (e[0]) { buf_str(&b, "NAME\t"); buf_str(&b, e); buf_str(&b, "\n"); }
        free(e);
    }
    /* variants in ascending number */
    int *order = (int *)malloc(sizeof(int) * (size_t)(c->count > 0 ? c->count : 1));
    if (!order) { free(b.p); return NULL; }
    for (int i = 0; i < c->count; i++) order[i] = i;
    for (int i = 1; i < c->count; i++) {   /* insertion sort: few variants */
        int k = order[i], j = i - 1;
        while (j >= 0 && c->v[order[j]].number > c->v[k].number) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }
    for (int oi = 0; oi < c->count; oi++) {
        const PxtfVariant *v = &c->v[order[oi]];
        char num[24];
        snprintf(num, sizeof num, "%04d\n", v->number);
        buf_str(&b, num);
        Lines L = {0};
        if (!variant_lines(v, &L)) { lines_free(&L); free(order); free(b.p); return NULL; }
        for (int i = 0; i < L.n; i++) {
            if (i > 0 && strcmp(L.v[i], L.v[i - 1]) == 0) continue;
            buf_str(&b, "\t");
            buf_str(&b, L.v[i]);
            buf_str(&b, "\n");
        }
        lines_free(&L);
    }
    free(order);
    if (b.bad) { free(b.p); return NULL; }
    if (len) *len = b.n;
    if (hashOut) px_hash128_hex(b.p, b.n, hashOut, false);
    return b.p;
}

void pxtf_hash(const PxtfTypeface *c, char out[33]) {
    size_t n;
    char *t = pxtf_text(c, &n, out);
    if (!t) out[0] = 0;
    free(t);
}

bool pxtf_save(const PxtfTypeface *c, const char *root, char hashOut[33], char *pathOut, size_t pathSize,
               char err[PX_ERR_LEN]) {
    size_t n;
    char hash[33];
    char *t = pxtf_text(c, &n, hash);
    if (!t) return fail(err, "out of memory");
    char dir[1024], path[1100];
    snprintf(dir, sizeof dir, "%s/pxtf/%c%c", root, hash[0], hash[1]);
    snprintf(path, sizeof path, "%s/%s.pxtf", dir, hash);
    if (!px_file_exists(path)) {
        if (!px_mkdir_p(dir)) { free(t); return fail(err, "cannot create directory %s", dir); }
        if (!px_write_file(path, t, n)) { free(t); return fail(err, "cannot write %s", path); }
    }
    free(t);
    if (hashOut) memcpy(hashOut, hash, 33);
    if (pathOut && pathSize) snprintf(pathOut, pathSize, "%s", path);
    return true;
}

/* ------------------------------------------------------------------ reader */

static bool all_digits(const char *s) {
    if (!*s) return false;
    for (; *s; s++) if (*s < '0' || *s > '9') return false;
    return true;
}

static bool is_integer(const char *s) { /* -?(0|[1-9][0-9]*) */
    if (*s == '-') s++;
    if (!all_digits(s)) return false;
    if (s[0] == '0' && s[1]) return false;
    return true;
}

static bool is_code6(const char *s) {
    if (strlen(s) != 6) return false;
    for (int i = 0; i < 6; i++) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F'))) return false;
    return true;
}

/* Python's repr of a float (upper-cased), or the integer when whole: the spec's number form. */
static void format_number(double v, char *buf) {
    if (v == floor(v) && fabs(v) < 1e15) {
        snprintf(buf, 40, "%.0f", v);
        if (strcmp(buf, "-0") == 0) strcpy(buf, "0");
        return;
    }
    bool neg = v < 0;
    double a = fabs(v);
    char t[64];
    int p;
    for (p = 1; p <= 17; p++) {
        snprintf(t, sizeof t, "%.*e", p - 1, a);
        if (strtod(t, NULL) == a) break;
    }
    char ds[24];
    int nd = 0;
    const char *q = t;
    while (*q && *q != 'e') { if (*q != '.') ds[nd++] = *q; q++; }
    ds[nd] = 0;
    int ex = (*q == 'e') ? atoi(q + 1) : 0;
    while (nd > 1 && ds[nd - 1] == '0') ds[--nd] = 0;
    char *o = buf;
    if (neg) *o++ = '-';
    if (ex >= -4 && ex < 16) {
        if (ex >= 0) {
            for (int i = 0; i <= ex; i++) *o++ = (i < nd) ? ds[i] : '0';
            *o++ = '.';
            if (nd > ex + 1) { for (int i = ex + 1; i < nd; i++) *o++ = ds[i]; }
            else *o++ = '0';
        } else {
            *o++ = '0'; *o++ = '.';
            for (int i = 0; i < -ex - 1; i++) *o++ = '0';
            for (int i = 0; i < nd; i++) *o++ = ds[i];
        }
        *o = 0;
    } else {
        *o++ = ds[0];
        if (nd > 1) { *o++ = '.'; for (int i = 1; i < nd; i++) *o++ = ds[i]; }
        snprintf(o, 8, "E%c%02d", ex < 0 ? '-' : '+', abs(ex) % 1000);
    }
}

static bool is_number(const char *s) {
    char *end;
    double v = strtod(s, &end);
    if (end == s || *end || !isfinite(v)) return false;
    char t[64];
    format_number(v, t);
    return strcmp(t, s) == 0;
}

static bool is_format(const char *s) {
    if (!*s) return false;
    for (; *s; s++) if (!((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z'))) return false;
    return true;
}

static bool is_variant_number(const char *s) { /* [0-9]{4}|[1-9][0-9]{4,} */
    size_t n = strlen(s);
    if (!all_digits(s) || n < 4) return false;
    if (n > 4 && s[0] == '0') return false;
    return true;
}

static const char *const OPT_KEYS[] = {"ADVANCEGAP", "CELL", "CHARSET", "GRID", "IGNORE", "NINTHCOLUMN",
                                       "PFM", "RECORDSIZE", "SPACEADVANCE"};

/* ------------------------------------------------------------------ licence terms: the words */

static const PxtfWord W_PERMISSION[] = {{"yes", "allowed"}, {"no", "not allowed"}, {"ask", "ask the holder"},
                                        {"not-stated", "not stated"}, {"unknown", "unknown"}};
static const PxtfWord W_CLASS[] = {
    {"cc0", "CC0"}, {"public-domain", "Public domain"}, {"ofl", "SIL OFL"}, {"mit", "MIT"}, {"gpl", "GPL"},
    {"cc-by", "CC BY"}, {"cc-by-sa", "CC BY-SA"}, {"cc-by-nd", "CC BY-ND"}, {"cc-by-nc", "CC BY-NC"},
    {"free", "Free (own terms)"}, {"free-personal", "Free for personal use"}, {"free-unspecified", "Free, unspecified"},
    {"shareware-demo", "Shareware or demo"}, {"proprietary", "Proprietary"}, {"unknown", "Unknown"}};
static const PxtfWord W_CONFIDENCE[] = {{"high", "high"}, {"medium", "medium"}, {"low", "low"}};
static const PxtfWord W_OBLIGATION[] = {
    {"attribution", "credit the author"}, {"device-limit", "a limited number of computers"},
    {"embed-only", "passed on only embedded in a work"}, {"include-license", "the licence text goes with the font"},
    {"include-notice", "the notice or readme goes with the font"}, {"no-collections", "not in font collections"},
    {"no-embedding", "no embedding"}, {"no-nft-crypto", "no NFTs or cryptocurrencies"}, {"no-sale", "not to be sold"},
    {"no-sale-alone", "not to be sold by itself"}, {"notify-author", "tell the author"},
    {"only-bundled-in-project", "passed on only in one's own project"}, {"rename", "a changed version gets another name"},
    {"rename-if-reserved-name", "a changed version cannot keep the Reserved Font Name"},
    {"share-alike", "a changed version under the same licence"}};
static const PxtfWord W_PROVENANCE[] = {{"original", "the author's own"}, {"vendor-original", "a vendor's design"},
                                        {"derived", "derived"}, {"possibly-derived", "possibly derived"}, {"unknown", "unknown"}};
static const PxtfWord W_UPSTREAM[] = {{"cleared", "cleared"}, {"not-cleared", "not cleared"}, {"unknown", "unknown"}};
static const PxtfWord W_USELIMIT[] = {{"personal-only", "personal use only"}, {"non-commercial", "non-commercial use only"}};

#define NW(a) (int)(sizeof a / sizeof *a)
const PxtfWord *pxtf_terms_words(const char *key, int *n) {
    if (!key) return NULL;
    if (!strcmp(key, "TERMS-COMMERCIAL") || !strcmp(key, "TERMS-MODIFY") || !strcmp(key, "TERMS-REDISTRIBUTE") ||
        !strcmp(key, "TERMS-REDISTRIBUTE-MODIFIED")) { *n = NW(W_PERMISSION); return W_PERMISSION; }
    if (!strcmp(key, "TERMS-CLASS")) { *n = NW(W_CLASS); return W_CLASS; }
    if (!strcmp(key, "TERMS-CONFIDENCE")) { *n = NW(W_CONFIDENCE); return W_CONFIDENCE; }
    if (!strcmp(key, "TERMS-OBLIGATION")) { *n = NW(W_OBLIGATION); return W_OBLIGATION; }
    if (!strcmp(key, "TERMS-PROVENANCE")) { *n = NW(W_PROVENANCE); return W_PROVENANCE; }
    if (!strcmp(key, "TERMS-UPSTREAM-STATUS")) { *n = NW(W_UPSTREAM); return W_UPSTREAM; }
    if (!strcmp(key, "TERMS-USE-LIMIT")) { *n = NW(W_USELIMIT); return W_USELIMIT; }
    return NULL;
}
#undef NW

const char *pxtf_terms_meaning(const char *key, const char *word) {
    if (!word) return "";
    int n = 0;
    const PxtfWord *w = pxtf_terms_words(key, &n);
    for (int i = 0; w && i < n; i++) if (!strcmp(w[i].word, word)) return w[i].meaning;
    return word;
}

const char *pxtf_opt(const PxtfVariant *v, const char *key) {
    size_t kl = strlen(key);
    for (int i = 0; v && i < v->nopts; i++)
        if (!strncmp(v->opts[i], key, kl) && (v->opts[i][kl] == '\t' || !v->opts[i][kl])) return v->opts[i][kl] ? v->opts[i] + kl + 1 : "";
    return NULL;
}

int pxtf_opt_all(const PxtfVariant *v, const char *key, const char **out, int max) {
    size_t kl = strlen(key);
    int n = 0;
    for (int i = 0; v && i < v->nopts && n < max; i++)
        if (!strncmp(v->opts[i], key, kl) && v->opts[i][kl] == '\t') out[n++] = v->opts[i] + kl + 1;
    return n;
}

const char *pxtf_license_short(const PxtfVariant *v) {
    if (!v) return "";
    if (v->text[PXTF_TERMS_SPDX] && v->text[PXTF_TERMS_SPDX][0]) return v->text[PXTF_TERMS_SPDX];
    if (v->text[PXTF_TERMS_LICENSE] && v->text[PXTF_TERMS_LICENSE][0]) return v->text[PXTF_TERMS_LICENSE];
    const char *c = pxtf_opt(v, "TERMS-CLASS");
    if (c) return pxtf_terms_meaning("TERMS-CLASS", c);
    if (v->text[PXTF_LICENSE] && v->text[PXTF_LICENSE][0]) return v->text[PXTF_LICENSE];
    return "";
}

static bool is_date(const char *s) {   /* YYYY-MM-DD, a real date */
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && !(s[i] >= '0' && s[i] <= '9')) return false;
    int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
    static const int dm[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12 || d < 1 || d > dm[m - 1]) return false;
    if (m == 2 && d == 29 && !((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return false;
    return true;
}

/* a TERMS- key of a word, a date or a hash: 1 (msg set and -1 when bad), 0 when the key is not one */
static int check_terms(const char *key, char **f, int nf, char *msg, size_t msgSize) {
    if (strncmp(key, "TERMS-", 6) != 0) return 0;
    int n = 0;
    const PxtfWord *w = pxtf_terms_words(key, &n);
    bool date = !strcmp(key, "TERMS-DATE"), text = !strcmp(key, "TERMS-TEXT");
    if (!w && !date && !text) return 0;
    if (nf != 1) { snprintf(msg, msgSize, "%s takes one field", key); return -1; }
    if (date) { if (!is_date(f[0])) { snprintf(msg, msgSize, "TERMS-DATE '%s' is not a date YYYY-MM-DD", f[0]); return -1; } return 1; }
    if (text) { if (!px_is_hash(f[0], true, false)) { snprintf(msg, msgSize, "TERMS-TEXT '%s' is not 32 upper-case hex digits", f[0]); return -1; } return 1; }
    for (int i = 0; i < n; i++) if (!strcmp(w[i].word, f[0])) return 1;
    snprintf(msg, msgSize, "%s '%s' is not one of its words", key, f[0]);
    return -1;
}

/* the rules across the TERMS- lines of a variant (spec "Errors") */
static bool terms_ok(const PxtfVariant *v, char *msg, size_t msgSize) {
    const char *pv = pxtf_opt(v, "TERMS-PROVENANCE");
    bool derived = pv && (!strcmp(pv, "derived") || !strcmp(pv, "possibly-derived"));
    if (!derived && (v->text[PXTF_TERMS_DERIVED_FROM] || v->text[PXTF_TERMS_UPSTREAM_HOLDER] || pxtf_opt(v, "TERMS-UPSTREAM-STATUS"))) {
        snprintf(msg, msgSize, "TERMS-DERIVED-FROM, TERMS-UPSTREAM-HOLDER and TERMS-UPSTREAM-STATUS need TERMS-PROVENANCE derived or possibly-derived");
        return false;
    }
    const char *cm = pxtf_opt(v, "TERMS-COMMERCIAL");
    if (cm && !strcmp(cm, "yes") && pxtf_opt(v, "TERMS-USE-LIMIT")) { snprintf(msg, msgSize, "TERMS-USE-LIMIT with TERMS-COMMERCIAL yes"); return false; }
    return true;
}

/* ------------------------------------------------------------------ licence texts */

char *pxtf_license_canonical(const char *data, size_t len, size_t *outLen, char err[PX_ERR_LEN]) {
    if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) { fail(err, "a byte order mark"); return NULL; }
    if (!pxtf_utf8_valid(data, len)) { fail(err, "not valid UTF-8"); return NULL; }
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)data[i];
        if ((ch < 0x20 && ch != '\t' && ch != '\r' && ch != '\n') || ch == 0x7F) { fail(err, "a control character 0x%02X", ch); return NULL; }
    }
    if (!pxtf_nfc_ok(data, len)) { fail(err, "not in NFC"); return NULL; }
    char *out = (char *)malloc(len + 2);
    if (!out) { fail(err, "out of memory"); return NULL; }
    size_t o = 0, i = 0;
    bool pendingBlank = false;   /* an empty line seen after some text */
    while (i < len) {
        size_t s = i;
        while (i < len && data[i] != '\r' && data[i] != '\n') i++;
        size_t e = i;
        if (i < len) { if (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n') i += 2; else i++; }
        while (e > s && (data[e - 1] == ' ' || data[e - 1] == '\t')) e--;
        if (e == s) { if (o > 0) pendingBlank = true; continue; }
        if (pendingBlank) { out[o++] = '\n'; pendingBlank = false; }
        memcpy(out + o, data + s, e - s);
        o += e - s;
        out[o++] = '\n';
    }
    if (o == 0) { free(out); fail(err, "empty"); return NULL; }
    out[o] = 0;
    *outLen = o;
    return out;
}

/* checks one variant line's fields for a reading option; returns 1 if the key is an option (err set when bad), 0 if not */
static int check_option(const char *key, char **f, int nf, char *msg, size_t msgSize) {
    int known = 0;
    for (size_t i = 0; i < sizeof OPT_KEYS / sizeof *OPT_KEYS; i++) if (strcmp(key, OPT_KEYS[i]) == 0) known = 1;
    if (!known) return 0;
#define BAD(...) do { snprintf(msg, msgSize, __VA_ARGS__); return -1; } while (0)
    if (strcmp(key, "GRID") == 0) {
        if (nf != 4 && nf != 5) BAD("GRID takes x, y, x phase, y phase and maybe PERGLYPH");
        if (nf == 5 && strcmp(f[4], "PERGLYPH") != 0) BAD("GRID: the fifth field is PERGLYPH or nothing");
        for (int i = 0; i < 4; i++) if (!is_number(f[i])) BAD("GRID '%s' is not a number in its shortest form", f[i]);
        return 1;
    }
    if (strcmp(key, "IGNORE") == 0) {
        if (nf != 2 || !is_code6(f[0]) || !is_code6(f[1])) BAD("IGNORE takes two code points");
        return 1;
    }
    if (strcmp(key, "NINTHCOLUMN") == 0) {
        if (nf != 2 || !is_integer(f[0]) || !is_integer(f[1])) BAD("NINTHCOLUMN takes two integers");
        return 1;
    }
    if (nf != 1) BAD("%s takes one field", key);
    if (strcmp(key, "CELL") == 0) {
        const char *x = strchr(f[0], 'x');
        char w[24];
        if (!x || x == f[0] || (size_t)(x - f[0]) >= sizeof w) BAD("CELL '%s' is not <width>x<height>", f[0]);
        memcpy(w, f[0], (size_t)(x - f[0]));
        w[x - f[0]] = 0;
        if (!all_digits(w) || w[0] == '0' || !all_digits(x + 1) || x[1] == '0') BAD("CELL '%s' is not <width>x<height>", f[0]);
    } else if (strcmp(key, "PFM") == 0) {
        if (!px_is_hash(f[0], true, false)) BAD("PFM '%s' is not 32 upper-case hex digits", f[0]);
    } else {
        if (!is_integer(f[0])) BAD("%s '%s' is not an integer", key, f[0]);
    }
    return 1;
#undef BAD
}

static bool variant_push(PxtfTypeface *c, int number) {
    if (c->count == c->cap) {
        int nc = c->cap ? c->cap * 2 : 4;
        PxtfVariant *nv = (PxtfVariant *)realloc(c->v, sizeof(PxtfVariant) * (size_t)nc);
        if (!nv) return false;
        c->v = nv;
        c->cap = nc;
    }
    memset(&c->v[c->count], 0, sizeof(PxtfVariant));
    c->v[c->count].number = number;
    c->count++;
    return true;
}

bool pxtf_parse(const char *data, size_t len, PxtfTypeface *out, char err[PX_ERR_LEN]) {
    if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        return fail(err, "a byte order mark");
    if (!pxtf_utf8_valid(data, len)) return fail(err, "not valid UTF-8");
    size_t cl;
    char *can = px_canonical(data, len, &cl);
    if (!can) return fail(err, "out of memory");
    if (!pxtf_nfc_ok(can, cl)) { free(can); return fail(err, "not in NFC"); }

    char *orig = (char *)malloc(cl + 1);   /* the lines are split in place below; keep the text to compare with */
    if (!orig) { free(can); return fail(err, "out of memory"); }
    memcpy(orig, can, cl + 1);
    PxtfTypeface c;
    pxtf_init(&c);
    bool haveName = false;
    PxtfVariant *cur = NULL;
    bool seenText[PXTF_TEXT_COUNT] = {false};
    bool seenFlag[3] = {false}, seenPxfm = false, seenFormat = false, seenType = false, seenSource = false;
    int lineNo = 0;
    char msg[2 * PX_ERR_LEN];
    char *p = can;

#define LFAIL(...) do { snprintf(msg, sizeof msg, __VA_ARGS__); fail(err, "line %d: %.400s", lineNo, msg); goto bad; } while (0)
#define CLOSE_VARIANT() do { if (cur && (!seenPxfm || !seenFormat || !seenSource || !seenType)) { lineNo = -1; \
        fail(err, "variant %04d has no %s line", cur->number, !seenPxfm ? "PXFM" : !seenFormat ? "SOURCE-FORMAT" : !seenSource ? "SOURCE-HASH" : "SOURCE-TYPE"); goto bad; } \
        if (cur) { char tm[200]; if (!terms_ok(cur, tm, sizeof tm)) { fail(err, "variant %04d: %s", cur->number, tm); goto bad; } } } while (0)

    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        *nl = 0;
        p = nl + 1;
        lineNo++;

        if (line[0] != '\t') {
            if (is_variant_number(line)) {
                CLOSE_VARIANT();
                long num = atol(line);
                if (num < 1 || num > 99999999) LFAIL("variant number %s out of range", line);
                if (c.count > 0 && c.v[c.count - 1].number >= (int)num) LFAIL("variant %s is not in ascending order or twice", line);
                if (!variant_push(&c, (int)num)) LFAIL("out of memory");
                cur = &c.v[c.count - 1];
                memset(seenText, 0, sizeof seenText);
                memset(seenFlag, 0, sizeof seenFlag);
                seenPxfm = seenFormat = seenType = seenSource = false;
                continue;
            }
            if (cur) LFAIL("a top line after a variant");
            char *tab = strchr(line, '\t');
            if (!tab || strchr(tab + 1, '\t') || !tab[1]) LFAIL("top line '%.40s' takes one field", line);
            *tab = 0;
            const char *val = tab + 1;
            if (strcmp(line, "NAME") == 0) {
                if (haveName) LFAIL("two NAME lines");
                char e2[PX_ERR_LEN];
                if (!pxtf_unescape(val, &c.name, e2)) LFAIL("NAME: %s", e2);
                haveName = true;
            } else {
                LFAIL("a top line of the key '%s' (only NAME is one; FORMAT, HASH and TYPE are version 1)", line);
            }
            continue;
        }
        if (!cur) LFAIL("a variant line before any variant number");
        char *fld[8];
        int nf = 0;
        char *s = line + 1;
        for (;;) {
            if (nf >= 7) LFAIL("too many fields");
            fld[nf++] = s;
            char *t = strchr(s, '\t');
            if (!t) break;
            *t = 0;
            s = t + 1;
        }
        const char *key = fld[0];
        int na = nf - 1;
        for (int i = 1; i < nf; i++) if (!fld[i][0]) LFAIL("empty field");
        int ti = -1;
        for (int i = 0; i < PXTF_TEXT_COUNT; i++) if (strcmp(key, TEXT_KEY[i]) == 0) ti = i;
        if (ti >= 0) {
            if (na != 1) LFAIL("%s takes one field", key);
            if (seenText[ti]) LFAIL("two %s lines", key);
            seenText[ti] = true;
            char e2[PX_ERR_LEN];
            if (!pxtf_unescape(fld[1], &cur->text[ti], e2)) LFAIL("%s: %s", key, e2);
        } else if (strcmp(key, "ITALIC") == 0 || strcmp(key, "STRUCKOUT") == 0 || strcmp(key, "UNDERLINED") == 0) {
            int fi = key[0] == 'I' ? 0 : key[0] == 'S' ? 1 : 2;
            if (na != 0) LFAIL("%s takes no field", key);
            if (seenFlag[fi]) LFAIL("two %s lines", key);
            seenFlag[fi] = true;
            if (fi == 0) cur->italic = true; else if (fi == 1) cur->struckout = true; else cur->underlined = true;
        } else if (strcmp(key, "PXFM") == 0) {
            if (na != 1 || !px_is_hash(fld[1], true, false)) LFAIL("PXFM is not 32 upper-case hex digits");
            if (seenPxfm) LFAIL("two PXFM lines");
            seenPxfm = true;
            for (int i = 0; i < 32; i++) cur->pxfm[i] = (char)((fld[1][i] >= 'A' && fld[1][i] <= 'F') ? fld[1][i] + 32 : fld[1][i]);
            cur->pxfm[32] = 0;
        } else if (strcmp(key, "SOURCE-FORMAT") == 0) {
            if (na != 1 || !is_format(fld[1])) LFAIL("SOURCE-FORMAT is not upper-case letters and digits");
            if (seenFormat) LFAIL("two SOURCE-FORMAT lines");
            seenFormat = true;
            cur->format = dup_str(fld[1]);
        } else if (strcmp(key, "SOURCE-HASH") == 0) {
            if (na != 1 || !px_is_hash(fld[1], true, false)) LFAIL("SOURCE-HASH is not 32 upper-case hex digits");
            if (seenSource) LFAIL("two SOURCE-HASH lines");
            seenSource = true;
            for (int i = 0; i < 32; i++) cur->source[i] = (char)((fld[1][i] >= 'A' && fld[1][i] <= 'F') ? fld[1][i] + 32 : fld[1][i]);
            cur->source[32] = 0;
        } else if (strcmp(key, "SOURCE-TYPE") == 0) {
            char e2[PX_ERR_LEN];
            if (na != 1) LFAIL("SOURCE-TYPE takes one field");
            if (seenType) LFAIL("two SOURCE-TYPE lines");
            seenType = true;
            if (!pxtf_unescape(fld[1], &cur->type, e2)) LFAIL("SOURCE-TYPE: %s", e2);
        } else {
            char m2[200];
            int r = check_option(key, fld + 1, na, m2, sizeof m2);
            if (r == 0) r = check_terms(key, fld + 1, na, m2, sizeof m2);
            if (r == 0) LFAIL("unknown key '%s'", key);
            if (r < 0) LFAIL("%s", m2);
            if (strcmp(key, "IGNORE") != 0 && strcmp(key, "TERMS-TEXT") != 0 && strcmp(key, "TERMS-OBLIGATION") != 0)
                for (int i = 0; i < cur->nopts; i++) {
                    size_t kl = strlen(key);
                    if (strncmp(cur->opts[i], key, kl) == 0 && (cur->opts[i][kl] == '\t' || !cur->opts[i][kl])) LFAIL("two %s lines", key);
                }
            /* raw line: key and fields joined by tabs (re-join what was split) */
            size_t total = strlen(key);
            for (int i = 1; i < nf; i++) total += 1 + strlen(fld[i]);
            char *raw = (char *)malloc(total + 1);
            if (!raw) LFAIL("out of memory");
            size_t o = 0;
            for (int i = 0; i < nf; i++) {
                if (i) raw[o++] = '\t';
                size_t l = strlen(fld[i]);
                memcpy(raw + o, fld[i], l);
                o += l;
            }
            raw[o] = 0;
            char **nv = (char **)realloc(cur->opts, sizeof(char *) * (size_t)(cur->nopts + 1));
            if (!nv) { free(raw); LFAIL("out of memory"); }
            cur->opts = nv;
            cur->opts[cur->nopts++] = raw;
        }
    }
    CLOSE_VARIANT();
#undef CLOSE_VARIANT
#undef LFAIL

    if (c.count == 0 && !haveName) { fail(err, "no variant and no NAME line"); goto bad; }
    {
        size_t n2;
        char *again = pxtf_text(&c, &n2, NULL);
        if (!again || n2 != cl || memcmp(again, orig, cl) != 0) {
            free(again);
            fail(err, "the lines are not in canonical order (or a value is not in its canonical form)");
            goto bad;
        }
        free(again);
    }
    free(can);
    free(orig);
    *out = c;
    return true;

bad:
    free(can);
    free(orig);
    pxtf_free(&c);
    return false;
}

bool pxtf_read(const char *path, PxtfTypeface *out, char err[PX_ERR_LEN]) {
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) return fail(err, "cannot read %s", path);
    char e2[PX_ERR_LEN];
    bool ok = pxtf_parse(raw, n, out, e2);
    free(raw);
    if (!ok) return fail(err, "%s: %s", path, e2);
    return true;
}
