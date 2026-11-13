/* editor.c - the glyph editing view of pxfe (see DESIGN.md, "Glyph view").
 *
 * Coordinates: "glyph space" is (x, r): x = column from the pen, r = row counted DOWN from the baseline line
 * (the pixel row that sits on the baseline is r = -1, the first row below it r = 0).  A glyph with offsets (x, y)
 * and height h has its top row at r = -(y + h).  The screen position of the cell (x, r) is
 * (ox + x*zoom, oy + r*zoom) with integer ox, oy (the pen on the baseline) and zoom.
 * The font box runs from r = -above to r = below (px_font_box). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "layers.h"
#include "sample.h"
#include "theme.h"

#define TOOLBAR_H 40
#define PALETTE_H 34
#define STATUS_H TH_ROW_H
#define MARGIN 3          /* cells of free space around the box when fitting */
#define MAX_BITMAP 4096

typedef enum { T_PENCIL = 0, T_ERASER, T_LINE, T_RECT, T_FILL, T_COUNT } Tool;
typedef enum { M_NONE = 0, M_PAINT, M_SHAPE, M_PAN, M_ADVANCE, M_LAYER } DragMode;

static const char *kToolName[T_COUNT] = {"Pencil", "Eraser", "Line", "Rectangle", "Fill"};
static const char *kToolKey[T_COUNT] = {"B", "E", "L", "R", "F"};

struct Editor {
    PxFont *font;
    int gi;
    EditorBeforeChange before;
    void *user;
    bool changed;
    bool readOnly;      /* an original: the glyph view is a viewer */
    bool hasOrig;       /* origHash is known (the pxgl hash of this glyph when the font was opened) */
    char origHash[33];
    int above, below;   /* the font box of the current frame */
    bool editOpen;      /* beforeChange already called for the current stroke / command */

    Tool tool;
    char ink;
    bool checker;
    bool showAll;       /* all-37-characters popup open */
    bool showMetrics;   /* the metric lines are drawn */
    bool layersActive;  /* the Layers tab of the right panel is showing (set by main.c; lights the bar's Layers toggle) */
    bool layersReq;     /* the Layers toggle was clicked (main.c selects the tab) */
    Layers *layers;     /* the reference layers (session) */
    bool curHidden;     /* the edited glyph's layer is hidden: no editing */

    int zoom, vx, vy;   /* view: origin of cell (0,0) relative to the canvas rectangle */
    bool needFit, userZoom;
    float lastW, lastH;

    DragMode mode;
    int btn;            /* mouse button that started the drag */
    int sx, sy, lx, ly; /* stroke start / last cell */
    Vector2 panLast;
    int dragGrab;       /* advance drag: cell offset */

    bool hasHover;
    int hx, hy;

    EditorResult result;
    const char *tip;
    Rectangle tipRect;
    Rectangle popupRect; /* all-chars popup, valid while showAll */
    Rectangle cv;        /* canvas rectangle of the last frame (for scripted input) */
};

/* ------------------------------------------------------------------ small helpers */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int floordiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

static PxGlyph *G(Editor *e)
{
    if (!e->font || e->gi < 0 || e->gi >= e->font->count) return NULL;
    return &e->font->glyphs[e->gi];
}

/* pixel at glyph-space (x, y); '.' outside the bitmap */
static int gtop(const PxGlyph *g) { return -(g->y + g->h); }
static char gget(const PxGlyph *g, int x, int r) { return px_glyph_get(g, x - g->x, r - gtop(g)); }

/* nothing may change the glyph: an original, or its layer is hidden */
static bool locked(const Editor *e) { return e->readOnly || e->curHidden; }

static void box_update(Editor *e) { px_font_box(e->font, &e->above, &e->below); }
static int box_h(const Editor *e) { return e->above + e->below; }

static Color pixcol(const PxFont *f, char c)
{
    int i = px_pixel_index(c);
    if (i < 0) return BLACK;
    return Th_FromRGBA(f->color[i]);
}

static Color fade(Color c, int a) { c.a = (unsigned char)((c.a * a) / 255); return c; }

static void touch(Editor *e)
{
    if (e->editOpen) return;
    e->editOpen = true;
    if (e->before) e->before(e->user);
}

static void mark(Editor *e) { e->changed = true; }

/* ------------------------------------------------------------------ bitmap editing */

static bool ensure(PxGlyph *g, int x0, int y0, int x1, int y1)
{
    int l, t, r, b;
    if (g->w <= 0 || g->h <= 0 || !g->pix) {
        l = x0; t = y0; r = x1 + 1; b = y1 + 1;
    } else {
        l = imin(g->x, x0); t = imin(gtop(g), y0);
        r = imax(g->x + g->w, x1 + 1); b = imax(gtop(g) + g->h, y1 + 1);
        if (l == g->x && t == gtop(g) && r == g->x + g->w && b == gtop(g) + g->h) return true;
    }
    int nw = r - l, nh = b - t;
    if (nw <= 0 || nh <= 0 || nw > MAX_BITMAP || nh > MAX_BITMAP) return false;
    char *n = (char *)malloc((size_t)nw * nh);
    if (!n) return false;
    memset(n, '.', (size_t)nw * nh);
    if (g->pix && g->w > 0)
        for (int y = 0; y < g->h; y++)
            memcpy(n + (size_t)(gtop(g) + y - t) * nw + (g->x - l), g->pix + (size_t)y * g->w, (size_t)g->w);
    free(g->pix);
    g->pix = n; g->w = nw; g->h = nh; g->x = l; g->y = -(t + nh);
    px_glyph_touch(g);
    return true;
}

/* Set one cell. Painting outside the bitmap grows it; erasing never does. */
static void put(Editor *e, int x, int y, char c)
{
    PxGlyph *g = G(e);
    if (!g || locked(e) || px_pixel_index(c) < 0) return;
    if (gget(g, x, y) == c) return;
    touch(e);
    if (c != '.' && !ensure(g, x, y, x, y)) return;
    g->pix[(size_t)(y - gtop(g)) * g->w + (x - g->x)] = c;
    g->asDrawn = false;
    px_glyph_touch(g);
    mark(e);
}

typedef void (*PlotFn)(Editor *e, int x, int y, char c);

