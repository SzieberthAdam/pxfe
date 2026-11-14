/* layers.c - reference layers of the glyph view: the stack, the glyph cache, the Layers tab and the Add Layer popover
 * (see layers.h and DESIGN.md "Layers"). */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layers.h"
#include "theme.h"

#define MAX_LAYERS 48      /* the whole stack: session layers and font layers */
#define MAX_CACHE 40
#define POP_W 400
#define LIST_ROWS_MAX 8
#define ROW_H 32

static void draw_pix(const PxGlyph *g, int sx, int sy, int scale, Color col);

typedef struct CacheFont {
    char pxfm[33];
    bool ok;            /* false: the store has no such font (not retried) */
    PxFont f;
    unsigned long used;
} CacheFont;

struct Layers {
    Layer v[MAX_LAYERS];
    int n, sel;
    Store *store;
    const PxFont *doc;
    int32_t editedCode;

    CacheFont cache[MAX_CACHE];
    int ncache;
    unsigned long tick;

    /* the number buttons that repeat while held, the slider being dragged */
    char dragId[32];

    /* the Add Layer popover */
    bool pop;
    bool justOpened;            /* the frame the popover opens: the click that opened it must not close it */
    int popSrc;                 /* 0 this font, 1 a font of the library, 2 a file */
    int popScope;               /* 0 this glyph only (session), 1 font layer (saved with the font) */
    bool scopeTouched;          /* the user chose the scope: a change of source leaves it */
    FlLayer *probe;             /* the file chosen in the popover (opened to show what it holds) */
    char fileMsg[200];
    char search[64];
    char glyphText[32];
    bool same;                  /* the same code as the edited glyph */
    int fontSel;                /* index into fonts.e, -1 none */
    int listScroll;
    StoreEntryList fonts;
    bool haveFonts;
    Store *fontsStore;
    int *order;                 /* the filtered, sorted entries */
    int nOrder;
    char orderKey[64];
    bool orderValid;
    Rectangle popRect;
    int nextColor;
    FlDoc *fdoc;                /* the font layers of the open font (not owned) */
    int fontPx;                 /* the edited font's pixel size, 0 = not computed this frame */
};

static bool (*fileDialog)(char *out, int size);
void Layers_SetFileDialog(bool (*fn)(char *out, int size)) { fileDialog = fn; }

static const Color kSwatch[8] = {TH_DANGER, TH_ACCENT, TH_M_CAP, TH_M_XHEIGHT, TH_M_UNDERLINE, TH_LAYER_PINK, TH_LAYER_TEAL, TH_TEXT};

