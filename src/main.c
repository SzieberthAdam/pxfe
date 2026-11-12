/* main.c - pxfe main window: app state, views (library browser, font view, glyph view), documents (originals,
 * duplicates, load / save / import / export of the .pxfdb), toolbar, glyph grid, status bar, dialogs, undo (see DESIGN.md). */
#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pxfont.h"
#include "pxtf.h"
#include "store.h"
#include "insight.h"
#include "finder.h"
#include "sqlite3.h"
#include "library.h"
#include "browser.h"
#include "sample.h"
#include "theme.h"
#include "editor.h"
#include "layers.h"
#include "winfile.h"

/* Scripted text input (hidden --play flag): GetCharPressed is wrapped so the script runner can inject characters.
 * The e2e build also passes -DGetCharPressed=Play_GetChar to every file, so the finder (finder.c) reads the shim too. */
int Play_GetChar(void);
#define GetCharPressed Play_GetChar

/* ------------------------------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------------------------------- */
#define TOOLBAR_H   48
#define CMDBAR_H    38          /* the command strip under the toolbar in the font view */
#define INFO_H      34
#define STATUS_H    26
#define PANEL_W     304
#define GRID_MARGIN 12
#define GRID_GAP    8
#define CELL_PADX   6
#define CELL_PADY   6
#define CAPTION_H   22
#define UNDO_MAX    100
#define PATH_MAX_LEN 1024
#define PXFDB_FILTER "Pixel glyph library (*.pxfdb)|*.pxfdb|All files|*.*"

/* ------------------------------------------------------------------------------------------------
 * App state
 * ---------------------------------------------------------------------------------------------- */
static PxFont font;                    /* the document's font (all glyphs loaded) */
static bool hasFont = false;
static PxtfTypeface cont;              /* its typeface (a copy), valid when hasCont */
static bool hasCont = false;
static int  contVariant = 0;           /* the variant of the typeface this font is */
static bool readOnly = false;          /* an original: nothing may change it */
static bool quit = false;

static char title[256];                /* "Duplicate of <name>" or the original's name */
static char origTitle[256];            /* the name of the original */
static char docPath[PATH_MAX_LEN];     /* what the status bar shows: <hash>.pxfm of a store font, or the .pxfd */
static char sourcePath[PATH_MAX_LEN];  /* "<hash>.pxfm" of the original in the store ("" if none) */
static char libRoot[PATH_MAX_LEN];     /* the open database file ("" none) */
static char iniOverride[PATH_MAX_LEN];

/* Change detection by hash: modified = current canonical hash differs from the one the font was opened with
 * (or last saved to the library), or the typeface text differs. */
static char origHash[33], origContHash[33];  /* baseline */
static char curHash[33], curContHash[33];    /* of the current state */
static bool modified = false, needHash = false;
static char exportSummary[PATH_MAX_LEN + 900];              /* the text of the last export summary */
static char lastPxfm[33], lastPxtf[33], lastPxfmPath[PATH_MAX_LEN], lastPxtfPath[PATH_MAX_LEN];

/* the pxgl hash of every glyph when the font was opened (sorted by code point) for the glyph view */
static int32_t *ghCp = NULL;
static char (*ghHash)[33] = NULL;
static int ghN = 0;

/* Dialog overrides for scripted runs (play command setdialog). */
static char dlgFolderPath[PATH_MAX_LEN], dlgSavePath[PATH_MAX_LEN];
static bool noDialogs = false;

/* ---- the library: one Store for the app's lifetime, the browser / insight / finder on it ---- */
static Store *store = NULL;
static Insight *insight = NULL;
static Finder *finder = NULL;
static bool inFinder = false;          /* the glyph finder view */
static bool wasLibrary = true;       /* the view the finder was entered from (the library, else the font) */
static char dlgDbPath[PATH_MAX_LEN];

static bool DlgFolder(char *out, int size, const char *what)
{
    if (dlgFolderPath[0]) { snprintf(out, (size_t)size, "%s", dlgFolderPath); return true; }
    if (noDialogs) return false;
    return Win_PickFolder(out, size, what);
}

static bool DlgDb(char *out, int size)
{
    if (dlgDbPath[0]) { snprintf(out, (size_t)size, "%s", dlgDbPath); return true; }
    if (noDialogs) return false;
    return Win_OpenFileDialog(out, size, PXFDB_FILTER);
}

static int  zoom = 4;
static bool showMetrics = true;
static bool showCaptions = true;

static bool *sel = NULL;               /* selection flags, one per glyph */
static int  selN = 0;                  /* length of sel */
static int  selAnchor = -1;
static int  selCount = 0;

static float scrollY = 0;
static int   revealIdx = -1;
static int   hoverIdx = -1;
static int   lastClickIdx = -1;
static double lastClickTime = 0;

static Editor *ed = NULL;
static bool inEditor = false;          /* glyph view */
static Browser *browser = NULL;
static bool inBrowser = false;         /* library browser */

/* layout cache for the grid */
static float *lx = NULL, *ly = NULL, *lw = NULL;
static int lcap = 0;
static float rowH = 0, contentH = 0;
static int boxAbove = 6, boxBelow = 2; /* the font box of the current frame (px_font_box) */
static float panelScroll = 0;          /* scroll of the right-hand panel */
static int glyphTab = 0;               /* tab of the glyph view's right panel: 0 Glyph, 1 Usage, 2 Layers (not remembered) */
static int panelTab = 0;               /* tab of the right-hand panel: 0 Font, 1 Typeface, 2 Glyph, 3 Similar (kept in pxfe.ini) */
static int ctFocus = -1;               /* the typeface text field being edited (a PxtfText), or -1 */

/* ------------------------------------------------------------------------------------------------
 * Small utilities
 * ---------------------------------------------------------------------------------------------- */
static const char *BaseName(const char *p)
{
    const char *a = strrchr(p, '/'), *b = strrchr(p, 0x5C);
    const char *s = a > b ? a : b;
    return s ? s + 1 : p;
}

static void DirName(const char *p, char *out, size_t cap)
{
    snprintf(out, cap, "%s", p);
    char *a = strrchr(out, '/'), *b = strrchr(out, 0x5C);
    char *s = a > b ? a : b;
    if (s) *s = 0; else out[0] = 0;
}

static void Slashes(char *s)
{
    for (; *s; s++) if (*s == 0x5C) *s = '/';
}

static bool HasExt(const char *p, const char *ext)
{
    size_t n = strlen(p), e = strlen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++) {
        char a = p[n - e + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != b) return false;
    }
    return true;
}

static float Fl(float v) { return floorf(v + 0.5f); }

