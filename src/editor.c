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
#include "colormath.h"
#include "colorpick.h"
#include "layers.h"
#include "raster.h"
#include "sample.h"
#include "selection.h"
#include "theme.h"

#define BAR_H 44          /* the bar over the canvas: the height of the right panel's tabs (PANEL_TABS_H) */
#define STATUS_H TH_ROW_H
#define MARGIN 3          /* cells of free space around the box when fitting */
#define MAX_BITMAP 4096

/* The tool table: a new tool is a row here (name, shortcut, icon, bar group), a Tool value and, when it works with the mouse, a branch in canvas_input.
 * A tool's shortcut is Shift and its letter (the plain letters, digits and '.' pick the slots of the palette bar).
 * The selection tools (`select`) share the selection state of the editor (see "selection" below); the drawing tools paint with the ink. */
typedef enum { T_PENCIL = 0, T_ERASER, T_LINE, T_RECT, T_FILL, T_MARQUEE, T_LASSO, T_WAND, T_MOVE, T_CURVE, T_ELLIPSE, T_PICK, T_POLY, T_CONTOUR, T_COUNT } Tool;
typedef enum { M_NONE = 0, M_PAINT, M_SHAPE, M_PAN, M_ADVANCE, M_LAYER, M_SELBOX, M_SELLASSO, M_SELMOVE, M_CURVE, M_CONTOUR, M_KERN } DragMode;

typedef struct ToolInfo {
    const char *name;   /* the tooltip and the script name ("Pencil  (Shift+B)" registers @Pencil) */
    const char *key;    /* the shortcut as shown */
    int keycode;        /* raylib key, pressed with Shift */
    int icon;           /* TH_IC_* */
    int group;          /* the tool strip leaves a gap between groups: 0 draw, 1 shapes, 2 fill, 3 selection (the order of the buttons is kStrip) */
    bool select;        /* a selection tool */
    bool hidden;        /* no button in the strip (a tool reached by its key only) */
} ToolInfo;

static const ToolInfo kTool[T_COUNT] = {
    {"Pencil",     "Shift+B", KEY_B, TH_IC_PENCIL,  0, false},
    {"Eraser",     "Shift+E", KEY_E, TH_IC_ERASER,  0, false},
    {"Line",       "Shift+L", KEY_L, TH_IC_LINE,    1, false},
    {"Rectangle",  "Shift+R", KEY_R, TH_IC_RECT,    1, false},
    {"Fill",       "Shift+F", KEY_F, TH_IC_FILL,    2, false},
    {"Marquee",    "Shift+M", KEY_M, TH_IC_MARQUEE, 3, true},
    {"Lasso",      "Shift+Q", KEY_Q, TH_IC_LASSO,   3, true},
    {"Magic Wand", "Shift+W", KEY_W, TH_IC_WAND,    3, true},
    {"Move",       "Shift+V", KEY_V, TH_IC_MOVE,    3, true},
    {"Curve",      "Shift+U", KEY_U, TH_IC_CURVE,   1, false},
    {"Ellipse",    "Shift+O", KEY_O, TH_IC_ELLIPSE, 1, false},
    {"Eyedropper", "Shift+I", KEY_I, TH_IC_PICK,    2, false},
    {"Polygon",    "Shift+D", KEY_D, TH_IC_POLY,    1, false},
    {"Contour",    "Shift+C", KEY_C, TH_IC_CONTOUR, 1, false},
};

/* the order of the strip's buttons (the table keeps the Tool numbers the scripts use): a group's tools are together */
static const Tool kStrip[T_COUNT] = {T_PENCIL, T_ERASER, T_LINE, T_CURVE, T_RECT, T_ELLIPSE, T_POLY, T_CONTOUR, T_FILL, T_PICK, T_MARQUEE, T_LASSO, T_WAND, T_MOVE};

#define NUDGE_BIG 8     /* Shift+arrow moves a selection by this many cells */

struct Editor {
    PxFont *font;
    int gi;
    EditorBeforeChange before;
    void *user;
    bool changed;
    bool readOnly;      /* the font is locked: the glyph view is a viewer */
    bool hasOrig;       /* origHash is known (the pxgl hash of this glyph when the font was opened) */
    char origHash[33];
    int above, below;   /* the font box of the current frame */
    bool editOpen;      /* beforeChange already called for the current stroke / command */

    Tool tool;
    int toolCols;       /* columns of the tool strip (test hook) */
    bool inkErase;      /* the ink is '.', the eraser ink (the last swatch) */
    uint32_t inkColor;  /* else the ink: a colour (0xRRGGBBAA), which the glyph may not have yet (drawing adds it to its palette) */
    bool checker;
    bool showMetrics;   /* the metric lines are drawn */
    bool kernReq;       /* Shift+K was pressed (main.c shows or leaves the Kerning tab) */
    EditorResult navReq;   /* the View tab's previous or next glyph was clicked, or its field named a glyph: the next frame returns it */
    int gotoIndex;         /* the glyph the field named (EDITOR_GOTO) */
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

    /* line, curve and pencil (see DESIGN.md, "Tools of the glyph view") */
    bool filled;        /* the bar's Filled: the rectangle, ellipse, polygon and contour are drawn solid (kept in pxfe.ini by main.c) */
    bool pixPerfect;    /* the bar's Pixel-perfect: a pencil stroke loses the corner cells of its Ls (kept in pxfe.ini by main.c) */
    RasPt *stk;         /* the cells of the stroke being drawn with the pencil, and what each held before it was painted */
    char *stkPrev;
    int stkN, stkCap;
    bool hasLast;       /* lastX, lastY: the last cell the pencil or the line ended on (Shift+click draws a line from it) */
    int lastX, lastY;
    int curveStage;     /* 0 no curve; 1 the end points are set; 2 the first control point too (the next drag is the second) */
    int curveDrag;      /* while mode is M_CURVE: 0 the end points, 1 the first control point, 2 the second */
    RasPt cv4[4];       /* the curve: end point, control point, control point, end point */
    int curveBtn;       /* the mouse button that started the curve ('.' for the right one) */

    /* fill, polygon and contour (see DESIGN.md, "Tools of the glyph view") */
    bool contiguous;    /* the bar's Contiguous: the fill takes the cells joined to the one clicked (off: every cell of that character) */
    RasPt *poly;        /* the polygon being made: its vertices */
    int polyN, polyCap;
    int polyBtn;        /* the mouse button that started the polygon (the right one draws '.') */
    RasPath cont;       /* the contour being dragged: the cells the mouse went through */

    /* selection (glyph space cells).  While `floating`, the selected pixels are lifted into `fl` and the glyph itself is unchanged: the drop
     * is the edit (one undo step), a cancel leaves the glyph as it was. */
    SelMask sel;        /* the selection; while floating it is the float's shape, moved with it */
    SelMode selMode;    /* the bar's choice of how a new shape meets the selection */
    bool wandDiag;      /* the wand also joins diagonal neighbours */
    bool floating;
    SelFloat fl;        /* the lifted pixels ('.' = transparent), characters of flPal */
    uint32_t flPal[PX_GLYPH_MAX_COLORS];   /* the colours of the float's characters ('0' = flPal[0]): the drop maps them into the glyph's palette */
    int flPalN;
    SelMask src;        /* where they were lifted from (empty for a paste) */
    bool flCopy;        /* the original stays (Ctrl+drag) */
    bool flPaste;       /* from the clipboard: nothing to clear */
    bool implicitSel;   /* the Move tool had no selection and took the whole bitmap: the selection goes with the drop */
    SelMode dragSel;    /* how the shape being dragged is combined */
    bool shiftAtPress;  /* Shift was down at the press (it adds; later it makes the marquee square) */
    bool dragged;       /* the mouse moved more than a click */
    Vector2 pressPos;
    bool moveCopy;      /* the move drag was started with Ctrl */
    int moveBx, moveBy; /* where the float's top-left cell was when the move drag began */
    int *lasso;         /* the lasso's path: x, y pairs */
    int lassoN, lassoCap;

    bool hasHover;
    int hx, hy;

    EditorResult result;
    const char *tip;
    Rectangle tipRect;
    /* the palette bar under the canvas (see "palette bar" below) */
    Rectangle bar;       /* where it was drawn last frame */
    int barRows;         /* its rows: 1, or more with the font's other colours (test hook) */
    int barExtras;       /* tiles of the font's other colours */
    float barScroll;     /* the scroll of the extra rows */
    int pressKind;       /* the tile pressed: 0 none, 1 '.', 2 a slot, 3 '+', 4 a colour of the font */
    uint32_t pressCol;   /* its colour */
    int pressSlot;
    Vector2 pressAt;
    bool barDrag;        /* a colour of the font is dragged towards a slot or '+' */
    double lastClick;    /* a double click on a slot opens the picker */
    uint32_t lastClickCol;
    int pickTag;         /* the question the colour picker answers (PICK_*), 0 none */
    uint32_t pickCol;    /* the colour of the slot it edits */
    uint32_t *p0;        /* the glyph's palette when the view opened it (the unused-slot rule, see leave_glyph) */
    int p0n;             /* 0: the default palette */
    int p0gi; const PxFont *p0font; PxCode p0code;
    struct { uint32_t c; float from, to; double t0; bool flash; } anim[PX_GLYPH_MAX_COLORS];   /* the slots sliding to new places */
    int nanim;
    struct { uint32_t c; float x; } shown[PX_GLYPH_MAX_COLORS];   /* where each slot was drawn last frame */
    int nshown, shownGi; float shownStep, shownX0;
    Rectangle cv;        /* canvas rectangle of the last frame (for scripted input) */

