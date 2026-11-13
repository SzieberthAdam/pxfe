/* theme.h - shared look of pxfe: colours, UI fonts, drawing helpers (see DESIGN.md). */
#ifndef THEME_H
#define THEME_H

#include "raylib.h"
#include <stddef.h>
#include <stdint.h>

/* Palette */
#define TH_BG          (Color){0xF3, 0xF3, 0xF4, 0xFF}  /* window background */
#define TH_PANEL       (Color){0xFF, 0xFF, 0xFF, 0xFF}  /* panels, cells */
#define TH_PANEL_ALT   (Color){0xF8, 0xF8, 0xFA, 0xFF}  /* caption strips, toolbars */
#define TH_LINE        (Color){0xDC, 0xDC, 0xE0, 0xFF}  /* hairline separators */
#define TH_LINE_SOFT   (Color){0xEA, 0xEA, 0xEE, 0xFF}  /* pixel grid, faint lines */
#define TH_TEXT        (Color){0x1E, 0x1E, 0x22, 0xFF}
#define TH_TEXT_DIM    (Color){0x6B, 0x6B, 0x76, 0xFF}
#define TH_TEXT_OFF    (Color){0xB8, 0xB8, 0xC0, 0xFF}  /* disabled text and icons */
#define TH_TEXT_HINT   (Color){0xA8, 0xA8, 0xB2, 0xFF}  /* placeholder text */
#define TH_TEXT_WARN   (Color){0x7A, 0x55, 0x00, 0xFF}  /* text on the amber notice */
#define TH_TEXT_NOTE   (Color){0xB0, 0x6A, 0x00, 0xFF}  /* amber note text */
#define TH_TEXT_ERR    (Color){0xB5, 0x5A, 0x5E, 0xFF}  /* dim error text */
#define TH_TEXT_ON     (Color){0xFF, 0xFF, 0xFF, 0xFF}  /* text on an accent or danger fill */
#define TH_TEXT_TIP    (Color){0xF3, 0xF3, 0xF4, 0xFF}  /* text on the dark tooltip */
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
#define TH_PRESSED_FILL (Color){0xDC, 0xE6, 0xFC, 0xFF}  /* accent tint, opaque (pressed button) */
#define TH_HOVER_BORDER (Color){0xA9, 0xC2, 0xF5, 0xFF}  /* light accent border on hover */
#define TH_INK         (Color){0x14, 0x14, 0x18, 0xFF}  /* ink of sample text and renderings (content, not interface) */
#define TH_SCRIM       (Color){0x14, 0x14, 0x1C, 0x66}  /* veil behind a modal dialog */
#define TH_TIP_BG      (Color){0x2A, 0x2A, 0x30, 0xF2}  /* dark tooltip background */
#define TH_NOTICE_BG   (Color){0xFF, 0xF6, 0xE0, 0xFF}  /* amber notice bar (read-only) */
#define TH_CHECKER     (Color){0xEE, 0xEE, 0xF1, 0xFF}  /* dark square of the transparency checker */
#define TH_SELECTION   (Color){0x2F, 0x6F, 0xED, 0x46}  /* selected text */
#define TH_ON_LIGHT    (Color){0x00, 0x00, 0x00, 0x96}  /* text over a light swatch */
#define TH_ON_DARK     (Color){0xFF, 0xFF, 0xFF, 0xBE}  /* text over a dark swatch */
#define TH_LAYER_PINK  (Color){0xD6, 0x40, 0x9F, 0xFF}  /* layer palette, beside the metric colours */
#define TH_LAYER_TEAL  (Color){0x12, 0xA5, 0xB5, 0xFF}
#define TH_RADIUS 4

/* Type scale (DESIGN.md "Typography"): exactly three sizes, the em in px (Noto Sans is loaded at a larger raylib size, see theme.c).
 * GNU Unifont (hashes, codes) is a 16 px pixel font: drawn only at 16 px (32 for a large preview), never scaled otherwise. */
