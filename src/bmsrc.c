/* bmsrc.c - bitmap font sources: BDF, PCF, FNT/FON, sheet descriptors, sbit strikes of sfnt files.  See bmsrc.h.
 * Follows pixelglyphlib/readers/{bdf,pcf,fnt,sheet,sbit}.py.  Every read of the file goes through bounds-checked helpers
 * (reads outside the data give 0) and every count read from a file is checked against the size of the data. */
#include "bmsrc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_MAX_DIMENSIONS (1 << 12)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "bmsrc_cp.inc"

#define BUDGET_BYTES ((size_t)192 << 20)   /* all glyph pixels of one source */
#define MAX_DIM 2048

/* ------------------------------------------------------------------------------------------------ structures */
typedef struct { int cp, idx; } MapEnt;

typedef struct BmStrike {
    BmStrikeInfo info;
    BmGlyph *g;
    int n, cap;
    MapEnt *map;
    int nmap, capmap;
} BmStrike;

struct BmSource {
    BmStrike *s;
    int n, cap;
    size_t budget;
};

typedef struct { const unsigned char *d; size_t n; } Buf;

/* ------------------------------------------------------------------------------------------------ small helpers */
static int fail(char *err, size_t en, const char *fmt, ...) {
    if (err && en) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, en, fmt, ap);
        va_end(ap);
    }
    return 0;
}

static unsigned rd8(const Buf *b, uint64_t o) { return o < b->n ? b->d[o] : 0u; }
static unsigned rd16(const Buf *b, uint64_t o, int be) {
    if (o > b->n || b->n - o < 2) return 0;
    return be ? (unsigned)(b->d[o] << 8 | b->d[o + 1]) : (unsigned)(b->d[o + 1] << 8 | b->d[o]);
}
static uint32_t rd32(const Buf *b, uint64_t o, int be) {
    if (o > b->n || b->n - o < 4) return 0;
    const unsigned char *p = b->d + o;
    return be ? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3])
              : ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]);
}
static int rdi8(const Buf *b, uint64_t o) { return (int)(signed char)rd8(b, o); }
static int rdi16(const Buf *b, uint64_t o, int be) { return (int)(short)rd16(b, o, be); }
static int rdi32(const Buf *b, uint64_t o, int be) { return (int)rd32(b, o, be); }

static void copy_str(char *dst, size_t cap, const char *s, size_t len) {
    size_t n = len < cap - 1 ? len : cap - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
}

static char *dup_n(const char *s, size_t len) {
    char *p = (char *)malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len);
    p[len] = 0;
    return p;
}

static int round_div_even(long long num, long long den) {   /* Python's round(num / den), for den > 0 */
    if (den <= 0) return 0;
    long long q = num / den, r = num % den;
    if (r < 0) { r += den; q -= 1; }
    if (2 * r > den || (2 * r == den && (q & 1))) q++;
    return (int)q;
}

/* ------------------------------------------------------------------------------------------------ source / strike building */
static void strike_free(BmStrike *s) {
    for (int i = 0; i < s->n; i++) {
        free((void *)s->g[i].pix);
        free((void *)s->g[i].name);
    }
    free(s->g);
    free(s->map);
}

static BmSource *src_new(void) {
    BmSource *src = (BmSource *)calloc(1, sizeof *src);
    if (src) src->budget = BUDGET_BYTES;
    return src;
}

void bm_close(BmSource *src) {
    if (!src) return;
    for (int i = 0; i < src->n; i++) strike_free(&src->s[i]);
    free(src->s);
    free(src);
}

static BmStrike *src_add_strike(BmSource *src) {
    if (src->n == src->cap) {
        int nc = src->cap ? src->cap * 2 : 4;
        BmStrike *p = (BmStrike *)realloc(src->s, (size_t)nc * sizeof *p);
        if (!p) return NULL;
        src->s = p;
        src->cap = nc;
    }
    BmStrike *s = &src->s[src->n++];
    memset(s, 0, sizeof *s);
    s->info.depth = 1;
    return s;
}

static void src_drop_last(BmSource *src) {
    if (src->n > 0) strike_free(&src->s[--src->n]);
}

static unsigned char *pix_alloc(BmSource *src, size_t n) {
    if (n == 0) return NULL;
    if (n > src->budget) return NULL;
    unsigned char *p = (unsigned char *)calloc(n, 1);
    if (p) src->budget -= n;
    return p;
}

static BmGlyph *strike_add(BmStrike *s) {
    if (s->n == s->cap) {
        int nc = s->cap ? s->cap * 2 : 64;
        BmGlyph *p = (BmGlyph *)realloc(s->g, (size_t)nc * sizeof *p);
        if (!p) return NULL;
        s->g = p;
        s->cap = nc;
    }
    BmGlyph *g = &s->g[s->n++];
    memset(g, 0, sizeof *g);
    g->codepoint = -1;
    g->code = -1;
    g->depth = s->info.depth;
    return g;
}

static int map_add(BmStrike *s, int cp, int idx) {
    if (cp < 0) return 1;
    if (s->nmap == s->capmap) {
        int nc = s->capmap ? s->capmap * 2 : 64;
        MapEnt *p = (MapEnt *)realloc(s->map, (size_t)nc * sizeof *p);
        if (!p) return 0;
        s->map = p;
        s->capmap = nc;
    }
    s->map[s->nmap].cp = cp;
    s->map[s->nmap].idx = idx;
    s->nmap++;
    return 1;
}

static int map_cmp(const void *a, const void *b) {
    const MapEnt *x = (const MapEnt *)a, *y = (const MapEnt *)b;
    if (x->cp != y->cp) return x->cp < y->cp ? -1 : 1;
    return x->idx < y->idx ? -1 : x->idx > y->idx;
}

static void strike_finish(BmStrike *s) {
    if (s->nmap > 1) qsort(s->map, (size_t)s->nmap, sizeof *s->map, map_cmp);
    s->info.glyph_count = s->n;
}

/* ------------------------------------------------------------------------------------------------ codepages */
typedef struct { int mode; const uint16_t *t; } Codec;   /* 0: code = Unicode, 1: table, 2: ASCII only, 3: nothing maps */

static void norm_name(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (; *in && o + 1 < cap; in++) {
        char c = *in;
        if (c == '-' || c == '_' || c == ' ') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[o++] = c;
    }
    out[o] = 0;
}

static int codec_from_name(const char *name, Codec *c) {
    char n[48];
    norm_name(name, n, sizeof n);
    if (!strcmp(n, "ascii") || !strcmp(n, "usascii") || !strcmp(n, "646")) { c->mode = 2; c->t = NULL; return 1; }
    if (!strcmp(n, "latin1") || !strcmp(n, "l1") || !strcmp(n, "iso88591")) strcpy(n, "iso88591");
    else if (!strncmp(n, "windows", 7) && n[7]) { memmove(n + 2, n + 7, strlen(n + 7) + 1); n[0] = 'c'; n[1] = 'p'; }
    else if (n[0] >= '0' && n[0] <= '9') { memmove(n + 2, n, strlen(n) + 1); n[0] = 'c'; n[1] = 'p'; }
    else if (!strncmp(n, "ibm", 3) && n[3] >= '0' && n[3] <= '9') { memmove(n + 2, n + 3, strlen(n + 3) + 1); n[0] = 'c'; n[1] = 'p'; }
    for (size_t i = 0; i < sizeof BM_CODEPAGES / sizeof BM_CODEPAGES[0]; i++)
        if (!strcmp(BM_CODEPAGES[i].name, n)) { c->mode = 1; c->t = BM_CODEPAGES[i].t; return 1; }
    return 0;
}

static int codec_map(const Codec *c, int code) {   /* Unicode of a byte code, or -1 (the Python reader's None) */
    switch (c->mode) {
    case 0: return code;
    case 3: return -1;
    default:
        if (code < 0 || code > 255) return -1;
        if (code < 0x80) return code;
        if (c->mode == 2) return -1;
        return c->t[code - 128] ? (int)c->t[code - 128] : -1;
    }
}

/* ------------------------------------------------------------------------------------------------ properties */
typedef struct { char **k, **v; int n, cap; } Props;

static void props_free(Props *p) {
    for (int i = 0; i < p->n; i++) { free(p->k[i]); free(p->v[i]); }
    free(p->k);
    free(p->v);
    memset(p, 0, sizeof *p);
}

static int props_add(Props *p, const char *k, size_t kl, const char *v, size_t vl) {
    if (p->n == p->cap) {
        int nc = p->cap ? p->cap * 2 : 32;
        char **nk = (char **)realloc(p->k, (size_t)nc * sizeof *nk);
        if (!nk) return 0;
        p->k = nk;
        char **nv = (char **)realloc(p->v, (size_t)nc * sizeof *nv);
        if (!nv) return 0;
        p->v = nv;
        p->cap = nc;
    }
    p->k[p->n] = dup_n(k, kl);
    p->v[p->n] = dup_n(v, vl);
    if (!p->k[p->n] || !p->v[p->n]) { free(p->k[p->n]); free(p->v[p->n]); return 0; }
    p->n++;
    return 1;
}

static const char *props_get(const Props *p, const char *key) {   /* the last of equal keys (a dict in the Python reader) */
    for (int i = p->n - 1; i >= 0; i--)
        if (!strcmp(p->k[i], key)) return p->v[i];
    return NULL;
}

static int props_int(const Props *p, const char *key, int def) {
    const char *s = props_get(p, key);
    if (!s || !*s) return def;
    char *e;
    long v = strtol(s, &e, 10);
    return (int)v;
}

/* the X11 family of properties shared by BDF and PCF: charset codec, names, sizes */
static Codec x11_codec(const Props *p) {
    const char *reg = props_get(p, "CHARSET_REGISTRY");
    const char *enc = props_get(p, "CHARSET_ENCODING");
    char r[64], name[96];
    Codec c = {0, NULL};
    if (!reg) reg = "ISO10646";
    if (!enc) enc = "1";
    size_t i = 0;
    for (; reg[i] && i + 1 < sizeof r; i++) r[i] = (reg[i] >= 'a' && reg[i] <= 'z') ? (char)(reg[i] - 32) : reg[i];
    r[i] = 0;
    if (!strcmp(r, "ISO10646")) return c;
    if (!strncmp(r, "ISO8859", 7)) snprintf(name, sizeof name, "iso8859-%s", enc);
    else if (!strcmp(r, "MICROSOFT") || !strncmp(r, "CP", 2)) snprintf(name, sizeof name, "cp%s", enc);
    else if (!strcmp(r, "KOI8")) snprintf(name, sizeof name, "koi8-%s", enc);
    else return c;
    if (!codec_from_name(name, &c)) { c.mode = 3; c.t = NULL; }
    return c;
}

static void x11_info(BmStrikeInfo *in, const Props *p, int ascent, int descent) {
    const char *s;
    in->ascent = ascent;
    in->descent = descent;
    in->pixel_size = props_int(p, "PIXEL_SIZE", ascent + descent);
    snprintf(in->label, sizeof in->label, "%dpx", in->pixel_size);
    if ((s = props_get(p, "FAMILY_NAME")) != NULL) copy_str(in->family, sizeof in->family, s, strlen(s));
    else if ((s = props_get(p, "FONT")) != NULL) copy_str(in->family, sizeof in->family, s, strlen(s));
    if ((s = props_get(p, "WEIGHT_NAME")) != NULL) copy_str(in->style, sizeof in->style, s, strlen(s));
    if ((s = props_get(p, "FACE_NAME")) != NULL) copy_str(in->name, sizeof in->name, s, strlen(s));
    else if ((s = props_get(p, "FONT")) != NULL) copy_str(in->name, sizeof in->name, s, strlen(s));
    else snprintf(in->name, sizeof in->name, "%s", in->family);
}

