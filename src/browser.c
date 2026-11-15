/* browser.c - the library browser view of pxfe (see DESIGN.md, "Library browser").
 *
 * Structure: a card grid (virtualized, anchored scrolling, baseline-aligned pixel-perfect samples drawn directly as
 * run-merged rectangles) on the left, a side panel with the view controls on the right, a status line at the bottom.
 * All widgets of the view are drawn here (segmented controls, lists and cards) so that the look is exact. */
#include "browser.h"
#include "theme.h"
#include "sample.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_W      352
#define STATUS_H     TH_ROW_H
#define TOP_H        0           /* the bar of the application replaces the tab row */
#define PAD          16
#define GAP          12
#define MIN_CARD_W   300
#define SAMPLE_TOP   58          /* card top to the sample box */
#define CARD_BOTTOM  16
#define MAX_SAMPLE_H 900
#define MAX_LINES_B  8           /* lines of the sample a card shows */
#define HDR_H        58          /* typeface box: header height */
#define V_TOP        54          /* typeface variant row: top to the sample */
#define V_BOT        12
#define BOX_PAD      8
#define MAX_SAMPLE   400         /* bytes of the sample text */

enum { TAB_TYPEFACES = 0, TAB_FONTS = 1 };
enum { SORT_NAME = 0, SORT_SIZE, SORT_FORMAT, SORT_GLYPHS };
enum { ST_LOADING = 0, ST_READY, ST_FAILED };

/* ------------------------------------------------------------------------------------------------ types */

typedef struct Box { int v0, n; } Box;     /* a typeface box: the variants vis[v0 .. v0+n) */

typedef struct Card {
    int reqGen, okGen;             /* sample generation requested / known complete in the loader */
    int32_t wantDefault, defReq;   /* default glyph code point to load as well / already requested, PX_NO_CODEPOINT none */
    bool infoReq;                  /* info-only load requested (filter/sort needs it) */
    unsigned stamp;                /* frame in which the card was last visible */
    const LibFont *layLf;          /* layout validity */
    int layGen, layCount;
    char layHash[33];
    SampleLayout lay; bool hasLay; int layW; /* layout in font pixels */
    int inkLines;                  /* lines of the laid-out sample that have at least one inked pixel */
    char hFam[200], hSty[200], hDet[200]; float hStyX; int hW, hState; bool hHashOnly; /* cached header strings */
    Texture2D tex; bool hasTex; int texTop, texW, texH; unsigned texUsed;
} Card;

typedef struct Popup {
    bool open; int which;          /* 0 style, 1 format, 2 context menu */
    Rectangle anchor; int scroll;
} Popup;

enum { SIDE_VIEW = 0, SIDE_SIMILAR = 1, SIDE_TYPEFACE = 2 };
static BrowserTypefaceTab g_tfTab;
static BrowserTypefaceEditing g_tfEditing;
void Browser_SetTypefaceTab(BrowserTypefaceTab draw, BrowserTypefaceEditing editing) { g_tfTab = draw; g_tfEditing = editing; }
static BrowserLocked g_locked;
void Browser_SetLockQuery(BrowserLocked locked) { g_locked = locked; }
static bool is_locked(bool typeface, const char *hash) { return !g_locked || g_locked(typeface, hash); }

struct Browser {
    Store *store; Insight *insight;
    char root[1100];
    float panelScroll, panelContentH;
    Library lib;
    LibLoader *loader;
    Card *cl;                      /* per entry of lib */
    int nTf;                       /* typefaces (boxes) among the entries of the Typefaces tab */

    /* view state */
    int tab, zoom, cols, sortKey; bool sortDesc, mono, italic;
    int sideTab;                   /* side panel: SIDE_VIEW, SIDE_SIMILAR, SIDE_TYPEFACE (shown as View | Typeface | Similar) */
    bool tfTabShown;               /* the Typeface tab was drawn this frame */
    char scHash[33]; PxtfTypeface scCont; bool scHas;   /* the typeface of the selected entry, for the Typeface tab */
    /* the Typeface view: the Typefaces tab narrowed to the variants of one typeface, drawn as large rows */
    char tfView[33];               /* its pxtf hash, "" in the lists */
    char tfFamily[200];            /* its family (to follow a rewrite of the typeface after a deletion) */
    PxtfTypeface tfCont; bool tfHas;
    char tfSaveKey[1100]; int tfSaveAnchor; float tfSaveOff;
    char pendingTf[33];            /* a typeface to open at the end of the frame */
    bool wantRescan;
    char filter[MAX_SAMPLE + 4];   /* the name filter */
    SampleEdit sampleEdit;
    unsigned sampleCtr;            /* Sample_Counter() the cards were laid out for */
    int nLines;                    /* lines of the sample (at most MAX_LINES_B) */

    char selStr[2][160];           /* chosen style / format ("" = Any) */
    char **vals[2]; int nvals[2];  /* values present in the current tab, [0] is "Any" */
    Popup pop;
    int ctxN, ctxVi; bool ctxHdr; char ctxLabel[5][32]; BrowserAction ctxAct[5]; bool ctxPick; BrowserChoice ctxChoice;
    BrowserAction lastAction; char lastPxfm[33], lastPxtf[33];
    float msUpd[120], msFrame[120]; int msN;   /* CPU time of Browser_UpdateDraw and whole frame time (GetFrameTime) of the last frames, in ms */
    bool mouseEaten, closeEat;
    bool modalBlocked;             /* Th_InputBlocked() when the frame began (a dialog is open) */

    /* filtered list */
    int *vis; int nvis, capVis;
    bool dirtyVis, infoPending, keepAnchor; double lastRefilter;
    char selKey[3][1100];
    int selVis; bool selHdr;        /* Typefaces: the box header is selected (selVis is its first variant) */
    int keepSelIdx; bool keepSelHdr; /* after a rescan: select this visible index when the selection is gone */
    Box *box; int nbox, capBox;     /* Typefaces tab: the boxes of vis */
    int *boxOf; float *vTop, *vH, *boxHt;   /* per visible variant: its box, its y (content) and height; per box: its height */

    /* scrolling (anchored on the first item of the top row so that rows changing height do not make it jump) */
    int anchorItem[3]; float anchorOff[3];
    float scrollY, maxScroll; bool reveal;
    double lastClickT; int lastClickIdx;

    /* sample */
    int32_t *sCps; int nS;         /* the sample, in order */
    int32_t *uCps; int nU;         /* unique, for requests */
    int gen; bool recheck;
    unsigned frame;

    /* last frame's layout */
    float *rowTop, *rowH, *rowAbove; int nrowsCap;
    int lcols; float lx0, lcw, lgy; int lnrows;
    Rectangle gridR;
    int readyVisible;
    double scanT, createT;
    int loadedInfoCount; int texCount;
};

/* ------------------------------------------------------------------------------------------------ small utils */

static float snapf(float v) { return floorf(v + 0.5f); }
static Rectangle RR(float x, float y, float w, float h) { return (Rectangle){ x, y, w, h }; }
static bool pt_in(Rectangle r, Vector2 p) { return CheckCollisionPointRec(p, r); }
static bool hov(const Browser *b, Rectangle r) { return !Th_InputBlocked() && !b->mouseEaten && pt_in(r, GetMousePosition()); }
static Color alpha(Color c, int a) { c.a = (unsigned char)a; return c; }

static int ci_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

static bool ci_contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return true;
    for (; *hay; hay++) {
        size_t i = 0;
        for (; i < n; i++) {
            int x = (unsigned char)hay[i], y = (unsigned char)needle[i];
            if (!x) return false;
            if (x >= 'A' && x <= 'Z') x += 32;
            if (y >= 'A' && y <= 'Z') y += 32;
            if (x != y) break;
        }
        if (i == n) return true;
    }
    return false;
}

/* Pixel-exact rounded rectangle (cheap: a handful of rectangles; corner profile of a circle of radius rad). */
static int corner_inset(int i, int rad)
{
    float d = rad - i - 0.5f;
    return rad - (int)(sqrtf((float)(rad * rad) - d * d) + 0.5f);
}

static void rr_fill(int x, int y, int w, int h, int rad, Color c)
{
    if (w <= 0 || h <= 0) return;
    if (rad * 2 > h) rad = h / 2;
    if (rad * 2 > w) rad = w / 2;
    for (int i = 0; i < rad; i++) {
        int in = corner_inset(i, rad);
        DrawRectangle(x + in, y + i, w - 2 * in, 1, c);
        DrawRectangle(x + in, y + h - 1 - i, w - 2 * in, 1, c);
    }
    if (h - 2 * rad > 0) DrawRectangle(x, y + rad, w, h - 2 * rad, c);
}

static void rr_ring(int x, int y, int w, int h, int rad, Color c)
{
    if (w < 2 || h < 2) return;
    if (rad * 2 > h) rad = h / 2;
    if (rad * 2 > w) rad = w / 2;
    int ri = rad > 0 ? rad - 1 : 0;
    DrawRectangle(x + (rad ? corner_inset(0, rad) : 0), y, w - 2 * (rad ? corner_inset(0, rad) : 0), 1, c);
    DrawRectangle(x + (rad ? corner_inset(0, rad) : 0), y + h - 1, w - 2 * (rad ? corner_inset(0, rad) : 0), 1, c);
    for (int i = 1; i < rad; i++) {
        int o = corner_inset(i, rad);
        int in = 1 + (i - 1 < ri ? corner_inset(i - 1, ri) : 0);
        if (in < o) in = o;
        DrawRectangle(x + o, y + i, in - o + 0 > 0 ? in - o : 1, 1, c);
        DrawRectangle(x + w - in, y + i, in - o > 0 ? in - o : 1, 1, c);
        DrawRectangle(x + o, y + h - 1 - i, in - o > 0 ? in - o : 1, 1, c);
        DrawRectangle(x + w - in, y + h - 1 - i, in - o > 0 ? in - o : 1, 1, c);
    }
    if (h - 2 * rad > 0) {
        DrawRectangle(x, y + rad, 1, h - 2 * rad, c);
        DrawRectangle(x + w - 1, y + rad, 1, h - 2 * rad, c);
    }
}

static void rrect(Rectangle r, Color fill, Color border, float radius)
{
    int x = (int)floorf(r.x), y = (int)floorf(r.y), w = (int)floorf(r.x + r.width) - x, h = (int)floorf(r.y + r.height) - y;
    int rad = (int)(radius + 0.5f);
    if (fill.a) rr_fill(x, y, w, h, rad, fill);
    if (border.a) rr_ring(x, y, w, h, rad, border);
}

/* ------------------------------------------------------------------------------------------------ entries */

static Library *tab_lib(Browser *b) { return &b->lib; }
static int tab_start(const Browser *b) { return b->tab == TAB_FONTS ? b->lib.nTypefaceEntries : 0; }
static int tab_count_of(const Browser *b, int tab)
{
    return tab == TAB_TYPEFACES ? b->lib.nTypefaceEntries : tab == TAB_FONTS ? b->lib.nFontEntries : 0;
}
static int cmp_str_ptr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* the number of distinct typefaces among the variant entries (the boxes of the Typefaces tab with no filter) */
static int count_typefaces(const Library *lib)
{
    int n = lib->nTypefaceEntries, k = 0;
    if (n <= 0) return 0;
    const char **h = (const char **)malloc((size_t)n * sizeof *h);
    if (!h) return n;
    for (int i = 0; i < n; i++) h[i] = lib->e[i].pxtf;
    qsort(h, (size_t)n, sizeof *h, cmp_str_ptr);
    for (int i = 0; i < n; i++) if (i == 0 || strcmp(h[i], h[i - 1])) k++;
    free(h);
    return k;
}

static LibEntry *E(Browser *b, int abs) { return &tab_lib(b)->e[abs]; }
static Card *CD(Browser *b, int abs) { return b->cl + abs; }

static void entry_key(const LibEntry *e, char *out, size_t n)
{
    if (e->kind == LIB_TYPEFACE) snprintf(out, n, "c:%s#%d", e->pxtf, e->variant);
    else snprintf(out, n, "f:%s", e->pxfm);
}

static void entry_names(const LibEntry *e, char *fam, size_t fn, char *sty, size_t sn, bool *hashOnly)
{
    fam[0] = sty[0] = 0;
    *hashOnly = false;
    if (e->family[0]) { snprintf(fam, fn, "%s", e->family); snprintf(sty, sn, "%s", e->style); }
    else if (e->fullname[0]) snprintf(fam, fn, "%s", e->fullname);
    else { snprintf(fam, fn, "%.8s", e->pxfm); *hashOnly = true; }
}

/* ------------------------------------------------------------------------------------------------ sample */

static int cmp_i32(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return x < y ? -1 : x > y;
}

static void sample_changed(Browser *b)
{
    free(b->sCps); free(b->uCps);
    b->sampleCtr = Sample_Counter();
    int nl = Sample_LineCount(Sample_Get());
    b->nLines = nl > MAX_LINES_B ? MAX_LINES_B : nl;
    int cap = (int)strlen(Sample_Get()) + 1;
    b->sCps = (int32_t *)malloc(sizeof(int32_t) * (size_t)cap);
    b->uCps = (int32_t *)malloc(sizeof(int32_t) * (size_t)cap);
    b->nS = 0;
    const char *p = Sample_Get();
    while (*p) {
        int sz = 1;
        int cp = GetCodepointNext(p, &sz);
        p += sz > 0 ? sz : 1;
        if (cp >= 32 && cp != 127) b->sCps[b->nS++] = cp;
    }
    memcpy(b->uCps, b->sCps, sizeof(int32_t) * (size_t)b->nS);
    qsort(b->uCps, (size_t)b->nS, sizeof(int32_t), cmp_i32);
    int o = 0;
    for (int i = 0; i < b->nS; i++) if (o == 0 || b->uCps[o - 1] != b->uCps[i]) b->uCps[o++] = b->uCps[i];
    b->nU = o;
    b->gen++;
}

/* ------------------------------------------------------------------------------------------------ filter and sort */

static Browser *g_sort;

static int entry_size(const LibEntry *e) { return e->info.ready && !e->info.failed ? e->info.above + e->info.below : -1; }

static int cmp_name(const LibEntry *a, const LibEntry *b)
{
    char fa[300], sa[300], fb[300], sb[300];
    bool ha, hb;
    entry_names(a, fa, sizeof fa, sa, sizeof sa, &ha);
    entry_names(b, fb, sizeof fb, sb, sizeof sb, &hb);
    if (ha != hb) return ha ? 1 : -1;          /* unnamed (hash only) last */
    int c = ci_cmp(fa, fb);
    if (!c) c = ci_cmp(sa, sb);
    if (!c) c = strcmp(a->pxfm, b->pxfm);
    return c;
}

static int cmp_entries(const void *pa, const void *pb)
{
    Browser *b = g_sort;
    int ia = *(const int *)pa, ib = *(const int *)pb;
    const LibEntry *a = &tab_lib(b)->e[ia], *e = &tab_lib(b)->e[ib];
    int c = 0;
    switch (b->sortKey) {
    case SORT_SIZE: {
        int sa = entry_size(a), sb = entry_size(e);
        if ((sa < 0) != (sb < 0)) return sa < 0 ? 1 : -1;       /* unknown always last */
        c = sa - sb;
        break; }
    case SORT_GLYPHS: {
        int ga = a->info.ready && !a->info.failed ? a->info.glyphCount : -1;
        int gb = e->info.ready && !e->info.failed ? e->info.glyphCount : -1;
        if ((ga < 0) != (gb < 0)) return ga < 0 ? 1 : -1;
        c = ga - gb;
        break; }
    case SORT_FORMAT: {
        bool ea = !a->format[0], eb = !e->format[0];
        if (ea != eb) return ea ? 1 : -1;
        c = ci_cmp(a->format, e->format);
        break; }
    default: break;
    }
    if (c) return b->sortDesc ? -c : c;
    c = cmp_name(a, e);
    if (b->sortKey == SORT_NAME && b->sortDesc) c = -c;
    return c ? c : ia - ib;
}

