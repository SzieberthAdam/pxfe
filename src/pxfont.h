/* pxfont.h - in-memory pixel font model and pxfm (version 2, F-codes) / pxgl / draft (version 3) I/O for pxfe.
 *
 * Formats: C:/Users/jnsGEzD3/Documents/GitHub/pixelglyphlib/spec/pxfm.md (version 2) and pxgl.md.
 * This module has NO raylib dependency (pure C99 + stdio; Win32 threads for the parallel glyph loader).
 *
 * COORDINATES (read this before drawing anything)
 *   Every position is counted from the pen position and the baseline, y UP.
 *   A glyph's (x, y) is the bottom-left corner of its bitmap: x columns right of the pen, y rows above the
 *   baseline (negative: below).  The bitmap is stored top row first (row 0 is the top row).
 *   So row r (0 = top) of a glyph covers the band from  y + (h-1-r)  to  y + (h-r)  rows above the baseline;
 *   px_glyph_top(g) = y + h is the height of the bitmap's top edge above the baseline, px_glyph_bottom(g) = y
 *   the height of its bottom edge.  To draw on a y-down screen with the baseline line at screen row `by` and a
 *   pixel size `s`:   screenY(row r) = by - (px_glyph_top(g) - r) * s,   screenX(col c) = penX + (g->x + c) * s.
 *   In a font box of `above` rows over the baseline (px_font_box) the top row's index inside the box (0 = top
 *   of box) is px_glyph_box_row(g, above) = above - (y + h).
 *   The font box is ASCENT rows above the baseline and DESCENT below it, but a glyph may lie outside it.
 */
#ifndef PXFONT_H
#define PXFONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PX_PIXELS "." "0123456789" "ABCDEFGHIJKLMNOPQRSTUVWXYZ" /* 37 pixel characters */
#define PX_NUM_PIXELS 37
#define PX_ERR_LEN 512

/* CODES (pxfm.md "Code points").  A glyph's `code` is an int32:
 *   0 .. 0x10FFFF    a code point;
 *   0xF00000 ..      a glyph with no code point: the first such glyph F00000, the next F00001, ... with NO gap
 *                    (up to FFFFFE); the code names the glyph only inside its font;
 *   0xFFFFFF         the default glyph: a glyph of its own (bitmap, offsets, advance), always the last one; it may
 *                    repeat a coded glyph or appear nowhere else in the font (pxfm.md calls such a line "ignored",
 *                    pxfe keeps and shows it).
 * Codes 0x110000 .. 0xEFFFFF do not exist.
 * PX_NO_CODEPOINT (-1) is only the sentinel "none" (no default glyph, glyph not found); it is no glyph's code any more.
 * The UI shows a code point as U+XXXX and an uncoded glyph as #n, n = code - PX_CODE_UNCODED_FIRST. */
#define PX_NO_CODEPOINT (-1)
#define PX_CODE_MAX_CODEPOINT 0x10FFFF
#define PX_CODE_UNCODED_FIRST 0xF00000
#define PX_CODE_DEFAULT 0xFFFFFF
static inline bool px_code_is_codepoint(int32_t c) { return c >= 0 && c <= PX_CODE_MAX_CODEPOINT; }
static inline bool px_code_is_uncoded(int32_t c)   { return c >= PX_CODE_UNCODED_FIRST && c < PX_CODE_DEFAULT; }
static inline bool px_code_is_valid(int32_t c)     { return px_code_is_codepoint(c) || px_code_is_uncoded(c); }

/* ------------------------------------------------------------------ model */