    /* kerning (see DESIGN.md, "Kerning"): the partner glyph is drawn after the edited one, at its advance plus the pair's kerning */
    bool kernOn;         /* the Kerning tab of the right panel is showing (main.c sets it every frame) */
    PxCode kernCode;     /* the partner's code, PX_NO_CODE before one is chosen; kept across glyphs */
    int kernStart, kernCx;   /* a kerning drag: the value and the cell column at the press */
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

/* nothing may change the glyph: the font is locked, or its layer is hidden */
static bool locked(const Editor *e) { return e->readOnly || e->curHidden; }

static void box_update(Editor *e) { px_font_box(e->font, &e->above, &e->below); }
static int box_h(const Editor *e) { return e->above + e->below; }

/* The ink stands for a colour the glyph has not got yet: put() and the fill add it to the glyph's palette as they paint (copy on write). */
#define INK_NEW '*'

/* the colour of a pixel character of the edited glyph (INK_NEW: the ink) */
static Color pixcol(const Editor *e, char c)
{
    if (c == INK_NEW) return Th_FromRGBA(e->inkColor);
    const PxGlyph *g = (e->font && e->gi >= 0 && e->gi < e->font->count) ? &e->font->glyphs[e->gi] : NULL;
    int i = px_pixel_index(c);
    if (i < 0 || !g) return BLACK;
    return ((const Color *)px_glyph_palette(e->font, g)->draw)[i];
}

/* the character the tools paint with: '.' for the eraser ink, the glyph's character of the ink colour, or INK_NEW */
static char ink_ch(const Editor *e)
{
    if (e->inkErase) return '.';
    const PxGlyph *g = (e->font && e->gi >= 0 && e->gi < e->font->count) ? &e->font->glyphs[e->gi] : NULL;
    int s = g ? px_glyph_find_color(e->font, g, e->inkColor) : -1;
    if (s >= 0 && g->pi > 0) return PX_PIXELS[1 + s];
    if (s >= 0 && !px_font_is_colour(e->font)) return PX_PIXELS[1 + s];   /* black in a font of one colour: the default palette is enough */
    return INK_NEW;
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

/* With a selection, the drawing tools (pencil, eraser, line, curve, rectangle, ellipse, polygon, contour) and the fill paint only inside it. */
static bool clipped_out(const Editor *e, int x, int y) { return !sel_empty(&e->sel) && !sel_get(&e->sel, x, y); }

/* The ink colour into the glyph's palette (after touch(): the change is in the undo step): its character, or 0 when the glyph has 36 colours in use
 * already (a toast says so, once a second) or memory ran out. */
static char ink_add(Editor *e, PxGlyph *g)
{
    int s = px_glyph_add_color(e->font, g, e->inkColor);
    if (s >= 0) return PX_PIXELS[1 + s];
    static double last = -10;
    if (s == PX_COLOR_FULL && GetTime() - last > 1.0) { Th_Toast(2, "This glyph has 36 colours: drop the colour onto a slot of the palette bar to replace one"); last = GetTime(); }
    return 0;
}

/* Set one cell. Painting outside the bitmap grows it; erasing never does.  A cell outside the selection (when there is one) is left alone.
 * INK_NEW paints the ink colour, which joins the glyph's palette. */
static void put(Editor *e, int x, int y, char c)
{
    PxGlyph *g = G(e);
    if (!g || locked(e) || (c != INK_NEW && px_pixel_index(c) < 0)) return;
    if (clipped_out(e, x, y)) return;
    if (gget(g, x, y) == c) return;
    touch(e);
    if (c == INK_NEW && !(c = ink_add(e, g))) return;
    if (gget(g, x, y) == c) return;
    if (c != '.' && !ensure(g, x, y, x, y)) return;
    g->pix[(size_t)(y - gtop(g)) * g->w + (x - g->x)] = c;
    g->asDrawn = false;
    px_glyph_touch(g);
    mark(e);
}

typedef void (*PlotFn)(Editor *e, int x, int y, char c);

static void plot_line(Editor *e, int x0, int y0, int x1, int y1, PlotFn fn, char c)
{
    RasPath r = {0};
    if (ras_line(&r, x0, y0, x1, y1))
        for (int i = 0; i < r.n; i++) fn(e, r.p[i].x, r.p[i].y, c);
    ras_free(&r);
}

/* A cubic curve through the four points of e->cv4, cell by cell (always pixel-perfect: one cell wide). */
static void plot_curve(Editor *e, PlotFn fn, char c)
{
    RasPath r = {0};
    if (ras_bezier(&r, e->cv4[0], e->cv4[1], e->cv4[2], e->cv4[3], true))
        for (int i = 0; i < r.n; i++) fn(e, r.p[i].x, r.p[i].y, c);
    ras_free(&r);
}

/* The pencil.  A stroke is a list of cells; with Pixel-perfect on, a new cell that makes an L with the last two takes the middle cell back
 * (the cell goes back to what it held before the stroke painted it), so the stroke stays one cell wide.  One undo step per stroke. */
static void stroke_begin(Editor *e) { e->stkN = 0; }

static bool stroke_push(Editor *e, int x, int y, char prev)
{
    if (e->stkN >= e->stkCap) {
        int nc = e->stkCap ? e->stkCap * 2 : 128;
        RasPt *a = (RasPt *)realloc(e->stk, sizeof(RasPt) * (size_t)nc);
        if (a) e->stk = a;
        char *b = (char *)realloc(e->stkPrev, (size_t)nc);
        if (b) e->stkPrev = b;
        if (!a || !b) return false;
        e->stkCap = nc;
    }
    e->stk[e->stkN].x = x; e->stk[e->stkN].y = y; e->stkPrev[e->stkN] = prev;
    e->stkN++;
    return true;
}

/* the stroke goes through the cell (x, y) */
static void stroke_cell(Editor *e, int x, int y, char c)
{
    PxGlyph *g = G(e);
    if (!g) return;
    if (e->stkN > 0 && e->stk[e->stkN - 1].x == x && e->stk[e->stkN - 1].y == y) return;
    RasPt cur = {x, y};
    if (e->pixPerfect && e->stkN >= 2 && ras_is_corner(e->stk[e->stkN - 2], e->stk[e->stkN - 1], cur)) {
        RasPt b = e->stk[e->stkN - 1];
        char was = e->stkPrev[e->stkN - 1];
        if (b.x >= g->x && b.x < g->x + g->w && b.y >= gtop(g) && b.y < gtop(g) + g->h && gget(g, b.x, b.y) != was) {
            g->pix[(size_t)(b.y - gtop(g)) * g->w + (b.x - g->x)] = was;
            g->asDrawn = false;
            px_glyph_touch(g);
        }
        e->stkN--;
    }
    stroke_push(e, x, y, gget(g, x, y));
    put(e, x, y, c);
}

/* the stroke ends: the bitmap loses the empty margin that restored cells may have left */
static void stroke_end(Editor *e)
{
    PxGlyph *g = G(e);
    if (e->stkN > 0) { e->hasLast = true; e->lastX = e->stk[e->stkN - 1].x; e->lastY = e->stk[e->stkN - 1].y; }
    if (g && e->pixPerfect && e->editOpen && g->pix) { px_glyph_crop(g); px_glyph_touch(g); }
    e->stkN = 0;
    e->editOpen = false;
}

/* the pencil moves from its last cell to (x, y): every cell on the way */
static void stroke_to(Editor *e, int x0, int y0, int x1, int y1, char c)
{
    RasPath r = {0};
    if (ras_line(&r, x0, y0, x1, y1))
        for (int i = 0; i < r.n; i++) stroke_cell(e, r.p[i].x, r.p[i].y, c);
    ras_free(&r);
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

/* ------------------------------------------------------------------ kerning (see DESIGN.md, "Kerning")
 *
 * The bar's Kerning shows a second glyph, the partner, after the edited one: its pen is at the edited glyph's advance plus the kerning of the
 * pair (edited glyph, partner).  The value is changed by the bar's number box, by Ctrl+Left / Ctrl+Right, and by dragging the partner's ink. */

#define KERN_MAX 999

/* the partner's index in the font, -1 when kerning is off, no partner is chosen or the font has no glyph of its code (an undo, a delete) */
static int kern_partner(const Editor *e)
{
    if (!e->kernOn || e->kernCode == PX_NO_CODE || !e->font) return -1;
    return px_font_find_code(e->font, e->kernCode);
}

static int kern_value(const Editor *e)
{
    const PxGlyph *g = (e->font && e->gi >= 0 && e->gi < e->font->count) ? &e->font->glyphs[e->gi] : NULL;
    return g && e->kernCode != PX_NO_CODE ? px_font_kern(e->font, g->code, e->kernCode) : 0;
}

/* the pen of the partner, in cells from the edited glyph's pen */
static int kern_pen(const Editor *e) { const PxGlyph *g = &e->font->glyphs[e->gi]; return g->advance + kern_value(e); }

static void set_kern(Editor *e, int v)
{
    PxGlyph *g = G(e);
    if (!g || locked(e) || kern_partner(e) < 0) return;
    if (v < -KERN_MAX) v = -KERN_MAX;
    if (v > KERN_MAX) v = KERN_MAX;
    if (v == px_font_kern(e->font, g->code, e->kernCode)) return;
    touch(e);
    px_font_set_kern(e->font, g->code, e->kernCode, v);   /* 0 removes the pair */
    mark(e);
}

static void kern_set_partner(Editor *e, PxCode code)
{
    e->kernOn = true;
    if (code == e->kernCode) return;
    e->kernCode = code;
    if (!e->userZoom) e->needFit = true;
}

/* The next (dir 1) or previous (dir -1) partner the edited glyph has a kerning pair with, as the first glyph, in code order, round the end;
 * PX_NO_CODE when it has none. */
static PxCode kern_neighbour(const Editor *e, int dir)
{
    const PxGlyph *g = (e->font && e->gi >= 0 && e->gi < e->font->count) ? &e->font->glyphs[e->gi] : NULL;
    PxCode cur = e->kernCode, best = PX_NO_CODE, wrap = PX_NO_CODE;
    if (!g) return PX_NO_CODE;
    for (int i = 0; i < e->font->kernCount; i++) {
        const PxKern *k = &e->font->kern[i];
        if (k->first != g->code || k->value == 0 || px_font_find_code(e->font, k->second) < 0) continue;
        PxCode c = k->second;
        if (dir > 0) {
            if (c > cur && (best == PX_NO_CODE || c < best)) best = c;
            if (wrap == PX_NO_CODE || c < wrap) wrap = c;
        } else {
            if ((cur == PX_NO_CODE || c < cur) && (best == PX_NO_CODE || c > best)) best = c;
            if (wrap == PX_NO_CODE || c > wrap) wrap = c;
        }
    }
    return best != PX_NO_CODE ? best : wrap;
}

/* Kerning on (the partner: the one chosen before while the font has it, else the first pair of the glyph, else the glyph itself) or off. */
static void kern_set_on(Editor *e, bool on)
{
    if (on == e->kernOn) return;
    if (!on) { e->kernOn = false; if (e->mode == M_KERN) e->mode = M_NONE; if (!e->userZoom) e->needFit = true; return; }
    PxGlyph *g = G(e);
    if (!g) return;
    PxCode c = e->kernCode;
    if (c == PX_NO_CODE || px_font_find_code(e->font, c) < 0) {
        PxCode keep = e->kernCode;
        e->kernCode = PX_NO_CODE;
        c = kern_neighbour(e, 1);
        e->kernCode = keep;
        if (c == PX_NO_CODE) c = g->code;
    }
    e->kernCode = PX_NO_CODE;   /* kern_set_partner fits the view again */
    kern_set_partner(e, c);
}

/* the partner's label in the bar's field: the character itself while it is a plain one, else the code (U+0301, #3, Default, U+0041-001) */
static void kern_label(PxCode c, char *out, size_t n)
{
    out[0] = 0;
    if (c == PX_NO_CODE) return;
    int32_t cp = px_code_is_codepoint(c) && px_code_variant(c) == 0 ? px_code_codepoint(c) : -1;
    if (cp == 0x20) { snprintf(out, n, "space"); return; }
    if (cp >= 0x21 && cp != 0x7F && cp <= 0x17F && !(cp >= 0x80 && cp < 0xA1) && cp != 0xAD) {
        int len = 0;
        const char *u = CodepointToUTF8(cp, &len);
        snprintf(out, n, "%.*s", len, u);
        return;
    }
    px_code_label(c, out, (int)n);
}

/* What was typed into the bar's field: one character (or the word "space"), else a code as the Code point field takes it (U+00E9, 41, #3, default). */
static bool kern_parse(const char *in, PxCode *out)
{
    char s[64];
    snprintf(s, sizeof s, "%s", in ? in : "");
    if (!strcmp(s, " ")) { *out = px_code_cp(0x20); return true; }
    char *p = s;
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n > 0 && p[n - 1] == ' ') p[--n] = 0;
    if (n == 0) return false;
    if (!strcmp(p, "space")) { *out = px_code_cp(0x20); return true; }
    int sz = 0;
    int c = GetCodepointNext(p, &sz);
    if ((size_t)sz == n && !(c == 0x3F && p[0] != '?')) { *out = px_code_cp(c); return true; }
    return px_code_parse_user(p, out);
}

/* the cell (cx, cy) is ink of the partner and not of the edited glyph: a press there drags the kerning */
static bool kern_hit(Editor *e, int cx, int cy)
{
    int pi = kern_partner(e);
    PxGlyph *g = G(e);
    if (pi < 0 || !g || locked(e)) return false;
    const PxGlyph *p = &e->font->glyphs[pi];
    return gget(p, cx - kern_pen(e), cy) != '.' && gget(g, cx, cy) == '.';
}

/* ------------------------------------------------------------------ selection
 *
 * Behaviour modelled on common pixel editors (see DESIGN.md, "Selection and moving").  The mask (src/selection.c) is in glyph space.
 * A move is a float: the selected pixels are copied into `fl` and the glyph stays as it is; the float is drawn over the glyph (the cells it was
 * lifted from drawn empty unless it is a copy) and the drop writes it, one undo step per drop.  Cells of the float that hold '.' are transparent:
 * they do not erase what they land on. */

static SelFloat g_clip;     /* the in-app clipboard: the pixels and the cells they had, kept for the whole session (also across glyphs and fonts) */
static uint32_t g_clipPal[PX_GLYPH_MAX_COLORS];   /* the colours of its characters ('0' = g_clipPal[0]): a paste maps them into the glyph's palette */
static int g_clipPalN;

/* the colours of the glyph's characters '0'..'Z' (its palette's first 36 entries) */
static int glyph_pal36(const PxFont *f, const PxGlyph *g, uint32_t out[PX_GLYPH_MAX_COLORS])
{
    const PxPalette *p = px_glyph_palette(f, g);
    int n = p->n < PX_GLYPH_MAX_COLORS ? p->n : PX_GLYPH_MAX_COLORS;
    memcpy(out, p->c, sizeof(uint32_t) * (size_t)n);
    return n;
}

/* The colours the float's pixels use (in flPal order) and, per character, its index among them; their count. */
static int float_colors(const Editor *e, uint32_t cols[PX_GLYPH_MAX_COLORS], int idx[PX_NUM_PIXELS])
{
    bool usedc[PX_NUM_PIXELS] = {false};
    size_t n = e->fl.pix ? (size_t)e->fl.shape.w * (size_t)e->fl.shape.h : 0;
    for (size_t k = 0; k < n; k++) { int i = px_pixel_index(e->fl.pix[k]); if (i > 0) usedc[i] = true; }
    int m = 0;
    for (int i = 1; i < PX_NUM_PIXELS; i++) {
        idx[i] = -1;
        if (!usedc[i]) continue;
        idx[i] = m;
        cols[m++] = i - 1 < e->flPalN ? e->flPal[i - 1] : 0x000000FFu;
    }
    return m;
}

static SelBitmap bm_of(const PxGlyph *g)
{
    SelBitmap b = {g->pix, g->x, gtop(g), g->w, g->h};
    if (!g->pix || g->w <= 0 || g->h <= 0) { b.pix = NULL; b.w = b.h = 0; }
    return b;
}

static bool is_sel_tool(Tool t) { return kTool[t].select; }
static bool key_shift(void) { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }
static bool key_alt(void) { return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT); }
static bool key_ctrl(void) { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }

/* the shape drag's two ends: Shift constrains the angle, Ctrl makes the press cell the centre */
static void shape_ends(const Editor *e, int hx, int hy, int ends[4])
{
    ras_ends(e->sx, e->sy, hx, hy, key_shift(), key_ctrl(), ends);
}

/* the rectangle or ellipse dragged from the press cell to (hx, hy): Shift makes it a square or circle, Ctrl puts the press cell in the middle; filled by the bar's option */
static void plot_shape(Editor *e, int hx, int hy, PlotFn fn, char c)
{
    int b[4];
    RasPath r = {0};
    ras_box(e->sx, e->sy, hx, hy, key_shift(), key_ctrl(), b);
    if (e->tool == T_ELLIPSE ? ras_ellipse(&r, b[0], b[1], b[2], b[3], e->filled) : ras_rect(&r, b[0], b[1], b[2], b[3], e->filled))
        for (int i = 0; i < r.n; i++) fn(e, r.p[i].x, r.p[i].y, c);
    ras_free(&r);
}

/* the cells the glyph view works in: the font box across the advance, and the bitmap where it reaches out of it (ends exclusive) */
static bool glyph_region(Editor *e, int *x0, int *y0, int *x1, int *y1)
{
    PxGlyph *g = G(e);
    if (!g) return false;
    *x0 = 0; *y0 = -e->above; *x1 = g->advance > 0 ? g->advance : 0; *y1 = e->below;
    if (g->w > 0 && g->h > 0) {
        *x0 = imin(*x0, g->x); *y0 = imin(*y0, gtop(g));
        *x1 = imax(*x1, g->x + g->w); *y1 = imax(*y1, gtop(g) + g->h);
    }
    return *x1 > *x0 && *y1 > *y0 && *x1 - *x0 <= MAX_BITMAP && *y1 - *y0 <= MAX_BITMAP;
}

/* the characters of the glyph region (see glyph_region), '.' where the bitmap is not; the caller frees it */
static char *region_pix(Editor *e, int *x0, int *y0, int *bw, int *bh)
{
    PxGlyph *g = G(e);
    int x1, y1;
    if (!g || !glyph_region(e, x0, y0, &x1, &y1)) return NULL;
    *bw = x1 - *x0; *bh = y1 - *y0;
    char *mem = (char *)malloc((size_t)*bw * *bh);
    if (!mem) return NULL;
    memset(mem, '.', (size_t)*bw * *bh);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++)
            mem[(size_t)(gtop(g) + y - *y0) * *bw + (g->x + x - *x0)] = g->pix[(size_t)y * g->w + x];
    return mem;
}

/* The paint bucket: the cells ras_flood finds in the glyph region (the font box and the bitmap, as the wand sees it) are painted with c, so empty space can be
 * filled and the bitmap grows.  The selection is a wall: only its cells are filled, and a click outside it fills nothing.  One undo step. */
static void fill_at(Editor *e, int cx, int cy, char c)
{
    PxGlyph *g = G(e);
    int x0, y0, bw, bh;
    if (!g || locked(e) || (c != INK_NEW && px_pixel_index(c) < 0)) return;
    char *mem = region_pix(e, &x0, &y0, &bw, &bh);
    if (!mem) return;
    unsigned char *clip = NULL;
    RasPath r = {0};
    bool ok = cx >= x0 && cx < x0 + bw && cy >= y0 && cy < y0 + bh && mem[(size_t)(cy - y0) * bw + (cx - x0)] != c;
    if (ok && !sel_empty(&e->sel)) {
        clip = (unsigned char *)malloc((size_t)bw * bh);
        ok = clip != NULL;
        for (int y = 0; ok && y < bh; y++)
            for (int x = 0; x < bw; x++) clip[(size_t)y * bw + x] = sel_get(&e->sel, x0 + x, y0 + y);
    }
    if (ok) ok = ras_flood(&r, mem, x0, y0, bw, bh, cx, cy, e->contiguous, e->wandDiag, clip) && r.n > 0;
    if (ok) {
        int minx = r.p[0].x, maxx = minx, miny = r.p[0].y, maxy = miny;
        for (int i = 1; i < r.n; i++) { minx = imin(minx, r.p[i].x); maxx = imax(maxx, r.p[i].x); miny = imin(miny, r.p[i].y); maxy = imax(maxy, r.p[i].y); }
        touch(e);
        if (c == INK_NEW) c = ink_add(e, g);
        if (c && (c == '.' || ensure(g, minx, miny, maxx, maxy))) {
            for (int i = 0; i < r.n; i++) g->pix[(size_t)(r.p[i].y - gtop(g)) * g->w + (r.p[i].x - g->x)] = c;
            g->asDrawn = false;
            px_glyph_touch(g);
            mark(e);
        }
    }
    ras_free(&r);
    free(clip);
    free(mem);
}

static bool refuse(Editor *e)
{
    if (!locked(e)) return false;
    Th_Toast(2, e->readOnly ? "The font is locked" : "The current glyph is hidden");
    return true;
}

static void float_end(Editor *e)
{
    selfloat_free(&e->fl);
    sel_free(&e->src);
    e->floating = e->flCopy = e->flPaste = false;
    if (e->implicitSel) { sel_free(&e->sel); e->implicitSel = false; }
}

/* the float goes away without a trace: the selection is what it was before the lift (a paste leaves none) */
static void float_cancel(Editor *e)
{
    if (!e->floating) return;
    if (e->flPaste) sel_free(&e->sel);
    else sel_copy(&e->sel, &e->src);
    float_end(e);
}