static void plot_line(Editor *e, int x0, int y0, int x1, int y1, PlotFn fn, char c)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fn(e, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void plot_rect(Editor *e, int x0, int y0, int x1, int y1, bool filled, PlotFn fn, char c)
{
    int ax = imin(x0, x1), bx = imax(x0, x1), ay = imin(y0, y1), by = imax(y0, y1);
    for (int y = ay; y <= by; y++)
        for (int x = ax; x <= bx; x++)
            if (filled || x == ax || x == bx || y == ay || y == by) fn(e, x, y, c);
}

static void flood_fill(Editor *e, int px, int py, char c)
{
    PxGlyph *g = G(e);
    if (!g || locked(e)) return;
    int adv = g->advance > 0 ? g->advance : 0;
    int x0 = 0, y0 = -e->above, x1 = adv, y1 = e->below; /* exclusive ends */
    if (g->w > 0 && g->h > 0) {
        x0 = imin(x0, g->x); y0 = imin(y0, gtop(g));
        x1 = imax(x1, g->x + g->w); y1 = imax(y1, gtop(g) + g->h);
    }
    int bw = x1 - x0, bh = y1 - y0;
    if (bw <= 0 || bh <= 0 || bw > MAX_BITMAP || bh > MAX_BITMAP) return;
    if (px < x0 || px >= x1 || py < y0 || py >= y1) return;
    char target = gget(g, px, py);
    if (target == c) return;
    size_t n = (size_t)bw * bh;
    unsigned char *seen = (unsigned char *)calloc(n, 1);
    int *stack = (int *)malloc(n * sizeof(int));
    if (!seen || !stack) { free(seen); free(stack); return; }
    int sp = 0;
    stack[sp++] = (py - y0) * bw + (px - x0);
    seen[stack[0]] = 1;
    int minx = px, maxx = px, miny = py, maxy = py;
    while (sp > 0) {
        int idx = stack[--sp];
        int x = idx % bw + x0, y = idx / bw + y0;
        minx = imin(minx, x); maxx = imax(maxx, x); miny = imin(miny, y); maxy = imax(maxy, y);
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int k = 0; k < 4; k++) {
            int nx = x + d[k][0], ny = y + d[k][1];
            if (nx < x0 || nx >= x1 || ny < y0 || ny >= y1) continue;
            int ni = (ny - y0) * bw + (nx - x0);
            if (seen[ni] || gget(g, nx, ny) != target) continue;
            seen[ni] = 1;
            stack[sp++] = ni;
        }
    }
    touch(e);
    bool ok = true;
    if (c != '.') ok = ensure(g, minx, miny, maxx, maxy);
    if (ok) {
        for (int y = miny; y <= maxy; y++)
            for (int x = minx; x <= maxx; x++)
                if (seen[(y - y0) * bw + (x - x0)]) {
                    if (x >= g->x && x < g->x + g->w && y >= gtop(g) && y < gtop(g) + g->h)
                        g->pix[(size_t)(y - gtop(g)) * g->w + (x - g->x)] = c;
                }
        g->asDrawn = false;
        px_glyph_touch(g);
        mark(e);
    }
    free(seen);
    free(stack);
}

static void move_bitmap(Editor *e, int dx, int dy)
{
    PxGlyph *g = G(e);
    if (!g || locked(e) || g->w <= 0 || g->h <= 0) return;
    touch(e);
    g->x += dx; g->y -= dy;
    mark(e);
    e->editOpen = false;
}

static void set_advance(Editor *e, int v)
{
    PxGlyph *g = G(e);
    if (!g || locked(e)) return;
    if (v < 0) v = 0;
    if (v == g->advance) return;
    touch(e);
    g->advance = v;
    mark(e);
}

/* ------------------------------------------------------------------ view */

static void fit_view(Editor *e, Rectangle cv)
{
    PxGlyph *g = G(e);
    if (!g) return;
    int adv = g->advance > 0 ? g->advance : 0;
    int xa = imin(0, g->x) - MARGIN, xb = imax(adv, g->x + g->w) + MARGIN;
    int ya = imin(-e->above, gtop(g)) - MARGIN, yb = imax(e->below, gtop(g) + g->h) + MARGIN;
    if (g->w <= 0) { xa = -MARGIN; xb = adv + MARGIN; ya = -e->above - MARGIN; yb = e->below + MARGIN; }
    int ew = imax(1, xb - xa), eh = imax(1, yb - ya);
    int z = imin((int)cv.width / ew, (int)cv.height / eh);
    e->zoom = z < 1 ? 1 : (z > 48 ? 48 : z);
    e->vx = ((int)cv.width - ew * e->zoom) / 2 - xa * e->zoom;
    e->vy = ((int)cv.height - eh * e->zoom) / 2 - ya * e->zoom;
    e->needFit = false;
}

/* ------------------------------------------------------------------ drawing: glyph bitmaps */

/* (sx, sy) is the screen position of the pen on the baseline line */
static void draw_bitmap(const PxFont *f, const PxGlyph *g, int sx, int sy, int scale)
{
    if (!g->pix || g->w <= 0) return;
    for (int y = 0; y < g->h; y++) {
        int x = 0;
        while (x < g->w) {
            char c = g->pix[y * g->w + x];
            int run = 1;
            while (x + run < g->w && g->pix[y * g->w + x + run] == c) run++;
            if (c != '.') {
                DrawRectangle(sx + (g->x + x) * scale, sy + (gtop(g) + y) * scale, run * scale, scale, pixcol(f, c));
            }
            x += run;
        }
    }
}

/* A layer's glyph: every inked pixel in one colour, at the offset (dx right, dy up) */
static void draw_bitmap_color(const PxGlyph *g, int sx, int sy, int scale, Color col)
{
    if (!g->pix || g->w <= 0) return;
    for (int y = 0; y < g->h; y++) {
        int x = 0;
        while (x < g->w) {
            if (g->pix[y * g->w + x] == '.') { x++; continue; }
            int run = 1;
            while (x + run < g->w && g->pix[y * g->w + x + run] != '.') run++;
            DrawRectangle(sx + (g->x + x) * scale, sy + (gtop(g) + y) * scale, run * scale, scale, col);
            x += run;
        }
    }
}

/* ------------------------------------------------------------------ widgets */

static bool hit(Rectangle r) { return !Th_InputBlocked() && CheckCollisionPointRec(GetMousePosition(), r); }

static void sep(float x, float y, float h) { Th_VLine(x, y + 9, y + h - 9, TH_LINE); }

static void glyph_label(const PxGlyph *g, char *buf, size_t n)
{
    if (px_code_is_uncoded(g->codepoint)) { snprintf(buf, n, "#%d  (no code point)", g->codepoint - PX_CODE_UNCODED_FIRST); return; }
    if (!px_code_is_codepoint(g->codepoint)) { snprintf(buf, n, "(no code point)"); return; }
    snprintf(buf, n, "U+%04X", (unsigned)g->codepoint);   /* drawn as a glyph caption (Th_TextRich) */
}