#define TH_SIZE_HEAD    18   /* Noto Sans SemiBold: view and dialog titles */
#define TH_SIZE_BODY    16   /* Noto Sans Regular: labels, values, buttons, rows */
#define TH_SIZE_CAP     13   /* Noto Sans SemiBold: section captions, upper case */
#define TH_SIZE_SMALL   13   /* Noto Sans Regular: status line, hints, footnotes */
#define TH_UNI_H        16   /* line height of Unifont text */
/* Spacing (4 px grid) */
#define TH_PAD   12   /* panel padding */
#define TH_GAP    8   /* gap between controls */
#define TH_ROW_H 28   /* control and list row height */
#define TH_LINE_H 20  /* line pitch of running body text */
typedef struct Theme {
    Font ui;       /* TH_SIZE_BODY regular */
    Font head;     /* TH_SIZE_HEAD semibold */
    Font cap;      /* TH_SIZE_CAP semibold */
    Font small;    /* TH_SIZE_SMALL regular */
    bool loaded;
    char missing[256];   /* font files that were not found ("" = all): Theme_Load leaves it for the caller to report */
} Theme;
extern Theme theme;
/* Load the fonts from assets/fonts (next to the exe, or two folders up for tests	mp, or the cwd): Noto Sans for the interface, GNU Unifont for hashes and
 * glyph codes (rasterised on demand, see Th_Uni*).  A missing file is named in theme.missing and the raylib default font (Unifont: the small font) stands in.
 * Call after InitWindow. */
void Theme_Load(void);
void Theme_Unload(void);
/* Text helpers using the theme fonts (spacing 0). */
void    Th_Text(const char *text, float x, float y, Color c);        /* ui font */
void    Th_TextHead(const char *text, float x, float y, Color c);    /* heading font */
void    Th_TextSmall(const char *text, float x, float y, Color c);   /* small font */
void    Th_TextCap(const char *text, float x, float y, Color c);     /* caption: semibold, upper case */
Vector2 Th_Measure(const char *text);
Vector2 Th_MeasureHead(const char *text);
Vector2 Th_MeasureCap(const char *text);
Vector2 Th_MeasureSmall(const char *text);

/* GNU Unifont: any code point of the BMP (unifont.otf) and the planes 1-16 (unifont_upper.otf), rasterised at exactly 16 px when first needed into a point-filtered
 * atlas on whole pixels (an atlas that is full is flushed and refilled: a glyph evicted is rasterised again when needed).
 * Th_TextUni / Th_MeasureUni: a line of text (8 px per half-width, 16 px per full-width glyph; height TH_UNI_H); x, y are rounded.
 * Th_CodeVisible: the code point has a Unifont image that shows (not a control, space or blank; false for a code point Unifont lacks).
 * Th_CodeDraw: the glyph caption of cp centred in `area` at `scale` (1 or 2): the character itself, but "U+XXXX" while `hover` (or always when it
 * has no visible image); "#n" for an uncoded glyph, "-" for none.  Th_CodeW: the width of its U+XXXX form (the slot a caption takes). */
void    Th_TextUni(const char *text, float x, float y, Color c);
float   Th_MeasureUni(const char *text);
bool    Th_CodeVisible(int32_t cp);
void    Th_CodeDraw(int32_t cp, Rectangle area, Color c, bool hover, int scale);
float   Th_CodeW(int32_t cp);
int     Th_CodeForm(int32_t cp);   /* how Th_CodeDraw last drew cp: 1 the character, 2 "U+XXXX", 0 not drawn (for scripts) */
/* Text with codes: every "U+XXXX" of `text` is drawn as Th_CodeDraw in a slot of its width (hover: over the slot, or `hov` 0/1 for all of them),
 * the rest in the face (TH_F_*).  Th_FitText / Th_TextClip take TH_F_RICH for the same. */
