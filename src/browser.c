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
#define BROWSER_MAX_COLS 16     /* the most columns the Columns stepper sets */
#define SAMPLE_EDIT_LINES 12    /* the sample field grows with its text up to this many lines, then scrolls */
#define SAMPLE_TOP   58          /* card top to the sample box */
#define CARD_BOTTOM  16
#define MAX_SAMPLE_H 900
#define MAX_LINES_B  8           /* lines of the sample a card shows */
#define HDR_H        58          /* typeface box: header height */
#define V_TOP        54          /* typeface variant row: top to the sample */
#define V_BOT        12
#define BOX_PAD      8
#define PLUS_SZ      (HDR_H + BOX_PAD)   /* the "+" after the typeface boxes: a square as tall as a box with no variant */
#define MAX_SAMPLE   400         /* bytes of the sample text */

enum { TAB_TYPEFACES = 0, TAB_FONTS = 1 };
enum { SORT_NAME = 0, SORT_SIZE, SORT_FORMAT, SORT_GLYPHS };
enum { ST_LOADING = 0, ST_READY, ST_FAILED };

/* ------------------------------------------------------------------------------------------------ types */

typedef struct Box { int v0, n; } Box;     /* a typeface box: the variants vis[v0 .. v0+n) */

typedef struct Card {
    int reqGen, okGen;             /* sample generation requested / known complete in the loader */
    PxCode wantDefault, defReq;    /* default glyph code to load as well / already requested, PX_NO_CODE none */
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
    bool open; int which;          /* 0 style, 1 format, 2 colours, 3 context menu */
    Rectangle anchor; int scroll;
} Popup;

enum { SIDE_VIEW = 0, SIDE_SIMILAR = 1, SIDE_TYPEFACE = 2, SIDE_FONT = 3 };
static BrowserInfoTab g_infoTab;
void Browser_SetInfoTab(BrowserInfoTab draw) { g_infoTab = draw; }
static BrowserLocked g_locked;
void Browser_SetLockQuery(BrowserLocked locked) { g_locked = locked; }
/* the state mark of an unlocked font or typeface (an open padlock at the right end of r); nothing while it is locked */
static float unlock_mark(float xr, float y, bool typeface, const char *hash)
{
    if (!g_locked || !hash[0] || g_locked(typeface, hash)) return 0;
    Th_Icon(TH_IC_UNLOCK, (Rectangle){ xr - 18, y, 18, 18 }, TH_ACCENT);
    return 22;
}

struct Browser {
    Store *store; Insight *insight;
    char root[1100];
    float panelScroll, panelContentH;
    Library lib;
    LibLoader *loader;
    Card *cl;                      /* per entry of lib */
    int nTf;                       /* typefaces (boxes) among the entries of the Typefaces tab */

    /* view state */
    int tab, zoom, cols, sortKey; bool sortDesc, mono, colorOnly;   /* cols 0: Auto; colorOnly: only fonts of more than one colour */
    int autoCols;                  /* the columns Auto gives at the current width (the Columns stepper shows it) */
    int sideTab;                   /* side panel: SIDE_* (shown as Typeface | Font | Similar; SIDE_VIEW is no tab any more) */
    ThViewArea viewArea;           /* the View area above the side panel's tabs (open: kept in the ini) */
    bool tfTabShown;               /* the Typeface or Font tab was drawn this frame */
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

    char selStr[3][160];           /* chosen style / format / number of colours ("" = Any) */
    char **vals[3]; int nvals[3];  /* values present in the current tab, [0] is "Any" */
    Popup pop;
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