/* One glyph line of a pxfm file.  The bitmap is stored as pixel characters ('.', '0'-'9', 'A'-'Z'),
 * row-major, w*h bytes, no terminator, top row first.  A glyph with no ink has w == h == 0, pix == NULL,
 * x == y == 0 (a space).
 *
 * PIXELS IN MEMORY ARE THE GLYPH'S OWN.  A pixel character in memory names a colour of the GLYPH's palette: '0' is its entry 0,
 * '1' its entry 1, ... ('.' is the background and has no colour).  The palette is an entry of the font's palette table
 * (PxFont.pals), named by `pi`; glyphs of the same colours share one entry (see PALETTES below).  While editing, the
 * characters need not be in reading order: the writer renumbers each glyph canonically (pxgl.md "Canonical colours") and
 * gives it a palette of its colours in that order.  A glyph uses at most 36 colours (the 36 ink characters).
 * pi 0 is the default palette: black and nothing else.  The glyphs of a one-colour font (no COLORS line) all have it, so
 * they cost no palette at all; a sample draws a one-colour font in the theme's ink.
 *
 * HASH CACHE.  The pxgl hash of the bitmap is cached in the glyph (hash, cdx, cdy, ncol, order, hashValid) so that
 * px_font_hash() after an edit is cheap on a font of 8000 glyphs.  Whoever changes the PIXELS or the SIZE of a
 * glyph (pix, w, h) MUST call px_glyph_touch(g) afterwards; changes of x, y, advance and codepoint need no touch.
 * A glyph made with memset(0) is dirty (not cached) by definition.  The hash is that of the CANONICAL pxgl text: the
 * characters renumbered in reading order ('0' for the first ink character, '1' for the next new one...), so it does not
 * depend on which font-wide characters the glyph uses.
 *
 * CROPPING.  The hash (and so the written pxgl file) is that of the bitmap cropped to its inked pixels, so a
 * margin the editor grew and a margin it shrank again give the same hash, unless `asDrawn` is set: a bitmap the
 * library keeps "as drawn" (uncropped, such files exist) is hashed and written as it is, so reading and writing
 * it again gives the same bytes.  The reader sets asDrawn for such glyphs; px_glyph_crop() clears it.
 *
 * LAZY.  px_read_pxfm_lines() gives glyphs with `lazy` set: no bitmap (pix NULL, w = h = 0), only the hash (and the
 * palette hash `pal`); x, y, advance, codepoint are valid.  px_load_glyphs() loads them.  A colour font is never left
 * partly lazy by the loaders: its bitmaps are all loaded at once (colour fonts are small), so the COLORS count and the font's
 * colour list are always exact. */
typedef struct PxGlyph {
    union { int32_t code; int32_t codepoint; };  /* the glyph's code (see CODES above); `codepoint` is the old name */
    int x, y;              /* bitmap bottom-left corner from the pen position on the baseline, y up */
    int advance;           /* whole pixels */
    int w, h;              /* bitmap size */
    char *pix;             /* w*h pixel characters, malloc'd, owned by the glyph */
    bool asDrawn;          /* hash/write the bitmap uncropped, as stored */
    bool lazy;             /* bitmap not loaded (hash below is the pxgl hash named by the file) */
    bool hashValid;        /* cache below is valid (always true for a lazy glyph) */
    char hash[33];         /* cache: lower-case pxgl hash of the canonical (cropped or as-drawn) bitmap, "" = no ink */
    int cdx, cdy;          /* cache: the crop moved the bitmap's bottom-left by this much (0,0 when asDrawn) */
    int ncol;              /* cache: ink characters the bitmap uses (0..36); not valid for a lazy glyph */
    char order[36];        /* cache: those characters in reading order (order[0] becomes '0' in the file, ...) */
    char pal[33];          /* lower-case hash of the hex palette the glyph was READ with ("" none, or a one-colour font); the
                              writer keeps it while its first ncol colours are still the glyph's colours in reading order */
    int pi;                /* its palette: an index into PxFont.pals (0 = the default palette, black); it means something only
                              in the font that holds the glyph (a glyph copied into ANOTHER font must be given its palette) */
} PxGlyph;

typedef struct PxKern { int32_t first, second; int value; } PxKern;

/* A palette: its colours in order, 0xRRGGBBAA each (the alpha FF when a hex line has six digits), and the hash of its hex file
 * (spec/hex.md; 32 lower-case hex digits, "" when the colours are no hex palette: a colour twice).  `draw` is the palette
 * ready to draw, indexed by px_pixel_index: draw[0] ('.') is transparent, draw[1 + i] the colour of entry i (black past the
 * end), as r, g, b, a bytes: the layout of raylib's Color, so a renderer casts it to (const Color *) and converts nothing per
 * pixel.  The writer's lists of palettes (px_font_write_palettes) leave `draw` unset. */