static bool Same(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* ------------------------------------------------------------------------------------------------
 * Toasts
 * ---------------------------------------------------------------------------------------------- */
typedef struct Toast { char text[200]; double t0; float life; int kind; } Toast; /* kind 0 info, 1 ok, 2 error */
static Toast toasts[4];
static int toastCount = 0;

static void ToastPush(int kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void ToastPush(int kind, const char *fmt, ...)
{
    if (toastCount == 4) {
        memmove(&toasts[0], &toasts[1], sizeof(Toast) * 3);
        toastCount = 3;
    }
    Toast *t = &toasts[toastCount++];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(t->text, sizeof t->text, fmt, ap);
    __builtin_va_end(ap);
    t->t0 = GetTime();
    t->life = kind == 2 ? 6.0f : 3.2f;
    t->kind = kind;
}

/* ------------------------------------------------------------------------------------------------
 * UI primitives (flat, themed; raygui is only linked for its style plumbing)
 * ---------------------------------------------------------------------------------------------- */
static bool uiBlocked = false;
static bool swallowMouse = false;   /* a left button held while a document opened: ignore it until it is released */
static const char *hintText = NULL;
static Rectangle hovClip;              /* while hovClipOn, widgets only react inside this rectangle (scrolled panels) */
static bool hovClipOn = false;
static bool wheelUsed = false;         /* a widget consumed the mouse wheel this frame */

static Vector2 Mouse(void) { return GetMousePosition(); }
static bool Hov(Rectangle r)
{
    if (uiBlocked) return false;
    if (hovClipOn && !CheckCollisionPointRec(Mouse(), hovClip)) return false;
    return CheckCollisionPointRec(Mouse(), r);
}

static void RRect(Rectangle r, Color fill, Color line)
{
    float m = r.width < r.height ? r.width : r.height;
    float rn = 2.0f * TH_RADIUS / m;
    if (fill.a) DrawRectangleRounded(r, rn, 8, fill);
    if (line.a) DrawRectangleRoundedLinesEx(r, rn, 8, 1.0f, line);
}

static void TextCentered(const char *s, Rectangle r, Color c)
{
    Vector2 m = Th_Measure(s);
    Th_Text(s, Fl(r.x + (r.width - m.x) / 2), Fl(r.y + (r.height - m.y) / 2), c);
}

static Color Mix(Color a, Color b, float t)
{
    return (Color){(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                   (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

/* style: 0 normal, 1 primary, 2 danger */
static bool Button(Rectangle r, const char *label, bool enabled, int style, const char *hint)
{
    bool hov = Hov(r);
    Th_RegLabel(label, r);
    if (hov && hint) hintText = hint;
    bool down = enabled && hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color bg = TH_PANEL, line = TH_LINE, fg = TH_TEXT;
    if (style == 1) {
        bg = TH_ACCENT; line = TH_ACCENT; fg = WHITE;
        if (enabled && hov) bg = Mix(TH_ACCENT, BLACK, down ? 0.22f : 0.10f);
    } else {
        if (style == 2) fg = TH_DANGER;
        if (enabled && hov) bg = down ? (Color){0xE4, 0xE4, 0xE9, 0xFF} : (Color){0xF1, 0xF1, 0xF4, 0xFF};
        if (enabled && hov) line = (Color){0xC9, 0xC9, 0xD0, 0xFF};
    }
    if (!enabled) { fg = Mix(fg, TH_PANEL, 0.62f); if (style == 1) { bg = Mix(TH_ACCENT, WHITE, 0.6f); line = bg; fg = WHITE; } }
    RRect(r, bg, line);
    TextCentered(label, r, fg);
    return enabled && hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static bool ToggleBtn(Rectangle r, const char *label, bool *on, const char *hint)
{
    bool hov = Hov(r);
    Th_RegLabel(label, r);
    if (hov && hint) hintText = hint;
    Color bg = *on ? TH_ACCENT_TINT : TH_PANEL;
    if (hov) bg = *on ? (Color){0x2F, 0x6F, 0xED, 0x3A} : (Color){0xF1, 0xF1, 0xF4, 0xFF};
    RRect(r, bg, *on ? Mix(TH_ACCENT, WHITE, 0.45f) : TH_LINE);
    TextCentered(label, r, *on ? TH_ACCENT : TH_TEXT);
    if (hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { *on = !*on; return true; }
    return false;
}

static bool Checkbox(Rectangle r, bool *v, bool enabled, const char *name)
{
    Th_Reg(name, r);
    bool hov = Hov((Rectangle){r.x - 2, r.y - 2, r.width + 4, r.height + 4});
    Color off = (Color){0xB4, 0xB4, 0xBD, 0xFF};
    Color line = !enabled ? TH_LINE : (*v ? TH_ACCENT : (hov ? TH_TEXT_DIM : off));
    Color fill = *v ? (enabled ? TH_ACCENT : Mix(TH_ACCENT, WHITE, 0.55f)) : (enabled ? TH_PANEL : TH_PANEL_ALT);
    RRect(r, fill, enabled ? line : (*v ? fill : TH_LINE));
    if (*v) {
        DrawLineEx((Vector2){r.x + 3.5f, r.y + 8.5f}, (Vector2){r.x + 6.5f, r.y + 11.5f}, 1.6f, WHITE);
        DrawLineEx((Vector2){r.x + 6.5f, r.y + 11.5f}, (Vector2){r.x + 12.5f, r.y + 4.5f}, 1.6f, WHITE);
    }
    if (enabled && hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { *v = !*v; return true; }
    return false;
}

/* A flat on/off switch (pill); returns true when toggled. */
static bool Switch(Rectangle r, bool *v, bool enabled, const char *name)
{
    Th_Reg(name, r);
    bool hov = Hov(r);
    Color onC = enabled ? TH_ACCENT : Mix(TH_ACCENT, WHITE, 0.55f);
    Color offC = enabled ? (Color){0xC4, 0xC4, 0xCC, 0xFF} : (Color){0xE2, 0xE2, 0xE7, 0xFF};
    DrawRectangleRounded(r, 1.0f, 8, *v ? onC : offC);
    float d = r.height - 6;
    float kx = *v ? r.x + r.width - 3 - d : r.x + 3;
    DrawCircle((int)(kx + d / 2), (int)(r.y + r.height / 2), d / 2, WHITE);
    if (enabled && hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { *v = !*v; return true; }
    return false;
}

/* Greedy wrap into lines; returns the line count. */
static int WrapLines(const char *text, float maxW, char lines[][160], int maxLines)
{
    int n = 0;
    const char *p = text;
    while (*p && n < maxLines) {
        char buf[160];
        int len = 0, lastSpace = -1;
        while (p[len] && p[len] != '\n' && len < 158) {
            buf[len] = p[len];
            buf[len + 1] = 0;
            if (p[len] == ' ') lastSpace = len;
            if (Th_Measure(buf).x > maxW) {
                if (lastSpace > 0) len = lastSpace; /* break at the space */
                else if (len > 0) len--;
                break;
            }
            len++;
        }
        memcpy(lines[n], p, (size_t)len);
        lines[n][len] = 0;
        n++;
        p += len;
        if (*p == ' ' || *p == '\n') p++;
    }
    return n;
}

/* Text editing keys on a UTF-8 buffer. Returns true when changed. */
static bool EditText(char *buf, int cap, bool numeric)
{
    bool changed = false;
    int c;
    while ((c = GetCharPressed()) > 0) {
        if (numeric && !((c >= '0' && c <= '9') || c == '-')) continue;
        if (c < 32) continue;
        int sz = 0;
        const char *u = CodepointToUTF8(c, &sz);
        int len = (int)strlen(buf);
        if (len + sz < cap) { memcpy(buf + len, u, (size_t)sz); buf[len + sz] = 0; changed = true; }
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        int len = (int)strlen(buf);
        if (len > 0) {
            len--;
            while (len > 0 && (buf[len] & 0xC0) == 0x80) len--; /* UTF-8 continuation */
            buf[len] = 0;
            changed = true;
        }
    }
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip) {
            int len = (int)strlen(buf);
            for (const char *q = clip; *q && *q != '\n' && *q != '\r' && len < cap - 1; q++) buf[len++] = *q;
            buf[len] = 0;
            changed = true;
        }
    }
    return changed;
}

/* Offset of the longest tail of s (on a UTF-8 boundary) that fits maxW. */
static int TailStart(const char *s, float maxW)
{
    int n = (int)strlen(s);
    if (Th_Measure(s).x <= maxW) return 0;
    int lo = 0, hi = n; /* smallest start that fits is in [lo, hi] */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        while (mid < hi && (s[mid] & 0xC0) == 0x80) mid++;
        if (Th_Measure(s + mid).x <= maxW) hi = mid; else lo = mid + 1;
        if (lo < n) while (lo < n && (s[lo] & 0xC0) == 0x80) lo++;
    }
    return hi;
}

/* Copy of the longest head of s that fits maxW, with "..." when cut. */
static void HeadFit(const char *s, float maxW, char *out, size_t cap)
{
    if (Th_Measure(s).x <= maxW) { snprintf(out, cap, "%s", s); return; }
    int n = (int)strlen(s), lo = 0, hi = n;   /* largest prefix length that fits (with the dots) */
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        while (mid > lo && (s[mid] & 0xC0) == 0x80) mid--;
        char tmp[512];
        int l = mid < (int)sizeof tmp - 4 ? mid : (int)sizeof tmp - 4;
        memcpy(tmp, s, (size_t)l);
        memcpy(tmp + l, "...", 4);
        if (Th_Measure(tmp).x <= maxW) lo = mid; else hi = mid - 1;
        if (mid == lo && hi > lo && lo + 1 > hi) break;
    }
    int l = lo < (int)cap - 4 ? lo : (int)cap - 4;
    memcpy(out, s, (size_t)l);
    memcpy(out + l, "...", 4);
}

static void DrawInputBox(Rectangle r, const char *text, bool focused, bool selected, bool enabled)
{
    RRect(r, enabled ? TH_PANEL : TH_PANEL_ALT, focused ? TH_ACCENT : (enabled ? (Color){0xB4, 0xB4, 0xBD, 0xFF} : TH_LINE));
    if (focused) DrawRectangleRoundedLinesEx((Rectangle){r.x - 1, r.y - 1, r.width + 2, r.height + 2}, 2.0f * 5 / r.height, 8, 1.0f,
                                             (Color){0x2F, 0x6F, 0xED, 0x40});
    float maxW = r.width - 16;
    char head[560];
    const char *s = text;
    if (focused) s = text + TailStart(text, maxW);   /* show the tail while typing */
    else { HeadFit(text, maxW, head, sizeof head); s = head; }
    Vector2 m = Th_Measure(s);
    float tx = r.x + 8, ty = Fl(r.y + (r.height - m.y) / 2);
    if (selected && *s) DrawRectangle((int)tx - 1, (int)ty, (int)m.x + 2, (int)m.y, TH_ACCENT_TINT);
    Th_Text(s, tx, ty, enabled ? TH_TEXT : TH_TEXT_DIM);
    if (focused && !selected && fmod(GetTime(), 1.0) < 0.6) DrawRectangle((int)(tx + m.x + 1), (int)ty, 1, (int)m.y, TH_TEXT);
}

/* ------------------------------------------------------------------------------------------------
 * Undo stack, selection
 * ---------------------------------------------------------------------------------------------- */
static PxFont *undoStack[UNDO_MAX + 1], *redoStack[UNDO_MAX + 1];
static PxtfTypeface *undoCont[UNDO_MAX + 1], *redoCont[UNDO_MAX + 1];   /* the typeface beside each snapshot, or NULL */
static int undoN = 0, redoN = 0;
static int lastUndoKey = 0;
static double lastUndoTime = 0;

static void StackClear(PxFont **st, int *n)
{
    PxtfTypeface **cs = st == undoStack ? undoCont : redoCont;
    for (int i = 0; i < *n; i++) {
        px_font_free(st[i]); free(st[i]);
        if (cs[i]) { pxtf_free(cs[i]); free(cs[i]); cs[i] = NULL; }
    }
    *n = 0;
}

static void SelResize(void)
{
    int n = hasFont ? font.count : 0;
    if (n == selN) return;
    bool *ns = (bool *)calloc((size_t)(n > 0 ? n : 1), sizeof(bool));
    for (int i = 0; i < n && i < selN; i++) ns[i] = sel[i];
    free(sel);
    sel = ns;
    selN = n;
}

static void SelClear(void)
{
    SelResize();
    for (int i = 0; i < selN; i++) sel[i] = false;
    selAnchor = -1;
}

static void SelOnly(int i)
{
    SelClear();
    if (i >= 0 && i < selN) { sel[i] = true; selAnchor = i; }
}

/* Snapshot the font before an edit. key != 0 coalesces rapid edits of the same field.  Whether the font is
 * "modified" is decided by its hash (EnsureHashes), never by the fact that an edit happened. */
static void PushUndo(int key)
{
    double now = GetTime();
    needHash = true;
    if (key != 0 && key == lastUndoKey && now - lastUndoTime < 0.8) { lastUndoTime = now; return; }
    lastUndoKey = key;
    lastUndoTime = now;
    StackClear(redoStack, &redoN);
    PxFont *s = (PxFont *)malloc(sizeof(PxFont));
    if (!px_font_copy(s, &font)) { free(s); return; }
    PxtfTypeface *sc = NULL;
    if (hasCont) {
        sc = (PxtfTypeface *)malloc(sizeof(PxtfTypeface));
        if (!pxtf_copy(sc, &cont)) { free(sc); sc = NULL; px_font_free(s); free(s); return; }
    }
    if (undoN == UNDO_MAX) {
        px_font_free(undoStack[0]); free(undoStack[0]);
        if (undoCont[0]) { pxtf_free(undoCont[0]); free(undoCont[0]); }
        memmove(&undoStack[0], &undoStack[1], sizeof(PxFont *) * (UNDO_MAX - 1));
        memmove(&undoCont[0], &undoCont[1], sizeof(PxtfTypeface *) * (UNDO_MAX - 1));
        undoN--;
    }
    undoCont[undoN] = sc;
    undoStack[undoN++] = s;
}

static void EnsureHashes(void)
{
    if (!hasFont) return;
    if (!needHash && curHash[0]) return;
    px_font_hash(&font, curHash);
    if (hasCont) pxtf_hash(&cont, curContHash); else curContHash[0] = 0;
    modified = !Same(curHash, origHash) || (hasCont && !Same(curContHash, origContHash));
    needHash = false;
}

static void SnapGlyphHashes(void)
{
    free(ghCp); free(ghHash);
    ghCp = NULL; ghHash = NULL; ghN = 0;
    if (!hasFont || font.count == 0) return;
    ghCp = (int32_t *)malloc(sizeof(int32_t) * (size_t)font.count);
    ghHash = (char (*)[33])malloc(33 * (size_t)font.count);
    for (int i = 0; i < font.count; i++) { ghCp[i] = font.glyphs[i].codepoint; px_glyph_hash(&font.glyphs[i], ghHash[i]); }
    ghN = font.count;
}

static const char *OrigGlyphHash(int32_t cp)
{
    if (!px_code_is_codepoint(cp)) return NULL;   /* an uncoded glyph's code is renumbered by deletes: its origin is unknown */
    int lo = 0, hi = ghN;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (ghCp[mid] < cp) lo = mid + 1; else hi = mid;
    }
    return (lo < ghN && ghCp[lo] == cp) ? ghHash[lo] : NULL;
}

static void EditorBefore(void *user) { (void)user; PushUndo(0); }

static void OpenEditor(int idx)
{
    if (!hasFont || idx < 0 || idx >= font.count) return;
    if (!ed) ed = Editor_Create();
    Editor_Open(ed, &font, idx, EditorBefore, NULL);
    Editor_SetReadOnly(ed, readOnly);
    Editor_SetOriginalHash(ed, OrigGlyphHash(font.glyphs[idx].codepoint));
    if (!inEditor) { glyphTab = 0; panelScroll = 0; }   /* the glyph view opens on its Glyph tab */
    inEditor = true;
    inBrowser = false;
    SelResize();
    SelOnly(idx);
}

static void DoUndoRedo(bool redo)
{
    if (readOnly) return;
    PxFont **from = redo ? redoStack : undoStack;
    int *fn = redo ? &redoN : &undoN;
    PxFont **to = redo ? undoStack : redoStack;
    int *tn = redo ? &undoN : &redoN;
    PxtfTypeface **fromC = redo ? redoCont : undoCont;
    PxtfTypeface **toC = redo ? undoCont : redoCont;
    if (*fn == 0) return;
    PxFont *cur = (PxFont *)malloc(sizeof(PxFont));
    if (!px_font_copy(cur, &font)) { free(cur); return; }
    PxtfTypeface *curC = NULL;
    if (hasCont) {
        curC = (PxtfTypeface *)malloc(sizeof(PxtfTypeface));
        if (!pxtf_copy(curC, &cont)) { free(curC); px_font_free(cur); free(cur); return; }
    }
    PxFont *s = from[--(*fn)];
    PxtfTypeface *sc = fromC[*fn];
    fromC[*fn] = NULL;
    px_font_free(&font);
    font = *s;
    free(s);
    if (sc && hasCont) { pxtf_free(&cont); cont = *sc; free(sc); }
    else if (sc) { pxtf_free(sc); free(sc); }
    toC[*tn] = curC;
    to[(*tn)++] = cur;
    ctFocus = -1;
    needHash = true;
    lastUndoKey = 0;
    SelResize();
    SelClear(); /* indices may no longer match */
    if (inEditor) {
        int gi = Editor_GlyphIndex(ed);
        if (gi >= font.count) gi = font.count - 1;
        if (gi >= 0) OpenEditor(gi); else inEditor = false;
    }
    ToastPush(0, redo ? "Redo" : "Undo");
}

/* ------------------------------------------------------------------------------------------------
 * Modal dialogs
 * ---------------------------------------------------------------------------------------------- */
enum { M_NONE, M_CONFIRM, M_INPUT };
enum { A_NONE, A_DELETE, A_CLONE, A_CLONE_REPLACE, A_UNSAVED, A_MESSAGE, A_DELETE_ITEM, A_IMPORT_REPLACE, A_SAVEAS_REPLACE, A_OVERWRITE_NOBACKUP, A_BACKUP_FAILED };

static struct {
    int kind, action;
    char title[64], msg[800];
    char btn[3][32];
    int nbtn, primary, cancel, danger;
    char input[80];
    int frame;
    int32_t pendingCp;
} modal;
static int frameNo = 0;

static void ModalOpen(int kind, int action, const char *titleIn, const char *msg, int nbtn, const char *b0, const char *b1,
                      const char *b2, int primary, int cancel, bool danger)
{
    memset(&modal, 0, sizeof modal);
    modal.kind = kind; modal.action = action;
    snprintf(modal.title, sizeof modal.title, "%s", titleIn);
    snprintf(modal.msg, sizeof modal.msg, "%s", msg);
    modal.nbtn = nbtn;
    if (b0) snprintf(modal.btn[0], 32, "%s", b0);
    if (b1) snprintf(modal.btn[1], 32, "%s", b1);
    if (b2) snprintf(modal.btn[2], 32, "%s", b2);
    modal.primary = primary; modal.cancel = cancel;
    modal.danger = danger ? primary : -1;
    modal.frame = frameNo;
}

static void ShowError(const char *titleIn, const char *msg)
{
    ModalOpen(M_CONFIRM, A_MESSAGE, titleIn, msg, 1, "OK", NULL, NULL, 0, 0, false);
}

/* ------------------------------------------------------------------------------------------------
 * Settings (pxfe.ini beside the exe; the browser keeps its own [browser] section)
 * ---------------------------------------------------------------------------------------------- */
static const char *IniPath(void)
{
    static char p[PATH_MAX_LEN];
    if (iniOverride[0]) return iniOverride;
    snprintf(p, sizeof p, "%spxfe.ini", GetApplicationDirectory());
    Slashes(p);
    return p;
}

/* A key of the [app] section of pxfe.ini (the browser keeps its own [browser] section). */
static bool IniGetApp(const char *key, char *out, size_t cap)
{
    size_t n = 0;
    char *t = px_read_file(IniPath(), &n);
    if (!t) return false;
    size_t kl = strlen(key);
    bool in = false, found = false;
    for (char *line = t; line && *line;) {
        char *e = strchr(line, '\n');
        if (e) *e = 0;
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[l - 1] = 0;
        if (line[0] == '[') in = !strcmp(line, "[app]");
        else if (in && !strncmp(line, key, kl) && line[kl] == '=') { snprintf(out, cap, "%s", line + kl + 1); found = out[0] != 0; }
        line = e ? e + 1 : NULL;
    }
    free(t);
    return found;
}

static void IniSetApp(const char *key, const char *val)
{
    size_t n = 0;
    char *t = px_read_file(IniPath(), &n);
    size_t cap = (t ? n : 0) + strlen(key) + strlen(val) + 64;
    char *o = (char *)malloc(cap);
    size_t on = 0, kl = strlen(key);
    bool in = false, sawSection = false, wrote = false;
    for (char *line = t; line && *line;) {
        char *e = strchr(line, '\n');
        if (e) *e = 0;
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[l - 1] = 0;
        bool skip = false;
        if (line[0] == '[') {
            if (in && !wrote) { on += (size_t)snprintf(o + on, cap - on, "%s=%s\n", key, val); wrote = true; }
            in = !strcmp(line, "[app]");
            if (in) sawSection = true;
        } else if (in && !strncmp(line, key, kl) && line[kl] == '=') skip = true;
        if (!skip) on += (size_t)snprintf(o + on, cap - on, "%s\n", line);
        line = e ? e + 1 : NULL;
    }
    if (in && !wrote) { on += (size_t)snprintf(o + on, cap - on, "%s=%s\n", key, val); wrote = true; }
    if (!sawSection) on += (size_t)snprintf(o + on, cap - on, "[app]\n%s=%s\n", key, val);
    px_write_file(IniPath(), o, on);
    free(o);
    free(t);
}

static bool IniGetLastDb(char *out, size_t cap) { return IniGetApp("lastPxfdb", out, cap); }
static void IniSetLastDb(const char *path) { IniSetApp("lastPxfdb", path); }

static void PanelSetTab(int tab)   /* a click on the tab control: remembered in pxfe.ini */
{
    if (tab == panelTab || tab < 0 || tab > 3) return;
    panelTab = tab;
    panelScroll = 0;
    char v[8];
    snprintf(v, sizeof v, "%d", tab);
    IniSetApp("panelTab", v);
}

/* ------------------------------------------------------------------------------------------------
 * The database, documents: opening originals, duplicating, committing a duplicate into memory
 * ---------------------------------------------------------------------------------------------- */

/* A duplicate is committed into memory (as a font, and a typeface when it has one) when its font view is left, before Save,
 * Export, Load and quit.  A later commit of the same duplicate replaces the earlier one. */
static struct {
    bool valid;                     /* the duplicate has been committed once */
    StoreSaveResult res;            /* what the commit did (the hashes, and what was new) */
    char hash[33], contHash[33];    /* the state of the duplicate (curHash, curContHash) that was committed */
} dupCommit;

/* The current commits of the duplicates that were left (not the one open): what they name is never removed by a replacement. */
static struct { StoreSaveResult r[64]; int n; Store *forStore; } liveDups;

static void LiveDupsFor(void)
{
    if (liveDups.forStore != store) { liveDups.n = 0; liveDups.forStore = store; }
}

static void LiveDupsRetire(void)    /* the open duplicate's commit joins the list of earlier duplicates */
{
    LiveDupsFor();
    if (!dupCommit.valid) return;
    for (int i = 0; i < liveDups.n; i++)
        if (!strcmp(liveDups.r[i].pxfm, dupCommit.res.pxfm) && !strcmp(liveDups.r[i].pxtf, dupCommit.res.pxtf)) {
            liveDups.r[i].newPxfm |= dupCommit.res.newPxfm;
            liveDups.r[i].newPxtf |= dupCommit.res.newPxtf;
            return;
        }
    if (liveDups.n < 64) liveDups.r[liveDups.n++] = dupCommit.res;
}

static bool DupPending(void)
{
    if (!hasFont || readOnly || !store) return false;
    EnsureHashes();
    if (dupCommit.valid) return !Same(curHash, dupCommit.hash) || (hasCont && !Same(curContHash, dupCommit.contHash));
    return modified;
}

/* memory differs from the file as loaded or last saved, or a duplicate has edits that are not in memory yet */
static bool DbDirty(void)
{
    return store && (store_modified(store) || DupPending());
}

static const char *DbName(void)
{
    return store ? BaseName(store_info(store)->path) : "";
}

static char winTitle[PATH_MAX_LEN + 64];   /* the window title (play.inc asserts on it) */

static void UpdateTitle(void)
{
    char t[PATH_MAX_LEN + 64];
    if (store) snprintf(t, sizeof t, "%.1000s%s", DbName(), DbDirty() ? " \xE2\x80\x94 modified" : "");
    else snprintf(t, sizeof t, "pxfe");
    if (strcmp(t, winTitle) != 0) { SetWindowTitle(t); snprintf(winTitle, sizeof winTitle, "%s", t); }
}

/* "Family Style" of a typeface variant, else the first 8 digits of the font hash */
static void NameOf(const PxtfVariant *v, const char *hash, char *out, size_t cap)
{
    const char *fam = v && v->text[PXTF_FAMILY] ? v->text[PXTF_FAMILY] : "";
    const char *sty = v && v->text[PXTF_STYLE] ? v->text[PXTF_STYLE] : "";
    const char *full = v && v->text[PXTF_FULLNAME] ? v->text[PXTF_FULLNAME] : "";
    if (fam[0] && sty[0]) snprintf(out, cap, "%s %s", fam, sty);
    else if (fam[0]) snprintf(out, cap, "%s", fam);
    else if (full[0]) snprintf(out, cap, "%s", full);
    else snprintf(out, cap, "%.8s", hash);
}

static bool CommitDuplicate(void);

/* Takes over *nf and *nc (nc may be NULL).  origH / origContH: the baseline hashes ("" = the state as loaded). */
static void AdoptDocument(PxFont *nf, PxtfTypeface *nc, int variant, bool ro, const char *titleIn, const char *origTitleIn,
                          const char *origH, const char *origContH, const char *docP, const char *srcP)
{
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) swallowMouse = true;
    CommitDuplicate();      /* the document being left */
    LiveDupsRetire();
    dupCommit.valid = false;
    StackClear(undoStack, &undoN);
    StackClear(redoStack, &redoN);
    if (hasFont) px_font_free(&font);
    if (hasCont) pxtf_free(&cont);
    font = *nf;
    hasFont = true;
    hasCont = nc != NULL;
    if (nc) cont = *nc;
    contVariant = variant;
    readOnly = ro;
    snprintf(title, sizeof title, "%s", titleIn);
    snprintf(origTitle, sizeof origTitle, "%s", origTitleIn);
    snprintf(docPath, sizeof docPath, "%s", docP);
    snprintf(sourcePath, sizeof sourcePath, "%s", srcP);
    curHash[0] = 0;
    EnsureHashes();   /* the current state */
    snprintf(origHash, sizeof origHash, "%s", origH && origH[0] ? origH : curHash);
    snprintf(origContHash, sizeof origContHash, "%s", hasCont ? (origContH && origContH[0] ? origContH : curContHash) : "");
    needHash = true;
    EnsureHashes();
    SnapGlyphHashes();
    selN = 0;
    SelClear();
    scrollY = 0;
    panelScroll = 0;
    revealIdx = -1;
    lastUndoKey = 0;
    inEditor = false;
    inBrowser = false;
    inFinder = false;
    {   /* a zoom that fits the font box in about 56 px */
        int a, b;
        px_font_box(&font, &a, &b);
        int bh = a + b;
        zoom = bh > 0 ? 56 / bh : 4;
        if (zoom < 1) zoom = 1;
        if (zoom > 8) zoom = 8;
    }
}

/* the document goes (another database is installed) */
static void DropDocument(void)
{
    StackClear(undoStack, &undoN);
    StackClear(redoStack, &redoN);
    if (hasFont) px_font_free(&font);
    if (hasCont) pxtf_free(&cont);
    hasFont = false;
    hasCont = false;
    readOnly = false;
    inEditor = false;
    curHash[0] = curContHash[0] = origHash[0] = origContHash[0] = 0;
    title[0] = origTitle[0] = docPath[0] = sourcePath[0] = 0;
    modified = false;
    LiveDupsRetire();
    dupCommit.valid = false;
    SnapGlyphHashes();
    selN = 0;
    SelClear();
}

/* ---- views ---- */
static void ShowLibrary(void)
{
    if (!browser) { ToastPush(2, "No database is loaded"); return; }
    CommitDuplicate();
    inBrowser = true;
    inFinder = false;
}

static void ShowGlyphs(void)
{
    if (!finder) { ToastPush(2, "No database is loaded"); return; }
    if (!inFinder) wasLibrary = inBrowser || !hasFont;
    CommitDuplicate();
    inFinder = true;
    inBrowser = false;
}

static void ShowFont(void)
{
    if (!hasFont) return;
    inBrowser = false;
    inFinder = false;
}

/* the typeface variant (in the open library) that names a pxfm */
static void TypefaceOfPxfm(const char *pxfm, char pxtfOut[33], int *variant)
{
    pxtfOut[0] = 0;
    *variant = 0;
    if (!browser) return;
    const Library *L = Browser_Lib(browser);
    int i = library_find_by_pxfm(L, LIB_TYPEFACE, pxfm);
    if (i >= 0) { snprintf(pxtfOut, 33, "%s", L->e[i].pxtf); *variant = L->e[i].variant; }
}

/* Open a font of the open library read-only (an original).  pxtfHash / variant: its typeface, if known (else the first typeface
 * variant that names the font).  selCode >= 0 selects that glyph and scrolls it into view. */
static bool OpenStoreFont(const char *pxfm, const char *pxtfHash, int variant, int32_t selCode)
{
    char err[PX_ERR_LEN] = "";
    if (!store) { ShowError("Cannot open font", "No database is loaded."); return false; }
    CommitDuplicate();      /* the font being left (the browser then lists what it committed) */
    StoreConn *conn = store_main_conn(store);
    PxFont nf;
    px_font_init(&nf);
    if (!store_load_font(conn, pxfm, NULL, 0, &nf, err)) {
        px_font_free(&nf);
        ShowError("Cannot open font", err[0] ? err : "Unknown error.");
        return false;
    }
    char tfh[33] = "";
    int var = variant;
    if (pxtfHash && pxtfHash[0]) snprintf(tfh, sizeof tfh, "%s", pxtfHash);
    else TypefaceOfPxfm(pxfm, tfh, &var);
    PxtfTypeface nc;
    pxtf_init(&nc);
    bool haveC = false;
    if (tfh[0]) {
        if (store_load_typeface(conn, tfh, &nc, err)) {
            haveC = true;
            if (var <= 0 || !pxtf_find_variant(&nc, var)) {
                var = 0;
                for (int i = 0; i < nc.count; i++) if (Same(nc.v[i].pxfm, nf.fileHash)) { var = nc.v[i].number; break; }
                if (!var) var = nc.count ? nc.v[0].number : 0;
            }
        } else { pxtf_free(&nc); pxtf_init(&nc); }
    }
    char name[256], dp[64];
    NameOf(haveC ? pxtf_find_variant(&nc, var) : NULL, nf.fileHash, name, sizeof name);
    snprintf(dp, sizeof dp, "%s.pxfm", nf.fileHash);
    AdoptDocument(&nf, haveC ? &nc : NULL, haveC ? var : 0, true, name, name, nf.fileHash, haveC ? tfh : "", dp, dp);
    if (selCode >= 0) {
        int gi = px_font_find(&font, selCode);
        if (gi >= 0) { SelOnly(gi); revealIdx = gi; }
    }
    return true;
}

/* A typeface of the library: open the font of the given variant (or of its first variant that is in the library). */
static bool OpenStoreTypeface(const char *pxtfHash, int variant)
{
    char err[PX_ERR_LEN] = "";
    if (!store) { ShowError("Cannot open typeface", "No database is loaded."); return false; }
    PxtfTypeface c;
    pxtf_init(&c);
    if (!store_load_typeface(store_main_conn(store), pxtfHash, &c, err)) {
        pxtf_free(&c);
        ShowError("Cannot open typeface", err[0] ? err : "Unknown error.");
        return false;
    }
    int num = 0;
    char pxfm[33] = "";
    for (int i = 0; i < c.count && !num; i++)
        if ((variant <= 0 || c.v[i].number == variant) && store_has_pxfm(store_main_conn(store), c.v[i].pxfm)) { num = c.v[i].number; snprintf(pxfm, sizeof pxfm, "%s", c.v[i].pxfm); }
    for (int i = 0; i < c.count && !num; i++)
        if (store_has_pxfm(store_main_conn(store), c.v[i].pxfm)) { num = c.v[i].number; snprintf(pxfm, sizeof pxfm, "%s", c.v[i].pxfm); }
    pxtf_free(&c);
    if (!num) { ShowError("Cannot open typeface", "None of the fonts of this typeface is in the database."); return false; }
    return OpenStoreFont(pxfm, pxtfHash, num, -1);
}

static void DuplicateToEdit(void)
{
    if (!hasFont || !readOnly) return;
    PxFont nf;
    if (!px_font_copy(&nf, &font)) { ToastPush(2, "Out of memory"); return; }
    px_font_free(&font);
    font = nf;
    if (hasCont) {
        PxtfTypeface nc;
        pxtf_init(&nc);
        if (pxtf_copy(&nc, &cont)) { pxtf_free(&cont); cont = nc; }
        else pxtf_free(&nc);
    }
    readOnly = false;
    snprintf(title, sizeof title, "Duplicate of %.200s", origTitle);
    dupCommit.valid = false;
    StackClear(undoStack, &undoN);
    StackClear(redoStack, &redoN);
    if (inEditor && ed) Editor_SetReadOnly(ed, false);
    needHash = true;
    ToastPush(1, "Duplicate created: you are editing a copy");
}

/* Commits the duplicate being edited into memory (a font, and a typeface when it has one), replacing its earlier commit.  The
 * browser is rescanned when something changed.  False when the commit failed (an error is shown). */
static bool CommitDuplicate(void)
{
    if (!hasFont || readOnly || !store) return true;
    EnsureHashes();
    if (!DupPending()) return true;
    char err[PX_ERR_LEN] = "";
    StoreSaveResult earlier = dupCommit.res;
    bool hadEarlier = dupCommit.valid;
    bool changed = false;
    LiveDupsFor();
    StoreSaveResult *others[65];
    int nOthers = 0;
    for (int i = 0; i < liveDups.n; i++) others[nOthers++] = &liveDups.r[i];
    if (!modified) {
        /* edited back to the original: nothing is committed, the earlier commit goes */
        if (hadEarlier) {
            StoreSaveResult none;
            memset(&none, 0, sizeof none);
            if (!store_replace_commit(store, &earlier, &none, others, nOthers, err)) { ShowError("Cannot commit the duplicate", err[0] ? err : "Unknown error."); return false; }
            changed = true;
        }
        dupCommit.valid = false;
    } else {
        StoreSaveResult res;
        memset(&res, 0, sizeof res);
        if (!store_save_to_memory(store, &font, hasCont ? &cont : NULL, hasCont ? contVariant : 0, &res, err)) {
            ShowError("Cannot commit the duplicate", err[0] ? err : "Unknown error.");
            return false;
        }
        if (hadEarlier && !store_replace_commit(store, &earlier, &res, others, nOthers, err)) { ShowError("Cannot replace the earlier commit", err[0] ? err : "Unknown error."); return false; }
        changed = res.newPxfm || res.newPxtf || hadEarlier;
        dupCommit.valid = true;
        dupCommit.res = res;
        snprintf(dupCommit.hash, sizeof dupCommit.hash, "%s", curHash);
        snprintf(dupCommit.contHash, sizeof dupCommit.contHash, "%s", curContHash);
        snprintf(lastPxfm, sizeof lastPxfm, "%s", res.pxfm);
        snprintf(lastPxtf, sizeof lastPxtf, "%s", res.pxtf);
        lastPxfmPath[0] = lastPxtfPath[0] = 0;
    }
    if (changed && browser) Browser_Rescan(browser);
    return true;
}

/* ---- the library on a worker thread: Load, Import and Export (the first build of a big folder takes minutes) ---- */
enum { JOB_LOAD, JOB_IMPORT, JOB_EXPORT };
enum { AFTER_LIBRARY, AFTER_KEEP, AFTER_FONT, AFTER_TYPEFACE };

static struct {
    bool active;
    void *thread;
    volatile long finished;
    int kind;
    char path[PATH_MAX_LEN];       /* load: the database file; import and export: the folder */
    char target[PATH_MAX_LEN];     /* import: the new database file; export: the backup file ("" none) */
    int mode;                      /* export: StoreExportMode */
    volatile long stage;           /* export: 0 the backup, 1 the files */
    StoreProgress prog;
    Store *result;
    StoreExportResult xres;
    bool ok, backupFailed;
    char err[PX_ERR_LEN];
    double t0, seconds;
    int after;
    char afterHash[33];
    int holdMs;                    /* debug: keep the progress screen at least this long */
} job;

/* what the command line asked for once the font is there */
static struct { bool pending, dup, browser; int zoom, edit, panelScroll; const char *sel; char font[40], typeface[40]; } startup;
static void ApplyStartup(void);

static void JobThread(void *arg)
{
    (void)arg;
    if (job.kind == JOB_LOAD) job.ok = store_open_pxfdb(job.path, &job.prog, &job.result, job.err);
    else if (job.kind == JOB_IMPORT) job.ok = store_import_folder(job.path, job.target, &job.prog, &job.result, job.err);
    else {
        job.ok = true;
        if (job.target[0]) {
            job.stage = 0;
            if (!store_import_to_file(job.path, job.target, &job.prog, job.err)) { job.ok = false; job.backupFailed = true; }
            job.prog.phase = 0; job.prog.done = job.prog.total = 0;
        }
        job.stage = 1;
        if (job.ok) job.ok = store_export_folder(store, job.path, (StoreExportMode)job.mode, &job.prog, &job.xres, job.err);
    }
    job.finished = 1;
}

static void CloseBrowser(void)
{
    if (!browser) return;
    Browser_SaveSettings(browser, IniPath());
    Browser_Destroy(browser);
    browser = NULL;
}

/* closes the browser, the finder, the insight and the store, in that order (the browser's loader reads the store) */
static void CloseLibrary(void)
{
    CloseBrowser();
    if (finder) { Finder_Destroy(finder); finder = NULL; }
    if (insight) { Insight_Destroy(insight); insight = NULL; }
    if (store) { store_close(store); store = NULL; }
    inBrowser = false;
    inFinder = false;
}

static bool InstallStore(Store *s)
{
    char err[PX_ERR_LEN] = "";
    DropDocument();
    CloseLibrary();
    store = s;
    liveDups.n = 0;
    liveDups.forStore = s;
    snprintf(libRoot, sizeof libRoot, "%.1000s", store_info(store)->path);
    Slashes(libRoot);
    insight = Insight_Create(store);
    finder = Finder_Create(store, insight);
    browser = Browser_Create(store, insight, err);
    if (!browser) {
        ShowError("Cannot load database", err[0] ? err : "Unknown error.");
        return false;
    }
    Browser_LoadSettings(browser, IniPath());
    IniSetLastDb(libRoot);
    return true;
}

/* ---- the dialogs of the files (scripts override them with setdialog) ---- */
static bool DlgSave(char *out, int size, const char *defaultPath)
{
    if (dlgSavePath[0]) { snprintf(out, (size_t)size, "%s", dlgSavePath); return true; }
    if (noDialogs) return false;
    return Win_SaveFileDialog(out, size, PXFDB_FILTER, defaultPath, "pxfdb");
}

static bool FileExistsAt(const char *p) { return FileExists(p); }

/* ---- Load, Import, Export ---- */
static bool JobBegin(int kind, const char *path, const char *target, int mode, int after, const char *afterHash)
{
    if (job.active) { ToastPush(2, "Another file operation is running"); return false; }
    memset(&job.prog, 0, sizeof job.prog);
    job.result = NULL;
    job.ok = false;
    job.backupFailed = false;
    job.err[0] = 0;
    job.finished = 0;
    job.kind = kind;
    job.mode = mode;
    job.stage = 1;
    memset(&job.xres, 0, sizeof job.xres);
    snprintf(job.path, sizeof job.path, "%s", path);
    Slashes(job.path);
    size_t n = strlen(job.path);
    while (n > 3 && job.path[n - 1] == '/') job.path[--n] = 0;
    snprintf(job.target, sizeof job.target, "%s", target ? target : "");
    Slashes(job.target);
    job.after = after;
    snprintf(job.afterHash, sizeof job.afterHash, "%s", afterHash ? afterHash : "");
    job.t0 = GetTime();
    job.thread = Win_StartThread(JobThread, NULL);
    if (!job.thread) { ShowError("Cannot start", "Cannot start the worker thread."); return false; }
    job.active = true;
    return true;
}

static bool LoadPath(const char *path, int after, const char *afterHash)
{
    if (!FileExists(path)) { ShowError("Cannot load database", "This file does not exist."); return false; }
    return JobBegin(JOB_LOAD, path, "", 0, after, afterHash);
}

/* Import: the folder is checked for data, the new database file is chosen (default <folder>/pxfdb.pxfdb), then the worker runs */
static struct { char folder[PATH_MAX_LEN], target[PATH_MAX_LEN]; } imp;

static void ImportRun(void)
{
    int after = startup.font[0] ? AFTER_FONT : startup.typeface[0] ? AFTER_TYPEFACE : AFTER_LIBRARY;
    JobBegin(JOB_IMPORT, imp.folder, imp.target, 0, after, startup.font[0] ? startup.font : startup.typeface);
}

static void ImportChooseTarget(const char *folder, const char *targetGiven)
{
    StoreFolderScan sc;
    store_scan_folder(folder, &sc);
    if (!store_scan_has_data(&sc)) {
        ShowError("Cannot import", "This folder holds no pxtf, pxfm or pxgl files.\nChoose a folder that contains the folders pxtf, pxfm and pxgl.");
        return;
    }
    snprintf(imp.folder, sizeof imp.folder, "%s", folder);
    Slashes(imp.folder);
    char def[PATH_MAX_LEN + 32];
    char f[PATH_MAX_LEN];
    snprintf(f, sizeof f, "%s", imp.folder);
    size_t fl = strlen(f);
    while (fl > 1 && f[fl - 1] == '/') f[--fl] = 0;
    snprintf(def, sizeof def, "%s/pxfdb.pxfdb", f);
    if (targetGiven && targetGiven[0]) snprintf(imp.target, sizeof imp.target, "%s", targetGiven);
    else if (!DlgSave(imp.target, sizeof imp.target, def)) return;
    Slashes(imp.target);
    if (FileExistsAt(imp.target) && !(targetGiven && targetGiven[0])) {
        char msg[PATH_MAX_LEN + 200];
        snprintf(msg, sizeof msg, "%s already exists.\nThe imported database replaces it.", imp.target);
        ModalOpen(M_CONFIRM, A_IMPORT_REPLACE, "Replace file?", msg, 2, "Cancel", "Replace", NULL, 1, 0, true);
        return;
    }
    ImportRun();
}

static void ImportDialog(void)
{
    char dir[PATH_MAX_LEN];
    if (DlgFolder(dir, sizeof dir, "Choose the folder to import (it contains pxtf/, pxfm/ and pxgl/)")) ImportChooseTarget(dir, "");
}

static void LoadDialog(void)
{
    char path[PATH_MAX_LEN];
    if (DlgDb(path, sizeof path)) LoadPath(path, AFTER_LIBRARY, "");
}

/* Export: the folder is scanned; an empty one is written at once, one with library data asks Append / Overwrite / Cancel */
static struct {
    bool open;
    char folder[PATH_MAX_LEN];
    StoreFolderScan scan;
    bool backup;
    char backupPath[PATH_MAX_LEN];
    int frame;
} xd;

static void DefaultBackupPath(const char *folderIn, char *out, size_t cap)
{
    char f[PATH_MAX_LEN], parent[PATH_MAX_LEN], stamp[32];
    snprintf(f, sizeof f, "%s", folderIn);
    Slashes(f);
    size_t fl = strlen(f);
    while (fl > 1 && f[fl - 1] == '/') f[--fl] = 0;
    DirName(f, parent, sizeof parent);
    const char *base = BaseName(f);
    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M", localtime(&t));
    snprintf(out, cap, "%.700s/%.200s-backup-%s.pxfdb", parent[0] ? parent : ".", base[0] ? base : "library", stamp);
}

static void ExportRun(const char *folder, int mode, const char *backupPath)
{
    JobBegin(JOB_EXPORT, folder, backupPath, mode, AFTER_KEEP, "");
}

static void ExportDialogOpen(const char *folder)
{
    memset(&xd, 0, sizeof xd);
    snprintf(xd.folder, sizeof xd.folder, "%s", folder);
    Slashes(xd.folder);
    store_scan_folder(xd.folder, &xd.scan);
    if (!store_scan_has_data(&xd.scan)) { ExportRun(xd.folder, STORE_EXPORT_APPEND, ""); return; }
    xd.open = true;
    xd.frame = frameNo;
    xd.backup = true;
    DefaultBackupPath(xd.folder, xd.backupPath, sizeof xd.backupPath);
}

static void ExportStart(void)
{
    if (!store) { ToastPush(2, "No database is loaded"); return; }
    if (!CommitDuplicate()) return;
    char dir[PATH_MAX_LEN];
    if (DlgFolder(dir, sizeof dir, "Choose the folder to export to (the files go to pxtf/, pxfm/ and pxgl/)")) ExportDialogOpen(dir);
}

/* a backup file inside pxtf/ pxfm/ pxgl/ of the folder would be deleted by the overwrite */
static bool PathInsideLibraryFolders(const char *folder, const char *path)
{
    static const char *const sub[3] = {"pxtf", "pxfm", "pxgl"};
    char f[PATH_MAX_LEN];
    snprintf(f, sizeof f, "%s", folder);
    size_t fl = strlen(f);
    while (fl > 1 && f[fl - 1] == '/') f[--fl] = 0;
    for (int i = 0; i < 3; i++) {
        char p[PATH_MAX_LEN + 8];
        snprintf(p, sizeof p, "%s/%s/", f, sub[i]);
        if (!strncmp(path, p, strlen(p))) return true;
    }
    return false;
}

static void ExportOverwriteChosen(void)
{
    if (xd.backup) {
        Slashes(xd.backupPath);
        if (!xd.backupPath[0]) { ShowError("Cannot back up", "Choose a file for the backup, or switch the backup off."); return; }
        if (PathInsideLibraryFolders(xd.folder, xd.backupPath)) { ShowError("Cannot back up", "The backup file must not be inside the folders pxtf, pxfm or pxgl: the overwrite would delete it."); return; }
        ExportRun(xd.folder, STORE_EXPORT_OVERWRITE, xd.backupPath);
        xd.open = false;
    } else {
        char msg[400], c[160];
        store_counts_text(&xd.scan.found, c, sizeof c);
        snprintf(msg, sizeof msg, "Overwrite deletes the %s there without a backup.\nThis cannot be undone.", c);
        xd.open = false;
        ModalOpen(M_CONFIRM, A_OVERWRITE_NOBACKUP, "Overwrite without a backup?", msg, 2, "Cancel", "Overwrite", NULL, 1, 0, true);
    }
}

static void ExportFinished(void)
{
    if (job.backupFailed) {
        char msg[PX_ERR_LEN + 300];
        snprintf(msg, sizeof msg, "The backup could not be made, and nothing was deleted:\n%s\nOverwrite without a backup?", job.err);
        snprintf(xd.folder, sizeof xd.folder, "%s", job.path);
        ModalOpen(M_CONFIRM, A_BACKUP_FAILED, "The backup failed", msg, 2, "Cancel", "Overwrite Without Backup", NULL, 1, 0, true);
        return;
    }
    if (!job.ok) { ShowError("Cannot export", job.err[0] ? job.err : "Unknown error."); return; }
    const StoreExportResult *r = &job.xres;
    char w[200], sk[200], dl[200], msg[PATH_MAX_LEN + 700];
    store_counts_text(&r->written, w, sizeof w);
    store_counts_text(&r->skipped, sk, sizeof sk);
    store_counts_text(&r->deleted, dl, sizeof dl);
    size_t o = (size_t)snprintf(msg, sizeof msg, "Wrote %s.", w);
    if (r->skipped.fonts + r->skipped.typefaces + r->skipped.glyphs) o += (size_t)snprintf(msg + o, sizeof msg - o, "\nSkipped %s (already there).", sk);
    if (r->deleted.fonts + r->deleted.typefaces + r->deleted.glyphs) o += (size_t)snprintf(msg + o, sizeof msg - o, "\nDeleted %s.", dl);
    if (job.target[0]) o += (size_t)snprintf(msg + o, sizeof msg - o, "\nBackup: %s", job.target);
    snprintf(exportSummary, sizeof exportSummary, "%s", msg);
    if (lastPxfm[0]) snprintf(lastPxfmPath, sizeof lastPxfmPath, "%.900s/pxfm/%.2s/%s.pxfm", job.path, lastPxfm, lastPxfm);
    if (lastPxtf[0]) snprintf(lastPxtfPath, sizeof lastPxtfPath, "%.900s/pxtf/%.2s/%s.pxtf", job.path, lastPxtf, lastPxtf);
    ModalOpen(M_CONFIRM, A_MESSAGE, "Export finished", msg, 1, "OK", NULL, NULL, 0, 0, false);
}

static void JobFinish(void)
{
    Win_JoinThread(job.thread);
    job.thread = NULL;
    job.active = false;
    job.seconds = GetTime() - job.t0;
    if (job.kind == JOB_EXPORT) { ExportFinished(); return; }
    if (!job.ok) {
        ShowError(job.kind == JOB_IMPORT ? "Cannot import folder" : "Cannot load database", job.err[0] ? job.err : "Unknown error.");
        startup.pending = false;
        return;
    }
    if (!InstallStore(job.result)) return;
    const StoreInfo *si = store_info(store);
    if (job.kind == JOB_IMPORT) ToastPush(1, "Imported %d fonts, %d glyphs into %s (%.1f s)", si->nPxfm, si->nGlyph, DbName(), job.seconds);
    else ToastPush(0, "Loaded %s: %d fonts, %d glyphs (%.1f s)", DbName(), si->nPxfm, si->nGlyph, job.seconds);
    if (getenv("PXFE_TRACE")) { printf("TRACE %s fonts=%d glyphs=%d seconds=%.2f\n", job.kind == JOB_IMPORT ? "import" : "load", si->nPxfm, si->nGlyph, job.seconds); fflush(stdout); }
    if (job.after == AFTER_LIBRARY) { inBrowser = true; inFinder = false; }
    else if (job.after == AFTER_KEEP) { if (!hasFont) inBrowser = true; }
    else if (job.after == AFTER_FONT || job.after == AFTER_TYPEFACE) {
        bool ok = job.after == AFTER_FONT ? OpenStoreFont(job.afterHash, "", 0, -1) : OpenStoreTypeface(job.afterHash, 0);
        if (ok && startup.pending && startup.dup) DuplicateToEdit();
        if (!ok) inBrowser = true;
    }
    if (startup.pending && hasFont) ApplyStartup();
    startup.pending = false;
}

static void JobAbort(void)
{
    if (!job.active) return;
    job.prog.cancel = 1;
    Win_JoinThread(job.thread);
    job.thread = NULL;
    job.active = false;
    if (job.ok && job.result) store_close(job.result);
}

/* ---- the progress screen ---- */
static const char *PhaseLabel(long ph)
{
    switch (ph) {
    case 1: return "Listing the folders";
    case 3: return "Reading and checking the files";
    case 4: return "Building the database";
    case 5: return "Indexing";
    case 7: return "Loading the database into memory";
    case 8: return "Writing the database file";
    case 10: return "Deleting the old files";
    case 11: return "Writing the files";
    default: return "Starting";
    }
}

static void DrawProgressScreen(int W, int H)
{
    DrawRectangle(0, 0, W, H, TH_BG);
    const float cw = 580, ch = 244;
    Rectangle c = {Fl((W - cw) / 2), Fl((H - ch) / 2.4f), cw, ch};
    DrawRectangleRounded((Rectangle){c.x - 1, c.y + 4, c.width + 2, c.height + 2}, 0.04f, 10, (Color){0, 0, 0, 20});
    RRect(c, TH_PANEL, TH_LINE);
    float x = c.x + 32, w = c.width - 64, y = c.y + 28;
    const char *head = job.kind == JOB_LOAD ? "Loading database"
                     : job.kind == JOB_IMPORT ? "Importing folder"
                     : job.stage == 0 ? "Making a backup" : "Exporting to folder";
    Th_Text(head, x, y, TH_TEXT);
    Th_Text(head, x + 1, y, TH_TEXT);
    y += 28;
    char fit[PATH_MAX_LEN + 8];
    HeadFit(job.path, w, fit, sizeof fit);
    Th_TextMono(fit, x, y, TH_TEXT_DIM);
    y += 34;
    long ph = job.prog.phase, done = job.prog.done, total = job.prog.total;
    Th_Text(PhaseLabel(ph), x, y, TH_TEXT);
    char cnt[96] = "";
    if (ph != 7 && ph != 8 && total > 0) snprintf(cnt, sizeof cnt, "%ld of %ld", done, total);
    if (cnt[0]) { Vector2 m = Th_Measure(cnt); Th_Text(cnt, Fl(x + w - m.x), y, TH_TEXT_DIM); }
    y += 28;
    Rectangle bar = {x, y, w, 10};
    DrawRectangleRounded(bar, 1.0f, 8, (Color){0xE8, 0xE8, 0xEC, 0xFF});
    bool known = ph != 7 && ph != 8 && total > 0;
    if (known) {
        float frac = (float)done / (float)total;
        if (frac > 1) frac = 1;
        float fw = bar.width * frac;
        if (fw < 10) fw = 10;
        DrawRectangleRounded((Rectangle){bar.x, bar.y, fw, bar.height}, 1.0f, 8, TH_ACCENT);
    } else {   /* no count (yet): a bar that travels */
        float seg = bar.width * 0.28f, ph2 = (float)fmod(GetTime() * 0.9, 1.0);
        float sx = bar.x + (bar.width + seg) * ph2 - seg;
        float x0 = sx < bar.x ? bar.x : sx, x1 = sx + seg > bar.x + bar.width ? bar.x + bar.width : sx + seg;
        if (x1 > x0) DrawRectangleRounded((Rectangle){x0, bar.y, x1 - x0, bar.height}, 1.0f, 8, TH_ACCENT);
    }
    y += 26;
    char el[96];
    snprintf(el, sizeof el, "%.1f s", GetTime() - job.t0);
    Th_TextSmall(el, x, y, TH_TEXT_DIM);
    char note[PATH_MAX_LEN + 120], noteFit[PATH_MAX_LEN + 120];
    if (job.kind == JOB_LOAD) snprintf(note, sizeof note, "The file is copied into memory; it is not changed.");
    else if (job.kind == JOB_IMPORT) snprintf(note, sizeof note, "Every file is checked; the database goes to %s", job.target);
    else if (job.stage == 0) snprintf(note, sizeof note, "The folder's current data goes to %s before anything is deleted.", job.target);
    else snprintf(note, sizeof note, "Memory is written to the folder; nothing outside pxtf, pxfm and pxgl is touched.");
    HeadFit(note, w, noteFit, sizeof noteFit);
    y += 22;
    Th_TextSmall(noteFit, x, y, TH_TEXT_DIM);
}

/* ---- opening a path from the command line: a .pxfdb loads, a folder is imported ---- */
static bool OpenAny(const char *pathIn, const char *importTo)
{
    char path[PATH_MAX_LEN];
    snprintf(path, sizeof path, "%s", pathIn);
    Slashes(path);
    if (DirectoryExists(path)) { ImportChooseTarget(path, importTo); return true; }
    if (!FileExists(path)) { ShowError("Cannot open", "This file or folder does not exist."); return false; }
    if (HasExt(path, ".pxfdb")) {
        int after = startup.font[0] ? AFTER_FONT : startup.typeface[0] ? AFTER_TYPEFACE : AFTER_LIBRARY;
        return LoadPath(path, after, startup.font[0] ? startup.font : startup.typeface);
    }
    ShowError("Cannot open", "pxfe loads a .pxfdb database (Load), or imports a folder of pxtf, pxfm and pxgl files (Import Folder).");
    return false;
}

static void GoBrowser(void)
{
    if (!browser) {
        char last[PATH_MAX_LEN];
        if (!job.active && IniGetLastDb(last, sizeof last) && FileExists(last)) { OpenAny(last, ""); return; }
        ToastPush(2, "No database is loaded");
        return;
    }
    ShowLibrary();
}

/* ---- Save and Save As ---- */
static bool SaveDb(void)
{
    if (!store) { ToastPush(2, "No database is loaded"); return false; }
    if (!CommitDuplicate()) return false;
    if (!store_modified(store)) { ToastPush(0, "No changes to save"); return true; }
    char err[PX_ERR_LEN] = "";
    if (!store_save(store, err)) { ShowError("Cannot save", err[0] ? err : "Unknown error."); return false; }
    ToastPush(1, "Saved %s", DbName());
    return true;
}

static char saveAsPending[PATH_MAX_LEN];

static bool DoSaveAs(const char *path)
{
    if (!store) return false;
    if (!CommitDuplicate()) return false;
    char err[PX_ERR_LEN] = "";
    double t0 = GetTime();
    if (!store_save_as(store, path, err)) { ShowError("Cannot save", err[0] ? err : "Unknown error."); return false; }
    if (getenv("PXFE_TRACE")) { printf("TRACE saveas seconds=%.2f\n", GetTime() - t0); fflush(stdout); }
    snprintf(libRoot, sizeof libRoot, "%.1000s", store_info(store)->path);
    Slashes(libRoot);
    IniSetLastDb(libRoot);
    ToastPush(1, "Saved as %s", DbName());
    return true;
}

static void SaveAsDialog(void)
{
    if (!store) { ToastPush(2, "No database is loaded"); return; }
    char path[PATH_MAX_LEN];
    if (!DlgSave(path, sizeof path, store_info(store)->path)) return;
    Slashes(path);
    if (Same(path, store_info(store)->path)) { SaveDb(); return; }
    if (FileExistsAt(path)) {
        char msg[PATH_MAX_LEN + 200];
        snprintf(msg, sizeof msg, "%s already exists.\nSaving replaces it.", path);
        snprintf(saveAsPending, sizeof saveAsPending, "%s", path);
        ModalOpen(M_CONFIRM, A_SAVEAS_REPLACE, "Replace file?", msg, 2, "Cancel", "Replace", NULL, 1, 0, true);
        return;
    }
    DoSaveAs(path);
}

/* ---- unsaved changes: Load, Import and quit ask first (Save / Discard / Cancel) ---- */
enum { G_NONE, G_LOAD_DIALOG, G_LOAD_PATH, G_IMPORT_DIALOG, G_QUIT };
static struct { int kind; char path[PATH_MAX_LEN]; } guard;

static void RunGuarded(void)
{
    int k = guard.kind;
    guard.kind = G_NONE;
    if (k == G_LOAD_DIALOG) LoadDialog();
    else if (k == G_LOAD_PATH) LoadPath(guard.path, AFTER_LIBRARY, "");
    else if (k == G_IMPORT_DIALOG) ImportDialog();
    else if (k == G_QUIT) quit = true;
}

static void Guard(int kind, const char *path)
{
    guard.kind = kind;
    snprintf(guard.path, sizeof guard.path, "%s", path ? path : "");
    if (store) {
        CommitDuplicate();
        if (DbDirty()) {
            char msg[PATH_MAX_LEN + 200];
            snprintf(msg, sizeof msg, "%s has changes that are not saved.\nSave them first, or discard them?", DbName());
            ModalOpen(M_CONFIRM, A_UNSAVED, "Unsaved changes", msg, 3, "Cancel", "Discard", "Save", 2, 0, false);
            return;
        }
    }
    RunGuarded();
}

static void RequestQuit(void)
{
    if (job.active) { quit = true; return; }
    Guard(G_QUIT, "");
}

static void HandleInsightTarget(const InsightTarget *t)
{
    switch (t->action) {
    case INSIGHT_OPEN_FONT: OpenStoreFont(t->pxfm, t->pxtf, t->variant, t->code); break;
    case INSIGHT_OPEN_TYPEFACE: OpenStoreTypeface(t->pxtf, t->variant); break;
    case INSIGHT_FIND_CODE:
        if (finder && t->code >= 0) { Finder_SetCode(finder, t->code); ShowGlyphs(); }
        break;
    default: break;
    }
}

static void RunChoice(const BrowserChoice *c)
{
    if (c->action == BROWSER_INSIGHT) HandleInsightTarget(&c->insight);
    else if (c->action == BROWSER_OPEN || c->action == BROWSER_DUPLICATE) {
        if (OpenStoreFont(c->pxfm, c->pxtf, c->variant, -1) && c->action == BROWSER_DUPLICATE) DuplicateToEdit();
    }
}

/* ---- deleting fonts and typefaces (in memory; Save writes the deletion to the database file) ---- */
static struct { StoreDeletePlan plan; bool have; bool fromView; } del;

static void AskDelete(bool isTypeface, const char *hash, bool fromView)
{
    char err[PX_ERR_LEN] = "";
    if (!store) { ShowError("Cannot delete", "No database is loaded."); return; }
    if (del.have) { store_plan_free(&del.plan); del.have = false; }
    bool ok = isTypeface ? store_plan_delete_typeface(store, hash, &del.plan, err) : store_plan_delete_font(store, hash, &del.plan, err);
    if (!ok) { ShowError("Cannot delete", err[0] ? err : "Unknown error."); return; }
    del.have = true;
    del.fromView = fromView;
    char msg[1200];
    snprintf(msg, sizeof msg, "Delete %s?\nThis is done in memory; Save writes it to the database file.", del.plan.summary);
    ModalOpen(M_CONFIRM, A_DELETE_ITEM, isTypeface ? "Delete typeface" : "Delete font", msg, 2, "Cancel", "Delete", NULL, 1, 0, true);
}

static void DeleteConfirmed(void)
{
    char err[PX_ERR_LEN] = "";
    if (!del.have) return;
    if (!store_apply_delete(store, &del.plan, err)) { ShowError("Cannot delete", err[0] ? err : "Unknown error."); return; }
    ToastPush(1, "Deleted: %s", del.plan.summary);
    if (browser) Browser_Rescan(browser);
    if (del.fromView) ShowLibrary();
}

/* ------------------------------------------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------------------------------------- */
static void RecountSel(void)
{
    SelResize();
    selCount = 0;
    for (int i = 0; i < selN; i++) if (sel[i]) selCount++;
}

static int FirstSelected(void)
{
    for (int i = 0; i < selN; i++) if (sel[i]) return i;
    return -1;
}

static void CmdSelectAll(void)
{
    SelResize();
    for (int i = 0; i < selN; i++) sel[i] = true;
}

static int CmpHash(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* Adds every glyph with the same bitmap as a selected one (same pxgl hash). Each glyph is hashed once. */
static void CmdSelectOccurrences(void)
{
    RecountSel();
    if (selCount == 0) return;
    char (*hs)[33] = (char (*)[33])malloc(sizeof(char[33]) * (size_t)font.count);
    char (*sh)[33] = (char (*)[33])malloc(sizeof(char[33]) * (size_t)selCount);
    int ns = 0, n = 0;
    for (int i = 0; i < font.count; i++) {
        px_glyph_hash(&font.glyphs[i], hs[i]); /* "" for a glyph without ink: all empty glyphs share one shape */
        if (sel[i]) memcpy(sh[ns++], hs[i], 33);
    }
    qsort(sh, (size_t)ns, 33, CmpHash);
    for (int j = 0; j < font.count; j++)
        if (!sel[j] && bsearch(hs[j], sh, (size_t)ns, 33, CmpHash)) { sel[j] = true; n++; }
    free(hs);
    free(sh);
    ToastPush(0, n ? "Added %d matching glyph%s" : "No other glyph has the same shape", n, n == 1 ? "" : "s");
}

static void CmdDelete(void)
{
    if (readOnly) return;
    PushUndo(0);
    for (int i = font.count - 1; i >= 0; i--)
        if (i < selN && sel[i]) px_font_remove_glyph(&font, i);   /* also drops the glyph's kerning pairs and a default it was */
    selN = 0;
    SelClear();
    ToastPush(0, "Deleted");
}

static bool ParseCodepoint(const char *in, int32_t *cp)
{
    char s[80];
    snprintf(s, sizeof s, "%s", in);
    char *p = s;
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n > 0 && p[n - 1] == ' ') p[--n] = 0;
    if (n == 0) return false;
    const char *hex = NULL;
    if ((p[0] == 'U' || p[0] == 'u') && p[1] == '+') hex = p + 2;
    else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) hex = p + 2;
    if (!hex) {
        int sz = 0;
        int c = GetCodepointNext(p, &sz);
        if ((size_t)sz == n && !(c == 0x3F && p[0] != '?')) { *cp = c; return c >= 0 && c <= 0x10FFFF; }
        hex = p;
    }
    size_t hn = strlen(hex);
    if (hn == 0 || hn > 6) return false;
    long v = 0;
    for (size_t i = 0; i < hn; i++) {
        char c = hex[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v * 16 + d;
    }
    if (v > 0x10FFFF) return false;
    *cp = (int32_t)v;
    return true;
}

/* Clone the selected glyph to code point cp.  Kerning is not copied; replacing an existing glyph keeps its place
 * (and its kerning pairs and default status). */
static void DoClone(int32_t cp)
{
    if (readOnly) return;
    int src = FirstSelected();
    if (src < 0) return;
    PushUndo(0);
    PxGlyph g;
    memset(&g, 0, sizeof g);
    px_glyph_copy(&g, &font.glyphs[src]);
    g.codepoint = cp;
    int ex = px_font_find(&font, cp);
    int ni;
    if (ex >= 0) {
        px_glyph_free(&font.glyphs[ex]);
        px_glyph_copy(&font.glyphs[ex], &g);
        px_glyph_touch(&font.glyphs[ex]);
        ni = ex;
    } else ni = px_font_add_glyph(&font, &g);
    px_glyph_free(&g);
    selN = 0;
    SelOnly(ni);
    revealIdx = ni;
    ToastPush(1, "Cloned to U+%04X", (unsigned)cp);
}

/* Clone the selected glyph as a new glyph without a code point (the next #n). */
static void DoCloneUncoded(void)
{
    if (readOnly) return;
    int src = FirstSelected();
    if (src < 0) return;
    PushUndo(0);
    PxGlyph g;
    memset(&g, 0, sizeof g);
    px_glyph_copy(&g, &font.glyphs[src]);
    int ni = px_font_add_uncoded(&font, &g);
    px_glyph_free(&g);
    selN = 0;
    SelOnly(ni);
    revealIdx = ni;
    ToastPush(1, "Cloned to #%d (no code point)", font.glyphs[ni].codepoint - PX_CODE_UNCODED_FIRST);
}

static void CmdClone(void)
{
    if (readOnly) { ToastPush(2, "An original is read-only: use Duplicate to Edit first"); return; }
    RecountSel();
    if (selCount != 1) { ToastPush(2, "Select exactly one glyph to clone"); return; }
    char msg[200];
    snprintf(msg, sizeof msg, "Clone the selected glyph to a new code point.\nType U+XXXX, hex digits, or a single character; or clone it without a code point.");
    ModalOpen(M_INPUT, A_CLONE, "Clone glyph", msg, 3, "Cancel", "No Code Point", "Clone", 2, 0, false);
}

static void CmdEditSelected(void)
{
    RecountSel();
    int i = FirstSelected();
    if (i >= 0) OpenEditor(i);
}

static void CmdDeleteAsk(void)
{
    if (readOnly) { ToastPush(2, "An original is read-only: use Duplicate to Edit first"); return; }
    RecountSel();
    if (selCount == 0) return;
    char msg[160];
    snprintf(msg, sizeof msg, "Delete %d selected glyph%s from the font?\nYou can undo this with Ctrl+Z.", selCount, selCount == 1 ? "" : "s");
    ModalOpen(M_CONFIRM, A_DELETE, selCount == 1 ? "Delete glyph" : "Delete glyphs", msg, 2, "Cancel", "Delete", NULL, 1, 0, true);
}

static bool CanSetDefault(void)
{
    if (!hasFont || readOnly || selCount != 1) return false;
    int i = FirstSelected();
    return i >= 0 && px_code_is_codepoint(font.glyphs[i].codepoint) && font.defaultCodepoint != font.glyphs[i].codepoint;
}

static void CmdSetDefault(void)
{
    if (readOnly) { ToastPush(2, "An original is read-only: use Duplicate to Edit first"); return; }
    RecountSel();
    int i = FirstSelected();
    if (selCount != 1 || i < 0 || !px_code_is_codepoint(font.glyphs[i].codepoint)) { ToastPush(2, "Select one glyph that has a code point"); return; }
    if (font.defaultCodepoint == font.glyphs[i].codepoint) return;
    PushUndo(0);
    px_font_set_default(&font, font.glyphs[i].codepoint);
    ToastPush(1, "U+%04X is the default glyph", (unsigned)font.glyphs[i].codepoint);
}

static int zoomAnchor = -1;   /* glyph at the top of the view when the zoom changed; keeps its place after the re-layout */

static void ApplyZoom(int z)
{
    if (z < 1) z = 1;
    if (z > 16) z = 16;
    if (z == zoom) return;
    zoom = z;
    zoomAnchor = -1;
    for (int i = 0; hasFont && i < font.count && i < lcap; i++)
        if (ly[i] + rowH > scrollY) { zoomAnchor = i; break; }
}

/* ------------------------------------------------------------------------------------------------
 * Number field (a spinner with a text box)
 * ---------------------------------------------------------------------------------------------- */
static char editName[40];             /* the numeric field being edited ("" none) */
static char editBuf[48];
static bool editFresh = false;
static char holdName[40];
static int holdDir = 0;
static double holdT = 0;

/* Spinner / value box. Returns true and *out when the user changes the value. */
static bool NumField(Rectangle r, const char *name, int val, bool enabled, int minV, int *out)
{
    bool changed = false;
    float bwid = r.width >= 100 ? 24 : 20;
    Rectangle bm = {r.x, r.y, bwid, r.height}, bp = {r.x + r.width - bwid, r.y, bwid, r.height};
    Rectangle bv = {r.x + bwid, r.y, r.width - 2 * bwid, r.height};
    bool editing = Same(editName, name);
    {
        char n[64];
        snprintf(n, sizeof n, "%s.minus", name); Th_Reg(n, bm);
        snprintf(n, sizeof n, "%s.plus", name); Th_Reg(n, bp);
        snprintf(n, sizeof n, "%s.value", name); Th_Reg(n, bv);
    }
    char text[40];
    snprintf(text, sizeof text, "%d", val);

    Color line = enabled ? (Color){0xB4, 0xB4, 0xBD, 0xFF} : TH_LINE;
    RRect(r, enabled ? TH_PANEL : TH_PANEL_ALT, line);
    if (editing) {
        RRect(r, TH_PANEL, TH_ACCENT);
    } else if (enabled) {
        if (Hov(bm)) DrawRectangle((int)bm.x + 1, (int)bm.y + 1, (int)bm.width - 1, (int)bm.height - 2, (Color){0xEE, 0xEE, 0xF2, 0xFF});
        if (Hov(bp)) DrawRectangle((int)bp.x, (int)bp.y + 1, (int)bp.width - 1, (int)bp.height - 2, (Color){0xEE, 0xEE, 0xF2, 0xFF});
    }
    Th_VLine(bm.x + bm.width, r.y + 1, r.y + r.height - 1, TH_LINE);
    Th_VLine(bp.x, r.y + 1, r.y + r.height - 1, TH_LINE);
    Color glyphC = enabled ? TH_TEXT : (Color){0xB8, 0xB8, 0xC0, 0xFF};
    float cy = Fl(r.y + r.height / 2);
    DrawRectangle((int)(bm.x + bwid / 2 - 4), (int)cy, 8, 1, glyphC);
    DrawRectangle((int)(bp.x + bwid / 2 - 4), (int)cy, 8, 1, glyphC);
    DrawRectangle((int)(bp.x + bwid / 2 - 0.5f), (int)cy - 4, 1, 9, glyphC);

    if (editing) {
        Vector2 m = Th_Measure(editBuf);
        float tx = bv.x + 6, ty = Fl(r.y + (r.height - m.y) / 2);
        if (editFresh && editBuf[0]) DrawRectangle((int)tx - 1, (int)ty, (int)m.x + 2, (int)m.y, TH_ACCENT_TINT);
        Th_Text(editBuf, tx, ty, TH_TEXT);
        if (!editFresh && fmod(GetTime(), 1.0) < 0.6) DrawRectangle((int)(tx + m.x + 1), (int)ty, 1, (int)m.y, TH_TEXT);
        if (editFresh) {
            if (IsKeyPressed(KEY_BACKSPACE)) { editBuf[0] = 0; editFresh = false; }
            else {
                char tmp[48] = "";
                if (EditText(tmp, (int)sizeof tmp, true)) { snprintf(editBuf, sizeof editBuf, "%s", tmp); editFresh = false; }
            }
        } else EditText(editBuf, (int)sizeof editBuf, true);
        bool commit = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(Mouse(), bv)) commit = true;
        if (IsKeyPressed(KEY_ESCAPE)) { editName[0] = 0; }
        else if (commit) {
            char *end = NULL;
            long nv = strtol(editBuf, &end, 10);
            if (end != editBuf) {
                if (nv < minV) nv = minV;
                if (nv > 99999) nv = 99999;
                if ((int)nv != val) { *out = (int)nv; changed = true; }
            }
            editName[0] = 0;
        }
    } else {
        Vector2 m = Th_Measure(text);
        Th_Text(text, Fl(bv.x + (bv.width - m.x) / 2), Fl(r.y + (r.height - m.y) / 2), glyphC);
        if (enabled && Hov(bv)) {
            SetMouseCursor(MOUSE_CURSOR_IBEAM);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                snprintf(editName, sizeof editName, "%s", name);
                snprintf(editBuf, sizeof editBuf, "%s", text);
                editFresh = true;
                (void)GetCharPressed();
            }
        }
        int step = 0;
        if (enabled) {
            if (Hov(bm) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { step = -1; snprintf(holdName, sizeof holdName, "%s", name); holdDir = -1; holdT = GetTime() + 0.45; }
            else if (Hov(bp) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { step = 1; snprintf(holdName, sizeof holdName, "%s", name); holdDir = 1; holdT = GetTime() + 0.45; }
            else if (Same(holdName, name) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && Hov(holdDir < 0 ? bm : bp) && GetTime() > holdT) {
                step = holdDir; holdT += 0.05;
            }
            float wheel = Hov(r) ? GetMouseWheelMove() : 0;
            if (wheel != 0) { step = wheel > 0 ? 1 : -1; wheelUsed = true; }
        }
        if (Same(holdName, name) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) holdName[0] = 0;
        if (step != 0) {
            int nv = val + step;
            if (nv < minV) nv = minV;
            if (nv != val) { *out = nv; changed = true; }
        }
    }
    return changed;
}

/* ------------------------------------------------------------------------------------------------
 * Grid
 * ---------------------------------------------------------------------------------------------- */
static int BoxRows(void) { int b = boxAbove + boxBelow; return b < 1 ? 1 : b; }

/* Columns the cell shows: from the leftmost of the pen and the bitmap to the rightmost of the advance and the bitmap */
static void GlyphSpan(const PxGlyph *g, int *left, int *right)
{
    int l = 0, r = g->advance > 0 ? g->advance : 0;
    if (g->w > 0 && g->h > 0) {
        if (g->x < l) l = g->x;
        if (g->x + g->w > r) r = g->x + g->w;
    }
    *left = l; *right = r;
}

static float CellW(const PxGlyph *g)
{
    int l, r;
    GlyphSpan(g, &l, &r);
    float w = (float)((r - l) * zoom) + 2 * CELL_PADX;
    float minW = showCaptions ? 76.0f : 28.0f;
    return w < minW ? minW : w;
}

/* x of the pen inside a cell of width cw (relative to the cell's left edge) */
static float CellPen(const PxGlyph *g, float cw)
{
    int l, r;
    GlyphSpan(g, &l, &r);
    float span = (float)((r - l) * zoom);
    return floorf((cw - span) / 2) - (float)(l * zoom);
}

static void LayoutGrid(Rectangle area)
{
    if (font.count > lcap) {
        lcap = font.count + 256;
        lx = (float *)realloc(lx, sizeof(float) * (size_t)lcap);
        ly = (float *)realloc(ly, sizeof(float) * (size_t)lcap);
        lw = (float *)realloc(lw, sizeof(float) * (size_t)lcap);
    }
    px_font_box(&font, &boxAbove, &boxBelow);
    rowH = (float)(BoxRows() * zoom) + 2 * CELL_PADY + (showCaptions ? CAPTION_H : 0);
    float maxX = area.width - GRID_MARGIN - 14;
    float x = GRID_MARGIN, y = GRID_MARGIN;
    for (int i = 0; i < font.count; i++) {
        float w = CellW(&font.glyphs[i]);
        if (x + w > maxX && x > GRID_MARGIN) { x = GRID_MARGIN; y += rowH + GRID_GAP; }
        lx[i] = x; ly[i] = y; lw[i] = w;
        x += w + GRID_GAP;
    }
    contentH = (font.count ? y + rowH : 0) + GRID_MARGIN;
}

/* "U+0041" for a code point, "#n" for a glyph without one (n = its code minus F00000) */
static void CodeLabel(int32_t c, char *out, size_t n)
{
    if (px_code_is_uncoded(c)) snprintf(out, n, "#%d", c - PX_CODE_UNCODED_FIRST);
    else if (c >= 0) snprintf(out, n, "U+%04X", (unsigned)c);
    else snprintf(out, n, "-");
}

static bool PrintableInFont(int32_t cp)
{
    if (cp <= 0x20 || (cp >= 0x7F && cp <= 0xA0) || cp == 0xAD) return false;
    int gi = GetGlyphIndex(theme.ui, cp);
    return gi >= 0 && theme.ui.glyphs && theme.ui.glyphs[gi].value == cp;
}

static Rectangle Intersect(Rectangle a, Rectangle b)
{
    float x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    float x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    float y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    return (Rectangle){x0, y0, x1 - x0, y1 - y0};
}

/* (ox, baseY) is the pen on the baseline line; y is up, so the top row sits (y + h) rows above it */
static void DrawGlyphPixels(const PxGlyph *g, float ox, float baseY, int z, const Color *pal)
{
    if (!g->pix || g->w <= 0 || g->h <= 0) return;
    float topY = baseY - (float)(px_glyph_top(g) * z);
    for (int y = 0; y < g->h; y++) {
        const char *row = g->pix + (size_t)y * g->w;
        int x = 0;
        while (x < g->w) {
            char ch = row[x];
            if (ch == '.') { x++; continue; }
            int x0 = x;
            while (x < g->w && row[x] == ch) x++;
            int idx = px_pixel_index(ch);
            if (idx < 0) continue;
            DrawRectangle((int)(ox + (g->x + x0) * z), (int)(topY + y * z), (x - x0) * z, z, pal[idx]);
        }
    }
}

static void DrawCell(int i, float cx, float cy, float cw, Rectangle area, const Color *pal, bool selected, bool hovered)
{
    const PxGlyph *g = &font.glyphs[i];
    float boxPx = (float)(BoxRows() * zoom);
    float ch = rowH;
    Rectangle cell = {cx, cy, cw, ch};
    DrawRectangleRec(cell, TH_PANEL);
    Rectangle boxR = {cx, cy, cw, boxPx + 2 * CELL_PADY};

    float advPx = (float)(g->advance * zoom);
    float ox = cx + CellPen(g, cw);
    float oy = cy + CELL_PADY;                 /* top of the font box */
    float by = oy + (float)(boxAbove * zoom);  /* the baseline line */

    Rectangle clip = Intersect(boxR, area);
    if (clip.width > 0 && clip.height > 0) {
        BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);
        if (showMetrics) {
            float x0 = cx + 1, x1 = cx + cw - 1;
            Th_HLine(x0, x1, oy, Fade(TH_M_BOX, 0.7f));
            Th_HLine(x0, x1, oy + boxPx - 1, Fade(TH_M_BOX, 0.7f));
            if (font.has[PXM_XHEIGHT]) Th_HLine(x0, x1, by - font.m[PXM_XHEIGHT] * zoom, TH_M_XHEIGHT);
            if (font.has[PXM_CAPHEIGHT]) Th_HLine(x0, x1, by - font.m[PXM_CAPHEIGHT] * zoom, TH_M_CAP);
            /* underline and strike-out: position = the top pixel row, so the line is that row's top edge */
            if (font.has[PXM_UNDERLINEPOSITION]) Th_DashedHLine(x0, x1, by - (font.m[PXM_UNDERLINEPOSITION] + 1) * zoom, 1, TH_M_UNDERLINE);
            if (font.has[PXM_STRIKEOUTPOSITION]) Th_DashedHLine(x0, x1, by - (font.m[PXM_STRIKEOUTPOSITION] + 1) * zoom, 1, TH_M_UNDERLINE);
            Th_HLine(x0, x1, by, TH_M_BASELINE);
            Th_DashedVLine(ox + advPx, oy, oy + boxPx, 3, TH_M_ADVANCE);
        }
        DrawGlyphPixels(g, ox, by, zoom, pal);
        EndScissorMode();
    }

    if (showCaptions) {
        Rectangle cap = {cx, cy + boxPx + 2 * CELL_PADY, cw, CAPTION_H};
        DrawRectangleRec(cap, TH_PANEL_ALT);
        Th_HLine(cap.x, cap.x + cap.width, cap.y, TH_LINE_SOFT);
        char code[16];
        CodeLabel(g->codepoint, code, sizeof code);
        char chs[8] = "";
        if (px_code_is_codepoint(g->codepoint) && PrintableInFont(g->codepoint)) {
            int sz = 0;
            const char *u = CodepointToUTF8(g->codepoint, &sz);
            memcpy(chs, u, (size_t)sz);
            chs[sz] = 0;
        }
        bool isDef = g->codepoint != PX_NO_CODEPOINT && g->codepoint == font.defaultCodepoint;
        Vector2 mc = Th_MeasureMono(code), mh = chs[0] ? Th_Measure(chs) : (Vector2){0, 0};
        float gap = chs[0] ? 7.0f : 0.0f;
        float total = mh.x + gap + mc.x;
        float tx = Fl(cap.x + (cap.width - total) / 2);
        if (isDef) tx = Fl(cap.x + 6);
        if (tx < cap.x + 2) tx = cap.x + 2;
        float ty = Fl(cap.y + (cap.height - mc.y) / 2);
        if (chs[0]) Th_Text(chs, tx, Fl(cap.y + (cap.height - mh.y) / 2), TH_TEXT);
        Th_TextMono(code, tx + mh.x + gap, ty, TH_TEXT_DIM);
        if (isDef) {
            Vector2 md = Th_MeasureSmall("DEF");
            Rectangle br = {cap.x + cap.width - md.x - 14, cap.y + 4, md.x + 10, CAPTION_H - 8};
            DrawRectangleRounded(br, 0.5f, 6, TH_ACCENT);
            Th_TextSmall("DEF", br.x + 5, Fl(br.y + (br.height - md.y) / 2), WHITE);
        }
    }

    if (hovered && !selected) DrawRectangleRec(cell, TH_HOVER);
    if (selected) {
        DrawRectangleRec(cell, TH_ACCENT_TINT);
        Th_RectLines(cell, TH_ACCENT);
        Th_RectLines((Rectangle){cell.x + 1, cell.y + 1, cell.width - 2, cell.height - 2}, TH_ACCENT);
    } else {
        Th_RectLines(cell, TH_LINE);
    }
}

static bool sbDrag = false;
static float sbDragOff = 0;

static void ScrollBar(Rectangle area)
{
    float maxScroll = contentH - area.height;
    if (maxScroll <= 0) { scrollY = 0; sbDrag = false; return; }
    Rectangle track = {area.x + area.width - 12, area.y + 4, 8, area.height - 8};
    float thumbH = track.height * area.height / contentH;
    if (thumbH < 36) thumbH = 36;
    float room = track.height - thumbH;
    float thumbY = track.y + room * (scrollY / maxScroll);
    Rectangle thumb = {track.x, thumbY, track.width, thumbH};
    Rectangle hit = {track.x - 4, track.y, track.width + 8, track.height};
    bool hov = Hov(hit);
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        sbDrag = true;
        if (CheckCollisionPointRec(Mouse(), thumb)) sbDragOff = Mouse().y - thumbY;
        else sbDragOff = thumbH / 2;
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) sbDrag = false;
    if (sbDrag && room > 0) {
        float t = (Mouse().y - sbDragOff - track.y) / room;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        scrollY = t * maxScroll;
        thumb.y = track.y + room * t;
    }
    Color c = sbDrag ? (Color){0x8A, 0x8A, 0x96, 0xFF} : (hov ? (Color){0xA8, 0xA8, 0xB2, 0xFF} : (Color){0xC6, 0xC6, 0xCE, 0xFF});
    DrawRectangleRounded(thumb, 1.0f, 6, c);
}

static void MoveSelection(int dx, int dy, bool extend)
{
    if (!hasFont || font.count == 0) return;
    RecountSel();
    int cur = selAnchor >= 0 && selAnchor < font.count ? selAnchor : FirstSelected();
    int ni = cur;
    if (cur < 0) ni = 0;
    else if (dx) { ni = cur + dx; }
    else if (dy) {
        float ty = ly[cur] + dy * (rowH + GRID_GAP);
        float cxm = lx[cur] + lw[cur] / 2;
        float best = 1e9f;
        ni = cur;
        for (int j = 0; j < font.count; j++) {
            if (fabsf(ly[j] - ty) > 1.0f) continue;
            float d = fabsf(lx[j] + lw[j] / 2 - cxm);
            if (d < best) { best = d; ni = j; }
        }
    }
    if (ni < 0) ni = 0;
    if (ni >= font.count) ni = font.count - 1;
    if (extend && cur >= 0) {
        int a = cur < ni ? cur : ni, b = cur < ni ? ni : cur;
        for (int j = a; j <= b; j++) sel[j] = true;
        selAnchor = ni;
    } else SelOnly(ni);
    revealIdx = ni;
}

static void GridSelectClick(int idx, bool ctrl, bool shift)
{
    SelResize();
    if (idx < 0) { if (!ctrl && !shift) SelClear(); return; }
    if (shift && selAnchor >= 0 && selAnchor < selN) {
        if (!ctrl) { for (int i = 0; i < selN; i++) sel[i] = false; }
        int a = selAnchor < idx ? selAnchor : idx, b = selAnchor < idx ? idx : selAnchor;
        for (int i = a; i <= b; i++) sel[i] = true;
    } else if (ctrl) {
        sel[idx] = !sel[idx];
        selAnchor = idx;
    } else {
        SelOnly(idx);
    }
}

/* ---- context menu of the grid ---- */
static struct { bool open; Vector2 pos; } ctx;
enum { CM_EDIT, CM_DEFAULT, CM_CLONE, CM_OCC, CM_DELETE, CM_COUNT };
static const char *kCtxLabel[CM_COUNT] = {"Edit Glyph", "Set as Default Glyph", "Clone...", "Select Occurrences", "Delete"};

static bool CtxEnabled(int k)
{
    switch (k) {
    case CM_EDIT: return selCount >= 1;
    case CM_DEFAULT: return CanSetDefault();
    case CM_CLONE: return !readOnly && selCount == 1;
    case CM_OCC: return selCount >= 1;
    default: return !readOnly && selCount >= 1;
    }
}

static void DrawContextMenu(int W, int H)
{
    if (!ctx.open) return;
    const float iw = 200, ih = 28, pad = 4;
    Rectangle mr = {ctx.pos.x, ctx.pos.y, iw + 2 * pad, CM_COUNT * ih + 2 * pad};
    if (mr.x + mr.width > W - 4) mr.x = W - 4 - mr.width;
    if (mr.y + mr.height > H - STATUS_H - 4) mr.y = H - STATUS_H - 4 - mr.height;
    DrawRectangleRounded((Rectangle){mr.x, mr.y + 3, mr.width, mr.height}, 0.08f, 8, (Color){0, 0, 0, 24});
    RRect(mr, TH_PANEL, TH_LINE);
    int clicked = -1;
    for (int k = 0; k < CM_COUNT; k++) {
        Rectangle ir = {mr.x + pad, mr.y + pad + k * ih, iw, ih};
        bool en = CtxEnabled(k);
        bool hv = !uiBlocked && CheckCollisionPointRec(Mouse(), ir);
        Th_RegLabel(kCtxLabel[k], ir);
        if (hv && en) DrawRectangleRounded(ir, 0.15f, 6, TH_ACCENT_TINT);
        Th_Text(kCtxLabel[k], ir.x + 10, Fl(ir.y + (ir.height - Th_Measure(kCtxLabel[k]).y) / 2),
                en ? (k == CM_DELETE ? TH_DANGER : TH_TEXT) : (Color){0xB8, 0xB8, 0xC0, 0xFF});
        if (hv && en && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) clicked = k;
    }
    bool outside = !CheckCollisionPointRec(Mouse(), mr);
    if (IsKeyPressed(KEY_ESCAPE) || (outside && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)))) ctx.open = false;
    if (clicked >= 0) {
        ctx.open = false;
        switch (clicked) {
        case CM_EDIT: CmdEditSelected(); break;
        case CM_DEFAULT: CmdSetDefault(); break;
        case CM_CLONE: CmdClone(); break;
        case CM_OCC: CmdSelectOccurrences(); break;
        case CM_DELETE: CmdDeleteAsk(); break;
        }
    }
}

