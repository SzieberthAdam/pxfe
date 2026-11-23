/* panel.inc - the right-hand panel of the font view, included by main.c.  A segmented control at its top picks the tab:
 * View (zoom, metric lines, captions), Font (the padlock, the hash, Clone / Export / Delete, the font's fields and every PXM_* metric as an
 * optional row), Glyph (the selection, the glyph commands, "Find in Library" and the glyph usage), Similar (similar fonts and typefaces) and
 * Layers (the font layers and the glyphs drawn from one of them, fitdoc.inc).  The tab is only changed by the user and is kept in pxfe.ini.
 * All editing is disabled in a locked font.  The content of a tab scrolls.  DrawInfoTab is also the Typeface and Font tab of the library
 * views (browser.c). */

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
    int i = px_font_find_cp(&font, cp);
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
    case PXM_QUADWIDTH: { int i = px_font_find_cp(&font, 'M'); return i >= 0 && font.glyphs[i].advance > 0 ? font.glyphs[i].advance : a + b; }
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

/* The BASELINE row of the font metrics: two buttons move the baseline one row up or down while everything stays where it is on the screen.
 * Up by 1: the ascent falls by 1 and the descent grows by 1, and so every height counted from the baseline falls by 1 (cap height, x-height, the
 * positions of the underline and the strike-out), every glyph's offset y falls by 1 (its bitmap stays where it is), and the font layers (they sit on
 * the baseline's origin) move their offset by -1.  Down is the mirror.  Thicknesses, line gap, internal leading and quad width do not change.
 * Only the stated metrics move.  A step that would make a metric go below its smallest value (the ascent, descent, cap height and x-height are
 * never negative) is refused. */
static const PxMetric kAboveBaseline[] = {PXM_ASCENT, PXM_CAPHEIGHT, PXM_XHEIGHT, PXM_UNDERLINEPOSITION, PXM_STRIKEOUTPOSITION};

/* why the baseline cannot move one row up (dir 1) or down (dir -1), NULL when it can */
static const char *BaselineWhy(int dir)
{
    if (!hasFont) return "No font is open";
    if (readOnly) return "The font is locked: unlock it first";
    int d = -dir;   /* what the heights above the baseline get */
    for (size_t i = 0; i < sizeof kAboveBaseline / sizeof kAboveBaseline[0]; i++) {
        PxMetric m = kAboveBaseline[i];
        if (font.has[m] && font.m[m] + d < MetricMin(m)) return TextFormat("The %s cannot go below %d", px_metric_label(m), MetricMin(m));
    }
    if (font.has[PXM_DESCENT] && font.m[PXM_DESCENT] - d < 0) return "The descent cannot go below 0";
    return NULL;
}

static void CmdBaseline(int dir)
{
    const char *why = BaselineWhy(dir);
    if (why) { Th_Toast(TH_TOAST_ERR, "%s", why); return; }
    PushUndo(0);
    int d = -dir;
    for (size_t i = 0; i < sizeof kAboveBaseline / sizeof kAboveBaseline[0]; i++)
        if (font.has[kAboveBaseline[i]]) font.m[kAboveBaseline[i]] += d;
    if (font.has[PXM_DESCENT]) font.m[PXM_DESCENT] -= d;
    for (int i = 0; i < font.count; i++) {
        PxGlyph *g = &font.glyphs[i];
        if (g->w > 0 || (g->lazy && g->hash[0])) g->y += d;   /* a glyph without ink has no offset (0, 0) */
    }
    LayersShiftY((float)d);
    baseShift += dir;
    needHash = true;
}

static void BaselineRow(float px, float y)
{
    float x = px + 16, ty = Fl(y + (TH_ROW_H - Th_Measure("A").y) / 2);
    Th_Text("Baseline", x, ty, TH_TEXT_DIM);
    float right = px + PANEL_W - 16;
    const char *whyUp = BaselineWhy(1), *whyDown = BaselineWhy(-1);
    Rectangle bd = {right - TH_ROW_H, y, TH_ROW_H, TH_ROW_H}, bu = {right - 2 * TH_ROW_H - 4, y, TH_ROW_H, TH_ROW_H};
    bool up = Th_Button(bu, "Baseline up", TH_BTN_ICON(TH_IC_UP) | TH_BTN_OFF(!whyUp), whyUp ? whyUp : "Move the baseline up one row: ascent -1, descent +1, the glyphs stay where they are");
    bool down = Th_Button(bd, "Baseline down", TH_BTN_ICON(TH_IC_DOWN) | TH_BTN_OFF(!whyDown), whyDown ? whyDown : "Move the baseline down one row: ascent +1, descent -1, the glyphs stay where they are");
    if (up) CmdBaseline(1);
    else if (down) CmdBaseline(-1);   /* a button acts only when the press began on it (Th_Released) */
}

/* A row of commands sharing the width (two rows when a label does not fit, the last row's buttons as wide as they can be); returns the index of the one pressed, -1 when none. */
static int ButtonRow(float x, float *y, float w, int n, const char *const *lab, const int *flags, const char *const *tips)
{
    int per = n;
    for (int i = 0; i < n; i++) if (Th_ButtonW(lab[i]) > (w - (n - 1) * TH_GAP) / n) { per = (n + 1) / 2; break; }
    int hit = -1;
    for (int i = 0; i < n; i++) {
        int inRow = n - i / per * per < per ? n - i / per * per : per;   /* the last row's buttons share the whole width too */
        float bw = (w - (float)(inRow - 1) * TH_GAP) / (float)inRow;
        Rectangle r = {x + (float)(i % per) * (bw + TH_GAP), *y + (float)(i / per) * (TH_ROW_H + TH_GAP), bw, TH_ROW_H};
        if (Th_Button(r, lab[i], flags ? flags[i] : 0, tips ? tips[i] : NULL)) hit = i;
    }
    *y += (float)((n + per - 1) / per) * (TH_ROW_H + TH_GAP);
    return hit;
}

/* ---- the commands of the Typeface and Font tabs: run at the end of the frame (they may replace the document or rescan the library) ---- */
/* the typeface-wide fields and the font's own (the Typeface and Font tabs, Add Font) */
static const PxtfText kTfFields[] = {PXTF_VERSION, PXTF_DESIGNER, PXTF_DESIGNERURL, PXTF_COPYRIGHT, PXTF_TRADEMARK, PXTF_LICENSE,
                                     PXTF_LICENSEURL, PXTF_VENDOR, PXTF_VENDORURL, PXTF_DESCRIPTION, PXTF_CODEPAGE};
static const PxtfText kFontFields[] = {PXTF_STYLE, PXTF_NAME, PXTF_WEIGHTNAME, PXTF_SLANT};

enum { LC_NONE, LC_LOCK, LC_DELETE, LC_CLONE, LC_EXPORT, LC_ADDFONT, LC_RENAME };
static struct { int cmd; bool typeface; char pxtf[33], pxfm[33]; int variant; char text[300]; } libCmd;

static void LibCmd(int cmd, bool typeface, const char *pxtf, const char *pxfm, int variant)
{
    libCmd.cmd = cmd;
    libCmd.typeface = typeface;
    snprintf(libCmd.pxtf, sizeof libCmd.pxtf, "%s", pxtf ? pxtf : "");
    snprintf(libCmd.pxfm, sizeof libCmd.pxfm, "%s", pxfm ? pxfm : "");
    libCmd.variant = variant;
}

/* Clone Font: a copy of the variant in the same typeface, naming the same font, its style "<style> copy".  The two variants share the font
 * until one of them is edited: then only that variant follows the new font (CommitEdits, store_edit_in_place). */
static void CloneFont(const char *pxtfIn, const char *pxfmIn, int variant)
{
    char tfh[33], fmh[33], err[PX_ERR_LEN] = "";
    snprintf(tfh, sizeof tfh, "%s", pxtfIn);
    snprintf(fmh, sizeof fmh, "%s", pxfmIn);
    if (!store || !tfh[0]) return;
    if (hasFont && docStored && hasCont && Same(tfh, origContHash) && Same(fmh, origHash)) {   /* the open font: its edits go in first */
        if (!CommitEdits()) return;
        snprintf(tfh, sizeof tfh, "%s", origContHash);
        snprintf(fmh, sizeof fmh, "%s", origHash);
        variant = contVariant;
    }
    if (LockedOf(true, tfh)) { Th_Toast(TH_TOAST_ERR, "The typeface is locked: unlock it first"); return; }
    PxtfTypeface t;
    pxtf_init(&t);
    PxtfVariant *nv = store_load_typeface(store_main_conn(store), tfh, &t, err) ? pxtf_clone_variant(&t, variant) : NULL;
    StoreEditResult er;
    memset(&er, 0, sizeof er);
    if (nv) {
        char st[300];
        snprintf(st, sizeof st, "%s copy", nv->text[PXTF_STYLE] && nv->text[PXTF_STYLE][0] ? nv->text[PXTF_STYLE] : "Regular");
        pxtf_set_text(nv, PXTF_STYLE, st);
    }
    if (!nv || !store_edit_in_place(store, fmh, NULL, tfh, &t, &er, err)) {
        ShowError("Cannot clone the font", err[0] ? err : "The typeface could not be read.");
        pxtf_free(&t);
        return;
    }
    int num = nv->number;
    if (fsettings) fs_rename_typeface(fsettings, tfh, er.pxtf);
    if (browser) Browser_Follow(browser, tfh, er.pxtf);
    for (int i = 0; i < er.nMoved; i++) {
        if (fsettings) fs_rename_typeface(fsettings, er.movedFrom[i], er.movedTo[i]);
        if (browser) Browser_Follow(browser, er.movedFrom[i], er.movedTo[i]);
    }
    if (hasCont && Same(origContHash, tfh)) {   /* the open font's typeface got the new variant */
        pxtf_free(&cont);
        pxtf_copy(&cont, &t);
        snprintf(origContHash, sizeof origContHash, "%s", er.pxtf);
        needHash = true;
        EnsureHashes();
    }
    pxtf_free(&t);
    if (browser) Browser_Rescan(browser);
    Th_Toast(TH_TOAST_OK, "Cloned as variant %04d: edit either of them and it gets a font of its own", num);
}

/* New Typeface (the "+" after the browser's boxes): a typeface with no variant (FORMAT PXFE, TYPE pxfe, a random HASH), unlocked, selected
 * with its Typeface tab showing; Add Font gives it fonts. */
static void NewTypeface(void)
{
    char h[33], err[PX_ERR_LEN] = "", name[64] = "New Typeface";
    if (!store) { Th_Toast(TH_TOAST_ERR, "No database is loaded"); return; }
    bool ok = false;
    for (int i = 1; i < 1000 && !ok; i++) {   /* two empty typefaces of one name would be one file: New Typeface, New Typeface 2, ... */
        if (i > 1) snprintf(name, sizeof name, "New Typeface %d", i);
        ok = store_new_typeface(store, name, h, err);
    }
    if (!ok) { ShowError("Cannot make a typeface", err); return; }
    if (fsettings) fs_set_unlocked(fsettings, 1, h, true);
    if (browser) { Browser_Rescan(browser); Browser_SelectTypeface(browser, h); }
    Th_Toast(TH_TOAST_OK, "New typeface: Add Font in its Typeface tab gives it fonts");
}

