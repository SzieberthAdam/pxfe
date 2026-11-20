/* theme.c - shared look of pxfe: fonts, crisp drawing helpers (see DESIGN.md). */
#include "theme.h"

#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "rlgl.h"
#include "pxfont.h"

#define TH_FONT_UI_PATH    "assets/fonts/noto-sans/NotoSans-Regular.ttf"
#define TH_FONT_SEMI_PATH  "assets/fonts/noto-sans/NotoSans-SemiBold.ttf"
#define TH_FONT_UNI_PATH   "assets/fonts/unifont/unifont-18.0.01.otf"
#define TH_FONT_UNIUP_PATH "assets/fonts/unifont/unifont_upper-18.0.01.otf"
#define TH_NOTO_RATIO      1.362f   /* (ascender 1069 + descender 293) / 1000: raylib's size per em */
#define TH_NOTO_ASC        1.069f

#define TH_BUTTON_SEGMENTS 8

Theme theme = { 0 };

/* Code points loaded for every theme font, as inclusive ranges. */
static const int th_glyph_ranges[][2] = {
    { 0x0020, 0x007E },   /* ASCII */
    { 0x00A0, 0x017F },   /* Latin-1 Supplement + Latin Extended-A (includes 0x00D7 multiplication sign) */
    { 0x2013, 0x2014 },   /* en dash, em dash */
    { 0x2018, 0x2019 },   /* single quotes */
    { 0x201C, 0x201D },   /* double quotes */
    { 0x2026, 0x2026 },   /* ellipsis */
};

#define TH_RANGE_COUNT (sizeof(th_glyph_ranges) / sizeof(th_glyph_ranges[0]))
#define TH_MAX_CODEPOINTS 512

static int th_codepoints[TH_MAX_CODEPOINTS];
static int th_codepoint_count = 0;

static void th_build_codepoints(void)
{
    if (th_codepoint_count > 0) return;

    for (size_t i = 0; i < TH_RANGE_COUNT; i++) {
        for (int cp = th_glyph_ranges[i][0]; cp <= th_glyph_ranges[i][1]; cp++) {
            if (th_codepoint_count < TH_MAX_CODEPOINTS) th_codepoints[th_codepoint_count++] = cp;
        }
    }
}

/* The path of an asset: assets/ next to the exe, two folders up (tests	mp), or in the cwd; NULL (and the name in theme.missing) when absent. */
static const char *th_asset(const char *rel)
{
    static char buf[600];
    const char *app = GetApplicationDirectory(), *pre[] = { "", "../../", "" };
    for (int i = 0; i < 3; i++) {
        snprintf(buf, sizeof buf, "%s%s%s", i < 2 ? app : "", pre[i], rel);
        if (FileExists(buf)) return buf;
    }
    size_t l = strlen(theme.missing);
    snprintf(theme.missing + l, sizeof theme.missing - l, "%s%s", l ? ", " : "", strrchr(rel, '/') + 1);
    return NULL;
}

/* A Noto Sans face with the theme code points and bilinear filtering; the raylib default font when the file is missing or fails.
 * `em` is the type size in px; raylib's size is the height from ascent to descent, so it is em * TH_NOTO_RATIO. */
static Font th_load_font(const char *rel, int em)
{
    Font font = { 0 };
    const char *path = th_asset(rel);
    if (path) font = LoadFontEx(path, (int)((float)em * TH_NOTO_RATIO + 0.5f), th_codepoints, th_codepoint_count);
    if ((font.texture.id == 0) || (font.glyphCount == 0)) return GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
}

/* Unifont: the two files in memory, an atlas of 32 x 32 slots (a glyph is at most 16 x 16 plus marks) and the glyphs rasterised so far.
 * LoadFontData (stb_truetype, CFF outlines) at 16 px gives 0 / 255 coverage only: the outlines lie on the pixel grid. */
#define UF_SLOT 32
#define UF_COLS 32
#define UF_MAX  (UF_COLS * UF_COLS)
#define UF_HASH 4096
typedef struct { int32_t cp; short ox, oy, w, h, adv, slot; bool show; char form; } ThUfG;
static struct {
    unsigned char *file[2];
    int size[2], n;
    Texture2D tex;
    ThUfG g[UF_MAX];
    short hash[UF_HASH];
    bool ok;
} uf;

/* LoadFontData of one code point; raylib warns for a code point the file lacks, which is no error here */
static GlyphInfo *uf_rasterise(int f, int *cp, int *n)
{
    SetTraceLogLevel(LOG_ERROR);
    GlyphInfo *gi = uf.file[f] ? LoadFontData(uf.file[f], uf.size[f], 16, cp, 1, FONT_DEFAULT, n) : NULL;
    SetTraceLogLevel(LOG_WARNING);
    return gi;
}

static bool uf_mask(const GlyphInfo *g, unsigned char m[UF_SLOT * UF_SLOT])
{
    memset(m, 0, UF_SLOT * UF_SLOT);
    if (!g->image.data || g->image.width > UF_SLOT || g->image.height > UF_SLOT) return false;
    const unsigned char *p = g->image.data;
    for (int y = 0; y < g->image.height; y++) for (int x = 0; x < g->image.width; x++) m[y * UF_SLOT + x] = p[y * g->image.width + x] > 127;
    return true;
}

static void uf_flush(void)
{
    rlDrawRenderBatchActive();   /* quads of the batch refer to the old slots */
    uf.n = 0;
    memset(uf.hash, 0xFF, sizeof uf.hash);
}

static const ThUfG *uf_get(int32_t cp)
{
    if (!uf.ok) return NULL;
    unsigned h = ((unsigned)cp * 2654435761u) >> 20;
    for (int pass = 0; pass < 2; pass++) {
        while (uf.hash[h] >= 0) { if (uf.g[uf.hash[h]].cp == cp) return &uf.g[uf.hash[h]]; h = (h + 1) & (UF_HASH - 1); }
        if (uf.n < UF_MAX) break;
        uf_flush();
        h = ((unsigned)cp * 2654435761u) >> 20;
    }
    int f = cp >= 0x10000, n = 0;
    ThUfG *e = &uf.g[uf.n];
    *e = (ThUfG){ .cp = cp, .slot = (short)uf.n };
    GlyphInfo *gi = uf_rasterise(f, &cp, &n);
    /* raylib answers n == 0 (and glyphs that are not initialised) for a code point the file lacks: nothing of it may be read or freed */
    if (gi && n == 1 && gi[0].image.data) {
        unsigned char m[UF_SLOT * UF_SLOT];
        bool has = uf_mask(&gi[0], m);
        int ink = 0;
        for (int i = 0; i < UF_SLOT * UF_SLOT; i++) ink += m[i];
        e->adv = (short)((gi[0].advanceX + 4) / 8 * 8);
        e->ox = (short)gi[0].offsetX; e->oy = (short)gi[0].offsetY; e->w = (short)gi[0].image.width; e->h = (short)gi[0].image.height;
        if (has && ink) {
            unsigned char px[UF_SLOT * UF_SLOT * 4];
            for (int i = 0; i < UF_SLOT * UF_SLOT; i++) { px[4 * i] = px[4 * i + 1] = px[4 * i + 2] = 255; px[4 * i + 3] = m[i] ? 255 : 0; }
            UpdateTextureRec(uf.tex, (Rectangle){ (float)(uf.n % UF_COLS * UF_SLOT), (float)(uf.n / UF_COLS * UF_SLOT), UF_SLOT, UF_SLOT }, px);
            /* shows: has ink, a width and is not a control */
            e->show = e->adv > 0 && cp > 0x20 && !(cp >= 0x7F && cp <= 0xA0) && cp != 0xAD;
        }
    }
    if (gi) { if (n == 1) UnloadFontData(gi, 1); else MemFree(gi); }
    if (!e->adv) e->adv = cp == 0x3000 ? 16 : 8;
    uf.hash[h] = (short)uf.n++;
    return e;
}

static void uf_load(void)
{
    static const char *rel[2] = { TH_FONT_UNI_PATH, TH_FONT_UNIUP_PATH };
    for (int i = 0; i < 2; i++) {
        const char *p = th_asset(rel[i]);
        if (p) uf.file[i] = LoadFileData(p, &uf.size[i]);
    }
    if (!uf.file[0] && !uf.file[1]) return;
    Image im = GenImageColor(UF_COLS * UF_SLOT, UF_COLS * UF_SLOT, BLANK);
    uf.tex = LoadTextureFromImage(im);
    UnloadImage(im);
    SetTextureFilter(uf.tex, TEXTURE_FILTER_POINT);
    uf.ok = uf.tex.id != 0;
    uf_flush();
}

/* Integer rectangle from a float rectangle (snapped to the pixel grid). */
static Rectangle th_snap(Rectangle r)
{
    return (Rectangle){ floorf(r.x), floorf(r.y), floorf(r.width), floorf(r.height) };
}

static Vector2 th_snap_pos(float x, float y)
{
    return (Vector2){ roundf(x), roundf(y) };
}

void Theme_Load(void)
{
    if (theme.loaded) Theme_Unload();

    th_build_codepoints();

    theme.missing[0] = 0;
    theme.ui = th_load_font(TH_FONT_UI_PATH, TH_SIZE_BODY);
    theme.head = th_load_font(TH_FONT_SEMI_PATH, TH_SIZE_HEAD);
    theme.cap = th_load_font(TH_FONT_SEMI_PATH, TH_SIZE_CAP);
    theme.small = th_load_font(TH_FONT_UI_PATH, TH_SIZE_SMALL);
    uf_load();
    theme.loaded = true;

}

void Theme_Unload(void)
{
    Font def = GetFontDefault();
    Font *fonts[] = { &theme.ui, &theme.head, &theme.cap, &theme.small };

    /* Only fonts loaded from files are unloaded; the default font is shared. */
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        if (fonts[i]->texture.id != 0 && fonts[i]->texture.id != def.texture.id) UnloadFont(*fonts[i]);
        *fonts[i] = (Font){ 0 };
    }

    if (uf.tex.id) UnloadTexture(uf.tex);
    UnloadFileData(uf.file[0]); UnloadFileData(uf.file[1]);
    uf.ok = false; uf.tex = (Texture2D){ 0 }; uf.file[0] = uf.file[1] = NULL;
    theme.loaded = false;
}

/* Text helpers */

static void th_draw(const Font *f, const char *text, float x, float y, Color c) { DrawTextEx(*f, text, th_snap_pos(x, y), (float)f->baseSize, 0, c); }
static Vector2 th_measure(const Font *f, const char *text) { return MeasureTextEx(*f, text, (float)f->baseSize, 0); }