float   Th_MeasureRich(const char *text, int face);
float   Th_TextRich(const char *text, float x, float y, Color c, int face, int hov);
/* Crisp 1 px lines on the pixel grid (coordinates are snapped). */
void Th_HLine(float x0, float x1, float y, Color c);
void Th_VLine(float x, float y0, float y1, Color c);
void Th_DashedHLine(float x0, float x1, float y, float dash, Color c);
void Th_DashedVLine(float x, float y0, float y1, float dash, Color c);
void Th_RectLines(Rectangle r, Color c);                /* 1 px outline inside r */
/* Buttons (DESIGN.md "Push button"): flags; the icon of an icon button is TH_BTN_ICON(TH_IC_*) and its label only names it (registered, tooltip).
 * `hint` (or the icon's label) is offered to the view by Th_Tip while the mouse is over the button, also when it is disabled.
 * Th_ButtonW: the width of a button holding `label`.  Th_Toggle flips *on and returns true when clicked. */
#define TH_BTN_DISABLED 1
#define TH_BTN_DANGER   2   /* red; with TH_BTN_PRIMARY a red fill */
#define TH_BTN_PRIMARY  4   /* accent fill: the main command of a bar or dialog */
#define TH_BTN_ACTIVE   8   /* shown as on (a tool, a toggle) */
#define TH_BTN_ICON(i)  (((i) + 1) << 8)
#define TH_BTN_OFF(enabled) ((enabled) ? 0 : TH_BTN_DISABLED)
enum { TH_IC_PENCIL, TH_IC_ERASER, TH_IC_LINE, TH_IC_RECT, TH_IC_FILL, TH_IC_PREV, TH_IC_NEXT, TH_IC_LEFT, TH_IC_RIGHT, TH_IC_UP, TH_IC_DOWN,
       TH_IC_CLOSE, TH_IC_EYE, TH_IC_EYE_OFF, TH_IC_CHEV_DOWN, TH_IC_CHEV_RIGHT, TH_IC_SEARCH };
bool  Th_Button(Rectangle r, const char *label, int flags, const char *hint);
#define Th_ButtonDanger(r, label) Th_Button(r, label, TH_BTN_DANGER, NULL)   /* every delete command */
/* The command that makes an editable copy: always the primary button, one wording (`typeface`: of a typeface).  `off`: TH_BTN_OFF(enabled) or 0. */
#define Th_DupLabel(typeface) ((typeface) ? "Duplicate Typeface to Edit" : "Duplicate to Edit")
#define Th_DuplicateW(typeface) Th_ButtonW(Th_DupLabel(typeface))
#define Th_Duplicate(r, typeface, off, hint) Th_Button(r, Th_DupLabel(typeface), TH_BTN_PRIMARY | (off), hint)
bool  Th_Toggle(Rectangle r, const char *label, bool *on, int flags, const char *hint);
float Th_ButtonW(const char *label);
const char *Th_Tip(Rectangle *r);

/* Segmented control and tab bar (FontLab-like): a track with one raised segment; returns the index clicked this frame, else -1.
 * Bits of `disabled` mark dim, inert segments; `weights` (NULL = equal) are the relative widths; `hints` (or NULL) the tooltips.
 * Segment i registers "<prefix>_<label>" and "<prefix>.<i>". */
#define TH_TRACK (Color){0xE8, 0xE8, 0xEC, 0xFF}
typedef struct ThSeg {
    const char *const *labels;
    int n, sel;
    unsigned disabled;
    const char *prefix;
    const float *weights;
    const char *const *hints;
    bool rich;   /* labels with "U+XXXX" are glyph captions (hover over the segment shows the code) */
} ThSeg;
int Th_Segmented(Rectangle r, const ThSeg *g);

/* The stepper [-] 3x [+]: the zoom of every view and the number fields.  r.width >= 96 (96 for a zoom); `value` (NULL: the caller draws it) is centred.
 * Returns -1 / +1 when a button was pressed (or held: it repeats), else 0.  Registers "<prefix>-" and "<prefix>+".
 * flags: TH_STEP_NO_MINUS / TH_STEP_NO_PLUS dim and disable a button (a limit), TH_STEP_EDIT marks a value being typed (accent border, buttons idle).
 * Th_StepperHover: -1 / 1 while the mouse is over the minus / plus button (for hints). */
#define TH_STEP_W        96
#define TH_STEP_NO_MINUS 1
#define TH_STEP_NO_PLUS  2
#define TH_STEP_EDIT     4
int Th_Stepper(Rectangle r, const char *value, const char *prefix, int flags);
Rectangle Th_StepperValue(Rectangle r);   /* the middle of the stepper r, where the value is */
int Th_StepperHover(void);