/* would writing cells (x0, y0)-(x1, y1) keep the bitmap within its limit? */
static bool grow_ok(const PxGlyph *g, int x0, int y0, int x1, int y1)
{
    int l = x0, t = y0, r = x1 + 1, b = y1 + 1;
    if (g->w > 0 && g->h > 0) { l = imin(l, g->x); t = imin(t, gtop(g)); r = imax(r, g->x + g->w); b = imax(b, gtop(g) + g->h); }
    return r - l <= MAX_BITMAP && b - t <= MAX_BITMAP;
}

/* The drop: clear where the pixels came from (a move), write them where they are now (the bitmap grows to take them), crop the bitmap to its
 * ink as the hash does.  One undo step, and none when nothing would change.  The selection stays, on the new place. */
static void float_drop(Editor *e)
{
    if (!e->floating) return;
    PxGlyph *g = G(e);
    if (!g || locked(e)) { float_cancel(e); return; }
    bool lifted = !e->flPaste;
    bool moved = !lifted || e->fl.shape.x != e->src.x || e->fl.shape.y != e->src.y;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool ink = moved && selfloat_ink_bounds(&e->fl, &x0, &y0, &x1, &y1);
    SelBitmap b = bm_of(g);
    bool clears = moved && lifted && !e->flCopy && selbm_ink_in(&b, &e->src) > 0;
    if (ink && !grow_ok(g, x0, y0, x1, y1)) {
        Th_Toast(2, "The glyph would get too large");
        float_cancel(e);
        return;
    }
    /* the float's colours into the glyph's palette: those it has keep their characters, a new one takes an entry no pixel uses or a new entry */
    uint32_t cols[PX_GLYPH_MAX_COLORS];
    int idx[PX_NUM_PIXELS], ncols = ink ? float_colors(e, cols, idx) : 0;
    if (ncols && !px_glyph_colors_fit(e->font, g, cols, ncols)) {
        Th_Toast(2, "A glyph has at most 36 colours: the pixels bring too many new ones");
        float_cancel(e);
        return;
    }
    if (clears || ink) {
        touch(e);
        if (clears) { SelBitmap cb = bm_of(g); selbm_clear(&cb, &e->src); }
        int slots[PX_GLYPH_MAX_COLORS];
        if (ink && ncols && px_glyph_add_colors(e->font, g, cols, ncols, slots) != 0) ink = false;
        if (ink && ensure(g, x0, y0, x1, y1)) {
            size_t n = (size_t)e->fl.shape.w * (size_t)e->fl.shape.h;
            for (size_t k = 0; k < n; k++) {      /* the float's characters become the glyph's */
                int i = px_pixel_index(e->fl.pix[k]);
                if (i > 0 && idx[i] >= 0) e->fl.pix[k] = PX_PIXELS[1 + slots[idx[i]]];
            }
            SelBitmap sb = bm_of(g);
            selbm_stamp(&sb, &e->fl);
        }
        g->asDrawn = false;
        if (g->pix) px_glyph_crop(g);
        px_glyph_touch(g);
        mark(e);
        e->editOpen = false;
    }
    float_end(e);
}

/* lift the selected pixels into a float (the glyph does not change until the drop) */
static bool float_lift(Editor *e, bool copy)
{
    PxGlyph *g = G(e);
    if (e->floating) return true;
    if (!g || locked(e) || sel_empty(&e->sel)) return false;
    SelBitmap b = bm_of(g);
    if (!selfloat_lift(&e->fl, &e->sel, &b)) return false;
    e->flPalN = glyph_pal36(e->font, g, e->flPal);
    sel_copy(&e->src, &e->sel);
    e->floating = true;
    e->flCopy = copy;
    e->flPaste = false;
    return true;
}

static void float_move(Editor *e, int dx, int dy)
{
    selfloat_shift(&e->fl, dx, dy);
    sel_shift(&e->sel, dx, dy);
}

/* ------------------------------------------------------------------ the curve
 *
 * Three drags with the Curve tool: the first sets the two end points (like a line: Shift constrains, Ctrl centres), the second pulls the first
 * control point to the cell under the pointer, the third the second one.  The curve is drawn when the third drag is released or on Enter, thrown
 * away by Esc.  Until then nothing changes in the glyph (the preview and the handles are drawn over it); the drawing is one undo step. */

static void curve_cancel(Editor *e)
{
    e->curveStage = 0;
    if (e->mode == M_CURVE) e->mode = M_NONE;
}

static void curve_commit(Editor *e)
{
    if (e->curveStage == 0) return;
    e->curveStage = 0;
    if (locked(e)) return;
    plot_curve(e, put, e->curveBtn == MOUSE_BUTTON_RIGHT ? '.' : ink_ch(e));
    e->editOpen = false;
}

static void curve_press(Editor *e, int cx, int cy, int btn)
{
    e->mode = M_CURVE;
    if (e->curveStage == 0) {
        e->btn = e->curveBtn = btn;
        e->sx = e->lx = cx; e->sy = e->ly = cy;
        e->curveDrag = 0;
        for (int i = 0; i < 4; i++) e->cv4[i] = (RasPt){cx, cy};
    } else {
        e->btn = e->curveBtn;
        e->curveDrag = e->curveStage;
    }
}

static void curve_drag(Editor *e, int cx, int cy)
{
    if (IsKeyPressed(KEY_ESCAPE)) { curve_cancel(e); return; }
    if (e->curveDrag == 0) {
        int en[4];
        shape_ends(e, cx, cy, en);
        e->cv4[0] = (RasPt){en[0], en[1]}; e->cv4[3] = (RasPt){en[2], en[3]};
        e->cv4[1] = e->cv4[0]; e->cv4[2] = e->cv4[3];
    } else if (e->curveDrag == 1) {
        e->cv4[1] = (RasPt){cx, cy};
    } else {
        e->cv4[2] = (RasPt){cx, cy};
    }
    if (IsMouseButtonDown(e->curveBtn)) return;
    e->mode = M_NONE;
    if (e->curveDrag == 0) {
        bool same = e->cv4[0].x == e->cv4[3].x && e->cv4[0].y == e->cv4[3].y;
        e->curveStage = same ? 0 : 1;   /* a click without a drag sets no end points */
    } else if (e->curveDrag == 1) {
        e->curveStage = 2;
    } else {
        curve_commit(e);
    }
}

/* ------------------------------------------------------------------ the polygon and the contour
 *
 * The Polygon: a click adds a vertex and a line follows the mouse; Enter, a click on the last vertex again (a double click) or on the first one closes it, Esc throws
 * it away, Backspace takes the last vertex back.  Shift makes each new edge a clean angle (as for the Line).  The Contour is a drag: on the release the path of the mouse
 * is closed.  Both draw the outline and, with the bar's Filled, the inside by the even-odd rule; one undo step; nothing changes in the glyph until then. */

static void poly_cancel(Editor *e) { e->polyN = 0; }

static void poly_commit(Editor *e)
{
    int n = e->polyN;
    e->polyN = 0;
    if (n == 0 || locked(e)) return;
    RasPath r = {0};
    if (ras_polygon(&r, e->poly, n, true, e->filled))
        for (int i = 0; i < r.n; i++) put(e, r.p[i].x, r.p[i].y, e->polyBtn == MOUSE_BUTTON_RIGHT ? '.' : ink_ch(e));
    ras_free(&r);
    e->editOpen = false;
}

/* the vertex that (hx, hy) means: with Shift the edge from the last vertex takes a clean angle */
static RasPt poly_point(const Editor *e, int hx, int hy)
{
    RasPt p = {hx, hy};
    if (e->polyN > 0 && key_shift()) {
        RasPt l = e->poly[e->polyN - 1];
        int dx, dy;
        ras_snap(hx - l.x, hy - l.y, &dx, &dy);
        p.x = l.x + dx; p.y = l.y + dy;
    }
    return p;
}

static void poly_press(Editor *e, int cx, int cy, int btn)
{
    if (e->polyN > 0 && btn != e->polyBtn) return;
    RasPt p = poly_point(e, cx, cy);
    int n = e->polyN;
    if (n > 0 && ((p.x == e->poly[n - 1].x && p.y == e->poly[n - 1].y) || (n >= 2 && p.x == e->poly[0].x && p.y == e->poly[0].y))) { poly_commit(e); return; }
    if (n >= e->polyCap) {
        int nc = e->polyCap ? e->polyCap * 2 : 32;
        RasPt *q = (RasPt *)realloc(e->poly, sizeof(RasPt) * (size_t)nc);
        if (!q) return;
        e->poly = q; e->polyCap = nc;
    }
    if (n == 0) e->polyBtn = btn;
    e->poly[e->polyN++] = p;
}

static void poly_back(Editor *e) { if (e->polyN > 0) e->polyN--; }

/* the cells of the contour dragged so far, closed: the mouse's path loses the corner cells of its Ls (pixel-perfect), then it is a polygon */
static bool cont_cells(const Editor *e, RasPath *out)
{
    RasPath t = {0};
    bool ok = true;
    for (int i = 0; i < e->cont.n && ok; i++) ok = ras_add(&t, e->cont.p[i].x, e->cont.p[i].y);
    if (ok) ok = ras_polygon(out, t.p, ras_pixel_perfect(t.p, t.n), true, e->filled);
    ras_free(&t);
    return ok;
}

static void cont_press(Editor *e, int cx, int cy, int btn)
{
    ras_free(&e->cont);
    ras_add(&e->cont, cx, cy);
    e->mode = M_CONTOUR;
    e->btn = btn;
}

static void cont_drag(Editor *e, int cx, int cy)
{
    if (IsKeyPressed(KEY_ESCAPE)) { ras_free(&e->cont); e->mode = M_NONE; return; }
    RasPath s = {0};
    if (e->cont.n > 0 && ras_line(&s, e->cont.p[e->cont.n - 1].x, e->cont.p[e->cont.n - 1].y, cx, cy))
        for (int i = 0; i < s.n; i++) ras_add(&e->cont, s.p[i].x, s.p[i].y);
    ras_free(&s);
    if (IsMouseButtonDown(e->btn)) return;
    e->mode = M_NONE;
    RasPath r = {0};
    if (!locked(e) && cont_cells(e, &r))
        for (int i = 0; i < r.n; i++) put(e, r.p[i].x, r.p[i].y, e->btn == MOUSE_BUTTON_RIGHT ? '.' : ink_ch(e));
    ras_free(&r);
    ras_free(&e->cont);
    e->editOpen = false;
}

static void set_tool(Editor *e, Tool t)
{
    if (t == e->tool) return;
    curve_commit(e);
    poly_commit(e);
    float_drop(e);
    e->tool = t;
}

static void sel_none(Editor *e)
{
    float_drop(e);
    sel_free(&e->sel);
    e->implicitSel = false;
}

static void sel_everything(Editor *e)
{
    int x0, y0, x1, y1;
    if (locked(e) || !glyph_region(e, &x0, &y0, &x1, &y1)) return;
    float_drop(e);
    sel_set_rect(&e->sel, x0, y0, x1 - 1, y1 - 1);
}

static void sel_inverse(Editor *e)
{
    int x0, y0, x1, y1;
    if (locked(e) || !glyph_region(e, &x0, &y0, &x1, &y1)) return;
    float_drop(e);
    sel_invert(&e->sel, x0, y0, x1 - 1, y1 - 1);
}

static void sel_wand_at(Editor *e, int cx, int cy, SelMode mode)
{
    int x0, y0, bw, bh;
    char *mem = region_pix(e, &x0, &y0, &bw, &bh);
    if (!mem) return;
    SelBitmap rg = {mem, x0, y0, bw, bh};
    SelMask shape;
    memset(&shape, 0, sizeof shape);
    sel_wand(&shape, &rg, cx, cy, e->wandDiag);
    sel_combine(&e->sel, &shape, mode);
    sel_free(&shape);
    free(mem);
}

/* Delete / Backspace: the selected cells become empty.  A float that is a move loses its pixels (and the place they came from is cleared); a float
 * that is a copy or a paste is just thrown away. */
static void sel_delete(Editor *e)
{
    PxGlyph *g = G(e);
    if (!g || refuse(e)) return;
    if (e->floating) {
        if (e->flPaste || e->flCopy) { float_cancel(e); sel_free(&e->sel); return; }
        SelBitmap b = bm_of(g);
        if (selbm_ink_in(&b, &e->src) > 0) {
            touch(e);
            selbm_clear(&b, &e->src);
            g->asDrawn = false;
            if (g->pix) px_glyph_crop(g);
            px_glyph_touch(g);
            mark(e);
            e->editOpen = false;
        }
        float_end(e);
        return;
    }
    if (sel_empty(&e->sel)) return;
    SelBitmap b = bm_of(g);
    if (selbm_ink_in(&b, &e->sel) == 0) return;
    touch(e);
    selbm_clear(&b, &e->sel);
    g->asDrawn = false;
    if (g->pix) px_glyph_crop(g);
    px_glyph_touch(g);
    mark(e);
    e->editOpen = false;
}

static bool clip_copy(Editor *e)
{
    PxGlyph *g = G(e);
    if (!g) return false;
    if (e->floating) {
        if (!selfloat_copy(&g_clip, &e->fl)) return false;
        memcpy(g_clipPal, e->flPal, sizeof g_clipPal);
        g_clipPalN = e->flPalN;
        return true;
    }
    if (sel_empty(&e->sel)) return false;
    SelBitmap b = bm_of(g);
    if (!selfloat_lift(&g_clip, &e->sel, &b)) return false;
    g_clipPalN = glyph_pal36(e->font, g, g_clipPal);   /* the clipboard carries the colours (a paste into another glyph or font maps them) */
    return true;
}

static void clip_cut(Editor *e)
{
    if (refuse(e)) return;
    if (clip_copy(e)) sel_delete(e);
}

/* Paste: a float at the cells the pixels were copied from; when they are not all in view, with its top-left cell on the top-left cell of the view */
static void clip_paste(Editor *e)
{
    if (refuse(e)) return;
    if (sel_empty(&g_clip.shape) || !g_clip.pix) { Th_Toast(1, "Nothing to paste"); return; }
    float_drop(e);
    int z = e->zoom > 0 ? e->zoom : 1;
    int vx0 = floordiv(-e->vx, z), vy0 = floordiv(-e->vy, z);
    int vx1 = floordiv((int)e->cv.width - 1 - e->vx, z), vy1 = floordiv((int)e->cv.height - 1 - e->vy, z);
    int dx = 0, dy = 0;
    const SelMask *s = &g_clip.shape;
    if (e->cv.width > 0 && (s->x < vx0 || s->y < vy0 || s->x + s->w - 1 > vx1 || s->y + s->h - 1 > vy1)) { dx = vx0 - s->x; dy = vy0 - s->y; }
    if (!selfloat_copy(&e->fl, &g_clip)) return;
    memcpy(e->flPal, g_clipPal, sizeof e->flPal);    /* the pixels keep their colours: the drop adds those the glyph has not got to its palette */
    e->flPalN = g_clipPalN;
    selfloat_shift(&e->fl, dx, dy);
    sel_copy(&e->sel, &e->fl.shape);
    sel_free(&e->src);
    e->floating = true;
    e->flPaste = true;
    e->flCopy = false;
    e->implicitSel = false;
    if (!is_sel_tool(e->tool)) e->tool = T_MARQUEE;   /* the pasted pixels are dragged with a selection tool */
}

/* arrow keys and the bar's MOVE buttons with a selection: the pixels float by whole cells until the drop */
static void sel_nudge(Editor *e, int dx, int dy)
{
    if (locked(e) || sel_empty(&e->sel)) return;
    if (!float_lift(e, false)) return;
    float_move(e, dx, dy);
}

