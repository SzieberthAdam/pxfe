/* editor.h - the glyph editing view of pxfe (see DESIGN.md, "Glyph view"). */
#ifndef EDITOR_H
#define EDITOR_H

#include "raylib.h"
#include "pxfont.h"

/* EDITOR_LOCK: the user clicked the padlock (main.c runs the same action as the font view's). */
typedef enum { EDITOR_RUNNING = 0, EDITOR_CLOSE, EDITOR_PREV, EDITOR_NEXT, EDITOR_LOCK } EditorResult;

typedef struct Editor Editor; /* opaque, defined in editor.c */

/* Called by main.c before any font edit the editor makes, so the app can push an undo snapshot.
 * The editor calls it once at the start of each stroke / command (not per pixel). */
typedef void (*EditorBeforeChange)(void *user);

Editor *Editor_Create(void);
void    Editor_Destroy(Editor *e);

/* Start editing font->glyphs[glyphIndex]. The editor edits the font in place. */
void    Editor_Open(Editor *e, PxFont *font, int glyphIndex, EditorBeforeChange beforeChange, void *user);
int     Editor_GlyphIndex(const Editor *e);
/* The glyph being edited moved inside the font (its code changed, the glyphs were sorted again): it stays the same glyph, the view stays as it is. */
void    Editor_SetGlyphIndex(Editor *e, int glyphIndex);

/* Per frame, between BeginDrawing/EndDrawing, inside `area` (the editor owns all input inside it,
 * plus the editor keyboard shortcuts while it is open). Returns what main.c should do next:
 * EDITOR_CLOSE -> back to the grid; EDITOR_PREV/NEXT -> main.c reopens on the neighbouring glyph. */
EditorResult Editor_UpdateDraw(Editor *e, Rectangle area);

/* Test hooks for scripted input (--play): screen centre of the cell (x, y), x from the pen and y a row of the font
 * box (0 = its top row), screen position of the
 * advance line handle, and the current tool index (0 pencil, 1 eraser, 2 line, 3 rect, 4 fill, 5 marquee, 6 lasso, 7 wand, 8 move, 9 curve), ink and zoom. */
bool    Editor_CellCenter(const Editor *e, int x, int y, Vector2 *out);
bool    Editor_AdvanceHandle(const Editor *e, Vector2 *out);
int     Editor_Tool(const Editor *e);
int     Editor_ToolCols(const Editor *e);   /* columns of the tool strip */
int     Editor_StripTool(int slot);          /* test hook: the Tool number of the strip's button at slot (0 first, in the strip's order), -1 past the last */
/* The ink: '.' the transparent ink, else its slot in the edited glyph's palette as a pixel character (its key in the palette bar: '0', ..., 'Z'),
 * '?' when the glyph has not got the colour; Editor_InkColor its colour (0xRRGGBBAA, 0 for '.').  The ink is a colour: drawing with one the
 * glyph has not adds it to its palette.  Test hooks of the palette bar: its rows (1 + the rows of the font's other colours) and
 * the number of those colours. */
char    Editor_Ink(const Editor *e);
int     Editor_BarRows(const Editor *e);
int     Editor_BarExtras(const Editor *e);
uint32_t Editor_InkColor(const Editor *e);
int     Editor_Zoom(const Editor *e);

/* A read-only editor is a viewer of a locked font: no tool, advance or move command changes the glyph. */
void    Editor_SetReadOnly(Editor *e, bool readOnly);
/* The pxgl hash (32 lower-case hex, "" = no ink) the glyph had when the font was opened, or NULL when unknown:
 * the status line shows "pxgl <original> -> <current>" once the bitmap changed.  Call after Editor_Open. */
void    Editor_SetOriginalHash(Editor *e, const char *hash);

/* The pxgl hash the glyph had when the font was opened, or NULL when unknown (see Editor_SetOriginalHash). */
const char *Editor_OriginalHash(const Editor *e);

/* The reference layers of the glyph view (layers.h); owned by the editor, they live as long as it (the whole session). */
struct Layers;
struct Layers *Editor_Layers(Editor *e);
/* The metric lines are on (the "Metrics" toggle of the glyph view's bar). */
bool    Editor_ShowMetrics(const Editor *e);
void    Editor_ViewPanel(Editor *e, float x, float *y, float w);   /* the View tab of the panel: zoom, metrics, checker */
/* The bar's Layers toggle: main.c tells it whether the Layers tab is showing and takes the click (true once). */
void    Editor_SetLayersTabActive(Editor *e, bool on);
bool    Editor_TakeLayersRequest(Editor *e);
/* The edited glyph's layer is hidden: the editing tools are disabled. */
bool    Editor_CurrentHidden(const Editor *e);

/* Selection (see DESIGN.md, "Selection and moving").  Pixels lifted by a move or a paste float over the glyph and are written by the drop:
 * Enter, another tool, a press outside, leaving the glyph; main.c calls Editor_Flush before it reads the font (commit, save).
 * Editor_CancelFloat is the Ctrl+Z of a float: it throws the floating pixels away and returns true, or returns false when nothing floats. */
void    Editor_Flush(Editor *e);
bool    Editor_CancelFloat(Editor *e);
/* Test hooks: pixels float; cells selected; the selection box (x from the pen, y a row of the font box as for Editor_CellCenter); one cell selected;
 * cells on the in-app clipboard. */
bool    Editor_Floating(const Editor *e);
int     Editor_SelCount(const Editor *e);
bool    Editor_SelBounds(const Editor *e, int *x, int *boxRow, int *w, int *h);
bool    Editor_SelCell(const Editor *e, int x, int boxRow);
int     Editor_ClipCount(void);

/* Line, curve and pencil (see DESIGN.md, "Tools of the glyph view").  Pixel-perfect is the bar's option of the pencil: main.c keeps it in
 * pxfe.ini.  A curve being made (Curve tool, tool index 9) is drawn by Enter, a tool change, leaving the glyph and Editor_Flush, thrown away by Esc and by
 * Editor_CancelFloat (the Ctrl+Z of main.c).  Test hook: 0 no curve, 1 the end points are set, 2 the first control point too. */
bool    Editor_PixelPerfect(const Editor *e);
void    Editor_SetPixelPerfect(Editor *e, bool on);
int     Editor_CurveStage(const Editor *e);

/* Rectangle and ellipse (see DESIGN.md, "Tools of the glyph view"): the bar's Filled option of both, kept by main.c in pxfe.ini. */
bool    Editor_Filled(const Editor *e);
void    Editor_SetFilled(Editor *e, bool on);

/* Fill, eyedropper, polygon and contour (see DESIGN.md, "Tools of the glyph view").  Test hooks: the vertices of the polygon being made (tool 12:
 * Enter, a click on its last or first vertex, a tool change, leaving the glyph and Editor_Flush draw it, Esc and Editor_CancelFloat throw it away), and the bar's Contiguous. */
int     Editor_PolyVertices(const Editor *e);
bool    Editor_Contiguous(const Editor *e);

/* Set true by the editor whenever it changed the font; main.c clears it after reading. */
bool    Editor_TakeChanged(Editor *e);

#endif
