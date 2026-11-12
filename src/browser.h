/* browser.h - the library browser view of pxfe (see DESIGN.md, "Library browser"). */
#ifndef BROWSER_H
#define BROWSER_H

#include "raylib.h"
#include "library.h"
#include "insight.h"

typedef struct Browser Browser; /* opaque, defined in browser.c */

typedef enum {
    BROWSER_NONE = 0,
    BROWSER_OPEN,       /* open the chosen entry read-only (original) */
    BROWSER_DUPLICATE,  /* open a duplicate of the chosen entry for editing */
    BROWSER_INSIGHT,    /* the user clicked an entry of the "Similar" list: see BrowserChoice.insight */
    BROWSER_DELETE_FONT,     /* delete the font choice.pxfm (Del, context menu); main.c confirms and deletes */
    BROWSER_DELETE_TYPEFACE  /* delete the typeface choice.pxtf (box header or a variant row) */
} BrowserAction;

typedef struct BrowserChoice {
    BrowserAction action;
    LibKind kind;          /* kind of the chosen entry */
    char pxfm[33];         /* pxfm hash (lower case) */
    char pxtf[33];         /* typeface hash, "" if none */
    int variant;           /* typeface variant number, 0 if none */
    InsightTarget insight; /* BROWSER_INSIGHT only */
} BrowserChoice;

/* Create on an open store (lists it, starts the loader).  The store must outlive the browser.
 * `insight` (may be NULL) draws the "Similar fonts / typefaces" section of the side panel.  NULL with err on failure. */
Browser *Browser_Create(Store *store, Insight *insight, char err[PX_ERR_LEN]);
void     Browser_Destroy(Browser *b);
const char *Browser_Root(const Browser *b);
/* The index of the store (typeface variants first, then fonts): main.c finds the typeface of a font in it. */
const Library *Browser_Lib(const Browser *b);

/* Rescan the library (after a commit, a deletion or a load), keeping filters, scroll and selection where possible. */
void     Browser_Rescan(Browser *b);

/* Per frame, between BeginDrawing/EndDrawing, inside `area`; owns all input inside it.
 * Returns true and fills *choice when the user opened or duplicated an entry. */
bool     Browser_UpdateDraw(Browser *b, Rectangle area, BrowserChoice *choice);

/* View settings are loaded from / saved to an ini file (pxfe.ini beside the exe), section [browser]. */
void     Browser_LoadSettings(Browser *b, const char *iniPath);
void     Browser_SaveSettings(const Browser *b, const char *iniPath);

/* Test hooks for scripted input (--play): screen centre of the card of the n-th visible entry, entry counts. */
bool     Browser_CardCenter(const Browser *b, int visibleIndex, Vector2 *out);
int      Browser_VisibleCount(const Browser *b);   /* entries after filtering */
int      Browser_TotalCount(const Browser *b);     /* entries of the current source tab */

/* Where the text fields get typed characters from (default GetCharPressed). */
void     Browser_SetCharSource(int (*fn)(void));

/* ---- additions (state hooks for the demo harness / scripted tests) ---- */
void     Browser_SetTab(Browser *b, int tab);          /* 0 Typefaces, 1 Fonts */
void     Browser_SetFilter(Browser *b, const char *text);
void     Browser_SetSample(Browser *b, const char *utf8);
void     Browser_SetZoom(Browser *b, int zoom);        /* 1..8 */
void     Browser_SetColumns(Browser *b, int cols);     /* 0 = Auto, 1..8 */
void     Browser_ScrollBy(Browser *b, float pixels);   /* positive: down */
void     Browser_SelectVisible(Browser *b, int visibleIndex);
int      Browser_Tab(const Browser *b);
int      Browser_SideTab(const Browser *b);   /* 0 View, 1 Similar */
int      Browser_Zoom(const Browser *b);
int      Browser_Columns(const Browser *b);
const char *Browser_FilterText(const Browser *b);
const char *Browser_SampleText(const Browser *b);
int      Browser_SelectedVisible(const Browser *b);       /* -1 when none is visible */
bool     Browser_SelectedPxfm(const Browser *b, char out[33]);
int      Browser_Pending(const Browser *b);            /* font loads still queued or running */
int      Browser_ReadyVisible(const Browser *b);       /* cards drawn with their sample in the last frame */

/* Frame time of the last 120 frames in ms: the CPU time of Browser_UpdateDraw and the whole frame (GetFrameTime). */
void     Browser_FrameStats(const Browser *b, float *updAvg, float *updMax, float *frameAvg, float *frameMax);
int      Browser_CardInkLines(const Browser *b, int visibleIndex);   /* lines of that entry's laid-out sample that show ink; -1 until laid out */

/* Typefaces tab (one box per typeface): hooks for scripts. */
int      Browser_TypefaceTotal(const Browser *b);               /* typefaces of the library (the badge of the Typefaces tab), whatever the filters */
int      Browser_BoxCount(const Browser *b);                    /* boxes of the visible list (0 in the other tabs) */
int      Browser_BoxVariants(const Browser *b, int box);        /* variants shown in a box, -1 when there is none */
bool     Browser_SelectedIsHeader(const Browser *b);            /* the box header (the typeface) is selected, not a variant row */
bool     Browser_TypefaceHeaderCenter(const Browser *b, int box, Vector2 *out);   /* screen centre of the left part of a box header */
/* The last choice the browser returned (also delete requests): the action (BrowserAction), pxfm and pxtf. */
int      Browser_LastAction(const Browser *b);
const char *Browser_LastPxfm(const Browser *b);
const char *Browser_LastPxtf(const Browser *b);

#endif
