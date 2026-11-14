/* fit.h - fitting an OUTLINE font to a pixel grid (DESIGN.md "Version 10: importing a font file").
 *
 * Two questions are answered here, and one job is done:
 *
 *   fit_find_grid   The font IS a pixel font drawn with outlines (squares, dots, rectangles on a grid): which grid?
 *                   A port of pixelglyphlib/readers/ttf.py (find_grid): the common divisor of the coordinates, a coarser grid
 *                   that redraws the ink, dots of one size, a shifted or non-square lattice fitted to the edges, a lattice
 *                   whose columns start anew in each glyph, and a loose lattice for carelessly drawn pixels.
 *   fit_find_size   The font is NOT a pixel font: at which small size, vertical position and coverage threshold does it turn
 *                   into pixels best?  "Best" means that the glyphs keep their parts and their holes (the counter of an A
 *                   does not close, the dot of an i does not melt into its stem) and that the pixels cover what the outline
 *                   covers; among the sizes that do so, a small one is taken.
 *   fit_render      Draws one glyph as pixels for a placement (the same placement a font layer of flayers.h has).
 *
 * THE PLACEMENT (FitParams).  A point (X, Y) of the outline, in font units, lies at the pixel position
 *       x = X * em * aspect / unitsPerEm + dx,     y = Y * em / unitsPerEm + dy        (y up, from the pen on the baseline)
 * so `em` is the size in pixels per em, `aspect` stretches it across (1 = square pixels), and (dx, dy) move it.  A glyph may
 * add its own correction: an offset and a factor on the size (FitCorr; the per-glyph corrections of a font layer).
 * mode FIT_PIXEL: a pixel is inked when its CENTRE is inside the outline (a pixel font: the grid is known).
 * mode FIT_SMOOTH: a pixel is inked when the outline covers at least `threshold` of it; a glyph without an x correction is
 * moved by a quarter pixel step (-0.5 .. +0.25) to where its pixels match its outline best (stems then fall on whole pixels).
 *
 * Plain C99, no globals, no raylib.  Needs outline.h. */
#ifndef FIT_H
#define FIT_H

#include "outline.h"

enum { FIT_SMOOTH = 0, FIT_PIXEL = 1 };

typedef struct FitParams {
    float em;          /* pixels per em (up) */
    float aspect;      /* width of the outline: pixels per em across = em * aspect */
    float dx, dy;      /* pixels, y up */
    float threshold;   /* FIT_SMOOTH: 0.05 .. 0.95 */
    int mode;          /* FIT_* */
} FitParams;

typedef struct FitCorr { float dx, dy, dsize; } FitCorr;   /* a glyph's own correction; dsize 1 = none */

typedef struct FitBitmap {
    int w, h;              /* cropped to the ink; 0 x 0: no ink (pix NULL) */
    int left, top;         /* the bitmap's left column from the pen, and the rows of its top edge above the baseline */
    unsigned char *pix;    /* w*h bytes 0 / 1, top row first; malloc'd */
    int advance;           /* whole pixels */
} FitBitmap;

void fit_bitmap_free(FitBitmap *b);

/* Draws a glyph (by glyph index).  corr may be NULL.  0 ok (also a blank glyph), -1 on an error. */
int fit_render(const Outline *ol, int glyph, const FitParams *p, const FitCorr *corr, FitBitmap *out);
/* The same; *shift gets how far the glyph was moved across to meet the pixels, in pixels (FIT_SMOOTH: -0.5 .. 0.25, else 0):
 * the outline drawn under the pixels must be moved by as much to lie where they were made from. */
int fit_render_shift(const Outline *ol, int glyph, const FitParams *p, const FitCorr *corr, FitBitmap *out, float *shift);

/* The font box the placement gives: the rows above and below the baseline of the font's ascender and descender. */
void fit_box(const Outline *ol, const FitParams *p, int *ascent, int *descent);

/* ---- the code points of a font ---- */

/* Every (code point, glyph index) of the font with a glyph (index > 0), ascending by code point; malloc'd pairs
 * (cps[2*i], cps[2*i+1]); returns the count, -1 when out of memory. */
int fit_codepoints(const Outline *ol, int **pairs);

/* ---- the grid of a pixel font ---- */

typedef struct FitGrid {
    double x, y;               /* font units per pixel, across and up */
    double xPhase, yPhase;     /* where a cell edge lies, font units */
    int perGlyph;              /* the columns start anew in each glyph: phase[] holds each glyph's own x phase */
    int *phaseCp; double *phase; int nphase;   /* perGlyph: (code point, x phase) ascending by code point; malloc'd */
    int sure;                  /* 1: the grid passed the tests of the search; 0: only the best guess (how says "guess") */
    double match;              /* how much of the ink the grid redraws (0 when the search did not measure it) */
    char how[24];              /* which search found it: exact, coarser, dots, lattice, edges, glyph columns, loose, guess */
} FitGrid;

void fit_grid_free(FitGrid *g);
/* 1: a grid was found (g filled), 0: none (the font is no pixel font, or it has no ink), -1: an error (msg). */
int fit_find_grid(const Outline *ol, FitGrid *g, char *msg, int msgsz);
/* The placement of a grid (mode FIT_PIXEL); a per-glyph x phase becomes the dx of FitCorr through fit_grid_corr. */
void fit_grid_params(const Outline *ol, const FitGrid *g, FitParams *p);
/* The correction of a code point on a per-glyph grid (dx in pixels); 0 when it has none. */
int fit_grid_corr(const FitGrid *g, int codepoint, FitCorr *c);

/* ---- the size of a font that is no pixel font ---- */

typedef struct FitSizeOpts {
    float minEm, maxEm;        /* the sizes tried, in pixels per em (0: 5 and 40) */
    float fixedEm;             /* > 0: only this size; the position and the threshold are still searched */
} FitSizeOpts;

typedef struct FitSizeReport {
    int probes;                /* glyphs looked at */
    float topo;                /* share of them that keep their parts and holes at the chosen size */
    float cover;               /* how well the pixels cover the outline there (1 = exactly) */
    float bestTopo;            /* the best share any size reached */
} FitSizeReport;

/* 1: found (p filled, mode FIT_SMOOTH, aspect 1, dx 0), 0: the font has no glyph to look at, -1: an error. */
int fit_find_size(const Outline *ol, const FitSizeOpts *opts, FitParams *p, FitSizeReport *rep, char *msg, int msgsz);

#endif
