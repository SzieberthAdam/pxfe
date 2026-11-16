/* sheetref.h - the reference glyphs of sheet.h, drawn from outline fonts with fit.h (no raylib). */
#ifndef SHEETREF_H
#define SHEETREF_H

#include "sheet.h"

/* Adds one reference font: the candidate codes of sheet.h drawn from an outline font file at `em` pixels per em (as a pixel font
 * when the file is one, else smooth).  Returns the number of glyphs added, 0 when the file cannot be read. */
int sheet_refs_add_outline(SheetRefs *r, const char *path, float em);
/* The fonts pxfe ships: GNU Unifont and Noto Sans Regular and Bold under `fontsDir` (assets/fonts).  Returns the glyphs added. */
int sheet_refs_add_defaults(SheetRefs *r, const char *fontsDir);

#endif
