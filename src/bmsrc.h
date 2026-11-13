/* bmsrc.h - bitmap font sources for the glyph view: .bdf, .pcf, .fnt, .fon (NE and PE), .json sheet descriptors
 * (image grid, raw dump, "records") and the embedded bitmap strikes of .ttf/.otf/.ttc (EBLC/EBDT, CBLC/CBDT, bloc/bdat).
 *
 * The files are read exactly as pixelglyphlib does (pixelglyphlib/readers/bdf.py, pcf.py, fnt.py, sheet.py, sbit.py):
 * the same baseline, offsets, advances, ascent/descent and encodings (converted to Unicode).
 *
 * Portable C99, no raylib, no Win32, no globals.  Memory only through malloc.  Nothing crashes on a truncated or garbage
 * file: bm_open() returns NULL and writes a message to `err`.  Reading is lazy in no way: the whole file is parsed on open.
 *
 * MODEL.  A source holds one or more STRIKES (a bitmap size): BDF, PCF, FNT and sheets have one, a .fon one per FNT
 * resource, a .ttf one per bitmap strike (a strike of several bit depths is one strike each).
 * A strike holds GLYPHS in file order.  A glyph has an optional Unicode code point; lookup by code point is bm_find().
 *
 * COORDINATES (as PxGlyph in pxfont.h).  Positions are counted from the pen position on the baseline, y UP.
 * A glyph's (x, y) is the bottom-left corner of its bitmap: x columns right of the pen, y rows above the baseline
 * (negative: below).  The bitmap is stored top row first.  Row r (0 = top) covers the band from y+(h-1-r) to y+(h-r).
 * The strike's box is `ascent` rows above the baseline and `descent` rows below it; a glyph may lie outside it.
 * advance is in whole pixels.
 *
 * PIXELS.  glyph->depth is 1 or 8.  Depth 1: each byte of pix is 0 or 1.  Depth 8 (the strike is a greyscale or colour
 * strike of the sbit tables: bit depth 2, 4, 8 or 32): each byte is coverage 0..255 (a depth of d bits scaled by 255/(2^d-1);
 * a PNG strike, formats 17-19 of CBDT, contributes its alpha channel).  A strike is depth 8 if its source depth is above 1.
 */
#ifndef BMSRC_H
#define BMSRC_H

#include <stddef.h>
#include <stdint.h>

#define BM_ERR_LEN 256

typedef struct BmSource BmSource;

typedef struct BmGlyph {
    int codepoint;           /* Unicode code point, or -1 if the glyph has none (an uncoded glyph) */
    int code;                /* the code in the source: BDF ENCODING, PCF code, FNT character code, sheet cell code; the glyph ID of a
                                .ttf strike; -1 if none */
    int x, y;                /* bottom-left corner of the bitmap from the pen on the baseline, y up */
    int advance;             /* whole pixels */
    int w, h;                /* bitmap size (0 x 0: an empty glyph, pix is NULL) */
    int depth;               /* 1 or 8, see PIXELS above */
    const unsigned char *pix; /* w*h bytes, top row first; owned by the source */
    const char *name;        /* glyph name (BDF, PCF) or NULL; owned by the source */
} BmGlyph;

typedef struct BmStrikeInfo {
    char label[32];          /* short, for a list: "8x16" (FNT), "16px", "12x16px" (ppem x and y of an sbit strike) */
    char name[160];          /* full name, best effort: BDF FONT, FNT face name, name table of a .ttf, sheet meta family; may be "" */
    char family[96];         /* family, best effort, may be "" */
    char style[64];          /* style ("Bold", "Regular", "Medium Italic"), best effort, may be "" */
    int pixel_size;          /* BDF PIXEL_SIZE or ascent+descent, FNT height, sbit ppem y, sheet cell height */
    int ascent, descent;     /* rows above and below the baseline (descent >= 0 usually) */
    int depth;               /* 1 or 8 */
    int glyph_count;         /* number of glyphs (also bm_glyph_count) */
} BmStrikeInfo;

/* A glyph as a plain structure for the UI; the same fields and pixel characters as PxGlyph of pxfont.h ('.' none, '0'..'9',
 * 'A'..'Z' = 37 levels; ink of a 1-bit glyph is 'Z', coverage c of a depth-8 glyph becomes level (c*36+127)/255). */
typedef struct BmPlain {
    int codepoint;           /* Unicode code point or -1 */
    int x, y;                /* as BmGlyph */
    int advance;
    int w, h;
    char *pix;               /* w*h pixel characters, malloc'd, owned by the BmPlain (bm_plain_free); NULL if w*h == 0 */
} BmPlain;

/* Opens a bitmap font file.  The kind is detected from the content (PCF magic, "STARTFONT", MZ, sfnt tags, a JSON object, else
 * a bare FNT).  A .json path is a sheet descriptor; its "source" is read from the descriptor's directory (or absolute).
 * Returns NULL on failure with a message in err (BM_ERR_LEN bytes; err may be NULL).  Free with bm_close(). */
BmSource *bm_open(const char *path, char *err, size_t errlen);

/* The same for a font in memory (not a JSON sheet: that needs its source file).  The data is not kept. */
BmSource *bm_open_mem(const unsigned char *data, size_t n, char *err, size_t errlen);

void bm_close(BmSource *src);                       /* NULL ok; frees everything incl. all returned glyphs */

int bm_strike_count(const BmSource *src);           /* >= 1 for a source returned by bm_open */
int bm_strike_info(const BmSource *src, int strike, BmStrikeInfo *out);   /* 1 ok, 0 bad index */

int bm_glyph_count(const BmSource *src, int strike);
const BmGlyph *bm_glyph_at(const BmSource *src, int strike, int index);   /* file order; NULL out of range; owned by the source */
const BmGlyph *bm_find(const BmSource *src, int strike, int codepoint);   /* by Unicode code point; NULL if none; owned by the source.
                                                                              If several glyphs have the code, the first in file order. */

/* Fills *out from a glyph (copies the bitmap); 1 ok, 0 on out of memory or bad arguments.  Free with bm_plain_free(). */
int bm_glyph_to_plain(const BmGlyph *g, BmPlain *out);
void bm_plain_free(BmPlain *p);

#endif
