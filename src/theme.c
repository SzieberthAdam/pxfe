/* theme.c - shared look of pxfe: fonts, raygui style, crisp drawing helpers (see DESIGN.md). */
#include "theme.h"
#include "raygui.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stddef.h>

#define TH_FONT_UI_PATH    "C:/Windows/Fonts/segoeui.ttf"
#define TH_FONT_MONO_PATH  "C:/Windows/Fonts/consola.ttf"
#define TH_FONT_UI_SIZE    13
#define TH_FONT_SMALL_SIZE 11
#define TH_FONT_MONO_SIZE  12

#define TH_BUTTON_SEGMENTS 8
#define TH_PRESSED_FILL    (Color){0xDC, 0xE6, 0xFC, 0xFF}  /* accent tint, opaque (pressed button) */
#define TH_HOVER_BORDER    (Color){0xA9, 0xC2, 0xF5, 0xFF}  /* light accent border on hover */

Theme theme = { 0 };

/* Code points loaded for every theme font, as inclusive ranges. */
static const int th_glyph_ranges[][2] = {
    { 0x0020, 0x007E },   /* ASCII */
    { 0x00A0, 0x017F },   /* Latin-1 Supplement + Latin Extended-A (includes 0x00D7 multiplication sign) */
    { 0x2013, 0x2014 },   /* en dash, em dash */
    { 0x2018, 0x2019 },   /* single quotes */
    { 0x201C, 0x201D },   /* double quotes */
    { 0x2026, 0x2026 },   /* ellipsis */
    { 0x2190, 0x2193 },   /* arrows left, up, right, down */
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

/* Load a font from a file with the theme code points and bilinear filtering;
 * falls back to raylib's default font when the file is missing or fails. */
static Font th_load_font(const char *path, int size)
{
    Font font = { 0 };

    if (FileExists(path)) font = LoadFontEx(path, size, th_codepoints, th_codepoint_count);

    if ((font.texture.id == 0) || (font.glyphCount == 0)) return GetFontDefault();

    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
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

static void th_apply_raygui_style(void)
{
    GuiSetFont(theme.ui);

    GuiSetStyle(DEFAULT, TEXT_SIZE, TH_FONT_UI_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 0);
    GuiSetStyle(DEFAULT, BORDER_WIDTH, 1);

    /* Normal */
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt(TH_LINE));
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, ColorToInt(TH_PANEL));
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(TH_TEXT));

    /* Focused */
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(TH_ACCENT));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, ColorToInt((Color){0xF0, 0xF4, 0xFE, 0xFF}));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt(TH_TEXT));

    /* Pressed */
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(TH_ACCENT));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, ColorToInt(TH_PRESSED_FILL));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt(TH_ACCENT));

    /* Disabled */
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, ColorToInt(TH_LINE_SOFT));
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, ColorToInt(TH_PANEL_ALT));
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, ColorToInt(TH_TEXT_DIM));

    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt(TH_BG));
    GuiSetStyle(DEFAULT, LINE_COLOR, ColorToInt(TH_LINE));

    /* Per-control padding (set after the DEFAULT broadcast above). */
    GuiSetStyle(TEXTBOX, TEXT_PADDING, 4);
    GuiSetStyle(VALUEBOX, TEXT_PADDING, 4);
}

void Theme_Load(void)
{
    if (theme.loaded) Theme_Unload();

    th_build_codepoints();

    theme.ui = th_load_font(TH_FONT_UI_PATH, TH_FONT_UI_SIZE);
    theme.small = th_load_font(TH_FONT_UI_PATH, TH_FONT_SMALL_SIZE);
    theme.mono = th_load_font(TH_FONT_MONO_PATH, TH_FONT_MONO_SIZE);
    theme.loaded = true;

    th_apply_raygui_style();
}

void Theme_Unload(void)
{
    Font def = GetFontDefault();
    Font *fonts[] = { &theme.ui, &theme.small, &theme.mono };

    /* Only fonts loaded from files are unloaded; the default font is shared. */
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        if (fonts[i]->texture.id != 0 && fonts[i]->texture.id != def.texture.id) UnloadFont(*fonts[i]);
        *fonts[i] = (Font){ 0 };
    }

    theme.loaded = false;
}

/* Text helpers */

void Th_Text(const char *text, float x, float y, Color c)
{
    DrawTextEx(theme.ui, text, th_snap_pos(x, y), (float)theme.ui.baseSize, 0, c);
}

void Th_TextSmall(const char *text, float x, float y, Color c)
{
    DrawTextEx(theme.small, text, th_snap_pos(x, y), (float)theme.small.baseSize, 0, c);
}

void Th_TextMono(const char *text, float x, float y, Color c)
{
    DrawTextEx(theme.mono, text, th_snap_pos(x, y), (float)theme.mono.baseSize, 0, c);
}

Vector2 Th_Measure(const char *text)
{
    return MeasureTextEx(theme.ui, text, (float)theme.ui.baseSize, 0);
}

Vector2 Th_MeasureSmall(const char *text)
{
    return MeasureTextEx(theme.small, text, (float)theme.small.baseSize, 0);
}

Vector2 Th_MeasureMono(const char *text)
{
    return MeasureTextEx(theme.mono, text, (float)theme.mono.baseSize, 0);
}

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

