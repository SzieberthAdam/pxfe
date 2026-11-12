/* pxfont.c - pixel font model, pxfm v2 (F-codes) / pxgl read and write, SHA-256, drafts v3. See pxfont.h. */
#include "pxfont.h"
#include "pxtf.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PX_MKDIR(p) _mkdir(p)
#else
#include <sys/types.h>
#define PX_MKDIR(p) mkdir((p), 0777)
#endif

static const char PIXELS[] = PX_PIXELS;


/* ------------------------------------------------------------------ SHA-256 */

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void px_sha256(const void *data, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t *p = (const uint8_t *)data;
    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) sha_block(h, p + 64 * i);
    uint8_t tail[128];
    size_t rem = len - full * 64;
    memset(tail, 0, sizeof tail);
    if (rem) memcpy(tail, p + full * 64, rem);
    tail[rem] = 0x80;
    size_t tl = (rem < 56) ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha_block(h, tail);
    if (tl == 128) sha_block(h, tail + 64);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24); out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8); out[4 * i + 3] = (uint8_t)h[i];
    }
}

void px_hash128_hex(const void *data, size_t len, char out[33], bool upper) {
    uint8_t d[32];
    px_sha256(data, len, d);
    const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    for (int i = 0; i < 16; i++) { out[2 * i] = hex[d[i] >> 4]; out[2 * i + 1] = hex[d[i] & 15]; }
    out[32] = 0;
}


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

int px_pixel_index(char c) {
    if (c == '.') return 0;
    if (c >= '0' && c <= '9') return 1 + (c - '0');
    if (c >= 'A' && c <= 'Z') return 11 + (c - 'A');
    return -1;
}

char *px_canonical(const char *text, size_t len, size_t *outLen) {
    char *out = (char *)malloc(len + 2);
    if (!out) return NULL;
    size_t o = 0, i = 0;
    while (i < len) {
        size_t s = i;
        while (i < len && text[i] != '\n' && text[i] != '\r') i++;
        size_t e = i;
        if (i < len) {
            if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') i += 2; else i++;
        }
        while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t')) e--;
        if (e > s) {
            memcpy(out + o, text + s, e - s);
            o += e - s;
            out[o++] = '\n';
        }
    }
    out[o] = 0;
    if (outLen) *outLen = o;
    return out;
}

bool px_mkdir_p(const char *path) {
    char tmp[1024];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof tmp) return false;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            if (tmp[i - 1] == ':' || tmp[i - 1] == '/' || tmp[i - 1] == '\\') continue;
            char c = tmp[i];
            tmp[i] = 0;
            PX_MKDIR(tmp);
            tmp[i] = c;
        }
    }
    PX_MKDIR(tmp);
    struct stat st;
    return stat(tmp, &st) == 0 && (st.st_mode & S_IFDIR);
}

char *px_read_file(const char *path, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    size_t cap = 8192, n = 0;
    if (fseek(fp, 0, SEEK_END) == 0) {
        long sz = ftell(fp);
        if (sz >= 0) cap = (size_t)sz + 1;     /* one extra byte so that EOF is seen by the same fread */
        fseek(fp, 0, SEEK_SET);
    }
    char *b = (char *)malloc(cap + 1);
    while (b) {
        size_t r = fread(b + n, 1, cap - n, fp);
        n += r;
        if (n < cap) break;
        cap *= 2;
        char *nb = (char *)realloc(b, cap + 1);
        if (!nb) { free(b); b = NULL; }
        else b = nb;
    }
    fclose(fp);
    if (!b) return NULL;
    b[n] = 0;
    if (len) *len = n;
    return b;
}

bool px_write_file(const char *path, const void *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    size_t w = fwrite(data, 1, len, fp);
    bool ok = (w == len);
    if (fclose(fp) != 0) ok = false;
    return ok;
}

bool px_file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

bool px_is_hash(const char *s, bool upper, bool lower) {
    if (!s) return false;
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') continue;
        if (c >= 'A' && c <= 'F' && upper) continue;
        if (c >= 'a' && c <= 'f' && lower) continue;
        return false;
    }
    return s[32] == 0;
}

static void hash_upper(const char *lower, char *out) { /* out >= 33 */
    int i = 0;
    for (; lower[i]; i++) out[i] = (char)toupper((unsigned char)lower[i]);
    out[i] = 0;
}

static void hash_lower(const char *upper, char *out) {
    int i = 0;
    for (; upper[i]; i++) out[i] = (char)tolower((unsigned char)upper[i]);
    out[i] = 0;
}

/* ------------------------------------------------------------------ growable text buffer */

typedef struct Buf { char *p; size_t n, cap; bool bad; } Buf;

static void buf_add(Buf *b, const char *s, size_t n) {
    if (b->bad) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 4096;
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

static void buf_printf(Buf *b, const char *fmt, ...) {
    char t[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (n > 0) buf_add(b, t, (size_t)(n < (int)sizeof t ? n : (int)sizeof t - 1));
}

/* ------------------------------------------------------------------ font model */

static const char *const METRIC_KEY[PXM_COUNT] = {
    "ASCENT", "DESCENT", "LINEGAP", "CAPHEIGHT", "XHEIGHT", "INTERNALLEADING", "QUADWIDTH",
    "UNDERLINEPOSITION", "UNDERLINETHICKNESS", "STRIKEOUTPOSITION", "STRIKEOUTTHICKNESS"};
static const char *const METRIC_LABEL[PXM_COUNT] = {
    "Ascent", "Descent", "Line gap", "Cap height", "x-height", "Internal leading", "Quad width",
    "Underline position", "Underline thickness", "Strike-out position", "Strike-out thickness"};

const char *px_metric_key(PxMetric m) { return (m >= 0 && m < PXM_COUNT) ? METRIC_KEY[m] : ""; }
const char *px_metric_label(PxMetric m) { return (m >= 0 && m < PXM_COUNT) ? METRIC_LABEL[m] : ""; }

void px_font_init(PxFont *f) {
    memset(f, 0, sizeof *f);
    f->defaultCodepoint = PX_NO_CODEPOINT;
    for (int i = 0; i < PX_NUM_PIXELS; i++) f->color[i] = 0x000000FFu;
    f->color[0] = 0xFFFFFFFFu;
    f->colorUsed[0] = f->colorUsed[1] = true;
}

void px_glyph_free(PxGlyph *g) {
    free(g->pix);
    g->pix = NULL;
    g->w = g->h = 0;
}

void px_font_free(PxFont *f) {
    for (int i = 0; i < f->count; i++) free(f->glyphs[i].pix);
    free(f->glyphs);
    free(f->kern);
    f->glyphs = NULL; f->kern = NULL;
    f->count = f->capacity = f->kernCount = f->kernCap = 0;
}

bool px_glyph_copy(PxGlyph *dst, const PxGlyph *src) {
    *dst = *src;
    dst->pix = NULL;
    if (src->pix && src->w > 0 && src->h > 0) {
        size_t n = (size_t)src->w * (size_t)src->h;
        dst->pix = (char *)malloc(n);
        if (!dst->pix) { dst->w = dst->h = 0; return false; }
        memcpy(dst->pix, src->pix, n);
    } else {
        dst->w = dst->h = 0;
    }
    return true;
}

bool px_font_copy(PxFont *dst, const PxFont *src) {
    *dst = *src;
    dst->glyphs = NULL; dst->kern = NULL;
    dst->count = dst->capacity = dst->kernCount = dst->kernCap = 0;
    if (src->count > 0) {
        dst->glyphs = (PxGlyph *)malloc(sizeof(PxGlyph) * (size_t)src->count);
        if (!dst->glyphs) return false;
        dst->capacity = src->count;
        for (int i = 0; i < src->count; i++) {
            if (!px_glyph_copy(&dst->glyphs[i], &src->glyphs[i]) && src->glyphs[i].pix) { px_font_free(dst); return false; }
            dst->count++;
        }
    }
    if (src->kernCount > 0) {
        dst->kern = (PxKern *)malloc(sizeof(PxKern) * (size_t)src->kernCount);
        if (!dst->kern) { px_font_free(dst); return false; }
        memcpy(dst->kern, src->kern, sizeof(PxKern) * (size_t)src->kernCount);
        dst->kernCount = dst->kernCap = src->kernCount;
    }
    return true;
}

int px_font_find(const PxFont *f, int32_t codepoint) {
    int lo = 0, hi = f->count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (f->glyphs[mid].codepoint < codepoint) lo = mid + 1; else hi = mid;
    }
    return (lo < f->count && f->glyphs[lo].codepoint == codepoint) ? lo : -1;
}

static bool font_reserve(PxFont *f) {
    if (f->count < f->capacity) return true;
    int nc = f->capacity ? f->capacity * 2 : 64;
    PxGlyph *ng = (PxGlyph *)realloc(f->glyphs, sizeof(PxGlyph) * (size_t)nc);
    if (!ng) return false;
    f->glyphs = ng;
    f->capacity = nc;
    return true;
}

int px_font_add_glyph(PxFont *f, const PxGlyph *g) {
    if (!font_reserve(f)) return -1;
    int lo = 0, hi = f->count;               /* upper bound: after the glyphs of the same code point */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (f->glyphs[mid].codepoint <= g->codepoint) lo = mid + 1; else hi = mid;
    }
    memmove(&f->glyphs[lo + 1], &f->glyphs[lo], sizeof(PxGlyph) * (size_t)(f->count - lo));
    px_glyph_copy(&f->glyphs[lo], g);
    f->count++;
    return lo;
}