/* Palette swatch. */
static bool swatch(Editor *e, Rectangle r, char c)
{
    PxFont *f = e->font;
    bool popupBlock = e->showAll && CheckCollisionPointRec(GetMousePosition(), e->popupRect) && r.y < e->popupRect.y;
    bool hv = hit(r) && !popupBlock && !locked(e);
    bool sel = e->ink == c;
    { char rn[8]; snprintf(rn, sizeof rn, "ink:%c", c); Th_Reg(rn, r); }
    Color col = pixcol(f, c);
    if (c == '.') {
        DrawRectangleRec(r, TH_PANEL);
        DrawLineEx((Vector2){r.x + 5, r.y + 5}, (Vector2){r.x + r.width - 5, r.y + r.height - 5}, 1, TH_TEXT_DIM);
        DrawLineEx((Vector2){r.x + r.width - 5, r.y + 5}, (Vector2){r.x + 5, r.y + r.height - 5}, 1, TH_TEXT_DIM);
    } else {
        if (col.a < 255) {
            DrawRectangleRec(r, TH_PANEL);
            DrawRectangle((int)r.x, (int)r.y, (int)r.width / 2, (int)r.height / 2, TH_TRACK);
        }
        DrawRectangleRec(r, col);
        int lum = (col.r * 30 + col.g * 59 + col.b * 11) / 100;
        char s[2] = {c, 0};
        Vector2 m = Th_MeasureSmall(s);
        Th_TextSmall(s, r.x + (int)((r.width - m.x) / 2), r.y + (int)((r.height - m.y) / 2),
                     lum > 140 ? TH_ON_LIGHT : TH_ON_DARK);
    }
    Th_RectLines(r, hv ? TH_M_BOX : TH_LINE);
    if (sel) {
        Rectangle o = {r.x - 2, r.y - 2, r.width + 4, r.height + 4};
        DrawRectangleLinesEx(o, 2, TH_ACCENT);
    }
    if (hv) {
        static char tip[24];
        snprintf(tip, sizeof tip, c == '.' ? "Eraser ink (.)" : "Ink '%c'", c);
        e->tip = tip; e->tipRect = r;
    }
    return hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

/* ------------------------------------------------------------------ toolbar */

static int palette_chars(const Editor *e, char *out)
{
    int n = 0;
    const PxFont *f = e->font;
    if (!f->hasPalette) {
        out[n++] = '0';
    } else {
        for (int i = 1; i < PX_NUM_PIXELS; i++)
            if (f->colorUsed[i]) out[n++] = PX_PIXELS[i];
        if (n == 0) out[n++] = '0';
    }
    out[n++] = '.';
    return n;
}

static void close_edit_cmd(Editor *e) { e->editOpen = false; }

/* Zoom by `delta` steps around the middle of the canvas. */
static void zoom_by(Editor *e, int delta)
{
    int nz = e->zoom + delta;
    if (nz < 1) nz = 1;
    if (nz > 64) nz = 64;
    if (nz == e->zoom) return;
    double fx = (e->cv.width / 2 - e->vx) / (double)e->zoom, fy = (e->cv.height / 2 - e->vy) / (double)e->zoom;
    e->zoom = nz;
    e->vx = (int)floor(e->cv.width / 2 - fx * nz + 0.5);
    e->vy = (int)floor(e->cv.height / 2 - fy * nz + 0.5);
    e->userZoom = true;
}

/* Draws the glyph view's bar (two rows) inside r; returns the height used. */
static float draw_toolbar(Editor *e, Rectangle area)
{
    PxGlyph *g = G(e);
    char chars[PX_NUM_PIXELS + 1];
    int nchars = palette_chars(e, chars);
    float h1 = TOOLBAR_H, total = h1 + PALETTE_H;

    DrawRectangle((int)area.x, (int)area.y, (int)area.width, (int)total, TH_PANEL_ALT);
    Th_HLine(area.x, area.x + area.width, area.y + h1 - 1, TH_LINE_SOFT);
    Th_HLine(area.x, area.x + area.width, area.y + total - 1, TH_LINE);

    /* ---- row 1: Back, previous / next, the glyph's name, tools, zoom, metrics, the read-only chip */
    float x = area.x + 10, y = area.y + 6, bh = TH_ROW_H;
    if (Th_Button((Rectangle){x, y, Th_ButtonW("Back"), bh}, "Back", 0, NULL)) e->result = EDITOR_CLOSE;
    x += Th_ButtonW("Back") + 10;
    sep(x, area.y, h1); x += 10;

    if (Th_Button((Rectangle){x, y, bh, bh}, "Previous glyph  (PgUp)", TH_BTN_ICON(TH_IC_PREV) | TH_BTN_OFF(e->gi > 0), "Previous glyph  (PgUp)")) e->result = EDITOR_PREV;
    x += bh + 2;
    if (Th_Button((Rectangle){x, y, bh, bh}, "Next glyph  (PgDn)", TH_BTN_ICON(TH_IC_NEXT) | TH_BTN_OFF(e->gi + 1 < e->font->count), "Next glyph  (PgDn)")) e->result = EDITOR_NEXT;
    x += bh + 8;

    char name[48] = "";
    if (g) glyph_label(g, name, sizeof name);
    Vector2 nm = { Th_MeasureRich(name, TH_F_BODY), Th_Measure("A").y };
    Th_TextRich(name, x, area.y + (int)((h1 - 1 - nm.y) / 2), TH_TEXT, TH_F_BODY, -1);
    x += (nm.x > 72 ? nm.x : 72) + 8;
    sep(x, area.y, h1); x += 10;

    for (int t = 0; t < T_COUNT; t++) {
        char tip[48];
        snprintf(tip, sizeof tip, "%s  (%s)", kToolName[t], kToolKey[t]);
        static char tips[T_COUNT][48];
        memcpy(tips[t], tip, sizeof tip);
        if (Th_Button((Rectangle){x, y, bh, bh}, tips[t], TH_BTN_ICON(TH_IC_PENCIL + t) | (e->tool == (Tool)t ? TH_BTN_ACTIVE : 0) | TH_BTN_OFF(!locked(e)), tips[t])) e->tool = (Tool)t;
        x += bh + 2;
    }
    x += 4;
    sep(x, area.y, h1); x += 10;

    /* zoom: Fit, minus, value, plus */
    if (Th_Button((Rectangle){x, y, Th_ButtonW("Fit"), bh}, "Fit", 0, NULL)) { e->needFit = true; e->userZoom = false; }
    Th_Reg("ezoom.fit", (Rectangle){x, y, Th_ButtonW("Fit"), bh});
    x += Th_ButtonW("Fit") + 6;
    {
        char zt[12];
        snprintf(zt, sizeof zt, "%dx", e->zoom);
        int zs = Th_Stepper((Rectangle){x, y, TH_STEP_W, bh}, zt, "ezoom", (e->zoom <= 1 ? TH_STEP_NO_MINUS : 0) | (e->zoom >= 64 ? TH_STEP_NO_PLUS : 0));
        if (Th_StepperHover()) { e->tip = Th_StepperHover() < 0 ? "Zoom out" : "Zoom in"; e->tipRect = (Rectangle){x, y, TH_STEP_W, bh}; }
        if (zs) zoom_by(e, zs);
        x += TH_STEP_W + 6;
    }
    sep(x, area.y, h1); x += 10;
    Th_Toggle((Rectangle){x, y, Th_ButtonW("Metrics"), bh}, "Metrics", &e->showMetrics, 0, NULL);
    x += Th_ButtonW("Metrics") + 6;
    {
        int extra = e->layers ? Layers_Count(e->layers) - 1 : 0;
        char ll[24];
        if (extra > 0) snprintf(ll, sizeof ll, "Layers %d", extra); else snprintf(ll, sizeof ll, "Layers");
        Rectangle lr = {x, y, Th_ButtonW(ll), bh};
        Th_Reg("Layers", lr);
        if (Th_Button(lr, ll, e->layersActive ? TH_BTN_ACTIVE : 0, "Reference layers: trace or compare other glyphs")) e->layersReq = true;
        x += lr.width;
    }

    /* ---- row 2: the ink palette on the left, the move and advance controls on the right */
    float y2 = area.y + h1 + 3;
    float px = area.x + 10, py = area.y + h1 + 6;
    Th_TextCap("INK", px, py + 4, TH_TEXT_DIM);
    px += Th_MeasureCap("INK").x + TH_GAP;
    for (int i = 0; i < nchars; i++) {
        if (chars[i] == '.' && i > 0) px += 8;
        if (swatch(e, (Rectangle){px, py, 22, 22}, chars[i])) e->ink = chars[i];
        px += 26;
    }
    if (e->font->hasPalette) {
        Th_Toggle((Rectangle){px + 4, py, 44, 22}, "All", &e->showAll, 0, NULL);
        px += 52;
    }
    (void)close_edit_cmd;

    float rx = area.x + area.width - 10;
    if (g) {
        char adv[40];
        snprintf(adv, sizeof adv, "%d", g->advance);
        Rectangle sr = {rx - TH_STEP_W, y2, TH_STEP_W, bh};
        int st = Th_Stepper(sr, adv, "", locked(e) ? TH_STEP_NO_MINUS | TH_STEP_NO_PLUS : 0);
        if (st) set_advance(e, g->advance + st), close_edit_cmd(e);
        rx = sr.x;
        Vector2 lm = Th_MeasureCap("ADVANCE");
        rx -= lm.x + 8; Th_TextCap("ADVANCE", rx, y2 + (int)((bh - lm.y) / 2), TH_TEXT_DIM);
        rx -= 8; sep(rx, area.y + h1, PALETTE_H); rx -= 10;

        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap down  (Shift+Down)", TH_BTN_ICON(TH_IC_DOWN) | TH_BTN_OFF(g->w > 0 && !locked(e)), "Move bitmap down  (Shift+Down)")) move_bitmap(e, 0, 1);
        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap up  (Shift+Up)", TH_BTN_ICON(TH_IC_UP) | TH_BTN_OFF(g->w > 0 && !locked(e)), "Move bitmap up  (Shift+Up)")) move_bitmap(e, 0, -1);
        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap right  (Shift+Right)", TH_BTN_ICON(TH_IC_RIGHT) | TH_BTN_OFF(g->w > 0 && !locked(e)), "Move bitmap right  (Shift+Right)")) move_bitmap(e, 1, 0);
        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap left  (Shift+Left)", TH_BTN_ICON(TH_IC_LEFT) | TH_BTN_OFF(g->w > 0 && !locked(e)), "Move bitmap left  (Shift+Left)")) move_bitmap(e, -1, 0);
        lm = Th_MeasureCap("MOVE");
        rx -= lm.x + 8; Th_TextCap("MOVE", rx, y2 + (int)((bh - lm.y) / 2), TH_TEXT_DIM);
    }
    if (e->readOnly) {   /* between the ink palette and the move controls: the notice (dropped when it does not fit) and the primary button */
        float lw = Th_Measure("Read-only").x + 8, cw = Th_DuplicateW(false), x0 = px + 8;
        bool lab = x0 + lw + cw <= rx - 8;
        Rectangle cr = {x0 + (lab ? lw : 0), y2, cw, bh};
        if (lab) Th_Text("Read-only", x0, y2 + (int)((bh - Th_Measure("M").y) / 2), TH_TEXT_WARN);
        Th_Reg("ro_chip", cr);
        if (Th_Duplicate(cr, false, 0, "An original is read-only: make an editable copy of this font")) e->result = EDITOR_DUPLICATE;
    }
    return total;
}

/* The popup with all 37 pixel characters (drawn last). */
static void draw_popup(Editor *e, Rectangle anchor)
{
    int cols = 13, rows = 3, cs = 26;
    Rectangle p = {anchor.x + 10 + 30, anchor.y + anchor.height + 2, cols * cs + 16, rows * cs + 16};
    if (p.x + p.width > GetScreenWidth()) p.x = GetScreenWidth() - p.width - 4;
    e->popupRect = p;
    Th_Raised(p, 1);
    for (int i = 0; i < PX_NUM_PIXELS; i++) {
        Rectangle r = {p.x + 8 + (i % cols) * cs, p.y + 8 + (i / cols) * cs, cs - 4, cs - 4};
        bool hv = hit(r);
        char c = PX_PIXELS[i];
        Color col = pixcol(e->font, c);
        if (c == '.') col = TH_PANEL;
        DrawRectangleRec(r, col);
        int lum = (col.r * 30 + col.g * 59 + col.b * 11) / 100;
        char s[2] = {c, 0};
        Vector2 m = Th_MeasureSmall(s);
        Th_TextSmall(s, r.x + (int)((r.width - m.x) / 2), r.y + (int)((r.height - m.y) / 2),
                     lum > 140 ? TH_ON_LIGHT : TH_ON_DARK);
        Th_RectLines(r, hv ? TH_M_BOX : TH_LINE);
        if (e->ink == c) DrawRectangleLinesEx((Rectangle){r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 2, TH_ACCENT);
        if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { e->ink = c; e->showAll = false; }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !hit(p) && !hit((Rectangle){anchor.x, anchor.y, anchor.width, anchor.height - 0}))
        e->showAll = false;
}

/* ------------------------------------------------------------------ preview strip */

/* The shared sample text (src/sample.h), all its lines that fit, at 1x and at 2x, in the font being edited (so a stroke shows at
 * once).  The last line shown is followed by the edited glyph repeated (when it has a code point). */
static SampleCache pvCache1, pvCache2;

#define PV_REPEAT 16
#define PV_PAD 10
#define PV_GAP 12

/* the first `lines` lines of the sample, then (on the last of them) a space and the edited glyph PV_REPEAT times */
static void pv_text(const Editor *e, int lines, char *out, size_t cap)
{
    const char *s = Sample_Get();
    size_t o = 0;
    int ln = 1;
    for (; *s && o + 1 < cap; s++) {
        if (*s == '\n') { if (ln >= lines) break; ln++; }
        out[o++] = *s;
    }
    const PxGlyph *g = G((Editor *)e);
    if (g && g->codepoint > 0x20 && g->codepoint <= 0x10FFFF && o + PV_REPEAT * 4 + 2 < cap) {
        int n = 0;
        const char *u = CodepointToUTF8(g->codepoint, &n);
        out[o++] = ' ';
        for (int i = 0; i < PV_REPEAT && n > 0; i++) { memcpy(out + o, u, (size_t)n); o += (size_t)n; }
    }
    out[o] = 0;
}

/* height in font pixels of `lines` lines of the sample */
static int pv_lines_h(const Editor *e, int lines)
{
    int w, h, n;
    Sample_Measure(e->font, "x", e->above, e->below, -1, 1, &w, &h, &n);
    int one = h;
    if (lines <= 1) return one;
    char two[8] = "x\nx";
    Sample_Measure(e->font, two, e->above, e->below, -1, 2, &w, &h, &n);
    return one + (lines - 1) * (h - one);
}

static int pv_total_lines(void)
{
    int n = Sample_LineCount(Sample_Get());
    return n < 1 ? 1 : (n > SAMPLE_MAX_LINES ? SAMPLE_MAX_LINES : n);
}

/* the height of the strip for the whole sample (before the cap) */
static float pv_wanted(const Editor *e)
{
    int h = pv_lines_h(e, pv_total_lines());
    return (float)(PV_PAD + h + PV_GAP + 2 * h + PV_PAD + 2);
}

/* how many lines fit in a strip of height r (at least one) */
static int pv_fit(const Editor *e, float height)
{
    int n = pv_total_lines();
    for (int k = n; k > 1; k--)
        if ((float)(PV_PAD + pv_lines_h(e, k) * 3 + PV_GAP + PV_PAD) <= height) return k;
    return 1;
}

static void draw_preview(Editor *e, Rectangle r)
{
    DrawRectangleRec(r, TH_PANEL);
    Th_HLine(r.x, r.x + r.width, r.y, TH_LINE);
    BeginScissorMode((int)r.x, (int)r.y + 1, (int)r.width, (int)r.height - 1);
    int k = pv_fit(e, r.height);
    int H = pv_lines_h(e, k);
    float lx = r.x + 10, tx = r.x + 44;
    float y1 = r.y + PV_PAD, y2 = y1 + H + PV_GAP;
    Th_TextSmall("1x", lx, y1 + (box_h(e) > 11 ? (box_h(e) - 11) / 2 : 0), TH_TEXT_DIM);
    Th_TextSmall("2x", lx, y2 + (box_h(e) > 11 ? (box_h(e) * 2 - 11) / 2 : 0), TH_TEXT_DIM);
    Th_Reg("Glyph_Preview", (Rectangle){tx, y1, 0, 0});     /* the top left of the 1x text (scripts) */
    SampleStyle st;
    memset(&st, 0, sizeof st);
    st.lineGap = -1;
    st.bg = TH_PANEL;
    st.boxAbove = e->above;
    st.boxBelow = e->below;
    float maxW = r.x + r.width - 8 - tx;
    char text[SAMPLE_MAX + PV_REPEAT * 4 + 16];
    pv_text(e, k, text, sizeof text);
    Sample_Draw(&pvCache1, e->font, text, tx, y1, 1, maxW, k, &st, NULL);
    Sample_Draw(&pvCache2, e->font, text, tx, y2, 2, maxW, k, &st, NULL);
    EndScissorMode();
}

/* ------------------------------------------------------------------ canvas */

static void plot_preview(Editor *e, int x, int y, char c)
{
    int ox = (int)e->panLast.x, oy = (int)e->panLast.y; /* reused as screen origin during preview */
    Color col = c == '.' ? fade(TH_DANGER, 110) : fade(pixcol(e->font, c), 150);
    DrawRectangle(ox + x * e->zoom, oy + y * e->zoom, e->zoom, e->zoom, col);
}

static void draw_metric(Rectangle cv, int oy, int zoom, int row, Color c, const char *label, float *lastLabelY, bool dotted)
{
    int sy = oy + row * zoom;
    if (sy < cv.y - 1 || sy > cv.y + cv.height) return;
    if (dotted) Th_DashedHLine(cv.x, cv.x + cv.width, (float)sy, 1, c);
    else Th_HLine(cv.x, cv.x + cv.width, (float)sy, c);
    if (label && (fabsf(*lastLabelY - sy) >= 13)) {
        Th_TextSmall(label, cv.x + 8, (float)sy - 14, c);
        *lastLabelY = (float)sy;
    }
}

static void draw_canvas(Editor *e, Rectangle cv, Vector2 mouse)
{
    PxFont *f = e->font;
    PxGlyph *g = G(e);
    int z = e->zoom;
    int ox = (int)cv.x + e->vx, oy = (int)cv.y + e->vy;   /* the pen on the baseline */
    int H = box_h(e);
    int advW = g->advance > 0 ? g->advance : 0;
    int advX = ox + g->advance * z;
    int boxTop = oy - e->above * z;
    bool canPaint = !locked(e) && (e->mode == M_NONE || e->mode == M_PAINT || e->mode == M_SHAPE);

    BeginScissorMode((int)cv.x, (int)cv.y, (int)cv.width, (int)cv.height);
    DrawRectangleRec(cv, TH_BG);

    /* font box area */
    {
        int bx0 = ox, by0 = boxTop, bw = advW * z, bh = H * z;
        if (e->checker) {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < advW; x++)
                    DrawRectangle(ox + x * z, boxTop + y * z, z, z, ((x + y) & 1) ? TH_CHECKER : TH_PANEL);
        } else {
            DrawRectangle(bx0, by0, bw, bh, TH_PANEL);
        }
    }

    /* pixel grid */
    if (z >= 4) {
        int x0 = ox + floordiv((int)cv.x - ox, z) * z;
        for (int x = x0; x < cv.x + cv.width; x += z) Th_VLine((float)x, cv.y, cv.y + cv.height, TH_LINE_SOFT);
        int y0 = oy + floordiv((int)cv.y - oy, z) * z;
        for (int y = y0; y < cv.y + cv.height; y += z) Th_HLine(cv.x, cv.x + cv.width, (float)y, TH_LINE_SOFT);
    }

    /* the layers and the edited glyph, from the bottom of the stack to the top */
    for (int i = Layers_Count(e->layers) - 1; i >= 0; i--) {
        const Layer *a = Layers_Get(e->layers, i);
        if (!a->visible) continue;
        if (a->isCurrent) { draw_bitmap(f, g, ox, oy, z); continue; }
        const PxGlyph *lg = Layers_Glyph(e->layers, i, NULL);
        if (lg) draw_bitmap_color(lg, ox + a->dx * z, oy - a->dy * z, z, fade(a->color, a->opacity * 255 / 100));
    }
    if (!e->curHidden && g->w > 0 && g->h > 0 && z >= 6) {
        /* faint outline of the bitmap extent */
        Rectangle br = {(float)(ox + g->x * z), (float)(oy + gtop(g) * z), (float)(g->w * z + 1), (float)(g->h * z + 1)};
        DrawRectangleLinesEx(br, 1, fade(TH_ACCENT, 60));
    }

    /* shape preview */
    if (e->mode == M_SHAPE && e->hasHover) {
        char c = e->btn == MOUSE_BUTTON_RIGHT ? '.' : e->ink;
        Vector2 keep = e->panLast;
        e->panLast = (Vector2){(float)ox, (float)oy};
        if (e->tool == T_LINE) plot_line(e, e->sx, e->sy, e->hx, e->hy, plot_preview, c);
        else plot_rect(e, e->sx, e->sy, e->hx, e->hy, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), plot_preview, c);
        e->panLast = keep;
    }

    /* metric lines across the canvas */
    float ll = -100;
    if (e->showMetrics) {
    draw_metric(cv, oy, z, 0, TH_M_BASELINE, "Baseline", &ll, false);
    draw_metric(cv, oy, z, -e->above, TH_M_BOX, f->has[PXM_ASCENT] ? "Ascent" : "Box top", &ll, false);
    draw_metric(cv, oy, z, e->below, TH_M_BOX, f->has[PXM_DESCENT] ? "Descent" : "Box bottom", &ll, false);
    if (f->has[PXM_CAPHEIGHT]) draw_metric(cv, oy, z, -f->m[PXM_CAPHEIGHT], TH_M_CAP, "Cap", &ll, false);
    if (f->has[PXM_XHEIGHT]) draw_metric(cv, oy, z, -f->m[PXM_XHEIGHT], TH_M_XHEIGHT, "x-height", &ll, false);
    if (f->has[PXM_UNDERLINEPOSITION]) {
        int row = -(f->m[PXM_UNDERLINEPOSITION] + 1), t = f->has[PXM_UNDERLINETHICKNESS] ? f->m[PXM_UNDERLINETHICKNESS] : 1;
        if (t > 0) DrawRectangle((int)cv.x, oy + row * z, (int)cv.width, t * z, fade(TH_M_UNDERLINE, 28));
        draw_metric(cv, oy, z, row, TH_M_UNDERLINE, "Underline", &ll, true);
    }
    if (f->has[PXM_STRIKEOUTPOSITION]) {
        int row = -(f->m[PXM_STRIKEOUTPOSITION] + 1), t = f->has[PXM_STRIKEOUTTHICKNESS] ? f->m[PXM_STRIKEOUTTHICKNESS] : 1;
        if (t > 0) DrawRectangle((int)cv.x, oy + row * z, (int)cv.width, t * z, fade(TH_M_UNDERLINE, 28));
        draw_metric(cv, oy, z, row, TH_M_UNDERLINE, "Strike-out", &ll, true);
    }
    }

    /* box side lines, pen origin and advance */
    Th_VLine((float)ox, cv.y, cv.y + cv.height, TH_M_BOX);
    bool nearAdv = !locked(e) && (e->mode == M_ADVANCE || (e->mode == M_NONE && mouse.x >= cv.x && mouse.x < cv.x + cv.width &&
                                            mouse.y >= cv.y && mouse.y < cv.y + cv.height && fabsf(mouse.x - advX) <= 4 &&
                                            !(e->showAll && CheckCollisionPointRec(mouse, e->popupRect))));
    Th_DashedVLine((float)advX, cv.y, cv.y + cv.height, 4, TH_M_ADVANCE);
    if (nearAdv) {
        DrawRectangle(advX - 1, (int)cv.y, 3, (int)cv.height, fade(TH_M_ADVANCE, 70));
    }
    Th_TextSmall("Pen", (float)ox + 4, cv.y + cv.height - 16, TH_M_BOX);
    Th_TextSmall("Advance", (float)advX + 4, cv.y + cv.height - 16, TH_M_ADVANCE);

    /* hover cell */
    if (e->hasHover && canPaint && !nearAdv) {
        Rectangle hr = {(float)(ox + e->hx * z), (float)(oy + e->hy * z), (float)z + 1, (float)z + 1};
        if (e->tool != T_FILL && e->mode == M_NONE) {
            char c = e->tool == T_ERASER ? '.' : e->ink;
            if (c != '.') DrawRectangle((int)hr.x, (int)hr.y, z, z, fade(pixcol(f, c), 70));
        }
        DrawRectangleLinesEx(hr, 1, TH_ACCENT);
    }
    if (e->curHidden) {   /* a quiet notice: the edited glyph's layer is off */
        const char *msg = "Current glyph hidden \xE2\x80\x94 show it to edit";
        Vector2 m = Th_Measure(msg);
        Rectangle nr = {cv.x + (int)((cv.width - m.x - 28) / 2), cv.y + 12, m.x + 28, 26};
        DrawRectangleRounded(nr, 0.5f, 8, Fade(TH_PANEL, 0.902f));
        DrawRectangleRoundedLinesEx(nr, 0.5f, 8, 1, TH_LINE);
        Th_Text(msg, nr.x + 14, nr.y + (int)((nr.height - m.y) / 2), TH_TEXT_DIM);
    }
    EndScissorMode();
    Th_RectLines((Rectangle){cv.x, cv.y, cv.width, cv.height}, TH_LINE);
}

