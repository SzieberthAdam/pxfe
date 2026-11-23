/* fontwrite.h - writing fonts of the library in the formats of other programs: BDF, PCF, FNT, FON, TTF, OTF, WOFF.  No raylib.
 *
 * Follows the Python writers of bitmapfont/tools/writers (bdf.py, pcf.py, fnt.py, ttf.py), which pixelglyphlib's readers (and
 * pxfe's bmsrc.c and outline.c) read back.  One FwFace is a font with the text of its typeface variant; every format holds one
 * face except FON, which holds them all (one FNT resource each).
 *
 * WHAT GETS IN.  A pixel other than '.' is ink.  Only OpenType takes a colour font (px_font_is_colour): its colours go into an SVG
 * table, the CFF outlines (every colour as ink) stay for programs without SVG glyphs; the other formats refuse one.
 * Glyph variants (code variant other than 0) are left out.  A symbol (X+n) has no code: BDF writes ENCODING -1, PCF and the sfnt
 * formats keep it without a character mapping, FNT leaves it out.  The default glyph (I+DEFA00) is .notdef in the sfnt formats
 * and left out of the others (FNT's dfDefaultChar names '?' when the font has it).
 * The font box is px_font_box: every bitmap must be loaded (store_load_font with codes NULL).
 * A font of SYMBOLIC keeps its code points as the codes of a private encoding: BDF and PCF write the charset "Misc-FontSpecific",
 * FNT dfCharSet 2 (SYMBOL_CHARSET), the sfnt formats a (3,0) cmap subtable.
 *
 * WARNINGS.  What a format could not hold (code points outside the code page, glyphs outside the cell of a FNT, codes above
 * FFFF in a PCF) is left out and told in `warn` as lines ended with LF (it may be NULL). */
#ifndef FONTWRITE_H
#define FONTWRITE_H

#include <stdbool.h>
#include <stddef.h>

#include "pxfont.h"

typedef enum FwFormat { FW_BDF, FW_PCF, FW_FNT, FW_FON, FW_TTF, FW_OTF, FW_WOFF, FW_COUNT } FwFormat;

const char *fw_ext(FwFormat f);             /* "bdf", "pcf", ... (lower case, no dot) */
const char *fw_label(FwFormat f);           /* "BDF", "PCF", "FNT", "FON", "TrueType", "OpenType", "WOFF" */
bool fw_format_of_ext(const char *ext, FwFormat *out);   /* any case, with or without the dot */
static inline bool fw_one_file(FwFormat f) { return f == FW_FON; }   /* every face in one file */
static inline bool fw_takes_colour(FwFormat f) { return f == FW_OTF; }   /* a colour font (SVG glyphs) */

/* The text of a face, UTF-8; NULL or "" when not stated.  The writers fall back as the Python ones do: family to the file's stem,
 * style to "Regular", full name to "<family> <style>". */
typedef struct FwMeta {
    const char *family;            /* the typeface's NAME */
    const char *style;             /* STYLE */
    const char *fullName;          /* the variant's NAME */
    const char *weightName;        /* WEIGHTNAME */
    const char *version, *copyright, *trademark, *description, *designer, *designerUrl, *vendor, *vendorUrl, *license, *licenseUrl;
    const char *codepage;          /* CODEPAGE: FNT uses it when it is one of its character sets */
    bool italic, underlined, struckout;
} FwMeta;

typedef struct FwFace { const PxFont *font; FwMeta meta; } FwFace;

/* A growing byte buffer (the writers' output). */
typedef struct FwBuf { unsigned char *p; size_t n, cap; bool failed; } FwBuf;
void fw_buf_add(FwBuf *b, const void *data, size_t n);
void fw_buf_zero(FwBuf *b, size_t n);
void fw_buf_free(FwBuf *b);
void fw_be16(FwBuf *b, unsigned v);
void fw_be32(FwBuf *b, unsigned long v);
void fw_le16(FwBuf *b, unsigned v);
void fw_le32(FwBuf *b, unsigned long v);

/* Appends a line (printf format, LF added) to the warnings, cut at warnCap. */
void fw_warn(char *warn, size_t warnCap, const char *fmt, ...);

/* The bytes of a file of `faces[0..n)` (n is 1 except for FW_FON), malloc'd, *len set; NULL with err on failure.  `stem` is the
 * file name without folder and extension (the BDF and PCF FONT property, the fallback family). */
unsigned char *fw_bytes(FwFormat fmt, const FwFace *faces, int n, const char *stem, size_t *len, char *warn, size_t warnCap,
                        char err[PX_ERR_LEN]);
/* fw_bytes written to path (the stem taken from it). */
bool fw_write(FwFormat fmt, const FwFace *faces, int n, const char *path, char *warn, size_t warnCap, char err[PX_ERR_LEN]);

/* The sfnt formats (fontwrite_sfnt.c): what fw_bytes calls for FW_TTF, FW_OTF and FW_WOFF. */
unsigned char *fw_sfnt_bytes(FwFormat fmt, const FwFace *face, const char *stem, size_t *len, char *warn, size_t warnCap,
                             char err[PX_ERR_LEN]);

/* Helpers shared by the writers. */
typedef struct FwGlyphs { const PxGlyph **g; int n; } FwGlyphs;
/* The glyphs a writer takes, in font order: no variant, the default glyph only when `withDefault` (then last).  malloc'd. */
bool fw_glyphs(const PxFont *f, bool withDefault, FwGlyphs *out);
void fw_glyphs_free(FwGlyphs *g);
static inline bool fw_ink(const PxGlyph *g, int col, int row) { return g->pix[row * g->w + col] != '.'; }   /* col, row from the top-left */
/* A file-name-safe ASCII form of text (letters, digits, '-'; other runs become nothing): "Test Sans" -> "TestSans". */
void fw_ascii_name(const char *utf8, char *out, size_t cap, bool keepSpaces);

#endif