int px_font_uncoded_count(const PxFont *f) {
    int n = 0;
    for (int i = f->count - 1; i >= 0 && px_code_is_uncoded(f->glyphs[i].code); i--) n++;
    return n;
}

int px_font_add_uncoded(PxFont *f, const PxGlyph *g) {
    PxGlyph t = *g;
    t.code = PX_CODE_UNCODED_FIRST + px_font_uncoded_count(f);
    return px_font_add_glyph(f, &t);
}

void px_font_remove_glyph(PxFont *f, int index) {
    if (index < 0 || index >= f->count) return;
    int32_t cp = f->glyphs[index].code;
    free(f->glyphs[index].pix);
    memmove(&f->glyphs[index], &f->glyphs[index + 1], sizeof(PxGlyph) * (size_t)(f->count - index - 1));
    f->count--;
    if (px_code_is_uncoded(cp)) {
        /* the later uncoded glyphs move down by one, with their kerning pairs and the default glyph */
        px_font_remove_kern_of(f, cp);
        for (int i = index; i < f->count; i++)
            if (px_code_is_uncoded(f->glyphs[i].code) && f->glyphs[i].code > cp) f->glyphs[i].code--;
        for (int i = 0; i < f->kernCount; i++) {
            if (px_code_is_uncoded(f->kern[i].first) && f->kern[i].first > cp) f->kern[i].first--;
            if (px_code_is_uncoded(f->kern[i].second) && f->kern[i].second > cp) f->kern[i].second--;
        }
        if (f->defaultCodepoint == cp) f->defaultCodepoint = PX_NO_CODEPOINT;
        else if (px_code_is_uncoded(f->defaultCodepoint) && f->defaultCodepoint > cp) f->defaultCodepoint--;
    } else if (px_font_find(f, cp) < 0) {
        if (f->defaultCodepoint == cp) f->defaultCodepoint = PX_NO_CODEPOINT;
        px_font_remove_kern_of(f, cp);
    }
}

void px_font_sort(PxFont *f) { /* stable merge sort by code point */
    if (f->count < 2) return;
    bool sorted = true;
    for (int i = 1; i < f->count; i++) if (f->glyphs[i - 1].codepoint > f->glyphs[i].codepoint) { sorted = false; break; }
    if (sorted) return;
    PxGlyph *tmp = (PxGlyph *)malloc(sizeof(PxGlyph) * (size_t)f->count);
    if (!tmp) return;
    for (int width = 1; width < f->count; width *= 2) {
        for (int lo = 0; lo < f->count; lo += 2 * width) {
            int mid = lo + width < f->count ? lo + width : f->count;
            int hi = lo + 2 * width < f->count ? lo + 2 * width : f->count;
            int a = lo, b = mid, o = lo;
            while (a < mid && b < hi) tmp[o++] = (f->glyphs[b].codepoint < f->glyphs[a].codepoint) ? f->glyphs[b++] : f->glyphs[a++];
            while (a < mid) tmp[o++] = f->glyphs[a++];
            while (b < hi) tmp[o++] = f->glyphs[b++];
        }
        memcpy(f->glyphs, tmp, sizeof(PxGlyph) * (size_t)f->count);
    }
    free(tmp);
}

void px_font_set_default(PxFont *f, int32_t code) {
    f->defaultCodepoint = code;
    f->hasDanglingDef = false;
}

static int kern_cmp(const void *a, const void *b) {
    const PxKern *x = (const PxKern *)a, *y = (const PxKern *)b;
    if (x->first != y->first) return x->first < y->first ? -1 : 1;
    if (x->second != y->second) return x->second < y->second ? -1 : 1;
    return 0;
}

void px_font_set_kern(PxFont *f, int32_t first, int32_t second, int value) {
    for (int i = 0; i < f->kernCount; i++)
        if (f->kern[i].first == first && f->kern[i].second == second) {
            if (value == 0) {
                memmove(&f->kern[i], &f->kern[i + 1], sizeof(PxKern) * (size_t)(f->kernCount - i - 1));
                f->kernCount--;
            } else {
                f->kern[i].value = value;
            }
            return;
        }
    if (value == 0) return;
    if (f->kernCount == f->kernCap) {
        int nc = f->kernCap ? f->kernCap * 2 : 16;
        PxKern *nk = (PxKern *)realloc(f->kern, sizeof(PxKern) * (size_t)nc);
        if (!nk) return;
        f->kern = nk;
        f->kernCap = nc;
    }
    f->kern[f->kernCount].first = first;
    f->kern[f->kernCount].second = second;
    f->kern[f->kernCount].value = value;
    f->kernCount++;
}

int px_font_kern(const PxFont *f, int32_t first, int32_t second) {
    for (int i = 0; i < f->kernCount; i++)
        if (f->kern[i].first == first && f->kern[i].second == second) return f->kern[i].value;
    return 0;
}

void px_font_remove_kern_of(PxFont *f, int32_t codepoint) {
    int o = 0;
    for (int i = 0; i < f->kernCount; i++)
        if (f->kern[i].first != codepoint && f->kern[i].second != codepoint) f->kern[o++] = f->kern[i];
    f->kernCount = o;
}

bool px_font_box(const PxFont *f, int *above, int *below) {
    bool exact = true;
    int a = 0, b = 0;
    if (f->has[PXM_ASCENT]) a = f->m[PXM_ASCENT];
    else {
        for (int i = 0; i < f->count; i++) {
            const PxGlyph *g = &f->glyphs[i];
            if (g->lazy) { if (g->hash[0]) exact = false; continue; }
            if (g->pix && g->h > 0 && g->y + g->h > a) a = g->y + g->h;
        }
    }
    if (f->has[PXM_DESCENT]) b = f->m[PXM_DESCENT];
    else {
        for (int i = 0; i < f->count; i++) {
            const PxGlyph *g = &f->glyphs[i];
            if (g->lazy) { if (g->hash[0] && -g->y > b) b = -g->y; continue; }
            if (g->pix && g->h > 0 && -g->y > b) b = -g->y;
        }
    }
    if (above) *above = a;
    if (below) *below = b;
    return exact;
}

/* ------------------------------------------------------------------ glyph bitmaps and their hash */

char px_glyph_get(const PxGlyph *g, int x, int y) {
    if (!g->pix || x < 0 || y < 0 || x >= g->w || y >= g->h) return '.';
    return g->pix[(size_t)y * g->w + x];
}