typedef struct PxPalette { char hash[33]; int n; uint32_t *c; uint8_t draw[PX_NUM_PIXELS][4]; } PxPalette;

/* The optional font-wide integer metrics, in the order of the array below (not of the file). */
typedef enum PxMetric {
    PXM_ASCENT, PXM_DESCENT, PXM_LINEGAP, PXM_CAPHEIGHT, PXM_XHEIGHT, PXM_INTERNALLEADING, PXM_QUADWIDTH,
    PXM_UNDERLINEPOSITION, PXM_UNDERLINETHICKNESS, PXM_STRIKEOUTPOSITION, PXM_STRIKEOUTTHICKNESS,
    PXM_COUNT
} PxMetric;
const char *px_metric_key(PxMetric m);      /* "ASCENT", ... as written in a pxfm file */
const char *px_metric_label(PxMetric m);    /* "Ascent", "Line gap", ... for a UI */

typedef struct PxFont {
    bool has[PXM_COUNT];           /* the metric is stated (and written) */
    int m[PXM_COUNT];              /* its value (0 when not stated); ASCENT and DESCENT are never negative */
    bool hasRatio; int ratioA, ratioB;  /* RATIO a:b, both positive, coprime */
    bool monospaced;               /* MONOSPACED: what the source says */
    bool symbolic;                 /* SYMBOLIC: the source says it is a symbol font (code points of a private encoding) */
    int colorsRead;                /* the COLORS of the file the font was read from (0: none); the writer repeats it while a
                                      glyph with a palette is lazy (a parsed, unloaded font) */
    /* THE PALETTE TABLE.  Every palette the glyphs have, each content once (interned): a glyph names its entry by
     * PxGlyph.pi.  Entries never change: a glyph that takes another colour is pointed at another entry, found or added
     * (copy on write, px_glyph_add_color), so glyphs of the same colours share one.  pals[0] is the default palette (black)
     * while the table exists; it is empty (npals 0) while every glyph has the default.  An entry no glyph names any more
     * stays until px_font_compact_palettes (px_font_copy compacts its copy: undo snapshots), but the palettes the glyphs
     * were read with stay while a PxGlyph.pal names them (the writer reuses them). */
    PxPalette *pals;
    int npals, palCap;
    unsigned palGen;               /* new whenever an index of the table may name another palette (a compaction, a copy) */
    /* cache of px_font_color_list (mutable, rebuilt when the glyphs' palettes changed) */
    uint32_t *clist; int nclist, clistCap; unsigned clistKey; bool clistOk;
    PxGlyph *glyphs;               /* kept sorted by code (stable): code points, then the uncoded glyphs F00000.., then the default glyph FFFFFF */
    int count, capacity;
    PxKern *kern; int kernCount, kernCap;  /* kerning pairs (codes; an F-code names an uncoded glyph, FFFFFF the default glyph);
                                              the writer sorts them, drops value 0 and pairs that name an F-code no glyph has */
    char fileHash[33];             /* hash of the pxfm this font was read from ("" if built in memory) */
} PxFont;

/* ------------------------------------------------------------------ helpers */

int  px_pixel_index(char c);                 /* 0..36, or -1 if c is not a pixel character */
void px_sha256(const void *data, size_t len, uint8_t out[32]);
void px_hash128_hex(const void *data, size_t len, char out[33], bool upper); /* first 32 hex digits of SHA-256 */
/* Canonical form (split CRLF/CR/LF, strip trailing space/tab, drop empty lines, LF-terminate). malloc'd. */
char *px_canonical(const char *text, size_t len, size_t *outLen);
/* Create a directory and all missing parents ('/' or '\\' separators). True if it exists afterwards. */
bool px_mkdir_p(const char *path);
char *px_read_file(const char *path, size_t *len);      /* malloc'd, NUL-terminated; NULL on failure */
bool px_write_file(const char *path, const void *data, size_t len);
bool px_file_exists(const char *path);
bool px_is_hash(const char *s, bool upper, bool lower);  /* 32 hex digits of the allowed case(s) */