static bool needs_info(const Browser *b) { return b->mono || b->sortKey == SORT_SIZE || b->sortKey == SORT_GLYPHS; }

static void collect_values(Browser *b)
{
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < b->nvals[k]; i++) free(b->vals[k][i]);
        free(b->vals[k]);
        b->vals[k] = NULL; b->nvals[k] = 0;
    }
    Library *L = tab_lib(b);
    int s = tab_start(b), n = tab_count_of(b, b->tab);
    for (int k = 0; k < 2; k++) {
        char **v = (char **)calloc((size_t)n + 1, sizeof(char *));
        int m = 0;
        for (int i = 0; i < n; i++) {
            const char *val = k == 0 ? L->e[s + i].style : L->e[s + i].format;
            if (!val[0]) continue;
            int j = 0;
            for (; j < m; j++) if (strcmp(v[j], val) == 0) break;
            if (j == m) v[m++] = _strdup(val);
        }
        /* sort the values (insertion, they are few), then put "Any" first */
        for (int i = 1; i < m; i++) {
            char *t = v[i]; int j = i - 1;
            while (j >= 0 && ci_cmp(v[j], t) > 0) { v[j + 1] = v[j]; j--; }
            v[j + 1] = t;
        }
        char **out = (char **)calloc((size_t)m + 1, sizeof(char *));
        out[0] = _strdup("Any");
        for (int i = 0; i < m; i++) out[i + 1] = v[i];
        free(v);
        b->vals[k] = out; b->nvals[k] = m + 1;
        if (b->selStr[k][0]) {                   /* keep the chosen value only when this tab has it */
            bool found = false;
            for (int i = 1; i <= m; i++) if (strcmp(out[i], b->selStr[k]) == 0) found = true;
            if (!found) b->selStr[k][0] = 0;
        }
    }
}

static void sel_set(Browser *b, int vi, bool reveal)
{
    if (vi < 0 || vi >= b->nvis) return;
    b->selVis = vi; b->selHdr = false;
    entry_key(E(b, b->vis[vi]), b->selKey[b->tab], sizeof b->selKey[0]);
    if (reveal) b->reveal = true;
}

/* Selects the header of a typeface box (the typeface itself); selVis is its first variant. */
static void sel_header(Browser *b, int bi, bool reveal)
{
    if (bi < 0 || bi >= b->nbox) return;
    b->selVis = b->box[bi].v0; b->selHdr = true;
    snprintf(b->selKey[b->tab], sizeof b->selKey[0], "h:%s", E(b, b->vis[b->selVis])->pxtf);
    if (reveal) b->reveal = true;
}

typedef struct GT { int rank, var, abs; } GT;
static int cmp_gt(const void *pa, const void *pb)
{
    const GT *a = (const GT *)pa, *b = (const GT *)pb;
    if (a->rank != b->rank) return a->rank - b->rank;
    if (a->var != b->var) return a->var - b->var;
    return a->abs - b->abs;
}

