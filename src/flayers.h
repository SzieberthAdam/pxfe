/* flayers.h - FONT LAYERS: reference layers that belong to a font, so that every glyph of the font offers another font's drawing
 * of the same code point (DESIGN.md "Font layers").
 *
 * A font layer is persisted in the settings sidecar (fsettings.h), keyed by the pxfm hash of the edited font.  A FlDoc holds the font
 * layers of the font that is open; main.c loads it when a font is opened and stores it again when it changed (see main.c "Font layers").
 *
 * SOURCES.  kind 0: a font of the database (source = its pxfm hash; drawn by layers.c as a pixel layer, no size).  kind 1: a file
 * (source = path): an OUTLINE font (outline.h: .ttf .otf .ttc .woff .eot .pfb) or a BITMAP font (bmsrc.h: .bdf .pcf .fnt .fon .json and
 * the bitmap strikes of .ttf/.otf).  `face` selects the .ttc face, the .fon font or the strike.  A .ttf/.otf that has outlines AND strikes
 * is read as outlines unless face >= FL_BM_FLAG, then the strike is face - FL_BM_FLAG (a file with no outline for the strike's glyphs
 * is read as bitmap anyway).
 *
 * PLACEMENT.  The source glyph's baseline origin (pen x = 0, baseline y = 0) sits on the edited glyph's origin; the layer's (dx, dy) in
 * pixels of the edited font (y up) moves it, and a per-glyph correction (dx, dy, dsize, hidden) adds to that.
 * SIZE.  Outline: a layer with s.em > 0 (an imported font's source, DESIGN.md "Importing a font file") is drawn at  em * dsize  pixels per em and
 * s.aspect times as wide; else the glyph is drawn at  fontPx * scale * dsize  pixels per em, fontPx = the edited font's pixel size = its ascent +
 * descent (px_font_box), scale 1.0 = natural size.  Bitmap: whole-pixel multiples only: the strike's pixels are drawn k x k edited
 * pixels, k = round(scale * dsize) clamped to 1 .. FL_MAX_K; scale 1 is the strike's own size (no shrinking).  Database layers: none.
 *
 * Outline glyphs are drawn smooth: rasterised at (size * canvas zoom) with outline_rasterize, uploaded as a texture lazily and kept in a
 * bounded cache (FL_CACHE_N entries, FL_CACHE_PIX pixels).  Needs raylib. */
#ifndef FLAYERS_H
#define FLAYERS_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"
#include "bmsrc.h"
#include "fit.h"
#include "fsettings.h"
#include "outline.h"

#define FL_MAX 24
#define FL_BM_FLAG 0x10000
#define FL_MAX_K 16
#define FL_CACHE_N 64
#define FL_CACHE_PIX (8 * 1000 * 1000)
#define FL_MAX_EM 1600.0f        /* the biggest em (size * zoom) that is rasterised; above it the layer is not drawn */

typedef struct FlCorr { PxCode code; float dx, dy, dsize; int hidden; } FlCorr;

enum { FL_UNOPENED = 0, FL_OK = 1, FL_FAILED = -1 };

typedef struct FlLayer {
    FsLayer s;                   /* what is persisted (id and font unused here) */
    FlCorr *corr; int ncorr, capcorr;   /* the per-glyph corrections, sorted by code, all differ from the defaults */
    int serial;                  /* unique in the run: the key of the texture cache */
    int origin;                  /* a copy (FlLayer_Copy): the serial of the layer it was copied from, else 0 */
    int state;                   /* FL_*: the file is opened lazily */
    char err[160];               /* why FL_FAILED ("file missing", ...) */
    bool outline;                /* FL_OK: an outline source (else bitmap) */
    bool canOutline, canBitmap;  /* FL_OK: what the file holds (both: a .ttf with strikes) */
    int nFaces;                  /* FL_OK: faces (outline) or strikes (bitmap) the file offers for the chosen mode */
    int strike;                  /* FL_OK, bitmap: the strike drawn */
    Outline *ol;
    BmSource *bm;
    bool metaChanged;            /* the stored size / mtime was updated by a reload: the owner persists it */
    double checked;              /* GetTime() of the last look at the file's size and time */
    struct { bool valid; PxCode code; FitParams p; FitCorr c; float shift; } snap;   /* the last answer of FlLayer_Shift */
} FlLayer;

