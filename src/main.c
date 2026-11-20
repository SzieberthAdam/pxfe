/* main.c - pxfe main window: app state, views (library browser, font view, glyph view), documents (locked and unlocked fonts,
 * load / save / import / export of the .pxfdb), toolbar, glyph grid, status bar, dialogs, undo (see DESIGN.md). */
#include "raylib.h"

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
#include "package.h"
#include "sample.h"
#include "theme.h"
#include "editor.h"
#include "colorpick.h"
#include "layers.h"
#include "flayers.h"
#include "fit.h"
#include "sheet.h"
#include "sheetref.h"
#include "fsettings.h"
#include "winfile.h"

/* Scripted text input (hidden --play flag): GetCharPressed is wrapped so the script runner can inject characters.
 * The e2e build also passes -DGetCharPressed=Play_GetChar to every file, so the finder (finder.c) reads the shim too. */
int Play_GetChar(void);
#define GetCharPressed Play_GetChar

/* ------------------------------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------------------------------- */
#define TOOLBAR_H   48
#define STATUS_H    TH_ROW_H
#define PANEL_W     352
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
static bool readOnly = false;          /* the font is locked: nothing may change it (every font is locked until it is unlocked) */
static bool contLocked = true;         /* the typeface of the open font is locked: its fields cannot be edited */
static bool docStored = false;         /* the open font is in the store (false: New Font or an imported font until its first commit) */
static bool quit = false;

static char title[256];                /* the name of the font */
static char origTitle[256];            /* the name it was opened with */
static char docPath[PATH_MAX_LEN];     /* what the status bar shows: <hash>.pxfm of a store font, or the .pxfd */
static char sourcePath[PATH_MAX_LEN];  /* "<hash>.pxfm" of the font in the store ("" if none) */
static char libRoot[PATH_MAX_LEN];     /* the open database file ("" none) */
static char iniOverride[PATH_MAX_LEN];

/* Change detection by hash: modified = current canonical hash differs from the one the store holds for the font (the last
 * commit), or the typeface text differs.  A commit changes the store in place and moves these to the new hashes. */
static char origHash[33], origContHash[33];  /* what the store holds */
static char curHash[33], curContHash[33];    /* of the current state */
static bool modified = false, needHash = false;
static char exportSummary[PATH_MAX_LEN + 900];              /* the text of the last export summary */
static char lastPxfm[33], lastPxtf[33], lastPxfmPath[PATH_MAX_LEN], lastPxtfPath[PATH_MAX_LEN];

/* the pxgl hash of every glyph when the font was opened (sorted by code point) for the glyph view */
static PxCode *ghCp = NULL;
static char (*ghHash)[33] = NULL;
static int ghN = 0;

/* Dialog overrides for scripted runs (play command setdialog). */
static char dlgFolderPath[PATH_MAX_LEN], dlgSavePath[PATH_MAX_LEN], dlgLayerPath[PATH_MAX_LEN];
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