/* ------------------------------------------------------------------ font lifecycle */

void px_font_init(PxFont *f);                /* nothing stated, no palette, no glyphs, no default glyph */
void px_font_free(PxFont *f);
bool px_font_copy(PxFont *dst, const PxFont *src);      /* deep copy (lazy glyphs stay lazy); the copy's palette table is compacted */
void px_glyph_free(PxGlyph *g);
bool px_glyph_copy(PxGlyph *dst, const PxGlyph *src);   /* deep copy */
/* Index of the first glyph with this code (binary search), or -1. */
int  px_font_find(const PxFont *f, int32_t codepoint);
int  px_font_add_glyph(PxFont *f, const PxGlyph *g);    /* deep copies g (keeping g->code), inserts in order, returns new index */
/* Deep copies g and gives it the next code without a code point (F00000 + number of uncoded glyphs); returns its index. */
int  px_font_add_uncoded(PxFont *f, const PxGlyph *g);
int  px_font_uncoded_count(const PxFont *f);            /* number of glyphs without a code point */
/* Removes the glyph (the default glyph too).  Its kerning pairs go.  Removing an
 * UNCODED glyph also renumbers the later F-codes (code - 1): the glyphs and their kerning pairs. */
void px_font_remove_glyph(PxFont *f, int index);
void px_font_sort(PxFont *f);                           /* restore order (stable) */
int  px_font_default_index(const PxFont *f);            /* index of the default glyph (code FFFFFF), or -1 */
/* Makes glyph `src` the default: its bitmap, offsets and advance are copied into the FFFFFF glyph (made when missing).
 * The copy is independent from then on. */
bool px_font_set_default_from(PxFont *f, int src);
/* Set (or, value 0, remove) a kerning pair; keeps pairs unique. */
void px_font_set_kern(PxFont *f, int32_t first, int32_t second, int value);
int  px_font_kern(const PxFont *f, int32_t first, int32_t second);   /* 0 if none */
void px_font_remove_kern_of(PxFont *f, int32_t codepoint);           /* every pair that names it */

/* The font box: stated ASCENT / DESCENT, else derived from the glyphs: as many rows above the baseline as the
 * highest bitmap reaches (y + h) and below as the lowest reaches (-y), at least 0 (spec "Reading").  The derived
 * box is pxfe's own and is never written.  Returns true when the box is exact; false when a value had to be
 * derived while some glyph with ink is lazy (bitmap not loaded, so its top is unknown): the box is then built
 * from the loaded glyphs only.  (The lower edge is always exact, a lazy glyph still has y.) */
bool px_font_box(const PxFont *f, int *above, int *below);
/* A "no-ASCENT font" gets the same box as before as soon as ASCENT/DESCENT is stated, glyphs never move. */

/* ------------------------------------------------------------------ glyph helpers */

static inline int px_glyph_top(const PxGlyph *g)    { return g->y + g->h; }   /* rows from baseline up to the top edge */
static inline int px_glyph_bottom(const PxGlyph *g) { return g->y; }
static inline int px_glyph_box_row(const PxGlyph *g, int above) { return above - (g->y + g->h); } /* row of the box where the bitmap starts */
static inline void px_glyph_set_top(PxGlyph *g, int top) { g->y = top - g->h; }
char px_glyph_get(const PxGlyph *g, int x, int y);      /* '.' outside the bitmap; (x, y) are bitmap columns/rows from the top-left */
void px_glyph_touch(PxGlyph *g);                        /* pixels or size changed: invalidate the cached hash */
/* Crop the bitmap to inked pixels, moving x, y.  Empty result => w=h=0, pix=NULL, x=y=0.  Clears asDrawn. */
void px_glyph_crop(PxGlyph *g);
/* Hash (32 lower-case hex) of the pxgl text the glyph is written as (cropped, or as drawn); out "" if no ink.
 * Cached; does nothing slow after the first call (a mutable cache: not thread-safe on one glyph). */