/* bounding box of inked pixels; false if none */
static bool ink_bbox(const PxGlyph *g, int *x0, int *y0, int *x1, int *y1) {
    if (!g->pix || g->w <= 0 || g->h <= 0) return false;
    int minx = g->w, miny = g->h, maxx = -1, maxy = -1;
    for (int y = 0; y < g->h; y++) {
        const char *row = g->pix + (size_t)y * g->w;
        for (int x = 0; x < g->w; x++)
            if (row[x] != '.') {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
    }
    if (maxx < 0) return false;
    *x0 = minx; *y0 = miny; *x1 = maxx; *y1 = maxy;
    return true;
}

void px_glyph_touch(PxGlyph *g) {
    if (!g->lazy) g->hashValid = false;
}

void px_glyph_crop(PxGlyph *g) {
    g->asDrawn = false;
    g->hashValid = false;
    int x0, y0, x1, y1;
    if (!ink_bbox(g, &x0, &y0, &x1, &y1)) {
        free(g->pix);
        g->pix = NULL;
        g->w = g->h = 0;
        g->x = g->y = 0;
        return;
    }
    int nw = x1 - x0 + 1, nh = y1 - y0 + 1;
    if (nw == g->w && nh == g->h) return;
    char *np = (char *)malloc((size_t)nw * (size_t)nh);
    if (!np) return;
    for (int y = 0; y < nh; y++) memcpy(np + (size_t)y * nw, g->pix + (size_t)(y + y0) * g->w + x0, (size_t)nw);
    free(g->pix);
    g->pix = np;
    g->x += x0;
    g->y += g->h - 1 - y1;
    g->w = nw;
    g->h = nh;
}

/* The pxgl text the glyph is written as (cropped, or as drawn); NULL when no ink. cdx/cdy: the crop shift. */
static char *glyph_text(const PxGlyph *g, size_t *len, int *cdx, int *cdy) {
    int x0, y0, x1, y1;
    if (!ink_bbox(g, &x0, &y0, &x1, &y1)) return NULL;
    if (g->asDrawn) { x0 = 0; y0 = 0; x1 = g->w - 1; y1 = g->h - 1; }
    int nw = x1 - x0 + 1, nh = y1 - y0 + 1;
    size_t n = (size_t)(nw + 1) * (size_t)nh;
    char *t = (char *)malloc(n + 1);
    if (!t) return NULL;
    size_t o = 0;
    for (int y = 0; y < nh; y++) {
        memcpy(t + o, g->pix + (size_t)(y + y0) * g->w + x0, (size_t)nw);
        o += (size_t)nw;
        t[o++] = '\n';
    }
    t[o] = 0;
    if (len) *len = o;
    if (cdx) *cdx = x0;
    if (cdy) *cdy = g->h - 1 - y1;
    return t;
}

static void glyph_prepare(const PxGlyph *cg) {   /* mutable cache */
    PxGlyph *g = (PxGlyph *)cg;
    if (g->hashValid || g->lazy) return;
    size_t n;
    int dx = 0, dy = 0;
    char *t = glyph_text(g, &n, &dx, &dy);
    if (!t) { g->hash[0] = 0; g->cdx = g->cdy = 0; }
    else { px_hash128_hex(t, n, g->hash, false); g->cdx = dx; g->cdy = dy; free(t); }
    g->hashValid = true;
}

void px_glyph_hash(const PxGlyph *g, char out[33]) {
    glyph_prepare(g);
    memcpy(out, g->hash, 33);
}

void px_glyph_written_offset(const PxGlyph *g, int *x, int *y) {
    glyph_prepare(g);
    if (!g->hash[0]) { *x = 0; *y = 0; return; }
    *x = g->x + g->cdx;
    *y = g->y + g->cdy;
}

char *px_glyph_pxgl(const PxGlyph *g, size_t *len, char hash[33]) {
    if (g->lazy) { hash[0] = 0; return NULL; }
    size_t n;
    char *t = glyph_text(g, &n, NULL, NULL);
    if (!t) { hash[0] = 0; return NULL; }
    px_hash128_hex(t, n, hash, false);
    if (len) *len = n;
    return t;
}

bool px_glyph_same_shape(const PxGlyph *a, const PxGlyph *b) {
    glyph_prepare(a);
    glyph_prepare(b);
    return strcmp(a->hash, b->hash) == 0;
}

bool px_library_root_of(const char *filePath, char *root, size_t rootSize) {
    size_t n = strlen(filePath);
    int seps = 0;
    size_t i = n;
    while (i > 0) {
        i--;
        if (filePath[i] == '/' || filePath[i] == '\\') {
            if (++seps == 3) break;
        }
    }
    if (seps < 3) return false;
    if (i == 0) { /* root is "/" */
        if (rootSize < 2) return false;
        root[0] = filePath[0]; root[1] = 0;
        return true;
    }
    if (i + 1 > rootSize) return false;
    memcpy(root, filePath, i);
    root[i] = 0;
    return true;
}

/* ------------------------------------------------------------------ writer */

static int cmp_lineptr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static bool palette_is_default(const PxFont *f) {
    if (!f->hasPalette) return true;
    for (int i = 0; i < PX_NUM_PIXELS; i++) {
        bool want = (i <= 1);
        if (f->colorUsed[i] != want) return false;
    }
    return f->color[0] == 0xFFFFFFFFu && f->color[1] == 0x000000FFu;
}

/* a kerning pair may name a code point, an F-code of a glyph the font has, or FFFFFF when the file has the line */
static bool kern_code_ok(const PxFont *f, int32_t c, bool hasDefLine) {
    if (px_code_is_codepoint(c)) return true;
    if (c == PX_CODE_DEFAULT) return hasDefLine;
    return px_code_is_uncoded(c) && px_font_find(f, c) >= 0;
}

static void glyph_fields(const PxGlyph *g, char *out) { /* "x,y\tadvance[\tHASH]" */
    int x, y;
    px_glyph_written_offset(g, &x, &y);
    int n = sprintf(out, "%d,%d\t%d", x, y, g->advance);
    if (g->hash[0]) { out[n++] = '\t'; hash_upper(g->hash, out + n); }
}

char *px_pxfm_text(const PxFont *f, size_t *len, char hashOut[33]) {
    Buf b = {0};
    bool hasDefLine = false;
    /* key order: ASCENT CAPHEIGHT COLOR DESCENT GLYPH INTERNALLEADING KERNING LINEGAP MONOSPACED QUADWIDTH RATIO
     * STRIKEOUTPOSITION STRIKEOUTTHICKNESS UNDERLINEPOSITION UNDERLINETHICKNESS XHEIGHT */
#define NUM(id) do { if (f->has[id]) buf_printf(&b, "%s\t%d\n", METRIC_KEY[id], f->m[id]); } while (0)
    NUM(PXM_ASCENT);
    NUM(PXM_CAPHEIGHT);
    if (!palette_is_default(f))
        for (int i = 0; i < PX_NUM_PIXELS; i++)
            if (f->colorUsed[i]) buf_printf(&b, "COLOR\t%c\t%08X\n", PIXELS[i], (unsigned)f->color[i]);
    NUM(PXM_DESCENT);
    /* glyph lines */
    {
        size_t cap = (size_t)f->count + 2;
        char *pool = (char *)malloc(cap * 112);
        char **lines = (char **)malloc(cap * sizeof(char *));
        if (!pool || !lines) { free(pool); free(lines); free(b.p); return NULL; }
        int nl = 0;
        for (int i = 0; i < f->count; i++) {
            const PxGlyph *g = &f->glyphs[i];
            char *line = pool + (size_t)nl * 112;
            int n;
            n = sprintf(line, "GLYPH\t%06X\t", (unsigned)g->code);
            glyph_fields(g, line + n);
            lines[nl++] = line;
        }
        qsort(lines, (size_t)nl, sizeof(char *), cmp_lineptr);
        for (int i = 0; i < nl; i++) {
            if (i > 0 && strcmp(lines[i], lines[i - 1]) == 0) continue;
            buf_str(&b, lines[i]);
            buf_str(&b, "\n");
        }
        /* the FFFFFF line (the default glyph) sorts last among the glyph lines */
        int di = (f->defaultCodepoint != PX_NO_CODEPOINT) ? px_font_find(f, f->defaultCodepoint) : -1;
        char fields[96];
        if (di >= 0) {
            glyph_fields(&f->glyphs[di], fields);
            buf_printf(&b, "GLYPH\tFFFFFF\t%s\n", fields);
            hasDefLine = true;
        } else if (f->defaultCodepoint == PX_NO_CODEPOINT && f->hasDanglingDef) {
            hasDefLine = true;
            if (f->defHash[0]) {
                char up[33];
                hash_upper(f->defHash, up);
                buf_printf(&b, "GLYPH\tFFFFFF\t%d,%d\t%d\t%s\n", f->defDx, f->defDy, f->defAdvance, up);
            } else {
                buf_printf(&b, "GLYPH\tFFFFFF\t%d,%d\t%d\n", f->defDx, f->defDy, f->defAdvance);
            }
        }
        free(pool);
        free(lines);
    }
    NUM(PXM_INTERNALLEADING);
    if (f->kernCount > 0) {
        PxKern *k = (PxKern *)malloc(sizeof(PxKern) * (size_t)f->kernCount);
        if (!k) { free(b.p); return NULL; }
        int nk = 0;
        for (int i = 0; i < f->kernCount; i++)
            if (f->kern[i].value != 0 && kern_code_ok(f, f->kern[i].first, hasDefLine) && kern_code_ok(f, f->kern[i].second, hasDefLine))
                k[nk++] = f->kern[i];
        qsort(k, (size_t)nk, sizeof(PxKern), kern_cmp);
        for (int i = 0; i < nk; i++) {
            if (i > 0 && k[i].first == k[i - 1].first && k[i].second == k[i - 1].second) continue;
            buf_printf(&b, "KERNING\t%06X\t%06X\t%d\n", (unsigned)k[i].first, (unsigned)k[i].second, k[i].value);
        }
        free(k);
    }
    NUM(PXM_LINEGAP);
    if (f->monospaced) buf_str(&b, "MONOSPACED\n");
    NUM(PXM_QUADWIDTH);
    if (f->hasRatio) buf_printf(&b, "RATIO\t%d:%d\n", f->ratioA, f->ratioB);
    NUM(PXM_STRIKEOUTPOSITION);
    NUM(PXM_STRIKEOUTTHICKNESS);
    NUM(PXM_UNDERLINEPOSITION);
    NUM(PXM_UNDERLINETHICKNESS);
    NUM(PXM_XHEIGHT);
#undef NUM
    if (b.bad) { free(b.p); return NULL; }
    if (!b.p) { b.p = (char *)malloc(1); if (!b.p) return NULL; b.p[0] = 0; }
    if (len) *len = b.n;
    if (hashOut) px_hash128_hex(b.p, b.n, hashOut, false);
    return b.p;
}

void px_font_hash(const PxFont *f, char out[33]) {
    size_t n;
    char *t = px_pxfm_text(f, &n, out);
    if (!t) out[0] = 0;
    free(t);
}

/* ------------------------------------------------------------------ pxgl loader */

/* One distinct glyph file of a pxfm. Many glyphs share a file, and cold file opens are slow on Windows
 * (about 5 ms each with real-time virus scanning, so 8000 glyphs took ~40 s), so the distinct files are loaded
 * once each and, when there are many, by several threads. */
typedef struct PxlJob {
    char hash[33];
    char *pix;
    int w, h;
    bool cropped;
    bool ok;
    char err[PX_ERR_LEN];
} PxlJob;

static void load_pxgl(const char *libRoot, PxlJob *j) {
    char *hash = j->hash;
    char path[1024];
    snprintf(path, sizeof path, "%s/pxgl/%c%c/%s.pxgl", libRoot, hash[0], hash[1], hash);
    j->ok = false;
    size_t rawLen;
    char *raw = px_read_file(path, &rawLen);
    if (!raw) { fail(j->err, "glyph file %s not found (%s)", hash, path); return; }
    size_t cl;
    char *can = px_canonical(raw, rawLen, &cl);
    free(raw);
    if (!can) { fail(j->err, "out of memory"); return; }
    for (size_t i = 0; i < cl; i++)
        if ((unsigned char)can[i] >= 0x80) { free(can); fail(j->err, "glyph file %s: byte of 0x80 or above", hash); return; }
    char h[33];
    px_hash128_hex(can, cl, h, false);
    if (strcmp(h, hash) != 0) { free(can); fail(j->err, "glyph file %s: content hash is %s, not its name", hash, h); return; }
    int w = -1, rows = 0;
    bool ink = false, firstCol = false, lastCol = false, firstRow = false, lastRow = false;
    size_t i = 0;
    while (i < cl) {
        size_t s = i;
        while (can[i] != '\n') i++;
        int rw = (int)(i - s);
        i++;
        if (w < 0) w = rw;
        if (rw != w) { free(can); fail(j->err, "glyph file %s: rows of unequal width", hash); return; }
        bool rowInk = false;
        for (size_t k = s; k < s + (size_t)rw; k++) {
            if (px_pixel_index(can[k]) < 0) { char bad = can[k]; free(can); fail(j->err, "glyph file %s: character '%c' is not a pixel", hash, bad); return; }
            if (can[k] != '.') rowInk = true;
        }
        if (rowInk) { ink = true; if (rows == 0) firstRow = true; lastRow = true; } else lastRow = false;
        if (rw > 0) {
            if (can[s] != '.') firstCol = true;
            if (can[s + (size_t)rw - 1] != '.') lastCol = true;
        }
        rows++;
    }
    if (rows == 0 || w < 1) { free(can); fail(j->err, "glyph file %s: no rows", hash); return; }
    if (!ink) { free(can); fail(j->err, "glyph file %s: no inked pixel", hash); return; }
    char *pix = (char *)malloc((size_t)w * (size_t)rows);
    if (!pix) { free(can); fail(j->err, "out of memory"); return; }
    for (int r = 0; r < rows; r++) memcpy(pix + (size_t)r * w, can + (size_t)r * (w + 1), (size_t)w);
    free(can);
    j->w = w;
    j->h = rows;
    j->pix = pix;
    j->cropped = firstRow && lastRow && firstCol && lastCol;
    j->ok = true;
}

typedef struct PxlCtx { const char *root; PxlJob *jobs; long n; volatile long next; } PxlCtx;

static void pxl_work(PxlCtx *c) {
    for (;;) {
#ifdef _WIN32
        long i = InterlockedIncrement(&c->next) - 1;
#else
        long i = c->next++;
#endif
        if (i >= c->n) break;
        load_pxgl(c->root, &c->jobs[i]);
    }
}

#ifdef _WIN32
static DWORD WINAPI pxl_thread(LPVOID p) { pxl_work((PxlCtx *)p); return 0; }
#endif

static void load_pxgl_jobs(const char *root, PxlJob *jobs, long n) {
    PxlCtx ctx = {root, jobs, n, 0};
#ifdef _WIN32
    if (n >= 32) {
        enum { NT = 32 };
        HANDLE th[NT];
        int started = 0;
        for (int t = 0; t < NT; t++) {
            th[started] = CreateThread(NULL, 0, pxl_thread, &ctx, 0, NULL);
            if (th[started]) started++;
        }
        if (started > 0) {
            WaitForMultipleObjects((DWORD)started, th, TRUE, INFINITE);
            for (int t = 0; t < started; t++) CloseHandle(th[t]);
            return;
        }
    }
#endif
    pxl_work(&ctx);
}

typedef struct PxlRef { int gi; int job; const char *hash; } PxlRef;

static int cmp_ref_hash(const void *a, const void *b) {
    int c = strcmp(((const PxlRef *)a)->hash, ((const PxlRef *)b)->hash);
    if (c) return c;
    return ((const PxlRef *)a)->gi - ((const PxlRef *)b)->gi;
}

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return x < y ? -1 : x > y;
}

