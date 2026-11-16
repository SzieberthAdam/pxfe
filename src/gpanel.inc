/* gpanel.inc - the right panel of the glyph view, included by main.c after panel.inc (DESIGN.md "Version 4", "Glyph view").
 * Three tabs: Glyph (code, character, advance, bitmap, offsets, the full pxgl hashes of the original and of the current
 * bitmap, "Find in Library"), Usage (the glyph usage list of the Insight module) and Layers (layers.c).
 * The tab (glyphTab) is not remembered: the glyph view opens on Glyph.  Every tab scrolls by itself. */

#define GPANEL_TABS_H 44

static void DrawGlyphInfo(float px, float *yp, int gi)
{
    float y = *yp;
    char cur[33];
    GlyphFacts(px, &y, gi, true, true);   /* editable while the font is unlocked; a new code point moves the glyph, so look it up again */
    gi = Editor_GlyphIndex(ed);
    const PxGlyph *g = &font.glyphs[gi];
    y += TH_GAP;
    Th_Section("GLYPH HASH", px + 16, &y, PANEL_W - 32, 0, NULL);
    const char *orig = Editor_OriginalHash(ed);
    px_glyph_hash(g, cur);
    bool differs = orig && strcmp(orig, cur) != 0;
    Th_HashField(px, &y, PANEL_W, "ORIGINAL (WHEN THE FONT WAS OPENED)", orig, "ghash_orig", "Original glyph", 0);
    Th_HashField(px, &y, PANEL_W, differs ? "CURRENT (CHANGED)" : "CURRENT", cur, "ghash_cur", "Current glyph", differs ? TH_HASH_ACCENT : 0);
    *yp = y;
}

static void DrawGlyphPanel(Rectangle p)
{
    DrawRectangleRec(p, TH_PANEL);
    Th_VLine(p.x, p.y, p.y + p.height, TH_LINE);

    int tab = glyphTab;
    {
        static const char *labels[3] = {"Glyph", "Usage", "Layers"};
        int c = Th_Segmented((Rectangle){p.x + 8, p.y + 8, p.width - 16, TH_ROW_H}, &(ThSeg){ labels, 3, tab, 0, "gtab" });
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
        Th_Section("GLYPH", px + 16, &y, PANEL_W - 32, 0, NULL);
        DrawGlyphInfo(px, &y, gi);
        FindRow(px, &y, insCode);
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
            bool prevB = Th_MouseOnly();
            if (!CheckCollisionPointRec(Mouse(), cr)) Th_SetMouseBlocked(true);   /* an entry scrolled out of the panel must not react */
            Insight_SetVisible(insight, cr);
            float used = Insight_Draw(insight, (Rectangle){px, y, PANEL_W, cr.y + cr.height - y > 200 ? cr.y + cr.height - y : 200}, INSIGHT_SECTION_USAGE, &cl, &tg);
            Th_SetMouseBlocked(prevB);
            y += used;
            if (cl && tg.action != INSIGHT_NONE) { panelTarget = tg; panelTargetSet = true; }
        } else {
            Insight_Draw(insight, (Rectangle){px, y, PANEL_W, 0}, 0, &cl, &tg);   /* nothing wanted: the queries stop */
        }
    }

    panelContentH = y + panelScroll - cr.y + 16;
    EndScissorMode();
    hovClipOn = false;

    Th_ScrollBar(cr, &panelScroll, panelContentH, 48, true);
}