static void UpdateDrawGrid(Rectangle area)
{
    DrawRectangleRec(area, TH_BG);
    SelResize();
    LayoutGrid(area);
    RecountSel();
    if (zoomAnchor >= 0) {
        if (zoomAnchor < font.count) scrollY = ly[zoomAnchor] > GRID_MARGIN ? ly[zoomAnchor] - GRID_MARGIN : 0;
        zoomAnchor = -1;
    }

    bool inArea = Hov(area) && !ctx.open;
    float wheel = inArea ? GetMouseWheelMove() : 0;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (wheel != 0) {
        if (ctrl) ApplyZoom(zoom + (wheel > 0 ? 1 : -1));
        else scrollY -= wheel * 72.0f;
    }
    if (revealIdx >= 0 && revealIdx < font.count) {
        float top = ly[revealIdx], bot = ly[revealIdx] + rowH;
        if (top - scrollY < 0) scrollY = top - GRID_MARGIN;
        else if (bot - scrollY > area.height) scrollY = bot - area.height + GRID_MARGIN;
    }
    revealIdx = -1;
    ScrollBar(area);
    float maxScroll = contentH - area.height;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollY > maxScroll) scrollY = maxScroll;
    if (scrollY < 0) scrollY = 0;
    scrollY = floorf(scrollY);

    Color pal[PX_NUM_PIXELS];
    for (int i = 0; i < PX_NUM_PIXELS; i++) pal[i] = Th_FromRGBA(font.color[i]);

    hoverIdx = -1;
    Vector2 m = Mouse();
    bool overBar = m.x > area.x + area.width - 18;
    for (int i = 0; i < font.count; i++) {
        float cy = area.y + ly[i] - scrollY;
        if (cy > area.y + area.height) break;
        if (cy + rowH < area.y) continue;
        float cx = area.x + lx[i];
        Rectangle cr = {cx, cy, lw[i], rowH};
        bool hv = inArea && !overBar && CheckCollisionPointRec(m, cr) && m.y >= area.y;
        if (hv) hoverIdx = i;
        DrawCell(i, cx, cy, lw[i], area, pal, i < selN && sel[i], hv);
    }

    if (inArea && !overBar && !sbDrag && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        double now = GetTime();
        if (hoverIdx >= 0 && !ctrl && !shift && hoverIdx == lastClickIdx && now - lastClickTime < 0.4) {
            SelOnly(hoverIdx);
            OpenEditor(hoverIdx);
            lastClickIdx = -1;
        } else {
            GridSelectClick(hoverIdx, ctrl, shift);
            lastClickIdx = hoverIdx;
            lastClickTime = now;
        }
    }
    if (inArea && !overBar && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        if (hoverIdx >= 0) {
            if (!(hoverIdx < selN && sel[hoverIdx])) SelOnly(hoverIdx);
            ctx.open = true;
            ctx.pos = m;
        } else ctx.open = false;
    }
    RecountSel();
}

