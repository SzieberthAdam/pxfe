/* sample.c - the shared sample text, its renderer, the multi-line editor widget and the font view's sample strip
 * (see sample.h and DESIGN.md, "Samples"). */
#include "sample.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SAMPLE "The quick brown fox jumps over the lazy dog"

static char g_text[SAMPLE_MAX + 1] = DEFAULT_SAMPLE;
static unsigned g_counter = 1;
static char g_ini[1100];
static bool g_dirty;
static double g_dirtyT;
static int (*g_charSrc)(void) = GetCharPressed;

/* ------------------------------------------------------------------------------------------------ ini helpers */

static char *ini_line_trim(char *s)
{
    size_t l = strlen(s);
    while (l && (s[l - 1] == '\r' || s[l - 1] == '\n')) s[--l] = 0;
    return s;
}

bool SampleIni_Get(const char *path, const char *section, const char *key, char *out, size_t cap)
{
    size_t n = 0;
    char *t = path && path[0] ? px_read_file(path, &n) : NULL;
    if (!t) return false;
    char hdr[80];
    snprintf(hdr, sizeof hdr, "[%s]", section);
    size_t kl = strlen(key);
    bool in = false, found = false;
    for (char *line = t; line && *line;) {
        char *e = strchr(line, '\n');
        if (e) *e = 0;
        ini_line_trim(line);
        if (line[0] == '[') in = !strcmp(line, hdr);
        else if (in && !strncmp(line, key, kl) && line[kl] == '=') { snprintf(out, cap, "%s", line + kl + 1); found = true; }
        line = e ? e + 1 : NULL;
    }
    free(t);
    return found;
}

void SampleIni_Set(const char *path, const char *section, const char *key, const char *val)
{
    if (!path || !path[0]) return;
    size_t n = 0;
    char *t = px_read_file(path, &n);
    size_t cap = (t ? n : 0) + strlen(key) + strlen(val) + strlen(section) * 2 + 128;
    char *o = (char *)malloc(cap);
    char hdr[80];
    snprintf(hdr, sizeof hdr, "[%s]", section);
    size_t on = 0, kl = strlen(key);
    bool in = false, sawSection = false, wrote = false;
    for (char *line = t; line && *line;) {
        char *e = strchr(line, '\n');
        if (e) *e = 0;
        ini_line_trim(line);
        bool skip = false;
        if (line[0] == '[') {
            if (in && !wrote) { on += (size_t)snprintf(o + on, cap - on, "%s=%s\n", key, val); wrote = true; }
            in = !strcmp(line, hdr);
            if (in) sawSection = true;
        } else if (in && !strncmp(line, key, kl) && line[kl] == '=') skip = true;
        if (!skip) on += (size_t)snprintf(o + on, cap - on, "%s\n", line);
        line = e ? e + 1 : NULL;
    }
    if (in && !wrote) { on += (size_t)snprintf(o + on, cap - on, "%s=%s\n", key, val); wrote = true; }
    if (!sawSection) on += (size_t)snprintf(o + on, cap - on, "%s\n%s=%s\n", hdr, key, val);
    px_write_file(path, o, on);
    free(o);
    free(t);
}

/* ------------------------------------------------------------------------------------------------ the setting */

const char *Sample_Get(void) { return g_text; }
unsigned Sample_Counter(void) { return g_counter; }
void Sample_SetCharSource(int (*fn)(void)) { g_charSrc = fn ? fn : GetCharPressed; }

int Sample_LineCount(const char *text)
{
    int n = 1;
    for (; *text; text++) if (*text == '\n') n++;
    return n;
}

static int utf8_len(unsigned char c) { return c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; }

/* the text made valid: \r\n and \r become \n, tabs become spaces, other control characters go, limits apply */
static int sanitize(const char *in, char *out, int cap, int linesAlready)
{
    int k = 0, lines = linesAlready;
    for (int i = 0; in[i];) {
        unsigned char c = (unsigned char)in[i];
        char one[4];
        int m = 0, adv = 1;
        if (c == 13) { adv = in[i + 1] == 10 ? 2 : 1; c = 10; }
        if (c == 10) { if (lines < SAMPLE_MAX_LINES) { lines++; one[m++] = 10; } }
        else if (c == 9) one[m++] = 32;
        else if (c < 32 || c == 127) { /* dropped */ }
        else {
            int L = utf8_len(c);
            for (int j = 1; j < L; j++) if (!in[i + j]) { L = j; break; }
            memcpy(one, in + i, (size_t)L);
            m = L; adv = L;
        }
        i += adv;
        if (m) { if (k + m > cap) break; memcpy(out + k, one, (size_t)m); k += m; }
    }
    out[k] = 0;
    return k;
}