/* Text field and search field (DESIGN.md "Text field"): one field of the whole window has the keyboard; its text is edited in a buffer of the theme.
 * `text` is what the field shows when it does not have the keyboard; `cap` the most bytes the caller can hold (0: 4095).  A live field (the default) has
 * its model follow every TH_TF_CHANGED by copying Th_TextValue(); a TH_TF_COMMIT field (a number) is applied on TH_TF_ENTER or TH_TF_BLUR.
 * The field loses the keyboard on Enter, Tab, Escape and a click elsewhere, and when it is not drawn for a frame.  Registers `name`
 * (and "<name>_Clear" for the clear mark of a search field).  Th_TextIs(NULL): some field has the keyboard.  Th_TextFocus: give it to a field. */
#define TH_TF_DISABLED 1     /* dim, no input */
#define TH_TF_READONLY 2     /* shown like a field, takes no input */
#define TH_TF_SEARCH   4     /* magnifier at the left, clear mark at the right */
#define TH_TF_NUM      8     /* digits and '-' only */
#define TH_TF_HEX      16    /* hex digits and '#' only */
#define TH_TF_BARE     32    /* no frame, centred: the value of a stepper */
#define TH_TF_COMMIT   64
#define TH_TF_SELECT   128   /* a click that gives the keyboard selects the whole text */
#define TH_TF_REVERT   256   /* Escape puts back the text from when the field got the keyboard (reported as TH_TF_CHANGED) */
#define TH_TF_CHANGED 1
#define TH_TF_ENTER   2      /* Enter or Tab */
#define TH_TF_ESC     4
#define TH_TF_BLUR    8      /* a click elsewhere */
int         Th_TextField(Rectangle r, const char *name, const char *text, int cap, int flags, const char *placeholder);
const char *Th_TextValue(void);
bool        Th_TextIs(const char *name);
void        Th_TextFocus(const char *name, const char *text, bool selectAll);
void        Th_TextBlur(void);
/* Text that does not fit: Th_FitText copies `s` shortened with an ellipsis to fit maxW in the face (TH_F_*); Th_TextClip draws it so. */
enum { TH_F_BODY, TH_F_SMALL, TH_F_UNI, TH_F_HEAD };
#define TH_F_RICH 0x200   /* or-ed to the face: "U+XXXX" tokens are glyph captions (Th_TextRich) */
#define TH_F_TAIL 0x100   /* or-ed to the face: keep the end of the text and cut the start (a path) */
void        Th_FitText(const char *s, int face, float maxW, char *out, size_t cap);
void        Th_TextClip(const char *s, float x, float y, float maxW, Color c, int face);

/* Number box: a stepper whose value is a TH_TF_COMMIT text field; flags TH_TF_DISABLED, TH_NUM_WHEEL (the wheel over it steps; Th_WheelUsed tells
 * the view the wheel was taken this frame).  Registers "<prefix>-", "<prefix>+" and "<prefix>.value".  True and *out when the value changes. */
#define TH_NUM_WHEEL 0x10000
bool Th_Number(Rectangle r, const char *prefix, int val, int lo, int hi, int flags, int *out);
bool Th_WheelUsed(void);

/* Checkbox, switch and dropdown.  Th_Check: a box with a tick and its label on the right; Th_Switch: a pill with a knob; both toggle *v on a click
 * (anywhere on the row r) and return true then.  TH_CK_RIGHT: control at the right end, label at the left; TH_CK_GAP: leave room for a swatch before
 * the label.  `name` is what the control registers.  Th_Dropdown: the closed box of a select; returns true when clicked. */
#define TH_CK_OFF   1
#define TH_CK_RIGHT 2
#define TH_CK_GAP   4
bool Th_Check(Rectangle r, const char *label, bool *v, int flags, const char *name);
bool Th_Switch(Rectangle r, const char *label, bool *v, int flags, const char *name);
bool Th_Dropdown(Rectangle r, const char *name, const char *value, bool dim, bool open);

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

