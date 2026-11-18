/* colorpick.c - the colour picker of the design system (see colorpick.h and DESIGN.md, "Version 23").  Drawn with the theme's
 * parts: Th_Raised, Th_TextHead, Th_Segmented, Th_TextField, Th_Button; the sliders and planes are its own (no other view has one). */
#include "colorpick.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "colormath.h"
#include "theme.h"

#define CP_W 560
#define CP_PAD 24
#define CP_PLANE 200     /* the HSV square and the wheel */
#define CP_BAR 18        /* the hue and value bars beside them */
#define CP_TRACK 14      /* a slider's track */

static struct {
    bool open;
    int tag, answerTag, answer;     /* the answer waiting to be taken: 1 OK, -1 cancel */
    uint32_t old, answerColor;
    char title[64];
    int mode;
    int frames;                     /* drawn since it opened (the first frame takes no keys or presses) */
    double r, g, b, a;              /* the colour, sRGB 0..1 */
    double h, s, v;                 /* HSV (h is kept while the colour is grey) */
    double hs, hl;                  /* HSL saturation and lightness (hue: h) */
    double L, A, B;                 /* CIE Lab */
    char drag[16];                  /* the control being dragged */
    char hex[16];                   /* the hex field's text */
    Texture2D wheel;                /* the hue and saturation disc at value 1 (drawn tinted by the value) */
    bool wheelOk;
} cp = {.mode = TH_CP_HSV};

static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

/* the other models from r, g, b; a grey keeps the hue it had */
static void from_rgb(bool keepLab)
{
    double h, s, v, hh, hs, hl;
    cm_rgb_to_hsv(cp.r, cp.g, cp.b, &h, &s, &v);
    cm_rgb_to_hsl(cp.r, cp.g, cp.b, &hh, &hs, &hl);
    if (s > 1e-9) cp.h = h;
    if (v > 1e-9) cp.s = s;      /* black keeps its saturation too (the square's cursor stays on its column) */
    cp.v = v;
    if (hl > 1e-9 && hl < 1 - 1e-9) cp.hs = hs;
    cp.hl = hl;
    if (!keepLab) cm_rgb_to_lab(cp.r, cp.g, cp.b, &cp.L, &cp.A, &cp.B);
}

static void set_rgba(uint32_t c) { cm_unpack(c, &cp.r, &cp.g, &cp.b, &cp.a); from_rgb(false); }
static void set_hsv(void) { cm_hsv_to_rgb(cp.h, cp.s, cp.v, &cp.r, &cp.g, &cp.b); double h = cp.h, s = cp.s; from_rgb(false); cp.h = h; cp.s = s; }
static void set_hsl(void) { cm_hsl_to_rgb(cp.h, cp.hs, cp.hl, &cp.r, &cp.g, &cp.b); double h = cp.h, s = cp.hs; from_rgb(false); cp.h = h; cp.hs = s; }
static void set_lab(void) { cm_lab_to_rgb(cp.L, cp.A, cp.B, &cp.r, &cp.g, &cp.b); from_rgb(true); }
static uint32_t cur(void) { return cm_pack(cp.r, cp.g, cp.b, cp.a); }

void Th_ColorPickOpen(uint32_t rgba, const char *title, int tag)
{
    cp.open = true;
    cp.tag = tag;
    cp.answer = 0;
    cp.old = rgba;
    cp.frames = 0;
    cp.drag[0] = 0;
    cp.h = 0; cp.s = 0; cp.hs = 0;
    set_rgba(rgba);
    snprintf(cp.title, sizeof cp.title, "%s", title ? title : "Colour");
    cm_format_hex(rgba, cp.hex);
}

bool Th_ColorPickIsOpen(void) { return cp.open; }
int  Th_ColorPickMode(void) { return cp.mode; }
void Th_ColorPickSetMode(int m) { if (m >= 0 && m < TH_CP_MODES) cp.mode = m; }
uint32_t Th_ColorPickValue(void) { return cur(); }