/* the rectangle of the marquee being dragged (Shift pressed after the press makes it a square) */
static void box_cells(const Editor *e, int *x0, int *y0, int *x1, int *y1)
{
    int ex = e->hx, ey = e->hy;
    if (key_shift() && !e->shiftAtPress) {
        int dx = ex - e->sx, dy = ey - e->sy, side = imax(abs(dx), abs(dy));
        ex = e->sx + (dx < 0 ? -side : side);
        ey = e->sy + (dy < 0 ? -side : side);
    }
    *x0 = imin(e->sx, ex); *x1 = imax(e->sx, ex);
    *y0 = imin(e->sy, ey); *y1 = imax(e->sy, ey);
}

static bool lasso_add(Editor *e, int cx, int cy)
{
    if (e->lassoN > 0 && e->lasso[2 * e->lassoN - 2] == cx && e->lasso[2 * e->lassoN - 1] == cy) return true;
    if (e->lassoN >= e->lassoCap) {
        int nc = e->lassoCap ? e->lassoCap * 2 : 64;
        int *n = (int *)realloc(e->lasso, sizeof(int) * 2 * (size_t)nc);
        if (!n) return false;
        e->lasso = n; e->lassoCap = nc;
    }
    e->lasso[2 * e->lassoN] = cx; e->lasso[2 * e->lassoN + 1] = cy;
    e->lassoN++;
    return true;
}

/* The press of a selection tool at cell (cx, cy).  Modifiers at the press: Shift adds, Alt subtracts, both intersect; otherwise the bar's mode. */
static void sel_press(Editor *e, int cx, int cy, Vector2 mouse)
{
    PxGlyph *g = G(e);
    bool shift = key_shift(), alt = key_alt(), modKey = shift || alt;
    SelMode m = e->selMode;
    if (shift && alt) m = SEL_INTERSECT; else if (shift) m = SEL_ADD; else if (alt) m = SEL_SUB;
    e->dragSel = m;
    e->shiftAtPress = shift;
    e->dragged = false;
    e->pressPos = mouse;
    e->btn = MOUSE_BUTTON_LEFT;
    e->sx = e->lx = cx; e->sy = e->ly = cy;
    bool inFloat = e->floating && sel_get(&e->fl.shape, cx, cy);
    bool inSel = sel_get(&e->sel, cx, cy);
    Tool t = e->tool;
    if (e->floating && !inFloat && t != T_MOVE) float_drop(e);   /* a press outside drops the pixels first */

    if (t == T_WAND) {
        float_drop(e);
        sel_wand_at(e, cx, cy, m);
        return;
    }
    if (t == T_MOVE || (!modKey && m == SEL_REPLACE && (inFloat || inSel))) {   /* in the bar's Add, Sub and And modes a press inside the selection still changes the selection */
        bool copy = key_ctrl();
        if (!e->floating && sel_empty(&e->sel)) {   /* the Move tool with nothing selected takes the whole bitmap */
            if (!g || g->w <= 0 || g->h <= 0) return;
            sel_set_rect(&e->sel, g->x, gtop(g), g->x + g->w - 1, gtop(g) + g->h - 1);
            e->implicitSel = true;
        }
        e->mode = M_SELMOVE;
        e->moveCopy = copy;
        e->moveBx = e->floating ? e->fl.shape.x : e->sel.x;
        e->moveBy = e->floating ? e->fl.shape.y : e->sel.y;
        return;
    }
    if (t == T_LASSO) {
        e->lassoN = 0;
        lasso_add(e, cx, cy);
        e->mode = M_SELLASSO;
        return;
    }
    e->mode = M_SELBOX;
}

/* the drag of a selection tool: called every frame while e->mode is one of M_SELBOX, M_SELLASSO, M_SELMOVE */
static void sel_drag(Editor *e, int cx, int cy, Vector2 mouse)
{
    bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (fabsf(mouse.x - e->pressPos.x) >= 3 || fabsf(mouse.y - e->pressPos.y) >= 3) e->dragged = true;
    if (e->mode == M_SELMOVE) {
        SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        if (IsKeyPressed(KEY_ESCAPE)) { float_cancel(e); if (e->implicitSel) { sel_free(&e->sel); e->implicitSel = false; } e->mode = M_NONE; return; }
        int dx = cx - e->sx, dy = cy - e->sy;
        if (!e->floating && (dx != 0 || dy != 0)) {
            if (!float_lift(e, e->moveCopy)) { e->mode = M_NONE; return; }
            e->moveBx = e->fl.shape.x; e->moveBy = e->fl.shape.y;
        }
        if (e->floating) float_move(e, e->moveBx + dx - e->fl.shape.x, e->moveBy + dy - e->fl.shape.y);
        if (!down) {
            e->mode = M_NONE;
            if (!e->floating && e->implicitSel) { sel_free(&e->sel); e->implicitSel = false; }   /* a click: nothing moved */
        }
        return;
    }
    SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
    if (IsKeyPressed(KEY_ESCAPE)) { e->mode = M_NONE; return; }
    if (e->mode == M_SELLASSO && !lasso_add(e, cx, cy)) { e->mode = M_NONE; return; }
    if (down) return;
    bool lasso = e->mode == M_SELLASSO;
    e->mode = M_NONE;
    SelMask shape;
    memset(&shape, 0, sizeof shape);
    if (!e->dragged) {   /* a click: deselect (adding or subtracting nothing changes nothing) */
        if (e->dragSel == SEL_REPLACE) sel_free(&e->sel);
        return;
    }
    if (lasso) sel_polygon(&shape, e->lasso, e->lassoN);
    else { int x0, y0, x1, y1; box_cells(e, &x0, &y0, &x1, &y1); sel_set_rect(&shape, x0, y0, x1, y1); }
    sel_combine(&e->sel, &shape, e->dragSel);
    sel_free(&shape);
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
    int pi = kern_partner(e);
    if (pi >= 0) {   /* the partner after it */
        const PxGlyph *p = &e->font->glyphs[pi];
        int pp = kern_pen(e);
        xb = imax(xb, pp + imax(p->advance, p->w > 0 ? p->x + p->w : 0) + MARGIN);
        xa = imin(xa, pp + (p->w > 0 ? p->x : 0) - MARGIN);
        if (p->w > 0) { ya = imin(ya, gtop(p) - MARGIN); yb = imax(yb, gtop(p) + p->h + MARGIN); }
    }
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
    const Color *pal = (const Color *)px_glyph_palette(f, g)->draw;   /* the glyph's own colours, ready to draw */
    for (int y = 0; y < g->h; y++) {
        int x = 0;
        while (x < g->w) {
            char c = g->pix[y * g->w + x];
            int run = 1;
            while (x + run < g->w && g->pix[y * g->w + x + run] == c) run++;
            if (c != '.') {
                int pi = px_pixel_index(c);
                if (pi > 0) DrawRectangle(sx + (g->x + x) * scale, sy + (gtop(g) + y) * scale, run * scale, scale, pal[pi]);
            }
            x += run;
        }
    }
}

/* A layer's glyph: every inked pixel in one colour, at the offset (dx right, dy up) */
/* ------------------------------------------------------------------ widgets */

static bool hit(Rectangle r) { return !Th_MouseBlocked() && CheckCollisionPointRec(GetMousePosition(), r); }

static void sep(float x, float y, float h) { Th_VLine(x, y + 9, y + h - 9, TH_LINE); }

/* ------------------------------------------------------------------ the palette bar (see DESIGN.md, "The palette bar and the colour picker")
 *
 * A horizontal bar under the canvas and the tool strip, over the status line.  Its first row is the edited glyph: the transparent tile '.' (the
 * eraser ink), then the slots of the glyph's palette in canonical order, each labelled with its pixel character (0-9, A-Z: the key that picks
 * it), then '+' while the palette has fewer than 36 entries.  A slot no pixel uses has a dashed frame.  The rows under it are the font's
 * other colours (no key).  A click picks the ink; a double click on a slot opens the colour picker to change that colour in the glyph; a
 * colour of the font dragged onto a slot replaces the slot's colour, onto '+' adds it.  When an edit reorders the slots they slide to their
 * new places.  Tiles register as "pal:." "pal:0" ... "pal:Z" "pal:+" and every colour as "pal:#RRGGBB" (RRGGBBAA when not opaque). */

#define BAR_PAD 8
#define BAR_SEP 6        /* after '.' */
#define BAR_ROWS_MAX 3   /* the font's colours: rows shown before the bar scrolls */
#define ANIM_T 0.2       /* the slide of a reordered slot, seconds */
enum { PICK_ADD = 0x2301, PICK_EDIT = 0x2302 };

static bool ink_is(const Editor *e, int idx, uint32_t c) { return idx < 0 ? e->inkErase : (!e->inkErase && e->inkColor == c); }

static void set_ink(Editor *e, int idx, uint32_t c) { e->inkErase = idx < 0; if (idx >= 0) e->inkColor = c; }

/* the font's colour list (at least black) */
static int ink_list(const Editor *e, const uint32_t **out) { return px_font_color_list(e->font, out); }

/* the ink is the colour list's entry k (Editor_Open) */
static void ink_pick_index(Editor *e, int k)
{
    const uint32_t *l;
    int n = ink_list(e, &l);
    if (k >= 0 && k < n) set_ink(e, k, l[k]);
}
static void close_edit_cmd(Editor *e) { e->editOpen = false; }

/* the slots the bar shows: the glyph's palette, its first 36 entries */
static int bar_slots(const Editor *e, const uint32_t **c)
{
    const PxGlyph *g = G((Editor *)e);
    if (!g) { *c = NULL; return 0; }
    const PxPalette *p = px_glyph_palette(e->font, g);
    *c = p->c;
    return p->n < PX_GLYPH_MAX_COLORS ? p->n : PX_GLYPH_MAX_COLORS;
}

/* the colours used by pixels: slots 0..k-1 (the glyph is canonical) */
static int bar_used(const Editor *e)
{
    const PxGlyph *g = G((Editor *)e);
    return g ? px_glyph_used_colors(g) : 0;
}

/* the font's colours the glyph has not: a list (malloc'd) and its length */
static int bar_extras(const Editor *e, uint32_t **out)
{
    const uint32_t *l, *c;
    int n = ink_list(e, &l), ns = bar_slots(e, &c), m = 0;
    *out = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(n + 1));
    if (!*out) return 0;
    for (int i = 0; i < n; i++) {
        bool has = false;
        for (int j = 0; j < ns && !has; j++) has = c[j] == l[i];
        if (!has) (*out)[m++] = l[i];
    }
    return m;
}

/* the tile pitch for a bar of width w: a full glyph ('.', 36 slots, '+') fits one row */
static int bar_step(float w)
{
    int s = (int)((w - 2 * BAR_PAD - BAR_SEP) / (PX_GLYPH_MAX_COLORS + 2));
    return s < 18 ? 18 : s > 28 ? 28 : s;
}
static int bar_gap(int step) { return step >= 24 ? 4 : 3; }

static int bar_per_row(float w, int step) { int n = (int)((w - 2 * BAR_PAD + bar_gap(step)) / step); return n < 1 ? 1 : n; }

/* the height of the bar for a width w */
static float bar_height(const Editor *e, float w)
{
    int step = bar_step(w);
    uint32_t *x;
    int nx = bar_extras(e, &x);
    free(x);
    int rows = (nx + bar_per_row(w, step) - 1) / bar_per_row(w, step);
    if (rows > BAR_ROWS_MAX) rows = BAR_ROWS_MAX;
    return (float)(2 * BAR_PAD + step - bar_gap(step) + (rows > 0 ? 9 + rows * step - bar_gap(step) : 0));
}

static void tile_names(uint32_t c, char *out)
{
    if ((c & 0xFFu) == 0xFFu) snprintf(out, 24, "pal:#%06X", (unsigned)(c >> 8));
    else snprintf(out, 24, "pal:#%08X", (unsigned)c);
}

