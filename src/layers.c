/* layers.c - reference layers of the glyph view: the stack, the glyph cache, the Layers tab and the Add Layer popover
 * (see layers.h and DESIGN.md "Layers"). */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layers.h"
#include "theme.h"

#define MAX_LAYERS 24
#define MAX_CACHE 40
#define POP_W 372
#define LIST_ROWS_MAX 8
#define ROW_H 32

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

    /* keyboard focus of one text field */
    char focus[32];
    char buf[64];
    bool fresh;

    /* the number buttons that repeat while held, the slider being dragged */
    char holdId[32];
    int holdDir;
    double holdT;
    char dragId[32];

    /* the Add Layer popover */
    bool pop;
    bool justOpened;            /* the frame the popover opens: the click that opened it must not close it */
    int popSrc;                 /* 0 this font, 1 a font of the library */
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
};

static const unsigned kSwatch[8] = {0xE5484D, 0x2F6FED, 0x2F9E6E, 0xD68A00, 0x9B59D0, 0xD6409F, 0x12A5B5, 0x1E1E22};

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static Color rgb(unsigned v) { return (Color){(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, 0xFF}; }
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
    l->v[i].dx = clampi(l->v[i].dx + dx, -999, 999);
    l->v[i].dy = clampi(l->v[i].dy + dy, -999, 999);
}

int Layers_Add(Layers *l, const char *pxfm, const char *fontName, bool sameCode, int32_t code)
{
    if (!l || l->n >= MAX_LAYERS) return -1;
    memmove(&l->v[1], &l->v[0], sizeof(Layer) * (size_t)l->n);
    l->n++;
    Layer *a = &l->v[0];
    memset(a, 0, sizeof *a);
    a->visible = true;
    a->color = rgb(kSwatch[l->nextColor++ % 8]);
    a->opacity = 60;
    a->sameCode = sameCode;
    a->code = code;
    snprintf(a->pxfm, sizeof a->pxfm, "%s", pxfm ? pxfm : "");
    snprintf(a->fontName, sizeof a->fontName, "%s", fontName && fontName[0] ? fontName : "This font");
    l->sel = 0;
    return 0;
}

void Layers_Remove(Layers *l, int i)
{
    if (!l || i < 0 || i >= l->n || l->v[i].isCurrent) return;
    memmove(&l->v[i], &l->v[i + 1], sizeof(Layer) * (size_t)(l->n - i - 1));
    l->n--;
    if (l->sel == i) l->sel = imin(i, l->n - 1);
    else if (l->sel > i) l->sel--;
}

void Layers_Move(Layers *l, int i, int dir)
{
    if (!l || i < 0 || i >= l->n) return;
    int j = i + (dir < 0 ? -1 : 1);
    if (j < 0 || j >= l->n) return;
    Layer t = l->v[i]; l->v[i] = l->v[j]; l->v[j] = t;
    if (l->sel == i) l->sel = j; else if (l->sel == j) l->sel = i;
}

bool Layers_TextFocus(const Layers *l) { return l && l->focus[0]; }
bool Layers_PopoverOpen(const Layers *l) { return l && l->pop; }

/* ------------------------------------------------------------------ glyph source */

