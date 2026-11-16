/* raster.h - the cell paths behind the glyph editor's line, curve and pencil tools (see DESIGN.md, "Version 17: line, curve and pixel-perfect").
 *
 * No raylib, no pxfont: plain integer cells, so the logic can be unit tested (tests/test_raster.c).
 * Coordinates are the editor's glyph space (x to the right, y down); a path is a list of cells in drawing order. */
#ifndef RASTER_H
#define RASTER_H

#include <stdbool.h>

typedef struct RasPt { int x, y; } RasPt;

typedef struct RasPath {
    RasPt *p;
    int n, cap;
} RasPath;

void ras_free(RasPath *r);                       /* empties it */
bool ras_add(RasPath *r, int x, int y);          /* appends a cell unless it equals the last one; false when out of memory */

/* The cells of the straight line from (x0, y0) to (x1, y1), both ends included, in order from the first end to the second (Bresenham).
 * The line is symmetric: the cells of the line from B to A are those of the line from A to B (the line is always walked from the smaller end). */
bool ras_line(RasPath *out, int x0, int y0, int x1, int y1);

/* The constrained angle: the vector (dx, dy) is turned to the nearest of the clean directions and cut to a whole number of its steps.
 * Directions, in steps (run, rise): 0 degrees (1, 0), 14 (4, 1), 27 (2, 1), 45 (1, 1), 63 (1, 2), 76 (1, 4) and 90 (0, 1), in all four quadrants.
 * They stand for the angles 0, 15, 30, 45, 60, 75 and 90 degrees, chosen by the nearest multiple of 15 degrees. */
void ras_snap(int dx, int dy, int *ox, int *oy);

/* The two ends of a line (or of the ends of a curve) dragged from the cell (sx, sy) to the cell (hx, hy): with `snap` the constrained angle,
 * with `center` the press cell is the middle of the line and the far end is mirrored through it.  ends = {x0, y0, x1, y1}. */
void ras_ends(int sx, int sy, int hx, int hy, bool snap, bool center, int ends[4]);

/* b is the corner of an L: a, b and c are consecutive cells, a to b and b to c are one step each along different axes, and a and c touch
 * diagonally.  Dropping b leaves the path connected (a and c are diagonal neighbours) and one cell wide. */
bool ras_is_corner(RasPt a, RasPt b, RasPt c);

/* The pixel-perfect rule: drops the middle cell of every L of the path, in place; returns the new length. */
int  ras_pixel_perfect(RasPt *p, int n);

/* The cubic Bezier curve with the end points p0 and p3 and the control points p1 and p2 as a connected path of cells (8-connected, no cell twice
 * in a row, no gap): the curve is sampled at steps of less than a cell, rounded to cells and, with `perfect`, cleaned by the pixel-perfect rule.
 * With p1 = p0 and p2 = p3 it is the straight line. */
bool ras_bezier(RasPath *out, RasPt p0, RasPt p1, RasPt p2, RasPt p3, bool perfect);

/* The box of a rectangle or ellipse dragged from the cell (sx, sy) to (hx, hy): with `square` the longer side wins and both are equal, with `center` the press
 * cell is the middle of the box and the far corner is mirrored through it.  box = {x0, y0, x1, y1} with x0 <= x1 and y0 <= y1. */
void ras_box(int sx, int sy, int hx, int hy, bool square, bool center, int box[4]);

/* The cells of the rectangle with the corners (x0, y0) and (x1, y1) (x0 <= x1, y0 <= y1), in row order: its outline, or all cells when `filled`.
 * A box one cell wide or high is a line. */
bool ras_rect(RasPath *out, int x0, int y0, int x1, int y1, bool filled);

/* The cells of the ellipse that fills the box (x0 <= x1, y0 <= y1), in row order: a closed ring of 8-connected cells, one cell thick, no cell twice,
 * no L corner (the pixel-perfect rule), symmetric in both axes, touching the box on all four sides; with `filled` the ring and its inside.
 * A box one cell wide or high is a line; a box two cells wide or high is a solid band. */
bool ras_ellipse(RasPath *out, int x0, int y0, int x1, int y1, bool filled);


/* ---- fill and polygon (see DESIGN.md, "Version 20: fill, eyedropper, polygon and contour") */

/* The cells a paint bucket would fill: the region is the rw x rh characters `pix` with its top-left cell at (rx, ry); the seed is (sx, sy).
 * Contiguous: the cells joined to the seed that hold the seed's character (4-connected, 8 with `diagonals`); not contiguous: every cell of the region that
 * holds that character.  `clip` (rw * rh bytes, or NULL) limits the fill: a cell with 0 is neither filled nor passed through; a seed on such a cell fills nothing.
 * The cells come in row order.  A seed outside the region fills nothing (true is returned); false only when out of memory. */
bool ras_flood(RasPath *out, const char *pix, int rx, int ry, int rw, int rh, int sx, int sy, bool contiguous, bool diagonals, const unsigned char *clip);

/* The polygon through the vertices v[0..n-1] (cell centres): its outline is the line raster between consecutive vertices (and, when `closed` and n >= 3, from the last
 * back to the first); a vertex alone is one cell.  With `filled` (and closed, n >= 3) the cells whose centres are inside by the even-odd rule are added, so a
 * self-crossing polygon has holes where it crosses itself; the outline is always part of the result.  Without `filled` the cells come in drawing order, with it
 * in row order and no cell twice.  Collinear or repeated vertices give a line or a point. */
bool ras_polygon(RasPath *out, const RasPt *v, int n, bool closed, bool filled);

#endif
