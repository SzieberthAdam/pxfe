/* gpanel.inc - the right panel of the glyph view, included by main.c after panel.inc (DESIGN.md "Version 4", "Glyph view").
 * Three tabs: Glyph (code, character, advance, bitmap, offsets, the full pxgl hashes of the original and of the current
 * bitmap, "Find in Library"), Usage (the glyph usage list of the Insight module) and Layers (layers.c).
 * The tab (glyphTab) is not remembered: the glyph view opens on Glyph.  Every tab scrolls by itself. */

#define GPANEL_TABS_H 44

static void CopyHash(const char *hash, const char *what)
{
    SetClipboardText(hash);
    ToastPush(0, "%s copied", what);
}

/* One full hash in a box (click copies it).  hash NULL: not known, "" : the bitmap has no ink. */
static void DrawHashBox(float px, float *yp, const char *label, const char *hash, const char *name, const char *what, bool accent)
{
    float y = *yp;
    Th_TextSmall(label, px + 16, y, TH_TEXT_DIM);
    y += 17;
    Rectangle r = {px + 12, y, PANEL_W - 24, 26};
    bool real = hash && hash[0];
    Th_Reg(name, r);
    bool hv = real && Hov(r);
    RRect(r, hv ? (Color){0xF1, 0xF4, 0xFE, 0xFF} : TH_PANEL_ALT, hv ? TH_ACCENT : TH_LINE);
    const char *txt = real ? hash : (hash ? "no ink" : "not known");
    Vector2 m = Th_MeasureMono(txt);
    Th_TextMono(txt, Fl(r.x + (r.width - m.x) / 2), Fl(r.y + (r.height - m.y) / 2) + 1, real ? (accent ? TH_ACCENT : TH_TEXT) : TH_TEXT_DIM);
    if (hv) {
        hintText = "Click to copy the hash";
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) CopyHash(hash, what);
    }
    *yp = y + 34;
}

static void DrawGlyphInfo(float px, float *yp, const PxGlyph *g)
{
    float y = *yp;
    char v[64], cur[33];
    struct { const char *k; char v[64]; bool mono; } rows[6];
    CodeLabel(g->codepoint, rows[0].v, 64);
    if (px_code_is_codepoint(g->codepoint)) {
        int len = 0;
        const char *u = CodepointToUTF8(g->codepoint, &len);
        bool shown = g->codepoint >= 0x21 && g->codepoint != 0x7F && g->codepoint != 0xAD && g->codepoint <= 0x17F && !(g->codepoint >= 0x80 && g->codepoint < 0xA1);
        if (shown) snprintf(rows[1].v, 64, "%.*s", len, u);
        else if (g->codepoint == 0x20) snprintf(rows[1].v, 64, "space");
        else snprintf(rows[1].v, 64, "\xE2\x80\x94");
    } else snprintf(rows[1].v, 64, "\xE2\x80\x94");
    snprintf(rows[2].v, 64, "%d", g->advance);
    if (g->w > 0) snprintf(rows[3].v, 64, "%d x %d", g->w, g->h); else snprintf(rows[3].v, 64, "empty");
    snprintf(rows[4].v, 64, "%d, %d", g->x, g->y);
    snprintf(rows[5].v, 64, "%s", g->codepoint != PX_NO_CODEPOINT && g->codepoint == font.defaultCodepoint ? "yes" : "no");
    rows[0].k = px_code_is_uncoded(g->codepoint) ? "No code point" : "Code point";
    rows[1].k = "Character"; rows[2].k = "Advance"; rows[3].k = "Bitmap"; rows[4].k = "Offset x, y"; rows[5].k = "Default glyph";
    for (int i = 0; i < 6; i++) {
        Th_Text(rows[i].k, px + 16, y, TH_TEXT_DIM);
        snprintf(v, sizeof v, "%s", rows[i].v);
        bool mono = i == 0;
        Vector2 m = mono ? Th_MeasureMono(v) : Th_Measure(v);
        if (mono) Th_TextMono(v, Fl(px + PANEL_W - 16 - m.x), y, TH_TEXT);
        else Th_Text(v, Fl(px + PANEL_W - 16 - m.x), y, TH_TEXT);
        y += 21;
    }
    y += 8;
    SectionLabel(px, &y, "GLYPH HASH");
    const char *orig = Editor_OriginalHash(ed);
    px_glyph_hash(g, cur);
    bool differs = orig && strcmp(orig, cur) != 0;
    DrawHashBox(px, &y, "ORIGINAL (WHEN THE FONT WAS OPENED)", orig, "ghash_orig", "Original glyph hash", false);
    DrawHashBox(px, &y, differs ? "CURRENT (CHANGED)" : "CURRENT", cur, "ghash_cur", "Current glyph hash", differs);
    *yp = y;
}