/* ------------------------------------------------------------------------------------------------ BDF */
static int is_prefix(const char *s, size_t len, const char *pre) {
    size_t n = strlen(pre);
    return len >= n && !memcmp(s, pre, n);
}

static int next_line(const Buf *b, size_t *pos, const char **ls, size_t *len) {
    if (*pos >= b->n) return 0;
    const char *d = (const char *)b->d;
    size_t s = *pos, e = s;
    while (e < b->n && d[e] != '\n') e++;
    *pos = e < b->n ? e + 1 : e;
    if (e > s && d[e - 1] == '\r') e--;
    *ls = d + s;
    *len = e - s;
    return 1;
}

static const char *skip_ws(const char *p, const char *e) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\v' || *p == '\f')) p++;
    return p;
}

/* splits "key rest" like Python's split(None, 1); rest is trimmed of trailing whitespace here (callers strip anyway) */
static void split_key(const char *ls, size_t len, const char **key, size_t *kl, const char **rest, size_t *rl) {
    const char *e = ls + len;
    const char *p = skip_ws(ls, e);
    const char *k = p;
    while (p < e && !(*p == ' ' || *p == '\t' || *p == '\v' || *p == '\f')) p++;
    *key = k;
    *kl = (size_t)(p - k);
    p = skip_ws(p, e);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\v' || e[-1] == '\f')) e--;
    *rest = p;
    *rl = (size_t)(e - p);
}

static int parse_ints(const char *s, size_t len, int *out, int max) {
    int n = 0;
    const char *e = s + len;
    while (n < max) {
        s = skip_ws(s, e);
        if (s >= e) break;
        char tmp[32];
        size_t l = 0;
        while (s + l < e && s[l] != ' ' && s[l] != '\t' && l + 1 < sizeof tmp) l++;
        memcpy(tmp, s, l);
        tmp[l] = 0;
        char *end;
        long v = strtol(tmp, &end, 10);
        if (end == tmp) break;
        out[n++] = (int)v;
        s += l;
    }
    return n;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int load_bdf(BmSource *src, const Buf *b, char *err, size_t en) {
    Props props;
    memset(&props, 0, sizeof props);
    int bbox[4] = {0, 0, 0, 0}, have_bbox = 0;
    BmStrike *st = src_add_strike(src);
    if (!st) { return fail(err, en, "out of memory"); }
    size_t pos = 0;
    const char *ls;
    size_t len;
    int ok = 1;
    while (next_line(b, &pos, &ls, &len)) {
        const char *key, *rest;
        size_t kl, rl;
        split_key(ls, len, &key, &kl, &rest, &rl);
        if (kl == 15 && !memcmp(key, "FONTBOUNDINGBOX", 15)) {
            int v[4];
            if (parse_ints(rest, rl, v, 4) == 4) { memcpy(bbox, v, sizeof bbox); have_bbox = 1; }
        } else if (kl == 15 && !memcmp(key, "STARTPROPERTIES", 15)) {
            while (next_line(b, &pos, &ls, &len)) {
                if (is_prefix(ls, len, "ENDPROPERTIES")) break;
                split_key(ls, len, &key, &kl, &rest, &rl);
                if (!kl) continue;
                if (rl >= 2 && rest[0] == '"' && rest[rl - 1] == '"') {   /* unquote, "" is a quote */
                    char *tmp = dup_n(rest + 1, rl - 2);
                    if (tmp) {
                        size_t o = 0;
                        for (size_t i = 0; tmp[i]; i++) { if (tmp[i] == '"' && tmp[i + 1] == '"') i++; tmp[o++] = tmp[i]; }
                        tmp[o] = 0;
                        if (!props_add(&props, key, kl, tmp, o)) ok = 0;
                        free(tmp);
                    } else ok = 0;
                } else if (!props_add(&props, key, kl, rest, rl)) ok = 0;
                if (!ok) break;
            }
        } else if (kl == 9 && !memcmp(key, "STARTCHAR", 9)) {
            char *name = dup_n(rest, rl);
            int enc = -1, dwidth = -1000000, bbx[4] = {0, 0, 0, 0}, have_bbx = 0, w = 0, h = 0, bitmap_seen = 0;
            unsigned char *pix = NULL;
            int complete = 0, bad = 0;
            while (next_line(b, &pos, &ls, &len)) {
                if (is_prefix(ls, len, "ENDCHAR")) { complete = 1; break; }
                split_key(ls, len, &key, &kl, &rest, &rl);
                if (kl == 8 && !memcmp(key, "ENCODING", 8)) { int v[1]; if (parse_ints(rest, rl, v, 1) == 1) enc = v[0]; }
                else if (kl == 6 && !memcmp(key, "DWIDTH", 6)) { int v[1]; if (parse_ints(rest, rl, v, 1) == 1) dwidth = v[0]; }
                else if (kl == 3 && !memcmp(key, "BBX", 3)) { int v[4]; if (parse_ints(rest, rl, v, 4) == 4) { memcpy(bbx, v, sizeof bbx); have_bbx = 1; } }
                else if (kl == 6 && !memcmp(key, "BITMAP", 6)) {
                    bitmap_seen = 1;
                    int *bb = have_bbx ? bbx : bbox;
                    w = bb[0]; h = bb[1];
                    if (!have_bbx && !have_bbox) bad = 1;
                    if (w < 0 || h < 0 || w > MAX_DIM || h > MAX_DIM) bad = 1;
                    if (!bad && w > 0 && h > 0) {
                        pix = pix_alloc(src, (size_t)w * (size_t)h);
                        if (!pix) { bad = 1; ok = 0; }
                    }
                    int row = 0;
                    while (next_line(b, &pos, &ls, &len)) {
                        if (is_prefix(ls, len, "ENDCHAR")) { complete = 1; break; }
                        if (pix && row < h) {
                            const char *p = skip_ws(ls, ls + len), *e = ls + len;
                            while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
                            size_t hl = (size_t)(e - p);
                            for (int x = 0; x < w; x++) {
                                size_t di = (size_t)x / 4;
                                if (di >= hl) break;
                                if (hexval(p[di]) >> (3 - x % 4) & 1) pix[(size_t)row * (size_t)w + (size_t)x] = 1;
                            }
                        }
                        row++;
                    }
                    break;
                }
            }
            (void)bitmap_seen;
            if (complete && !bad) {
                BmGlyph *g = strike_add(st);
                if (!g) { free(pix); free(name); ok = 0; break; }
                g->code = enc;
                g->name = name;
                g->w = pix ? w : 0;
                g->h = pix ? h : 0;
                g->x = have_bbx ? bbx[2] : bbox[2];
                g->y = have_bbx ? bbx[3] : bbox[3];
                g->advance = dwidth != -1000000 ? dwidth : w;
                g->pix = pix;
            } else {
                free(pix);
                free(name);
            }
        }
        if (!ok) break;
    }
    if (!ok) { props_free(&props); return fail(err, en, "BDF: out of memory or glyph data too large"); }
    if (st->n == 0) { props_free(&props); return fail(err, en, "BDF: no glyphs"); }
    int ascent = props_int(&props, "FONT_ASCENT", bbox[1] + bbox[3]);
    int descent = props_int(&props, "FONT_DESCENT", -bbox[3]);
    Codec codec = x11_codec(&props);
    x11_info(&st->info, &props, ascent, descent);
    for (int i = 0; i < st->n; i++) {
        BmGlyph *g = &st->g[i];
        int cp = g->code < 0 ? -1 : codec_map(&codec, g->code);
        g->codepoint = cp;
        if (!map_add(st, cp, i)) { props_free(&props); return fail(err, en, "out of memory"); }
    }
    props_free(&props);
    strike_finish(st);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ PCF */
#define PCF_PROPERTIES 1u
#define PCF_ACCELERATORS 2u
#define PCF_METRICS 4u
#define PCF_BITMAPS 8u
#define PCF_BDF_ENCODINGS 32u
#define PCF_GLYPH_NAMES 128u
#define PCF_BDF_ACCELERATORS 256u

static size_t cstr_len_in(const Buf *b, uint64_t o, size_t cap) {   /* length of the zero-ended string at o, at most cap */
    size_t l = 0;
    while (o + l < b->n && b->d[o + l] && l < cap) l++;
    return l;
}

static int pcf_props(const Buf *b, uint64_t off, Props *props) {
    uint32_t fmt = rd32(b, off, 0);
    int be = (fmt & 4) != 0;
    uint64_t at = off + 4;
    int count = rdi32(b, at, be);
    at += 4;
    if (count < 0 || (uint64_t)count * 9 > b->n) return 1;
    uint64_t entries = at;
    at += (uint64_t)count * 9;
    at += (uint64_t)((4 - count % 4) % 4);
    at += 4;   /* size of the strings */
    for (int i = 0; i < count; i++) {
        uint64_t e = entries + (uint64_t)i * 9;
        uint32_t name = rd32(b, e, be);
        int is_str = rdi8(b, e + 4) != 0;
        uint32_t val = rd32(b, e + 5, be);
        char num[24];
        size_t nl = cstr_len_in(b, at + name, 256);
        if (at + name >= b->n) continue;
        const char *vs;
        size_t vl;
        if (is_str) {
            if (at + val >= b->n) continue;
            vs = (const char *)b->d + at + val;
            vl = cstr_len_in(b, at + val, 1024);
        } else {
            vl = (size_t)snprintf(num, sizeof num, "%d", (int)val);
            vs = num;
        }
        if (!props_add(props, (const char *)b->d + at + name, nl, vs, vl)) return 0;
    }
    return 1;
}

static int load_pcf(BmSource *src, const Buf *b, char *err, size_t en) {
    if (b->n < 8 || memcmp(b->d, "\x01" "fcp", 4)) return fail(err, en, "not a PCF font");
    uint32_t count = rd32(b, 4, 0);
    if (count > 4096 || 8 + 16ull * count > b->n) return fail(err, en, "PCF: bad table count");
    uint64_t offs[9];
    int have[9] = {0};
    static const uint32_t kinds[9] = {PCF_PROPERTIES, PCF_ACCELERATORS, PCF_METRICS, PCF_BITMAPS, PCF_BDF_ENCODINGS,
                                      PCF_GLYPH_NAMES, PCF_BDF_ACCELERATORS, 0, 0};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t kind = rd32(b, 8 + 16ull * i, 0);
        for (int k = 0; k < 7; k++)
            if (kinds[k] == kind) { offs[k] = rd32(b, 8 + 16ull * i + 12, 0); have[k] = 1; }
    }
    if (!have[2]) return fail(err, en, "PCF font without metrics");
    if (!have[3]) return fail(err, en, "PCF font without bitmaps");
    if (!have[4]) return fail(err, en, "PCF font without encodings");
    Props props;
    memset(&props, 0, sizeof props);
    if (have[0] && !pcf_props(b, offs[0], &props)) { props_free(&props); return fail(err, en, "out of memory"); }

    /* metrics */
    uint64_t mo = offs[2];
    uint32_t mfmt = rd32(b, mo, 0);
    int mbe = (mfmt & 4) != 0, compressed = (mfmt & 0x100) != 0;
    int nmet;
    if (compressed) nmet = (int)rd16(b, mo + 4, mbe);
    else nmet = rdi32(b, mo + 4, mbe);
    uint64_t esz = compressed ? 5 : 12;
    if (nmet < 0 || (uint64_t)nmet * esz > b->n) { props_free(&props); return fail(err, en, "PCF: bad metrics count"); }
    int (*met)[5] = (int (*)[5])malloc((size_t)(nmet ? nmet : 1) * sizeof *met);
    if (!met) { props_free(&props); return fail(err, en, "out of memory"); }
    for (int i = 0; i < nmet; i++) {
        uint64_t o = mo + (compressed ? 6 : 8) + (uint64_t)i * esz;
        for (int k = 0; k < 5; k++)
            met[i][k] = compressed ? (int)rd8(b, o + (uint64_t)k) - 0x80 : rdi16(b, o + 2ull * (uint64_t)k, mbe);
    }

    int accel_have = 0, accel_asc = 0, accel_desc = 0;
    int ai = have[6] ? 6 : (have[1] ? 1 : -1);
    if (ai >= 0) {
        uint32_t afmt = rd32(b, offs[ai], 0);
        int abe = (afmt & 4) != 0;
        accel_asc = rdi32(b, offs[ai] + 12, abe);
        accel_desc = rdi32(b, offs[ai] + 16, abe);
        accel_have = 1;
    }
    int ascent, descent;
    if (props_get(&props, "FONT_ASCENT") && props_get(&props, "FONT_DESCENT")) {
        ascent = props_int(&props, "FONT_ASCENT", 0);
        descent = props_int(&props, "FONT_DESCENT", 0);
    } else if (accel_have) {
        ascent = accel_asc;
        descent = accel_desc;
    } else {
        ascent = -32768; descent = -32768;
        for (int i = 0; i < nmet; i++) {
            if (met[i][3] > ascent) ascent = met[i][3];
            if (met[i][4] > descent) descent = met[i][4];
        }
        if (nmet == 0) ascent = descent = 0;
    }

    /* bitmaps */
    uint64_t bo = offs[3];
    uint32_t bfmt = rd32(b, bo, 0);
    int bbe = (bfmt & 4) != 0, bmsb = (bfmt & 8) != 0;
    int pad = 1 << (bfmt & 3), unit = 1 << ((bfmt >> 4) & 3);
    int nbm = rdi32(b, bo + 4, bbe);
    BmStrike *st = NULL;
    unsigned char *bits = NULL;
    int ok = 0;
    if (nbm < 0 || (uint64_t)nbm * 4 > b->n) { fail(err, en, "PCF: bad bitmap count"); goto done; }
    uint64_t offs_at = bo + 8;
    uint64_t sizes_at = offs_at + (uint64_t)nbm * 4;
    uint64_t data_at = sizes_at + 16;
    uint64_t bsize = rd32(b, sizes_at + 4ull * (bfmt & 3), bbe);
    if (data_at > b->n) bsize = 0;
    else if (bsize > b->n - data_at) bsize = b->n - data_at;
    bits = (unsigned char *)malloc((size_t)bsize + 1);
    if (!bits) { fail(err, en, "out of memory"); goto done; }
    memcpy(bits, b->d + data_at, (size_t)bsize);
    if (!bmsb)
        for (size_t i = 0; i < bsize; i++) {
            unsigned v = bits[i], r = 0;
            for (int k = 0; k < 8; k++) r |= ((v >> k) & 1u) << (7 - k);
            bits[i] = (unsigned char)r;
        }
    if (bbe != bmsb && unit > 1)
        for (size_t i = 0; i < bsize; i += (size_t)unit) {
            size_t l = bsize - i < (size_t)unit ? bsize - i : (size_t)unit;
            for (size_t a = 0, z = l; a < z / 2; a++) { unsigned char t = bits[i + a]; bits[i + a] = bits[i + z - 1 - a]; bits[i + z - 1 - a] = t; }
        }

    st = src_add_strike(src);
    if (!st) { fail(err, en, "out of memory"); goto done; }
    int ng = nbm < nmet ? nbm : nmet;
    int enc_ok = 1;
    for (int i = 0; i < ng; i++) {
        BmGlyph *g = strike_add(st);
        if (!g) { fail(err, en, "out of memory"); goto done; }
        int left = met[i][0], right = met[i][1], advance = met[i][2], gasc = met[i][3], gdesc = met[i][4];
        int w = right - left, h = gasc + gdesc;
        g->x = left;
        g->advance = advance;
        g->code = -1;
        if (w > 0 && h > 0 && w <= MAX_DIM && h <= MAX_DIM) {
            int32_t start = rdi32(b, offs_at + 4ull * (uint64_t)i, bbe);
            size_t stride = ((size_t)w + 8u * (size_t)pad - 1) / (8u * (size_t)pad) * (size_t)pad;
            unsigned char *pix = pix_alloc(src, (size_t)w * (size_t)h);
            if (!pix) { fail(err, en, "PCF: glyph data too large"); goto done; }
            if (start >= 0)
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        uint64_t bi = (uint64_t)start + (uint64_t)y * stride + (uint64_t)(x >> 3);
                        if (bi < bsize && (bits[bi] >> (7 - (x & 7)) & 1)) pix[(size_t)y * (size_t)w + (size_t)x] = 1;
                    }
            g->pix = pix;
            g->w = w;
            g->h = h;
            g->y = gasc - h;
        } else {
            g->y = gasc;
        }
        g->name = NULL;
    }
    if (have[5]) {   /* glyph names */
        uint64_t no = offs[5];
        uint32_t nfmt = rd32(b, no, 0);
        int nbe = (nfmt & 4) != 0;
        int cnt = rdi32(b, no + 4, nbe);
        if (cnt >= 0 && (uint64_t)cnt * 4 <= b->n) {
            uint64_t strs = no + 8 + (uint64_t)cnt * 4 + 4;
            for (int i = 0; i < cnt && i < st->n; i++) {
                uint32_t so = rd32(b, no + 8 + 4ull * (uint64_t)i, nbe);
                if (strs + so < b->n) st->g[i].name = dup_n((const char *)b->d + strs + so, cstr_len_in(b, strs + so, 255));
            }
        }
    }
    {   /* encodings */
        uint64_t eo = offs[4];
        uint32_t efmt = rd32(b, eo, 0);
        int ebe = (efmt & 4) != 0;
        int fc = rdi16(b, eo + 4, ebe), lc = rdi16(b, eo + 6, ebe), fr = rdi16(b, eo + 8, ebe), lr = rdi16(b, eo + 10, ebe);
        long cols = (long)lc - fc + 1, rows = (long)lr - fr + 1;
        Codec codec = x11_codec(&props);
        if (cols > 0 && rows > 0 && (uint64_t)cols * (uint64_t)rows * 2 <= b->n) {
            uint64_t ix = eo + 14;
            for (long i = 0; i < cols * rows; i++) {
                unsigned idx = rd16(b, ix + 2ull * (uint64_t)i, ebe);
                if (idx == 0xFFFF || (int)idx >= st->n) continue;
                int code = (int)((fr + i / cols) << 8 | (fc + i % cols));
                int cp = codec_map(&codec, code);
                BmGlyph *g = &st->g[idx];
                if (g->code < 0) g->code = code;
                if (cp >= 0) {
                    if (g->codepoint < 0) g->codepoint = cp;
                    if (!map_add(st, cp, (int)idx)) { fail(err, en, "out of memory"); enc_ok = 0; break; }
                }
            }
        }
        if (!enc_ok) goto done;
    }
    x11_info(&st->info, &props, ascent, descent);
    strike_finish(st);
    ok = 1;
done:
    free(bits);
    free(met);
    props_free(&props);
    if (!ok && st) src_drop_last(src);
    return ok;
}