/* Add Font: a font of the library becomes a new variant of a typeface; it stays in its other typefaces too.  The variant has FORMAT PXFE and
 * TYPE pxfe and no reading options, the typeface-wide fields of the typeface (of the font's old variant when the typeface has none), and the
 * style, names, slant and flags of the font's old variant (STYLE Regular when the font had none). */
static struct {
    bool open; int frame;
    char pxtf[33];
    char filter[120];
    StoreEntryList fl; int *vis, nvis;
    int sel; float scroll; double lastClick; int lastRow;
    bool dirty;
} af;

static void AddFontClose(void)
{
    store_entry_list_free(&af.fl);
    free(af.vis);
    af.vis = NULL;
    af.open = false;
    Th_TextBlur();
}

static void AddFontOpen(const char *pxtf)
{
    char err[PX_ERR_LEN] = "";
    if (!store || !pxtf || !pxtf[0]) return;
    if (LockedOf(true, pxtf)) { Th_Toast(TH_TOAST_ERR, "The typeface is locked: unlock it first"); return; }
    if (af.open) AddFontClose();
    memset(&af, 0, sizeof af);
    if (!store_list_fonts(store_main_conn(store), &af.fl, err)) { ShowError("Cannot list the fonts", err); return; }
    snprintf(af.pxtf, sizeof af.pxtf, "%s", pxtf);
    af.vis = (int *)malloc(sizeof(int) * (size_t)(af.fl.count + 1));
    af.open = true;
    af.frame = frameNo;
    af.sel = -1;
    af.lastRow = -1;
    af.dirty = true;
    Th_TextFocus("af.filter", "", false);
}

static void AddFontToTypeface(const char *pxtfIn, const char *pxfmIn)
{
    char tfh[33], fmh[33], err[PX_ERR_LEN] = "";
    snprintf(tfh, sizeof tfh, "%s", pxtfIn);
    snprintf(fmh, sizeof fmh, "%s", pxfmIn);
    if (!store || !tfh[0] || !fmh[0]) return;
    if (hasFont && docStored && hasCont && Same(tfh, origContHash)) {   /* the open font's typeface: its edits go in first */
        if (!CommitEdits()) return;
        snprintf(tfh, sizeof tfh, "%s", origContHash);
    }
    if (LockedOf(true, tfh)) { Th_Toast(TH_TOAST_ERR, "The typeface is locked: unlock it first"); return; }
    StoreConn *c = store_main_conn(store);
    PxtfTypeface t, old;
    pxtf_init(&t);
    pxtf_init(&old);
    bool haveOld = false;
    const PxtfVariant *ov = NULL;
    {   /* the font's old variant: the first typeface variant that names it */
        StoreEntryList one;
        uint8_t fb[16];
        memset(&one, 0, sizeof one);
        if (store_hex_to_blob(fmh, fb) && store_list_fonts(c, &one, err)) {
            for (int i = 0; i < one.count; i++)
                if (Same(one.e[i].pxfm, fmh) && one.e[i].pxtf[0]) {
                    haveOld = store_load_typeface(c, one.e[i].pxtf, &old, err);
                    if (haveOld) ov = pxtf_find_variant(&old, one.e[i].variant);
                    break;
                }
            store_entry_list_free(&one);
        }
        err[0] = 0;
    }
    PxtfVariant *nv = store_load_typeface(c, tfh, &t, err) ? pxtf_add_variant(&t) : NULL;
    StoreEditResult er;
    memset(&er, 0, sizeof er);
    bool ok = nv != NULL;
    if (ok) {
        const PxtfVariant *fromTf = t.count > 1 ? &t.v[0] : ov;   /* the typeface-wide fields */
        nv = &t.v[t.count - 1];
        memcpy(nv->pxfm, fmh, 33);
        if (ov) {   /* the font's source, as its old variant states it (pxtf: "a variant copied from another typeface keeps its source lines") */
            memcpy(nv->source, ov->source, 33);
            nv->format = _strdup(ov->format ? ov->format : PXTF_EDITOR_FORMAT);
            nv->type = _strdup(ov->type ? ov->type : PXTF_EDITOR_TYPE);
            for (int o = 0; o < ov->nopts; o++) {   /* and its reading options */
                char **no = (char **)realloc(nv->opts, sizeof(char *) * (size_t)(nv->nopts + 1));
                if (!no) break;
                nv->opts = no;
                nv->opts[nv->nopts++] = _strdup(ov->opts[o]);
            }
        } else pxtf_set_editor_source(nv);   /* a font no typeface names: made in an editor */
        if (!t.name && haveOld && old.name) pxtf_set_name(&t, old.name);
        for (int k = 0; fromTf && k < (int)(sizeof kTfFields / sizeof kTfFields[0]); k++) pxtf_set_text(nv, kTfFields[k], fromTf->text[kTfFields[k]]);
        for (int k = 0; ov && k < (int)(sizeof kFontFields / sizeof kFontFields[0]); k++) pxtf_set_text(nv, kFontFields[k], ov->text[kFontFields[k]]);
        if (ov) { nv->italic = ov->italic; nv->struckout = ov->struckout; nv->underlined = ov->underlined; }
        if (!nv->text[PXTF_STYLE]) pxtf_set_text(nv, PXTF_STYLE, "Regular");
        ok = store_edit_in_place(store, fmh, NULL, tfh, &t, &er, err);
    }
    if (haveOld) pxtf_free(&old);
    if (!ok) {
        ShowError("Cannot add the font", err[0] ? err : "The typeface could not be read.");
        pxtf_free(&t);
        return;
    }
    int num = t.v[t.count - 1].number;
    if (fsettings) fs_rename_typeface(fsettings, tfh, er.pxtf);
    if (browser) Browser_Follow(browser, tfh, er.pxtf);
    for (int i = 0; i < er.nMoved; i++) {
        if (fsettings) fs_rename_typeface(fsettings, er.movedFrom[i], er.movedTo[i]);
        if (browser) Browser_Follow(browser, er.movedFrom[i], er.movedTo[i]);
    }
    if (hasCont && Same(origContHash, tfh)) {   /* the open font's typeface got the new variant */
        pxtf_free(&cont);
        pxtf_copy(&cont, &t);
        snprintf(origContHash, sizeof origContHash, "%s", er.pxtf);
        needHash = true;
        EnsureHashes();
    }
    pxtf_free(&t);
    if (browser) {
        Browser_Rescan(browser);
        if (inBrowser && !Browser_InTypeface(browser)) Browser_SelectTypeface(browser, er.pxtf);
    }
    Th_Toast(TH_TOAST_OK, "Added as variant %04d of the typeface", num);
}

/* the fonts of the list that match the filter (typeface name, style, font name, hash), in the order of their names */
static int AfCmp(const void *pa, const void *pb)
{
    const StoreEntry *a = &af.fl.e[*(const int *)pa], *b = &af.fl.e[*(const int *)pb];
    int r = _stricmp(a->typefaceName, b->typefaceName);
    if (!r) r = _stricmp(a->style, b->style);
    return r ? r : strcmp(a->pxfm, b->pxfm);
}

/* `needle` in `hay`, ASCII letters in either case */
static bool AfHas(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) if (_strnicmp(hay, needle, n) == 0) return true;
    return false;
}

static void AfRefilter(void)
{
    af.nvis = 0;
    for (int i = 0; i < af.fl.count; i++) {
        const StoreEntry *e = &af.fl.e[i];
        if (af.filter[0] && !(AfHas(e->typefaceName, af.filter) || AfHas(e->style, af.filter) || AfHas(e->fontName, af.filter) || AfHas(e->pxfm, af.filter))) continue;
        af.vis[af.nvis++] = i;
    }
    qsort(af.vis, (size_t)af.nvis, sizeof(int), AfCmp);
    af.sel = af.nvis > 0 ? 0 : -1;
    af.scroll = 0;
    af.dirty = false;
}

