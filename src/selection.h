/* selection.h - the cell mask behind the glyph editor's selection tools (see DESIGN.md, "Selection and moving").
 *
 * No raylib, no pxfont: plain masks and character bitmaps, so the logic can be unit tested (tests/test_selection.c).
 * Coordinates are the editor's glyph space: x = column from the pen, y = a row counted down from the baseline line.
 * A mask is a bounding box with one byte per cell; a non-empty mask is always tight (its box is the box of its cells),
 * an empty mask has w == 0 and no storage. */
#ifndef SELECTION_H
#define SELECTION_H

#include <stdbool.h>

typedef struct SelMask {
    int x, y, w, h;          /* top-left cell and size of the box */
    unsigned char *m;        /* w * h bytes, 0 or 1 */
} SelMask;

/* how a new shape meets the selection that is there */
typedef enum { SEL_REPLACE = 0, SEL_ADD, SEL_SUB, SEL_INTERSECT } SelMode;

/* A character bitmap: the glyph's pixels seen through its box (x, y = top-left cell). */
typedef struct SelBitmap {
    char *pix;               /* w * h pixel characters ('.' = empty); may be NULL when w == 0 */
    int x, y, w, h;
} SelBitmap;

/* Pixels lifted out of a bitmap or the clipboard: the cells of `shape` with their characters ('.' = transparent). */
typedef struct SelFloat {
    SelMask shape;
    char *pix;               /* shape.w * shape.h characters, '.' outside the shape and where the shape's cell was empty */
} SelFloat;

/* ---- masks */
void sel_free(SelMask *s);                                   /* empties it */
bool sel_empty(const SelMask *s);
bool sel_get(const SelMask *s, int x, int y);
int  sel_count(const SelMask *s);                            /* cells set */
bool sel_copy(SelMask *dst, const SelMask *src);             /* false when out of memory (dst is then empty) */
void sel_shift(SelMask *s, int dx, int dy);
bool sel_set_rect(SelMask *out, int x0, int y0, int x1, int y1);   /* the cells of the rectangle, corners inclusive, any order */
bool sel_combine(SelMask *dst, const SelMask *shape, SelMode mode);
bool sel_invert(SelMask *s, int x0, int y0, int x1, int y1);       /* the cells of the rectangle that were not selected; cells outside it are dropped */
/* The cells inside the closed polygon through the points (xy[2*i], xy[2*i+1]), a cell being inside when its centre is (even-odd rule), plus the
 * cells the outline itself passes through, so a thin loop or a line still selects something. */
bool sel_polygon(SelMask *out, const int *xy, int n);
/* The magic wand: the cells connected to (sx, sy) that hold the same character, inside `region` (4-connected, 8 with `diagonals`);
 * empty when (sx, sy) is outside the region. */
bool sel_wand(SelMask *out, const SelBitmap *region, int sx, int sy, bool diagonals);

/* ---- bitmaps */
char selbm_get(const SelBitmap *b, int x, int y);            /* '.' outside */
int  selbm_ink_in(const SelBitmap *b, const SelMask *m);     /* selected cells that hold a pixel other than '.' */
int  selbm_clear(SelBitmap *b, const SelMask *m);            /* sets the selected cells to '.'; returns how many changed */
int  selbm_stamp(SelBitmap *b, const SelFloat *f);           /* writes the float's pixels other than '.' (cells outside the bitmap are skipped); returns how many changed */

/* ---- floating pixels */
void selfloat_free(SelFloat *f);
bool selfloat_lift(SelFloat *f, const SelMask *m, const SelBitmap *b);   /* the pixels of the selected cells */
bool selfloat_copy(SelFloat *dst, const SelFloat *src);
void selfloat_shift(SelFloat *f, int dx, int dy);
bool selfloat_ink_bounds(const SelFloat *f, int *x0, int *y0, int *x1, int *y1);   /* box of the pixels other than '.', inclusive; false when none */

#endif