static unsigned hash_str(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

/* Typefaces tab: the sorted variants become boxes, one per typeface, in the order of the first variant of each; the
 * variants of a box are in variant order.  vis is rewritten box by box. */
static void group_boxes(Browser *b, int anchorAbs)
{
    int n = b->nvis;
    if (n + 1 > b->capBox) {
        b->capBox = n + 64;
        b->box = (Box *)realloc(b->box, sizeof(Box) * (size_t)b->capBox);
        b->boxOf = (int *)realloc(b->boxOf, sizeof(int) * (size_t)b->capBox);
        b->vTop = (float *)realloc(b->vTop, sizeof(float) * (size_t)b->capBox);
        b->vH = (float *)realloc(b->vH, sizeof(float) * (size_t)b->capBox);
        b->boxHt = (float *)realloc(b->boxHt, sizeof(float) * (size_t)b->capBox);
    }
    b->nbox = 0;
    if (n == 0) return;
    unsigned tsz = 16;
    while (tsz < (unsigned)n * 2) tsz <<= 1;
    int *slot = (int *)malloc(sizeof(int) * tsz), *slotRank = (int *)malloc(sizeof(int) * tsz);
    for (unsigned i = 0; i < tsz; i++) slot[i] = -1;
    GT *g = (GT *)malloc(sizeof(GT) * (size_t)n);
    int nr = 0;
    Library *L = tab_lib(b);
    for (int i = 0; i < n; i++) {
        const LibEntry *e = &L->e[b->vis[i]];
        unsigned h = hash_str(e->pxtf) & (tsz - 1);
        while (slot[h] >= 0 && strcmp(L->e[b->vis[slot[h]]].pxtf, e->pxtf) != 0) h = (h + 1) & (tsz - 1);
        if (slot[h] < 0) { slot[h] = i; slotRank[h] = nr++; }
        g[i].rank = slotRank[h]; g[i].var = e->variant; g[i].abs = b->vis[i];
    }
    qsort(g, (size_t)n, sizeof(GT), cmp_gt);
    int bi = -1;
    for (int i = 0; i < n; i++) {
        b->vis[i] = g[i].abs;
        if (i == 0 || g[i].rank != g[i - 1].rank) { bi++; b->box[bi].v0 = i; b->box[bi].n = 0; }
        b->box[bi].n++;
        b->boxOf[i] = bi;
    }
    b->nbox = bi + 1;
    if (anchorAbs >= 0) {
        for (int i = 0; i < n; i++) if (b->vis[i] == anchorAbs) { b->anchorItem[b->tab] = b->box[b->boxOf[i]].v0; break; }
    }
    free(slot); free(slotRank); free(g);
}

static void rebuild_vis(Browser *b)
{
    int anchorAbs = -1;
    if (b->keepAnchor && b->anchorItem[b->tab] >= 0 && b->anchorItem[b->tab] < b->nvis) anchorAbs = b->vis[b->anchorItem[b->tab]];
    b->dirtyVis = false;
    b->lastRefilter = GetTime();
    Library *L = tab_lib(b);
    int s = tab_start(b), n = tab_count_of(b, b->tab);
    if (n > b->capVis) { b->capVis = n; b->vis = (int *)realloc(b->vis, sizeof(int) * (size_t)(n + 1)); }
    b->nvis = 0;
    const char *q = b->filter;
    for (int i = s; i < s + n; i++) {
        const LibEntry *e = &L->e[i];
        if (b->tfView[0] && b->tab == TAB_TYPEFACES) {
            if (strcmp(e->pxtf, b->tfView) != 0) continue;
            b->vis[b->nvis++] = i;
            continue;
        }
        if (q[0]) {
            if (!(ci_contains(e->family, q) || ci_contains(e->fullname, q) || ci_contains(e->style, q) ||
                  ci_contains(e->designer, q) || ci_contains(e->pxfm, q) || ci_contains(e->pxtf, q)))
                continue;
        }
        if (b->selStr[0][0] && strcmp(e->style, b->selStr[0]) != 0) continue;
        if (b->selStr[1][0] && strcmp(e->format, b->selStr[1]) != 0) continue;
        if (b->italic && !e->italic) continue;
        if (b->mono && !(e->info.ready && !e->info.failed && e->info.monospaced)) continue;
        b->vis[b->nvis++] = i;
    }
    g_sort = b;
    qsort(b->vis, (size_t)b->nvis, sizeof(int), cmp_entries);
    if (anchorAbs >= 0) {
        for (int i = 0; i < b->nvis; i++) if (b->vis[i] == anchorAbs) { b->anchorItem[b->tab] = i; break; }
    }
    b->keepAnchor = false;
    if (b->tab == TAB_TYPEFACES) group_boxes(b, anchorAbs); else b->nbox = 0;
    b->selVis = -1; b->selHdr = false;
    const char *key = b->selKey[b->tab];
    if (key[0]) {
        char k[1100];
        bool hdr = key[0] == 'h';
        for (int i = 0; i < b->nvis; i++) {
            if (hdr) {
                if (strcmp(L->e[b->vis[i]].pxtf, key + 2) == 0) { b->selVis = i; b->selHdr = true; break; }
                continue;
            }
            entry_key(&L->e[b->vis[i]], k, sizeof k);
            if (strcmp(k, key) == 0) { b->selVis = i; break; }
        }
    }
    if (b->tfView[0] && b->selVis < 0 && b->nvis > 0) sel_set(b, 0, true);   /* the Typeface view always has a font selected */
    if (b->selVis < 0 && b->keepSelIdx >= 0 && b->nvis > 0) {    /* the selection was deleted: the next item */
        int i = b->keepSelIdx < b->nvis ? b->keepSelIdx : b->nvis - 1;
        if (b->tab == TAB_TYPEFACES && b->keepSelHdr) sel_header(b, b->boxOf[i], false);
        else sel_set(b, i, false);
        b->reveal = true;
    }
    b->keepSelIdx = -1;
}

/* ------------------------------------------------------------------------------------------------ lifecycle */

static void tex_free(Card *c);

static void free_cards(Card *c, int n)
{
    if (!c) return;
    for (int i = 0; i < n; i++) { Sample_LayoutFree(&c[i].lay); tex_free(&c[i]); }
    free(c);
}

static void init_cards(Browser *b)
{
    free_cards(b->cl, b->lib.count);
    b->texCount = 0;
    b->cl = (Card *)calloc((size_t)(b->lib.count + 1), sizeof(Card));
    for (int i = 0; i < b->lib.count; i++) { b->cl[i].reqGen = b->cl[i].okGen = -1; b->cl[i].wantDefault = b->cl[i].defReq = PX_NO_CODEPOINT; }
}

static void scan_all(Browser *b)
{
    init_cards(b);
    collect_values(b);
    b->dirtyVis = true;
    b->gen++;
}

static void tf_load(Browser *b)
{
    char err[PX_ERR_LEN] = "";
    if (b->tfHas) pxtf_free(&b->tfCont);
    pxtf_init(&b->tfCont);
    b->tfHas = b->store && store_load_typeface(store_main_conn(b->store), b->tfView, &b->tfCont, err);
    if (!b->tfHas) pxtf_free(&b->tfCont);
}

Browser *Browser_Create(Store *store, Insight *insight, char err[PX_ERR_LEN])
{
    double t0 = GetTime();
    Browser *b = (Browser *)calloc(1, sizeof *b);
    if (!b) { snprintf(err, PX_ERR_LEN, "out of memory"); return NULL; }
    b->store = store; b->insight = insight;
    snprintf(b->root, sizeof b->root, "%s", store_info(store)->path);
    if (!library_scan(b->store, &b->lib, err)) { free(b); return NULL; }
    b->nTf = count_typefaces(&b->lib);
    b->scanT = b->lib.scanSeconds;
    b->loader = libloader_create(b->store, 0, 512);
    b->zoom = 2; b->cols = 0; b->tab = TAB_TYPEFACES; b->sortKey = SORT_NAME;
    b->selVis = -1; b->lastClickIdx = -1; b->keepSelIdx = -1;
    b->filter[0] = 0;
    sample_changed(b);
    scan_all(b);
    b->createT = GetTime() - t0;
    return b;
}

void Browser_Destroy(Browser *b)
{
    if (!b) return;
    libloader_destroy(b->loader);
    free_cards(b->cl, b->lib.count);
    for (int k = 0; k < 2; k++) { for (int i = 0; i < b->nvals[k]; i++) free(b->vals[k][i]); free(b->vals[k]); }
    library_free(&b->lib);
    if (b->tfHas) pxtf_free(&b->tfCont);
    if (b->scHas) pxtf_free(&b->scCont);
    free(b->vis); free(b->sCps); free(b->uCps);
    free(b->box); free(b->boxOf); free(b->vTop); free(b->vH); free(b->boxHt);
    free(b->rowTop); free(b->rowH); free(b->rowAbove);
    free(b);
}

const char *Browser_Root(const Browser *b) { return b->root; }
const Library *Browser_Lib(const Browser *b) { return &b->lib; }

void Browser_Follow(Browser *b, const char *oldPxtf, const char *newPxtf)
{
    if (b->tfView[0] && !strcmp(b->tfView, oldPxtf)) { snprintf(b->tfView, sizeof b->tfView, "%s", newPxtf); b->scHash[0] = 0; }
}

void Browser_Rescan(Browser *b)
{
    char err[PX_ERR_LEN];
    b->keepSelIdx = b->selVis; b->keepSelHdr = b->selHdr;
    b->scHash[0] = 0;   /* the typeface of the Typeface tab is read again */
    free_cards(b->cl, b->lib.count); b->cl = NULL;
    library_free(&b->lib);
    if (!library_scan(b->store, &b->lib, err)) memset(&b->lib, 0, sizeof b->lib);
    b->nTf = count_typefaces(&b->lib);
    b->scanT = b->lib.scanSeconds;
    scan_all(b);
    if (b->tfView[0]) {   /* the typeface may be gone or rewritten (a deleted font): follow its family, else leave the view */
        bool found = false;
        for (int i = 0; i < b->lib.nTypefaceEntries && !found; i++) found = strcmp(b->lib.e[i].pxtf, b->tfView) == 0;
        if (!found) {
            const char *np = NULL;
            for (int i = 0; i < b->lib.nTypefaceEntries && !np; i++)
                if (b->tfFamily[0] && strcmp(b->lib.e[i].family, b->tfFamily) == 0) np = b->lib.e[i].pxtf;
            if (np) { char h[33]; snprintf(h, sizeof h, "%s", np); snprintf(b->tfView, sizeof b->tfView, "%s", h); tf_load(b); }
            else Browser_CloseTypeface(b);
        }
    }
}

/* ------------------------------------------------------------------------------------------------ settings (ini) */

static const char *ini_trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

void Browser_LoadSettings(Browser *b, const char *iniPath)
{
    size_t n;
    char *raw = iniPath ? px_read_file(iniPath, &n) : NULL;
    if (!raw) return;
    bool inSec = false;
    char *p = raw;
    while (*p) {
        char *line = p;
        char *nl = strchr(p, '\n');
        if (nl) { *nl = 0; p = nl + 1; } else p += strlen(p);
        const char *l = ini_trim(line);
        if (l[0] == '[') { inSec = strncmp(l, "[browser]", 9) == 0; continue; }
        if (!inSec || l[0] == ';' || l[0] == '#') continue;
        char *eq = strchr((char *)l, '=');
        if (!eq) continue;
        *eq = 0;
        const char *key = ini_trim((char *)l);
        const char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        char v[512];
        snprintf(v, sizeof v, "%s", val);
        size_t vl = strlen(v);
        while (vl && (v[vl - 1] == '\r' || v[vl - 1] == '\n')) v[--vl] = 0;
        int iv = atoi(v);
        if (!strcmp(key, "tab")) { if (iv >= 0 && iv <= 1) b->tab = iv; }
        else if (!strcmp(key, "sidetab")) { if (iv >= 0 && iv <= 2) b->sideTab = iv; }
        else if (!strcmp(key, "zoom")) { if (iv >= 1 && iv <= 8) b->zoom = iv; }
        else if (!strcmp(key, "cols")) { if (iv >= 0 && iv <= 8) b->cols = iv; }
        else if (!strcmp(key, "sort")) { if (iv >= 0 && iv <= 3) b->sortKey = iv; }
        else if (!strcmp(key, "desc")) b->sortDesc = iv != 0;
        else if (!strcmp(key, "mono")) b->mono = iv != 0;
        else if (!strcmp(key, "italic")) b->italic = iv != 0;
        else if (!strcmp(key, "style")) snprintf(b->selStr[0], sizeof b->selStr[0], "%.159s", v);
        else if (!strcmp(key, "format")) snprintf(b->selStr[1], sizeof b->selStr[1], "%.159s", v);
        /* "sample" moved to [app] (sample.c migrates it) */
    }
    free(raw);
    collect_values(b);
    b->dirtyVis = true;
}

void Browser_SaveSettings(const Browser *b, const char *iniPath)
{
    if (!iniPath) return;
    Sample_Flush();
    size_t n = 0;
    char *old = px_read_file(iniPath, &n);
    size_t cap = (old ? n : 0) + 1024;
    char *out = (char *)malloc(cap);
    size_t o = 0;
    if (old) {
        bool inSec = false;
        char *p = old;
        while (*p) {
            char *line = p;
            char *nl = strchr(p, '\n');
            size_t ll = nl ? (size_t)(nl - p) + 1 : strlen(p);
            p += ll;
            char tmp[64];
            size_t k = 0;
            const char *t = line;
            while (*t == ' ' || *t == '\t') t++;
            while (*t && *t != '\r' && *t != '\n' && k < sizeof tmp - 1) tmp[k++] = *t++;
            tmp[k] = 0;
            if (tmp[0] == '[') inSec = strncmp(tmp, "[browser]", 9) == 0;
            if (inSec) continue;
            memcpy(out + o, line, ll);
            o += ll;
        }
        if (o && out[o - 1] != '\n') out[o++] = '\n';
        if (o > 1 && out[o - 2] != '\n') out[o++] = '\n';
    }
    char sec[1200];
    int m = snprintf(sec, sizeof sec,
                     "[browser]\ntab=%d\nsidetab=%d\nzoom=%d\ncols=%d\nsort=%d\ndesc=%d\nmono=%d\nitalic=%d\nstyle=%s\nformat=%s\n",
                     b->tab, b->sideTab, b->zoom, b->cols, b->sortKey, b->sortDesc, b->mono, b->italic, b->selStr[0], b->selStr[1]);
    if (m > 0) { memcpy(out + o, sec, (size_t)m); o += (size_t)m; }
    px_write_file(iniPath, out, o);
    free(out);
    free(old);
}

/* ------------------------------------------------------------------------------------------------ hooks */

void Browser_SetTab(Browser *b, int tab)
{
    if (tab < 0 || tab > 2) return;
    if (b->tfView[0]) Browser_CloseTypeface(b);
    if (tab == b->tab) return;
    b->tab = tab;
    collect_values(b);
    b->dirtyVis = true;
    b->selVis = -1;          /* the old index belongs to the other tab's list: never read it before the list is rebuilt */
    rebuild_vis(b);
    b->reveal = b->selKey[tab][0] != 0;
}
bool Browser_OpenTypeface(Browser *b, const char *pxtf, const char *selPxfm)
{
    int first = -1;
    for (int i = 0; i < b->lib.nTypefaceEntries && first < 0; i++) if (pxtf && strcmp(b->lib.e[i].pxtf, pxtf) == 0) first = i;
    if (first < 0) return false;
    if (b->tfView[0]) Browser_CloseTypeface(b);
    if (b->tab != TAB_TYPEFACES) Browser_SetTab(b, TAB_TYPEFACES);
    snprintf(b->tfSaveKey, sizeof b->tfSaveKey, "%s", b->selKey[TAB_TYPEFACES]);
    b->tfSaveAnchor = b->anchorItem[TAB_TYPEFACES]; b->tfSaveOff = b->anchorOff[TAB_TYPEFACES];
    snprintf(b->tfView, sizeof b->tfView, "%s", pxtf);
    snprintf(b->tfFamily, sizeof b->tfFamily, "%s", b->lib.e[first].family);
    tf_load(b);
    b->selKey[TAB_TYPEFACES][0] = 0;
    b->scrollY = 0;
    b->panelScroll = 0;
    b->selVis = -1;
    b->dirtyVis = true;
    rebuild_vis(b);
    for (int i = 0; selPxfm && selPxfm[0] && i < b->nvis; i++)
        if (strcmp(E(b, b->vis[i])->pxfm, selPxfm) == 0) { sel_set(b, i, true); break; }
    return true;
}

void Browser_CloseTypeface(Browser *b)
{
    if (!b->tfView[0]) return;
    b->tfView[0] = 0;
    b->tfFamily[0] = 0;
    if (b->tfHas) { pxtf_free(&b->tfCont); b->tfHas = false; }
    snprintf(b->selKey[TAB_TYPEFACES], sizeof b->selKey[0], "%s", b->tfSaveKey);
    b->anchorItem[TAB_TYPEFACES] = b->tfSaveAnchor; b->anchorOff[TAB_TYPEFACES] = b->tfSaveOff;
    b->selVis = -1;
    b->dirtyVis = true;
    rebuild_vis(b);
    b->reveal = b->selKey[TAB_TYPEFACES][0] != 0;
}

bool Browser_InTypeface(const Browser *b) { return b->tfView[0] != 0; }
const char *Browser_TypefaceHash(const Browser *b) { return b->tfView; }
void Browser_TypefaceName(const Browser *b, char *out, size_t n)
{
    out[0] = 0;
    if (!b->tfView[0]) return;
    const PxtfVariant *v = b->tfHas && b->tfCont.count ? &b->tfCont.v[0] : NULL;
    for (int i = 0; b->tfHas && i < b->tfCont.count; i++) if (b->tfCont.v[i].text[PXTF_FAMILY] && b->tfCont.v[i].text[PXTF_FAMILY][0]) { v = &b->tfCont.v[i]; break; }
    if (v && v->text[PXTF_FAMILY] && v->text[PXTF_FAMILY][0]) snprintf(out, n, "%s", v->text[PXTF_FAMILY]);
    else if (b->tfFamily[0]) snprintf(out, n, "%s", b->tfFamily);
}

void Browser_SetFilter(Browser *b, const char *t) { snprintf(b->filter, sizeof b->filter, "%s", t ? t : ""); b->dirtyVis = true; b->anchorItem[b->tab] = 0; b->anchorOff[b->tab] = 0; }
void Browser_SetSample(Browser *b, const char *t) { Sample_Set(t); sample_changed(b); }
void Browser_SetZoom(Browser *b, int z) { if (z >= 1 && z <= 8) b->zoom = z; }
void Browser_SetColumns(Browser *b, int c) { if (c >= 0 && c <= 8) b->cols = c; }
void Browser_ScrollBy(Browser *b, float px) { b->scrollY += px; if (b->scrollY < 0) b->scrollY = 0; b->anchorItem[b->tab] = -1; }
void Browser_SelectVisible(Browser *b, int vi) { if (b->dirtyVis) rebuild_vis(b); sel_set(b, vi, true); }
int  Browser_Pending(const Browser *b) { return libloader_pending(b->loader); }
int  Browser_ReadyVisible(const Browser *b) { return b->readyVisible; }
int  Browser_VisibleCount(const Browser *b) { return b->nvis; }
int  Browser_Tab(const Browser *b) { return b->tab; }
bool Browser_TypefaceTabShown(const Browser *b) { return b->tfTabShown; }
int  Browser_SideTab(const Browser *b) { return b->sideTab; }   /* 0 View, 1 Similar, 2 Typeface */
int  Browser_Zoom(const Browser *b) { return b->zoom; }
int  Browser_Columns(const Browser *b) { return b->cols; }
const char *Browser_FilterText(const Browser *b) { return b->filter; }
const char *Browser_SampleText(const Browser *b) { return Sample_Get(); }
int  Browser_SelectedVisible(const Browser *b) { return b->selVis; }
bool Browser_SelectedPxfm(const Browser *b, char out[33])
{
    out[0] = 0;
    if (b->selVis < 0 || b->selVis >= b->nvis) return false;
    const LibEntry *e = E((Browser *)b, b->vis[b->selVis]);
    snprintf(out, 33, "%s", e->pxfm);
    return true;
}
int  Browser_TotalCount(const Browser *b) { return tab_count_of(b, b->tab); }
void Browser_FrameStats(const Browser *b, float *updAvg, float *updMax, float *frameAvg, float *frameMax)
{
    int n = b->msN < 120 ? b->msN : 120;
    float ua = 0, um = 0, fa = 0, fm = 0;
    for (int i = 0; i < n; i++) {
        ua += b->msUpd[i]; fa += b->msFrame[i];
        if (b->msUpd[i] > um) um = b->msUpd[i];
        if (b->msFrame[i] > fm) fm = b->msFrame[i];
    }
    *updAvg = n ? ua / (float)n : 0; *updMax = um; *frameAvg = n ? fa / (float)n : 0; *frameMax = fm;
}
int  Browser_CardInkLines(const Browser *b, int vi)
{
    if (vi < 0 || vi >= b->nvis) return -1;
    const Card *c = &b->cl[b->vis[vi]];
    return c->hasLay && c->layGen == b->gen ? c->inkLines : -1;
}
int  Browser_TypefaceTotal(const Browser *b) { return b->nTf; }
int  Browser_BoxCount(const Browser *b) { return b->tab == TAB_TYPEFACES ? b->nbox : 0; }
int  Browser_BoxVariants(const Browser *b, int box) { return b->tab == TAB_TYPEFACES && box >= 0 && box < b->nbox ? b->box[box].n : -1; }
bool Browser_SelectedIsHeader(const Browser *b) { return b->tab == TAB_TYPEFACES && b->selVis >= 0 && b->selHdr; }
int  Browser_LastAction(const Browser *b) { return (int)b->lastAction; }
const char *Browser_LastPxfm(const Browser *b) { return b->lastPxfm; }
const char *Browser_LastPxtf(const Browser *b) { return b->lastPxtf; }
bool Browser_TypefaceHeaderCenter(const Browser *b, int box, Vector2 *out)
{
    if (b->tab != TAB_TYPEFACES || box < 0 || box >= b->nbox || b->lcols < 1) return false;
    int row = box / b->lcols, col = box % b->lcols;
    float x = b->lx0 + col * (b->lcw + GAP) + 70;
    float y = b->lgy + b->rowTop[row] - b->scrollY + HDR_H / 2;
    if (y < b->gridR.y || y > b->gridR.y + b->gridR.height) return false;
    *out = (Vector2){ x, y };
    return true;
}

bool Browser_CardCenter(const Browser *b, int vi, Vector2 *out)
{
    if (b->tab == TAB_TYPEFACES && vi >= 0 && vi < b->nvis && b->lcols >= 1 && b->nbox > 0) {   /* a variant row */
        int col = b->boxOf[vi] % b->lcols;
        float x = b->lx0 + col * (b->lcw + GAP) + b->lcw / 2;
        float y = b->lgy + b->vTop[vi] - b->scrollY + b->vH[vi] / 2;
        if (y < b->gridR.y || y > b->gridR.y + b->gridR.height) return false;
        *out = (Vector2){ x, y };
        return true;
    }
    if (vi < 0 || vi >= b->nvis || b->lcols < 1 || b->lnrows < 1) return false;
    int row = vi / b->lcols, col = vi % b->lcols;
    float x = b->lx0 + col * (b->lcw + GAP) + b->lcw / 2;
    float y = b->lgy + b->rowTop[row] - b->scrollY + (b->rowH[row]) / 2;
    if (y < b->gridR.y || y > b->gridR.y + b->gridR.height) return false;
    *out = (Vector2){ x, y };
    return true;
}

/* ------------------------------------------------------------------------------------------------ typed characters, select boxes */

static int (*charSrc)(void) = GetCharPressed;   /* the text source (the script player injects characters) */
void Browser_SetCharSource(int (*fn)(void)) { charSrc = fn ? fn : GetCharPressed; Sample_SetCharSource(fn); }

static void ui_dropdown(Browser *b, Rectangle r, int which, const char *name)
{
    bool any = !b->selStr[which][0];
    if (Th_Dropdown(r, name, any ? "Any" : b->selStr[which], any, b->pop.open && b->pop.which == which)) {
        b->pop.open = true; b->pop.which = which; b->pop.anchor = r; b->pop.scroll = 0;
        b->mouseEaten = true;
    }
}

#define POP_VIS 10

static int pop_count(const Browser *b) { return b->pop.which == 2 ? b->ctxN : b->nvals[b->pop.which]; }

static void pop_get(void *ud, int i, ThMenuItem *it)
{
    const Browser *b = ud;
    if (b->pop.which == 2) {   /* the context menu */
        BrowserAction a = b->ctxAct[i];
        it->label = b->ctxLabel[i];
        it->name = a == BROWSER_DELETE_FONT ? "Ctx_Delete_Font" : a == BROWSER_DELETE_TYPEFACE ? "Ctx_Delete_Typeface" : a == BROWSER_OPEN ? "Ctx_Open" : "Ctx_Lock";
        it->flags = a == BROWSER_DELETE_FONT || a == BROWSER_DELETE_TYPEFACE ? TH_MI_DANGER : 0;
    } else {
        it->label = b->vals[b->pop.which][i];
        it->flags = (i == 0 ? !b->selStr[b->pop.which][0] : !strcmp(it->label, b->selStr[b->pop.which])) ? TH_MI_CUR : i == 0 ? TH_MI_DIM : 0;
    }
}

static ThMenu pop_menu(Browser *b)
{
    return (ThMenu){.n = pop_count(b), .get = pop_get, .ud = b, .x = b->pop.anchor.x, .y = b->pop.anchor.y + b->pop.anchor.height + 4,
                    .w = b->pop.anchor.width, .vis = POP_VIS, .scroll = &b->pop.scroll};
}

/* popup input runs at the start of the frame (the click that closes it must not reach the widgets below) */
static void pop_input(Browser *b)
{
    if (b->closeEat) {
        b->mouseEaten = true;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) b->closeEat = false;
    }
    if (!b->pop.open) return;
    ThMenu m = pop_menu(b);
    bool in = pt_in(Th_MenuRect(&m), GetMousePosition());
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !in) { b->pop.open = false; b->closeEat = true; }
    if (in || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) b->mouseEaten = true;
}

static void pop_draw(Browser *b)
{
    if (!b->pop.open) return;
    ThMenu m = pop_menu(b);
    int i = Th_Menu(&m, &b->pop.open);
    if (i < 0) return;
    int k = b->pop.which;
    if (k == 2) { b->ctxPick = true; b->ctxChoice.action = b->ctxAct[i]; return; }
    b->selStr[k][0] = 0;
    if (i > 0) snprintf(b->selStr[k], sizeof b->selStr[k], "%s", b->vals[k][i]);
    b->dirtyVis = true;
    b->anchorItem[b->tab] = 0; b->anchorOff[b->tab] = 0;
}

/* ------------------------------------------------------------------------------------------------ card data */

