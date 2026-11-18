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
}

void px_palettes_free(PxPalette *p, int n) {
    if (!p) return;
    for (int i = 0; i < n; i++) free(p[i].c);
    free(p);
}

static unsigned g_palGen;     /* palGen values are unique over all fonts (a copy gets a new one) */
static unsigned new_gen(void) { return ++g_palGen ? g_palGen : ++g_palGen; }

/* the table entries of src that are named (by a glyph's pi, or by its pal: a palette read), in a new table for dst; the glyphs
 * of dst (copies of src's, in the same order) are renumbered.  keep[] may be NULL: every entry. */
static bool copy_palettes(PxFont *dst, const PxFont *src, const bool *keep) {
    dst->pals = NULL;
    dst->npals = dst->palCap = 0;
    dst->clist = NULL; dst->nclist = dst->clistCap = 0; dst->clistOk = false;
    dst->palGen = new_gen();
    if (src->npals <= 0) return true;
    int *map = (int *)malloc(sizeof(int) * (size_t)src->npals);
    dst->pals = (PxPalette *)calloc((size_t)src->npals, sizeof(PxPalette));
    if (!map || !dst->pals) { free(map); free(dst->pals); dst->pals = NULL; return false; }
    int n = 0;
    for (int i = 0; i < src->npals; i++) {
        map[i] = 0;
        if (i > 0 && keep && !keep[i]) continue;
        dst->pals[n] = src->pals[i];
        dst->pals[n].c = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(src->pals[i].n > 0 ? src->pals[i].n : 1));
        if (!dst->pals[n].c) { px_palettes_free(dst->pals, n); dst->pals = NULL; free(map); return false; }
        memcpy(dst->pals[n].c, src->pals[i].c, sizeof(uint32_t) * (size_t)src->pals[i].n);
        map[i] = n++;
    }
    dst->npals = dst->palCap = n;
    for (int i = 0; i < dst->count; i++) {
        int p = dst->glyphs[i].pi;
        dst->glyphs[i].pi = p > 0 && p < src->npals ? map[p] : 0;
    }
    free(map);
    return true;
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
    px_palettes_free(f->pals, f->npals);
    free(f->clist);
    f->glyphs = NULL; f->kern = NULL; f->pals = NULL; f->clist = NULL;
    f->count = f->capacity = f->kernCount = f->kernCap = 0;
    f->npals = f->palCap = 0;
    f->nclist = f->clistCap = 0;
    f->clistOk = false;
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

static bool *named_palettes(const PxFont *f);

