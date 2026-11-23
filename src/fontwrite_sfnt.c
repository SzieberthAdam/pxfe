/* fontwrite_sfnt.c - the TrueType, OpenType (CFF) and WOFF writer of fontwrite.h.
 * A C port of bitmapfont/tools/writers/ttf.py, without fontTools.
 *
 * OUTLINES.  The ink of every glyph is traced to closed outlines along the pixel edges (ttf.py `contours`): 64 units a pixel,
 * 1024 units per em, the pen on the baseline at (0, 0).  TrueType outers run clockwise (the ink on the right), CFF ones
 * counter-clockwise (each contour reversed, as ttf.py's `_draw(reverse=True)`).  The walk takes the least edge start (x, then y)
 * first and drops collinear points; where two pixels touch only at a corner it turns right, and a walk that passes a corner
 * twice is cut there, so every contour is a simple polygon (ttf.py may give one contour touching itself; the same area).
 *
 * GLYPHS.  Glyph 0 is .notdef: the default glyph (I+DEFA00) when the font has one, else an empty glyph of advance 0.  The others
 * follow in font order: a code point is "uniXXXX" (the AGL's "uXXXXX" above FFFF), a symbol (X+n) "glyphNNNNN" with its glyph id
 * and no character mapping.
 *
 * TABLES.  head, hhea, maxp, OS/2 (version 4), hmtx, cmap, name, post, kern and glyf + loca (TrueType) or CFF (OpenType); an
 * OpenType colour font also has an SVG table (svg_table).
 *   cmap: (0,3) and (3,1) format 4 for the code points below FFFF, (0,4) and (3,10) format 12 for all of them when one is FFFF
 *     or above.  A SYMBOLIC font has besides a (3,0) format 4 subtable, the Windows symbol encoding: a code point below 100 is
 *     mapped as F000 + it, the others as they are (a glyph of its own at F0xx keeps that code).  outline.c and bmsrc.c (and
 *     fontTools' has_symbol_cmap) call a font with a (3,0) subtable symbolic; outline.c and fontTools' getBestCmap read the
 *     code points through the (3,1) / (3,10) subtables, so they come back unchanged (outline.c would also find a code below
 *     100 at F000 + it in a (3,0) subtable alone).
 *   Vertical metrics: the font box (px_font_box) in hhea, the typographic and the Windows set alike; the line gap is LINEGAP
 *     when stated (hhea and sTypoLineGap), else 0.  XHEIGHT and CAPHEIGHT give sxHeight and sCapHeight, UNDERLINE* the post
 *     underline, STRIKEOUT* the OS/2 strike-out (a position names the top pixel row, so the top edge is one row above it),
 *     MONOSPACED post.isFixedPitch.  The weight class comes from the style text (as FNT's), fsSelection and macStyle from the
 *     style and FwMeta.italic.
 *   post: format 2.0 with the glyph names for TrueType and WOFF, 3.0 for CFF (whose charset names the glyphs), as fontTools.
 *   kern: version 0 with one format 0 subtable of the font's kerning pairs (value * 64), in all three formats.
 *   name: Windows (3,1,0x409) UTF-16BE, and Mac Roman (1,0,0) copies of the ASCII ones: 1 family (else the stem), 2 style
 *     (else "Regular"), 3 "pxfe:<PostScript name>", 4 full name (else "<family> <style>"), 5 version (else "Version 1.000";
 *     its first number is head.fontRevision), 6 PostScript name, and 0, 7..14 when stated.
 *   head: dates 0 (the output depends on nothing but the font), checkSumAdjustment set.
 * WOFF 1.0 wraps the TrueType tables, each zlib-compressed when that is smaller. */
#include "fontwrite.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlib/zlib.h"

#define UPP 64                  /* font units per pixel */
#define UPEM 1024
#define MAX_UNITS 16383         /* a coordinate's limit: the difference of two must fit 16 bits (glyf and charstrings) */

static bool fail(char err[PX_ERR_LEN], const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, PX_ERR_LEN, fmt, ap);
    va_end(ap);
    return false;
}

static const char *nz(const char *s) { return s && *s ? s : NULL; }
static void be16s(FwBuf *b, int v) { fw_be16(b, (unsigned)(uint16_t)(int16_t)v); }
static void put8(FwBuf *b, unsigned v) { unsigned char c = (unsigned char)v; fw_buf_add(b, &c, 1); }

/* ------------------------------------------------------------------ outlines */

typedef struct Pt { int x, y; } Pt;
/* Contours in font units: contour c is the points end[c-1] .. end[c]-1 (end[-1] = 0). */
typedef struct Outl { Pt *p; int *end; int np, nc; int xMin, yMin, xMax, yMax; } Outl;

static void outl_free(Outl *o) { free(o->p); free(o->end); memset(o, 0, sizeof *o); }

static bool ink_at(const PxGlyph *g, int c, int j)   /* column c, row j from the bottom */
{
    return c >= 0 && c < g->w && j >= 0 && j < g->h && fw_ink(g, c, g->h - 1 - j);
}

/* One closed loop of grid corners (pixels, relative to the bitmap) as a contour: collinear points dropped (ttf.py
 * `_drop_collinear`), in font units, reversed for CFF. */
static void emit(const PxGlyph *g, const Pt *loop, int n, bool reverse, Outl *o)
{
    int first = o->np;
    for (int i = 0; i < n; i++) {
        Pt a = loop[(i + n - 1) % n], p = loop[i], b = loop[(i + 1) % n];
        if ((long)(p.x - a.x) * (b.y - p.y) != (long)(p.y - a.y) * (b.x - p.x))
            o->p[o->np++] = (Pt){(g->x + p.x) * UPP, (g->y + p.y) * UPP};
    }
    if (o->np == first) return;
    if (reverse)
        for (int i = first, k = o->np - 1; i < k; i++, k--) { Pt t = o->p[i]; o->p[i] = o->p[k]; o->p[k] = t; }
    o->end[o->nc++] = o->np;
}

/* ttf.py `contours`: the closed outlines of the ink, ink on the right.  Edges leave the corners of the pixel grid, (w+1) x (h+1)
 * corners numbered x-major (i * (h+1) + k) so that "the least start" is the first corner in that order that still has an edge.
 * A corner has two edges leaving it where two pixels touch only there; the walk turns right (ttf.py takes whichever Python's set
 * order gives).  A walk that comes back to a corner it passed (a hole touching the outside at a corner) is cut there into
 * loops of their own, so every contour is a simple polygon. */
