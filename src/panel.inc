/* panel.inc - the right-hand panel of the font view, included by main.c.  A segmented control at its top picks the tab:
 * Font (every PXM_* metric is an optional row: a "stated" checkbox and a spinner, RATIO as two spinners, MONOSPACED as a
 * switch), Typeface (the text fields and flags of the typeface variant; disabled without one), Glyph (the selection, the
 * "Find in Library" link and the glyph usage) and Similar (similar fonts and typefaces).  The tab is only changed by the
 * user (and set to Glyph when the glyph view opens) and is kept in pxfe.ini.  All editing is disabled in an original.
 * The content of a tab scrolls. */

static float panelContentH = 0;
#define PANEL_TABS_H 44                 /* the tab control and its hairline */
static char ctBuf[4096], ctOld[4096];   /* the typeface text field being edited and its value before */
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

/* A small-caps dim section label. */
static void SectionLabel(float px, float *y, const char *label)
{
    Th_TextSmall(label, px + 16, *y, TH_TEXT_DIM);
    *y += 22;
}

static void MetricRow(float px, float y, PxMetric m, Color swatch)
{
    char low[32], n1[48], n2[48];
    LowerKey(px_metric_key(m), low, sizeof low);
    snprintf(n1, sizeof n1, "%s.on", low);
    snprintf(n2, sizeof n2, "%s", low);
    bool en = !readOnly;
    bool on = font.has[m];
    float ty = Fl(y + (26 - Th_Measure("A").y) / 2);
    if (Checkbox((Rectangle){px + 16, y + 5, 16, 16}, &on, en, n1)) {
        int dv = on ? DefaultMetric(m) : 0;
        PushUndo(0);
        font.has[m] = on;
        font.m[m] = dv;
    }
    RRect((Rectangle){px + 40, y + 10, 6, 6}, swatch, (Color){0, 0, 0, 0});
    Th_Text(px_metric_label(m), px + 54, ty, font.has[m] ? TH_TEXT : TH_TEXT_DIM);
    int nv;
    if (NumField((Rectangle){px + PANEL_W - 16 - 100, y, 100, 26}, n2, font.m[m], en && font.has[m], MetricMin(m), &nv)) {
        PushUndo(100 + (int)m);
        font.m[m] = nv;
    }
}

static void RatioRow(float px, float y)
{
    bool en = !readOnly;
    bool on = font.hasRatio;
    float ty = Fl(y + (26 - Th_Measure("A").y) / 2);
    if (Checkbox((Rectangle){px + 16, y + 5, 16, 16}, &on, en, "ratio.on")) {
        PushUndo(0);
        font.hasRatio = on;
        font.ratioA = font.ratioB = on ? 1 : 0;
    }
    RRect((Rectangle){px + 40, y + 10, 6, 6}, TH_TEXT_DIM, (Color){0, 0, 0, 0});
    Th_Text("Ratio", px + 54, ty, on ? TH_TEXT : TH_TEXT_DIM);
    float right = px + PANEL_W - 16;
    int nv;
    Rectangle rb = {right - 78, y, 78, 26}, ra = {right - 78 - 12 - 78, y, 78, 26};
    Th_Text(":", right - 78 - 9, ty, TH_TEXT_DIM);
    if (NumField(ra, "ratioa", font.ratioA, en && on, 1, &nv)) {
        PushUndo(120);
        font.ratioA = nv;
        int g = Gcd(font.ratioA, font.ratioB);
        if (g > 1) { font.ratioA /= g; font.ratioB /= g; }
    }
    if (NumField(rb, "ratiob", font.ratioB, en && on, 1, &nv)) {
        PushUndo(121);
        font.ratioB = nv;
        int g = Gcd(font.ratioA, font.ratioB);
        if (g > 1) { font.ratioA /= g; font.ratioB /= g; }
    }
}

static const PxtfText kCtOrder[PXTF_TEXT_COUNT] = {
    PXTF_FAMILY, PXTF_STYLE, PXTF_FULLNAME, PXTF_WEIGHTNAME, PXTF_SLANT, PXTF_VERSION, PXTF_DESIGNER, PXTF_DESIGNERURL,
    PXTF_COPYRIGHT, PXTF_TRADEMARK, PXTF_LICENSE, PXTF_LICENSEURL, PXTF_VENDOR, PXTF_VENDORURL, PXTF_DESCRIPTION, PXTF_CODEPAGE};

static void CtUnfocus(void) { ctFocus = -1; }