/* Hash display, the two forms (DESIGN.md "Hash display").  Th_HashShort: dim kind word ("Font", NULL for none) and the first 8 characters in Unifont,
 * top-left at (x, y) like Th_Text (TH_HASH_SMALL: like Th_TextSmall), or with TH_HASH_RIGHT ending at x; TH_HASH_MEASURE only returns the width.  copyName != NULL makes it a copy link
 * (registered under that name).  Th_HashField: the caption and a 28 px box with the full hash ("" = "no ink", NULL = "not known"); a click copies it;
 * *y advances.  `what` names it in the toast ("<what> hash copied").  The hooks (set by main) give the scrolled-panel hover and the hint / toast. */
#define TH_HASH_RIGHT   1
#define TH_HASH_ACCENT  2   /* the hash is a changed state */
#define TH_HASH_MEASURE 4
#define TH_HASH_SMALL   8   /* small size (kind word), for a line of small text: the status bar */
float Th_HashShort(const char *kind, const char *hash, float x, float y, int flags, const char *copyName);
void  Th_HashField(float x, float *y, float w, const char *caption, const char *hash, const char *name, const char *what, int flags);
extern bool (*Th_HoverFn)(Rectangle r);
extern void (*Th_NoticeFn)(const char *what, bool copied);

/* Section caption, label/value row, list row and card (DESIGN.md "Section caption", "Label/value row", "List row and card").
 * Th_Section: the caption `title` (semibold, upper case, dim) and a hairline to the right at (x, *y), width w; *y advances by 24.  `right`: small dim text
 * at the right end.  TH_SEC_OPEN / TH_SEC_CLOSED make it a fold header (chevron, hover plate); true when clicked this frame; it registers "<title>_Fold".
 * Th_Row: a dim label at the left, in a column of TH_ROW_LABEL_W; `value` right-aligned at x + w, fitted (TH_ROW_UNI: Unifont).  With value NULL the
 * row is for a control: the returned rectangle is the value part, where the caller draws it.  *y advances.
 * Th_ListRow (a row in a list or inside a card: hover plate, selected tint with an accent bar) and Th_Card (a bordered card) draw only the surface and
 * return its opaque fill (the background of a sample drawn on it); the caller draws the content.  Flags: TH_LR_HOVER, TH_LR_SELECTED, TH_LR_WITHIN
 * (a card that holds the selected row), TH_LR_DISABLED (inert: dim surface). */
#define TH_SEC_OPEN   1
#define TH_SEC_CLOSED 2
#define TH_ROW_LABEL_W 108
#define TH_ROW_UNI    1
#define TH_LR_HOVER    1
#define TH_LR_SELECTED 2
#define TH_LR_WITHIN   4
#define TH_LR_DISABLED 8
#define TH_SELECTED_FILL (Color){0xEA, 0xF0, 0xFD, 0xFF}  /* TH_ACCENT_TINT over TH_PANEL, opaque */
bool      Th_Section(const char *title, float x, float *y, float w, int flags, const char *right);
Rectangle Th_Row(float x, float *y, float w, const char *label, const char *value, int flags);
Color     Th_ListRow(Rectangle r, int flags);
Color     Th_Card(Rectangle r, int flags);

/* Scroll bar (DESIGN.md "Scroll bar"): a thin bar at the right edge of `view` (TH_SB_W of it is the bar's: content stays clear).  Th_ScrollBar takes
 * the wheel (`step` px a notch, 0: the caller's), the drag and a click on the track (the thumb centres there) for *scroll, clamps it to 0..content - view.height
 * (whole pixels) and draws the bar; `live` false gives it no input (a popup is open).  True while the mouse is on the bar or drags it.
 * Th_ScrollRows is the same for a list of `n` rows `rowH` high with `vis` shown from `*first`. */
#define TH_SB_W 12
#define TH_SCROLL_THUMB     (Color){0x6B, 0x6B, 0x76, 110}
#define TH_SCROLL_THUMB_HOT (Color){0x6B, 0x6B, 0x76, 200}
#define TH_SCROLL_TRACK     (Color){0x6B, 0x6B, 0x76, 30}
bool Th_ScrollBar(Rectangle view, float *scroll, float content, float step, bool live);
bool Th_ScrollRows(Rectangle view, int *first, int n, int vis, float rowH, bool live);