static void DrawAddFontDialog(int W, int H)
{
    if (!af.open) return;
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    uiBlocked = false;
    Th_SetInputBlocked(false);
    bool fresh = af.frame == frameNo;
    if (af.dirty) AfRefilter();
    const float pw = 560, pad = 24, rowH = 46;
    float ph = fminf(620, H - 80.0f);
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    Th_Raised(pr, 1);
    float x = pr.x + pad, w = pr.width - 2 * pad, y = pr.y + pad;
    Th_TextHead("Add Font to Typeface", x, y, TH_TEXT);
    y += TH_SIZE_HEAD + 6;
    Th_TextSmall("The font becomes a new variant of the typeface and stays in its other typefaces too.", x, y, TH_TEXT_DIM);
    y += TH_LINE_H + TH_PAD / 2;
    if (Th_TextField((Rectangle){x, y, w, TH_ROW_H}, "af.filter", af.filter, (int)sizeof af.filter, TH_TF_SEARCH, "Filter by name or hash\xE2\x80\xA6") & TH_TF_CHANGED) {
        snprintf(af.filter, sizeof af.filter, "%s", Th_TextValue());
        af.dirty = true;
    }
    y += TH_ROW_H + TH_GAP;
    /* the list */
    float listBot = pr.y + pr.height - pad - TH_ROW_H - TH_PAD;
    Rectangle L = {x, y, w, listBot - y};
    Th_RectLines(L, TH_LINE);
    float content = af.nvis * rowH;
    Rectangle in = {L.x + 1, L.y + 1, L.width - 2, L.height - 2};
    bool onBar = Th_ScrollBar(in, &af.scroll, content, rowH, true);
    if (CheckCollisionPointRec(GetMousePosition(), in)) { float wh = GetMouseWheelMove(); if (wh != 0) af.scroll -= wh * rowH * 2; }
    float maxS = content > in.height ? content - in.height : 0;
    if (af.scroll > maxS) af.scroll = maxS;
    if (af.scroll < 0) af.scroll = 0;
    bool add = false;
    /* keys: Up, Down, Enter, Escape */
    if (!fresh && af.nvis > 0) {
        int s0 = af.sel;
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) af.sel = af.sel + 1 < af.nvis ? af.sel + 1 : af.nvis - 1;
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) af.sel = af.sel > 0 ? af.sel - 1 : 0;
        if (af.sel != s0) {
            float top = af.sel * rowH;
            if (top < af.scroll) af.scroll = top;
            if (top + rowH > af.scroll + in.height) af.scroll = top + rowH - in.height;
        }
    }
    BeginScissorMode((int)in.x, (int)in.y, (int)(in.width - TH_SB_W), (int)in.height);
    int r0 = (int)(af.scroll / rowH);
    for (int i = r0; i < af.nvis; i++) {
        float ry = in.y + i * rowH - af.scroll;
        if (ry > in.y + in.height) break;
        const StoreEntry *e = &af.fl.e[af.vis[i]];
        Rectangle rr = {in.x, ry, in.width - TH_SB_W, rowH};
        bool over = !onBar && CheckCollisionPointRec(GetMousePosition(), rr) && CheckCollisionPointRec(GetMousePosition(), in);
        Th_Reg(TextFormat("af.row%d", i), rr);
        Th_ListRow(rr, (i == af.sel ? TH_LR_SELECTED : 0) | (over ? TH_LR_HOVER : 0));
        if (over && !fresh && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            double now = GetTime();
            if (af.lastRow == i && now - af.lastClick < 0.4) add = true;
            af.sel = i;
            af.lastRow = i;
            af.lastClick = now;
        }
        char title[300], fit[300], det[200];
        if (e->typefaceName[0]) snprintf(title, sizeof title, "%s%s%s", e->typefaceName, e->style[0] ? " " : "", e->style);
        else if (e->fontName[0]) snprintf(title, sizeof title, "%s", e->fontName);
        else snprintf(title, sizeof title, "Font %.8s", e->pxfm);
        Th_FitText(title, 0, rr.width - 24, fit, sizeof fit);
        Th_Text(fit, rr.x + 12, ry + 4, TH_TEXT);
        int nd = 0;
        det[0] = 0;
        if (e->hasHeight) nd += snprintf(det + nd, sizeof det - (size_t)nd, "%d px  \xC2\xB7  ", e->height);
        snprintf(det + nd, sizeof det - (size_t)nd, "%d glyph%s%s", e->glyphCount, e->glyphCount == 1 ? "" : "s", e->monospaced ? "  \xC2\xB7  Mono" : "");
        Th_TextSmall(det, rr.x + 12, ry + 26, TH_TEXT_DIM);
        Th_HashShort("Font", e->pxfm, rr.x + rr.width - 12, ry + 26, TH_HASH_RIGHT | TH_HASH_SMALL, NULL);
    }
    if (af.nvis == 0) {
        const char *m = af.fl.count ? "No font matches" : "The library has no fonts";
        Vector2 ms = Th_Measure(m);
        Th_Text(m, in.x + (in.width - ms.x) / 2, in.y + in.height / 2 - 10, TH_TEXT_DIM);
    }
    EndScissorMode();
    /* the buttons */
    y = pr.y + pr.height - pad - TH_ROW_H;
    float bx = pr.x + pr.width - pad - 96;
    Th_Reg("af.add", (Rectangle){bx, y, 96, TH_ROW_H});
    if (Th_Button((Rectangle){bx, y, 96, TH_ROW_H}, "Add", TH_BTN_PRIMARY | TH_BTN_OFF(af.sel >= 0), "Add the selected font to the typeface") && !fresh) add = true;
    bool cancel = Th_Button((Rectangle){bx - 8 - 96, y, 96, TH_ROW_H}, "Cancel", 0, NULL) && !fresh;
    if (!fresh && IsKeyPressed(KEY_ESCAPE)) cancel = true;
    if (!fresh && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) add = true;
    if (cancel) { AddFontClose(); return; }
    if (add && af.sel >= 0 && af.sel < af.nvis) {
        char tf[33], fm[33];
        snprintf(tf, sizeof tf, "%s", af.pxtf);
        snprintf(fm, sizeof fm, "%s", af.fl.e[af.vis[af.sel]].pxfm);
        AddFontClose();
        AddFontToTypeface(tf, fm);
    }
}

/* ------------------------------------------------------------------ the licence of a font (a click on its card's licence label)
 * A table of the terms in plain words (pxtf "Showing the terms"), what the source states about the licence, and the licence
 * texts a click away (the text view of the same dialog). */

static struct {
    bool open;
    int frame;
    char pxtf[33];
    int variant;
    PxtfTypeface tf;
    bool haveTf;
    char title[300];
    float scroll;
    /* the text view: one licence text, wrapped to the dialog */
    char textHash[33];
    char **lines; int nlines;
    size_t textLen;
} li;

static void LicenceTextFree(void)
{
    for (int i = 0; i < li.nlines; i++) free(li.lines[i]);
    free(li.lines);
    li.lines = NULL;
    li.nlines = 0;
    li.textHash[0] = 0;
}

static void LicenceClose(void)
{
    LicenceTextFree();
    if (li.haveTf) pxtf_free(&li.tf);
    li.haveTf = false;
    li.open = false;
}

static void LicenceOpen(const char *pxtf, int variant)
{
    char err[PX_ERR_LEN] = "";
    if (!store || !pxtf || !pxtf[0]) return;
    if (li.open) LicenceClose();
    pxtf_init(&li.tf);
    if (!store_load_typeface(store_main_conn(store), pxtf, &li.tf, err)) { pxtf_free(&li.tf); ShowError("Cannot read the typeface", err); return; }
    li.haveTf = true;
    snprintf(li.pxtf, sizeof li.pxtf, "%s", pxtf);
    li.variant = variant;
    const PxtfVariant *v = pxtf_find_variant(&li.tf, variant);
    const char *st = v && v->text[PXTF_STYLE] ? v->text[PXTF_STYLE] : "";
    if (li.tf.name && li.tf.name[0]) snprintf(li.title, sizeof li.title, "%s%s%s", li.tf.name, st[0] ? " " : "", st);
    else if (v && v->text[PXTF_NAME]) snprintf(li.title, sizeof li.title, "%s", v->text[PXTF_NAME]);
    else snprintf(li.title, sizeof li.title, "Font %.8s", v ? v->pxfm : "");
    li.scroll = 0;
    li.open = true;
    li.frame = frameNo;
}

/* greedy word wrap of `s` to maxW (face 0 body, 1 small); a word longer than the line is cut where it must */
static int LicWrap(const char *s, float maxW, int face, char (*out)[400], int maxLines)
{
    int n = 0;
    char line[400] = "";
    const char *p = s;
    while (*p && n < maxLines) {
        if (*p == '\n') { snprintf(out[n++], 400, "%s", line); line[0] = 0; p++; continue; }
        const char *we = p;
        while (*we && *we != ' ' && *we != '\n') we++;
        char word[400];
        snprintf(word, sizeof word, "%.*s", (int)(we - p) > 380 ? 380 : (int)(we - p), p);
        char trial[800];
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        float tw = face ? Th_MeasureSmall(trial).x : Th_Measure(trial).x;
        if (tw <= maxW || !line[0]) {
            if (tw > maxW) {   /* a word alone too long: cut it */
                size_t k = strlen(word);
                while (k > 1) { char c = word[k]; word[k] = 0; float ww = face ? Th_MeasureSmall(word).x : Th_Measure(word).x; word[k] = c; if (ww <= maxW) break; k--; }
                while (k > 1 && (word[k] & 0xC0) == 0x80) k--;   /* not inside a UTF-8 sequence */
                snprintf(out[n++], 400, "%.*s", (int)k, word);
                p += k;
                continue;
            }
            snprintf(line, sizeof line, "%.399s", trial);
        } else {   /* the line is full: the word starts the next one, and is cut there if it is too long alone */
            snprintf(out[n++], 400, "%s", line);
            line[0] = 0;
            continue;
        }
        p = we;
        while (*p == ' ') p++;
    }
    if (line[0] && n < maxLines) snprintf(out[n++], 400, "%s", line);
    return n;
}

