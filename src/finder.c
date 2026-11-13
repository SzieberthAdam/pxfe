/* finder.c - the glyph finder view (see finder.h and DESIGN.md "The glyph finder").
 *
 * The distinct bitmaps of a code point come from a worker thread (store_glyphs_for_code on its own StoreConn); the bitmaps
 * of the cells that are on screen are read on the UI thread, one pxgl row each (a few microseconds), and kept as
 * textures in a cache the size of the result list.  The grid is virtualized by rows. */
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>

#include "finder.h"
#include "theme.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FD_PANEL_MAX 360
#define FD_PANEL_MIN 300
#define FD_PAD_P 16             /* inside the side panel (the same as the library browser's) */
#define FD_PAD 8                /* inside a cell, around the box */
#define FD_GAP 8                /* between cells */
#define FD_LABEL_H 18
#define FD_BOX_MAX_W 200
#define FD_BOX_MAX_H 240
#define FD_TEX_PER_FRAME 40
#define FD_TEX_KEEP 700

typedef enum { SORT_COUNT = 0, SORT_HEIGHT, SORT_WIDTH } SortMode;

struct Finder {
    Store *store;
    Insight *insight;
    StoreConn *conn;            /* the UI thread's own connection (bitmaps of visible cells) */
    /* worker */
    HANDLE th;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    bool quit;
    int32_t reqCode; unsigned reqGen, doneGen;
    StoreCodeGlyphList pend; unsigned pendGen; bool hasPend;
    /* query */
    int32_t code;               /* -1 empty */
    char text[96];
    /* results */
    StoreCodeGlyphList list;
    bool loading;
    int *order; int nOrder;
    SortMode sort;
    int zoom;                   /* 1..8 */
    int sideTab;                /* the side panel: 0 View, 1 Usage */
    int sel;                    /* display index, -1 */
    char selHash[33];
    float scroll;
    int reveal;                 /* display index to scroll into view, -1 */
    Texture2D *tex; unsigned *used; int nTex; unsigned frame;
    double lastClickT; int lastClickIdx;
    char dblHash[33]; double dblT;
    float usageScroll;
    /* layout of the last frame, for the test hooks */
    Rectangle gridView;
    int z;                      /* integer zoom in use */
    int nRows; int *rowFirst; float *rowY, *rowH; int *rowAbove, *rowBelow;   /* flow layout: rows of cells on one baseline */
    float *cellX, *cellW;       /* per display index: x inside the grid, width */
    float layoutW; int layoutZ, layoutN; unsigned layoutVer, ver;
};

/* ------------------------------------------------------------------ Unicode blocks (BMP and a few of the SMP) */