static int card_font(Browser *b, int abs, const PxFont **pf, LibFont **plf)
{
    LibEntry *e = &b->lib.e[abs];
    Card *c = b->cl + abs;
    *pf = NULL; *plf = NULL;
    if (e->info.failed) return ST_FAILED;
    if (c->okGen != b->gen && (c->reqGen != b->gen || b->recheck)) {
        int32_t tmp[MAX_SAMPLE + 2];
        int n = b->nU;
        if (n > MAX_SAMPLE) n = MAX_SAMPLE;
        memcpy(tmp, b->uCps, sizeof(int32_t) * (size_t)n);
        if (c->wantDefault != PX_NO_CODEPOINT) tmp[n++] = c->wantDefault;
        bool have = libloader_request(b->loader, e->pxfm, tmp, n);
        c->reqGen = b->gen;
        if (have) c->okGen = b->gen;
    }
    if (c->okGen != b->gen) return ST_LOADING;
    LibFont *lf = libloader_get(b->loader, e->pxfm);
    if (!lf) { c->okGen = c->reqGen = -1; return ST_LOADING; }
    if (!e->info.ready) library_apply_info(&b->lib, e->pxfm, &lf->info);
    *plf = lf;
    *pf = &lf->font;
    return ST_READY;
}

static void layout_build(Browser *b, Card *c, const PxFont *f, const LibInfo *info, const LibFont *lf)
{
    if (c->hasLay) Sample_LayoutFree(&c->lay);
    if (c->hasTex) { tex_free(c); b->texCount--; }
    /* the box of the entry (its row height was made from it), one pixel between two lines */
    Sample_Layout(f, Sample_Get(), info->above, info->below, 1, MAX_LINES_B, &c->lay);
    c->hasLay = true;
    c->layLf = lf; c->layGen = b->gen; c->layCount = f->count;
    snprintf(c->layHash, sizeof c->layHash, "%s", lf ? lf->hash : "");
    c->wantDefault = c->lay.wantDefault;
    c->layW = c->lay.width;
    c->inkLines = 0;
    for (int ln = 0; ln < c->lay.nlines; ln++) {
        bool ink = false;
        for (int i = c->lay.lineStart[ln]; i < c->lay.lineStart[ln + 1] && !ink; i++) {
            const SampleRun *r = &c->lay.runs[i];
            if (r->gi < 0) { ink = r->gi == -1; continue; }
            const PxGlyph *g = &f->glyphs[r->gi];
            for (int k = 0; g->pix && k < g->w * g->h; k++) if (g->pix[k] != '.') { ink = true; break; }
        }
        if (ink) c->inkLines++;
    }
}

/* The sample of a card is rendered once, at 1x, into a CPU image and uploaded as a texture; every frame it is then
 * drawn as one quad scaled by the integer zoom with point filtering (pixel perfect, and cheap with 50 cards).
 * The layout and the rendering are sample.c's, shared with the font view's strip. */
static void tex_free(Card *c)
{
    if (c->hasTex) UnloadTexture(c->tex);
    c->hasTex = false;
}

