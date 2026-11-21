/* layers.h - reference layers of the glyph view (DESIGN.md "Font layers").
 *
 * A layer is a glyph drawn under or over the edited glyph in the glyph view, to compare and to trace.  Layers are a
 * session setting: they are never saved into a font, they stay while the user moves to the previous or next glyph and
 * while another font is opened.  The stack runs from the top layer (index 0) to the bottom one; the edited glyph is one
 * of the entries (the "current glyph" row, isCurrent), it cannot be removed, but it can be hidden and moved.
 *
 * A layer takes its glyph from "this font" (the font being edited, live) or from a font of the library (loaded through
 * the store with only the needed glyph bitmap, cached per font), by a fixed code or "the same code as the edited glyph".
 * It is drawn in one colour at an opacity, shifted by an offset in pixels (x right, y up).
 *
 * The UI: Layers_DrawPanel (the Layers tab of the glyph view's right panel) and Layers_DrawPopover (the "Add Layer"
 * dialog, drawn last, over everything).  Needs raylib and theme.h. */
#ifndef LAYERS_H
#define LAYERS_H

#include "raylib.h"
#include "pxfont.h"
#include "store.h"
#include "flayers.h"

typedef struct Layer {
    bool isCurrent;              /* the edited glyph itself */
    bool visible;
    Color color;                 /* opaque RGB; the alpha is `opacity` */
    int opacity;                 /* 0 .. 100 (percent) */
    int dx, dy;                  /* offset in pixels, y up */
    bool sameCode;               /* follows the code of the edited glyph */
    PxCode code;                /* the fixed code when !sameCode */
    char pxfm[33];               /* the font of the library, "" = this font */
    char fontName[96];           /* "Typeface-name Style" of that font (for the row) */
    FlLayer *fl;                 /* a layer of a file or a font layer of the database (flayers.h); NULL for a plain session layer */
    bool fontLevel;              /* a FONT layer: fl belongs to the FlDoc of the font (saved in the sidecar), else to this entry */
} Layer;

typedef struct Layers Layers;

Layers *Layers_Create(void);
void    Layers_Destroy(Layers *l);

/* Per frame (cheap): the store the other fonts come from (NULL: none) and what the editor shows.  `doc` is the font being
 * edited (this font), `editedCode` the code of the edited glyph. */
void    Layers_SetStore(Layers *l, Store *s);
void    Layers_SetContext(Layers *l, const PxFont *doc, PxCode editedCode);

int          Layers_Count(const Layers *l);
const Layer *Layers_Get(const Layers *l, int i);
int          Layers_CurrentIndex(const Layers *l);
bool         Layers_CurrentVisible(const Layers *l);
/* The glyph a layer shows (NULL: "not in font"); *srcFont gets the font it belongs to.  Loads a bitmap when needed. */
const PxGlyph *Layers_Glyph(Layers *l, int i, const PxFont **srcFont);
/* The selected layer (the one Alt+drag in the canvas moves), -1 none. */
int     Layers_Selected(const Layers *l);
/* Shift a layer by whole pixels (y up); a no-op for the current glyph. */
void    Layers_Nudge(Layers *l, int i, int dx, int dy);

/* Adds a layer on top.  pxfm "" = this font.  sameCode: follow the edited glyph, else `code`.  Returns its index. */
int     Layers_Add(Layers *l, const char *pxfm, const char *fontName, bool sameCode, PxCode code);
void    Layers_Remove(Layers *l, int i);          /* not the current glyph */
void    Layers_Move(Layers *l, int i, int dir);   /* -1 up (towards the top), +1 down */

/* FONT LAYERS (flayers.h).  The font layers of the open font appear in the same stack as the session layers, marked "FONT"; main.c hands
 * over the FlDoc of the open font (NULL: none; call with NULL before the document is freed).  The stack's order of the font layers is
 * the document's order (it is what is persisted: Layers bumps doc->rev for every change that has to be stored).  Session layers that
 * come from a file (fontLevel false) own their FlLayer and are never stored. */
void    Layers_SetFontDoc(Layers *l, FlDoc *d);
/* Adds a layer from a file on top (the code of the edited glyph; a font layer follows it for every glyph).  face: the .ttc face, strike or
 * font index (flayers.h).  Returns its index, -1 with a message in err (not added). */
int     Layers_AddFile(Layers *l, const char *path, int face, bool fontLevel, char *err, int errn);
/* Adds a layer of a database font as a font layer (same code, saved with the font). */
int     Layers_AddDbFont(Layers *l, const char *pxfm, const char *fontName);
/* Draws layer i (not the current glyph) with the glyph origin at screen (ox, oy), canvas zoom z. */
void    Layers_DrawGlyph(Layers *l, int i, int ox, int oy, int z);
/* The file dialog of the popover (NULL: none; the popover then offers no file source); returns false when cancelled. */
void    Layers_SetFileDialog(bool (*fn)(char *out, int size));
/* The script hook that chooses the file of the popover without a dialog. */
bool    Layers_PopoverSetFile(Layers *l, const char *path);

/* The Add Layer popover: open, and its rectangle (the rest of the UI ignores the mouse while it is open). */
bool    Layers_PopoverOpen(const Layers *l);
void    Layers_PopoverClose(Layers *l);

/* The Layers tab.  `area` is the column of the panel (it scrolls inside: area.y may be above `visible`), `visible` the
 * part that is really on screen.  Returns the height used. */
float   Layers_DrawPanel(Layers *l, Rectangle area, Rectangle visible);
/* The popover (call outside any scissor, after the panel and the editor); anchor = the panel's rectangle. */
void    Layers_DrawPopover(Layers *l, Rectangle panel);

/* Test hooks. */
int     Layers_FoundCount(Layers *l, int i);      /* ink pixels of the layer's glyph, -1 when not in font */
int     Layers_CachedFonts(const Layers *l);

#endif