int Th_ColorPickTake(int tag, uint32_t *rgba)
{
    if (!cp.answer || cp.answerTag != tag) return 0;
    int a = cp.answer;
    cp.answer = 0;
    if (a > 0 && rgba) *rgba = cp.answerColor;
    return a;
}

static void close_with(int answer)
{
    cp.answer = answer;
    cp.answerTag = cp.tag;
    cp.answerColor = cur();
    cp.open = false;
    cp.drag[0] = 0;
    if (Th_TextIs("cp.hex")) Th_TextBlur();
}

static Color col_of(double r, double g, double b, double a) { uint32_t c = cm_pack(r, g, b, a); return Th_FromRGBA(c); }

/* the transparency checker under a colour that is not opaque */
static void checker(Rectangle r)
{
    DrawRectangleRec(r, TH_PANEL);
    for (int y = 0; y * 6 < (int)r.height; y++)
        for (int x = 0; x * 6 < (int)r.width; x++)
            if ((x + y) & 1) {
                float w = fminf(6, r.width - x * 6), h = fminf(6, r.height - y * 6);
                DrawRectangle((int)r.x + x * 6, (int)r.y + y * 6, (int)w, (int)h, TH_TRACK);
            }
}

static bool hover(Rectangle r) { return !Th_MouseBlocked() && CheckCollisionPointRec(GetMousePosition(), r); }

/* a press on r starts a drag of `id`; true while it is dragged (the mouse is held) */
static bool dragging(const char *id, Rectangle r)
{
    Th_Reg(id, r);
    Rectangle hit = {r.x - 6, r.y - 6, r.width + 12, r.height + 12};
    if (cp.frames > 1 && hover(hit) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) snprintf(cp.drag, sizeof cp.drag, "%s", id);
    if (strcmp(cp.drag, id)) return false;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { cp.drag[0] = 0; }
    return true;
}

static void knob(float x, float y, bool hot)
{
    DrawCircle((int)x, (int)y + 1, 7.5f, Fade(BLACK, 0.09f));
    DrawCircle((int)x, (int)y, 7, TH_PANEL);
    DrawCircleLines((int)x, (int)y, 7, hot ? TH_ACCENT : TH_TEXT_OFF);
}

/* the colour at t (0..1) along a slider of channel k of the current model */
typedef Color (*TrackFn)(int k, double t);

static Color track_rgb(int k, double t) { double c[3] = {cp.r, cp.g, cp.b}; c[k] = t; return col_of(c[0], c[1], c[2], 1); }
static Color track_hsl(int k, double t)
{
    double h = cp.h, s = cp.hs, l = cp.hl, r, g, b;
    if (k == 0) h = t * 360; else if (k == 1) s = t; else l = t;
    cm_hsl_to_rgb(h, s, l, &r, &g, &b);
    return col_of(r, g, b, 1);
}
static Color track_lab(int k, double t)
{
    double L = cp.L, A = cp.A, B = cp.B, r, g, b;
    if (k == 0) L = t * 100; else if (k == 1) A = -128 + t * 255; else B = -128 + t * 255;
    cm_lab_to_rgb(L, A, B, &r, &g, &b);
    return col_of(r, g, b, 1);
}
static Color track_alpha(int k, double t) { (void)k; return col_of(cp.r, cp.g, cp.b, t); }

/* A horizontal slider of *t (0..1) whose track shows the colours along it; true when it changed.  Its caption and value are above it. */
static bool slider(const char *id, Rectangle r, const char *label, const char *value, double *t, TrackFn fn, int k)
{
    Th_TextSmall(label, r.x, r.y - 17, TH_TEXT_DIM);
    Vector2 vm = Th_MeasureSmall(value);
    Th_TextSmall(value, r.x + r.width - vm.x, r.y - 17, TH_TEXT);
    Rectangle tr = {r.x, r.y, r.width, CP_TRACK};
    if (fn == track_alpha) checker(tr);
    const int N = 32;
    for (int i = 0; i < N; i++) {
        float x0 = tr.x + tr.width * i / N, x1 = tr.x + tr.width * (i + 1) / N;
        DrawRectangleGradientH((int)x0, (int)tr.y, (int)ceilf(x1) - (int)x0, (int)tr.height, fn(k, (double)i / N), fn(k, (double)(i + 1) / N));
    }
    Th_RectLines(tr, TH_LINE);
    bool d = dragging(id, tr), changed = false;
    if (d) {
        double nt = clampd((GetMousePosition().x - tr.x) / tr.width, 0, 1);
        if (nt != *t) { *t = nt; changed = true; }
    }
    knob(tr.x + tr.width * (float)*t, tr.y + tr.height / 2, d || hover(tr));
    return changed;
}