void Th_Text(const char *text, float x, float y, Color c) { th_draw(&theme.ui, text, x, y, c); }
void Th_TextHead(const char *text, float x, float y, Color c) { th_draw(&theme.head, text, x, y, c); }
void Th_TextSmall(const char *text, float x, float y, Color c) { th_draw(&theme.small, text, x, y, c); }
Vector2 Th_Measure(const char *text) { return th_measure(&theme.ui, text); }
Vector2 Th_MeasureHead(const char *text) { return th_measure(&theme.head, text); }
float Th_BesideHead(float headY) { return headY + roundf(TH_NOTO_ASC * TH_SIZE_HEAD) - roundf(TH_NOTO_ASC * TH_SIZE_BODY); }
Vector2 Th_MeasureSmall(const char *text) { return th_measure(&theme.small, text); }

/* Unifont text and glyph captions */

static bool th_hover(Rectangle r);

static void uf_draw(const ThUfG *g, float x, float y, int scale, Color c)
{
    if (g->w <= 0 || g->h <= 0 || !(g->w <= UF_SLOT && g->h <= UF_SLOT)) return;
    Rectangle src = { (float)(g->slot % UF_COLS * UF_SLOT), (float)(g->slot / UF_COLS * UF_SLOT), (float)g->w, (float)g->h };
    DrawTexturePro(uf.tex, src, (Rectangle){ x + g->ox * scale, y + g->oy * scale, (float)(g->w * scale), (float)(g->h * scale) }, (Vector2){ 0, 0 }, 0, c);
}

static float th_uni(const char *text, float x, float y, Color c, int scale, bool draw)
{
    if (!uf.ok) {   /* no Unifont: the small face (the default font when that is missing too) */
        if (draw) th_draw(&theme.small, text, x, y, c);
        return th_measure(&theme.small, text).x;
    }
    float x0 = roundf(x), y0 = roundf(y), w = 0;
    for (int i = 0; text[i];) {
        int sz = 0, cp = GetCodepoint(text + i, &sz);
        i += sz > 0 ? sz : 1;
        const ThUfG *g = uf_get(cp);
        if (!g) continue;
        if (draw) uf_draw(g, x0 + w, y0, scale, c);
        w += g->adv * scale;
    }
    return w;
}
void  Th_TextUni(const char *text, float x, float y, Color c) { th_uni(text, x, y, c, 1, true); }
float Th_MeasureUni(const char *text) { return th_uni(text, 0, 0, WHITE, 1, false); }

bool Th_CodeVisible(int32_t cp)
{
    const ThUfG *g = cp >= 0 && cp <= PX_CODE_MAX_CODEPOINT ? uf_get(cp) : NULL;
    return g && g->show;
}

float Th_CodeW(PxCode code) { char t[24]; px_code_label(code, t, sizeof t); return th_uni(t, 0, 0, WHITE, 1, false); }

void Th_CodeDraw(PxCode code, Rectangle area, Color c, bool hover, int scale)
{
    char t[24];
    /* the character's image only for the glyph of the code point itself; a variant is told by its label */
    ThUfG *g = px_code_is_codepoint(code) && px_code_variant(code) == 0 ? (ThUfG *)uf_get(px_code_codepoint(code)) : NULL;
    bool img = g && g->show && !hover;
    if (g) g->form = img ? 1 : 2;
    px_code_label(code, t, sizeof t);
    float w = img ? (float)(g->adv * scale) : th_uni(t, 0, 0, WHITE, scale, false), h = (float)(TH_UNI_H * scale);
    float x = floorf(area.x + (area.width - w) / 2), y = floorf(area.y + (area.height - h) / 2);
    if (img) uf_draw(g, x, y, scale, c);
    else th_uni(t, x, y, c, scale, true);
}

int Th_CodeForm(int32_t cp) { const ThUfG *g = cp >= 0 && cp <= PX_CODE_MAX_CODEPOINT ? uf_get(cp) : NULL; return g ? g->form : 0; }

/* Text with "U+XXXX" tokens: the length of the token at p (0: none) */
static int th_tok(const char *p)
{
    if (p[0] != 'U' || p[1] != '+') return 0;
    int n = 2;
    while (n < 8 && isxdigit((unsigned char)p[n])) n++;
    return n >= 6 ? n : 0;
}

static float th_rich(int face, const char *text, float x, float y, Color c, int hov, bool draw)
{
    const Font *f = face == TH_F_SMALL ? &theme.small : face == TH_F_HEAD ? &theme.head : &theme.ui;
    float lh = th_measure(f, "A").y, w = 0;
    char seg[512];
    for (const char *p = text; *p;) {
        size_t k = 0;
        int tl = 0;
        while (p[k] && !(tl = th_tok(p + k)) && k < sizeof seg - 1) k++;
        memcpy(seg, p, k);
        seg[k] = 0;
        if (k) { if (draw) th_draw(f, seg, x + w, y, c); w += th_measure(f, seg).x; }
        p += k;
        if (tl) {
            char tk[10];
            snprintf(tk, sizeof tk, "%.*s", tl, p);
            int32_t cp = (int32_t)strtol(tk + 2, NULL, 16);
            const ThUfG *g = Th_CodeVisible(cp) ? uf_get(cp) : NULL;
            float slot = th_uni(tk, 0, 0, WHITE, 1, false);
            bool hv = hov < 0 ? th_hover((Rectangle){ x + w, y, slot, lh }) : hov > 0, shown = g && !hv;
            Rectangle a = { x + w, y, shown ? (float)g->adv : slot, lh };   /* the character sits at the left of its slot */
            if (draw) Th_CodeDraw(px_code_cp(cp), a, c, hv, 1);
            w += hov == 0 ? a.width : slot;   /* hov 0 (a label centred by its caller): the width as drawn; else the slot, so that hovering moves nothing */
            p += tl;
        }
    }
    return w;
}
float Th_MeasureRich(const char *text, int face) { return th_rich(face, text, 0, 0, WHITE, 1, false); }
float Th_TextRich(const char *text, float x, float y, Color c, int face, int hov) { return th_rich(face, text, roundf(x), roundf(y), c, hov, true); }

/* Crisp lines */

void Th_HLine(float x0, float x1, float y, Color c)
{
    float xa = floorf(x0 < x1 ? x0 : x1);
    float xb = floorf(x0 < x1 ? x1 : x0);
    float yy = floorf(y);

    if (xb > xa) DrawRectangle((int)xa, (int)yy, (int)(xb - xa), 1, c);
}

void Th_VLine(float x, float y0, float y1, Color c)
{
    float ya = floorf(y0 < y1 ? y0 : y1);
    float yb = floorf(y0 < y1 ? y1 : y0);
    float xx = floorf(x);

    if (yb > ya) DrawRectangle((int)xx, (int)ya, 1, (int)(yb - ya), c);
}

void Th_DashedHLine(float x0, float x1, float y, float dash, Color c)
{
    float xa = floorf(x0 < x1 ? x0 : x1);
    float xb = floorf(x0 < x1 ? x1 : x0);

    if (dash <= 0.0f) {
        Th_HLine(xa, xb, y, c);
        return;
    }

    for (float x = xa; x < xb; x += 2.0f * dash) {
        float end = x + dash;
        if (end > xb) end = xb;
        Th_HLine(x, end, y, c);
    }
}

void Th_DashedVLine(float x, float y0, float y1, float dash, Color c)
{
    float ya = floorf(y0 < y1 ? y0 : y1);
    float yb = floorf(y0 < y1 ? y1 : y0);

    if (dash <= 0.0f) {
        Th_VLine(x, ya, yb, c);
        return;
    }

    for (float y = ya; y < yb; y += 2.0f * dash) {
        float end = y + dash;
        if (end > yb) end = yb;
        Th_VLine(x, y, end, c);
    }
}

void Th_RectLines(Rectangle r, Color c)
{
    int x0 = (int)floorf(r.x);
    int y0 = (int)floorf(r.y);
    int x1 = (int)floorf(r.x + r.width);
    int y1 = (int)floorf(r.y + r.height);

    if ((x1 - x0) < 1 || (y1 - y0) < 1) return;

    DrawRectangle(x0, y0, x1 - x0, 1, c);             /* top */
    DrawRectangle(x0, y1 - 1, x1 - x0, 1, c);         /* bottom */
    DrawRectangle(x0, y0, 1, y1 - y0, c);             /* left */
    DrawRectangle(x1 - 1, y0, 1, y1 - y0, c);         /* right */
}

/* Buttons */

static float th_roundness(Rectangle r)
{
    float side = (r.width < r.height) ? r.width : r.height;
    return (side > 0.0f) ? (2.0f * TH_RADIUS) / side : 0.0f;
}

static void th_draw_rounded_button(Rectangle r, Color fill, Color border)
{
    float roundness = th_roundness(r);

    DrawRectangleRounded(r, roundness, TH_BUTTON_SEGMENTS, fill);
    DrawRectangleRoundedLinesEx(r, roundness, TH_BUTTON_SEGMENTS, 1.0f, border);
}

static void th_draw_label(Rectangle r, const char *label, Color c)
{
    Vector2 size = Th_Measure(label);
    float x = r.x + (r.width - size.x) * 0.5f;
    float y = r.y + (r.height - size.y) * 0.5f;

    Th_Text(label, x, y, c);
}

static bool th_blocked = false, th_mouse_blocked = false;
static const char *th_tip;
static Rectangle th_tip_r;
void Th_SetInputBlocked(bool b) { th_blocked = b; }
bool Th_InputBlocked(void) { return th_blocked; }
void Th_SetMouseBlocked(bool b) { th_mouse_blocked = b; }
bool Th_MouseBlocked(void) { return th_blocked || th_mouse_blocked; }
bool Th_MouseOnly(void) { return th_mouse_blocked; }

/* The control the left button went down on (a release fires only the control that was pressed): a hash of its id and its rectangle. */
static unsigned th_press_key;
static bool th_press_set;
bool Th_Released(Rectangle r, const char *id, bool hover)
{
    if (!hover) return false;
    unsigned k = 2166136261u;
    for (const char *p = id; p && *p; p++) k = (k ^ (unsigned char)*p) * 16777619u;
    k = (k ^ (unsigned)(int)r.x) * 16777619u; k = (k ^ (unsigned)(int)r.y) * 16777619u;
    k = (k ^ (unsigned)(int)r.width) * 16777619u; k = (k ^ (unsigned)(int)r.height) * 16777619u;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { th_press_key = k; th_press_set = true; }
    return IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && th_press_set && th_press_key == k;
}

