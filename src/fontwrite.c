/* fontwrite.c - BDF, PCF, FNT and FON writers and what every writer shares.  See fontwrite.h.
 * Follows bitmapfont/tools/writers/{bdf,pcf,fnt,common}.py. */
#include "fontwrite.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmsrc_cp.inc"

static bool fail(char err[PX_ERR_LEN], const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, PX_ERR_LEN, fmt, ap);
    va_end(ap);
    return false;
}

/* ------------------------------------------------------------------ shared */

static const char *const kExt[FW_COUNT] = {"bdf", "pcf", "fnt", "fon", "ttf", "otf", "woff"};
static const char *const kLabel[FW_COUNT] = {"BDF", "PCF", "FNT", "FON", "TrueType", "OpenType", "WOFF"};

const char *fw_ext(FwFormat f) { return f >= 0 && f < FW_COUNT ? kExt[f] : ""; }
const char *fw_label(FwFormat f) { return f >= 0 && f < FW_COUNT ? kLabel[f] : ""; }

bool fw_format_of_ext(const char *ext, FwFormat *out)
{
    if (!ext) return false;
    if (*ext == '.') ext++;
    for (int i = 0; i < FW_COUNT; i++) {
        const char *a = kExt[i], *b = ext;
        while (*a && *b && (*b | 0x20) == *a) a++, b++;
        if (!*a && !*b) { *out = (FwFormat)i; return true; }
    }
    return false;
}

