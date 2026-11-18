/* sheet.h - finding a font in a picture (DESIGN.md "Importing a picture of a font").
 *
 * A picture of a font tells nothing about itself: not where the glyphs are, not which characters they are, not what is background.
 * sheet_analyze works it out, step by step, and every step can be overridden (SheetOpts):
 *
 *   background   the transparent pixels when the picture has any, else its commonest colour; a JPEG's noise is told from ink by a
 *                tolerance.
 *   scale        a picture whose font pixels are blocks of s x s picture pixels (a preview, a poster) is brought back to one pixel
 *                per font pixel.
 *   layout       SHEET_MARKER  a strip with a marker row: single pixels in the top (or bottom) row stand over the columns that
 *                              separate the glyphs (a glyph is the columns between two markers);
 *                SHEET_GRID    cells of one size, found from the gaps between the glyphs or, when the glyphs fill their cells, from
 *                              the period of the picture;
 *                SHEET_ROWS    rows of glyphs of their own widths, cut where a column is blank.
 *   characters   each glyph is compared with the same character of reference fonts (SheetRefs: shape and place in the line), and
 *                the best SEQUENCE is taken: sheets list their characters in runs of ascending codes (ASCII, or the upper half of
 *                code page 437), so a glyph that continues the run of its neighbour is favoured, and a blank cell holds the place of
 *                a missing character.  Capitals standing in the places of small letters are accepted there.
 *   colours      the colours of the ink become the palette of the font (at most SHEET_MAX_COLORS, the closest are merged).
 *   metrics      the baseline is where most glyphs end.
 *
 * Plain C99, no raylib, no file reading (the caller hands the pixels; bm_load_rgba of bmsrc.h reads a file). */
#ifndef SHEET_H
#define SHEET_H

#include <stdint.h>

#define SHEET_MAX_COLORS 36

enum { SHEET_AUTO = 0, SHEET_GRID = 1, SHEET_ROWS = 2, SHEET_MARKER = 3 };

/* ---- the reference glyphs the characters are recognised with ---- */

typedef struct SheetRefs SheetRefs;

SheetRefs *sheet_refs_new(void);
void sheet_refs_free(SheetRefs *r);
/* One reference font: begin, add its glyphs, end (the font's own line metrics are taken from its capitals and digits).
 * A glyph: w*h bytes 0 / non-0, top row first; left = its left column from the pen, top = the rows of its top edge above the baseline. */
void sheet_refs_begin(SheetRefs *r);
void sheet_refs_add(SheetRefs *r, int code, const unsigned char *pix, int w, int h, int left, int top);
void sheet_refs_end(SheetRefs *r);
int sheet_refs_count(const SheetRefs *r);            /* glyphs added in all */
/* The codes a sheet may hold (what the references should cover): ASCII, Latin-1, the upper half of code page 437 and a few more;
 * fills out[0..max) ascending and returns the count. */
int sheet_candidate_codes(int *out, int max);

/* ---- the analysis ---- */

typedef struct SheetOpts {
    int layout;                /* SHEET_AUTO or the layout to use */
    int scale;                 /* 0 auto, else picture pixels per font pixel */
    int cellW, cellH;          /* SHEET_GRID: 0 auto */
    int offX, offY;            /* SHEET_GRID: where the first cell starts (with cellW / cellH given) */
    int firstCode;             /* > 0: no recognition, the glyphs are this code and the following ones (blank cells count) */
    const int *codes;          /* != NULL: no recognition, the codes of the glyphs in order (ncodes of them; -1 skips a glyph) */
    int ncodes;
    int mono;                  /* 1: one ink colour, whatever the picture has */
    int block;                 /* a sheet that holds the same font several times (in several colours): which copy, 0 = the first */
} SheetOpts;

typedef struct SheetCell {
    int x, y, w, h;            /* the cell in the sheet's pixels (after the scale) */
    int ix, iy, iw, ih;        /* its ink, iw == 0: blank */
    int row;                   /* the text row it is in */
    int code;                  /* the character, -1: none (a blank cell that holds no place, or a glyph left without a code) */
    float score;               /* how well it matches that character (0 .. 1), 0 when not recognised */
} SheetCell;

typedef struct Sheet {
    int w, h;                  /* size after the scale */
    uint32_t *px;              /* w*h pixels 0xRRGGBBAA, top row first */
    unsigned char *ink;        /* w*h: 1 = ink */
    int scale;                 /* picture pixels per font pixel */
    int alphaBg;               /* the background is the transparent pixels */
    uint32_t bg;               /* else this colour */
    int noisy;                 /* the picture has noise (a JPEG) */
    int layout;                /* SHEET_GRID / SHEET_ROWS / SHEET_MARKER */
    int markerRow;             /* SHEET_MARKER: the row of the markers */
    int cellW, cellH, offX, offY;   /* SHEET_GRID */
    int nrows;                 /* text rows */
    SheetCell *cells;
    int ncells;
    int glyphs;                /* cells with ink and a code */
    int blocks, block;         /* the sheet holds the font this many times (1: once); the copy the cells are of */
    int ascent, descent;       /* the font box: rows above and below the baseline */
    int lineTop;               /* the row of a cell (from its top) where the font box starts */
    uint32_t palette[SHEET_MAX_COLORS];   /* 0xRRGGBBAA of the ink colours, the commonest first */
    int npalette;
    int recognised;            /* 1: the characters were recognised, 0: given by the options or counted from a first code */
    float confidence;          /* mean score of the glyphs */
    char note[160];            /* what was found, for the user */
} Sheet;

/* rgba: w*h*4 bytes R G B A.  refs may be NULL (then the glyphs are counted from '!' or space).  0 ok, -1 on an error (msg). */
int sheet_analyze(const unsigned char *rgba, int w, int h, const SheetOpts *opts, const SheetRefs *refs, Sheet *out, char *msg, int msgsz);
void sheet_free(Sheet *s);

/* The pixels of a cell as palette entries: out[cw*ch] gets 0 for background and 1 + palette index for ink (the cell's own size).
 * out must hold cell->w * cell->h bytes. */
void sheet_cell_pixels(const Sheet *s, const SheetCell *c, unsigned char *out);

#endif