/* the scrolled views of this frame, for Th_ScrollInto (the e2e player brings a widget below the fold into view) */
static struct { Rectangle v; float *s; } th_scr[16];
static int th_scr_n;

#define TH_REG_MAX 384
static struct { char name[64]; Rectangle r; } th_reg[TH_REG_MAX];
static int th_reg_n = 0;

static struct { char name[64], buf[4096], old[4096]; int cur, anc, start; bool seen, drag; double clickT; } th_ed;   /* the text field that has the keyboard; between anc and cur is selected */

static bool th_wheel_used;
bool Th_WheelUsed(void) { return th_wheel_used; }

static bool th_menu_prev, th_menu_cur;   /* a menu was open at the end of the last frame / is open this frame (Th_MenuKeys) */
void Th_RegClear(void) { if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) th_press_set = false; th_menu_prev = th_menu_cur; th_menu_cur = false; th_reg_n = 0; th_tip = NULL; th_wheel_used = false; th_scr_n = 0; if (!th_ed.seen) th_ed.name[0] = 0; th_ed.seen = false; }

void Th_Reg(const char *name, Rectangle r)
{
    if (th_reg_n >= TH_REG_MAX) return;
    snprintf(th_reg[th_reg_n].name, sizeof th_reg[0].name, "%s", name);
    th_reg[th_reg_n].r = r;
    th_reg_n++;
}

void Th_RegLabel(const char *label, Rectangle r)
{
    char n[64];
    snprintf(n, sizeof n, "%s", label);
    char *cut = strstr(n, "  ");   /* a hint after two spaces ("Undo  (Ctrl+Z)") is not part of the name */
    if (cut) *cut = 0;
    for (char *p = n; *p; p++) if (*p == ' ') *p = '_';
    Th_Reg(n, r);
}

bool Th_RegFind(const char *name, Rectangle *r)
{
    for (int i = th_reg_n - 1; i >= 0; i--) {
        const char *a = th_reg[i].name, *b = name;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (*a == 0 && *b == 0) { *r = th_reg[i].r; return true; }
    }
    return false;
}

static Color th_mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

static bool th_hover(Rectangle r) { return !th_blocked && !th_mouse_blocked && (Th_HoverFn ? Th_HoverFn(r) : CheckCollisionPointRec(GetMousePosition(), r)); }

/* Hint of the hovered button (set by Th_Button, read by the view's hint line or tooltip); cleared by Th_RegClear. */
const char *Th_Tip(Rectangle *r) { if (r) *r = th_tip_r; return th_tip; }

static void th_tri(Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, c, b, col); DrawTriangle(a, b, c, col); }

/* The icons of the icon buttons, centred in r. */
void Th_Icon(int ic, Rectangle r, Color c)
{
    float x = (float)(int)(r.x + r.width / 2), y = (float)(int)(r.y + r.height / 2);
    switch (ic) {
    case TH_IC_PENCIL: DrawLineEx((Vector2){x - 5, y + 5}, (Vector2){x + 4, y - 4}, 2.5f, c); DrawRectangle((int)x + 4, (int)y - 7, 3, 3, c); DrawRectangle((int)x - 7, (int)y + 5, 3, 2, c); break;
    case TH_IC_ERASER: DrawRectangleLinesEx((Rectangle){x - 6, y - 4, 12, 9}, 1, c); DrawRectangle((int)x - 6, (int)y + 1, 12, 4, c); break;
    case TH_IC_LINE: DrawLineEx((Vector2){x - 6, y + 6}, (Vector2){x + 6, y - 6}, 1.5f, c); DrawRectangle((int)x - 8, (int)y + 4, 3, 3, c); DrawRectangle((int)x + 5, (int)y - 7, 3, 3, c); break;
    case TH_IC_RECT: DrawRectangleLinesEx((Rectangle){x - 6, y - 5, 12, 10}, 1.5f, c); break;
    case TH_IC_FILL: DrawRectangleLinesEx((Rectangle){x - 6, y - 3, 11, 9}, 1.5f, c); DrawRectangle((int)x - 5, (int)y + 1, 9, 4, c); DrawLineEx((Vector2){x - 2, y - 3}, (Vector2){x + 2, y - 7}, 1.5f, c); DrawCircle((int)x + 7, (int)y + 4, 1.8f, c); break;
    case TH_IC_PREV: th_tri((Vector2){x + 3, y - 5}, (Vector2){x - 3, y}, (Vector2){x + 3, y + 5}, c); break;
    case TH_IC_NEXT: th_tri((Vector2){x - 3, y - 5}, (Vector2){x + 3, y}, (Vector2){x - 3, y + 5}, c); break;
    case TH_IC_LEFT: th_tri((Vector2){x + 3, y - 4}, (Vector2){x - 3, y}, (Vector2){x + 3, y + 4}, c); break;
    case TH_IC_RIGHT: th_tri((Vector2){x - 3, y - 4}, (Vector2){x + 3, y}, (Vector2){x - 3, y + 4}, c); break;
    case TH_IC_UP: th_tri((Vector2){x - 4, y + 3}, (Vector2){x, y - 3}, (Vector2){x + 4, y + 3}, c); break;
    case TH_IC_DOWN: th_tri((Vector2){x - 4, y - 3}, (Vector2){x, y + 3}, (Vector2){x + 4, y - 3}, c); break;
    case TH_IC_CLOSE: DrawLineEx((Vector2){x - 4, y - 4}, (Vector2){x + 4, y + 4}, 1.6f, c); DrawLineEx((Vector2){x + 4, y - 4}, (Vector2){x - 4, y + 4}, 1.6f, c); break;
    case TH_IC_EYE: case TH_IC_EYE_OFF:
        DrawEllipseLines((int)x, (int)y, 7, 4, c); DrawCircle((int)x, (int)y, 2.2f, c);
        if (ic == TH_IC_EYE_OFF) DrawLineEx((Vector2){x - 7, y + 6}, (Vector2){x + 7, y - 6}, 1.5f, c);
        break;
    case TH_IC_CHEV_DOWN: DrawLineEx((Vector2){x - 4, y - 2}, (Vector2){x, y + 2}, 1.4f, c); DrawLineEx((Vector2){x, y + 2}, (Vector2){x + 4, y - 2}, 1.4f, c); break;
    case TH_IC_SEARCH: DrawCircleLines((int)x - 1, (int)y - 1, 5, c); DrawLineEx((Vector2){x + 3, y + 3}, (Vector2){x + 7, y + 7}, 1.5f, c); break;
    case TH_IC_LOCK: case TH_IC_UNLOCK:   /* the body, and the shackle closed or opened */
        DrawRectangle((int)x - 5, (int)y - 1, 10, 8, c);
        DrawRectangleLinesEx((Rectangle){x - 3, y - 7, 6, 7}, 1.5f, c);
        if (ic == TH_IC_UNLOCK) DrawRectangle((int)x + 1, (int)y - 3, 4, 3, TH_PANEL);
        break;
    case TH_IC_MARQUEE:   /* a dashed rectangle */
        for (int i = 0; i < 12; i += 4) {
            DrawRectangle((int)x - 6 + i, (int)y - 5, 2, 1, c); DrawRectangle((int)x - 6 + i, (int)y + 5, 2, 1, c);
        }
        for (int i = 0; i < 10; i += 4) {
            DrawRectangle((int)x - 6, (int)y - 5 + i, 1, 2, c); DrawRectangle((int)x + 5, (int)y - 5 + i, 1, 2, c);
        }
        break;
    case TH_IC_LASSO:   /* a loop with a tail */
        DrawEllipseLines((int)x, (int)y - 2, 6, 4, c);
        DrawLineEx((Vector2){x - 3, y + 1}, (Vector2){x - 5, y + 6}, 1.4f, c);
        DrawLineEx((Vector2){x - 5, y + 6}, (Vector2){x - 1, y + 7}, 1.4f, c);
        break;
    case TH_IC_WAND:   /* a stick with a spark */
        DrawLineEx((Vector2){x - 6, y + 6}, (Vector2){x + 1, y - 1}, 2.2f, c);
        DrawLineEx((Vector2){x + 4, y - 7}, (Vector2){x + 4, y - 1}, 1.2f, c);
        DrawLineEx((Vector2){x + 1, y - 4}, (Vector2){x + 7, y - 4}, 1.2f, c);
        break;
    case TH_IC_ELLIPSE: DrawEllipseLines((int)x, (int)y, 7, 5, c); break;
    case TH_IC_PICK:   /* a dropper: bulb, tube and tip */
        DrawCircleV((Vector2){x + 4, y - 4}, 3.2f, c);
        DrawLineEx((Vector2){x + 2, y - 2}, (Vector2){x - 4, y + 4}, 3.4f, c);
        DrawLineEx((Vector2){x - 4, y + 4}, (Vector2){x - 7, y + 7}, 1.4f, c);
        break;
    case TH_IC_POLY:   /* an irregular polygon with its corners marked */
        {
            Vector2 p[5] = {{x - 6, y + 4}, {x - 4, y - 5}, {x + 3, y - 6}, {x + 7, y + 1}, {x + 1, y + 6}};
            for (int i = 0; i < 5; i++) { DrawLineEx(p[i], p[(i + 1) % 5], 1.3f, c); DrawRectangle((int)p[i].x - 1, (int)p[i].y - 1, 3, 3, c); }
        }
        break;
    case TH_IC_CONTOUR:   /* a wobbly hand-drawn loop */
        DrawSplineCatmullRom((Vector2[9]){{x - 4, y + 5}, {x - 7, y + 1}, {x - 4, y - 5}, {x + 2, y - 6}, {x + 7, y - 1}, {x + 3, y + 5}, {x - 4, y + 5}, {x - 7, y + 1}, {x - 4, y - 5}}, 9, 1.4f, c);
        break;
    case TH_IC_CURVE: DrawSplineBezierCubic((Vector2[4]){{x - 6, y + 6}, {x - 4, y - 10}, {x + 4, y + 10}, {x + 6, y - 6}}, 4, 1.5f, c); DrawRectangle((int)x - 8, (int)y + 4, 3, 3, c); DrawRectangle((int)x + 5, (int)y - 7, 3, 3, c); break;
    case TH_IC_MOVE:   /* four arrows */
        DrawLineEx((Vector2){x - 6, y}, (Vector2){x + 6, y}, 1.3f, c);
        DrawLineEx((Vector2){x, y - 6}, (Vector2){x, y + 6}, 1.3f, c);
        th_tri((Vector2){x - 3, y - 4}, (Vector2){x, y - 7}, (Vector2){x + 3, y - 4}, c);
        th_tri((Vector2){x - 3, y + 4}, (Vector2){x + 3, y + 4}, (Vector2){x, y + 7}, c);
        th_tri((Vector2){x - 4, y - 3}, (Vector2){x - 7, y}, (Vector2){x - 4, y + 3}, c);
        th_tri((Vector2){x + 4, y - 3}, (Vector2){x + 4, y + 3}, (Vector2){x + 7, y}, c);
        break;
    case TH_IC_CHEV_RIGHT: DrawLineEx((Vector2){x - 2, y - 4}, (Vector2){x + 2, y}, 1.4f, c); DrawLineEx((Vector2){x + 2, y}, (Vector2){x - 2, y + 4}, 1.4f, c); break;
    }
}