/* the hue bar (vertical, red at the top) */
static bool hue_bar(Rectangle r)
{
    static const Color hues[7] = {{255, 0, 0, 255}, {255, 255, 0, 255}, {0, 255, 0, 255}, {0, 255, 255, 255}, {0, 0, 255, 255}, {255, 0, 255, 255}, {255, 0, 0, 255}};
    for (int i = 0; i < 6; i++) {
        float y0 = r.y + r.height * i / 6, y1 = r.y + r.height * (i + 1) / 6;
        DrawRectangleGradientV((int)r.x, (int)y0, (int)r.width, (int)ceilf(y1) - (int)y0, hues[i], hues[i + 1]);
    }
    Th_RectLines(r, TH_LINE);
    bool d = dragging("cp.hue", r), changed = false;
    if (d) {
        double h = clampd((GetMousePosition().y - r.y) / r.height, 0, 1) * 360;
        if (h >= 360) h = 359.999;
        if (h != cp.h) { cp.h = h; changed = true; }
    }
    float y = r.y + r.height * (float)(cp.h / 360);
    DrawRectangleLinesEx((Rectangle){r.x - 3, y - 3, r.width + 6, 6}, 2, TH_PANEL);
    DrawRectangleLinesEx((Rectangle){r.x - 4, y - 4, r.width + 8, 8}, 1, d || hover(r) ? TH_ACCENT : TH_TEXT_DIM);
    return changed;
}

/* the value bar beside the wheel (vertical, 1 at the top) */
static bool value_bar(Rectangle r)
{
    double rr, gg, bb;
    cm_hsv_to_rgb(cp.h, cp.s, 1, &rr, &gg, &bb);
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, col_of(rr, gg, bb, 1), BLACK);
    Th_RectLines(r, TH_LINE);
    bool d = dragging("cp.val", r), changed = false;
    if (d) {
        double v = 1 - clampd((GetMousePosition().y - r.y) / r.height, 0, 1);
        if (v != cp.v) { cp.v = v; changed = true; }
    }
    float y = r.y + r.height * (float)(1 - cp.v);
    DrawRectangleLinesEx((Rectangle){r.x - 3, y - 3, r.width + 6, 6}, 2, TH_PANEL);
    DrawRectangleLinesEx((Rectangle){r.x - 4, y - 4, r.width + 8, 8}, 1, d || hover(r) ? TH_ACCENT : TH_TEXT_DIM);
    return changed;
}

/* the ring cursor of a plane at (x, y), dark on a light colour */
static void plane_cursor(float x, float y)
{
    bool light = cm_luminance(cm_pack(cp.r, cp.g, cp.b, 1)) > 0.36;
    DrawCircleLines((int)x, (int)y, 6, light ? TH_TEXT : TH_PANEL);
    DrawCircleLines((int)x, (int)y, 7, light ? TH_PANEL : TH_TEXT);
}

/* the saturation (across) and value (down) square of the hue */
static bool sv_square(Rectangle r)
{
    double hr, hg, hb;
    cm_hsv_to_rgb(cp.h, 1, 1, &hr, &hg, &hb);
    DrawRectangleGradientH((int)r.x, (int)r.y, (int)r.width, (int)r.height, WHITE, col_of(hr, hg, hb, 1));
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, (Color){0, 0, 0, 0}, BLACK);
    Th_RectLines(r, TH_LINE);
    bool d = dragging("cp.sv", r), changed = false;
    if (d) {
        Vector2 m = GetMousePosition();
        cp.s = clampd((m.x - r.x) / r.width, 0, 1);
        cp.v = 1 - clampd((m.y - r.y) / r.height, 0, 1);
        changed = true;
    }
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    plane_cursor(r.x + r.width * (float)cp.s, r.y + r.height * (float)(1 - cp.v));
    EndScissorMode();
    return changed;
}