/* a colour tile: the colour (over a checker when it is not opaque), its key, the frame (dashed for an unused slot) and the ink's ring */
static void draw_tile(Rectangle r, uint32_t c, char key, bool unused, bool ink, bool hot, float flash)
{
    Color col = Th_FromRGBA(c);
    Rectangle in = unused ? (Rectangle){r.x + 3, r.y + 3, r.width - 6, r.height - 6} : r;
    if (col.a < 255) {
        DrawRectangleRec(in, TH_PANEL);
        DrawRectangle((int)in.x, (int)in.y, (int)in.width / 2, (int)in.height / 2, TH_TRACK);
        DrawRectangle((int)(in.x + in.width / 2), (int)(in.y + in.height / 2), (int)(in.width - (int)in.width / 2), (int)(in.height - (int)in.height / 2), TH_TRACK);
    }
    DrawRectangleRec(in, col);
    if (key) {
        double lum = col.a < 128 ? 1.0 : cm_luminance(c);
        char s[2] = {key, 0};
        Vector2 m = Th_MeasureSmall(s);
        Th_TextSmall(s, r.x + (int)((r.width - m.x) / 2), r.y + (int)((r.height - m.y) / 2), lum > 0.36 ? TH_ON_LIGHT : TH_ON_DARK);
    }
    if (unused) {   /* dashed: a colour of the glyph's palette that no pixel uses yet */
        Color d = hot ? TH_ACCENT : TH_TEXT_DIM;
        Th_DashedHLine(r.x, r.x + r.width, r.y, 2, d);
        Th_DashedHLine(r.x, r.x + r.width, r.y + r.height - 1, 2, d);
        Th_DashedVLine(r.x, r.y, r.y + r.height, 2, d);
        Th_DashedVLine(r.x + r.width - 1, r.y, r.y + r.height, 2, d);
    } else Th_RectLines(r, hot ? TH_M_BOX : TH_LINE);
    if (flash > 0) DrawRectangleLinesEx((Rectangle){r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 2, Fade(TH_ACCENT, flash));
    if (ink) DrawRectangleLinesEx((Rectangle){r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 2, TH_ACCENT);
}

static void hex_of(uint32_t c, char *out) { char h[10]; cm_format_hex(c, h); snprintf(out, 12, "#%s", h); }

/* The palette is changed by a command of the bar (one undo step each). */
static void pal_add(Editor *e, uint32_t c)
{
    PxGlyph *g = G(e);
    if (!g || refuse(e)) return;
    const uint32_t *sl;
    int ns = bar_slots(e, &sl);
    for (int i = 0; i < ns; i++) if (sl[i] == c && (g->pi > 0 || g->pix)) { set_ink(e, 0, c); return; }   /* it has it */
    if (px_glyph_palette(e->font, g)->n >= PX_GLYPH_MAX_COLORS) { Th_Toast(2, "This glyph has 36 colours: drop the colour onto a slot to replace one"); return; }
    touch(e);
    if (px_glyph_add_slot(e->font, g, c) >= 0) { mark(e); set_ink(e, 0, c); }
    e->editOpen = false;
}

static void pal_replace(Editor *e, int slot, uint32_t c)
{
    PxGlyph *g = G(e);
    if (!g || refuse(e)) return;
    const uint32_t *sl;
    int ns = bar_slots(e, &sl);
    if (slot < 0 || slot >= ns || sl[slot] == c) return;
    uint32_t was = sl[slot];
    touch(e);
    if (px_glyph_replace_color(e->font, g, slot, c) == 0) {
        mark(e);
        if (!e->inkErase && e->inkColor == was) e->inkColor = c;   /* the ink follows its slot */
    }
    e->editOpen = false;
}

/* the answer of the colour picker */
static void pick_answer(Editor *e)
{
    uint32_t c = 0;
    int r = e->pickTag ? Th_ColorPickTake(e->pickTag, &c) : 0;
    if (!r) return;
    int tag = e->pickTag;
    e->pickTag = 0;
    if (r < 0) return;     /* cancelled */
    if (tag == PICK_ADD) pal_add(e, c);
    else {
        const uint32_t *sl;
        int ns = bar_slots(e, &sl);
        for (int i = 0; i < ns; i++) if (sl[i] == e->pickCol) { pal_replace(e, i, c); break; }
    }
}

static void open_picker(Editor *e, int tag, uint32_t col)
{
    e->pickTag = tag;
    e->pickCol = col;
    Th_ColorPickOpen(col, tag == PICK_ADD ? "Add a colour to the glyph" : "Change the colour of the slot", tag);
}

/* the slide of the slots: where slot colour c is drawn this frame (target x) */
static float anim_x(Editor *e, uint32_t c, float target, float *flash)
{
    *flash = 0;
    double now = GetTime();
    for (int i = 0; i < e->nanim; i++) {
        if (e->anim[i].c != c) continue;
        double t = (now - e->anim[i].t0) / ANIM_T;
        if (t >= 1) return target;
        double k = 1 - t, ease = k * k * k;     /* ease-out cubic */
        if (e->anim[i].flash) { *flash = (float)ease; return target; }
        return target + (float)((e->anim[i].from - target) * ease);
    }
    return target;
}

/* a new frame of the bar: the slots that are no longer where they were start to slide from where they were drawn */
static void anim_update(Editor *e, const uint32_t *sl, int ns, float x0, int step)
{
    bool same = e->shownGi == e->gi && e->shownStep == (float)step && e->shownX0 == x0 && e->nshown > 0;
    double now = GetTime();
    typeof(e->anim[0]) na[PX_GLYPH_MAX_COLORS];
    int nn = 0;
    for (int i = 0; i < ns; i++) {
        float target = x0 + BAR_SEP + step * (1 + i);
        int old = -1;
        for (int j = 0; j < e->nanim; j++) if (e->anim[j].c == sl[i]) old = j;
        int was = -1;
        for (int j = 0; j < e->nshown; j++) if (e->shown[j].c == sl[i]) was = j;
        if (!same) continue;
        if (old >= 0 && e->anim[old].to == target && now - e->anim[old].t0 < ANIM_T) na[nn++] = e->anim[old];   /* still sliding there */
        else if (was >= 0 && fabsf(e->shown[was].x - target) > 0.5f) {
            na[nn].c = sl[i]; na[nn].from = e->shown[was].x; na[nn].to = target; na[nn].t0 = now; na[nn].flash = false; nn++;
        } else if (was < 0) {
            na[nn].c = sl[i]; na[nn].from = na[nn].to = target; na[nn].t0 = now; na[nn].flash = true; nn++;   /* new in the row: a short glow */
        }
    }
    memcpy(e->anim, na, sizeof(na[0]) * (size_t)nn);
    e->nanim = nn;
}

/* the tile under the mouse while a colour is dragged: 2 a slot (*slot), 3 '+', 0 none */
static int drop_target(const Editor *e, const Rectangle *slots, int ns, Rectangle plus, bool hasPlus, int *slot)
{
    Vector2 m = GetMousePosition();
    for (int i = 0; i < ns; i++) if (CheckCollisionPointRec(m, slots[i])) { *slot = i; return 2; }
    if (hasPlus && CheckCollisionPointRec(m, plus)) return 3;
    return 0;
}

static void draw_bar(Editor *e, Rectangle r)
{
    PxGlyph *g = G(e);
    e->bar = r;
    DrawRectangleRec(r, TH_PANEL_ALT);
    Th_HLine(r.x, r.x + r.width, r.y, TH_LINE);
    if (!g) return;
    int step = bar_step(r.width), gap = bar_gap(step), ts = step - gap;
    const uint32_t *sl;
    int ns = bar_slots(e, &sl), used = bar_used(e);
    bool empty = !(g->pi > 0) && !g->pix;          /* an empty glyph of the default palette: its black is unused */
    if (empty) used = 0;
    bool edit = !locked(e), hasPlus = edit && px_glyph_palette(e->font, g)->n < PX_GLYPH_MAX_COLORS;
    float x0 = r.x + BAR_PAD, y0 = r.y + BAR_PAD;
    bool live = !Th_InputBlocked();
    Vector2 mouse = GetMousePosition();
    bool pressed = live && IsMouseButtonPressed(MOUSE_BUTTON_LEFT), released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    static char tip[200];
    char nm[24];
    bool pressedSlot = false;   /* a press elsewhere ends a double click */

    anim_update(e, sl, ns, x0, step);

    /* row 1: '.', the slots, '+' */
    Rectangle dot = {x0, y0, (float)ts, (float)ts};
    Rectangle srect[PX_GLYPH_MAX_COLORS];
    for (int i = 0; i < ns; i++) srect[i] = (Rectangle){x0 + BAR_SEP + step * (1 + i), y0, (float)ts, (float)ts};
    Rectangle plus = {x0 + BAR_SEP + step * (1 + ns), y0, (float)ts, (float)ts};
    int dropKind = 0, dropSlot = -1;
    if (e->barDrag) dropKind = drop_target(e, srect, ns, plus, hasPlus, &dropSlot);

    {   /* '.' */
        bool hv = live && hit(dot);
        Th_Reg("pal:.", dot);
        DrawRectangleRec(dot, TH_PANEL);
        DrawLineEx((Vector2){dot.x + 4, dot.y + 4}, (Vector2){dot.x + dot.width - 4, dot.y + dot.height - 4}, 1, TH_TEXT_OFF);
        Vector2 m = Th_MeasureSmall(".");
        Th_TextSmall(".", dot.x + (int)((dot.width - m.x) / 2), dot.y + (int)((dot.height - m.y) / 2) - 3, TH_TEXT_DIM);
        Th_RectLines(dot, hv ? TH_M_BOX : TH_LINE);
        if (e->inkErase) DrawRectangleLinesEx((Rectangle){dot.x - 2, dot.y - 2, dot.width + 4, dot.height + 4}, 2, TH_ACCENT);
        if (hv) { snprintf(tip, sizeof tip, "Transparent: draws nothing, erases  (.)"); e->tip = tip; e->tipRect = dot; }
        if (hv && pressed) { e->pressKind = 1; e->pressAt = mouse; }
        if (released && e->pressKind == 1 && hv) set_ink(e, -1, 0);
    }
    for (int i = 0; i < ns; i++) {
        float flash;
        Rectangle t = srect[i];
        t.x = anim_x(e, sl[i], srect[i].x, &flash);
        bool hv = live && hit(srect[i]);
        bool unused = i >= used;
        draw_tile(t, sl[i], PX_PIXELS[1 + i], unused, ink_is(e, 0, sl[i]), hv || (dropKind == 2 && dropSlot == i), flash);
        snprintf(nm, sizeof nm, "pal:%c", PX_PIXELS[1 + i]);
        Th_Reg(nm, srect[i]);
        tile_names(sl[i], nm);
        Th_Reg(nm, srect[i]);
        if (hv && !e->barDrag) {
            char hx[12];
            hex_of(sl[i], hx);
            snprintf(tip, sizeof tip, "Ink %s  (%c)%s", hx, PX_PIXELS[1 + i], unused ? ": no pixel uses it yet" : edit ? ": double-click to change it in this glyph" : "");
            e->tip = tip; e->tipRect = srect[i];
        }
        if (hv && pressed) { e->pressKind = 2; e->pressCol = sl[i]; e->pressSlot = i; e->pressAt = mouse; pressedSlot = true; }
        if (released && e->pressKind == 2 && hv && e->pressSlot == i) {
            set_ink(e, 0, sl[i]);
            if (edit && GetTime() - e->lastClick < 0.4 && e->lastClickCol == sl[i]) { open_picker(e, PICK_EDIT, sl[i]); e->lastClick = -10; }
            else { e->lastClick = GetTime(); e->lastClickCol = sl[i]; }
        }
    }
    if (hasPlus) {
        bool hv = live && hit(plus);
        bool hot = hv || dropKind == 3;
        Th_Reg("pal:+", plus);
        DrawRectangleRec(plus, hot ? TH_PANEL : TH_PANEL_ALT);
        Th_RectLines(plus, hot ? TH_ACCENT : TH_LINE);
        float cx = plus.x + plus.width / 2, cy = plus.y + plus.height / 2, k = ts * 0.28f;
        DrawRectangle((int)(cx - k), (int)cy - 1, (int)(2 * k), 2, hot ? TH_ACCENT : TH_TEXT_DIM);
        DrawRectangle((int)cx - 1, (int)(cy - k), 2, (int)(2 * k), hot ? TH_ACCENT : TH_TEXT_DIM);
        if (hv && !e->barDrag) { snprintf(tip, sizeof tip, "Add a colour to this glyph (the colour picker)"); e->tip = tip; e->tipRect = plus; }
        if (hv && pressed) { e->pressKind = 3; e->pressAt = mouse; }
        if (released && e->pressKind == 3 && hv) open_picker(e, PICK_ADD, e->inkErase ? 0xFFFFFFFFu : e->inkColor);
    }

    /* the rows of the font's other colours */
    uint32_t *xs;
    int nx = bar_extras(e, &xs);
    e->barExtras = nx;
    e->barRows = 1;
    if (nx > 0) {
        int per = bar_per_row(r.width, step), rows = (nx + per - 1) / per, vis = rows < BAR_ROWS_MAX ? rows : BAR_ROWS_MAX;
        e->barRows = 1 + rows;
        float ry = y0 + step + 9;
        Th_HLine(r.x + BAR_PAD, r.x + r.width - BAR_PAD, ry - 5, TH_LINE_SOFT);
        Rectangle view = {r.x, ry - 2, r.width, (float)(vis * step - gap + 4)};
        bool onBar = Th_ScrollBar(view, &e->barScroll, (float)(rows * step - gap + 4), (float)step, live);
        BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
        for (int i = 0; i < nx; i++) {
            Rectangle t = {x0 + step * (i % per), ry + step * (i / per) - e->barScroll, (float)ts, (float)ts};
            if (t.y + t.height < view.y || t.y > view.y + view.height) continue;
            bool inside = t.y >= view.y && t.y + t.height <= view.y + view.height;
            bool hv = live && inside && !onBar && hit(t);
            draw_tile(t, xs[i], 0, false, ink_is(e, 0, xs[i]), hv, 0);
            snprintf(nm, sizeof nm, "palx:%d", i);
            Th_Reg(nm, t);
            tile_names(xs[i], nm);
            Th_Reg(nm, t);
            if (hv && !e->barDrag) {
                char hx[12];
                hex_of(xs[i], hx);
                snprintf(tip, sizeof tip, "%s, a colour of other glyphs: click to draw with it%s", hx, edit ? ", drag it onto a slot to replace that colour, onto + to add it" : "");
                e->tip = tip; e->tipRect = t;
            }
            if (hv && pressed) { e->pressKind = 4; e->pressCol = xs[i]; e->pressAt = mouse; }
            if (released && e->pressKind == 4 && hv && !e->barDrag) set_ink(e, 0, xs[i]);
        }
        EndScissorMode();
    }
    free(xs);

    /* the drag of a colour of the font: it follows the mouse; the release drops it */
    if (e->pressKind == 4 && edit && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (fabsf(mouse.x - e->pressAt.x) > 4 || fabsf(mouse.y - e->pressAt.y) > 4)) e->barDrag = true;
    if (e->barDrag) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        Rectangle t = {mouse.x - ts / 2.0f, mouse.y - ts / 2.0f, (float)ts, (float)ts};
        DrawRectangle((int)t.x + 2, (int)t.y + 3, (int)t.width, (int)t.height, Fade(BLACK, 0.15f));
        draw_tile(t, e->pressCol, 0, false, false, true, 0);
        snprintf(tip, sizeof tip, dropKind == 2 ? "Drop: slot %c becomes this colour" : dropKind == 3 ? "Drop: add the colour to this glyph" : "Drop it onto a slot (replace) or onto + (add)", dropSlot >= 0 ? PX_PIXELS[1 + dropSlot] : ' ');
        e->tip = tip; e->tipRect = t;
        if (released) {
            if (dropKind == 2) pal_replace(e, dropSlot, e->pressCol);
            else if (dropKind == 3) pal_add(e, e->pressCol);
        }
    }
    if (released || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { e->pressKind = 0; e->barDrag = false; }
    if (pressed && !pressedSlot) e->lastClick = -10;

    /* where the slots are, for the slide of the next reorder */
    e->nshown = ns;
    for (int i = 0; i < ns; i++) { float f; e->shown[i].c = sl[i]; e->shown[i].x = anim_x(e, sl[i], srect[i].x, &f); }
    e->shownGi = e->gi; e->shownStep = (float)step; e->shownX0 = x0;
}

/* A slot of the glyph (or '.') picked by its key: '.', '0'-'9', 'A'-'Z' (either case).  Nothing for a slot it has not. */
static void key_slot(Editor *e, int ch)
{
    if (ch == '.') { set_ink(e, -1, 0); return; }
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    int i = px_pixel_index((char)ch);
    if (ch > 127 || i <= 0) return;
    const uint32_t *sl;
    int ns = bar_slots(e, &sl);
    if (i - 1 < ns) set_ink(e, 0, sl[i - 1]);
}

/* The unused-slot rule (DESIGN.md, "The unused-slot rule"): when the view leaves the glyph, or the font is saved, the slots it made that no pixel uses go.
 * The palette the glyph had when the view opened it stays when it is still that one, and comes back when its first colours are the glyph's
 * again (a palette read or shared keeps being written); otherwise the palette is cut to the colours the pixels use. */
static void leave_glyph(Editor *e)
{
    PxGlyph *g = G(e);
    if (!g || locked(e) || g->lazy) return;
    px_glyph_canonicalize(e->font, g);
    const PxPalette *p = px_glyph_palette(e->font, g);
    bool own = g->pi > 0 && g->pi < e->font->npals;
    if (!own && e->p0n == 0) return;
    if (own && p->n == e->p0n && memcmp(p->c, e->p0, sizeof(uint32_t) * (size_t)p->n) == 0) return;
    int k = px_glyph_used_colors(g);
    if (e->p0n >= k && k > 0 && memcmp(p->c, e->p0, sizeof(uint32_t) * (size_t)k) == 0) { px_glyph_set_palette(e->font, g, e->p0, e->p0n); return; }
    px_glyph_trim_palette(e->font, g);
}

static void remember_p0(Editor *e)
{
    PxGlyph *g = G(e);
    e->p0n = 0;
    e->p0gi = e->gi; e->p0font = e->font; e->p0code = g ? g->code : PX_NO_CODE;
    if (!g || !(g->pi > 0 && g->pi < e->font->npals)) return;
    const PxPalette *p = px_glyph_palette(e->font, g);
    uint32_t *n = (uint32_t *)realloc(e->p0, sizeof(uint32_t) * (size_t)p->n);
    if (!n) return;
    e->p0 = n;
    e->p0n = p->n;
    memcpy(e->p0, p->c, sizeof(uint32_t) * (size_t)e->p0n);
}

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

/* Draws the glyph view's bar inside r: one row as tall as the right panel's tabs, so that their hairlines meet; returns its height.
 * The lock on the left, the tools' options, the move and advance controls on the right (the ink is the palette bar's, under the canvas;
 * the neighbours are in the View tab, the layers and kerning have tabs of their own). */
static float draw_toolbar(Editor *e, Rectangle area)
{
    PxGlyph *g = G(e);
    float total = BAR_H, bh = TH_ROW_H;
    DrawRectangle((int)area.x, (int)area.y, (int)area.width, (int)total, TH_PANEL_ALT);
    Th_HLine(area.x, area.x + area.width, area.y + total - 1, TH_LINE);
    float y2 = area.y + (int)((total - 1 - bh) / 2);
    float px = area.x + 2;
    float rx = area.x + area.width - 10;
    bool hasSel = !sel_empty(&e->sel);
    if (g) {
        char adv[40];
        snprintf(adv, sizeof adv, "%d", g->advance);
        Rectangle sr = {rx - TH_STEP_W, y2, TH_STEP_W, bh};
        int st = Th_Stepper(sr, adv, "", locked(e) ? TH_STEP_NO_MINUS | TH_STEP_NO_PLUS : 0);
        if (st) set_advance(e, g->advance + st), close_edit_cmd(e);
        rx = sr.x;
        Vector2 lm = Th_MeasureCap("ADVANCE");
        rx -= lm.x + 8; Th_TextCap("ADVANCE", rx, y2 + (int)((bh - lm.y) / 2), TH_TEXT_DIM);
        rx -= 8; sep(rx, area.y, total); rx -= 10;

        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap down  (Shift+Down)", TH_BTN_ICON(TH_IC_DOWN) | TH_BTN_OFF((g->w > 0 || hasSel) && !locked(e)), hasSel ? "Move the selection down one cell  (Arrow keys)" : "Move bitmap down  (Shift+Down)")) { if (hasSel) sel_nudge(e, 0, 1); else move_bitmap(e, 0, 1); }
        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap up  (Shift+Up)", TH_BTN_ICON(TH_IC_UP) | TH_BTN_OFF((g->w > 0 || hasSel) && !locked(e)), hasSel ? "Move the selection up one cell  (Arrow keys)" : "Move bitmap up  (Shift+Up)")) { if (hasSel) sel_nudge(e, 0, -1); else move_bitmap(e, 0, -1); }
        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap right  (Shift+Right)", TH_BTN_ICON(TH_IC_RIGHT) | TH_BTN_OFF((g->w > 0 || hasSel) && !locked(e)), hasSel ? "Move the selection right one cell  (Arrow keys)" : "Move bitmap right  (Shift+Right)")) { if (hasSel) sel_nudge(e, 1, 0); else move_bitmap(e, 1, 0); }
        rx -= bh; if (Th_Button((Rectangle){rx, y2, bh, bh}, "Move bitmap left  (Shift+Left)", TH_BTN_ICON(TH_IC_LEFT) | TH_BTN_OFF((g->w > 0 || hasSel) && !locked(e)), hasSel ? "Move the selection left one cell  (Arrow keys)" : "Move bitmap left  (Shift+Left)")) { if (hasSel) sel_nudge(e, -1, 0); else move_bitmap(e, -1, 0); }
        lm = Th_MeasureCap("MOVE");
        rx -= lm.x + 8; Th_TextCap("MOVE", rx, y2 + (int)((bh - lm.y) / 2), TH_TEXT_DIM);
    }
    /* at the left: the lock of the font */
    if (Th_Lock(((Rectangle){px + 8, y2, bh, bh}), e->readOnly, e->readOnly ? "The font is locked: unlock it to edit it" : "Lock the font: nothing can change it")) e->result = EDITOR_LOCK;
    /* the tools' own controls, between the lock and the move buttons: Pixel-perfect for the pencil and the shapes, and for the selection tools
     * how a new shape meets the selection and the wand's connectivity (a font with a big palette leaves them no room: the keys work) */
    bool freehand = e->tool == T_PENCIL || e->tool == T_LINE || e->tool == T_CURVE, solid = e->tool == T_RECT || e->tool == T_ELLIPSE || e->tool == T_POLY || e->tool == T_CONTOUR, bucket = e->tool == T_FILL;
    if ((freehand || solid || bucket || is_sel_tool(e->tool)) && g) {
        static const char *const modeLab[4] = {"Set", "Add", "Sub", "And"};
        static const char *const modeHint[4] = {"Replace the selection", "Add to the selection  (Shift)", "Subtract from the selection  (Alt)", "Keep only what is in both  (Shift+Alt)"};
        float sx = px + 8 + bh + 10, segW = 4 * 40, ppW = Th_ButtonW("Pixel-perfect"), tgW = Th_ButtonW("8-way"), fiW = Th_ButtonW("Filled");
        float coW = Th_ButtonW("Contiguous"), need = freehand ? ppW : solid ? fiW : bucket ? coW + 8 + tgW : segW + (e->tool == T_WAND ? tgW + 8 : 0);
        if (sx + 16 + need < rx - 8) {
            sep(sx - 4, area.y, total);
            sx += 6;
            if (freehand) {
                Th_Toggle((Rectangle){sx, y2, ppW, bh}, "Pixel-perfect", &e->pixPerfect, locked(e) ? TH_BTN_DISABLED : 0,
                          "Pixel-perfect: a pencil stroke loses the corner cells that make it two pixels wide.  Lines and curves are always one pixel wide");
            } else if (solid) {
                Th_Toggle((Rectangle){sx, y2, fiW, bh}, "Filled", &e->filled, locked(e) ? TH_BTN_DISABLED : 0, "Filled: the inside is drawn too (even-odd for the polygon and the contour).  Shift makes a square or circle, Ctrl draws from the centre");
            } else if (bucket) {
                Th_Toggle((Rectangle){sx, y2, coW, bh}, "Contiguous", &e->contiguous, locked(e) ? TH_BTN_DISABLED : 0, "Contiguous: only the cells joined to the clicked one.  Off: every cell of that pixel character in the glyph box");
                Th_Toggle((Rectangle){sx + coW + 8, y2, tgW, bh}, "8-way", &e->wandDiag, locked(e) ? TH_BTN_DISABLED : 0, "Also joins diagonal neighbours (shared with the wand)");
            } else {
                ThSeg sg;
                memset(&sg, 0, sizeof sg);
                sg.labels = modeLab; sg.n = 4; sg.sel = (int)e->selMode; sg.prefix = "selmode"; sg.hints = modeHint;
                if (locked(e)) sg.disabled = 15;
                int hit = Th_Segmented((Rectangle){sx, y2, segW, bh}, &sg);
                if (hit >= 0) e->selMode = (SelMode)hit;
                if (e->tool == T_WAND) Th_Toggle((Rectangle){sx + segW + 8, y2, tgW, bh}, "8-way", &e->wandDiag, locked(e) ? TH_BTN_DISABLED : 0, "The wand also joins diagonal neighbours");
            }
        }
    }
    return total;
}

/* The tool strip at the left of the canvas: the buttons of kTool[] by group, one column while they fit r's height, else more.
 * Draws it at r's x, y and height and returns its width. */
#define STRIP_PAD 6
#define STRIP_GAP 2     /* between buttons */
#define STRIP_GROUP 8   /* extra between groups */
static float draw_strip(Editor *e, Rectangle r)
{
    Rectangle pos[T_COUNT];
    float step = TH_ROW_H + STRIP_GAP, y = STRIP_PAD;
    int col = 0, prev = -1;
    for (int i = 0; i < T_COUNT; i++) {
        Tool t = kStrip[i];
        if (kTool[t].hidden) continue;
        bool first = kTool[t].group != prev;   /* a group that does not fit the rest of the column starts the next one (a group is never split while it fits one) */
        int need = 1;
        if (prev >= 0 && first) y += STRIP_GROUP;
        if (first)
            for (int j = i + 1; j < T_COUNT && (kTool[kStrip[j]].hidden || kTool[kStrip[j]].group == kTool[t].group); j++) need += !kTool[kStrip[j]].hidden;
        if (y + need * step - STRIP_GAP + STRIP_PAD > r.height && y > STRIP_PAD) col++, y = STRIP_PAD;
        pos[t] = (Rectangle){r.x + STRIP_PAD + col * step, r.y + y, TH_ROW_H, TH_ROW_H};
        y += step;
        prev = kTool[t].group;
    }
    e->toolCols = col + 1;
    float w = 2 * STRIP_PAD + e->toolCols * step - STRIP_GAP;
    DrawRectangle((int)r.x, (int)r.y, (int)w, (int)r.height, TH_PANEL_ALT);
    Th_VLine(r.x + w - 1, r.y, r.y + r.height, TH_LINE);
    static char tips[T_COUNT][48];
    for (int t = 0; t < T_COUNT; t++) {
        if (kTool[t].hidden) continue;
        char rn[24];
        snprintf(tips[t], sizeof tips[t], "%s  (%s)", kTool[t].name, kTool[t].key);
        snprintf(rn, sizeof rn, "tool.%d", t);
        Th_Reg(rn, pos[t]);
        if (Th_Button(pos[t], tips[t], TH_BTN_ICON(kTool[t].icon) | (e->tool == (Tool)t ? TH_BTN_ACTIVE : 0) | TH_BTN_OFF(!locked(e)), tips[t])) set_tool(e, (Tool)t);
    }
    return w;
}

/* ------------------------------------------------------------------ canvas */

static void plot_preview(Editor *e, int x, int y, char c)
{
    int ox = (int)e->panLast.x, oy = (int)e->panLast.y; /* reused as screen origin during preview */
    if (clipped_out(e, x, y)) return;
    Color col = c == '.' ? fade(TH_DANGER, 110) : fade(pixcol(e, c), 150);
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

/* ------------------------------------------------------------------ drawing: the selection */

/* a 1 px dashed run, 4 px dashes in two colours; the pattern is tied to the screen so that neighbouring cells' edges join up, and `phase` marches it */
static void dash_run(int x, int y, int len, bool horizontal, int phase, Color a, Color b)
{
    int u0 = horizontal ? x : y, u = u0, end = u0 + len;
    while (u < end) {
        int seg = floordiv(u + phase, 4), stop = imin(end, (seg + 1) * 4 - phase);
        Color c = (seg & 1) ? a : b;
        if (horizontal) DrawRectangle(u, y, stop - u, 1, c); else DrawRectangle(x, u, 1, stop - u, c);
        u = stop;
    }
}

/* the marching ants: a dashed outline round the cells of the mask (dark and light dashes), over a faint tint of the cells */
static void draw_ants(const SelMask *m, int ox, int oy, int z, int phase, Color dark, Color tint)
{
    if (sel_empty(m)) return;
    for (int y = m->y; y < m->y + m->h; y++)
        for (int x = m->x; x < m->x + m->w; x++) {
            if (!sel_get(m, x, y)) continue;
            int sx = ox + x * z, sy = oy + y * z;
            if (tint.a) DrawRectangle(sx, sy, z, z, tint);
            if (!sel_get(m, x, y - 1)) dash_run(sx, sy, z, true, phase, dark, TH_PANEL);
            if (!sel_get(m, x, y + 1)) dash_run(sx, sy + z - 1, z, true, phase, dark, TH_PANEL);
            if (!sel_get(m, x - 1, y)) dash_run(sx, sy, z, false, phase, dark, TH_PANEL);
            if (!sel_get(m, x + 1, y)) dash_run(sx + z - 1, sy, z, false, phase, dark, TH_PANEL);
        }
}

/* the current glyph's bitmap; while a move floats, the cells it was lifted from are drawn empty */
static void draw_current(const Editor *e, const PxGlyph *g, int sx, int sy, int scale)
{
    if (e->floating && !e->flCopy && !e->flPaste && g->pix && g->w > 0 && g->h > 0) {
        PxGlyph t = *g;
        t.pix = (char *)malloc((size_t)g->w * g->h);
        if (t.pix) {
            memcpy(t.pix, g->pix, (size_t)g->w * g->h);
            SelBitmap b = bm_of(&t);
            selbm_clear(&b, &e->src);
            draw_bitmap(e->font, &t, sx, sy, scale);
            free(t.pix);
            return;
        }
    }
    draw_bitmap(e->font, g, sx, sy, scale);
}

/* the floating pixels, over the glyph */
static void draw_float(const Editor *e, int sx, int sy, int scale)
{
    if (!e->floating || !e->fl.pix) return;
    const SelMask *s = &e->fl.shape;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            char c = e->fl.pix[(size_t)y * s->w + x];
            int pi = px_pixel_index(c);
            if (pi > 0) DrawRectangle(sx + (s->x + x) * scale, sy + (s->y + y) * scale, scale, scale, Th_FromRGBA(pi - 1 < e->flPalN ? e->flPal[pi - 1] : 0x000000FFu));
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
    bool canPaint = !locked(e) && (e->mode == M_NONE || e->mode == M_PAINT || e->mode == M_SHAPE || e->mode == M_CURVE || e->mode == M_CONTOUR);

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
    int kpi = kern_partner(e), kpx = kpi >= 0 ? ox + kern_pen(e) * z : 0;   /* the partner and its pen on the screen */
    if (kpi >= 0 && f->glyphs[kpi].advance > 0) DrawRectangle(kpx, boxTop, f->glyphs[kpi].advance * z, H * z, fade(TH_PANEL, 170));

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
        if (a->isCurrent) { draw_current(e, g, ox, oy, z); continue; }
        Layers_DrawGlyph(e->layers, i, ox, oy, z);
    }
    if (kpi >= 0) draw_bitmap(f, &f->glyphs[kpi], kpx, oy, z);   /* the partner over the layers: it is the text that follows */
    if (!e->curHidden && g->w > 0 && g->h > 0 && z >= 6) {
        /* faint outline of the bitmap extent */
        Rectangle br = {(float)(ox + g->x * z), (float)(oy + gtop(g) * z), (float)(g->w * z + 1), (float)(g->h * z + 1)};
        DrawRectangleLinesEx(br, 1, fade(TH_ACCENT, 60));
    }

    /* shape preview */
    if (e->mode == M_SHAPE && e->hasHover) {
        char c = e->btn == MOUSE_BUTTON_RIGHT ? '.' : ink_ch(e);
        Vector2 keep = e->panLast;
        e->panLast = (Vector2){(float)ox, (float)oy};
        if (e->tool == T_LINE) { int en[4]; shape_ends(e, e->hx, e->hy, en); plot_line(e, en[0], en[1], en[2], en[3], plot_preview, c); }
        else plot_shape(e, e->hx, e->hy, plot_preview, c);
        e->panLast = keep;
    }

    /* the polygon being made: its edges and the line that follows the mouse (open: the closing edge is drawn on the close); the contour: its closed path */
    if (!e->curHidden && (e->polyN > 0 || e->mode == M_CONTOUR)) {
        Vector2 keep = e->panLast;
        RasPath r = {0};
        e->panLast = (Vector2){(float)ox, (float)oy};
        if (e->mode == M_CONTOUR) {
            if (cont_cells(e, &r)) for (int i = 0; i < r.n; i++) plot_preview(e, r.p[i].x, r.p[i].y, e->btn == MOUSE_BUTTON_RIGHT ? '.' : ink_ch(e));
        } else {
            RasPt *v = (RasPt *)malloc(sizeof(RasPt) * (size_t)(e->polyN + 1));
            int n = e->polyN;
            if (v) {
                memcpy(v, e->poly, sizeof(RasPt) * (size_t)n);
                if (e->hasHover) v[n++] = poly_point(e, e->hx, e->hy);
                if (ras_polygon(&r, v, n, false, false)) for (int i = 0; i < r.n; i++) plot_preview(e, r.p[i].x, r.p[i].y, e->polyBtn == MOUSE_BUTTON_RIGHT ? '.' : ink_ch(e));
                free(v);
            }
        }
        ras_free(&r);
        e->panLast = keep;
    }

    /* the curve being made: its cells and its handles */
    if (!e->curHidden && (e->curveStage > 0 || e->mode == M_CURVE)) {
        char c = e->curveBtn == MOUSE_BUTTON_RIGHT ? '.' : ink_ch(e);
        Vector2 keep = e->panLast;
        e->panLast = (Vector2){(float)ox, (float)oy};
        plot_curve(e, plot_preview, c);
        e->panLast = keep;
        Vector2 h[4];
        for (int i = 0; i < 4; i++) h[i] = (Vector2){(float)(ox + e->cv4[i].x * z + z / 2), (float)(oy + e->cv4[i].y * z + z / 2)};
        DrawLineEx(h[0], h[1], 1, fade(TH_ACCENT, 200));
        DrawLineEx(h[3], h[2], 1, fade(TH_ACCENT, 200));
        for (int i = 0; i < 4; i++) {
            bool endp = i == 0 || i == 3;
            float rr = z >= 8 ? 5.0f : 4.0f;
            if (endp) DrawRectangle((int)(h[i].x - rr), (int)(h[i].y - rr), (int)(2 * rr), (int)(2 * rr), TH_ACCENT);
            else DrawCircleV(h[i], rr, TH_ACCENT);
            if (endp) DrawRectangleLines((int)(h[i].x - rr), (int)(h[i].y - rr), (int)(2 * rr), (int)(2 * rr), TH_PANEL);
            else DrawCircleLines((int)h[i].x, (int)h[i].y, rr, TH_PANEL);
        }
    }

    /* the floating pixels, the selection and the shape being dragged */
    if (!e->curHidden) {
        int phase = (int)(GetTime() * 10.0) % 8;
        draw_float(e, ox, oy, z);
        draw_ants(&e->sel, ox, oy, z, phase, TH_TEXT, fade(TH_ACCENT, 34));
        if (e->mode == M_SELBOX || e->mode == M_SELLASSO) {
            SelMask pv;
            memset(&pv, 0, sizeof pv);
            if (e->mode == M_SELBOX) { int x0, y0, x1, y1; box_cells(e, &x0, &y0, &x1, &y1); sel_set_rect(&pv, x0, y0, x1, y1); }
            else if (e->lassoN > 0) sel_polygon(&pv, e->lasso, e->lassoN);
            draw_ants(&pv, ox, oy, z, phase, TH_ACCENT, fade(TH_ACCENT, 26));
            sel_free(&pv);
        }
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
                                            mouse.y >= cv.y && mouse.y < cv.y + cv.height && fabsf(mouse.x - advX) <= 4));
    Th_DashedVLine((float)advX, cv.y, cv.y + cv.height, 4, TH_M_ADVANCE);
    if (nearAdv) {
        DrawRectangle(advX - 1, (int)cv.y, 3, (int)cv.height, fade(TH_M_ADVANCE, 70));
    }
    Th_TextSmall("Pen", (float)ox + 4, cv.y + cv.height - 16, TH_M_BOX);
    Th_TextSmall("Advance", (float)advX + 4, cv.y + cv.height - 16, TH_M_ADVANCE);
    if (kpi >= 0) {   /* the partner's pen, with the pair's kerning */
        bool on = e->mode == M_KERN || (e->mode == M_NONE && e->hasHover && kern_hit(e, e->hx, e->hy));
        Th_DashedVLine((float)kpx, cv.y, cv.y + cv.height, 4, on ? TH_ACCENT : fade(TH_ACCENT, 150));
        Th_TextSmall(TextFormat("Kerning %d", kern_value(e)), (float)kpx + 4, cv.y + cv.height - 32, TH_ACCENT);
    }

    /* hover cell */
    if (e->hasHover && canPaint && !nearAdv && !(e->mode == M_NONE && kern_hit(e, e->hx, e->hy))) {
        Rectangle hr = {(float)(ox + e->hx * z), (float)(oy + e->hy * z), (float)z + 1, (float)z + 1};
        if (e->tool != T_FILL && e->tool != T_PICK && !is_sel_tool(e->tool) && e->mode == M_NONE) {
            char c = e->tool == T_ERASER ? '.' : ink_ch(e);
            if (c != '.') DrawRectangle((int)hr.x, (int)hr.y, z, z, fade(pixcol(e, c), 70));
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

/* The eyedropper: the colour of the pixel under the cell becomes the ink.  Empty space (and a cell outside the bitmap) is '.', the eraser ink (the last swatch).
 * There is one ink: the right button picks the same way. */
static void pick_at(Editor *e, int cx, int cy)
{
    PxGlyph *g = G(e);
    char c = g ? gget(g, cx, cy) : '.';
    if (c == '.' || px_pixel_index(c) <= 0) { set_ink(e, -1, 0); return; }
    set_ink(e, 0, px_glyph_color(e->font, g, c));
}

/* the tools that Alt turns into the eyedropper while it is held at the press */
static bool alt_picks(Tool t) { return t == T_PENCIL || t == T_LINE || t == T_RECT || t == T_ELLIPSE || t == T_CURVE || t == T_POLY || t == T_CONTOUR || t == T_FILL; }

static void canvas_input(Editor *e, Rectangle cv, Vector2 mouse)
{
    PxGlyph *g = G(e);
    int z = e->zoom;
    int ox = (int)cv.x + e->vx, oy = (int)cv.y + e->vy;
    bool inside = CheckCollisionPointRec(mouse, cv) && !e->barDrag;
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
            if (fabsf(mouse.x - advX) <= 4 || kern_hit(e, cx, cy)) SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
            else if (e->tool == T_MOVE || (is_sel_tool(e->tool) && e->tool != T_WAND && (sel_get(&e->sel, cx, cy) || (e->floating && sel_get(&e->fl.shape, cx, cy)))))
                SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
            else SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
        }
        if (inside && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            e->mode = M_PAN; e->panLast = mouse;
        } else if (locked(e)) {
            /* a viewer (or the glyph is hidden): nothing to paint */
        } else if (inside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && fabsf(mouse.x - advX) <= 4) {
            e->mode = M_ADVANCE; e->btn = MOUSE_BUTTON_LEFT;
        } else if (inside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && kern_hit(e, cx, cy)) {
            float_drop(e);
            e->mode = M_KERN; e->btn = MOUSE_BUTTON_LEFT; e->kernStart = kern_value(e); e->kernCx = cx;
        } else if (inside && is_sel_tool(e->tool) && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) sel_press(e, cx, cy, mouse);
        } else if (inside && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            int b = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ? MOUSE_BUTTON_LEFT : MOUSE_BUTTON_RIGHT;
            e->btn = b;
            float_drop(e);   /* pixels floating from an arrow key land first: the drawing is clipped to the selection that stays */
            char c = (b == MOUSE_BUTTON_RIGHT || e->tool == T_ERASER) ? '.' : ink_ch(e);
            e->sx = e->lx = cx; e->sy = e->ly = cy;
            if (key_alt() && alt_picks(e->tool)) {   /* Alt turns a drawing tool into the eyedropper for this click */
                pick_at(e, cx, cy);
            } else if (e->tool == T_PICK) {
                pick_at(e, cx, cy);
            } else if (e->tool == T_CURVE) {
                if (e->curveStage == 0 || b == e->curveBtn) curve_press(e, cx, cy, b);
            } else if (e->tool == T_POLY) {
                poly_press(e, cx, cy, b);
            } else if (e->tool == T_CONTOUR) {
                cont_press(e, cx, cy, b);
            } else if (e->tool == T_FILL) {
                fill_at(e, cx, cy, c);
                close_edit_cmd(e);
            } else if (e->tool == T_LINE || e->tool == T_RECT || e->tool == T_ELLIPSE) {
                e->mode = M_SHAPE;
            } else {
                e->mode = M_PAINT;
                stroke_begin(e);
                if (e->tool == T_PENCIL && key_shift() && e->hasLast) {   /* Shift+click: a line from where the pencil last ended */
                    stroke_push(e, e->lastX, e->lastY, gget(G(e), e->lastX, e->lastY));   /* the start is where the line begins, not painted again */
                    stroke_to(e, e->lastX, e->lastY, cx, cy, c);
                } else stroke_cell(e, cx, cy, c);
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
    if (e->mode == M_KERN) {   /* the partner follows the cursor cell by cell: the kerning changes, one undo step a drag */
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
        set_kern(e, e->kernStart + cx - e->kernCx);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { e->mode = M_NONE; e->editOpen = false; }
        return;
    }
    if (e->mode == M_SELBOX || e->mode == M_SELLASSO || e->mode == M_SELMOVE) { sel_drag(e, cx, cy, mouse); return; }
    SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);
    if (e->mode == M_CURVE) { curve_drag(e, cx, cy); return; }
    if (e->mode == M_CONTOUR) { cont_drag(e, cx, cy); return; }
    char c = (e->btn == MOUSE_BUTTON_RIGHT || e->tool == T_ERASER) ? '.' : ink_ch(e);
    if (e->mode == M_PAINT) {
        if (cx != e->lx || cy != e->ly) {
            stroke_to(e, e->lx, e->ly, cx, cy, c);
            e->lx = cx; e->ly = cy;
        }
        if (!IsMouseButtonDown(e->btn)) { e->mode = M_NONE; stroke_end(e); }
    } else if (e->mode == M_SHAPE) {
        if (IsKeyPressed(KEY_ESCAPE)) { e->mode = M_NONE; return; }
        if (!IsMouseButtonDown(e->btn)) {
            if (e->tool == T_LINE) {
                int en[4];
                shape_ends(e, cx, cy, en);
                plot_line(e, en[0], en[1], en[2], en[3], put, c);
                e->hasLast = true; e->lastX = en[2]; e->lastY = en[3];
            } else plot_shape(e, cx, cy, put, c);
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
    e->contiguous = true;
    e->inkColor = 0x000000FFu;
    e->zoom = 8;
    e->needFit = true;
    e->showMetrics = true;
    e->layers = Layers_Create();
    e->kernCode = PX_NO_CODE;
    return e;
}

void Editor_Destroy(Editor *e)
{
    if (!e) return;
    Layers_Destroy(e->layers);
    float_cancel(e);
    sel_free(&e->sel);
    free(e->lasso);
    free(e->stk);
    free(e->stkPrev);
    free(e->poly);
    free(e->p0);
    ras_free(&e->cont);
    free(e);
}

struct Layers *Editor_Layers(Editor *e) { return e->layers; }
bool Editor_ShowMetrics(const Editor *e) { return e->showMetrics; }
bool Editor_TakeKernRequest(Editor *e) { bool r = e->kernReq; e->kernReq = false; return r; }
void Editor_SetKerning(Editor *e, bool on) { kern_set_on(e, on); }
bool Editor_CurrentHidden(const Editor *e) { return e->curHidden; }
const char *Editor_OriginalHash(const Editor *e) { return e->hasOrig ? e->origHash : NULL; }

void Editor_Open(Editor *e, PxFont *font, int glyphIndex, EditorBeforeChange beforeChange, void *user)
{
    float_cancel(e);           /* pixels still floating belong to what was there before (an undo, another glyph) */
    curve_cancel(e);
    poly_cancel(e);
    ras_free(&e->cont);
    e->hasLast = false;
    e->stkN = 0;
    sel_free(&e->sel);
    e->implicitSel = false;
    e->font = font;
    e->gi = glyphIndex;
    e->before = beforeChange;
    e->user = user;
    e->editOpen = false;
    e->mode = M_NONE;
    e->needFit = true;
    e->userZoom = false;
    e->result = EDITOR_RUNNING;
    if (!e->inkErase && px_font_color_index(font, e->inkColor) < 0) ink_pick_index(e, 0);   /* an ink the font has not: its first colour */
    e->barScroll = 0;
    e->pressKind = 0; e->barDrag = false;
    e->nanim = 0; e->nshown = 0;
    box_update(e);
    /* the palette the glyph has now, for the unused-slot rule; not again when the same glyph is opened again (an undo, a redo) */
    const PxGlyph *g = (glyphIndex >= 0 && glyphIndex < font->count) ? &font->glyphs[glyphIndex] : NULL;
    if (!(e->p0font == font && e->p0gi == glyphIndex && g && e->p0code == g->code)) remember_p0(e);
}

void Editor_SetReadOnly(Editor *e, bool readOnly)
{
    e->readOnly = readOnly;
    if (readOnly) { e->mode = M_NONE; e->pressKind = 0; e->barDrag = false; curve_cancel(e); poly_cancel(e); ras_free(&e->cont); float_cancel(e); sel_free(&e->sel); e->implicitSel = false; }
}

void Editor_SetOriginalHash(Editor *e, const char *hash)
{
    e->hasOrig = hash != NULL;
    snprintf(e->origHash, sizeof e->origHash, "%s", hash ? hash : "");
}

int Editor_GlyphIndex(const Editor *e) { return e->gi; }
int Editor_GotoIndex(const Editor *e) { return e->gotoIndex; }
void Editor_SetGlyphIndex(Editor *e, int glyphIndex) { e->gi = glyphIndex; }

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
int  Editor_ToolCols(const Editor *e) { return e->toolCols; }
int  Editor_StripTool(int slot)
{
    for (int i = 0, n = 0; i < T_COUNT; i++)
        if (!kTool[kStrip[i]].hidden && n++ == slot) return (int)kStrip[i];
    return -1;
}
char Editor_Ink(const Editor *e)
{
    if (e->inkErase) return '.';
    const uint32_t *sl;
    int ns = bar_slots(e, &sl);
    for (int i = 0; i < ns; i++) if (sl[i] == e->inkColor) return PX_PIXELS[1 + i];
    return '?';
}
int  Editor_BarRows(const Editor *e) { return e->barRows; }
int  Editor_BarExtras(const Editor *e) { return e->barExtras; }
uint32_t Editor_InkColor(const Editor *e) { return e->inkErase ? 0 : e->inkColor; }
int  Editor_Zoom(const Editor *e) { return e->zoom; }
bool Editor_Kerning(const Editor *e) { return e->kernOn; }
PxCode Editor_KernPartner(const Editor *e) { return e->kernOn ? e->kernCode : PX_NO_CODE; }
void Editor_SetKernPartner(Editor *e, PxCode code) { kern_set_partner(e, code); }

bool Editor_TakeChanged(Editor *e)
{
    bool c = e->changed;
    e->changed = false;
    return c;
}

static EditorResult editor_frame(Editor *e, Rectangle area)
{
    PxGlyph *g = G(e);
    if (!g) return EDITOR_CLOSE;
    box_update(e);
    Layers_SetContext(e->layers, e->font, g->code);
    e->curHidden = !Layers_CurrentVisible(e->layers);
    e->result = e->navReq;   /* the View tab's neighbours, clicked in the panel after the last frame */
    e->navReq = EDITOR_RUNNING;
    e->tip = NULL;
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    Vector2 mouse = GetMousePosition();
    bool blocked = Th_InputBlocked();
    if (blocked) mouse = (Vector2){-10000, -10000};
    bool typing = blocked || Th_TextIs(NULL);   /* the keyboard is a field's (main.c leaves the glyph view's own fields, kern.*, unblocked) */

    area.x = floorf(area.x); area.y = floorf(area.y);
    area.width = floorf(area.width); area.height = floorf(area.height);
    DrawRectangleRec(area, TH_BG);

    /* keyboard */
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    pick_answer(e);
    /* the slot keys: a character typed with no modifier while no text field has the keyboard ('blocked' covers a field that has it):
     * '.', '0'-'9' and 'A'-'Z' pick that slot of the palette bar.  The characters are read whatever happens (a drag) so that none is left over. */
    if (!typing) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (!ctrl && !shift && e->mode == M_NONE) key_slot(e, ch);
    }
    if (!ctrl && e->mode == M_NONE && !typing) {
        if (shift && IsKeyPressed(KEY_K)) e->kernReq = true;   /* the Kerning tab, also in a locked font (a viewer): main.c shows it or leaves it */
        if (!locked(e) && shift) {   /* the tools: Shift and their letter */
            for (int t = 0; t < T_COUNT; t++)
                if (kTool[t].keycode && IsKeyPressed(kTool[t].keycode)) set_tool(e, (Tool)t);
        }
        /* arrows: with a selection they move its pixels (Shift: NUDGE_BIG cells); without one Shift+arrow moves the whole bitmap by a cell */
        bool selected = !sel_empty(&e->sel);
        if (selected || shift) {
            int step = selected ? (shift ? NUDGE_BIG : 1) : 1;
            int ax = 0, ay = 0;
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) ax = -step;
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) ax = step;
            if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) ay = -step;
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) ay = step;
            if (ax || ay) { if (selected) sel_nudge(e, ax, ay); else move_bitmap(e, ax, ay); }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) { curve_commit(e); poly_commit(e); float_drop(e); }
        if (e->polyN > 0 && IsKeyPressed(KEY_BACKSPACE)) poly_back(e);   /* a polygon being made takes its last vertex back */
        else if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) sel_delete(e);
        if (IsKeyPressed(KEY_PAGE_UP) && e->gi > 0) e->result = EDITOR_PREV;
        if (IsKeyPressed(KEY_PAGE_DOWN) && e->gi + 1 < e->font->count) e->result = EDITOR_NEXT;
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (e->floating) float_cancel(e);
            else if (e->curveStage) curve_cancel(e);
            else if (e->polyN > 0) poly_cancel(e);
            else if (!sel_empty(&e->sel)) sel_none(e);
            else e->result = EDITOR_CLOSE;
        }
    }
    /* the selection commands (the real Ctrl: Alt is the subtract modifier) */
    if (key_ctrl() && !key_alt() && e->mode == M_NONE && !typing) {
        if (IsKeyPressed(KEY_A) && !shift) sel_everything(e);
        if (IsKeyPressed(KEY_D) && !shift && !locked(e)) sel_none(e);
        if (IsKeyPressed(KEY_I) && shift) sel_inverse(e);
        if (IsKeyPressed(KEY_C) && !locked(e)) clip_copy(e);
        if (IsKeyPressed(KEY_X)) clip_cut(e);
        if (IsKeyPressed(KEY_V)) clip_paste(e);
        /* Ctrl+Left / Ctrl+Right: the kerning of the pair shown, one cell a press */
        int kd = (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) ? -1 : (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) ? 1 : 0;
        if (kd && !shift && kern_partner(e) >= 0) { set_kern(e, kern_value(e) + kd); close_edit_cmd(e); }
    }

    /* layout */
    float tbH = draw_toolbar(e, area);
    Rectangle st = {area.x, area.y + area.height - STATUS_H, area.width, STATUS_H};
    float barH = bar_height(e, area.width);
    Rectangle br = {area.x, st.y - barH, area.width, barH};
    float sw = draw_strip(e, (Rectangle){area.x, area.y + tbH, 0, br.y - (area.y + tbH)});
    Rectangle cv = {area.x + sw + 12, area.y + tbH + 12, area.width - sw - 24, br.y - (area.y + tbH) - 12 - 4};
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
    /* the glyph is canonical after every edit: its pixels and palette in reading order, the slots follow */
    if (e->mode == M_NONE && !locked(e) && !g->lazy && px_glyph_canonicalize(e->font, g))
        px_font_compact_palettes(e->font);          /* the palette the stroke made, now renamed, goes from the table */
    draw_canvas(e, cv, mouse);
    draw_bar(e, br);

    /* status line: the same strip as the application's (Th_StatusBar) */
    {
        char pos[32], tool[360], cur[33], ink[48], kern[112] = "";
        snprintf(pos, sizeof pos, e->hasHover ? "x %d   y %d" : "x -   y -", e->hx, -1 - e->hy);
        char key = Editor_Ink(e), hx[12];
        if (e->inkErase) snprintf(ink, sizeof ink, ". (transparent)");
        else { hex_of(e->inkColor, hx); if (key != '?') snprintf(ink, sizeof ink, "%s (key %c)", hx, key); else snprintf(ink, sizeof ink, "%s (not in this glyph)", hx); }
        if (kern_partner(e) >= 0) { char a[40], b[40]; kern_label(g->code, a, sizeof a); kern_label(e->kernCode, b, sizeof b); snprintf(kern, sizeof kern, "   kerning %s %s %d", a, b, kern_value(e)); }
        snprintf(tool, sizeof tool, "%s   ink %s   zoom %dx   bitmap %s%s%s", e->readOnly ? "View" : kTool[e->tool].name, ink, e->zoom,
                 g->w > 0 ? TextFormat("%dx%d at %d,%d", g->w, g->h, g->x, g->y) : "empty",
                 sel_empty(&e->sel) ? "" : TextFormat("   %s %dx%d (%d cells)", e->floating ? "floating" : "selection", e->sel.w, e->sel.h, sel_count(&e->sel)), kern);
        px_glyph_hash(g, cur);
        bool differs = e->hasOrig && strcmp(cur, e->origHash) != 0;
        ThPart l[2] = {{pos, e->hasHover ? TH_TEXT : TH_TEXT_DIM}, {tool}};
        ThPart r[3] = {{NULL, {0}, 0, 0, "Glyph", e->origHash}, {"->"}, {NULL, {0}, 0, TH_HASH_ACCENT, NULL, cur}};
        if (differs) Th_StatusBar(st, 90, l, 2, r, 3);
        else Th_StatusBar(st, 90, l, 2, &(ThPart){NULL, {0}, 0, 0, "Glyph", cur}, 1);
    }



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

