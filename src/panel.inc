/* panel.inc - the right-hand panel of the font view, included by main.c.  A segmented control at its top picks the tab:
 * Font (every PXM_* metric is an optional row: a "stated" checkbox and a spinner, RATIO as two spinners, MONOSPACED as a
 * switch), Glyph (the selection, the "Find in Library" link and the glyph usage) and Similar (similar fonts and typefaces).
 * The tab is only changed by the user and is kept in pxfe.ini.  All editing is disabled in an original.
 * The content of a tab scrolls.  DrawTypefaceTab, the typeface's fields, is the Typeface tab of the library views (browser.c). */

static float panelContentH = 0;
#define PANEL_TABS_H 44                 /* the tab control and its hairline */
static InsightTarget panelTarget;       /* a click on an Insight entry: handled at the end of the frame (it replaces the document) */
static bool panelTargetSet = false;

static void LowerKey(const char *key, char *out, size_t cap)
{
    size_t i = 0;
    for (; key[i] && i + 1 < cap; i++) out[i] = (char)tolower((unsigned char)key[i]);
    out[i] = 0;
}

static int GlyphTopOf(int32_t cp)
{
    int i = px_font_find(&font, cp);
    if (i < 0 || font.glyphs[i].w <= 0) return -1;
    return px_glyph_top(&font.glyphs[i]);
}

/* The value a metric gets when it is switched on: what the font shows already (the box, the height of H and x). */
static int DefaultMetric(PxMetric m)
{
    int a, b;
    px_font_box(&font, &a, &b);
    switch (m) {
    case PXM_ASCENT: return a;
    case PXM_DESCENT: return b;
    case PXM_CAPHEIGHT: { int t = GlyphTopOf('H'); return t > 0 ? t : (a * 7 + 5) / 10; }
    case PXM_XHEIGHT: { int t = GlyphTopOf('x'); return t > 0 ? t : (a * 5 + 5) / 10; }
    case PXM_QUADWIDTH: { int i = px_font_find(&font, 'M'); return i >= 0 && font.glyphs[i].advance > 0 ? font.glyphs[i].advance : a + b; }
    case PXM_UNDERLINEPOSITION: return -1;
    case PXM_STRIKEOUTPOSITION: { int xh = font.has[PXM_XHEIGHT] ? font.m[PXM_XHEIGHT] : (a * 5 + 5) / 10; return xh / 2 > 0 ? xh / 2 : 1; }
    case PXM_UNDERLINETHICKNESS: case PXM_STRIKEOUTTHICKNESS: return 1;
    default: return 0;
    }
}

static int MetricMin(PxMetric m) { return (m == PXM_UNDERLINEPOSITION || m == PXM_STRIKEOUTPOSITION) ? -999 : 0; }

static int Gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

static void MetricRow(float px, float y, PxMetric m, Color swatch)
{
    char low[32], n1[48], n2[48];
    LowerKey(px_metric_key(m), low, sizeof low);
    snprintf(n1, sizeof n1, "%s.on", low);
    snprintf(n2, sizeof n2, "%s", low);
    bool en = !readOnly;
    bool on = font.has[m];
    if (Th_Check((Rectangle){px + 16, y, PANEL_W - 140, TH_ROW_H}, px_metric_label(m), &on, TH_CK_GAP | (en ? 0 : TH_CK_OFF), n1)) {
        int dv = on ? DefaultMetric(m) : 0;
        PushUndo(0);
        font.has[m] = on;
        font.m[m] = dv;
    }
    RRect((Rectangle){px + 40, y + TH_ROW_H / 2 - 3, 6, 6}, swatch, (Color){0, 0, 0, 0});
    int nv;
    if (Th_Number((Rectangle){px + PANEL_W - 16 - 100, y, 100, TH_ROW_H}, n2, font.m[m], MetricMin(m), 99999, TH_NUM_WHEEL | (en && font.has[m] ? 0 : TH_TF_DISABLED), &nv)) {
        PushUndo(100 + (int)m);
        font.m[m] = nv;
    }
}