void Sample_Set(const char *utf8)
{
    char tmp[SAMPLE_MAX + 8];
    sanitize(utf8 ? utf8 : "", tmp, SAMPLE_MAX, 1);
    if (!strcmp(tmp, g_text)) return;
    strcpy(g_text, tmp);
    g_counter++;
    g_dirty = true;
    g_dirtyT = GetTime();
}

static void escape_into(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    for (; *s && o + 3 < cap; s++) {
        if (*s == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (*s == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
        else out[o++] = *s;
    }
    out[o] = 0;
}

static void unescape_inplace(char *s)
{
    char *w = s;
    for (const char *r = s; *r; r++) {
        if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
        else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r++; }
        else *w++ = *r;
    }
    *w = 0;
}

void Sample_Flush(void)
{
    if (!g_dirty) return;
    g_dirty = false;
    if (!g_ini[0]) return;
    char esc[SAMPLE_MAX * 2 + 8];
    escape_into(g_text, esc, sizeof esc);
    SampleIni_Set(g_ini, "app", "sample", esc);
}

void Sample_Tick(void)
{
    if (g_dirty && GetTime() - g_dirtyT > 0.5) Sample_Flush();
}

void Sample_LoadIni(const char *iniPath)
{
    snprintf(g_ini, sizeof g_ini, "%s", iniPath ? iniPath : "");
    char v[SAMPLE_MAX * 2 + 8];
    if (SampleIni_Get(g_ini, "app", "sample", v, sizeof v)) {
        unescape_inplace(v);
        char tmp[SAMPLE_MAX + 8];
        sanitize(v, tmp, SAMPLE_MAX, 1);
        if (strcmp(tmp, g_text)) { strcpy(g_text, tmp); g_counter++; }
    } else if (SampleIni_Get(g_ini, "browser", "sample", v, sizeof v)) {   /* the old place: migrate it */
        char tmp[SAMPLE_MAX + 8];
        sanitize(v, tmp, SAMPLE_MAX, 1);
        if (strcmp(tmp, g_text)) { strcpy(g_text, tmp); g_counter++; }
        g_dirty = true;
        Sample_Flush();
    }
    SampleStrip_Load(g_ini);
}

/* ------------------------------------------------------------------------------------------------ layout */

int Sample_Layout(const PxFont *f, const char *text, int boxAbove, int boxBelow, int lineGap, int maxLines, SampleLayout *out)
{
    memset(out, 0, sizeof *out);
    out->wantDefault = PX_NO_CODE;
    int a = boxAbove, b = boxBelow;
    if (a <= 0 && b <= 0) { if (!px_font_box(f, &a, &b)) { a = 8; b = 2; } }
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    out->above = a; out->below = b;
    int gap = lineGap >= 0 ? lineGap : (f->has[PXM_LINEGAP] ? f->m[PXM_LINEGAP] : 1);
    out->lineH = a + b + gap;
    if (out->lineH < 1) out->lineH = 1;
    int maxL = maxLines > 0 && maxLines < SAMPLE_MAX_LINES ? maxLines : SAMPLE_MAX_LINES;
    out->runs = (SampleRun *)malloc(sizeof(SampleRun) * (strlen(text) + 2));
    out->nlines = 1;
    int di = px_font_default_index(f);
    int bh = f->has[PXM_CAPHEIGHT] && f->m[PXM_CAPHEIGHT] > 0 ? f->m[PXM_CAPHEIGHT] : (a > 2 ? a * 2 / 3 : 3);
    if (bh < 3) bh = 3;
    int bw = bh * 2 / 3;
    if (bw < 2) bw = 2;
    int pen = 0;
    PxCode prev = PX_NO_CODE;
    const char *p = text;
    while (*p) {
        int sz = 1;
        int cp = GetCodepointNext(p, &sz);
        p += sz > 0 ? sz : 1;
        if (cp == '\n') {
            out->lineW[out->nlines - 1] = pen;
            if (out->width < pen) out->width = pen;
            if (out->nlines >= maxL) { pen = -1; break; }
            out->lineStart[out->nlines] = out->nrun;
            out->nlines++;
            pen = 0; prev = PX_NO_CODE;
            continue;
        }
        if (cp < 32 || cp == 127) continue;
        int gi = px_font_find_cp(f, cp);
        bool fallback = false;
        if (gi < 0 && di >= 0) { gi = di; fallback = true; }
        PxCode code = gi >= 0 ? f->glyphs[gi].code : px_code_cp(cp);   /* kerning names the glyph drawn: the default glyph for a missing one */
        if (prev != PX_NO_CODE) pen += px_font_kern(f, prev, code);
        SampleRun *r = &out->runs[out->nrun++];
        if (gi >= 0) {
            const PxGlyph *g = &f->glyphs[gi];
            if (g->lazy && g->hash[0] && fallback) out->wantDefault = PX_CODE_DEFAULT;
            r->gi = gi; r->x = pen; r->w = g->w; r->h = g->h;
            pen += g->advance;
        } else {
            r->gi = cp == 32 ? -2 : -1; r->x = pen; r->w = bw; r->h = bh;
            pen += bw + 1;
        }
        prev = code;
    }
    if (pen >= 0) {
        out->lineW[out->nlines - 1] = pen;
        if (out->width < pen) out->width = pen;
    }
    out->lineStart[out->nlines] = out->nrun;
    return out->nlines;
}

void Sample_LayoutFree(SampleLayout *L) { free(L->runs); L->runs = NULL; L->nrun = 0; }

void Sample_Measure(const PxFont *f, const char *text, int boxAbove, int boxBelow, int lineGap, int maxLines, int *w, int *h, int *lines)
{
    SampleLayout L;
    Sample_Layout(f, text, boxAbove, boxBelow, lineGap, maxLines, &L);
    if (w) *w = L.width;
    if (h) *h = (L.nlines - 1) * L.lineH + L.above + L.below;
    if (lines) *lines = L.nlines;
    Sample_LayoutFree(&L);
}

bool Sample_Render(const PxFont *f, const SampleLayout *L, Color ink, int maxW, SampleBitmap *out)
{
    memset(out, 0, sizeof *out);
    int top = L->above, bot = L->below;
    for (int i = 0; i < L->nrun; i++) {
        const SampleRun *r = &L->runs[i];
        if (r->gi >= 0) {
            const PxGlyph *g = &f->glyphs[r->gi];
            if (g->w > 0 && g->pix) { if (g->y + g->h > top) top = g->y + g->h; if (-g->y > bot) bot = -g->y; }
        } else if (r->gi == -1 && r->h > top) top = r->h;
    }
    int W = L->width < 1 ? 1 : L->width;
    int cap = maxW > 0 ? maxW : 2048;
    if (W > cap) W = cap;
    int H = top + (L->nlines - 1) * L->lineH + bot;
    if (H < 1) H = 1;
    unsigned char *px = (unsigned char *)calloc((size_t)W * (size_t)H, 4);
    if (!px) return false;
    bool colour = px_font_is_colour(f);
    const uint8_t inkRgba[4] = {ink.r, ink.g, ink.b, ink.a};
    for (int line = 0; line < L->nlines; line++) {
        int baseRow = top + line * L->lineH;
        for (int i = L->lineStart[line]; i < L->lineStart[line + 1]; i++) {
            const SampleRun *r = &L->runs[i];
            if (r->gi >= 0) {
                const PxGlyph *g = &f->glyphs[r->gi];
                if (g->w <= 0 || g->h <= 0 || !g->pix) continue;
                const uint8_t (*pal)[4] = px_glyph_palette(f, g)->draw;   /* the glyph's own colours, ready to draw */
                for (int row = 0; row < g->h; row++) {
                    int y = baseRow - (g->y + g->h) + row;
                    if (y < 0 || y >= H) continue;
                    for (int col = 0; col < g->w; col++) {
                        char ch = g->pix[row * g->w + col];
                        if (ch == '.') continue;
                        int x = r->x + g->x + col;
                        if (x < 0 || x >= W) continue;
                        int pi = px_pixel_index(ch);
                        const uint8_t *k = colour && pi > 0 ? pal[pi] : inkRgba;
                        unsigned char *d = px + ((size_t)y * (size_t)W + (size_t)x) * 4;
                        d[0] = k[0]; d[1] = k[1]; d[2] = k[2]; d[3] = k[3];
                    }
                }
            } else if (r->gi == -1) {
                for (int yy = 0; yy < r->h; yy++)
                    for (int xx = 0; xx < r->w; xx++) {
                        if (yy != 0 && yy != r->h - 1 && xx != 0 && xx != r->w - 1) continue;
                        int x = r->x + xx, y = baseRow - r->h + yy;
                        if (x < 0 || x >= W || y < 0 || y >= H) continue;
                        unsigned char *d = px + ((size_t)y * (size_t)W + (size_t)x) * 4;
                        d[0] = ink.r; d[1] = ink.g; d[2] = ink.b; d[3] = 150;
                    }
            }
        }
    }
    out->px = px; out->w = W; out->h = H; out->baseline = top;
    return true;
}

void SampleCache_Free(SampleCache *c)
{
    if (c->has) UnloadTexture(c->tex);
    memset(c, 0, sizeof *c);
}

static unsigned mix(unsigned h, unsigned v) { h ^= v; h *= 16777619u; return h; }

float Sample_Draw(SampleCache *c, const PxFont *f, const char *text, float x, float y, int zoom, float maxWidth,
                  int maxLines, const SampleStyle *st, int *outW)
{
    SampleStyle def;
    memset(&def, 0, sizeof def);
    def.lineGap = -1;
    if (!st) st = &def;
    Color ink = st->ink.a ? st->ink : TH_INK;
    if (zoom < 1) zoom = 1;
    SampleLayout L;
    Sample_Layout(f, text, st->boxAbove, st->boxBelow, st->lineGap, maxLines, &L);
    unsigned key = 2166136261u;
    key = mix(key, (unsigned)L.above); key = mix(key, (unsigned)L.below); key = mix(key, (unsigned)L.lineH);
    key = mix(key, ink.r | (unsigned)ink.g << 8 | (unsigned)ink.b << 16 | (unsigned)ink.a << 24);
    key = mix(key, px_font_is_colour(f));
    key = mix(key, f->palGen);              /* with each glyph's pi below: its palette (entries never change) */
    key = mix(key, (unsigned)L.nlines);
    for (int ln = 0; ln < L.nlines; ln++) key = mix(key, (unsigned)L.lineStart[ln + 1]);
    for (int i = 0; i < L.nrun; i++) {
        const SampleRun *r = &L.runs[i];
        key = mix(key, (unsigned)r->gi); key = mix(key, (unsigned)r->x); key = mix(key, (unsigned)r->w); key = mix(key, (unsigned)r->h);
        if (r->gi >= 0) {
            const PxGlyph *g = &f->glyphs[r->gi];
            key = mix(key, (unsigned)g->x); key = mix(key, (unsigned)g->y); key = mix(key, (unsigned)g->pi);
            if (g->pix) for (int k = 0; k < g->w * g->h; k++) key = mix(key, (unsigned char)g->pix[k]);
        }
    }
    if (!c->has || c->key != key) {
        SampleBitmap bm;
        if (c->has) { UnloadTexture(c->tex); c->has = false; }
        if (Sample_Render(f, &L, ink, 0, &bm)) {
            Image img = { bm.px, bm.w, bm.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            c->tex = LoadTextureFromImage(img);
            free(bm.px);
            SetTextureFilter(c->tex, TEXTURE_FILTER_POINT);
            c->has = c->tex.id != 0;
            c->w = bm.w; c->h = bm.h; c->baseline = bm.baseline;
        }
        c->key = key;
    }
    float height = (float)(((L.nlines - 1) * L.lineH + L.above + L.below) * zoom);
    Rectangle clip = st->useClip ? st->clip : (Rectangle){ x, y - 2000, maxWidth, 4000 + height };
    if (c->has) {
        float baselineY = floorf(y) + (float)(L.above * zoom);
        Th_DrawTextureClipped(c->tex, floorf(x), baselineY - (float)(c->baseline * zoom), zoom, clip, WHITE);
    }
    if (st->bg.a && (float)(L.width * zoom) > clip.width - 4) {
        Color bg = st->bg;
        Color clear = bg; clear.a = 0;
        float y0 = fmaxf(clip.y, y), y1 = fminf(clip.y + clip.height, y + height);
        if (y1 > y0) DrawRectangleGradientH((int)(clip.x + clip.width) - 28, (int)y0, 28, (int)(y1 - y0), clear, bg);
    }
    if (outW) *outW = L.width;
    Sample_LayoutFree(&L);
    return height;
}

/* ------------------------------------------------------------------------------------------------ the editor widget */

#define LH 22          /* line height of the box */
#define EPAD 4

static SampleEdit *g_focus;

bool SampleEdit_Focused(const SampleEdit *e) { return g_focus == e; }
bool Sample_TextFocused(void) { return g_focus != NULL; }

void SampleEdit_SetFocus(SampleEdit *e, bool on)
{
    if (on) { g_focus = e; e->cur = (int)strlen(g_text); e->hasSel = false; e->pxCol = -1; }
    else if (g_focus == e) { g_focus = NULL; Sample_Flush(); }
}

float SampleEdit_Height(int maxLines)
{
    int n = Sample_LineCount(g_text);
    if (n > maxLines) n = maxLines;
    if (n < 1) n = 1;
    return (float)(n * LH + 2 * EPAD);
}

static int e_prev(int i) { if (i > 0) i--; while (i > 0 && ((unsigned char)g_text[i] & 0xC0) == 0x80) i--; return i; }
static int e_next(int i, int len) { if (i < len) i++; while (i < len && ((unsigned char)g_text[i] & 0xC0) == 0x80) i++; return i; }
static int e_ls(int pos) { while (pos > 0 && g_text[pos - 1] != '\n') pos--; return pos; }
static int e_le(int pos) { while (g_text[pos] && g_text[pos] != '\n') pos++; return pos; }

/* width in pixels of the bytes [a, b) of the text (one line) */
static float e_mx(int a, int b)
{
    if (b <= a) return 0;
    char tmp[SAMPLE_MAX + 2];
    int n = b - a;
    memcpy(tmp, g_text + a, (size_t)n);
    tmp[n] = 0;
    return Th_Measure(tmp).x;
}

static int e_pos_at(int ls, int le, float x)
{
    int best = ls;
    float bd = 1e9f;
    for (int i = ls;; i = e_next(i, le)) {
        float d = fabsf(e_mx(ls, i) - x);
        if (d < bd) { bd = d; best = i; }
        if (i >= le) break;
    }
    return best;
}

static int e_line_of(int pos) { int n = 0; for (int i = 0; i < pos; i++) if (g_text[i] == '\n') n++; return n; }
static int e_line_start(int line) { int i = 0; while (line > 0 && g_text[i]) { if (g_text[i] == '\n') line--; i++; } return i; }

static bool e_replace(SampleEdit *e, int a, int b, const char *s)
{
    int len = (int)strlen(g_text);
    if (a > b) { int t = a; a = b; b = t; }
    int removedLines = 0;
    for (int i = a; i < b; i++) if (g_text[i] == '\n') removedLines++;
    char tmp[SAMPLE_MAX + 8];
    int room = SAMPLE_MAX - (len - (b - a));
    if (room < 0) room = 0;
    int k = sanitize(s, tmp, room, Sample_LineCount(g_text) - removedLines);
    if (k == 0 && a == b) return false;
    memmove(g_text + a + k, g_text + b, (size_t)(len - b) + 1);
    memcpy(g_text + a, tmp, (size_t)k);
    e->cur = a + k;
    e->hasSel = false;
    e->pxCol = -1;
    g_counter++;
    g_dirty = true;
    g_dirtyT = GetTime();
    return true;
}

static void e_move(SampleEdit *e, int np, bool shift)
{
    if (shift) { if (!e->hasSel) { e->anchor = e->cur; e->hasSel = true; } }
    else e->hasSel = false;
    e->cur = np;
    if (e->hasSel && e->anchor == e->cur) e->hasSel = false;
}

static void e_sel_range(const SampleEdit *e, int *a, int *b)
{
    if (!e->hasSel) { *a = *b = e->cur; return; }
    *a = e->anchor < e->cur ? e->anchor : e->cur;
    *b = e->anchor < e->cur ? e->cur : e->anchor;
}

static void rr_fill(int x, int y, int w, int h, int rad, Color c)
{
    if (w <= 0 || h <= 0) return;
    if (rad * 2 > h) rad = h / 2;
    for (int i = 0; i < rad; i++) {
        float d = rad - i - 0.5f;
        int in = rad - (int)(sqrtf((float)(rad * rad) - d * d) + 0.5f);
        DrawRectangle(x + in, y + i, w - 2 * in, 1, c);
        DrawRectangle(x + in, y + h - 1 - i, w - 2 * in, 1, c);
    }
    if (h - 2 * rad > 0) DrawRectangle(x, y + rad, w, h - 2 * rad, c);
}

#define RP(k) (IsKeyPressed(k) || IsKeyPressedRepeat(k))

bool SampleEdit_Do(SampleEdit *e, Rectangle r, const char *placeholder, const char *regName, bool blocked, const Rectangle *mouseClip)
{
    bool changed = false;
    Sample_Tick();
    Th_Reg(regName, r);
    int len = (int)strlen(g_text);
    if (e->cur > len) e->cur = len;
    if (e->anchor > len) e->anchor = len;
    Vector2 m = GetMousePosition();
    bool over = !blocked && CheckCollisionPointRec(m, r) && (!mouseClip || CheckCollisionPointRec(m, *mouseClip));
    bool focused = g_focus == e;
    if (blocked && focused) { SampleEdit_SetFocus(e, false); focused = false; }

    float textX = r.x + 8;
    int nlines = Sample_LineCount(g_text);
    bool onBar = nlines * LH + 2 * EPAD > r.height && CheckCollisionPointRec(m, (Rectangle){ r.x + r.width - TH_SB_W - 4, r.y, TH_SB_W + 4, r.height });
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    /* focus and mouse */
    if (!blocked && !onBar && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (over) {
            g_focus = e; focused = true;
            int line = (int)floorf((m.y - (r.y + EPAD) + e->scroll) / LH);
            if (line < 0) line = 0;
            if (line >= nlines) line = nlines - 1;
            int ls = e_line_start(line);
            int pos = e_pos_at(ls, e_le(ls), m.x - textX);
            if (shift && e->hasSel) e->cur = pos; else if (shift) { e->anchor = e->cur; e->hasSel = true; e->cur = pos; }
            else { e->cur = pos; e->anchor = pos; e->hasSel = false; }
            e->drag = true;
            e->pxCol = -1;
        } else if (focused) { SampleEdit_SetFocus(e, false); focused = false; }
    }
    if (e->drag) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && focused) {
            int line = (int)floorf((m.y - (r.y + EPAD) + e->scroll) / LH);
            if (line < 0) line = 0;
            if (line >= nlines) line = nlines - 1;
            int ls = e_line_start(line);
            int pos = e_pos_at(ls, e_le(ls), m.x - textX);
            if (pos != e->cur) { if (!e->hasSel) { e->hasSel = true; } e->cur = pos; if (e->anchor == e->cur) e->hasSel = false; }
        } else e->drag = false;
    }

    /* keyboard */
    if (focused && !blocked) {
        int cp;
        while ((cp = g_charSrc()) > 0) {
            if (cp < 32 || cp == 127) continue;
            int sz = 0;
            const char *u = CodepointToUTF8(cp, &sz);
            char one[8];
            memcpy(one, u, (size_t)sz);
            one[sz] = 0;
            int a, b;
            e_sel_range(e, &a, &b);
            if (e_replace(e, a, b, one)) changed = true;
        }
        len = (int)strlen(g_text);
        int a, b;
        e_sel_range(e, &a, &b);
        if (RP(KEY_BACKSPACE)) {
            if (e->hasSel) changed |= e_replace(e, a, b, "");
            else if (e->cur > 0) changed |= e_replace(e, e_prev(e->cur), e->cur, "");
        } else if (RP(KEY_DELETE)) {
            if (e->hasSel) changed |= e_replace(e, a, b, "");
            else if (e->cur < len) changed |= e_replace(e, e->cur, e_next(e->cur, len), "");
        }
        len = (int)strlen(g_text);
        if (RP(KEY_LEFT)) { if (e->hasSel && !shift) e_move(e, a, false); else e_move(e, e_prev(e->cur), shift); e->pxCol = -1; }
        if (RP(KEY_RIGHT)) { if (e->hasSel && !shift) e_move(e, b, false); else e_move(e, e_next(e->cur, len), shift); e->pxCol = -1; }
        if (RP(KEY_UP) || RP(KEY_DOWN)) {
            bool up = RP(KEY_UP);
            int ls = e_ls(e->cur);
            if (e->pxCol < 0) e->pxCol = e_mx(ls, e->cur);
            if (up) {
                if (ls > 0) { int pl = e_ls(ls - 1); e_move(e, e_pos_at(pl, ls - 1, e->pxCol), shift); }
                else e_move(e, 0, shift);
            } else {
                int le = e_le(e->cur);
                if (g_text[le]) { int nl = le + 1; e_move(e, e_pos_at(nl, e_le(nl), e->pxCol), shift); }
                else e_move(e, len, shift);
            }
        }
        if (IsKeyPressed(KEY_HOME)) { e_move(e, ctrl ? 0 : e_ls(e->cur), shift); e->pxCol = -1; }
        if (IsKeyPressed(KEY_END)) { e_move(e, ctrl ? len : e_le(e->cur), shift); e->pxCol = -1; }
        if (ctrl && IsKeyPressed(KEY_A)) { e->anchor = 0; e->cur = len; e->hasSel = len > 0; }
        e_sel_range(e, &a, &b);
        if (ctrl && IsKeyPressed(KEY_C) && e->hasSel) {
            char tmp[SAMPLE_MAX + 2];
            memcpy(tmp, g_text + a, (size_t)(b - a)); tmp[b - a] = 0;
            SetClipboardText(tmp);
        }
        if (ctrl && IsKeyPressed(KEY_X) && e->hasSel) {
            char tmp[SAMPLE_MAX + 2];
            memcpy(tmp, g_text + a, (size_t)(b - a)); tmp[b - a] = 0;
            SetClipboardText(tmp);
            changed |= e_replace(e, a, b, "");
        }
        if (ctrl && IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip && clip[0]) changed |= e_replace(e, a, b, clip);
        }
        bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressedRepeat(KEY_ENTER);
        if (enter && ctrl) { SampleEdit_SetFocus(e, false); focused = false; }
        else if (enter) { e_sel_range(e, &a, &b); changed |= e_replace(e, a, b, "\n"); }
        if (IsKeyPressed(KEY_ESCAPE)) { SampleEdit_SetFocus(e, false); focused = false; }
    }
    len = (int)strlen(g_text);
    nlines = Sample_LineCount(g_text);

    /* keep the caret in view */
    int cline = e_line_of(e->cur);
    if (focused && e->cur != e->seen) {   /* only when the caret moved: the bar and the wheel may scroll it out of view */
        float cy = (float)(cline * LH);
        if (cy - e->scroll < 0) e->scroll = cy;
        if (cy + LH - e->scroll > r.height - 2 * EPAD) e->scroll = cy + LH - (r.height - 2 * EPAD);
    }
    e->seen = e->cur;

    /* draw */
    Color border = focused ? TH_ACCENT : (over ? TH_HOVER_BORDER : TH_LINE);
    rr_fill((int)r.x, (int)r.y, (int)r.width, (int)r.height, TH_RADIUS, border);
    rr_fill((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, (int)r.height - 2, TH_RADIUS - 1, TH_PANEL);
    Th_ScrollBar(r, &e->scroll, nlines * LH + 2 * EPAD, LH * 2, !blocked && (!mouseClip || CheckCollisionPointRec(m, *mouseClip)));
    {   /* inside the field, and inside mouseClip (the caller's clip, restored at the end) */
        float x0 = r.x + 2, y0 = r.y + 2, x1 = r.x + r.width - 2, y1 = r.y + r.height - 2;
        if (mouseClip) {
            x0 = fmaxf(x0, mouseClip->x); y0 = fmaxf(y0, mouseClip->y);
            x1 = fminf(x1, mouseClip->x + mouseClip->width); y1 = fminf(y1, mouseClip->y + mouseClip->height);
        }
        BeginScissorMode((int)x0, (int)y0, x1 > x0 ? (int)(x1 - x0) : 0, y1 > y0 ? (int)(y1 - y0) : 0);
    }
    float th = Th_Measure("Ag").y;
    if (len == 0) Th_Text(placeholder, textX, r.y + EPAD + (LH - th) / 2, TH_TEXT_DIM);
    int sa, sb;
    e_sel_range(e, &sa, &sb);
    int line = 0;
    for (int ls = 0; ; line++) {
        int le = e_le(ls);
        float ly = r.y + EPAD + line * LH - e->scroll;
        if (ly + LH >= r.y && ly <= r.y + r.height) {
            if (e->hasSel && sb > ls && sa <= le) {
                int a2 = sa > ls ? sa : ls, b2 = sb < le ? sb : le;
                float x0 = e_mx(ls, a2), x1 = e_mx(ls, b2) + (sb > le ? 5 : 0);
                DrawRectangle((int)(textX + x0), (int)ly, (int)(x1 - x0) + 1, LH, TH_SELECTION);
            }
            if (le > ls) {
                char tmp[SAMPLE_MAX + 2];
                memcpy(tmp, g_text + ls, (size_t)(le - ls)); tmp[le - ls] = 0;
                Th_Text(tmp, textX, ly + (LH - th) / 2, TH_TEXT);
            }
            if (focused && cline == line && ((int)(GetTime() * 2) % 2 == 0))
                DrawRectangle((int)(textX + e_mx(ls, e->cur)), (int)ly + 2, 1, LH - 4, TH_TEXT);
        }
        if (!g_text[le]) break;
        ls = le + 1;
    }
    EndScissorMode();
    if (mouseClip) BeginScissorMode((int)mouseClip->x + 1, (int)mouseClip->y, (int)mouseClip->width - 1, (int)mouseClip->height);
    return changed;
}