bool px_font_copy(PxFont *dst, const PxFont *src) {
    *dst = *src;
    dst->glyphs = NULL; dst->kern = NULL; dst->pals = NULL; dst->clist = NULL;
    dst->count = dst->capacity = dst->kernCount = dst->kernCap = 0;
    dst->npals = dst->palCap = dst->nclist = dst->clistCap = 0;
    dst->clistOk = false;
    if (src->count > 0) {
        dst->glyphs = (PxGlyph *)malloc(sizeof(PxGlyph) * (size_t)src->count);
        if (!dst->glyphs) return false;
        dst->capacity = src->count;
        for (int i = 0; i < src->count; i++) {
            if (!px_glyph_copy(&dst->glyphs[i], &src->glyphs[i]) && src->glyphs[i].pix) { px_font_free(dst); return false; }
            dst->count++;
        }
    }
    bool *keep = named_palettes(src);
    bool ok = copy_palettes(dst, src, keep);
    free(keep);
    if (!ok) { px_font_free(dst); return false; }
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
    int n = 0, i = f->count - 1;
    if (i >= 0 && f->glyphs[i].code == PX_CODE_DEFAULT) i--;        /* the default glyph sorts after the F-codes */
    for (; i >= 0 && px_code_is_uncoded(f->glyphs[i].code); i--) n++;
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
    } else if (px_font_find(f, cp) < 0) {
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

int px_font_default_index(const PxFont *f) { return px_font_find(f, PX_CODE_DEFAULT); }

bool px_font_set_default_from(PxFont *f, int src) {
    if (src < 0 || src >= f->count) return false;
    PxGlyph t;
    if (!px_glyph_copy(&t, &f->glyphs[src]) && f->glyphs[src].pix) return false;
    t.code = PX_CODE_DEFAULT;
    int di = px_font_find(f, PX_CODE_DEFAULT);
    if (di == src) { px_glyph_free(&t); return true; }
    if (di >= 0) { px_glyph_free(&f->glyphs[di]); f->glyphs[di] = t; return true; }
    int ni = px_font_add_glyph(f, &t);
    px_glyph_free(&t);
    return ni >= 0;
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

/* The ink characters of n pixels in reading order (at most 36; a character that is no pixel counts as none); their number. */
static int ink_order(const char *pix, size_t n, char order[36]) {
    bool seen[128] = {false};
    int k = 0;
    for (size_t i = 0; i < n && k < 36; i++) {
        unsigned char c = (unsigned char)pix[i];
        if (c == '.' || c >= 128 || seen[c] || px_pixel_index((char)c) <= 0) continue;
        seen[c] = true;
        order[k++] = (char)c;
    }
    return k;
}

/* The pxgl text the glyph is written as (cropped, or as drawn), its colours renumbered in reading order ('0' for the first
 * ink character, ...; mono: every ink pixel '0'); NULL when no ink. cdx/cdy: the crop shift. */
static char *glyph_text(const PxGlyph *g, size_t *len, int *cdx, int *cdy, bool mono) {
    int x0, y0, x1, y1;
    if (!ink_bbox(g, &x0, &y0, &x1, &y1)) return NULL;
    if (g->asDrawn) { x0 = 0; y0 = 0; x1 = g->w - 1; y1 = g->h - 1; }
    char map[128];
    memset(map, '.', sizeof map);
    {
        char order[36];
        int k = ink_order(g->pix, (size_t)g->w * (size_t)g->h, order);
        for (int i = 0; i < k; i++) map[(unsigned char)order[i]] = mono ? '0' : PIXELS[1 + i];
    }
    int nw = x1 - x0 + 1, nh = y1 - y0 + 1;
    size_t n = (size_t)(nw + 1) * (size_t)nh;
    char *t = (char *)malloc(n + 1);
    if (!t) return NULL;
    size_t o = 0;
    for (int y = 0; y < nh; y++) {
        const char *row = g->pix + (size_t)(y + y0) * g->w + x0;
        for (int x = 0; x < nw; x++) t[o++] = map[(unsigned char)row[x] & 127];
        t[o++] = '\n';
    }
    t[o] = 0;
    if (len) *len = o;
    if (cdx) *cdx = x0;
    if (cdy) *cdy = g->h - 1 - y1;
    return t;
}

static void glyph_colors_cache(PxGlyph *g) {
    g->ncol = g->pix ? ink_order(g->pix, (size_t)g->w * (size_t)g->h, g->order) : 0;
}

static void glyph_prepare(const PxGlyph *cg) {   /* mutable cache */
    PxGlyph *g = (PxGlyph *)cg;
    if (g->hashValid || g->lazy) return;
    size_t n;
    int dx = 0, dy = 0;
    char *t = glyph_text(g, &n, &dx, &dy, false);
    if (!t) { g->hash[0] = 0; g->cdx = g->cdy = 0; }
    else { px_hash128_hex(t, n, g->hash, false); g->cdx = dx; g->cdy = dy; free(t); }
    glyph_colors_cache(g);
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
    char *t = glyph_text(g, &n, NULL, NULL, false);
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

/* ------------------------------------------------------------------ colours: hex palettes (spec/hex.md) */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool px_hex_parse(const char *data, size_t len, uint32_t **colors, int *n, char err[PX_ERR_LEN]) {
    *colors = NULL;
    *n = 0;
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)data[i] >= 0x80) return fail(err, "a byte of 0x80 or above");
    /* the lines as in the canonical form (CR LF, CR, LF; trailing blanks cut), but an empty line is an error: only the end of
     * the last line may be followed by nothing */
    size_t cap = 16;
    uint32_t *c = (uint32_t *)malloc(sizeof(uint32_t) * cap);
    if (!c) return fail(err, "out of memory");
    int count = 0;
    size_t i = 0;
    while (i < len) {
        size_t s = i;
        while (i < len && data[i] != '\n' && data[i] != '\r') i++;
        size_t e = i;
        if (i < len) { if (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n') i += 2; else i++; }
        while (e > s && (data[e - 1] == ' ' || data[e - 1] == '\t')) e--;
        size_t l = e - s;
        if (l == 0) { free(c); return fail(err, "an empty line"); }
        if (l != 6 && l != 8) { free(c); return fail(err, "line %d is not RRGGBB or RRGGBBAA", count + 1); }
        uint32_t v = 0;
        for (size_t k = 0; k < l; k++) {
            int d = hexval(data[s + k]);
            if (d < 0) { free(c); return fail(err, "line %d is not RRGGBB or RRGGBBAA in upper-case hex digits", count + 1); }
            v = v * 16 + (uint32_t)d;
        }
        if (l == 6) v = (v << 8) | 0xFFu;
        else if ((v & 0xFFu) == 0xFFu) { free(c); return fail(err, "line %d writes the alpha FF (an opaque colour has six digits)", count + 1); }
        for (int k = 0; k < count; k++)
            if (c[k] == v) { free(c); return fail(err, "the colour of line %d is in the palette twice", count + 1); }
        if ((size_t)count == cap) {
            cap *= 2;
            uint32_t *nc = (uint32_t *)realloc(c, sizeof(uint32_t) * cap);
            if (!nc) { free(c); return fail(err, "out of memory"); }
            c = nc;
        }
        c[count++] = v;
    }
    if (count == 0) { free(c); return fail(err, "no colour"); }
    *colors = c;
    *n = count;
    return true;
}

char *px_hex_text(const uint32_t *colors, int n, size_t *len, char hash[33]) {
    if (n <= 0) return NULL;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < i; k++) if (colors[k] == colors[i]) return NULL;
    char *t = (char *)malloc((size_t)n * 9 + 1);
    if (!t) return NULL;
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        uint32_t v = colors[i];
        if ((v & 0xFFu) == 0xFFu) o += (size_t)sprintf(t + o, "%06X\n", (unsigned)(v >> 8));
        else o += (size_t)sprintf(t + o, "%08X\n", (unsigned)v);
    }
    if (len) *len = o;
    if (hash) px_hash128_hex(t, o, hash, false);
    return t;
}

bool px_pixels_canonical(const char *pix, size_t n) {
    int seen = 0;
    for (size_t i = 0; i < n; i++) {
        if (pix[i] == '.') continue;
        int num = px_pixel_index(pix[i]) - 1;
        if (num < 0 || num > seen) return false;
        if (num == seen) seen++;
    }
    return true;
}

/* ---- the palette table (see THE PALETTE TABLE in pxfont.h) */

static uint32_t BLACK1[1] = {0x000000FFu};
#define BK {0, 0, 0, 255}
static const PxPalette DEFPAL = {     /* constant: read by the loader threads too */
    "2f1ab4cf0adb400efabfa05a6c1ca379", 1, BLACK1,      /* the hash of "000000\n" */
    {{0, 0, 0, 0}, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK,
     BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK, BK}};
#undef BK

static void fill_draw(PxPalette *p) {
    memset(p->draw, 0, sizeof p->draw);       /* '.' is transparent */
    for (int i = 1; i < PX_NUM_PIXELS; i++) {
        uint32_t c = i - 1 < p->n ? p->c[i - 1] : 0x000000FFu;
        p->draw[i][0] = (uint8_t)(c >> 24); p->draw[i][1] = (uint8_t)(c >> 16); p->draw[i][2] = (uint8_t)(c >> 8); p->draw[i][3] = (uint8_t)c;
    }
}

static void pal_hash(PxPalette *p) {
    char *t = px_hex_text(p->c, p->n, NULL, p->hash);
    if (!t) p->hash[0] = 0;
    free(t);
}

static const PxPalette *default_palette(void) { return &DEFPAL; }

/* room for one more entry (the default entry 0 is made with the table) */
static bool table_room(PxFont *f) {
    int need = (f->npals ? f->npals : 1) + 1;
    if (need > f->palCap) {
        int nc = f->palCap ? f->palCap * 2 : 8;
        while (nc < need) nc *= 2;
        PxPalette *np = (PxPalette *)realloc(f->pals, sizeof(PxPalette) * (size_t)nc);
        if (!np) return false;
        f->pals = np;
        f->palCap = nc;
    }
    if (f->npals == 0) {
        PxPalette *d = &f->pals[0];
        *d = *default_palette();
        d->c = (uint32_t *)malloc(sizeof(uint32_t));
        if (!d->c) return false;
        d->c[0] = 0x000000FFu;
        f->npals = 1;
    }
    return true;
}

int px_font_intern_palette(PxFont *f, const uint32_t *colors, int n) {
    if (n < 1) return -1;
    for (int i = 1; i < f->npals; i++)
        if (f->pals[i].n == n && memcmp(f->pals[i].c, colors, sizeof(uint32_t) * (size_t)n) == 0) return i;
    if (!table_room(f)) return -1;
    PxPalette *p = &f->pals[f->npals];
    memset(p, 0, sizeof *p);
    p->c = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)n);
    if (!p->c) return -1;
    memcpy(p->c, colors, sizeof(uint32_t) * (size_t)n);
    p->n = n;
    fill_draw(p);
    pal_hash(p);
    return f->npals++;
}

int px_font_add_palette(PxFont *f, const char *hash, const uint32_t *colors, int n) {
    (void)hash;
    return px_font_intern_palette(f, colors, n);
}

static int find_pal(const PxFont *f, const char *hash) {
    if (!hash || !hash[0]) return -1;
    for (int i = 1; i < f->npals; i++) if (strcmp(f->pals[i].hash, hash) == 0) return i;
    return -1;
}

const PxPalette *px_font_find_palette(const PxFont *f, const char *hash) {
    int i = find_pal(f, hash);
    return i > 0 ? &f->pals[i] : NULL;
}