/* A frame; whatever leaves the glyph (Back, the neighbours, the padlock) first drops floating pixels onto it. */
EditorResult Editor_UpdateDraw(Editor *e, Rectangle area)
{
    EditorResult r = editor_frame(e, area);
    if (r != EDITOR_RUNNING) { curve_commit(e); poly_commit(e); float_drop(e); leave_glyph(e); }
    return r;
}

void Editor_Flush(Editor *e) { if (e) { curve_commit(e); poly_commit(e); float_drop(e); leave_glyph(e); } }

bool Editor_CancelFloat(Editor *e)
{
    if (!e) return false;
    if (e->curveStage) { curve_cancel(e); return true; }   /* Ctrl+Z of a curve being made throws it away */
    if (e->polyN > 0) { poly_cancel(e); return true; }   /* Ctrl+Z of a polygon being made throws it away */
    if (!e->floating) return false;
    float_cancel(e);
    return true;
}

bool Editor_Filled(const Editor *e) { return e->filled; }
void Editor_SetFilled(Editor *e, bool on) { e->filled = on; }
bool Editor_PixelPerfect(const Editor *e) { return e->pixPerfect; }
void Editor_SetPixelPerfect(Editor *e, bool on) { e->pixPerfect = on; }
int  Editor_CurveStage(const Editor *e) { return e->curveStage; }
int  Editor_PolyVertices(const Editor *e) { return e->polyN; }
bool Editor_Contiguous(const Editor *e) { return e->contiguous; }