float Th_ButtonW(const char *label) { return floorf(Th_Measure(label).x) + 2 * TH_PAD; }

bool Th_Button(Rectangle r, const char *label, int flags, const char *hint)
{
    Rectangle s = th_snap(r);
    int icon = flags >> 8;
    bool off = flags & TH_BTN_DISABLED, danger = flags & TH_BTN_DANGER, act = flags & TH_BTN_ACTIVE;
    bool over = th_hover(s), hover = over && !off, down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color hue = danger ? TH_DANGER : TH_ACCENT, tint = th_mix(TH_PANEL, hue, danger ? 0.08f : 0.15f);
    Color fill = icon ? BLANK : TH_PANEL_ALT, line = icon ? BLANK : TH_LINE, ink = off ? TH_TEXT_OFF : danger ? TH_DANGER : TH_TEXT;
    Th_RegLabel(label, s);
    if (flags & TH_BTN_PRIMARY) { fill = line = th_mix(hue, off ? TH_PANEL : BLACK, off ? 0.6f : down ? 0.22f : hover ? 0.1f : 0); ink = TH_TEXT_ON; }
    else if (act) { th_draw_rounded_button(s, TH_PANEL, BLANK); fill = tint; line = hue; ink = hue; }
    else if (down) { fill = danger ? tint : TH_PRESSED_FILL; line = hue; ink = hue; }
    else if (hover) { fill = icon ? TH_HOVER : TH_PANEL; line = icon ? BLANK : danger ? hue : TH_HOVER_BORDER; }
    else if (danger && !off && !icon) line = th_mix(TH_PANEL, hue, 0.4f);
    th_draw_rounded_button(s, fill, line);
    if (icon) Th_Icon(icon - 1, s, ink); else th_draw_label(s, label, ink);
    if (over && hint) { th_tip = hint ? hint : label; th_tip_r = s; }
    return Th_Released(s, label, hover);
}

bool Th_Toggle(Rectangle r, const char *label, bool *on, int flags, const char *hint)
{
    bool c = Th_Button(r, label, flags | (*on ? TH_BTN_ACTIVE : 0), hint);
    if (c) *on = !*on;
    return c;
}

int Th_Segmented(Rectangle r, const ThSeg *g)
{
    Rectangle s = th_snap(r);
    th_draw_rounded_button(s, TH_TRACK, TH_TRACK);
    float total = 0, x = s.x, acc = 0;
    for (int i = 0; i < g->n; i++) total += g->weights ? g->weights[i] : 1.0f;
    int clicked = -1;
    for (int i = 0; i < g->n; i++) {
        acc += g->weights ? g->weights[i] : 1.0f;
        float x1 = floorf(s.x + s.width * acc / total);
        Rectangle seg = { x, s.y, x1 - x, s.height };
        x = x1;
        char nm[64];
        snprintf(nm, sizeof nm, "%s_%s", g->prefix, g->labels[i]);
        Th_RegLabel(nm, seg);
        snprintf(nm, sizeof nm, "%s.%d", g->prefix, i);
        Th_Reg(nm, seg);
        bool dis = (g->disabled >> i) & 1u, over = th_hover(seg), hover = !dis && over;
        Rectangle in = { seg.x + 2, seg.y + 2, seg.width - 4, seg.height - 4 };
        Color tc = dis ? TH_TEXT_OFF : TH_TEXT_DIM;
        if (!dis && i == g->sel) { th_draw_rounded_button(in, TH_PANEL, TH_LINE); tc = TH_ACCENT; }
        else if (hover) { th_draw_rounded_button(in, th_mix(TH_TRACK, WHITE, 0.6f), BLANK); tc = TH_TEXT; }
        if (g->rich) {
            float lw = th_rich(TH_F_BODY, g->labels[i], 0, 0, tc, over, false), lh = Th_Measure("A").y;
            th_rich(TH_F_BODY, g->labels[i], seg.x + (seg.width - lw) * 0.5f, seg.y + (seg.height - lh) * 0.5f, tc, over, true);
        } else th_draw_label(seg, g->labels[i], tc);
        if (over && g->hints && g->hints[i]) { th_tip = g->hints[i]; th_tip_r = seg; }
        if (Th_Released(seg, nm, hover)) clicked = i;
    }
    return clicked;
}

/* A caption: semibold, dim, upper case. */
static const char *th_upper(const char *t, char *out, size_t n)
{
    size_t i = 0;
    for (; t[i] && i + 1 < n; i++) out[i] = (char)toupper((unsigned char)t[i]);
    out[i] = 0;
    return out;
}

void Th_TextCap(const char *text, float x, float y, Color c) { char up[96]; th_draw(&theme.cap, th_upper(text, up, sizeof up), x, y, c); }
Vector2 Th_MeasureCap(const char *text) { char up[96]; return th_measure(&theme.cap, th_upper(text, up, sizeof up)); }

/* Surfaces of lists: a row (plate) and a card (bordered).  The caller draws the content and takes the hover from its own geometry (scrolled lists). */
Color Th_ListRow(Rectangle r, int f)
{
    r = th_snap(r);
    if (f & TH_LR_DISABLED) return TH_PANEL;
    if (f & TH_LR_SELECTED) {
        DrawRectangleRec(r, TH_SELECTED_FILL);
        DrawRectangle((int)r.x, (int)r.y, 3, (int)r.height, TH_ACCENT);
        return TH_SELECTED_FILL;
    }
    if (f & TH_LR_HOVER) DrawRectangleRec(r, TH_HOVER);
    return TH_PANEL;
}

Color Th_Card(Rectangle r, int f)
{
    Color fill = f & TH_LR_SELECTED ? TH_SELECTED_FILL : f & TH_LR_DISABLED ? TH_PANEL_ALT : TH_PANEL;
    Color line = f & TH_LR_SELECTED ? TH_ACCENT : f & TH_LR_DISABLED ? TH_LINE_SOFT : f & TH_LR_HOVER ? TH_HOVER_BORDER : f & TH_LR_WITHIN ? Fade(TH_ACCENT, 0.47f) : TH_LINE;
    th_draw_rounded_button(th_snap(r), fill, line);
    return fill;
}

/* The fold state of the section captions: the titles that are closed (every caption folds; the state lasts for the session). */
#define TH_FOLD_MAX 64
static char th_fold[TH_FOLD_MAX][48];
static int th_fold_n;
static int th_fold_find(const char *t) { for (int i = 0; i < th_fold_n; i++) if (!strcmp(th_fold[i], t)) return i; return -1; }
bool Th_SectionOpen(const char *title) { return th_fold_find(title) < 0; }
void Th_SectionSetOpen(const char *title, bool open)
{
    int i = th_fold_find(title);
    if (open && i >= 0) memmove(th_fold[i], th_fold[i + 1], sizeof th_fold[0] * (size_t)(--th_fold_n - i));
    else if (!open && i < 0 && th_fold_n < TH_FOLD_MAX) snprintf(th_fold[th_fold_n++], sizeof th_fold[0], "%s", title);
}

bool Th_Section(const char *title, float x, float *y, float w, const char *right)
{
    Rectangle r = { x - 4, *y, w + 4, 24 };
    bool open = Th_SectionOpen(title), hv = th_hover(r);
    char n[64];
    snprintf(n, sizeof n, "%s Fold", title);
    Th_RegLabel(n, r);
    Th_ListRow(r, hv ? TH_LR_HOVER : 0);
    Th_Icon(open ? TH_IC_CHEV_DOWN : TH_IC_CHEV_RIGHT, (Rectangle){ x - 4, *y, 18, r.height }, TH_TEXT_DIM);
    float tx = x + 14;
    Vector2 m = Th_MeasureCap(title);
    Th_TextCap(title, tx, *y + 2, TH_TEXT_DIM);
    float x1 = x + w;
    if (right && right[0]) {
        Vector2 sm = Th_MeasureSmall(right);
        Th_TextSmall(right, x1 - sm.x, *y + 2 + (m.y - sm.y) / 2, TH_TEXT_DIM);
        x1 -= sm.x + TH_GAP;
    }
    Th_HLine(tx + m.x + TH_GAP, x1, *y + 2 + m.y / 2, TH_LINE_SOFT);
    *y += r.height;
    if (Th_Released(r, title, hv)) { open = !open; Th_SectionSetOpen(title, open); }
    if (!open) *y += 4;
    return open;
}

Rectangle Th_Row(float x, float *y, float w, const char *label, const char *value, int flags)
{
    Rectangle v = { x + TH_ROW_LABEL_W, *y, w - TH_ROW_LABEL_W, TH_ROW_H };
    Th_Text(label, x, *y + (TH_ROW_H - Th_Measure("A").y) / 2, TH_TEXT_DIM);
    if (value) {
        char fit[200];
        bool mono = flags & TH_ROW_UNI;
        Th_FitText(value, mono ? TH_F_UNI : TH_F_BODY, v.width, fit, sizeof fit);
        if (mono) Th_TextUni(fit, x + w - Th_MeasureUni(fit), *y + (TH_ROW_H - TH_UNI_H) / 2, TH_TEXT);
        else Th_Text(fit, x + w - Th_Measure(fit).x, *y + (TH_ROW_H - Th_Measure("A").y) / 2, TH_TEXT);
    }
    *y += value ? TH_ROW_H : TH_ROW_H + TH_GAP / 2;
    return v;
}

/* The stepper of pxfe: [-] 3x [+] (the zoom, and the number fields); a held button repeats. */
static int th_stepper_hover = 0;
int Th_StepperHover(void) { return th_stepper_hover; }
Rectangle Th_StepperValue(Rectangle r)
{
    float bw = r.width < 96 ? 20 : 24;
    return (Rectangle){ r.x + bw, r.y, r.width - 2 * bw, r.height };
}