const PxPalette *px_glyph_palette(const PxFont *f, const PxGlyph *g) {
    if (g->pi > 0 && g->pi < f->npals) return &f->pals[g->pi];
    return default_palette();
}

uint32_t px_glyph_color(const PxFont *f, const PxGlyph *g, char c) {
    int i = px_pixel_index(c);
    if (i <= 0) return 0;
    const PxPalette *p = px_glyph_palette(f, g);
    return i - 1 < p->n ? p->c[i - 1] : 0x000000FFu;
}

int px_glyph_find_color(const PxFont *f, const PxGlyph *g, uint32_t color) {
    const PxPalette *p = px_glyph_palette(f, g);
    for (int i = 0; i < p->n && i < PX_GLYPH_MAX_COLORS; i++) if (p->c[i] == color) return i;
    return -1;
}

bool px_glyph_set_palette(PxFont *f, PxGlyph *g, const uint32_t *colors, int n) {
    int i = px_font_intern_palette(f, colors, n);
    if (i < 0) return false;
    g->pi = i;
    return true;
}

/* px_glyph_add_colors, or with dry only whether it would succeed (0 or PX_COLOR_FULL; nothing changes) */
static int add_colors(PxFont *f, PxGlyph *g, const uint32_t *colors, int nc, int *slots, bool dry) {
    const PxPalette *p = px_glyph_palette(f, g);
    int n = p->n;
    uint32_t *c = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(n + nc + 1));
    if (!c) return -1;
    memcpy(c, p->c, sizeof(uint32_t) * (size_t)n);
    bool used[PX_GLYPH_MAX_COLORS] = {false}, changed = !(g->pi > 0 && g->pi < f->npals), ink = false;
    size_t np = g->pix ? (size_t)g->w * (size_t)g->h : 0;
    for (size_t k = 0; k < np; k++) { int i = px_pixel_index(g->pix[k]); if (i > 0) { used[i - 1] = true; ink = true; } }
    if (changed && !ink) n = 0;     /* an empty glyph of the default palette: its black is no colour of its own */
    int lim0 = n < PX_GLYPH_MAX_COLORS ? n : PX_GLYPH_MAX_COLORS;
    bool *have = (bool *)calloc((size_t)nc + 1, sizeof(bool));
    if (!have) { free(c); return -1; }
    for (int j = 0; j < nc; j++) {                        /* the colours the palette has keep their entries */
        int s = 0;
        while (s < lim0 && c[s] != colors[j]) s++;
        if (s < lim0) { used[s] = true; have[j] = true; if (slots) slots[j] = s; }
    }
    for (int j = 0; j < nc; j++) {
        if (have[j]) continue;
        int lim = n < PX_GLYPH_MAX_COLORS ? n : PX_GLYPH_MAX_COLORS, s = 0;
        while (s < lim && c[s] != colors[j]) s++;          /* a colour twice in colors[] */
        if (s == lim) {                                   /* a new colour: a new entry at the end, else (36 entries) one no pixel uses */
            if (n < PX_GLYPH_MAX_COLORS) s = n++;
            else {
                for (s = 0; s < lim && used[s]; s++) {}
                if (s == lim) { free(have); free(c); return PX_COLOR_FULL; }
            }
            c[s] = colors[j];
            changed = true;
        }
        used[s] = true;                                   /* taken: a later new colour does not get it */
        if (slots) slots[j] = s;
    }
    free(have);
    bool ok = dry || !changed || px_glyph_set_palette(f, g, c, n);   /* the table entry is found or added: copy on write */
    free(c);
    return ok ? 0 : -1;
}

int px_glyph_add_colors(PxFont *f, PxGlyph *g, const uint32_t *colors, int n, int *slots) {
    return add_colors(f, g, colors, n, slots, false);
}

bool px_glyph_colors_fit(const PxFont *f, const PxGlyph *g, const uint32_t *colors, int n) {
    return add_colors((PxFont *)f, (PxGlyph *)g, colors, n, NULL, true) == 0;
}

int px_glyph_add_color(PxFont *f, PxGlyph *g, uint32_t color) {
    int s = 0;
    int r = add_colors(f, g, &color, 1, &s, false);
    return r < 0 ? r : s;
}

/* The ink characters of n pixels in reading order (at most 36; a character that is no pixel counts as none); their number. */
static int ink_order(const char *pix, size_t n, char order[36]);

bool px_glyph_set_colors(PxFont *f, PxGlyph *g, const uint32_t charColor[PX_NUM_PIXELS]) {
    size_t np = g->pix ? (size_t)g->w * (size_t)g->h : 0;
    char order[36];
    int k = np ? ink_order(g->pix, np, order) : 0;
    if (k == 0) return true;
    uint32_t col[36];
    int nc = 0;
    char map[128];
    memset(map, '.', sizeof map);
    for (int i = 0; i < k; i++) {
        uint32_t c = charColor[px_pixel_index(order[i])];
        int j = 0;
        while (j < nc && col[j] != c) j++;
        if (j == nc) col[nc++] = c;
        map[(unsigned char)order[i]] = PIXELS[1 + j];
    }
    for (size_t i = 0; i < np; i++) g->pix[i] = map[(unsigned char)g->pix[i] & 127];
    px_glyph_touch(g);
    return px_glyph_set_palette(f, g, col, nc);
}

/* ---- canonical palettes in memory (see CANONICAL IN MEMORY in pxfont.h) */

static bool cin(const uint32_t *v, int n, uint32_t c) {
    for (int i = 0; i < n; i++) if (v[i] == c) return true;
    return false;
}

/* the glyph's palette as a list; *n entries, malloc'd (room for `extra` more); an empty glyph of the default palette has none */
static uint32_t *pal_list(const PxFont *f, const PxGlyph *g, int *n, int extra, bool *ink) {
    const PxPalette *p = px_glyph_palette(f, g);
    size_t np = g->pix ? (size_t)g->w * (size_t)g->h : 0;
    bool any = false;
    for (size_t k = 0; k < np && !any; k++) any = px_pixel_index(g->pix[k]) > 0;
    if (ink) *ink = any;
    uint32_t *c = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(p->n + extra + 1));
    if (!c) return NULL;
    *n = (!any && !(g->pi > 0 && g->pi < f->npals)) ? 0 : p->n;
    memcpy(c, p->c, sizeof(uint32_t) * (size_t)*n);
    return c;
}

/* points the glyph at the palette c[0..n) (none: the default palette); unchanged when it is that palette already */
static bool pal_set(PxFont *f, PxGlyph *g, const uint32_t *c, int n) {
    const PxPalette *p = px_glyph_palette(f, g);
    bool own = g->pi > 0 && g->pi < f->npals;
    if (n == 0) { g->pi = 0; return true; }
    if (p->n == n && memcmp(p->c, c, sizeof(uint32_t) * (size_t)n) == 0 && (own || n == 1)) return true;
    return px_glyph_set_palette(f, g, c, n);
}