static int32_t layer_code(const Layers *l, const Layer *a)
{
    int32_t c = a->sameCode ? l->editedCode : a->code;
    if (a->pxfm[0] && a->sameCode && !px_code_is_codepoint(c)) return -1;   /* an uncoded code names a glyph only inside its font */
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

int Layers_FoundCount(Layers *l, int i)
{
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

static void fit_text(const char *s, float maxW, char *out, size_t cap)
{
    snprintf(out, cap, "%s", s);
    if (Th_Measure(out).x <= maxW) return;
    size_t n = strlen(out);
    while (n > 1) {
        n--;
        while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80) n--;   /* not inside a UTF-8 sequence */
        out[n] = 0;
        char t[200];
        snprintf(t, sizeof t, "%s\xE2\x80\xA6", out);
        if (Th_Measure(t).x <= maxW) { snprintf(out, cap, "%s", t); return; }
    }
}

static void code_label(int32_t cp, char *out, size_t n)
{
    if (px_code_is_uncoded(cp)) { snprintf(out, n, "#%d", cp - PX_CODE_UNCODED_FIRST); return; }
    if (!px_code_is_codepoint(cp)) { snprintf(out, n, "no code"); return; }
    if ((cp >= 0x21 && cp < 0x7F) || (cp >= 0xA1 && cp <= 0x17F && cp != 0xAD)) {
        int len = 0;
        const char *u = CodepointToUTF8(cp, &len);
        snprintf(out, n, "U+%04X  %.*s", (unsigned)cp, len, u);
    } else snprintf(out, n, "U+%04X", (unsigned)cp);
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

static void icon_eye(Rectangle r, bool on, Color c)
{
    float cx = (float)(int)(r.x + r.width / 2), cy = (float)(int)(r.y + r.height / 2);
    DrawEllipseLines((int)cx, (int)cy, 7, 4, c);
    DrawCircle((int)cx, (int)cy, 2.2f, c);
    if (!on) DrawLineEx((Vector2){cx - 7, cy + 6}, (Vector2){cx + 7, cy - 6}, 1.5f, c);
}

static void icon_tri(Rectangle r, int dir, Color c)   /* dir: -1 up, +1 down */
{
    float cx = (float)(int)(r.x + r.width / 2), cy = (float)(int)(r.y + r.height / 2);
    Vector2 a, b, d;
    if (dir < 0) { a = (Vector2){cx - 4, cy + 2.5f}; b = (Vector2){cx, cy - 3}; d = (Vector2){cx + 4, cy + 2.5f}; }
    else { a = (Vector2){cx - 4, cy - 2.5f}; b = (Vector2){cx, cy + 3}; d = (Vector2){cx + 4, cy - 2.5f}; }
    DrawTriangle(a, d, b, c);
    DrawTriangle(a, b, d, c);
}

static void icon_x(Rectangle r, Color c)
{
    float cx = (float)(int)(r.x + r.width / 2), cy = (float)(int)(r.y + r.height / 2);
    DrawLineEx((Vector2){cx - 4, cy - 4}, (Vector2){cx + 4, cy + 4}, 1.6f, c);
    DrawLineEx((Vector2){cx + 4, cy - 4}, (Vector2){cx - 4, cy + 4}, 1.6f, c);
}

/* A flat icon button; returns true when clicked. */
static bool ibtn(Rectangle vis, Rectangle r, const char *name, int kind, bool enabled)
{
    Th_Reg(name, r);
    bool h = enabled && hov(vis, r);
    if (h) DrawRectangleRounded(r, 0.25f, 6, IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? (Color){0, 0, 0, 24} : TH_HOVER);
    Color c = enabled ? (h ? TH_TEXT : TH_TEXT_DIM) : (Color){0xC6, 0xC6, 0xCE, 0xFF};
    if (kind == 0) icon_tri(r, -1, c);
    else if (kind == 1) icon_tri(r, 1, c);
    else icon_x(r, kind == 2 && h ? TH_DANGER : c);
    return h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

/* ------------------------------------------------------------------ text fields and number widgets */

enum { F_TEXT = 0, F_NUM = 1, F_HEX = 2, F_BARE = 8 };   /* F_BARE: no frame of its own, centred text */

static void focus_clear(Layers *l) { l->focus[0] = 0; l->fresh = false; }

static void utf8_pop(char *s)
{
    size_t n = strlen(s);
    if (!n) return;
    n--;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    s[n] = 0;
}

/* One text field.  kind F_TEXT edits `live` (the caller's buffer) in place; F_NUM and F_HEX edit a copy of `shown` and return
 * 1 when the user commits it (Enter, Tab, a click elsewhere; the text is then in l->buf), 2 on Esc. */
static int field(Layers *l, Rectangle vis, const char *id, Rectangle r, char *live, int cap, const char *shown, const char *hint,
                 bool enabled, int kind)
{
    bool bare = (kind & F_BARE) != 0;
    kind &= ~F_BARE;
    Th_Reg(id, r);
    bool editing = !strcmp(l->focus, id);
    bool h = enabled && hov(vis, r);
    int ret = 0;
    if (!bare) DrawRectangleRounded(r, 0.14f, 6, enabled ? TH_PANEL : TH_PANEL_ALT);
    if (h) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    char *tb = kind == F_TEXT ? live : l->buf;
    int tcap = kind == F_TEXT ? cap : (int)sizeof l->buf;
    if (!editing && h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        snprintf(l->focus, sizeof l->focus, "%s", id);
        editing = true;
        if (kind != F_TEXT) { snprintf(l->buf, sizeof l->buf, "%s", shown); l->fresh = true; }
        tb = kind == F_TEXT ? live : l->buf;
        (void)GetCharPressed();
    } else if (editing && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hov(vis, r) && !Th_InputBlocked()) {
        focus_clear(l);
        editing = false;
        if (kind != F_TEXT) ret = 1;
    }
    float ty = r.y + (int)((r.height - Th_Measure("A").y) / 2);
    if (editing && !Th_InputBlocked()) {
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (ch < 32 || ch == 127) continue;
            if (kind == F_NUM && !((ch >= '0' && ch <= '9') || ch == '-')) continue;
            if (kind == F_HEX && !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == '#')) continue;
            if (l->fresh) { tb[0] = 0; l->fresh = false; }
            int len = 0;
            const char *u = CodepointToUTF8(ch, &len);
            if ((int)strlen(tb) + len < tcap) strncat(tb, u, (size_t)len);
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (l->fresh) { tb[0] = 0; l->fresh = false; } else utf8_pop(tb);
        }
        if (kind != F_TEXT && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB))) { focus_clear(l); editing = false; ret = 1; }
        else if (kind == F_TEXT && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB))) { focus_clear(l); editing = false; }
        else if (IsKeyPressed(KEY_ESCAPE)) { focus_clear(l); editing = false; ret = kind == F_TEXT ? 0 : 2; }
    }
    if (!bare) DrawRectangleRoundedLinesEx(r, 0.14f, 6, 1, editing ? TH_ACCENT : (enabled ? (Color){0xB4, 0xB4, 0xBD, 0xFF} : TH_LINE));
    else if (editing) DrawRectangleLinesEx((Rectangle){r.x - 1, r.y, r.width + 2, r.height}, 1, TH_ACCENT);
    const char *txt = editing ? tb : (kind == F_TEXT ? live : shown);
    if (!txt[0] && !editing && hint) {
        Th_Text(hint, r.x + 8, ty, (Color){0xA8, 0xA8, 0xB2, 0xFF});
    } else {
        char fit[96];
        float maxW = bare ? r.width - 4 : r.width - 16;
        if (editing) {   /* the tail stays visible while typing */
            snprintf(fit, sizeof fit, "%s", txt);
            while (fit[0] && Th_Measure(fit).x > maxW) memmove(fit, fit + 1, strlen(fit));
        } else fit_text(txt, maxW, fit, sizeof fit);
        Vector2 m = Th_Measure(fit);
        float tx = bare ? r.x + (int)((r.width - m.x) / 2) : r.x + 8;
        if (editing && l->fresh && fit[0]) DrawRectangle((int)tx - 1, (int)ty, (int)m.x + 2, (int)m.y, TH_ACCENT_TINT);
        Th_Text(fit, tx, ty, enabled ? TH_TEXT : TH_TEXT_DIM);
        if (editing && !l->fresh && fmod(GetTime(), 1.0) < 0.6) DrawRectangle((int)(tx + 1 + m.x), (int)ty, 1, (int)m.y, TH_TEXT);
    }
    return ret;
}