/* ------------------------------------------------------------------ input on the canvas */

static void canvas_input(Editor *e, Rectangle cv, Vector2 mouse)
{
    PxGlyph *g = G(e);
    int z = e->zoom;
    int ox = (int)cv.x + e->vx, oy = (int)cv.y + e->vy;
    bool popupBlock = e->showAll && CheckCollisionPointRec(mouse, e->popupRect);
    bool inside = CheckCollisionPointRec(mouse, cv) && !popupBlock;
    int cx = floordiv((int)mouse.x - ox, z), cy = floordiv((int)mouse.y - oy, z);
    e->hasHover = inside || e->mode != M_NONE;
    if (e->hasHover) { e->hx = cx; e->hy = cy; }

    /* wheel zoom around the cursor */
    float wheel = GetMouseWheelMove();
    if (inside && wheel != 0 && e->mode == M_NONE) {
        double fx = (mouse.x - ox) / (double)z, fy = (mouse.y - oy) / (double)z;
        int nz = e->zoom + (wheel > 0 ? 1 : -1);
        if (e->zoom >= 8) nz = e->zoom + (wheel > 0 ? 2 : -2);
        if (nz < 1) nz = 1;
        if (nz > 64) nz = 64;
        e->zoom = nz;
        e->vx = (int)mouse.x - (int)cv.x - (int)floor(fx * nz + 0.5);
        e->vy = (int)mouse.y - (int)cv.y - (int)floor(fy * nz + 0.5);
        e->userZoom = true;
        z = nz;
        ox = (int)cv.x + e->vx; oy = (int)cv.y + e->vy;
        cx = floordiv((int)mouse.x - ox, z); cy = floordiv((int)mouse.y - oy, z);
        e->hx = cx; e->hy = cy;
    }

    int advX = ox + g->advance * z;

    if (e->mode == M_NONE) {
        bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        const Layer *sl = Layers_Get(e->layers, Layers_Selected(e->layers));
        if (inside && alt && sl && !sl->isCurrent) SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        if (inside && alt && sl && !sl->isCurrent && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            e->mode = M_LAYER; e->lx = cx; e->ly = cy;
            return;
        }
        if (inside && !locked(e)) {
            if (fabsf(mouse.x - advX) <= 4) SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
            else SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
        }
        if (inside && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            e->mode = M_PAN; e->panLast = mouse;
        } else if (locked(e)) {
            /* a viewer (or the glyph is hidden): nothing to paint */
        } else if (inside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && fabsf(mouse.x - advX) <= 4) {
            e->mode = M_ADVANCE; e->btn = MOUSE_BUTTON_LEFT;
        } else if (inside && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            int b = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ? MOUSE_BUTTON_LEFT : MOUSE_BUTTON_RIGHT;
            e->btn = b;
            char c = (b == MOUSE_BUTTON_RIGHT || e->tool == T_ERASER) ? '.' : e->ink;
            e->sx = e->lx = cx; e->sy = e->ly = cy;
            if (e->tool == T_FILL) {
                flood_fill(e, cx, cy, c);
                close_edit_cmd(e);
            } else if (e->tool == T_LINE || e->tool == T_RECT) {
                e->mode = M_SHAPE;
            } else {
                e->mode = M_PAINT;
                put(e, cx, cy, c);
            }
        }
        return;
    }

    if (e->mode == M_LAYER) {   /* Alt+drag: the selected layer follows the cursor cell by cell */
        SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        if (cx != e->lx || cy != e->ly) {
            Layers_Nudge(e->layers, Layers_Selected(e->layers), cx - e->lx, -(cy - e->ly));
            e->lx = cx; e->ly = cy;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) e->mode = M_NONE;
        return;
    }
    if (e->mode == M_PAN) {
        SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        e->vx += (int)(mouse.x - e->panLast.x); e->vy += (int)(mouse.y - e->panLast.y);
        e->panLast = mouse;
        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) e->mode = M_NONE;
        return;
    }
    if (e->mode == M_ADVANCE) {
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        set_advance(e, floordiv((int)floor(mouse.x - ox + z / 2.0), z));
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { e->mode = M_NONE; e->editOpen = false; }
        return;
    }
    SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
    char c = (e->btn == MOUSE_BUTTON_RIGHT || e->tool == T_ERASER) ? '.' : e->ink;
    if (e->mode == M_PAINT) {
        if (cx != e->lx || cy != e->ly) {
            plot_line(e, e->lx, e->ly, cx, cy, put, c);
            e->lx = cx; e->ly = cy;
        }
        if (!IsMouseButtonDown(e->btn)) { e->mode = M_NONE; e->editOpen = false; }
    } else if (e->mode == M_SHAPE) {
        if (IsKeyPressed(KEY_ESCAPE)) { e->mode = M_NONE; return; }
        if (!IsMouseButtonDown(e->btn)) {
            if (e->tool == T_LINE) plot_line(e, e->sx, e->sy, cx, cy, put, c);
            else plot_rect(e, e->sx, e->sy, cx, cy, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), put, c);
            e->mode = M_NONE; e->editOpen = false;
        }
    }
}