bool px_glyph_canonicalize(PxFont *f, PxGlyph *g) {
    if (g->lazy) return false;
    const PxPalette *p = px_glyph_palette(f, g);
    int n = p->n;
    size_t np = g->pix ? (size_t)g->w * (size_t)g->h : 0;
    char order[36];
    int k = np ? ink_order(g->pix, np, order) : 0;
    uint32_t col[PX_NUM_PIXELS + 1];
    char map[128];
    memset(map, '.', sizeof map);
    int m = 0;
    bool pixSame = true;
    for (int i = 0; i < k; i++) {
        int s = px_pixel_index(order[i]) - 1;
        uint32_t c = s < n ? p->c[s] : 0x000000FFu;
        int j = 0;
        while (j < m && col[j] != c) j++;
        if (j == m) col[m++] = c;
        map[(unsigned char)order[i]] = PIXELS[1 + j];
        if (PIXELS[1 + j] != order[i]) pixSame = false;
    }
    if (k == 0 && !(g->pi > 0 && g->pi < f->npals)) return false;   /* an empty glyph of the default palette */
    uint32_t *all = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(m + n + 1));
    if (!all) return false;
    memcpy(all, col, sizeof(uint32_t) * (size_t)m);
    int t = m;
    for (int i = 0; i < n; i++) if (!cin(all, t, p->c[i])) all[t++] = p->c[i];   /* the unused entries, in their order */
    bool palSame = t == n && memcmp(all, p->c, sizeof(uint32_t) * (size_t)n) == 0;
    if (!pixSame) {
        for (size_t i = 0; i < np; i++) if (g->pix[i] != '.') g->pix[i] = map[(unsigned char)g->pix[i] & 127];
        px_glyph_touch(g);
    }
    if (!palSame) pal_set(f, g, all, t);
    free(all);
    return !pixSame || !palSame;
}

int px_glyph_used_colors(const PxGlyph *g) {
    if (g->lazy) return 0;
    char h[33];
    px_glyph_hash(g, h);
    return g->ncol;
}

int px_glyph_add_slot(PxFont *f, PxGlyph *g, uint32_t color) {
    int s = px_glyph_find_color(f, g, color);
    bool ink;
    int n;
    uint32_t *c = pal_list(f, g, &n, 1, &ink);
    if (!c) return -1;
    if (s >= 0 && (n > 0)) { free(c); return s; }
    if (n >= PX_GLYPH_MAX_COLORS) { free(c); return PX_COLOR_FULL; }
    c[n++] = color;
    bool ok = pal_set(f, g, c, n);
    free(c);
    return ok ? n - 1 : -1;
}

int px_glyph_replace_color(PxFont *f, PxGlyph *g, int slot, uint32_t color) {
    int n;
    uint32_t *c = pal_list(f, g, &n, 1, NULL);
    if (!c) return -1;
    if (n == 0 && slot == 0) { c[0] = 0x000000FFu; n = 1; }   /* the default black of an empty glyph */
    if (slot < 0 || slot >= n || slot >= PX_GLYPH_MAX_COLORS) { free(c); return -1; }
    if (c[slot] == color) { free(c); return 0; }
    int j = 0;
    while (j < n && (j == slot || c[j] != color)) j++;
    c[slot] = color;
    size_t np = g->pix ? (size_t)g->w * (size_t)g->h : 0;
    if (j < n) {     /* the colour is the glyph's already, in entry j: its pixels join the slot's, and entry j goes */
        for (int i = j; i + 1 < n; i++) c[i] = c[i + 1];
        n--;
        for (size_t i = 0; i < np; i++) {
            int x = px_pixel_index(g->pix[i]) - 1;
            if (x < 0) continue;
            if (x == j) x = slot;
            if (x > j) x--;
            g->pix[i] = PIXELS[1 + x];
        }
        if (np) px_glyph_touch(g);
    }
    bool ok = pal_set(f, g, c, n);
    free(c);
    if (!ok) return -1;
    px_glyph_canonicalize(f, g);
    return 0;
}

bool px_glyph_trim_palette(PxFont *f, PxGlyph *g) {
    if (g->lazy) return false;
    px_glyph_canonicalize(f, g);
    const PxPalette *p = px_glyph_palette(f, g);
    int k = px_glyph_used_colors(g);
    bool own = g->pi > 0 && g->pi < f->npals;
    if (!own || p->n <= k) return false;
    if (k == 0) { g->pi = 0; return true; }
    uint32_t c[PX_GLYPH_MAX_COLORS];
    memcpy(c, p->c, sizeof(uint32_t) * (size_t)k);
    return px_glyph_set_palette(f, g, c, k);
}

/* a set of colours (open addressing), for the font's colours */
typedef struct CSet { uint32_t *k; uint8_t *on; size_t cap, n; } CSet;

static bool cset_add(CSet *s, uint32_t v) {   /* true when v was not in it yet */
    if ((s->n + 1) * 2 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 64;
        uint32_t *nk = (uint32_t *)malloc(sizeof(uint32_t) * nc);
        uint8_t *no = (uint8_t *)calloc(nc, 1);
        if (!nk || !no) { free(nk); free(no); return false; }
        for (size_t i = 0; i < s->cap; i++)
            if (s->on[i]) {
                size_t h = (s->k[i] * 2654435761u) & (nc - 1);
                while (no[h]) h = (h + 1) & (nc - 1);
                no[h] = 1; nk[h] = s->k[i];
            }
        free(s->k); free(s->on);
        s->k = nk; s->on = no; s->cap = nc;
    }
    size_t h = (v * 2654435761u) & (s->cap - 1);
    while (s->on[h]) { if (s->k[h] == v) return false; h = (h + 1) & (s->cap - 1); }
    s->on[h] = 1; s->k[h] = v; s->n++;
    return true;
}

static void cset_free(CSet *s) { free(s->k); free(s->on); memset(s, 0, sizeof *s); }

static unsigned clist_key(const PxFont *f) {
    unsigned h = 2166136261u;
    h = (h ^ f->palGen) * 16777619u;
    h = (h ^ (unsigned)f->npals) * 16777619u;
    h = (h ^ (unsigned)f->count) * 16777619u;
    for (int i = 0; i < f->count; i++) h = (h ^ (unsigned)f->glyphs[i].pi) * 16777619u;
    return h;
}

static bool clist_push(PxFont *m, uint32_t c) {
    if (m->nclist == m->clistCap) {
        int nc = m->clistCap ? m->clistCap * 2 : 16;
        uint32_t *nl = (uint32_t *)realloc(m->clist, sizeof(uint32_t) * (size_t)nc);
        if (!nl) return false;
        m->clist = nl;
        m->clistCap = nc;
    }
    m->clist[m->nclist++] = c;
    return true;
}