static void TypefaceChangedUi(void)
{
    needHash = true;
}

static void PanelHashBox(float px, float *yp, const char *label, const char *hash, const char *name, const char *what);

static void DrawTypefaceSection(float px, float *yp)
{
    PxtfVariant *cv = pxtf_find_variant(&cont, contVariant);
    if (!cv) return;
    float y = *yp;
    bool en = !readOnly;
    char info[96];
    snprintf(info, sizeof info, "Variant %04d of %d  \xC2\xB7  source %.8s", cv->number, cont.count, cont.hash);
    Th_TextSmall(info, px + 16, y, TH_TEXT_DIM);
    y += 20;
    EnsureHashes();
    PanelHashBox(px, &y, readOnly ? "TYPEFACE HASH" : "TYPEFACE HASH (CURRENT)", readOnly ? origContHash : curContHash, "hash_typeface_full", "Typeface");

    int newFocus = -1;
    bool pressedOnFocused = false;
    for (int k = 0; k < PXTF_TEXT_COUNT; k++) {
        PxtfText t = kCtOrder[k];
        Rectangle r = {px + 104, y, PANEL_W - 16 - 104, 26};
        char low[40], nm[48];
        LowerKey(pxtf_text_key(t), low, sizeof low);
        snprintf(nm, sizeof nm, "c.%s", low);
        Th_Reg(nm, r);
        Th_Text(pxtf_text_label(t), px + 16, Fl(y + (26 - Th_Measure("A").y) / 2), en ? TH_TEXT : TH_TEXT_DIM);
        bool foc = ctFocus == (int)t;
        const char *val = cv->text[t] ? cv->text[t] : "";
        DrawInputBox(r, foc ? ctBuf : val, foc, false, en);
        if (en && Hov(r)) {
            SetMouseCursor(MOUSE_CURSOR_IBEAM);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { if (foc) pressedOnFocused = true; else newFocus = (int)t; }
        }
        y += 32;
    }
    if (newFocus >= 0) {
        ctFocus = newFocus;
        const char *v = cv->text[newFocus] ? cv->text[newFocus] : "";
        snprintf(ctBuf, sizeof ctBuf, "%s", v);
        snprintf(ctOld, sizeof ctOld, "%s", v);
        (void)GetCharPressed();
    } else if (ctFocus >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !pressedOnFocused) CtUnfocus();
    if (ctFocus >= 0 && en && !uiBlocked) {
        if (EditText(ctBuf, (int)sizeof ctBuf, false)) {
            PushUndo(200 + ctFocus);
            pxtf_set_text(cv, (PxtfText)ctFocus, ctBuf);
            TypefaceChangedUi();
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_TAB)) CtUnfocus();
        else if (IsKeyPressed(KEY_ESCAPE)) {
            PushUndo(0);
            pxtf_set_text(cv, (PxtfText)ctFocus, ctOld);
            TypefaceChangedUi();
            CtUnfocus();
        }
    }

    y += 4;
    struct { const char *label, *name; bool *flag; } sw[3] = {
        {"Italic", "c.italic", &cv->italic}, {"Underlined", "c.underlined", &cv->underlined}, {"Struck-out", "c.struckout", &cv->struckout}};
    for (int i = 0; i < 3; i++) {
        Th_Text(sw[i].label, px + 16, Fl(y + (22 - Th_Measure("A").y) / 2), en ? TH_TEXT : TH_TEXT_DIM);
        bool v = *sw[i].flag;
        if (Switch((Rectangle){px + PANEL_W - 16 - 38, y + 1, 38, 20}, &v, en, sw[i].name)) {
            PushUndo(0);
            *sw[i].flag = v;
            TypefaceChangedUi();
        }
        y += 30;
    }
    *yp = y;
}

/* One full hash in a box of the info blocks (click copies it): "Font 0820dc69...". */
static void PanelHashBox(float px, float *yp, const char *label, const char *hash, const char *name, const char *what)
{
    float y = *yp;
    Th_TextSmall(label, px + 16, y, TH_TEXT_DIM);
    y += 17;
    Rectangle r = {px + 12, y, PANEL_W - 24, 26};
    bool real = hash && hash[0];
    Th_Reg(name, r);
    bool hv = real && Hov(r);
    RRect(r, hv ? (Color){0xF1, 0xF4, 0xFE, 0xFF} : TH_PANEL_ALT, hv ? TH_ACCENT : TH_LINE);
    const char *txt = real ? hash : "no ink";
    Vector2 m = Th_MeasureMono(txt);
    Th_TextMono(txt, Fl(r.x + (r.width - m.x) / 2), Fl(r.y + (r.height - m.y) / 2) + 1, real ? TH_TEXT : TH_TEXT_DIM);
    if (hv) {
        hintText = "Click to copy the full hash";
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { SetClipboardText(hash); ToastPush(0, "%s hash copied", what); }
    }
    *yp = y + 34;
}