static bool trace(const PxGlyph *g, bool reverse, Outl *o)
{
    memset(o, 0, sizeof *o);
    if (!g->w || !g->h || !g->pix) return true;
    int w = g->w, h = g->h, H = h + 1;
    size_t nv = (size_t)(w + 1) * (size_t)H;
    unsigned char *cnt = calloc(nv, 1), *dir = malloc(nv * 2);
    int *pos = malloc(sizeof *pos * nv);
    size_t edges = 0;
    if (!cnt || !dir || !pos) { free(cnt); free(dir); free(pos); return false; }
    for (size_t v = 0; v < nv; v++) pos[v] = -1;
#define ADD(i, k, d) do { size_t v_ = (size_t)(i) * (size_t)H + (size_t)(k); dir[v_ * 2 + cnt[v_]++] = (d); edges++; } while (0)
    /* directions: 0 +x, 1 -y, 2 -x, 3 +y (clockwise) */
    for (int j = h - 1; j >= 0; j--)
        for (int c = 0; c < w; c++) {
            if (!ink_at(g, c, j)) continue;
            if (!ink_at(g, c, j + 1)) ADD(c, j + 1, 0);
            if (!ink_at(g, c + 1, j)) ADD(c + 1, j + 1, 1);
            if (!ink_at(g, c, j - 1)) ADD(c + 1, j, 2);
            if (!ink_at(g, c - 1, j)) ADD(c, j, 3);
        }
#undef ADD
    Pt *stack = malloc(sizeof *stack * (edges + 1));
    o->p = malloc(sizeof *o->p * (edges + 1));
    o->end = malloc(sizeof *o->end * (edges / 4 + 1));
    if (!stack || !o->p || !o->end) { free(cnt); free(dir); free(pos); free(stack); outl_free(o); return false; }
    static const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, -1, 0, 1};
    size_t scan = 0;
    for (;;) {
        while (scan < nv && !cnt[scan]) scan++;
        if (scan >= nv) break;
        int x = (int)(scan / (size_t)H), y = (int)(scan % (size_t)H), top = 0, d = -1;
        stack[top++] = (Pt){x, y};
        pos[scan] = 0;
        for (;;) {
            size_t v = (size_t)x * (size_t)H + (size_t)y;
            int k = cnt[v] == 2 && d >= 0 && dir[v * 2] == (d + 1) % 4 ? 0 : cnt[v] - 1;   /* at a corner contact: turn right */
            d = dir[v * 2 + k];
            if (k == 0 && cnt[v] == 2) dir[v * 2] = dir[v * 2 + 1];
            cnt[v]--;
            x += DX[d];
            y += DY[d];
            size_t u = (size_t)x * (size_t)H + (size_t)y;
            if (pos[u] < 0) { pos[u] = top; stack[top++] = (Pt){x, y}; continue; }
            int at = pos[u];   /* back at a corner of this walk: the loop since then is done */
            emit(g, stack + at, top - at, reverse, o);
            for (int i = at + 1; i < top; i++) pos[(size_t)stack[i].x * (size_t)H + (size_t)stack[i].y] = -1;
            top = at + 1;
            if (!at) { pos[u] = -1; break; }   /* back at the start */
        }
    }
    free(cnt);
    free(dir);
    free(pos);
    free(stack);
    for (int i = 0; i < o->np; i++) {
        Pt p = o->p[i];
        if (!i || p.x < o->xMin) o->xMin = p.x;
        if (!i || p.x > o->xMax) o->xMax = p.x;
        if (!i || p.y < o->yMin) o->yMin = p.y;
        if (!i || p.y > o->yMax) o->yMax = p.y;
    }
    return true;
}

/* ------------------------------------------------------------------ the glyph set */

typedef struct SGlyph { const PxGlyph *g; PxCode code; char name[16]; Outl o; int advance; } SGlyph;
typedef struct Map { uint32_t cp; int gid; bool own; } Map;

static int map_cmp(const void *a, const void *b)
{
    const Map *x = a, *y = b;
    if (x->cp != y->cp) return x->cp < y->cp ? -1 : 1;
    if (x->own != y->own) return x->own ? -1 : 1;
    return x->gid - y->gid;
}

/* the glyph id of a code, -1 when the glyph set has none (glyphs 1..n-1 are sorted by code) */
static int gid_of(const SGlyph *s, int n, bool hasDefault, PxCode code)
{
    if (px_code_is_default(code)) return hasDefault && !px_code_variant(code) ? 0 : -1;
    int lo = 1, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (s[mid].code < code) lo = mid + 1; else hi = mid;
    }
    return lo < n && s[lo].code == code ? lo : -1;
}

/* ------------------------------------------------------------------ text */

/* the next code point of UTF-8 text (U+FFFD for a bad sequence) */
static uint32_t utf8_next(const unsigned char **s)
{
    const unsigned char *p = *s;
    uint32_t c = *p++;
    int more = c >= 0xF0 && c < 0xF8 ? 3 : c >= 0xE0 ? (c < 0xF0 ? 2 : -1) : c >= 0xC0 ? 1 : c >= 0x80 ? -1 : 0;
    if (more < 0) { *s = p; return 0xFFFD; }
    if (more) c &= 0x3Fu >> more;
    for (int i = 0; i < more; i++) {
        if ((*p & 0xC0) != 0x80) { *s = p; return 0xFFFD; }
        c = c << 6 | (*p++ & 0x3Fu);
    }
    *s = p;
    return c > 0x10FFFF || (c >= 0xD800 && c < 0xE000) ? 0xFFFD : c;
}

static void utf16be(FwBuf *b, const char *utf8)
{
    for (const unsigned char *s = (const unsigned char *)utf8; *s;) {
        uint32_t c = utf8_next(&s);
        if (c >= 0x10000) { c -= 0x10000; fw_be16(b, 0xD800 + (c >> 10)); fw_be16(b, 0xDC00 + (c & 0x3FF)); }
        else fw_be16(b, c);
    }
}

static bool is_ascii(const char *s) { for (; *s; s++) if ((unsigned char)*s >= 0x80) return false; return true; }

/* printable ASCII of UTF-8 text for the CFF strings, '?' for any other character */
static void ascii_of(const char *utf8, char *out, size_t cap)
{
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)utf8; *s && o + 1 < cap;) {
        uint32_t c = utf8_next(&s);
        out[o++] = c >= 0x20 && c < 0x7F ? (char)c : '?';
    }
    out[o] = 0;
}

static bool has_word(const char *s, const char *word)   /* case-insensitive ASCII substring */
{
    for (; *s; s++) {
        size_t k = 0;
        while (word[k] && s[k] && (s[k] | 0x20) == word[k]) k++;
        if (!word[k]) return true;
    }
    return false;
}

static int weight_class(const char *style)   /* as fontwrite.c's FNT: the first weight name the style starts with */
{
    static const struct { const char *name; int w; } kW[] = {{"thin", 100}, {"extralight", 200}, {"light", 300}, {"regular", 400},
        {"medium", 500}, {"semibold", 600}, {"bold", 700}, {"extrabold", 800}, {"black", 900}};
    for (size_t i = 0; i < sizeof kW / sizeof kW[0]; i++) {
        size_t l = strlen(kW[i].name), k = 0;
        while (k < l && style[k] && (style[k] | 0x20) == kW[i].name[k]) k++;
        if (k == l) return kW[i].w;
    }
    return 400;
}

/* the first number of a version text as 16.16 ("Version 1.250" -> 0x00014000); 1.0 when it has none */
static unsigned long font_revision(const char *v)
{
    const char *s = v;
    while (*s && (*s < '0' || *s > '9')) s++;
    if (!*s) return 0x10000;
    double x = 0, scale = 1;
    for (; *s >= '0' && *s <= '9' && x < 30000; s++) x = x * 10 + (*s - '0');
    if (*s == '.') for (s++; *s >= '0' && *s <= '9' && scale > 1e-6; s++) { scale /= 10; x += (*s - '0') * scale; }
    if (x >= 32768) return 0x10000;
    return (unsigned long)(x * 65536.0 + 0.5);
}

/* ------------------------------------------------------------------ tables */

typedef struct Table { char tag[5]; FwBuf b; } Table;