bool Editor_Floating(const Editor *e) { return e->floating; }
int  Editor_SelCount(const Editor *e) { return sel_count(&e->sel); }

bool Editor_SelBounds(const Editor *e, int *x, int *boxRow, int *w, int *h)
{
    if (sel_empty(&e->sel)) return false;
    *x = e->sel.x; *boxRow = e->sel.y + e->above; *w = e->sel.w; *h = e->sel.h;
    return true;
}

bool Editor_SelCell(const Editor *e, int x, int boxRow) { return sel_get(&e->sel, x, boxRow - e->above); }
int  Editor_ClipCount(void) { return sel_count(&g_clip.shape); }

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
    e->contiguous = true;
    fill_at(e, 20, 3, '0');
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


/* The View tab of the glyph view's panel: the glyph with its neighbours, the zoom (Fit and a stepper), the metric lines and the checker under the glyph. */
void Editor_ViewPanel(Editor *e, float x, float *y, float w)
{
    {   /* the glyph: [<] field [>], the arrows step to the neighbours (PgUp, PgDn), the field takes a character or a code and opens that glyph */
        const PxGlyph *g = G(e);
        char lab[48] = "";
        if (g) kern_label(g->code, lab, sizeof lab);
        Rectangle gr = Th_Row(x, y, w, "Glyph", NULL, 0);
        Rectangle sr = {gr.x + gr.width - 128, gr.y, 128, TH_ROW_H};
        bool editing = Th_TextIs("eglyph.value");
        int st = Th_Stepper(sr, NULL, "eglyph", TH_STEP_ARROWS | (editing ? TH_STEP_EDIT : (e->gi > 0 ? 0 : TH_STEP_NO_MINUS) | (e->font && e->gi + 1 < e->font->count ? 0 : TH_STEP_NO_PLUS)));
        if (st < 0 && e->gi > 0) e->navReq = EDITOR_PREV;
        if (st > 0 && e->font && e->gi + 1 < e->font->count) e->navReq = EDITOR_NEXT;
        int tf = Th_TextField(Th_StepperValue(sr), "eglyph.value", lab, 40, TH_TF_BARE | TH_TF_COMMIT | TH_TF_SELECT, NULL);
        if (tf & (TH_TF_ENTER | TH_TF_BLUR)) {
            PxCode c;
            int gi;
            if (!strcmp(Th_TextValue(), lab)) { /* untouched */ }
            else if (!kern_parse(Th_TextValue(), &c)) Th_Toast(TH_TOAST_ERR, "Type one character, or a code such as U+00E9");
            else if ((gi = px_font_find_code(e->font, c)) < 0) Th_Toast(TH_TOAST_ERR, "The font has no glyph for %s", Th_TextValue());
            else if (gi != e->gi) { e->gotoIndex = gi; e->navReq = EDITOR_GOTO; }
        }
    }
    char zt[12];
    snprintf(zt, sizeof zt, "%dx", e->zoom);
    Rectangle zr = Th_Row(x, y, w, "Zoom", NULL, 0);
    int zs = Th_Stepper((Rectangle){zr.x, zr.y, TH_STEP_W, TH_ROW_H}, zt, "ezoom", (e->zoom <= 1 ? TH_STEP_NO_MINUS : 0) | (e->zoom >= 64 ? TH_STEP_NO_PLUS : 0));
    if (zs) zoom_by(e, zs);
    Rectangle fr = {zr.x + zr.width - Th_ButtonW("Fit"), zr.y, Th_ButtonW("Fit"), TH_ROW_H};
    Th_Reg("ezoom.fit", fr);
    if (Th_Button(fr, "Fit", 0, "Fit the glyph in the canvas")) { e->needFit = true; e->userZoom = false; }
    Th_Switch((Rectangle){x, *y, w, TH_ROW_H}, "Metrics", &e->showMetrics, TH_CK_RIGHT, "Metrics");
    *y += TH_ROW_H + 2;
    Th_Switch((Rectangle){x, *y, w, TH_ROW_H}, "Checker", &e->checker, TH_CK_RIGHT, "Checker");
    *y += TH_ROW_H + 2;
}