/* ------------------------------------------------------------------------------------------------ FNT / FON */
static int fnt_charset_codec(int charset, Codec *c) {
    const char *n = NULL;
    switch (charset) {
    case 0: n = "cp1252"; break;
    case 161: n = "cp1253"; break;
    case 162: n = "cp1254"; break;
    case 177: n = "cp1255"; break;
    case 178: n = "cp1256"; break;
    case 186: n = "cp1257"; break;
    case 204: n = "cp1251"; break;
    case 238: n = "cp1250"; break;
    case 255: n = "cp437"; break;
    default: return 0;
    }
    return codec_from_name(n, c);
}

/* parses one FNT (data = the resource or the file); returns 1 and appends a strike, 0 (message) if it is not a raster FNT */
static int fnt_parse(BmSource *src, const Buf *b, char *err, size_t en) {
    unsigned version = rd16(b, 0, 0);
    if (version != 0x200 && version != 0x300) return fail(err, en, "FNT: unsupported version 0x%x", version);
    if (b->n < 109) return fail(err, en, "FNT: truncated header");
    unsigned ftype = rd16(b, 66, 0);
    if (ftype & 1) return fail(err, en, "FNT: vector font");
    int points = (int)rd16(b, 68, 0);
    int ascent = (int)rd16(b, 74, 0);
    unsigned weight = rd16(b, 83, 0);
    int italic = (int)rd8(b, 80);
    int charset = (int)rd8(b, 85);
    int pix_width = (int)rd16(b, 86, 0);
    int height = (int)rd16(b, 88, 0);
    int first = (int)rd8(b, 95), last = (int)rd8(b, 96);
    uint32_t face = rd32(b, 105, 0);
    (void)points;
    if (height <= 0 || height > MAX_DIM) return fail(err, en, "FNT: bad height %d", height);
    uint64_t table = version == 0x300 ? 148 : 118;
    uint64_t step = version == 0x300 ? 6 : 4;
    int count = last - first + 1;
    if (count < 0) count = 0;
    if (table + step * (uint64_t)count > b->n) return fail(err, en, "FNT: truncated character table");
    BmStrike *st = src_add_strike(src);
    if (!st) return fail(err, en, "out of memory");
    Codec codec;
    int have_codec = fnt_charset_codec(charset, &codec);
    for (int i = 0; i < count; i++) {
        int code = first + i;
        int width = (int)rd16(b, table + step * (uint64_t)i, 0);
        uint32_t offset = step == 6 ? rd32(b, table + step * (uint64_t)i + 2, 0) : rd16(b, table + step * (uint64_t)i + 2, 0);
        if (!width) continue;
        if (width > MAX_DIM) continue;
        BmGlyph *g = strike_add(st);
        if (!g) { src_drop_last(src); return fail(err, en, "out of memory"); }
        unsigned char *pix = pix_alloc(src, (size_t)width * (size_t)height);
        if (!pix) { src_drop_last(src); return fail(err, en, "FNT: glyph data too large"); }
        for (int col = 0; col < (width + 7) / 8; col++)
            for (int y = 0; y < height; y++) {
                unsigned byte = rd8(b, (uint64_t)offset + (uint64_t)col * (uint64_t)height + (uint64_t)y);
                for (int bit = 0; bit < 8; bit++) {
                    int x = col * 8 + bit;
                    if (x < width && (byte & (0x80u >> bit))) pix[(size_t)y * (size_t)width + (size_t)x] = 1;
                }
            }
        g->pix = pix;
        g->w = width;
        g->h = height;
        g->x = 0;
        g->y = ascent - height;
        g->advance = width;
        g->code = code;
        int cp = have_codec ? codec_map(&codec, code) : -1;
        if (cp >= 0) {
            if (!map_add(st, cp, st->n - 1)) { src_drop_last(src); return fail(err, en, "out of memory"); }
            g->codepoint = cp;
        } else {
            char nm[16];
            snprintf(nm, sizeof nm, "char%02X", code);
            g->name = dup_n(nm, strlen(nm));
        }
    }
    BmStrikeInfo *in = &st->info;
    in->ascent = ascent;
    in->descent = height - ascent;
    in->pixel_size = height;
    if (pix_width) snprintf(in->label, sizeof in->label, "%dx%d", pix_width, height);
    else snprintf(in->label, sizeof in->label, "%dpx", height);
    if (face && face < b->n) {
        size_t l = cstr_len_in(b, face, sizeof in->family - 1);
        copy_str(in->family, sizeof in->family, (const char *)b->d + face, l);
        copy_str(in->name, sizeof in->name, in->family, strlen(in->family));
    }
    {
        unsigned w100 = (weight + 50) / 100 * 100;
        const char *nm = w100 == 100 ? "Thin" : w100 == 200 ? "ExtraLight" : w100 == 300 ? "Light" : w100 == 400 ? "Regular" :
                         w100 == 500 ? "Medium" : w100 == 600 ? "SemiBold" : w100 == 700 ? "Bold" : w100 == 800 ? "ExtraBold" :
                         w100 == 900 ? "Black" : NULL;
        char tmp[48];
        if (nm) snprintf(tmp, sizeof tmp, "%s", nm); else snprintf(tmp, sizeof tmp, "%u", weight);
        snprintf(in->style, sizeof in->style, "%s%s", tmp, italic ? " Italic" : "");
    }
    strike_finish(st);
    return 1;
}