/* the file of a font layer (Add Layer popover) */
static bool DlgLayerFile(char *out, int size)
{
    if (dlgLayerPath[0]) { snprintf(out, (size_t)size, "%s", dlgLayerPath); return true; }
    if (noDialogs) return false;
    return Win_OpenFileDialog(out, size, FL_FILE_FILTER);
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
static int glyphTab = 0;               /* tab of the glyph view's right panel: 0 Glyph, 2 Layers, 3 View (not remembered) */
static bool TypefaceEditing(const char *pxtf);   /* panel.inc */
static bool FlPending(void);                    /* font layers (below) */
static bool FsDirty(void);
static void CmdAddGlyph(void);
static void NewFontOpen(void);
static bool SaveAsDialog(void);
static void ImportFontOpen(void);
static FlLayer *FollowCopy(void);          /* fitdoc.inc: the settings of the layer the glyphs are drawn from, for an undo snapshot */
static void FollowRestore(FlLayer *snap);
static void FitSnapDrop(void);
static void LayersShiftY(float d);         /* fitdoc.inc: every font layer moves by d rows (the baseline moved) */
static bool filledShapes = false;      /* the glyph view's Filled option for the rectangle and the ellipse (kept in pxfe.ini, key filled) */
static bool pixelPerfect = false;      /* the glyph view's Pixel-perfect option (kept in pxfe.ini, key pixelPerfect) */
static int panelTab = 0;               /* tab of the right-hand panel: 0 Font, 1 Glyph, 2 Similar, 3 Layers, 4 View (kept in pxfe.ini) */

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
 * UI primitives (flat, themed)
 * ---------------------------------------------------------------------------------------------- */
static bool uiBlocked = false;
static bool swallowMouse = false;   /* a left button held while a document opened: ignore it until it is released */
static const char *hintText = NULL;
static Rectangle hovClip;              /* while hovClipOn, widgets only react inside this rectangle (scrolled panels) */
static bool hovClipOn = false;

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

/* ------------------------------------------------------------------------------------------------
 * Undo stack, selection
 * ---------------------------------------------------------------------------------------------- */
static PxFont *undoStack[UNDO_MAX + 1], *redoStack[UNDO_MAX + 1];
static FlLayer *undoFit[UNDO_MAX + 1], *redoFit[UNDO_MAX + 1];   /* beside each snapshot: the settings its glyphs were drawn with (fitdoc.inc), or NULL */
static int undoN = 0, redoN = 0;
/* beside each snapshot: how far the baseline had been moved up in all (the font layers sit on it), and the code of the glyph the glyph view showed (-1: none) */
static int undoBase[UNDO_MAX + 1], redoBase[UNDO_MAX + 1];
static PxCode undoCode[UNDO_MAX + 1], redoCode[UNDO_MAX + 1];
static int baseShift = 0;
static int lastUndoKey = 0;
static double lastUndoTime = 0;

static void StackClear(PxFont **st, int *n)
{
    FlLayer **fit = st == undoStack ? undoFit : redoFit;
    for (int i = 0; i < *n; i++) { px_font_free(st[i]); free(st[i]); FlLayer_Free(fit[i]); fit[i] = NULL; }
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
    if (!px_font_copy(s, &font)) { free(s); return; }     /* the snapshot's palette table is compacted */
    px_font_compact_palettes(&font);                        /* the palettes no glyph names any more go (between edits) */
    if (undoN == UNDO_MAX) {
        px_font_free(undoStack[0]); free(undoStack[0]);
        FlLayer_Free(undoFit[0]);
        memmove(&undoStack[0], &undoStack[1], sizeof(PxFont *) * (UNDO_MAX - 1));
        memmove(&undoFit[0], &undoFit[1], sizeof(FlLayer *) * (UNDO_MAX - 1));
        memmove(&undoBase[0], &undoBase[1], sizeof(int) * (UNDO_MAX - 1));
        memmove(&undoCode[0], &undoCode[1], sizeof(PxCode) * (UNDO_MAX - 1));
        undoN--;
    }
    undoFit[undoN] = FollowCopy();
    undoBase[undoN] = baseShift;
    undoCode[undoN] = (inEditor && ed && Editor_GlyphIndex(ed) >= 0 && Editor_GlyphIndex(ed) < font.count) ? font.glyphs[Editor_GlyphIndex(ed)].code : PX_NO_CODE;
    undoStack[undoN++] = s;
}

/* A font made by New Font: it is not in the store yet, so it counts as modified until it is committed (and again after every commit's edits). */
static bool docIsNew = false;

static void EnsureHashes(void)
{
    if (!hasFont) return;
    if (!needHash && curHash[0]) return;
    px_font_hash(&font, curHash);
    if (hasCont) pxtf_hash(&cont, curContHash); else curContHash[0] = 0;
    modified = docIsNew || !Same(curHash, origHash) || (hasCont && !Same(curContHash, origContHash));
    needHash = false;
}

static void SnapGlyphHashes(void)
{
    free(ghCp); free(ghHash);
    ghCp = NULL; ghHash = NULL; ghN = 0;
    if (!hasFont || font.count == 0) return;
    ghCp = (PxCode *)malloc(sizeof(PxCode) * (size_t)font.count);
    ghHash = (char (*)[33])malloc(33 * (size_t)font.count);
    for (int i = 0; i < font.count; i++) { ghCp[i] = font.glyphs[i].code; px_glyph_hash(&font.glyphs[i], ghHash[i]); }
    ghN = font.count;
}

static const char *OrigGlyphHash(PxCode cp)
{
    if (!px_code_is_codepoint(cp) && !px_code_is_default(cp)) return NULL;   /* a symbol's code is renumbered by deletes: its origin is unknown */
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
    if (!ed) { ed = Editor_Create(); Editor_SetPixelPerfect(ed, pixelPerfect); Editor_SetFilled(ed, filledShapes); }
    Editor_Open(ed, &font, idx, EditorBefore, NULL);
    Editor_SetReadOnly(ed, readOnly);
    Editor_SetOriginalHash(ed, OrigGlyphHash(font.glyphs[idx].code));
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
    FlLayer **fromFit = redo ? redoFit : undoFit, **toFit = redo ? undoFit : redoFit;
    int *fromBase = redo ? redoBase : undoBase, *toBase = redo ? undoBase : redoBase;
    PxCode *fromCode = redo ? redoCode : undoCode, *toCode = redo ? undoCode : redoCode;
    int *tn = redo ? &undoN : &redoN;
    if (*fn == 0) return;
    PxFont *cur = (PxFont *)malloc(sizeof(PxFont));
    if (!px_font_copy(cur, &font)) { free(cur); return; }
    FlLayer *curFit = FollowCopy();
    PxFont *s = from[--(*fn)];
    FlLayer *sFit = fromFit[*fn];
    fromFit[*fn] = NULL;
    px_font_free(&font);
    font = *s;
    free(s);
    toFit[*tn] = curFit;
    toBase[*tn] = baseShift;
    PxCode hadCode = (inEditor && ed && Editor_GlyphIndex(ed) >= 0 && Editor_GlyphIndex(ed) < cur->count) ? cur->glyphs[Editor_GlyphIndex(ed)].code : PX_NO_CODE;
    toCode[*tn] = hadCode;
    to[(*tn)++] = cur;
    PxCode wantCode = fromCode[*fn];
    if (fromBase[*fn] != baseShift) { LayersShiftY((float)(baseShift - fromBase[*fn])); baseShift = fromBase[*fn]; }   /* the layers follow the baseline back */
    FollowRestore(sFit);   /* the layer the glyphs are drawn from gets the settings this font was drawn with */
    needHash = true;
    lastUndoKey = 0;
    SelResize();
    SelClear(); /* indices may no longer match */
    if (inEditor) {
        int gi = Editor_GlyphIndex(ed);
        if (wantCode >= 0 && (gi >= font.count || font.glyphs[gi].code != hadCode) && px_font_find_code(&font, wantCode) >= 0)
            gi = px_font_find_code(&font, wantCode);   /* the glyph is not where it was (its code changed): follow it by the code it had */
        if (gi >= font.count) gi = font.count - 1;
        if (gi >= 0) OpenEditor(gi); else inEditor = false;
    }
    Th_Toast(0, redo ? "Redo" : "Undo");
}

/* ------------------------------------------------------------------------------------------------
 * Modal dialogs
 * ---------------------------------------------------------------------------------------------- */
enum { M_NONE, M_CONFIRM, M_INPUT };
enum { A_NONE, A_DELETE, A_CLONE, A_CLONE_REPLACE, A_UNSAVED, A_MESSAGE, A_DELETE_ITEM, A_IMPORT_REPLACE, A_SAVEAS_REPLACE, A_OVERWRITE_NOBACKUP, A_BACKUP_FAILED, A_ADDGLYPH };

static struct {
    int kind, action;
    char title[64], msg[800];
    char btn[3][32];
    int nbtn, primary, cancel, danger;
    char input[80];
    int frame;
    PxCode pendingCp;
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
    if (tab == panelTab || tab < 0 || tab > 4) return;
    panelTab = tab;
    panelScroll = 0;
    char v[8];
    snprintf(v, sizeof v, "%d", tab);
    IniSetApp("fontTab", v);
}

/* ------------------------------------------------------------------------------------------------
 * The database, documents: opening fonts, locks, committing edits into the store
 * ---------------------------------------------------------------------------------------------- */

/* Editing in place: the open font is the font of the store (and its typeface); an unlocked one is changed in the store, under its new
 * hash, when its font view is left, before Save, Export, Load and quit (CommitEdits).  Everything that names it follows. */

static bool EditPending(void)
{
    if (!hasFont || !store) return false;
    EnsureHashes();
    if (!docStored) return true;
    return (!readOnly && !Same(curHash, origHash)) || (hasCont && !contLocked && !Same(curContHash, origContHash));
}

/* memory differs from the file as loaded or last saved, or the open font has edits that are not in memory yet */
static bool DbDirty(void)
{
    return store && (store_modified(store) || EditPending() || FsDirty() || FlPending());
}

static const char *DbName(void)
{
    return !store ? "" : store_info(store)->path[0] ? BaseName(store_info(store)->path) : "Untitled";   /* New Database: no file yet */
}

static char winTitle[PATH_MAX_LEN + 64];   /* the window title (play.inc asserts on it) */

static void UpdateTitle(void)
{
    char t[PATH_MAX_LEN + 64];
    if (store) snprintf(t, sizeof t, "%.1000s%s", DbName(), DbDirty() ? " \xE2\x80\x94 modified" : "");
    else snprintf(t, sizeof t, "pxfe");
    if (strcmp(t, winTitle) != 0) { SetWindowTitle(t); snprintf(winTitle, sizeof winTitle, "%s", t); }
}

/* "Family Style" of a typeface variant, else "Unnamed font" */
static void NameOf(const PxtfVariant *v, char *out, size_t cap)
{
    const char *fam = v && v->text[PXTF_FAMILY] ? v->text[PXTF_FAMILY] : "";
    const char *sty = v && v->text[PXTF_STYLE] ? v->text[PXTF_STYLE] : "";
    const char *full = v && v->text[PXTF_FULLNAME] ? v->text[PXTF_FULLNAME] : "";
    if (fam[0] && sty[0]) snprintf(out, cap, "%s %s", fam, sty);
    else if (fam[0]) snprintf(out, cap, "%s", fam);
    else if (full[0]) snprintf(out, cap, "%s", full);
    else snprintf(out, cap, "Unnamed font");
}

/* ------------------------------------------------------------------------------------------------
 * Font layers and the settings sidecar (flayers.h, fsettings.h)
 *
 * The sidecar <db>-settings holds, per font hash, the font layers of that font.  fsettings is the whole file in memory (like the store);
 * Save writes it (next to the .pxfdb), Save As writes a new one beside the new path, Import starts with an empty one.  The font layers of
 * the open font live in flDoc and are the truth while the font is open; they are copied into fsettings under the font's hash whenever they
 * changed (FlSync): under the font's CURRENT hash, which changes with every edit, so a
 * sync after pixel edits moves the layers to the new hash and drops the old one (a commit renames them with the font)
 * Leaving an unlocked font commits it into the store and syncs, so the layers are written under the hash it is committed with.
 * ---------------------------------------------------------------------------------------------- */
static FSettings *fsettings = NULL;
static bool fsNoWrite = false;         /* the sidecar on disk is not a settings file: never written over */
static FlDoc *flDoc = NULL;            /* the font layers of the open font */
static char flSyncedHash[33];          /* the hash flDoc was last written under */
static unsigned flSyncedRev;

static void FlDocSet(FlDoc *nd)
{
    if (ed) Layers_SetFontDoc(Editor_Layers(ed), nd);   /* the stack lets go of the old layers first */
    FlDoc_Free(flDoc);
    flDoc = nd;
    flSyncedRev = nd ? nd->rev : 0;
}

static bool FlPending(void) { return flDoc && fsettings && flDoc->rev != flSyncedRev; }
static bool FsDirty(void) { return fsettings && fs_dirty(fsettings); }

static void FsOpenFor(Store *s)
{
    if (fsettings) { fs_close(fsettings); fsettings = NULL; }
    fsNoWrite = false;
    char sp[1200], err[FS_ERR_LEN] = "";
    if (!store_info(s)->imported && fs_sidecar_path(store_info(s)->path, sp, sizeof sp)) {
        Slashes(sp);
        fsettings = fs_open(sp, err);
        if (!fsettings) {
            ShowError("Cannot read the layer settings", err[0] ? err : "Unknown error.");
            fsNoWrite = true;
        }
    }
    if (!fsettings) fsettings = fs_new_empty();
}

static void FlSync(void)
{
    if (!hasFont || !flDoc || !fsettings) return;
    EnsureHashes();
    bool revCh = flDoc->rev != flSyncedRev, hashCh = !Same(curHash, flSyncedHash);
    if (!revCh && !hashCh) return;
    if (hashCh && flSyncedHash[0] && fs_has_font(fsettings, flSyncedHash)) fs_remove_font(fsettings, flSyncedHash);
    if (!FlDoc_Store(flDoc, fsettings, curHash)) Th_Toast(TH_TOAST_ERR, "Cannot keep the layer settings: %s", fs_error(fsettings));
    snprintf(flSyncedHash, sizeof flSyncedHash, "%s", curHash);
    flSyncedRev = flDoc->rev;
}

/* the new document's layers: those the settings hold under its hash (a stored font), or none (a new font) */
static void FlOpenDoc(bool stored)
{
    FlDoc *nd = FlDoc_New();
    if (stored && fsettings) FlDoc_Load(nd, fsettings, curHash);
    FlDocSet(nd);
    snprintf(flSyncedHash, sizeof flSyncedHash, "%s", stored ? curHash : "");
}

/* writes the sidecar after the database was written (or by itself when only the settings changed); asNew: a new path (Save As) */
static bool SaveSidecar(bool asNew)
{
    if (!fsettings || fsNoWrite || !store) return true;
    char sp[1200];
    if (!fs_sidecar_path(store_info(store)->path, sp, sizeof sp)) return true;
    Slashes(sp);
    bool other = !Same(fs_path(fsettings), sp);
    if (!asNew && !other && !fs_dirty(fsettings)) return true;
    bool ok = (asNew || other) ? fs_save_as(fsettings, sp) : fs_save(fsettings);
    if (!ok) ShowError("Cannot save the layer settings", fs_error(fsettings)[0] ? fs_error(fsettings) : "Unknown error.");
    return ok;
}

static bool CommitEdits(void);

/* Takes over *nf and *nc (nc may be NULL).  origH / origContH: the baseline hashes ("" = the state as loaded). */
static void AdoptDocument(PxFont *nf, PxtfTypeface *nc, int variant, bool stored, const char *titleIn, const char *origTitleIn,
                          const char *origH, const char *origContH, const char *docP, const char *srcP)
{
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) swallowMouse = true;
    CommitEdits();      /* the document being left */
    docIsNew = false;
    baseShift = 0;
    StackClear(undoStack, &undoN);
    StackClear(redoStack, &redoN);
    if (hasFont) px_font_free(&font);
    if (hasCont) pxtf_free(&cont);
    font = *nf;
    hasFont = true;
    hasCont = nc != NULL;
    if (nc) cont = *nc;
    contVariant = variant;
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
    docStored = stored;
    readOnly = stored && !(fsettings && fs_is_unlocked(fsettings, 0, curHash));      /* a new font is unlocked: it is the user's own */
    contLocked = stored && hasCont && !(fsettings && fs_is_unlocked(fsettings, 1, curContHash));
    if (!stored) contLocked = false;
    FlOpenDoc(stored);
    FitSnapDrop();
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
    baseShift = 0;
    StackClear(undoStack, &undoN);
    StackClear(redoStack, &redoN);
    if (hasFont) px_font_free(&font);
    if (hasCont) pxtf_free(&cont);
    hasFont = false;
    hasCont = false;
    readOnly = false;
    contLocked = true;
    inEditor = false;
    curHash[0] = curContHash[0] = origHash[0] = origContHash[0] = 0;
    title[0] = origTitle[0] = docPath[0] = sourcePath[0] = 0;
    modified = false;
    docIsNew = false;
    FlDocSet(NULL);
    FitSnapDrop();
    docStored = false;
    SnapGlyphHashes();
    selN = 0;
    SelClear();
}

/* ---- views ---- */
static void ShowTypefaces(void)
{
    if (!browser) { Th_Toast(2, "No database is loaded"); return; }
    CommitEdits();
    Browser_SetTab(browser, 0);      /* also leaves the Typeface view */
    inBrowser = true;
    inFinder = false;
}

static void ShowFonts(void)
{
    if (!browser) { Th_Toast(2, "No database is loaded"); return; }
    CommitEdits();
    Browser_SetTab(browser, 1);
    inBrowser = true;
    inFinder = false;
}

/* the Typeface view of a typeface of the library (the Typefaces list when the library has none such) */
static void ShowTypeface(const char *pxtf, const char *selPxfm)
{
    if (!browser) { Th_Toast(2, "No database is loaded"); return; }
    CommitEdits();
    if (!Browser_OpenTypeface(browser, pxtf, selPxfm)) Browser_SetTab(browser, 0);
    inBrowser = true;
    inFinder = false;
}