/* Raised surface of everything that floats (menu, popover, dialog, toast): shadow, white fill, hairline border; `a` (0..1) fades it. */
void Th_Raised(Rectangle r, float a);

/* Menu (DESIGN.md "Popover and menu"): the items are `items`, or `get(ud, i, out)` for a long list scrolled with `scroll` (`vis` rows shown).
 * (x, y) is the top-left corner (top-right with TH_MENU_RIGHT), w the width; the menu stays inside the window.  An item: `label`, the shortcut `key`,
 * the `name` it registers (default the label), a `why` hint for a disabled item, TH_MI_* flags.  Th_Menu returns the index picked (release over an enabled
 * item, or Enter on the highlighted one), else -1, and clears *open on a pick, Escape and a click outside (not on `keep`, the opener).  Th_MenuRect: where it is. */
#define TH_MI_OFF    1   /* disabled */
#define TH_MI_DANGER 2   /* a delete command */
#define TH_MI_SEP    4   /* a hairline after the item */
#define TH_MI_CUR    8   /* the current choice of a dropdown */
#define TH_MI_DIM    16  /* a placeholder choice ("Any") */
#define TH_MENU_RIGHT 1
typedef struct ThMenuItem { const char *label, *key, *name, *why; int flags; } ThMenuItem;
typedef struct ThMenu {
    const ThMenuItem *items;
    int n;
    void (*get)(void *ud, int i, ThMenuItem *out);
    void *ud;
    float x, y, w;
    int flags, vis, *scroll;
    Rectangle keep;
} ThMenu;
Rectangle Th_MenuRect(const ThMenu *m);
int Th_Menu(const ThMenu *m, bool *open);
/* Keys of an open menu: Up, Down, Home, End move the highlight (skipping disabled items; hover moves it too), Enter picks it, Escape closes.  They are the menu's alone:
 * IsKeyPressed / IsKeyPressedRepeat (wrapped here for every user interface file; Th_Menu reads the raw ones) answer false for them.  Th_MenuHi: the highlight, for scripts. */
bool Th_MenuKeys(int key);
int  Th_MenuHi(void);
#define IsKeyPressed(k) (!Th_MenuKeys(k) && (IsKeyPressed)(k))
#define IsKeyPressedRepeat(k) (!Th_MenuKeys(k) && (IsKeyPressedRepeat)(k))

/* Toast (DESIGN.md "Toast"): kind INFO, OK (green bar) or ERR (red bar, stays longer).  Th_ToastDraw stacks them up from the bottom of `area`, at its
 * right end (`left`: at its left end).  Th_ToastCount and Th_ToastHas are for scripts. */
#define TH_TOAST_INFO 0
#define TH_TOAST_OK   1
#define TH_TOAST_ERR  2
void Th_Toast(int kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void Th_ToastDraw(Rectangle area, bool left);
int  Th_ToastCount(void);
bool Th_ToastHas(const char *sub);

/* Status bar (DESIGN.md "Status bar"): a TH_ROW_H strip.  Left parts run from the left, right parts end at the right (less `reserve`, room for a control of
 * the caller's); a part that does not fit is dropped.  A text part: `text`, `color` (alpha 0: dim), at most `maxFrac` of the width, cut with an ellipsis at
 * its start when TH_SP_TAIL (a path).  A hash part (text NULL): Th_HashShort(kind, hash) with TH_HASH_ACCENT and the copy link `copy`. */
#define TH_SP_TAIL 0x100
typedef struct ThPart { const char *text; Color color; float maxFrac; int flags; const char *kind, *hash, *copy; } ThPart;
void Th_StatusBar(Rectangle r, float reserve, const ThPart *left, int nl, const ThPart *right, int nr);

/* Convert a pxfm RRGGBBAA colour. */
Color Th_FromRGBA(unsigned int rrggbbaa);

#endif