static void DrawGlyphPanel(Rectangle p)
{
    DrawRectangleRec(p, TH_PANEL);
    Th_VLine(p.x, p.y, p.y + p.height, TH_LINE);

    int tab = glyphTab;
    {
        static const char *labels[3] = {"Glyph", "Usage", "Layers"};
        int c = Th_Segmented((Rectangle){p.x + 8, p.y + 8, p.width - 16, 28}, labels, 3, tab, 0, "gtab");
        if (c >= 0 && c != glyphTab) { glyphTab = c; tab = c; panelScroll = 0; }
        Th_HLine(p.x + 1, p.x + p.width, p.y + GPANEL_TABS_H - 1, TH_LINE);
    }
    Rectangle cr = {p.x, p.y + GPANEL_TABS_H, p.width, p.height - GPANEL_TABS_H};   /* the scrolling content */
    float maxS0 = panelContentH - cr.height;
    if (maxS0 < 0) maxS0 = 0;
    if (panelScroll > maxS0) panelScroll = maxS0;
    if (panelScroll < 0) panelScroll = 0;

    int gi = Editor_GlyphIndex(ed);
    const PxGlyph *g = (gi >= 0 && gi < font.count) ? &font.glyphs[gi] : NULL;
    int32_t insCode = (g && px_code_is_codepoint(g->codepoint)) ? g->codepoint : -1;

    hovClip = cr;
    hovClipOn = true;
    BeginScissorMode((int)cr.x + 1, (int)cr.y, (int)cr.width - 1, (int)cr.height);
    float px = p.x;
    float y = cr.y + 14 - panelScroll;

    if (tab == 0 && g) {
        SectionLabel(px, &y, "GLYPH");
        DrawGlyphInfo(px, &y, g);
        bool can = insCode >= 0 && finder;   /* the finder on this code point (also Ctrl+Shift+F and the toolbar) */
        Rectangle lr = {px + 8, y - 2, PANEL_W - 16, 24};
        Th_Reg("glyph_find", lr);
        bool hv = can && Hov(lr);
        if (hv) {
            DrawRectangleRounded(lr, 0.2f, 6, TH_HOVER);
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                memset(&panelTarget, 0, sizeof panelTarget);
                panelTarget.action = INSIGHT_FIND_CODE;
                panelTarget.code = insCode;
                panelTargetSet = true;
            }
        }
        Th_Text("Find in Library", px + 16, y + 2, can ? TH_ACCENT : TH_TEXT_DIM);
        if (can) {
            char cl[24];
            CodeLabel(insCode, cl, sizeof cl);
            Vector2 m = Th_MeasureSmall(cl);
            Th_TextSmall(cl, px + PANEL_W - 16 - m.x, y + 4, TH_TEXT_DIM);
        }
        y += 28;
    } else if (tab == 2) {
        float used = Layers_DrawPanel(Editor_Layers(ed), (Rectangle){px, y - 8, p.width, 0}, cr);
        y = y - 8 + used;
    }

    if (insight) {
        bool cl = false;
        InsightTarget tg;
        memset(&tg, 0, sizeof tg);
        if (tab == 1 && g) {
            enum { N = 1 };
            static char insH[N][33];
            char h[33];
            px_glyph_hash(g, h);            /* "" = no ink: no bitmap to look for */
            int n = h[0] ? 1 : 0;
            if (n) memcpy(insH[0], h, 33);
            Insight_SetGlyphs(insight, (const char (*)[33])insH, n);
            Insight_SetPreferredCode(insight, insCode);
            bool prevB = Th_InputBlocked();
            if (!CheckCollisionPointRec(Mouse(), cr)) Th_SetInputBlocked(true);   /* an entry scrolled out of the panel must not react */
            Insight_SetVisible(insight, cr);
            float used = Insight_Draw(insight, (Rectangle){px, y, PANEL_W, cr.y + cr.height - y > 200 ? cr.y + cr.height - y : 200}, INSIGHT_SECTION_USAGE, &cl, &tg);
            Th_SetInputBlocked(prevB);
            y += used;
            if (cl && tg.action != INSIGHT_NONE) { panelTarget = tg; panelTargetSet = true; }
        } else {
            Insight_Draw(insight, (Rectangle){px, y, PANEL_W, 0}, 0, &cl, &tg);   /* nothing wanted: the queries stop */
        }
    }

    panelContentH = y + panelScroll - cr.y + 16;
    EndScissorMode();
    hovClipOn = false;

    float maxS = panelContentH - cr.height;
    if (maxS < 0) maxS = 0;
    if (Hov(p) && !wheelUsed) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) panelScroll -= wheel * 48.0f;
    }
    if (panelScroll > maxS) panelScroll = maxS;
    if (panelScroll < 0) panelScroll = 0;
    panelScroll = floorf(panelScroll);
    if (maxS > 0) {
        float th = cr.height * cr.height / panelContentH;
        if (th < 30) th = 30;
        float ty = cr.y + (cr.height - th) * (panelScroll / maxS);
        DrawRectangleRounded((Rectangle){p.x + p.width - 7, ty + 2, 4, th - 4}, 1.0f, 4, (Color){0xC6, 0xC6, 0xCE, 0xFF});
    }
}