/* resource tables of .fon files: collect (offset, length) of RT_FONT resources */
typedef struct { uint64_t off, len; } Res;

static int ne_fonts(const Buf *b, uint64_t ne, Res *res, int max) {
    int n = 0;
    uint64_t rsrc = ne + rd16(b, ne + 0x24, 0);
    unsigned shift = rd16(b, rsrc, 0);
    if (shift > 15) return 0;
    uint64_t pos = rsrc + 2;
    for (int guard = 0; guard < 256; guard++) {
        unsigned type_id = rd16(b, pos, 0), count = rd16(b, pos + 2, 0);
        if (type_id == 0 || pos >= b->n) break;
        pos += 8;
        for (unsigned i = 0; i < count && pos < b->n; i++) {
            uint64_t off = (uint64_t)rd16(b, pos, 0) << shift, len = (uint64_t)rd16(b, pos + 2, 0) << shift;
            if (type_id == 0x8008 && n < max) { res[n].off = off; res[n].len = len; n++; }
            pos += 12;
        }
    }
    return n;
}

static int pe_rva(const Buf *b, uint64_t sects, int nsec, uint32_t rva, uint64_t *off) {
    for (int i = 0; i < nsec; i++) {
        uint64_t s = sects + 40ull * (uint64_t)i;
        uint32_t vsize = rd32(b, s + 8, 0), vrva = rd32(b, s + 12, 0), rsize = rd32(b, s + 16, 0), rptr = rd32(b, s + 20, 0);
        uint32_t span = vsize > rsize ? vsize : rsize;
        if (rva >= vrva && rva - vrva < span) { *off = (uint64_t)rptr + (rva - vrva); return 1; }
    }
    return 0;
}

static int pe_fonts(const Buf *b, uint64_t pe, Res *res, int max) {
    int n = 0;
    uint64_t coff = pe + 4;
    int nsec = (int)rd16(b, coff + 2, 0);
    uint64_t opt = coff + 20, optsize = rd16(b, coff + 16, 0);
    unsigned magic = rd16(b, opt, 0);
    uint64_t dirs = opt + (magic == 0x20B ? 112 : 96);
    uint32_t rsrc_rva = rd32(b, dirs + 16, 0);
    uint64_t sects = opt + optsize, base;
    if (nsec > 96 || !rsrc_rva || !pe_rva(b, sects, nsec, rsrc_rva, &base)) return 0;
    /* level 0: types; level 1: names; level 2: languages; then a data entry */
    uint32_t nt = rd16(b, base + 12, 0) + rd16(b, base + 14, 0);
    for (uint32_t i = 0; i < nt && i < 256; i++) {
        uint64_t e = base + 16 + 8ull * i;
        uint32_t id = rd32(b, e, 0), sub = rd32(b, e + 4, 0);
        if ((id & 0x80000000u) || id != 8 || !(sub & 0x80000000u)) continue;
        uint64_t d1 = base + (sub & 0x7FFFFFFFu);
        uint32_t n1 = rd16(b, d1 + 12, 0) + rd16(b, d1 + 14, 0);
        for (uint32_t j = 0; j < n1 && j < 256; j++) {
            uint32_t sub1 = rd32(b, d1 + 16 + 8ull * j + 4, 0);
            if (!(sub1 & 0x80000000u)) continue;
            uint64_t d2 = base + (sub1 & 0x7FFFFFFFu);
            uint32_t n2 = rd16(b, d2 + 12, 0) + rd16(b, d2 + 14, 0);
            for (uint32_t k = 0; k < n2 && k < 16; k++) {
                uint32_t leaf = rd32(b, d2 + 16 + 8ull * k + 4, 0);
                if (leaf & 0x80000000u) continue;
                uint64_t de = base + leaf, foff;
                uint32_t rva = rd32(b, de, 0), size = rd32(b, de + 4, 0);
                if (n < max && pe_rva(b, sects, nsec, rva, &foff)) { res[n].off = foff; res[n].len = size; n++; }
            }
        }
    }
    return n;
}

static int load_fon(BmSource *src, const Buf *b, char *err, size_t en) {
    uint64_t ne = rd32(b, 0x3C, 0);
    Res res[256];
    int n;
    if (ne + 2 <= b->n && b->d[ne] == 'N' && b->d[ne + 1] == 'E') n = ne_fonts(b, ne, res, 256);
    else if (ne + 4 <= b->n && !memcmp(b->d + ne, "PE\0\0", 4)) n = pe_fonts(b, ne, res, 256);
    else return fail(err, en, "FON: neither an NE nor a PE executable");
    int made = 0;
    char e2[BM_ERR_LEN] = "";
    for (int i = 0; i < n; i++) {
        if (res[i].off >= b->n) continue;
        Buf sub = {b->d + res[i].off, (size_t)(res[i].len < b->n - res[i].off ? res[i].len : b->n - res[i].off)};
        if (fnt_parse(src, &sub, e2, sizeof e2)) made++;
    }
    if (!made) return fail(err, en, "FON: no raster font resource%s%s", e2[0] ? " (" : "", e2[0] ? e2 : "");
    return 1;
}

/* ------------------------------------------------------------------------------------------------ sbit (embedded strikes in sfnt) */
typedef struct { int gid, ord; uint64_t pos; int fmt; int have_im; int ih, iw, ibx, iby, iadv; } SbEnt;

typedef struct {
    const Buf *edt;
    SbEnt *ents;
    int nent;
    int bitdepth;      /* of the strike */
    long *work;        /* what is left to spend on rendering glyphs (pixels + 16 per glyph); composites can nest */
} SbCtx;

static int sbent_cmp(const void *a, const void *b) {
    const SbEnt *x = (const SbEnt *)a, *y = (const SbEnt *)b;
    if (x->gid != y->gid) return x->gid < y->gid ? -1 : 1;
    return x->ord < y->ord ? -1 : x->ord > y->ord;
}

static const SbEnt *sb_find(const SbCtx *c, int gid) {
    int lo = 0, hi = c->nent - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (c->ents[mid].gid == gid) return &c->ents[mid];
        if (c->ents[mid].gid < gid) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* Renders a glyph to a malloc'd w*h byte array (scaled to 0/1 for a 1-bit strike, else coverage 0..255).
 * Returns NULL with *w = 0 for a glyph that cannot be read; *w >= 0 and a NULL result with *h == 0 is an empty glyph. */
static unsigned char *sb_render(const SbCtx *c, const SbEnt *e, int level, int *pw, int *ph, int *pbx, int *pby, int *padv, int *ok) {
    const Buf *edt = c->edt;
    int fmt = e->fmt, w, h, bx, by, adv;
    uint64_t pos = e->pos, data;
    *ok = 0;
    *pw = *ph = 0;
    switch (fmt) {
    case 1: case 2: case 8: case 17:
        h = (int)rd8(edt, pos); w = (int)rd8(edt, pos + 1); bx = rdi8(edt, pos + 2); by = rdi8(edt, pos + 3); adv = (int)rd8(edt, pos + 4);
        data = pos + 5;
        break;
    case 6: case 7: case 9: case 18:
        h = (int)rd8(edt, pos); w = (int)rd8(edt, pos + 1); bx = rdi8(edt, pos + 2); by = rdi8(edt, pos + 3); adv = (int)rd8(edt, pos + 4);
        data = pos + 8;
        break;
    case 5: case 19:
        if (!e->have_im) return NULL;
        h = e->ih; w = e->iw; bx = e->ibx; by = e->iby; adv = e->iadv;
        data = pos;
        break;
    default:
        return NULL;
    }
    *pw = w; *ph = h; *pbx = bx; *pby = by; *padv = adv;
    if (pos >= edt->n) return NULL;
    long cost = 16 + (long)w * (long)h;
    if (*c->work < cost) return NULL;
    *c->work -= cost;
    if (w == 0 || h == 0) { *ok = 1; return NULL; }
    unsigned char *pix = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    if (!pix) return NULL;
    if (fmt == 8 || fmt == 9) {
        uint64_t p = data;
        if (fmt == 8) p += 1;   /* pad */
        unsigned ncomp = rd16(edt, p, 1);
        p += 2;
        if (level >= 4 || ncomp > 255) { free(pix); return NULL; }
        for (unsigned i = 0; i < ncomp; i++) {
            int gid = (int)rd16(edt, p + 4ull * i, 1), xo = rdi8(edt, p + 4ull * i + 2), yo = rdi8(edt, p + 4ull * i + 3);
            const SbEnt *ce = sb_find(c, gid);
            if (!ce) { free(pix); return NULL; }
            int sw, sh, sbx, sby, sadv, sok;
            unsigned char *sub = sb_render(c, ce, level + 1, &sw, &sh, &sbx, &sby, &sadv, &sok);
            if (!sok) { free(pix); return NULL; }
            if (sub) {
                for (int y = 0; y < sh; y++)
                    for (int x = 0; x < sw; x++) {
                        int ty = y + yo, tx = x + xo;
                        if (ty >= 0 && ty < h && tx >= 0 && tx < w && sub[(size_t)y * (size_t)sw + (size_t)x] > pix[(size_t)ty * (size_t)w + (size_t)tx])
                            pix[(size_t)ty * (size_t)w + (size_t)tx] = sub[(size_t)y * (size_t)sw + (size_t)x];
                    }
                free(sub);
            }
        }
        *ok = 1;
        return pix;
    }
    if (fmt == 17 || fmt == 18 || fmt == 19) {
        uint32_t plen = rd32(edt, data, 1);
        uint64_t start = data + 4;
        if (start > edt->n) { free(pix); return NULL; }
        if (plen > edt->n - start) plen = (uint32_t)(edt->n - start);
        int iw = 0, ih = 0, comp = 0;
        unsigned char *rgba = stbi_load_from_memory(edt->d + start, (int)plen, &iw, &ih, &comp, 4);
        if (!rgba) { free(pix); return NULL; }
        for (int y = 0; y < h && y < ih; y++)
            for (int x = 0; x < w && x < iw; x++) pix[(size_t)y * (size_t)w + (size_t)x] = rgba[((size_t)y * (size_t)iw + (size_t)x) * 4 + 3];
        stbi_image_free(rgba);
        *ok = 1;
        return pix;
    }
    int d = c->bitdepth;
    if (d != 1 && d != 2 && d != 4 && d != 8) { free(pix); return NULL; }
    int aligned = (fmt == 1 || fmt == 6);
    uint64_t row_bits = aligned ? ((uint64_t)w * (uint64_t)d + 7) / 8 * 8 : (uint64_t)w * (uint64_t)d;
    unsigned maxv = (1u << d) - 1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint64_t bit = (uint64_t)y * row_bits + (uint64_t)x * (uint64_t)d;
            uint64_t bi = data + (bit >> 3);
            unsigned v = 0;
            if (bi < edt->n) v = (edt->d[bi] >> (8 - d - (int)(bit & 7))) & maxv;
            pix[(size_t)y * (size_t)w + (size_t)x] = d == 1 ? (unsigned char)v : (unsigned char)(v * 255u / maxv);
        }
    *ok = 1;
    return pix;
}