/* ------------------------------------------------------------------------------------------------
 * Toolbar and info bar
 * ---------------------------------------------------------------------------------------------- */
static float TbX;
static float TbY = 10;                 /* top of the buttons of the row being drawn */

static float BtnW(const char *label) { return Fl(Th_Measure(label).x) + 26; }

static bool TbButton(const char *label, bool enabled, int style, const char *hint)
{
    float w = BtnW(label);
    bool r = Button((Rectangle){TbX, TbY, w, 28}, label, enabled, style, hint);
    TbX += w + 6;
    return r;
}

static void TbSep(void)
{
    TbX += 6;
    Th_VLine(TbX, TbY + 2, TbY + 26, TH_LINE);
    TbX += 12;
}

/* The File menu: Load, Save, Save As, Import Folder, Export to Folder, Quit */
static struct { bool open; Rectangle anchor; } fileMenu;

/* "Find in Library": the finder on the selected glyph's code point */
static void CmdFindInLibrary(void)
{
    RecountSel();
    int i = FirstSelected();
    if (!finder) { ToastPush(2, "No library is open"); return; }
    if (selCount != 1 || i < 0 || !px_code_is_codepoint(font.glyphs[i].codepoint)) { ToastPush(2, "Select one glyph that has a code point"); return; }
    Finder_SetCode(finder, font.glyphs[i].codepoint);
    ShowGlyphs();
}