typedef struct Block { int32_t lo, hi; const char *name; } Block;
static const Block kBlocks[] = {
    {0x0000, 0x007F, "Basic Latin"}, {0x0080, 0x00FF, "Latin-1 Supplement"}, {0x0100, 0x017F, "Latin Extended-A"},
    {0x0180, 0x024F, "Latin Extended-B"}, {0x0250, 0x02AF, "IPA Extensions"}, {0x02B0, 0x02FF, "Spacing Modifier Letters"},
    {0x0300, 0x036F, "Combining Diacritical Marks"}, {0x0370, 0x03FF, "Greek and Coptic"}, {0x0400, 0x04FF, "Cyrillic"},
    {0x0500, 0x052F, "Cyrillic Supplement"}, {0x0530, 0x058F, "Armenian"}, {0x0590, 0x05FF, "Hebrew"}, {0x0600, 0x06FF, "Arabic"},
    {0x0700, 0x074F, "Syriac"}, {0x0780, 0x07BF, "Thaana"}, {0x0900, 0x097F, "Devanagari"}, {0x0980, 0x09FF, "Bengali"},
    {0x0A00, 0x0A7F, "Gurmukhi"}, {0x0A80, 0x0AFF, "Gujarati"}, {0x0B00, 0x0B7F, "Oriya"}, {0x0B80, 0x0BFF, "Tamil"},
    {0x0C00, 0x0C7F, "Telugu"}, {0x0C80, 0x0CFF, "Kannada"}, {0x0D00, 0x0D7F, "Malayalam"}, {0x0D80, 0x0DFF, "Sinhala"},
    {0x0E00, 0x0E7F, "Thai"}, {0x0E80, 0x0EFF, "Lao"}, {0x0F00, 0x0FFF, "Tibetan"}, {0x1000, 0x109F, "Myanmar"},
    {0x10A0, 0x10FF, "Georgian"}, {0x1100, 0x11FF, "Hangul Jamo"}, {0x1200, 0x137F, "Ethiopic"}, {0x13A0, 0x13FF, "Cherokee"},
    {0x1400, 0x167F, "Unified Canadian Aboriginal Syllabics"}, {0x1680, 0x169F, "Ogham"}, {0x16A0, 0x16FF, "Runic"},
    {0x1780, 0x17FF, "Khmer"}, {0x1800, 0x18AF, "Mongolian"}, {0x1D00, 0x1D7F, "Phonetic Extensions"},
    {0x1E00, 0x1EFF, "Latin Extended Additional"}, {0x1F00, 0x1FFF, "Greek Extended"}, {0x2000, 0x206F, "General Punctuation"},
    {0x2070, 0x209F, "Superscripts and Subscripts"}, {0x20A0, 0x20CF, "Currency Symbols"},
    {0x20D0, 0x20FF, "Combining Diacritical Marks for Symbols"}, {0x2100, 0x214F, "Letterlike Symbols"}, {0x2150, 0x218F, "Number Forms"},
    {0x2190, 0x21FF, "Arrows"}, {0x2200, 0x22FF, "Mathematical Operators"}, {0x2300, 0x23FF, "Miscellaneous Technical"},
    {0x2400, 0x243F, "Control Pictures"}, {0x2440, 0x245F, "Optical Character Recognition"}, {0x2460, 0x24FF, "Enclosed Alphanumerics"},
    {0x2500, 0x257F, "Box Drawing"}, {0x2580, 0x259F, "Block Elements"}, {0x25A0, 0x25FF, "Geometric Shapes"},
    {0x2600, 0x26FF, "Miscellaneous Symbols"}, {0x2700, 0x27BF, "Dingbats"}, {0x27C0, 0x27EF, "Miscellaneous Mathematical Symbols-A"},
    {0x27F0, 0x27FF, "Supplemental Arrows-A"}, {0x2800, 0x28FF, "Braille Patterns"}, {0x2900, 0x297F, "Supplemental Arrows-B"},
    {0x2980, 0x29FF, "Miscellaneous Mathematical Symbols-B"}, {0x2A00, 0x2AFF, "Supplemental Mathematical Operators"},
    {0x2B00, 0x2BFF, "Miscellaneous Symbols and Arrows"}, {0x2C00, 0x2C5F, "Glagolitic"}, {0x2C60, 0x2C7F, "Latin Extended-C"},
    {0x2E00, 0x2E7F, "Supplemental Punctuation"}, {0x2E80, 0x2EFF, "CJK Radicals Supplement"}, {0x3000, 0x303F, "CJK Symbols and Punctuation"},
    {0x3040, 0x309F, "Hiragana"}, {0x30A0, 0x30FF, "Katakana"}, {0x3100, 0x312F, "Bopomofo"}, {0x3130, 0x318F, "Hangul Compatibility Jamo"},
    {0x3200, 0x32FF, "Enclosed CJK Letters and Months"}, {0x3300, 0x33FF, "CJK Compatibility"},
    {0x3400, 0x4DBF, "CJK Unified Ideographs Extension A"}, {0x4DC0, 0x4DFF, "Yijing Hexagram Symbols"},
    {0x4E00, 0x9FFF, "CJK Unified Ideographs"}, {0xA000, 0xA48F, "Yi Syllables"}, {0xA720, 0xA7FF, "Latin Extended-D"},
    {0xAC00, 0xD7AF, "Hangul Syllables"}, {0xD800, 0xDFFF, "Surrogates"}, {0xE000, 0xF8FF, "Private Use Area"},
    {0xF900, 0xFAFF, "CJK Compatibility Ideographs"}, {0xFB00, 0xFB4F, "Alphabetic Presentation Forms"},
    {0xFB50, 0xFDFF, "Arabic Presentation Forms-A"}, {0xFE00, 0xFE0F, "Variation Selectors"}, {0xFE20, 0xFE2F, "Combining Half Marks"},
    {0xFE30, 0xFE4F, "CJK Compatibility Forms"}, {0xFE50, 0xFE6F, "Small Form Variants"}, {0xFE70, 0xFEFF, "Arabic Presentation Forms-B"},
    {0xFF00, 0xFFEF, "Halfwidth and Fullwidth Forms"}, {0xFFF0, 0xFFFF, "Specials"},
    {0x10000, 0x100FF, "Linear B"}, {0x1D100, 0x1D1FF, "Musical Symbols"}, {0x1D400, 0x1D7FF, "Mathematical Alphanumeric Symbols"},
    {0x1F000, 0x1F02F, "Mahjong Tiles"}, {0x1F030, 0x1F09F, "Domino Tiles"}, {0x1F300, 0x1F5FF, "Miscellaneous Symbols and Pictographs"},
    {0x1F600, 0x1F64F, "Emoticons"}, {0x1F680, 0x1F6FF, "Transport and Map Symbols"}, {0x1F900, 0x1F9FF, "Supplemental Symbols and Pictographs"},
    {0x20000, 0x2A6DF, "CJK Unified Ideographs Extension B"}, {0xE0000, 0xE007F, "Tags"}, {0xF0000, 0xFFFFF, "Supplementary Private Use Area-A"},
    {0x100000, 0x10FFFF, "Supplementary Private Use Area-B"},
};

