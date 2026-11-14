/* outline.h - outline font source for the glyph view: opens an OUTLINE font file and rasterises single glyphs smoothly.

   Formats (the ones pixelglyphlib reads, no WOFF2):
     .ttf .otf .ttc   TrueType (glyf) and CFF (OTTO) outlines; a collection holds several faces
     .woff            zlib-compressed tables (own inflate)
     .eot             uncompressed EOT, with or without the XOR 0x50 encryption; MicroType Express compression -> error
     .pfb             Adobe Type 1 (PFB segments, eexec, charstrings, subrs, flex, seac); no cmap: glyph names are mapped to
                      code points through the font's own Encoding, uniXXXX / uXXXX names and a built-in Latin name table
   The format is decided by the MAGIC of the data; the extension is only a hint for messages.

   Design:
     - Plain C99, no globals, no platform code.  Every object belongs to the caller (close / free what you opened / got).
     - All parsing is bounds-checked: truncated, garbled or hostile files give an error, never a crash.  (For this reason the
       TrueType, CFF and Type 1 parsers are our own; stb_truetype trusts offsets in the file and is only used by the tests
       to cross-check results.)
     - Every function that can fail returns a status (0 / OUTLINE_OK, or a negative value / NULL) and, when err is not NULL,
       writes a short message into err[errsz].
     - Rasterising is a pure function of (font, face, glyph, size, subpixel offset): the caller may cache the bitmap.
       The outline is flattened and filled by an exact-area coverage rasteriser (signed area accumulation, non-zero style:
       coverage = min(1, |winding area|)); a 600 px glyph takes about a millisecond.
     - Variable fonts show their default instance.  Hinting is ignored.  Composite glyphs with point-matching are placed
       with offset 0.  Limits: em size 0.5 .. 8192 px and bitmaps up to 16384 x 16384 and 16 M pixels.

   Coordinates: font units are the font's own (units_per_em per em).  Bitmap coordinates: the baseline origin is (0, 0),
   x to the right, y UP.  A bitmap with left/top has its upper-left pixel centred at (left + 0.5, top - 0.5); pixel row r covers
   y in [top - r - 1, top - r]; so a glyph resting on the baseline has top > 0 and a descender goes below (top - h < 0). */
#ifndef OUTLINE_H
#define OUTLINE_H

#include <stddef.h>

typedef struct Outline Outline;   /* an opened font file (opaque) */

typedef struct {
    unsigned char *pix;   /* w*h bytes of coverage 0..255, row-major, top row first; NULL when w == 0 or h == 0 */
    int w, h;
    int left, top;        /* position of the upper-left corner relative to the glyph origin (y up) */
    float advance;        /* horizontal advance in px (font advance * size / units_per_em) */
} OutlineBitmap;

enum { OUTLINE_OK = 0, OUTLINE_MISSING = 1, OUTLINE_ERROR = -1 };

/* ---- open / close ---- */

/* Reads the file and opens it (face 0).  NULL + err on failure. */
Outline *outline_open(const char *path, char *err, int errsz);
/* Same from memory (the data is copied); ext (".woff", "ttf", ... or NULL) is a hint for messages only. */
Outline *outline_open_mem(const void *data, size_t size, const char *ext, char *err, int errsz);
void outline_close(Outline *f);

/* ---- faces ---- */

int outline_face_count(const Outline *f);               /* 1 except for .ttc / .otc */
int outline_face(const Outline *f);                     /* the selected face */
int outline_select_face(Outline *f, int index, char *err, int errsz);   /* 0 or -1; the old face stays selected on failure */

/* ---- description (of the selected face) ---- */

const char *outline_format(const Outline *f);           /* "ttf" "otf" "ttc" "woff" "eot" "pfb" (by magic; "otf" = CFF outlines) */
const char *outline_family(const Outline *f);           /* name table (ID 16, else 1) / Type 1 FamilyName; "" if none; UTF-8 */
const char *outline_style(const Outline *f);            /* name table (ID 17, else 2) / Type 1 Weight; "" if none */
int outline_units_per_em(const Outline *f);
/* ascender (positive up), descender (negative, down) and line gap in font units; any pointer may be NULL */
void outline_metrics(const Outline *f, int *ascender, int *descender, int *line_gap);
int outline_glyph_count(const Outline *f);

/* ---- lookup ---- */

/* code point -> glyph index; 0 (the .notdef glyph) when the font has no glyph for it */
int outline_glyph_index(const Outline *f, int codepoint);
/* advance width in font units (0 for a bad glyph) */
int outline_advance(const Outline *f, int glyph);

/* ---- rasterising ---- */

/* px: em size in pixels.  (fx, fy): subpixel offset of the glyph origin, normally in [0, 1) (added to the glyph position,
   y up).  The bitmap is allocated for you: release it with outline_bitmap_free.
   Return OUTLINE_OK (also for blank glyphs: w = h = 0, advance set), OUTLINE_MISSING (outline_rasterize only: no glyph
   for the code point; *out is zeroed) or OUTLINE_ERROR (bad font data, bad size; *out is zeroed). */
int outline_rasterize(const Outline *f, int codepoint, float px, float fx, float fy,
                      OutlineBitmap *out, char *err, int errsz);
int outline_rasterize_glyph(const Outline *f, int glyph, float px, float fx, float fy,
                            OutlineBitmap *out, char *err, int errsz);
/* The same with its own size along x: pxx pixels per em across, px pixels per em up (a stretched glyph; the advance follows pxx). */
int outline_rasterize_glyph_xy(const Outline *f, int glyph, float pxx, float px, float fx, float fy,
                               OutlineBitmap *out, char *err, int errsz);
void outline_bitmap_free(OutlineBitmap *b);

/* The pixel box rasterising would produce (same left/top/w/h, no allocation of pixels), also the advance in px. */
int outline_glyph_bbox(const Outline *f, int glyph, float px, float fx, float fy,
                       int *left, int *top, int *w, int *h, float *advance, char *err, int errsz);

/* ---- names ---- */

/* A string of the name table by its ID (0 copyright, 1 family, 2 style, 4 full name, 5 version, 7 trademark, 8 vendor, 9 designer,
   10 description, 11 vendor URL, 12 designer URL, 13 licence, 14 licence URL), UTF-8.  Returns its length, 0 when the font has
   none (a Type 1 font knows 1 and 2 only). */
int outline_name(const Outline *f, int id, char *out, int outsz);

/* ---- the outline itself (for the grid search of fit.h) ---- */

typedef struct OutlineShape {
    int nc;                 /* contours */
    int *start;             /* nc + 1 entries: contour c is the points start[c] .. start[c+1]-1, closed (last to first) */
    float *x, *y; int n;    /* the points in font units, y up; a curve is `steps` straight pieces */
    unsigned char *curved;  /* per contour: it has a curve */
    float *px, *py; int np; /* the points the font itself names: ends of lines and curves and their control points */
    int *pstart;            /* nc + 1 entries: the named points of contour c are pstart[c] .. pstart[c+1]-1 */
    int advance;            /* font units */
} OutlineShape;

/* The contours of a glyph (composites resolved) in font units.  OUTLINE_OK (a blank glyph has nc == 0) or OUTLINE_ERROR.
   Release with outline_shape_free. */
int outline_glyph_shape(const Outline *f, int glyph, int steps, OutlineShape *out, char *err, int errsz);
void outline_shape_free(OutlineShape *s);

#endif