int Th_Stepper(Rectangle r, const char *value, const char *prefix, int flags)
{
    static char hold[48];
    static int holdDir;
    static double holdT;
    Rectangle s = th_snap(r);
    float bw = Th_StepperValue(s).x - s.x;
    Rectangle b[2] = { { s.x, s.y, bw, s.height }, { s.x + s.width - bw, s.y, bw, s.height } };
    bool edit = flags & TH_STEP_EDIT, both = (flags & 3) == 3;
    th_draw_rounded_button(s, both ? TH_PANEL_ALT : TH_PANEL, edit ? TH_ACCENT : TH_LINE);
    int step = 0;
    th_stepper_hover = 0;
    for (int k = 0; k < 2; k++) {
        int dir = k ? 1 : -1;
        char nm[48];
        snprintf(nm, sizeof nm, "%s%c", prefix, k ? '+' : '-');
        Th_Reg(nm, b[k]);
        bool off = flags & (1 << k), over = th_hover(b[k]), hover = over && !off && !edit, down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        if (over) th_stepper_hover = dir;
        if (hover) DrawRectangle((int)b[k].x + k, (int)b[k].y + 1, (int)b[k].width - 1, (int)b[k].height - 2, down ? TH_PRESSED_FILL : TH_HOVER);
        Th_VLine(k ? b[k].x : b[k].x + bw, s.y + 1, s.y + s.height - 1, TH_LINE);
        Color ink = off ? TH_TEXT_OFF : TH_TEXT;
        int cy = (int)(s.y + s.height / 2);
        DrawRectangle((int)(b[k].x + bw / 2 - 4), cy, 8, 1, ink);
        if (k) DrawRectangle((int)(b[k].x + bw / 2 - 0.5f), cy - 4, 1, 9, ink);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { step = dir; snprintf(hold, sizeof hold, "%s", prefix); holdDir = dir; holdT = GetTime() + 0.45; }
        else if (hover && holdDir == dir && !strcmp(hold, prefix) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && GetTime() > holdT) { step = dir; holdT += 0.05; }
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) holdDir = 0;
    if (value) {
        Vector2 tm = Th_Measure(value);
        Th_Text(value, s.x + (s.width - tm.x) / 2, s.y + (s.height - tm.y) / 2, both ? TH_TEXT_OFF : TH_TEXT);
    }
    return step;
}

Color Th_FromRGBA(unsigned int rrggbbaa)
{
    return (Color){
        (unsigned char)((rrggbbaa >> 24) & 0xFF),
        (unsigned char)((rrggbbaa >> 16) & 0xFF),
        (unsigned char)((rrggbbaa >> 8) & 0xFF),
        (unsigned char)(rrggbbaa & 0xFF),
    };
}

Texture2D Th_MaskTexture(const unsigned char *mask, int w, int h, Color ink)
{
    Texture2D t = { 0 };
    if (w <= 0 || h <= 0 || !mask) return t;
    Color *px = (Color *)MemAlloc((unsigned)(w * h) * sizeof(Color));
    for (int i = 0; i < w * h; i++) px[i] = mask[i] ? ink : (Color){ 0, 0, 0, 0 };
    Image img = { px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    t = LoadTextureFromImage(img);
    MemFree(px);
    if (t.id) SetTextureFilter(t, TEXTURE_FILTER_POINT);
    return t;
}

void Th_DrawTextureClipped(Texture2D tex, float x, float y, int s, Rectangle clip, Color tint)
{
    if (!tex.id || s < 1) return;
    float dw = (float)(tex.width * s), dh = (float)(tex.height * s);
    float x0 = fmaxf(x, clip.x), y0 = fmaxf(y, clip.y);
    float x1 = fminf(x + dw, clip.x + clip.width), y1 = fminf(y + dh, clip.y + clip.height);
    if (x1 <= x0 || y1 <= y0) return;
    Rectangle src = { (x0 - x) / (float)s, (y0 - y) / (float)s, (x1 - x0) / (float)s, (y1 - y0) / (float)s };
    Rectangle dst = { x0, y0, x1 - x0, y1 - y0 };
    DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0, tint);
}

void Th_Spinner(Vector2 c, float radius, Color col)
{
    int phase = (int)(GetTime() * 10.0) % 8;
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 6.2831853f / 8.0f;
        int age = (phase - i + 8) % 8;
        Color k = col;
        k.a = (unsigned char)(255 - age * 28);
        DrawCircleV((Vector2){ c.x + cosf(a) * radius, c.y + sinf(a) * radius }, 1.6f, k);
    }
}

/* Hash display */

bool (*Th_HoverFn)(Rectangle r);
void (*Th_NoticeFn)(const char *what, bool copied);


/* Hover of a copyable hash: the notice (hint), and on a click the clipboard and the toast. */
static bool th_hash_copy(Rectangle r, const char *name, const char *hash, const char *what)
{
    Th_Reg(name, r);
    if (!hash || !hash[0] || !th_hover(r)) return false;
    if (Th_NoticeFn) Th_NoticeFn(what, false);
    DrawRectangleRounded(r, 0.3f, 6, TH_HOVER);
    if (!Th_Released(r, name, true)) return true;
    SetClipboardText(hash);
    if (Th_NoticeFn) Th_NoticeFn(what, true);
    return true;
}

float Th_HashShort(const char *kind, const char *hash, float x, float y, int flags, const char *copyName)
{
    char h8[10];
    snprintf(h8, sizeof h8, "%.8s", hash && hash[0] ? hash : "no ink");
    bool sm = flags & TH_HASH_SMALL;
    const Font *kf = sm ? &theme.small : &theme.ui;
    float kh = th_measure(kf, "M").y, kw = kind && kind[0] ? th_measure(kf, kind).x + 6 : 0, w = kw + Th_MeasureUni(h8);
    if (flags & TH_HASH_RIGHT) x -= w;
    if (flags & TH_HASH_MEASURE) return w;
    if (copyName) th_hash_copy((Rectangle){ x - 4, y - 3, w + 8, kh + 6 }, copyName, hash, kind);
    if (kw) th_draw(kf, kind, x, y, TH_TEXT_DIM);
    /* Unifont's baseline is its row 14: put it on the baseline of the kind word */
    Th_TextUni(h8, x + kw, y + roundf(TH_NOTO_ASC * (sm ? TH_SIZE_SMALL : TH_SIZE_BODY) - 14), hash && hash[0] ? (flags & TH_HASH_ACCENT ? TH_ACCENT : TH_TEXT) : TH_TEXT_DIM);
    return w;
}

void Th_HashField(float x, float *y, float w, const char *caption, const char *hash, const char *name, const char *what, int flags)
{
    if (!Th_Section(caption, x + 16, y, w - 32, NULL)) return;
    Rectangle r = { x + 16, *y, w - 32, TH_ROW_H };   /* on the panel's margins, as the section caption and the rows */
    bool hv = th_hash_copy(r, name, hash, what);
    th_draw_rounded_button(th_snap(r), TH_PANEL_ALT, hv ? TH_ACCENT : TH_LINE);
    const char *t = hash ? (hash[0] ? hash : "no ink") : "not known";
    Th_TextUni(t, r.x + (r.width - Th_MeasureUni(t)) / 2, r.y + (r.height - TH_UNI_H) / 2, hash && hash[0] ? (flags & TH_HASH_ACCENT ? TH_ACCENT : TH_TEXT) : TH_TEXT_DIM);
    *y += TH_ROW_H + TH_GAP;
}

/* Copy of s shortened with an ellipsis to fit maxW in the face. */
static const Font *th_face(int face) { return face == TH_F_SMALL ? &theme.small : face == TH_F_HEAD ? &theme.head : &theme.ui; }
static float th_fw(int face, const char *s)
{
    if ((face & 0xFF) == TH_F_UNI) return Th_MeasureUni(s);
    return face & TH_F_RICH ? th_rich(face & 0xFF, s, 0, 0, WHITE, 1, false) : th_measure(th_face(face & 0xFF), s).x;
}

void Th_FitText(const char *s, int face, float maxW, char *out, size_t cap)
{
    snprintf(out, cap, "%s", s);
    if (th_fw(face, out) <= maxW) return;
    size_t len = strlen(s);
    for (size_t n = 1; n < len; n++) {
        if (face & TH_F_TAIL) {   /* the ellipsis and the end of s */
            if (((unsigned char)s[n] & 0xC0) == 0x80) continue;
            snprintf(out, cap, "…%s", s + n);
        } else {
            size_t k = len - n;
            while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--;   /* not inside a UTF-8 sequence */
            if (k + 4 > cap) continue;
            memcpy(out, s, k);
            memcpy(out + k, "…", 4);
        }
        if (th_fw(face, out) <= maxW) return;
    }
}

void Th_TextClip(const char *s, float x, float y, float maxW, Color c, int face)
{
    char buf[512];
    Th_FitText(s, face, maxW, buf, sizeof buf);
    if ((face & 0xFF) == TH_F_UNI) Th_TextUni(buf, x, y, c);
    else if (face & TH_F_RICH) Th_TextRich(buf, x, y, c, face & 0xFF, -1);
    else th_draw(th_face(face & 0xFF), buf, x, y, c);
}

/* Text field and search field.  One field has the keyboard (th_ed); its text is edited in th_ed.buf and handed out by Th_TextValue. */
static int th_prev(const char *s, int i) { if (i > 0) i--; while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--; return i; }
static int th_next(const char *s, int i) { if (s[i]) i++; while (s[i] && ((unsigned char)s[i] & 0xC0) == 0x80) i++; return i; }
static bool th_key(int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); }
static bool th_ctrl(void) { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }

/* Width of the bytes [a, b) of s. */
static float th_w(const char *s, int a, int b)
{
    char t[sizeof th_ed.buf];
    memcpy(t, s + a, (size_t)(b - a));
    t[b - a] = 0;
    return Th_Measure(t).x;
}