static int sb_add_ent(SbEnt **ents, int *n, int *cap, SbEnt e) {
    if (*n >= 262144) return 1;
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 256;
        SbEnt *p = (SbEnt *)realloc(*ents, (size_t)nc * sizeof *p);
        if (!p) return 0;
        *ents = p;
        *cap = nc;
    }
    e.ord = *n;
    (*ents)[(*n)++] = e;
    return 1;
}

/* the entries (glyph id, where its image lies, its format) of the index subtables of one strike */
static int sb_entries(const Buf *loc, const Buf *edt, uint64_t sbt, SbEnt **ents, int *n) {
    int cap = 0;
    *ents = NULL;
    *n = 0;
    uint64_t arr = rd32(loc, sbt, 1);
    uint32_t nsub = rd32(loc, sbt + 8, 1);
    if (nsub > 4096) nsub = 4096;
    for (uint32_t k = 0; k < nsub; k++) {
        uint64_t a = arr + 8ull * k;
        if (a + 8 > loc->n) break;
        int first = (int)rd16(loc, a, 1), lastg = (int)rd16(loc, a + 2, 1);
        uint64_t sub = arr + rd32(loc, a + 4, 1);
        if (lastg < first || sub + 8 > loc->n) continue;
        unsigned ifmt = rd16(loc, sub, 1), imgfmt = rd16(loc, sub + 2, 1);
        uint64_t dataoff = rd32(loc, sub + 4, 1);
        int cnt = lastg - first + 1;
        SbEnt e;
        memset(&e, 0, sizeof e);
        e.fmt = (int)imgfmt;
        if (ifmt == 1 || ifmt == 3) {
            int w4 = ifmt == 1;
            uint64_t osz = w4 ? 4 : 2;
            if (sub + 8 + osz * (uint64_t)(cnt + 1) > loc->n) continue;
            for (int i = 0; i < cnt; i++) {
                uint32_t o0 = w4 ? rd32(loc, sub + 8 + 4ull * (uint64_t)i, 1) : rd16(loc, sub + 8 + 2ull * (uint64_t)i, 1);
                uint32_t o1 = w4 ? rd32(loc, sub + 12 + 4ull * (uint64_t)i, 1) : rd16(loc, sub + 10 + 2ull * (uint64_t)i, 1);
                if (o1 <= o0) continue;
                e.gid = first + i;
                e.pos = dataoff + o0;
                if (!sb_add_ent(ents, n, &cap, e)) return 0;
            }
        } else if (ifmt == 2 || ifmt == 5) {
            uint32_t isz = rd32(loc, sub + 8, 1);
            e.have_im = 1;
            e.ih = (int)rd8(loc, sub + 12); e.iw = (int)rd8(loc, sub + 13);
            e.ibx = rdi8(loc, sub + 14); e.iby = rdi8(loc, sub + 15); e.iadv = (int)rd8(loc, sub + 16);
            if (ifmt == 2) {
                for (int i = 0; i < cnt; i++) {
                    e.gid = first + i;
                    e.pos = dataoff + (uint64_t)i * isz;
                    if (!sb_add_ent(ents, n, &cap, e)) return 0;
                }
            } else {
                uint32_t ng = rd32(loc, sub + 20, 1);
                if (sub + 24 + 2ull * ng > loc->n) continue;
                for (uint32_t i = 0; i < ng; i++) {
                    e.gid = (int)rd16(loc, sub + 24 + 2ull * i, 1);
                    e.pos = dataoff + (uint64_t)i * isz;
                    if (!sb_add_ent(ents, n, &cap, e)) return 0;
                }
            }
        } else if (ifmt == 4) {
            uint32_t ng = rd32(loc, sub + 8, 1);
            if (sub + 12 + 4ull * ((uint64_t)ng + 1) > loc->n) continue;
            for (uint32_t i = 0; i < ng; i++) {
                e.gid = (int)rd16(loc, sub + 12 + 4ull * i, 1);
                e.pos = dataoff + rd16(loc, sub + 14 + 4ull * i, 1);
                if (!sb_add_ent(ents, n, &cap, e)) return 0;
            }
        }
    }
    (void)edt;
    if (*n > 1) {
        qsort(*ents, (size_t)*n, sizeof **ents, sbent_cmp);
        int o = 1;
        for (int i = 1; i < *n; i++)
            if ((*ents)[i].gid != (*ents)[o - 1].gid) (*ents)[o++] = (*ents)[i];
        *n = o;
    }
    return 1;
}

static int sfnt_open(const Buf *b) {
    uint64_t base = 0;
    uint32_t tag = rd32(b, 0, 1);
    if (tag == 0x74746366u) base = rd32(b, 12, 1);   /* ttcf: the first font */
    tag = rd32(b, base, 1);
    if (tag != 0x00010000u && tag != 0x4F54544Fu && tag != 0x74727565u && tag != 0x74797031u) return 0;
    return 1;
}

static int sfnt_table(const Buf *b, const char *tag, Buf *out) {
    uint64_t base = 0;
    if (rd32(b, 0, 1) == 0x74746366u) base = rd32(b, 12, 1);
    unsigned nt = rd16(b, base + 4, 1);
    for (unsigned i = 0; i < nt && i < 1024; i++) {
        uint64_t r = base + 12 + 16ull * i;
        if (r + 16 > b->n) break;
        if (!memcmp(b->d + r, tag, 4)) {
            uint64_t off = rd32(b, r + 8, 1), len = rd32(b, r + 12, 1);
            if (off > b->n) return 0;
            if (len > b->n - off) len = b->n - off;
            out->d = b->d + off;
            out->n = (size_t)len;
            return 1;
        }
    }
    return 0;
}

/* name table: best string of an ID as UTF-8 */
static void name_get(const Buf *nm, unsigned id, char *out, size_t cap) {
    out[0] = 0;
    unsigned count = rd16(nm, 2, 1);
    uint64_t sto = rd16(nm, 4, 1);
    int best = -1;
    uint64_t bo = 0, bl = 0;
    int bmac = 0;
    for (unsigned i = 0; i < count && i < 4096; i++) {
        uint64_t r = 6 + 12ull * i;
        if (r + 12 > nm->n) break;
        unsigned plat = rd16(nm, r, 1), lang = rd16(nm, r + 4, 1), nid = rd16(nm, r + 6, 1);
        if (nid != id) continue;
        int score = (plat == 3 && lang == 0x409) ? 4 : plat == 3 ? 3 : plat == 1 ? 2 : plat == 0 ? 1 : 0;
        if (score > best) { best = score; bl = rd16(nm, r + 8, 1); bo = sto + rd16(nm, r + 10, 1); bmac = (plat == 1); }
    }
    if (best <= 0 || bo >= nm->n) return;
    if (bl > nm->n - bo) bl = nm->n - bo;
    size_t o = 0;
    if (bmac) {
        for (uint64_t i = 0; i < bl && o + 3 < cap; i++) {
            unsigned c = nm->d[bo + i];
            if (c < 0x80) out[o++] = (char)c;
            else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        }
    } else {
        for (uint64_t i = 0; i + 1 < bl && o + 5 < cap; i += 2) {
            unsigned c = (unsigned)(nm->d[bo + i] << 8 | nm->d[bo + i + 1]);
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < bl) {
                unsigned c2 = (unsigned)(nm->d[bo + i + 2] << 8 | nm->d[bo + i + 3]);
                if (c2 >= 0xDC00 && c2 < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); i += 2; }
            }
            if (c < 0x80) out[o++] = (char)c;
            else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
            else { out[o++] = (char)(0xF0 | (c >> 18)); out[o++] = (char)(0x80 | ((c >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        }
    }
    out[o] = 0;
}

typedef struct { int cp, gid; } CmapPair;

/* the best cmap subtable (fontTools' order) as (code point, glyph id) pairs; *pairs malloc'd, may be NULL with n 0 */
static int cmap_load(const Buf *b, CmapPair **pairs, int *np) {
    Buf cm;
    *pairs = NULL;
    *np = 0;
    if (!sfnt_table(b, "cmap", &cm)) return 1;
    static const int pref[9][2] = {{3, 10}, {0, 6}, {0, 4}, {3, 1}, {0, 3}, {0, 2}, {0, 1}, {0, 0}, {1, 0}};
    unsigned nsub = rd16(&cm, 2, 1);
    if (nsub > 256) nsub = 256;
    uint64_t sub = 0;
    int found = 0, mac = 0;
    for (int p = 0; p < 9 && !found; p++)
        for (unsigned i = 0; i < nsub; i++) {
            uint64_t r = 4 + 8ull * i;
            if ((int)rd16(&cm, r, 1) == pref[p][0] && (int)rd16(&cm, r + 2, 1) == pref[p][1]) {
                uint64_t o = rd32(&cm, r + 4, 1);
                unsigned f = rd16(&cm, o, 1);
                if (f == 0 || f == 4 || f == 6 || f == 12) { sub = o; found = 1; mac = (p == 8); break; }
            }
        }
    if (!found) return 1;
    unsigned fmt = rd16(&cm, sub, 1);
    int cap = 0;
    CmapPair *out = NULL;
    int n = 0;
#define PUSH(CP, GID) do { if ((GID) != 0 && !(mac && (CP) >= 128)) { \
        if (n == cap) { int nc = cap ? cap * 2 : 512; CmapPair *q = (CmapPair *)realloc(out, (size_t)nc * sizeof *q); \
            if (!q) { free(out); return 0; } out = q; cap = nc; } \
        out[n].cp = (CP); out[n].gid = (GID); n++; } } while (0)
    if (fmt == 0) {
        for (int c = 0; c < 256; c++) { int g = (int)rd8(&cm, sub + 6 + (uint64_t)c); PUSH(c, g); }
    } else if (fmt == 6) {
        unsigned fc = rd16(&cm, sub + 6, 1), ec = rd16(&cm, sub + 8, 1);
        for (unsigned i = 0; i < ec; i++) { int g = (int)rd16(&cm, sub + 10 + 2ull * i, 1); PUSH((int)(fc + i), g); }
    } else if (fmt == 4) {
        unsigned segx2 = rd16(&cm, sub + 6, 1), seg = segx2 / 2;
        uint64_t endp = sub + 14, startp = sub + 16 + segx2, deltap = sub + 16 + 2ull * segx2, rop = sub + 16 + 3ull * segx2;
        long budget = 1 << 20;
        for (unsigned s = 0; s < seg && budget > 0; s++) {
            unsigned end = rd16(&cm, endp + 2ull * s, 1), start = rd16(&cm, startp + 2ull * s, 1);
            unsigned delta = rd16(&cm, deltap + 2ull * s, 1), ro = rd16(&cm, rop + 2ull * s, 1);
            for (unsigned c = start; c <= end && c < 0xFFFF && budget > 0; c++, budget--) {
                int g;
                if (ro == 0) g = (int)((c + delta) & 0xFFFF);
                else {
                    g = (int)rd16(&cm, rop + 2ull * s + ro + 2ull * (c - start), 1);
                    if (g) g = (int)(((unsigned)g + delta) & 0xFFFF);
                }
                PUSH((int)c, g);
            }
        }
    } else if (fmt == 12) {
        uint32_t ng = rd32(&cm, sub + 12, 1);
        long budget = 0x110000;
        for (uint32_t i = 0; i < ng && i < 65536 && budget > 0; i++) {
            uint32_t s = rd32(&cm, sub + 16 + 12ull * i, 1), e = rd32(&cm, sub + 20 + 12ull * i, 1), g0 = rd32(&cm, sub + 24 + 12ull * i, 1);
            for (uint32_t c = s; c <= e && c <= 0x10FFFF && budget > 0; c++, budget--) { PUSH((int)c, (int)(g0 + (c - s))); }
        }
    }
#undef PUSH
    *pairs = out;
    *np = n;
    return 1;
}