static void tex_build(Browser *b, Card *c, const PxFont *f)
{
    tex_free(c);
    if (!c->hasLay || c->layW < 1) return;
    SampleBitmap bm;
    if (!Sample_Render(f, &c->lay, TH_INK, 0, &bm)) return;
    Image img = { bm.px, bm.w, bm.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    c->tex = LoadTextureFromImage(img);
    free(bm.px);
    SetTextureFilter(c->tex, TEXTURE_FILTER_POINT);
    c->hasTex = c->tex.id != 0;
    c->texTop = bm.baseline; c->texW = bm.w; c->texH = bm.h;
    b->texCount++;
    if (b->texCount > 320) {                          /* LRU: drop the least recently drawn */
        Card *victim = NULL;
        Card *arr = b->cl;
        for (int i = 0; i < b->lib.count; i++)
            if (arr[i].hasTex && &arr[i] != c && (!victim || arr[i].texUsed < victim->texUsed)) victim = &arr[i];
        if (victim) { tex_free(victim); b->texCount--; }
    }
}

/* draws the cached sample with its baseline at screen row `by`, clipped to `clip` (whole source pixels only) */
static void draw_sample(Browser *b, Card *c, const PxFont *f, Rectangle clip, int x0, int by, int z, Color bg)
{
    if (!c->hasTex) tex_build(b, c, f);
    if (!c->hasTex) return;
    c->texUsed = b->frame;
    int destTop = by - c->texTop * z;
    int sy0 = 0, sy1 = c->texH;
    int cy0 = (int)clip.y, cy1 = (int)(clip.y + clip.height);
    if (destTop < cy0) sy0 = (cy0 - destTop + z - 1) / z;
    if (destTop + sy1 * z > cy1) sy1 = (cy1 - destTop) / z;
    int sw = c->texW;
    int maxw = (int)clip.width / z;
    if (sw > maxw) sw = maxw;
    if (sy1 > sy0 && sw > 0)
        DrawTexturePro(c->tex, (Rectangle){ 0, (float)sy0, (float)sw, (float)(sy1 - sy0) },
                       (Rectangle){ (float)x0, (float)(destTop + sy0 * z), (float)(sw * z), (float)((sy1 - sy0) * z) },
                       (Vector2){ 0, 0 }, 0, WHITE);
    float rightEdge = clip.x + clip.width;
    if (c->layW * z > clip.width - 4)       /* fade the right edge when the sample is clipped */
        DrawRectangleGradientH((int)rightEdge - 28, (int)clip.y, 28, (int)clip.height, alpha(bg, 0), bg);
}

static void draw_shimmer(Rectangle r, float t, int seed)
{
    float w = r.width * (0.5f + 0.1f * (seed % 4));
    if (w > 260) w = 260;
    Rectangle bar = RR(r.x, r.y, w, r.height);
    rrect(bar, TH_LINE_SOFT, (Color){0}, 3);
    float ph = fmodf(t * 0.9f + seed * 0.13f, 1.6f) - 0.3f;
    float cx = bar.x + ph * bar.width;
    float x0 = cx - 40, x1 = cx + 40;
    if (x0 < bar.x) x0 = bar.x;
    if (x1 > bar.x + bar.width) x1 = bar.x + bar.width;
    if (x1 > x0 + 2) {
        float mid = (x0 + x1) / 2;
        DrawRectangleGradientH((int)x0, (int)bar.y, (int)(mid - x0), (int)bar.height, alpha(WHITE, 0), alpha(WHITE, 200));
        DrawRectangleGradientH((int)mid, (int)bar.y, (int)(x1 - mid), (int)bar.height, alpha(WHITE, 200), alpha(WHITE, 0));
    }
}

static void details_string(const LibEntry *e, char *out, size_t n)
{
    char *p = out;
    size_t left = n;
    out[0] = 0;
    #define ADD(...) do { int w_ = snprintf(p, left, __VA_ARGS__); if (w_ > 0 && (size_t)w_ < left) { p += w_; left -= (size_t)w_; } } while (0)
    char sep[16] = "";
    if (e->format[0]) { ADD("%s", e->format); strcpy(sep, "  \xC2\xB7  "); }
    if (e->info.ready && !e->info.failed) {
        ADD("%s%d px", sep, e->info.above + e->info.below); strcpy(sep, "  \xC2\xB7  ");
        ADD("%s%d glyph%s", sep, e->info.glyphCount, e->info.glyphCount == 1 ? "" : "s");
        if (e->info.monospaced) ADD("%sMono", sep);
        if (e->info.hasRatio) ADD("%s%d:%d", sep, e->info.ratioA, e->info.ratioB);
    } else if (!e->info.failed) ADD("%sloading\xE2\x80\xA6", sep);
    #undef ADD
}

/* ------------------------------------------------------------------------------------------------ choice */

static void make_choice(Browser *b, int vi, BrowserAction act, BrowserChoice *ch)
{
    const LibEntry *e = E(b, b->vis[vi]);
    memset(ch, 0, sizeof *ch);
    ch->action = act;
    ch->kind = e->kind;
    snprintf(ch->pxfm, sizeof ch->pxfm, "%s", e->pxfm);
    snprintf(ch->pxtf, sizeof ch->pxtf, "%s", e->pxtf);
    ch->variant = e->variant;
}

static bool can_open(Browser *b, int vi)
{
    const LibEntry *e = E(b, b->vis[vi]);
    return e->fontPresent && !e->info.failed;
}

/* The padlock of a font (or, with `typeface`, of the typeface whose pxtf is `hash`; vi is the entry that names its variant); the popup menu blocks it. */
static void lock_button(Browser *b, Rectangle r, bool typeface, const char *hash, int vi, BrowserChoice *choice, bool *chosen)
{
    if (b->pop.open || Th_InputBlocked()) Th_SetInputBlocked(true);
    bool lk = is_locked(typeface, hash);
    if (typeface) Th_Reg("tf_lock", r);   /* the typeface's own name, the label is the font lock's too */
    if (!Th_Lock(r, lk, typeface ? (lk ? "The typeface is locked: unlock it to edit its fields" : "Lock the typeface") : lk ? "Locked: unlock it to edit it" : "Unlocked: lock it")) return;
    sel_set(b, vi, false);
    make_choice(b, vi, typeface ? BROWSER_LOCK_TYPEFACE : BROWSER_LOCK, choice);
    if (typeface) snprintf(choice->pxtf, sizeof choice->pxtf, "%s", hash);
    *chosen = true;
}

/* ------------------------------------------------------------------------------------------------ the panel */

static void draw_empty_icon(float cx, float cy, Color c)
{
    static const char *A[7] = { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" };
    int s = 5;
    float x0 = cx - 5 * s / 2.0f, y0 = cy - 7 * s / 2.0f;
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 5; k++)
            if (A[r][k] == '#') DrawRectangle((int)(x0 + k * s), (int)(y0 + r * s), s - 1, s - 1, c);
}

#define SEL_H 158               /* the selection block (name, details, Open / lock) is pinned to the bottom of the panel */

/* opening a box header opens the typeface (the Typeface view); a variant row or a card opens its font */
static BrowserAction open_action(const Browser *b, bool hdr)
{
    return hdr && b->tab == TAB_TYPEFACES && !b->tfView[0] ? BROWSER_OPEN_TYPEFACE : BROWSER_OPEN;
}

/* The typeface of the selected entry (loaded once per typeface) and the variant of that entry, for the Typeface tab. */
static const PxtfTypeface *sel_typeface(Browser *b, int *variant)
{
    if (b->selVis < 0 || b->selVis >= b->nvis) return NULL;
    const LibEntry *e = E(b, b->vis[b->selVis]);
    if (!e->pxtf[0] || !b->store) return NULL;
    if (strcmp(b->scHash, e->pxtf) != 0) {
        char err[PX_ERR_LEN] = "";
        if (b->scHas) pxtf_free(&b->scCont);
        pxtf_init(&b->scCont);
        b->scHas = store_load_typeface(store_main_conn(b->store), e->pxtf, &b->scCont, err);
        if (!b->scHas) pxtf_free(&b->scCont);
        snprintf(b->scHash, sizeof b->scHash, "%s", e->pxtf);
    }
    *variant = e->variant;
    return b->scHas ? &b->scCont : NULL;
}

/* A control that needs the whole width of the panel: its label on a line of its own, the returned rectangle below it. */
static Rectangle wide_row(float x, float *y, float w, const char *label)
{
    Th_Text(label, x, *y, TH_TEXT_DIM);
    *y += 22;
    Rectangle r = { x, *y, w, TH_ROW_H };
    *y += TH_ROW_H + TH_GAP / 2;
    return r;
}

static void panel_ui(Browser *b, Rectangle P, BrowserChoice *choice, bool *chosen)
{
    DrawRectangle((int)P.x, (int)P.y, (int)P.width, (int)P.height, TH_PANEL);
    Th_VLine(P.x, P.y, P.y + P.height, TH_LINE);
    /* the tab control: View | Similar (Similar needs a card of the Fonts or Typefaces tab) */
    bool hasTfTab = b->tab == TAB_TYPEFACES;      /* the Typefaces and Typeface views: View | Typeface | Similar; the Fonts view: View | Similar */
    int side = b->sideTab == SIDE_TYPEFACE && !hasTfTab ? SIDE_VIEW : b->sideTab;
    {
        static const char *labels3[3] = { "View", "Typeface", "Similar" }, *labels2[2] = { "View", "Similar" };
        static const int ids3[3] = { SIDE_VIEW, SIDE_TYPEFACE, SIDE_SIMILAR }, ids2[2] = { SIDE_VIEW, SIDE_SIMILAR };
        const int *ids = hasTfTab ? ids3 : ids2;
        int nt = hasTfTab ? 3 : 2, cur = 0;
        for (int i = 0; i < nt; i++) if (ids[i] == side) cur = i;
        bool prevB0 = Th_InputBlocked();
        if (b->pop.open) Th_SetInputBlocked(true);
        int c = Th_Segmented(RR(P.x + PAD - 8, P.y + 8, P.width - 2 * PAD + 16, TH_ROW_H), &(ThSeg){ hasTfTab ? labels3 : labels2, nt, cur, 0u, "bside" });
        Th_SetInputBlocked(prevB0);
        if (c >= 0 && ids[c] != side) { b->sideTab = ids[c]; side = ids[c]; b->panelScroll = 0; }
        Th_HLine(P.x + 1, P.x + P.width, P.y + 43, TH_LINE);
    }
    Rectangle SR = RR(P.x, P.y + 44, P.width, P.height - SEL_H - 44);      /* the scrolling part */
    float x = P.x + PAD, w = P.width - 2 * PAD;
    float top0 = SR.y + 12 - b->panelScroll;
    float y = top0;

    /* mouse over the pinned block (or outside the panel): the scrolled widgets must not react */
    bool prevBlocked = Th_InputBlocked(), prevEaten = b->mouseEaten;
    bool overScroll = pt_in(SR, GetMousePosition());
    if (!overScroll) { Th_SetInputBlocked(true); b->mouseEaten = true; }
    if (b->pop.open) Th_SetInputBlocked(true);
    BeginScissorMode((int)SR.x + 1, (int)SR.y, (int)SR.width - 1, (int)SR.height);

    if (side == SIDE_VIEW) {
    {
        char ct[64];
        if (b->tfView[0]) snprintf(ct, sizeof ct, "%d font%s", b->nvis, b->nvis == 1 ? "" : "s");
        else snprintf(ct, sizeof ct, b->tab == TAB_TYPEFACES ? "%d of %d variants" : "%d of %d", b->nvis, tab_count_of(b, b->tab));
        float rw = Th_ButtonW("Rescan"), hy = y;
        Th_Section("LIBRARY", x, &y, w - rw - TH_GAP, 0, ct);
        if (Th_Button(RR(x + w - rw, hy - 2, rw, TH_ROW_H), "Rescan", 0, NULL)) b->wantRescan = true;
        y += TH_GAP;
    }

    if (!b->tfView[0]) {
    /* ---- filter */
    Th_Section("FILTER", x, &y, w, 0, NULL);
    {
        if (Th_TextField(RR(x, y, w, TH_ROW_H), "Filter", b->filter, (int)sizeof b->filter, TH_TF_SEARCH, "Filter by name\xE2\x80\xA6") & TH_TF_CHANGED) Browser_SetFilter(b, Th_TextValue());
        y += TH_ROW_H + TH_GAP;
    }
    ui_dropdown(b, Th_Row(x, &y, w, "Style", NULL, 0), 0, "Style_Dropdown");
    ui_dropdown(b, Th_Row(x, &y, w, "Format", NULL, 0), 1, "Format_Dropdown");
    if (Th_Switch(RR(x, y, w, TH_ROW_H), "Monospaced only", &b->mono, 0, "Monospaced_only")) b->dirtyVis = true;
    y += TH_ROW_H;
    if (Th_Switch(RR(x, y, w, TH_ROW_H), "Italic only", &b->italic, 0, "Italic_only")) b->dirtyVis = true;
    y += TH_ROW_H + TH_GAP;
    }

    /* ---- sample */
    Th_Section("SAMPLE", x, &y, w, 0, NULL);
    {
        float eh = SampleEdit_Height(6);
        if (eh < 30) eh = 30;
        Rectangle clipR = SR;
        SampleEdit_Do(&b->sampleEdit, RR(x, y, w, eh), "Sample text (Enter: new line)", "Sample", b->modalBlocked || b->pop.open, &clipR);

        y += eh + 8;
    }
    {
        static const char *names[5] = { "Pangram", "Alphabet", "Digits", "Hamburgefontsiv", "Pangram and digits (two lines)" };
        static const char *texts[5] = {
            "The quick brown fox jumps over the lazy dog",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz",
            "0123456789 !?.,;:()[]{}+-*/=",
            "Hamburgefontsiv",
            "The quick brown fox jumps over the lazy dog\n0123456789" };
        float bw = (w - 8) / 2;
        for (int i = 0; i < 5; i++) {
            Rectangle br = i < 4 ? RR(x + (i % 2) * (bw + 8), y + (i / 2) * (TH_ROW_H + 4), bw, TH_ROW_H) : RR(x, y + 2 * (TH_ROW_H + 4), w, TH_ROW_H);
            bool act = strcmp(Sample_Get(), texts[i]) == 0;
            if (Th_Toggle(br, i < 4 ? names[i] : "Two lines", &act, 0, NULL)) { Sample_Set(texts[i]); SampleEdit_SetFocus(&b->sampleEdit, false); }
        }
        y += 3 * (TH_ROW_H + 4) + TH_GAP;
    }

    /* ---- view */
    Th_Section("VIEW", x, &y, w, 0, NULL);
    {
        char zt[8];
        int c;
        snprintf(zt, sizeof zt, "%dx", b->zoom);
        Rectangle zr = Th_Row(x, &y, w, "Zoom", NULL, 0);
        c = Th_Stepper((Rectangle){ zr.x, zr.y, TH_STEP_W, TH_ROW_H }, zt, "bzoom", (b->zoom <= 1 ? TH_STEP_NO_MINUS : 0) | (b->zoom >= 8 ? TH_STEP_NO_PLUS : 0));
        if (c) b->zoom += c;
        if (!b->tfView[0]) {
        const char *cl[9] = { "Auto", "1", "2", "3", "4", "5", "6", "7", "8" };
        const float cw[9] = { 2.2f, 1, 1, 1, 1, 1, 1, 1, 1 };
        c = Th_Segmented(wide_row(x, &y, w, "Columns"), &(ThSeg){ cl, 9, b->cols, 0u, "Cols", cw });
        if (c >= 0) b->cols = c;
        static const char *base[4] = { "Name", "Size", "Format", "Glyphs" };
        Rectangle sr = wide_row(x, &y, w, "Sort by");
        static const float sw4[4] = { 1.4f, 1, 1.2f, 1.2f };
        c = Th_Segmented(sr, &(ThSeg){ base, 4, b->sortKey, 0u, "Sort", sw4 });
        {   /* direction arrow after the label of the active sort */
            float acc = 0, tot = 4.8f; for (int k = 0; k < b->sortKey; k++) acc += sw4[k];
            float sw = sr.width * sw4[b->sortKey] / tot, ax = sr.x + sr.width * acc / tot + sw / 2 + Th_Measure(base[b->sortKey]).x / 2 + 9, ay = sr.y + TH_ROW_H / 2;
            float d = b->sortDesc ? 1.0f : -1.0f;
            DrawLineEx((Vector2){ ax - 3, ay - 1.5f * d }, (Vector2){ ax, ay + 1.5f * d }, 1.2f, TH_ACCENT);
            DrawLineEx((Vector2){ ax, ay + 1.5f * d }, (Vector2){ ax + 3, ay - 1.5f * d }, 1.2f, TH_ACCENT);
        }
        if (c >= 0) {
            if (c == b->sortKey) b->sortDesc = !b->sortDesc; else { b->sortKey = c; b->sortDesc = false; }
            b->dirtyVis = true;
        }
        }
    }

    }

    /* ---- similar fonts / typefaces of the selected card (the shared Insight; it queries on a worker thread) */
    b->tfTabShown = side == SIDE_TYPEFACE;

    if (side == SIDE_TYPEFACE) {
        int var = 0;
        const PxtfTypeface *tc = sel_typeface(b, &var);
        if (tc && g_tfTab) {
            {   /* the lock of the typeface: its fields are editable while it is unlocked */
                bool lk = is_locked(true, b->scHash);
                lock_button(b, RR(x, y, TH_ROW_H, TH_ROW_H), true, b->scHash, b->selVis, choice, chosen);
                Th_Text(lk ? "Locked" : "Unlocked: edits change the typeface in place", x + TH_ROW_H + TH_GAP, y + 3, lk ? TH_TEXT_WARN : TH_TEXT);
                y += TH_ROW_H + TH_GAP;
            }
            g_tfTab(P.x, P.width, &y, tc, b->scHash, var);
        }
        else {
            const char *msg = b->selVis >= 0 && b->selVis < b->nvis ? "This font belongs to no typeface" : "Select a typeface to see its fields";
            Vector2 m = Th_Measure(msg);
            Th_Text(msg, x + floorf((w - m.x) / 2), SR.y + floorf(SR.height / 2) - 24, TH_TEXT_DIM);
        }
    }
    if (side == SIDE_SIMILAR) {
        bool tf = b->tab == TAB_TYPEFACES;
        bool haveSel = b->selVis >= 0 && b->selVis < b->nvis;
        if (b->insight && haveSel) {
            const LibEntry *e = E(b, b->vis[b->selVis]);
            Insight_SetGlyphs(b->insight, NULL, 0);
            Insight_SetFont(b->insight, e->pxfm, tf ? e->pxtf : "");
            Rectangle ar = RR(P.x, y - 4, P.width, SR.y + SR.height - y > 160 ? SR.y + SR.height - y : 160);
            bool clicked = false;
            InsightTarget tg;
            memset(&tg, 0, sizeof tg);
            Insight_SetVisible(b->insight, SR);
            float used = Insight_Draw(b->insight, ar, tf ? INSIGHT_SECTION_SIMILAR_TYPEFACES : INSIGHT_SECTION_SIMILAR_FONTS, &clicked, &tg);
            y += used - 4;
            if (clicked && tg.action != INSIGHT_NONE) {
                memset(choice, 0, sizeof *choice);
                choice->action = BROWSER_INSIGHT;
                choice->insight = tg;
                *chosen = true;
            }
        } else {
            if (b->insight) Insight_Draw(b->insight, RR(P.x, y, P.width, 0), 0, NULL, NULL);   /* nothing wanted */
            const char *msg = tf ? "Select a typeface to see similar ones" : "Select a font to see similar ones";
            Vector2 m = Th_Measure(msg);
            Th_Text(msg, x + floorf((w - m.x) / 2), SR.y + floorf(SR.height / 2) - 24, TH_TEXT_DIM);
        }
    } else if (b->insight) {
        Insight_Draw(b->insight, RR(P.x, y, P.width, 0), 0, NULL, NULL);   /* the similarity queries stop */
    }
    y += 12;
    b->panelContentH = y - top0 + 16;
    EndScissorMode();
    Th_SetInputBlocked(prevBlocked || b->pop.open);
    b->mouseEaten = prevEaten;

    Th_ScrollBar(SR, &b->panelScroll, b->panelContentH, 56, !b->pop.open);

    /* ---- selection, pinned at the bottom */
    float by = P.y + P.height - SEL_H;
    DrawRectangle((int)P.x + 1, (int)by, (int)P.width - 1, SEL_H, TH_PANEL);
    Th_HLine(P.x + PAD, P.x + P.width - PAD, by, TH_LINE_SOFT);
    by += 12;
    if (b->selVis >= 0 && b->selVis < b->nvis) {
        LibEntry *e = E(b, b->vis[b->selVis]);
        char fam[300], sty[300], det[300];
        bool ho;
        entry_names(e, fam, sizeof fam, sty, sizeof sty, &ho);
        bool hdrSel = b->tab == TAB_TYPEFACES && b->selHdr;
        if (hdrSel) sty[0] = 0;
        if (sty[0]) { strncat(fam, "  ", sizeof fam - strlen(fam) - 1); strncat(fam, sty, sizeof fam - strlen(fam) - 1); }
        if (ho) Th_HashShort(hdrSel && e->pxtf[0] ? "Typeface" : "Font", hdrSel && e->pxtf[0] ? e->pxtf : e->pxfm, x, by, 0, NULL); else Th_TextClip(fam, x, by, w, TH_TEXT, TH_F_BODY);
        by += 22;
        if (hdrSel) {
            int nv = b->box[b->boxOf[b->selVis]].n;
            snprintf(det, sizeof det, "Typeface  \xC2\xB7  %d variant%s", nv, nv == 1 ? "" : "s");
        } else details_string(e, det, sizeof det);
        Th_TextClip(det, x, by, w, TH_TEXT_DIM, TH_F_SMALL);
        by += 20;
        if (ho) {}
        else if (hdrSel && e->pxtf[0]) Th_HashShort("Typeface", e->pxtf, x, by, 0, NULL);
        else if (e->pxfm[0]) Th_HashShort("Font", e->pxfm, x, by, 0, NULL);
        by += 24;
        bool ok = can_open(b, b->selVis);
        float ow = Th_ButtonW("Open");
        if (Th_Button(RR(x, by, ow, TH_ROW_H), "Open", TH_BTN_OFF(ok), NULL)) {
            make_choice(b, b->selVis, open_action(b, hdrSel), choice);
            *chosen = true;
        }
        lock_button(b, RR(x + ow + TH_GAP, by, TH_ROW_H, TH_ROW_H), hdrSel, hdrSel ? e->pxtf : e->pxfm, b->selVis, choice, chosen);   /* of the typeface when its header is selected */
        /* the deletions, in red under the open commands (also Del and the context menu); main.c asks for the confirmation */
        {
            float dy = by + TH_ROW_H + TH_GAP;
            if (b->tab == TAB_TYPEFACES && hdrSel) {
                if (Th_ButtonDanger(RR(x, dy, w, TH_ROW_H), "Delete Typeface")) { make_choice(b, b->selVis, BROWSER_DELETE_TYPEFACE, choice); *chosen = true; }
            } else if (b->tab == TAB_TYPEFACES) {
                float hw = (w - 8) / 2;
                if (Th_ButtonDanger(RR(x, dy, hw, TH_ROW_H), "Delete Font")) { make_choice(b, b->selVis, BROWSER_DELETE_FONT, choice); *chosen = true; }
                if (Th_ButtonDanger(RR(x + hw + TH_GAP, dy, hw, TH_ROW_H), "Delete Typeface")) { make_choice(b, b->selVis, BROWSER_DELETE_TYPEFACE, choice); *chosen = true; }
            } else {
                if (Th_ButtonDanger(RR(x, dy, w, TH_ROW_H), "Delete Font")) { make_choice(b, b->selVis, BROWSER_DELETE_FONT, choice); *chosen = true; }
            }
        }
    } else {
        Th_TextSmall(b->nvis ? "Select a font to open or unlock it." : "Nothing selected.", x, by + 4, TH_TEXT_DIM);
    }
}

/* ------------------------------------------------------------------------------------------------ the grid */

static void variant_details(const LibEntry *e, char *out, size_t n)
{
    char *p = out;
    size_t left = n;
    out[0] = 0;
    #define ADD(...) do { int w_ = snprintf(p, left, __VA_ARGS__); if (w_ > 0 && (size_t)w_ < left) { p += w_; left -= (size_t)w_; } } while (0)
    const char *sep = "";
    if (e->info.failed) { ADD("failed to load"); }
    else if (e->info.ready) {
        ADD("%d px", e->info.above + e->info.below); sep = "  \xC2\xB7  ";
        ADD("%s%d glyph%s", sep, e->info.glyphCount, e->info.glyphCount == 1 ? "" : "s");
        if (e->info.monospaced) ADD("%sMono", sep);
        if (e->italic) ADD("%sItalic", sep);
        if (e->underlined) ADD("%sUnderlined", sep);
        if (e->struckout) ADD("%sStruck out", sep);
        if (e->info.hasRatio) ADD("%s%d:%d", sep, e->info.ratioA, e->info.ratioB);
    } else ADD("loading\xE2\x80\xA6");
    #undef ADD
}

/* rows over the baseline and rows from the baseline to the bottom of the last line, in font pixels */
static void sample_extent(const Browser *b, const LibEntry *e, int *ab, int *bt)
{
    int ua = 8, ub = 2;
    if (e->info.ready && !e->info.failed) { ua = e->info.above; ub = e->info.below; }
    *ab = ua;
    *bt = (b->nLines - 1) * (ua + ub + 1) + ub;
    if (*ab + *bt < 8) *ab += 8 - (*ab + *bt);
}

static void ctx_open(Browser *b, int vi, bool hdr)
{
    if (hdr && b->tab == TAB_TYPEFACES) sel_header(b, b->boxOf[vi], false); else sel_set(b, vi, false);
    b->ctxN = 0; b->ctxVi = vi; b->ctxHdr = hdr;
    #define ITEM(lbl, act) do { snprintf(b->ctxLabel[b->ctxN], sizeof b->ctxLabel[0], "%s", lbl); b->ctxAct[b->ctxN++] = (act); } while (0)
    ITEM("Open", open_action(b, hdr));
    {
        const LibEntry *ce = E(b, b->vis[vi]);
        ITEM(is_locked(hdr, hdr ? ce->pxtf : ce->pxfm) ? "Unlock" : "Lock", hdr ? BROWSER_LOCK_TYPEFACE : BROWSER_LOCK);
    }
    if (b->tab == TAB_TYPEFACES) {
        if (!hdr) ITEM("Delete Font\xE2\x80\xA6", BROWSER_DELETE_FONT);
        ITEM("Delete Typeface\xE2\x80\xA6", BROWSER_DELETE_TYPEFACE);
    } else ITEM("Delete Font\xE2\x80\xA6", BROWSER_DELETE_FONT);
    #undef ITEM
    Vector2 m = GetMousePosition();
    b->pop.open = true; b->pop.which = 2; b->pop.scroll = 0;
    b->pop.anchor = RR(m.x, m.y - 4, 210, 0);
    b->mouseEaten = true;
}



/* Del on the selection: the font, or the typeface when its header is selected */
static bool key_delete(Browser *b, BrowserChoice *choice)
{
    if (b->selVis < 0 || b->selVis >= b->nvis) return false;
    make_choice(b, b->selVis, b->tab == TAB_TYPEFACES && b->selHdr ? BROWSER_DELETE_TYPEFACE : BROWSER_DELETE_FONT, choice);
    return true;
}

/* the flat list of the Typefaces tab: per box its header row, then its variants */
static int tf_row_index(const Browser *b, int vi, bool hdr)
{
    int bi = b->boxOf[vi];
    return hdr ? bi + b->box[bi].v0 : bi + vi + 1;
}

static void tf_row_select(Browser *b, int row)
{
    int lo = 0, hi = b->nbox - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (mid + b->box[mid].v0 <= row) lo = mid; else hi = mid - 1; }
    int off = row - (lo + b->box[lo].v0);
    if (off <= 0) sel_header(b, lo, true); else sel_set(b, b->box[lo].v0 + off - 1, true);
}