static bool check_palette(const PxFont *f, char err[PX_ERR_LEN]) {
    bool used[PX_NUM_PIXELS] = {false};
    used[0] = true;
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        if (!g->pix) continue;
        size_t n = (size_t)g->w * (size_t)g->h;
        for (size_t k = 0; k < n; k++) used[px_pixel_index(g->pix[k]) < 0 ? 0 : px_pixel_index(g->pix[k])] = true;
    }
    if (f->hasPalette) {
        for (int i = 0; i < PX_NUM_PIXELS; i++) {
            if (used[i] && !f->colorUsed[i]) return fail(err, "no COLOR line for pixel '%c' used by the glyphs", PIXELS[i]);
            if (!used[i] && f->colorUsed[i]) return fail(err, "COLOR line for pixel '%c' that no glyph uses", PIXELS[i]);
        }
    } else {
        for (int i = 2; i < PX_NUM_PIXELS; i++)
            if (used[i]) return fail(err, "no COLOR lines, but the glyph files use pixel '%c'", PIXELS[i]);
    }
    return true;
}

bool px_load_glyphs(PxFont *f, const char *libRoot, const int32_t *cps, int n, char err[PX_ERR_LEN]) {
    int32_t *want = NULL;
    if (cps) {
        want = (int32_t *)malloc(sizeof(int32_t) * (size_t)(n > 0 ? n : 1));
        if (!want) return fail(err, "out of memory");
        memcpy(want, cps, sizeof(int32_t) * (size_t)n);
        qsort(want, (size_t)(n > 0 ? n : 0), sizeof(int32_t), cmp_i32);
    }
    PxlRef *refs = (PxlRef *)malloc(sizeof(PxlRef) * (size_t)(f->count > 0 ? f->count : 1));
    PxlJob *jobs = NULL;
    long njobs = 0;
    int nref = 0;
    bool ok = false;
    if (!refs) { free(want); return fail(err, "out of memory"); }
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        if (!g->lazy) continue;
        if (want) {
            if (!bsearch(&g->code, want, (size_t)n, sizeof(int32_t), cmp_i32)) continue;
        }
        refs[nref].gi = i;
        refs[nref].hash = g->hash;
        refs[nref].job = 0;
        nref++;
    }
    if (nref > 0) {
        qsort(refs, (size_t)nref, sizeof(PxlRef), cmp_ref_hash);
        for (int i = 0; i < nref; i++) {
            if (i > 0 && strcmp(refs[i].hash, refs[i - 1].hash) == 0) refs[i].job = refs[i - 1].job;
            else refs[i].job = (int)njobs++;
        }
        jobs = (PxlJob *)calloc((size_t)njobs, sizeof(PxlJob));
        if (!jobs) { fail(err, "out of memory"); goto done; }
        for (int i = 0; i < nref; i++) memcpy(jobs[refs[i].job].hash, refs[i].hash, 33);
        load_pxgl_jobs(libRoot, jobs, njobs);
        for (int i = 0; i < nref; i++) {
            const PxlJob *j = &jobs[refs[i].job];
            if (!j->ok) { fail(err, "code %06X: %s", (unsigned)f->glyphs[refs[i].gi].code, j->err); goto done; }
        }
        for (int i = 0; i < nref; i++) {
            const PxlJob *j = &jobs[refs[i].job];
            PxGlyph *g = &f->glyphs[refs[i].gi];
            size_t sz = (size_t)j->w * (size_t)j->h;
            g->pix = (char *)malloc(sz);
            if (!g->pix) { fail(err, "out of memory"); goto done; }
            memcpy(g->pix, j->pix, sz);
            g->w = j->w;
            g->h = j->h;
            g->asDrawn = !j->cropped;
            g->lazy = false;
            g->hashValid = true;     /* hash is the file's; the crop shift is none, the bitmap is as the file has it */
            g->cdx = g->cdy = 0;
        }
    }
    ok = true;
    {
        bool anyLazy = false;
        for (int i = 0; i < f->count; i++) if (f->glyphs[i].lazy) { anyLazy = true; break; }
        if (!anyLazy && !check_palette(f, err)) ok = false;
    }