/* ------------------------------------------------------------------------------------------------ the strip */

#define STRIP_HDR 32
#define STRIP_MIN 96
#define STRIP_EDIT_W 330

static int s_zoom = 2;
static int s_h = 140;
static bool s_collapsed = false;
static bool s_resizing;
static float s_availH = 600;
static float s_scroll, s_drawnH;
static SampleEdit s_edit;
static SampleCache s_cache;

int SampleStrip_Zoom(void) { return s_zoom; }
int SampleStrip_Setting(void) { return s_h; }
bool SampleStrip_Collapsed(void) { return s_collapsed; }

static void strip_save(void)
{
    char v[16];
    snprintf(v, sizeof v, "%d", s_zoom); SampleIni_Set(g_ini, "app", "stripZoom", v);
    snprintf(v, sizeof v, "%d", s_h); SampleIni_Set(g_ini, "app", "stripH", v);
    SampleIni_Set(g_ini, "app", "stripCollapsed", s_collapsed ? "1" : "0");
}

void SampleStrip_SetZoom(int z)
{
    if (z < 1 || z > 8 || z == s_zoom) return;
    s_zoom = z;
    s_scroll = 0;
    strip_save();
}

void SampleStrip_Load(const char *iniPath)
{
    char v[32];
    if (SampleIni_Get(iniPath, "app", "stripZoom", v, sizeof v)) { int z = atoi(v); if (z >= 1 && z <= 8) s_zoom = z; }
    if (SampleIni_Get(iniPath, "app", "stripH", v, sizeof v)) { int h = atoi(v); if (h >= STRIP_MIN && h <= 900) s_h = h; }
    if (SampleIni_Get(iniPath, "app", "stripCollapsed", v, sizeof v)) s_collapsed = atoi(v) != 0;
}