static void DrawSelectionSection(float px, float *yp)
{
    float y = *yp;
    if (selCount == 1) {
        int fi = FirstSelected();
        const PxGlyph *g = &font.glyphs[fi];
        char v[64], h[33];
        struct { const char *k; char v[64]; } rows[6];
        CodeLabel(g->codepoint, rows[0].v, 64);
        snprintf(rows[1].v, 64, "%d", g->advance);
        if (g->w > 0) snprintf(rows[2].v, 64, "%d x %d", g->w, g->h); else snprintf(rows[2].v, 64, "empty");
        snprintf(rows[3].v, 64, "%d, %d", g->x, g->y);
        px_glyph_hash(g, h);
        snprintf(rows[4].v, 64, "%.8s", h[0] ? h : "no ink");
        snprintf(rows[5].v, 64, "%s", g->codepoint != PX_NO_CODEPOINT && g->codepoint == font.defaultCodepoint ? "yes" : "no");
        rows[0].k = px_code_is_uncoded(g->codepoint) ? "No code point" : "Code point"; rows[1].k = "Advance"; rows[2].k = "Bitmap"; rows[3].k = "Offset x, y"; rows[4].k = "Glyph"; rows[5].k = "Default glyph";
        for (int i = 0; i < 6; i++) {
            Th_Text(rows[i].k, px + 16, y, TH_TEXT_DIM);
            snprintf(v, sizeof v, "%s", rows[i].v);
            bool mono = i == 0 || i == 4;
            Vector2 m = mono ? Th_MeasureMono(v) : Th_Measure(v);
            if (mono) Th_TextMono(v, Fl(px + PANEL_W - 16 - m.x), y, TH_TEXT);
            else Th_Text(v, Fl(px + PANEL_W - 16 - m.x), y, TH_TEXT);
            y += 21;
        }
        y += 4;
        {   /* how to get into the glyph view (double click or Enter do the same) */
            Rectangle br = {px + 12, y, PANEL_W - 24, 30};
            if (Th_Button(br, "Open in Glyph View") && Hov(br)) CmdEditSelected();
            const char *k = "Enter";
            Vector2 km = Th_MeasureSmall(k);
            Th_TextSmall(k, Fl(br.x + br.width - km.x - 10), Fl(br.y + (br.height - km.y) / 2), TH_TEXT_DIM);
            y += 30 + 8;
        }
        PanelHashBox(px, &y, "GLYPH HASH", h, "hash_glyph", "Glyph");
    } else {
        char s[48];
        if (selCount == 0) snprintf(s, sizeof s, "Nothing selected");
        else snprintf(s, sizeof s, "%d glyphs selected", selCount);
        Th_Text(s, px + 16, y, TH_TEXT_DIM);
        y += 21;
        Th_TextSmall("Select one glyph, then press Enter or", px + 16, y, TH_TEXT_DIM);
        y += 15;
        Th_TextSmall("double-click it to open the glyph view.", px + 16, y, TH_TEXT_DIM);
        y += 20;
    }
    *yp = y;
}