void px_glyph_hash(const PxGlyph *g, char out[33]);
/* The pxgl file text of the bitmap as written (cropped, or as drawn; rows ended with LF) and its 32-digit lower-case
 * hash; NULL (hash "") when it has no ink.  malloc'd; *len gets the length. */
char *px_glyph_pxgl(const PxGlyph *g, size_t *len, char hash[33]);
/* The offsets the glyph is written with (those of the cropped bitmap), (0,0) when no ink. */
void px_glyph_written_offset(const PxGlyph *g, int *x, int *y);
/* Same shape test: same cropped/as-drawn bitmap (ignores position/advance/codepoint). */
bool px_glyph_same_shape(const PxGlyph *a, const PxGlyph *b);

/* ------------------------------------------------------------------ I/O
 * Library layout: <root>/pxfm/<h2>/<hash>.pxfm and <root>/pxgl/<h2>/<hash>.pxgl (and pxtf/<h2>/<hash>.pxtf).
 * px_library_root_of: given a path to a file inside a library, derive <root> (three separators up). */
bool px_library_root_of(const char *filePath, char *root, size_t rootSize);

/* Serialize to the canonical pxfm bytes (spec order, deduped, no-ink glyphs without hash, 0 kerning dropped; a colour font
 * gets COLORS and a palette hash on every glyph line with a glyph file, see PALETTES below).  Returns malloc'd text;
 * *hashOut gets the 32 lower-case hex digits.  Lazy glyphs are written from their cached hash and palette hash. */
char *px_pxfm_text(const PxFont *f, size_t *len, char hashOut[33]);
/* The canonical pxfm hash of the font's current state: change detection compares it with the hash the font was
 * opened with (px_font_hash == f->fileHash right after reading).  About one SHA-256 pass over the text. */
void px_font_hash(const PxFont *f, char out[33]);

/* ------------------------------------------------------------------ colours: hex palettes, canonical glyphs, COLORS
 * PALETTES.  On READ the hex palettes the glyph lines name are put in the font's table (pals), whole (all their lines), and
 * each glyph is pointed at the palette it was read with: the canonical pixels of its file are already its own characters,
 * so nothing is mapped.  A colour font is loaded whole, so that COLORS can be checked.  A font has any number of colours;
 * a glyph at most 36 (its ink characters).
 * On WRITE every glyph is renumbered in reading order.  Its palette is the one it was read with while that palette's first
 * k lines are its colours in that order (so an unchanged font writes the same bytes, also when a palette is shared and longer
 * than a glyph needs).  Else it is made as pixelglyphlib's store.canonical_pixels permutes a glyph's palette: the glyph's
 * colours in their new order, then the other entries of its palette in memory in their order, without repeats (the glyph view
 * drops the unused slots it made before a save, see CANONICAL IN MEMORY).  COLORS is the
 * number of different colours the glyphs use; a font of one colour writes no colour at all (its pixels are '0': black).
 * The FONT'S COLOUR LIST (px_font_color_list, the ink chooser) is the distinct colours of the glyphs' palettes other than the
 * default one, glyph by glyph in the font's order and entry by entry; just black when there is none. */

#define PX_GLYPH_MAX_COLORS 36

/* A hex palette file, read strictly (hex.md "Errors": ASCII, RRGGBB or RRGGBBAA in upper case with no alpha FF, at least one
 * line, no empty line, no colour twice; line ends and trailing blanks as in the canonical form).  *colors is malloc'd. */
bool px_hex_parse(const char *data, size_t len, uint32_t **colors, int *n, char err[PX_ERR_LEN]);
/* The bytes of a palette file (a colour a line, LF) and its hash (lower case); NULL when it is no palette (none, a colour twice). */
char *px_hex_text(const uint32_t *colors, int n, size_t *len, char hash[33]);
/* Whether n pixel characters ('.' empty) are numbered in reading order from '0' (pxgl.md "Canonical colours"). */
bool px_pixels_canonical(const char *pix, size_t n);
/* Puts a palette in the font's table, its content once: its index (>= 1), or -1 (out of memory, n < 1).  The hash of an entry
 * is that of its colours: px_font_add_palette's `hash` (of a palette read) is not needed and not checked. */