static bool th_accepts(int c, int flags)
{
    if (c < 32 || c == 127) return false;
    if (flags & TH_TF_DEC) return (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ',';
    if (flags & TH_TF_NUM) return (c >= '0' && c <= '9') || c == '-';
    if (flags & TH_TF_HEX) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '#';
    return true;
}

static int th_lo(void) { return th_ed.anc < th_ed.cur ? th_ed.anc : th_ed.cur; }
static int th_hi(void) { return th_ed.anc < th_ed.cur ? th_ed.cur : th_ed.anc; }

/* Deletes the selection; true when there was one. */
static bool th_cut(void)
{
    int a = th_lo(), z = th_hi();
    if (z <= a) return false;
    memmove(th_ed.buf + a, th_ed.buf + z, strlen(th_ed.buf + z) + 1);
    th_ed.cur = th_ed.anc = a;
    return true;
}

static bool th_insert(const char *s, int n, int cap)
{
    th_cut();
    int len = (int)strlen(th_ed.buf);
    if (len + n >= cap) return false;
    memmove(th_ed.buf + th_ed.cur + n, th_ed.buf + th_ed.cur, (size_t)(len - th_ed.cur) + 1);
    memcpy(th_ed.buf + th_ed.cur, s, (size_t)n);
    th_ed.cur += n;
    th_ed.anc = th_ed.cur;
    return true;
}

const char *Th_TextValue(void) { return th_ed.buf; }
bool Th_TextIs(const char *name) { return th_ed.name[0] && (!name || !strcmp(th_ed.name, name)); }
bool Th_TextIsIn(const char *prefix) { return th_ed.name[0] && !strncmp(th_ed.name, prefix, strlen(prefix)); }
void Th_TextBlur(void) { th_ed.name[0] = 0; th_ed.drag = false; }

void Th_TextFocus(const char *name, const char *text, bool selectAll)
{
    snprintf(th_ed.name, sizeof th_ed.name, "%s", name);
    snprintf(th_ed.buf, sizeof th_ed.buf, "%s", text);
    snprintf(th_ed.old, sizeof th_ed.old, "%s", text);
    th_ed.cur = (int)strlen(th_ed.buf);
    th_ed.anc = selectAll ? 0 : th_ed.cur;
    th_ed.start = 0;
    th_ed.seen = true;
    while (GetCharPressed() > 0) {}   /* the keystroke that led here is not text */
}

/* The caret position nearest to the mouse in a field whose visible text starts at x0. */
static int th_pos_at(float x0)
{
    float mx = GetMousePosition().x - x0, bd = 1e9f;
    int best = th_ed.start;
    for (int i = th_ed.start; ; i = th_next(th_ed.buf, i)) {
        float d = fabsf(th_w(th_ed.buf, th_ed.start, i) - mx);
        if (d < bd) { bd = d; best = i; }
        if (!th_ed.buf[i] || d > bd + 40) break;
    }
    return best;
}

static bool th_word(char c) { return c && c != ' ' && c != ',' && c != ';'; }

/* The keys of the focused field; returns TH_TF_* bits.  A read-only field selects and copies but takes no edit. */
static int th_edit(int cap, int flags)
{
    int res = 0, c;
    bool ro = flags & TH_TF_READONLY;
    char *b = th_ed.buf;
    if (cap <= 0 || cap > (int)sizeof th_ed.buf) cap = (int)sizeof th_ed.buf;
    while ((c = GetCharPressed()) > 0) {
        int n = 0;
        const char *u = CodepointToUTF8(c, &n);
        if (!ro && th_accepts(c, flags) && th_insert(u, n, cap)) res |= TH_TF_CHANGED;
    }
    int len = (int)strlen(b);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), ctrl = th_ctrl();
    for (int k = 0; k < 2 && !ro; k++) if (th_key(k ? KEY_DELETE : KEY_BACKSPACE)) {
        if (th_ed.anc == th_ed.cur) th_ed.cur = k ? th_next(b, th_ed.cur) : th_prev(b, th_ed.cur);
        if (th_cut()) { res |= TH_TF_CHANGED; len = (int)strlen(b); }
    }
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    int to = -1;
    if (!alt && th_key(KEY_LEFT)) to = !shift && th_ed.anc != th_ed.cur ? th_lo() : th_prev(b, th_ed.cur);
    if (!alt && th_key(KEY_RIGHT)) to = !shift && th_ed.anc != th_ed.cur ? th_hi() : th_next(b, th_ed.cur);
    if (IsKeyPressed(KEY_HOME)) to = 0;
    if (IsKeyPressed(KEY_END)) to = len;
    if (to >= 0) { th_ed.cur = to; if (!shift) th_ed.anc = to; }
    if (ctrl && IsKeyPressed(KEY_A)) { th_ed.anc = 0; th_ed.cur = len; }
    bool cut = ctrl && IsKeyPressed(KEY_X);
    if (ctrl && (cut || IsKeyPressed(KEY_C)) && th_hi() > th_lo()) {
        char t[sizeof th_ed.buf];
        snprintf(t, sizeof t, "%.*s", th_hi() - th_lo(), b + th_lo());
        SetClipboardText(t);
        if (!ro && cut && th_cut()) res |= TH_TF_CHANGED;
    }
    if (!ro && ctrl && IsKeyPressed(KEY_V) && GetClipboardText()) {
        if (th_cut()) res |= TH_TF_CHANGED;
        for (const char *q = GetClipboardText(); *q; q++)
            if (*q != '\n' && *q != '\r' && *q != '\t' && th_accepts((unsigned char)*q, flags) && th_insert(q, 1, cap)) res |= TH_TF_CHANGED;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB)) { res |= TH_TF_ENTER; th_ed.name[0] = 0; }
    else if (IsKeyPressed(KEY_ESCAPE)) {
        res |= TH_TF_ESC;
        if (flags & TH_TF_REVERT) { snprintf(b, sizeof th_ed.buf, "%s", th_ed.old); res |= TH_TF_CHANGED; }
        th_ed.name[0] = 0;
    }
    return res;
}

int Th_TextField(Rectangle r, const char *name, const char *text, int cap, int flags, const char *placeholder)
{
    Rectangle s = th_snap(r);
    bool off = flags & TH_TF_DISABLED, ro = flags & TH_TF_READONLY, bare = flags & TH_TF_BARE, search = flags & TH_TF_SEARCH;
    bool foc = !off && Th_TextIs(name), over = !off && th_hover(s);
    int res = 0;
    Th_Reg(name, s);
    if (foc && !th_blocked) {
        if ((ro || !(flags & TH_TF_COMMIT)) && strcmp(text, th_ed.buf)) { snprintf(th_ed.buf, sizeof th_ed.buf, "%s", text); th_ed.cur = th_ed.anc = (int)strlen(th_ed.buf); }   /* changed from outside */
        res = th_edit(cap, flags);
        foc = Th_TextIs(name);
    }
    if (over) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    float pad = search ? 30 : bare ? 2 : 8, avail = s.width - 2 * pad, x0 = s.x + pad;
    if (bare && foc) x0 = s.x + (s.width - th_w(th_ed.buf, th_ed.start, (int)strlen(th_ed.buf))) / 2;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !th_blocked) {
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        double t = GetTime();
        if (over && !foc) {
            Th_TextFocus(name, text, flags & TH_TF_SELECT);
            foc = true;
            if (!(flags & TH_TF_SELECT)) { th_ed.cur = th_ed.anc = th_pos_at(x0); th_ed.drag = true; }
            th_ed.clickT = t;
        } else if (over) {   /* the caret at the click (Shift: the selection to it); a double click selects the word */
            int p = th_pos_at(x0);
            if (t - th_ed.clickT < 0.4) {
                int a = p, z = p;
                while (a > 0 && th_word(th_ed.buf[a - 1])) a--;
                while (th_word(th_ed.buf[z])) z++;
                th_ed.anc = a; th_ed.cur = z;
            } else { th_ed.cur = p; if (!shift) th_ed.anc = p; th_ed.drag = true; }
            th_ed.clickT = t;
        } else if (foc) { Th_TextBlur(); foc = false; res |= TH_TF_BLUR; }
    }
    if (foc && th_ed.drag) { if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) th_ed.cur = th_pos_at(x0); else th_ed.drag = false; }
    if (foc) th_ed.seen = true;
    const char *show = foc ? th_ed.buf : text;
    th_draw_rounded_button(s, bare ? BLANK : (off || ro) ? TH_PANEL_ALT : TH_PANEL, bare ? BLANK : foc ? TH_ACCENT : over ? TH_HOVER_BORDER : TH_LINE);
    if (search) Th_Icon(TH_IC_SEARCH, (Rectangle){ s.x + 4, s.y, 22, s.height }, TH_TEXT_DIM);
    float ty = s.y + (s.height - Th_Measure("A").y) / 2;
    char vis[sizeof th_ed.buf];
    if (!foc) {
        Th_FitText(show, TH_F_BODY, avail, vis, sizeof vis);
        if (!show[0] && placeholder) Th_Text(placeholder, x0, ty, TH_TEXT_HINT);
        else Th_Text(vis, bare ? s.x + (s.width - Th_Measure(vis).x) / 2 : x0, ty, (off || ro) ? TH_TEXT_DIM : TH_TEXT);
    } else {
        int len = (int)strlen(show), end;
        if (th_ed.cur < th_ed.start) th_ed.start = th_ed.cur;
        while (th_ed.start < th_ed.cur && th_w(show, th_ed.start, th_ed.cur) > avail - 2) th_ed.start = th_next(show, th_ed.start);
        if (th_w(show, 0, len) <= avail) th_ed.start = 0;
        for (end = th_ed.start; show[end] && th_w(show, th_ed.start, th_next(show, end)) <= avail; ) end = th_next(show, end);
        memcpy(vis, show + th_ed.start, (size_t)(end - th_ed.start));
        vis[end - th_ed.start] = 0;
        Vector2 m = Th_Measure(vis);
        float tx = bare ? s.x + (s.width - m.x) / 2 : x0;
        int a = th_lo() > th_ed.start ? th_lo() : th_ed.start, z = th_hi() < end ? th_hi() : end;
        if (z > a) DrawRectangle((int)(tx + th_w(show, th_ed.start, a)), (int)ty, (int)(th_w(show, a, z) + 1), (int)m.y, TH_SELECTION);
        Th_Text(vis, tx, ty, ro ? TH_TEXT_DIM : TH_TEXT);
        if (th_ed.anc == th_ed.cur && !ro && (int)(GetTime() * 2) % 2 == 0) DrawRectangle((int)(tx + th_w(show, th_ed.start, th_ed.cur) + 1), (int)ty, 1, (int)m.y, TH_TEXT);
    }
    if (search && show[0] && !off && !ro) {
        char nm[72];
        snprintf(nm, sizeof nm, "%s_Clear", name);
        if (Th_Button((Rectangle){ s.x + s.width - 24, s.y + 4, 20, 20 }, nm, TH_BTN_ICON(TH_IC_CLOSE), "Clear")) {
            if (!foc) Th_TextFocus(name, text, false);
            th_ed.buf[0] = 0; th_ed.cur = th_ed.anc = 0; res |= TH_TF_CHANGED;
        }
    }
    return res;
}