static void RatioRow(float px, float y)
{
    bool en = !readOnly;
    bool on = font.hasRatio;
    float ty = Fl(y + (TH_ROW_H - Th_Measure("A").y) / 2);
    if (Th_Check((Rectangle){px + 16, y, 80, TH_ROW_H}, "Ratio", &on, TH_CK_GAP | (en ? 0 : TH_CK_OFF), "ratio.on")) {
        PushUndo(0);
        font.hasRatio = on;
        font.ratioA = font.ratioB = on ? 1 : 0;
    }
    RRect((Rectangle){px + 40, y + TH_ROW_H / 2 - 3, 6, 6}, TH_TEXT_DIM, (Color){0, 0, 0, 0});
    float right = px + PANEL_W - 16;
    int nv;
    Rectangle rb = {right - 78, y, 78, TH_ROW_H}, ra = {right - 78 - 12 - 78, y, 78, TH_ROW_H};
    Th_Text(":", right - 78 - 9, ty, TH_TEXT_DIM);
    if (Th_Number(ra, "ratioa", font.ratioA, 1, 99999, TH_NUM_WHEEL | (en && on ? 0 : TH_TF_DISABLED), &nv)) {
        PushUndo(120);
        font.ratioA = nv;
        int g = Gcd(font.ratioA, font.ratioB);
        if (g > 1) { font.ratioA /= g; font.ratioB /= g; }
    }
    if (Th_Number(rb, "ratiob", font.ratioB, 1, 99999, TH_NUM_WHEEL | (en && on ? 0 : TH_TF_DISABLED), &nv)) {
        PushUndo(121);
        font.ratioB = nv;
        int g = Gcd(font.ratioA, font.ratioB);
        if (g > 1) { font.ratioA /= g; font.ratioB /= g; }
    }
}

/* "Find in Library" of the Glyph tab (also Ctrl+Shift+F and the toolbar), with the code point at the right. */
static void FindRow(float px, float *y, int32_t insCode)
{
    bool can = insCode >= 0 && finder;
    Rectangle br = {px + 8, *y - 2, Th_ButtonW("Find in Library"), TH_ROW_H};
    Th_Reg("glyph_find", br);
    if (Th_Button(br, "Find in Library", TH_BTN_OFF(can), NULL)) {
        memset(&panelTarget, 0, sizeof panelTarget);
        panelTarget.action = INSIGHT_FIND_CODE;
        panelTarget.code = insCode;
        panelTargetSet = true;
    }
    if (can) {
        char cl[24];
        CodeLabel(insCode, cl, sizeof cl);
        Th_TextSmall(cl, px + PANEL_W - 16 - Th_MeasureSmall(cl).x, *y + 4, TH_TEXT_DIM);
    }
    *y += TH_ROW_H + 4;
}

static const PxtfText kCtOrder[PXTF_TEXT_COUNT] = {
    PXTF_FAMILY, PXTF_STYLE, PXTF_FULLNAME, PXTF_WEIGHTNAME, PXTF_SLANT, PXTF_VERSION, PXTF_DESIGNER, PXTF_DESIGNERURL,
    PXTF_COPYRIGHT, PXTF_TRADEMARK, PXTF_LICENSE, PXTF_LICENSEURL, PXTF_VENDOR, PXTF_VENDORURL, PXTF_DESCRIPTION, PXTF_CODEPAGE};

static char tabHash[40];   /* the hash the Typeface tab shows (a test hook) */

/* The open document is a duplicate of the typeface with this pxtf hash (made in the Typefaces or Typeface view): its fields are editable. */
static bool TypefaceEditing(const char *pxtf)
{
    return hasFont && !readOnly && hasCont && pxtf && origContHash[0] && Same(origContHash, pxtf);
}

/* The Typeface tab of the library views (browser.c calls it): one variant of the typeface with this pxtf hash.  Read-only, unless the
 * open document is a duplicate of that typeface: then the fields of its `cont` are edited. */