typedef struct TfCtx {
    Rectangle G; int r0, r1, cols; float cardW, innerX; int z;
    int hoverBox, hoverVi; bool hoverHdr;
    BrowserChoice *choice; bool *chosen; float t;
} TfCtx;

/* draws the visible typeface boxes */
static void draw_boxes(Browser *b, const TfCtx *c)
{
    Rectangle G = c->G;
    int z = c->z;
    for (int r = c->r0; r < c->r1; r++) {
        float cy = snapf(G.y + PAD + b->rowTop[r] - b->scrollY);
        for (int k = 0; k < c->cols; k++) {
            int bi = r * c->cols + k;
            if (bi >= b->nbox) break;
            const Box *bx = &b->box[bi];
            float cx = c->innerX + k * (c->cardW + GAP), bh = b->boxHt[bi];
            Rectangle br = RR(cx, cy, c->cardW, bh);
            bool inBox = b->selVis >= 0 && b->boxOf[b->selVis] == bi;
            bool hdrSel = inBox && b->selHdr;
            bool hvBox = c->hoverBox == bi;
            const LibEntry *e0 = E(b, b->vis[bx->v0]);
            Rectangle hr = RR(cx + 1, cy + 1, c->cardW - 2, HDR_H - 1);
            Th_Card(br, (hvBox ? TH_LR_HOVER : 0) | (inBox ? TH_LR_WITHIN : 0));
            Th_ListRow(hr, (hdrSel ? TH_LR_SELECTED : 0) | (hvBox && c->hoverHdr ? TH_LR_HOVER : 0));
            /* header: family, typeface hash, format and variant count */
            {
                char fam[300], sty[300], buf[320];
                bool ho;
                entry_names(e0, fam, sizeof fam, sty, sizeof sty, &ho);
                float hw = e0->pxtf[0] ? Th_HashShort("Typeface", e0->pxtf, 0, 0, TH_HASH_MEASURE, NULL) + 12 : 0;
                float tx = cx + 14, tw = c->cardW - 28;
                Th_FitText(fam, ho ? 2 : 3, tw - hw, buf, sizeof buf);
                if (ho) Th_HashShort("Typeface", e0->pxtf[0] ? e0->pxtf : e0->pxfm, tx, cy + 12, 0, NULL); else Th_TextHead(buf, tx, cy + 6, TH_TEXT);
                if (e0->pxtf[0] && !ho) Th_HashShort("Typeface", e0->pxtf, tx + tw, cy + 12, TH_HASH_RIGHT, NULL);
                char det[160];
                snprintf(det, sizeof det, "%s%s%d variant%s", e0->format[0] ? e0->format : "", e0->format[0] ? "  \xC2\xB7  " : "", bx->n, bx->n == 1 ? "" : "s");
                Th_TextSmall(det, tx, cy + 36, TH_TEXT_DIM);
            }
            Th_HLine(cx + 14, cx + c->cardW - 14, cy + HDR_H - 1, TH_LINE_SOFT);
            /* the variants */
            for (int v = 0; v < bx->n; v++) {
                int vi = bx->v0 + v, abs = b->vis[vi];
                LibEntry *e = E(b, abs);
                Card *cd = CD(b, abs);
                cd->stamp = b->frame;
                float ry = snapf(G.y + PAD + b->vTop[vi] - b->scrollY), rh = b->vH[vi];
                if (ry + rh < G.y || ry > G.y + G.height) continue;
                bool sel = inBox && !b->selHdr && vi == b->selVis;
                bool hvR = c->hoverVi == vi;
                Color bg = Th_ListRow(RR(cx + 1, ry, c->cardW - 2, rh), (sel ? TH_LR_SELECTED : 0) | (hvR ? TH_LR_HOVER : 0));
                if (v > 0) Th_HLine(cx + 14, cx + c->cardW - 14, ry, TH_LINE_SOFT);
                float tx = cx + 14, tw = c->cardW - 28;
                char nm[200], det[200], clipd[260];
                const char *st = e->style[0] ? e->style : (e->fullname[0] ? e->fullname : "Regular");
                Th_FitText(st, 0, tw * 0.55f, nm, sizeof nm);
                Th_Text(nm, tx, ry + 6, TH_TEXT);
                variant_details(e, det, sizeof det);
                bool btn = sel || hvR;
                float bbw = TH_ROW_H;
                Th_FitText(det, 1, tw, clipd, sizeof clipd);
                Th_TextSmall(clipd, tx, ry + 32, e->info.failed ? TH_TEXT_ERR : TH_TEXT_DIM);
                if (!btn && e->pxfm[0]) Th_HashShort("Font", e->pxfm, tx + tw, ry + 9, TH_HASH_RIGHT, NULL);
                /* the sample */
                int ab, bt;
                sample_extent(b, e, &ab, &bt);
                Rectangle sbox = RR(tx, ry + V_TOP, tw, rh - V_TOP - V_BOT);
                Rectangle clip = RR(tx, ry + V_TOP - 2, tw, rh - V_TOP - V_BOT + 4);
                const PxFont *pf = NULL; LibFont *lf = NULL;
                int stt = card_font(b, abs, &pf, &lf);
                if (stt == ST_READY) {
                    if (cd->layLf != lf || cd->layGen != b->gen || cd->layCount != pf->count || (lf && strcmp(cd->layHash, lf->hash) != 0) || (!lf && cd->layLf) || !cd->hasLay)
                        layout_build(b, cd, pf, &e->info, lf);
                    if (cd->wantDefault != PX_NO_CODEPOINT && cd->defReq != cd->wantDefault && lf) {
                        int32_t tmp[MAX_SAMPLE + 2];
                        int nn = b->nU > MAX_SAMPLE ? MAX_SAMPLE : b->nU;
                        memcpy(tmp, b->uCps, sizeof(int32_t) * (size_t)nn);
                        tmp[nn++] = cd->wantDefault;
                        cd->defReq = cd->wantDefault;
                        libloader_request(b->loader, e->pxfm, tmp, nn);
                    }
                    draw_sample(b, cd, pf, clip, (int)sbox.x, (int)(ry + V_TOP + ab * z), z, bg);
                    b->readyVisible++;
                } else if (stt == ST_FAILED) {
                    Th_TextClip(e->info.err[0] ? e->info.err : "could not be read", sbox.x, sbox.y + 2, sbox.width, TH_TEXT_ERR, TH_F_SMALL);
                } else draw_shimmer(RR(sbox.x, sbox.y + 2, sbox.width, fminf(sbox.height, 4.0f + 6.0f * z)), c->t, vi);
                if (lf) libloader_release(b->loader, lf);
                if (btn) {
                    Rectangle bb = RR(cx + c->cardW - 8 - bbw, ry + 2, bbw, TH_ROW_H);
                    lock_button(b, bb, false, E(b, b->vis[vi])->pxfm, vi, c->choice, c->chosen);
                }
            }
        }
    }
}

static void tview_ui(Browser *b, Rectangle G, BrowserChoice *choice, bool *chosen);