    /* sample */
    int32_t *sCps; int nS;         /* the sample, in order */
    PxCode *uCps; int nU;          /* their glyph codes, unique, for requests */
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
static bool hov(const Browser *b, Rectangle r) { return !Th_MouseBlocked() && !b->mouseEaten && pt_in(r, GetMousePosition()); }
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

static int cmp_code(const void *a, const void *b)
{
    PxCode x = *(const PxCode *)a, y = *(const PxCode *)b;
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
    b->uCps = (PxCode *)malloc(sizeof(PxCode) * (size_t)cap);
    b->nS = 0;
    const char *p = Sample_Get();
    while (*p) {
        int sz = 1;
        int cp = GetCodepointNext(p, &sz);
        p += sz > 0 ? sz : 1;
        if (cp >= 32 && cp != 127) b->sCps[b->nS++] = cp;
    }
    for (int i = 0; i < b->nS; i++) b->uCps[i] = px_code_cp(b->sCps[i]);
    qsort(b->uCps, (size_t)b->nS, sizeof(PxCode), cmp_code);
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

/* the colour filter needs the info of every font while it is set or its dropdown is open (to list the counts that occur) */
static bool needs_info(const Browser *b)
{
    return b->mono || b->colorOnly || b->sortKey == SORT_SIZE || b->sortKey == SORT_GLYPHS;
}

/* the number of ink colours of an entry, 0 while its info is not known */
static int entry_colors(const LibEntry *e) { return e->info.ready && !e->info.failed ? e->info.colors : 0; }

/* The values of the colour dropdown: "Any" and the counts of the fonts whose info is known, in numeric order.
 * The chosen count is dropped only when the info of every font of the tab is known and none has it. */
static void collect_colors(Browser *b)
{
    for (int i = 0; i < b->nvals[2]; i++) free(b->vals[2][i]);
    free(b->vals[2]);
    Library *L = tab_lib(b);
    int s = tab_start(b), n = tab_count_of(b, b->tab);
    int *cs = (int *)calloc((size_t)n + 1, sizeof(int));
    int m = 0;
    bool allKnown = true;
    for (int i = 0; i < n; i++) {
        int c = entry_colors(&L->e[s + i]);
        if (c <= 0) { allKnown = false; continue; }
        int j = m;
        while (j > 0 && cs[j - 1] > c) j--;
        if (j > 0 && cs[j - 1] == c) continue;
        memmove(cs + j + 1, cs + j, sizeof(int) * (size_t)(m - j));
        cs[j] = c; m++;
    }
    char **out = (char **)calloc((size_t)m + 1, sizeof(char *));
    out[0] = _strdup("Any");
    bool found = false;
    for (int i = 0; i < m; i++) {
        char t[16]; snprintf(t, sizeof t, "%d", cs[i]);
        out[i + 1] = _strdup(t);
        if (strcmp(t, b->selStr[2]) == 0) found = true;
    }
    free(cs);
    b->vals[2] = out; b->nvals[2] = m + 1;
    if (b->selStr[2][0] && !found && allKnown) b->selStr[2][0] = 0;
}

static void collect_values(Browser *b)
{
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < b->nvals[k]; i++) free(b->vals[k][i]);
        free(b->vals[k]);
        b->vals[k] = NULL; b->nvals[k] = 0;
    }
    for (int i = 0; i < b->nvals[2]; i++) free(b->vals[2][i]);
    free(b->vals[2]); b->vals[2] = NULL; b->nvals[2] = 0;
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
    collect_colors(b);
}

static void sel_header(Browser *b, int bi, bool reveal);

static void sel_set(Browser *b, int vi, bool reveal)
{
    if (vi < 0 || vi >= b->nvis) return;
    if (b->tab == TAB_TYPEFACES && E(b, b->vis[vi])->variant == 0 && b->nbox > 0) { sel_header(b, b->boxOf[vi], reveal); return; }   /* a typeface with no variant */
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
    collect_colors(b);
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
        if (b->mono && !(e->info.ready && !e->info.failed && e->info.monospaced)) continue;
        if (b->colorOnly && entry_colors(e) < 2) continue;
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
    for (int i = 0; i < b->lib.count; i++) { b->cl[i].reqGen = b->cl[i].okGen = -1; b->cl[i].wantDefault = b->cl[i].defReq = PX_NO_CODE; }
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
    b->viewArea.open = true;
    b->store = store; b->insight = insight;
    snprintf(b->root, sizeof b->root, "%s", store_info(store)->path);
    if (!library_scan(b->store, &b->lib, err)) { free(b); return NULL; }
    b->nTf = count_typefaces(&b->lib);
    b->scanT = b->lib.scanSeconds;
    b->loader = libloader_create(b->store, 0, 512);
    b->zoom = 2; b->cols = 0; b->tab = TAB_TYPEFACES; b->sortKey = SORT_NAME;
    b->selVis = -1; b->keepSelIdx = -1;
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
    for (int k = 0; k < 3; k++) { for (int i = 0; i < b->nvals[k]; i++) free(b->vals[k][i]); free(b->vals[k]); }
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
        else if (!strcmp(key, "sidetab")) { if (iv >= 0 && iv <= 3) b->sideTab = iv; }
        else if (!strcmp(key, "viewopen")) b->viewArea.open = iv != 0;
        else if (!strcmp(key, "zoom")) { if (iv >= 1 && iv <= 8) b->zoom = iv; }
        else if (!strcmp(key, "cols")) { if (iv >= 0 && iv <= BROWSER_MAX_COLS) b->cols = iv; }
        else if (!strcmp(key, "sort")) { if (iv >= 0 && iv <= 3 && iv != SORT_FORMAT) b->sortKey = iv; }   /* Format is no sort any more */
        else if (!strcmp(key, "desc")) b->sortDesc = iv != 0;
        else if (!strcmp(key, "mono")) b->mono = iv != 0;
        else if (!strcmp(key, "style")) snprintf(b->selStr[0], sizeof b->selStr[0], "%.159s", v);
        else if (!strcmp(key, "colouronly")) b->colorOnly = iv != 0;
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
                     "[browser]\ntab=%d\nsidetab=%d\nviewopen=%d\nzoom=%d\ncols=%d\nsort=%d\ndesc=%d\nmono=%d\ncolouronly=%d\nstyle=%s\n",
                     b->tab, b->sideTab, b->viewArea.open, b->zoom, b->cols, b->sortKey, b->sortDesc, b->mono, b->colorOnly, b->selStr[0]);
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

