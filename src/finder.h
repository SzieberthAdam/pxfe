/* finder.h - the glyph finder view (DESIGN.md "The Glyphs view"): every distinct bitmap of the library
 * drawn for one code point, with how many fonts use it, and the usage of the selected one. */
#ifndef FINDER_H
#define FINDER_H

#include "raylib.h"
#include "store.h"
#include "insight.h"

typedef struct Finder Finder; /* opaque, defined in finder.c */

Finder *Finder_Create(Store *store, Insight *insight);   /* the finder draws its usage panel through the shared Insight */
void    Finder_Destroy(Finder *f);

/* Set the query: a code point (0..10FFFF). Also settable by typing in the view's search box. */
void    Finder_SetCode(Finder *f, int32_t code);
int32_t Finder_Code(const Finder *f);                    /* -1 when empty */

/* Parse the search text: one character (UTF-8), "U+00E9", "00E9", "0xE9", "E9". Returns -1 if it is none. */
int32_t Finder_ParseQuery(const char *text);

/* Per frame inside `area`; owns the input inside it (including the search box focus: Ctrl+G / Ctrl+F focus it).
 * Returns true and fills *target when the user wants to open a font (INSIGHT_OPEN_FONT / _TYPEFACE). */
bool    Finder_UpdateDraw(Finder *f, Rectangle area, InsightTarget *target);

/* The pxgl hash of the selected bitmap (false when none is selected). */
bool    Finder_SelectedGlyph(const Finder *f, char out[33]);

/* Test hooks for scripted input. */
int     Finder_SideTab(const Finder *f);                   /* 0 View, 1 Usage */
int     Finder_Zoom(const Finder *f);
int     Finder_ResultCount(const Finder *f);              /* -1 while loading */
int     Finder_CellFonts(const Finder *f, int index);      /* the "N fonts" of a cell (in the display order); -1 when out of range */
bool    Finder_CellCenter(const Finder *f, int index, Vector2 *out);
void    Finder_Select(Finder *f, int index);

#endif