/* The top-level navigation: Library | Glyphs | <current font> */
static void DrawNav(void)
{
    bool fontView = hasFont && !inBrowser && !inFinder;
    char fontLabel[300] = "";
    if (hasFont) {
        char t[260];
        snprintf(t, sizeof t, "%s%s", title[0] ? title : "untitled", modified ? " *" : "");
        HeadFit(t, 230, fontLabel, sizeof fontLabel);
    }
    const char *names[3] = {"Library", "Glyphs", fontLabel};
    float wi[3];
    int n = hasFont ? 3 : 2;
    float total = 0;
    for (int i = 0; i < n; i++) { wi[i] = Fl(Th_Measure(names[i]).x) + 32; total += wi[i]; }
    Rectangle track = {TbX, 8, total + 4, 32};
    RRect(track, (Color){0xE8, 0xE8, 0xEC, 0xFF}, (Color){0, 0, 0, 0});
    float cx = TbX + 2;
    for (int i = 0; i < n; i++) {
        Rectangle s = {cx, 10, wi[i], 28};
        cx += wi[i];
        bool enabled = i == 2 || store != NULL;
        bool act = i == 0 ? (inBrowser && !inFinder) : (i == 1 ? inFinder : fontView);
        if (i == 2) Th_Reg("tab.font", s); else Th_RegLabel(names[i], s);
        bool hv = enabled && Hov(s);
        if (hv) hintText = i == 0 ? "The library: every typeface and font  (Alt+Left from a font)" : (i == 1 ? "The glyph finder: every bitmap drawn for a code point  (Ctrl+G)" : "The open font");
        if (act) RRect(s, TH_PANEL, TH_LINE);
        else if (hv) RRect(s, Fade(TH_PANEL, 0.6f), (Color){0, 0, 0, 0});
        Color tc = !enabled ? (Color){0xB8, 0xB8, 0xC0, 0xFF} : (act ? TH_ACCENT : (hv ? TH_TEXT : TH_TEXT_DIM));
        TextCentered(names[i], s, tc);
        if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !act) {
            if (i == 0) ShowLibrary(); else if (i == 1) ShowGlyphs(); else ShowFont();
        }
    }
    TbX = track.x + track.width + 8;
}