int px_font_color_list(const PxFont *f, const uint32_t **out) {
    static const uint32_t black = 0x000000FFu;
    PxFont *m = (PxFont *)f;      /* the cache is mutable */
    unsigned key = clist_key(f);
    if (!m->clistOk || m->clistKey != key) {
        m->nclist = 0;
        m->clistOk = false;
        bool *done = f->npals > 0 ? (bool *)calloc((size_t)f->npals, sizeof(bool)) : NULL;
        CSet s = {0};
        bool ok = f->npals == 0 || done;
        for (int i = 0; i < f->count && ok; i++) {
            int p = f->glyphs[i].pi;
            if (p <= 0 || p >= f->npals || done[p]) continue;
            done[p] = true;
            const PxPalette *pp = &f->pals[p];
            for (int k = 0; k < pp->n && k < PX_GLYPH_MAX_COLORS && ok; k++)
                if (cset_add(&s, pp->c[k])) ok = clist_push(m, pp->c[k]);
        }
        free(done);
        cset_free(&s);
        if (ok && m->nclist == 0) ok = clist_push(m, black);
        if (ok) { m->clistOk = true; m->clistKey = key; }
    }
    if (!m->clistOk || m->nclist == 0) { *out = &black; return 1; }
    *out = m->clist;
    return m->nclist;
}

int px_font_color_index(const PxFont *f, uint32_t color) {
    const uint32_t *l;
    int n = px_font_color_list(f, &l);
    for (int i = 0; i < n; i++) if (l[i] == color) return i;
    return -1;
}

static bool any_lazy_palette(const PxFont *f) {
    for (int i = 0; i < f->count; i++) if (f->glyphs[i].lazy && f->glyphs[i].pal[0] && f->glyphs[i].hash[0]) return true;
    return false;
}

bool px_font_is_colour(const PxFont *f) {
    if (any_lazy_palette(f)) return true;
    const uint32_t *l;
    int n = px_font_color_list(f, &l);
    return n >= 2 || (n == 1 && l[0] != 0x000000FFu);
}

/* the entries a glyph names: by pi, or by pal (the palette it was read with); NULL when the table is empty (or out of memory) */
static bool *named_palettes(const PxFont *f) {
    if (f->npals <= 0) return NULL;
    bool *keep = (bool *)calloc((size_t)f->npals, sizeof(bool));
    if (!keep) return NULL;
    keep[0] = true;
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        if (g->pi > 0 && g->pi < f->npals) {
            keep[g->pi] = true;
            if (g->pal[0] && strcmp(f->pals[g->pi].hash, g->pal) == 0) continue;
        }
        if (g->pal[0]) { int r = find_pal(f, g->pal); if (r > 0) keep[r] = true; }
    }
    return keep;
}

void px_font_compact_palettes(PxFont *f) {
    bool *keep = named_palettes(f);
    if (!keep) return;
    int *map = (int *)malloc(sizeof(int) * (size_t)f->npals);
    if (!map) { free(keep); return; }
    int n = 0;
    for (int i = 0; i < f->npals; i++) {
        map[i] = 0;
        if (!keep[i]) { free(f->pals[i].c); continue; }
        if (n != i) f->pals[n] = f->pals[i];
        map[i] = n++;
    }
    if (n != f->npals) {
        for (int i = 0; i < f->count; i++) {
            int p = f->glyphs[i].pi;
            f->glyphs[i].pi = p > 0 && p < f->npals ? map[p] : 0;
        }
        f->npals = n;
        f->palGen = new_gen();
        f->clistOk = false;
    }
    free(map);
    free(keep);
}

bool px_font_has_glyph_palettes(const PxFont *f) {
    for (int i = 0; i < f->count; i++) if (f->glyphs[i].pal[0]) return true;
    return false;
}

/* the palettes the glyph lines name are in the table and the glyphs point at them */
static bool palettes_adopted(const PxFont *f) {
    for (int i = 0; i < f->count; i++) if (f->glyphs[i].pal[0]) return f->glyphs[i].pi > 0;
    return true;
}

bool px_font_adopt_palettes(PxFont *f, char err[PX_ERR_LEN]) {
    for (int i = 0; i < f->count; i++) {
        PxGlyph *g = &f->glyphs[i];
        if (!g->pal[0]) continue;
        int p = (g->pi > 0 && g->pi < f->npals && strcmp(f->pals[g->pi].hash, g->pal) == 0) ? g->pi : find_pal(f, g->pal);
        if (p <= 0) return fail(err, "code %06X: no palette file %s", (unsigned)g->code, g->pal);
        g->pi = p;
    }
    return true;
}

bool px_glyph_from_canonical(PxFont *f, PxGlyph *g, char err[PX_ERR_LEN]) {
    size_t n = (size_t)g->w * (size_t)g->h;
    if (!g->pix || !n) { g->ncol = 0; return true; }
    if (!px_pixels_canonical(g->pix, n))
        return fail(err, "code %06X: glyph file %s: the colours are not numbered in reading order from 0", (unsigned)g->code, g->hash);
    char order[36];
    int k = ink_order(g->pix, n, order);
    if (g->pal[0]) {
        const PxPalette *p = px_font_find_palette(f, g->pal);
        if (!p) return fail(err, "code %06X: no palette file %s", (unsigned)g->code, g->pal);
        if (p->n < k) return fail(err, "code %06X: the glyph uses %d colours, its palette %s has %d", (unsigned)g->code, k, g->pal, p->n);
        g->pi = (int)(p - f->pals);
    } else if (k > 1) {
        return fail(err, "code %06X: glyph file %s has %d colours, but the font has no COLORS line", (unsigned)g->code, g->hash, k);
    }
    glyph_colors_cache(g);
    return true;
}

bool px_font_finish_palette(PxFont *f, char err[PX_ERR_LEN]) {
    for (int i = 0; i < f->count; i++) if (f->glyphs[i].lazy) return true;
    int n = px_font_colors(f);
    if (f->colorsRead && n != f->colorsRead) return fail(err, "COLORS is %d, the glyph lines refer to %d colours", f->colorsRead, n);
    return true;
}

int px_font_colors(const PxFont *f) {
    if (any_lazy_palette(f)) return f->colorsRead;
    CSet s = {0};
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        if (g->lazy) continue;
        glyph_prepare(g);
        const PxPalette *p = px_glyph_palette(f, g);
        for (int k = 0; k < g->ncol; k++) {
            int s1 = px_pixel_index(g->order[k]);
            cset_add(&s, s1 > 0 && s1 - 1 < p->n ? p->c[s1 - 1] : 0x000000FFu);
        }
    }
    int n = (int)s.n;
    cset_free(&s);
    return n >= 2 ? n : 0;
}

static bool color_in(const uint32_t *v, int n, uint32_t c) {
    for (int i = 0; i < n; i++) if (v[i] == c) return true;
    return false;
}

/* The palette a loaded glyph with ink is written with in a colour font: its colours (malloc'd, *n of them) and hash; see
 * PALETTES in pxfont.h.  NULL on out of memory or a glyph whose colours are no palette. */