/* The Kerning tab of the glyph view's panel (see DESIGN.md, "Kerning"): the partner drawn after the edited glyph (a field and the arrows that step
 * through the glyph's pairs), the kerning of the pair, and the glyph's pairs, a click on one makes it the partner. */
void Editor_KernPanel(Editor *e, float x, float *y, float w)
{
    PxGlyph *g = G(e);
    if (!g) return;
    float bh = TH_ROW_H;
    Rectangle ar = Th_Row(x, y, w, "After", NULL, 0);
    bool pairs = kern_neighbour(e, 1) != PX_NO_CODE;
    Rectangle pr = {ar.x, ar.y, bh, bh}, nr = {ar.x + ar.width - bh, ar.y, bh, bh};
    Th_Reg("kern.prev", pr);
    if (Th_Button(pr, "Previous kerning pair", TH_BTN_ICON(TH_IC_PREV) | TH_BTN_OFF(pairs), "The previous glyph this one has a kerning pair with")) kern_set_partner(e, kern_neighbour(e, -1));
    Th_Reg("kern.next", nr);
    if (Th_Button(nr, "Next kerning pair", TH_BTN_ICON(TH_IC_NEXT) | TH_BTN_OFF(pairs), "The next glyph this one has a kerning pair with")) kern_set_partner(e, kern_neighbour(e, 1));
    char lab[48];
    kern_label(e->kernCode, lab, sizeof lab);
    int tf = Th_TextField((Rectangle){pr.x + bh + 4, ar.y, nr.x - 4 - (pr.x + bh + 4), bh}, "kern.after", lab, 40, TH_TF_COMMIT | TH_TF_SELECT, "glyph");
    if (tf & (TH_TF_ENTER | TH_TF_BLUR)) {
        PxCode c;
        int pi;
        if (!strcmp(Th_TextValue(), lab)) { /* untouched */ }
        else if (!kern_parse(Th_TextValue(), &c)) Th_Toast(TH_TOAST_ERR, "Type one character, or a code such as U+00E9");
        else if ((pi = px_font_find_code(e->font, c)) < 0) Th_Toast(TH_TOAST_ERR, "The font has no glyph for %s", Th_TextValue());
        else kern_set_partner(e, e->font->glyphs[pi].code);
    }
    Rectangle kr = Th_Row(x, y, w, "Kerning", NULL, 0);
    int nv;
    bool can = kern_partner(e) >= 0 && !locked(e);
    if (Th_Number((Rectangle){kr.x + kr.width - 100, kr.y, 100, bh}, "kern", kern_value(e), -KERN_MAX, KERN_MAX, TH_NUM_WHEEL | (can ? 0 : TH_TF_DISABLED), &nv)) { set_kern(e, nv); close_edit_cmd(e); }
    Th_TextSmall("Ctrl+Left / Ctrl+Right, or drag the second glyph", x, *y, TH_TEXT_DIM);
    *y += 22 + TH_GAP;

    /* the glyph's pairs, in code order */
    PxCode *pc = (PxCode *)malloc(sizeof(PxCode) * (size_t)(e->font->kernCount > 0 ? e->font->kernCount : 1));
    int n = 0;
    for (int i = 0; pc && i < e->font->kernCount; i++) {
        const PxKern *k = &e->font->kern[i];
        if (k->first != g->code || k->value == 0 || px_font_find_code(e->font, k->second) < 0) continue;
        int j = n++;
        while (j > 0 && pc[j - 1] > k->second) { pc[j] = pc[j - 1]; j--; }
        pc[j] = k->second;
    }
    PxCode pick = PX_NO_CODE;
    float ty = (bh - Th_Measure("A").y) / 2;
    for (int i = 0; i < n; i++) {
        Rectangle rr = {x, *y, w, bh};   /* on the panel's margins */
        bool hv = !Th_MouseBlocked() && CheckCollisionPointRec(GetMousePosition(), rr);
        Th_ListRow(rr, (pc[i] == e->kernCode ? TH_LR_SELECTED : 0) | (hv ? TH_LR_HOVER : 0));
        char a[48], rn[24];
        kern_label(pc[i], a, sizeof a);
        Th_Text(a, x + 8, rr.y + (int)ty, TH_TEXT);
        const char *v = TextFormat("%d", px_font_kern(e->font, g->code, pc[i]));
        Th_Text(v, x + w - 8 - Th_Measure(v).x, rr.y + (int)ty, TH_TEXT_DIM);
        snprintf(rn, sizeof rn, "kern.pair.%d", i);
        Th_Reg(rn, rr);
        if (hv && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) pick = pc[i];
        *y += bh + 2;
    }
    free(pc);
    if (pick != PX_NO_CODE) kern_set_partner(e, pick);
    if (n == 0) { Th_TextSmall("This glyph has no kerning pairs yet", x, *y, TH_TEXT_DIM); *y += 22; }
}