static unsigned long checksum(const unsigned char *p, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i += 4) {
        uint32_t v = 0;
        for (size_t k = 0; k < 4; k++) v = v << 8 | (i + k < n ? p[i + k] : 0u);
        sum += v;
    }
    return sum;
}

static int table_cmp(const void *a, const void *b) { return memcmp(((const Table *)a)->tag, ((const Table *)b)->tag, 4); }

/* searchRange, entrySelector, rangeShift of n items of `size` bytes */
static void bsearch_header(FwBuf *b, unsigned n, unsigned size)
{
    unsigned p = 1, e = 0;
    while (p * 2 <= n) p *= 2, e++;
    if (!n) p = 0;
    fw_be16(b, p * size); fw_be16(b, e); fw_be16(b, n * size - p * size);
}

/* cmap format 4 of m[0..n) (sorted, codes below FFFF): one segment per run of consecutive codes and glyph ids */
static bool cmap4(FwBuf *b, const Map *m, int n)
{
    int segs = 1;
    for (int i = 0; i < n; i++) if (i + 1 == n || m[i + 1].cp != m[i].cp + 1 || m[i + 1].gid != m[i].gid + 1) segs++;
    unsigned long len = 16 + 8ul * (unsigned long)segs;
    if (len > 0xFFFF) return false;
    fw_be16(b, 4); fw_be16(b, (unsigned)len); fw_be16(b, 0); fw_be16(b, (unsigned)segs * 2);
    {
        unsigned p = 1, e = 0;
        while (p * 2 <= (unsigned)segs) p *= 2, e++;
        fw_be16(b, p * 2); fw_be16(b, e); fw_be16(b, (unsigned)segs * 2 - p * 2);
    }
    for (int pass = 0; pass < 3; pass++) {   /* endCode, startCode, idDelta */
        for (int i = 0, s = 0; i < n; i++) {
            if (i + 1 < n && m[i + 1].cp == m[i].cp + 1 && m[i + 1].gid == m[i].gid + 1) continue;
            fw_be16(b, pass == 0 ? m[i].cp : pass == 1 ? m[s].cp : (unsigned)(m[s].gid - (int)m[s].cp) & 0xFFFF);
            s = i + 1;
        }
        fw_be16(b, pass == 2 ? 1 : 0xFFFF);
        if (pass == 0) fw_be16(b, 0);   /* reservedPad */
    }
    for (int i = 0; i < segs; i++) fw_be16(b, 0);   /* idRangeOffset */
    return true;
}

static void cmap12(FwBuf *b, const Map *m, int n)
{
    unsigned long groups = 0;
    for (int i = 0; i < n; i++) if (i + 1 == n || m[i + 1].cp != m[i].cp + 1 || m[i + 1].gid != m[i].gid + 1) groups++;
    fw_be16(b, 12); fw_be16(b, 0); fw_be32(b, 16 + 12 * groups); fw_be32(b, 0); fw_be32(b, groups);
    for (int i = 0, s = 0; i < n; i++) {
        if (i + 1 < n && m[i + 1].cp == m[i].cp + 1 && m[i + 1].gid == m[i].gid + 1) continue;
        fw_be32(b, m[s].cp); fw_be32(b, m[i].cp); fw_be32(b, (unsigned long)m[s].gid);
        s = i + 1;
    }
}

static bool cmap_table(FwBuf *b, const Map *m, int n, bool symbolic, char err[PX_ERR_LEN])
{
    int bmp = 0;
    while (bmp < n && m[bmp].cp < 0xFFFF) bmp++;
    bool wide = bmp < n;
    Map *sym = NULL;
    int ns = 0;
    if (symbolic) {
        sym = malloc(sizeof *sym * (size_t)(bmp + 1));
        if (!sym) return fail(err, "out of memory");
        for (int i = 0; i < bmp; i++) sym[ns++] = (Map){m[i].cp < 0x100 ? 0xF000 + m[i].cp : m[i].cp, m[i].gid, m[i].cp >= 0x100};
        qsort(sym, (size_t)ns, sizeof *sym, map_cmp);
        int k = 0;
        for (int i = 0; i < ns; i++) if (!k || sym[i].cp != sym[k - 1].cp) sym[k++] = sym[i];   /* a glyph of its own at F0xx first */
        ns = k;
    }
    FwBuf s4 = {0}, s12 = {0}, s0 = {0};
    bool ok = cmap4(&s4, m, bmp) && (!symbolic || cmap4(&s0, sym, ns));
    if (wide) cmap12(&s12, m, n);
    free(sym);
    if (!ok) { fw_buf_free(&s4); fw_buf_free(&s0); fw_buf_free(&s12); return fail(err, "too many code point ranges for a cmap"); }
    struct { unsigned pid, eid; int sub; } rec[5];
    int nr = 0;
    rec[nr].pid = 0; rec[nr].eid = 3; rec[nr++].sub = 0;
    if (wide) { rec[nr].pid = 0; rec[nr].eid = 4; rec[nr++].sub = 1; }
    if (symbolic) { rec[nr].pid = 3; rec[nr].eid = 0; rec[nr++].sub = 2; }
    rec[nr].pid = 3; rec[nr].eid = 1; rec[nr++].sub = 0;
    if (wide) { rec[nr].pid = 3; rec[nr].eid = 10; rec[nr++].sub = 1; }
    unsigned long at = 4 + 8ul * (unsigned long)nr, off[3];
    off[0] = at; at += s4.n;
    off[1] = at; at += s12.n;
    off[2] = at;
    fw_be16(b, 0); fw_be16(b, (unsigned)nr);
    for (int i = 0; i < nr; i++) { fw_be16(b, rec[i].pid); fw_be16(b, rec[i].eid); fw_be32(b, off[rec[i].sub]); }
    fw_buf_add(b, s4.p, s4.n);
    fw_buf_add(b, s12.p, s12.n);
    fw_buf_add(b, s0.p, s0.n);
    if (s4.failed || s12.failed || s0.failed) b->failed = true;
    fw_buf_free(&s4); fw_buf_free(&s12); fw_buf_free(&s0);
    return true;
}

typedef struct NameRec { unsigned pid, eid, lang, id; size_t off, len; } NameRec;

static void name_table(FwBuf *b, const char *const text[15], char *warn, size_t wc)
{
    NameRec rec[30];
    int nr = 0;
    FwBuf store = {0};
    for (int plat = 0; plat < 2; plat++)   /* Mac Roman (ASCII texts only), then Windows */
        for (unsigned id = 0; id < 15; id++) {
            const char *t = text[id];
            if (!nz(t) || (plat == 0 && !is_ascii(t))) continue;
            if (store.n > 0xFFFF) { fw_warn(warn, wc, "name table full: name %u left out", id); continue; }
            NameRec *r = &rec[nr++];
            r->pid = plat ? 3 : 1; r->eid = plat ? 1 : 0; r->lang = plat ? 0x409 : 0; r->id = id; r->off = store.n;
            if (plat) utf16be(&store, t); else fw_buf_add(&store, t, strlen(t));
            r->len = store.n - r->off;
            if (r->len > 0xFFFF) {   /* cut on a character boundary */
                size_t keep = plat ? 0xFFFE : 0xFFFF;
                if (plat && store.p && (store.p[r->off + keep - 2] & 0xFC) == 0xD8) keep -= 2;
                store.n = r->off + keep;
                r->len = keep;
                fw_warn(warn, wc, "name %u cut to %u bytes", id, (unsigned)keep);
            }
        }
    fw_be16(b, 0); fw_be16(b, (unsigned)nr); fw_be16(b, 6 + 12 * (unsigned)nr);
    for (int i = 0; i < nr; i++) {
        fw_be16(b, rec[i].pid); fw_be16(b, rec[i].eid); fw_be16(b, rec[i].lang); fw_be16(b, rec[i].id);
        fw_be16(b, (unsigned)rec[i].len); fw_be16(b, (unsigned)rec[i].off);
    }
    fw_buf_add(b, store.p, store.n);
    if (store.failed) b->failed = true;
    fw_buf_free(&store);
}