/* Checkbox and switch share one geometry: the control (a 16 px box or a 36 x 20 pill) at the left of the row, its label on the right;
 * TH_CK_RIGHT puts the control at the right end and the label at the left.  The whole row is the hit area. */
static bool th_check(Rectangle r, const char *label, bool *v, int flags, const char *name, bool pill)
{
    Rectangle s = th_snap(r);
    bool off = flags & TH_CK_OFF, right = flags & TH_CK_RIGHT, on = *v, over = !off && th_hover(s);
    float w = pill ? 36 : 16, h = pill ? 20 : 16, lx = right ? s.x : s.x + w + 10 + ((flags & TH_CK_GAP) ? 12 : 0);
    Rectangle c = { right ? s.x + s.width - w : s.x, s.y + floorf((s.height - h) / 2), w, h };
    Color accent = off ? th_mix(TH_ACCENT, TH_PANEL, 0.55f) : TH_ACCENT;
    Th_Reg(name, c);
    if (pill) {
        DrawRectangleRounded(c, 1.0f, TH_BUTTON_SEGMENTS, on ? accent : off ? TH_TRACK : th_mix(TH_TRACK, TH_TEXT_OFF, 0.5f));
        DrawCircle((int)(on ? c.x + c.width - h / 2 : c.x + h / 2), (int)(c.y + h / 2), h / 2 - 3, TH_PANEL);
    } else {
        th_draw_rounded_button(c, on ? accent : off ? TH_PANEL_ALT : TH_PANEL, on ? accent : off ? TH_LINE : over ? TH_TEXT_DIM : TH_TEXT_OFF);
        if (on) {
            DrawLineEx((Vector2){ c.x + 3.5f, c.y + 8.5f }, (Vector2){ c.x + 6.5f, c.y + 11.5f }, 1.6f, TH_TEXT_ON);
            DrawLineEx((Vector2){ c.x + 6.5f, c.y + 11.5f }, (Vector2){ c.x + 12.5f, c.y + 4.5f }, 1.6f, TH_TEXT_ON);
        }
    }
    if (label) Th_Text(label, lx, s.y + (s.height - Th_Measure(label).y) / 2, off ? TH_TEXT_DIM : TH_TEXT);
    if (Th_Released(s, name, over)) { *v = !on; return true; }
    return false;
}
bool Th_Check(Rectangle r, const char *label, bool *v, int flags, const char *name) { return th_check(r, label, v, flags, name, false); }
bool Th_Switch(Rectangle r, const char *label, bool *v, int flags, const char *name) { return th_check(r, label, v, flags, name, true); }

/* The closed select box ("Any v"): the value (dim when `dim`, a placeholder like "Any"), a chevron; accent border while its list is open.
 * Returns true when clicked; the caller opens its list. */
bool Th_Dropdown(Rectangle r, const char *name, const char *value, bool dim, bool open)
{
    Rectangle s = th_snap(r);
    bool over = th_hover(s);
    char fit[256];
    Th_Reg(name, s);
    th_draw_rounded_button(s, TH_PANEL, open ? TH_ACCENT : over ? TH_HOVER_BORDER : TH_LINE);
    Th_FitText(value, TH_F_BODY, s.width - 36, fit, sizeof fit);
    Th_Text(fit, s.x + 8, s.y + (s.height - Th_Measure(fit).y) / 2, dim ? TH_TEXT_DIM : TH_TEXT);
    Th_Icon(TH_IC_CHEV_DOWN, (Rectangle){ s.x + s.width - 24, s.y, 20, s.height }, TH_TEXT_DIM);
    return Th_Released(s, name, over);
}

/* Number box: a stepper whose value is a text field (typed digits are applied on Enter, Tab or a click elsewhere, Escape drops them).
 * Returns true and *out when the value changes. */
bool Th_Number(Rectangle r, const char *prefix, int val, int lo, int hi, int flags, int *out)
{
    char text[24], n[64];
    bool off = flags & TH_TF_DISABLED;
    snprintf(text, sizeof text, "%d", val);
    snprintf(n, sizeof n, "%s.value", prefix);
    bool editing = Th_TextIs(n);
    int step = Th_Stepper(r, NULL, prefix, editing ? TH_STEP_EDIT : off ? TH_STEP_NO_MINUS | TH_STEP_NO_PLUS : (val <= lo ? TH_STEP_NO_MINUS : 0) | (val >= hi ? TH_STEP_NO_PLUS : 0));
    int f = Th_TextField(Th_StepperValue(r), n, text, 12, TH_TF_NUM | TH_TF_BARE | TH_TF_COMMIT | TH_TF_SELECT | (flags & TH_TF_DISABLED), NULL);
    if (f & (TH_TF_ENTER | TH_TF_BLUR)) {
        char *end = NULL;
        long v = strtol(Th_TextValue(), &end, 10);
        if (end != Th_TextValue()) step = (int)(v < lo ? lo : v > hi ? hi : v) - val;   /* the typed value, as a step */
    } else if ((flags & TH_NUM_WHEEL) && !editing && !off && th_hover(r) && GetMouseWheelMove() != 0) { step = GetMouseWheelMove() > 0 ? 1 : -1; th_wheel_used = true; }
    if (!step) return false;
    int nv = val + step;
    nv = nv < lo ? lo : nv > hi ? hi : nv;
    if (nv == val) return false;
    *out = nv;
    return true;
}

bool Th_NumberF(Rectangle r, const char *prefix, float val, float lo, float hi, float step, int flags, float *out)
{
    char text[24], n[64];
    bool off = flags & TH_TF_DISABLED;
    float shown = roundf(val * 1000.0f) / 1000.0f;
    snprintf(text, sizeof text, "%g", fabsf(shown) < 0.0005f ? 0.0 : (double)shown);
    snprintf(n, sizeof n, "%s.value", prefix);
    bool editing = Th_TextIs(n);
    int st = Th_Stepper(r, NULL, prefix, editing ? TH_STEP_EDIT : off ? TH_STEP_NO_MINUS | TH_STEP_NO_PLUS : (val <= lo ? TH_STEP_NO_MINUS : 0) | (val >= hi ? TH_STEP_NO_PLUS : 0));
    int f = Th_TextField(Th_StepperValue(r), n, text, 12, TH_TF_DEC | TH_TF_BARE | TH_TF_COMMIT | TH_TF_SELECT | (flags & TH_TF_DISABLED), NULL);
    float nv = val;
    if (f & (TH_TF_ENTER | TH_TF_BLUR)) {
        char buf[32], *end = NULL;
        snprintf(buf, sizeof buf, "%.30s", Th_TextValue());
        for (char *q = buf; *q; q++) if (*q == ',') *q = '.';
        double v = strtod(buf, &end);
        if (end != buf && v == v) nv = (float)v;
    } else if (st && !off) nv = roundf((val + (float)st * step) * 10000.0f) / 10000.0f;   /* no drift after many steps */
    else return false;
    nv = nv < lo ? lo : nv > hi ? hi : nv;
    if (nv == val) return false;
    *out = nv;
    return true;
}

/* Scroll bar: the track is the right edge of `view`, inset 4 px; one bar is dragged at a time, identified by its track's origin. */
static Vector2 th_sb_id;
static bool th_sb_drag;
static float th_sb_grab;

bool Th_ScrollInto(Rectangle r)
{
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    int best = -1;
    for (int i = 0; i < th_scr_n; i++) {
        Rectangle v = th_scr[i].v;
        if (r.x < v.x || r.x + r.width > v.x + v.width || r.height >= v.height) continue;
        if (cy >= v.y + 4 && cy <= v.y + v.height - 4) return false;   /* a view shows it */
        if (best < 0 || v.height > th_scr[best].v.height) best = i;
    }
    if (best < 0 || cx < 0) return false;
    *th_scr[best].s += cy - (th_scr[best].v.y + th_scr[best].v.height / 2);
    return true;
}

static bool th_scrollbar(Rectangle view, float *scroll, float content, float step, bool live, bool reg)
{
    if (reg && th_scr_n < 16) th_scr[th_scr_n++] = (typeof(th_scr[0])){ view, scroll };
    float max = content - view.height;
    Rectangle tr = { view.x + view.width - TH_SB_W, view.y + 4, TH_SB_W - 4, view.height - 8 };
    bool mine = th_sb_drag && th_sb_id.x == tr.x && th_sb_id.y == tr.y;
    if (max <= 0 || tr.height <= 0) { *scroll = 0; if (mine) th_sb_drag = false; return false; }
    float thumbH = fminf(tr.height, fmaxf(36, tr.height * view.height / content)), room = tr.height - thumbH;
    Vector2 m = GetMousePosition();
    bool on = live && th_hover((Rectangle){ tr.x - 4, tr.y, tr.width + 8, tr.height });
    if (live && step > 0 && !th_wheel_used && th_hover(view) && GetMouseWheelMove() != 0) { *scroll -= GetMouseWheelMove() * step; th_wheel_used = true; }
    if (on && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        float ty = tr.y + room * (*scroll / max);
        th_sb_drag = mine = true;
        th_sb_id = (Vector2){ tr.x, tr.y };
        th_sb_grab = m.y >= ty && m.y <= ty + thumbH ? m.y - ty : thumbH / 2;   /* a click beside the thumb centres it there */
    }
    bool hot = on || mine;   /* a drag that ends this frame still has the mouse */
    if (mine && (!live || !IsMouseButtonDown(MOUSE_BUTTON_LEFT))) th_sb_drag = mine = false;
    if (mine && room > 0) *scroll = fminf(1, fmaxf(0, (m.y - th_sb_grab - tr.y) / room)) * max;
    *scroll = floorf(fminf(max, fmaxf(0, *scroll)));
    if (hot) DrawRectangleRounded(tr, 1.0f, 6, TH_SCROLL_TRACK);
    float tw = hot ? tr.width : 4;
    DrawRectangleRounded((Rectangle){ tr.x + (tr.width - tw) / 2, tr.y + room * (*scroll / max), tw, thumbH }, 1.0f, 6, hot ? TH_SCROLL_THUMB_HOT : TH_SCROLL_THUMB);
    return hot;
}

bool Th_ScrollBar(Rectangle view, float *scroll, float content, float step, bool live) { return th_scrollbar(view, scroll, content, step, live, true); }

bool Th_ScrollRows(Rectangle view, int *first, int n, int vis, float rowH, bool live)
{
    float px = *first * rowH;
    bool hot = th_scrollbar(view, &px, n * rowH + view.height - vis * rowH, 2 * rowH, live, false);
    *first = n > vis ? (int)(px / rowH + 0.5f) : 0;
    return hot;
}

