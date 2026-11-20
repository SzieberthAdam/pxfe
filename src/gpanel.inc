/* gpanel.inc - the right panel of the glyph view, included by main.c after panel.inc (DESIGN.md "Glyph view").
 * Three tabs: View (zoom, metric lines, checker: Editor_ViewPanel), Glyph (code, character, advance, bitmap, offsets, "Find in Library",
 * the hashes of the original and of the current bitmap, the glyph usage) and Layers (layers.c).  glyphTab: 3 View, 0 Glyph, 2 Layers.
 * The tab is not remembered: the glyph view opens on Glyph.  Every tab scrolls by itself. */

static void DrawGlyphInfo(float px, float *yp, int gi, PxCode insCode)
{
    float y = *yp;
    char cur[33];
    GlyphFacts(px, &y, gi, true, true);   /* editable while the font is unlocked; a new code point moves the glyph, so look it up again */
    gi = Editor_GlyphIndex(ed);
    const PxGlyph *g = &font.glyphs[gi];
    y += 4;
    static const char *lab[1] = {"Find in Library"};
    const int fl[1] = {TH_BTN_OFF(insCode >= 0 && finder)};
    if (ButtonRow(px + 16, &y, PANEL_W - 32, 1, lab, fl, NULL) == 0) {
        memset(&panelTarget, 0, sizeof panelTarget);
        panelTarget.action = INSIGHT_FIND_CODE;
        panelTarget.code = insCode;
        panelTargetSet = true;
    }
    const char *orig = Editor_OriginalHash(ed);
    px_glyph_hash(g, cur);
    bool differs = orig && strcmp(cur, orig) != 0;
    Th_HashField(px, &y, PANEL_W, "ORIGINAL (WHEN THE FONT WAS OPENED)", orig, "ghash_orig", "Original glyph", 0);
    Th_HashField(px, &y, PANEL_W, differs ? "CURRENT (CHANGED)" : "CURRENT", cur, "ghash_cur", "Current glyph", differs ? TH_HASH_ACCENT : 0);
    *yp = y;
}

static void DrawGlyphPanel(Rectangle p)
{
    DrawRectangleRec(p, TH_PANEL);
    Th_VLine(p.x, p.y, p.y + p.height, TH_LINE);
    static const char *labels[3] = {"View", "Glyph", "Layers"};
    static const int ids[3] = {3, 0, 2};
    int tab = glyphTab, c = PanelTabs(p, 3, labels, ids, tab, "gtab");
    if (c >= 0 && c != glyphTab) { glyphTab = c; tab = c; panelScroll = 0; }
    Rectangle cr = {p.x, p.y + PANEL_TABS_H, p.width, p.height - PANEL_TABS_H};   /* the scrolling content */
    float maxS0 = panelContentH - cr.height;
    if (panelScroll > maxS0) panelScroll = maxS0 > 0 ? maxS0 : 0;
    if (panelScroll < 0) panelScroll = 0;

    int gi = Editor_GlyphIndex(ed);
    const PxGlyph *g = (gi >= 0 && gi < font.count) ? &font.glyphs[gi] : NULL;
    PxCode insCode = (g && px_code_is_codepoint(g->code)) ? g->code : PX_NO_CODE;

    hovClip = cr;
    hovClipOn = true;
    BeginScissorMode((int)cr.x + 1, (int)cr.y, (int)cr.width - 1, (int)cr.height);
    float px = p.x;
    float y = cr.y + 14 - panelScroll;
    int mask = 0;
    if (tab == 3) {
        if (Th_Section("VIEW", px + 16, &y, PANEL_W - 32, NULL)) Editor_ViewPanel(ed, px + 16, &y, PANEL_W - 32);
    } else if (tab == 0 && g) {
        if (Th_Section("GLYPH", px + 16, &y, PANEL_W - 32, NULL)) DrawGlyphInfo(px, &y, gi, insCode);
        static char insH[1][33];
        px_glyph_hash(g, insH[0]);            /* "" = no ink: no bitmap to look for */
        if (insight) {
            Insight_SetGlyphs(insight, (const char (*)[33])insH, insH[0][0] ? 1 : 0);
            Insight_SetPreferredCode(insight, insCode);
            Insight_SetPalette(insight, &font, gi);
        }
        mask = INSIGHT_SECTION_USAGE;
    } else if (tab == 2) {
        float used = Layers_DrawPanel(Editor_Layers(ed), (Rectangle){px, y - 8, p.width, 0}, cr);
        y = y - 8 + used;
    }
    if (insight) y += PanelInsight(px, y, cr, mask);

    panelContentH = y + panelScroll - cr.y + 16;
    EndScissorMode();
    hovClipOn = false;
    Th_ScrollBar(cr, &panelScroll, panelContentH, 48, true);
}
