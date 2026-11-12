/* theme.h - shared look of pxfe: colours, UI fonts, raygui style, drawing helpers (see DESIGN.md). */
#ifndef THEME_H
#define THEME_H

#include "raylib.h"

/* Palette */
#define TH_BG          (Color){0xF3, 0xF3, 0xF4, 0xFF}  /* window background */
#define TH_PANEL       (Color){0xFF, 0xFF, 0xFF, 0xFF}  /* panels, cells */
#define TH_PANEL_ALT   (Color){0xF8, 0xF8, 0xFA, 0xFF}  /* caption strips, toolbars */
#define TH_LINE        (Color){0xDC, 0xDC, 0xE0, 0xFF}  /* hairline separators */
#define TH_LINE_SOFT   (Color){0xEA, 0xEA, 0xEE, 0xFF}  /* pixel grid, faint lines */
#define TH_TEXT        (Color){0x1E, 0x1E, 0x22, 0xFF}
#define TH_TEXT_DIM    (Color){0x7A, 0x7A, 0x85, 0xFF}
#define TH_ACCENT      (Color){0x2F, 0x6F, 0xED, 0xFF}
#define TH_ACCENT_TINT (Color){0x2F, 0x6F, 0xED, 0x26}
#define TH_HOVER       (Color){0x00, 0x00, 0x00, 0x0A}
#define TH_DANGER      (Color){0xE5, 0x48, 0x4D, 0xFF}

/* Metric lines */
#define TH_M_BASELINE  (Color){0xE5, 0x48, 0x4D, 0xFF}
#define TH_M_BOX       (Color){0x8E, 0x8E, 0x99, 0xFF}
#define TH_M_CAP       (Color){0x2F, 0x9E, 0x6E, 0xFF}
#define TH_M_XHEIGHT   (Color){0xD6, 0x8A, 0x00, 0xFF}
#define TH_M_ADVANCE   (Color){0x2F, 0x6F, 0xED, 0xFF}
#define TH_M_UNDERLINE (Color){0x9B, 0x59, 0xD0, 0xFF}  /* underline and strike-out, dotted */

#define TH_RADIUS 4

typedef struct Theme {
    Font ui;       /* 13 px UI font */
    Font small;    /* 11 px captions */
    Font mono;     /* 12 px monospace for code points (Consolas), falls back to ui */
    bool loaded;
} Theme;

extern Theme theme;

/* Load fonts (Segoe UI / Consolas with Latin-1 + Latin Extended-A + common punctuation code points,
 * bilinear filter; fallback raylib default) and apply the raygui style. Call after InitWindow. */
void Theme_Load(void);
void Theme_Unload(void);

/* Text helpers using the theme fonts (spacing 0). */
void    Th_Text(const char *text, float x, float y, Color c);        /* ui font */
void    Th_TextSmall(const char *text, float x, float y, Color c);   /* small font */
void    Th_TextMono(const char *text, float x, float y, Color c);    /* mono font */
Vector2 Th_Measure(const char *text);
Vector2 Th_MeasureSmall(const char *text);
Vector2 Th_MeasureMono(const char *text);

/* Crisp 1 px lines on the pixel grid (coordinates are snapped). */
void Th_HLine(float x0, float x1, float y, Color c);
void Th_VLine(float x, float y0, float y1, Color c);
void Th_DashedHLine(float x0, float x1, float y, float dash, Color c);
void Th_DashedVLine(float x, float y0, float y1, float dash, Color c);
void Th_RectLines(Rectangle r, Color c);                /* 1 px outline inside r */
void Th_Panel(Rectangle r);                             /* white panel with hairline border */

/* Flat button/toggle in the theme style; returns true when clicked. */
bool Th_Button(Rectangle r, const char *label);
bool Th_ButtonDanger(Rectangle r, const char *label);   /* a button for a destructive command: red label and border */
bool Th_Toggle(Rectangle r, const char *label, bool *active);

/* Segmented control (FontLab-like): a track with one raised segment; returns the index clicked this frame, else -1.
 * Segments whose bit is set in disabledMask are dim and inert.  Segment i registers as "<prefix>_<label>". */
int Th_Segmented(Rectangle r, const char *const *labels, int n, int sel, unsigned disabledMask, const char *prefix);

/* Input gate: while true (a modal dialog is open) Th_Button/Th_Toggle and the editor ignore the mouse and keyboard. */
void Th_SetInputBlocked(bool blocked);
bool Th_InputBlocked(void);

/* Widget registry for scripted input (hidden --play flag). Widgets register their rectangle by name each frame;
 * names have no spaces ("Save_Draft"). Lookup is case-insensitive and prefers the most recently registered one. */
void Th_RegClear(void);
void Th_Reg(const char *name, Rectangle r);
void Th_RegLabel(const char *label, Rectangle r);   /* registers `label` with spaces turned into '_' */
bool Th_RegFind(const char *name, Rectangle *r);

/* Texture (point filter) from a w*h mask, row-major top row first: a non-zero byte is an inked pixel drawn in `ink`.
 * The caller unloads it with UnloadTexture. */
Texture2D Th_MaskTexture(const unsigned char *mask, int w, int h, Color ink);
/* Draws `tex` at integer scale `s` with its top-left at (x, y), clipped to `clip` (pixel-exact, no scissor mode). */
void Th_DrawTextureClipped(Texture2D tex, float x, float y, int s, Rectangle clip, Color tint);
/* A small busy indicator (a ring of dots) centred at c. */
void Th_Spinner(Vector2 c, float radius, Color col);

/* Convert a pxfm RRGGBBAA colour. */
Color Th_FromRGBA(unsigned int rrggbbaa);

#endif