/* ------------------------------------------------------------------ glyf */

static void glyf_glyph(FwBuf *b, const Outl *o)
{
    if (!o->nc) return;
    be16s(b, o->nc); be16s(b, o->xMin); be16s(b, o->yMin); be16s(b, o->xMax); be16s(b, o->yMax);
    for (int c = 0; c < o->nc; c++) fw_be16(b, (unsigned)(o->end[c] - 1));
    fw_be16(b, 0);   /* no instructions */
    for (int pass = 0; pass < 3; pass++) {   /* flags, x, y */
        int px = 0, py = 0;
        for (int i = 0; i < o->np; i++) {
            int dx = o->p[i].x - px, dy = o->p[i].y - py;
            px = o->p[i].x; py = o->p[i].y;
            if (pass == 0) {
                unsigned f = 1;   /* on the curve */
                if (!dx) f |= 0x10; else if (dx > -256 && dx < 256) f |= 0x02 | (dx > 0 ? 0x10 : 0);
                if (!dy) f |= 0x20; else if (dy > -256 && dy < 256) f |= 0x04 | (dy > 0 ? 0x20 : 0);
                put8(b, f);
            } else {
                int d = pass == 1 ? dx : dy;
                if (!d) continue;
                if (d > -256 && d < 256) put8(b, (unsigned)(d < 0 ? -d : d)); else be16s(b, d);
            }
        }
    }
    fw_buf_zero(b, (size_t)(-(long)b->n & 3));
}

/* ------------------------------------------------------------------ CFF */

static void dict_int(FwBuf *b, long v)
{
    if (v >= -107 && v <= 107) put8(b, (unsigned)(v + 139));
    else if (v >= 108 && v <= 1131) { v -= 108; put8(b, (unsigned)(v >> 8) + 247); put8(b, (unsigned)v & 0xFF); }
    else if (v >= -1131 && v <= -108) { v = -v - 108; put8(b, (unsigned)(v >> 8) + 251); put8(b, (unsigned)v & 0xFF); }
    else if (v >= -32768 && v <= 32767) { put8(b, 28); be16s(b, (int)v); }
    else { put8(b, 29); fw_be32(b, (unsigned long)v); }
}
static void dict_int5(FwBuf *b, unsigned long v) { put8(b, 29); fw_be32(b, v); }   /* fixed size: an offset */

static void cs_int(FwBuf *b, int v)   /* a Type 2 charstring number (|v| < 32768) */
{
    if (v >= -107 && v <= 107) put8(b, (unsigned)(v + 139));
    else if (v >= 108 && v <= 1131) { v -= 108; put8(b, (unsigned)(v >> 8) + 247); put8(b, (unsigned)v & 0xFF); }
    else if (v >= -1131 && v <= -108) { v = -v - 108; put8(b, (unsigned)(v >> 8) + 251); put8(b, (unsigned)v & 0xFF); }
    else { put8(b, 28); be16s(b, v); }
}

/* An INDEX of n items lying back to back in data, ends[i] the end of item i. */
static void cff_index(FwBuf *b, const unsigned char *data, const size_t *ends, int n)
{
    fw_be16(b, (unsigned)n);
    if (!n) return;
    size_t last = ends[n - 1] + 1;
    unsigned os = last < 0x100 ? 1 : last < 0x10000 ? 2 : last < 0x1000000 ? 3 : 4;
    put8(b, os);
    for (int i = 0; i <= n; i++) {
        size_t v = (i ? ends[i - 1] : 0) + 1;
        for (unsigned k = os; k-- > 0;) put8(b, (unsigned)(v >> (8 * k)) & 0xFF);
    }
    fw_buf_add(b, data, ends[n - 1]);
}

/* A Type 2 charstring: the width (when not defaultWidthX 0: nominalWidthX is 0 too), rmoveto, runs of hlineto / vlineto (a
 * rectilinear contour alternates), rlineto for anything else, endchar.  The closing line of each contour is implicit. */
static void charstring(FwBuf *b, const Outl *o, int advance)
{
    bool width = advance != 0;
    int cx = 0, cy = 0;
    for (int c = 0, s = 0; c < o->nc; s = o->end[c++]) {
        int e = o->end[c];
        if (width) cs_int(b, advance), width = false;
        cs_int(b, o->p[s].x - cx); cs_int(b, o->p[s].y - cy); put8(b, 21);   /* rmoveto */
        cx = o->p[s].x; cy = o->p[s].y;
        int i = s + 1;
        while (i < e) {
            int dx = o->p[i].x - cx, dy = o->p[i].y - cy;
            if (dx && dy) {   /* rlineto */
                int k = 0;
                while (i < e && k < 20) {
                    dx = o->p[i].x - cx; dy = o->p[i].y - cy;
                    if (!dx || !dy) break;
                    cs_int(b, dx); cs_int(b, dy); cx += dx; cy += dy; i++; k++;
                }
                put8(b, 5);
                continue;
            }
            bool horiz = dy == 0, h = horiz;
            int k = 0;
            while (i < e && k < 40) {
                dx = o->p[i].x - cx; dy = o->p[i].y - cy;
                if (h ? dy != 0 : dx != 0) break;
                cs_int(b, h ? dx : dy); cx += dx; cy += dy; i++; k++;
                h = !h;
            }
            put8(b, horiz ? 6 : 7);   /* hlineto, vlineto */
        }
    }
    if (width) cs_int(b, advance);
    put8(b, 14);   /* endchar */
}