static void LicenceShowText(const char *hash, float width)
{
    char err[PX_ERR_LEN] = "", got[33];
    size_t len = 0;
    char *t = store_license_text(store_main_conn(store), hash, &len, got);
    if (!t) { snprintf(err, sizeof err, "The library has no licence text %s.", hash); ShowError("Cannot show the licence text", err); return; }
    LicenceTextFree();
    snprintf(li.textHash, sizeof li.textHash, "%s", hash);
    li.textLen = len;
    int cap = 256;
    li.lines = (char **)malloc(sizeof(char *) * (size_t)cap);
    static char wrapped[64][400];
    char *p = t;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        int k = p[0] ? LicWrap(p, width, 1, wrapped, 64) : 0;
        if (k == 0) { snprintf(wrapped[0], 400, "%s", ""); k = 1; }
        for (int i = 0; i < k; i++) {
            if (li.nlines == cap) { cap *= 2; li.lines = (char **)realloc(li.lines, sizeof(char *) * (size_t)cap); }
            li.lines[li.nlines++] = _strdup(wrapped[i]);
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(t);
    li.scroll = 0;
}

/* one row of the table: the label, then the value wrapped in the rest; returns its height (draws only when draw) */
static float LicRow(float x, float y, float w, const char *label, const char *value, Color vc, bool draw)
{
    const float lw = 232;
    static char lines[16][400];
    int n = LicWrap(value && value[0] ? value : "\xE2\x80\x94", w - lw, 0, lines, 16);
    if (draw) {
        Th_Text(label, x, y, TH_TEXT_DIM);
        for (int i = 0; i < n; i++) Th_Text(lines[i], x + lw, y + i * TH_LINE_H, vc);
    }
    return n * TH_LINE_H + 8;
}

/* a row whose value has a URL: "Copy URL" at the right copies it (widget `name`) */
static float LicUrlRow(float x, float y, float w, const char *label, const char *value, const char *url, const char *name, const char *what,
                       bool draw, bool fresh)
{
    if (!url || !url[0]) return LicRow(x, y, w, label, value, TH_TEXT, draw);
    const float bw = Th_ButtonW("Copy URL");
    float h = LicRow(x, y, w - bw - 12, label, value, TH_TEXT, draw);
    if (draw) {
        Rectangle br = {x + w - bw, y - 4, bw, TH_ROW_H};
        Th_Reg(name, br);
        if (Th_Button(br, "Copy URL", 0, url) && !fresh) { SetClipboardText(url); Th_Toast(0, "%s copied", what); }
    }
    return h > TH_ROW_H + 4 ? h : TH_ROW_H + 4;
}

static const char *LicWord(const PxtfVariant *v, const char *key)
{
    const char *w = pxtf_opt(v, key);
    return w ? pxtf_terms_meaning(key, w) : NULL;
}

/* the terms of the variant as rows; the licence texts' buttons; returns the height of it all */
static float LicTable(const PxtfVariant *v, float x, float y0, float w, bool draw, bool fresh)
{
    float y = y0;
    char buf[1200];
    StoreTerms t;
    store_terms_of_variant(v, &t);
    /* who made it and under what: Copyright, Designer, Licence (a Copy URL button when there is a URL), the licence texts */
    if (v->text[PXTF_COPYRIGHT]) y += LicRow(x, y, w, "Copyright", v->text[PXTF_COPYRIGHT], TH_TEXT, draw);
    if (v->text[PXTF_DESIGNER] || v->text[PXTF_DESIGNERURL])
        y += LicUrlRow(x, y, w, "Designer", v->text[PXTF_DESIGNER] ? v->text[PXTF_DESIGNER] : v->text[PXTF_DESIGNERURL], v->text[PXTF_DESIGNERURL],
                       "li.copy.designer", "Designer URL", draw, fresh);
    const char *nm = v->text[PXTF_TERMS_LICENSE], *sp = v->text[PXTF_TERMS_SPDX], *cl = LicWord(v, "TERMS-CLASS");
    {   /* Licence: the name the terms give, the SPDX expression in brackets, else the kind; without terms the source's own */
        if (nm && sp) snprintf(buf, sizeof buf, "%s (%s)", nm, sp);
        else snprintf(buf, sizeof buf, "%s", nm ? nm : sp ? sp : cl ? cl : v->text[PXTF_LICENSE] ? v->text[PXTF_LICENSE] : "");
        const char *url = v->text[PXTF_LICENSEURL];
        if (buf[0] || url) y += LicUrlRow(x, y, w, "Licence", buf[0] ? buf : url, url, "li.copy.licence", "Licence URL", draw, fresh);
    }
    {   /* the licence texts: a button each, with the hash */
        const char *tx[16];
        int nt = pxtf_opt_all(v, "TERMS-TEXT", tx, 16);
        if (nt > 0) {
            if (draw) Th_Text(nt == 1 ? "Licence text" : "Licence texts", x, y + 4, TH_TEXT_DIM);
            for (int i = 0; i < nt; i++) {
                char hl[33];
                for (int k = 0; k < 32; k++) hl[k] = (char)(tx[i][k] >= 'A' && tx[i][k] <= 'F' ? tx[i][k] + 32 : tx[i][k]);
                hl[32] = 0;
                if (draw) {
                    Rectangle br = {x + 232, y, 110, TH_ROW_H};
                    char bn[32];
                    snprintf(bn, sizeof bn, "li.text%d", i);
                    Th_Reg(bn, br);
                    if (Th_Button(br, "Show text", 0, "Show the licence text") && !fresh) LicenceShowText(hl, 660 - 2 * 24 - TH_SB_W - 8);
                    Th_HashShort("Licence text", hl, br.x + br.width + 12, y + 6, 0, NULL);
                }
                y += TH_ROW_H + 4;
            }
            y += 4;
        }
    }
    if (!t.any) {
        if (draw) Th_Text("Nobody has looked at the terms of this font yet.", x, y, TH_TEXT_NOTE);
        y += TH_LINE_H + 12;
    } else {
        if (cl && (nm || sp)) y += LicRow(x, y, w, "Kind", cl, TH_TEXT, draw);
        const char *cm = LicWord(v, "TERMS-COMMERCIAL"), *lim = LicWord(v, "TERMS-USE-LIMIT");
        snprintf(buf, sizeof buf, "%s%s%s", cm ? cm : "", lim ? ": " : "", lim ? lim : "");
        y += LicRow(x, y, w, "Commercial use", buf, TH_TEXT, draw);
        y += LicRow(x, y, w, "Changing it", LicWord(v, "TERMS-MODIFY"), TH_TEXT, draw);
        y += LicRow(x, y, w, "Passing it on", LicWord(v, "TERMS-REDISTRIBUTE"), TH_TEXT, draw);
        y += LicRow(x, y, w, "Passing on a changed version", LicWord(v, "TERMS-REDISTRIBUTE-MODIFIED"), TH_TEXT, draw);
        {   /* the conditions, one per line */
            const char *ob[16];
            int no = pxtf_opt_all(v, "TERMS-OBLIGATION", ob, 16);
            size_t o = 0;
            buf[0] = 0;
            for (int i = 0; i < no && o < sizeof buf - 2; i++)
                o += (size_t)snprintf(buf + o, sizeof buf - o, "%s%s", i ? "\n" : "", pxtf_terms_meaning("TERMS-OBLIGATION", ob[i]));
            y += LicRow(x, y, w, "Conditions", no ? buf : "none", TH_TEXT, draw);
        }
        {   /* the design: where it comes from, what it is drawn after, and the rights of that */
            const char *pv = LicWord(v, "TERMS-PROVENANCE"), *df = v->text[PXTF_TERMS_DERIVED_FROM], *uh = v->text[PXTF_TERMS_UPSTREAM_HOLDER];
            const char *us = LicWord(v, "TERMS-UPSTREAM-STATUS");
            size_t o = (size_t)snprintf(buf, sizeof buf, "%s", pv ? pv : "");
            if (df) o += (size_t)snprintf(buf + o, sizeof buf - o, "%safter %s", o ? ", " : "", df);
            if (uh) o += (size_t)snprintf(buf + o, sizeof buf - o, "%srights of %s", o ? "; " : "", uh);
            if (us) snprintf(buf + o, sizeof buf - o, "%s%s", o ? ": " : "rights: ", us);
            y += LicRow(x, y, w, "Design", buf, TH_TEXT, draw);
        }
        if (v->text[PXTF_TERMS_NOTE]) y += LicRow(x, y, w, "Note", v->text[PXTF_TERMS_NOTE], TH_TEXT_NOTE, draw);
        if (v->text[PXTF_TERMS_EVIDENCE]) {
            snprintf(buf, sizeof buf, "\xE2\x80\x9C%s\xE2\x80\x9D", v->text[PXTF_TERMS_EVIDENCE]);
            y += LicRow(x, y, w, "Evidence", buf, TH_TEXT_DIM, draw);
        }
        {
            const char *src = v->text[PXTF_TERMS_SOURCE], *cf = pxtf_opt(v, "TERMS-CONFIDENCE"), *dt = pxtf_opt(v, "TERMS-DATE");
            size_t o = (size_t)snprintf(buf, sizeof buf, "%s", src ? src : "");
            if (cf) o += (size_t)snprintf(buf + o, sizeof buf - o, "%s%s confidence", o ? ", " : "", cf);
            if (dt) snprintf(buf + o, sizeof buf - o, "%s%s", o ? ", " : "", dt);
            y += LicRow(x, y, w, "Read from", buf, TH_TEXT, draw);
        }
    }
    return y - y0;
}

static void DrawLicenceDialog(int W, int H)
{
    if (!li.open) return;
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    uiBlocked = false;
    Th_SetInputBlocked(false);
    bool fresh = li.frame == frameNo;
    const PxtfVariant *v = li.haveTf ? pxtf_find_variant(&li.tf, li.variant) : NULL;
    const float pw = 660, pad = 24;
    bool textView = li.textHash[0] != 0;
    float x0 = 0, w = pw - 2 * pad - TH_SB_W - 8;
    float content = textView ? li.nlines * 18.0f + 8 : (v ? LicTable(v, x0, 0, w, false, true) : TH_LINE_H);
    float head = TH_SIZE_HEAD + 6 + TH_LINE_H + TH_PAD / 2;
    float ph = fminf(H - 80.0f, head + content + 2 * pad + TH_ROW_H + TH_PAD + 8);
    if (textView) ph = H - 80.0f < 700 ? H - 80.0f : 700;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    Th_Raised(pr, 1);
    float x = pr.x + pad, y = pr.y + pad;
    char fit[300];
    Th_FitText(li.title, 0, pr.width - 2 * pad, fit, sizeof fit);
    Th_TextHead(fit, x, y, TH_TEXT);
    y += TH_SIZE_HEAD + 6;
    if (textView) {
        char sub[120];
        snprintf(sub, sizeof sub, "Licence text, %zu bytes", li.textLen);
        Th_TextSmall(sub, x, y, TH_TEXT_DIM);
        Th_HashShort("Licence text", li.textHash, pr.x + pr.width - pad, y, TH_HASH_RIGHT | TH_HASH_SMALL, NULL);
    } else {
        Th_TextSmall("The terms as the library reads them; they are no legal advice.", x, y, TH_TEXT_DIM);
    }
    y += TH_LINE_H + TH_PAD / 2;
    Rectangle in = {x - 4, y, pr.width - 2 * pad + 8, pr.y + pr.height - pad - TH_ROW_H - TH_PAD - y};
    bool onBar = Th_ScrollBar(in, &li.scroll, content, 40, true);
    (void)onBar;
    if (CheckCollisionPointRec(GetMousePosition(), in)) { float wh = GetMouseWheelMove(); if (wh != 0) li.scroll -= wh * 60; }
    float maxS = content > in.height ? content - in.height : 0;
    if (li.scroll > maxS) li.scroll = maxS;
    if (li.scroll < 0) li.scroll = 0;
    BeginScissorMode((int)in.x, (int)in.y, (int)(in.width - TH_SB_W), (int)in.height);
    if (textView) {
        int first = (int)(li.scroll / 18.0f);
        for (int i = first < 0 ? 0 : first; i < li.nlines; i++) {
            float ly = in.y + 4 + i * 18.0f - li.scroll;
            if (ly > in.y + in.height) break;
            Th_TextSmall(li.lines[i], x, ly, TH_TEXT);
        }
    } else if (v) LicTable(v, x, in.y - li.scroll, w, true, fresh);
    else Th_Text("The variant is not in the typeface any more.", x, in.y, TH_TEXT_DIM);
    EndScissorMode();
    /* the buttons */
    y = pr.y + pr.height - pad - TH_ROW_H;
    float bx = pr.x + pr.width - pad - 96;
    Th_Reg("li.close", (Rectangle){bx, y, 96, TH_ROW_H});
    bool close = Th_Button((Rectangle){bx, y, 96, TH_ROW_H}, "Close", TH_BTN_PRIMARY, NULL) && !fresh;
    if (textView) {
        Th_Reg("li.back", (Rectangle){bx - 8 - 96, y, 96, TH_ROW_H});
        if (Th_Button((Rectangle){bx - 8 - 96, y, 96, TH_ROW_H}, "Back", 0, "Back to the terms") && !fresh) { LicenceTextFree(); li.scroll = 0; }
    }
    if (!fresh && IsKeyPressed(KEY_ESCAPE)) {
        if (li.textHash[0]) { LicenceTextFree(); li.scroll = 0; } else close = true;
    }
    if (close) LicenceClose();
}

/* The Name of an unlocked typeface of the library that is not the open document's (an empty one, say): it is rewritten in place under a
 * new hash (the open document's typeface is edited in `cont` and committed with the font instead). */
static void RenameTypeface(const char *pxtfIn, const char *name)
{
    char tfh[33], err[PX_ERR_LEN] = "";
    snprintf(tfh, sizeof tfh, "%s", pxtfIn);
    if (!store || !tfh[0]) return;
    if (LockedOf(true, tfh)) { Th_Toast(TH_TOAST_ERR, "The typeface is locked: unlock it first"); return; }
    PxtfTypeface t;
    pxtf_init(&t);
    StoreEditResult er;
    memset(&er, 0, sizeof er);
    bool ok = store_load_typeface(store_main_conn(store), tfh, &t, err);
    if (ok && t.count == 0 && !(name && name[0])) { pxtf_free(&t); Th_Toast(TH_TOAST_ERR, "A typeface with no font needs a name"); return; }
    if (ok) ok = pxtf_set_name(&t, name) && store_edit_in_place(store, "", NULL, tfh, &t, &er, err);
    pxtf_free(&t);
    if (!ok) { ShowError("Cannot rename the typeface", err[0] ? err : "The typeface could not be read."); return; }
    if (fsettings) fs_rename_typeface(fsettings, tfh, er.pxtf);
    if (browser) {
        Browser_Follow(browser, tfh, er.pxtf);
        Browser_Rescan(browser);
        if (inBrowser && !Browser_InTypeface(browser)) Browser_SelectTypeface(browser, er.pxtf);
    }
}

static void ExportPackage(bool typeface, const char *pxtf, const char *pxfm, int variant);   /* below */

static void RunLibCommand(void)
{
    int cmd = libCmd.cmd;
    libCmd.cmd = LC_NONE;
    bool tf = libCmd.typeface;
    if (cmd == LC_LOCK) ToggleLockOf(tf, libCmd.pxtf, libCmd.pxfm, libCmd.variant);
    else if (cmd == LC_DELETE) AskDelete(tf, tf ? libCmd.pxtf : libCmd.pxfm, !inBrowser);
    else if (cmd == LC_CLONE) CloneFont(libCmd.pxtf, libCmd.pxfm, libCmd.variant);
    else if (cmd == LC_EXPORT) ExportPackage(tf, libCmd.pxtf, libCmd.pxfm, libCmd.variant);
    else if (cmd == LC_ADDFONT) AddFontOpen(libCmd.pxtf);
    else if (cmd == LC_RENAME) RenameTypeface(libCmd.pxtf, libCmd.text);
}

/* ---- the Typeface and Font tabs ---- */
static char tabHash[40];   /* the hash the Typeface tab shows (a test hook) */

/* the text fields `keys` of variant cv; `all`: a typeface-wide field, an edit sets it in every variant of tf */
static void FieldRows(float x, float *y, float w, PxtfTypeface *tf, PxtfVariant *cv, const PxtfText *keys, int n, bool en, bool all)
{
    for (int k = 0; k < n; k++) {
        PxtfText t = keys[k];
        char low[40], nm[48];
        LowerKey(pxtf_text_key(t), low, sizeof low);
        snprintf(nm, sizeof nm, "c.%s", low);
        Rectangle r = Th_Row(x, y, w, pxtf_text_label(t), NULL, 0);
        if (Th_TextField(r, nm, cv->text[t] ? cv->text[t] : "", 0, TH_TF_REVERT | (en ? 0 : TH_TF_READONLY), NULL) & TH_TF_CHANGED) {
            if (all) for (int i = 0; i < tf->count; i++) pxtf_set_text(&tf->v[i], t, Th_TextValue());
            else pxtf_set_text(cv, t, Th_TextValue());
            needHash = true;
        }
    }
}

/* The Typeface tab (`typeface`) or the Font tab, in the library views and in the font view: the padlock, the hash, the commands and the
 * fields.  `doc`: it is the open document's (its fields are edited in `cont` while unlocked); otherwise tf is the library's copy and read-only.
 * The typeface lock governs the typeface-wide fields, the font lock the font's own (style, names, slant, the three flags). */
static void DrawInfoTab(bool typeface, float px, float pw, float *yp, const PxtfTypeface *tf, const char *pxtf, int variant, const char *pxfm, bool doc)
{
    float x = px + 16, w = pw - 32, y = *yp;
    bool svBlocked = uiBlocked;
    uiBlocked = Th_InputBlocked();
    if (doc) { EnsureHashes(); tf = hasCont ? &cont : NULL; variant = contVariant; }
    PxtfVariant *cv = tf ? pxtf_find_variant((PxtfTypeface *)tf, variant) : NULL;
    const char *what = typeface ? "typeface" : "font";
    bool locked = doc ? (typeface ? contLocked : readOnly) : LockedOf(typeface, typeface ? pxtf : pxfm);
    if (doc && !docStored) {
        Th_Text("New font, not in the database yet", x, y + 3, TH_TEXT);
        y += TH_ROW_H + TH_GAP;
    } else {
        Rectangle lr = {x, y, TH_ROW_H, TH_ROW_H};
        if (typeface) Th_Reg("tf_lock", lr);
        if (Th_Lock(lr, locked, TextFormat(locked ? "Unlock the %s: edits change it in place" : "Lock the %s: nothing can change it", what))) LibCmd(LC_LOCK, typeface, pxtf, pxfm, variant);
        Th_Text(locked ? "Locked" : "Unlocked: edits change it in place", x + TH_ROW_H + TH_GAP, y + 3, locked ? TH_TEXT_WARN : TH_TEXT);
        y += TH_ROW_H + TH_GAP;
    }
    /* the hash, the current one while the open document differs from the database */
    const char *cur = typeface ? (doc ? curContHash : pxtf) : (doc && !readOnly ? curHash : pxfm), *orig = typeface ? (doc ? origContHash : pxtf) : (doc ? origHash : pxfm);
    bool differs = doc && docStored && !Same(cur, orig);
    bool stored = !doc || docStored;
    if (typeface) {
        snprintf(tabHash, sizeof tabHash, "%s", cur);
        /* META: the typeface's fields; ACTIONS under it: the hash and the commands */
        if (tf && Th_Section("META", x, &y, w, TextFormat("%d variant%s", tf->count, tf->count == 1 ? "" : "s"))) {
            /* the typeface's NAME: edited in the open document's typeface, else (unlocked) rewritten in the library at once */
            bool enName = doc ? !contLocked : stored && !locked;
            Rectangle r = Th_Row(x, &y, w, "Name", NULL, 0);
            if (Th_TextField(r, "c.typeface", tf->name ? tf->name : "", 0, TH_TF_REVERT | (enName ? 0 : TH_TF_READONLY), NULL) & TH_TF_CHANGED) {
                if (doc) { pxtf_set_name((PxtfTypeface *)tf, Th_TextValue()); needHash = true; }
                else { LibCmd(LC_RENAME, true, pxtf, pxfm, variant); snprintf(libCmd.text, sizeof libCmd.text, "%s", Th_TextValue()); }
            }
            if (cv) FieldRows(x, &y, w, (PxtfTypeface *)tf, cv, kTfFields, (int)(sizeof kTfFields / sizeof kTfFields[0]), doc && !contLocked, true);
            y += 4;
        }
        if (Th_Section("ACTIONS", x, &y, w, NULL)) {
            if (differs) { Th_TextSmall("Typeface hash, current", x, y, TH_TEXT_DIM); y += 20; }
            Th_HashField(px, &y, pw, NULL, cur, "hash_typeface_full", "Typeface", TH_HASH_NOCAP);
            if (differs) {
                Th_TextSmall("In the database", x, y, TH_TEXT_DIM);
                y += 20;
                Th_HashField(px, &y, pw, NULL, orig, "hash_typeface_orig", "Typeface", TH_HASH_NOCAP);
            }
            bool canAdd = stored && tf && !locked;
            {   /* Add Font: a row of its own */
                static const char *lab1[1] = {"Add Font\xE2\x80\xA6"};
                const int fl1[1] = {TH_BTN_OFF(canAdd)};
                const char *tip1[1] = {canAdd ? "A font of the library becomes a new variant of the typeface (it stays in its other typefaces too)" : "The typeface is locked: unlock it first"};
                if (ButtonRow(x, &y, w, 1, lab1, fl1, tip1) == 0) LibCmd(LC_ADDFONT, true, doc ? origContHash : pxtf, pxfm, variant);
            }
            static const char *lab[2] = {"Export Typeface\xE2\x80\xA6", "Delete Typeface"};
            const int fl[2] = {TH_BTN_OFF(stored && tf), TH_BTN_DANGER | TH_BTN_OFF(stored && tf)};
            static const char *tip[2] = {"Write the typeface into a .pxtp package, or its fonts into font files: BDF, PCF, FNT, FON, TrueType, OpenType, WOFF", "Delete the typeface from the database"};
            int c = ButtonRow(x, &y, w, 2, lab, fl, tip);
            if (c >= 0) LibCmd(c == 0 ? LC_EXPORT : LC_DELETE, true, pxtf, pxfm, variant);
        }
    } else {
        Th_HashField(px, &y, pw, differs ? "FONT HASH (CURRENT)" : "FONT HASH", cur, "hash_font_full", "Font", 0);
        if (differs) Th_HashField(px, &y, pw, "FONT HASH (IN THE DATABASE)", orig, "hash_font_orig", "Font", 0);
        static const char *lab[3] = {"Clone Font", "Export Font\xE2\x80\xA6", "Delete Font"};
        bool canClone = stored && cv && !LockedOf(true, doc ? origContHash : pxtf);
        const int fl[3] = {TH_BTN_OFF(canClone), TH_BTN_OFF(stored), TH_BTN_DANGER | TH_BTN_OFF(stored)};
        const char *tip[3] = {!cv ? "The font belongs to no typeface" : canClone ? "A new variant of the typeface with the same font, its style \"... copy\"" : "The typeface is locked: unlock it first",
                              "Write the font into a .pxfp package or a font file: BDF, PCF, FNT, FON, TrueType, OpenType, WOFF", "Delete the font from the database"};
        int c = ButtonRow(x, &y, w, 3, lab, fl, tip);
        if (c >= 0) LibCmd(c == 0 ? LC_CLONE : c == 1 ? LC_EXPORT : LC_DELETE, false, doc ? origContHash : pxtf, doc ? origHash : pxfm, variant);
    }
    y += 4;
    /* the fields of the font */
    if (!typeface && cv && Th_Section("FONT", x, &y, w, TextFormat("variant %04d", cv->number))) {
        bool en = doc && !readOnly;
        FieldRows(x, &y, w, (PxtfTypeface *)tf, cv, kFontFields, (int)(sizeof kFontFields / sizeof kFontFields[0]), en, false);
        struct { const char *label, *name; bool *flag; } sw[3] = {
            {"Italic", "c.italic", &cv->italic}, {"Underlined", "c.underlined", &cv->underlined}, {"Struck-out", "c.struckout", &cv->struckout}};
        for (int i = 0; i < 3; i++) {
            bool v = *sw[i].flag;
            if (Th_Switch((Rectangle){x, y, w, TH_ROW_H}, sw[i].label, &v, TH_CK_RIGHT | (en ? 0 : TH_CK_OFF), sw[i].name)) { *sw[i].flag = v; needHash = true; }
            y += TH_ROW_H + 2;
        }
        y += 4;
    }
    *yp = y;
    uiBlocked = svBlocked;
}

/* the browser's Typeface and Font tabs: the selected entry is the open document's when its hashes are the open ones */
static void DrawLibraryTab(bool typeface, float px, float pw, float *yp, const PxtfTypeface *tf, const char *pxtf, int variant, const char *pxfm)
{
    bool doc = hasFont && docStored && (typeface ? hasCont && Same(pxtf, origContHash) : Same(pxfm, origHash) && (!hasCont || Same(pxtf, origContHash)));
    DrawInfoTab(typeface, px, pw, yp, tf, pxtf, variant, pxfm, doc);
}

/* The typeface of the open font is the one with this pxtf hash and it is unlocked (the navigation bar names the copy being edited). */
static bool TypefaceEditing(const char *pxtf)
{
    return hasFont && docStored && !contLocked && hasCont && pxtf && origContHash[0] && Same(origContHash, pxtf);
}

/* ---- editing the facts of a glyph (the glyph view's Glyph tab) ---- */

/* one character (UTF-8), or the word "space": its code point */
static bool ParseCharCode(const char *in, int32_t *cp)
{
    if (!strcmp(in, " ") || !strcmp(in, "space")) { *cp = 0x20; return true; }
    char s[40];
    snprintf(s, sizeof s, "%s", in);
    char *p = s;
    while (*p == ' ') p++;
    size_t n = strlen(p);
    while (n > 0 && p[n - 1] == ' ') p[--n] = 0;
    if (n == 0) return false;
    int sz = 0;
    int c = GetCodepointNext(p, &sz);
    if ((size_t)sz != n || (c == 0x3F && p[0] != '?')) return false;
    *cp = c;
    return true;
}

/* Gives glyph gi (not the default glyph) another code: a code point or a variant of one (U+0041-001).  A code another glyph has, the default glyph's
 * codes and a symbol's (#n) are refused with a message, and so is a symbol that has variants (they would lose their glyph 0).  The glyph keeps its
 * bitmap, offsets and advance and moves to its place in the order; its kerning pairs follow the new code.  Returns the glyph's index afterwards (gi when
 * nothing changed). */
static int SetGlyphCode(int gi, PxCode cp)
{
    if (readOnly || gi < 0 || gi >= font.count) return gi;
    PxCode oc = font.glyphs[gi].code;
    if (cp == oc) return gi;
    char cl[40];
    px_code_label(cp, cl, sizeof cl);
    if (oc == PX_CODE_DEFAULT) { Th_Toast(TH_TOAST_ERR, "The default glyph keeps its code"); return gi; }
    if (px_code_is_default(cp)) { Th_Toast(TH_TOAST_ERR, "%s is a code of the default glyph: use Set Default", cl); return gi; }
    if (!px_code_is_codepoint(cp)) { Th_Toast(TH_TOAST_ERR, "%s is no code point: type U+XXXX, or U+XXXX-V for a variant", cl); return gi; }
    if (px_code_is_uncoded(oc) && px_code_variant(oc) == 0 && gi + 1 < font.count && px_code_base(font.glyphs[gi + 1].code) == oc) {
        Th_Toast(TH_TOAST_ERR, "This glyph has variants, which need it: re-code or delete them first"); return gi;
    }
    if (px_font_find_code(&font, cp) >= 0) { Th_Toast(TH_TOAST_ERR, "%s belongs to another glyph of this font: re-code or delete that one first", cl); return gi; }
    PxGlyph t;
    if (!px_glyph_copy(&t, &font.glyphs[gi]) && font.glyphs[gi].pix) return gi;
    PushUndo(0);
    PxKern *kept = (PxKern *)malloc(sizeof(PxKern) * (size_t)(font.kernCount > 0 ? font.kernCount : 1));
    int nk = 0;
    for (int i = 0; kept && i < font.kernCount; i++)
        if (font.kern[i].first == oc || font.kern[i].second == oc) kept[nk++] = font.kern[i];
    px_font_remove_glyph(&font, gi);   /* drops the pairs of the old code, and renumbers the later symbols when it was one */
    t.code = cp;
    int ni = px_font_add_glyph(&font, &t);
    px_glyph_free(&t);
    for (int i = 0; i < nk; i++) {   /* the pairs follow the glyph (the other side of a pair may have been renumbered) */
        PxCode a = kept[i].first, b = kept[i].second;
        bool ao = a == oc, bo = b == oc;   /* decided before the renumbering: a later symbol takes over the number oc had */
        if (px_code_is_uncoded(oc) && px_code_variant(oc) == 0) {
            const PxCode step = (PxCode)1 << PX_CODE_VARIANT_BITS;
            if (px_code_is_uncoded(a) && a > oc) a -= step;
            if (px_code_is_uncoded(b) && b > oc) b -= step;
        }
        px_font_set_kern(&font, ao ? cp : a, bo ? cp : b, kept[i].value);
    }
    free(kept);
    SelResize();
    SelOnly(ni);
    revealIdx = ni;
    if (inEditor && ed) Editor_SetGlyphIndex(ed, ni);   /* the same glyph, now at another place */
    needHash = true;
    Th_Toast(TH_TOAST_OK, "The glyph is now %s%s", cl, nk ? " (its kerning pairs came along)" : "");
    return ni;
}

/* The facts of glyph gi as label/value rows (the Glyph tab of the glyph view, the selection of the font view).  `editable`: the code point, the
 * character, the advance and the offsets are fields (dim and inert while the font is locked); the font view's selection shows them as text. */
static void GlyphFacts(float px, float *y, int gi, bool character, bool editable)
{
    float x = px + 16, w = PANEL_W - 32;
    const PxGlyph *g = &font.glyphs[gi];
    char cl[64], ch[16] = "";
    CodeLabel(g->code, cl, sizeof cl);
    int32_t gcp = px_code_codepoint(g->code);   /* a variant shows the character of its code point too */
    if (gcp >= 0) {
        int len = 0;
        const char *u = CodepointToUTF8(gcp, &len);
        bool shown = gcp >= 0x21 && gcp != 0x7F && gcp != 0xAD && gcp <= 0x17F && !(gcp >= 0x80 && gcp < 0xA1);
        if (shown) snprintf(ch, sizeof ch, "%.*s", len, u);
        else if (gcp == 0x20) snprintf(ch, sizeof ch, "space");
    }
    const char *codeLabel = px_code_is_uncoded(g->code) ? "No code point" : px_code_is_default(g->code) ? "Glyph" : "Code point";
    if (!editable) {
        Th_Row(x, y, w, codeLabel, cl, TH_ROW_UNI);
        if (character) Th_Row(x, y, w, "Character", ch[0] ? ch : "—", 0);
        Th_Row(x, y, w, "Advance", TextFormat("%d", g->advance), 0);
        Th_Row(x, y, w, "Bitmap", g->w > 0 ? TextFormat("%d x %d", g->w, g->h) : "empty", 0);
        Th_Row(x, y, w, "Offset x, y", TextFormat("%d, %d", g->x, g->y), 0);
        return;
    }
    bool en = !readOnly;
    int f;
    Rectangle r = Th_Row(x, y, w, codeLabel, NULL, 0);
    f = Th_TextField(r, "g.code", cl, 40, TH_TF_COMMIT | TH_TF_SELECT | (en && g->code != PX_CODE_DEFAULT ? 0 : TH_TF_DISABLED), NULL);
    if (f & (TH_TF_ENTER | TH_TF_BLUR)) {
        PxCode cp;
        if (!strcmp(Th_TextValue(), cl)) { /* untouched */ }
        else if (!px_code_parse_user(Th_TextValue(), &cp)) Th_Toast(TH_TOAST_ERR, "Type a code point as hex digits, for example U+00E9, or U+00E9-1 for a variant");
        else gi = SetGlyphCode(gi, cp);
    }
    g = &font.glyphs[gi];
    if (character) {
        r = Th_Row(x, y, w, "Character", NULL, 0);
        f = Th_TextField(r, "g.char", ch, 16, TH_TF_COMMIT | TH_TF_SELECT | (en && g->code != PX_CODE_DEFAULT ? 0 : TH_TF_DISABLED), "none");
        if (f & (TH_TF_ENTER | TH_TF_BLUR)) {
            int32_t cp;
            if (!strcmp(Th_TextValue(), ch)) { /* untouched */ }
            else if (!ParseCharCode(Th_TextValue(), &cp)) Th_Toast(TH_TOAST_ERR, "Type one character");
            else gi = SetGlyphCode(gi, px_code_with_variant(px_code_cp(cp), px_code_is_codepoint(g->code) ? px_code_variant(g->code) : 0));   /* a variant stays one */
        }
        g = &font.glyphs[gi];
    }
    int nv;
    r = Th_Row(x, y, w, "Advance", NULL, 0);
    if (Th_Number((Rectangle){r.x + r.width - 100, r.y, 100, TH_ROW_H}, "g.advance", g->advance, 0, 99999, TH_NUM_WHEEL | (en ? 0 : TH_TF_DISABLED), &nv)) {
        PushUndo(1000 + gi * 4);
        font.glyphs[gi].advance = nv;
    }
    Th_Row(x, y, w, "Bitmap", g->w > 0 ? TextFormat("%d x %d", g->w, g->h) : "empty", 0);
    r = Th_Row(x, y, w, "Offset x, y", NULL, 0);
    bool eo = en && g->w > 0;   /* a glyph without ink has no offset */
    if (Th_Number((Rectangle){r.x + r.width - 212, r.y, 100, TH_ROW_H}, "g.offx", g->x, -9999, 9999, TH_NUM_WHEEL | (eo ? 0 : TH_TF_DISABLED), &nv)) {
        PushUndo(1001 + gi * 4);
        font.glyphs[gi].x = nv;
    }
    if (Th_Number((Rectangle){r.x + r.width - 100, r.y, 100, TH_ROW_H}, "g.offy", g->y, -9999, 9999, TH_NUM_WHEEL | (eo ? 0 : TH_TF_DISABLED), &nv)) {
        PushUndo(1002 + gi * 4);
        font.glyphs[gi].y = nv;
    }
}

/* The selection of the font view's Glyph tab: the facts of one glyph (with its character), the way into the glyph view, the glyph
 * commands and its hash; a count otherwise. */
static void DrawSelectionSection(float px, float *yp, PxCode insCode)
{
    float x = px + 16, w = PANEL_W - 32, y = *yp;
    int fi = FirstSelected();
    if (selCount == 1) {
        GlyphFacts(px, &y, fi, true, false);
        y += 4;
        if (Th_Button((Rectangle){x, y, w, TH_ROW_H}, "Open in Glyph View", 0, "Double click or Enter do the same")) CmdEditSelected();
        y += TH_ROW_H + TH_GAP;
    } else {
        Th_Text(selCount ? TextFormat("%d glyphs selected", selCount) : "Nothing selected", x, y, TH_TEXT_DIM);
        y += TH_ROW_H;
    }
    const char *lock = readOnly ? "The font is locked: unlock it first" : NULL;
    bool can = insCode >= 0 && finder;
    {
        static const char *lab[2] = {"Select Occurrences", "Find in Library"};
        const int fl[2] = {TH_BTN_OFF(selCount > 0), TH_BTN_OFF(can)};
        static const char *tip[2] = {"Select every glyph with the same shape as the selected ones  (Ctrl+Shift+A)", "Find every bitmap the database draws for this code point  (Ctrl+Shift+F)"};
        int c = ButtonRow(x, &y, w, 2, lab, fl, tip);
        if (c == 0) CmdSelectOccurrences();
        else if (c == 1) { memset(&panelTarget, 0, sizeof panelTarget); panelTarget.action = INSIGHT_FIND_CODE; panelTarget.code = insCode; panelTargetSet = true; }
    }
    {
        static const char *lab[4] = {"Add Glyph", "Clone", "Set Default", "Delete"};
        const int fl[4] = {TH_BTN_OFF(!readOnly), TH_BTN_OFF(!readOnly && selCount == 1), TH_BTN_OFF(CanSetDefault()), TH_BTN_DANGER | TH_BTN_OFF(!readOnly && selCount > 0)};
        const char *tip[4] = {lock ? lock : "Add an empty glyph: a code point, or one without", lock ? lock : "Clone the selected glyph to a new code point, or to a glyph without one  (Ctrl+D)",
                              lock ? lock : "Make the selected glyph the font's default glyph", lock ? lock : "Delete the selected glyphs  (Del)"};
        int c = ButtonRow(x, &y, w, 4, lab, fl, tip);
        if (c == 0) CmdAddGlyph();
        else if (c == 1) CmdClone();
        else if (c == 2) CmdSetDefault();
        else if (c == 3) CmdDeleteAsk();
    }
    {
        static const char *lab[1] = {"Auto Spacing"};
        const int fl[1] = {TH_BTN_OFF(!readOnly && selCount > 0)};
        const char *tip[1] = {lock ? lock : "The selected glyphs: x offset 0 and the advance one pixel past the ink (glyphs without ink stay)"};
        if (ButtonRow(x, &y, w, 1, lab, fl, tip) == 0) CmdAutoSpacing();
    }
    if (selCount == 1) {
        char h[33];
        px_glyph_hash(&font.glyphs[fi], h);
        Th_HashField(px, &y, PANEL_W, "GLYPH HASH", h, "hash_glyph", "Glyph", 0);
    }
    *yp = y;
}

/* The View tab of the font view: the zoom of the grid, the metric lines and the captions of the cells. */
static void DrawFontView(float px, float *yp)
{
    float x = px + 16, w = PANEL_W - 32, y = *yp;
    {
        char zt[8];
        snprintf(zt, sizeof zt, "%dx", zoom);
        Rectangle zr = Th_Row(x, &y, w, "Zoom", NULL, 0);
        int zs = Th_Stepper((Rectangle){zr.x, zr.y, TH_STEP_W, TH_ROW_H}, zt, "zoom", zoom <= 1 ? TH_STEP_NO_MINUS : 0);
        if (Th_StepperHover()) hintText = Th_StepperHover() < 0 ? "Zoom out  (-)" : "Zoom in  (+)";
        if (zs) ApplyZoom(zoom + zs);
        Th_Switch((Rectangle){x, y, w, TH_ROW_H}, "Metrics", &showMetrics, TH_CK_RIGHT, "Metrics");
        y += TH_ROW_H + 2;
        Th_Switch((Rectangle){x, y, w, TH_ROW_H}, "Captions", &showCaptions, TH_CK_RIGHT, "Captions");
        y += TH_ROW_H + 2;
    }
    *yp = y;
}

/* The font metrics of the Font tab. */
static void DrawMetricsSection(float px, float *yp)
{
    float y = *yp;
    if (!Th_Section("FONT METRICS", px + 16, &y, PANEL_W - 32, NULL)) { *yp = y; return; }
    static const struct { PxMetric m; Color c; } rows[] = {
        {PXM_ASCENT, TH_M_BOX}, {PXM_DESCENT, TH_M_BOX}, {PXM_LINEGAP, TH_TEXT_DIM}, {PXM_CAPHEIGHT, TH_M_CAP},
        {PXM_XHEIGHT, TH_M_XHEIGHT}, {PXM_INTERNALLEADING, TH_TEXT_DIM}, {PXM_QUADWIDTH, TH_M_ADVANCE},
        {PXM_UNDERLINEPOSITION, TH_M_UNDERLINE}, {PXM_UNDERLINETHICKNESS, TH_M_UNDERLINE},
        {PXM_STRIKEOUTPOSITION, TH_M_UNDERLINE}, {PXM_STRIKEOUTTHICKNESS, TH_M_UNDERLINE}};
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        MetricRow(px, y, rows[i].m, rows[i].c);
        y += TH_ROW_H + 4;
        if (rows[i].m == PXM_DESCENT) { BaselineRow(px, y); y += TH_ROW_H + 4; }
    }
    RatioRow(px, y);
    y += TH_ROW_H + 4;
    struct { const char *label, *name; bool *v; } sw[2] = {{"Monospaced", "monospaced", &font.monospaced}, {"Symbolic", "symbolic", &font.symbolic}};
    for (int i = 0; i < 2; i++) {   /* SYMBOLIC: the codes are not the characters' code points (a symbol or dingbat font) */
        bool v = *sw[i].v;
        if (Th_Switch((Rectangle){px + 54, y, PANEL_W - 54 - 16, TH_ROW_H}, sw[i].label, &v, TH_CK_RIGHT | (readOnly ? TH_CK_OFF : 0), sw[i].name)) { PushUndo(0); *sw[i].v = v; }
        y += TH_ROW_H + 4;
    }
    int a, b;
    px_font_box(&font, &a, &b);
    Th_TextSmall(TextFormat("Font box %d above, %d below the baseline%s", a, b, (font.has[PXM_ASCENT] && font.has[PXM_DESCENT]) ? "" : " (from the glyphs)"), px + 16, y + 2, TH_TEXT_DIM);
    *yp = y + 28;
}

