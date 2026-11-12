/* editor.h - the glyph editing view of pxfe (see DESIGN.md, "Glyph editor"). */
#ifndef EDITOR_H
#define EDITOR_H

#include "raylib.h"
#include "pxfont.h"

/* EDITOR_DUPLICATE: the user clicked "Read-only · Duplicate to Edit" (main.c runs the same action as the font view's). */
typedef enum { EDITOR_RUNNING = 0, EDITOR_CLOSE, EDITOR_PREV, EDITOR_NEXT, EDITOR_DUPLICATE } EditorResult;

typedef struct Editor Editor; /* opaque, defined in editor.c */

/* Called by main.c before any font edit the editor makes, so the app can push an undo snapshot.
 * The editor calls it once at the start of each stroke / command (not per pixel). */
typedef void (*EditorBeforeChange)(void *user);

Editor *Editor_Create(void);
void    Editor_Destroy(Editor *e);

/* Start editing font->glyphs[glyphIndex]. The editor edits the font in place. */
void    Editor_Open(Editor *e, PxFont *font, int glyphIndex, EditorBeforeChange beforeChange, void *user);
int     Editor_GlyphIndex(const Editor *e);

/* Per frame, between BeginDrawing/EndDrawing, inside `area` (the editor owns all input inside it,
 * plus the editor keyboard shortcuts while it is open). Returns what main.c should do next:
 * EDITOR_CLOSE -> back to the grid; EDITOR_PREV/NEXT -> main.c reopens on the neighbouring glyph. */
EditorResult Editor_UpdateDraw(Editor *e, Rectangle area);

/* Test hooks for scripted input (--play): screen centre of the cell (x, y), x from the pen and y a row of the font
 * box (0 = its top row), screen position of the
 * advance line handle, and the current tool index (0 pencil, 1 eraser, 2 line, 3 rect, 4 fill), ink and zoom. */
bool    Editor_CellCenter(const Editor *e, int x, int y, Vector2 *out);
bool    Editor_AdvanceHandle(const Editor *e, Vector2 *out);
int     Editor_Tool(const Editor *e);
char    Editor_Ink(const Editor *e);
int     Editor_Zoom(const Editor *e);

/* A read-only editor is a viewer of an original: no tool, advance or move command changes the glyph. */
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
/* The bar's Layers toggle: main.c tells it whether the Layers tab is showing and takes the click (true once). */
void    Editor_SetLayersTabActive(Editor *e, bool on);
bool    Editor_TakeLayersRequest(Editor *e);
/* The edited glyph's layer is hidden: the editing tools are disabled. */
bool    Editor_CurrentHidden(const Editor *e);

/* Set true by the editor whenever it changed the font; main.c clears it after reading. */
bool    Editor_TakeChanged(Editor *e);

#endif