int  px_font_intern_palette(PxFont *f, const uint32_t *colors, int n);
int  px_font_add_palette(PxFont *f, const char *hash, const uint32_t *colors, int n);
/* The entry of the table whose hash this is (a palette the glyphs were read with), or NULL. */
const PxPalette *px_font_find_palette(const PxFont *f, const char *hash);
/* The palette of a glyph (never NULL: the default palette when pi is 0 or out of range). */
const PxPalette *px_glyph_palette(const PxFont *f, const PxGlyph *g);
/* The colour of a pixel character of the glyph (0 for '.', black past its palette). */
uint32_t px_glyph_color(const PxFont *f, const PxGlyph *g, char c);
/* The entry (0..35) of the glyph's palette that has this colour, or -1. */
int  px_glyph_find_color(const PxFont *f, const PxGlyph *g, uint32_t color);
/* The entry (0..35) the glyph draws a colour with: its own when it has it, else the colour takes a new entry at the end (version 23:
 * unused entries are kept), or, when the palette has 36 entries, the first one no pixel uses; the glyph is pointed at the palette so
 * made (copy on write: found or added in the table).  A glyph of the default palette is given a palette of its own (black too, unless
 * it has no ink: then the new colour alone).  PX_COLOR_FULL when its 36 entries are all in use by its pixels, -1 on out of memory. */
#define PX_COLOR_FULL (-2)
int  px_glyph_add_color(PxFont *f, PxGlyph *g, uint32_t color);
/* The same for n colours at once (pasted pixels): slots[i] gets the entry of colors[i]; a new colour never takes the entry another
 * of them got.  0, or PX_COLOR_FULL (nothing changed) when they do not all fit, -1 on out of memory.  px_glyph_colors_fit only
 * tells whether they would. */
int  px_glyph_add_colors(PxFont *f, PxGlyph *g, const uint32_t *colors, int n, int *slots);
bool px_glyph_colors_fit(const PxFont *f, const PxGlyph *g, const uint32_t *colors, int n);
/* Points the glyph at the palette of these colours (found or added); false on out of memory. */
bool px_glyph_set_palette(PxFont *f, PxGlyph *g, const uint32_t *colors, int n);
/* The glyph's pixels are characters whose colours charColor gives (indexed by px_pixel_index, like a font-wide palette): they
 * are renumbered in reading order, each colour once, and the glyph gets the palette of those colours.  A glyph without ink
 * keeps its palette.  For importers, old drafts and tests. */
bool px_glyph_set_colors(PxFont *f, PxGlyph *g, const uint32_t charColor[PX_NUM_PIXELS]);
/* CANONICAL IN MEMORY (version 23).  The glyph view keeps the glyph it edits canonical after every edit: its pixels numbered in
 * reading order and its palette in the same order, so entry i is the colour of pixel character i; the entries no pixel uses (the
 * view's "unused slots": a colour added with "+", all its pixels erased, the rest of a longer palette it was read with) follow
 * the used ones in their order.  The glyph view drops the unused entries it made when it leaves the glyph or the font is saved
 * (Editor_Flush), so a file gets them only when the palette it was read with (or one shared with other glyphs) is kept.
 * px_glyph_canonicalize renumbers pixels and palette so (no change, false, when they are so already; a colour twice in the palette
 * becomes one entry); px_glyph_used_colors is the number of colours the pixels use (k: entries 0..k-1 of a canonical glyph).
 * px_glyph_add_slot appends a colour as an unused entry: its entry, the existing one when the glyph has it, PX_COLOR_FULL at 36 entries.
 * px_glyph_replace_color gives entry `slot` another colour (every pixel of it changes); a colour the glyph has already in entry j:
 * the pixels of j join the slot and entry j goes; then canonical; 0, -1 for a bad slot or out of memory.
 * px_glyph_trim_palette drops the unused entries (an empty glyph goes back to the default palette); true when it changed. */