float SampleStrip_Height(float availH)
{
    s_availH = availH;
    if (s_collapsed) return STRIP_HDR;
    float maxH = availH * 0.6f;
    if (maxH < STRIP_MIN) maxH = STRIP_MIN;
    float h = (float)s_h;
    if (h > maxH) h = maxH;
    if (h < STRIP_MIN) h = STRIP_MIN;
    return floorf(h);
}

void SampleStrip_Draw(Rectangle r, const PxFont *f, bool blocked)
{
    bool prevBlocked = Th_InputBlocked();
    if (blocked) Th_SetInputBlocked(true);
    bool inp = !blocked && !prevBlocked;
    Vector2 m = GetMousePosition();
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)r.height, TH_PANEL);
    Th_HLine(r.x, r.x + r.width, r.y, TH_LINE);

    /* the top edge resizes the strip */
    if (!s_collapsed) {
        Rectangle hot = { r.x, r.y - 3, r.width, 7 };
        Th_Reg("Sample_Resize", hot);
        bool over = inp && CheckCollisionPointRec(m, hot);
        if (over || s_resizing) SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
        if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) s_resizing = true;
        if (s_resizing) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float h = (r.y + r.height) - m.y;
                float maxH = s_availH * 0.6f;
                if (maxH < STRIP_MIN) maxH = STRIP_MIN;
                if (h > maxH) h = maxH;
                if (h < STRIP_MIN) h = STRIP_MIN;
                s_h = (int)h;
            } else { s_resizing = false; strip_save(); }
        }
        if (over || s_resizing) Th_HLine(r.x, r.x + r.width, r.y, TH_ACCENT);
    }

    /* header: the fold caption, zoom */
    {
        Rectangle zr = { r.x + r.width - 12 - TH_STEP_W, r.y + (STRIP_HDR - TH_ROW_H) / 2, TH_STEP_W, TH_ROW_H };
        float zx = zr.x - 8 - Th_Measure("Zoom").x, hy = zr.y + (TH_ROW_H - 24) / 2;
        const char *hint = s_edit.hasSel || g_focus == &s_edit ? "Ctrl+Enter applies" : NULL;
        Th_SectionSetOpen("SAMPLE", !s_collapsed);
        if (Th_Section("SAMPLE", r.x + 12, &hy, zx - TH_GAP - r.x - 12, hint) == s_collapsed) { s_collapsed = !s_collapsed; strip_save(); }
        Th_Text("Zoom", zx, r.y + (STRIP_HDR - Th_Measure("Ag").y) / 2, TH_TEXT_DIM);
        char zt[8];
        snprintf(zt, sizeof zt, "%dx", s_zoom);
        int zs = Th_Stepper(zr, zt, "sz", (s_zoom <= 1 ? TH_STEP_NO_MINUS : 0) | (s_zoom >= 8 ? TH_STEP_NO_PLUS : 0));
        if (zs) SampleStrip_SetZoom(s_zoom + zs);
    }

    if (!s_collapsed) {
        Th_HLine(r.x, r.x + r.width, r.y + STRIP_HDR - 1, TH_LINE_SOFT);
        Rectangle body = { r.x, r.y + STRIP_HDR, r.width, r.height - STRIP_HDR };
        Rectangle er = { body.x + 12, body.y + 8, STRIP_EDIT_W, body.height - 16 };
        SampleEdit_Do(&s_edit, er, "Edit sample", "Sample_Edit", blocked || prevBlocked, NULL);
        Rectangle pr = { er.x + er.width + 12, body.y + 8, body.width - er.width - 36, body.height - 16 };
        Th_Reg("Sample_Preview", pr);
        Th_Reg("Sample_Origin", (Rectangle){ pr.x + 10, pr.y + 8, 0, 0 });   /* the top left of the first line's font box (scripts) */
        DrawRectangle((int)pr.x, (int)pr.y, (int)pr.width, (int)pr.height, TH_PANEL_ALT);
        Th_RectLines(pr, TH_LINE);
        Rectangle inner = { pr.x + 1, pr.y + 1, pr.width - 2, pr.height - 2 };
        if (f && Sample_Get()[0]) {
            SampleStyle st;
            memset(&st, 0, sizeof st);
            st.bg = TH_PANEL_ALT;
            st.lineGap = -1;
            st.useClip = true;
            st.clip = inner;
            s_drawnH = Sample_Draw(&s_cache, f, Sample_Get(), pr.x + 10, pr.y + 8 - s_scroll, s_zoom, pr.width - 10 - TH_SB_W, 0, &st, NULL);
        } else if (f) {
            Th_Text("The sample is empty", pr.x + 10, pr.y + 8, TH_TEXT_DIM);
            s_drawnH = 0;
        }
        Th_ScrollBar(pr, &s_scroll, s_drawnH + 16, 24, inp);
    }
    Th_SetInputBlocked(prevBlocked);
}