static uint32_t *glyph_write_palette(const PxFont *f, const PxGlyph *g, int *n, char hash[33]) {
    glyph_prepare(g);
    int k = g->ncol;
    const PxPalette *gp = px_glyph_palette(f, g);
    uint32_t c[36];
    for (int i = 0; i < k; i++) { int s = px_pixel_index(g->order[i]); c[i] = s > 0 && s - 1 < gp->n ? gp->c[s - 1] : 0x000000FFu; }
    const PxPalette *p = px_font_find_palette(f, g->pal);
    if (p && p->n >= k && memcmp(p->c, c, sizeof(uint32_t) * (size_t)k) == 0) {   /* the palette it was read with */
        uint32_t *out = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)p->n);
        if (!out) return NULL;
        memcpy(out, p->c, sizeof(uint32_t) * (size_t)p->n);
        *n = p->n;
        memcpy(hash, p->hash, 33);
        return out;
    }
    /* store.canonical_pixels: the glyph's colours in reading order, then the rest of its palette in memory (the glyph view drops
     * the unused slots it made before the font is written, see "The unused-slot rule" in DESIGN.md) */
    uint32_t *out = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(k + gp->n + 1));
    if (!out) return NULL;
    int m = 0;
    for (int i = 0; i < k; i++) out[m++] = c[i];
    for (int i = 0; i < gp->n; i++) if (!color_in(out, m, gp->c[i])) out[m++] = gp->c[i];
    char *t = px_hex_text(out, m, NULL, hash);
    if (!t) { free(out); return NULL; }
    free(t);
    *n = m;
    return out;
}

int px_font_write_palettes(const PxFont *f, PxPalette **out) {
    *out = NULL;
    if (!px_font_colors(f)) return 0;
    PxPalette *v = NULL;
    int n = 0, cap = 0;
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        if (g->lazy) continue;
        glyph_prepare(g);
        if (!g->hash[0]) continue;
        int pn;
        char h[33];
        uint32_t *c = glyph_write_palette(f, g, &pn, h);
        if (!c) { px_palettes_free(v, n); return -1; }
        bool have = false;
        for (int k = 0; k < n && !have; k++) if (strcmp(v[k].hash, h) == 0) have = true;
        if (have) { free(c); continue; }
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            PxPalette *nv = (PxPalette *)realloc(v, sizeof(PxPalette) * (size_t)cap);
            if (!nv) { free(c); px_palettes_free(v, n); return -1; }
            v = nv;
        }
        memcpy(v[n].hash, h, 33);
        v[n].c = c;
        v[n].n = pn;
        n++;
    }
    *out = v;
    return n;
}

char *px_font_glyph_pxgl(const PxFont *f, const PxGlyph *g, size_t *len, char hash[33]) {
    if (g->lazy) { hash[0] = 0; return NULL; }
    size_t n;
    char *t = glyph_text(g, &n, NULL, NULL, px_font_colors(f) == 0);
    if (!t) { hash[0] = 0; return NULL; }
    px_hash128_hex(t, n, hash, false);
    if (len) *len = n;
    return t;
}

/* ------------------------------------------------------------------ writer */

static int cmp_lineptr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* a kerning pair may name a code point, an F-code of a glyph the font has, or FFFFFF when the file has the line */
static bool kern_code_ok(const PxFont *f, int32_t c) {
    if (px_code_is_codepoint(c)) return true;
    return (px_code_is_uncoded(c) || c == PX_CODE_DEFAULT) && px_font_find(f, c) >= 0;
}

/* "x,y\tadvance[\tHASH[\tPALETTE]]"; ncolors: the font's COLORS (0: one colour) */
static void glyph_fields(const PxFont *f, const PxGlyph *g, int ncolors, char *out) {
    int x, y;
    px_glyph_written_offset(g, &x, &y);
    int n = sprintf(out, "%d,%d\t%d", x, y, g->advance);
    if (!g->hash[0]) return;
    char h[33];
    memcpy(h, g->hash, 33);
    if (!g->lazy && !ncolors && g->ncol > 1) {      /* a font of one colour: every ink pixel is '0' */
        size_t tl;
        char *t = glyph_text(g, &tl, NULL, NULL, true);
        if (t) { px_hash128_hex(t, tl, h, false); free(t); }
    }
    out[n++] = '\t';
    hash_upper(h, out + n);
    n += 32;
    if (!ncolors) return;
    char ph[33] = "";
    if (g->lazy) memcpy(ph, g->pal, 33);
    else {
        int pn;
        uint32_t *c = glyph_write_palette(f, g, &pn, ph);
        if (!c) ph[0] = 0;
        free(c);
    }
    if (ph[0]) { out[n++] = '\t'; hash_upper(ph, out + n); }
}

