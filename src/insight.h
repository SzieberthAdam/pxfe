/* insight.h - side-panel sections that query the store (DESIGN.md "Version 3"):
 * "Glyph usage" (which fonts / typefaces hold the selected glyph bitmaps) and
 * "Similar fonts" / "Similar typefaces". Queries run on a worker thread with its own StoreConn;
 * the draw call never blocks. */
#ifndef INSIGHT_H
#define INSIGHT_H

#include "raylib.h"
#include "store.h"

typedef struct Insight Insight; /* opaque, defined in insight.c */

typedef enum {
    INSIGHT_NONE = 0,
    INSIGHT_OPEN_FONT,      /* open target.pxfm read-only, select target.code if it is >= 0 */
    INSIGHT_OPEN_TYPEFACE,  /* open target.pxtf variant target.variant (or its first) read-only */
    INSIGHT_FIND_CODE       /* open the glyph finder on target.code */
} InsightAction;

typedef struct InsightTarget {
    InsightAction action;
    char pxfm[33];
    char pxtf[33];
    int variant;
    int32_t code;
} InsightTarget;

Insight *Insight_Create(Store *store);
void     Insight_Destroy(Insight *in);

/* Set what the panel is about; cheap to call every frame (only a change starts a query).
 * Glyph usage: the pxgl hashes (lower case) of the selected glyphs, n == 0 for none.
 * Similar: the current font's pxfm hash and, when it belongs to a typeface, its pxtf hash ("" or NULL otherwise). */
void     Insight_SetGlyphs(Insight *in, const char (*glyphHashes)[33], int n);
void     Insight_SetFont(Insight *in, const char *pxfmHash, const char *pxtfHash);

/* Draw the sections (the usage rows register as usage_row_N, the similar rows as similar_row_N) into `area` (a column of the right panel; the panel scrolls inside it).
 * Returns the height used. When the user clicks an entry, returns true in *clicked and fills *target. */
typedef enum { INSIGHT_SECTION_USAGE = 1, INSIGHT_SECTION_SIMILAR_FONTS = 2, INSIGHT_SECTION_SIMILAR_TYPEFACES = 4 } InsightSections;
float    Insight_Draw(Insight *in, Rectangle area, int sections, bool *clicked, InsightTarget *target);

/* Additions (not in the first version of this header).
 * Insight_SetVisible: for the NEXT Insight_Draw only: the visible part of the panel when `area` is scrolled (area.y may be
 *   above the visible rectangle); rows outside it are not drawn and cannot be hovered.  The caller keeps its own scissor
 *   (BeginScissorMode) around the call, as the right panel does for its other sections.  Without it `area` is visible.
 * Insight_BestUsage: the font that uses the glyph for the most codes (the "most used font" of the finder), when the usage
 *   of exactly this single glyph has been delivered; target gets INSIGHT_OPEN_FONT with that font and code. */
void     Insight_SetVisible(Insight *in, Rectangle visible);
/* Insight_SetPreferredCode: for a single glyph, clicking a usage row (and BestUsage) selects this code when the font has the
 *   glyph under it (the finder passes its code point), else the first code the font uses the glyph for.  -1 (default): none. */
void     Insight_SetPreferredCode(Insight *in, int32_t code);
bool     Insight_BestUsage(Insight *in, const char *glyphHash, InsightTarget *target);

/* Test hooks for scripted input: state of the last finished queries. */
int      Insight_UsageFontCount(const Insight *in);      /* -1 while loading */
int      Insight_SimilarCount(const Insight *in);        /* -1 while loading */
bool     Insight_Busy(const Insight *in);
int      Insight_UsageVariantCount(const Insight *in);   /* typeface variants (pxtf rows) naming a font that holds the glyph; -1 loading */
int      Insight_UsageCodeFonts(const Insight *in);      /* fonts holding the single glyph under the preferred code; -1 when none */

#endif