bool px_glyph_canonicalize(PxFont *f, PxGlyph *g);
int  px_glyph_used_colors(const PxGlyph *g);
int  px_glyph_add_slot(PxFont *f, PxGlyph *g, uint32_t color);
int  px_glyph_replace_color(PxFont *f, PxGlyph *g, int slot, uint32_t color);
bool px_glyph_trim_palette(PxFont *f, PxGlyph *g);
/* The font's colour list (see PALETTES): *out points into the font's cache, valid until the font changes; its length (>= 1). */
int  px_font_color_list(const PxFont *f, const uint32_t **out);
/* Index of a colour in the font's colour list, or -1. */
int  px_font_color_index(const PxFont *f, uint32_t color);
/* A colour font: its colour list is more than black alone (or it was read with COLORS and is not loaded).  A sample draws a
 * font that is not one in the theme's ink. */
bool px_font_is_colour(const PxFont *f);
/* Drops the table entries no glyph names (by pi, or by pal: a palette read) and renumbers pi. */
void px_font_compact_palettes(PxFont *f);
/* After the palettes the glyph lines name were added and before any bitmap is loaded: each glyph is pointed at its palette.
 * An error when a glyph names a palette the font has not got. */
bool px_font_adopt_palettes(PxFont *f, char err[PX_ERR_LEN]);
/* True when some glyph line names a palette (a colour font as read): its loaders load every bitmap at once. */
bool px_font_has_glyph_palettes(const PxFont *f);
/* A bitmap just loaded in the canonical numbering of its file: checked (reading order, a palette long enough, a glyph of
 * several colours only with a palette); the colour cache is filled.  The caller has set pix, w, h, hash and hashValid. */
bool px_glyph_from_canonical(PxFont *f, PxGlyph *g, char err[PX_ERR_LEN]);
/* When no glyph is lazy any more: the COLORS read is checked against the colours the glyphs use.  Nothing while a glyph is lazy. */
bool px_font_finish_palette(PxFont *f, char err[PX_ERR_LEN]);
/* The COLORS the writer writes: the different colours the glyphs use (any number), 0 for a font of one colour (or none). */
int  px_font_colors(const PxFont *f);
/* The palettes the writer names (each once), with their colours: what px_save_final writes and the store inserts.
 * *out is malloc'd (free with px_palettes_free); returns the count, -1 on out of memory. */
int  px_font_write_palettes(const PxFont *f, PxPalette **out);
void px_palettes_free(PxPalette *p, int n);
/* The pxgl text of a glyph as the font writes it (canonical; a font of one colour writes every ink pixel '0') and its hash;
 * NULL (hash "") when it has no ink or is lazy. */
char *px_font_glyph_pxgl(const PxFont *f, const PxGlyph *g, size_t *len, char hash[33]);

/* Strict pxfm v2 parsing of a text (the rules of "Errors" in pxfm.md: codes, F-codes without gaps, kerning of F-codes
 * that exist, 110000..EFFFFF rejected, no `-` or `_DEF`, no two glyph lines of one code, COLORS and the palette fields).
 * `where` is only for messages.  Glyphs are lazy, with their palette hashes (the palettes are not read).  A version 1 file
 * gives the error "pxfm version 1 file", a COLOR line (colours before hex palettes) its own error.  Line ORDER is not checked
 * here (the store builder compares with the writer's text). */
bool px_parse_pxfm(const char *data, size_t len, const char *where, PxFont *out, char err[PX_ERR_LEN]);
/* Read a pxfm without loading any bitmap: glyphs keep their hash (lazy).  Sets out->fileHash. */
bool px_read_pxfm_lines(const char *path, PxFont *out, char err[PX_ERR_LEN]);
/* Load the bitmaps of the lazy glyphs whose code point is in cps[0..n) from <libRoot>/pxgl (cps NULL: all of
 * them, in parallel when many; a glyph without code point loads only in that case).  Each distinct pxgl file is
 * read once and checked (ASCII, pixels, equal widths, ink, colours in reading order, hash == file name).  The first call
 * reads the hex palettes the glyph lines name from <libRoot>/palette; a colour font loads every bitmap whatever cps says.
 * When no glyph is lazy any more the colours are checked as the spec says.  Thread-safe with respect to DIFFERENT fonts. */