static void DrawMetricsPanel(Rectangle p)
{
    if (inEditor) { DrawGlyphPanel(p); return; }   /* the glyph view has its own panel (gpanel.inc) */
    DrawRectangleRec(p, TH_PANEL);
    Th_VLine(p.x, p.y, p.y + p.height, TH_LINE);

    /* the tab control: Font | Typeface | Glyph | Similar */
    int tab = panelTab;
    if (tab == 1 && !hasCont) tab = 0;
    if (tab == 3 && inEditor) tab = 2;
    {
        static const char *labels[4] = {"Font", "Typeface", "Glyph", "Similar"};
        unsigned dis = (hasCont ? 0u : 2u) | (inEditor ? 8u : 0u);
        int c = Th_Segmented((Rectangle){p.x + 8, p.y + 8, p.width - 16, 28}, labels, 4, tab, dis, "ptab");
        if (c >= 0) { PanelSetTab(c); tab = c; }
        Th_HLine(p.x + 1, p.x + p.width, p.y + PANEL_TABS_H - 1, TH_LINE);
    }
    Rectangle cr = {p.x, p.y + PANEL_TABS_H, p.width, p.height - PANEL_TABS_H};   /* the scrolling content */

    float maxS0 = panelContentH - cr.height;
    if (maxS0 < 0) maxS0 = 0;
    if (panelScroll > maxS0) panelScroll = maxS0;
    if (panelScroll < 0) panelScroll = 0;
    if (!hasCont || readOnly || tab != 1) { if (ctFocus >= 0) CtUnfocus(); }

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
        SectionLabel(px, &y, "FONT");
        PanelHashBox(px, &y, readOnly ? "FONT HASH" : "FONT HASH (CURRENT)", readOnly ? origHash : curHash, "hash_font_full", "Font");
        if (!readOnly && !Same(curHash, origHash)) PanelHashBox(px, &y, "FONT HASH (ORIGINAL)", origHash, "hash_font_orig", "Font");
        SectionLabel(px, &y, "FONT METRICS");
        static const struct { PxMetric m; Color c; } rows[] = {
            {PXM_ASCENT, TH_M_BOX}, {PXM_DESCENT, TH_M_BOX}, {PXM_LINEGAP, TH_TEXT_DIM}, {PXM_CAPHEIGHT, TH_M_CAP},
            {PXM_XHEIGHT, TH_M_XHEIGHT}, {PXM_INTERNALLEADING, TH_TEXT_DIM}, {PXM_QUADWIDTH, TH_M_ADVANCE},
            {PXM_UNDERLINEPOSITION, TH_M_UNDERLINE}, {PXM_UNDERLINETHICKNESS, TH_M_UNDERLINE},
            {PXM_STRIKEOUTPOSITION, TH_M_UNDERLINE}, {PXM_STRIKEOUTTHICKNESS, TH_M_UNDERLINE}};
        for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            MetricRow(px, y, rows[i].m, rows[i].c);
            y += 30;
        }
        RatioRow(px, y);
        y += 30;
        {
            bool v = font.monospaced;
            Th_Text("Monospaced", px + 54, Fl(y + (26 - Th_Measure("A").y) / 2), TH_TEXT);
            if (Switch((Rectangle){px + PANEL_W - 16 - 38, y + 3, 38, 20}, &v, !readOnly, "monospaced")) {
                PushUndo(0);
                font.monospaced = v;
            }
            y += 30;
        }
        int a, b;
        px_font_box(&font, &a, &b);
        char s[96];
        snprintf(s, sizeof s, "Font box %d above, %d below the baseline%s", a, b, (font.has[PXM_ASCENT] && font.has[PXM_DESCENT]) ? "" : " (from the glyphs)");
        Th_TextSmall(s, px + 16, y + 2, TH_TEXT_DIM);
        y += 24;
    } else if (tab == 1) {
        DrawTypefaceSection(px, &y);
    } else if (tab == 2) {
        SectionLabel(px, &y, "SELECTION");
        DrawSelectionSection(px, &y);
        if (selCount == 1) {   /* the finder on this code point (also Ctrl+Shift+F and the toolbar) */
            bool can = insCode >= 0 && finder;
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
        }
    }
    if (insight && (tab == 2 || tab == 3)) {
        int mask = 0;
        static char hasKey[2][33];
        static bool hasVal[2];
        if (!Same(hasKey[0], origHash) || !hasKey[0][0]) { snprintf(hasKey[0], 33, "%s", origHash); hasVal[0] = origHash[0] && store_has_pxfm(store_main_conn(store), origHash); }
        if (!Same(hasKey[1], origContHash) || !hasKey[1][0]) { snprintf(hasKey[1], 33, "%s", origContHash); hasVal[1] = origContHash[0] && store_has_pxtf(store_main_conn(store), origContHash); }
        if (tab == 2) mask = INSIGHT_SECTION_USAGE;
        else if (hasVal[0]) {
            Insight_SetFont(insight, origHash, hasVal[1] ? origContHash : "");
            mask = INSIGHT_SECTION_SIMILAR_FONTS | (hasVal[1] ? INSIGHT_SECTION_SIMILAR_TYPEFACES : 0);
        } else {
            Th_Text("Similar fonts need a font of the library.", px + 16, y + 2, TH_TEXT_DIM);
            y += 26;
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
