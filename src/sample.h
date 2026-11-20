/* sample.h - the shared sample text of pxfe and its renderer (see DESIGN.md, "Samples").
 *
 * One sample text (UTF-8, several lines) is shared by the library browser, the font view's sample strip and (later) the
 * glyph view's preview.  It is kept in pxfe.ini, [app] sample= with the new lines written as \n (and a backslash as \\).
 *
 *   - the setting:       Sample_Get / Sample_Set / Sample_Counter, Sample_LoadIni (migrates the old [browser] sample)
 *   - the renderer:      Sample_Layout + Sample_Render (a 1x RGBA bitmap, for callers that cache their own texture, like
 *                        the browser's cards) and Sample_Draw (immediate: layout, cached texture, pixel-perfect integer
 *                        zoom, clipping with a fade at the right edge); Sample_Measure
 *   - the editor widget: SampleEdit_Do, a multi-line text box on the shared text (Enter = new line, Ctrl+Enter or a click
 *                        outside commits, Ctrl+V pastes lines, caret up / down)
 *   - the strip:         SampleStrip_*, the font view's collapsible, resizable sample strip.
 */
#ifndef SAMPLE_H
#define SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"
#include "pxfont.h"

#define SAMPLE_MAX        1500      /* bytes of the sample text */
#define SAMPLE_MAX_LINES  12

/* ---------------------------------------------------------------------------------------- the setting */

const char *Sample_Get(void);                 /* never NULL; lines are separated by '\n' */
void        Sample_Set(const char *utf8);     /* normalises (\r\n -> \n, control characters dropped, length limits) */
const char *Sample_Custom(void);              /* the text as the user edited it last (the Custom preset; [app] sampleCustom) */
unsigned    Sample_Counter(void);             /* increments on every change: callers re-layout when it differs */
int         Sample_LineCount(const char *text);
void        Sample_LoadIni(const char *iniPath);   /* also remembers the path for saving */
void        Sample_Flush(void);               /* writes [app] sample= now when it changed */
void        Sample_Tick(void);                /* per frame: writes a pending change after a short quiet time */
void        Sample_SetCharSource(int (*fn)(void)); /* where typed characters come from (default GetCharPressed) */

/* Generic helpers on pxfe.ini, for [section] key=value lines (values are written as given). */
bool SampleIni_Get(const char *path, const char *section, const char *key, char *out, size_t cap);
void SampleIni_Set(const char *path, const char *section, const char *key, const char *val);

/* ---------------------------------------------------------------------------------------- layout and rendering */

typedef struct SampleRun { int gi; int x; int w, h; } SampleRun;   /* gi >= 0 glyph, -1 hollow box, -2 gap only */

typedef struct SampleLayout {
    SampleRun *runs; int nrun;
    int lineStart[SAMPLE_MAX_LINES + 1];  /* runs of line i: lineStart[i] .. lineStart[i+1] */
    int lineW[SAMPLE_MAX_LINES];          /* width of every line in font pixels */
    int nlines;
    int width;                            /* the widest line */
    int above, below;                     /* the font box (rows over / under the baseline) */
    int lineH;                            /* distance between two baselines: above + below + line gap */
    PxCode wantDefault;                   /* a default glyph whose bitmap is not loaded yet (the caller requests it), else PX_NO_CODE */
} SampleLayout;

/* Lays `text` out in font f.  boxAbove / boxBelow: the font box (both 0: px_font_box of f).  lineGap: pixels between the
 * boxes of two lines (-1: the font's LINEGAP when stated, else 1).  maxLines 0: no limit (SAMPLE_MAX_LINES).
 * Kerning is applied, a missing character is drawn as the font's default glyph or a hollow box.  Returns the number of lines. */
int  Sample_Layout(const PxFont *f, const char *text, int boxAbove, int boxBelow, int lineGap, int maxLines, SampleLayout *out);
void Sample_LayoutFree(SampleLayout *L);
/* Size in font pixels of the laid-out text. */
void Sample_Measure(const PxFont *f, const char *text, int boxAbove, int boxBelow, int lineGap, int maxLines, int *w, int *h, int *lines);

typedef struct SampleBitmap {
    unsigned char *px;     /* w*h RGBA, malloc'd (free it) */
    int w, h;
    int baseline;          /* rows from the top of the bitmap to the baseline of the first line */
} SampleBitmap;
/* Renders a layout at 1x.  ink: colour of fonts without a palette; the width is cut at maxW (0: 2048). */
bool Sample_Render(const PxFont *f, const SampleLayout *L, Color ink, int maxW, SampleBitmap *out);

typedef struct SampleCache { Texture2D tex; bool has; unsigned key; int w, h, baseline; int contentW; } SampleCache;   /* zero-initialise */
void SampleCache_Free(SampleCache *c);

typedef struct SampleStyle {
    Color ink;                       /* a = 0: near black */
    Color bg;                        /* what is behind: the colour the right edge fades to (a = 0: no fade) */
    int boxAbove, boxBelow, lineGap; /* see Sample_Layout */
    bool useClip; Rectangle clip;    /* screen rectangle the sample is clipped to (default: x..x+maxWidth, unlimited vertically) */
} SampleStyle;

/* Draws the sample with the top of the first line's font box at (x, y), at integer `zoom`, at most maxWidth wide
 * (clipped with a fade).  The texture is cached in `cache` and rebuilt when the text, the font or the glyphs change.
 * Returns the height drawn in screen pixels (all lines), and the layout width through *outW (font pixels, may be NULL). */
float Sample_Draw(SampleCache *cache, const PxFont *f, const char *text, float x, float y, int zoom, float maxWidth,
                  int maxLines, const SampleStyle *style, int *outW);

/* ---------------------------------------------------------------------------------------- the editor widget */

typedef struct SampleEdit { int cur, anchor; bool hasSel, drag; float pxCol; float scroll; int seen; } SampleEdit;   /* zero-initialise */

/* A multi-line text box on the shared text in `r`; registers `regName` for scripts.  blocked: a modal is open.  mouseClip (may be NULL):
 * the mouse only counts inside it and the text is clipped to it (a scrolled panel; its scissor is set again on return).  Returns true when the text changed.
 * While it has the focus it owns the keyboard (see Sample_TextFocused). */
bool SampleEdit_Do(SampleEdit *e, Rectangle r, const char *placeholder, const char *regName, bool blocked, const Rectangle *mouseClip);
bool SampleEdit_Focused(const SampleEdit *e);
void SampleEdit_SetFocus(SampleEdit *e, bool on);
bool Sample_TextFocused(void);        /* any sample box has the keyboard: shortcuts must not fire */
/* The height a box needs for the current text (lines * line height + padding), at most maxLines lines shown. */
float SampleEdit_Height(int maxLines);

/* ---------------------------------------------------------------------------------------- the font view's strip */

void  SampleStrip_Load(const char *iniPath);   /* called by Sample_LoadIni */
/* Height of the strip for an area of `availH` pixels (collapsed: only its header). */
float SampleStrip_Height(float availH);
/* Draws the strip into r (r.height = SampleStrip_Height) for font f and handles its input (when !blocked). */
void  SampleStrip_Draw(Rectangle r, const PxFont *f, bool blocked);
int   SampleStrip_Zoom(void);
int   SampleStrip_Setting(void);               /* its stored height in pixels */
bool  SampleStrip_Collapsed(void);
void  SampleStrip_SetZoom(int z);

#endif