done:
    if (jobs) for (long i = 0; i < njobs; i++) free(jobs[i].pix);
    free(jobs);
    free(refs);
    free(want);
    return ok;
}

/* ------------------------------------------------------------------ pxfm reader */

static bool parse_int(const char *s, int *out) { /* -?(0|[1-9][0-9]*), at most 9 digits */
    const char *p = s;
    if (*p == '-') p++;
    if (!*p) return false;
    if (*p == '0' && p[1]) return false;
    if (strlen(p) > 9) return false;
    for (const char *q = p; *q; q++) if (*q < '0' || *q > '9') return false;
    if (p != s && strcmp(p, "0") == 0) return false;   /* "-0" */
    *out = atoi(s);
    return true;
}

/* six upper-case hex digits: a code point (<= 10FFFF), an F-code (F00000..FFFFFE) or, with allowDefault, FFFFFF */
static bool parse_code(const char *s, int32_t *out, bool allowDefault) {
    if (strlen(s) != 6) return false;
    long v = 0;
    for (int i = 0; i < 6; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v * 16 + d;
    }
    if (v > 0x10FFFF && v < PX_CODE_UNCODED_FIRST) return false;
    if (v == PX_CODE_DEFAULT && !allowDefault) return false;
    *out = (int32_t)v;
    return true;
}

static bool parse_color(const char *s, uint32_t *out) {
    if (strlen(s) != 8) return false;
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v * 16 + (uint32_t)d;
    }
    *out = v;
    return true;
}

static int gcd_int(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

static bool font_push(PxFont *f, const PxGlyph *g) {
    if (!font_reserve(f)) return false;
    f->glyphs[f->count++] = *g;
    return true;
}

bool px_parse_pxfm(const char *data, size_t len, const char *where, PxFont *out, char err[PX_ERR_LEN]) {
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)data[i] >= 0x80) return fail(err, "%s: byte of 0x80 or above", where);
    size_t cl;
    char *can = px_canonical(data, len, &cl);
    if (!can) return fail(err, "out of memory");

    PxFont f;
    px_font_init(&f);
    px_hash128_hex(can, cl, f.fileHash, false);   /* before the lines are split in place */
    bool anyColor = false, seenDef = false, seenRatio = false, seenMono = false, seen[PXM_COUNT] = {false};
    int lineNo = 0;
    int defX = 0, defY = 0, defAdv = 0;
    char defHash[33] = "";
    char msg[2 * PX_ERR_LEN];
    bool colorSeen[PX_NUM_PIXELS] = {false};
    char *p = can;