char *px_pxfm_text(const PxFont *f, size_t *len, char hashOut[33]) {
    Buf b = {0};
    int ncolors = px_font_colors(f);
    /* key order: ASCENT CAPHEIGHT COLORS DESCENT GLYPH INTERNALLEADING KERNING LINEGAP MONOSPACED QUADWIDTH RATIO
     * STRIKEOUTPOSITION STRIKEOUTTHICKNESS SYMBOLIC UNDERLINEPOSITION UNDERLINETHICKNESS XHEIGHT */
#define NUM(id) do { if (f->has[id]) buf_printf(&b, "%s\t%d\n", METRIC_KEY[id], f->m[id]); } while (0)
    NUM(PXM_ASCENT);
    NUM(PXM_CAPHEIGHT);
    if (ncolors) buf_printf(&b, "COLORS\t%d\n", ncolors);
    NUM(PXM_DESCENT);
    /* glyph lines */
    {
        enum { LINE = 176 };
        size_t cap = (size_t)f->count + 2;
        char *pool = (char *)malloc(cap * LINE);
        char **lines = (char **)malloc(cap * sizeof(char *));
        if (!pool || !lines) { free(pool); free(lines); free(b.p); return NULL; }
        int nl = 0;
        for (int i = 0; i < f->count; i++) {
            const PxGlyph *g = &f->glyphs[i];
            char *line = pool + (size_t)nl * LINE;
            int n;
            n = sprintf(line, "GLYPH\t%06X\t", (unsigned)g->code);
            glyph_fields(f, g, ncolors, line + n);
            lines[nl++] = line;
        }
        qsort(lines, (size_t)nl, sizeof(char *), cmp_lineptr);
        for (int i = 0; i < nl; i++) {
            if (i > 0 && strcmp(lines[i], lines[i - 1]) == 0) continue;
            buf_str(&b, lines[i]);
            buf_str(&b, "\n");
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
            if (f->kern[i].value != 0 && kern_code_ok(f, f->kern[i].first) && kern_code_ok(f, f->kern[i].second))
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
    if (f->symbolic) buf_str(&b, "SYMBOLIC\n");
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

/* the hex palettes the glyph lines name, from <libRoot>/palette, each read once and checked (hex.md, hash == name), into the
 * font's table; then each glyph is pointed at its palette */
static bool load_palettes(PxFont *f, const char *libRoot, char err[PX_ERR_LEN]) {
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        if (!g->pal[0] || px_font_find_palette(f, g->pal)) continue;
        char path[1100];
        snprintf(path, sizeof path, "%s/palette/%c%c/%s.hex", libRoot, g->pal[0], g->pal[1], g->pal);
        size_t rawLen;
        char *raw = px_read_file(path, &rawLen);
        if (!raw) return fail(err, "code %06X: palette file %s not found (%s)", (unsigned)g->code, g->pal, path);
        uint32_t *c;
        int n;
        char e2[PX_ERR_LEN];
        if (!px_hex_parse(raw, rawLen, &c, &n, e2)) { free(raw); return fail(err, "palette file %s: %s", g->pal, e2); }
        size_t cl;
        char *can = px_canonical(raw, rawLen, &cl);
        free(raw);
        if (!can) { free(c); return fail(err, "out of memory"); }
        char h[33];
        px_hash128_hex(can, cl, h, false);
        free(can);
        if (strcmp(h, g->pal) != 0) { free(c); return fail(err, "palette file %s: content hash is %s, not its name", g->pal, h); }
        bool ok = px_font_add_palette(f, g->pal, c, n) > 0;
        free(c);
        if (!ok) return fail(err, "out of memory");
    }
    return px_font_adopt_palettes(f, err);
}

bool px_load_glyphs(PxFont *f, const char *libRoot, const int32_t *cps, int n, char err[PX_ERR_LEN]) {
    if (px_font_has_glyph_palettes(f)) {
        /* a colour font: its palettes first (once), then every bitmap at once */
        if (!palettes_adopted(f) && !load_palettes(f, libRoot, err)) return false;
        cps = NULL;
        n = 0;
    }
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
            if (!px_glyph_from_canonical(f, g, err)) goto done;
        }
    }
    ok = px_font_finish_palette(f, err);
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

/* six upper-case hex digits: a code point (<= 10FFFF), an F-code (F00000..FFFFFE) or FFFFFF */
static bool parse_code(const char *s, int32_t *out) {
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
    bool seenRatio = false, seenMono = false, seenSym = false, seenColors = false, seen[PXM_COUNT] = {false};
    int lineNo = 0;
    char msg[2 * PX_ERR_LEN];
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
        } else if (strcmp(key, "SYMBOLIC") == 0) {
            if (na != 0) LFAIL("SYMBOLIC takes no field");
            if (seenSym) LFAIL("two SYMBOLIC lines");
            seenSym = true;
            f.symbolic = true;
        } else if (strcmp(key, "COLORS") == 0) {
            int v;
            if (na != 1) LFAIL("COLORS takes exactly 1 field");
            if (seenColors) LFAIL("two COLORS lines");
            seenColors = true;
            if (!parse_int(fld[1], &v)) LFAIL("COLORS '%s' is not an integer", fld[1]);
            if (v < 2) LFAIL("COLORS is written only for more than one colour");
            f.colorsRead = v;
        } else if (strcmp(key, "COLOR") == 0) {
            LFAIL("a COLOR line: the colours of a pxfm file before hex palettes (the file was made by an older pixelglyphlib)");
        } else if (strcmp(key, "KERNING") == 0) {
            if (na != 3) LFAIL("KERNING takes exactly 3 fields");
            int32_t a, b;
            int v;
            if (!parse_code(fld[1], &a) || !parse_code(fld[2], &b)) LFAIL("KERNING codes are six upper-case hex digits, a code point or from F00000");
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
            if (na < 3 || na > 5) LFAIL("GLYPH takes 3, 4 or 5 fields, not %d", na);
            if (strchr(fld[2], 'x') || (!strchr(fld[2], ',') && strchr(fld[3], ','))) LFAIL("pxfm version 1 file (GLYPH line '%s')", fld[2]);
            PxGlyph g;
            memset(&g, 0, sizeof g);
            if (strcmp(fld[1], "-") == 0 || strcmp(fld[1], "_DEF") == 0) LFAIL("pxfm version 1 file (code '%s')", fld[1]);
            if (!parse_code(fld[1], &g.code)) LFAIL("code '%s' is not six upper-case hex digits, up to 10FFFF or from F00000", fld[1]);
            char *comma = strchr(fld[2], ',');
            if (!comma) LFAIL("offsets '%s' are not x,y", fld[2]);
            *comma = 0;
            if (!parse_int(fld[2], &g.x) || !parse_int(comma + 1, &g.y)) LFAIL("offsets '%s,%s' are not integers", fld[2], comma + 1);
            if (!parse_int(fld[3], &g.advance)) LFAIL("advance '%s' is not an integer", fld[3]);
            if (na >= 4) {
                if (!px_is_hash(fld[4], true, false)) LFAIL("glyph hash '%s' is not 32 upper-case hex digits", fld[4]);
                hash_lower(fld[4], g.hash);
                g.lazy = true;
                if (na == 5) {
                    if (!px_is_hash(fld[5], true, false)) LFAIL("palette hash '%s' is not 32 upper-case hex digits", fld[5]);
                    hash_lower(fld[5], g.pal);
                }
            } else {
                if (g.x != 0 || g.y != 0) LFAIL("a glyph without a glyph file must be at 0,0");
                g.hash[0] = 0;
            }
            g.hashValid = true;
            if (!font_push(&f, &g)) LFAIL("out of memory");
        } else {
            LFAIL("unknown key '%s'", key);
        }
    }