static void grid_ui(Browser *b, Rectangle G, BrowserChoice *choice, bool *chosen)
{
    if (b->tfView[0] && b->tab == TAB_TYPEFACES) { tview_ui(b, G, choice, chosen); return; }
    int tab = b->tab;
    bool tf = tab == TAB_TYPEFACES;
    float viewH = G.height;
    float innerX = G.x + PAD;
    float availW = G.width - 2 * PAD - TH_SB_W;
    int cols = b->cols;
    if (cols == 0) cols = (int)((availW + GAP) / (MIN_CARD_W + GAP));
    if (cols < 1) cols = 1;
    float cardW = floorf((availW - GAP * (cols - 1)) / cols);
    int z = b->zoom;
    int n = b->nvis;
    int items = tf ? b->nbox : n;              /* what the grid lays out: boxes or cards */
    int nrows = (items + cols - 1) / cols;

    if (nrows + 1 > b->nrowsCap) {
        b->nrowsCap = nrows + 64;
        b->rowTop = (float *)realloc(b->rowTop, sizeof(float) * (size_t)b->nrowsCap);
        b->rowH = (float *)realloc(b->rowH, sizeof(float) * (size_t)b->nrowsCap);
        b->rowAbove = (float *)realloc(b->rowAbove, sizeof(float) * (size_t)b->nrowsCap);
    }
    float yy = 0;
    if (tf) {
        /* box heights from their variant rows; a grid row is as tall as its tallest box */
        for (int bi = 0; bi < b->nbox; bi++) {
            const Box *bx = &b->box[bi];
            float y = HDR_H;
            for (int k = 0; k < bx->n; k++) {
                int vi = bx->v0 + k;
                int ab, bt;
                sample_extent(b, E(b, b->vis[vi]), &ab, &bt);
                float sh = (float)((ab + bt) * z);
                if (sh > MAX_SAMPLE_H) sh = MAX_SAMPLE_H;
                b->vTop[vi] = y;
                b->vH[vi] = V_TOP + sh + V_BOT;
                y += b->vH[vi];
            }
            b->boxHt[bi] = y + BOX_PAD;
        }
        for (int r = 0; r < nrows; r++) {
            float h = 0;
            for (int bi = r * cols; bi < b->nbox && bi < r * cols + cols; bi++) if (b->boxHt[bi] > h) h = b->boxHt[bi];
            b->rowTop[r] = yy; b->rowAbove[r] = 0; b->rowH[r] = h;
            for (int bi = r * cols; bi < b->nbox && bi < r * cols + cols; bi++)
                for (int k = 0; k < b->box[bi].n; k++) b->vTop[b->box[bi].v0 + k] += yy;
            yy += h + GAP;
        }
    } else {
        /* row heights: the tallest known font of the row decides (all baselines of a row are aligned) */
        for (int r = 0; r < nrows; r++) {
            int ab = 0, bt = 0; bool known = false;
            for (int i = r * cols; i < n && i < r * cols + cols; i++) {
                const LibEntry *e = E(b, b->vis[i]);
                if (e->info.ready && !e->info.failed) {
                    int a2 = e->info.above, b2 = (b->nLines - 1) * (e->info.above + e->info.below + 1) + e->info.below;
                    if (a2 > ab) ab = a2;
                    if (b2 > bt) bt = b2;
                    known = true;
                }
            }
            if (!known) { ab = 8; bt = (b->nLines - 1) * 11 + 2; }
            if (ab + bt < 8) ab += 8 - (ab + bt);
            float sh = (float)((ab + bt) * z);
            if (sh > MAX_SAMPLE_H) sh = MAX_SAMPLE_H;
            b->rowTop[r] = yy;
            b->rowAbove[r] = (float)ab;
            b->rowH[r] = SAMPLE_TOP + sh + CARD_BOTTOM;
            yy += b->rowH[r] + GAP;
        }
    }
    float content = (nrows ? yy - GAP : 0) + 2 * PAD;       /* PAD above (rows start at PAD) and below */
    b->maxScroll = content > viewH ? content - viewH : 0;

    /* anchored scroll (the anchor is a visible index; with boxes, its box) */
    int anchor = b->anchorItem[tab];
    if (anchor >= 0 && nrows > 0) {
        int it = anchor;
        if (tf) it = b->boxOf[anchor < n ? anchor : n - 1];
        int ar = it / cols;
        if (ar >= nrows) ar = nrows - 1;
        b->scrollY = b->rowTop[ar] + b->anchorOff[tab];
    }
    bool mouseInGrid = hov(b, G);

    /* keyboard */
    bool typing = Th_TextIs(NULL) || Sample_TextFocused();
    if (!typing && !Th_InputBlocked() && !b->pop.open && n > 0) {
        int sv = b->selVis;
        bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);   /* Alt+Left / Alt+Right are view navigation, not selection */
        #define KP(k) (!altDown && (IsKeyPressed(k) || IsKeyPressedRepeat(k)))
        if (tf) {
            int total = b->nbox + n;
            int cur = sv < 0 ? -1 : tf_row_index(b, sv, b->selHdr);
            int target = cur;
            int bi = sv < 0 ? -1 : b->boxOf[sv];
            if (KP(KEY_DOWN)) target = cur + 1;
            else if (KP(KEY_UP)) target = cur - 1;
            else if (KP(KEY_PAGE_DOWN)) target = cur + 8;
            else if (KP(KEY_PAGE_UP)) target = cur - 8;
            else if (KP(KEY_RIGHT)) target = bi + 1 < b->nbox ? tf_row_index(b, b->box[bi + 1].v0, true) : cur;
            else if (KP(KEY_LEFT)) target = bi > 0 ? tf_row_index(b, b->box[bi - 1].v0, true) : (cur >= 0 ? 0 : cur);
            else if (IsKeyPressed(KEY_HOME)) target = 0;
            else if (IsKeyPressed(KEY_END)) target = total - 1;
            if (target != cur) {
                if (target < 0) target = 0;
                if (target >= total) target = total - 1;
                tf_row_select(b, target);
            }
        } else {
            int rowsVis = (int)(viewH / (b->rowH[0] + GAP));
            if (rowsVis < 1) rowsVis = 1;
            int d = 0;
            if (KP(KEY_RIGHT)) d = 1;
            else if (KP(KEY_LEFT)) d = -1;
            else if (KP(KEY_DOWN)) d = cols;
            else if (KP(KEY_UP)) d = -cols;
            else if (KP(KEY_PAGE_DOWN)) d = cols * rowsVis;
            else if (KP(KEY_PAGE_UP)) d = -cols * rowsVis;
            if (IsKeyPressed(KEY_HOME)) { sel_set(b, 0, true); }
            else if (IsKeyPressed(KEY_END)) { sel_set(b, n - 1, true); }
            else if (d) {
                int t = sv < 0 ? 0 : sv + d;
                if (t < 0) t = 0;
                if (t >= n) t = n - 1;
                sel_set(b, t, true);
            }
        }
        #undef KP
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && b->selVis >= 0 && can_open(b, b->selVis)) {
            make_choice(b, b->selVis, open_action(b, b->selHdr), choice);
            *chosen = true;
        }
        if (IsKeyPressed(KEY_DELETE) && key_delete(b, choice)) *chosen = true;
    }
    if (b->reveal && b->selVis >= 0 && nrows > 0) {
        float top, bot;
        if (tf) {
            int bi = b->boxOf[b->selVis];
            if (b->selHdr) { top = b->rowTop[bi / cols]; bot = top + HDR_H; }
            else { top = b->vTop[b->selVis]; bot = top + b->vH[b->selVis]; }
        } else {
            int r = b->selVis / cols;
            top = b->rowTop[r]; bot = top + b->rowH[r];
        }
        float vt = b->scrollY, vb = b->scrollY + viewH - 2 * PAD;
        if (top < vt) b->scrollY = top;
        else if (bot > vb) b->scrollY = bot - (viewH - 2 * PAD);
        b->reveal = false;
    }

    bool onBar = Th_ScrollBar(G, &b->scrollY, content, 72, !b->pop.open && !b->mouseEaten);
    if (onBar) b->mouseEaten = true;

    /* anchor from the final scroll */
    if (nrows > 0) {
        int lo = 0, hi = nrows - 1;
        while (lo < hi) { int mid = (lo + hi + 1) / 2; if (b->rowTop[mid] <= b->scrollY) lo = mid; else hi = mid - 1; }
        b->anchorItem[tab] = tf ? b->box[lo * cols].v0 : lo * cols;
        b->anchorOff[tab] = b->scrollY - b->rowTop[lo];
    } else { b->anchorItem[tab] = 0; b->anchorOff[tab] = 0; }

    b->lcols = cols; b->lx0 = innerX; b->lcw = cardW; b->lgy = G.y + PAD; b->lnrows = nrows;

    /* visible rows */
    float vTop = b->scrollY - PAD, vBot = b->scrollY + viewH;
    int r0 = 0;
    if (nrows > 0) {
        int lo = 0, hi = nrows - 1;
        while (lo < hi) { int mid = (lo + hi) / 2; if (b->rowTop[mid] + b->rowH[mid] < vTop) lo = mid + 1; else hi = mid; }
        r0 = lo;
    }
    int r1 = r0;
    while (r1 < nrows && b->rowTop[r1] < vBot) r1++;

    /* mouse: hover and click */
    Vector2 mp = GetMousePosition();
    int hoverIdx = -1, hoverBox = -1, hoverVi = -1;
    bool hoverHdr = false;
    if (mouseInGrid && !onBar) {
        for (int r = r0; r < r1; r++) {
            float cy = G.y + PAD + b->rowTop[r] - b->scrollY;
            if (mp.y < cy || mp.y >= cy + b->rowH[r]) continue;
            for (int k = 0; k < cols && r * cols + k < items; k++) {
                float cx = innerX + k * (cardW + GAP);
                if (mp.x < cx || mp.x >= cx + cardW) continue;
                if (!tf) { hoverIdx = r * cols + k; continue; }
                int bi = r * cols + k;
                float ly = mp.y - cy;
                if (ly >= b->boxHt[bi]) continue;
                hoverBox = bi;
                if (ly < HDR_H) hoverHdr = true;
                else for (int v = 0; v < b->box[bi].n; v++) {
                    int vi = b->box[bi].v0 + v;
                    float rel = b->vTop[vi] - b->rowTop[r];
                    if (ly >= rel && ly < rel + b->vH[vi]) hoverVi = vi;
                }
            }
        }
    }
    bool hit = tf ? hoverBox >= 0 : hoverIdx >= 0;
    /* the lock button of the hovered card (or variant row) eats clicks over it */
    bool overBtn = false;
    if (!tf && hoverIdx >= 0) {
        int r = hoverIdx / cols, k = hoverIdx % cols;
        Rectangle br = RR(innerX + k * (cardW + GAP) + cardW - 8 - 118, G.y + PAD + b->rowTop[r] - b->scrollY + 8, 118, 24);
        overBtn = pt_in(br, mp);
    } else if (tf && hoverVi >= 0) {
        int k = b->boxOf[hoverVi] % cols;
        Rectangle br = RR(innerX + k * (cardW + GAP) + cardW - 8 - 118, G.y + PAD + b->vTop[hoverVi] - b->scrollY + 5, 118, 22);
        overBtn = pt_in(br, mp);
    }
    if (hit && !overBtn && !b->pop.open && !Th_InputBlocked() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !b->mouseEaten) {
        int code, openVi;
        if (tf && hoverHdr) { sel_header(b, hoverBox, false); code = -2 - hoverBox; openVi = b->box[hoverBox].v0; }
        else if (tf && hoverVi >= 0) { sel_set(b, hoverVi, false); code = hoverVi; openVi = hoverVi; }
        else if (tf) { code = -1; openVi = -1; }                  /* the padding of a box */
        else { sel_set(b, hoverIdx, false); code = hoverIdx; openVi = hoverIdx; }
        Th_TextBlur();
        double t = GetTime();
        if (code != -1 && b->lastClickIdx == code && t - b->lastClickT < 0.4) {
            if (openVi >= 0 && can_open(b, openVi)) {
                make_choice(b, openVi, open_action(b, tf && hoverHdr), choice);
                *chosen = true;
            }
            b->lastClickIdx = -1;
        } else { b->lastClickIdx = code; b->lastClickT = t; }
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInGrid && !hit) Th_TextBlur();
    if (hit && !overBtn && !b->pop.open && !Th_InputBlocked() && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !b->mouseEaten) {
        if (!tf) ctx_open(b, hoverIdx, false);
        else if (hoverHdr) ctx_open(b, b->box[hoverBox].v0, true);
        else if (hoverVi >= 0) ctx_open(b, hoverVi, false);
    }

    /* wanted list and requests (visible entries, most urgent first) */
    b->frame++;
    b->readyVisible = 0;
    char (*wanted)[33] = NULL;
    int nw = 0;
    int firstVis, lastVis;
    if (tf) {
        firstVis = r0 * cols < b->nbox ? b->box[r0 * cols].v0 : n;
        lastVis = r1 * cols < b->nbox ? b->box[r1 * cols].v0 : n;
    } else {
        firstVis = r0 * cols; lastVis = r1 * cols;
        if (lastVis > n) lastVis = n;
    }
    bool needAll = needs_info(b);
    {
        wanted = (char (*)[33])malloc(33 * (size_t)(2 * b->lib.count + 8));
        for (int i = firstVis; i < lastVis; i++) {
            LibEntry *e = E(b, b->vis[i]);
            int j = 0;
            for (; j < nw; j++) if (strcmp(wanted[j], e->pxfm) == 0) break;
            if (j == nw) { memcpy(wanted[nw], e->pxfm, 33); nw++; }
        }
    }

    /* ---------------- drawing */
    BeginScissorMode((int)G.x, (int)G.y, (int)G.width, (int)G.height);
    float t = (float)GetTime();
    if (tf) {
        TfCtx tc = { G, r0, r1, cols, cardW, innerX, z, hoverBox, hoverVi, hoverHdr, choice, chosen, t };
        draw_boxes(b, &tc);
    } else
    for (int pass = 0; pass < 3; pass++) {
    /* backgrounds, then texts, then samples: few texture switches */
    for (int r = r0; r < r1; r++) {
        float cy = snapf(G.y + PAD + b->rowTop[r] - b->scrollY);
        float baseOff = SAMPLE_TOP + b->rowAbove[r] * z;
        float rowSampleH = b->rowH[r] - SAMPLE_TOP - CARD_BOTTOM;
        for (int k = 0; k < cols && r * cols + k < n; k++) {
            int vi = r * cols + k;
            int abs = b->vis[vi];
            LibEntry *e = E(b, abs);
            Card *c = CD(b, abs);
            c->stamp = b->frame;
            float cx = innerX + k * (cardW + GAP);
            Rectangle cr = RR(cx, cy, cardW, b->rowH[r]);
            bool sel = vi == b->selVis, hv = vi == hoverIdx;
            Color bg = sel ? TH_SELECTED_FILL : TH_PANEL;
            if (pass == 0) {
            Th_Card(cr, (sel ? TH_LR_SELECTED : 0) | (hv ? TH_LR_HOVER : 0));
            Th_HLine(cx + 14, cx + cardW - 14, cy + 51, TH_LINE_SOFT);
            }

            /* header */
            if (pass == 1) {
            float tx = cx + 14, tw = cardW - 28;
            int hstate = e->info.failed ? 2 : e->info.ready ? 1 : 0;
            if (c->hW != (int)cardW || c->hState != hstate || c->hW == 0) {
                char fam[300], sty[300];
                entry_names(e, fam, sizeof fam, sty, sizeof sty, &c->hHashOnly);
                float hw = e->pxfm[0] ? Th_HashShort("Font", e->pxfm, 0, 0, TH_HASH_MEASURE, NULL) + 10 : 0;
                float titleW = tw - hw;
                float famW = Th_Measure(fam).x;
                Th_FitText(fam, 0, titleW, c->hFam, sizeof c->hFam);
                c->hSty[0] = 0;
                if (sty[0] && famW + 12 < titleW) Th_FitText(sty, 0, titleW - famW - 8, c->hSty, sizeof c->hSty);
                c->hStyX = famW + 8;
                if (e->info.failed) snprintf(c->hDet, sizeof c->hDet, "failed to load");
                else { char det[300]; details_string(e, det, sizeof det); Th_FitText(det, 1, tw, c->hDet, sizeof c->hDet); }
                c->hW = (int)cardW; c->hState = hstate;
            }
            if (c->hHashOnly) Th_HashShort("Font", e->pxfm, tx, cy + 9, 0, NULL); else Th_Text(c->hFam, tx, cy + 9, TH_TEXT);
            if (c->hSty[0]) Th_Text(c->hSty, tx + c->hStyX, cy + 9, TH_TEXT_DIM);
            if (e->pxfm[0] && !c->hHashOnly && !(sel || hv)) Th_HashShort("Font", e->pxfm, tx + tw, cy + 9, TH_HASH_RIGHT, NULL);
            Th_TextSmall(c->hDet, tx, cy + 33, e->info.failed ? TH_TEXT_ERR : TH_TEXT_DIM);
            }

            /* sample */
            if (pass == 2) {
            Rectangle sbox = RR(cx + 14, cy + SAMPLE_TOP, cardW - 28, rowSampleH);
            Rectangle clip = RR(cx + 2, cy + 53, cardW - 4, b->rowH[r] - 53 - 2);
            clip.x = sbox.x; clip.width = sbox.width;
            const PxFont *pf = NULL; LibFont *lf = NULL;
            int st = card_font(b, abs, &pf, &lf);
            if (st == ST_READY) {
                if (c->layLf != lf || c->layGen != b->gen || c->layCount != pf->count || (lf && strcmp(c->layHash, lf->hash) != 0) || (!lf && c->layLf) || !c->hasLay)
                    layout_build(b, c, pf, &e->info, lf);
                if (c->wantDefault != PX_NO_CODEPOINT && c->defReq != c->wantDefault && lf) {
                    int32_t tmp[MAX_SAMPLE + 2];
                    int nn = b->nU > MAX_SAMPLE ? MAX_SAMPLE : b->nU;
                    memcpy(tmp, b->uCps, sizeof(int32_t) * (size_t)nn);
                    tmp[nn++] = c->wantDefault;
                    c->defReq = c->wantDefault;
                    libloader_request(b->loader, e->pxfm, tmp, nn);
                }
                draw_sample(b, c, pf, clip, (int)sbox.x, (int)(cy + baseOff), z, bg);
                b->readyVisible++;
            } else if (st == ST_FAILED) {
                const char *msg = e->info.err[0] ? e->info.err : "could not be read";
                Th_TextClip(msg, sbox.x, sbox.y + 2, sbox.width, TH_TEXT_ERR, TH_F_SMALL);
            } else {
                draw_shimmer(RR(sbox.x, sbox.y + 2, sbox.width, fminf(rowSampleH, 4.0f + 6.0f * z)), t, vi);
            }
            if (lf) libloader_release(b->loader, lf);

            /* hover / selected: the lock */
            if (sel || hv) {
                Rectangle br = RR(cx + cardW - 8 - TH_ROW_H, cy + 6, TH_ROW_H, TH_ROW_H);
                lock_button(b, br, false, E(b, b->vis[vi])->pxfm, vi, choice, chosen);
            }
            }
        }
    }
    }
    EndScissorMode();

    /* tell the loader what is wanted: the visible entries first, then (when a filter or sort needs the info of every
     * font) all the others whose info is still unknown */
    {
        if (needAll) {
            /* a bounded window of info-only loads: every outstanding one stays wanted, new ones are added up to 48 */
            Library *L = &b->lib;
            int s = tab_start(b), cnt = tab_count_of(b, tab);
            int outstanding = 0;
            for (int i = s; i < s + cnt; i++)
                if (b->cl[i].infoReq && !L->e[i].info.ready) {
                    LibEntry *e = &L->e[i];
                    int j = 0;
                    for (; j < nw; j++) if (strcmp(wanted[j], e->pxfm) == 0) break;
                    if (j == nw) { memcpy(wanted[nw++], e->pxfm, 33); }
                    outstanding++;
                }
            for (int i = s; i < s + cnt && outstanding < 48; i++) {
                LibEntry *e = &L->e[i];
                Card *c = &b->cl[i];
                if (e->info.ready || c->infoReq) continue;
                c->infoReq = true;
                if (libloader_request(b->loader, e->pxfm, NULL, 0)) {
                    LibFont *lf = libloader_get(b->loader, e->pxfm);
                    if (lf) { library_apply_info(L, e->pxfm, &lf->info); libloader_release(b->loader, lf); }
                    b->infoPending = true;
                } else {
                    memcpy(wanted[nw++], e->pxfm, 33);
                    outstanding++;
                }
            }
        }
        libloader_set_wanted(b->loader, (const char (*)[33])wanted, nw);
        /* a request cancelled because its card left the view is made again when it returns */
        for (int i = 0; i < b->lib.count; i++) {
            Card *c = &b->cl[i];
            if (c->reqGen == b->gen && c->okGen != b->gen && c->stamp != b->frame) c->reqGen = -1;
            if (c->infoReq && !b->lib.e[i].info.ready && !needAll) c->infoReq = false;
        }
        free(wanted);
    }
    b->recheck = false;

    /* empty states */
    if (n == 0) {
        float cx = G.x + (G.width - TH_SB_W) / 2, cy = G.y + G.height / 2 - 40;
        draw_empty_icon(cx, cy - 10, TH_LINE);
        int total = tab_count_of(b, tab);
        const char *h1, *h2;
        if (total == 0) {
            h1 = "This library is empty"; h2 = "The database has no fonts. Import a folder of pxtf, pxfm and pxgl files (File, Import Folder).";
        } else { h1 = "No fonts match"; h2 = "Nothing fits the current filters. Loosen the name, style or format filter."; }
        Vector2 m1 = Th_Measure(h1), m2 = Th_MeasureSmall(h2);
        Th_Text(h1, cx - m1.x / 2, cy + 36, TH_TEXT);
        Th_TextSmall(h2, cx - m2.x / 2, cy + 60, TH_TEXT_DIM);
        if (total > 0) {
            bool anyFilter = b->filter[0] || b->selStr[0][0] || b->selStr[1][0] || b->mono || b->italic;
            if (anyFilter && Th_Button(RR(cx - Th_ButtonW("Clear filters") / 2, cy + 86, Th_ButtonW("Clear filters"), TH_ROW_H), "Clear filters", 0, NULL)) {
                b->filter[0] = 0; b->selStr[0][0] = b->selStr[1][0] = 0; b->mono = b->italic = false;
                b->dirtyVis = true;
            }
        }
    }
}

/* ------------------------------------------------------------------------------------------------ the Typeface view */

#define TV_BAND   66             /* the typeface's heading above its fonts */
#define TV_TOP    70             /* row top to its sample */
#define TV_BOT    18