static void ShowGlyphs(void)
{
    if (!finder) { Th_Toast(2, "No database is loaded"); return; }
    if (!inFinder) wasLibrary = inBrowser || !hasFont;
    CommitEdits();
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

/* Open a font of the open library (locked or not, as the settings say).  pxtfHash / variant: its typeface, if known (else the first typeface
 * variant that names the font).  selCode >= 0 selects that glyph and scrolls it into view. */
static bool OpenStoreFont(const char *pxfm, const char *pxtfHash, int variant, PxCode selCode)
{
    char err[PX_ERR_LEN] = "";
    if (!store) { ShowError("Cannot open font", "No database is loaded."); return false; }
    CommitEdits();      /* the font being left (the browser then lists what it committed) */
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
    NameOf(haveC ? pxtf_find_variant(&nc, var) : NULL, name, sizeof name);
    snprintf(dp, sizeof dp, "%s.pxfm", nf.fileHash);
    AdoptDocument(&nf, haveC ? &nc : NULL, haveC ? var : 0, true, name, name, nf.fileHash, haveC ? tfh : "", dp, dp);
    if (selCode >= 0) {
        int gi = px_font_find_code(&font, selCode);
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

/* Commits the edits of the open font (and typeface) into the store, in place.  The font keeps its place everywhere: a new hash replaces
 * the old one in every typeface that names it (those are rewritten under their own new hashes), in the settings sidecar (layers,
 * corrections, locks) and in the open views; the old font goes, and the glyphs no font names any more.  A font that is not in the store
 * yet (New Font, Import Font File) is added.  False when it failed (an error is shown). */
static bool CommitEdits(void)
{
    if (inEditor && ed) Editor_Flush(ed);   /* pixels still floating in the glyph view are part of the edit */
    if (!hasFont || !store || !EditPending()) return true;
    char err[PX_ERR_LEN] = "";
    StoreEditResult er;
    memset(&er, 0, sizeof er);
    if (!docStored) {
        StoreSaveResult res;
        memset(&res, 0, sizeof res);
        if (!store_save_to_memory(store, &font, hasCont ? &cont : NULL, hasCont ? contVariant : 0, &res, err)) {
            ShowError("Cannot add the font", err[0] ? err : "Unknown error.");
            return false;
        }
        PxtfVariant *v = hasCont ? pxtf_find_variant(&cont, contVariant) : NULL;
        if (v) snprintf(v->pxfm, sizeof v->pxfm, "%s", res.pxfm);
        snprintf(er.pxfm, sizeof er.pxfm, "%s", res.pxfm);
        snprintf(er.pxtf, sizeof er.pxtf, "%s", res.pxtf);
        docStored = true;
        docIsNew = false;
        if (fsettings) {
            fs_set_unlocked(fsettings, 0, er.pxfm, true);
            if (hasCont) fs_set_unlocked(fsettings, 1, er.pxtf, true);
        }
    } else {
        bool fontCh = !readOnly && !Same(curHash, origHash);
        PxtfVariant *ov = hasCont ? pxtf_find_variant(&cont, contVariant) : NULL;
        if (fontCh && ov) { snprintf(ov->pxfm, sizeof ov->pxfm, "%s", curHash); needHash = true; EnsureHashes(); }   /* only the open variant follows: a clone keeps the old font */
        bool tfCh = hasCont && (!contLocked || !readOnly) && !Same(curContHash, origContHash);
        if (!store_edit_in_place(store, origHash, fontCh ? &font : NULL, hasCont ? origContHash : NULL, tfCh ? &cont : NULL, &er, err)) {
            ShowError("Cannot change the font in the database", err[0] ? err : "Unknown error.");
            return false;
        }
        if (fsettings) {
            fs_rename_font(fsettings, origHash, er.pxfm);
            for (int i = 0; i < er.nMoved; i++) fs_rename_typeface(fsettings, er.movedFrom[i], er.movedTo[i]);
        }
        if (browser) for (int i = 0; i < er.nMoved; i++) Browser_Follow(browser, er.movedFrom[i], er.movedTo[i]);
        snprintf(flSyncedHash, sizeof flSyncedHash, "%s", er.pxfm);
    }
    snprintf(origHash, sizeof origHash, "%s", er.pxfm);
    needHash = true;
    EnsureHashes();
    snprintf(origContHash, sizeof origContHash, "%s", hasCont ? (er.pxtf[0] ? er.pxtf : curContHash) : "");
    needHash = true;
    EnsureHashes();
    snprintf(lastPxfm, sizeof lastPxfm, "%s", er.pxfm);
    snprintf(lastPxtf, sizeof lastPxtf, "%s", er.pxtf);
    lastPxfmPath[0] = lastPxtfPath[0] = 0;
    FlSync();
    if (browser) Browser_Rescan(browser);
    return true;
}

/* The lock of the open font, or of its typeface (its fields): unlocking lets every edit change it in place; locking commits what is there first. */
static void SetLocked(bool typeface, bool lock)
{
    bool *flag = typeface ? &contLocked : &readOnly;
    if (!hasFont || !store || (typeface && !hasCont) || lock == *flag) return;
    if (lock && (!CommitEdits() || !docStored)) return;
    if (fsettings) fs_set_unlocked(fsettings, typeface ? 1 : 0, typeface ? origContHash : origHash, !lock);
    *flag = lock;
    if (!typeface) { if (ed) Editor_SetReadOnly(ed, lock); FitSnapDrop(); needHash = true; }
    Th_Toast(1, "The %s is %s", typeface ? "typeface" : "font", lock ? "locked" : "unlocked: edits change it in place");
}
#define SetFontLocked(lock) SetLocked(false, lock)
#define SetTypefaceLocked(lock) SetLocked(true, lock)

/* the lock of a font or typeface of the library (the browser's lock buttons) */
static bool LockedOf(bool typeface, const char *hash)
{
    if (!hash || !hash[0]) return true;
    if (typeface ? (hasCont && docStored && Same(hash, origContHash)) : (hasFont && docStored && Same(hash, origHash))) return typeface ? contLocked : readOnly;
    return !(fsettings && fs_is_unlocked(fsettings, typeface ? 1 : 0, hash));
}

/* The padlock of a Typeface or Font tab.  The open font (or its typeface) locks and unlocks in place.  Another one is unlocked by opening its
 * font quietly (its fields are edited in the open document), staying in the view the user is in. */
static bool OpenStoreFont(const char *pxfm, const char *pxtfHash, int variant, PxCode selCode);
static void ToggleLockOf(bool typeface, const char *pxtf, const char *pxfm, int variant)
{
    const char *hash = typeface ? pxtf : pxfm;
    bool locked = LockedOf(typeface, hash);
    bool doc = typeface ? hasCont && docStored && Same(hash, origContHash) : hasFont && docStored && Same(hash, origHash);
    if (doc) { SetLocked(typeface, !locked); return; }
    if (!locked) { if (fsettings) fs_set_unlocked(fsettings, typeface ? 1 : 0, hash, false); return; }
    bool wasB = inBrowser, wasF = inFinder;
    char tf[33], fm[33];
    snprintf(tf, sizeof tf, "%s", pxtf);
    snprintf(fm, sizeof fm, "%s", pxfm);
    if (!OpenStoreFont(fm, tf, variant, -1)) return;
    SetLocked(typeface, false);
    inBrowser = wasB;
    inFinder = wasF;
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
static struct { bool pending, unlock, browser; int zoom, edit, panelScroll; const char *sel; char font[40], typeface[40]; } startup;
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
    if (fsettings) { fs_close(fsettings); fsettings = NULL; }
    inBrowser = false;
    inFinder = false;
}

static bool InstallStore(Store *s)
{
    char err[PX_ERR_LEN] = "";
    DropDocument();
    CloseLibrary();
    store = s;
    FsOpenFor(s);
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
    if (libRoot[0]) IniSetLastDb(libRoot);
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
    if (job.active) { Th_Toast(2, "Another file operation is running"); return false; }
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
    if (!store) { Th_Toast(2, "No database is loaded"); return; }
    if (!CommitEdits()) return;
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
    if (job.kind == JOB_IMPORT) Th_Toast(1, "Imported %d fonts, %d glyphs into %s (%.1f s)", si->nPxfm, si->nGlyph, DbName(), job.seconds);
    else Th_Toast(0, "Loaded %s: %d fonts, %d glyphs (%.1f s)", DbName(), si->nPxfm, si->nGlyph, job.seconds);
    if (getenv("PXFE_TRACE")) { printf("TRACE %s fonts=%d glyphs=%d seconds=%.2f\n", job.kind == JOB_IMPORT ? "import" : "load", si->nPxfm, si->nGlyph, job.seconds); fflush(stdout); }
    if (job.after == AFTER_LIBRARY) { ShowTypefaces(); }   /* the program starts in the Typefaces view, whatever the browser showed last */
    else if (job.after == AFTER_KEEP) { if (!hasFont) inBrowser = true; }
    else if (job.after == AFTER_FONT || job.after == AFTER_TYPEFACE) {
        bool ok = job.after == AFTER_FONT ? OpenStoreFont(job.afterHash, "", 0, -1) : OpenStoreTypeface(job.afterHash, 0);
        if (ok && startup.pending && startup.unlock) SetFontLocked(false);
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
    Th_Raised(c, 1);
    float x = c.x + 32, w = c.width - 64, y = c.y + 28;
    const char *head = job.kind == JOB_LOAD ? "Loading database"
                     : job.kind == JOB_IMPORT ? "Importing folder"
                     : job.stage == 0 ? "Making a backup" : "Exporting to folder";
    Th_TextHead(head, x, y, TH_TEXT);
    y += 34;
    char fit[PATH_MAX_LEN + 8];
    Th_FitText(job.path, TH_F_UNI, w, fit, sizeof fit);
    Th_TextUni(fit, x, y, TH_TEXT_DIM);
    y += 34;
    long ph = job.prog.phase, done = job.prog.done, total = job.prog.total;
    Th_Text(PhaseLabel(ph), x, y, TH_TEXT);
    char cnt[96] = "";
    if (ph != 7 && ph != 8 && total > 0) snprintf(cnt, sizeof cnt, "%ld of %ld", done, total);
    if (cnt[0]) { Vector2 m = Th_Measure(cnt); Th_Text(cnt, Fl(x + w - m.x), y, TH_TEXT_DIM); }
    y += 28;
    Rectangle bar = {x, y, w, 10};
    DrawRectangleRounded(bar, 1.0f, 8, TH_TRACK);
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
    Th_FitText(note, TH_F_SMALL, w, noteFit, sizeof noteFit);
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
        Th_Toast(2, "No database is loaded");
        return;
    }
    ShowTypefaces();
}

/* ---- Save and Save As ---- */
static bool SaveDb(void)
{
    if (!store) { Th_Toast(2, "No database is loaded"); return false; }
    if (!CommitEdits()) return false;
    FlSync();
    bool dbMod = store_modified(store);
    if (!store_info(store)->path[0]) return SaveAsDialog();   /* a new database: its first Save chooses the file */
    if (!dbMod && !FsDirty()) { Th_Toast(0, "No changes to save"); return true; }
    char err[PX_ERR_LEN] = "";
    if (dbMod && !store_save(store, err)) { ShowError("Cannot save", err[0] ? err : "Unknown error."); return false; }
    if (!SaveSidecar(false)) return false;   /* only the settings changed: the .pxfdb is not rewritten */
    Th_Toast(1, "Saved %s", dbMod ? DbName() : "the layer settings");
    return true;
}

static char saveAsPending[PATH_MAX_LEN];

static bool DoSaveAs(const char *path)
{
    if (!store) return false;
    if (!CommitEdits()) return false;
    char err[PX_ERR_LEN] = "";
    double t0 = GetTime();
    if (!store_save_as(store, path, err)) { ShowError("Cannot save", err[0] ? err : "Unknown error."); return false; }
    if (getenv("PXFE_TRACE")) { printf("TRACE saveas seconds=%.2f\n", GetTime() - t0); fflush(stdout); }
    snprintf(libRoot, sizeof libRoot, "%.1000s", store_info(store)->path);
    Slashes(libRoot);
    IniSetLastDb(libRoot);
    FlSync();
    SaveSidecar(true);     /* the settings go beside the new file; the old sidecar stays */
    Th_Toast(1, "Saved as %s", DbName());
    return true;
}

/* true when it saved now (false: cancelled, failed, or the Replace question is open) */
static bool SaveAsDialog(void)
{
    if (!store) { Th_Toast(2, "No database is loaded"); return false; }
    char path[PATH_MAX_LEN];
    if (!DlgSave(path, sizeof path, store_info(store)->path)) return false;
    Slashes(path);
    if (Same(path, store_info(store)->path)) return SaveDb();
    if (FileExistsAt(path)) {
        char msg[PATH_MAX_LEN + 200];
        snprintf(msg, sizeof msg, "%s already exists.\nSaving replaces it.", path);
        snprintf(saveAsPending, sizeof saveAsPending, "%s", path);
        ModalOpen(M_CONFIRM, A_SAVEAS_REPLACE, "Replace file?", msg, 2, "Cancel", "Replace", NULL, 1, 0, true);
        return false;
    }
    return DoSaveAs(path);
}

/* New Database: an empty library in memory, with no file until the first Save; the New Font dialog opens on it */
static void NewDatabase(void)
{
    Store *s = NULL;
    char err[PX_ERR_LEN] = "";
    if (!store_create(&s, err)) { ShowError("Cannot create a database", err[0] ? err : "Unknown error."); return; }
    if (!InstallStore(s)) return;
    ShowTypefaces();
    NewFontOpen();
}

/* ---- unsaved changes: Load, Import and quit ask first (Save / Discard / Cancel) ---- */
enum { G_NONE, G_LOAD_DIALOG, G_LOAD_PATH, G_IMPORT_DIALOG, G_NEW_DB, G_QUIT };
static struct { int kind; char path[PATH_MAX_LEN]; } guard;

static void RunGuarded(void)
{
    int k = guard.kind;
    guard.kind = G_NONE;
    if (k == G_LOAD_DIALOG) LoadDialog();
    else if (k == G_LOAD_PATH) LoadPath(guard.path, AFTER_LIBRARY, "");
    else if (k == G_IMPORT_DIALOG) ImportDialog();
    else if (k == G_NEW_DB) NewDatabase();
    else if (k == G_QUIT) quit = true;
}

static void Guard(int kind, const char *path)
{
    guard.kind = kind;
    snprintf(guard.path, sizeof guard.path, "%s", path ? path : "");
    if (store) {
        CommitEdits();
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
    case INSIGHT_OPEN_FONT: {
        bool toGlyph = inFinder;   /* from the Glyphs view the usage leads to the glyph itself: the font's Glyph view */
        char gh[33] = "";
        if (toGlyph && finder) Finder_SelectedGlyph(finder, gh);
        if (OpenStoreFont(t->pxfm, t->pxtf, t->variant, t->code) && toGlyph) {
            int gi = t->code >= 0 ? px_font_find_code(&font, t->code) : -1;
            for (int i = 0; gi < 0 && gh[0] && i < font.count; i++) {
                char h[33];
                px_glyph_hash(&font.glyphs[i], h);
                if (Same(h, gh)) gi = i;
            }
            if (gi >= 0) OpenEditor(gi);
        }
        break; }
    case INSIGHT_OPEN_TYPEFACE: OpenStoreTypeface(t->pxtf, t->variant); break;
    case INSIGHT_FIND_CODE:
        if (finder && px_code_codepoint(t->code) >= 0) { Finder_SetCode(finder, px_code_codepoint(t->code)); ShowGlyphs(); }
        break;
    default: break;
    }
}

static void RunChoice(const BrowserChoice *c)
{
    if (c->action == BROWSER_INSIGHT) HandleInsightTarget(&c->insight);
    else if (c->action == BROWSER_OPEN) OpenStoreFont(c->pxfm, c->pxtf, c->variant, -1);
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
    Th_Toast(1, "Deleted: %s", del.plan.summary);
    if (browser) Browser_Rescan(browser);
    if (del.fromView) ShowTypefaces();
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
    Th_Toast(0, n ? "Added %d matching glyph%s" : "No other glyph has the same shape", n, n == 1 ? "" : "s");
}

static void CmdDelete(void)
{
    if (readOnly) return;
    PushUndo(0);
    for (int i = font.count - 1; i >= 0; i--)
        if (i < selN && sel[i]) px_font_remove_glyph(&font, i);   /* also drops the glyph's kerning pairs and a default it was */
    selN = 0;
    SelClear();
    Th_Toast(0, "Deleted");
}

/* The code of a code point, or of a variant of one, as the user types it: one character that is no hex digit, or px_code_parse_user's
 * U+0041, 0x41, 41 (hex), each with -V for a variant (U+0041-1).  Symbols and the default glyph are not taken. */
static bool ParseCodepoint(const char *in, PxCode *cp)
{
    char s[80];
    snprintf(s, sizeof s, "%s", in);
    char *p = s;
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n > 0 && p[n - 1] == ' ') p[--n] = 0;
    if (n == 0) return false;
    int sz = 0;
    int c = GetCodepointNext(p, &sz);
    if ((size_t)sz == n && !(c == 0x3F && p[0] != '?')) { *cp = px_code_cp(c); return c >= 0 && c <= 0x10FFFF; }   /* one character is that character */
    return px_code_parse_user(p, cp) && px_code_is_codepoint(*cp);
}

/* Clone the selected glyph to the code cp (a code point or a variant of one).  Kerning is not copied; replacing an existing glyph
 * keeps its place (and its kerning pairs and default status). */
static void DoClone(PxCode cp)
{
    if (readOnly) return;
    int src = FirstSelected();
    if (src < 0) return;
    PushUndo(0);
    PxGlyph g;
    memset(&g, 0, sizeof g);
    px_glyph_copy(&g, &font.glyphs[src]);
    g.code = cp;
    int ex = px_font_find_code(&font, cp);
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
    char cl[40];
    px_code_label(cp, cl, sizeof cl);
    Th_Toast(1, "Cloned to %s", cl);
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
    Th_Toast(1, "Cloned to #%d (no code point)", px_code_symbol_index(font.glyphs[ni].code));
}

static void CmdClone(void)
{
    if (readOnly) { Th_Toast(2, "The font is locked: unlock it first"); return; }
    RecountSel();
    if (selCount != 1) { Th_Toast(2, "Select exactly one glyph to clone"); return; }
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
    if (readOnly) { Th_Toast(2, "The font is locked: unlock it first"); return; }
    RecountSel();
    if (selCount == 0) return;
    char msg[160];
    snprintf(msg, sizeof msg, "Delete %d selected glyph%s from the font?\nYou can undo this with Ctrl+Z.", selCount, selCount == 1 ? "" : "s");
    ModalOpen(M_CONFIRM, A_DELETE, selCount == 1 ? "Delete glyph" : "Delete glyphs", msg, 2, "Cancel", "Delete", NULL, 1, 0, true);
}

static void CodeLabel(PxCode c, char *out, size_t n);

static bool CanSetDefault(void)
{
    if (!hasFont || readOnly || selCount != 1) return false;
    int i = FirstSelected();
    return i >= 0 && font.glyphs[i].code != PX_CODE_DEFAULT;
}

static void CmdSetDefault(void)
{
    if (readOnly) { Th_Toast(2, "The font is locked: unlock it first"); return; }
    RecountSel();
    int i = FirstSelected();
    if (selCount != 1 || i < 0) { Th_Toast(2, "Select one glyph"); return; }
    if (font.glyphs[i].code == PX_CODE_DEFAULT) return;
    PushUndo(0);
    char cl[32];
    CodeLabel(font.glyphs[i].code, cl, sizeof cl);
    px_font_set_default_from(&font, i);
    needHash = true;
    SelResize();
    Th_Toast(1, "The default glyph is now a copy of %s", cl);
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

/* "U+0041" for a code point, "#n" for a glyph without one, "Default", "-VVV" after a variant (px_code_label); "-" for none */
static void CodeLabel(PxCode c, char *out, size_t n)
{
    px_code_label(c, out, (int)n);
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
_Static_assert(sizeof(Color) == 4, "PxPalette.draw is read as raylib Colors");
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

static void DrawCell(int i, float cx, float cy, float cw, Rectangle area, bool selected, bool hovered)
{
    const PxGlyph *g = &font.glyphs[i];
    float boxPx = (float)(BoxRows() * zoom);
    float ch = rowH;
    Rectangle cell = {cx, cy, cw, ch};
    Th_Card(cell, (selected ? TH_LR_SELECTED : 0) | (hovered ? TH_LR_HOVER : 0));
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
        DrawGlyphPixels(g, ox, by, zoom, (const Color *)px_glyph_palette(&font, g)->draw);   /* the glyph's own palette, ready to draw */
        EndScissorMode();
    }

    if (showCaptions) {
        Rectangle cap = {cx + 1, cy + boxPx + 2 * CELL_PADY, cw - 2, CAPTION_H - 1};
        DrawRectangleRec(cap, TH_PANEL_ALT);
        Th_HLine(cap.x, cap.x + cap.width, cap.y, TH_LINE_SOFT);
        Th_CodeDraw(g->code, cap, TH_TEXT, hovered, 1);   /* the character in Unifont, "U+XXXX" while the mouse is over the cell; the label of the others */
    }
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

    bool inArea = Hov(area);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    float wheel = inArea && ctrl ? GetMouseWheelMove() : 0;
    if (wheel != 0) ApplyZoom(zoom + (wheel > 0 ? 1 : -1));
    if (revealIdx >= 0 && revealIdx < font.count) {
        float top = ly[revealIdx], bot = ly[revealIdx] + rowH;
        if (top - scrollY < 0) scrollY = top - GRID_MARGIN;
        else if (bot - scrollY > area.height) scrollY = bot - area.height + GRID_MARGIN;
    }
    revealIdx = -1;
    bool overBar = Th_ScrollBar(area, &scrollY, contentH, ctrl ? 0 : 72, true);

    hoverIdx = -1;
    Vector2 m = Mouse();
    for (int i = 0; i < font.count; i++) {
        float cy = area.y + ly[i] - scrollY;
        if (cy > area.y + area.height) break;
        if (cy + rowH < area.y) continue;
        float cx = area.x + lx[i];
        Rectangle cr = {cx, cy, lw[i], rowH};
        bool hv = inArea && !overBar && CheckCollisionPointRec(m, cr) && m.y >= area.y;
        if (hv) hoverIdx = i;
        DrawCell(i, cx, cy, lw[i], area, i < selN && sel[i], hv);
    }

    if (inArea && !overBar && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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
    RecountSel();
}

/* ------------------------------------------------------------------------------------------------
 * Toolbar and info bar
 * ---------------------------------------------------------------------------------------------- */
static float TbX;
static float TbY = 10;                 /* top of the buttons of the row being drawn */

static bool TbButton(const char *label, int flags, const char *hint)   /* a button at the toolbar's cursor */
{
    float w = Th_ButtonW(label);
    bool r = Th_Button((Rectangle){TbX, TbY, w, TH_ROW_H}, label, flags, hint);
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
    if (!finder) { Th_Toast(2, "No library is open"); return; }
    if (selCount != 1 || i < 0 || !px_code_is_codepoint(font.glyphs[i].code)) { Th_Toast(2, "Select one glyph that has a code point"); return; }
    Finder_SetCode(finder, px_code_codepoint(font.glyphs[i].code));
    ShowGlyphs();
}

/* The navigation: Typefaces | Fonts | Glyphs, which becomes <typeface> | <style> | <glyph> inside a typeface, a font and a glyph. */
enum { NV_TYPEFACES, NV_TYPEFACE, NV_FONTS, NV_FONT, NV_GLYPH, NV_GLYPHS };
static int navView = NV_TYPEFACES, navActive = 0;
static char navText[3][320];

static int NavView(void)
{
    if (inFinder) return NV_GLYPHS;
    if (inBrowser && browser) return Browser_InTypeface(browser) ? NV_TYPEFACE : (Browser_Tab(browser) == 1 ? NV_FONTS : NV_TYPEFACES);
    if (inBrowser || !hasFont) return NV_TYPEFACES;
    return inEditor ? NV_GLYPH : NV_FONT;
}

/* the names of the open font in the bar: its typeface (family) or "Orphaned", and its style */
static void FontNavNames(char fam[300], char sty[300])
{
    const PxtfVariant *v = hasCont ? pxtf_find_variant(&cont, contVariant) : NULL;
    if (!hasCont) { snprintf(fam, 300, "Orphaned"); snprintf(sty, 300, "%s", origTitle[0] ? origTitle : "Regular"); }
    else {
        const char *f = v && v->text[PXTF_FAMILY] && v->text[PXTF_FAMILY][0] ? v->text[PXTF_FAMILY] : NULL;
        const char *full = v && v->text[PXTF_FULLNAME] && v->text[PXTF_FULLNAME][0] ? v->text[PXTF_FULLNAME] : NULL;
        const char *s = v && v->text[PXTF_STYLE] && v->text[PXTF_STYLE][0] ? v->text[PXTF_STYLE] : NULL;
        if (f) snprintf(fam, 300, "%s", f);
        else if (full) snprintf(fam, 300, "%s", full);
        else snprintf(fam, 300, "Typeface");
        snprintf(sty, 300, "%s", s ? s : "Regular");
    }
}

static void GoTypefaceOfFont(void)
{
    if (hasCont && origContHash[0] && browser) ShowTypeface(origContHash, origHash);
    else ShowTypefaces();   /* a font without a typeface has no Typeface view */
}

static void CloseGlyphView(void)
{
    if (!inEditor || !ed) return;
    Editor_Flush(ed);
    int gi = Editor_GlyphIndex(ed);
    inEditor = false;
    SelResize();
    SelOnly(gi);
    revealIdx = gi;
}

/* one level up: Glyph -> Font -> Typeface -> Typefaces */
static void NavBack(void)
{
    switch (NavView()) {
    case NV_GLYPH: CloseGlyphView(); break;
    case NV_FONT: GoTypefaceOfFont(); break;
    case NV_TYPEFACE: ShowTypefaces(); break;
    case NV_GLYPHS: if (hasFont && !wasLibrary) ShowFont(); else ShowTypefaces(); break;
    default: break;
    }
}

static void DrawNav(void)
{
    int view = NavView();
    char fam[300] = "", sty[300] = "", full[3][320];
    const char *names[3];
    int active;
    const char *hint[3];
    static char hbuf[3][400];
    switch (view) {
    case NV_TYPEFACE: {
        char nm[320];
        Browser_TypefaceName(browser, nm, sizeof nm);
        if (!nm[0]) snprintf(nm, sizeof nm, "Typeface");
        if (TypefaceEditing(Browser_TypefaceHash(browser))) { FontNavNames(fam, sty); snprintf(nm, sizeof nm, "%s%s", fam, modified ? " *" : ""); }   /* the copy being edited */
        snprintf(full[0], 320, "%s", nm); snprintf(full[1], 320, "Fonts"); snprintf(full[2], 320, "Glyphs");
        active = 0;
        break; }
    case NV_FONT: case NV_GLYPH:
        FontNavNames(fam, sty);
        snprintf(full[0], 320, "%s", fam);
        snprintf(full[1], 320, "%s%s", sty, modified ? " *" : "");
        if (view == NV_GLYPH && ed && Editor_GlyphIndex(ed) >= 0 && Editor_GlyphIndex(ed) < font.count) CodeLabel(font.glyphs[Editor_GlyphIndex(ed)].code, full[2], 320);
        else snprintf(full[2], 320, "Glyphs");
        active = view == NV_GLYPH ? 2 : 1;
        break;
    default:
        snprintf(full[0], 320, "Typefaces"); snprintf(full[1], 320, "Fonts"); snprintf(full[2], 320, "Glyphs");
        active = view == NV_FONTS ? 1 : (view == NV_GLYPHS ? 2 : 0);
        break;
    }
    navView = view; navActive = active;
    char fit[3][300];
    float wi[3], total = 0;
    for (int i = 0; i < 3; i++) {
        Th_FitText(full[i], TH_F_BODY | TH_F_RICH, 190, fit[i], sizeof fit[i]);
        names[i] = fit[i];
        snprintf(navText[i], sizeof navText[i], "%.300s", full[i]);
        wi[i] = Fl(Th_MeasureRich(names[i], TH_F_BODY)) + 32;
        total += wi[i];
    }
    bool inFont = view == NV_FONT || view == NV_GLYPH;
    snprintf(hbuf[0], sizeof hbuf[0], "%s", view == NV_TYPEFACES ? "All typefaces: the library as boxes, one per typeface" :
             view == NV_TYPEFACE ? "Back to all typefaces  (Alt+Left)" :
             inFont ? (hasCont ? TextFormat("The typeface %s: its fonts  (Alt+Left from the font)", full[0]) : "This font belongs to no typeface: the list of all typefaces") :
             "All typefaces: the library as boxes, one per typeface");
    snprintf(hbuf[1], sizeof hbuf[1], "%s", inFont ? (view == NV_GLYPH ? TextFormat("Back to the font %s  (Alt+Left)", full[1]) : TextFormat("The open font %s", full[1])) :
             "Fonts: every font of the library, flat");
    snprintf(hbuf[2], sizeof hbuf[2], "%s", view == NV_GLYPH ? TextFormat("The open glyph %s", full[2]) : "Glyphs: every bitmap drawn for a code point  (Ctrl+G)");
    for (int i = 0; i < 3; i++) hint[i] = hbuf[i];
    Rectangle track = {TbX, TbY, total + 4, TH_ROW_H};
    int c = Th_Segmented(track, &(ThSeg){ names, 3, active, (store != NULL || inFont) ? 0u : 7u, "nav", wi, hint, true });
    for (int i = 0; i < 3; i++) {   /* the scripts also find a segment by its full name */
        Rectangle sr;
        char rn[16];
        snprintf(rn, sizeof rn, "nav.%d", i);
        if (!Th_RegFind(rn, &sr)) continue;
        if (!inFont || (i == 2 && view == NV_FONT)) Th_RegLabel(full[i], sr);
        if (view == NV_TYPEFACE && i == 0) Th_Reg("Typefaces", sr);
    }
    if (c == 0) { if (inFont) GoTypefaceOfFont(); else ShowTypefaces(); }   /* also from the Typeface view (the active segment): back to the list */
    else if (c == 1) { if (view == NV_GLYPH) CloseGlyphView(); else if (active != 1) ShowFonts(); }
    else if (c == 2 && active != 2 && view != NV_GLYPH) ShowGlyphs();
    TbX = track.x + track.width + 8;
}

static void DrawToolbar(int W)
{
    DrawRectangle(0, 0, W, TOOLBAR_H, TH_PANEL);
    Th_HLine(0, (float)W, TOOLBAR_H - 1, TH_LINE);
    TbX = 12;
    TbY = 10;
    bool fontView = hasFont && !inBrowser && !inFinder;
    {   /* the File menu button */
        Rectangle fb = {TbX, TbY, Th_ButtonW("Menu") + 10, TH_ROW_H};
        if (Th_Button(fb, "Menu", 0, "Load, Save, Import, Export")) { fileMenu.open = !fileMenu.open; fileMenu.anchor = fb; }
        TbX += fb.width + 6;
    }
    TbSep();
    DrawNav();
    TbSep();
    /* Save on the right: it shows the modified state */
    float limit = W - 12;
    if (store) {
        bool dirty = DbDirty();
        float sw = Th_ButtonW("Save") + 8;
        Rectangle sr = {(float)W - 12 - sw, TbY, sw, TH_ROW_H};
        if (Th_Button(sr, "Save", dirty ? TH_BTN_PRIMARY : 0, dirty ? "Save the changes to the database file  (Ctrl+S)" : "Nothing to save: the file is as it was loaded or last saved  (Ctrl+S)")) SaveDb();
        limit = sr.x - 12;
    }
    if (!fontView) return;
    if (TbX + 18 + Th_ButtonW("Undo") + Th_ButtonW("Redo") <= limit) {
        TbSep();
        if (TbButton("Undo", TH_BTN_OFF(!readOnly && undoN > 0), "Undo  (Ctrl+Z)")) DoUndoRedo(false);
        if (TbButton("Redo", TH_BTN_OFF(!readOnly && redoN > 0), "Redo  (Ctrl+Y)")) DoUndoRedo(true);
    }
}

static void DrawFileMenu(void)
{
    if (!fileMenu.open) return;
    const char *nodb = "No database is loaded";
    int off = store ? 0 : TH_MI_OFF;
    ThMenuItem it[9] = {
        {"New Database", NULL, "file.newdb"}, {"New Font\xE2\x80\xA6", NULL, "file.new", nodb, off}, {"Import Font File\xE2\x80\xA6", NULL, "file.importfont", nodb, off | TH_MI_SEP},
        {"Load\xE2\x80\xA6", "Ctrl+O", "file.load"}, {"Save", "Ctrl+S", "file.save", nodb, off}, {"Save As\xE2\x80\xA6", "Ctrl+Shift+S", "file.saveas", nodb, off | TH_MI_SEP},
        {"Import Folder\xE2\x80\xA6", NULL, "file.import"}, {"Export to Folder\xE2\x80\xA6", NULL, "file.export", nodb, off | TH_MI_SEP}, {"Quit", NULL, "file.quit"}};
    switch (Th_Menu(&(ThMenu){it, 9, .x = fileMenu.anchor.x, .y = fileMenu.anchor.y + fileMenu.anchor.height + 4, .w = 276, .keep = fileMenu.anchor}, &fileMenu.open)) {
    case 0: Guard(G_NEW_DB, ""); break;
    case 1: NewFontOpen(); break;
    case 2: ImportFontOpen(); break;
    case 3: Guard(G_LOAD_DIALOG, ""); break;
    case 4: SaveDb(); break;
    case 5: SaveAsDialog(); break;
    case 6: Guard(G_IMPORT_DIALOG, ""); break;
    case 7: ExportStart(); break;
    case 8: RequestQuit(); break;
    default: break;
    }
}

/* The hint and the toast of a copyable hash (theme.c). */
static void HashNotice(const char *what, bool copied)
{
    if (copied) Th_Toast(0, "%s hash copied", what);
    else hintText = "Click to copy the full hash";
}

static int FontPx(void)   /* the pixel size of the open font: its box */
{
    int a, b;
    px_font_box(&font, &a, &b);
    return a + b > 0 ? a + b : 16;
}

#include "fitdoc.inc"

static void DrawGlyphPanel(Rectangle p);
#include "panel.inc"
#include "gpanel.inc"

/* ------------------------------------------------------------------------------------------------
 * Status bar, toasts, empty state
 * ---------------------------------------------------------------------------------------------- */
static char statusDoc[PATH_MAX_LEN + 96], statusDb[PATH_MAX_LEN + 128];   /* what the bar says (play.inc asserts on it) */

static void DrawStatus(int W, int H)
{
    ThPart l[8] = {{0}}, rt[1] = {{0}};
    int nl = 0;
    char word[16], t1[PATH_MAX_LEN + 64], t2[96], nm[PATH_MAX_LEN + 16], r[200] = "", cl[24];
    statusDoc[0] = 0;
    statusDb[0] = 0;
    if (store) {   /* the file and whether memory differs from it */
        bool dirty = DbDirty();
        snprintf(word, sizeof word, "%s", dirty ? "modified" : "saved");
        snprintf(statusDb, sizeof statusDb, "%s  \xC2\xB7  %s", store_info(store)->path, word);
        l[nl++] = (ThPart){store_info(store)->path, TH_TEXT, 0.4f, TH_SP_TAIL};
        l[nl++] = (ThPart){word, dirty ? TH_ACCENT : TH_TEXT_DIM};
    }
    if (inFinder && finder) {
        int32_t c = Finder_Code(finder);
        int rc = Finder_ResultCount(finder);
        snprintf(cl, sizeof cl, c >= 0 ? "U+%04X" : "none", (unsigned)c);
        snprintf(t1, sizeof t1, rc < 0 ? "Glyph finder   code point %s   searching..." : "Glyph finder   code point %s   %d bitmap%s", cl, rc, rc == 1 ? "" : "s");
        l[nl++] = (ThPart){t1};
    } else if (inBrowser && browser) {
        snprintf(t1, sizeof t1, "Library %s", Browser_Root(browser));
        snprintf(statusDoc, sizeof statusDoc, "%s", t1);
        int vc = Browser_VisibleCount(browser);
        if (Browser_InTypeface(browser)) snprintf(t2, sizeof t2, "%d font%s in this typeface", vc, vc == 1 ? "" : "s");
        else snprintf(t2, sizeof t2, "%d of %d shown", vc, Browser_TotalCount(browser));
        l[nl++] = (ThPart){t1, {0}, 0.55f, TH_SP_TAIL};
        l[nl++] = (ThPart){t2};
    } else if (!hasFont) {
        l[nl++] = (ThPart){"No file open"};
    } else {
        EnsureHashes();
        snprintf(nm, sizeof nm, "%s", docPath[0] ? BaseName(docPath) : "untitled");
        snprintf(statusDoc, sizeof statusDoc, "%s", nm);
        snprintf(t2, sizeof t2, "%d glyphs", font.count);
        l[nl++] = (ThPart){nm, TH_TEXT, 0.5f, TH_SP_TAIL};
        l[nl++] = (ThPart){t2};
        l[nl++] = (ThPart){NULL, {0}, 0, 0, "Font", origHash, "hash"};
        if (!Same(curHash, origHash)) l[nl++] = (ThPart){NULL, {0}, 0, TH_HASH_ACCENT, "Current", curHash};
    }
    /* right side: the hint of the hovered control, or the glyph under the mouse */
    if (!hintText) hintText = Th_Tip(NULL);
    if (hintText) snprintf(r, sizeof r, "%s", hintText);
    else if (hasFont && !inBrowser && !inFinder && !inEditor && hoverIdx >= 0 && hoverIdx < font.count && !uiBlocked) {
        const PxGlyph *g = &font.glyphs[hoverIdx];
        CodeLabel(g->code, cl, sizeof cl);
        snprintf(r, sizeof r, "%s%s   advance %d   %s", cl, px_code_is_codepoint(g->code) || px_code_is_default(g->code) ? "" : " (no code point)", g->advance, g->w > 0 ? TextFormat("%d x %d", g->w, g->h) : "empty");
    }
    rt[0].text = r;
    Th_StatusBar((Rectangle){0, (float)(H - STATUS_H), (float)W, STATUS_H}, 0, l, nl, rt, r[0] ? 1 : 0);
}

static void DrawEmptyState(Rectangle area)
{
    DrawRectangleRec(area, TH_BG);
    Rectangle c = {Fl(area.x + area.width / 2 - 280), Fl(area.y + area.height / 2 - 130), 560, 260};
    RRect(c, TH_PANEL, TH_LINE);
    const char *head = "No database loaded";
    Th_TextHead(head, Fl(c.x + (c.width - Th_MeasureHead(head).x) / 2), c.y + 32, TH_TEXT);
    TextCentered("Start a new database with an empty page, load a .pxfdb database,", (Rectangle){c.x, c.y + 72, c.width, TH_LINE_H}, TH_TEXT_DIM);
    TextCentered("or import a folder of pxtf, pxfm and pxgl files into a new one.", (Rectangle){c.x, c.y + 72 + TH_LINE_H, c.width, TH_LINE_H}, TH_TEXT_DIM);
    float bw = 166, bx = c.x + (c.width - 3 * bw - 2 * TH_GAP) / 2;
    if (Th_Button((Rectangle){bx, c.y + 144, bw, TH_ROW_H + 4}, "New Database", 0, "An empty database with a new font: no file until the first Save")) Guard(G_NEW_DB, "");
    if (Th_Button((Rectangle){bx + bw + TH_GAP, c.y + 144, bw, TH_ROW_H + 4}, "Load Database…", TH_BTN_PRIMARY, "Load a .pxfdb database file  (Ctrl+O)")) Guard(G_LOAD_DIALOG, "");
    if (Th_Button((Rectangle){bx + 2 * (bw + TH_GAP), c.y + 144, bw, TH_ROW_H + 4}, "Import Folder…", 0, "Import a folder of pxtf, pxfm and pxgl files into a new .pxfdb")) Guard(G_IMPORT_DIALOG, "");
    TextCentered("Ctrl+O", (Rectangle){c.x, c.y + 200, c.width, TH_LINE_H}, TH_TEXT_DIM);
}

/* The Export dialog: what the folder holds, Append or Overwrite, and the backup of the folder's current data */
/* ---- Add Glyph and New Font ---- */

/* an empty glyph (no ink) with the code cp, or without a code point when cp < 0; it is drawn in the glyph view */
static void DoAddGlyph(PxCode cp)
{
    if (readOnly) return;
    PushUndo(0);
    int a, b;
    px_font_box(&font, &a, &b);
    PxGlyph g;
    memset(&g, 0, sizeof g);
    g.advance = (a + b) / 2 > 0 ? (a + b) / 2 : 1;
    int ni;
    if (cp < 0) ni = px_font_add_uncoded(&font, &g);
    else { g.code = cp; ni = px_font_add_glyph(&font, &g); }
    needHash = true;
    selN = 0;
    SelOnly(ni);
    revealIdx = ni;
    char cl[40];
    CodeLabel(font.glyphs[ni].code, cl, sizeof cl);
    Th_Toast(1, cp < 0 ? "Added %s (no code point)" : "Added %s", cl);
}

static void CmdAddGlyph(void)
{
    if (!hasFont) return;
    if (readOnly) { Th_Toast(2, "The font is locked: unlock it first"); return; }
    ModalOpen(M_INPUT, A_ADDGLYPH, "Add glyph", "Add an empty glyph.\nType U+XXXX, hex digits, or a single character; or add it without a code point.", 3, "Cancel", "No Code Point", "Add", 2, 0, false);
}

/* New Font: name, style, pixel size, ascent and descent.  The font has no glyphs and no typeface (it is orphaned: the name is only the
 * title of this session); it is committed into the store like an unlocked font, and Add Glyph gives it glyphs. */
static struct { bool open; int frame; char name[80], style[40]; int size, ascent, descent; } nf;

static void NewFontOpen(void)
{
    if (!store) { Th_Toast(2, "No database is loaded"); return; }
    memset(&nf, 0, sizeof nf);
    nf.open = true;
    nf.frame = frameNo;
    snprintf(nf.name, sizeof nf.name, "New Font");
    snprintf(nf.style, sizeof nf.style, "Regular");
    nf.size = 16; nf.ascent = 12; nf.descent = 4;
    Th_TextBlur();
}

static void NewFontCreate(void)
{
    PxFont f;
    px_font_init(&f);
    f.has[PXM_ASCENT] = f.has[PXM_DESCENT] = true;
    f.m[PXM_ASCENT] = nf.ascent;
    f.m[PXM_DESCENT] = nf.descent;
    char t[200];
    snprintf(t, sizeof t, "%s%s%s", nf.name, nf.name[0] && nf.style[0] ? " " : "", nf.style);
    if (!t[0]) snprintf(t, sizeof t, "New Font");
    nf.open = false;
    AdoptDocument(&f, NULL, 0, false, t, t, NULL, NULL, "new font", "");
    docIsNew = true;
    needHash = true;
    EnsureHashes();
    Th_Toast(1, "New font: add glyphs with Add Glyph");
}

static void DrawNewFontDialog(int W, int H)
{
    if (!nf.open) return;
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    uiBlocked = false;
    Th_SetInputBlocked(false);
    bool fresh = nf.frame == frameNo;
    const float pw = 460, pad = 24;
    float ph = pad + TH_SIZE_HEAD + TH_PAD + 5 * (TH_ROW_H + TH_GAP / 2) + 2 * TH_LINE_H + TH_PAD + TH_ROW_H + pad;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    Th_Raised(pr, 1);
    float x = pr.x + pad, w = pr.width - 2 * pad, y = pr.y + pad;
    Th_TextHead("New Font", x, y, TH_TEXT);
    y += TH_SIZE_HEAD + TH_PAD;
    Rectangle vr = Th_Row(x, &y, w, "Name", NULL, 0);
    if (Th_TextField((Rectangle){vr.x, vr.y, vr.width, TH_ROW_H}, "nf.name", nf.name, (int)sizeof nf.name, 0, "Family") & TH_TF_CHANGED) snprintf(nf.name, sizeof nf.name, "%s", Th_TextValue());
    vr = Th_Row(x, &y, w, "Style", NULL, 0);
    if (Th_TextField((Rectangle){vr.x, vr.y, vr.width, TH_ROW_H}, "nf.style", nf.style, (int)sizeof nf.style, 0, "Regular") & TH_TF_CHANGED) snprintf(nf.style, sizeof nf.style, "%s", Th_TextValue());
    int nv;
    vr = Th_Row(x, &y, w, "Pixel size", NULL, 0);
    if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "nf.size", nf.size, 1, 256, 0, &nv)) { nf.size = nv; nf.ascent = (nv * 3 + 2) / 4; nf.descent = nv - nf.ascent; }
    vr = Th_Row(x, &y, w, "Ascent", NULL, 0);
    if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "nf.ascent", nf.ascent, 0, 256, 0, &nv)) { nf.ascent = nv; nf.size = nf.ascent + nf.descent; }
    vr = Th_Row(x, &y, w, "Descent", NULL, 0);
    if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "nf.descent", nf.descent, 0, 256, 0, &nv)) { nf.descent = nv; nf.size = nf.ascent + nf.descent; }
    Th_TextSmall("An empty font without a typeface; Add Glyph gives it glyphs.", x, y, TH_TEXT_DIM);
    Th_TextSmall("The size is ascent + descent. The name is not stored in the font.", x, y + TH_LINE_H, TH_TEXT_DIM);
    y = pr.y + pr.height - pad - TH_ROW_H;
    bool create = false, cancel = false;
    float bx = pr.x + pr.width - pad - 96;
    create = Th_Button((Rectangle){bx, y, 96, TH_ROW_H}, "Create", TH_BTN_PRIMARY | TH_BTN_OFF(nf.ascent + nf.descent > 0), NULL) && !fresh;
    cancel = Th_Button((Rectangle){bx - 8 - 96, y, 96, TH_ROW_H}, "Cancel", 0, NULL) && !fresh;
    if (!fresh && IsKeyPressed(KEY_ESCAPE)) cancel = true;
    if (!fresh && !Th_TextIs(NULL) && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && nf.ascent + nf.descent > 0) create = true;
    if (cancel) nf.open = false;
    else if (create) NewFontCreate();
}