#define LFAIL(...) do { snprintf(msg, sizeof msg, __VA_ARGS__); fail(err, "%.150s:%d: %.300s", where, lineNo, msg); goto bad; } while (0)

    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        *nl = 0;
        p = nl + 1;
        lineNo++;

        char *fld[8];
        int nf = 0;
        char *s = line;
        for (;;) {
            if (nf >= 7) LFAIL("too many fields");
            fld[nf++] = s;
            char *t = strchr(s, '\t');
            if (!t) break;
            *t = 0;
            s = t + 1;
        }
        for (int i = 0; i < nf; i++)
            if (i > 0 && (!fld[i][0] || strpbrk(fld[i], " \t"))) LFAIL("empty field or blank inside a field");
        const char *key = fld[0];
        int na = nf - 1;
        if (strcmp(key, "BASELINE") == 0 || strcmp(key, "EMWIDTH") == 0) LFAIL("pxfm version 1 file (%s line)", key);

        int mi = -1;
        for (int i = 0; i < PXM_COUNT; i++) if (strcmp(key, METRIC_KEY[i]) == 0) mi = i;
        if (mi >= 0) {
            if (na != 1) LFAIL("%s takes exactly 1 field", key);
            if (seen[mi]) LFAIL("two %s lines", key);
            seen[mi] = true;
            int v;
            if (!parse_int(fld[1], &v)) LFAIL("%s '%s' is not an integer", key, fld[1]);
            if ((mi == PXM_ASCENT || mi == PXM_DESCENT) && v < 0) LFAIL("negative %s", key);
            f.has[mi] = true;
            f.m[mi] = v;
        } else if (strcmp(key, "RATIO") == 0) {
            if (na != 1) LFAIL("RATIO takes exactly 1 field");
            if (seenRatio) LFAIL("two RATIO lines");
            seenRatio = true;
            char *colon = strchr(fld[1], ':');
            int a, b;
            if (!colon) LFAIL("RATIO '%s' is not A:B", fld[1]);
            *colon = 0;
            if (!parse_int(fld[1], &a) || !parse_int(colon + 1, &b) || a < 1 || b < 1 || gcd_int(a, b) != 1)
                LFAIL("RATIO is not A:B of two positive integers with no common factor");
            f.hasRatio = true; f.ratioA = a; f.ratioB = b;
        } else if (strcmp(key, "MONOSPACED") == 0) {
            if (na != 0) LFAIL("MONOSPACED takes no field");
            if (seenMono) LFAIL("two MONOSPACED lines");
            seenMono = true;
            f.monospaced = true;
        } else if (strcmp(key, "COLOR") == 0) {
            if (na != 2) LFAIL("COLOR takes exactly 2 fields");
            int pi = (fld[1][1] == 0) ? px_pixel_index(fld[1][0]) : -1;
            uint32_t c;
            if (pi < 0) LFAIL("COLOR pixel '%s' is not a pixel character", fld[1]);
            if (!parse_color(fld[2], &c)) LFAIL("COLOR '%s' is not 8 upper-case hex digits", fld[2]);
            if (colorSeen[pi]) LFAIL("two colours for '%s'", fld[1]);
            if (!anyColor) {
                for (int i = 0; i < PX_NUM_PIXELS; i++) f.colorUsed[i] = false;
                anyColor = true;
            }
            colorSeen[pi] = true;
            f.colorUsed[pi] = true;
            f.color[pi] = c;
        } else if (strcmp(key, "KERNING") == 0) {
            if (na != 3) LFAIL("KERNING takes exactly 3 fields");
            int32_t a, b;
            int v;
            if (!parse_code(fld[1], &a, true) || !parse_code(fld[2], &b, true)) LFAIL("KERNING codes are six upper-case hex digits, a code point or from F00000");
            if (!parse_int(fld[3], &v)) LFAIL("KERNING '%s' is not an integer", fld[3]);
            if (v == 0) LFAIL("a KERNING of 0");
            if (f.kernCount == f.kernCap) {
                int nc = f.kernCap ? f.kernCap * 2 : 64;
                PxKern *nk = (PxKern *)realloc(f.kern, sizeof(PxKern) * (size_t)nc);
                if (!nk) LFAIL("out of memory");
                f.kern = nk;
                f.kernCap = nc;
            }
            f.kern[f.kernCount].first = a;
            f.kern[f.kernCount].second = b;
            f.kern[f.kernCount].value = v;
            f.kernCount++;
        } else if (strcmp(key, "GLYPH") == 0) {
            if (na != 3 && na != 4) LFAIL("GLYPH takes 3 or 4 fields, not %d", na);
            if (strchr(fld[2], 'x') || (!strchr(fld[2], ',') && strchr(fld[3], ','))) LFAIL("pxfm version 1 file (GLYPH line '%s')", fld[2]);
            PxGlyph g;
            memset(&g, 0, sizeof g);
            bool isDef = strcmp(fld[1], "FFFFFF") == 0;
            if (strcmp(fld[1], "-") == 0 || strcmp(fld[1], "_DEF") == 0) LFAIL("pxfm version 1 file (code '%s')", fld[1]);
            if (isDef) g.code = PX_NO_CODEPOINT;
            else if (!parse_code(fld[1], &g.code, false)) LFAIL("code '%s' is not six upper-case hex digits, up to 10FFFF or from F00000", fld[1]);
            char *comma = strchr(fld[2], ',');
            if (!comma) LFAIL("offsets '%s' are not x,y", fld[2]);
            *comma = 0;
            if (!parse_int(fld[2], &g.x) || !parse_int(comma + 1, &g.y)) LFAIL("offsets '%s,%s' are not integers", fld[2], comma + 1);
            if (!parse_int(fld[3], &g.advance)) LFAIL("advance '%s' is not an integer", fld[3]);
            if (na == 4) {
                if (!px_is_hash(fld[4], true, false)) LFAIL("glyph hash '%s' is not 32 upper-case hex digits", fld[4]);
                hash_lower(fld[4], g.hash);
                g.lazy = true;
            } else {
                if (g.x != 0 || g.y != 0) LFAIL("a glyph without a glyph file must be at 0,0");
                g.hash[0] = 0;
            }
            g.hashValid = true;
            if (isDef) {
                if (seenDef) LFAIL("two glyph lines of the default code FFFFFF");
                seenDef = true;
                defX = g.x; defY = g.y; defAdv = g.advance;
                memcpy(defHash, g.hash, 33);
            } else if (!font_push(&f, &g)) {
                LFAIL("out of memory");
            }
        } else {
            LFAIL("unknown key '%s'", key);
        }
    }
#undef LFAIL

    if (anyColor) {
        f.hasPalette = true;
        bool dflt = f.colorUsed[0] && f.colorUsed[1] && f.color[0] == 0xFFFFFFFFu && f.color[1] == 0x000000FFu;
        for (int i = 2; i < PX_NUM_PIXELS && dflt; i++) if (f.colorUsed[i]) dflt = false;
        if (dflt) { fail(err, "%s: colour lines of the default colours", where); goto bad; }
        if (!f.colorUsed[0]) { fail(err, "%s: no COLOR line for '.'", where); goto bad; }
    }
    px_font_sort(&f);
    for (int i = 1; i < f.count; i++)
        if (f.glyphs[i].code == f.glyphs[i - 1].code) { fail(err, "%s: two GLYPH lines of the code %06X", where, (unsigned)f.glyphs[i].code); goto bad; }
    {
        int nu = px_font_uncoded_count(&f);      /* the F-codes are the last ones: F00000, F00001, ... without a gap */
        for (int i = 0; i < nu; i++)
            if (f.glyphs[f.count - nu + i].code != PX_CODE_UNCODED_FIRST + i) { fail(err, "%s: the codes from F00000 up are not F00000, F00001, ... each once", where); goto bad; }
    }
    if (f.kernCount > 1) {
        qsort(f.kern, (size_t)f.kernCount, sizeof(PxKern), kern_cmp);
        for (int i = 1; i < f.kernCount; i++)
            if (kern_cmp(&f.kern[i], &f.kern[i - 1]) == 0) { fail(err, "%s: two KERNING lines of one pair", where); goto bad; }
    }
    for (int i = 0; i < f.kernCount; i++) {
        int32_t c2[2] = {f.kern[i].first, f.kern[i].second};
        for (int k = 0; k < 2; k++)
            if (c2[k] >= PX_CODE_UNCODED_FIRST && !(c2[k] == PX_CODE_DEFAULT ? seenDef : px_font_find(&f, c2[k]) >= 0)) {
                fail(err, "%s: KERNING of code %06X, which no GLYPH line has", where, (unsigned)c2[k]);
                goto bad;
            }
    }
    if (seenDef) {
        for (int i = 0; i < f.count; i++) {
            const PxGlyph *g = &f.glyphs[i];
            if (px_code_is_codepoint(g->code) && g->x == defX && g->y == defY && g->advance == defAdv &&
                strcmp(g->hash, defHash) == 0) { f.defaultCodepoint = g->code; break; }
        }
        if (f.defaultCodepoint == PX_NO_CODEPOINT) {
            f.hasDanglingDef = true;
            f.defDx = defX; f.defDy = defY; f.defAdvance = defAdv;
            memcpy(f.defHash, defHash, 33);
        }
    }
    free(can);
    *out = f;
    return true;