void Browser_SelectTypeface(Browser *b, const char *pxtf)
{
    if (b->tfView[0]) Browser_CloseTypeface(b);
    if (b->tab != TAB_TYPEFACES) Browser_SetTab(b, TAB_TYPEFACES);
    snprintf(b->selKey[TAB_TYPEFACES], sizeof b->selKey[0], "h:%s", pxtf);
    b->sideTab = SIDE_TYPEFACE;
    b->dirtyVis = true;
    rebuild_vis(b);
    b->reveal = true;
}

void Browser_SetFilter(Browser *b, const char *t) { snprintf(b->filter, sizeof b->filter, "%s", t ? t : ""); b->dirtyVis = true; b->anchorItem[b->tab] = 0; b->anchorOff[b->tab] = 0; }
void Browser_SetSample(Browser *b, const char *t) { Sample_Set(t); sample_changed(b); }
void Browser_SetZoom(Browser *b, int z) { if (z >= 1 && z <= 8) b->zoom = z; }
void Browser_SetColumns(Browser *b, int c) { if (c >= 0 && c <= BROWSER_MAX_COLS) b->cols = c; }
void Browser_ScrollBy(Browser *b, float px) { b->scrollY += px; if (b->scrollY < 0) b->scrollY = 0; b->anchorItem[b->tab] = -1; }
void Browser_SelectVisible(Browser *b, int vi) { if (b->dirtyVis) rebuild_vis(b); sel_set(b, vi, true); }
int  Browser_Pending(const Browser *b) { return libloader_pending(b->loader); }
int  Browser_ReadyVisible(const Browser *b) { return b->readyVisible; }
int  Browser_VisibleCount(const Browser *b) { return b->nvis; }
int  Browser_Tab(const Browser *b) { return b->tab; }
bool Browser_TypefaceTabShown(const Browser *b) { return b->tfTabShown; }
int  Browser_SideTab(const Browser *b) { return b->sideTab; }
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
int  Browser_ColorFilter(const Browser *b) { return b->colorOnly; }
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

static int pop_count(const Browser *b) { return b->nvals[b->pop.which]; }