static void DrawTypefaceTab(float px, float pw, float *yp, const PxtfTypeface *tf, const char *hash, int variant)
{
    bool en = TypefaceEditing(hash);
    if (en) { tf = &cont; EnsureHashes(); }

    bool svBlocked = uiBlocked;
    uiBlocked = Th_InputBlocked();
    PxtfVariant *cv = pxtf_find_variant((PxtfTypeface *)tf, variant);
    float y = *yp;
    if (!cv) { Th_Text("The typeface could not be read.", px + 16, y, TH_TEXT_DIM); *yp = y + 26; uiBlocked = svBlocked; return; }
    char info[128];
    snprintf(info, sizeof info, "Variant %04d of %d  \xC2\xB7  %s", cv->number, tf->count, cv->format && cv->format[0] ? cv->format : "no format");
    Th_TextSmall(info, px + 16, y, TH_TEXT_DIM);
    y += 24;
    snprintf(tabHash, sizeof tabHash, "%s", en ? curContHash : hash);
    Th_HashField(px, &y, pw, en ? "TYPEFACE HASH (CURRENT)" : "TYPEFACE HASH", tabHash, "hash_typeface_full", "Typeface", 0);
    if (en && !Same(curContHash, origContHash)) Th_HashField(px, &y, pw, "TYPEFACE HASH (ORIGINAL)", origContHash, "hash_typeface_orig", "Typeface", 0);
    for (int k = 0; k < PXTF_TEXT_COUNT; k++) {
        PxtfText t = kCtOrder[k];
        char low[40], nm[48];
        LowerKey(pxtf_text_key(t), low, sizeof low);
        snprintf(nm, sizeof nm, "c.%s", low);
        Rectangle r = Th_Row(px + 16, &y, pw - 32, pxtf_text_label(t), NULL, 0);
        if (Th_TextField(r, nm, cv->text[t] ? cv->text[t] : "", 0, TH_TF_REVERT | (en ? 0 : TH_TF_DISABLED), NULL) & TH_TF_CHANGED) { pxtf_set_text(cv, t, Th_TextValue()); needHash = true; }
    }
    y += 4;
    struct { const char *label, *name; bool *flag; } sw[3] = {
        {"Italic", "c.italic", &cv->italic}, {"Underlined", "c.underlined", &cv->underlined}, {"Struck-out", "c.struckout", &cv->struckout}};
    for (int i = 0; i < 3; i++) {
        bool v = *sw[i].flag;
        if (Th_Switch((Rectangle){px + 16, y, pw - 32, TH_ROW_H}, sw[i].label, &v, TH_CK_RIGHT | (en ? 0 : TH_CK_OFF), sw[i].name)) { *sw[i].flag = v; needHash = true; }
        y += TH_ROW_H + 2;
    }
    *yp = y;
    uiBlocked = svBlocked;
}

/* The facts of a glyph as label/value rows (the Glyph tab, the selection of the font view). */
static void GlyphFacts(float px, float *y, const PxGlyph *g, bool character)
{
    float x = px + 16, w = PANEL_W - 32;
    char cl[64], ch[16] = "—";
    CodeLabel(g->codepoint, cl, sizeof cl);
    Th_Row(x, y, w, px_code_is_uncoded(g->codepoint) ? "No code point" : "Code point", cl, TH_ROW_MONO);
    if (character) {
        int len = 0;
        const char *u = px_code_is_codepoint(g->codepoint) ? CodepointToUTF8(g->codepoint, &len) : "";
        bool shown = g->codepoint >= 0x21 && g->codepoint != 0x7F && g->codepoint != 0xAD && g->codepoint <= 0x17F && !(g->codepoint >= 0x80 && g->codepoint < 0xA1);
        if (shown) snprintf(ch, sizeof ch, "%.*s", len, u);
        else if (g->codepoint == 0x20) snprintf(ch, sizeof ch, "space");
        Th_Row(x, y, w, "Character", ch, 0);
    }
    Th_Row(x, y, w, "Advance", TextFormat("%d", g->advance), 0);
    Th_Row(x, y, w, "Bitmap", g->w > 0 ? TextFormat("%d x %d", g->w, g->h) : "empty", 0);
    Th_Row(x, y, w, "Offset x, y", TextFormat("%d, %d", g->x, g->y), 0);
    Th_Row(x, y, w, "Default glyph", g->codepoint != PX_NO_CODEPOINT && g->codepoint == font.defaultCodepoint ? "yes" : "no", 0);
}