/* ------------------------------------------------------------------ public API */

Editor *Editor_Create(void)
{
    Editor *e = (Editor *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->tool = T_PENCIL;
    e->ink = '0';
    e->zoom = 8;
    e->needFit = true;
    e->showMetrics = true;
    e->layers = Layers_Create();
    return e;
}

void Editor_Destroy(Editor *e)
{
    if (!e) return;
    Layers_Destroy(e->layers);
    free(e);
}

struct Layers *Editor_Layers(Editor *e) { return e->layers; }
bool Editor_ShowMetrics(const Editor *e) { return e->showMetrics; }
void Editor_SetLayersTabActive(Editor *e, bool on) { e->layersActive = on; }
bool Editor_TakeLayersRequest(Editor *e) { bool r = e->layersReq; e->layersReq = false; return r; }
bool Editor_CurrentHidden(const Editor *e) { return e->curHidden; }
const char *Editor_OriginalHash(const Editor *e) { return e->hasOrig ? e->origHash : NULL; }

void Editor_Open(Editor *e, PxFont *font, int glyphIndex, EditorBeforeChange beforeChange, void *user)
{
    e->font = font;
    e->gi = glyphIndex;
    e->before = beforeChange;
    e->user = user;
    e->editOpen = false;
    e->mode = M_NONE;
    e->needFit = true;
    e->userZoom = false;
    e->showAll = false;
    e->result = EDITOR_RUNNING;
    if (px_pixel_index(e->ink) < 0) e->ink = '0';
    box_update(e);
}

void Editor_SetReadOnly(Editor *e, bool readOnly)
{
    e->readOnly = readOnly;
    if (readOnly) { e->mode = M_NONE; e->showAll = false; }
}

void Editor_SetOriginalHash(Editor *e, const char *hash)
{
    e->hasOrig = hash != NULL;
    snprintf(e->origHash, sizeof e->origHash, "%s", hash ? hash : "");
}

int Editor_GlyphIndex(const Editor *e) { return e->gi; }

bool Editor_CellCenter(const Editor *e, int x, int y, Vector2 *out)
{
    if (!e->font) return false;
    /* y is a row of the font box (0 = its top row) */
    out->x = e->cv.x + e->vx + x * e->zoom + e->zoom / 2.0f;
    out->y = e->cv.y + e->vy + (y - e->above) * e->zoom + e->zoom / 2.0f;
    return true;
}

bool Editor_AdvanceHandle(const Editor *e, Vector2 *out)
{
    const PxGlyph *g = (e->font && e->gi >= 0 && e->gi < e->font->count) ? &e->font->glyphs[e->gi] : NULL;
    if (!g) return false;
    out->x = e->cv.x + e->vx + (float)(g->advance * e->zoom);
    out->y = e->cv.y + e->cv.height / 2;
    return true;
}

int  Editor_Tool(const Editor *e) { return (int)e->tool; }
char Editor_Ink(const Editor *e) { return e->ink; }
int  Editor_Zoom(const Editor *e) { return e->zoom; }

bool Editor_TakeChanged(Editor *e)
{
    bool c = e->changed;
    e->changed = false;
    return c;
}

EditorResult Editor_UpdateDraw(Editor *e, Rectangle area)
{
    PxGlyph *g = G(e);
    if (!g) return EDITOR_CLOSE;
    box_update(e);
    Layers_SetContext(e->layers, e->font, g->codepoint);
    e->curHidden = !Layers_CurrentVisible(e->layers);
    e->result = EDITOR_RUNNING;
    e->tip = NULL;
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    Vector2 mouse = GetMousePosition();
    bool blocked = Th_InputBlocked();
    if (blocked) mouse = (Vector2){-10000, -10000};

    area.x = floorf(area.x); area.y = floorf(area.y);
    area.width = floorf(area.width); area.height = floorf(area.height);
    DrawRectangleRec(area, TH_BG);

    /* keyboard */
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (!ctrl && e->mode == M_NONE && !blocked) {
        if (!locked(e)) {
            if (IsKeyPressed(KEY_B)) e->tool = T_PENCIL;
            if (IsKeyPressed(KEY_E)) e->tool = T_ERASER;
            if (IsKeyPressed(KEY_L)) e->tool = T_LINE;
            if (IsKeyPressed(KEY_R)) e->tool = T_RECT;
            if (IsKeyPressed(KEY_F)) e->tool = T_FILL;
        }
        for (int k = 0; k < 10 && !locked(e); k++) {
            /* digits pick an ink only when the font's palette has it ('0' is always there) */
            int pi = 1 + k;
            if (IsKeyPressed(KEY_ZERO + k) && (k == 0 || (e->font->hasPalette && e->font->colorUsed[pi]))) e->ink = (char)('0' + k);
        }
        if (shift) {
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) move_bitmap(e, -1, 0);
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) move_bitmap(e, 1, 0);
            if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) move_bitmap(e, 0, -1);
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) move_bitmap(e, 0, 1);
        }
        if (IsKeyPressed(KEY_PAGE_UP) && e->gi > 0) e->result = EDITOR_PREV;
        if (IsKeyPressed(KEY_PAGE_DOWN) && e->gi + 1 < e->font->count) e->result = EDITOR_NEXT;
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (e->showAll) e->showAll = false;
            else e->result = EDITOR_CLOSE;
        }
    }

    /* layout */
    float tbH = draw_toolbar(e, area);
    float pvH = pv_wanted(e);
    if (pvH > area.height * 0.34f) pvH = floorf(area.height * 0.34f);
    if (pvH < 36) pvH = 36;
    Rectangle pv = {area.x, area.y + area.height - pvH, area.width, pvH};
    Rectangle st = {area.x, pv.y - STATUS_H, area.width, STATUS_H};
    Rectangle cv = {area.x + 12, area.y + tbH + 12, area.width - 24, st.y - (area.y + tbH) - 12 - 4};
    if (cv.height < 20 || cv.width < 20) return e->result;

    /* the screen mapping is frozen from mouse-down to mouse-up: a stroke that grows the font box changes the layout
     * (the preview height follows the box), and re-fitting mid-stroke would shift the view under the cursor */
    if (e->mode == M_NONE) {
        if (e->needFit || (!e->userZoom && (fabsf(cv.width - e->lastW) > 0.5f || fabsf(cv.height - e->lastH) > 0.5f))) {
            fit_view(e, cv);
        }
        e->lastW = cv.width; e->lastH = cv.height;
    }

    e->cv = cv;
    canvas_input(e, cv, mouse);
    draw_canvas(e, cv, mouse);

    /* status line: the same strip as the application's (Th_StatusBar), with a Checker toggle in its reserved end */
    {
        char pos[32], tool[160], cur[33];
        snprintf(pos, sizeof pos, e->hasHover ? "x %d   y %d" : "x -   y -", e->hx, -1 - e->hy);
        snprintf(tool, sizeof tool, "%s   ink %c   zoom %dx   bitmap %s", e->readOnly ? "View" : kToolName[e->tool], e->ink, e->zoom,
                 g->w > 0 ? TextFormat("%dx%d at %d,%d", g->w, g->h, g->x, g->y) : "empty");
        px_glyph_hash(g, cur);
        bool differs = e->hasOrig && strcmp(cur, e->origHash) != 0;
        ThPart l[2] = {{pos, e->hasHover ? TH_TEXT : TH_TEXT_DIM}, {tool}};
        ThPart r[3] = {{NULL, {0}, 0, 0, "Glyph", e->origHash}, {"->"}, {NULL, {0}, 0, TH_HASH_ACCENT, NULL, cur}};
        if (differs) Th_StatusBar(st, 90, l, 2, r, 3);
        else Th_StatusBar(st, 90, l, 2, &(ThPart){NULL, {0}, 0, 0, "Glyph", cur}, 1);
        Th_Toggle((Rectangle){st.x + st.width - 12 - 74, st.y + 1, 74, STATUS_H - 2}, "Checker", &e->checker, 0, NULL);
    }

    draw_preview(e, pv);

    if (e->showAll) draw_popup(e, (Rectangle){area.x, area.y, area.width, TOOLBAR_H + PALETTE_H});

    /* tooltip */
    if (!e->tip) e->tip = Th_Tip(&e->tipRect);
    if (e->tip && e->mode == M_NONE) {
        Vector2 m = Th_MeasureSmall(e->tip);
        float tw = m.x + 14, th = 22;
        float tx = e->tipRect.x, ty = e->tipRect.y + e->tipRect.height + 6;
        if (tx + tw > GetScreenWidth() - 4) tx = GetScreenWidth() - 4 - tw;
        DrawRectangleRounded((Rectangle){tx, ty, tw, th}, 0.25f, 6, TH_TIP_BG);
        Th_TextSmall(e->tip, tx + 7, ty + 5, TH_TEXT_TIP);
    }
    return e->result;
}