bool px_load_glyphs(PxFont *f, const char *libRoot, const int32_t *cps, int n, char err[PX_ERR_LEN]);
/* Full read = px_read_pxfm_lines + px_load_glyphs(all).  libRoot NULL: found from the path. */
bool px_read_pxfm(const char *path, const char *libRoot, PxFont *out, char err[PX_ERR_LEN]);

/* Final save: writes every glyph bitmap to <libRoot>/pxgl/xx/<hash>.pxgl if missing, every palette to
 * <libRoot>/palette/xx/<hash>.hex if missing and the pxfm to
 * <libRoot>/pxfm/xx/<hash>.pxfm if missing (an existing file is never touched: same name, same content).
 * Plain writer: the caller (the GUI) decides whether the hash differs from the original's.  Lazy glyphs are not
 * written (their pxgl is assumed to exist).  Returns the font hash and the pxfm path. */
bool px_save_final(const PxFont *f, const char *libRoot, char hashOut[33], char *pathOut, size_t pathSize,
                   char err[PX_ERR_LEN]);

/* ------------------------------------------------------------------ drafts (.pxfd, version 3)
 * Self-contained text file, bitmaps uncropped exactly as edited, no library needed:
 *   PXFEDRAFT	3                     (version 2 drafts, made before the pxtf / F-code change, are refused)
 *   NAME	<display name, pxtf text escapes>
 *   ORIGINALHASH	<original pxfm hash, upper case>          (absent: none)
 *   ORIGINALPXTF	<original pxtf hash>	<variant number>    (absent: a font without typeface)
 *   PXTF	<one line of the duplicated typeface's canonical text, escaped>   (one per line; absent: none)
 *   <metric lines as in pxfm: ASCENT .. XHEIGHT, RATIO, MONOSPACED, SYMBOLIC, KERNING (codes, F-codes allowed)>
 *   COLOR	<pixel>	<RRGGBBAA>       (only in drafts of pxfe 21, its font-wide palette: the glyphs of such a draft are
 *                                    given palettes of their own when it is loaded; the line for '.' is ignored)
 *   PALETTE	<HASH>	<RRGGBBAA,...>  (a palette the glyphs were read with, its colours in order)
 *   DEFAULT	<code>                  (default glyph, absent: none)
 *   DANGLINGDEF	<x,y>	<advance>[	<hash>]
 *   GLYPH	<code, 6 hex digits>	<x>,<y>	<advance>	<w>x<h>	<0|1 asDrawn>[	<HASH of the palette it was read with, or ->[	<RRGGBBAA,...>]]
 *                                   (the last field: the glyph's palette in memory; absent: the default palette)
 *   	<row>  (h rows, tab-indented; none when w==h==0)
 * Glyph blocks keep the glyph array order.  The hashes in a draft are upper case. */
struct PxtfTypeface;
typedef struct PxDraftInfo {
    char name[256];                /* display name (UTF-8) */
    char originalHash[33];         /* lower-case pxfm hash of the original font, "" none */
    char originalPxtfHash[33];     /* lower-case pxtf hash of the original typeface, "" none */
    int  originalVariant;          /* variant number in the original typeface (0 none) */
    struct PxtfTypeface *typeface; /* the duplicated typeface (edited copy; owned by the info), or NULL */
} PxDraftInfo;
void px_draft_info_init(PxDraftInfo *info);
void px_draft_info_free(PxDraftInfo *info);             /* frees the typeface */
bool px_save_draft(const PxFont *f, const PxDraftInfo *info, const char *path, char err[PX_ERR_LEN]);
/* Loads font and info (info->typeface is allocated when the draft has one); a lazy glyph cannot be saved. */
bool px_load_draft(const char *path, PxFont *out, PxDraftInfo *info, char err[PX_ERR_LEN]);

#endif