/* [-] value [+]; typing into the value is allowed.  Returns true and *out when the value changes. */
static bool spin(Layers *l, Rectangle vis, const char *id, Rectangle r, int val, int lo, int hi, int *out)
{
    bool changed = false;
    float bw = 22;
    Rectangle bm = {r.x, r.y, bw, r.height}, bp = {r.x + r.width - bw, r.y, bw, r.height};
    Rectangle bv = {r.x + bw, r.y, r.width - 2 * bw, r.height};
    char n[48];
    snprintf(n, sizeof n, "%s.minus", id); Th_Reg(n, bm);
    snprintf(n, sizeof n, "%s.plus", id); Th_Reg(n, bp);
    char vid[48];
    snprintf(vid, sizeof vid, "%s.value", id);
    bool editing = !strcmp(l->focus, vid);

    DrawRectangleRounded(r, 0.14f, 6, TH_PANEL);
    DrawRectangleRoundedLinesEx(r, 0.14f, 6, 1, editing ? TH_ACCENT : (Color){0xB4, 0xB4, 0xBD, 0xFF});
    bool hm = hov(vis, bm), hp = hov(vis, bp);
    if (hm && !editing) DrawRectangle((int)bm.x + 1, (int)bm.y + 1, (int)bm.width - 1, (int)bm.height - 2, (Color){0xEE, 0xEE, 0xF2, 0xFF});
    if (hp && !editing) DrawRectangle((int)bp.x, (int)bp.y + 1, (int)bp.width - 1, (int)bp.height - 2, (Color){0xEE, 0xEE, 0xF2, 0xFF});
    Th_VLine(bm.x + bm.width, r.y + 1, r.y + r.height - 1, TH_LINE);
    Th_VLine(bp.x, r.y + 1, r.y + r.height - 1, TH_LINE);
    int cy = (int)(r.y + r.height / 2);
    DrawRectangle((int)(bm.x + bw / 2 - 4), cy, 8, 1, TH_TEXT);
    DrawRectangle((int)(bp.x + bw / 2 - 4), cy, 8, 1, TH_TEXT);
    DrawRectangle((int)(bp.x + bw / 2 - 0.5f), cy - 4, 1, 9, TH_TEXT);

    char text[24];
    snprintf(text, sizeof text, "%d", val);
    int f = field(l, vis, vid, bv, NULL, 0, text, NULL, true, F_NUM | F_BARE);
    if (f == 1) {
        char *end = NULL;
        long v = strtol(l->buf, &end, 10);
        if (end != l->buf) { int nv = clampi((int)v, lo, hi); if (nv != val) { *out = nv; changed = true; } }
    }
    int step = 0;
    if (!editing) {
        if (hm && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { step = -1; snprintf(l->holdId, sizeof l->holdId, "%s", id); l->holdDir = -1; l->holdT = GetTime() + 0.45; }
        else if (hp && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { step = 1; snprintf(l->holdId, sizeof l->holdId, "%s", id); l->holdDir = 1; l->holdT = GetTime() + 0.45; }
        else if (!strcmp(l->holdId, id) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (l->holdDir < 0 ? hm : hp) && GetTime() > l->holdT) { step = l->holdDir; l->holdT += 0.05; }
    }
    if (!strcmp(l->holdId, id) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) l->holdId[0] = 0;
    if (step) { int nv = clampi(val + step, lo, hi); if (nv != val) { *out = nv; changed = true; } }
    return changed;
}

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
    DrawRectangleRounded((Rectangle){r.x, cy - 2, r.width, 4}, 1.0f, 4, (Color){0xE0, 0xE0, 0xE6, 0xFF});
    float t = (float)(*v - lo) / (float)(hi - lo);
    DrawRectangleRounded((Rectangle){r.x, cy - 2, r.width * t, 4}, 1.0f, 4, TH_ACCENT);
    float kx = r.x + r.width * t;
    DrawCircle((int)kx, (int)cy + 1, 7.5f, (Color){0, 0, 0, 22});
    DrawCircle((int)kx, (int)cy, 7, TH_PANEL);
    DrawCircleLines((int)kx, (int)cy, 7, dragging || h ? TH_ACCENT : (Color){0xB4, 0xB4, 0xBD, 0xFF});
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

#define CARD_HEAD 40
#define CARD_BODY 112

static void open_add(Layers *l)
{
    l->pop = true;
    l->justOpened = true;
    l->popSrc = 0;
    l->fontSel = -1;
    l->orderValid = false;
    l->search[0] = 0;
    l->glyphText[0] = 0;   /* the "same code" choice is remembered */
    focus_clear(l);
}

float Layers_DrawPanel(Layers *l, Rectangle area, Rectangle vis)
{
    float px = area.x + 12, pw = area.width - 24;
    float y = area.y + 6;
    if (!l) return 0;

    /* header: the title and the Add Layer button (with no reference layer yet the empty state has the big button) */
    Th_TextSmall("LAYERS", px + 4, y + 8, TH_TEXT_DIM);
    if (l->n > 1) {
        Rectangle br = {px + pw - 104, y, 104, 26};
        if (l->n < MAX_LAYERS) {
            if (Th_Button(br, "+ Add Layer") && hov(vis, br)) open_add(l);
        } else Th_Button(br, "+ Add Layer");
    }
    y += 36;

    int opKind = 0, opIdx = -1;   /* a reorder or a removal, applied after the loop */
    for (int i = 0; i < l->n; i++) {
        Layer *a = &l->v[i];
        bool selected = i == l->sel;
        bool cur = a->isCurrent;
        float h = CARD_HEAD + (selected && !cur ? CARD_BODY : 0);
        Rectangle card = {px, y, pw, h};
        const PxGlyph *g = cur ? NULL : Layers_Glyph(l, i, NULL);
        bool missing = !cur && !g;
        char nm[40];

        /* card */
        if (cur) {
            DrawRectangleRounded(card, 0.07f, 8, TH_PANEL);
            DrawRectangleRounded(card, 0.07f, 8, TH_ACCENT_TINT);
            DrawRectangleRoundedLinesEx(card, 0.07f, 8, 1, withA(TH_ACCENT, 150));
        } else {
            DrawRectangleRounded(card, 0.07f, 8, TH_PANEL);
            DrawRectangleRoundedLinesEx(card, 0.07f, 8, 1, selected ? TH_ACCENT : TH_LINE);
        }
        Rectangle head = {card.x, card.y, card.width, CARD_HEAD};
        snprintf(nm, sizeof nm, "lay_row_%d", i);
        Th_Reg(nm, head);

        /* the controls of the header (they take the click before the row) */
        Rectangle eye = {card.x + 8, card.y + 9, 24, 22};
        Rectangle bx = {card.x + card.width - 26, card.y + 10, 20, 20};
        Rectangle bd = {bx.x - 20, bx.y, 20, 20}, bu = {bd.x - 20, bx.y, 20, 20};
        bool ctl = false;
        snprintf(nm, sizeof nm, "lay_eye_%d", i);
        Th_Reg(nm, eye);
        bool he = hov(vis, eye);
        if (he) DrawRectangleRounded(eye, 0.25f, 6, TH_HOVER);
        icon_eye(eye, a->visible, a->visible ? (he ? TH_ACCENT : TH_TEXT) : (Color){0xB0, 0xB0, 0xBA, 0xFF});
        if (he) { ctl = true; if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) a->visible = !a->visible; }
        snprintf(nm, sizeof nm, "lay_up_%d", i);
        if (ibtn(vis, bu, nm, 0, i > 0)) { opKind = 1; opIdx = i; }
        if (hov(vis, bu)) ctl = true;
        snprintf(nm, sizeof nm, "lay_down_%d", i);
        if (ibtn(vis, bd, nm, 1, i + 1 < l->n)) { opKind = 2; opIdx = i; }
        if (hov(vis, bd)) ctl = true;
        if (!cur) {
            snprintf(nm, sizeof nm, "lay_del_%d", i);
            if (ibtn(vis, bx, nm, 2, true)) { opKind = 3; opIdx = i; }
            if (hov(vis, bx)) ctl = true;
        }

        /* colour chip, name and detail line */
        float tx = eye.x + eye.width + 8;
        if (!cur) {
            Rectangle chip = {tx, card.y + 14, 12, 12};
            DrawRectangleRounded(chip, 0.3f, 4, a->visible ? withA(a->color, 255 * a->opacity / 100 < 40 ? 40 : 255 * a->opacity / 100) : (Color){0xE0, 0xE0, 0xE6, 0xFF});
            DrawRectangleRoundedLinesEx(chip, 0.3f, 4, 1, withA(TH_TEXT, 60));
            tx += 20;
        }
        float tw = bu.x - tx - 4;
        char line1[160], line2[160], fit[200];
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
            if (missing) snprintf(line2, sizeof line2, "%s%snot in font", cl, cl[0] ? " \xC2\xB7 " : "");
            else if (g->w <= 0) snprintf(line2, sizeof line2, "%s \xC2\xB7 no ink", cl);
            else snprintf(line2, sizeof line2, "%s%s", cl, a->sameCode ? " \xC2\xB7 same code" : "");
        }
        Color c1 = (missing || !a->visible) ? TH_TEXT_DIM : TH_TEXT;
        fit_text(line1, tw, fit, sizeof fit);
        Th_Text(fit, tx, card.y + 5, c1);
        fit_text(line2, tw, fit, sizeof fit);
        Th_TextSmall(fit, tx, card.y + 23, TH_TEXT_DIM);
        if (missing) DrawRectangleRounded(card, 0.07f, 8, withA(TH_PANEL, 120));

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
                bool on = rgbval(a->color) == kSwatch[k];
                DrawRectangleRounded(sw, 0.25f, 6, rgb(kSwatch[k]));
                if (on) DrawRectangleRoundedLinesEx((Rectangle){sw.x - 2, sw.y - 2, sw.width + 4, sw.height + 4}, 0.25f, 6, 2, TH_ACCENT);
                else if (hv) DrawRectangleRoundedLinesEx(sw, 0.25f, 6, 2, withA(TH_TEXT, 90));
                if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) a->color = rgb(kSwatch[k]);
            }
            {
                char hx[12];
                snprintf(hx, sizeof hx, "#%06X", rgbval(a->color));
                Rectangle hr = {bxo + 8 * 24 + 4, by - 1, card.width - 24 - 8 * 24 - 4, 22};
                int f = field(l, vis, "lay_hex", hr, NULL, 0, hx, "#RRGGBB", true, F_HEX);
                if (f == 1) { Color nc; if (parse_hex(l->buf, &nc)) a->color = nc; }
            }
            by += 30;
            /* opacity */
            Th_Text("Opacity", bxo, by + 3, TH_TEXT_DIM);
            {
                Rectangle sr = {bxo + 58, by + 4, card.width - 24 - 58 - 8 - 84, 16};
                int nv = a->opacity;
                if (slider(l, vis, "lay_op", sr, &nv, 0, 100)) a->opacity = nv;
                Rectangle vr = {sr.x + sr.width + 8, by - 1, 84, 24};
                if (spin(l, vis, "lay_opv", vr, a->opacity, 0, 100, &nv)) a->opacity = nv;
            }
            by += 32;
            /* offsets */
            Th_Text("X", bxo, by + 4, TH_TEXT_DIM);
            Th_Text("Y", bxo + 128, by + 4, TH_TEXT_DIM);
            {
                int nv;
                if (spin(l, vis, "lay_x", (Rectangle){bxo + 18, by, 96, 24}, a->dx, -999, 999, &nv)) a->dx = nv;
                if (spin(l, vis, "lay_y", (Rectangle){bxo + 146, by, 96, 24}, a->dy, -999, 999, &nv)) a->dy = nv;
            }
        }
        y += h + 6;
    }
    if (opKind == 1) Layers_Move(l, opIdx, -1);
    else if (opKind == 2) Layers_Move(l, opIdx, 1);
    else if (opKind == 3) Layers_Remove(l, opIdx);
    y += 4;
    if (l->n == 1) {
        /* the empty state: what layers are for, and a prominent way to add the first one */
        Rectangle box = {px, y, pw, 150};
        DrawRectangleRounded(box, 0.05f, 8, TH_PANEL_ALT);
        DrawRectangleRoundedLinesEx(box, 0.05f, 8, 1, TH_LINE);
        float ty = box.y + 14;
        Th_Text("No reference layers yet", box.x + 14, ty, TH_TEXT);
        ty += 24;
        Th_TextSmall("Layers show other glyphs under or over", box.x + 14, ty, TH_TEXT_DIM);
        ty += 15;
        Th_TextSmall("the one you edit: compare shapes, or trace", box.x + 14, ty, TH_TEXT_DIM);
        ty += 15;
        Th_TextSmall("over a glyph from this or another font.", box.x + 14, ty, TH_TEXT_DIM);
        Rectangle br = {box.x + 14, box.y + box.height - 14 - 34, box.width - 28, 34};
        Th_Reg("+_Add_Layer", br);
        bool hv = hov(vis, br);
        DrawRectangleRounded(br, 0.2f, 8, hv ? (Color){0x25, 0x5C, 0xD0, 0xFF} : TH_ACCENT);
        Vector2 m = Th_Measure("+ Add Layer");
        Th_Text("+ Add Layer", floorf(br.x + (br.width - m.x) / 2), floorf(br.y + (br.height - m.y) / 2), WHITE);
        if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) open_add(l);
        y += box.height + 12;
    } else {
        Th_TextSmall("Alt+drag in the canvas moves the selected layer.", px + 4, y, TH_TEXT_DIM);
        y += 16;
    }
    Th_TextSmall("Layers are not saved into the font.", px + 4, y, TH_TEXT_DIM);
    y += 20;
    return y - area.y;
}