#undef LFAIL

    for (int i = 0; i < f.count; i++) {    /* a colour font has a palette on every line with a glyph file, any other none */
        const PxGlyph *g = &f.glyphs[i];
        if (!g->hash[0]) continue;
        if (seenColors && !g->pal[0]) { fail(err, "%s: a GLYPH line with a glyph file has no palette in a colour font (code %06X)", where, (unsigned)g->code); goto bad; }
        if (!seenColors && g->pal[0]) { fail(err, "%s: a GLYPH line has a palette in a font with no COLORS line (code %06X)", where, (unsigned)g->code); goto bad; }
    }
    px_font_sort(&f);
    for (int i = 1; i < f.count; i++)
        if (f.glyphs[i].code == f.glyphs[i - 1].code) { fail(err, "%s: two GLYPH lines of the code %06X", where, (unsigned)f.glyphs[i].code); goto bad; }
    {
        int nu = px_font_uncoded_count(&f);      /* the F-codes are the last ones before FFFFFF: F00000, F00001, ... without a gap */
        int end = f.count - (px_font_default_index(&f) >= 0 ? 1 : 0);
        for (int i = 0; i < nu; i++)
            if (f.glyphs[end - nu + i].code != PX_CODE_UNCODED_FIRST + i) { fail(err, "%s: the codes from F00000 up are not F00000, F00001, ... each once", where); goto bad; }
    }
    if (f.kernCount > 1) {
        qsort(f.kern, (size_t)f.kernCount, sizeof(PxKern), kern_cmp);
        for (int i = 1; i < f.kernCount; i++)
            if (kern_cmp(&f.kern[i], &f.kern[i - 1]) == 0) { fail(err, "%s: two KERNING lines of one pair", where); goto bad; }
    }
    for (int i = 0; i < f.kernCount; i++) {
        int32_t c2[2] = {f.kern[i].first, f.kern[i].second};
        for (int k = 0; k < 2; k++)
            if (c2[k] >= PX_CODE_UNCODED_FIRST && px_font_find(&f, c2[k]) < 0) {
                fail(err, "%s: KERNING of code %06X, which no GLYPH line has", where, (unsigned)c2[k]);
                goto bad;
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
    bool mono = px_font_colors(f) == 0;

    for (int i = 0; i < f->count; i++) {
        const PxGlyph *gl = &f->glyphs[i];
        if (gl->lazy) continue;
        size_t n;
        char *g = glyph_text(gl, &n, NULL, NULL, mono);
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
    {
        PxPalette *pals;
        int np = px_font_write_palettes(f, &pals);
        if (np < 0) { free(text); return fail(err, "out of memory"); }
        for (int i = 0; i < np; i++) {
            size_t n;
            char h[33];
            char *t = px_hex_text(pals[i].c, pals[i].n, &n, h);
            if (!t) continue;
            snprintf(dir, sizeof dir, "%s/palette/%c%c", libRoot, h[0], h[1]);
            snprintf(path, sizeof path, "%s/%s.hex", dir, h);
            bool ok = px_file_exists(path) || (px_mkdir_p(dir) && px_write_file(path, t, n));
            free(t);
            if (!ok) { px_palettes_free(pals, np); free(text); return fail(err, "cannot write %s", path); }
        }
        px_palettes_free(pals, np);
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
    if (f->symbolic) buf_str(&b, "SYMBOLIC\n");
    for (int i = 1; i < f->npals; i++) {       /* the palettes the glyphs were read with */
        bool named = false;
        for (int k = 0; k < f->count && !named; k++) named = f->glyphs[k].pal[0] && strcmp(f->glyphs[k].pal, f->pals[i].hash) == 0;
        if (!named) continue;
        char up[33];
        hash_upper(f->pals[i].hash, up);
        buf_printf(&b, "PALETTE\t%s\t", up);
        for (int k = 0; k < f->pals[i].n; k++) buf_printf(&b, "%s%08X", k ? "," : "", (unsigned)f->pals[i].c[k]);
        buf_str(&b, "\n");
    }
    for (int i = 0; i < f->kernCount; i++)
        if (f->kern[i].value != 0)
            buf_printf(&b, "KERNING\t%06X\t%06X\t%d\n", (unsigned)f->kern[i].first, (unsigned)f->kern[i].second, f->kern[i].value);
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        bool has = g->pix && g->w > 0 && g->h > 0;
        char cp[16];
        snprintf(cp, sizeof cp, "%06X", (unsigned)g->code);
        buf_printf(&b, "GLYPH\t%s\t%d,%d\t%d\t%dx%d\t%d", cp, g->x, g->y, g->advance, has ? g->w : 0, has ? g->h : 0, g->asDrawn ? 1 : 0);
        bool own = g->pi > 0 && g->pi < f->npals;
        if (g->pal[0]) { char up[33]; hash_upper(g->pal, up); buf_printf(&b, "\t%s", up); }
        else if (own) buf_str(&b, "\t-");
        if (own) {
            const PxPalette *p = &f->pals[g->pi];
            for (int k = 0; k < p->n; k++) buf_printf(&b, "%s%08X", k ? "," : "\t", (unsigned)p->c[k]);
        }
        buf_str(&b, "\n");
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
    bool anyColor = false, anyOwn = false;
    uint32_t oldColor[PX_NUM_PIXELS];       /* a draft of pxfe 21: its font-wide palette */
    for (int k = 0; k < PX_NUM_PIXELS; k++) oldColor[k] = 0x000000FFu;
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
        char *fld[9];
        int nf = 0;
        char *s = line;
        for (;;) {
            if (nf >= 8) LFAIL("too many fields");
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
        } else if (strcmp(key, "SYMBOLIC") == 0) {
            f.symbolic = true;
        } else if (strcmp(key, "COLOR") == 0) {
            uint32_t c;
            int pi = (na == 2 && fld[1][1] == 0) ? px_pixel_index(fld[1][0]) : -1;
            if (pi < 0 || !parse_color(fld[2], &c)) LFAIL("bad COLOR");
            if (pi > 0) { oldColor[pi] = c; anyColor = true; }   /* '.' (older drafts) has no colour */
        } else if (strcmp(key, "PALETTE") == 0) {
            char h[33];
            uint32_t c[256];
            int nc = 0;
            if (na != 2 || !px_is_hash(fld[1], true, false)) LFAIL("bad PALETTE");
            hash_lower(fld[1], h);
            for (char *q = fld[2]; *q && nc < 256;) {
                char *e = strchr(q, ',');
                if (e) *e = 0;
                if (!parse_color(q, &c[nc])) LFAIL("bad PALETTE colour '%s'", q);
                nc++;
                q = e ? e + 1 : q + strlen(q);
            }
            if (nc == 0 || px_font_add_palette(&f, h, c, nc) <= 0) LFAIL("bad PALETTE");
        } else if (strcmp(key, "KERNING") == 0) {
            int32_t a, b;
            int v;
            if (na != 3 || !parse_code(fld[1], &a) || !parse_code(fld[2], &b) || !parse_int(fld[3], &v)) LFAIL("bad KERNING");
            px_font_set_kern(&f, a, b, v);
        } else if (strcmp(key, "GLYPH") == 0) {
            if (na < 5 || na > 7) LFAIL("GLYPH takes 5, 6 or 7 fields");
            PxGlyph g;
            memset(&g, 0, sizeof g);
            if (!parse_code(fld[1], &g.code)) LFAIL("bad code '%s'", fld[1]);
            char *comma = strchr(fld[2], ',');
            if (!comma) LFAIL("bad offsets");
            *comma = 0;
            if (!parse_int(fld[2], &g.x) || !parse_int(comma + 1, &g.y) || !parse_int(fld[3], &g.advance)) LFAIL("bad glyph numbers");
            char *xs = strchr(fld[4], 'x');
            if (!xs) LFAIL("bad size '%s'", fld[4]);
            *xs = 0;
            if (!parse_int(fld[4], &g.w) || !parse_int(xs + 1, &g.h) || g.w < 0 || g.h < 0 || (g.w == 0) != (g.h == 0)) LFAIL("bad size");
            g.asDrawn = fld[5][0] == '1';
            if (na >= 6 && strcmp(fld[6], "-") != 0) {
                if (!px_is_hash(fld[6], true, false)) LFAIL("bad palette hash '%s'", fld[6]);
                hash_lower(fld[6], g.pal);
            }
            if (na == 7) {            /* the glyph's palette in memory */
                uint32_t c[256];
                int nc = 0;
                for (char *q = fld[7]; *q && nc < 256;) {
                    char *e = strchr(q, ',');
                    if (e) *e = 0;
                    if (!parse_color(q, &c[nc])) LFAIL("bad glyph palette colour '%s'", q);
                    nc++;
                    q = e ? e + 1 : q + strlen(q);
                }
                if (nc == 0 || !px_glyph_set_palette(&f, &g, c, nc)) LFAIL("bad glyph palette");
                anyOwn = true;
            }
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
    if (anyColor && !anyOwn)          /* a draft of pxfe 21: its glyphs get palettes of their own */
        for (int i = 0; i < f.count; i++)
            if (!px_glyph_set_colors(&f, &f.glyphs[i], oldColor)) { fail(err, "out of memory"); goto bad; }
    for (int i = 0; i < f.count; i++) if (f.glyphs[i].pal[0] && !px_font_find_palette(&f, f.glyphs[i].pal)) f.glyphs[i].pal[0] = 0;
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