/* one document's font layers, in stack order (0 = top); rev grows with every change that has to be persisted */
typedef struct FlDoc { FlLayer *v[FL_MAX]; int n; unsigned rev; } FlDoc;

FlDoc *FlDoc_New(void);
void   FlDoc_Free(FlDoc *d);
void   FlDoc_Clear(FlDoc *d);
/* Replaces the layers by those the settings hold for the font (hash); false on a database error. */
bool   FlDoc_Load(FlDoc *d, FSettings *fs, const char *hash);
/* Replaces what the settings hold for hash by the layers (nothing is written when both are empty). */
bool   FlDoc_Store(const FlDoc *d, FSettings *fs, const char *hash);
bool   FlDoc_Add(FlDoc *d, FlLayer *l, int pos);   /* takes l; pos < 0 or too big: the bottom */

/* A layer from a settings record (copied); NULL when out of memory.  Free with FlLayer_Free (the document does it for its own). */
FlLayer *FlLayer_New(const FsLayer *init);
void     FlLayer_Free(FlLayer *l);
FlLayer *FlLayer_Copy(const FlLayer *l);

/* Opens the file of a kind-1 layer when that has not been tried (and reopens it when its size or time changed: the stored size / time are
 * updated silently, metaChanged is set).  True when the source is usable. */
bool FlLayer_Ready(FlLayer *l);
/* Fills in a new layer's label from the source (family and style, strike name) */
void FlLayer_Label(FlLayer *l, char *out, int n);
/* Reopens with another face / mode (the probe of the Add Layer popover): false when it fails (the layer then shows err). */
bool FlLayer_SetFace(FlLayer *l, int face);

/* corrections: the stored one for a code (NULL: none), set / remove (all defaults remove) */
const FlCorr *FlLayer_Corr(const FlLayer *l, PxCode code);
void          FlLayer_SetCorr(FlLayer *l, PxCode code, const FlCorr *c);

typedef struct FlPlace { float dx, dy; float scale; bool hidden; float dsize; } FlPlace;   /* dsize: the correction's factor alone */
/* Where and how big for a glyph: the layer's offset and scale with the code's correction.  scale is the effective one (layer scale *
 * dsize; for a bitmap source a whole number >= 1).  Pure. */
void FlLayer_Place(const FlLayer *l, PxCode code, FlPlace *out);
/* The size an outline layer is drawn with, in pixels per em of the edited font: its own em or the font box times its scale, times the
 * correction of the place (p may be NULL). */
float FlLayer_Em(const FlLayer *l, int fontPx, const FlPlace *p);
/* The placement of an outline layer as fit.h draws pixels from it (without the correction of a glyph), and the correction of a code
 * (false: it has none; out then holds the defaults). */
void FlLayer_FitParams(const FlLayer *l, int fontPx, FitParams *out);
bool FlLayer_FitCorr(const FlLayer *l, PxCode code, FitCorr *out);
/* A layer the glyphs are drawn from in smooth mode moves each glyph by a quarter pixel step to meet the pixels (fit.h): how far, in
 * pixels of the edited font, for this code (0 for every other layer).  The outline is drawn moved by as much.  Needs FlLayer_Ready. */
float FlLayer_Shift(FlLayer *l, PxCode code, int fontPx);
/* Whole number for a bitmap scale */
int  fl_bitmap_k(float scale);

/* -1 the source has no glyph for the code, 0 it has one without ink, 1 it has ink (needs FlLayer_Ready) */
int FlLayer_Has(FlLayer *l, PxCode code, int fontPx);
/* Drawn pixels of the glyph at its natural size (for the tests), -1 not in font */
int FlLayer_InkCount(FlLayer *l, PxCode code, int fontPx);
/* Draws the glyph.  (ox, oy): screen position of the glyph origin (the pen on the baseline), z: screen pixels per edited pixel (the
 * canvas zoom); fontPx: the edited font's pixel size.  tint: the colour with the alpha of the layer's opacity. */
void FlLayer_Draw(FlLayer *l, PxCode code, int ox, int oy, int z, int fontPx, const FlPlace *p, Color tint);

/* the texture cache (test hook): entries in it */
int  FlCache_Count(void);
void FlCache_Clear(void);

/* Which file extensions the Add Layer popover offers: the filter for the file dialog */
#define FL_FILE_FILTER "Font files|*.ttf;*.otf;*.ttc;*.woff;*.eot;*.pfb;*.bdf;*.pcf;*.fnt;*.fon;*.json|All files|*.*"

#endif