static void make_wheel(void)
{
    const int n = 256;
    Image im = GenImageColor(n, n, (Color){0, 0, 0, 0});
    Color *px = (Color *)im.data;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            double dx = (x + 0.5 - n / 2.0) / (n / 2.0), dy = (y + 0.5 - n / 2.0) / (n / 2.0), d = sqrt(dx * dx + dy * dy);
            if (d > 1.0 + 1.0 / n) continue;
            double h = atan2(-dy, dx) * 180 / 3.14159265358979, r, g, b;
            if (h < 0) h += 360;
            cm_hsv_to_rgb(h, d > 1 ? 1 : d, 1, &r, &g, &b);
            double a = d > 1 ? 1 - (d - 1) * n : 1;    /* a soft edge one pixel wide */
            px[y * n + x] = col_of(r, g, b, clampd(a, 0, 1));
        }
    cp.wheel = LoadTextureFromImage(im);
    SetTextureFilter(cp.wheel, TEXTURE_FILTER_BILINEAR);
    UnloadImage(im);
    cp.wheelOk = true;
}

/* the hue (angle, red to the right, counter-clockwise) and saturation (radius) disc, drawn at the value */
static bool wheel(Rectangle r)
{
    if (!cp.wheelOk) make_wheel();
    unsigned char v = (unsigned char)lround(cp.v * 255);
    DrawTexturePro(cp.wheel, (Rectangle){0, 0, (float)cp.wheel.width, (float)cp.wheel.height}, r, (Vector2){0, 0}, 0, (Color){v, v, v, 255});
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2, R = r.width / 2;
    DrawCircleLines((int)cx, (int)cy, R, TH_LINE);
    bool d = dragging("cp.wheel", r), changed = false;
    if (d) {
        Vector2 m = GetMousePosition();
        double dx = (m.x - cx) / R, dy = (m.y - cy) / R, dist = sqrt(dx * dx + dy * dy);
        double h = atan2(-dy, dx) * 180 / 3.14159265358979;
        if (h < 0) h += 360;
        cp.h = h;
        cp.s = dist > 1 ? 1 : dist;
        changed = true;
    }
    double a = cp.h * 3.14159265358979 / 180;
    plane_cursor(cx + (float)(cos(a) * cp.s * R), cy - (float)(sin(a) * cp.s * R));
    return changed;
}

static const char *const kModes[TH_CP_MODES] = {"HSV", "Wheel", "RGB", "HSL", "Lab"};
static const char *const kModeHints[TH_CP_MODES] = {"Hue bar and a saturation/value square", "Hue and saturation disc and a value bar", "Red, green and blue sliders",
                                                  "Hue, saturation and lightness sliders", "CIE Lab (D65): lightness, a (green-red) and b (blue-yellow)"};

