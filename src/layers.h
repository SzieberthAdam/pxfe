/* layers.h - reference layers of the glyph view (DESIGN.md "Version 4", "Layers").
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

typedef struct Layer {
    bool isCurrent;              /* the edited glyph itself */
    bool visible;
    Color color;                 /* opaque RGB; the alpha is `opacity` */
    int opacity;                 /* 0 .. 100 (percent) */
    int dx, dy;                  /* offset in pixels, y up */
    bool sameCode;               /* follows the code of the edited glyph */
    int32_t code;                /* the fixed code when !sameCode */
    char pxfm[33];               /* the font of the library, "" = this font */
    char fontName[96];           /* "Family Style" of that font (for the row) */
} Layer;

typedef struct Layers Layers;

Layers *Layers_Create(void);
void    Layers_Destroy(Layers *l);

/* Per frame (cheap): the store the other fonts come from (NULL: none) and what the editor shows.  `doc` is the font being
 * edited (this font), `editedCode` the code of the edited glyph. */
void    Layers_SetStore(Layers *l, Store *s);
void    Layers_SetContext(Layers *l, const PxFont *doc, int32_t editedCode);

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
int     Layers_Add(Layers *l, const char *pxfm, const char *fontName, bool sameCode, int32_t code);
void    Layers_Remove(Layers *l, int i);          /* not the current glyph */
void    Layers_Move(Layers *l, int i, int dir);   /* -1 up (towards the top), +1 down */

/* A text field of the panel or of the popover has the keyboard: the editor must not use its shortcuts. */
bool    Layers_TextFocus(const Layers *l);
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