/* the "..." menu of the font view's toolbar: deleting the open original font or its typeface */
static struct { bool open; Rectangle anchor; } moreMenu;

static void DrawMoreMenu(void)
{
    if (!moreMenu.open) return;
    bool origFont = hasFont && readOnly && store && origHash[0] && store_has_pxfm(store_main_conn(store), origHash);
    bool origTf = origFont && hasCont && origContHash[0] && store_has_pxtf(store_main_conn(store), origContHash);
    const char *labels[2] = {"Delete Font...", "Delete Typeface..."};
    const char *names[2] = {"more.delfont", "more.deltf"};
    bool en[2] = {origFont, origTf};
    const char *why[2] = {"Open a font of the database as an original to delete it",
                          "Open a font that belongs to a typeface of the database as an original to delete the typeface"};
    const float iw = 230, ih = 30, pad = 4;
    Rectangle mr = {moreMenu.anchor.x + moreMenu.anchor.width - iw - 2 * pad, moreMenu.anchor.y + moreMenu.anchor.height + 4, iw + 2 * pad, 2 * ih + 2 * pad};
    DrawRectangleRounded((Rectangle){mr.x, mr.y + 3, mr.width, mr.height}, 0.08f, 8, (Color){0, 0, 0, 24});
    RRect(mr, TH_PANEL, TH_LINE);
    int clicked = -1;
    for (int k = 0; k < 2; k++) {
        Rectangle ir = {mr.x + pad, mr.y + pad + k * ih, iw, ih};
        bool hv = !uiBlocked && CheckCollisionPointRec(Mouse(), ir);
        Th_Reg(names[k], ir);
        if (hv && en[k]) DrawRectangleRounded(ir, 0.15f, 6, TH_ACCENT_TINT);
        if (hv && !en[k]) hintText = why[k];
        Color c = en[k] ? TH_DANGER : Mix(TH_TEXT, TH_PANEL, 0.6f);
        Th_Text(labels[k], ir.x + 10, Fl(ir.y + (ir.height - Th_Measure(labels[k]).y) / 2), c);
        if (hv && en[k] && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) clicked = k;
    }
    bool outside = !CheckCollisionPointRec(Mouse(), mr) && !CheckCollisionPointRec(Mouse(), moreMenu.anchor);
    if (IsKeyPressed(KEY_ESCAPE) || (outside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) moreMenu.open = false;
    if (clicked >= 0) {
        moreMenu.open = false;
        if (clicked == 0) AskDelete(false, origHash, true);
        else AskDelete(true, origContHash, true);
    }
}

static void DrawToolbar(int W)
{
    DrawRectangle(0, 0, W, TOOLBAR_H, TH_PANEL);
    Th_HLine(0, (float)W, TOOLBAR_H - 1, TH_LINE);
    TbX = 12;
    TbY = 10;
    bool fontView = hasFont && !inBrowser && !inFinder;
    {   /* the File menu button */
        Rectangle fb = {TbX, TbY, BtnW("File") + 10, 28};
        Th_Reg("File", fb);
        if (Button(fb, "File", true, 0, "Load, Save, Import, Export")) { fileMenu.open = !fileMenu.open; fileMenu.anchor = fb; }
        TbX += fb.width + 6;
    }
    TbSep();
    DrawNav();
    TbSep();
    /* Save on the right: it shows the modified state */
    float limit = W - 12;
    if (store) {
        bool dirty = DbDirty();
        float sw = BtnW("Save") + 8;
        Rectangle sr = {(float)W - 12 - sw, TbY, sw, 28};
        Th_Reg("Save", sr);
        if (Button(sr, "Save", true, dirty ? 1 : 0, dirty ? "Save the changes to the database file  (Ctrl+S)" : "Nothing to save: the file is as it was loaded or last saved  (Ctrl+S)")) SaveDb();
        limit = sr.x - 12;
    }
    if (!fontView) return;
    if (TbX + 18 + BtnW("Undo") + BtnW("Redo") <= limit) {
        TbSep();
        if (TbButton("Undo", !readOnly && undoN > 0, 0, "Undo  (Ctrl+Z)")) DoUndoRedo(false);
        if (TbButton("Redo", !readOnly && redoN > 0, 0, "Redo  (Ctrl+Y)")) DoUndoRedo(true);
    }
    RecountSel();
    {
        int fi = FirstSelected();
        bool can = store && selCount == 1 && fi >= 0 && px_code_is_codepoint(font.glyphs[fi].codepoint);
        if (TbX + 18 + BtnW("Find in Library") <= limit) {
            TbSep();
            if (TbButton("Find in Library", can, 0, can ? "Find every bitmap the database draws for this code point  (Ctrl+Shift+F)" : "Select one glyph that has a code point")) CmdFindInLibrary();
        }
    }
    if (TbX + 34 <= limit + 6) {
        Rectangle mb = {TbX, TbY, 34, 28};
        Th_Reg("More", mb);
        if (Button(mb, "\xE2\x80\xA6", true, 0, "More: delete the open font or typeface")) {
            moreMenu.open = !moreMenu.open;
            moreMenu.anchor = mb;
        }
        TbX += 40;
    }
}

/* the strip under the toolbar in the font view: view options and glyph commands */
static void DrawCommandBar(Rectangle r)
{
    DrawRectangleRec(r, TH_PANEL);
    Th_HLine(r.x, r.x + r.width, r.y + r.height - 1, TH_LINE);
    TbX = 12;
    TbY = r.y + 5;
    const char *saveHint = readOnly ? "An original is read-only: use Duplicate to Edit first" : NULL;
    /* zoom selector: [-] 4x [+] */
    Th_Text("Zoom", TbX, Fl(TbY + (28 - Th_Measure("Zoom").y) / 2), TH_TEXT_DIM);
    TbX += Fl(Th_Measure("Zoom").x) + 8;
    {
        Rectangle zr = {TbX, TbY, 96, 28};
        char zt[8];
        snprintf(zt, sizeof zt, "%dx", zoom);
        RRect(zr, TH_PANEL, TH_LINE);
        Rectangle zm = {zr.x, zr.y, 28, zr.height}, zp = {zr.x + zr.width - 28, zr.y, 28, zr.height};
        Th_Reg("zoom-", zm); Th_Reg("zoom+", zp);
        bool hm = Hov(zm) && !inEditor, hp = Hov(zp) && !inEditor;
        if (hm) { DrawRectangle((int)zm.x + 1, (int)zm.y + 1, (int)zm.width - 1, (int)zm.height - 2, (Color){0xF1, 0xF1, 0xF4, 0xFF}); hintText = "Zoom out  (-)"; }
        if (hp) { DrawRectangle((int)zp.x, (int)zp.y + 1, (int)zp.width - 1, (int)zp.height - 2, (Color){0xF1, 0xF1, 0xF4, 0xFF}); hintText = "Zoom in  (+)"; }
        Th_VLine(zm.x + zm.width, zr.y + 1, zr.y + zr.height - 1, TH_LINE);
        Th_VLine(zp.x, zr.y + 1, zr.y + zr.height - 1, TH_LINE);
        Color gc = TH_TEXT;
        DrawRectangle((int)(zm.x + 10), (int)(zr.y + 14), 8, 1, gc);
        DrawRectangle((int)(zp.x + 10), (int)(zr.y + 14), 8, 1, gc);
        DrawRectangle((int)(zp.x + 13.5f), (int)(zr.y + 10), 1, 9, gc);
        TextCentered(zt, (Rectangle){zr.x + 28, zr.y, zr.width - 56, zr.height}, gc);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (hm) ApplyZoom(zoom - 1);
            if (hp) ApplyZoom(zoom + 1);
        }
        TbX += zr.width + 6;
    }
    TbSep();
    ToggleBtn((Rectangle){TbX, TbY, BtnW("Metrics"), 28}, "Metrics", &showMetrics, "Show projected metric lines in every cell");
    TbX += BtnW("Metrics") + 6;
    ToggleBtn((Rectangle){TbX, TbY, BtnW("Captions"), 28}, "Captions", &showCaptions, "Show the character and code under each glyph");
    TbX += BtnW("Captions") + 6;
    TbSep();
    bool grid = !inEditor;
    if (TbButton("Select Occurrences", grid && selCount > 0, 0, "Select every glyph with the same shape as the selected ones  (Ctrl+Shift+A)")) CmdSelectOccurrences();
    if (TbButton("Clone", grid && !readOnly && selCount == 1, 0, readOnly ? saveHint : "Clone the selected glyph to a new code point, or to a glyph without one  (Ctrl+D)")) CmdClone();
    if (TbButton("Set Default", grid && CanSetDefault(), 0, readOnly ? saveHint : "Make the selected glyph the font's default glyph")) CmdSetDefault();
    if (TbButton("Delete", grid && !readOnly && selCount > 0, 2, readOnly ? saveHint : "Delete the selected glyphs  (Del)")) CmdDeleteAsk();
}