/* ------------------------------------------------------------------ the Add Layer popover */

void Layers_PopoverClose(Layers *l) { if (l) { l->pop = false; focus_clear(l); } }

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
    else snprintf(out, cap, "Font %.8s", e->pxfm);
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

void Layers_DrawPopover(Layers *l, Rectangle panel)
{
    if (!l || !l->pop) return;
    bool other = l->popSrc == 1;
    float W = POP_W;
    int sh = GetScreenHeight();
    int LIST_ROWS = clampi((sh - 400) / ROW_H, 3, LIST_ROWS_MAX);
    float H = other ? 96 + 32 + LIST_ROWS * ROW_H + 12 + 92 + 44 : 96 + 92 + 44;
    Rectangle p = {panel.x - W - 10, panel.y + 80, W, H};
    if (p.x < 8) p.x = 8;
    if (p.y + p.height > sh - 8) p.y = (float)(sh - 8) - p.height;
    if (p.y < 8) p.y = 8;
    l->popRect = p;
    DrawRectangleRounded((Rectangle){p.x + 1, p.y + 3, p.width, p.height}, 0.03f, 8, (Color){0, 0, 0, 30});
    DrawRectangleRounded(p, 0.03f, 8, TH_PANEL);
    DrawRectangleRoundedLinesEx(p, 0.03f, 8, 1, TH_LINE);
    Rectangle vis = p;
    float x = p.x + 16, w = p.width - 32, y = p.y + 14;
    Th_Reg("lay_pop", p);

    Th_Text("Add Layer", x, y, TH_TEXT);
    {
        Rectangle xr = {p.x + p.width - 34, y - 6, 24, 24};
        Th_Reg("lay_pop_close", xr);
        bool h = hov(vis, xr);
        if (h) DrawRectangleRounded(xr, 0.25f, 6, TH_HOVER);
        icon_x(xr, h ? TH_TEXT : TH_TEXT_DIM);
        if (h && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { Layers_PopoverClose(l); return; }
    }
    y += 28;

    /* source */
    Th_TextSmall("FONT", x, y, TH_TEXT_DIM);
    y += 16;
    {
        static const char *labels[2] = {"This font", "Library font"};
        int c = Th_Segmented((Rectangle){x, y, w, 28}, labels, 2, l->popSrc, 0, "lay_src");
        if (c >= 0 && c != l->popSrc) { l->popSrc = c; focus_clear(l); l->listScroll = 0; return; }
    }
    y += 38;

    if (other) {
        refresh_fonts(l);
        if (field(l, vis, "lay_search", (Rectangle){x, y, w, 26}, l->search, (int)sizeof l->search, NULL, "Search fonts by name or hash", true, F_TEXT)) {}
        y += 32;
        Rectangle lr = {x, y, w, LIST_ROWS * ROW_H};
        DrawRectangleRec(lr, TH_PANEL_ALT);
        Th_RectLines(lr, TH_LINE);
        Th_Reg("lay_list", lr);
        if (!l->store) Th_Text("No library is open.", lr.x + 10, lr.y + 10, TH_TEXT_DIM);
        else if (!l->nOrder) Th_Text(l->fonts.count ? "No font matches." : "The library has no fonts.", lr.x + 10, lr.y + 10, TH_TEXT_DIM);
        if (hov(vis, lr)) {
            float wh = GetMouseWheelMove();
            if (wh != 0) l->listScroll -= (int)(wh * 2);
        }
        l->listScroll = clampi(l->listScroll, 0, imax(0, l->nOrder - LIST_ROWS));
        for (int k = 0; k < LIST_ROWS; k++) {
            int oi = l->listScroll + k;
            if (oi >= l->nOrder) break;
            int ei = l->order[oi];
            const StoreEntry *e = &l->fonts.e[ei];
            Rectangle rr = {lr.x + 1, lr.y + k * ROW_H, lr.width - 8, ROW_H};
            char rn[24];
            snprintf(rn, sizeof rn, "lay_font_%d", k);
            Th_Reg(rn, rr);
            bool hv = hov(vis, rr);
            bool on = l->fontSel == ei;
            if (on) DrawRectangleRec(rr, TH_ACCENT_TINT);
            else if (hv) DrawRectangleRec(rr, TH_HOVER);
            draw_sample(l, e->pxfm, (Rectangle){rr.x + 8, rr.y, 56, rr.height});
            char nm[200], fit[200], h8[16];
            entry_name(e, nm, sizeof nm);
            snprintf(h8, sizeof h8, "%.8s", e->pxfm);
            float hw = Th_MeasureMono(h8).x;
            fit_text(nm, rr.width - 72 - hw - 18, fit, sizeof fit);
            Th_Text(fit, rr.x + 72, rr.y + 8, on ? TH_ACCENT : TH_TEXT);
            Th_TextMono(h8, rr.x + rr.width - hw - 8, rr.y + 9, TH_TEXT_DIM);
            if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) l->fontSel = ei;
        }
        if (l->nOrder > LIST_ROWS) {   /* the scrollbar */
            float th = lr.height * LIST_ROWS / (float)l->nOrder;
            if (th < 24) th = 24;
            float ty = lr.y + (lr.height - th) * (float)l->listScroll / (float)(l->nOrder - LIST_ROWS);
            DrawRectangleRounded((Rectangle){lr.x + lr.width - 6, ty + 1, 4, th - 2}, 1.0f, 4, (Color){0xC6, 0xC6, 0xCE, 0xFF});
        }
        y += lr.height + 12;
    }

    /* glyph */
    Th_TextSmall("GLYPH", x, y, TH_TEXT_DIM);
    y += 16;
    {
        Rectangle cb = {x, y + 1, 16, 16};
        Th_Reg("lay_same", (Rectangle){x, y - 2, w, 22});
        bool hv = hov(vis, (Rectangle){x, y - 2, w, 22});
        DrawRectangleRounded(cb, 0.2f, 4, l->same ? TH_ACCENT : TH_PANEL);
        DrawRectangleRoundedLinesEx(cb, 0.2f, 4, 1, l->same ? TH_ACCENT : (hv ? TH_ACCENT : (Color){0xB4, 0xB4, 0xBD, 0xFF}));
        if (l->same) {
            DrawLineEx((Vector2){cb.x + 3.5f, cb.y + 8.5f}, (Vector2){cb.x + 6.5f, cb.y + 11.5f}, 2, WHITE);
            DrawLineEx((Vector2){cb.x + 6.5f, cb.y + 11.5f}, (Vector2){cb.x + 12.5f, cb.y + 4.5f}, 2, WHITE);
        }
        Th_Text("Same code as the edited glyph", x + 26, y + 1, TH_TEXT);
        if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { l->same = !l->same; focus_clear(l); }
    }
    y += 26;
    int32_t code = -1;
    bool codeOk = l->same;
    {
        Rectangle fr = {x, y, 150, 26};
        field(l, vis, "lay_glyph", fr, l->glyphText, (int)sizeof l->glyphText, NULL, "A, U+0041 or 41", !l->same, F_TEXT);
        if (!l->same) codeOk = parse_glyph(l->glyphText, &code);
        char info[64] = "";
        if (l->same) snprintf(info, sizeof info, "Follows the edited glyph: previous and next change it.");
        else if (codeOk) { char cl[40]; code_label(code, cl, sizeof cl); snprintf(info, sizeof info, "%s", cl); }
        else if (l->glyphText[0]) snprintf(info, sizeof info, "Not a character or code");
        if (l->same) Th_TextSmall(info, x, y + 32, TH_TEXT_DIM);
        else Th_Text(info, x + 162, y + 4, codeOk ? TH_TEXT : TH_DANGER);
    }
    y += 50;

    /* buttons */
    bool canAdd = codeOk && (!other || l->fontSel >= 0) && l->n < MAX_LAYERS;
    Rectangle bc = {p.x + p.width - 16 - 80 - 8 - 80, p.y + p.height - 44, 80, 28}, ba = {p.x + p.width - 16 - 80, bc.y, 80, 28};
    bool doAdd = false;
    if (Th_Button(bc, "Cancel")) { Layers_PopoverClose(l); return; }
    if (canAdd) {
        DrawRectangleRounded(ba, 0.2f, 6, TH_ACCENT);
        Th_Reg("Add", ba);
        bool hv = hov(vis, ba);
        if (hv) DrawRectangleRounded(ba, 0.2f, 6, (Color){255, 255, 255, 40});
        Vector2 m = Th_Measure("Add");
        Th_Text("Add", ba.x + (int)((ba.width - m.x) / 2), ba.y + (int)((ba.height - m.y) / 2), WHITE);
        if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) doAdd = true;
    } else {
        DrawRectangleRounded(ba, 0.2f, 6, (Color){0xE4, 0xE4, 0xE9, 0xFF});
        Th_Reg("Add", ba);
        Vector2 m = Th_Measure("Add");
        Th_Text("Add", ba.x + (int)((ba.width - m.x) / 2), ba.y + (int)((ba.height - m.y) / 2), (Color){0xA8, 0xA8, 0xB2, 0xFF});
    }
    if (doAdd) {
        char name[200] = "This font";
        const char *pxfm = "";
        if (other) {
            const StoreEntry *e = &l->fonts.e[l->fontSel];
            entry_name(e, name, sizeof name);
            pxfm = e->pxfm;
        }
        Layers_Add(l, pxfm, name, l->same, code);
        Layers_PopoverClose(l);
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE) && !Th_InputBlocked()) { Layers_PopoverClose(l); return; }
    /* a click outside closes it, on release (the panel behind it was blocked while the button went down) */
    if (l->justOpened) l->justOpened = false;
    else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !Th_InputBlocked() && !CheckCollisionPointRec(GetMousePosition(), p)) Layers_PopoverClose(l);
}