/* The tallest a panel's View area gets (more scrolls inside it): half the panel. */
static float ViewAreaMaxH(Rectangle p) { return (p.height - TH_VIEW_HDR) / 2; }

/* The tab control of a panel: `labels` shown in order, `ids` the tab number each stands for; returns the id picked (-1: none). */
static int PanelTabs(Rectangle p, int n, const char *const *labels, const int *ids, int cur, const char *name)
{
    int at = 0;
    for (int i = 0; i < n; i++) if (ids[i] == cur) at = i;
    int c = Th_Segmented((Rectangle){p.x + 8, p.y + 8, p.width - 16, TH_ROW_H}, &(ThSeg){ (const char **)labels, n, at, 0u, name });
    Th_HLine(p.x + 1, p.x + p.width, p.y + PANEL_TABS_H - 1, TH_LINE);
    return c >= 0 ? ids[c] : -1;
}

/* The insight sections of a panel (usage of the selected glyphs, similar fonts): what is left of the visible content, at least 200 px. */
static float PanelInsight(float px, float y, Rectangle cr, int mask)
{
    bool prevB = Th_MouseOnly();
    if (!CheckCollisionPointRec(Mouse(), cr)) Th_SetMouseBlocked(true);   /* an entry scrolled out of the panel must not react */
    bool clicked = false;
    InsightTarget tg;
    memset(&tg, 0, sizeof tg);
    if (mask) Insight_SetVisible(insight, cr);
    float used = Insight_Draw(insight, (Rectangle){px, y, PANEL_W, mask ? (cr.y + cr.height - y > 200 ? cr.y + cr.height - y : 200) : 0}, mask, &clicked, &tg);
    Th_SetMouseBlocked(prevB);
    if (clicked && tg.action != INSIGHT_NONE) { panelTarget = tg; panelTargetSet = true; }
    return mask ? used : 0;
}