static void DrawFileMenu(void)
{
    if (!fileMenu.open) return;
    const char *labels[6] = {"Load\xE2\x80\xA6", "Save", "Save As\xE2\x80\xA6", "Import Folder\xE2\x80\xA6", "Export to Folder\xE2\x80\xA6", "Quit"};
    const char *keys[6] = {"Ctrl+O", "Ctrl+S", "Ctrl+Shift+S", "", "", ""};
    const char *names[6] = {"file.load", "file.save", "file.saveas", "file.import", "file.export", "file.quit"};
    bool en[6] = {true, store != NULL, store != NULL, true, store != NULL, true};
    const char *why[6] = {NULL, "No database is loaded", "No database is loaded", NULL, "No database is loaded", NULL};
    const bool sepAfter[6] = {false, false, true, false, true, false};
    const float iw = 268, ih = 30, pad = 4, sep = 9;
    float mh = 2 * pad + 6 * ih + 2 * sep;
    Rectangle mr = {fileMenu.anchor.x, fileMenu.anchor.y + fileMenu.anchor.height + 4, iw + 2 * pad, mh};
    DrawRectangleRounded((Rectangle){mr.x, mr.y + 3, mr.width, mr.height}, 0.05f, 8, (Color){0, 0, 0, 24});
    RRect(mr, TH_PANEL, TH_LINE);
    int clicked = -1;
    float y = mr.y + pad;
    for (int k = 0; k < 6; k++) {
        Rectangle ir = {mr.x + pad, y, iw, ih};
        bool hv = !uiBlocked && CheckCollisionPointRec(Mouse(), ir);
        Th_Reg(names[k], ir);
        if (hv && en[k]) DrawRectangleRounded(ir, 0.15f, 6, TH_ACCENT_TINT);
        if (hv && !en[k] && why[k]) hintText = why[k];
        Color c = en[k] ? TH_TEXT : Mix(TH_TEXT, TH_PANEL, 0.6f);
        Th_Text(labels[k], ir.x + 10, Fl(ir.y + (ir.height - Th_Measure(labels[k]).y) / 2), c);
        if (keys[k][0]) { Vector2 m = Th_MeasureSmall(keys[k]); Th_TextSmall(keys[k], Fl(ir.x + ir.width - m.x - 10), Fl(ir.y + (ir.height - m.y) / 2), TH_TEXT_DIM); }
        if (hv && en[k] && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) clicked = k;
        y += ih;
        if (sepAfter[k]) { Th_HLine(mr.x + 8, mr.x + mr.width - 8, y + sep / 2, TH_LINE); y += sep; }
    }
    bool outside = !CheckCollisionPointRec(Mouse(), mr) && !CheckCollisionPointRec(Mouse(), fileMenu.anchor);
    if (IsKeyPressed(KEY_ESCAPE) || (outside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) fileMenu.open = false;
    if (clicked >= 0) {
        fileMenu.open = false;
        switch (clicked) {
        case 0: Guard(G_LOAD_DIALOG, ""); break;
        case 1: SaveDb(); break;
        case 2: SaveAsDialog(); break;
        case 3: Guard(G_IMPORT_DIALOG, ""); break;
        case 4: ExportStart(); break;
        default: RequestQuit(); break;
        }
    }
}

/* A short hash that copies the full one when clicked; returns its width. */
static float HashLink(float x, float rowY, float rowH, const char *full, const char *name, const char *what, Color c)
{
    char h8[16];
    snprintf(h8, sizeof h8, "%.8s", full);
    Vector2 m = Th_MeasureMono(h8);
    Rectangle hr = {x - 3, rowY + 3, m.x + 6, rowH - 6};
    Th_Reg(name, hr);
    if (Hov(hr)) {
        hintText = "Click to copy the full hash";
        DrawRectangleRounded(hr, 0.3f, 6, TH_HOVER);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { SetClipboardText(full); ToastPush(0, "%s hash copied", what); }
    }
    Th_TextMono(h8, x, Fl(rowY + (rowH - m.y) / 2) + 1, c);
    return m.x;
}

/* The end of a path that does not fit (the file name is what matters). */
static void PathFit(const char *sIn, float maxW, char *out, size_t cap)
{
    if (Th_Measure(sIn).x <= maxW) { snprintf(out, cap, "%s", sIn); return; }
    int n = (int)strlen(sIn);
    for (int i = 1; i < n; i++) {
        if ((sIn[i] & 0xC0) == 0x80) continue;
        char tmp[PATH_MAX_LEN + 8];
        snprintf(tmp, sizeof tmp, "...%s", sIn + i);
        if (Th_Measure(tmp).x <= maxW) { snprintf(out, cap, "%s", tmp); return; }
    }
    snprintf(out, cap, "%s", sIn);
}

/* The slim bar above the grid: original (read-only) or duplicate. */
static void DrawInfoBar(Rectangle r)
{
    DrawRectangleRec(r, readOnly ? (Color){0xFF, 0xF6, 0xE0, 0xFF} : TH_PANEL_ALT);
    Th_HLine(r.x, r.x + r.width, r.y + r.height - 1, TH_LINE);
    float ty = Fl(r.y + (r.height - 13) / 2);
    float x = r.x + 14;
    if (readOnly) {
        const char *lead = "Font \xC2\xB7 original \xC2\xB7 read-only \xC2\xB7";
        Th_Text(lead, x, ty, (Color){0x7A, 0x55, 0x00, 0xFF});
        x += Th_Measure(lead).x + 8;
        HashLink(x, r.y, r.height, origHash, "hash_font", "Font", (Color){0x7A, 0x55, 0x00, 0xFF});
        const char *lbl = "Duplicate to Edit";
        float bw = BtnW(lbl);
        if (Button((Rectangle){r.x + r.width - bw - 12, r.y + 3, bw, r.height - 7}, lbl, true, 1, "Make an editable copy of this font  (the original stays untouched)")) DuplicateToEdit();
    } else {
        char s[400];
        snprintf(s, sizeof s, "%s \xC2\xB7 editable duplicate of Font", title);
        Th_Text(s, x, ty, TH_TEXT);
        x += Th_Measure(s).x + 8;
        x += HashLink(x, r.y, r.height, origHash, "hash_font", "Font", TH_TEXT_DIM) + 16;
        EnsureHashes();
        if (!modified) Th_Text("Unchanged: same hash as the original", x, ty, TH_TEXT_DIM);
    }
}

static void DrawGlyphPanel(Rectangle p);
#include "panel.inc"
#include "gpanel.inc"

/* ------------------------------------------------------------------------------------------------
 * Status bar, toasts, empty state
 * ---------------------------------------------------------------------------------------------- */
static char statusDoc[PATH_MAX_LEN + 96], statusDb[PATH_MAX_LEN + 96];   /* what the bar says (play.inc asserts on it) */

/* one piece of the status line, left to right, dropped when it does not fit */
static void StSeg(float *x, float ty, float limit, const char *t, Color c)
{
    float w = Th_Measure(t).x;
    if (*x + w > limit) return;
    Th_Text(t, *x, ty, c);
    *x += w + 14;
}

static void DrawStatus(int W, int H)
{
    float y = (float)(H - STATUS_H);
    DrawRectangle(0, (int)y, W, STATUS_H, TH_PANEL_ALT);
    Th_HLine(0, (float)W, y, TH_LINE);
    float x = 12;
    float ty = Fl(y + (STATUS_H - 13) / 2 + 0.5f);
    statusDoc[0] = 0;
    statusDb[0] = 0;
    float limit = (float)W - 12;
    if (store) {   /* the file and whether memory differs from it */
        char pf[PATH_MAX_LEN + 8];
        bool dirty = DbDirty();
        PathFit(store_info(store)->path, (limit - 12) * 0.4f, pf, sizeof pf);
        const char *word = dirty ? "modified" : "saved";
        snprintf(statusDb, sizeof statusDb, "%s  \xC2\xB7  %s", pf, word);
        Th_Text(pf, x, ty, TH_TEXT);
        x += Th_Measure(pf).x + 10;
        Th_Text(word, x, ty, dirty ? TH_ACCENT : TH_TEXT_DIM);
        x += Th_Measure(word).x + 18;
    }
    float room = limit;
    if (inFinder && finder) {
        char t[160], cl[24] = "none";
        int32_t c = Finder_Code(finder);
        if (c >= 0) snprintf(cl, sizeof cl, "U+%04X", (unsigned)c);
        int rc = Finder_ResultCount(finder);
        if (rc < 0) snprintf(t, sizeof t, "Glyph finder   code point %s   searching...", cl);
        else snprintf(t, sizeof t, "Glyph finder   code point %s   %d bitmap%s", cl, rc, rc == 1 ? "" : "s");
        StSeg(&x, ty, room, t, TH_TEXT_DIM);
    } else if (inBrowser && browser) {
        char t[PATH_MAX_LEN + 64], fit[PATH_MAX_LEN + 64];
        snprintf(t, sizeof t, "Library %s", Browser_Root(browser));
        PathFit(t, room * 0.55f, fit, sizeof fit);
        snprintf(statusDoc, sizeof statusDoc, "%s", t);
        StSeg(&x, ty, room, fit, TH_TEXT_DIM);
        snprintf(t, sizeof t, "%d of %d shown", Browser_VisibleCount(browser), Browser_TotalCount(browser));
        StSeg(&x, ty, room, t, TH_TEXT_DIM);
    } else if (!hasFont) {
        StSeg(&x, ty, room, "No file open", TH_TEXT_DIM);
    } else {
        EnsureHashes();
        char nm[PATH_MAX_LEN + 16], fit[PATH_MAX_LEN + 16];
        snprintf(nm, sizeof nm, "%s", docPath[0] ? BaseName(docPath) : "untitled");
        snprintf(statusDoc, sizeof statusDoc, "%s", nm);
        PathFit(nm, room * 0.5f, fit, sizeof fit);
        float x0 = x;
        StSeg(&x, ty, room, fit, TH_TEXT);
        if (x != x0) {
            char t[64];
            snprintf(t, sizeof t, "%d glyphs", font.count);
            StSeg(&x, ty, room, t, TH_TEXT_DIM);
            /* the hash of the font: what the label says it belongs to */
            if (x + 120 < room) {
                Th_Text("Font", x, ty, TH_TEXT_DIM);
                x += Th_Measure("Font").x + 6;
                x += HashLink(x, y, STATUS_H, origHash, "hash", "Font", TH_TEXT) + 14;
                if (!Same(curHash, origHash) && x + 130 < room) {
                    Th_Text("Current", x, ty, TH_TEXT_DIM);
                    x += Th_Measure("Current").x + 6;
                    char h8[16];
                    snprintf(h8, sizeof h8, "%.8s", curHash);
                    Th_TextMono(h8, x, Fl(y + (STATUS_H - Th_MeasureMono(h8).y) / 2) + 2, TH_ACCENT);
                    x += Th_MeasureMono(h8).x + 14;
                }
            }
        }
    }
    /* right side: the hint of the hovered control, or the glyph under the mouse */
    char r[200] = "";
    if (hintText) snprintf(r, sizeof r, "%s", hintText);
    else if (hasFont && !inBrowser && !inFinder && !inEditor && hoverIdx >= 0 && hoverIdx < font.count && !uiBlocked) {
        const PxGlyph *g = &font.glyphs[hoverIdx];
        const char *sz = g->w > 0 ? TextFormat("%d x %d", g->w, g->h) : "empty";
        char cl[24];
        CodeLabel(g->codepoint, cl, sizeof cl);
        if (px_code_is_codepoint(g->codepoint)) snprintf(r, sizeof r, "%s   advance %d   %s", cl, g->advance, sz);
        else snprintf(r, sizeof r, "%s (no code point)   advance %d   %s", cl, g->advance, sz);
    }
    if (r[0]) {
        Vector2 m = Th_Measure(r);
        if (limit - m.x > x) Th_Text(r, Fl(limit - m.x), ty, TH_TEXT_DIM);
    }
}

static void DrawToasts(int W, int H)
{
    double now = GetTime();
    int out = 0;
    for (int i = 0; i < toastCount; i++) {
        if (now - toasts[i].t0 < toasts[i].life) toasts[out++] = toasts[i];
    }
    toastCount = out;
    float y = (float)(H - STATUS_H - 16);
    for (int i = toastCount - 1; i >= 0; i--) {
        Toast *t = &toasts[i];
        double age = now - t->t0;
        float a = 1.0f;
        if (age < 0.15) a = (float)(age / 0.15);
        if (t->life - age < 0.4) a = (float)((t->life - age) / 0.4);
        Vector2 m = Th_Measure(t->text);
        float w = m.x + 36, h = 36;
        float rightEdge = (float)W - (inBrowser && browser && !inFinder ? 280.0f : 0.0f);   /* the browser's side panel (Delete buttons) stays clear */
        Rectangle r = {rightEdge - w - 16, y - h, w, h};
        Color bar = t->kind == 2 ? TH_DANGER : (t->kind == 1 ? TH_M_CAP : TH_ACCENT);
        DrawRectangleRounded((Rectangle){r.x, r.y + 2, r.width, r.height}, 0.2f, 8, (Color){0, 0, 0, (unsigned char)(18 * a)});
        RRect(r, Fade(TH_PANEL, a), Fade(TH_LINE, a));
        DrawRectangleRounded((Rectangle){r.x + 8, r.y + 9, 3, r.height - 18}, 1.0f, 4, Fade(bar, a));
        Th_Text(t->text, r.x + 22, Fl(r.y + (h - m.y) / 2), Fade(TH_TEXT, a));
        y -= h + 8;
    }
}

static void DrawEmptyState(Rectangle area)
{
    DrawRectangleRec(area, TH_BG);
    Rectangle c = {Fl(area.x + area.width / 2 - 250), Fl(area.y + area.height / 2 - 110), 500, 220};
    RRect(c, TH_PANEL, TH_LINE);
    TextCentered("No database loaded", (Rectangle){c.x, c.y + 34, c.width, 20}, TH_TEXT);
    TextCentered("Load a .pxfdb database to browse and edit its fonts,", (Rectangle){c.x, c.y + 62, c.width, 20}, TH_TEXT_DIM);
    TextCentered("or import a folder of pxtf, pxfm and pxgl files into a new one.", (Rectangle){c.x, c.y + 82, c.width, 20}, TH_TEXT_DIM);
    float bx = c.x + 76;
    if (Button((Rectangle){bx, c.y + 124, 170, 32}, "Load Database\xE2\x80\xA6", true, 1, "Load a .pxfdb database file  (Ctrl+O)")) Guard(G_LOAD_DIALOG, "");
    if (Button((Rectangle){bx + 180, c.y + 124, 170, 32}, "Import Folder\xE2\x80\xA6", true, 0, "Import a folder of pxtf, pxfm and pxgl files into a new .pxfdb")) Guard(G_IMPORT_DIALOG, "");
    TextCentered("Ctrl+O", (Rectangle){c.x, c.y + 174, c.width, 20}, TH_TEXT_DIM);
}

/* The Export dialog: what the folder holds, Append or Overwrite, and the backup of the folder's current data */
static void DrawExportDialog(int W, int H)
{
    if (!xd.open) return;
    DrawRectangle(0, 0, W, H, (Color){0x14, 0x14, 0x1C, 0x66});
    uiBlocked = false;
    bool fresh = xd.frame == frameNo;
    const float pw = 560, pad = 24, ph = 352;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    DrawRectangleRounded((Rectangle){pr.x - 1, pr.y + 5, pr.width + 2, pr.height + 2}, 0.04f, 10, (Color){0, 0, 0, 22});
    RRect(pr, TH_PANEL, TH_LINE);
    float x = pr.x + pad, w = pr.width - 2 * pad, y = pr.y + pad;
    Th_Text("Export to Folder", x, y, TH_TEXT);
    Th_Text("Export to Folder", x + 1, y, TH_TEXT);
    y += 30;
    char fit[PATH_MAX_LEN + 8];
    HeadFit(xd.folder, w, fit, sizeof fit);
    Th_TextMono(fit, x, y, TH_TEXT_DIM);
    y += 26;
    char found[200], line[300];
    store_counts_text(&xd.scan.found, found, sizeof found);
    snprintf(line, sizeof line, "Found in the folder: %s.", found);
    Th_Text(line, x, y, TH_TEXT);
    y += 30;
    char lines[4][160];
    int nl = WrapLines("Append adds what the folder does not have yet. Nothing that is there is changed or deleted.", w, lines, 4);
    for (int i = 0; i < nl; i++) { Th_Text(lines[i], x, y, TH_TEXT_DIM); y += 19; }
    y += 4;
    nl = WrapLines("Overwrite deletes the contents of pxtf, pxfm and pxgl in the folder, then writes everything in memory there. Other files stay.", w, lines, 4);
    for (int i = 0; i < nl; i++) { Th_Text(lines[i], x, y, TH_TEXT_DIM); y += 19; }
    y += 14;
    Rectangle cb = {x, y, 16, 16};
    if (Checkbox(cb, &xd.backup, true, "xd.backup") && fresh) xd.backup = !xd.backup;
    Th_Text("Make a backup first (a .pxfdb of what the folder holds now)", x + 26, Fl(y + (16 - Th_Measure("M").y) / 2), TH_TEXT);
    y += 30;
    {
        float bw = 96;
        Rectangle ir = {x + 26, y, w - 26 - bw - 8, 30}, br = {ir.x + ir.width + 8, y, bw, 30};
        DrawInputBox(ir, xd.backupPath, false, false, xd.backup);
        Th_Reg("xd.backup_path", ir);
        Th_Reg("xd.change", br);
        if (Button(br, "Change\xE2\x80\xA6", xd.backup, 0, "Choose where the backup file goes") && !fresh) {
            char p[PATH_MAX_LEN];
            if (DlgSave(p, sizeof p, xd.backupPath)) { Slashes(p); snprintf(xd.backupPath, sizeof xd.backupPath, "%s", p); }
        }
    }
    y = pr.y + pr.height - pad - 32;
    int clicked = -1;
    float bx = pr.x + pr.width - pad;
    const char *lb[3] = {"Cancel", "Append", "Overwrite"};
    for (int i = 2; i >= 0; i--) {
        float bw = Fl(Th_Measure(lb[i]).x) + 34;
        if (bw < 96) bw = 96;
        bx -= bw;
        if (Button((Rectangle){bx, y, bw, 32}, lb[i], true, i == 1 ? 1 : (i == 2 ? 2 : 0), NULL) && !fresh) clicked = i;
        bx -= 8;
    }
    if (!fresh && IsKeyPressed(KEY_ESCAPE)) clicked = 0;
    if (clicked == 0) xd.open = false;
    else if (clicked == 1) { xd.open = false; ExportRun(xd.folder, STORE_EXPORT_APPEND, ""); }
    else if (clicked == 2) ExportOverwriteChosen();
}

/* ------------------------------------------------------------------------------------------------
 * Modal drawing and results
 * ---------------------------------------------------------------------------------------------- */
static void ModalResult(int b)
{
    int action = modal.action;
    int32_t cp = modal.pendingCp;
    char input[80];
    snprintf(input, sizeof input, "%s", modal.input);
    modal.kind = M_NONE;
    switch (action) {
    case A_DELETE: if (b == 1) CmdDelete(); break;
    case A_CLONE:
        if (b == 1) DoCloneUncoded();
        else if (b == 2) {
            int32_t c;
            if (!ParseCodepoint(input, &c)) { /* handled by disabled button */ break; }
            if (px_font_find(&font, c) >= 0) {
                char msg[160];
                snprintf(msg, sizeof msg, "U+%04X already exists in this font.\nReplace it with the clone?", (unsigned)c);
                ModalOpen(M_CONFIRM, A_CLONE_REPLACE, "Replace glyph", msg, 2, "Cancel", "Replace", NULL, 1, 0, true);
                modal.pendingCp = c;
            } else DoClone(c);
        }
        break;
    case A_CLONE_REPLACE: if (b == 1) DoClone(cp); break;
    case A_UNSAVED:
        if (b == 2) { if (SaveDb()) RunGuarded(); else guard.kind = G_NONE; }
        else if (b == 1) RunGuarded();
        else guard.kind = G_NONE;
        break;
    case A_IMPORT_REPLACE: if (b == 1) ImportRun(); break;
    case A_SAVEAS_REPLACE: if (b == 1) DoSaveAs(saveAsPending); break;
    case A_OVERWRITE_NOBACKUP: if (b == 1) ExportRun(xd.folder, STORE_EXPORT_OVERWRITE, ""); break;
    case A_BACKUP_FAILED: if (b == 1) ExportRun(xd.folder, STORE_EXPORT_OVERWRITE, ""); break;
    case A_DELETE_ITEM:
        if (b == 1) DeleteConfirmed();
        store_plan_free(&del.plan);
        del.have = false;
        break;
    default: break;
    }
}

static void DrawModal(int W, int H)
{
    if (modal.kind == M_NONE) return;
    DrawRectangle(0, 0, W, H, (Color){0x14, 0x14, 0x1C, 0x66});
    uiBlocked = false;
    bool fresh = modal.frame == frameNo;

    const float pw = 440, pad = 24;
    char lines[8][160];
    int nl = WrapLines(modal.msg, pw - 2 * pad, lines, 8);
    float ph = pad + 20 + 10 + nl * 19 + 20 + 32 + pad - 4;
    if (modal.kind == M_INPUT) ph += 32 + 8 + 20 + 4;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    DrawRectangleRounded((Rectangle){pr.x - 1, pr.y + 5, pr.width + 2, pr.height + 2}, 0.06f, 10, (Color){0, 0, 0, 22});
    DrawRectangleRounded((Rectangle){pr.x, pr.y + 2, pr.width, pr.height}, 0.06f, 10, (Color){0, 0, 0, 22});
    RRect(pr, TH_PANEL, TH_LINE);

    float y = pr.y + pad;
    Th_Text(modal.title, pr.x + pad, y, TH_TEXT);
    Th_Text(modal.title, pr.x + pad + 1, y, TH_TEXT); /* second pass: poor man's semibold */
    y += 20 + 10;
    for (int i = 0; i < nl; i++) { Th_Text(lines[i], pr.x + pad, y, TH_TEXT_DIM); y += 19; }
    y += 6;

    bool inputOk = true;
    if (modal.kind == M_INPUT) {
        Rectangle ir = {pr.x + pad, y, pr.width - 2 * pad, 32};
        DrawInputBox(ir, modal.input, true, false, true);
        if (!fresh) EditText(modal.input, (int)sizeof modal.input, false);
        else (void)GetCharPressed();
        y += 32 + 8;
        int32_t cp;
        char info[120];
        Color ic = TH_TEXT_DIM;
        if (modal.input[0] == 0) { snprintf(info, sizeof info, "Enter a code point"); inputOk = false; }
        else if (!ParseCodepoint(modal.input, &cp)) { snprintf(info, sizeof info, "Not a valid code point"); inputOk = false; ic = TH_DANGER; }
        else {
            int sz = 0;
            const char *u = CodepointToUTF8(cp, &sz);
            char chs[8] = "";
            if (PrintableInFont(cp)) { memcpy(chs, u, (size_t)sz); chs[sz] = 0; }
            if (px_font_find(&font, cp) >= 0) { snprintf(info, sizeof info, "U+%04X %s - already in the font, will ask to replace", (unsigned)cp, chs); ic = (Color){0xB0, 0x6A, 0x00, 0xFF}; }
            else snprintf(info, sizeof info, "U+%04X %s", (unsigned)cp, chs);
        }
        Th_Text(info, pr.x + pad, y, ic);
        y += 20 + 4;
    }
    y = pr.y + pr.height - pad + 4 - 32;
    float bx = pr.x + pr.width - pad;
    int clicked = -1;
    for (int i = modal.nbtn - 1; i >= 0; i--) {
        float bw = Fl(Th_Measure(modal.btn[i]).x) + 34;
        if (bw < 84) bw = 84;
        bx -= bw;
        int style = (i == modal.primary) ? (i == modal.danger ? 2 : 1) : 0;
        bool en = !(modal.kind == M_INPUT && i == modal.primary && !inputOk);
        Rectangle br = {bx, y, bw, 32};
        if (style == 2) {
            /* filled danger button */
            bool hv = Hov(br);
            Th_RegLabel(modal.btn[i], br);
            RRect(br, hv ? Mix(TH_DANGER, BLACK, 0.1f) : TH_DANGER, TH_DANGER);
            TextCentered(modal.btn[i], br, WHITE);
            if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !fresh) clicked = i;
        } else if (Button(br, modal.btn[i], en, style, NULL) && !fresh) clicked = i;
        bx -= 8;
    }
    if (!fresh) {
        if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && !(modal.kind == M_INPUT && !inputOk)) clicked = modal.primary;
        if (IsKeyPressed(KEY_ESCAPE)) clicked = modal.cancel;
    }
    if (clicked >= 0) ModalResult(clicked);
}