static bool cff_table(FwBuf *out, const SGlyph *s, int n, const char *psName, const char *fullName, const char *family,
                      const char *style, const int bbox[4])
{
    /* strings: FullName, FamilyName, Weight, then the glyph names of glyphs 1..n-1 (SID 391 + index) */
    FwBuf str = {0}, cs = {0};
    size_t *strEnds = malloc(sizeof *strEnds * (size_t)(n + 3)), *csEnds = malloc(sizeof *csEnds * (size_t)n);
    if (!strEnds || !csEnds) { free(strEnds); free(csEnds); return false; }
    char t[512];
    int ns = 0;
    const char *top[3] = {fullName, family, style};
    for (int i = 0; i < 3; i++) { ascii_of(top[i], t, sizeof t); fw_buf_add(&str, t, strlen(t)); strEnds[ns++] = str.n; }
    for (int g = 1; g < n; g++) { fw_buf_add(&str, s[g].name, strlen(s[g].name)); strEnds[ns++] = str.n; }
    for (int g = 0; g < n; g++) { charstring(&cs, &s[g].o, s[g].advance); csEnds[g] = cs.n; }
    /* the Top DICT: its offsets are 5-byte numbers, so its size is known before them */
    unsigned long charsetOff = 0, csOff = 0, privOff = 0;
    static const unsigned char kPrivate[] = {139, 20, 139, 21};   /* defaultWidthX 0, nominalWidthX 0 */
    FwBuf dict = {0}, head = {0};
    for (int pass = 0; pass < 2; pass++) {
        dict.n = 0;
        dict_int(&dict, 391); put8(&dict, 2);    /* FullName */
        dict_int(&dict, 392); put8(&dict, 3);    /* FamilyName */
        dict_int(&dict, 393); put8(&dict, 4);    /* Weight */
        for (int i = 0; i < 4; i++) dict_int(&dict, bbox[i]);
        put8(&dict, 5);                          /* FontBBox */
        {   /* FontMatrix [1/1024 0 0 1/1024 0 0]: 9.765625E-4 as a real */
            static const unsigned char r[] = {30, 0x9a, 0x76, 0x56, 0x25, 0xc4, 0xff};
            fw_buf_add(&dict, r, sizeof r); put8(&dict, 139); put8(&dict, 139);
            fw_buf_add(&dict, r, sizeof r); put8(&dict, 139); put8(&dict, 139);
            put8(&dict, 12); put8(&dict, 7);
        }
        dict_int5(&dict, charsetOff); put8(&dict, 15);
        dict_int5(&dict, csOff); put8(&dict, 17);
        dict_int5(&dict, sizeof kPrivate); dict_int5(&dict, privOff); put8(&dict, 18);
        if (pass) break;
        /* the parts before the charset */
        head.n = 0;
        put8(&head, 1); put8(&head, 0); put8(&head, 4); put8(&head, 4);   /* version 1.0, header size, offset size */
        size_t nameEnd = strlen(psName), dictEnd = dict.n;
        cff_index(&head, (const unsigned char *)psName, &nameEnd, 1);
        cff_index(&head, dict.p, &dictEnd, 1);
        cff_index(&head, str.p, strEnds, ns);
        fw_be16(&head, 0);                       /* no global subroutines */
        charsetOff = head.n;
        csOff = charsetOff + 1 + 2ul * (unsigned long)(n - 1);
        FwBuf tmp = {0};
        cff_index(&tmp, cs.p, csEnds, n);
        privOff = csOff + tmp.n;
        fw_buf_free(&tmp);
    }
    size_t nameEnd = strlen(psName), dictEnd = dict.n;
    put8(out, 1); put8(out, 0); put8(out, 4); put8(out, 4);
    cff_index(out, (const unsigned char *)psName, &nameEnd, 1);
    cff_index(out, dict.p, &dictEnd, 1);
    cff_index(out, str.p, strEnds, ns);
    fw_be16(out, 0);
    put8(out, 0);                                /* charset format 0 */
    for (int g = 1; g < n; g++) fw_be16(out, (unsigned)(391 + 3 + g - 1));
    cff_index(out, cs.p, csEnds, n);
    fw_buf_add(out, kPrivate, sizeof kPrivate);
    bool ok = !str.failed && !cs.failed && !dict.failed && !head.failed && out->n == privOff + sizeof kPrivate;
    fw_buf_free(&str); fw_buf_free(&cs); fw_buf_free(&dict); fw_buf_free(&head);
    free(strEnds); free(csEnds);
    return ok;
}

/* ------------------------------------------------------------------ the sfnt and WOFF */

/* The SVG table of a colour font (OpenType "SVG "): one document per glyph with ink, `<g id="glyphN">` holding a path per colour
 * of the glyph's palette, traced as the outlines are (so a colour's area has no seams inside it), y flipped for SVG.  A pixel
 * past the palette's end is black, as everywhere in pxfe; an alpha below FF becomes fill-opacity. */
static bool svg_table(FwBuf *b, const PxFont *f, const SGlyph *s, int n)
{
    FwBuf docs = {0}, idx = {0};
    int entries = 0;
    char num[96];
    for (int g = 0; g < n; g++) {
        const PxGlyph *pg = s[g].g;
        if (!pg || !pg->w || !pg->h || !pg->pix) continue;
        const PxPalette *pal = f->npals > pg->pi && pg->pi >= 0 ? &f->pals[pg->pi] : NULL;
        size_t start = docs.n;
        int len = snprintf(num, sizeof num, "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\"><g id=\"glyph%d\">", g);
        fw_buf_add(&docs, num, (size_t)len);
        bool used[PX_NUM_PIXELS] = {false};
        size_t area = (size_t)pg->w * (size_t)pg->h;
        for (size_t i = 0; i < area; i++) { int k = px_pixel_index(pg->pix[i]); if (k > 0) used[k] = true; }
        PxGlyph one = *pg;   /* the pixels of one colour, as ink */
        one.pix = malloc(area);
        if (!one.pix) { fw_buf_free(&docs); fw_buf_free(&idx); return false; }
        for (int k = 1; k < PX_NUM_PIXELS; k++) {
            if (!used[k]) continue;
            for (size_t i = 0; i < area; i++) one.pix[i] = px_pixel_index(pg->pix[i]) == k ? '0' : '.';
            Outl o;
            if (!trace(&one, false, &o)) { free(one.pix); fw_buf_free(&docs); fw_buf_free(&idx); return false; }
            uint32_t c = pal && k - 1 < pal->n ? pal->c[k - 1] : 0x000000FFu;
            len = snprintf(num, sizeof num, "<path fill=\"#%06X\"", (unsigned)(c >> 8));
            fw_buf_add(&docs, num, (size_t)len);
            if ((c & 0xFF) != 0xFF) { len = snprintf(num, sizeof num, " fill-opacity=\"%.3f\"", (double)(c & 0xFF) / 255.0); fw_buf_add(&docs, num, (size_t)len); }
            fw_buf_add(&docs, " d=\"", 4);
            for (int ci = 0, p = 0; ci < o.nc; ci++) {
                for (int first = p; p < o.end[ci]; p++) {
                    len = snprintf(num, sizeof num, "%c%d %d", p == first ? 'M' : 'L', o.p[p].x, -o.p[p].y);
                    fw_buf_add(&docs, num, (size_t)len);
                }
                fw_buf_add(&docs, "Z", 1);
            }
            fw_buf_add(&docs, "\"/>", 3);
            outl_free(&o);
        }
        free(one.pix);
        fw_buf_add(&docs, "</g></svg>", 10);
        fw_be16(&idx, (unsigned)g); fw_be16(&idx, (unsigned)g);
        fw_be32(&idx, (unsigned long)start); fw_be32(&idx, (unsigned long)(docs.n - start));   /* the offset is fixed up below */
        entries++;
    }
    /* the header, then the document index; an offset counts from the index's start */
    fw_be16(b, 0); fw_be32(b, 10); fw_be32(b, 0);
    fw_be16(b, (unsigned)entries);
    size_t base = 2 + (size_t)entries * 12;
    for (int e = 0; e < entries && !idx.failed; e++) {
        unsigned char *r = idx.p + (size_t)e * 12;
        unsigned long off = ((unsigned long)r[4] << 24 | (unsigned long)r[5] << 16 | (unsigned long)r[6] << 8 | r[7]) + (unsigned long)base;
        r[4] = (unsigned char)(off >> 24); r[5] = (unsigned char)(off >> 16); r[6] = (unsigned char)(off >> 8); r[7] = (unsigned char)off;
    }
    fw_buf_add(b, idx.p, idx.n);
    fw_buf_add(b, docs.p, docs.n);
    bool ok = !docs.failed && !idx.failed && !b->failed;
    fw_buf_free(&docs);
    fw_buf_free(&idx);
    return ok;
}