static void DrawMetricsPanel(Rectangle p)
{
    if (inEditor) { DrawGlyphPanel(p); return; }   /* the glyph view has its own panel (gpanel.inc) */
    DrawRectangleRec(p, TH_PANEL);
    Th_VLine(p.x, p.y, p.y + p.height, TH_LINE);
    {   /* the view area above the tabs: zoom, Metrics, Captions */
        float vy;
        if (Th_ViewAreaBegin(&fontView, p, ViewAreaMaxH(p), &vy)) { DrawFontView(p.x, &vy); Th_ViewAreaEnd(&fontView, vy); }
        if (fontView.toggled) IniSetApp("viewFont", fontView.open ? "1" : "0");
        float b = Th_ViewAreaBottom(&fontView);
        p.height -= b - p.y;
        p.y = b;
    }
    static const char *labels[4] = {"Font", "Glyph", "Similar", "Layers"};
    static const int ids[4] = {0, 1, 2, 3};
    int tab = panelTab, c = PanelTabs(p, 4, labels, ids, tab, "ptab");
    if (c >= 0) { PanelSetTab(c); tab = c; }
    Rectangle cr = {p.x, p.y + PANEL_TABS_H, p.width, p.height - PANEL_TABS_H};   /* the scrolling content */
    float maxS0 = panelContentH - cr.height;
    if (panelScroll > maxS0) panelScroll = maxS0 > 0 ? maxS0 : 0;
    if (panelScroll < 0) panelScroll = 0;

    /* what the Insight queries are about: the selected glyphs (usage), the code of a single one, the font (similar) */
    enum { INS_MAX_SEL = 32 };
    static char insH[INS_MAX_SEL][33];
    int nIns = 0;
    PxCode insCode = PX_NO_CODE;
    for (int i = 0; i < font.count && i < selN && selCount >= 1 && selCount <= INS_MAX_SEL && nIns < INS_MAX_SEL; i++) {
        if (!sel[i]) continue;
        char h[33];
        px_glyph_hash(&font.glyphs[i], h);      /* "" = no ink: no bitmap to look for */
        bool dup = !h[0];
        for (int j = 0; j < nIns && !dup; j++) if (Same(insH[j], h)) dup = true;
        if (!dup) memcpy(insH[nIns++], h, 33);
        if (selCount == 1 && px_code_is_codepoint(font.glyphs[i].code)) insCode = font.glyphs[i].code;
    }
    if (insight) {
        Insight_SetGlyphs(insight, (const char (*)[33])insH, nIns);
        Insight_SetPreferredCode(insight, insCode);
        int fi = FirstSelected();
        Insight_SetPalette(insight, selCount == 1 && fi >= 0 ? &font : NULL, selCount == 1 ? fi : -1);
    }

    hovClip = cr;
    hovClipOn = true;
    BeginScissorMode((int)cr.x + 1, (int)cr.y, (int)cr.width - 1, (int)cr.height);
    float px = p.x;
    float y = cr.y + 14 - panelScroll;
    int mask = 0;
    if (tab == 0) {
        DrawInfoTab(false, px, PANEL_W, &y, NULL, origContHash, contVariant, origHash, true);
        DrawMetricsSection(px, &y);
    } else if (tab == 1) {
        if (Th_Section("SELECTION", px + 16, &y, PANEL_W - 32, NULL)) DrawSelectionSection(px, &y, insCode);
        mask = INSIGHT_SECTION_USAGE;
    } else if (tab == 3) DrawFontLayersTab(px, &y);   /* fitdoc.inc: the font layers and the glyphs drawn from one of them */
    else if (tab == 2) {
        static char hasKey[2][33];
        static bool hasVal[2];
        if (!Same(hasKey[0], origHash) || !hasKey[0][0]) { snprintf(hasKey[0], 33, "%s", origHash); hasVal[0] = origHash[0] && store_has_pxfm(store_main_conn(store), origHash); }
        if (!Same(hasKey[1], origContHash) || !hasKey[1][0]) { snprintf(hasKey[1], 33, "%s", origContHash); hasVal[1] = origContHash[0] && store_has_pxtf(store_main_conn(store), origContHash); }
        if (insight && hasVal[0]) {
            Insight_SetFont(insight, origHash, hasVal[1] ? origContHash : "");
            mask = INSIGHT_SECTION_SIMILAR_FONTS | (hasVal[1] ? INSIGHT_SECTION_SIMILAR_TYPEFACES : 0);
        } else {
            Th_Text("Similar fonts need a font of the library.", px + 16, y, TH_TEXT_DIM);
            y += TH_ROW_H;
        }
    }
    if (insight) y += PanelInsight(px, y, cr, mask);   /* nothing wanted (mask 0): the queries stop */

    panelContentH = y + panelScroll - cr.y + 16;
    EndScissorMode();
    hovClipOn = false;
    Th_ScrollBar(cr, &panelScroll, panelContentH, 48, true);
}