bad:
    free(can);
    px_font_free(&f);
    return false;
}

bool px_read_pxfm_lines(const char *path, PxFont *out, char err[PX_ERR_LEN]) {
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) return fail(err, "cannot read %s", path);
    bool ok = px_parse_pxfm(raw, n, path, out, err);
    free(raw);
    return ok;
}

bool px_read_pxfm(const char *path, const char *libRoot, PxFont *out, char err[PX_ERR_LEN]) {
    char root[1024];
    if (!libRoot) {
        if (!px_library_root_of(path, root, sizeof root)) return fail(err, "%s: not inside a library (pxfm/xx/hash.pxfm)", path);
        libRoot = root;
    }
    PxFont f;
    if (!px_read_pxfm_lines(path, &f, err)) return false;
    char e2[PX_ERR_LEN];
    if (!px_load_glyphs(&f, libRoot, NULL, 0, e2)) {
        px_font_free(&f);
        return fail(err, "%s: %s", path, e2);
    }
    *out = f;
    return true;
}

/* ------------------------------------------------------------------ final save */

bool px_save_final(const PxFont *f, const char *libRoot, char hashOut[33], char *pathOut, size_t pathSize,
                   char err[PX_ERR_LEN]) {
    size_t len;
    char hash[33];
    char *text = px_pxfm_text(f, &len, hash);
    if (!text) return fail(err, "out of memory");
    char path[2048], dir[1024];

    for (int i = 0; i < f->count; i++) {
        const PxGlyph *gl = &f->glyphs[i];
        if (gl->lazy) continue;
        size_t n;
        char *g = glyph_text(gl, &n, NULL, NULL);
        if (!g) continue;
        char h[33];
        px_hash128_hex(g, n, h, false);
        snprintf(dir, sizeof dir, "%s/pxgl/%c%c", libRoot, h[0], h[1]);
        snprintf(path, sizeof path, "%s/%s.pxgl", dir, h);
        if (!px_file_exists(path)) {
            if (!px_mkdir_p(dir)) { free(g); free(text); return fail(err, "cannot create directory %s", dir); }
            if (!px_write_file(path, g, n)) { free(g); free(text); return fail(err, "cannot write %s", path); }
        }
        free(g);
    }
    snprintf(dir, sizeof dir, "%s/pxfm/%c%c", libRoot, hash[0], hash[1]);
    snprintf(path, sizeof path, "%s/%s.pxfm", dir, hash);
    if (!px_file_exists(path)) {
        if (!px_mkdir_p(dir)) { free(text); return fail(err, "cannot create directory %s", dir); }
        if (!px_write_file(path, text, len)) { free(text); return fail(err, "cannot write %s", path); }
    }
    free(text);
    if (hashOut) memcpy(hashOut, hash, 33);
    if (pathOut && pathSize) snprintf(pathOut, pathSize, "%s", path);
    return true;
}

/* ------------------------------------------------------------------ draft files */

void px_draft_info_init(PxDraftInfo *info) { memset(info, 0, sizeof *info); }

void px_draft_info_free(PxDraftInfo *info) {
    if (info->typeface) { pxtf_free(info->typeface); free(info->typeface); }
    info->typeface = NULL;
}

bool px_save_draft(const PxFont *f, const PxDraftInfo *info, const char *path, char err[PX_ERR_LEN]) {
    for (int i = 0; i < f->count; i++)
        if (f->glyphs[i].lazy) return fail(err, "cannot save a draft with glyphs that are not loaded");
    Buf b = {0};
    buf_str(&b, "PXFEDRAFT\t3\n");
    if (info) {
        char *nm = pxtf_escape(info->name);
        buf_printf(&b, "NAME\t%s\n", nm && nm[0] ? nm : "-");
        free(nm);
        if (info->originalHash[0]) { char up[33]; hash_upper(info->originalHash, up); buf_printf(&b, "ORIGINALHASH\t%s\n", up); }
        if (info->originalPxtfHash[0]) {
            char up[33];
            hash_upper(info->originalPxtfHash, up);
            buf_printf(&b, "ORIGINALPXTF\t%s\t%d\n", up, info->originalVariant);
        }
        if (info->typeface) {
            size_t n;
            char *t = pxtf_text(info->typeface, &n, NULL);
            if (!t) { free(b.p); return fail(err, "out of memory"); }
            size_t i = 0;
            while (i < n) {
                size_t s = i;
                while (t[i] != '\n') i++;
                char *line = (char *)malloc(i - s + 1);
                memcpy(line, t + s, i - s);
                line[i - s] = 0;
                i++;
                char *esc = pxtf_escape(line);   /* a line has tabs: escaped as \t, a leading tab too */
                buf_printf(&b, "PXTF\t%s\n", esc);
                free(esc);
                free(line);
            }
            free(t);
        }
    } else {
        buf_str(&b, "NAME\t-\n");
    }
    for (int m = 0; m < PXM_COUNT; m++)
        if (f->has[m]) buf_printf(&b, "%s\t%d\n", METRIC_KEY[m], f->m[m]);
    if (f->hasRatio) buf_printf(&b, "RATIO\t%d:%d\n", f->ratioA, f->ratioB);
    if (f->monospaced) buf_str(&b, "MONOSPACED\n");
    if (f->hasPalette)
        for (int i = 0; i < PX_NUM_PIXELS; i++)
            if (f->colorUsed[i]) buf_printf(&b, "COLOR\t%c\t%08X\n", PIXELS[i], (unsigned)f->color[i]);
    for (int i = 0; i < f->kernCount; i++)
        if (f->kern[i].value != 0)
            buf_printf(&b, "KERNING\t%06X\t%06X\t%d\n", (unsigned)f->kern[i].first, (unsigned)f->kern[i].second, f->kern[i].value);
    if (f->defaultCodepoint != PX_NO_CODEPOINT) buf_printf(&b, "DEFAULT\t%06X\n", (unsigned)f->defaultCodepoint);
    if (f->hasDanglingDef) {
        if (f->defHash[0]) { char up[33]; hash_upper(f->defHash, up); buf_printf(&b, "DANGLINGDEF\t%d,%d\t%d\t%s\n", f->defDx, f->defDy, f->defAdvance, up); }
        else buf_printf(&b, "DANGLINGDEF\t%d,%d\t%d\n", f->defDx, f->defDy, f->defAdvance);
    }
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        bool has = g->pix && g->w > 0 && g->h > 0;
        char cp[16];
        snprintf(cp, sizeof cp, "%06X", (unsigned)g->code);
        buf_printf(&b, "GLYPH\t%s\t%d,%d\t%d\t%dx%d\t%d\n", cp, g->x, g->y, g->advance, has ? g->w : 0, has ? g->h : 0, g->asDrawn ? 1 : 0);
        if (has)
            for (int y = 0; y < g->h; y++) {
                buf_str(&b, "\t");
                buf_add(&b, g->pix + (size_t)y * g->w, (size_t)g->w);
                buf_str(&b, "\n");
            }
    }
    if (b.bad) { free(b.p); return fail(err, "out of memory"); }
    bool ok = px_write_file(path, b.p, b.n);
    free(b.p);
    return ok ? true : fail(err, "cannot write %s", path);
}