static void DrawSelectionSection(float px, float *yp)
{
    float y = *yp;
    if (selCount == 1) {
        int fi = FirstSelected();
        const PxGlyph *g = &font.glyphs[fi];
        char h[33];
        GlyphFacts(px, &y, g, false);
        px_glyph_hash(g, h);
        y += 4;
        {   /* how to get into the glyph view (double click or Enter do the same) */
            Rectangle br = {px + 12, y, PANEL_W - 24, TH_ROW_H};
            if (Th_Button(br, "Open in Glyph View", 0, NULL)) CmdEditSelected();
            const char *k = "Enter";
            Vector2 km = Th_MeasureSmall(k);
            Th_TextSmall(k, Fl(br.x + br.width - km.x - 10), Fl(br.y + (br.height - km.y) / 2), TH_TEXT_DIM);
            y += TH_ROW_H + 8;
        }
        Th_HashField(px, &y, PANEL_W, "GLYPH HASH", h, "hash_glyph", "Glyph", 0);
    } else {
        char s[48];
        if (selCount == 0) snprintf(s, sizeof s, "Nothing selected");
        else snprintf(s, sizeof s, "%d glyphs selected", selCount);
        Th_Text(s, px + 16, y, TH_TEXT_DIM);
        y += TH_ROW_H - 2;
        Th_TextSmall("Select one glyph, then press Enter or", px + 16, y, TH_TEXT_DIM);
        y += 20;
        Th_TextSmall("double-click it to open the glyph view.", px + 16, y, TH_TEXT_DIM);
        y += 24;
    }
    *yp = y;
}