/* The text of a variant for a font file (fontwrite.h); the strings stay the typeface's. */
static FwMeta FwMetaOf(const PxtfTypeface *tf, const PxtfVariant *v)
{
    FwMeta m;
    memset(&m, 0, sizeof m);
    m.family = tf ? tf->name : NULL;
    if (!v) return m;
    const char *const *t = (const char *const *)v->text;
    m.style = t[PXTF_STYLE]; m.fullName = t[PXTF_NAME]; m.weightName = t[PXTF_WEIGHTNAME]; m.version = t[PXTF_VERSION];
    m.copyright = t[PXTF_COPYRIGHT]; m.trademark = t[PXTF_TRADEMARK]; m.description = t[PXTF_DESCRIPTION]; m.designer = t[PXTF_DESIGNER];
    m.designerUrl = t[PXTF_DESIGNERURL]; m.vendor = t[PXTF_VENDOR]; m.vendorUrl = t[PXTF_VENDORURL]; m.license = t[PXTF_LICENSE];
    m.licenseUrl = t[PXTF_LICENSEURL]; m.codepage = t[PXTF_CODEPAGE];
    m.italic = v->italic; m.underlined = v->underlined; m.struckout = v->struckout;
    return m;
}

/* Export in a font format (fontwrite.h): the typeface's fonts of one colour, a file each (`<stem>-<Style>.<ext>` beside the chosen path when
 * there are several), or all in one FON; or the one font of Export Font... (with the text of its variant when it has one).  Colour fonts are
 * left out with a word in the toast; what a format could not hold is told in a dialog. */