static unsigned char *sfnt_assemble(Table *t, int nt, unsigned long flavor, size_t *len)
{
    qsort(t, (size_t)nt, sizeof *t, table_cmp);
    FwBuf o = {0};
    fw_be32(&o, flavor); fw_be16(&o, (unsigned)nt);
    bsearch_header(&o, (unsigned)nt, 16);
    unsigned long at = 12 + 16ul * (unsigned long)nt;
    int head = -1;
    for (int i = 0; i < nt; i++) {
        if (!memcmp(t[i].tag, "head", 4)) head = i;
        fw_buf_add(&o, t[i].tag, 4); fw_be32(&o, checksum(t[i].b.p, t[i].b.n)); fw_be32(&o, at); fw_be32(&o, (unsigned long)t[i].b.n);
        at += (unsigned long)((t[i].b.n + 3) & ~(size_t)3);
    }
    size_t headAt = 0;
    for (int i = 0; i < nt; i++) {
        if (i == head) headAt = o.n;
        fw_buf_add(&o, t[i].b.p, t[i].b.n);
        fw_buf_zero(&o, (size_t)(-(long)t[i].b.n & 3));
    }
    if (o.failed || head < 0) { fw_buf_free(&o); return NULL; }
    uint32_t adj = (uint32_t)(0xB1B0AFBAul - checksum(o.p, o.n));
    for (int k = 0; k < 4; k++) o.p[headAt + 8 + k] = t[head].b.p[8 + k] = (unsigned char)(adj >> (24 - 8 * k));
    *len = o.n;
    return o.p;
}

/* zlib at level 9 (vendor/zlib has no compress2); NULL when it fails */
static unsigned char *zpack(const unsigned char *in, size_t n, size_t *outLen)
{
    z_stream z;
    memset(&z, 0, sizeof z);
    if (deflateInit(&z, 9) != Z_OK) return NULL;
    uLong cap = deflateBound(&z, (uLong)n);
    unsigned char *out = malloc(cap ? cap : 1);
    if (!out) { deflateEnd(&z); return NULL; }
    z.next_in = (Bytef *)in; z.avail_in = (uInt)n;
    z.next_out = out; z.avail_out = (uInt)cap;
    int rc = deflate(&z, Z_FINISH);
    *outLen = z.total_out;
    deflateEnd(&z);
    if (rc != Z_STREAM_END) { free(out); return NULL; }
    return out;
}

/* WOFF 1.0 of the tables of an assembled sfnt (sorted, head adjusted) */
static unsigned char *woff_wrap(const Table *t, int nt, unsigned long flavor, size_t sfntLen, size_t *len)
{
    FwBuf dir = {0}, data = {0};
    unsigned long at = 44 + 20ul * (unsigned long)nt;
    for (int i = 0; i < nt; i++) {
        size_t zl = 0;
        unsigned char *z = zpack(t[i].b.p, t[i].b.n, &zl);
        bool packed = z && zl < t[i].b.n;
        const unsigned char *p = packed ? z : t[i].b.p;
        size_t n = packed ? zl : t[i].b.n;
        fw_buf_add(&dir, t[i].tag, 4); fw_be32(&dir, at + data.n); fw_be32(&dir, (unsigned long)n);
        unsigned long sum = checksum(t[i].b.p, t[i].b.n);
        if (!memcmp(t[i].tag, "head", 4) && t[i].b.n >= 12)   /* as in the sfnt: with checkSumAdjustment 0 */
            sum = (sum - ((unsigned long)t[i].b.p[8] << 24 | (unsigned long)t[i].b.p[9] << 16 | (unsigned long)t[i].b.p[10] << 8 | t[i].b.p[11])) & 0xFFFFFFFFul;
        fw_be32(&dir, (unsigned long)t[i].b.n); fw_be32(&dir, sum);
        fw_buf_add(&data, p, n);
        fw_buf_zero(&data, (size_t)(-(long)n & 3));
        free(z);
    }
    FwBuf o = {0};
    fw_buf_add(&o, "wOFF", 4); fw_be32(&o, flavor); fw_be32(&o, at + data.n); fw_be16(&o, (unsigned)nt); fw_be16(&o, 0);
    fw_be32(&o, (unsigned long)sfntLen); fw_be16(&o, 0); fw_be16(&o, 0);
    fw_be32(&o, 0); fw_be32(&o, 0); fw_be32(&o, 0); fw_be32(&o, 0); fw_be32(&o, 0);   /* no metadata, no private data */
    fw_buf_add(&o, dir.p, dir.n);
    fw_buf_add(&o, data.p, data.n);
    bool failed = dir.failed || data.failed || o.failed;
    fw_buf_free(&dir); fw_buf_free(&data);
    if (failed) { fw_buf_free(&o); return NULL; }
    *len = o.n;
    return o.p;
}

/* ------------------------------------------------------------------ entry point */

typedef struct KernPair { int l, r, v; } KernPair;
static int kern_cmp(const void *a, const void *b)
{
    const KernPair *x = a, *y = b;
    return x->l != y->l ? x->l - y->l : x->r - y->r;
}