static void pop_get(void *ud, int i, ThMenuItem *it)
{
    const Browser *b = ud;
    it->label = b->vals[b->pop.which][i];
    it->flags = (i == 0 ? !b->selStr[b->pop.which][0] : !strcmp(it->label, b->selStr[b->pop.which])) ? TH_MI_CUR : i == 0 ? TH_MI_DIM : 0;
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
        PxCode tmp[MAX_SAMPLE + 2];
        int n = b->nU;
        if (n > MAX_SAMPLE) n = MAX_SAMPLE;
        memcpy(tmp, b->uCps, sizeof(PxCode) * (size_t)n);
        if (c->wantDefault != PX_NO_CODE) tmp[n++] = c->wantDefault;
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

/* The sample of entry abs in sbox (its baseline at baseY): the font laid out and drawn, its error, or a shimmer while it loads. */
static void entry_sample(Browser *b, int abs, Rectangle sbox, Rectangle clip, int baseY, float shimmerH, int z, Color bg, float t, int seed)
{
    LibEntry *e = E(b, abs);
    Card *cd = CD(b, abs);
    const PxFont *pf = NULL; LibFont *lf = NULL;
    int st = card_font(b, abs, &pf, &lf);
    if (st == ST_READY) {
        if (cd->layLf != lf || cd->layGen != b->gen || cd->layCount != pf->count || (lf && strcmp(cd->layHash, lf->hash) != 0) || (!lf && cd->layLf) || !cd->hasLay)
            layout_build(b, cd, pf, &e->info, lf);
        if (cd->wantDefault != PX_NO_CODE && cd->defReq != cd->wantDefault && lf) {
            PxCode tmp[MAX_SAMPLE + 2];
            int nn = b->nU > MAX_SAMPLE ? MAX_SAMPLE : b->nU;
            memcpy(tmp, b->uCps, sizeof(PxCode) * (size_t)nn);
            tmp[nn++] = cd->wantDefault;
            cd->defReq = cd->wantDefault;
            libloader_request(b->loader, e->pxfm, tmp, nn);
        }
        draw_sample(b, cd, pf, clip, (int)sbox.x, baseY, z, bg);
        b->readyVisible++;
    } else if (st == ST_FAILED) Th_TextClip(e->info.err[0] ? e->info.err : "could not be read", sbox.x, sbox.y + 2, sbox.width, TH_TEXT_ERR, TH_F_SMALL);
    else draw_shimmer(RR(sbox.x, sbox.y + 2, sbox.width, fminf(shimmerH, 4.0f + 6.0f * z)), t, seed);
    if (lf) libloader_release(b->loader, lf);
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
        if (e->info.symbolic) ADD("%sSymbolic", sep);
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

/* The View area above the side panel's tabs, with no captions of its own: the name filter, Style, Monospaced and Colours; Zoom, Columns (a
 * stepper and Auto) and Sort by (Name, Size, Glyphs, beside its label); then the sample field, growing with its lines, and the presets A C D H P.
 * The Typeface view has only the zoom and the sample. */
static void view_area(Browser *b, float x, float w, float *yp, Rectangle clip)
{
    float y = *yp + 4;
    int c;
    if (!b->tfView[0]) {   /* ---- filter */
        if (Th_TextField(RR(x, y, w, TH_ROW_H), "Filter", b->filter, (int)sizeof b->filter, TH_TF_SEARCH, "Filter by name\xE2\x80\xA6") & TH_TF_CHANGED) Browser_SetFilter(b, Th_TextValue());
        y += TH_ROW_H + TH_GAP;
        ui_dropdown(b, Th_Row(x, &y, w, "Style", NULL, 0), 0, "Style_Dropdown");
        float hw = floorf((w - TH_GAP) / 2);
        if (Th_Switch(RR(x, y, hw, TH_ROW_H), "Monospaced", &b->mono, 0, "Monospaced")) b->dirtyVis = true;
        if (Th_Switch(RR(x + hw + TH_GAP, y, w - hw - TH_GAP, TH_ROW_H), "Colours", &b->colorOnly, 0, "Colours")) b->dirtyVis = true;
        y += TH_ROW_H + TH_GAP;
    }

    {   /* ---- the cards: zoom, columns, sort */
        char zt[8];
        snprintf(zt, sizeof zt, "%dx", b->zoom);
        Rectangle zr = Th_Row(x, &y, w, "Zoom", NULL, 0);
        c = Th_Stepper((Rectangle){ zr.x, zr.y, TH_STEP_W, TH_ROW_H }, zt, "bzoom", (b->zoom <= 1 ? TH_STEP_NO_MINUS : 0) | (b->zoom >= 8 ? TH_STEP_NO_PLUS : 0));
        if (c) b->zoom += c;
        if (!b->tfView[0]) {
        {   /* Columns: [-] n [+], and Auto (the columns from the width; the stepper shows how many that is) */
            int n = b->cols ? b->cols : b->autoCols;
            if (n < 1) n = 1;
            char ct[12];
            snprintf(ct, sizeof ct, "%d", n);
            Rectangle cr = Th_Row(x, &y, w, "Columns", NULL, 0);
            c = Th_Stepper((Rectangle){ cr.x, cr.y, TH_STEP_W, TH_ROW_H }, ct, "bcols", (n <= 1 ? TH_STEP_NO_MINUS : 0) | (n >= BROWSER_MAX_COLS ? TH_STEP_NO_PLUS : 0));
            if (c) { n += c; b->cols = n < 1 ? 1 : n > BROWSER_MAX_COLS ? BROWSER_MAX_COLS : n; }
            float aw = Th_ButtonW("Auto");
            Rectangle ar = RR(x + w - aw, cr.y, aw, TH_ROW_H);
            bool au = b->cols == 0;
            Th_Reg("Cols_Auto", ar);
            if (Th_Toggle(ar, "Auto", &au, 0, "Columns from the width of the window")) b->cols = au ? 0 : n;
        }
        static const char *base[3] = { "Name", "Size", "Glyphs" };
        static const int sortIds[3] = { SORT_NAME, SORT_SIZE, SORT_GLYPHS };
        int at = 0;
        for (int k = 0; k < 3; k++) if (sortIds[k] == b->sortKey) at = k;
        Rectangle sr = Th_Row(x, &y, w, "Sort by", NULL, 0);   /* the segments beside the label */
        static const float sw3[3] = { 1.4f, 1, 1.2f };
        c = Th_Segmented(sr, &(ThSeg){ base, 3, at, 0u, "Sort", sw3 });
        {   /* direction arrow after the label of the active sort */
            float acc = 0, tot = 3.6f; for (int k = 0; k < at; k++) acc += sw3[k];
            float sw = sr.width * sw3[at] / tot, ax = sr.x + sr.width * acc / tot + sw / 2 + Th_Measure(base[at]).x / 2 + 9, ay = sr.y + TH_ROW_H / 2;
            float d = b->sortDesc ? 1.0f : -1.0f;
            DrawLineEx((Vector2){ ax - 3, ay - 1.5f * d }, (Vector2){ ax, ay + 1.5f * d }, 1.2f, TH_ACCENT);
            DrawLineEx((Vector2){ ax, ay + 1.5f * d }, (Vector2){ ax + 3, ay - 1.5f * d }, 1.2f, TH_ACCENT);
        }
        if (c >= 0) {
            if (sortIds[c] == b->sortKey) b->sortDesc = !b->sortDesc; else { b->sortKey = sortIds[c]; b->sortDesc = false; }
            b->dirtyVis = true;
        }
        }
        y += TH_GAP;
    }

    {   /* ---- sample: the field, as tall as its lines */
        float eh = SampleEdit_Height(SAMPLE_EDIT_LINES);
        Rectangle clipR = clip;
        SampleEdit_Do(&b->sampleEdit, RR(x, y, w, eh), "Sample text (Enter: new line)", "Sample", b->modalBlocked || b->pop.open, &clipR);
        y += eh + 8;
    }
    {   /* the presets in one row: Alphabet, Custom (the text as edited last), Digits, Hamburgefontsiv, Pangram */
        static const char *letters[5] = { "A", "C", "D", "H", "P" };
        static const char *names[5] = { "Alphabet", "Custom", "Digits", "Hamburgefontsiv", "Pangram" };
        static const char *tips[5] = { "Alphabet", "Custom: the text as you edited it last", "Digits", "Hamburgefontsiv", "Pangram" };
        static const char *texts[5] = {
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz", NULL,
            "0123456789 !?.,;:()[]{}+-*/=",
            "Hamburgefontsiv",
            "The quick brown fox jumps over the lazy dog" };
        int cur = 1;
        for (int i = 0; i < 5; i++) if (texts[i] && strcmp(Sample_Get(), texts[i]) == 0) cur = i;
        float bw = floorf((w - 4 * 4) / 5);
        for (int i = 0; i < 5; i++) {
            Rectangle br = RR(x + i * (bw + 4), y, i == 4 ? w - 4 * (bw + 4) : bw, TH_ROW_H);
            bool act = i == cur;
            Th_Reg(names[i], br);
            if (Th_Toggle(br, letters[i], &act, 0, tips[i]) && i != cur) {
                Sample_Set(texts[i] ? texts[i] : Sample_Custom());
                SampleEdit_SetFocus(&b->sampleEdit, false);
            }
        }
        y += TH_ROW_H;
    }

    *yp = y;
}

static void panel_ui(Browser *b, Rectangle P, BrowserChoice *choice, bool *chosen)
{
    DrawRectangle((int)P.x, (int)P.y, (int)P.width, (int)P.height, TH_PANEL);
    Th_VLine(P.x, P.y, P.y + P.height, TH_LINE);
    /* the View area, then the tabs: Typeface | Font | Similar in the Typefaces and Typeface views, Font | Similar in the Fonts view */
    if (b->sideTab == SIDE_VIEW) b->sideTab = SIDE_FONT;   /* View was a tab once */
    {
        bool prevB0 = Th_InputBlocked();
        if (b->pop.open) Th_SetInputBlocked(true);
        float vy;
        if (Th_ViewAreaBegin(&b->viewArea, P, (P.height - TH_VIEW_HDR) / 2, &vy)) { view_area(b, P.x + PAD, P.width - 2 * PAD, &vy, b->viewArea.view); Th_ViewAreaEnd(&b->viewArea, vy); }
        Th_SetInputBlocked(prevB0);
        float bt = Th_ViewAreaBottom(&b->viewArea);
        P.height -= bt - P.y;
        P.y = bt;
    }
    bool hasTfTab = b->tab == TAB_TYPEFACES;
    int side = b->sideTab == SIDE_TYPEFACE && !hasTfTab ? SIDE_FONT : b->sideTab;
    {
        static const char *labels[3] = { "Typeface", "Font", "Similar" }, *labels3[2] = { "Font", "Similar" };
        static const int ids4[3] = { SIDE_TYPEFACE, SIDE_FONT, SIDE_SIMILAR }, ids3[2] = { SIDE_FONT, SIDE_SIMILAR };
        const int *ids = hasTfTab ? ids4 : ids3;
        int nt = hasTfTab ? 3 : 2, cur = 0;
        for (int i = 0; i < nt; i++) if (ids[i] == side) cur = i;
        bool prevB0 = Th_InputBlocked();
        if (b->pop.open) Th_SetInputBlocked(true);
        int c = Th_Segmented(RR(P.x + PAD - 8, P.y + 8, P.width - 2 * PAD + 16, TH_ROW_H), &(ThSeg){ hasTfTab ? labels : labels3, nt, cur, 0u, "bside" });
        Th_SetInputBlocked(prevB0);
        if (c >= 0 && ids[c] != side) { b->sideTab = ids[c]; side = ids[c]; b->panelScroll = 0; }
        Th_HLine(P.x + 1, P.x + P.width, P.y + 43, TH_LINE);
    }
    Rectangle SR = RR(P.x, P.y + 44, P.width, P.height - 44);      /* the scrolling part */
    float x = P.x + PAD, w = P.width - 2 * PAD;
    float top0 = SR.y + 12 - b->panelScroll;
    float y = top0;

    bool prevBlocked = Th_InputBlocked(), prevMouseB = Th_MouseOnly(), prevEaten = b->mouseEaten;
    if (!pt_in(SR, GetMousePosition())) { Th_SetMouseBlocked(true); b->mouseEaten = true; }   /* the mouse only: a focused field keeps its keys */
    if (b->pop.open) Th_SetInputBlocked(true);
    BeginScissorMode((int)SR.x + 1, (int)SR.y, (int)SR.width - 1, (int)SR.height);


    /* ---- the Typeface and Font tabs: drawn by the application (fields, lock, commands) */
    b->tfTabShown = side == SIDE_TYPEFACE || side == SIDE_FONT;
    if (b->tfTabShown) {
        bool haveSel = b->selVis >= 0 && b->selVis < b->nvis;
        bool hdr = haveSel && b->tab == TAB_TYPEFACES && b->selHdr;
        int var = 0;
        const PxtfTypeface *tc = haveSel ? sel_typeface(b, &var) : NULL;
        const LibEntry *e = haveSel ? E(b, b->vis[b->selVis]) : NULL;
        const char *msg = !haveSel ? (side == SIDE_TYPEFACE ? "Select a typeface to see its fields" : "Select a font to see its fields")
                        : side == SIDE_TYPEFACE && !tc ? "This font belongs to no typeface"
                        : side == SIDE_FONT && hdr ? "Select a font of the typeface" : NULL;
        if (msg) {
            Vector2 m = Th_Measure(msg);
            Th_Text(msg, x + floorf((w - m.x) / 2), SR.y + floorf(SR.height / 2) - 24, TH_TEXT_DIM);
        } else if (g_infoTab) g_infoTab(side == SIDE_TYPEFACE, P.x, P.width, &y, tc, tc ? b->scHash : "", var, e->pxfm);
    }
    /* ---- similar fonts / typefaces of the selected card (the shared Insight; it queries on a worker thread) */
    if (side == SIDE_SIMILAR) {
        bool tf = b->tab == TAB_TYPEFACES;
        bool haveSel = b->selVis >= 0 && b->selVis < b->nvis && E(b, b->vis[b->selVis])->pxfm[0];
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
    Th_SetMouseBlocked(prevMouseB);
    b->mouseEaten = prevEaten;

    Th_ScrollBar(SR, &b->panelScroll, b->panelContentH, 56, !b->pop.open);
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
        if (e->info.symbolic) ADD("%sSymbolic", sep);
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

/* the row of a typeface with no variant under its header (it is never selected: its header is) */
static bool tf_row_placeholder(const Browser *b, int row)
{
    int lo = 0, hi = b->nbox - 1;
    if (hi < 0) return false;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (mid + b->box[mid].v0 <= row) lo = mid; else hi = mid - 1; }
    int off = row - (lo + b->box[lo].v0);
    return off > 0 && E((Browser *)b, b->vis[b->box[lo].v0 + off - 1])->variant == 0;
}

/* a box of a typeface with no variant */
static bool box_empty(const Browser *b, int bi) { return E((Browser *)b, b->vis[b->box[bi].v0])->variant == 0; }

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
                float tx = cx + 14, tw = c->cardW - 28;
                /* row 1: the family name, the whole width; row 2: format and variants, and at the right the typeface hash and the lock */
                float mk = unlock_mark(tx + tw, cy + 34, true, e0->pxtf);
                float hw = e0->pxtf[0] && !ho ? Th_HashShort("Typeface", e0->pxtf, 0, 0, TH_HASH_MEASURE | TH_HASH_SMALL, NULL) + 12 : 0;
                Th_FitText(fam, ho ? 2 : 3, tw, buf, sizeof buf);
                if (ho) Th_HashShort("Typeface", e0->pxtf[0] ? e0->pxtf : e0->pxfm, tx, cy + 12, 0, NULL); else Th_TextHead(buf, tx, cy + 6, TH_TEXT);
                if (e0->pxtf[0] && !ho) Th_HashShort("Typeface", e0->pxtf, tx + tw - mk, cy + 36, TH_HASH_RIGHT | TH_HASH_SMALL, NULL);
                char det[160], detf[200];
                int nv = box_empty(b, bi) ? 0 : bx->n;
                snprintf(det, sizeof det, "%s%s%d variant%s", e0->format[0] ? e0->format : "", e0->format[0] ? "  \xC2\xB7  " : "", nv, nv == 1 ? "" : "s");
                Th_FitText(det, 1, tw - mk - hw, detf, sizeof detf);
                Th_TextSmall(detf, tx, cy + 36, TH_TEXT_DIM);
            }
            if (!box_empty(b, bi)) Th_HLine(cx + 14, cx + c->cardW - 14, cy + HDR_H - 1, TH_LINE_SOFT);
            /* the variants */
            for (int v = 0; v < bx->n; v++) {
                int vi = bx->v0 + v, abs = b->vis[vi];
                LibEntry *e = E(b, abs);
                if (e->variant == 0) continue;   /* a typeface with no variant: the header only */
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
                Th_FitText(st, 0, tw, nm, sizeof nm);
                Th_Text(nm, tx, ry + 6, TH_TEXT);
                /* row 2: size and glyphs, and at the right the font hash (small) and the lock, as in the box header */
                float mk = unlock_mark(tx + tw, ry + 30, false, e->pxfm);
                float hw = e->pxfm[0] ? Th_HashShort("Font", e->pxfm, 0, 0, TH_HASH_MEASURE | TH_HASH_SMALL, NULL) + 12 : 0;
                variant_details(e, det, sizeof det);
                Th_FitText(det, 1, tw - mk - hw, clipd, sizeof clipd);
                Th_TextSmall(clipd, tx, ry + 32, e->info.failed ? TH_TEXT_ERR : TH_TEXT_DIM);
                if (e->pxfm[0]) Th_HashShort("Font", e->pxfm, tx + tw - mk, ry + 32, TH_HASH_RIGHT | TH_HASH_SMALL, NULL);
                /* the sample */
                int ab, bt;
                sample_extent(b, e, &ab, &bt);
                Rectangle sbox = RR(tx, ry + V_TOP, tw, rh - V_TOP - V_BOT);
                Rectangle clip = RR(tx, ry + V_TOP - 2, tw, rh - V_TOP - V_BOT + 4);
                entry_sample(b, abs, sbox, clip, (int)(ry + V_TOP + ab * z), sbox.height, z, bg, c->t, vi);
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
    b->autoCols = (int)((availW + GAP) / (MIN_CARD_W + GAP));
    if (b->autoCols < 1) b->autoCols = 1;
    if (cols == 0) cols = b->autoCols;
    float cardW = floorf((availW - GAP * (cols - 1)) / cols);
    int z = b->zoom;
    int n = b->nvis;
    int items = tf ? b->nbox : n;              /* what the grid lays out: boxes or cards */
    bool plus = tf;                            /* the Typefaces tab: a "+" square after the boxes (a new, empty typeface) */
    int nrows = (items + (plus ? 1 : 0) + cols - 1) / cols;

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
                if (E(b, b->vis[vi])->variant == 0) { b->vTop[vi] = y; b->vH[vi] = 0; continue; }   /* no variant: no row */
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
            if (plus && b->nbox / cols == r && h < PLUS_SZ) h = PLUS_SZ;
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
    if (anchor >= 0 && nrows > 0 && n > 0) {
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
                int dir = target > cur ? 1 : -1;
                while (tf_row_placeholder(b, target) && target + dir >= 0 && target + dir < total) target += dir;
                if (tf_row_placeholder(b, target)) target -= 1;   /* the header of that box */
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
        b->anchorItem[tab] = tf ? (lo * cols < b->nbox ? b->box[lo * cols].v0 : n > 0 ? b->box[b->nbox - 1].v0 : 0) : lo * cols;
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
    /* a press selects; a press on the entry that is selected already opens it */
    if (hit && !b->pop.open && !Th_InputBlocked() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !b->mouseEaten) {
        int openVi = -1;
        bool was = false;
        if (tf && hoverHdr) { was = b->selHdr && b->selVis >= 0 && b->boxOf[b->selVis] == hoverBox; sel_header(b, hoverBox, false); openVi = b->box[hoverBox].v0; }
        else if (tf && hoverVi >= 0) { was = !b->selHdr && b->selVis == hoverVi; sel_set(b, hoverVi, false); openVi = hoverVi; }
        else if (!tf) { was = b->selVis == hoverIdx; sel_set(b, hoverIdx, false); openVi = hoverIdx; }
        Th_TextBlur();
        if (was && openVi >= 0 && can_open(b, openVi)) { make_choice(b, openVi, open_action(b, tf && hoverHdr), choice); *chosen = true; }
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInGrid && !hit) Th_TextBlur();

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
            if (!e->pxfm[0]) continue;   /* a typeface with no variant */
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
        int pr = b->nbox / cols;
        if (plus && pr >= r0 && pr < r1) {   /* the "+": a new, empty typeface */
            float px = innerX + (b->nbox % cols) * (cardW + GAP), py = snapf(G.y + PAD + b->rowTop[pr] - b->scrollY);
            Rectangle sq = RR(px, py, PLUS_SZ, PLUS_SZ);
            Th_Card(sq, hov(b, sq) && !b->pop.open ? TH_LR_HOVER : 0);
            bool pb = Th_InputBlocked();
            if (b->pop.open || onBar) Th_SetInputBlocked(true);
            Th_Reg("NewTypeface", sq);
            if (Th_Button(RR(px + 1, py + 1, PLUS_SZ - 2, PLUS_SZ - 2), "New Typeface", TH_BTN_ICON(TH_IC_PLUS), "A new, empty typeface: Add Font in its Typeface tab gives it fonts")) {
                memset(choice, 0, sizeof *choice);
                choice->action = BROWSER_NEW_TYPEFACE;
                *chosen = true;
            }
            Th_SetInputBlocked(pb);
        }
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
                float hw = e->pxfm[0] && !c->hHashOnly ? Th_HashShort("Font", e->pxfm, 0, 0, TH_HASH_MEASURE | TH_HASH_SMALL, NULL) + 12 + 22 : 0;   /* row 2: the hash and room for the lock */
                float titleW = tw;
                float famW = Th_Measure(fam).x;
                Th_FitText(fam, 0, titleW, c->hFam, sizeof c->hFam);
                c->hSty[0] = 0;
                if (sty[0] && famW + 12 < titleW) Th_FitText(sty, 0, titleW - famW - 8, c->hSty, sizeof c->hSty);
                c->hStyX = famW + 8;
                if (e->info.failed) snprintf(c->hDet, sizeof c->hDet, "failed to load");
                else { char det[300]; details_string(e, det, sizeof det); Th_FitText(det, 1, tw - hw, c->hDet, sizeof c->hDet); }
                c->hW = (int)cardW; c->hState = hstate;
            }
            if (c->hHashOnly) Th_HashShort("Font", e->pxfm, tx, cy + 9, 0, NULL); else Th_Text(c->hFam, tx, cy + 9, TH_TEXT);
            if (c->hSty[0]) Th_Text(c->hSty, tx + c->hStyX, cy + 9, TH_TEXT_DIM);
            Th_TextSmall(c->hDet, tx, cy + 33, e->info.failed ? TH_TEXT_ERR : TH_TEXT_DIM);
            {   /* row 2, at the right: the lock and the font hash (small), as in a typeface box */
                float mk = unlock_mark(tx + tw, cy + 31, false, e->pxfm);
                if (e->pxfm[0] && !c->hHashOnly) Th_HashShort("Font", e->pxfm, tx + tw - mk, cy + 33, TH_HASH_RIGHT | TH_HASH_SMALL, NULL);
            }
            }

            /* sample */
            if (pass == 2) {
            Rectangle sbox = RR(cx + 14, cy + SAMPLE_TOP, cardW - 28, rowSampleH);
            Rectangle clip = RR(cx + 2, cy + 53, cardW - 4, b->rowH[r] - 53 - 2);
            clip.x = sbox.x; clip.width = sbox.width;
            entry_sample(b, abs, sbox, clip, (int)(cy + baseOff), rowSampleH, z, bg, t, vi);

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
        } else { h1 = "No fonts match"; h2 = "Nothing fits the current filters. Loosen the name, style, format or colour filter."; }
        Vector2 m1 = Th_Measure(h1), m2 = Th_MeasureSmall(h2);
        Th_Text(h1, cx - m1.x / 2, cy + 36, TH_TEXT);
        Th_TextSmall(h2, cx - m2.x / 2, cy + 60, TH_TEXT_DIM);
        if (total > 0) {
            bool anyFilter = b->filter[0] || b->selStr[0][0] || b->mono || b->colorOnly;
            if (anyFilter && Th_Button(RR(cx - Th_ButtonW("Clear filters") / 2, cy + 86, Th_ButtonW("Clear filters"), TH_ROW_H), "Clear filters", 0, NULL)) {
                b->filter[0] = 0; b->selStr[0][0] = 0; b->mono = b->colorOnly = false;
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
            Th_HashShort("Typeface", b->tfView, innerX + w - 2, Th_BesideHead(hy + 4), TH_HASH_RIGHT, NULL);
        } else Th_HashShort("Typeface", b->tfView, innerX + 2, hy + 6, 0, NULL);
        const char *fmt = b->tfHas && b->tfCont.count && b->tfCont.v[0].format ? b->tfCont.v[0].format : "";
        snprintf(sub, sizeof sub, "%d font%s%s%s", n, n == 1 ? "" : "s", fmt[0] ? "  \xC2\xB7  " : "", fmt);
        Th_TextSmall(sub, innerX + 2, hy + 34, TH_TEXT_DIM);
        unlock_mark(innerX + w - 2, hy + 34, true, b->tfView);
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
        char nm[200], det[260], clipd[260];
        const char *st = e->style[0] ? e->style : (e->fullname[0] ? e->fullname : "Regular");
        Th_FitText(st, 0, tw, nm, sizeof nm);
        Th_Text(nm, tx, ry + 11, TH_TEXT);
        /* row 2: size and glyphs, and at the right the font hash (small) and the lock, as in the cards */
        float mk = unlock_mark(tx + tw, ry + 38, false, e->pxfm);
        float hw = e->pxfm[0] ? Th_HashShort("Font", e->pxfm, 0, 0, TH_HASH_MEASURE | TH_HASH_SMALL, NULL) + 12 : 0;
        variant_details(e, det, sizeof det);
        Th_FitText(det, 1, tw - mk - hw, clipd, sizeof clipd);
        Th_TextSmall(clipd, tx, ry + 40, e->info.failed ? TH_TEXT_ERR : TH_TEXT_DIM);
        if (e->pxfm[0]) Th_HashShort("Font", e->pxfm, tx + tw - mk, ry + 40, TH_HASH_RIGHT | TH_HASH_SMALL, NULL);
        Th_HLine(tx, tx + tw, ry + TV_TOP - 8, TH_LINE_SOFT);
        /* the sample */
        int ab, bt;
        sample_extent(b, e, &ab, &bt);
        Rectangle sbox = RR(tx, ry + TV_TOP, tw, rh - TV_TOP - TV_BOT);
        Rectangle clip = RR(tx, ry + TV_TOP - 2, tw, rh - TV_TOP - TV_BOT + 4);
        entry_sample(b, abs, sbox, clip, (int)(ry + TV_TOP + ab * z), sbox.height, z, bg, t, vi);
        /* a press selects the row; a press on the selected row opens its font */
        if (hv && !b->pop.open && !Th_InputBlocked() && !b->mouseEaten && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool was = vi == b->selVis;
            sel_set(b, vi, false);
            Th_TextBlur();
            if (was && can_open(b, vi)) { make_choice(b, vi, BROWSER_OPEN, choice); *chosen = true; }
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
    else if (prevBlocked && !Th_TextIsIn("nf.") && !Th_TextIsIn("af.")) Th_TextBlur();   /* a dialog over the browser owns its own fields (the New Font dialog) */

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