void fw_buf_add(FwBuf *b, const void *data, size_t n)
{
    if (b->failed || !n) return;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n) cap *= 2;
        unsigned char *p = realloc(b->p, cap);
        if (!p) { b->failed = true; return; }
        b->p = p;
        b->cap = cap;
    }
    if (data) memcpy(b->p + b->n, data, n); else memset(b->p + b->n, 0, n);
    b->n += n;
}
void fw_buf_zero(FwBuf *b, size_t n) { fw_buf_add(b, NULL, n); }
void fw_buf_free(FwBuf *b) { free(b->p); memset(b, 0, sizeof *b); }
void fw_be16(FwBuf *b, unsigned v) { unsigned char t[2] = {(unsigned char)(v >> 8), (unsigned char)v}; fw_buf_add(b, t, 2); }
void fw_be32(FwBuf *b, unsigned long v) { unsigned char t[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v}; fw_buf_add(b, t, 4); }
void fw_le16(FwBuf *b, unsigned v) { unsigned char t[2] = {(unsigned char)v, (unsigned char)(v >> 8)}; fw_buf_add(b, t, 2); }
void fw_le32(FwBuf *b, unsigned long v) { unsigned char t[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; fw_buf_add(b, t, 4); }

void fw_warn(char *warn, size_t warnCap, const char *fmt, ...)
{
    if (!warn || !warnCap) return;
    size_t l = strlen(warn);
    if (l + 2 >= warnCap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(warn + l, warnCap - l - 1, fmt, ap);
    va_end(ap);
    l = strlen(warn);
    warn[l] = '\n';
    warn[l + 1] = 0;
}

bool fw_glyphs(const PxFont *f, bool withDefault, FwGlyphs *out)
{
    out->n = 0;
    out->g = malloc(sizeof *out->g * (size_t)(f->count + 1));
    if (!out->g) return false;
    for (int i = 0; i < f->count; i++) {
        const PxGlyph *g = &f->glyphs[i];
        if (px_code_variant(g->code)) continue;
        if (px_code_is_default(g->code) && !withDefault) continue;
        out->g[out->n++] = g;
    }
    return true;
}
void fw_glyphs_free(FwGlyphs *g) { free(g->g); g->g = NULL; g->n = 0; }

void fw_ascii_name(const char *utf8, char *out, size_t cap, bool keepSpaces)
{
    size_t o = 0;
    if (!cap) return;
    for (const char *s = utf8 ? utf8 : ""; *s && o + 1 < cap; s++) {
        char c = *s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') out[o++] = c;
        else if (keepSpaces && c == ' ' && o && out[o - 1] != ' ') out[o++] = ' ';
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

static const char *nz(const char *s) { return s && *s ? s : NULL; }

/* Latin-1 of UTF-8 text with runs of white space made one space; '?' for what Latin-1 lacks.  Returns true when nothing was lost. */
static bool latin1(const char *utf8, char *out, size_t cap)
{
    bool ok = true, space = false;
    size_t o = 0;
    const unsigned char *s = (const unsigned char *)(utf8 ? utf8 : "");
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    while (*s && o + 1 < cap) {
        unsigned c = *s++;
        if (c >= 0x80) {
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            unsigned cp = c & (0x3F >> more);
            for (int i = 0; i < more && *s; i++) cp = cp << 6 | (*s++ & 0x3F);
            c = cp;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = true; continue; }
        if (space) { out[o++] = ' '; space = false; if (o + 1 >= cap) break; }
        if (c > 0xFF) { c = '?'; ok = false; }
        out[o++] = (char)c;
    }
    out[o] = 0;
    return ok;
}

/* The box of the font, checked: false when a bitmap is not loaded. */
static bool font_box(const PxFont *f, int *ascent, int *descent, char err[PX_ERR_LEN])
{
    if (!px_font_box(f, ascent, descent)) return fail(err, "the font's bitmaps are not loaded");
    return true;
}

/* ------------------------------------------------------------------ BDF */

static void put_line(FwBuf *b, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof line) n = (int)sizeof line - 1;
    fw_buf_add(b, line, (size_t)n);
    fw_buf_add(b, "\n", 1);
}

/* The X11 properties of BDF and PCF that come from text (bdf.py PROPERTY_KEYS order). */
typedef struct X11Text { const char *key; const char *value; } X11Text;
static int x11_texts(const FwMeta *m, X11Text out[8])
{
    const X11Text all[] = {{"COPYRIGHT", m->copyright}, {"NOTICE", m->description}, {"FAMILY_NAME", m->family},
                           {"WEIGHT_NAME", m->style}, {"FOUNDRY", m->vendor}, {"FACE_NAME", m->fullName}, {"FONT_VERSION", m->version}};
    int n = 0;
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) if (nz(all[i].value)) out[n++] = all[i];
    return n;
}

static const char *glyph_name(const PxGlyph *g, int i, char buf[24])
{
    int32_t cp = px_code_codepoint(g->code);
    if (cp >= 0) snprintf(buf, 24, "U%04X", (unsigned)cp); else snprintf(buf, 24, "glyph%d", i);
    return buf;
}

static unsigned char *bdf_bytes(const FwFace *face, const char *stem, size_t *len, char *warn, size_t wc, char err[PX_ERR_LEN])
{
    const PxFont *f = face->font;
    int ascent, descent;
    if (!font_box(f, &ascent, &descent, err)) return NULL;
    int height = ascent + descent;
    FwGlyphs gl;
    if (!fw_glyphs(f, false, &gl)) { fail(err, "out of memory"); return NULL; }
    bool inked = false;
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    for (int i = 0; i < gl.n; i++) {
        const PxGlyph *g = gl.g[i];
        if (!g->w || !g->h) continue;
        if (!inked || g->x < x0) x0 = g->x;
        if (!inked || g->x + g->w > x1) x1 = g->x + g->w;
        if (!inked || g->y + g->h > y1) y1 = g->y + g->h;
        if (!inked || g->y < y0) y0 = g->y;
        inked = true;
    }
    bool lossless = true;
    char t[1024], q[2100];
    FwBuf b = {0};
    X11Text tx[8];
    int ntx = x11_texts(&face->meta, tx);
    int nprops = ntx + 2 + (f->has[PXM_XHEIGHT] && f->m[PXM_XHEIGHT]) + (f->has[PXM_CAPHEIGHT] && f->m[PXM_CAPHEIGHT]) + 2;
    lossless &= latin1(stem, t, sizeof t);
    put_line(&b, "STARTFONT 2.1");
    put_line(&b, "FONT %s", t);
    put_line(&b, "SIZE %d 75 75", height);
    put_line(&b, "FONTBOUNDINGBOX %d %d %d %d", x1 - x0, y1 - y0, x0, y0);
    put_line(&b, "STARTPROPERTIES %d", nprops);
    for (int i = 0; i < ntx; i++) {
        lossless &= latin1(tx[i].value, t, sizeof t);
        size_t o = 0;
        q[o++] = '"';
        for (const char *s = t; *s && o + 3 < sizeof q; s++) { if (*s == '"') q[o++] = '"'; q[o++] = *s; }
        q[o++] = '"';
        q[o] = 0;
        put_line(&b, "%s %s", tx[i].key, q);
    }
    put_line(&b, "FONT_ASCENT %d", ascent);
    put_line(&b, "FONT_DESCENT %d", descent);
    if (f->has[PXM_XHEIGHT] && f->m[PXM_XHEIGHT]) put_line(&b, "X_HEIGHT %d", f->m[PXM_XHEIGHT]);
    if (f->has[PXM_CAPHEIGHT] && f->m[PXM_CAPHEIGHT]) put_line(&b, "CAP_HEIGHT %d", f->m[PXM_CAPHEIGHT]);
    put_line(&b, "CHARSET_REGISTRY \"%s\"", f->symbolic ? "Misc" : "ISO10646");
    put_line(&b, "CHARSET_ENCODING \"%s\"", f->symbolic ? "FontSpecific" : "1");
    put_line(&b, "ENDPROPERTIES");
    put_line(&b, "CHARS %d", gl.n);
    for (int i = 0; i < gl.n; i++) {
        const PxGlyph *g = gl.g[i];
        char nm[24];
        int32_t cp = px_code_codepoint(g->code);
        put_line(&b, "STARTCHAR %s", glyph_name(g, i, nm));
        put_line(&b, "ENCODING %ld", (long)cp);
        put_line(&b, "SWIDTH %d 0", height ? (int)((double)g->advance * 960.0 / height + (g->advance >= 0 ? 0.5 : -0.5)) : 0);
        put_line(&b, "DWIDTH %d 0", g->advance);
        put_line(&b, "BBX %d %d %d %d", g->w, g->h, g->w && g->h ? g->x : 0, g->w && g->h ? g->y : 0);
        put_line(&b, "BITMAP");
        int digits = (g->w + 7) / 8 * 2;
        for (int r = 0; r < g->h; r++) {
            char row[1100];
            int o = 0;
            for (int d = 0; d < digits && o + 2 < (int)sizeof row; d++) {
                unsigned v = 0;
                for (int k = 0; k < 4; k++) { int c = d * 4 + k; v = v << 1 | (c < g->w && fw_ink(g, c, r)); }
                row[o++] = "0123456789ABCDEF"[v];
            }
            row[o] = 0;
            put_line(&b, "%s", row);
        }
        put_line(&b, "ENDCHAR");
    }
    put_line(&b, "ENDFONT");
    fw_glyphs_free(&gl);
    if (b.failed) { fw_buf_free(&b); fail(err, "out of memory"); return NULL; }
    if (!lossless) fw_warn(warn, wc, "metadata characters outside Latin-1 replaced with '?'");
    *len = b.n;
    return b.p;
}

/* ------------------------------------------------------------------ PCF */

enum { PCF_PROPERTIES = 1 << 0, PCF_ACCELERATORS = 1 << 1, PCF_METRICS = 1 << 2, PCF_BITMAPS = 1 << 3, PCF_BDF_ENCODINGS = 1 << 5,
       PCF_SWIDTHS = 1 << 6, PCF_GLYPH_NAMES = 1 << 7, PCF_BDF_ACCELERATORS = 1 << 8 };
#define PCF_FORMAT (1u << 2 | 1u << 3 | 2u)     /* MSByte and MSBit first, rows padded to 4 bytes */
#define PCF_ACCEL_INK (PCF_FORMAT | 0x100u)
#define PCF_RES 75

typedef struct Metric { int l, r, adv, asc, desc; } Metric;

static void pcf_metric(FwBuf *b, Metric m)
{
    fw_be16(b, (unsigned)(int16_t)m.l); fw_be16(b, (unsigned)(int16_t)m.r); fw_be16(b, (unsigned)(int16_t)m.adv);
    fw_be16(b, (unsigned)(int16_t)m.asc); fw_be16(b, (unsigned)(int16_t)m.desc); fw_be16(b, 0);
}

/* the strings of the properties table */
typedef struct Strs { FwBuf b; } Strs;
static unsigned long str_place(Strs *s, const char *text)
{
    for (size_t at = 0; at < s->b.n; at += strlen((const char *)s->b.p + at) + 1)
        if (!strcmp((const char *)s->b.p + at, text)) return (unsigned long)at;
    unsigned long at = (unsigned long)s->b.n;
    fw_buf_add(&s->b, text, strlen(text) + 1);
    return at;
}

static void pcf_properties(FwBuf *out, const FwFace *face, const char *stem, int ascent, int descent, bool *lossless)
{
    const PxFont *f = face->font;
    struct { const char *key; const char *s; long v; } e[24];
    int n = 0;
    X11Text tx[8];
    int ntx = x11_texts(&face->meta, tx);
    for (int i = 0; i < ntx; i++) { e[n].key = tx[i].key; e[n].s = tx[i].value; n++; }
    int height = ascent + descent;
    e[n].key = "FONT"; e[n].s = stem; n++;
    e[n].key = "FONT_ASCENT"; e[n].s = NULL; e[n].v = ascent; n++;
    e[n].key = "FONT_DESCENT"; e[n].s = NULL; e[n].v = descent; n++;
    e[n].key = "PIXEL_SIZE"; e[n].s = NULL; e[n].v = height; n++;
    e[n].key = "POINT_SIZE"; e[n].s = NULL; e[n].v = (long)((double)height * 720.0 / PCF_RES + 0.5); n++;
    e[n].key = "RESOLUTION_X"; e[n].s = NULL; e[n].v = PCF_RES; n++;
    e[n].key = "RESOLUTION_Y"; e[n].s = NULL; e[n].v = PCF_RES; n++;
    e[n].key = "CHARSET_REGISTRY"; e[n].s = f->symbolic ? "Misc" : "ISO10646"; n++;
    e[n].key = "CHARSET_ENCODING"; e[n].s = f->symbolic ? "FontSpecific" : "1"; n++;
    if (f->has[PXM_XHEIGHT] && f->m[PXM_XHEIGHT]) { e[n].key = "X_HEIGHT"; e[n].s = NULL; e[n].v = f->m[PXM_XHEIGHT]; n++; }
    if (f->has[PXM_CAPHEIGHT] && f->m[PXM_CAPHEIGHT]) { e[n].key = "CAP_HEIGHT"; e[n].s = NULL; e[n].v = f->m[PXM_CAPHEIGHT]; n++; }
    Strs s = {{0}};
    fw_be32(out, (unsigned long)n);
    for (int i = 0; i < n; i++) {
        char t[1024];
        fw_be32(out, str_place(&s, e[i].key));
        fw_buf_add(out, e[i].s ? "\1" : "\0", 1);
        if (e[i].s) { *lossless &= latin1(e[i].s, t, sizeof t); fw_be32(out, str_place(&s, t)); }
        else fw_be32(out, (unsigned long)e[i].v);
    }
    fw_buf_zero(out, (size_t)((4 - n % 4) % 4));
    fw_be32(out, (unsigned long)s.b.n);
    fw_buf_add(out, s.b.p, s.b.n);
    if (s.b.failed) out->failed = true;
    fw_buf_free(&s.b);
}

static void pcf_accelerators(FwBuf *out, const Metric *m, int n, int ascent, int descent)
{
    Metric lo = {0}, hi = {0};
    bool oneWidth = true;
    for (int i = 0; i < n; i++) {
        const int *a = &m[i].l;
        int *l = &lo.l, *h = &hi.l;
        for (int k = 0; k < 5; k++) {
            if (!i || a[k] < l[k]) l[k] = a[k];
            if (!i || a[k] > h[k]) h[k] = a[k];
        }
        if (m[i].adv != m[0].adv) oneWidth = false;
    }
    unsigned char head[8] = {1, 0, 0, (unsigned char)oneWidth, 0, 0, 0, 0};
    fw_buf_add(out, head, 8);
    fw_be32(out, (unsigned long)ascent);
    fw_be32(out, (unsigned long)descent);
    fw_be32(out, 0);
    pcf_metric(out, lo); pcf_metric(out, hi); pcf_metric(out, lo); pcf_metric(out, hi);
}

static unsigned char *pcf_bytes(const FwFace *face, const char *stem, size_t *len, char *warn, size_t wc, char err[PX_ERR_LEN])
{
    const PxFont *f = face->font;
    int ascent, descent;
    if (!font_box(f, &ascent, &descent, err)) return NULL;
    int height = ascent + descent;
    FwGlyphs all, gl;
    if (!fw_glyphs(f, false, &all)) { fail(err, "out of memory"); return NULL; }
    gl.g = malloc(sizeof *gl.g * (size_t)(all.n + 1));
    gl.n = 0;
    Metric *m = malloc(sizeof *m * (size_t)(all.n + 1));
    if (!gl.g || !m) { free(gl.g); free(m); fw_glyphs_free(&all); fail(err, "out of memory"); return NULL; }
    int left = 0;
    char lost[400] = "";
    for (int i = 0; i < all.n; i++) {
        int32_t cp = px_code_codepoint(all.g[i]->code);
        if (cp > 0xFFFF) {
            size_t l = strlen(lost);
            if (l + 12 < sizeof lost) snprintf(lost + l, sizeof lost - l, " U+%04X", (unsigned)cp);
            left++;
            continue;
        }
        gl.g[gl.n++] = all.g[i];
    }
    fw_glyphs_free(&all);
    if (left) fw_warn(warn, wc, "PCF holds 16-bit codes only, %d left out:%s%s", left, lost, strlen(lost) > sizeof lost - 14 ? " ..." : "");
    if (gl.n >= 0xFFFF) { free(gl.g); free(m); fail(err, "too many glyphs for a PCF: %d", gl.n); return NULL; }
    for (int i = 0; i < gl.n; i++) {
        const PxGlyph *g = gl.g[i];
        bool ink = g->w && g->h;
        int asc = ink ? g->y + g->h : 0;
        m[i] = (Metric){ink ? g->x : 0, ink ? g->x + g->w : 0, g->advance, asc, (ink ? g->h : 0) - asc};
    }
    bool lossless = true;
    FwBuf t[8] = {{0}};
    unsigned kind[8] = {PCF_PROPERTIES, PCF_ACCELERATORS, PCF_METRICS, PCF_BITMAPS, PCF_BDF_ENCODINGS, PCF_SWIDTHS, PCF_GLYPH_NAMES, PCF_BDF_ACCELERATORS};
    unsigned fmt[8] = {PCF_FORMAT, PCF_ACCEL_INK, PCF_FORMAT, PCF_FORMAT, PCF_FORMAT, PCF_FORMAT, PCF_FORMAT, PCF_ACCEL_INK};
    for (int k = 0; k < 8; k++) fw_le32(&t[k], fmt[k]);
    pcf_properties(&t[0], face, stem, ascent, descent, &lossless);
    pcf_accelerators(&t[1], m, gl.n, ascent, descent);
    fw_be32(&t[2], (unsigned long)gl.n);
    for (int i = 0; i < gl.n; i++) pcf_metric(&t[2], m[i]);
    {   /* bitmaps: offsets, the four sizes, the rows */
        FwBuf data = {0};
        unsigned long sizes[4] = {0};
        fw_be32(&t[3], (unsigned long)gl.n);
        for (int i = 0; i < gl.n; i++) {
            const PxGlyph *g = gl.g[i];
            fw_be32(&t[3], (unsigned long)data.n);
            int stride = (g->w + 31) / 32 * 4;
            for (int r = 0; r < g->h; r++)
                for (int byte = 0; byte < stride; byte++) {
                    unsigned v = 0;
                    for (int bit = 0; bit < 8; bit++) { int c = byte * 8 + bit; if (c < g->w && fw_ink(g, c, r)) v |= 0x80u >> bit; }
                    unsigned char cv = (unsigned char)v;
                    fw_buf_add(&data, &cv, 1);
                }
            if (g->w && g->h)
                for (int p = 0; p < 4; p++) { int pad = 1 << p; sizes[p] += (unsigned long)(g->h * ((g->w + 8 * pad - 1) / (8 * pad)) * pad); }
        }
        for (int p = 0; p < 4; p++) fw_be32(&t[3], sizes[p]);
        fw_buf_add(&t[3], data.p, data.n);
        if (data.failed) t[3].failed = true;
        fw_buf_free(&data);
    }
    {   /* encodings: two-byte codes by row */
        int firstRow = 255, lastRow = 0;
        for (int i = 0; i < gl.n; i++) {
            int32_t cp = px_code_codepoint(gl.g[i]->code);
            if (cp < 0) continue;
            if (cp >> 8 < firstRow) firstRow = cp >> 8;
            if (cp >> 8 > lastRow) lastRow = cp >> 8;
        }
        if (firstRow > lastRow) firstRow = lastRow = 0;
        int cells = 256 * (lastRow - firstRow + 1);
        uint16_t *idx = malloc(sizeof *idx * (size_t)cells);
        if (!idx) t[4].failed = true;
        else {
            for (int i = 0; i < cells; i++) idx[i] = 0xFFFF;
            for (int i = 0; i < gl.n; i++) {
                int32_t cp = px_code_codepoint(gl.g[i]->code);
                if (cp >= 0) idx[((cp >> 8) - firstRow) * 256 + (cp & 0xFF)] = (uint16_t)i;
            }
            fw_be16(&t[4], 0); fw_be16(&t[4], 255); fw_be16(&t[4], (unsigned)firstRow); fw_be16(&t[4], (unsigned)lastRow); fw_be16(&t[4], 0xFFFF);
            for (int i = 0; i < cells; i++) fw_be16(&t[4], idx[i]);
            free(idx);
        }
    }
    fw_be32(&t[5], (unsigned long)gl.n);
    for (int i = 0; i < gl.n; i++) fw_be32(&t[5], (unsigned long)(height ? (long)((double)m[i].adv * 960.0 / height + (m[i].adv >= 0 ? 0.5 : -0.5)) : 0));
    {
        FwBuf s = {0};
        fw_be32(&t[6], (unsigned long)gl.n);
        for (int i = 0; i < gl.n; i++) {
            char nm[24];
            fw_be32(&t[6], (unsigned long)s.n);
            glyph_name(gl.g[i], i, nm);
            fw_buf_add(&s, nm, strlen(nm) + 1);
        }
        fw_be32(&t[6], (unsigned long)s.n);
        fw_buf_add(&t[6], s.p, s.n);
        if (s.failed) t[6].failed = true;
        fw_buf_free(&s);
    }
    pcf_accelerators(&t[7], m, gl.n, ascent, descent);
    free(m);
    free(gl.g);
    FwBuf out = {0};
    fw_buf_add(&out, "\1fcp", 4);
    fw_le32(&out, 8);
    unsigned long at = 8 + 16 * 8;
    bool failed = false;
    for (int k = 0; k < 8; k++) {
        fw_buf_zero(&t[k], (size_t)(-(long)t[k].n & 3));
        failed |= t[k].failed;
        fw_le32(&out, kind[k]); fw_le32(&out, fmt[k]); fw_le32(&out, (unsigned long)t[k].n); fw_le32(&out, at);
        at += (unsigned long)t[k].n;
    }
    for (int k = 0; k < 8; k++) { fw_buf_add(&out, t[k].p, t[k].n); fw_buf_free(&t[k]); }
    if (failed || out.failed) { fw_buf_free(&out); fail(err, "out of memory"); return NULL; }
    if (!lossless) fw_warn(warn, wc, "metadata characters outside Latin-1 replaced with '?'");
    *len = out.n;
    return out.p;
}

/* ------------------------------------------------------------------ FNT and FON */

static const struct { int charset; const char *cp; } kCharsets[] = {
    {0, "cp1252"}, {161, "cp1253"}, {162, "cp1254"}, {177, "cp1255"}, {178, "cp1256"}, {186, "cp1257"}, {204, "cp1251"},
    {238, "cp1250"}, {255, "cp437"}};
#define NCHARSETS ((int)(sizeof kCharsets / sizeof kCharsets[0]))

static const uint16_t *cp_table(const char *name)
{
    for (size_t i = 0; i < sizeof BM_CODEPAGES / sizeof BM_CODEPAGES[0]; i++) if (!strcmp(BM_CODEPAGES[i].name, name)) return BM_CODEPAGES[i].t;
    return NULL;
}

/* the byte of a code point in a code page (bytes below 0x80 are ASCII), or -1 */
static int cp_byte(const uint16_t *t, int32_t cp)
{
    if (cp < 0) return -1;
    if (cp < 0x80) return cp;
    for (int i = 0; i < 128; i++) if (t[i] == cp) return 0x80 + i;
    return -1;
}

/* the code page a CODEPAGE text names, if FNT has it: "cp1250", "windows-1250", "1250", "CP437" */
static int charset_of_text(const char *text)
{
    if (!nz(text)) return -1;
    char n[48];
    size_t o = 0;
    for (const char *s = text; *s && o + 1 < sizeof n; s++) {
        char c = *s;
        if (c == '-' || c == '_' || c == ' ') continue;
        n[o++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    n[o] = 0;
    const char *digits = !strncmp(n, "windows", 7) ? n + 7 : !strncmp(n, "cp", 2) ? n + 2 : !strncmp(n, "ibm", 3) ? n + 3 : n;
    for (int i = 0; i < NCHARSETS; i++) if (!strcmp(kCharsets[i].cp + 2, digits)) return i;
    return -1;
}

typedef struct Fnt { FwBuf b; char face[32]; } Fnt;

static bool fnt_build(const FwFace *face, const char *stem, Fnt *out, char *warn, size_t wc, char err[PX_ERR_LEN])
{
    const PxFont *f = face->font;
    int ascent, descent;
    if (!font_box(f, &ascent, &descent, err)) return false;
    int height = ascent + descent;
    if (height <= 0) return fail(err, "the font has no height");
    FwGlyphs gl;
    if (!fw_glyphs(f, false, &gl)) return fail(err, "out of memory");
    /* the character set: SYMBOL for a symbolic font, the CODEPAGE when FNT has it, else the one covering most glyphs */
    int cs = -1, charset;
    const uint16_t *table = NULL;
    if (f->symbolic) charset = 2;
    else {
        cs = charset_of_text(face->meta.codepage);
        if (cs < 0) {
            int best = -1;
            for (int i = 0; i < NCHARSETS; i++) {
                const uint16_t *t = cp_table(kCharsets[i].cp);
                int cover = 0;
                for (int k = 0; t && k < gl.n; k++) cover += cp_byte(t, px_code_codepoint(gl.g[k]->code)) >= 0;
                if (cover > best) { best = cover; cs = i; }
            }
        }
        charset = kCharsets[cs].charset;
        table = cp_table(kCharsets[cs].cp);
        if (!table) { fw_glyphs_free(&gl); return fail(err, "code page %s missing", kCharsets[cs].cp); }
    }
    const PxGlyph *cell[256] = {0};
    int unenc = 0, unfit = 0, symbols = 0;
    for (int i = 0; i < gl.n; i++) {
        const PxGlyph *g = gl.g[i];
        int32_t cp = px_code_codepoint(g->code);
        if (cp < 0) { symbols++; continue; }
        int code = table ? cp_byte(table, cp) : cp < 256 ? cp : -1;
        if (code < 0) { unenc++; continue; }
        bool ink = g->w && g->h;
        if (g->advance <= 0 || (ink && (g->x < 0 || g->x + g->w > g->advance || g->y < -descent || g->y + g->h > ascent))) { unfit++; continue; }
        if (!cell[code]) cell[code] = g;
    }
    fw_glyphs_free(&gl);
    if (unenc) fw_warn(warn, wc, "%d glyph(s) left out, not on code page %s", unenc, f->symbolic ? "symbol" : kCharsets[cs].cp);
    if (unfit) fw_warn(warn, wc, "%d glyph(s) left out, not fitting their advance and box", unfit);
    if (symbols) fw_warn(warn, wc, "%d symbol(s) left out, FNT has no glyphs without a code", symbols);
    int first = -1, last = -1;
    for (int c = 0; c < 256; c++) if (cell[c]) { if (first < 0) first = c; last = c; }
    if (first < 0) return fail(err, "no glyph can be stored in a FNT");
    long sumW = 0, nW = 0, maxW = 0, w0 = -1;
    bool mono = true;
    size_t bitmapSize = 0;
    for (int c = first; c <= last; c++) if (cell[c]) {
        int w = cell[c]->advance;
        sumW += w; nW++;
        if (w > maxW) maxW = w;
        if (w0 < 0) w0 = w; else if (w != w0) mono = false;
        bitmapSize += (size_t)((w + 7) / 8 * height);
    }
    int average = (int)((double)sumW / (double)nW + 0.5);
    size_t sentinel = (size_t)((average + 7) / 8 * height);
    int count = last - first + 2;
    int version = 0x200, hsize = 118, esize = 4;
    if ((size_t)hsize + (size_t)count * 4 + bitmapSize + sentinel > 0xFFFF) version = 0x300, hsize = 148, esize = 6;
    size_t offset = (size_t)hsize + (size_t)count * (size_t)esize;
    FwBuf ct = {0}, bm = {0};
    for (int c = first; c <= last + 1; c++) {
        const PxGlyph *g = c <= last ? cell[c] : NULL;
        int w = c > last ? average : g ? g->advance : 0;
        fw_le16(&ct, (unsigned)w);
        if (esize == 6) fw_le32(&ct, (unsigned long)(offset + bm.n)); else fw_le16(&ct, (unsigned)(offset + bm.n));
        if (c > last) { fw_buf_zero(&bm, sentinel); break; }
        if (!g) continue;
        for (int col = 0; col < (w + 7) / 8; col++)
            for (int row = 0; row < height; row++) {
                unsigned v = 0;
                int gr = row - (ascent - (g->y + g->h));   /* the glyph's row on this cell row */
                for (int bit = 0; bit < 8; bit++) {
                    int x = col * 8 + bit - g->x;
                    if (x >= 0 && x < g->w && gr >= 0 && gr < g->h && fw_ink(g, x, gr)) v |= 0x80u >> bit;
                }
                unsigned char cv = (unsigned char)v;
                fw_buf_add(&bm, &cv, 1);
            }
    }
    const char *style = nz(face->meta.style) ? face->meta.style : "";
    static const struct { const char *name; int w; } kW[] = {{"thin", 100}, {"extralight", 200}, {"light", 300}, {"regular", 400},
        {"medium", 500}, {"semibold", 600}, {"bold", 700}, {"extrabold", 800}, {"black", 900}};
    int weight = 400;
    for (size_t i = 0; i < sizeof kW / sizeof kW[0]; i++) {
        size_t l = strlen(kW[i].name);
        bool pre = strlen(style) >= l;
        for (size_t k = 0; pre && k < l; k++) pre = (style[k] | 0x20) == kW[i].name[k];
        if (pre) { weight = kW[i].w; break; }
    }
    bool italic = face->meta.italic;
    for (const char *s = style; !italic && *s; s++) {
        static const char *it = "italic";
        size_t k = 0;
        while (it[k] && s[k] && (s[k] | 0x20) == it[k]) k++;
        if (!it[k]) italic = true;
    }
    char copy[64] = "", fam[256];
    latin1(face->meta.copyright, copy, sizeof copy);   /* 60 bytes, at most 59 of text */
    copy[59] = 0;
    latin1(nz(face->meta.family) ? face->meta.family : stem, fam, sizeof fam);
    snprintf(out->face, sizeof out->face, "%.31s", fam);
    size_t faceOff = offset + bm.n;
    size_t total = faceOff + strlen(out->face) + 1;
    int points = (int)((double)height * 72.0 / 96.0 + 0.5);
    if (points < 1) points = 1;
    FwBuf *h = &out->b;
    fw_le16(h, (unsigned)version); fw_le32(h, (unsigned long)total);
    { char cbuf[60] = {0}; memcpy(cbuf, copy, strlen(copy)); fw_buf_add(h, cbuf, 60); }
    fw_le16(h, 0);                      /* dfType: raster */
    fw_le16(h, (unsigned)points); fw_le16(h, 96); fw_le16(h, 96);
    fw_le16(h, (unsigned)ascent); fw_le16(h, 0); fw_le16(h, 0);
    unsigned char fl[3] = {italic, face->meta.underlined, face->meta.struckout};
    fw_buf_add(h, fl, 3);
    fw_le16(h, (unsigned)weight);
    { unsigned char c = (unsigned char)charset; fw_buf_add(h, &c, 1); }
    fw_le16(h, mono ? (unsigned)w0 : 0); fw_le16(h, (unsigned)height);
    { unsigned char c = mono ? 0 : 1; fw_buf_add(h, &c, 1); }   /* dfPitchAndFamily: variable pitch */
    fw_le16(h, (unsigned)average); fw_le16(h, (unsigned)maxW);
    int dflt = '?' >= first && '?' <= last && cell['?'] ? '?' - first : 0;
    int brk = 0x20 >= first && 0x20 <= last ? 0x20 - first : 0;
    unsigned char cc[4] = {(unsigned char)first, (unsigned char)last, (unsigned char)dflt, (unsigned char)brk};
    fw_buf_add(h, cc, 4);
    fw_le16(h, (unsigned)((maxW + 7) / 8 * 2));
    fw_le32(h, 0); fw_le32(h, (unsigned long)faceOff); fw_le32(h, 0); fw_le32(h, (unsigned long)offset);
    fw_buf_zero(h, 1);                  /* dfReserved */
    if (version == 0x300) {
        fw_le32(h, (mono ? 0x1u : 0x2u) | 0x10u);
        fw_le16(h, 0); fw_le16(h, 0); fw_le16(h, 0); fw_le32(h, 0); fw_buf_zero(h, 16);
    }
    fw_buf_add(h, ct.p, ct.n);
    fw_buf_add(h, bm.p, bm.n);
    fw_buf_add(h, out->face, strlen(out->face) + 1);
    bool failed = ct.failed || bm.failed || h->failed;
    fw_buf_free(&ct);
    fw_buf_free(&bm);
    if (failed) { fw_buf_free(h); return fail(err, "out of memory"); }
    return true;
}

/* A minimal NE executable holding the FNTs as RT_FONT resources 1..n and their RT_FONTDIR (fnt.py build_fon, for n fonts). */
static unsigned char *fon_bytes(Fnt *fnt, int n, size_t *len, char err[PX_ERR_LEN])
{
    const char *face = fnt[0].face;
    char module[9] = "";
    int ml = 0;
    for (const char *s = face; *s && ml < 8; s++) {
        char c = *s;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) module[ml++] = c;
        else if (c >= 'a' && c <= 'z') module[ml++] = (char)(c - 32);
    }
    if (!ml) strcpy(module, "FONT"), ml = 4;
    module[ml] = 0;
    char desc[256];
    snprintf(desc, sizeof desc, "FONTRES 100,96,96 : %s", face);
    size_t dl = strlen(desc);
    FwBuf fontdir = {0};
    fw_le16(&fontdir, (unsigned)n);
    for (int i = 0; i < n; i++) {
        fw_le16(&fontdir, (unsigned)(i + 1));
        fw_buf_add(&fontdir, fnt[i].b.p, 0x71);
        fw_buf_zero(&fontdir, 1);                               /* no device name */
        fw_buf_add(&fontdir, fnt[i].face, strlen(fnt[i].face) + 1);
    }
    FwBuf resident = {0}, nonres = {0};
    { unsigned char c = (unsigned char)ml; fw_buf_add(&resident, &c, 1); fw_buf_add(&resident, module, (size_t)ml); fw_le16(&resident, 0); fw_buf_zero(&resident, 1); }
    { unsigned char c = (unsigned char)dl; fw_buf_add(&nonres, &c, 1); fw_buf_add(&nonres, desc, dl); fw_le16(&nonres, 0); fw_buf_zero(&nonres, 1); }
    const size_t neOff = 0x40, neSize = 0x40, importedLen = 1, entryLen = 2;
    const int shift = 4, align = 1 << shift;
    size_t rsrcSize = 2 + (8 + 12) + (8 + 12 * (size_t)n) + 2 + 1;
    size_t rsrcOff = neSize, residentOff = rsrcOff + rsrcSize, importedOff = residentOff + resident.n, entryOff = importedOff + importedLen;
    size_t nonresOff = neOff + entryOff + entryLen, dataStart = nonresOff + nonres.n;
    size_t pad = (size_t)(-(long)dataStart & (align - 1));
    size_t dirStart = dataStart + pad, dirBlocks = (fontdir.n + (size_t)align - 1) / (size_t)align;
    FwBuf o = {0};
    /* MZ */
    fw_buf_add(&o, "MZ", 2);
    fw_le16(&o, 0x40); fw_le16(&o, 1); fw_le16(&o, 0); fw_le16(&o, 4); fw_le16(&o, 0); fw_le16(&o, 0xFFFF); fw_le16(&o, 0);
    fw_buf_zero(&o, 0x18 - o.n);
    fw_le16(&o, 0x40);
    fw_buf_zero(&o, 0x3C - o.n);
    fw_le32(&o, (unsigned long)neOff);
    /* NE header */
    size_t ne = o.n;
    fw_buf_zero(&o, neSize);
    unsigned char *p = o.failed ? NULL : o.p + ne;
#define PUT16(at, v) do { if (p) { p[(at)] = (unsigned char)(v); p[(at) + 1] = (unsigned char)((v) >> 8); } } while (0)
    if (p) { p[0] = 'N'; p[1] = 'E'; p[2] = 5; p[3] = 10; }
    PUT16(4, entryOff); PUT16(6, entryLen); PUT16(0x0C, 0x8308); PUT16(0x20, nonres.n);
    PUT16(0x22, rsrcOff); PUT16(0x24, rsrcOff); PUT16(0x26, residentOff); PUT16(0x28, importedOff); PUT16(0x2A, importedOff);
    PUT16(0x2C, nonresOff & 0xFFFF); PUT16(0x2E, nonresOff >> 16); PUT16(0x32, shift);
    if (p) { p[0x36] = 2; p[0x37] = 0x08; }
    PUT16(0x3E, 0x030A);
#undef PUT16
    /* resource table */
    size_t fontStart = dirStart + dirBlocks * (size_t)align;
    fw_le16(&o, (unsigned)shift);
    fw_le16(&o, 0x8007); fw_le16(&o, 1); fw_le32(&o, 0);
    fw_le16(&o, (unsigned)(dirStart >> shift)); fw_le16(&o, (unsigned)dirBlocks); fw_le16(&o, 0x0C50); fw_le16(&o, 0x8000 | 100); fw_le16(&o, 0); fw_le16(&o, 0);
    fw_le16(&o, 0x8008); fw_le16(&o, (unsigned)n); fw_le32(&o, 0);
    size_t at = fontStart;
    for (int i = 0; i < n; i++) {
        size_t blocks = (fnt[i].b.n + (size_t)align - 1) / (size_t)align;
        fw_le16(&o, (unsigned)(at >> shift)); fw_le16(&o, (unsigned)blocks); fw_le16(&o, 0x1C30); fw_le16(&o, 0x8000 | (unsigned)(i + 1)); fw_le16(&o, 0); fw_le16(&o, 0);
        at += blocks * (size_t)align;
    }
    fw_le16(&o, 0); fw_buf_zero(&o, 1);
    fw_buf_add(&o, resident.p, resident.n);
    fw_buf_zero(&o, importedLen);
    fw_buf_zero(&o, entryLen);
    fw_buf_add(&o, nonres.p, nonres.n);
    fw_buf_zero(&o, pad);
    fw_buf_add(&o, fontdir.p, fontdir.n);
    fw_buf_zero(&o, dirBlocks * (size_t)align - fontdir.n);
    for (int i = 0; i < n; i++) {
        size_t blocks = (fnt[i].b.n + (size_t)align - 1) / (size_t)align;
        fw_buf_add(&o, fnt[i].b.p, fnt[i].b.n);
        fw_buf_zero(&o, blocks * (size_t)align - fnt[i].b.n);
    }
    bool failed = o.failed || fontdir.failed || resident.failed || nonres.failed || at >> shift > 0xFFFF;
    fw_buf_free(&fontdir); fw_buf_free(&resident); fw_buf_free(&nonres);
    if (failed) { fw_buf_free(&o); fail(err, "the FON could not be built (too large?)"); return NULL; }
    *len = o.n;
    return o.p;
}

/* ------------------------------------------------------------------ entry points */

unsigned char *fw_bytes(FwFormat fmt, const FwFace *faces, int n, const char *stem, size_t *len, char *warn, size_t warnCap,
                        char err[PX_ERR_LEN])
{
    err[0] = 0;
    if (n < 1 || (n > 1 && fmt != FW_FON)) { fail(err, "a %s file holds one font", fw_label(fmt)); return NULL; }
    for (int i = 0; i < n; i++) if (!fw_takes_colour(fmt) && px_font_is_colour(faces[i].font)) { fail(err, "%s holds fonts of one colour only", fw_label(fmt)); return NULL; }
    switch (fmt) {
    case FW_BDF: return bdf_bytes(faces, stem, len, warn, warnCap, err);
    case FW_PCF: return pcf_bytes(faces, stem, len, warn, warnCap, err);
    case FW_TTF: case FW_OTF: case FW_WOFF: return fw_sfnt_bytes(fmt, faces, stem, len, warn, warnCap, err);
    case FW_FNT: case FW_FON: {
        Fnt *f = calloc((size_t)n, sizeof *f);
        if (!f) { fail(err, "out of memory"); return NULL; }
        unsigned char *out = NULL;
        int built = 0;
        for (; built < n; built++) {
            char w2[1024] = "";
            if (!fnt_build(&faces[built], stem, &f[built], w2, sizeof w2, err)) break;
            if (w2[0]) {   /* each face's warnings, named when there are several */
                for (char *s = w2, *e; *s; s = e + 1) {
                    e = strchr(s, '\n');
                    if (!e) break;
                    *e = 0;
                    if (n > 1) fw_warn(warn, warnCap, "%s: %s", nz(faces[built].meta.style) ? faces[built].meta.style : f[built].face, s);
                    else fw_warn(warn, warnCap, "%s", s);
                }
            }
        }
        if (built == n) {
            if (fmt == FW_FNT) { out = f[0].b.p; *len = f[0].b.n; f[0].b.p = NULL; }
            else out = fon_bytes(f, n, len, err);
        }
        for (int i = 0; i < n; i++) fw_buf_free(&f[i].b);
        free(f);
        return out;
    }
    default: fail(err, "unknown format"); return NULL;
    }
}

bool fw_write(FwFormat fmt, const FwFace *faces, int n, const char *path, char *warn, size_t warnCap, char err[PX_ERR_LEN])
{
    const char *base = path;
    for (const char *s = path; *s; s++) if (*s == '/' || *s == '\\') base = s + 1;
    char stem[256];
    snprintf(stem, sizeof stem, "%s", base);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) *dot = 0;
    size_t len = 0;
    unsigned char *data = fw_bytes(fmt, faces, n, stem, &len, warn, warnCap, err);
    if (!data) return false;
    bool ok = px_write_file(path, data, len);
    free(data);
    if (!ok) return fail(err, "cannot write %s", path);
    return true;
}