static void ExportFontFile(bool typeface, const char *pxtfHash, const char *pxfmHash, int variant, const char *path, FwFormat fmt)
{
    StoreConn *c = store_main_conn(store);
    char err[PX_ERR_LEN] = "", warn[4096] = "";
    PxtfTypeface tf;
    pxtf_init(&tf);
    bool haveTf = pxtfHash && pxtfHash[0] && store_load_typeface(c, pxtfHash, &tf, err);
    if (typeface && !haveTf) { ShowError("Cannot export the typeface", err[0] ? err : "The typeface could not be read."); return; }
    int nv = typeface ? tf.count : 1;
    PxFont *fonts = calloc((size_t)(nv ? nv : 1), sizeof *fonts);
    FwFace *faces = calloc((size_t)(nv ? nv : 1), sizeof *faces);
    int *num = calloc((size_t)(nv ? nv : 1), sizeof *num);   /* the variant number of each face */
    int nf = 0, colour = 0;
    bool ok = fonts && faces && num;
    for (int i = 0; ok && i < nv; i++) {
        const PxtfVariant *v = typeface ? &tf.v[i] : haveTf ? pxtf_find_variant(&tf, variant) : NULL;
        const char *fm = typeface ? v->pxfm : pxfmHash;
        px_font_init(&fonts[nf]);
        if (!store_load_font(c, fm, NULL, 0, &fonts[nf], err)) { ok = false; break; }
        if (!fw_takes_colour(fmt) && px_font_is_colour(&fonts[nf])) { px_font_free(&fonts[nf]); colour++; continue; }
        faces[nf].font = &fonts[nf];
        faces[nf].meta = FwMetaOf(haveTf ? &tf : NULL, v);
        num[nf] = v ? v->number : 1;
        nf++;
    }
    char dir[PATH_MAX_LEN], stem[PATH_MAX_LEN], written[2 * PATH_MAX_LEN + 96] = "";
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    const char *base = slash ? slash + 1 : dir;
    snprintf(stem, sizeof stem, "%s", base);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) *dot = 0;
    if (slash) slash[1] = 0; else dir[0] = 0;
    int files = 0;
    if (!ok) ShowError(typeface ? "Cannot export the typeface" : "Cannot export the font", err[0] ? err : "Out of memory.");
    else if (!nv) ShowError("Cannot export the typeface", "The typeface has no font.");
    else if (!nf) ShowError(typeface ? "Cannot export the typeface" : "Cannot export the font",
                            TextFormat("%s holds fonts of one colour only, and %s.", fw_label(fmt), typeface ? "every font of the typeface has colours" : "the font has colours"));
    else if (fw_one_file(fmt) || nf == 1) {
        if (fw_write(fmt, faces, fw_one_file(fmt) ? nf : 1, path, warn, sizeof warn, err)) { files = 1; snprintf(written, sizeof written, "%s", path); }
        else ShowError("Cannot export", err);
    } else {
        for (int i = 0; i < nf; i++) {   /* <stem>-<Style>.<ext>, the variant's number when the style is empty or taken */
            char st[80], p[2 * PATH_MAX_LEN + 96], w1[1024] = "";
            fw_ascii_name(faces[i].meta.style, st, sizeof st, false);
            bool taken = !st[0];
            for (int k = 0; k < i && !taken; k++) { char o[80]; fw_ascii_name(faces[k].meta.style, o, sizeof o, false); taken = !strcmp(o, st); }
            if (taken) { size_t l = strlen(st); snprintf(st + l, sizeof st - l, "%s%04d", l ? "-" : "", num[i]); }
            snprintf(p, sizeof p, "%s%s-%s.%s", dir, stem, st, fw_ext(fmt));
            if (!fw_write(fmt, &faces[i], 1, p, w1, sizeof w1, err)) { ShowError("Cannot export", TextFormat("%s: %s", p, err)); break; }
            for (char *s = w1, *e; *s && (e = strchr(s, '\n')); s = e + 1) { *e = 0; fw_warn(warn, sizeof warn, "%s: %s", st, s); }
            files++;
            snprintf(written, sizeof written, "%s", p);
        }
    }
    if (files) {
        if (files == 1) Th_Toast(TH_TOAST_OK, "%s written: %s", fw_label(fmt), written);
        else Th_Toast(TH_TOAST_OK, "%d %s files written in %s", files, fw_label(fmt), dir);
        if (colour) Th_Toast(TH_TOAST_INFO, "%d colour font%s left out: %s holds fonts of one colour only", colour, colour == 1 ? "" : "s", fw_label(fmt));
        if (warn[0]) ShowError(TextFormat("%s export: not everything fits", fw_label(fmt)), warn);
    }
    for (int i = 0; i < nf; i++) px_font_free(&fonts[i]);
    free(fonts);
    free(faces);
    free(num);
    pxtf_free(&tf);
}

/* Export Typeface... / Export Font...: a .pxtp or .pxfp package (pixelglyphlib spec, src/package.c), or a font file (ExportFontFile), written
 * where the user picks; the dialog's file type (the extension) chooses.  The open document's edits go into the store first, so the file holds
 * what the user sees. */
static void ExportPackage(bool typeface, const char *pxtfIn, const char *pxfmIn, int variant)
{
    if (!store) return;
    char h[33], tfh[33], name[PATH_MAX_LEN], path[PATH_MAX_LEN], err[PX_ERR_LEN] = "";
    bool doc = typeface ? hasCont && docStored && Same(pxtfIn, origContHash) : hasFont && docStored && Same(pxfmIn, origHash);
    bool docTf = hasCont && docStored && Same(pxtfIn, origContHash);
    snprintf(h, sizeof h, "%s", typeface ? pxtfIn : pxfmIn);
    snprintf(tfh, sizeof tfh, "%s", pxtfIn ? pxtfIn : "");
    if (!CommitEdits()) return;
    if (doc) snprintf(h, sizeof h, "%s", typeface ? origContHash : origHash);
    if (docTf) snprintf(tfh, sizeof tfh, "%s", origContHash);
    StoreConn *c = store_main_conn(store);
    if (typeface) pkg_default_name_typeface(c, h, name, sizeof name); else pkg_default_name_font(c, h, name, sizeof name);
    char *dot = strrchr(name, '.');
    if (dot && dot != name) *dot = 0;   /* the extension comes from the file type chosen in the dialog */
    if (dlgSavePath[0]) snprintf(path, sizeof path, "%s", dlgSavePath);
    else if (noDialogs || !Win_SaveFileDialog(path, (int)sizeof path,
                                              TextFormat("%s|BDF (*.bdf)|*.bdf|PCF (*.pcf)|*.pcf|Windows font, one file per font (*.fnt)|*.fnt|"
                                                         "Windows font resource, %s (*.fon)|*.fon|TrueType (*.ttf)|*.ttf|OpenType, colour fonts with SVG glyphs (*.otf)|*.otf|WOFF (*.woff)|*.woff|All files|*.*",
                                                         typeface ? "Pixel typeface package (*.pxtp)|*.pxtp" : "Pixel font package (*.pxfp)|*.pxfp",
                                                         typeface ? "every font in one file" : "one font"),
                                              name, typeface ? "pxtp" : "pxfp")) return;
    Slashes(path);
    const char *ext = strrchr(path, '.');
    FwFormat fmt;
    if (ext && !strchr(ext, '/') && fw_format_of_ext(ext, &fmt)) { ExportFontFile(typeface, tfh, h, variant, path, fmt); return; }
    if (typeface ? pkg_write_typeface(c, h, path, err) : pkg_write_font(c, h, path, err)) Th_Toast(TH_TOAST_OK, "%s written: %s", typeface ? "Typeface package" : "Font package", path);
    else ShowError(typeface ? "Cannot export the typeface" : "Cannot export the font", err[0] ? err : "Unknown error.");
}