/* Raised surface, menu, toast and status bar: the things that float over or sit under a view. */
void Th_Raised(Rectangle r, float a)
{
    Rectangle s = th_snap(r);
    DrawRectangleRounded((Rectangle){ s.x, s.y + 3, s.width, s.height }, th_roundness(s), TH_BUTTON_SEGMENTS, (Color){ 0, 0, 0, (unsigned char)(24 * a) });
    th_draw_rounded_button(s, Fade(TH_PANEL, a), Fade(TH_LINE, a));
}

static void th_menu_item(const ThMenu *m, int i, ThMenuItem *o)
{
    memset(o, 0, sizeof *o);
    if (m->items) *o = m->items[i];
    else m->get(m->ud, i, o);
}

Rectangle Th_MenuRect(const ThMenu *m)
{
    int vis = m->vis > 0 && m->vis < m->n ? m->vis : m->n, seps = 0;
    for (int i = 0; vis == m->n && i < m->n; i++) { ThMenuItem it; th_menu_item(m, i, &it); seps += (it.flags & TH_MI_SEP) != 0; }
    Rectangle r = { m->x - ((m->flags & TH_MENU_RIGHT) ? m->w : 0), m->y, m->w, 8.0f + vis * TH_ROW_H + seps * 9.0f };
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    if (r.x + r.width > W - 4) r.x = W - 4 - r.width;
    if (r.y + r.height > H - TH_ROW_H - 4) r.y = H - TH_ROW_H - 4 - r.height;
    return r;
}

bool Th_MenuKeys(int key) { return th_menu_prev && (key == KEY_UP || key == KEY_DOWN || key == KEY_HOME || key == KEY_END || key == KEY_ENTER || key == KEY_KP_ENTER || key == KEY_ESCAPE); }

/* The next enabled item from `from` in direction dir (+1, -1), or -1. */
static int th_menu_step(const ThMenu *m, int from, int dir)
{
    for (int i = from + dir; i >= 0 && i < m->n; i += dir) { ThMenuItem it; th_menu_item(m, i, &it); if (!(it.flags & TH_MI_OFF)) return i; }
    return -1;
}

static int hi = -1;   /* the highlighted item of the open menu, -1 none */
int Th_MenuHi(void) { return hi; }

int Th_Menu(const ThMenu *m, bool *open)
{
    static const void *mid;
    Rectangle r = Th_MenuRect(m);
    Vector2 mp = GetMousePosition(), md = GetMouseDelta();
    bool in = CheckCollisionPointRec(mp, r), more = m->vis > 0 && m->vis < m->n, moved = md.x != 0 || md.y != 0;
    int vis = more ? m->vis : m->n, pick = -1;
    const void *id = m->items ? (const void *)m->items : (const void *)m->get;
    if (id != mid || !th_menu_prev) hi = -1;   /* a menu opened afresh starts without a highlight */
    mid = id;
    int step = ((IsKeyPressed)(KEY_DOWN) || (IsKeyPressedRepeat)(KEY_DOWN)) - ((IsKeyPressed)(KEY_UP) || (IsKeyPressedRepeat)(KEY_UP));
    if (step) { int nx = th_menu_step(m, hi < 0 && step < 0 ? m->n : hi, step); if (nx >= 0) hi = nx; }
    if ((IsKeyPressed)(KEY_HOME)) hi = th_menu_step(m, -1, 1);
    if ((IsKeyPressed)(KEY_END)) hi = th_menu_step(m, m->n, -1);
    if (more && hi >= 0) { if (hi < *m->scroll) *m->scroll = hi; else if (hi >= *m->scroll + vis) *m->scroll = hi - vis + 1; }
    Th_Raised(r, 1);
    bool grab = false, pb = th_blocked, pm = th_mouse_blocked;
    th_blocked = th_mouse_blocked = false;   /* the widgets under a popup are blocked, the popup is not */
    if (more) grab = Th_ScrollRows(r, m->scroll, m->n, vis, TH_ROW_H, true);
    th_blocked = pb; th_mouse_blocked = pm;
    int sc = more ? *m->scroll : 0;
    float y = r.y + 4;
    for (int k = 0; k < vis; k++) {
        ThMenuItem it;
        th_menu_item(m, sc + k, &it);
        Rectangle ir = { r.x + 4, y, r.width - 8 - (more ? 8 : 0), TH_ROW_H };
        bool off = it.flags & TH_MI_OFF, hv = in && CheckCollisionPointRec(mp, ir);
        if (it.name) Th_Reg(it.name, ir); else Th_RegLabel(it.label, ir);
        if (hv && !off && !grab && (moved || hi < 0)) hi = sc + k;   /* the mouse moves the highlight */
        if (hi == sc + k && !off) DrawRectangleRounded(ir, 0.2f, 6, TH_HOVER);
        if (it.flags & TH_MI_CUR) DrawRectangleRounded(ir, 0.2f, 6, TH_ACCENT_TINT);
        if (hv && off && it.why) th_tip = it.why;
        Color c = off ? TH_TEXT_OFF : (it.flags & TH_MI_DANGER) ? TH_DANGER : (it.flags & TH_MI_CUR) ? TH_ACCENT : (it.flags & TH_MI_DIM) ? TH_TEXT_DIM : TH_TEXT;
        float kw = it.key && it.key[0] ? Th_MeasureSmall(it.key).x + 12 : 0;
        Th_TextClip(it.label, ir.x + 10, ir.y + (ir.height - Th_Measure("A").y) / 2, ir.width - 20 - kw, c, TH_F_BODY);
        if (kw) Th_TextSmall(it.key, ir.x + ir.width - kw + 2, ir.y + (ir.height - Th_MeasureSmall("A").y) / 2, TH_TEXT_DIM);
        if (hv && !off && !grab && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) pick = sc + k;
        y += TH_ROW_H;
        if (it.flags & TH_MI_SEP) { Th_HLine(r.x + 8, r.x + r.width - 8, y + 4, TH_LINE); y += 9; }
    }
    if (pick < 0 && hi >= 0 && ((IsKeyPressed)(KEY_ENTER) || (IsKeyPressed)(KEY_KP_ENTER))) pick = hi;
    if (pick >= 0 || (IsKeyPressed)(KEY_ESCAPE) || (!in && !CheckCollisionPointRec(mp, m->keep) && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)))) { *open = false; hi = -1; }
    th_menu_cur |= *open;
    return pick;
}

static struct { char text[200]; double t0; float life; int kind; } th_toasts[4];
static int th_toast_n;

void Th_Toast(int kind, const char *fmt, ...)
{
    if (th_toast_n == 4) { memmove(&th_toasts[0], &th_toasts[1], sizeof th_toasts[0] * 3); th_toast_n = 3; }
    __typeof__(th_toasts[0]) *t = &th_toasts[th_toast_n++];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(t->text, sizeof t->text, fmt, ap);
    __builtin_va_end(ap);
    t->t0 = GetTime();
    t->life = kind == TH_TOAST_ERR ? 6.0f : 3.2f;
    t->kind = kind;
}

int Th_ToastCount(void) { return th_toast_n; }
bool Th_ToastHas(const char *sub) { for (int i = 0; i < th_toast_n; i++) if (strstr(th_toasts[i].text, sub)) return true; return false; }

void Th_ToastDraw(Rectangle area, bool left)
{
    double now = GetTime();
    int out = 0;
    for (int i = 0; i < th_toast_n; i++) if (now - th_toasts[i].t0 < th_toasts[i].life) th_toasts[out++] = th_toasts[i];
    th_toast_n = out;
    float y = area.y + area.height - TH_PAD - 4;
    for (int i = th_toast_n - 1; i >= 0; i--) {
        double age = now - th_toasts[i].t0;
        float a = age < 0.15 ? (float)(age / 0.15) : th_toasts[i].life - age < 0.4 ? (float)((th_toasts[i].life - age) / 0.4) : 1.0f;
        float w = fminf(Th_Measure(th_toasts[i].text).x + 36, area.width - 32), h = 36;
        Rectangle r = { left ? area.x + 16 : area.x + area.width - w - 16, y - h, w, h };
        Th_Raised(r, a);
        DrawRectangleRounded((Rectangle){ r.x + 8, r.y + 9, 3, h - 18 }, 1.0f, 4, Fade(th_toasts[i].kind == TH_TOAST_ERR ? TH_DANGER : th_toasts[i].kind == TH_TOAST_OK ? TH_M_CAP : TH_ACCENT, a));
        Th_TextClip(th_toasts[i].text, r.x + 22, r.y + (h - Th_Measure("A").y) / 2, w - 30, Fade(TH_TEXT, a), TH_F_BODY);
        y -= h + 8;
    }
}

/* Draws part p at x (ending at x when `right`) if it fits in `room`; returns its width, 0 when it is dropped. */
static float th_part(const ThPart *p, float x, float room, bool right, Rectangle r)
{
    char fit[1200];
    if (!p->text) {
        float w = Th_HashShort(p->kind, p->hash, 0, 0, TH_HASH_MEASURE | TH_HASH_SMALL, NULL);
        if (w > room) return 0;
        Th_HashShort(p->kind, p->hash, x, r.y + floorf((r.height - Th_MeasureSmall("M").y) / 2), TH_HASH_SMALL | (p->flags & TH_HASH_ACCENT) | (right ? TH_HASH_RIGHT : 0), p->copy);
        return w;
    }
    float lim = p->maxFrac > 0 && p->maxFrac * r.width < room ? p->maxFrac * r.width : room;
    Th_FitText(p->text, TH_F_SMALL | (p->flags & TH_SP_TAIL), lim, fit, sizeof fit);
    float w = Th_MeasureSmall(fit).x;
    if (w > lim) return 0;
    Th_TextSmall(fit, right ? x - w : x, r.y + floorf((r.height - Th_MeasureSmall("M").y) / 2), p->color.a ? p->color : TH_TEXT_DIM);
    return w;
}

void Th_StatusBar(Rectangle r, float reserve, const ThPart *left, int nl, const ThPart *right, int nr)
{
    DrawRectangleRec(r, TH_PANEL_ALT);
    Th_HLine(r.x, r.x + r.width, r.y, TH_LINE);
    float x = r.x + TH_PAD, lim = r.x + r.width - TH_PAD - reserve;
    for (int i = 0; i < nl; i++) { float w = th_part(&left[i], x, lim - x, false, r); if (w) x += w + 14; }
    for (int i = nr - 1; i >= 0; i--) { float w = th_part(&right[i], lim, lim - x - 16, true, r); if (w) lim -= w + 10; }
}