/* The fonts of one typeface as large rows: style, details, hash, the commands, and a multi-line sample (the shared one). */
static void tview_ui(Browser *b, Rectangle G, BrowserChoice *choice, bool *chosen)
{
    int n = b->nvis, z = b->zoom < 2 ? 2 : b->zoom;
    float innerX = G.x + PAD, w = G.width - 2 * PAD - TH_SB_W;
    float y = PAD + TV_BAND;
    for (int vi = 0; vi < n; vi++) {
        int ab, bt;
        sample_extent(b, E(b, b->vis[vi]), &ab, &bt);
        float sh = (float)((ab + bt) * z);
        if (sh > MAX_SAMPLE_H) sh = MAX_SAMPLE_H;
        b->vTop[vi] = y;
        b->vH[vi] = TV_TOP + sh + TV_BOT;
        y += b->vH[vi] + GAP;
    }
    float content = y + PAD;
    b->maxScroll = content > G.height ? content - G.height : 0;
    bool mouseInGrid = hov(b, G);
    bool typing = Th_TextIs(NULL) || Sample_TextFocused();
    if (!typing && !Th_InputBlocked() && !b->pop.open && n > 0) {
        bool altDown = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        #define KPT(k) (!altDown && (IsKeyPressed(k) || IsKeyPressedRepeat(k)))
        int sv = b->selVis < 0 ? 0 : b->selVis, t = sv;
        if (KPT(KEY_DOWN) || KPT(KEY_RIGHT)) t = sv + 1;
        else if (KPT(KEY_UP) || KPT(KEY_LEFT)) t = sv - 1;
        else if (KPT(KEY_PAGE_DOWN)) t = sv + 3;
        else if (KPT(KEY_PAGE_UP)) t = sv - 3;
        else if (IsKeyPressed(KEY_HOME)) t = 0;
        else if (IsKeyPressed(KEY_END)) t = n - 1;
        #undef KPT
        if (t < 0) t = 0;
        if (t >= n) t = n - 1;
        if (t != b->selVis) sel_set(b, t, true);
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && b->selVis >= 0 && can_open(b, b->selVis)) { make_choice(b, b->selVis, BROWSER_OPEN, choice); *chosen = true; }
        if (IsKeyPressed(KEY_DELETE) && key_delete(b, choice)) *chosen = true;
    }
    if (b->reveal && b->selVis >= 0) {
        float top = b->vTop[b->selVis] - PAD, bot = b->vTop[b->selVis] + b->vH[b->selVis] + PAD;
        if (top < b->scrollY) b->scrollY = top;
        else if (bot > b->scrollY + G.height) b->scrollY = bot - G.height;
        b->reveal = false;
    }
    bool onBar = Th_ScrollBar(G, &b->scrollY, content, 72, !b->pop.open && !b->mouseEaten);
    if (onBar) b->mouseEaten = true;

    Vector2 mp = GetMousePosition();
    int hoverVi = -1;
    if (mouseInGrid && !onBar)
        for (int vi = 0; vi < n; vi++) {
            float ry = G.y + b->vTop[vi] - b->scrollY;
            if (mp.y >= ry && mp.y < ry + b->vH[vi] && mp.x >= innerX && mp.x < innerX + w) hoverVi = vi;
        }

    b->frame++;
    b->readyVisible = 0;
    char (*wanted)[33] = (char (*)[33])malloc(33 * (size_t)(n + 1));
    int nw = 0;
    BeginScissorMode((int)G.x, (int)G.y, (int)G.width, (int)G.height);
    float t = (float)GetTime();
    /* the heading */
    {
        char nm[300], fit[320], sub[200];
        Browser_TypefaceName(b, nm, sizeof nm);
        Th_FitText(nm, 3, w - 160, fit, sizeof fit);
        float hy = snapf(G.y + PAD - b->scrollY);
        if (nm[0]) {
            Th_TextHead(fit, innerX + 2, hy + 4, TH_TEXT);
            Th_HashShort("Typeface", b->tfView, innerX + w - 2, hy + 6, TH_HASH_RIGHT, NULL);
        } else Th_HashShort("Typeface", b->tfView, innerX + 2, hy + 6, 0, NULL);
        const char *fmt = b->tfHas && b->tfCont.count && b->tfCont.v[0].format ? b->tfCont.v[0].format : "";
        snprintf(sub, sizeof sub, "%d font%s%s%s", n, n == 1 ? "" : "s", fmt[0] ? "  \xC2\xB7  " : "", fmt);
        Th_TextSmall(sub, innerX + 2, hy + 34, TH_TEXT_DIM);
        if (n > 0) {   /* the typeface's lock (the Typeface tab has the same button); its fields are editable while it is unlocked */
            lock_button(b, RR(innerX + w - TH_ROW_H - 2, hy + 28, TH_ROW_H, TH_ROW_H), true, b->tfView, b->selVis >= 0 ? b->selVis : 0, choice, chosen);
        }
    }
    for (int vi = 0; vi < n; vi++) {
        int abs = b->vis[vi];
        LibEntry *e = E(b, abs);
        Card *cd = CD(b, abs);
        float ry = snapf(G.y + b->vTop[vi] - b->scrollY), rh = b->vH[vi];
        if (ry + rh < G.y || ry > G.y + G.height) continue;
        cd->stamp = b->frame;
        int j = 0;
        for (; j < nw; j++) if (strcmp(wanted[j], e->pxfm) == 0) break;
        if (j == nw) { memcpy(wanted[nw], e->pxfm, 33); nw++; }
        bool sel = vi == b->selVis, hv = vi == hoverVi;
        Color bg = Th_Card(RR(innerX, ry, w, rh), (sel ? TH_LR_SELECTED : 0) | (hv ? TH_LR_HOVER : 0));
        float tx = innerX + 16, tw = w - 32;
        /* the commands of this font */
        float bx = innerX + w - 12;
        bool over = false;
        {
            const char *lab[2] = { "Delete Font", "Open" };
            if (b->pop.open || Th_InputBlocked()) Th_SetInputBlocked(true);
            {   /* the lock of the font */
                bx -= TH_ROW_H;
                Rectangle lr = RR(bx, ry + 8, TH_ROW_H, TH_ROW_H);
                lock_button(b, lr, false, e->pxfm, vi, choice, chosen);
                if (pt_in(lr, mp)) over = true;
                bx -= TH_GAP;
            }
            for (int k = 0; k < 2; k++) {
                float bwk = Th_ButtonW(lab[k]);
                bx -= bwk;
                Rectangle br = RR(bx, ry + 8, bwk, TH_ROW_H);
                bool dang = k == 0;
                bool hit = Th_Button(br, lab[k], dang ? TH_BTN_DANGER : 0, NULL);
                char rn[32];
                snprintf(rn, sizeof rn, "tv.%s.%d", k == 0 ? "delete" : "open", vi);
                Th_Reg(rn, br);
                if (pt_in(br, mp)) over = true;
                if (hit) {
                    sel_set(b, vi, false);
                    if (dang) { make_choice(b, vi, BROWSER_DELETE_FONT, choice); *chosen = true; }
                    else if (can_open(b, vi)) { make_choice(b, vi, BROWSER_OPEN, choice); *chosen = true; }
                }
                bx -= TH_GAP;
            }
            Th_SetInputBlocked(b->modalBlocked || b->pop.open);
        }
        char nm[200], det[260], clipd[260];
        const char *st = e->style[0] ? e->style : (e->fullname[0] ? e->fullname : "Regular");
        Th_FitText(st, 0, bx - tx, nm, sizeof nm);
        Th_Text(nm, tx, ry + 11, TH_TEXT);
        variant_details(e, det, sizeof det);
        float hw = Th_HashShort("Font", e->pxfm, 0, 0, TH_HASH_MEASURE, NULL);
        Th_FitText(det, 1, tw - hw - 16, clipd, sizeof clipd);
        Th_TextSmall(clipd, tx, ry + 40, e->info.failed ? TH_TEXT_ERR : TH_TEXT_DIM);
        if (e->pxfm[0]) Th_HashShort("Font", e->pxfm, tx + tw, ry + 38, TH_HASH_RIGHT, NULL);
        Th_HLine(tx, tx + tw, ry + TV_TOP - 8, TH_LINE_SOFT);
        /* the sample */
        int ab, bt;
        sample_extent(b, e, &ab, &bt);
        Rectangle sbox = RR(tx, ry + TV_TOP, tw, rh - TV_TOP - TV_BOT);
        Rectangle clip = RR(tx, ry + TV_TOP - 2, tw, rh - TV_TOP - TV_BOT + 4);
        const PxFont *pf = NULL; LibFont *lf = NULL;
        int stt = card_font(b, abs, &pf, &lf);
        if (stt == ST_READY) {
            if (cd->layLf != lf || cd->layGen != b->gen || cd->layCount != pf->count || (lf && strcmp(cd->layHash, lf->hash) != 0) || (!lf && cd->layLf) || !cd->hasLay)
                layout_build(b, cd, pf, &e->info, lf);
            if (cd->wantDefault != PX_NO_CODEPOINT && cd->defReq != cd->wantDefault && lf) {
                int32_t tmp[MAX_SAMPLE + 2];
                int nn = b->nU > MAX_SAMPLE ? MAX_SAMPLE : b->nU;
                memcpy(tmp, b->uCps, sizeof(int32_t) * (size_t)nn);
                tmp[nn++] = cd->wantDefault;
                cd->defReq = cd->wantDefault;
                libloader_request(b->loader, e->pxfm, tmp, nn);
            }
            draw_sample(b, cd, pf, clip, (int)sbox.x, (int)(ry + TV_TOP + ab * z), z, bg);
            b->readyVisible++;
        } else if (stt == ST_FAILED) {
            Th_TextClip(e->info.err[0] ? e->info.err : "could not be read", sbox.x, sbox.y + 2, sbox.width, TH_TEXT_ERR, TH_F_SMALL);
        } else draw_shimmer(RR(sbox.x, sbox.y + 2, sbox.width, fminf(sbox.height, 4.0f + 6.0f * z)), t, vi);
        if (lf) libloader_release(b->loader, lf);
        /* click and double click on the row (not on its buttons) */
        if (hv && !over && !b->pop.open && !Th_InputBlocked() && !b->mouseEaten) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                sel_set(b, vi, false);
                Th_TextBlur();
                double tt = GetTime();
                if (b->lastClickIdx == vi && tt - b->lastClickT < 0.4) {
                    if (can_open(b, vi)) { make_choice(b, vi, BROWSER_OPEN, choice); *chosen = true; }
                    b->lastClickIdx = -1;
                } else { b->lastClickIdx = vi; b->lastClickT = tt; }
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) ctx_open(b, vi, false);
        }
    }
    EndScissorMode();
    libloader_set_wanted(b->loader, (const char (*)[33])wanted, nw);
    for (int i = 0; i < b->lib.count; i++) {
        Card *c = &b->cl[i];
        if (c->reqGen == b->gen && c->okGen != b->gen && c->stamp != b->frame) c->reqGen = -1;
    }
    free(wanted);
    b->recheck = false;
}

/* ------------------------------------------------------------------------------------------------ the frame */

static void poll_results(Browser *b)
{
    double t0 = GetTime();
    LibLoadResult r;
    int n = 0;
    while (n < 256 && libloader_poll(b->loader, &r)) {
        library_apply_result(&b->lib, &r);
        if (r.font) libloader_release(b->loader, r.font);
        b->recheck = true;
        n++;
        if (GetTime() - t0 > 0.003) break;
    }
    if (n && needs_info(b)) b->infoPending = true;
}

bool Browser_UpdateDraw(Browser *b, Rectangle area, BrowserChoice *choice)
{
    bool chosen = false;
    double tStart = GetTime();
    bool prevBlocked = Th_InputBlocked();
    b->mouseEaten = false;
    b->modalBlocked = prevBlocked;
    memset(choice, 0, sizeof *choice);

    if (Sample_Counter() != b->sampleCtr) sample_changed(b);
    poll_results(b);
    if (b->infoPending && GetTime() - b->lastRefilter > 0.25) { b->infoPending = false; b->dirtyVis = true; b->keepAnchor = true; }
    if (b->dirtyVis) rebuild_vis(b);
    pop_input(b);
    if (!Th_TextIs(NULL) && !Sample_TextFocused()) { while (charSrc() > 0) {} }
    else if (prevBlocked && !Th_TextIsIn("nf.")) Th_TextBlur();   /* a dialog over the browser owns its own fields (the New Font dialog) */

    DrawRectangle((int)area.x, (int)area.y, (int)area.width, (int)area.height, TH_BG);
    Rectangle panelR = RR(area.x + area.width - PANEL_W, area.y, PANEL_W, area.height - STATUS_H);
    Rectangle gridR = RR(area.x, area.y + TOP_H, area.width - PANEL_W, area.height - STATUS_H - TOP_H);
    b->gridR = gridR;
    float noteH = 0;
    (void)noteH;

    /* mouse-over-popup blocks the Th_* widgets */
    if (b->pop.open) Th_SetInputBlocked(true);

    /* the text fields live in the panel: Ctrl+F focuses the name filter */
    if (!prevBlocked && !b->pop.open && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_F)) {
        Th_TextFocus("Filter", b->filter, true);
        while (charSrc() > 0) {}
    }

    int gen0 = b->gen;
    grid_ui(b, gridR, choice, &chosen);
    Th_HLine(gridR.x, gridR.x + gridR.width, gridR.y, TH_LINE_SOFT);
    Th_SetInputBlocked(prevBlocked || b->pop.open);
    bool chosen2 = false;
    BrowserChoice c2;
    memset(&c2, 0, sizeof c2);
    panel_ui(b, panelR, &c2, &chosen2);
    if (chosen2 && !chosen) { *choice = c2; chosen = true; }
    (void)gen0;

    pop_draw(b);
    Th_SetInputBlocked(prevBlocked);
    if (b->ctxPick && !chosen) {         /* an item of the context menu */
        b->ctxPick = false;
        BrowserAction act = b->ctxChoice.action;
        if (b->ctxVi >= 0 && b->ctxVi < b->nvis) { make_choice(b, b->ctxVi, act, choice); chosen = true; }
    }
    b->ctxPick = false;
    if (chosen && choice->action == BROWSER_OPEN_TYPEFACE) {   /* opened a box header: switch to the Typeface view (after the frame's drawing) */
        b->lastAction = choice->action;
        snprintf(b->lastPxfm, sizeof b->lastPxfm, "%s", choice->pxfm);
        snprintf(b->lastPxtf, sizeof b->lastPxtf, "%s", choice->pxtf);
        snprintf(b->pendingTf, sizeof b->pendingTf, "%s", choice->pxtf);
        chosen = false;
        memset(choice, 0, sizeof *choice);
    }
    if (b->pendingTf[0]) { char h[33]; snprintf(h, sizeof h, "%s", b->pendingTf); b->pendingTf[0] = 0; Browser_OpenTypeface(b, h, NULL); }
    if (b->wantRescan) { b->wantRescan = false; Browser_Rescan(b); }
    b->msUpd[b->msN % 120] = (float)((GetTime() - tStart) * 1000.0);
    b->msFrame[b->msN % 120] = GetFrameTime() * 1000.0f;
    b->msN++;
    if (chosen && choice->action != BROWSER_NONE) {
        b->lastAction = choice->action;
        snprintf(b->lastPxfm, sizeof b->lastPxfm, "%s", choice->pxfm);
        snprintf(b->lastPxtf, sizeof b->lastPxtf, "%s", choice->pxtf);
        return true;
    }
    return false;
}