bool px_load_draft(const char *path, PxFont *out, PxDraftInfo *info, char err[PX_ERR_LEN]) {
    size_t n;
    char *raw = px_read_file(path, &n);
    if (!raw) return fail(err, "cannot read %s", path);
    PxFont f;
    px_font_init(&f);
    PxDraftInfo inf;
    px_draft_info_init(&inf);
    Buf pxtfText = {0};
    bool anyColor = false;
    int lineNo = 0;
    char msg[2 * PX_ERR_LEN];
    char *p = raw;
    bool header = false;

#define LFAIL(...) do { snprintf(msg, sizeof msg, __VA_ARGS__); fail(err, "%.150s:%d: %.300s", path, lineNo, msg); goto bad; } while (0)

    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = 0; p = nl + 1; } else p += strlen(p);
        size_t ll = strlen(line);
        if (ll && line[ll - 1] == '\r') line[ll - 1] = 0;
        lineNo++;
        if (!line[0]) continue;
        char *fld[8];
        int nf = 0;
        char *s = line;
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
        if (!header) {
            if (strcmp(key, "PXFEDRAFT") != 0) LFAIL("not a pxfe draft");
            if (na == 1 && strcmp(fld[1], "2") == 0)
                LFAIL("this is a version 2 draft, made by an older pxfe before the pxtf and F-code format change; version 2 drafts cannot be opened any more");
            if (na != 1 || strcmp(fld[1], "3") != 0) LFAIL("draft version %s is not supported (this pxfe reads version 3)", na >= 1 ? fld[1] : "?");
            header = true;
            continue;
        }
        int mi = -1;
        for (int i = 0; i < PXM_COUNT; i++) if (strcmp(key, METRIC_KEY[i]) == 0) mi = i;
        if (strcmp(key, "NAME") == 0) {
            char *un = NULL;
            char e2[PX_ERR_LEN];
            if (na < 1) LFAIL("NAME needs a field");
            if (!pxtf_unescape(fld[1], &un, e2)) LFAIL("NAME: %s", e2);
            snprintf(inf.name, sizeof inf.name, "%s", strcmp(un, "-") == 0 ? "" : un);
            free(un);
        } else if (strcmp(key, "ORIGINALHASH") == 0) {
            if (na != 1 || !px_is_hash(fld[1], true, false)) LFAIL("ORIGINALHASH is not 32 upper-case hex digits");
            hash_lower(fld[1], inf.originalHash);
        } else if (strcmp(key, "ORIGINALPXTF") == 0) {
            if (na != 2 || !px_is_hash(fld[1], true, false) || !parse_int(fld[2], &inf.originalVariant)) LFAIL("bad ORIGINALPXTF");
            hash_lower(fld[1], inf.originalPxtfHash);
        } else if (strcmp(key, "PXTF") == 0) {
            char *un = NULL;
            char e2[PX_ERR_LEN];
            if (na != 1) LFAIL("PXTF needs one field");
            if (!pxtf_unescape(fld[1], &un, e2)) LFAIL("PXTF: %s", e2);
            buf_str(&pxtfText, un);
            buf_str(&pxtfText, "\n");
            free(un);
        } else if (mi >= 0) {
            int v;
            if (na != 1 || !parse_int(fld[1], &v)) LFAIL("%s: bad value", key);
            f.has[mi] = true;
            f.m[mi] = v;
        } else if (strcmp(key, "RATIO") == 0) {
            char *colon = na == 1 ? strchr(fld[1], ':') : NULL;
            int a, b;
            if (!colon) LFAIL("bad RATIO");
            *colon = 0;
            if (!parse_int(fld[1], &a) || !parse_int(colon + 1, &b) || a < 1 || b < 1) LFAIL("bad RATIO");
            f.hasRatio = true; f.ratioA = a; f.ratioB = b;
        } else if (strcmp(key, "MONOSPACED") == 0) {
            f.monospaced = true;
        } else if (strcmp(key, "COLOR") == 0) {
            uint32_t c;
            int pi = (na == 2 && fld[1][1] == 0) ? px_pixel_index(fld[1][0]) : -1;
            if (pi < 0 || !parse_color(fld[2], &c)) LFAIL("bad COLOR");
            if (!anyColor) { for (int i = 0; i < PX_NUM_PIXELS; i++) f.colorUsed[i] = false; anyColor = true; }
            f.colorUsed[pi] = true;
            f.color[pi] = c;
        } else if (strcmp(key, "KERNING") == 0) {
            int32_t a, b;
            int v;
            if (na != 3 || !parse_code(fld[1], &a, true) || !parse_code(fld[2], &b, true) || !parse_int(fld[3], &v)) LFAIL("bad KERNING");
            px_font_set_kern(&f, a, b, v);
        } else if (strcmp(key, "DEFAULT") == 0) {
            int32_t cp;
            if (na != 1 || !parse_code(fld[1], &cp, false)) LFAIL("bad DEFAULT");
            f.defaultCodepoint = cp;
        } else if (strcmp(key, "DANGLINGDEF") == 0) {
            char *comma = na >= 2 ? strchr(fld[1], ',') : NULL;
            if (!comma) LFAIL("bad DANGLINGDEF");
            *comma = 0;
            if (!parse_int(fld[1], &f.defDx) || !parse_int(comma + 1, &f.defDy) || !parse_int(fld[2], &f.defAdvance)) LFAIL("bad DANGLINGDEF");
            f.defHash[0] = 0;
            if (na == 3) { if (!px_is_hash(fld[3], true, false)) LFAIL("bad DANGLINGDEF hash"); hash_lower(fld[3], f.defHash); }
            f.hasDanglingDef = true;
        } else if (strcmp(key, "GLYPH") == 0) {
            if (na != 5) LFAIL("GLYPH takes 5 fields");
            PxGlyph g;
            memset(&g, 0, sizeof g);
            if (!parse_code(fld[1], &g.code, false)) LFAIL("bad code '%s'", fld[1]);
            char *comma = strchr(fld[2], ',');
            if (!comma) LFAIL("bad offsets");
            *comma = 0;
            if (!parse_int(fld[2], &g.x) || !parse_int(comma + 1, &g.y) || !parse_int(fld[3], &g.advance)) LFAIL("bad glyph numbers");
            char *xs = strchr(fld[4], 'x');
            if (!xs) LFAIL("bad size '%s'", fld[4]);
            *xs = 0;
            if (!parse_int(fld[4], &g.w) || !parse_int(xs + 1, &g.h) || g.w < 0 || g.h < 0 || (g.w == 0) != (g.h == 0)) LFAIL("bad size");
            g.asDrawn = fld[5][0] == '1';
            if (g.w > 0) {
                g.pix = (char *)malloc((size_t)g.w * (size_t)g.h);
                if (!g.pix) LFAIL("out of memory");
                for (int y = 0; y < g.h; y++) {
                    char *rl = p;
                    char *rn = strchr(p, '\n');
                    if (rn) { *rn = 0; p = rn + 1; } else p += strlen(p);
                    size_t rll = strlen(rl);
                    if (rll && rl[rll - 1] == '\r') rl[--rll] = 0;
                    lineNo++;
                    if (rl[0] != '\t' || (int)rll != g.w + 1) { free(g.pix); LFAIL("bad bitmap row"); }
                    for (int x = 0; x < g.w; x++)
                        if (px_pixel_index(rl[1 + x]) < 0) { free(g.pix); LFAIL("bad pixel '%c'", rl[1 + x]); }
                    memcpy(g.pix + (size_t)y * g.w, rl + 1, (size_t)g.w);
                }
            }
            if (!font_push(&f, &g)) { free(g.pix); LFAIL("out of memory"); }
        } else {
            LFAIL("unknown key '%s'", key);
        }
    }
#undef LFAIL
    if (!header) { fail(err, "%s: empty draft", path); goto bad; }
    if (anyColor) f.hasPalette = true;
    if (pxtfText.p) {
        PxtfTypeface *c = (PxtfTypeface *)calloc(1, sizeof *c);
        char e2[PX_ERR_LEN];
        if (!c) { fail(err, "out of memory"); goto bad; }
        if (!pxtf_parse(pxtfText.p, pxtfText.n, c, e2)) { free(c); fail(err, "%s: typeface: %s", path, e2); goto bad; }
        inf.typeface = c;
    }
    free(pxtfText.p);
    free(raw);
    px_font_sort(&f);
    *out = f;
    if (info) { px_draft_info_free(info); *info = inf; } else px_draft_info_free(&inf);
    return true;

bad:
    free(pxtfText.p);
    free(raw);
    px_font_free(&f);
    px_draft_info_free(&inf);
    return false;
}