static const char *block_name(int32_t code)
{
    int lo = 0, hi = (int)(sizeof kBlocks / sizeof *kBlocks) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (code < kBlocks[mid].lo) hi = mid - 1;
        else if (code > kBlocks[mid].hi) lo = mid + 1;
        else return kBlocks[mid].name;
    }
    return "Unknown block";
}

/* ------------------------------------------------------------------ query parsing */

int32_t Finder_ParseQuery(const char *text)
{
    if (!text) return -1;
    size_t len = strlen(text);
    if (len == 0) return -1;
    int size = 0;
    int cp = GetCodepoint(text, &size);
    if (size > 0 && (size_t)size == len) {                            /* exactly one character */
        if (cp == 0x3F && text[0] != '?') return -1;                  /* GetCodepoint's error value */
        return cp >= 0 && cp <= PX_CODE_MAX_CODEPOINT ? cp : -1;
    }
    const char *p = text;
    while (*p == ' ' || *p == '\t') p++;
    const char *e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    if (e == p) return -1;
    if (e - p >= 2 && (p[0] == 'U' || p[0] == 'u') && p[1] == '+') p += 2;
    else if (e - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (e == p || e - p > 6) return -1;
    long v = 0;
    for (; p < e; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v <= PX_CODE_MAX_CODEPOINT ? (int32_t)v : -1;
}

static bool printable(int32_t c)
{
    return c > 0x20 && !(c >= 0x7F && c <= 0x9F) && !(c >= 0xD800 && c <= 0xDFFF) && c <= PX_CODE_MAX_CODEPOINT;
}

/* ------------------------------------------------------------------ worker */

static DWORD WINAPI worker_main(LPVOID p)
{
    Finder *f = (Finder *)p;
    StoreConn *c = store_conn_open(f->store);
    for (;;) {
        EnterCriticalSection(&f->cs);
        while (!f->quit && f->reqGen == f->doneGen) SleepConditionVariableCS(&f->cv, &f->cs, INFINITE);
        if (f->quit) { LeaveCriticalSection(&f->cs); break; }
        unsigned gen = f->reqGen;
        int32_t code = f->reqCode;
        LeaveCriticalSection(&f->cs);

        StoreCodeGlyphList l = { 0 };
        char err[PX_ERR_LEN];
        if (code >= 0 && !store_glyphs_for_code(c, code, &l, err)) memset(&l, 0, sizeof l);

        EnterCriticalSection(&f->cs);
        if (f->hasPend) store_code_glyph_list_free(&f->pend);
        f->pend = l; f->pendGen = gen; f->hasPend = true;
        f->doneGen = gen;
        LeaveCriticalSection(&f->cs);
    }
    store_conn_close(c);
    return 0;
}

/* ------------------------------------------------------------------ lifecycle */

Finder *Finder_Create(Store *store, Insight *insight)
{
    Finder *f = (Finder *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->store = store; f->insight = insight;
    f->conn = store_conn_open(store);
    f->code = -1; f->zoom = 3; f->sel = -1; f->reveal = -1; f->lastClickIdx = -1;
    InitializeCriticalSection(&f->cs);
    InitializeConditionVariable(&f->cv);
    f->th = CreateThread(NULL, 0, worker_main, f, 0, NULL);
    return f;
}

static void free_textures(Finder *f)
{
    for (int i = 0; i < f->list.count && f->tex; i++) if (f->tex[i].id) UnloadTexture(f->tex[i]);
    free(f->tex); free(f->used);
    f->tex = NULL; f->used = NULL; f->nTex = 0;
}

static void free_layout(Finder *f);

void Finder_Destroy(Finder *f)
{
    if (!f) return;
    EnterCriticalSection(&f->cs);
    f->quit = true;
    WakeAllConditionVariable(&f->cv);
    LeaveCriticalSection(&f->cs);
    if (f->th) { WaitForSingleObject(f->th, INFINITE); CloseHandle(f->th); }
    free_textures(f);
    store_code_glyph_list_free(&f->list);
    if (f->hasPend) store_code_glyph_list_free(&f->pend);
    free(f->order);
    free_layout(f);
    if (f->conn) store_conn_close(f->conn);
    DeleteCriticalSection(&f->cs);
    free(f);
}

static void request(Finder *f)
{
    f->loading = f->code >= 0;
    EnterCriticalSection(&f->cs);
    f->reqCode = f->code;
    f->reqGen++;
    WakeAllConditionVariable(&f->cv);
    LeaveCriticalSection(&f->cs);
    f->sel = -1; f->selHash[0] = 0; f->scroll = 0; f->usageScroll = 0; f->reveal = -1;
    f->dblHash[0] = 0;
}

static void set_code(Finder *f, int32_t code, bool rewriteText)
{
    if (code < 0 || code > PX_CODE_MAX_CODEPOINT) code = -1;
    if (rewriteText) {
        if (code < 0) f->text[0] = 0;
        else if (printable(code) && code <= 0x17F) { int n = 0; const char *u = CodepointToUTF8(code, &n); memcpy(f->text, u, (size_t)n); f->text[n] = 0; }
        else snprintf(f->text, sizeof f->text, "U+%04X", (unsigned)code);
    }
    if (code == f->code) return;
    f->code = code;
    request(f);
}

void Finder_SetCode(Finder *f, int32_t code) { if (f) set_code(f, code, true); }
int32_t Finder_Code(const Finder *f) { return f ? f->code : -1; }

/* ------------------------------------------------------------------ results */

static const StoreCodeGlyphList *g_sortList;
static int g_sortMode;
static int ord_cmp(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb;
    const StoreCodeGlyph *x = &g_sortList->g[a], *y = &g_sortList->g[b];
    int d = 0;
    if (g_sortMode == SORT_HEIGHT) d = x->height - y->height;
    else if (g_sortMode == SORT_WIDTH) d = x->width - y->width;
    if (d) return d;
    if (x->nFonts != y->nFonts) return y->nFonts - x->nFonts;
    return a - b;
}

static void resort(Finder *f)
{
    f->ver++;
    f->nOrder = f->list.count;
    free(f->order);
    f->order = (int *)malloc((size_t)(f->nOrder + 1) * sizeof(int));
    for (int i = 0; i < f->nOrder; i++) f->order[i] = i;
    if (f->sort != SORT_COUNT) { g_sortList = &f->list; g_sortMode = f->sort; qsort(f->order, (size_t)f->nOrder, sizeof(int), ord_cmp); }
    f->sel = -1;
    if (f->selHash[0]) for (int i = 0; i < f->nOrder; i++) if (!strcmp(f->list.g[f->order[i]].glyph, f->selHash)) { f->sel = i; break; }
}

static void pull(Finder *f)
{
    StoreCodeGlyphList l = { 0 };
    bool got = false;
    EnterCriticalSection(&f->cs);
    if (f->hasPend) {
        if (f->pendGen == f->reqGen) { l = f->pend; got = true; }
        else store_code_glyph_list_free(&f->pend);
        memset(&f->pend, 0, sizeof f->pend);
        f->hasPend = false;
    }
    LeaveCriticalSection(&f->cs);
    if (!got) return;
    free_textures(f);
    store_code_glyph_list_free(&f->list);
    f->list = l;
    f->tex = (Texture2D *)calloc((size_t)l.count + 1, sizeof(Texture2D));
    f->used = (unsigned *)calloc((size_t)l.count + 1, sizeof(unsigned));
    f->loading = false;
    resort(f);
}

int Finder_ResultCount(const Finder *f)
{
    if (!f) return 0;
    pull((Finder *)f);
    if (f->code < 0) return 0;
    if (f->loading) return -1;
    return f->list.count;
}

void Finder_Select(Finder *f, int index)
{
    if (!f) return;
    pull(f);
    if (index < 0 || index >= f->nOrder) { f->sel = -1; f->selHash[0] = 0; return; }
    f->sel = index;
    memcpy(f->selHash, f->list.g[f->order[index]].glyph, 33);
    f->reveal = index;
}

int Finder_CellFonts(const Finder *f, int index)
{
    if (!f || index < 0 || index >= f->nOrder) return -1;
    return f->list.g[f->order[index]].nFonts;
}

bool Finder_CellCenter(const Finder *f, int index, Vector2 *out)
{
    if (!f || index < 0 || index >= f->nOrder || f->nRows <= 0 || !f->cellX) return false;
    int row = 0;
    while (row + 1 < f->nRows && f->rowFirst[row + 1] <= index) row++;
    float x = f->gridView.x + f->cellX[index] + f->cellW[index] / 2;
    float y = f->gridView.y + f->rowY[row] - f->scroll + f->rowH[row] / 2;
    if (y < f->gridView.y || y > f->gridView.y + f->gridView.height || x > f->gridView.x + f->gridView.width) return false;
    *out = (Vector2){ x, y };
    return true;
}

int Finder_SideTab(const Finder *f) { return f ? f->sideTab : -1; }
int Finder_Zoom(const Finder *f) { return f ? f->zoom : -1; }

bool Finder_SelectedGlyph(const Finder *f, char out[33])
{
    if (!f || f->sel < 0 || f->sel >= f->nOrder) return false;
    memcpy(out, f->list.g[f->order[f->sel]].glyph, 33);
    return true;
}

/* ------------------------------------------------------------------ bitmaps */

static bool load_cell_texture(Finder *f, int li)
{
    size_t len = 0;
    char *t = store_pxgl_text(f->conn, f->list.g[li].glyph, &len, NULL);
    if (!t) return false;
    int w = 0, h = 0, cur = 0;
    for (size_t i = 0; i < len; i++) { if (t[i] == '\n') { if (!w) w = cur; cur = 0; h++; } else cur++; }
    if (w > 0 && h > 0) {
        unsigned char *m = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
        int row = 0, col = 0;
        for (size_t i = 0; i < len && m; i++) {
            if (t[i] == '\n') { row++; col = 0; continue; }
            if (row < h && col < w) m[row * w + col] = t[i] != '.';
            col++;
        }
        f->tex[li] = Th_MaskTexture(m, w, h, TH_TEXT);
        free(m);
    }
    free(t);
    return f->tex[li].id != 0;
}

/* ------------------------------------------------------------------ text box */

static void box_apply(Finder *f)
{
    if (f->text[0] == 0) { set_code(f, -1, false); return; }
    int32_t c = Finder_ParseQuery(f->text);
    if (c >= 0) set_code(f, c, false);
}

/* ------------------------------------------------------------------ drawing */

static bool in_input_ok(void) { return !Th_InputBlocked(); }

/* The View tab of the side panel: the character to look for (search box, Previous / Next, its preview), the zoom and the sort order. */
static void draw_view_tab(Finder *f, Rectangle P)
{
    float x = P.x + FD_PAD_P, w = P.width - 2 * FD_PAD_P, y = P.y + 44 + 16;
    char s[96] = "";
    if (f->code >= 0 && !f->loading && f->list.count > 0) {
        long fonts = 0;
        for (int i = 0; i < f->list.count; i++) fonts += f->list.g[i].nFonts;
        snprintf(s, sizeof s, "%d bitmap%s, %ld font%s", f->list.count, f->list.count == 1 ? "" : "s", fonts, fonts == 1 ? "" : "s");
    }
    Th_Section("RESULTS", x, &y, w, 0, s);
    y += TH_GAP;
    Th_Section("CHARACTER", x, &y, w, 0, NULL);
    Rectangle sb = { x, y, w - 80, TH_ROW_H };
    int tf = Th_TextField(sb, "finder.search", f->text, (int)sizeof f->text, TH_TF_SEARCH | TH_TF_SELECT, "Character or U+XXXX");
    if (tf & TH_TF_CHANGED) snprintf(f->text, sizeof f->text, "%s", Th_TextValue());
    if (tf & (TH_TF_CHANGED | TH_TF_ENTER)) box_apply(f);
    int32_t step = 0;
    if (Th_Button((Rectangle){ sb.x + sb.width + TH_GAP, sb.y, TH_ROW_H, TH_ROW_H }, "finder.prev", TH_BTN_ICON(TH_IC_LEFT), "Previous code point")) step = -1;
    if (Th_Button((Rectangle){ sb.x + sb.width + 2 * TH_GAP + TH_ROW_H, sb.y, TH_ROW_H, TH_ROW_H }, "finder.next", TH_BTN_ICON(TH_IC_RIGHT), "Next code point")) step = 1;
    if (step && f->code >= 0) {
        int32_t c = f->code + step;
        if (c >= 0 && c <= PX_CODE_MAX_CODEPOINT) set_code(f, c, true);
    } else if (step && f->code < 0) set_code(f, step > 0 ? 0x20 : 0x7E, true);
    y += 44;
    if (f->code >= 0) {
        Rectangle cb = { x, y, 44, 44 };
        Th_Card(cb, 0);
        if (Th_CodeVisible(f->code)) {
            Th_CodeDraw(f->code, cb, TH_TEXT, false, 2);   /* Unifont at 32 px: twice the pixels */
        } else {
            Rectangle bx = { cb.x + 12, cb.y + 8, 20, 28 };
            Th_DashedHLine(bx.x, bx.x + bx.width, bx.y, 3, TH_TEXT_DIM);
            Th_DashedHLine(bx.x, bx.x + bx.width, bx.y + bx.height, 3, TH_TEXT_DIM);
            Th_DashedVLine(bx.x, bx.y, bx.y + bx.height, 3, TH_TEXT_DIM);
            Th_DashedVLine(bx.x + bx.width, bx.y, bx.y + bx.height, 3, TH_TEXT_DIM);
        }
        char code[24];
        snprintf(code, sizeof code, "U+%04X", (unsigned)f->code);
        Th_TextUni(code, x + 56, y + 4, TH_TEXT);
        Th_Text(block_name(f->code), x + 56, y + 22, TH_TEXT_DIM);
    }
    y += 62;
    Th_Section("VIEW", x, &y, w, 0, NULL);
    char zt[8];
    snprintf(zt, sizeof zt, "%dx", f->zoom);
    Rectangle zr = Th_Row(x, &y, w, "Zoom", NULL, 0);
    int zs = Th_Stepper((Rectangle){ zr.x, zr.y, TH_STEP_W, TH_ROW_H }, zt, "fzoom", (f->zoom <= 1 ? TH_STEP_NO_MINUS : 0) | (f->zoom >= 8 ? TH_STEP_NO_PLUS : 0));
    if (zs) f->zoom += zs;
    static const char *const names[] = { "Count", "Height", "Width" };
    int c = Th_Segmented(Th_Row(x, &y, w, "Sort by", NULL, 0), &(ThSeg){ names, 3, (int)f->sort, 0u, "fsort" });
    if (c >= 0 && (int)f->sort != c) { f->sort = (SortMode)c; resort(f); f->scroll = 0; }
}

static void draw_center_msg(Rectangle r, const char *msg, bool spinner)
{
    Vector2 m = Th_Measure(msg);
    float x = r.x + (r.width - m.x) / 2, y = r.y + r.height / 3;
    if (spinner) { Th_Spinner((Vector2){ r.x + r.width / 2, y - 18 }, 8, TH_TEXT_DIM); }
    Th_Text(msg, floorf(x), floorf(y), TH_TEXT_DIM);
}

static void free_layout(Finder *f)
{
    free(f->rowFirst); free(f->rowY); free(f->rowH); free(f->rowAbove); free(f->rowBelow); free(f->cellX); free(f->cellW);
    f->rowFirst = NULL; f->rowY = f->rowH = f->cellX = f->cellW = NULL; f->rowAbove = f->rowBelow = NULL;
    f->nRows = 0;
}

/* Flow layout: cells of their own width left to right, a new row when the next does not fit; a row's cells share the
 * baseline, the row is as tall as its tallest cell. */
static void layout(Finder *f, float gridW)
{
    int z = f->zoom;
    if (f->cellX && f->layoutW == gridW && f->layoutZ == z && f->layoutN == f->nOrder && f->layoutVer == f->ver) { f->z = z; return; }
    free_layout(f);
    int n = f->nOrder;
    f->z = z; f->layoutW = gridW; f->layoutZ = z; f->layoutN = n; f->layoutVer = f->ver;
    f->cellX = (float *)malloc((size_t)(n + 1) * sizeof(float));
    f->cellW = (float *)malloc((size_t)(n + 1) * sizeof(float));
    f->rowFirst = (int *)malloc((size_t)(n + 2) * sizeof(int));
    f->rowY = (float *)malloc((size_t)(n + 2) * sizeof(float));
    f->rowH = (float *)malloc((size_t)(n + 2) * sizeof(float));
    f->rowAbove = (int *)malloc((size_t)(n + 2) * sizeof(int));
    f->rowBelow = (int *)malloc((size_t)(n + 2) * sizeof(int));
    float avail = gridW - 2 * FD_GAP - 8, x = 0, y = FD_GAP;
    int row = -1;
    for (int di = 0; di < n; di++) {
        const StoreCodeGlyph *e = &f->list.g[f->order[di]];
        float cw = (float)(e->width * z + 2 * FD_PAD);
        if (cw < 64) cw = 64;
        if (cw > 420) cw = 420;
        if (row < 0 || (x > 0 && x + cw > avail)) {
            if (row >= 0) y += f->rowH[row] + FD_GAP;
            row++;
            f->rowFirst[row] = di; f->rowY[row] = y; f->rowAbove[row] = 1; f->rowBelow[row] = 0; f->rowH[row] = 0;
            x = 0;
        }
        int top = e->yTypical + e->height, bot = e->yTypical < 0 ? -e->yTypical : 0;
        if (top > f->rowAbove[row]) f->rowAbove[row] = top;
        if (bot > f->rowBelow[row]) f->rowBelow[row] = bot;
        f->cellX[di] = FD_GAP + x; f->cellW[di] = cw;
        x += cw + FD_GAP;
        float bh = (float)((f->rowAbove[row] + f->rowBelow[row]) * z);
        if (bh > FD_BOX_MAX_H) bh = FD_BOX_MAX_H;
        f->rowH[row] = bh + 2 * FD_PAD + FD_LABEL_H;
    }
    f->nRows = row + 1;
    f->rowFirst[f->nRows] = n;
}

static void draw_grid(Finder *f, Rectangle g)
{
    f->gridView = g;
    DrawRectangleRec(g, TH_BG);
    if (f->code < 0) { draw_center_msg(g, "Type a character or U+ code", false); f->nRows = 0; return; }
    if (f->loading) { draw_center_msg(g, "Searching the library\xE2\x80\xA6", true); f->nRows = 0; return; }
    if (f->list.count == 0) {
        char msg[96];
        snprintf(msg, sizeof msg, "No glyph in the library for U+%04X", (unsigned)f->code);
        draw_center_msg(g, msg, false);
        f->nRows = 0;
        return;
    }
    layout(f, g.width);
    int n = f->nOrder;
    float content = f->rowY[f->nRows - 1] + f->rowH[f->nRows - 1] + FD_GAP;
    Vector2 m = GetMousePosition();
    if (f->reveal >= 0 && f->reveal < n) {
        int rr = 0;
        while (rr + 1 < f->nRows && f->rowFirst[rr + 1] <= f->reveal) rr++;
        if (f->rowY[rr] - FD_GAP < f->scroll) f->scroll = f->rowY[rr] - FD_GAP;
        else if (f->rowY[rr] + f->rowH[rr] > f->scroll + g.height) f->scroll = f->rowY[rr] + f->rowH[rr] - g.height + FD_GAP;
        f->reveal = -1;
    }
    bool onBar = Th_ScrollBar(g, &f->scroll, content, 64, in_input_ok());
    bool inGrid = in_input_ok() && !onBar && CheckCollisionPointRec(m, g);

    f->frame++;
    int firstRow = 0;
    while (firstRow + 1 < f->nRows && f->rowY[firstRow] + f->rowH[firstRow] < f->scroll) firstRow++;
    int budget = FD_TEX_PER_FRAME;
    BeginScissorMode((int)g.x, (int)g.y, (int)g.width, (int)g.height);
    for (int row = firstRow; row < f->nRows && f->rowY[row] - f->scroll <= g.height; row++) {
        float boxH = (float)((f->rowAbove[row] + f->rowBelow[row]) * f->z);
        if (boxH > FD_BOX_MAX_H) boxH = FD_BOX_MAX_H;
        for (int di = f->rowFirst[row]; di < f->rowFirst[row + 1]; di++) {
            int li = f->order[di];
            const StoreCodeGlyph *e = &f->list.g[li];
            Rectangle cr = { floorf(g.x + f->cellX[di]), floorf(g.y + f->rowY[row] - f->scroll), f->cellW[di], f->rowH[row] };
            bool sel = di == f->sel;
            bool hov = inGrid && CheckCollisionPointRec(m, cr);
            Th_Card(cr, (sel ? TH_LR_SELECTED : 0) | (hov ? TH_LR_HOVER : 0));
            Rectangle box = { cr.x + FD_PAD, cr.y + FD_PAD, cr.width - 2 * FD_PAD, boxH };
            float baseY = box.y + box.height - (float)(f->rowBelow[row] * f->z);
            Th_HLine(box.x, box.x + box.width, floorf(baseY), Fade(TH_M_BASELINE, 0.315f));
            if (!f->tex[li].id && f->used[li] != 0xFFFFFFFFu && budget > 0) {
                budget--;
                if (!load_cell_texture(f, li)) f->used[li] = 0xFFFFFFFFu;   /* unreadable: do not retry */
            }
            if (f->used[li] != 0xFFFFFFFFu) f->used[li] = f->frame;
            if (f->tex[li].id) {
                float gw = (float)(e->width * f->z);
                float px = box.x + floorf((box.width - gw) / 2);
                float py = baseY - (float)((e->yTypical + e->height) * f->z);
                Th_DrawTextureClipped(f->tex[li], px, py, f->z, box, WHITE);
            }
            char lab[32];
            snprintf(lab, sizeof lab, "%d font%s", e->nFonts, e->nFonts == 1 ? "" : "s");
            Vector2 lm = Th_MeasureSmall(lab);
            Th_TextSmall(lab, cr.x + floorf((cr.width - lm.x) / 2), cr.y + cr.height - FD_LABEL_H + 1, TH_TEXT_DIM);
            if (hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                double now = GetTime();
                Finder_Select(f, di);
                f->reveal = -1;
                if (f->lastClickIdx == di && now - f->lastClickT < 0.4) {
                    memcpy(f->dblHash, e->glyph, 33);
                    f->dblT = now;
                    f->sideTab = 1;   /* the usage is asked for to find the font to open */
                    f->sideTab = 1;   /* the usage must be asked for to find the font to open */
                    f->lastClickIdx = -1;
                } else { f->lastClickIdx = di; f->lastClickT = now; }
            }
        }
    }
    EndScissorMode();
    /* the textures of cells that scrolled away: keep the cache small */
    int have = 0;
    for (int i = 0; i < f->list.count; i++) if (f->tex[i].id) have++;
    if (have > FD_TEX_KEEP)
        for (int i = 0; i < f->list.count; i++)
            if (f->tex[i].id && f->used[i] + 2 < f->frame) { UnloadTexture(f->tex[i]); memset(&f->tex[i], 0, sizeof f->tex[i]); }
}

bool Finder_UpdateDraw(Finder *f, Rectangle area, InsightTarget *target)
{
    if (!f) return false;
    pull(f);
    bool opened = false;
    bool blocked = Th_InputBlocked();
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (!blocked) {
        if (ctrl && (IsKeyPressed(KEY_G) || IsKeyPressed(KEY_F))) { f->sideTab = 0; Th_TextFocus("finder.search", f->text, true); }
        if (!Th_TextIs("finder.search")) {
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                if (ch == '[' && f->code > 0) set_code(f, f->code - 1, true);
                else if (ch == ']' && f->code >= 0 && f->code < PX_CODE_MAX_CODEPOINT) set_code(f, f->code + 1, true);
            }
        }
    } else while (GetCharPressed() != 0) {}

    float panelW = area.width * 0.3f;
    if (panelW > FD_PANEL_MAX) panelW = FD_PANEL_MAX;
    if (panelW < FD_PANEL_MIN) panelW = FD_PANEL_MIN;
    if (panelW > area.width * 0.5f) panelW = floorf(area.width * 0.5f);
    Rectangle grid = { area.x, area.y, area.width - panelW, area.height };
    Rectangle panel = { area.x + area.width - panelW, area.y, panelW, area.height };

    draw_grid(f, grid);

    /* the side panel: View | Usage (the usage of the selected bitmap comes from the shared Insight) */
    DrawRectangleRec(panel, TH_PANEL);
    Th_VLine(panel.x, panel.y, panel.y + panel.height, TH_LINE);
    {
        static const char *labels[2] = { "View", "Usage" };
        int c = Th_Segmented((Rectangle){ panel.x + FD_PAD_P - 8, panel.y + 8, panel.width - 2 * FD_PAD_P + 16, TH_ROW_H }, &(ThSeg){ labels, 2, f->sideTab, 0u, "fside" });
        if (c >= 0) f->sideTab = c;
        Th_HLine(panel.x + 1, panel.x + panel.width, panel.y + 43, TH_LINE);
    }
    if (f->sideTab == 0) draw_view_tab(f, panel);
    bool clicked = false;
    if (f->insight) {
        Insight_SetPreferredCode(f->insight, f->code);
        if (f->sel >= 0 && f->sel < f->nOrder) {
            char h[1][33];
            memcpy(h[0], f->list.g[f->order[f->sel]].glyph, 33);
            Insight_SetGlyphs(f->insight, (const char (*)[33])h, 1);
        } else Insight_SetGlyphs(f->insight, NULL, 0);
        if (f->sideTab == 1) {
            Rectangle body = { panel.x, panel.y + 44, panel.width, panel.height - 44 };
            Rectangle ia = { body.x, body.y + f->usageScroll + 8, body.width, body.height };
            BeginScissorMode((int)body.x + 1, (int)body.y, (int)body.width - 1, (int)body.height);
            Insight_SetVisible(f->insight, body);
            InsightTarget t = { 0 };
            float used = Insight_Draw(f->insight, ia, INSIGHT_SECTION_USAGE, &clicked, &t);
            EndScissorMode();
            float us = -f->usageScroll;
            Th_ScrollBar(body, &us, used + 8, 48, !blocked);
            f->usageScroll = -us;
            if (clicked && target) { *target = t; opened = true; }
        } else {
            bool cl = false;
            InsightTarget t0 = { 0 };
            Insight_Draw(f->insight, (Rectangle){ panel.x, panel.y, panel.width, 0 }, 0, &cl, &t0);   /* nothing wanted: the queries stop */
        }
        /* a double click on a cell opens its most used font once the usage has arrived */
        if (f->dblHash[0]) {
            InsightTarget bt;
            if (GetTime() - f->dblT > 3.0 || (f->sel >= 0 && strcmp(f->list.g[f->order[f->sel]].glyph, f->dblHash))) f->dblHash[0] = 0;
            else if (Insight_BestUsage(f->insight, f->dblHash, &bt)) {
                f->dblHash[0] = 0;
                if (target) { *target = bt; opened = true; }
            }
        }
    }
    return opened;
}