void Th_ColorPickDraw(int W, int H)
{
    if (!cp.open) return;
    cp.frames++;
    bool fresh = cp.frames <= 1;
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    float ph = CP_PAD + TH_SIZE_HEAD + 14 + TH_ROW_H + 30 + CP_PLANE + 40 + CP_TRACK + 24 + CP_PAD;
    Rectangle pr = {floorf((W - CP_W) / 2.0f), floorf((H - ph) / 2.5f), CP_W, ph};
    if (pr.y < 4) pr.y = 4;
    Th_Raised(pr, 1);
    float x = pr.x + CP_PAD, y = pr.y + CP_PAD;
    Th_TextHead(cp.title, x, y, TH_TEXT);
    y += TH_SIZE_HEAD + 14;

    ThSeg sg;
    memset(&sg, 0, sizeof sg);
    sg.labels = kModes; sg.n = TH_CP_MODES; sg.sel = cp.mode; sg.prefix = "cpmode"; sg.hints = kModeHints;
    int hit = Th_Segmented((Rectangle){x, y, CP_W - 2 * CP_PAD, TH_ROW_H}, &sg);
    if (hit >= 0) cp.mode = hit;
    y += TH_ROW_H + 30;

    /* the left part: the model's plane or sliders */
    float lw = CP_PLANE + 16 + CP_BAR;   /* width of the left part */
    char v1[24], v2[24], v3[24];
    switch (cp.mode) {
    case TH_CP_HSV:
        if (sv_square((Rectangle){x, y - 14, CP_PLANE, CP_PLANE})) set_hsv();
        if (hue_bar((Rectangle){x + CP_PLANE + 16, y - 14, CP_BAR, CP_PLANE})) set_hsv();
        break;
    case TH_CP_WHEEL:
        if (wheel((Rectangle){x, y - 14, CP_PLANE, CP_PLANE})) set_hsv();
        if (value_bar((Rectangle){x + CP_PLANE + 16, y - 14, CP_BAR, CP_PLANE})) set_hsv();
        break;
    default: {
        double t[3];
        TrackFn fn;
        const char *lab[3];
        if (cp.mode == TH_CP_RGB) {
            t[0] = cp.r; t[1] = cp.g; t[2] = cp.b; fn = track_rgb;
            lab[0] = "Red"; lab[1] = "Green"; lab[2] = "Blue";
            snprintf(v1, sizeof v1, "%d", (int)lround(cp.r * 255)); snprintf(v2, sizeof v2, "%d", (int)lround(cp.g * 255)); snprintf(v3, sizeof v3, "%d", (int)lround(cp.b * 255));
        } else if (cp.mode == TH_CP_HSL) {
            t[0] = cp.h / 360; t[1] = cp.hs; t[2] = cp.hl; fn = track_hsl;
            lab[0] = "Hue"; lab[1] = "Saturation"; lab[2] = "Lightness";
            snprintf(v1, sizeof v1, "%d\xC2\xB0", (int)lround(cp.h)); snprintf(v2, sizeof v2, "%d%%", (int)lround(cp.hs * 100)); snprintf(v3, sizeof v3, "%d%%", (int)lround(cp.hl * 100));
        } else {
            t[0] = cp.L / 100; t[1] = (cp.A + 128) / 255; t[2] = (cp.B + 128) / 255; fn = track_lab;
            lab[0] = "L*"; lab[1] = "a*"; lab[2] = "b*";
            snprintf(v1, sizeof v1, "%.1f", cp.L); snprintf(v2, sizeof v2, "%.1f", cp.A); snprintf(v3, sizeof v3, "%.1f", cp.B);
        }
        const char *val[3] = {v1, v2, v3};
        bool ch = false;
        for (int k = 0; k < 3; k++) {
            char id[12];
            snprintf(id, sizeof id, "cp.s%d", k);
            double tk = clampd(t[k], 0, 1);
            if (slider(id, (Rectangle){x, y + k * 62, lw, CP_TRACK}, lab[k], val[k], &tk, fn, k)) { t[k] = tk; ch = true; }
        }
        if (ch) {
            if (cp.mode == TH_CP_RGB) { cp.r = t[0]; cp.g = t[1]; cp.b = t[2]; from_rgb(false); }
            else if (cp.mode == TH_CP_HSL) { cp.h = fmin(t[0] * 360, 359.999); cp.hs = t[1]; cp.hl = t[2]; set_hsl(); }
            else { cp.L = t[0] * 100; cp.A = -128 + t[1] * 255; cp.B = -128 + t[2] * 255; set_lab(); }
        }
        break;
    }
    }
    {   /* alpha, under the left part in every model */
        char av[16];
        snprintf(av, sizeof av, "%d", (int)lround(cp.a * 255));
        double ta = cp.a;
        if (slider("cp.alpha", (Rectangle){x, y + CP_PLANE + 26, lw, CP_TRACK}, "Alpha", av, &ta, track_alpha, 0)) cp.a = ta;
    }

    /* the right part: new and old, the hex field, the values, OK and Cancel */
    float rx = x + lw + 32, rw = pr.x + pr.width - CP_PAD - rx, ry = y - 14;
    {
        float sw = floorf((rw - 8) / 2);
        Rectangle nr = {rx, ry + 18, sw, 64}, orr = {rx + sw + 8, ry + 18, sw, 64};
        Th_TextCap("New", nr.x, ry, TH_TEXT_DIM);
        Th_TextCap("Old", orr.x, ry, TH_TEXT_DIM);
        checker(nr); checker(orr);
        DrawRectangleRec(nr, Th_FromRGBA(cur()));
        DrawRectangleRec(orr, Th_FromRGBA(cp.old));
        Th_RectLines(nr, TH_LINE);
        bool oh = hover(orr) && !fresh;
        Th_RectLines(orr, oh ? TH_HOVER_BORDER : TH_LINE);
        Th_Reg("cp.new", nr);
        Th_Reg("cp.old", orr);
        if (Th_Released(orr, "cp.old", oh)) set_rgba(cp.old);   /* a click on the old colour takes it back */
        ry += 18 + 64 + 18;
    }
    {
        Th_TextCap("Hex", rx, ry, TH_TEXT_DIM);
        ry += 18;
        if (!Th_TextIs("cp.hex")) cm_format_hex(cur(), cp.hex);
        int tf = Th_TextField((Rectangle){rx, ry, rw, TH_ROW_H}, "cp.hex", cp.hex, 10, TH_TF_HEX | TH_TF_SELECT, "RRGGBB");
        if (tf & TH_TF_CHANGED) {
            snprintf(cp.hex, sizeof cp.hex, "%s", Th_TextValue());
            uint32_t c;
            if (cm_parse_hex(cp.hex, &c)) { cm_unpack(c, &cp.r, &cp.g, &cp.b, &cp.a); from_rgb(false); }
        }
        if (tf & TH_TF_ENTER) { uint32_t c; if (cm_parse_hex(cp.hex, &c)) { cm_unpack(c, &cp.r, &cp.g, &cp.b, &cp.a); from_rgb(false); } if (!IsKeyPressed(KEY_TAB)) { close_with(1); return; } }   /* Tab takes the value and leaves the field */
        if (tf & TH_TF_ESC) { close_with(-1); return; }
        ry += TH_ROW_H + 12;
    }
    {   /* the colour in the other models, small */
        char l1[64], l2[64], l3[64];
        snprintf(l1, sizeof l1, "RGB %d, %d, %d", (int)lround(cp.r * 255), (int)lround(cp.g * 255), (int)lround(cp.b * 255));
        snprintf(l2, sizeof l2, "HSV %d\xC2\xB0 %d%% %d%%", (int)lround(cp.h), (int)lround(cp.s * 100), (int)lround(cp.v * 100));
        snprintf(l3, sizeof l3, "Lab %.0f, %.0f, %.0f", cp.L, cp.A, cp.B);
        Th_TextSmall(l1, rx, ry, TH_TEXT_DIM);
        Th_TextSmall(l2, rx, ry + 18, TH_TEXT_DIM);
        Th_TextSmall(l3, rx, ry + 36, TH_TEXT_DIM);
    }
    {
        float by = pr.y + pr.height - CP_PAD - TH_ROW_H, ow = Th_ButtonW("OK") + 24, cw = Th_ButtonW("Cancel");
        Rectangle okr = {pr.x + pr.width - CP_PAD - ow, by, ow, TH_ROW_H}, car = {okr.x - TH_GAP - cw, by, cw, TH_ROW_H};
        Th_Reg("cp.ok", okr);
        Th_Reg("cp.cancel", car);
        bool cancel = Th_Button(car, "Cancel", 0, NULL);
        bool ok = Th_Button(okr, "OK", TH_BTN_PRIMARY, NULL);
        if (!fresh && cancel) { close_with(-1); return; }
        if (!fresh && ok) { close_with(1); return; }
    }
    if (!fresh && !Th_TextIs("cp.hex")) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) close_with(1);
        else if (IsKeyPressed(KEY_ESCAPE)) close_with(-1);
    }
}