/* ------------------------------------------------------------------------------------------------
 * Shortcuts
 * ---------------------------------------------------------------------------------------------- */
static void HandleShortcuts(void)
{
    if (modal.kind != M_NONE || xd.open || editName[0] != 0 || ctFocus >= 0 || Sample_TextFocused()) return;   /* a text field owns the keyboard */
    if (inEditor && ed && Layers_TextFocus(Editor_Layers(ed))) return;   /* a field of the Layers tab owns the keyboard */
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (ctrl && IsKeyPressed(KEY_O)) { Guard(G_LOAD_DIALOG, ""); return; }
    if (ctrl && shift && IsKeyPressed(KEY_S)) { SaveAsDialog(); return; }
    if (ctrl && !shift && IsKeyPressed(KEY_S)) { SaveDb(); return; }
    if (ctrl && !shift && IsKeyPressed(KEY_G) && !inFinder && store) { ShowGlyphs(); return; }
    if (alt && IsKeyPressed(KEY_RIGHT) && hasFont && (inBrowser || inFinder)) { ShowFont(); return; }
    if (alt && IsKeyPressed(KEY_LEFT) && inFinder) { if (hasFont && !wasLibrary) ShowFont(); else ShowLibrary(); return; }
    if (alt && IsKeyPressed(KEY_LEFT) && hasFont && !inBrowser) { GoBrowser(); return; }
    if (inBrowser || inFinder || !hasFont) return;
    if (ctrl && shift && IsKeyPressed(KEY_F)) { CmdFindInLibrary(); return; }
    if (ctrl && IsKeyPressed(KEY_Z) && !shift) { if (readOnly) ToastPush(2, "An original is read-only"); else DoUndoRedo(false); return; }
    if (ctrl && (IsKeyPressed(KEY_Y) || (IsKeyPressed(KEY_Z) && shift))) { if (!readOnly) DoUndoRedo(true); return; }
    if (inEditor) return;
    if (ctx.open) return;

    if (ctrl && shift && IsKeyPressed(KEY_A)) { CmdSelectOccurrences(); return; }
    if (ctrl && IsKeyPressed(KEY_A)) { CmdSelectAll(); return; }
    if (ctrl && !shift && IsKeyPressed(KEY_D)) { CmdClone(); return; }
    if (IsKeyPressed(KEY_ESCAPE)) { SelClear(); return; }
    if (IsKeyPressed(KEY_DELETE)) { CmdDeleteAsk(); return; }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) { CmdEditSelected(); return; }
    if (!ctrl && !alt) {
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) ApplyZoom(zoom + 1);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) ApplyZoom(zoom - 1);
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) MoveSelection(1, 0, shift);
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) MoveSelection(-1, 0, shift);
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) MoveSelection(0, 1, shift);
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) MoveSelection(0, -1, shift);
        if (IsKeyPressed(KEY_PAGE_DOWN) || IsKeyPressedRepeat(KEY_PAGE_DOWN)) scrollY += 400;
        if (IsKeyPressed(KEY_PAGE_UP) || IsKeyPressedRepeat(KEY_PAGE_UP)) scrollY -= 400;
        if (IsKeyPressed(KEY_HOME)) scrollY = 0;
        if (IsKeyPressed(KEY_END)) scrollY = 1e9f;
    }
}

#include "play.inc"

/* the options of the command line that apply to the first font (they wait for it when the library opens in the background) */
static void ApplyStartup(void)
{
    if (!hasFont) return;
    if (startup.zoom) zoom = startup.zoom < 1 ? 1 : (startup.zoom > 16 ? 16 : startup.zoom);
    if (startup.sel) {
        SelResize();
        const char *p = startup.sel;
        while (*p) {
            int v = atoi(p);
            if (v >= 0 && v < selN) { sel[v] = true; if (selAnchor < 0) selAnchor = v; }
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
    }
    if (startup.edit >= 0) OpenEditor(startup.edit);
    if (startup.browser && !inBrowser) GoBrowser();
    panelScroll = (float)startup.panelScroll;
}

/* ------------------------------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    const char *openPath = NULL, *shotPath = NULL, *modalArg = NULL;
    const char *playPath = NULL, *jobShotPath = NULL;
    int jobShotMs = 1500, jobHoldMs = 0;
    bool dupArg = false;
    char importTo[PATH_MAX_LEN] = "";
    int shotFrames = 5, winW = 1400, winH = 900;
    startup.edit = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--import-to") && i + 1 < argc) { snprintf(importTo, sizeof importTo, "%s", argv[++i]); Slashes(importTo); }   /* the target of a folder given on the command line (no dialog) */
        else if (!strcmp(a, "--font") && i + 1 < argc) snprintf(startup.font, sizeof startup.font, "%s", argv[++i]);   /* open this font of the database */
        else if (!strcmp(a, "--typeface") && i + 1 < argc) snprintf(startup.typeface, sizeof startup.typeface, "%s", argv[++i]);
        else if (!strcmp(a, "--ini") && i + 1 < argc) { snprintf(iniOverride, sizeof iniOverride, "%s", argv[++i]); Slashes(iniOverride); }
        else if (!strcmp(a, "--screenshot") && i + 1 < argc) shotPath = argv[++i];
        else if (!strcmp(a, "--shot-job") && i + 1 < argc) jobShotPath = argv[++i];       /* a screenshot of the progress screen ... */
        else if (!strcmp(a, "--shot-job-ms") && i + 1 < argc) jobShotMs = atoi(argv[++i]); /* ... after this many ms of opening */
        else if (!strcmp(a, "--job-hold-ms") && i + 1 < argc) jobHoldMs = atoi(argv[++i]); /* keep the progress screen at least this long */
        else if (!strcmp(a, "--frames") && i + 1 < argc) shotFrames = atoi(argv[++i]);
        else if (!strcmp(a, "--select") && i + 1 < argc) startup.sel = argv[++i];
        else if (!strcmp(a, "--zoom") && i + 1 < argc) startup.zoom = atoi(argv[++i]);
        else if (!strcmp(a, "--edit") && i + 1 < argc) startup.edit = atoi(argv[++i]);
        else if (!strcmp(a, "--duplicate")) dupArg = true;
        else if (!strcmp(a, "--browser")) startup.browser = true;
        else if (!strcmp(a, "--panel-scroll") && i + 1 < argc) startup.panelScroll = atoi(argv[++i]);
        else if (!strcmp(a, "--play") && i + 1 < argc) playPath = argv[++i];
        else if (!strcmp(a, "--no-dialogs")) noDialogs = true;
        else if (!strcmp(a, "--modal") && i + 1 < argc) modalArg = argv[++i];
        else if (!strcmp(a, "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &winW, &winH);
        else if (a[0] != '-' && !openPath) openPath = a;
    }
    startup.dup = dupArg;

    if (playPath) { noDialogs = true; if (!PlayLoad(playPath)) return 2; }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (playPath ? FLAG_WINDOW_ALWAYS_RUN : 0));   /* a script keeps running when the window is minimised or hidden */
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(winW, winH, "pxfe");
    Browser_SetCharSource(Play_GetChar);
    SetWindowMinSize(1000, 600);
    SetExitKey(0);
    SetTargetFPS(playPath ? play.fps : 60);
    Theme_Load();
    Sample_LoadIni(IniPath());   /* the shared sample text and the font view strip settings */
    { char v[8]; if (IniGetApp("panelTab", v, sizeof v)) { int t = atoi(v); panelTab = t < 0 || t > 3 ? 0 : t; } }

    px_font_init(&font);
    pxtf_init(&cont);
    job.holdMs = jobHoldMs;
    if (openPath) {
        startup.pending = true;
        OpenAny(openPath, importTo);
        if (!job.active && !hasFont) startup.pending = false;
    } else {
        char last[PATH_MAX_LEN];
        if (IniGetLastDb(last, sizeof last) && FileExists(last)) OpenAny(last, "");
        if (startup.zoom) zoom = startup.zoom < 1 ? 1 : (startup.zoom > 16 ? 16 : startup.zoom);
    }

    if (modalArg && hasFont) {
        if (!strcmp(modalArg, "delete")) CmdDeleteAsk();
        else if (!strcmp(modalArg, "clone")) CmdClone();
        else if (!strcmp(modalArg, "unsaved")) { ModalOpen(M_CONFIRM, A_UNSAVED, "Unsaved changes", "pxfdb.pxfdb has changes that are not saved.\nSave them first, or discard them?", 3, "Cancel", "Discard", "Save", 2, 0, false); }
        else if (!strcmp(modalArg, "error")) ShowError("Cannot open file", "Cannot find the glyph library for this file.\nIt is not inside <root>/pxfm/xx/.");
        else if (!strcmp(modalArg, "toast")) { ToastPush(1, "Saved pxfm 1e1a3ab8, pxtf 3c4d5e6f"); ToastPush(2, "Select exactly one glyph to clone"); }
    }
    int shotCounter = 0;
    bool jobShotDone = false;
    while (!quit) {
        frameNo++;
        if (job.active && job.finished && GetTime() - job.t0 >= job.holdMs / 1000.0) JobFinish();
        if (play.active) { PlayBeginFrame(); if (quit) break; }
        if (WindowShouldClose()) {
            if (job.active) { quit = true; break; }
            if (modal.kind != M_NONE && modal.action == A_UNSAVED) { /* already asking */ }
            else RequestQuit();
        }
        int W = GetScreenWidth(), H = GetScreenHeight();
        EnsureHashes();
        if (!shotPath && !job.active) HandleShortcuts();

        BeginDrawing();
        ClearBackground(TH_BG);
        /* the mouse button that opened a document (a double click) is still down: its release must not hit the new view */
        if (swallowMouse && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) swallowMouse = false;
        bool baseBlocked = modal.kind != M_NONE || xd.open || job.active || swallowMouse;
        uiBlocked = baseBlocked || fileMenu.open || moreMenu.open;   /* an open menu owns the mouse: nothing below it reacts */
        Th_SetInputBlocked(uiBlocked);
        hintText = NULL;
        wheelUsed = false;
        Th_RegClear();
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        if (job.active) {
            DrawRectangle(0, 0, W, TOOLBAR_H, TH_PANEL);
            Th_HLine(0, (float)W, TOOLBAR_H - 1, TH_LINE);
            DrawProgressScreen(W, H);
            DrawToasts(W, H);
            UpdateTitle();
            EndDrawing();
            if (jobShotPath && !jobShotDone && (GetTime() - job.t0) * 1000.0 >= jobShotMs) { TakeScreenshot(jobShotPath); jobShotDone = true; }
            continue;
        }

        bool fontView = hasFont && !inBrowser && !inFinder;
        bool edFrame = fontView && inEditor;   /* the glyph view has its own bar: no command bar, no info bar */
        float top = TOOLBAR_H + (fontView && !inEditor ? CMDBAR_H : 0);
        float contentH2 = (float)(H - top - STATUS_H);
        bool ownStatus = inBrowser && browser && !inFinder;   /* the browser has its own status line: no second bar below it */
        Rectangle fullArea = {0, TOOLBAR_H, (float)W, (float)(H - TOOLBAR_H - STATUS_H) + (ownStatus ? STATUS_H : 0)};
        Rectangle cmdArea = {0, TOOLBAR_H, (float)W, CMDBAR_H};
        Rectangle infoArea = {0, top, (float)(W - PANEL_W), INFO_H};
        Rectangle gridArea = {0, top + INFO_H, (float)(W - PANEL_W), contentH2 - INFO_H};
        Rectangle panelArea = {(float)(W - PANEL_W), top, PANEL_W, contentH2};
        Rectangle edArea = {0, top, (float)(W - PANEL_W), contentH2};
        Rectangle stripArea = {0};   /* the font view's sample strip, at the bottom of the grid */
        if (fontView && !inEditor) {
            float sh = SampleStrip_Height(gridArea.height);
            gridArea.height -= sh;
            stripArea = (Rectangle){0, gridArea.y + gridArea.height, gridArea.width, sh};
        }
        play.gridArea = gridArea;
        hoverIdx = -1;
        if (!fontView) { ctFocus = -1; editName[0] = 0; ctx.open = false; }
        if (inFinder && finder) {
            InsightTarget tg;
            memset(&tg, 0, sizeof tg);
            if (Finder_UpdateDraw(finder, fullArea, &tg) && tg.action != INSIGHT_NONE) {
                if (tg.action == INSIGHT_FIND_CODE) HandleInsightTarget(&tg);
                else HandleInsightTarget(&tg);
            }
        } else if (inBrowser && browser) {
            BrowserChoice ch;
            memset(&ch, 0, sizeof ch);
            if (Browser_UpdateDraw(browser, fullArea, &ch) && ch.action != BROWSER_NONE) {
                if (ch.action == BROWSER_DELETE_FONT) AskDelete(false, ch.pxfm, false);
                else if (ch.action == BROWSER_DELETE_TYPEFACE) AskDelete(true, ch.pxtf, false);
                else if (ch.action == BROWSER_INSIGHT && ch.insight.action == INSIGHT_FIND_CODE) HandleInsightTarget(&ch.insight);
                else RunChoice(&ch);
            }
        } else if (!hasFont) {
            DrawEmptyState(fullArea);
        } else {
            if (inEditor) {
                Layers_SetStore(Editor_Layers(ed), store);
                Th_SetInputBlocked(uiBlocked || editName[0] != 0 || ctFocus >= 0 || Layers_TextFocus(Editor_Layers(ed)) ||
                                   Layers_PopoverOpen(Editor_Layers(ed))); /* a field being edited owns the keyboard */
                Editor_SetLayersTabActive(ed, glyphTab == 2);
                EditorResult er = Editor_UpdateDraw(ed, edArea);
                if (Editor_TakeLayersRequest(ed)) { glyphTab = 2; panelScroll = 0; }
                Th_SetInputBlocked(uiBlocked);
                if (Editor_TakeChanged(ed)) needHash = true;
                int gi = Editor_GlyphIndex(ed);
                if (er == EDITOR_CLOSE) {
                    inEditor = false;
                    SelResize();
                    SelOnly(gi);
                    revealIdx = gi;
                } else if (er == EDITOR_PREV || er == EDITOR_NEXT) {
                    int ni = gi + (er == EDITOR_NEXT ? 1 : -1);
                    if (ni >= 0 && ni < font.count) OpenEditor(ni);
                } else if (er == EDITOR_DUPLICATE) {
                    DuplicateToEdit();
                }
            } else {
                if (ed) Layers_PopoverClose(Editor_Layers(ed));
                UpdateDrawGrid(gridArea);
                if (stripArea.height > 0) SampleStrip_Draw(stripArea, &font, uiBlocked);
            }
            if (edFrame == inEditor) {   /* (the frame in which the glyph view opens or closes draws no panel) */
                if (!inEditor) { DrawCommandBar(cmdArea); DrawInfoBar(infoArea); }
                bool popOpen = inEditor && ed && Layers_PopoverOpen(Editor_Layers(ed));
                if (popOpen) Th_SetInputBlocked(true);   /* the Add Layer popover is modal */
                DrawMetricsPanel(panelArea);
                Th_SetInputBlocked(uiBlocked);
                if (inEditor && ed) Layers_DrawPopover(Editor_Layers(ed), panelArea);
            }
        }
        uiBlocked = baseBlocked;   /* the toolbar and the menus themselves stay live */
        Th_SetInputBlocked(uiBlocked);
        DrawToolbar(W);
        EnsureHashes();
        DrawStatus(W, H);
        DrawFileMenu();
        DrawMoreMenu();
        DrawContextMenu(W, H);
        DrawToasts(W, H);
        DrawExportDialog(W, H);
        DrawModal(W, H);
        UpdateTitle();
        EndDrawing();
        if (play.active) PlayEndFrame();
        if (panelTargetSet) {   /* a click on an entry of the glyph usage or similar list in the panel */
            panelTargetSet = false;
            if (panelTarget.action == INSIGHT_FIND_CODE) HandleInsightTarget(&panelTarget);
            else HandleInsightTarget(&panelTarget);
        }

        if (shotPath && !job.active && ++shotCounter >= shotFrames) {
            TakeScreenshot(shotPath);
            break;
        }
    }

    JobAbort();
    StackClear(undoStack, &undoN);
    StackClear(redoStack, &redoN);
    CloseLibrary();
    if (ed) Editor_Destroy(ed);
    if (hasFont) px_font_free(&font);
    if (hasCont) pxtf_free(&cont);
    free(lx); free(ly); free(lw); free(sel); free(ghCp); free(ghHash);
    Theme_Unload();
    CloseWindow();
    return play.active ? PlayFinish() : 0;
}