/* ------------------------------------------------------------------ self test (kept for the glyph editor's internals) */

int Editor_SelfTest(Editor *e, PxFont *f)
{
    PxGlyph *g = G(e);
    int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "self test failed: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)
    PxGlyph backup;
    px_glyph_copy(&backup, g);
    int l0 = g->x, t0 = gtop(g), w0 = g->w, h0 = g->h;
    /* growth to the left and top, erase does not grow */
    put(e, l0 - 3, t0 - 2, '0');
    CHECK(g->x == l0 - 3 && gtop(g) == t0 - 2);
    CHECK(gget(g, l0 - 3, t0 - 2) == '0');
    int gw = g->w, gh = g->h;
    put(e, g->x - 10, gtop(g) - 10, '.');
    CHECK(g->w == gw && g->h == gh);
    /* stroke with Bresenham across a gap */
    plot_line(e, 0, 0, 7, 5, put, '0');
    CHECK(gget(g, 0, 0) == '0' && gget(g, 7, 5) == '0');
    /* growth right/bottom */
    put(e, g->x + g->w + 2, gtop(g) + g->h + 1, '0');
    CHECK(gget(g, g->x + g->w - 1, gtop(g) + g->h - 1) == '0');
    /* fill of an enclosed empty region and of the outside */
    int before = 0;
    for (int i = 0; i < g->w * g->h; i++) before += g->pix[i] != '.';
    flood_fill(e, 20, 3, '0');
    int after = 0;
    for (int i = 0; i < g->w * g->h; i++) after += g->pix[i] != '.';
    CHECK(after >= before);
    /* move */
    int ml = g->x, mt = gtop(g);
    move_bitmap(e, 2, -1);
    CHECK(g->x == ml + 2 && gtop(g) == mt - 1);
    /* advance */
    set_advance(e, 9);
    CHECK(g->advance == 9);
    CHECK(e->changed);
    (void)w0; (void)h0;
    /* restore the original glyph */
    free(g->pix);
    *g = backup;
    e->changed = false;
#undef CHECK
    return fails;
}

void Editor_DebugOpenPalette(Editor *e) { e->showAll = true; }