void Th_Panel(Rectangle r)
{
    Rectangle s = th_snap(r);

    if (s.width < 1 || s.height < 1) return;

    DrawRectangle((int)s.x, (int)s.y, (int)s.width, (int)s.height, TH_PANEL);
    Th_RectLines(s, TH_LINE);
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

static bool th_blocked = false;
void Th_SetInputBlocked(bool b) { th_blocked = b; }
bool Th_InputBlocked(void) { return th_blocked; }

#define TH_REG_MAX 384
static struct { char name[48]; Rectangle r; } th_reg[TH_REG_MAX];
static int th_reg_n = 0;

void Th_RegClear(void) { th_reg_n = 0; }

void Th_Reg(const char *name, Rectangle r)
{
    if (th_reg_n >= TH_REG_MAX) return;
    snprintf(th_reg[th_reg_n].name, sizeof th_reg[0].name, "%s", name);
    th_reg[th_reg_n].r = r;
    th_reg_n++;
}

void Th_RegLabel(const char *label, Rectangle r)
{
    char n[48];
    snprintf(n, sizeof n, "%s", label);
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

bool Th_Button(Rectangle r, const char *label)
{
    Rectangle s = th_snap(r);
    Th_RegLabel(label, s);
    bool hover = !th_blocked && CheckCollisionPointRec(GetMousePosition(), s);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    if (down) {
        th_draw_rounded_button(s, TH_PRESSED_FILL, TH_ACCENT);
        th_draw_label(s, label, TH_ACCENT);
    } else if (hover) {
        th_draw_rounded_button(s, TH_PANEL, TH_HOVER_BORDER);
        th_draw_label(s, label, TH_TEXT);
    } else {
        th_draw_rounded_button(s, TH_PANEL_ALT, TH_LINE);
        th_draw_label(s, label, TH_TEXT);
    }

    return clicked;
}

bool Th_ButtonDanger(Rectangle r, const char *label)
{
    Rectangle s = th_snap(r);
    Th_RegLabel(label, s);
    bool hover = !th_blocked && CheckCollisionPointRec(GetMousePosition(), s);
    bool down = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    Color red = TH_DANGER;
    if (down) {
        th_draw_rounded_button(s, (Color){0xFB, 0xE4, 0xE5, 0xFF}, red);
    } else if (hover) {
        th_draw_rounded_button(s, (Color){0xFD, 0xF1, 0xF1, 0xFF}, red);
    } else {
        th_draw_rounded_button(s, TH_PANEL_ALT, (Color){0xE9, 0xB6, 0xB8, 0xFF});
    }
    th_draw_label(s, label, red);
    return clicked;
}

bool Th_Toggle(Rectangle r, const char *label, bool *active)
{
    Rectangle s = th_snap(r);
    bool isActive = (active != NULL) && *active;
    Th_RegLabel(label, s);
    bool hover = !th_blocked && CheckCollisionPointRec(GetMousePosition(), s);
    bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    if (isActive) {
        DrawRectangleRounded(s, th_roundness(s), TH_BUTTON_SEGMENTS, TH_PANEL);
        DrawRectangleRounded(s, th_roundness(s), TH_BUTTON_SEGMENTS, TH_ACCENT_TINT);
        DrawRectangleRoundedLinesEx(s, th_roundness(s), TH_BUTTON_SEGMENTS, 1.0f, TH_ACCENT);
        th_draw_label(s, label, TH_ACCENT);
    } else if (hover) {
        th_draw_rounded_button(s, TH_PANEL, TH_HOVER_BORDER);
        th_draw_label(s, label, TH_TEXT);
    } else {
        th_draw_rounded_button(s, TH_PANEL_ALT, TH_LINE);
        th_draw_label(s, label, TH_TEXT);
    }

    if (clicked && (active != NULL)) *active = !*active;

    return clicked;
}

int Th_Segmented(Rectangle r, const char *const *labels, int n, int sel, unsigned disabledMask, const char *prefix)
{
    Rectangle s = th_snap(r);
    th_draw_rounded_button(s, (Color){ 0xE8, 0xE8, 0xEC, 0xFF }, (Color){ 0xE8, 0xE8, 0xEC, 0xFF });
    int clicked = -1;
    float x = s.x;
    for (int i = 0; i < n; i++) {
        float x1 = floorf(s.x + s.width * (float)(i + 1) / (float)n);
        Rectangle seg = { x, s.y, x1 - x, s.height };
        x = x1;
        char nm[64];
        snprintf(nm, sizeof nm, "%s_%s", prefix ? prefix : "seg", labels[i]);
        Th_RegLabel(nm, seg);
        bool dis = (disabledMask >> i) & 1u;
        bool hover = !dis && !th_blocked && CheckCollisionPointRec(GetMousePosition(), seg);
        Rectangle in = { seg.x + 2, seg.y + 2, seg.width - 4, seg.height - 4 };
        Color tc = TH_TEXT_DIM;
        if (dis) tc = (Color){ 0xB5, 0xB5, 0xBD, 0xFF };
        else if (i == sel) { th_draw_rounded_button(in, TH_PANEL, TH_LINE); tc = TH_ACCENT; }
        else if (hover) { DrawRectangleRounded(in, th_roundness(in), TH_BUTTON_SEGMENTS, (Color){ 0xFF, 0xFF, 0xFF, 0x96 }); tc = TH_TEXT; }
        th_draw_label(seg, labels[i], tc);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) clicked = i;
    }
    return clicked;
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