static int imin(int a, int b) { return a < b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static Color rgb(unsigned v) { return (Color){(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, 255}; }
static unsigned rgbval(Color c) { return ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b; }
static Color withA(Color c, int a) { c.a = (unsigned char)clampi(a, 0, 255); return c; }

/* ------------------------------------------------------------------ the stack */

Layers *Layers_Create(void)
{
    Layers *l = (Layers *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->v[0].isCurrent = true;
    l->v[0].visible = true;
    l->v[0].opacity = 100;
    l->v[0].color = BLACK;
    l->n = 1;
    l->sel = 0;
    l->fontSel = -1;
    l->same = true;
    return l;
}

static void cache_free(CacheFont *c) { if (c->ok) px_font_free(&c->f); memset(c, 0, sizeof *c); }

void Layers_Destroy(Layers *l)
{
    if (!l) return;
    for (int i = 0; i < l->ncache; i++) cache_free(&l->cache[i]);
    for (int i = 0; i < l->n; i++) if (l->v[i].fl && !l->v[i].fontLevel) FlLayer_Free(l->v[i].fl);
    FlLayer_Free(l->probe);
    if (l->haveFonts) store_entry_list_free(&l->fonts);
    free(l->order);
    free(l);
}

void Layers_SetStore(Layers *l, Store *s)
{
    if (!l) return;
    if (s != l->store) {
        l->store = s;
        if (l->haveFonts) { store_entry_list_free(&l->fonts); l->haveFonts = false; }
        l->orderValid = false;
        l->nOrder = 0;
        l->fontSel = -1;
    }
}

void Layers_SetContext(Layers *l, const PxFont *doc, int32_t editedCode)
{
    if (!l) return;
    l->doc = doc;
    l->editedCode = editedCode;
    l->fontPx = 0;
}

static int font_px(Layers *l)
{
    if (!l->fontPx) {
        int a = 0, b = 0;
        if (l->doc) px_font_box(l->doc, &a, &b);
        l->fontPx = a + b < 1 ? 16 : a + b;
    }
    return l->fontPx;
}

static void touch(Layers *l) { if (l->fdoc) l->fdoc->rev++; }

/* the document's order is the stack's order of the font layers */
static void sync_doc(Layers *l)
{
    if (!l->fdoc) return;
    FlLayer *nv[FL_MAX];
    int k = 0;
    for (int i = 0; i < l->n && k < FL_MAX; i++) if (l->v[i].fontLevel) nv[k++] = l->v[i].fl;
    bool same = k == l->fdoc->n;
    for (int i = 0; same && i < k; i++) same = nv[i] == l->fdoc->v[i];
    if (same) return;
    for (int i = 0; i < k; i++) l->fdoc->v[i] = nv[i];
    l->fdoc->n = k;
    l->fdoc->rev++;
}

static void pull(Layer *a)   /* a font layer's own settings into the entry the rest of the program reads */
{
    const FsLayer *s = &a->fl->s;
    a->visible = s->visible != 0;
    a->color = (Color){(unsigned char)s->r, (unsigned char)s->g, (unsigned char)s->b, 255};
    a->opacity = s->opacity;
    a->dx = (int)lroundf(s->dx);
    a->dy = (int)lroundf(s->dy);
}

static void push(Layers *l, Layer *a)   /* and back: what the panel's controls changed */
{
    FsLayer *s = &a->fl->s;
    if (s->visible != (int)a->visible || s->r != a->color.r || s->g != a->color.g || s->b != a->color.b || s->opacity != a->opacity) {
        s->visible = a->visible;
        s->r = a->color.r; s->g = a->color.g; s->b = a->color.b;
        s->opacity = a->opacity;
        touch(l);
    }
}

static void entry_from(Layer *a, FlLayer *f, bool fontLevel)
{
    memset(a, 0, sizeof *a);
    a->fl = f;
    a->fontLevel = fontLevel;
    a->sameCode = true;
    if (f->s.kind == 0) snprintf(a->pxfm, sizeof a->pxfm, "%.32s", f->s.source);
    snprintf(a->fontName, sizeof a->fontName, "%.95s", f->s.label[0] ? f->s.label : "Font layer");
    pull(a);
}

void Layers_SetFontDoc(Layers *l, FlDoc *d)
{
    if (!l || d == l->fdoc) return;
    int w = 0, newSel = -1;
    for (int i = 0; i < l->n; i++) {   /* the old document's layers leave the stack (the document owns them) */
        if (l->v[i].fontLevel) continue;
        if (i == l->sel) newSel = w;
        if (w != i) l->v[w] = l->v[i];
        w++;
    }
    l->n = w;
    l->fdoc = d;
    for (int i = 0; d && i < d->n && l->n < MAX_LAYERS; i++) entry_from(&l->v[l->n++], d->v[i], true);
    int cur = Layers_CurrentIndex(l);
    l->sel = newSel >= 0 ? newSel : (cur >= 0 ? cur : 0);
}

int Layers_Count(const Layers *l) { return l ? l->n : 0; }
const Layer *Layers_Get(const Layers *l, int i) { return (l && i >= 0 && i < l->n) ? &l->v[i] : NULL; }
int Layers_Selected(const Layers *l) { return l ? l->sel : -1; }

int Layers_CurrentIndex(const Layers *l)
{
    for (int i = 0; l && i < l->n; i++) if (l->v[i].isCurrent) return i;
    return -1;
}

bool Layers_CurrentVisible(const Layers *l)
{
    int i = Layers_CurrentIndex(l);
    return i < 0 || l->v[i].visible;
}

void Layers_Nudge(Layers *l, int i, int dx, int dy)
{
    if (!l || i < 0 || i >= l->n || l->v[i].isCurrent) return;
    Layer *a = &l->v[i];
    if (a->fontLevel) {   /* a font layer's own offset (every glyph); the panel's per-glyph correction is separate */
        a->fl->s.dx = fminf(999.0f, fmaxf(-999.0f, a->fl->s.dx + (float)dx));
        a->fl->s.dy = fminf(999.0f, fmaxf(-999.0f, a->fl->s.dy + (float)dy));
        touch(l);
        return;
    }
    a->dx = clampi(a->dx + dx, -999, 999);
    a->dy = clampi(a->dy + dy, -999, 999);
}

/* a new entry on top of the stack (index 0), selected; the caller fills it */
static Layer *push_top(Layers *l)
{
    if (l->n >= MAX_LAYERS) return NULL;
    memmove(&l->v[1], &l->v[0], sizeof(Layer) * (size_t)l->n);
    l->n++;
    l->sel = 0;
    Layer *a = &l->v[0];
    memset(a, 0, sizeof *a);
    return a;
}

int Layers_Add(Layers *l, const char *pxfm, const char *fontName, bool sameCode, int32_t code)
{
    if (!l) return -1;
    Layer *a = push_top(l);
    if (!a) return -1;
    a->visible = true;
    a->color = kSwatch[l->nextColor++ % 8];
    a->opacity = 60;
    a->sameCode = sameCode;
    a->code = code;
    snprintf(a->pxfm, sizeof a->pxfm, "%s", pxfm ? pxfm : "");
    snprintf(a->fontName, sizeof a->fontName, "%s", fontName && fontName[0] ? fontName : "This font");
    return 0;
}

static int add_font_layer(Layers *l, FlLayer *f, bool fontLevel)
{
    Layer *a = push_top(l);
    if (!a || (fontLevel && (!l->fdoc || !FlDoc_Add(l->fdoc, f, 0)))) {
        if (a) { memmove(&l->v[0], &l->v[1], sizeof(Layer) * (size_t)(l->n - 1)); l->n--; l->sel = Layers_CurrentIndex(l); }
        FlLayer_Free(f);
        return -1;
    }
    entry_from(a, f, fontLevel);
    if (fontLevel) { sync_doc(l); touch(l); }
    return 0;
}

static void new_layer_defaults(Layers *l, FsLayer *s)
{
    *s = fs_layer_default();
    Color c = kSwatch[l->nextColor++ % 8];
    s->r = c.r; s->g = c.g; s->b = c.b;
    s->opacity = 60;
}

int Layers_AddDbFont(Layers *l, const char *pxfm, const char *fontName)
{
    if (!l || !l->fdoc || l->n >= MAX_LAYERS || l->fdoc->n >= FL_MAX) return -1;
    FsLayer s;
    new_layer_defaults(l, &s);
    s.kind = 0;
    snprintf(s.source, sizeof s.source, "%s", pxfm);
    snprintf(s.label, sizeof s.label, "%s", fontName && fontName[0] ? fontName : "Font");
    FlLayer *f = FlLayer_New(&s);
    return f ? add_font_layer(l, f, true) : -1;
}

int Layers_AddFile(Layers *l, const char *path, int face, bool fontLevel, char *err, int errn)
{
    if (err && errn) err[0] = 0;
    if (!l || l->n >= MAX_LAYERS || (fontLevel && (!l->fdoc || l->fdoc->n >= FL_MAX))) {
        if (err && errn) snprintf(err, (size_t)errn, "no room for another layer");
        return -1;
    }
    FsLayer s;
    new_layer_defaults(l, &s);
    s.kind = 1;
    snprintf(s.source, sizeof s.source, "%s", path);
    s.face = face;
    FlLayer *f = FlLayer_New(&s);
    if (!f) return -1;
    if (!FlLayer_Ready(f)) {
        if (err && errn) snprintf(err, (size_t)errn, "%s", f->err);
        FlLayer_Free(f);
        return -1;
    }
    FlLayer_Label(f, f->s.label, (int)sizeof f->s.label);
    f->metaChanged = false;
    return add_font_layer(l, f, fontLevel);
}

void Layers_Remove(Layers *l, int i)
{
    if (!l || i < 0 || i >= l->n || l->v[i].isCurrent) return;
    FlLayer *dead = l->v[i].fl;
    bool fontLevel = l->v[i].fontLevel;
    memmove(&l->v[i], &l->v[i + 1], sizeof(Layer) * (size_t)(l->n - i - 1));
    l->n--;
    if (l->sel == i) l->sel = imin(i, l->n - 1);
    else if (l->sel > i) l->sel--;
    if (fontLevel) { sync_doc(l); touch(l); }
    FlLayer_Free(dead);
}

void Layers_Move(Layers *l, int i, int dir)
{
    if (!l || i < 0 || i >= l->n) return;
    int j = i + (dir < 0 ? -1 : 1);
    if (j < 0 || j >= l->n) return;
    Layer t = l->v[i]; l->v[i] = l->v[j]; l->v[j] = t;
    if (l->sel == i) l->sel = j; else if (l->sel == j) l->sel = i;
    sync_doc(l);
}

bool Layers_PopoverOpen(const Layers *l) { return l && l->pop; }

/* ------------------------------------------------------------------ glyph source */

static int32_t layer_code(const Layers *l, const Layer *a)
{
    int32_t c = a->sameCode ? l->editedCode : a->code;
    if ((a->pxfm[0] || (a->fl && a->fl->s.kind == 1)) && a->sameCode && !px_code_is_codepoint(c)) return -1;   /* an uncoded code names a glyph only inside its font */
    return px_code_is_valid(c) ? c : -1;
}

static CacheFont *cache_get(Layers *l, const char *pxfm)
{
    for (int i = 0; i < l->ncache; i++)
        if (!strcmp(l->cache[i].pxfm, pxfm)) { l->cache[i].used = ++l->tick; return &l->cache[i]; }
    if (!l->store) return NULL;
    CacheFont *c;
    if (l->ncache < MAX_CACHE) c = &l->cache[l->ncache++];
    else {
        c = &l->cache[0];
        for (int i = 1; i < l->ncache; i++) if (l->cache[i].used < c->used) c = &l->cache[i];
        cache_free(c);
    }
    memset(c, 0, sizeof *c);
    snprintf(c->pxfm, sizeof c->pxfm, "%s", pxfm);
    c->used = ++l->tick;
    char err[PX_ERR_LEN] = "";
    int32_t none = 0;
    px_font_init(&c->f);
    c->ok = store_load_font(store_main_conn(l->store), pxfm, &none, 0, &c->f, err);   /* the glyph lines, no bitmap yet */
    if (!c->ok) px_font_free(&c->f);
    return c;
}

/* The glyph of a font of the library under a code, its bitmap loaded; NULL when the font has no such glyph. */
static const PxGlyph *font_glyph(Layers *l, const char *pxfm, int32_t code, const PxFont **src)
{
    CacheFont *c = cache_get(l, pxfm);
    if (!c || !c->ok) return NULL;
    int gi = px_font_find(&c->f, code);
    if (gi < 0) return NULL;
    PxGlyph *g = &c->f.glyphs[gi];
    if (g->lazy) {
        char err[PX_ERR_LEN] = "";
        store_load_glyph_bitmaps(store_main_conn(l->store), &c->f, &code, 1, err);
    }
    if (src) *src = &c->f;
    return g;
}

const PxGlyph *Layers_Glyph(Layers *l, int i, const PxFont **srcFont)
{
    if (srcFont) *srcFont = NULL;
    if (!l || i < 0 || i >= l->n || l->v[i].isCurrent) return NULL;
    const Layer *a = &l->v[i];
    int32_t code = layer_code(l, a);
    if (code < 0) return NULL;
    if (!a->pxfm[0]) {
        if (!l->doc) return NULL;
        int gi = px_font_find(l->doc, code);
        if (gi < 0) return NULL;
        if (srcFont) *srcFont = l->doc;
        return &l->doc->glyphs[gi];
    }
    return font_glyph(l, a->pxfm, code, srcFont);
}

/* a file layer's source is usable (opened; a reload of a changed file is noticed here) */
static bool layer_ready(Layers *l, Layer *a)
{
    if (!a->fl || a->fl->s.kind != 1) return true;
    bool ok = FlLayer_Ready(a->fl);
    if (a->fl->metaChanged) { a->fl->metaChanged = false; if (a->fontLevel) touch(l); }
    return ok;
}

static bool is_file(const Layer *a) { return a->fl && a->fl->s.kind == 1; }

/* where and how big layer a draws the glyph of `code` */
static void place(const Layer *a, int32_t code, FlPlace *p)
{
    if (a->fl && a->fontLevel) FlLayer_Place(a->fl, code, p);
    else {
        p->dx = (float)a->dx; p->dy = (float)a->dy; p->hidden = false;
        p->scale = a->fl ? a->fl->s.scale : 1.0f;
        if (is_file(a) && a->fl->state == FL_OK && !a->fl->outline) p->scale = (float)fl_bitmap_k(p->scale);
    }
}

/* -1 not in the source, 0 no ink, 1 ink */
static int layer_has(Layers *l, int i)
{
    Layer *a = &l->v[i];
    int32_t code = layer_code(l, a);
    if (code < 0) return -1;
    if (is_file(a)) return layer_ready(l, a) ? FlLayer_Has(a->fl, code, font_px(l)) : -1;
    const PxGlyph *g = Layers_Glyph(l, i, NULL);
    return !g ? -1 : g->w > 0;
}

void Layers_DrawGlyph(Layers *l, int i, int ox, int oy, int z)
{
    if (!l || i < 0 || i >= l->n || l->v[i].isCurrent) return;
    Layer *a = &l->v[i];
    int32_t code = layer_code(l, a);
    if (code < 0) return;
    Color col = withA(a->color, a->opacity * 255 / 100);
    if (is_file(a) && !layer_ready(l, a)) return;
    FlPlace p;
    place(a, code, &p);
    if (p.hidden) return;
    if (is_file(a)) {
        FlLayer_Draw(a->fl, code, ox, oy, z, font_px(l), &p, col);
        return;
    }
    const PxGlyph *g = Layers_Glyph(l, i, NULL);
    if (g) draw_pix(g, ox + (int)lroundf(p.dx) * z, oy - (int)lroundf(p.dy) * z, z, col);
}

int Layers_FoundCount(Layers *l, int i)
{
    if (l && i >= 0 && i < l->n && is_file(&l->v[i])) {
        int32_t code = layer_code(l, &l->v[i]);
        return code < 0 || !layer_ready(l, &l->v[i]) ? -1 : FlLayer_InkCount(l->v[i].fl, code, font_px(l));
    }
    const PxGlyph *g = Layers_Glyph(l, i, NULL);
    if (!g) return -1;
    int n = 0;
    for (int k = 0; g->pix && k < g->w * g->h; k++) n += g->pix[k] != '.';
    return n;
}

int Layers_CachedFonts(const Layers *l) { return l ? l->ncache : 0; }

/* ------------------------------------------------------------------ small drawing helpers */

static bool hov(Rectangle vis, Rectangle r)
{
    if (Th_InputBlocked()) return false;
    Vector2 m = GetMousePosition();
    return CheckCollisionPointRec(m, r) && CheckCollisionPointRec(m, vis);
}

static void code_label(int32_t cp, char *out, size_t n)
{
    if (px_code_is_uncoded(cp)) { snprintf(out, n, "#%d", cp - PX_CODE_UNCODED_FIRST); return; }
    if (!px_code_is_codepoint(cp)) { snprintf(out, n, "no code"); return; }
    snprintf(out, n, "U+%04X", (unsigned)cp);   /* drawn as a glyph caption by Th_TextRich */
}

static void draw_pix(const PxGlyph *g, int sx, int sy, int scale, Color col)
{
    if (!g || !g->pix || g->w <= 0) return;
    int top = -(g->y + g->h);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w;) {
            if (g->pix[y * g->w + x] == '.') { x++; continue; }
            int run = 1;
            while (x + run < g->w && g->pix[y * g->w + x + run] != '.') run++;
            DrawRectangle(sx + (g->x + x) * scale, sy + (top + y) * scale, run * scale, scale, col);
            x += run;
        }
}

/* ------------------------------------------------------------------ text fields and number widgets */

/* A horizontal slider; returns true when the value changed. */
static bool slider(Layers *l, Rectangle vis, const char *id, Rectangle r, int *v, int lo, int hi)
{
    Th_Reg(id, r);
    Rectangle hit = {r.x - 6, r.y - 5, r.width + 12, r.height + 10};
    bool h = hov(vis, hit);
    bool dragging = !strcmp(l->dragId, id);
    bool changed = false;
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { snprintf(l->dragId, sizeof l->dragId, "%s", id); dragging = true; }
    if (dragging) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { l->dragId[0] = 0; dragging = false; }
        else {
            float t = (GetMousePosition().x - r.x) / r.width;
            int nv = clampi((int)floorf(lo + t * (float)(hi - lo) + 0.5f), lo, hi);
            if (nv != *v) { *v = nv; changed = true; }
        }
    }
    float cy = r.y + r.height / 2;
    DrawRectangleRounded((Rectangle){r.x, cy - 2, r.width, 4}, 1.0f, 4, TH_LINE);
    float t = (float)(*v - lo) / (float)(hi - lo);
    DrawRectangleRounded((Rectangle){r.x, cy - 2, r.width * t, 4}, 1.0f, 4, TH_ACCENT);
    float kx = r.x + r.width * t;
    DrawCircle((int)kx, (int)cy + 1, 7.5f, Fade(BLACK, 0.09f));
    DrawCircle((int)kx, (int)cy, 7, TH_PANEL);
    DrawCircleLines((int)kx, (int)cy, 7, dragging || h ? TH_ACCENT : TH_TEXT_OFF);
    return changed;
}

static bool parse_hex(const char *s, Color *out)
{
    if (*s == '#') s++;
    size_t n = strlen(s);
    if (n != 6 && n != 3) return false;
    for (size_t i = 0; i < n; i++) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return false;
    unsigned v;
    if (n == 3) {
        char t[7] = {s[0], s[0], s[1], s[1], s[2], s[2], 0};
        v = (unsigned)strtoul(t, NULL, 16);
    } else v = (unsigned)strtoul(s, NULL, 16);
    *out = rgb(v);
    return true;
}

/* ------------------------------------------------------------------ the Layers tab */

#define CARD_HEAD 48
#define CARD_BODY 156

static void open_add(Layers *l)
{
    l->pop = true;
    l->justOpened = true;
    l->popSrc = 0;
    l->popScope = 0;
    l->scopeTouched = false;
    l->fileMsg[0] = 0;
    FlLayer_Free(l->probe);
    l->probe = NULL;
    l->fontSel = -1;
    l->orderValid = false;
    l->search[0] = 0;
    l->glyphText[0] = 0;   /* the "same code" choice is remembered */
    Th_TextBlur();
}

/* the height of the details of the selected layer */
static int body_h(const Layers *l, const Layer *a)
{
    int h = CARD_BODY;
    if (is_file(a)) h += 32;
    if (a->fontLevel) {
        h += 24;
        h += px_code_is_codepoint(l->editedCode) ? 64 + (is_file(a) ? 32 : 0) + 36 : 24;
    }
    return h;
}

/* a decimal number box: the buttons step (quarter pixels for an outline layer, whole pixels for the others), any value can be typed */
static bool fstep(Rectangle r, const char *prefix, float *v, float step, float lo, float hi)
{
    float nv;
    if (!Th_NumberF(r, prefix, *v, lo, hi, step, 0, &nv)) return false;
    *v = nv;
    return true;
}

/* the size of a file layer: an outline in percent of the natural size, a bitmap in whole multiples of the strike */
static bool size_number(const FlLayer *f, Rectangle r, const char *prefix, float *scale)
{
    bool bitmap = f->state == FL_OK && !f->outline;
    int cur = bitmap ? fl_bitmap_k(*scale) : (int)lroundf(*scale * 100.0f), nv;
    if (!Th_Number(r, prefix, cur, bitmap ? 1 : 10, bitmap ? FL_MAX_K : 1000, 0, &nv)) return false;
    *scale = bitmap ? (float)nv : (float)nv / 100.0f;
    return true;
}

float Layers_DrawPanel(Layers *l, Rectangle area, Rectangle vis)
{
    float px = area.x + 12, pw = area.width - 24;
    float y = area.y + 6;
    if (!l) return 0;

    /* header: the title and the Add Layer button (with no reference layer yet the empty state has the big button) */
    float hy = y, bw = Th_ButtonW("+ Add Layer");
    Th_Section("LAYERS", px, &y, pw - (l->n > 1 ? bw + TH_GAP : 0), 0, NULL);
    if (l->n > 1) {
        Rectangle br = {px + pw - bw, hy - 2, bw, TH_ROW_H};
        if (l->n < MAX_LAYERS) {
            if (Th_Button(br, "+ Add Layer", 0, NULL)) open_add(l);
        } else Th_Button(br, "+ Add Layer", TH_BTN_DISABLED, NULL);
    }
    y = hy + TH_ROW_H + TH_GAP;

    int opKind = 0, opIdx = -1;   /* a reorder or a removal, applied after the loop */
    for (int i = 0; i < l->n; i++) {
        Layer *a = &l->v[i];
        bool selected = i == l->sel;
        bool cur = a->isCurrent;
        if (a->fl && a->fontLevel) pull(a);
        float h = CARD_HEAD + (selected && !cur ? body_h(l, a) : 0);
        Rectangle card = {px, y, pw, h};
        int has = cur ? 1 : layer_has(l, i);
        bool missing = !cur && has < 0;
        bool failed = is_file(a) && a->fl->state == FL_FAILED;
        char nm[40];

        /* card */
        Th_Card(card, (selected ? TH_LR_SELECTED : 0) | (cur ? TH_LR_WITHIN : 0) | (missing ? TH_LR_DISABLED : 0));
        Rectangle head = {card.x, card.y, card.width, CARD_HEAD};
        snprintf(nm, sizeof nm, "lay_row_%d", i);
        Th_Reg(nm, head);

        /* the controls of the header (they take the click before the row) */
        Rectangle eye = {card.x + 8, card.y + (CARD_HEAD - TH_ROW_H) / 2, TH_ROW_H, TH_ROW_H};
        Rectangle bx = {card.x + card.width - 8 - TH_ROW_H, eye.y, TH_ROW_H, TH_ROW_H};
        Rectangle bd = {bx.x - TH_ROW_H, eye.y, TH_ROW_H, TH_ROW_H}, bu = {bd.x - TH_ROW_H, eye.y, TH_ROW_H, TH_ROW_H};
        bool ctl = hov(vis, eye) || hov(vis, bu) || hov(vis, bd) || (!cur && hov(vis, bx));
        snprintf(nm, sizeof nm, "lay_eye_%d", i);
        if (Th_Button(eye, nm, TH_BTN_ICON(a->visible ? TH_IC_EYE : TH_IC_EYE_OFF), a->visible ? "Hide this layer" : "Show this layer")) a->visible = !a->visible;
        snprintf(nm, sizeof nm, "lay_up_%d", i);
        if (Th_Button(bu, nm, TH_BTN_ICON(TH_IC_UP) | TH_BTN_OFF(i > 0), "Move up")) { opKind = 1; opIdx = i; }
        snprintf(nm, sizeof nm, "lay_down_%d", i);
        if (Th_Button(bd, nm, TH_BTN_ICON(TH_IC_DOWN) | TH_BTN_OFF(i + 1 < l->n), "Move down")) { opKind = 2; opIdx = i; }
        if (!cur) {
            snprintf(nm, sizeof nm, "lay_del_%d", i);
            if (Th_Button(bx, nm, TH_BTN_ICON(TH_IC_CLOSE) | TH_BTN_DANGER, "Remove this layer")) { opKind = 3; opIdx = i; }
        }

        /* colour chip, name and detail line */
        float tx = eye.x + eye.width + 8;
        if (!cur) {
            Rectangle chip = {tx, card.y + CARD_HEAD / 2 - 6, 12, 12};
            DrawRectangleRounded(chip, 0.3f, 4, a->visible ? withA(a->color, 255 * a->opacity / 100 < 40 ? 40 : 255 * a->opacity / 100) : TH_LINE);
            DrawRectangleRoundedLinesEx(chip, 0.3f, 4, 1, withA(TH_TEXT, 60));
            tx += 20;
        }
        float tw = bu.x - tx - 4;
        char line1[160], line2[160], fit[200];
        Color c2 = TH_TEXT_DIM;
        if (cur) {
            snprintf(line1, sizeof line1, "Current glyph");
            char cl[40];
            code_label(l->editedCode, cl, sizeof cl);
            snprintf(line2, sizeof line2, "%s", cl);
        } else {
            snprintf(line1, sizeof line1, "%s", a->fontName);
            int32_t c = layer_code(l, a);
            char cl[40] = "";
            if (c >= 0) code_label(c, cl, sizeof cl);
            FlPlace pl;
            place(a, c >= 0 ? c : 0, &pl);
            if (failed) { snprintf(line2, sizeof line2, "%s", a->fl->err); c2 = TH_TEXT_NOTE; }
            else if (missing) snprintf(line2, sizeof line2, "%s%snot in font", cl, cl[0] ? " \xC2\xB7 " : "");
            else if (a->fontLevel && pl.hidden) snprintf(line2, sizeof line2, "%s \xC2\xB7 hidden for this glyph", cl);
            else if (has == 0) snprintf(line2, sizeof line2, "%s \xC2\xB7 no ink", cl);
            else snprintf(line2, sizeof line2, "%s%s", cl, a->sameCode ? " \xC2\xB7 same code" : "");
        }
        Color c1 = (missing || !a->visible) ? TH_TEXT_DIM : TH_TEXT;
        float tagW = a->fontLevel ? Th_MeasureSmall("FONT").x + 12 : 0;
        Th_FitText(line1, TH_F_BODY, tw - (tagW ? tagW + 6 : 0), fit, sizeof fit);
        Th_Text(fit, tx, card.y + 4, c1);
        if (tagW) {   /* the tag of a font layer: it belongs to the font, not to this session */
            Rectangle tg = {tx + Th_Measure(fit).x + 6, card.y + 6, tagW, 18};
            DrawRectangleRounded(tg, 0.3f, 4, TH_ACCENT_TINT);
            Th_TextSmall("FONT", tg.x + 6, tg.y + 2, TH_ACCENT);
        }
        Th_FitText(line2, TH_F_SMALL | TH_F_RICH, tw, fit, sizeof fit);
        Th_TextRich(fit, tx, card.y + 27, c2, TH_F_SMALL, hov(vis, head));

        /* a click on the row selects it */
        if (!ctl && hov(vis, head) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) l->sel = i;

        /* the details of the selected layer */
        if (selected && !cur) {
            float bxo = card.x + 12, by = card.y + CARD_HEAD;
            Th_HLine(card.x + 8, card.x + card.width - 8, by, TH_LINE_SOFT);
            by += 10;
            /* colour: swatches and hex */
            for (int k = 0; k < 8; k++) {
                Rectangle sw = {bxo + k * 24, by, 20, 20};
                char sn[24];
                snprintf(sn, sizeof sn, "lay_sw_%d", k);
                Th_Reg(sn, sw);
                bool hv = hov(vis, sw);
                bool on = rgbval(a->color) == rgbval(kSwatch[k]);
                DrawRectangleRounded(sw, 0.25f, 6, kSwatch[k]);
                if (on) DrawRectangleRoundedLinesEx((Rectangle){sw.x - 2, sw.y - 2, sw.width + 4, sw.height + 4}, 0.25f, 6, 2, TH_ACCENT);
                else if (hv) DrawRectangleRoundedLinesEx(sw, 0.25f, 6, 2, withA(TH_TEXT, 90));
                if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) a->color = kSwatch[k];
            }
            {
                char hx[12];
                snprintf(hx, sizeof hx, "#%06X", rgbval(a->color));
                Rectangle hr = {bxo + 8 * 24 + 4, by - 4, card.width - 24 - 8 * 24 - 4, TH_ROW_H};
                int f = Th_TextField(hr, "lay_hex", hx, 12, TH_TF_HEX | TH_TF_COMMIT | TH_TF_SELECT, "#RRGGBB");
                if (f & (TH_TF_ENTER | TH_TF_BLUR)) { Color nc; if (parse_hex(Th_TextValue(), &nc)) a->color = nc; }
            }
            by += 36;
            float rw = card.width - 24;
            Rectangle vr = Th_Row(bxo, &by, rw, "Opacity", NULL, 0);
            {
                int nv = a->opacity;
                Rectangle sr = {vr.x, vr.y + 6, vr.width - 96 - TH_GAP, 16};
                if (slider(l, vis, "lay_op", sr, &nv, 0, 100)) a->opacity = nv;
                if (Th_Number((Rectangle){vr.x + vr.width - 96, vr.y, 96, TH_ROW_H}, "lay_opv", a->opacity, 0, 100, 0, &nv)) a->opacity = nv;
            }
            int nv;
            bool outl = is_file(a) && a->fl->state == FL_OK && a->fl->outline;
            if (a->fontLevel) {   /* an offset in pixels of the edited font (quarter pixels for an outline) */
                FsLayer *s = &a->fl->s;
                float st = outl ? 0.25f : 1.0f;
                vr = Th_Row(bxo, &by, rw, "Offset X", NULL, 0);
                if (fstep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_x", &s->dx, st, -999, 999)) touch(l);
                vr = Th_Row(bxo, &by, rw, "Offset Y", NULL, 0);
                if (fstep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_y", &s->dy, st, -999, 999)) touch(l);
            } else {
                vr = Th_Row(bxo, &by, rw, "Offset X", NULL, 0);
                if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_x", a->dx, -999, 999, 0, &nv)) a->dx = nv;
                vr = Th_Row(bxo, &by, rw, "Offset Y", NULL, 0);
                if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_y", a->dy, -999, 999, 0, &nv)) a->dy = nv;
            }
            if (is_file(a)) {
                bool bitmap = a->fl->state == FL_OK && !a->fl->outline;
                if (!bitmap && a->fl->s.em > 0) {   /* an absolute size (an imported font's source): pixels per em, in quarter pixels */
                    vr = Th_Row(bxo, &by, rw, "Size (px/em)", NULL, 0);
                    if (fstep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_size", &a->fl->s.em, 0.25f, 2, 400) && a->fontLevel) touch(l);
                } else {
                    vr = Th_Row(bxo, &by, rw, bitmap ? "Size (x)" : "Size (%)", NULL, 0);
                    if (size_number(a->fl, (Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_size", &a->fl->s.scale) && a->fontLevel) touch(l);
                }
            }
            if (a->fontLevel) {   /* the correction of the edited glyph */
                Th_Section("THIS GLYPH", bxo, &by, rw, 0, NULL);
                int32_t code = l->editedCode;
                if (!px_code_is_codepoint(code)) {
                    Th_TextSmall("A glyph without a code point has no corrections.", bxo, by, TH_TEXT_DIM);
                } else {
                    const FlCorr *old = FlLayer_Corr(a->fl, code);
                    FlCorr c = old ? *old : (FlCorr){code, 0, 0, 1.0f, 0};
                    bool ch = false;
                    float st = outl ? 0.25f : 1.0f;
                    vr = Th_Row(bxo, &by, rw, "Offset X", NULL, 0);
                    ch |= fstep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_gx", &c.dx, st, -999, 999);
                    vr = Th_Row(bxo, &by, rw, "Offset Y", NULL, 0);
                    ch |= fstep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_gy", &c.dy, st, -999, 999);
                    if (is_file(a)) {
                        bool bitmap = a->fl->state == FL_OK && !a->fl->outline;
                        vr = Th_Row(bxo, &by, rw, bitmap ? "Size (x)" : "Size (%)", NULL, 0);
                        ch |= size_number(a->fl, (Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "lay_gsize", &c.dsize);
                    }
                    bool hide = c.hidden != 0;
                    if (Th_Check((Rectangle){bxo, by, 170, TH_ROW_H}, "Hide for this glyph", &hide, 0, "lay_ghide")) { c.hidden = hide; ch = true; }
                    Rectangle rr = {bxo + rw - 88, by, 88, TH_ROW_H};
                    Th_Reg("lay_greset", rr);
                    if (Th_Button(rr, "Reset", TH_BTN_OFF(old != NULL), "Remove the correction of this glyph")) { c = (FlCorr){code, 0, 0, 1.0f, 0}; ch = true; }
                    if (ch) { FlLayer_SetCorr(a->fl, code, &c); touch(l); }
                }
            }
        }
        if (a->fl && a->fontLevel) push(l, a);
        y += h + 6;
    }
    if (opKind == 1) Layers_Move(l, opIdx, -1);
    else if (opKind == 2) Layers_Move(l, opIdx, 1);
    else if (opKind == 3) Layers_Remove(l, opIdx);
    y += 4;
    if (l->n == 1) {
        /* the empty state: what layers are for, and a prominent way to add the first one */
        Rectangle box = {px, y, pw, 168};
        Th_Card(box, TH_LR_DISABLED);
        float ty = box.y + 14;
        Th_Text("No reference layers yet", box.x + 14, ty, TH_TEXT);
        ty += 28;
        Th_TextSmall("Layers show other glyphs under or over", box.x + 14, ty, TH_TEXT_DIM);
        ty += 19;
        Th_TextSmall("the one you edit: compare shapes, or trace", box.x + 14, ty, TH_TEXT_DIM);
        ty += 19;
        Th_TextSmall("over a glyph from this or another font.", box.x + 14, ty, TH_TEXT_DIM);
        if (Th_Button((Rectangle){box.x + 14, box.y + box.height - 14 - TH_ROW_H, box.width - 28, TH_ROW_H}, "+ Add Layer", TH_BTN_PRIMARY, NULL)) open_add(l);
        y += box.height + 12;
    } else {
        Th_TextSmall("Alt+drag in the canvas moves the selected layer.", px + 4, y, TH_TEXT_DIM);
        y += 20;
    }
    Th_TextSmall("Only FONT layers are saved, beside the database.", px + 4, y, TH_TEXT_DIM);
    y += 24;
    return y - area.y;
}

/* ------------------------------------------------------------------ the Add Layer popover */

void Layers_PopoverClose(Layers *l) { if (l && l->pop) { l->pop = false; FlLayer_Free(l->probe); l->probe = NULL; Th_TextBlur(); } }

static bool ci_has(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl) return true;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return true;
    }
    return false;
}

static void entry_name(const StoreEntry *e, char *out, size_t cap)
{
    if (e->family[0] && e->style[0]) snprintf(out, cap, "%s %s", e->family, e->style);
    else if (e->family[0]) snprintf(out, cap, "%s", e->family);
    else if (e->fullname[0]) snprintf(out, cap, "%s", e->fullname);
    else out[0] = 0;   /* the row shows the hash */
}

static const StoreEntryList *sort_src;
static int cmp_entry(const void *a, const void *b)
{
    char x[200], y[200];
    entry_name(&sort_src->e[*(const int *)a], x, sizeof x);
    entry_name(&sort_src->e[*(const int *)b], y, sizeof y);
    int c = strcmp(x, y);
    return c ? c : strcmp(sort_src->e[*(const int *)a].pxfm, sort_src->e[*(const int *)b].pxfm);
}

static void refresh_fonts(Layers *l)
{
    if (!l->store) return;
    if (!l->haveFonts) {
        char err[PX_ERR_LEN] = "";
        if (store_list_fonts(store_main_conn(l->store), &l->fonts, err)) l->haveFonts = true;
        l->orderValid = false;
    }
    if (!l->haveFonts) { l->nOrder = 0; return; }
    if (!l->orderValid || strcmp(l->orderKey, l->search)) {
        free(l->order);
        l->order = (int *)malloc(sizeof(int) * (size_t)(l->fonts.count > 0 ? l->fonts.count : 1));
        l->nOrder = 0;
        for (int i = 0; l->order && i < l->fonts.count; i++) {
            const StoreEntry *e = &l->fonts.e[i];
            char nm[200], all[260];
            entry_name(e, nm, sizeof nm);
            snprintf(all, sizeof all, "%s %.8s", nm, e->pxfm);
            if (ci_has(all, l->search)) l->order[l->nOrder++] = i;
        }
        sort_src = &l->fonts;
        if (l->order) qsort(l->order, (size_t)l->nOrder, sizeof(int), cmp_entry);
        snprintf(l->orderKey, sizeof l->orderKey, "%s", l->search);
        l->orderValid = true;
        l->listScroll = 0;
    }
}

/* the code typed in the glyph field: a character, U+0041, 0x41, 41 (hex), #3 (the fourth glyph without a code point) */
static bool parse_glyph(const char *in, int32_t *out)
{
    char s[40];
    snprintf(s, sizeof s, "%s", in);
    char *p = s;
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n && p[n - 1] == ' ') p[--n] = 0;
    if (!n) return false;
    if (p[0] == '#' && n > 1) {
        char *end;
        long v = strtol(p + 1, &end, 10);
        if (*end || v < 0 || v > 0xFFFF) return false;
        *out = PX_CODE_UNCODED_FIRST + (int32_t)v;
        return true;
    }
    int cnt = 0, first = 0;
    for (const char *q = p; *q;) {
        int b = 0;
        int cp = GetCodepointNext(q, &b);
        if (b <= 0) return false;
        if (!cnt) first = cp;
        cnt++;
        q += b;
    }
    if (cnt == 1) { *out = first; return px_code_is_codepoint(first); }
    const char *h = p;
    if ((h[0] == 'U' || h[0] == 'u') && h[1] == '+') h += 2;
    else if (h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h += 2;
    if (!*h || strlen(h) > 6) return false;
    for (const char *q = h; *q; q++) if (!((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f') || (*q >= 'A' && *q <= 'F'))) return false;
    long v = strtol(h, NULL, 16);
    if (v < 0 || v > PX_CODE_MAX_CODEPOINT) return false;
    *out = (int32_t)v;
    return true;
}

/* a two-letter sample of a library font, in the list */
static void draw_sample(Layers *l, const char *pxfm, Rectangle r)
{
    CacheFont *c = cache_get(l, pxfm);
    if (!c || !c->ok) return;
    int above, below;
    px_font_box(&c->f, &above, &below);
    int H = above + below;
    if (H < 1) return;
    int scale = clampi((int)(r.height - 6) / H, 1, 3);
    int baseY = (int)(r.y + (r.height - H * scale) / 2) + above * scale;
    float x = r.x + 2;
    static const int32_t codes[2] = {'A', 'g'};
    for (int k = 0; k < 2; k++) {
        int gi = px_font_find(&c->f, codes[k]);
        if (gi < 0) continue;
        PxGlyph *g = &c->f.glyphs[gi];
        if (g->lazy) { char err[PX_ERR_LEN] = ""; store_load_glyph_bitmaps(store_main_conn(l->store), &c->f, &codes[k], 1, err); }
        if ((int)x + (g->x + g->w) * scale > r.x + r.width) break;
        draw_pix(g, (int)x, baseY, scale, TH_TEXT);
        x += (float)((g->advance > 0 ? g->advance : g->w) + 1) * scale;
    }
}

/* the file chosen in the popover (by the dialog, or by a script): opened at once, to show what it holds */
bool Layers_PopoverSetFile(Layers *l, const char *path)
{
    if (!l) return false;
    FlLayer_Free(l->probe);
    l->probe = NULL;
    l->fileMsg[0] = 0;
    FsLayer s = fs_layer_default();
    s.kind = 1;
    snprintf(s.source, sizeof s.source, "%s", path);
    l->probe = FlLayer_New(&s);
    if (!l->probe) return false;
    if (FlLayer_Ready(l->probe)) return true;
    snprintf(l->fileMsg, sizeof l->fileMsg, "%s", l->probe->err);
    return false;
}

static int probe_idx(const FlLayer *f) { return f->s.face >= FL_BM_FLAG ? f->s.face - FL_BM_FLAG : f->s.face; }

static const char *outline_kind(const FlLayer *f)
{
    const char *fm = outline_format(f->ol);
    if (!strcmp(fm, "ttf")) return "TrueType outlines";
    if (!strcmp(fm, "otf")) return "OpenType CFF outlines";
    if (!strcmp(fm, "ttc")) return "TrueType collection";
    if (!strcmp(fm, "woff")) return "WOFF outlines";
    if (!strcmp(fm, "eot")) return "EOT outlines";
    if (!strcmp(fm, "pfb")) return "Type 1 outlines";
    return "Outlines";
}

void Layers_DrawPopover(Layers *l, Rectangle panel)
{
    if (!l || !l->pop) return;
    bool other = l->popSrc == 1, file = l->popSrc == 2;
    bool canFontLayer = l->fdoc && l->fdoc->n < FL_MAX && l->popSrc != 0;
    FlLayer *pr = l->probe;
    bool probeOk = file && pr && pr->state == FL_OK;
    bool showFace = probeOk && pr->nFaces > 1, showMode = probeOk && pr->canOutline && pr->canBitmap;
    float W = POP_W;
    int sh = GetScreenHeight();
    int LIST_ROWS = clampi((sh - 470) / ROW_H, 3, LIST_ROWS_MAX);
    float H = (other ? 34 + LIST_ROWS * ROW_H + 12 : 0) + (file ? 34 + 24 + (showFace ? 34 : 0) + (showMode ? 34 : 0) + 8 : 0) + 268 + 62;
    Rectangle p = {panel.x - W - 10, panel.y + 40, W, H};
    if (p.x < 8) p.x = 8;
    if (p.y + p.height > sh - 8) p.y = (float)(sh - 8) - p.height;
    if (p.y < 8) p.y = 8;
    l->popRect = p;
    Th_Raised(p, 1);
    Rectangle vis = p;
    float x = p.x + 16, w = p.width - 32, y = p.y + 14;
    Th_Reg("lay_pop", p);

    Th_TextHead("Add Layer", x, y - 4, TH_TEXT);
    if (Th_Button((Rectangle){p.x + p.width - 38, y - 6, TH_ROW_H, TH_ROW_H}, "lay_pop_close", TH_BTN_ICON(TH_IC_CLOSE), "Close")) { Layers_PopoverClose(l); return; }
    y += 32;

    /* source */
    Th_Section("FONT", x, &y, w, 0, NULL);
    {
        static const char *labels[3] = {"This font", "Library font", "File"};
        int c = Th_Segmented((Rectangle){x, y, w, TH_ROW_H}, &(ThSeg){ labels, 3, l->popSrc, fileDialog ? 0u : 4u, "lay_src" });
        if (c >= 0 && c != l->popSrc) {
            l->popSrc = c;
            if (c == 0) l->popScope = 0;
            else if (!l->scopeTouched) l->popScope = (c == 2 && l->fdoc) ? 1 : 0;
            Th_TextBlur();
            l->listScroll = 0;
            return;
        }
    }
    y += TH_ROW_H + 10;

    if (other) {
        refresh_fonts(l);
        if (Th_TextField((Rectangle){x, y, w, TH_ROW_H}, "lay_search", l->search, (int)sizeof l->search, TH_TF_SEARCH, "Search fonts by name or hash") & TH_TF_CHANGED) snprintf(l->search, sizeof l->search, "%s", Th_TextValue());
        y += TH_ROW_H + 6;
        Rectangle lr = {x, y, w, LIST_ROWS * ROW_H};
        DrawRectangleRec(lr, TH_PANEL_ALT);
        Th_RectLines(lr, TH_LINE);
        Th_Reg("lay_list", lr);
        if (!l->store) Th_Text("No library is open.", lr.x + 10, lr.y + 10, TH_TEXT_DIM);
        else if (!l->nOrder) Th_Text(l->fonts.count ? "No font matches." : "The library has no fonts.", lr.x + 10, lr.y + 10, TH_TEXT_DIM);
        Th_ScrollRows(lr, &l->listScroll, l->nOrder, LIST_ROWS, ROW_H, true);
        for (int k = 0; k < LIST_ROWS; k++) {
            int oi = l->listScroll + k;
            if (oi >= l->nOrder) break;
            int ei = l->order[oi];
            const StoreEntry *e = &l->fonts.e[ei];
            Rectangle rr = {lr.x + 1, lr.y + k * ROW_H, lr.width - 1 - TH_SB_W, ROW_H};
            char rn[24];
            snprintf(rn, sizeof rn, "lay_font_%d", k);
            Th_Reg(rn, rr);
            bool hv = hov(vis, rr);
            bool on = l->fontSel == ei;
            Th_ListRow(rr, (on ? TH_LR_SELECTED : 0) | (hv ? TH_LR_HOVER : 0));
            draw_sample(l, e->pxfm, (Rectangle){rr.x + 8, rr.y, 56, rr.height});
            char nm[200], fit[200];
            entry_name(e, nm, sizeof nm);
            float hw = Th_HashShort("Font", e->pxfm, rr.x + rr.width - 8, rr.y + (ROW_H - Th_Measure("M").y) / 2, TH_HASH_RIGHT, NULL);
            Th_FitText(nm, TH_F_BODY, rr.width - 72 - hw - 18, fit, sizeof fit);
            Th_Text(fit, rr.x + 72, rr.y + (ROW_H - Th_Measure(fit).y) / 2, on ? TH_ACCENT : TH_TEXT);
            if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) l->fontSel = ei;
        }
        y += lr.height + 12;
    }

    if (file) {   /* the file: its name, the choice button, what it holds, the face or strike */
        float bw = Th_ButtonW("Choose\xE2\x80\xA6") + 8;
        Th_TextField((Rectangle){x, y, w - bw - TH_GAP, TH_ROW_H}, "lay_file", pr ? pr->s.source : "", 0, TH_TF_READONLY, "No file chosen");
        Th_Reg("lay_choose", (Rectangle){x + w - bw, y, bw, TH_ROW_H});
        if (Th_Button((Rectangle){x + w - bw, y, bw, TH_ROW_H}, "Choose\xE2\x80\xA6", 0, "Choose a font file: ttf, otf, ttc, woff, eot, pfb, bdf, pcf, fnt, fon, json")) {
            char path[FS_PATH_LEN] = "";
            if (fileDialog && fileDialog(path, (int)sizeof path)) Layers_PopoverSetFile(l, path);
            return;
        }
        y += 34;
        char info[260], fit[260];
        Color ic = TH_TEXT_DIM;
        if (probeOk) {
            char lb[160];
            FlLayer_Label(pr, lb, sizeof lb);
            if (pr->outline) snprintf(info, sizeof info, "%s \xC2\xB7 %s", lb, outline_kind(pr));
            else snprintf(info, sizeof info, "%s \xC2\xB7 bitmap font", lb);
        } else if (pr) { snprintf(info, sizeof info, "%s", l->fileMsg[0] ? l->fileMsg : "cannot read"); ic = TH_TEXT_NOTE; }
        else snprintf(info, sizeof info, "Choose a font file.");
        Th_FitText(info, TH_F_SMALL, w, fit, sizeof fit);
        Th_TextSmall(fit, x, y, ic);
        y += 24;
        if (showFace) {
            bool bitmapMode = !pr->outline;
            const char *ex = strrchr(pr->s.source, '.');
            const char *what = !bitmapMode ? "Face" : (ex && tolower((unsigned char)ex[1]) == 'f' && tolower((unsigned char)ex[2]) == 'o' && tolower((unsigned char)ex[3]) == 'n' && !ex[4] ? "Font" : "Strike");
            Rectangle vr = Th_Row(x, &y, w, what, NULL, 0);
            char t[24];
            int idx = probe_idx(pr);
            snprintf(t, sizeof t, "%d / %d", idx + 1, pr->nFaces);
            int st = Th_Stepper((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, t, "lay_face", (idx <= 0 ? TH_STEP_NO_MINUS : 0) | (idx + 1 >= pr->nFaces ? TH_STEP_NO_PLUS : 0));
            if (st) { FlLayer_SetFace(pr, (bitmapMode && pr->canOutline ? FL_BM_FLAG : 0) + idx + st); return; }
            y += 2;
        }
        if (showMode) {
            static const char *ml[2] = {"Outlines", "Bitmap strikes"};
            int c = Th_Segmented((Rectangle){x, y, w, TH_ROW_H}, &(ThSeg){ ml, 2, pr->outline ? 0 : 1, 0, "lay_mode" });
            if (c >= 0 && c != (pr->outline ? 0 : 1)) { FlLayer_SetFace(pr, c == 1 ? FL_BM_FLAG : 0); return; }
            y += 34;
        }
        y += 8;
    }

    /* scope: a session layer (this glyph only, kept while the program runs) or a font layer (saved with the font) */
    {
        static const char *sl[2] = {"This glyph only", "Font layer"};
        Th_Section("LAYER", x, &y, w, 0, l->popScope == 1 ? "saved beside the database" : "session only");
        int c = Th_Segmented((Rectangle){x, y, w, TH_ROW_H}, &(ThSeg){ sl, 2, l->popScope, canFontLayer ? 0u : 2u, "lay_scope" });
        if (c >= 0 && c != l->popScope) { l->popScope = c; l->scopeTouched = true; Th_TextBlur(); return; }
        y += TH_ROW_H + 10;
    }
    bool fontScope = l->popScope == 1 && canFontLayer;

    /* glyph */
    Th_Section("GLYPH", x, &y, w, 0, NULL);
    int32_t code = -1;
    bool codeOk = l->same || fontScope;
    if (fontScope) {
        Th_TextSmall("A font layer follows the edited glyph:", x, y + 4, TH_TEXT_DIM);
        Th_TextSmall("it offers this font's code for every glyph.", x, y + 24, TH_TEXT_DIM);
        y += 56;
    } else {
        if (Th_Check((Rectangle){x, y, w, TH_ROW_H}, "Same code as the edited glyph", &l->same, 0, "lay_same")) Th_TextBlur();
        y += TH_ROW_H + 2;
        Rectangle fr = {x, y, 168, TH_ROW_H};
        if (Th_TextField(fr, "lay_glyph", l->glyphText, (int)sizeof l->glyphText, l->same ? TH_TF_DISABLED : 0, "A, U+0041 or 41") & TH_TF_CHANGED) snprintf(l->glyphText, sizeof l->glyphText, "%s", Th_TextValue());
        if (!l->same) codeOk = parse_glyph(l->glyphText, &code);
        char info[64] = "";
        if (l->same) snprintf(info, sizeof info, "Follows the edited glyph: previous and next change it.");
        else if (codeOk) { char cl[40]; code_label(code, cl, sizeof cl); snprintf(info, sizeof info, "%s", cl); }
        else if (l->glyphText[0]) snprintf(info, sizeof info, "Not a character or code");
        if (l->same) Th_TextSmall(info, x, y + TH_ROW_H + 6, TH_TEXT_DIM);
        else Th_TextRich(info, x + 180, y + (TH_ROW_H - Th_Measure("A").y) / 2, codeOk ? TH_TEXT : TH_DANGER, TH_F_BODY, -1);
        y += 56;
    }

    /* buttons */
    bool srcOk = l->popSrc == 0 || (other && l->fontSel >= 0) || probeOk;
    bool canAdd = codeOk && srcOk && l->n < MAX_LAYERS && (!fontScope || (l->fdoc && l->fdoc->n < FL_MAX));
    Rectangle bc = {p.x + p.width - 16 - 88 - 8 - 88, p.y + p.height - 44, 88, TH_ROW_H}, ba = {p.x + p.width - 16 - 88, bc.y, 88, TH_ROW_H};
    if (Th_Button(bc, "Cancel", 0, NULL)) { Layers_PopoverClose(l); return; }
    if (Th_Button(ba, "Add", TH_BTN_PRIMARY | TH_BTN_OFF(canAdd), NULL)) {
        char name[200] = "This font";
        const char *pxfm = "";
        if (other) {
            const StoreEntry *e = &l->fonts.e[l->fontSel];
            entry_name(e, name, sizeof name);
            pxfm = e->pxfm;
        }
        if (file) {
            char err[200];
            if (Layers_AddFile(l, pr->s.source, pr->s.face, fontScope, err, (int)sizeof err) >= 0) {
                if (!fontScope && !l->same) { l->v[0].sameCode = false; l->v[0].code = code; }
            } else Th_Toast(TH_TOAST_ERR, "Cannot add the layer: %s", err);
        } else if (other && fontScope) Layers_AddDbFont(l, pxfm, name);
        else Layers_Add(l, pxfm, name, l->same, code);
        Layers_PopoverClose(l);
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE) && !Th_InputBlocked()) { Layers_PopoverClose(l); return; }
    /* a click outside closes it, on release (the panel behind it was blocked while the button went down) */
    if (l->justOpened) l->justOpened = false;
    else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !Th_InputBlocked() && !CheckCollisionPointRec(GetMousePosition(), p)) Layers_PopoverClose(l);
}