static void DrawMetricsPanel(Rectangle p)
{
    if (inEditor) { DrawGlyphPanel(p); return; }   /* the glyph view has its own panel (gpanel.inc) */
    DrawRectangleRec(p, TH_PANEL);
    Th_VLine(p.x, p.y, p.y + p.height, TH_LINE);

    /* the tab control: Font | Glyph | Similar */
    int tab = panelTab;
    {
        static const char *labels[3] = {"Font", "Glyph", "Similar"};
        int c = Th_Segmented((Rectangle){p.x + 8, p.y + 8, p.width - 16, TH_ROW_H}, &(ThSeg){ labels, 3, tab, 0u, "ptab" });
        if (c >= 0) { PanelSetTab(c); tab = c; }
        Th_HLine(p.x + 1, p.x + p.width, p.y + PANEL_TABS_H - 1, TH_LINE);
    }
    Rectangle cr = {p.x, p.y + PANEL_TABS_H, p.width, p.height - PANEL_TABS_H};   /* the scrolling content */

    float maxS0 = panelContentH - cr.height;
    if (maxS0 < 0) maxS0 = 0;
    if (panelScroll > maxS0) panelScroll = maxS0;
    if (panelScroll < 0) panelScroll = 0;

    /* what the Insight queries are about: the selected glyphs (usage), the code of a single one, the font (similar) */
    enum { INS_MAX_SEL = 32 };
    static char insH[INS_MAX_SEL][33];
    int nIns = 0;
    int32_t insCode = -1;
    if (insight) {
        if (selCount >= 1 && selCount <= INS_MAX_SEL) {
            for (int i = 0; i < font.count && i < selN && nIns < INS_MAX_SEL; i++) {
                if (!sel[i]) continue;
                char h[33];
                px_glyph_hash(&font.glyphs[i], h);      /* "" = no ink: no bitmap to look for */
                bool dup = !h[0];
                for (int j = 0; j < nIns && !dup; j++) if (Same(insH[j], h)) dup = true;
                if (!dup) memcpy(insH[nIns++], h, 33);
                if (selCount == 1 && px_code_is_codepoint(font.glyphs[i].codepoint)) insCode = font.glyphs[i].codepoint;
            }
        }
        Insight_SetGlyphs(insight, (const char (*)[33])insH, nIns);
        Insight_SetPreferredCode(insight, insCode);
    }

    hovClip = cr;
    hovClipOn = true;
    BeginScissorMode((int)cr.x + 1, (int)cr.y, (int)cr.width - 1, (int)cr.height);
    float px = p.x;
    float y = cr.y + 14 - panelScroll;

    if (tab == 0) {
        Th_HashField(px, &y, PANEL_W, readOnly ? "FONT HASH" : "FONT HASH (CURRENT)", readOnly ? origHash : curHash, "hash_font_full", "Font", 0);
        if (!readOnly && !Same(curHash, origHash)) Th_HashField(px, &y, PANEL_W, "FONT HASH (ORIGINAL)", origHash, "hash_font_orig", "Font", 0);
        Th_Section("FONT METRICS", px + 16, &y, PANEL_W - 32, 0, NULL);
        static const struct { PxMetric m; Color c; } rows[] = {
            {PXM_ASCENT, TH_M_BOX}, {PXM_DESCENT, TH_M_BOX}, {PXM_LINEGAP, TH_TEXT_DIM}, {PXM_CAPHEIGHT, TH_M_CAP},
            {PXM_XHEIGHT, TH_M_XHEIGHT}, {PXM_INTERNALLEADING, TH_TEXT_DIM}, {PXM_QUADWIDTH, TH_M_ADVANCE},
            {PXM_UNDERLINEPOSITION, TH_M_UNDERLINE}, {PXM_UNDERLINETHICKNESS, TH_M_UNDERLINE},
            {PXM_STRIKEOUTPOSITION, TH_M_UNDERLINE}, {PXM_STRIKEOUTTHICKNESS, TH_M_UNDERLINE}};
        for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            MetricRow(px, y, rows[i].m, rows[i].c);
            y += TH_ROW_H + 4;
        }
        RatioRow(px, y);
        y += TH_ROW_H + 4;
        {
            bool v = font.monospaced;
            if (Th_Switch((Rectangle){px + 54, y, PANEL_W - 54 - 16, TH_ROW_H}, "Monospaced", &v, TH_CK_RIGHT | (readOnly ? TH_CK_OFF : 0), "monospaced")) {
                PushUndo(0);
                font.monospaced = v;
            }
            y += TH_ROW_H + 4;
        }
        int a, b;
        px_font_box(&font, &a, &b);
        char s[96];
        snprintf(s, sizeof s, "Font box %d above, %d below the baseline%s", a, b, (font.has[PXM_ASCENT] && font.has[PXM_DESCENT]) ? "" : " (from the glyphs)");
        Th_TextSmall(s, px + 16, y + 2, TH_TEXT_DIM);
        y += 28;
    } else if (tab == 1) {
        Th_Section("SELECTION", px + 16, &y, PANEL_W - 32, 0, NULL);
        DrawSelectionSection(px, &y);
        if (selCount == 1) {   /* the finder on this code point (also Ctrl+Shift+F and the toolbar) */
            FindRow(px, &y, insCode);
        }
    }
    if (insight && tab >= 1) {
        int mask = 0;
        static char hasKey[2][33];
        static bool hasVal[2];
        if (!Same(hasKey[0], origHash) || !hasKey[0][0]) { snprintf(hasKey[0], 33, "%s", origHash); hasVal[0] = origHash[0] && store_has_pxfm(store_main_conn(store), origHash); }
        if (!Same(hasKey[1], origContHash) || !hasKey[1][0]) { snprintf(hasKey[1], 33, "%s", origContHash); hasVal[1] = origContHash[0] && store_has_pxtf(store_main_conn(store), origContHash); }
        if (tab == 1) mask = INSIGHT_SECTION_USAGE;
        else if (hasVal[0]) {
            Insight_SetFont(insight, origHash, hasVal[1] ? origContHash : "");
            mask = INSIGHT_SECTION_SIMILAR_FONTS | (hasVal[1] ? INSIGHT_SECTION_SIMILAR_TYPEFACES : 0);
        } else {
            Th_Text("Similar fonts need a font of the library.", px + 16, y, TH_TEXT_DIM);
            y += TH_ROW_H;
        }
        bool prevB = Th_InputBlocked();
        if (!CheckCollisionPointRec(Mouse(), cr)) Th_SetInputBlocked(true);   /* an entry scrolled out of the panel must not react */
        bool clicked = false;
        InsightTarget tg;
        memset(&tg, 0, sizeof tg);
        Insight_SetVisible(insight, cr);
        float used = Insight_Draw(insight, (Rectangle){px, y, PANEL_W, cr.y + cr.height - y > 200 ? cr.y + cr.height - y : 200}, mask, &clicked, &tg);
        Th_SetInputBlocked(prevB);
        y += used;
        if (clicked && tg.action != INSIGHT_NONE) { panelTarget = tg; panelTargetSet = true; }
    } else if (insight) {
        bool cl = false;
        InsightTarget tg;
        memset(&tg, 0, sizeof tg);
        Insight_Draw(insight, (Rectangle){px, y, PANEL_W, 0}, 0, &cl, &tg);   /* nothing wanted: the similarity queries stop */
    }

    panelContentH = y + panelScroll - cr.y + 16;
    EndScissorMode();
    hovClipOn = false;

    Th_ScrollBar(cr, &panelScroll, panelContentH, 48, true);
}