static int load_sbit_pair(BmSource *src, const Buf *b, const char *loc_tag, const char *dat_tag, const CmapPair *pairs, int np,
                          long *work, char *err, size_t en) {
    Buf loc, edt;
    if (!sfnt_table(b, loc_tag, &loc) || !sfnt_table(b, dat_tag, &edt)) return 1;
    uint32_t nsizes = rd32(&loc, 4, 1);
    if (nsizes > 256 || 8 + 48ull * nsizes > loc.n) return 1;
    Buf nm = {NULL, 0}, hh = {NULL, 0}, hm = {NULL, 0}, hd = {NULL, 0}, mx = {NULL, 0};
    int have_nm = sfnt_table(b, "name", &nm);
    int have_hh = sfnt_table(b, "hhea", &hh), have_hm = sfnt_table(b, "hmtx", &hm), have_hd = sfnt_table(b, "head", &hd);
    int have_mx = sfnt_table(b, "maxp", &mx);
    int upem = have_hd ? (int)rd16(&hd, 18, 1) : 0;
    char family[96] = "", style[64] = "", full[160] = "";
    if (have_nm) {
        name_get(&nm, 1, family, sizeof family);
        if (!family[0]) name_get(&nm, 16, family, sizeof family);
        name_get(&nm, 2, style, sizeof style);
        if (!style[0]) name_get(&nm, 17, style, sizeof style);
        name_get(&nm, 4, full, sizeof full);
    }
    for (uint32_t si = 0; si < nsizes; si++) {
        uint64_t sbt = 8 + 48ull * si;
        int ppx = (int)rd8(&loc, sbt + 44), ppy = (int)rd8(&loc, sbt + 45), depth = (int)rd8(&loc, sbt + 46);
        int asc = rdi8(&loc, sbt + 16), desc = rdi8(&loc, sbt + 17);
        SbEnt *ents;
        int ne;
        if (!sb_entries(&loc, &edt, sbt, &ents, &ne)) { free(ents); return fail(err, en, "out of memory"); }
        BmStrike *st = src_add_strike(src);
        if (!st) { free(ents); return fail(err, en, "out of memory"); }
        st->info.depth = depth == 1 ? 1 : 8;
        SbCtx ctx = {&edt, ents, ne, depth, work};
        int derived = (asc == 0 && desc == 0);
        int ascent = asc, descent = desc < 0 ? -desc : desc;
        if (derived && have_hh && upem > 0) {
            ascent = round_div_even((long long)rdi16(&hh, 4, 1) * ppy, upem);
            descent = round_div_even((long long)(rdi16(&hh, 6, 1) < 0 ? -rdi16(&hh, 6, 1) : rdi16(&hh, 6, 1)) * ppy, upem);
        }
        int num_hm = have_hh ? (int)rd16(&hh, 34, 1) : 0;
        int num_glyphs = have_mx ? (int)rd16(&mx, 4, 1) : 0;
        for (int i = 0; i < ne; i++) {
            int w, h, bx, by, adv, ok;
            unsigned char *pix = sb_render(&ctx, &ents[i], 0, &w, &h, &bx, &by, &adv, &ok);
            if (!ok) { free(pix); continue; }
            size_t nb = (size_t)w * (size_t)h;
            if (pix) {
                if (nb > src->budget) { free(pix); free(ents); src_drop_last(src); return fail(err, en, "sbit: glyph data too large"); }
                src->budget -= nb;
            }
            if (adv == 0 && have_hm && num_hm > 0 && upem > 0 && ents[i].gid < num_glyphs) {
                int k = ents[i].gid < num_hm ? ents[i].gid : num_hm - 1;
                adv = round_div_even((long long)rd16(&hm, 4ull * (uint64_t)k, 1) * ppx, upem);
            }
            BmGlyph *g = strike_add(st);
            if (!g) { free(pix); free(ents); src_drop_last(src); return fail(err, en, "out of memory"); }
            g->code = ents[i].gid;
            g->w = pix ? w : 0;
            g->h = pix ? h : 0;
            g->x = bx;
            g->y = by - h;
            g->advance = adv;
            g->pix = pix;
        }
        free(ents);
        /* code points: each (cp, gid) of the cmap whose glyph is in this strike; glyphs are in glyph id order */
        for (int i = 0; i < np; i++) {
            int lo = 0, hi = st->n - 1, idx = -1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (st->g[mid].code == pairs[i].gid) { idx = mid; break; }
                if (st->g[mid].code < pairs[i].gid) lo = mid + 1; else hi = mid - 1;
            }
            if (idx < 0) continue;
            if (st->g[idx].codepoint < 0 || pairs[i].cp < st->g[idx].codepoint) st->g[idx].codepoint = pairs[i].cp;
            if (!map_add(st, pairs[i].cp, idx)) { src_drop_last(src); return fail(err, en, "out of memory"); }
        }
        BmStrikeInfo *in = &st->info;
        in->ascent = ascent;
        in->descent = descent;
        in->pixel_size = ppy;
        if (ppx == ppy) snprintf(in->label, sizeof in->label, "%dpx", ppy);
        else snprintf(in->label, sizeof in->label, "%dx%dpx", ppx, ppy);
        copy_str(in->family, sizeof in->family, family, strlen(family));
        copy_str(in->style, sizeof in->style, style, strlen(style));
        copy_str(in->name, sizeof in->name, full[0] ? full : family, strlen(full[0] ? full : family));
        strike_finish(st);
    }
    return 1;
}

static int load_sbit(BmSource *src, const Buf *b, char *err, size_t en) {
    if (!sfnt_open(b)) return fail(err, en, "not an sfnt font");
    CmapPair *pairs;
    int np;
    if (!cmap_load(b, &pairs, &np)) return fail(err, en, "out of memory");
    int ok = 1;
    long work = 400000000L;
    static const char *const tags[3][2] = {{"EBLC", "EBDT"}, {"CBLC", "CBDT"}, {"bloc", "bdat"}};
    for (int i = 0; i < 3 && ok; i++) ok = load_sbit_pair(src, b, tags[i][0], tags[i][1], pairs, np, &work, err, en);
    free(pairs);
    if (!ok) return 0;
    if (src->n == 0) return fail(err, en, "no embedded bitmap strikes (EBLC/EBDT, CBLC/CBDT, bloc/bdat)");
    return 1;
}

/* ------------------------------------------------------------------------------------------------ JSON (minimal) */
enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };

typedef struct JNode {
    int type;
    double num;
    char *str;
    struct JNode *kid;   /* array of n children */
    char **key;          /* objects: n keys */
    int n;
} JNode;

typedef struct { const char *p, *e; int nodes; int err; } JParse;

static void json_free(JNode *j) {
    if (!j) return;
    free(j->str);
    for (int i = 0; i < j->n; i++) {
        json_free(&j->kid[i]);   /* frees the insides; the array itself below */
        if (j->key) free(j->key[i]);
    }
    free(j->kid);
    free(j->key);
}

static void jskip(JParse *jp) {
    while (jp->p < jp->e && (*jp->p == ' ' || *jp->p == '\t' || *jp->p == '\n' || *jp->p == '\r')) jp->p++;
}