unsigned char *fw_sfnt_bytes(FwFormat fmt, const FwFace *face, const char *stem, size_t *len, char *warn, size_t warnCap,
                             char err[PX_ERR_LEN])
{
    if (fmt != FW_TTF && fmt != FW_OTF && fmt != FW_WOFF) { fail(err, "not an sfnt format"); return NULL; }
    const PxFont *f = face->font;
    const FwMeta *meta = &face->meta;
    bool ttf = fmt != FW_OTF;
    int ascent, descent;
    if (!px_font_box(f, &ascent, &descent)) { fail(err, "the font's bitmaps are not loaded"); return NULL; }
    if (ascent * UPP > 32767 || descent * UPP > 32768) { fail(err, "the font box is too tall for 64 units a pixel"); return NULL; }
    FwGlyphs gl;
    if (!fw_glyphs(f, true, &gl)) { fail(err, "out of memory"); return NULL; }
    for (int i = 0; i < gl.n; i++)   /* the box may be stated while a bitmap is still lazy */
        if (gl.g[i]->lazy && gl.g[i]->hash[0]) { fw_glyphs_free(&gl); fail(err, "the font's bitmaps are not loaded"); return NULL; }
    bool hasDefault = gl.n && px_code_is_default(gl.g[gl.n - 1]->code);
    int n = gl.n + !hasDefault;
    if (n > 0xFFFF) { fw_glyphs_free(&gl); fail(err, "too many glyphs: %d", n); return NULL; }
    SGlyph *s = calloc((size_t)n, sizeof *s);
    Map *map = malloc(sizeof *map * (size_t)n);
    KernPair *kp = malloc(sizeof *kp * (size_t)(f->kernCount + 1));
    Table t[12];
    int nt = 0;
    unsigned char *out = NULL;
    FwBuf glyf = {0};
    size_t *at = NULL;
    memset(t, 0, sizeof t);
    if (!s || !map || !kp) { fail(err, "out of memory"); goto done; }

    /* the glyph set, its names and outlines */
    s[0].g = hasDefault ? gl.g[gl.n - 1] : NULL;
    s[0].code = PX_CODE_DEFAULT;
    strcpy(s[0].name, ".notdef");
    for (int i = 0, g = 1; i < gl.n - hasDefault; i++, g++) {
        s[g].g = gl.g[i];
        s[g].code = gl.g[i]->code;
        int32_t cp = px_code_codepoint(s[g].code);
        if (cp < 0) snprintf(s[g].name, sizeof s[g].name, "glyph%05d", g);
        else snprintf(s[g].name, sizeof s[g].name, cp > 0xFFFF ? "u%X" : "uni%04X", (unsigned)cp);
    }
    int nmap = 0, negative = 0;
    for (int g = 0; g < n; g++) {
        SGlyph *q = &s[g];
        if (!q->g) continue;
        if (!trace(q->g, !ttf, &q->o)) { fail(err, "out of memory"); goto done; }
        if (q->o.nc && (q->o.xMin < -MAX_UNITS || q->o.yMin < -MAX_UNITS || q->o.xMax > MAX_UNITS || q->o.yMax > MAX_UNITS)) {
            fail(err, "glyph %s reaches more than 255 pixels from its origin", q->name); goto done;
        }
        if (q->o.np > 0xFFFF || q->o.nc > 0x7FFF) { fail(err, "glyph %s has too many points", q->name); goto done; }
        q->advance = q->g->advance * UPP;
        if (q->advance < 0) { q->advance = 0; negative++; }
        if (q->advance > 0xFFFF) { fail(err, "the advance of glyph %s is too wide", q->name); goto done; }
        int32_t cp = px_code_codepoint(q->code);
        if (cp >= 0) map[nmap++] = (Map){(uint32_t)cp, g, true};
    }
    if (negative) fw_warn(warn, warnCap, "%d negative advance(s) written as 0", negative);

    /* font-wide numbers */
    const char *family = nz(meta->family) ? meta->family : stem ? stem : "";
    const char *style = nz(meta->style) ? meta->style : "Regular";
    char full[1024], psSrc[1024], ps[64], unique[80];
    if (nz(meta->fullName)) snprintf(full, sizeof full, "%s", meta->fullName); else snprintf(full, sizeof full, "%s %s", family, style);
    snprintf(psSrc, sizeof psSrc, "%s-%s", family, style);
    fw_ascii_name(psSrc, ps, sizeof ps, false);
    if (!ps[0]) strcpy(ps, "PixelFont");
    snprintf(unique, sizeof unique, "pxfe:%s", ps);
    const char *version = nz(meta->version) ? meta->version : "Version 1.000";
    int weight = weight_class(style);
    bool bold = weight == 700, italic = meta->italic || has_word(style, "italic") || has_word(style, "oblique");
    int bbox[4] = {0, 0, 0, 0}, advMax = 0, minL = 0, minR = 0, maxExt = 0, maxPts = 0, maxCont = 0;
    long sumAdv = 0, nAdv = 0;
    bool any = false;
    for (int g = 0; g < n; g++) {
        const SGlyph *q = &s[g];
        if (q->advance > advMax) advMax = q->advance;
        if (q->advance > 0) sumAdv += q->advance, nAdv++;
        if (!q->o.nc) continue;
        if (!any || q->o.xMin < bbox[0]) bbox[0] = q->o.xMin;
        if (!any || q->o.yMin < bbox[1]) bbox[1] = q->o.yMin;
        if (!any || q->o.xMax > bbox[2]) bbox[2] = q->o.xMax;
        if (!any || q->o.yMax > bbox[3]) bbox[3] = q->o.yMax;
        if (!any || q->o.xMin < minL) minL = q->o.xMin;
        if (!any || q->advance - q->o.xMax < minR) minR = q->advance - q->o.xMax;
        if (!any || q->o.xMax > maxExt) maxExt = q->o.xMax;
        if (q->o.np > maxPts) maxPts = q->o.np;
        if (q->o.nc > maxCont) maxCont = q->o.nc;
        any = true;
    }
    int lineGap = f->has[PXM_LINEGAP] ? f->m[PXM_LINEGAP] * UPP : 0;
    if (lineGap < 0 || lineGap > 32767) lineGap = 0;

    /* glyf and loca */
    if (ttf) {
        at = malloc(sizeof *at * (size_t)(n + 1));
        if (!at) { fail(err, "out of memory"); goto done; }
        for (int g = 0; g < n; g++) { at[g] = glyf.n; glyf_glyph(&glyf, &s[g].o); }
        at[n] = glyf.n;
        if (!glyf.n) fw_buf_zero(&glyf, 1);   /* every glyph empty: one zero byte, as fontTools (OTS and Windows want a glyf) */
    }
    bool longLoca = ttf && glyf.n > 0x1FFFC;

    /* head */
    Table *T = &t[nt++];
    memcpy(T->tag, "head", 4);
    fw_be16(&T->b, 1); fw_be16(&T->b, 0); fw_be32(&T->b, font_revision(version)); fw_be32(&T->b, 0); fw_be32(&T->b, 0x5F0F3CF5);
    fw_be16(&T->b, 3); fw_be16(&T->b, UPEM); fw_buf_zero(&T->b, 16);   /* flags, units per em, created and modified 0 */
    for (int i = 0; i < 4; i++) be16s(&T->b, bbox[i]);
    fw_be16(&T->b, (bold ? 1u : 0u) | (italic ? 2u : 0u)); fw_be16(&T->b, 3); be16s(&T->b, 2); fw_be16(&T->b, longLoca); fw_be16(&T->b, 0);
    /* hhea */
    T = &t[nt++];
    memcpy(T->tag, "hhea", 4);
    fw_be32(&T->b, 0x10000); be16s(&T->b, ascent * UPP); be16s(&T->b, -descent * UPP); be16s(&T->b, lineGap);
    fw_be16(&T->b, (unsigned)advMax); be16s(&T->b, minL); be16s(&T->b, minR); be16s(&T->b, maxExt);
    be16s(&T->b, 1); be16s(&T->b, 0); be16s(&T->b, 0); fw_buf_zero(&T->b, 10); fw_be16(&T->b, (unsigned)n);
    /* maxp */
    T = &t[nt++];
    memcpy(T->tag, "maxp", 4);
    if (ttf) {
        fw_be32(&T->b, 0x10000); fw_be16(&T->b, (unsigned)n); fw_be16(&T->b, (unsigned)maxPts); fw_be16(&T->b, (unsigned)maxCont);
        fw_be16(&T->b, 0); fw_be16(&T->b, 0); fw_be16(&T->b, 2); fw_buf_zero(&T->b, 16);
    } else { fw_be32(&T->b, 0x5000); fw_be16(&T->b, (unsigned)n); }
    /* OS/2 */
    T = &t[nt++];
    memcpy(T->tag, "OS/2", 4);
    {
        int first = nmap ? (int)(map[0].cp > 0xFFFF ? 0xFFFF : map[0].cp) : 0;
        int last = 0;
        for (int i = 0; i < nmap; i++) if ((int)(map[i].cp > 0xFFFF ? 0xFFFF : map[i].cp) > last) last = (int)(map[i].cp > 0xFFFF ? 0xFFFF : map[i].cp);
        int strikeSize = f->has[PXM_STRIKEOUTTHICKNESS] ? f->m[PXM_STRIKEOUTTHICKNESS] * UPP : 0;
        int strikePos = f->has[PXM_STRIKEOUTPOSITION] ? (f->m[PXM_STRIKEOUTPOSITION] + 1) * UPP : 0;
        fw_be16(&T->b, 4); be16s(&T->b, nAdv ? (int)((sumAdv + nAdv / 2) / nAdv) : 0); fw_be16(&T->b, (unsigned)weight);
        fw_be16(&T->b, 5); fw_be16(&T->b, 4);    /* normal width; fsType as fontTools: preview & print */
        fw_buf_zero(&T->b, 16);                  /* sub- and superscript */
        be16s(&T->b, strikeSize); be16s(&T->b, strikePos); be16s(&T->b, 0);
        fw_buf_zero(&T->b, 10 + 16);             /* PANOSE, Unicode ranges */
        fw_buf_add(&T->b, "PXFE", 4);
        fw_be16(&T->b, (italic ? 1u : 0u) | (bold ? 0x20u : 0u) | (!italic && !bold ? 0x40u : 0u));
        fw_be16(&T->b, (unsigned)first); fw_be16(&T->b, (unsigned)last);
        be16s(&T->b, ascent * UPP); be16s(&T->b, -descent * UPP); be16s(&T->b, lineGap);
        fw_be16(&T->b, (unsigned)(ascent * UPP)); fw_be16(&T->b, (unsigned)(descent * UPP));
        fw_be32(&T->b, f->symbolic ? 0x80000000ul : 0); fw_be32(&T->b, 0);
        be16s(&T->b, f->has[PXM_XHEIGHT] ? f->m[PXM_XHEIGHT] * UPP : 0); be16s(&T->b, f->has[PXM_CAPHEIGHT] ? f->m[PXM_CAPHEIGHT] * UPP : 0);
        fw_be16(&T->b, 0); fw_be16(&T->b, 32); fw_be16(&T->b, 0);
    }
    /* hmtx */
    T = &t[nt++];
    memcpy(T->tag, "hmtx", 4);
    for (int g = 0; g < n; g++) { fw_be16(&T->b, (unsigned)s[g].advance); be16s(&T->b, s[g].o.nc ? s[g].o.xMin : 0); }
    /* cmap */
    T = &t[nt++];
    memcpy(T->tag, "cmap", 4);
    if (!cmap_table(&T->b, map, nmap, f->symbolic, err)) goto done;
    /* name */
    T = &t[nt++];
    memcpy(T->tag, "name", 4);
    {
        const char *text[15] = {meta->copyright, family, style, unique, full, version, ps, meta->trademark, meta->vendor,
                                meta->designer, meta->description, meta->vendorUrl, meta->designerUrl, meta->license, meta->licenseUrl};
        name_table(&T->b, text, warn, warnCap);
    }
    /* post */
    T = &t[nt++];
    memcpy(T->tag, "post", 4);
    fw_be32(&T->b, ttf ? 0x20000 : 0x30000); fw_be32(&T->b, 0);
    be16s(&T->b, f->has[PXM_UNDERLINEPOSITION] ? (f->m[PXM_UNDERLINEPOSITION] + 1) * UPP : 0);
    be16s(&T->b, f->has[PXM_UNDERLINETHICKNESS] ? f->m[PXM_UNDERLINETHICKNESS] * UPP : 0);
    fw_be32(&T->b, f->monospaced); fw_buf_zero(&T->b, 16);
    if (ttf) {
        fw_be16(&T->b, (unsigned)n);
        for (int g = 0; g < n; g++) fw_be16(&T->b, g ? 258u + (unsigned)g - 1 : 0);
        for (int g = 1; g < n; g++) { put8(&T->b, (unsigned)strlen(s[g].name)); fw_buf_add(&T->b, s[g].name, strlen(s[g].name)); }
    }
    /* kern */
    {
        int nk = 0, dropped = 0;
        for (int i = 0; i < f->kernCount; i++) {
            const PxKern *k = &f->kern[i];
            int l = gid_of(s, n, hasDefault, k->first), r = gid_of(s, n, hasDefault, k->second);
            if (!k->value) continue;
            if (l < 0 || r < 0) { dropped++; continue; }
            int v = k->value * UPP;
            kp[nk++] = (KernPair){l, r, v < -32768 ? -32768 : v > 32767 ? 32767 : v};
        }
        if (dropped) fw_warn(warn, warnCap, "%d kerning pair(s) of glyphs not written left out", dropped);
        if (nk > 10920) { fw_warn(warn, warnCap, "%d kerning pairs left out (a kern subtable holds 10920)", nk - 10920); nk = 10920; }
        if (nk) {
            qsort(kp, (size_t)nk, sizeof *kp, kern_cmp);
            T = &t[nt++];
            memcpy(T->tag, "kern", 4);
            fw_be16(&T->b, 0); fw_be16(&T->b, 1);
            fw_be16(&T->b, 0); fw_be16(&T->b, 14 + 6 * (unsigned)nk); fw_be16(&T->b, 1);   /* horizontal, format 0 */
            fw_be16(&T->b, (unsigned)nk); bsearch_header(&T->b, (unsigned)nk, 6);
            for (int i = 0; i < nk; i++) { fw_be16(&T->b, (unsigned)kp[i].l); fw_be16(&T->b, (unsigned)kp[i].r); be16s(&T->b, kp[i].v); }
        }
    }
    /* outlines */
    if (ttf) {
        T = &t[nt++];
        memcpy(T->tag, "loca", 4);
        for (int g = 0; g <= n; g++) if (longLoca) fw_be32(&T->b, (unsigned long)at[g]); else fw_be16(&T->b, (unsigned)(at[g] / 2));
        T = &t[nt++];
        memcpy(T->tag, "glyf", 4);
        T->b = glyf;
        glyf = (FwBuf){0};
    } else {
        T = &t[nt++];
        memcpy(T->tag, "CFF ", 4);
        if (!cff_table(&T->b, s, n, ps, full, family, style, bbox)) { fail(err, "out of memory"); goto done; }
        if (px_font_is_colour(f)) {   /* a colour font: the colours in an SVG table, the CFF outlines (all ink) the fallback */
            T = &t[nt++];
            memcpy(T->tag, "SVG ", 4);
            if (!svg_table(&T->b, f, s, n)) { fail(err, "out of memory"); goto done; }
        }
    }
    for (int i = 0; i < nt; i++) if (t[i].b.failed) { fail(err, "out of memory"); goto done; }

    unsigned long flavor = ttf ? 0x00010000ul : 0x4F54544Ful;   /* 'OTTO' */
    size_t sfntLen = 0;
    out = sfnt_assemble(t, nt, flavor, &sfntLen);
    if (out && fmt == FW_WOFF) {
        unsigned char *w = woff_wrap(t, nt, flavor, sfntLen, &sfntLen);
        free(out);
        out = w;
    }
    if (!out) fail(err, "out of memory");
    else *len = sfntLen;
done:
    fw_buf_free(&glyf);
    free(at);
    for (int i = 0; i < nt; i++) fw_buf_free(&t[i].b);
    if (s) for (int g = 0; g < n; g++) outl_free(&s[g].o);
    free(s); free(map); free(kp);
    fw_glyphs_free(&gl);
    return out;
}