static void DrawExportDialog(int W, int H)
{
    if (!xd.open) return;
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    uiBlocked = false;
    Th_SetInputBlocked(false);
    bool fresh = xd.frame == frameNo;
    const float pw = 600, pad = 24;
    char l1[4][160], l2[4][160];
    int n1 = WrapLines("Append adds what the folder does not have yet. Nothing that is there is changed or deleted.", pw - 2 * pad, l1, 4);
    int n2 = WrapLines("Overwrite deletes the contents of pxtf, pxfm and pxgl in the folder, then writes everything in memory there. Other files stay.", pw - 2 * pad, l2, 4);
    float ph = pad + TH_SIZE_HEAD + TH_PAD + 2 * TH_ROW_H + (n1 + n2) * TH_LINE_H + 4 + TH_PAD + 2 * TH_ROW_H + TH_PAD + TH_ROW_H + pad;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    Th_Raised(pr, 1);
    float x = pr.x + pad, w = pr.width - 2 * pad, y = pr.y + pad;
    Th_TextHead("Export to Folder", x, y, TH_TEXT);
    y += TH_SIZE_HEAD + TH_PAD;
    char fit[PATH_MAX_LEN + 8];
    Th_FitText(xd.folder, TH_F_UNI, w, fit, sizeof fit);
    Th_TextUni(fit, x, y, TH_TEXT_DIM);
    y += TH_ROW_H;
    char found[200], line[300];
    store_counts_text(&xd.scan.found, found, sizeof found);
    snprintf(line, sizeof line, "Found in the folder: %s.", found);
    Th_Text(line, x, y, TH_TEXT);
    y += TH_ROW_H;
    for (int i = 0; i < n1; i++) { Th_Text(l1[i], x, y, TH_TEXT_DIM); y += TH_LINE_H; }
    y += 4;
    for (int i = 0; i < n2; i++) { Th_Text(l2[i], x, y, TH_TEXT_DIM); y += TH_LINE_H; }
    y += TH_PAD;
    if (Th_Check((Rectangle){x, y, w, TH_ROW_H}, "Make a backup first (a .pxfdb of what the folder holds now)", &xd.backup, 0, "xd.backup") && fresh) xd.backup = !xd.backup;
    y += TH_ROW_H + 4;
    {
        float bw = 96;
        Rectangle ir = {x + 26, y, w - 26 - bw - 8, TH_ROW_H}, br = {ir.x + ir.width + 8, y, bw, TH_ROW_H};
        Th_TextField(ir, "xd.backup_path", xd.backupPath, 0, TH_TF_READONLY | (xd.backup ? 0 : TH_TF_DISABLED), NULL);
        Th_Reg("xd.change", br);
        if (Th_Button(br, "Change…", TH_BTN_OFF(xd.backup), "Choose where the backup file goes") && !fresh) {
            char p[PATH_MAX_LEN];
            if (DlgSave(p, sizeof p, xd.backupPath)) { Slashes(p); snprintf(xd.backupPath, sizeof xd.backupPath, "%s", p); }
        }
    }
    y = pr.y + pr.height - pad - TH_ROW_H;
    int clicked = -1;
    float bx = pr.x + pr.width - pad;
    const char *lb[3] = {"Cancel", "Append", "Overwrite"};
    for (int i = 2; i >= 0; i--) {
        float bw = fmaxf(Th_ButtonW(lb[i]), 96);
        bx -= bw;
        if (Th_Button((Rectangle){bx, y, bw, TH_ROW_H}, lb[i], i == 1 ? TH_BTN_PRIMARY : (i == 2 ? TH_BTN_DANGER : 0), NULL) && !fresh) clicked = i;
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
    PxCode cp = modal.pendingCp;
    char input[80];
    snprintf(input, sizeof input, "%s", modal.input);
    modal.kind = M_NONE;
    switch (action) {
    case A_DELETE: if (b == 1) CmdDelete(); break;
    case A_CLONE:
        if (b == 1) DoCloneUncoded();
        else if (b == 2) {
            PxCode c;
            if (!ParseCodepoint(input, &c)) { /* handled by disabled button */ break; }
            if (px_font_find_code(&font, c) >= 0) {
                char msg[160], cl[40];
                CodeLabel(c, cl, sizeof cl);
                snprintf(msg, sizeof msg, "%s already exists in this font.\nReplace it with the clone?", cl);
                ModalOpen(M_CONFIRM, A_CLONE_REPLACE, "Replace glyph", msg, 2, "Cancel", "Replace", NULL, 1, 0, true);
                modal.pendingCp = c;
            } else DoClone(c);
        }
        break;
    case A_CLONE_REPLACE: if (b == 1) DoClone(cp); break;
    case A_ADDGLYPH:
        if (b == 1) DoAddGlyph(-1);
        else if (b == 2) {
            PxCode c;
            if (!ParseCodepoint(input, &c)) break;
            int ex = px_font_find_code(&font, c);
            char cl[40];
            CodeLabel(c, cl, sizeof cl);
            if (ex >= 0) { selN = 0; SelOnly(ex); revealIdx = ex; Th_Toast(0, "%s is already in the font", cl); }
            else DoAddGlyph(c);
        }
        break;
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
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    uiBlocked = false;
    Th_SetInputBlocked(false);
    bool fresh = modal.frame == frameNo;

    const float pw = 440, pad = 24;
    char lines[8][160];
    int nl = WrapLines(modal.msg, pw - 2 * pad, lines, 8);
    float ph = pad + TH_SIZE_HEAD + TH_GAP + 2 + nl * TH_LINE_H + 6 + TH_ROW_H + pad;
    if (modal.kind == M_INPUT) ph += TH_ROW_H + TH_GAP + TH_LINE_H + 4;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    Th_Raised(pr, 1);

    float y = pr.y + pad;
    Th_TextHead(modal.title, pr.x + pad, y, TH_TEXT);
    y += TH_SIZE_HEAD + TH_GAP + 2;
    for (int i = 0; i < nl; i++) { Th_Text(lines[i], pr.x + pad, y, TH_TEXT_DIM); y += TH_LINE_H; }
    y += 6;

    bool inputOk = true;
    if (modal.kind == M_INPUT) {
        Rectangle ir = {pr.x + pad, y, pr.width - 2 * pad, TH_ROW_H};
        if (!Th_TextIs("modal.input")) Th_TextFocus("modal.input", modal.input, false);   /* always has the keyboard */
        if (Th_TextField(ir, "modal.input", modal.input, (int)sizeof modal.input, 0, NULL) & TH_TF_CHANGED) snprintf(modal.input, sizeof modal.input, "%s", Th_TextValue());
        y += TH_ROW_H + TH_GAP;
        PxCode cp;
        char info[120], cl[40];
        Color ic = TH_TEXT_DIM;
        if (modal.input[0] == 0) { snprintf(info, sizeof info, "Enter a code point"); inputOk = false; }
        else if (!ParseCodepoint(modal.input, &cp)) { snprintf(info, sizeof info, "Not a valid code point"); inputOk = false; ic = TH_DANGER; }
        else {
            CodeLabel(cp, cl, sizeof cl);
            if (px_font_find_code(&font, cp) >= 0) { snprintf(info, sizeof info, modal.action == A_ADDGLYPH ? "%s - already in the font" : "%s - already in the font, will ask to replace", cl); ic = TH_TEXT_NOTE; }
            else snprintf(info, sizeof info, "%s", cl);
        }
        Th_TextRich(info, pr.x + pad, y, ic, TH_F_BODY, -1);
        y += TH_LINE_H + 4;
    }
    y = pr.y + pr.height - pad - TH_ROW_H;
    float bx = pr.x + pr.width - pad;
    int clicked = -1;
    for (int i = modal.nbtn - 1; i >= 0; i--) {
        float bw = fmaxf(Th_ButtonW(modal.btn[i]), 84);
        bx -= bw;
        int fl = (i == modal.primary ? TH_BTN_PRIMARY | (i == modal.danger ? TH_BTN_DANGER : 0) : 0) | TH_BTN_OFF(!(modal.kind == M_INPUT && i == modal.primary && !inputOk));
        if (Th_Button((Rectangle){bx, y, bw, TH_ROW_H}, modal.btn[i], fl, NULL) && !fresh) clicked = i;
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
    if (modal.kind != M_NONE || xd.open || nf.open || fimp.open || Th_ColorPickIsOpen() || (Th_TextIs(NULL) && !Th_TextIs("Filter") && !Th_TextIs("finder.search")) || Sample_TextFocused()) return;   /* a text field owns the keyboard (the library and finder searches leave the navigation keys) */
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (ctrl && IsKeyPressed(KEY_O)) { Guard(G_LOAD_DIALOG, ""); return; }
    if (ctrl && shift && IsKeyPressed(KEY_S)) { SaveAsDialog(); return; }
    if (ctrl && !shift && IsKeyPressed(KEY_S)) { SaveDb(); return; }
    if (ctrl && !shift && IsKeyPressed(KEY_G) && !inFinder && store) { ShowGlyphs(); return; }
    if (alt && IsKeyPressed(KEY_RIGHT) && hasFont && (inBrowser || inFinder)) { ShowFont(); return; }
    if (alt && IsKeyPressed(KEY_LEFT)) { NavBack(); return; }
    if (inBrowser || inFinder || !hasFont) return;
    if (ctrl && shift && IsKeyPressed(KEY_F)) { CmdFindInLibrary(); return; }
    if (ctrl && IsKeyPressed(KEY_Z) && !shift) { if (readOnly) Th_Toast(2, "The font is locked"); else if (inEditor && ed && Editor_CancelFloat(ed)) Th_Toast(0, "Cancelled"); else DoUndoRedo(false); return; }
    if (ctrl && (IsKeyPressed(KEY_Y) || (IsKeyPressed(KEY_Z) && shift))) { if (!readOnly && !(inEditor && ed && Editor_CancelFloat(ed))) DoUndoRedo(true); return; }
    if (inEditor) return;

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
    bool unlockArg = false;
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
        else if (!strcmp(a, "--unlock")) unlockArg = true;
        else if (!strcmp(a, "--browser")) startup.browser = true;
        else if (!strcmp(a, "--panel-scroll") && i + 1 < argc) startup.panelScroll = atoi(argv[++i]);
        else if (!strcmp(a, "--play") && i + 1 < argc) playPath = argv[++i];
        else if (!strcmp(a, "--no-dialogs")) noDialogs = true;
        else if (!strcmp(a, "--modal") && i + 1 < argc) modalArg = argv[++i];
        else if (!strcmp(a, "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &winW, &winH);
        else if (a[0] != '-' && !openPath) openPath = a;
    }
    startup.unlock = unlockArg;

    if (playPath) { noDialogs = true; if (!PlayLoad(playPath)) return 2; }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (playPath ? FLAG_WINDOW_ALWAYS_RUN : 0));   /* a script keeps running when the window is minimised or hidden */
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(winW, winH, "pxfe");
    Browser_SetCharSource(Play_GetChar);
    Browser_SetInfoTab(DrawLibraryTab);
    Browser_SetLockQuery(LockedOf);
    SetWindowMinSize(1110, 600);
    SetExitKey(0);
    SetTargetFPS(playPath ? play.fps : 60);
    Theme_Load();
    if (theme.missing[0]) Th_Toast(TH_TOAST_ERR, "Font files not found in assets/fonts: %s. The default font is used.", theme.missing);
    Layers_SetFileDialog(DlgLayerFile);
    Th_HoverFn = Hov;
    Th_NoticeFn = HashNotice;
    Sample_LoadIni(IniPath());   /* the shared sample text and the font view strip settings */
    { char v[8]; if (IniGetApp("pixelPerfect", v, sizeof v)) pixelPerfect = atoi(v) != 0; }
    { char v[8]; if (IniGetApp("filled", v, sizeof v)) filledShapes = atoi(v) != 0; }
    { char v[8]; if (IniGetApp("fontTab", v, sizeof v)) { int t = atoi(v); panelTab = t < 0 || t > 4 ? 0 : t; } }
    { char v[8]; if (IniGetApp("pickerMode", v, sizeof v)) Th_ColorPickSetMode(atoi(v)); }

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
        else if (!strcmp(modalArg, "toast")) { Th_Toast(1, "Saved pxfm 1e1a3ab8, pxtf 3c4d5e6f"); Th_Toast(2, "Select exactly one glyph to clone"); }
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
        FitTick();                       /* the layer the glyphs are drawn from changed: they are drawn again */
        EnsureHashes();
        if (FlPending()) FlSync();       /* the font layers changed: they go into the settings under the font's hash */
        if (!shotPath && !job.active) HandleShortcuts();

        BeginDrawing();
        ClearBackground(TH_BG);
        /* the mouse button that opened a document (a double click) is still down: its release must not hit the new view */
        if (swallowMouse && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) swallowMouse = false;
        bool baseBlocked = modal.kind != M_NONE || xd.open || nf.open || fimp.open || job.active || swallowMouse || Th_ColorPickIsOpen();
        uiBlocked = baseBlocked || fileMenu.open;   /* an open menu owns the mouse: nothing below it reacts */
        Th_SetInputBlocked(uiBlocked);
        hintText = NULL;
        Th_RegClear();
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);

        if (job.active) {
            DrawRectangle(0, 0, W, TOOLBAR_H, TH_PANEL);
            Th_HLine(0, (float)W, TOOLBAR_H - 1, TH_LINE);
            DrawProgressScreen(W, H);
            Th_ToastDraw((Rectangle){0, 0, (float)W, (float)(H - STATUS_H)}, false);
            UpdateTitle();
            EndDrawing();
            if (jobShotPath && !jobShotDone && (GetTime() - job.t0) * 1000.0 >= jobShotMs) { TakeScreenshot(jobShotPath); jobShotDone = true; }
            continue;
        }

        bool fontView = hasFont && !inBrowser && !inFinder;
        bool edFrame = fontView && inEditor;
        float top = TOOLBAR_H;
        float contentH2 = (float)(H - top - STATUS_H);
        bool ownStatus = inBrowser && browser && !inFinder;   /* the browser has its own status line: no second bar below it */
        Rectangle fullArea = {0, TOOLBAR_H, (float)W, (float)(H - TOOLBAR_H - STATUS_H) + (ownStatus ? STATUS_H : 0)};
        Rectangle gridArea = {0, top, (float)(W - PANEL_W), contentH2};
        Rectangle panelArea = {(float)(W - PANEL_W), top, PANEL_W, contentH2};
        Rectangle stripArea = {0};   /* the sample strip, at the bottom of the grid or of the glyph view */
        if (fontView) {
            float sh = SampleStrip_Height(gridArea.height);
            gridArea.height -= sh;
            stripArea = (Rectangle){0, gridArea.y + gridArea.height, gridArea.width, sh};
        }
        Rectangle edArea = gridArea;
        play.gridArea = gridArea;
        hoverIdx = -1;
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
            bool got = Browser_UpdateDraw(browser, fullArea, &ch);
            if (got && ch.action != BROWSER_NONE) {
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
                Layers_SetFontDoc(Editor_Layers(ed), flDoc);
                Th_SetInputBlocked(uiBlocked || Th_TextIs(NULL) || Layers_PopoverOpen(Editor_Layers(ed))); /* a field being edited owns the keyboard */
                Editor_SetLayersTabActive(ed, glyphTab == 2);
                EditorResult er = Editor_UpdateDraw(ed, edArea);
                if (Editor_TakeLayersRequest(ed)) { glyphTab = 2; panelScroll = 0; }
                Th_SetInputBlocked(uiBlocked);
                if (Editor_TakeChanged(ed)) needHash = true;
                if (Editor_PixelPerfect(ed) != pixelPerfect) { pixelPerfect = Editor_PixelPerfect(ed); IniSetApp("pixelPerfect", pixelPerfect ? "1" : "0"); }
                if (Editor_Filled(ed) != filledShapes) { filledShapes = Editor_Filled(ed); IniSetApp("filled", filledShapes ? "1" : "0"); }
                int gi = Editor_GlyphIndex(ed);
                if (er == EDITOR_CLOSE) {
                    CloseGlyphView();
                } else if (er == EDITOR_PREV || er == EDITOR_NEXT) {
                    int ni = gi + (er == EDITOR_NEXT ? 1 : -1);
                    if (ni >= 0 && ni < font.count) OpenEditor(ni);
                } else if (er == EDITOR_LOCK) {
                    SetFontLocked(!readOnly);
                }
            } else {
                if (ed) Layers_PopoverClose(Editor_Layers(ed));
                UpdateDrawGrid(gridArea);
            }
            if (stripArea.height > 0) SampleStrip_Draw(stripArea, &font, uiBlocked);
            if (edFrame == inEditor) {   /* (the frame in which the glyph view opens or closes draws no panel) */
                bool popOpen = inEditor && ed && Layers_PopoverOpen(Editor_Layers(ed));
                if (popOpen) Th_SetInputBlocked(true);   /* the Add Layer popover is modal */
                DrawMetricsPanel(panelArea);
                Th_SetInputBlocked(uiBlocked);
                if (inEditor && ed) Layers_DrawPopover(Editor_Layers(ed), panelArea);
            }
        }
        RunLibCommand();           /* a command of a Typeface or Font tab */
        uiBlocked = baseBlocked;   /* the toolbar and the menus themselves stay live */
        Th_SetInputBlocked(uiBlocked);
        DrawToolbar(W);
        EnsureHashes();
        DrawStatus(W, H);
        DrawFileMenu();
        {   /* the right panel (its commands) stays clear */
            bool side = (inBrowser && browser && !inFinder) || (hasFont && !inBrowser && !inFinder);
            Th_ToastDraw((Rectangle){0, 0, (float)(W - (side ? PANEL_W : 0)), (float)(H - STATUS_H)}, inBrowser && browser && Browser_InTypeface(browser));
        }
        DrawExportDialog(W, H);
        DrawNewFontDialog(W, H);
        DrawImportFontDialog(W, H);
        DrawModal(W, H);
        if (Th_ColorPickIsOpen()) {   /* the colour picker: modal over everything */
            int pm = Th_ColorPickMode();
            uiBlocked = false;
            Th_SetInputBlocked(false);
            Th_ColorPickDraw(W, H);
            if (Th_ColorPickMode() != pm) { char v[8]; snprintf(v, sizeof v, "%d", Th_ColorPickMode()); IniSetApp("pickerMode", v); }
        }
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
    FlDoc_Free(flDoc);
    FlCache_Clear();
    if (hasFont) px_font_free(&font);
    if (hasCont) pxtf_free(&cont);
    free(lx); free(ly); free(lw); free(sel); free(ghCp); free(ghHash);
    Theme_Unload();
    CloseWindow();
    return play.active ? PlayFinish() : 0;
}