static void put_utf8(char *out, size_t *o, unsigned c) {
    if (c < 0x80) out[(*o)++] = (char)c;
    else if (c < 0x800) { out[(*o)++] = (char)(0xC0 | (c >> 6)); out[(*o)++] = (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out[(*o)++] = (char)(0xE0 | (c >> 12)); out[(*o)++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[(*o)++] = (char)(0x80 | (c & 0x3F)); }
    else { out[(*o)++] = (char)(0xF0 | (c >> 18)); out[(*o)++] = (char)(0x80 | ((c >> 12) & 0x3F)); out[(*o)++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[(*o)++] = (char)(0x80 | (c & 0x3F)); }
}

static char *jstring(JParse *jp) {   /* at the opening quote */
    jp->p++;
    size_t cap = (size_t)(jp->e - jp->p) + 1;
    char *out = (char *)malloc(cap + 4);
    if (!out) { jp->err = 1; return NULL; }
    size_t o = 0;
    while (jp->p < jp->e && *jp->p != '"') {
        char c = *jp->p++;
        if (c == '\\' && jp->p < jp->e) {
            c = *jp->p++;
            switch (c) {
            case 'n': out[o++] = '\n'; break;
            case 't': out[o++] = '\t'; break;
            case 'r': out[o++] = '\r'; break;
            case 'b': out[o++] = '\b'; break;
            case 'f': out[o++] = '\f'; break;
            case 'u': {
                unsigned v = 0;
                for (int i = 0; i < 4 && jp->p < jp->e; i++) v = v << 4 | (unsigned)hexval(*jp->p++);
                if (v >= 0xD800 && v < 0xDC00 && jp->e - jp->p >= 6 && jp->p[0] == '\\' && jp->p[1] == 'u') {
                    unsigned v2 = 0;
                    for (int i = 0; i < 4; i++) v2 = v2 << 4 | (unsigned)hexval(jp->p[2 + i]);
                    if (v2 >= 0xDC00 && v2 < 0xE000) { v = 0x10000 + ((v - 0xD800) << 10) + (v2 - 0xDC00); jp->p += 6; }
                }
                put_utf8(out, &o, v);
                break;
            }
            default: out[o++] = c; break;
            }
        } else out[o++] = c;
        if (o + 5 > cap + 4) break;
    }
    if (jp->p >= jp->e) { jp->err = 1; free(out); return NULL; }
    jp->p++;   /* closing quote */
    out[o] = 0;
    return out;
}

static int jvalue(JParse *jp, JNode *out, int depth);

static int jvalue(JParse *jp, JNode *out, int depth) {
    memset(out, 0, sizeof *out);
    if (depth > 24 || ++jp->nodes > 200000) { jp->err = 1; return 0; }
    jskip(jp);
    if (jp->p >= jp->e) { jp->err = 1; return 0; }
    char c = *jp->p;
    if (c == '"') {
        out->type = J_STR;
        out->str = jstring(jp);
        return out->str != NULL;
    }
    if (c == '{' || c == '[') {
        int obj = c == '{';
        char close = obj ? '}' : ']';
        out->type = obj ? J_OBJ : J_ARR;
        jp->p++;
        int cap = 0;
        jskip(jp);
        if (jp->p < jp->e && *jp->p == close) { jp->p++; return 1; }
        for (;;) {
            if (out->n == cap) {
                int nc = cap ? cap * 2 : 4;
                JNode *k = (JNode *)realloc(out->kid, (size_t)nc * sizeof *k);
                if (!k) { jp->err = 1; return 0; }
                out->kid = k;
                if (obj) {
                    char **ks = (char **)realloc(out->key, (size_t)nc * sizeof *ks);
                    if (!ks) { jp->err = 1; return 0; }
                    out->key = ks;
                }
                cap = nc;
            }
            char *key = NULL;
            jskip(jp);
            if (obj) {
                if (jp->p >= jp->e || *jp->p != '"') { jp->err = 1; return 0; }
                key = jstring(jp);
                if (!key) return 0;
                jskip(jp);
                if (jp->p >= jp->e || *jp->p != ':') { free(key); jp->err = 1; return 0; }
                jp->p++;
            }
            JNode *kid = &out->kid[out->n];
            if (!jvalue(jp, kid, depth + 1)) { free(key); json_free(kid); return 0; }
            if (obj) out->key[out->n] = key;
            out->n++;
            jskip(jp);
            if (jp->p < jp->e && *jp->p == ',') { jp->p++; continue; }
            if (jp->p < jp->e && *jp->p == close) { jp->p++; return 1; }
            jp->err = 1;
            return 0;
        }
    }
    if (jp->e - jp->p >= 4 && !strncmp(jp->p, "true", 4)) { out->type = J_BOOL; out->num = 1; jp->p += 4; return 1; }
    if (jp->e - jp->p >= 5 && !strncmp(jp->p, "false", 5)) { out->type = J_BOOL; jp->p += 5; return 1; }
    if (jp->e - jp->p >= 4 && !strncmp(jp->p, "null", 4)) { out->type = J_NULL; jp->p += 4; return 1; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char tmp[64];
        size_t l = 0;
        while (jp->p + l < jp->e && l + 1 < sizeof tmp && strchr("+-0123456789.eE", jp->p[l])) { tmp[l] = jp->p[l]; l++; }
        tmp[l] = 0;
        out->type = J_NUM;
        out->num = strtod(tmp, NULL);
        jp->p += l;
        return 1;
    }
    jp->err = 1;
    return 0;
}

static const JNode *jget(const JNode *o, const char *key) {
    if (!o || o->type != J_OBJ) return NULL;
    for (int i = o->n - 1; i >= 0; i--)
        if (!strcmp(o->key[i], key)) return &o->kid[i];
    return NULL;
}

static int jint(const JNode *o, const char *key, int def) {
    const JNode *n = jget(o, key);
    if (!n || n->type != J_NUM) return def;
    if (n->num > 1e9) return 1000000000;
    if (n->num < -1e9) return -1000000000;
    return (int)n->num;
}

static const char *jstr(const JNode *o, const char *key) {
    const JNode *n = jget(o, key);
    return n && n->type == J_STR ? n->str : NULL;
}

/* ------------------------------------------------------------------------------------------------ sheets */
static const unsigned CONTROL_GRAPHIC[32] = {
    0, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC};

typedef struct { int unicode, graphic; Codec c; } SheetCp;

static int sheet_cp(const char *name, SheetCp *sc) {
    memset(sc, 0, sizeof *sc);
    if (!strcmp(name, "unicode")) { sc->unicode = 1; return 1; }
    size_t l = strlen(name);
    char base[64];
    if (l > 8 && !strcmp(name + l - 8, "-graphic")) { sc->graphic = 1; l -= 8; }
    copy_str(base, sizeof base, name, l);
    return codec_from_name(base, &sc->c);
}

static int sheet_unicode(const SheetCp *sc, int code) {
    if (sc->unicode) return code;
    if (sc->graphic) {
        if (code == 0) return -1;
        if (code >= 1 && code < 32) return (int)CONTROL_GRAPHIC[code];
        if (code == 0x7F) return 0x2302;
    }
    return codec_map(&sc->c, code);
}

static int read_file(const char *path, unsigned char **out, size_t *n, char *err, size_t en) {
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, en, "cannot open %s", path);
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return fail(err, en, "cannot read %s", path); }
    long sz = ftell(f);
    if (sz < 0 || sz > (long)(512u << 20)) { fclose(f); return fail(err, en, "%s: bad size", path); }
    fseek(f, 0, SEEK_SET);
    unsigned char *d = (unsigned char *)malloc((size_t)sz + 1);
    if (!d) { fclose(f); return fail(err, en, "out of memory"); }
    size_t got = fread(d, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(d); return fail(err, en, "cannot read %s", path); }
    *out = d;
    *n = got;
    return 1;
}

static int load_sheet(BmSource *src, const Buf *b, const char *path, char *err, size_t en) {
    JParse jp = {(const char *)b->d, (const char *)b->d + b->n, 0, 0};
    JNode root;
    if (b->n >= 3 && !memcmp(b->d, "\xEF\xBB\xBF", 3)) jp.p += 3;
    if (!jvalue(&jp, &root, 0)) { json_free(&root); return fail(err, en, "JSON: syntax error or out of memory"); }
    int rc = 0;
    unsigned char *file = NULL, *rgba = NULL;
    size_t fn = 0;
    BmStrike *st = NULL;
    const JNode *cell = jget(&root, "cell");
    if (root.type != J_OBJ || !cell) { fail(err, en, "not a sheet descriptor (no \"cell\")"); goto done; }
    const char *source = jstr(&root, "source");
    if (!source) { fail(err, en, "sheet: no \"source\""); goto done; }
    int cw = jint(cell, "width", 0), ch = jint(cell, "height", 0);
    if (cw < 1 || ch < 1 || cw > 256 || ch > 256) { fail(err, en, "sheet: bad cell size %dx%d", cw, ch); goto done; }

    char spath[1024];
    int absolute = source[0] == '/' || source[0] == '\\' || (source[0] && source[1] == ':');
    if (absolute || !path) copy_str(spath, sizeof spath, source, strlen(source));
    else {
        size_t dl = strlen(path);
        while (dl > 0 && path[dl - 1] != '/' && path[dl - 1] != '\\') dl--;
        if (dl + strlen(source) + 1 > sizeof spath) { fail(err, en, "sheet: path too long"); goto done; }
        memcpy(spath, path, dl);
        strcpy(spath + dl, source);
    }
    if (!read_file(spath, &file, &fn, err, en)) goto done;

    const char *fmts = jstr(&root, "format");
    int fmt = 0;   /* 0 image, 1 raw, 2 records */
    if (fmts) {
        if (!strcmp(fmts, "image")) fmt = 0;
        else if (!strcmp(fmts, "raw")) fmt = 1;
        else if (!strcmp(fmts, "records")) fmt = 2;
        else { fail(err, en, "unknown sheet format '%s'", fmts); goto done; }
    }
    const char *cps = jstr(&root, "codepage");
    SheetCp scp;
    if (!sheet_cp(cps ? cps : "unicode", &scp)) { fail(err, en, "unknown codepage '%s'", cps ? cps : ""); goto done; }
    int first = jint(&root, "first", 0);
    const JNode *nin = jget(&root, "ninth_column");
    int ninth = 0, n_first = 0, n_last = 0;
    if (nin && nin->type == J_ARR && nin->n >= 2 && nin->kid[0].type == J_NUM && nin->kid[1].type == J_NUM) {
        ninth = 1;
        n_first = (int)nin->kid[0].num;
        n_last = (int)nin->kid[1].num;
        if (cw != 8) { fail(err, en, "\"ninth_column\" needs cells 8 wide, not %d", cw); goto done; }
    }

    st = src_add_strike(src);
    if (!st) { fail(err, en, "out of memory"); goto done; }

    /* the cells */
    size_t ncells = 0;
    int iw = 0, ih = 0, columns = 0;
    int ink_dark = 1;
    unsigned ink_rgba = 0, dummy_rgba = 0;
    int have_dummy = 0;
    size_t rowbytes = (size_t)(cw + 7) / 8, gsize = rowbytes * (size_t)ch;
    int record_size = jint(&root, "record_size", 0);
    if (fmt == 0) {
        const char *ink = jstr(&root, "ink");
        ink_dark = !ink || strcmp(ink, "light") != 0;
        const JNode *dc = jget(&root, "dummy_color");
        if (dc && dc->type == J_ARR && dc->n >= 3) {
            dummy_rgba = (unsigned)((int)dc->kid[0].num & 255) | (unsigned)((int)dc->kid[1].num & 255) << 8 | (unsigned)((int)dc->kid[2].num & 255) << 16 | 0xFF000000u;
            have_dummy = 1;
        }
        int comp;
        rgba = stbi_load_from_memory(file, (int)fn, &iw, &ih, &comp, 4);
        if (!rgba) { fail(err, en, "sheet image: %s", stbi_failure_reason()); goto done; }
        columns = jint(&root, "columns", 0);
        if (columns <= 0) columns = iw / cw;
        if (columns <= 0 || columns > 65536) { fail(err, en, "sheet: no columns"); goto done; }
        ncells = (size_t)columns * (size_t)(ih / ch);
        /* the colors: transparent (alpha 0), the dummy colour, and 1 or 2 solid ones */
        unsigned solid[2];
        int nsolid = 0, nbg = 0, bad = 0;
        for (size_t i = 0; i < (size_t)iw * (size_t)ih && !bad; i++) {
            const unsigned char *p = rgba + i * 4;
            unsigned v = (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
            if (p[3] == 0) { nbg = 1; continue; }
            if (have_dummy && v == dummy_rgba) continue;
            int known = 0;
            for (int k = 0; k < nsolid; k++) if (solid[k] == v) known = 1;
            if (known) continue;
            if (nsolid == 2) { bad = 1; break; }
            solid[nsolid++] = v;
        }
        if (nsolid == 2) {   /* sort by luminance, dark first */
            unsigned a = solid[0], bb = solid[1];
            long la = (long)(a & 255) * 299 + (long)(a >> 8 & 255) * 587 + (long)(a >> 16 & 255) * 114;
            long lb = (long)(bb & 255) * 299 + (long)(bb >> 8 & 255) * 587 + (long)(bb >> 16 & 255) * 114;
            if (lb < la) { solid[0] = bb; solid[1] = a; }
        }
        if (bad || nsolid != (nbg ? 1 : 2)) { fail(err, en, "sheet image is not black-and-white"); goto done; }
        if (nsolid == 0) ink_rgba = 0;
        else ink_rgba = (nbg || ink_dark || nsolid < 2) ? solid[0] : solid[1];
    } else if (fmt == 1) {
        if (fn % gsize) { fail(err, en, "file size %zu is not a multiple of the glyph size %zu", fn, gsize); goto done; }
        ncells = fn / gsize;
    } else {
        if (record_size < 2 || (size_t)record_size > fn || fn % (size_t)record_size) { fail(err, en, "file size is not a multiple of the record size"); goto done; }
        ncells = fn / (size_t)record_size;
    }
    if (ncells > 1048576) { fail(err, en, "sheet: too many cells"); goto done; }

    for (size_t i = 0; i < ncells; i++) {
        int code = first + (int)i;
        int cp = sheet_unicode(&scp, code);
        int w = cw;
        unsigned char *pix = NULL;
        if (fmt == 2) {
            const unsigned char *rec = file + i * (size_t)record_size;
            w = rec[0];
            if (w > cw || 1 + (size_t)w * (size_t)ch > (size_t)record_size) { fail(err, en, "record at %zu is %d wide, which does not fit", i * (size_t)record_size, w); goto done; }
        }
        if (cp < 0) continue;
        int gw = w + (ninth ? 1 : 0);
        if (w > 0) {
            pix = pix_alloc(src, (size_t)gw * (size_t)ch);
            if (!pix) { fail(err, en, "sheet: too large"); goto done; }
            for (int y = 0; y < ch; y++) {
                for (int x = 0; x < w; x++) {
                    int on = 0;
                    if (fmt == 0) {
                        size_t col = i % (size_t)columns, row = i / (size_t)columns;
                        size_t ix = col * (size_t)cw + (size_t)x, iy = row * (size_t)ch + (size_t)y;
                        if (ix < (size_t)iw && iy < (size_t)ih) {
                            const unsigned char *p = rgba + (iy * (size_t)iw + ix) * 4;
                            unsigned v = (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
                            on = p[3] != 0 && (v == ink_rgba || (have_dummy && v == dummy_rgba));
                        }
                    } else if (fmt == 1) {
                        on = (file[i * gsize + (size_t)y * rowbytes + (size_t)(x >> 3)] >> (7 - (x & 7))) & 1;
                    } else {
                        on = file[i * (size_t)record_size + 1 + (size_t)y * (size_t)w + (size_t)x] != 0;
                    }
                    pix[(size_t)y * (size_t)gw + (size_t)x] = (unsigned char)on;
                }
                if (ninth) pix[(size_t)y * (size_t)gw + 8] = (unsigned char)(pix[(size_t)y * (size_t)gw + 7] && code >= n_first && code <= n_last);
            }
        }
        BmGlyph *g = strike_add(st);
        if (!g) { free(pix); fail(err, en, "out of memory"); goto done; }
        g->code = code;
        g->codepoint = cp;
        g->pix = pix;
        g->w = pix ? gw : 0;
        g->h = pix ? ch : 0;
        g->advance = pix ? gw : 0;
        if (fmt == 2) {
            g->advance += jint(&root, "advance_gap", 0);
            if (cp == 0x20) g->advance = jint(&root, "space_advance", g->advance);
        }
        if (!map_add(st, cp, st->n - 1)) { fail(err, en, "out of memory"); goto done; }
    }

    /* the baseline */
    int baseline;
    const JNode *bn = jget(&root, "baseline");
    if (bn && bn->type == J_NUM) baseline = (int)bn->num;
    else {
        int count[257] = {0}, order[257], nord = 0, seen[257] = {0};
        for (int i = 0; i < st->n; i++) {
            const BmGlyph *g = &st->g[i];
            if (g->codepoint < 'A' || g->codepoint > 'Z' || !g->pix) continue;
            int last = -1;
            for (int y = 0; y < g->h; y++)
                for (int x = 0; x < g->w; x++)
                    if (g->pix[(size_t)y * (size_t)g->w + (size_t)x]) { last = y; break; }
            if (last >= 0) { if (!seen[last]) { seen[last] = 1; order[nord++] = last; } count[last]++; }
        }
        if (!nord) { fail(err, en, "no A-Z glyph to measure the baseline on; give \"baseline\""); goto done; }
        int best = order[0];
        for (int k = 1; k < nord; k++) if (count[order[k]] > count[best]) best = order[k];
        baseline = best;
    }
    int ascent = baseline + 1, descent = ch - baseline - 1;
    for (int i = 0; i < st->n; i++) {
        BmGlyph *g = &st->g[i];
        g->x = 0;
        g->y = g->h ? ascent - g->h : ascent;   /* the cell's top is the top of the font box */
    }
    {
        BmStrikeInfo *in = &st->info;
        const JNode *meta = jget(&root, "meta");
        const char *fam = jstr(meta, "family"), *sty = jstr(meta, "style");
        if (fam) copy_str(in->family, sizeof in->family, fam, strlen(fam));
        if (sty) copy_str(in->style, sizeof in->style, sty, strlen(sty));
        copy_str(in->name, sizeof in->name, in->family, strlen(in->family));
        in->ascent = ascent;
        in->descent = descent;
        in->pixel_size = ch;
        if (fmt == 2) snprintf(in->label, sizeof in->label, "x%d", ch);
        else snprintf(in->label, sizeof in->label, "%dx%d", cw + (ninth ? 1 : 0), ch);
    }
    strike_finish(st);
    rc = 1;
done:
    if (!rc && st) src_drop_last(src);
    if (rgba) stbi_image_free(rgba);
    free(file);
    json_free(&root);
    return rc;
}

/* ------------------------------------------------------------------------------------------------ open */
static int looks_json(const Buf *b) {
    size_t i = 0;
    if (b->n >= 3 && !memcmp(b->d, "\xEF\xBB\xBF", 3)) i = 3;
    while (i < b->n && (b->d[i] == ' ' || b->d[i] == '\t' || b->d[i] == '\n' || b->d[i] == '\r')) i++;
    return i < b->n && b->d[i] == '{';
}

static BmSource *open_buf(const Buf *b, const char *path, char *err, size_t en) {
    BmSource *src = src_new();
    if (!src) { fail(err, en, "out of memory"); return NULL; }
    int ok;
    uint32_t tag = rd32(b, 0, 1);
    if (b->n >= 4 && !memcmp(b->d, "\x01" "fcp", 4)) ok = load_pcf(src, b, err, en);
    else if (b->n >= 9 && !memcmp(b->d, "STARTFONT", 9)) ok = load_bdf(src, b, err, en);
    else if (looks_json(b)) {
        if (!path) { fail(err, en, "a JSON sheet descriptor needs a file path (bm_open)"); ok = 0; }
        else ok = load_sheet(src, b, path, err, en);
    }
    else if (b->n >= 2 && b->d[0] == 'M' && b->d[1] == 'Z') ok = load_fon(src, b, err, en);
    else if (tag == 0x00010000u || tag == 0x4F54544Fu || tag == 0x74727565u || tag == 0x74746366u || tag == 0x74797031u) ok = load_sbit(src, b, err, en);
    else if (b->n >= 2 && (rd16(b, 0, 0) == 0x200 || rd16(b, 0, 0) == 0x300)) ok = fnt_parse(src, b, err, en);
    else ok = fail(err, en, "unknown bitmap font format");
    if (!ok) { bm_close(src); return NULL; }
    if (src->n == 0) { bm_close(src); fail(err, en, "no strikes"); return NULL; }
    return src;
}

BmSource *bm_open_mem(const unsigned char *data, size_t n, char *err, size_t en) {
    Buf b = {data, n};
    if (err && en) err[0] = 0;
    if (!data) { fail(err, en, "no data"); return NULL; }
    return open_buf(&b, NULL, err, en);
}

BmSource *bm_open(const char *path, char *err, size_t en) {
    unsigned char *d;
    size_t n;
    if (err && en) err[0] = 0;
    if (!path) { fail(err, en, "no path"); return NULL; }
    if (!read_file(path, &d, &n, err, en)) return NULL;
    Buf b = {d, n};
    BmSource *src = open_buf(&b, path, err, en);
    free(d);
    return src;
}

/* ------------------------------------------------------------------------------------------------ access */
int bm_strike_count(const BmSource *src) { return src ? src->n : 0; }

int bm_strike_info(const BmSource *src, int strike, BmStrikeInfo *out) {
    if (!src || !out || strike < 0 || strike >= src->n) return 0;
    *out = src->s[strike].info;
    out->glyph_count = src->s[strike].n;
    return 1;
}

int bm_glyph_count(const BmSource *src, int strike) {
    return (src && strike >= 0 && strike < src->n) ? src->s[strike].n : 0;
}

const BmGlyph *bm_glyph_at(const BmSource *src, int strike, int index) {
    if (!src || strike < 0 || strike >= src->n || index < 0 || index >= src->s[strike].n) return NULL;
    return &src->s[strike].g[index];
}

const BmGlyph *bm_find(const BmSource *src, int strike, int codepoint) {
    if (!src || strike < 0 || strike >= src->n || codepoint < 0) return NULL;
    const BmStrike *s = &src->s[strike];
    int lo = 0, hi = s->nmap - 1, at = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s->map[mid].cp >= codepoint) { if (s->map[mid].cp == codepoint) at = mid; hi = mid - 1; }
        else lo = mid + 1;
    }
    return at < 0 ? NULL : &s->g[s->map[at].idx];
}

int bm_glyph_to_plain(const BmGlyph *g, BmPlain *out) {
    if (!g || !out) return 0;
    memset(out, 0, sizeof *out);
    out->codepoint = g->codepoint;
    out->x = g->x;
    out->y = g->y;
    out->advance = g->advance;
    out->w = g->w;
    out->h = g->h;
    if (g->w > 0 && g->h > 0 && g->pix) {
        static const char levels[] = ".0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        size_t n = (size_t)g->w * (size_t)g->h;
        out->pix = (char *)malloc(n);
        if (!out->pix) { out->w = out->h = 0; return 0; }
        for (size_t i = 0; i < n; i++) {
            unsigned v = g->pix[i];
            out->pix[i] = g->depth == 1 ? (v ? 'Z' : '.') : levels[(v * 36u + 127u) / 255u];
        }
    } else {
        out->w = out->h = 0;
    }
    return 1;
}

void bm_plain_free(BmPlain *p) {
    if (!p) return;
    free(p->pix);
    memset(p, 0, sizeof *p);
}
