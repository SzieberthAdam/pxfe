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
enum { LC_NONE, LC_LOCK, LC_DELETE, LC_CLONE, LC_EXPORT };
static struct { int cmd; bool typeface; char pxtf[33], pxfm[33]; int variant; } libCmd;

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
}

/* ---- the Typeface and Font tabs ---- */
static const PxtfText kTfFields[] = {PXTF_FAMILY, PXTF_VERSION, PXTF_DESIGNER, PXTF_DESIGNERURL, PXTF_COPYRIGHT, PXTF_TRADEMARK, PXTF_LICENSE,
                                     PXTF_LICENSEURL, PXTF_VENDOR, PXTF_VENDORURL, PXTF_DESCRIPTION, PXTF_CODEPAGE};
static const PxtfText kFontFields[] = {PXTF_STYLE, PXTF_FULLNAME, PXTF_WEIGHTNAME, PXTF_SLANT};
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
    if (typeface) snprintf(tabHash, sizeof tabHash, "%s", cur);
    Th_HashField(px, &y, pw, typeface ? (differs ? "TYPEFACE HASH (CURRENT)" : "TYPEFACE HASH") : (differs ? "FONT HASH (CURRENT)" : "FONT HASH"), cur,
                 typeface ? "hash_typeface_full" : "hash_font_full", typeface ? "Typeface" : "Font", 0);
    if (differs) Th_HashField(px, &y, pw, typeface ? "TYPEFACE HASH (IN THE DATABASE)" : "FONT HASH (IN THE DATABASE)", orig,
                              typeface ? "hash_typeface_orig" : "hash_font_orig", typeface ? "Typeface" : "Font", 0);
    /* the commands */
    bool stored = !doc || docStored;
    if (typeface) {
        static const char *lab[2] = {"Export Typeface\xE2\x80\xA6", "Delete Typeface"};
        const int fl[2] = {TH_BTN_OFF(stored && tf), TH_BTN_DANGER | TH_BTN_OFF(stored && tf)};
        static const char *tip[2] = {"Write the typeface and every font it names into a .pxtp package", "Delete the typeface from the database"};
        int c = ButtonRow(x, &y, w, 2, lab, fl, tip);
        if (c >= 0) LibCmd(c == 0 ? LC_EXPORT : LC_DELETE, true, pxtf, pxfm, variant);
    } else {
        static const char *lab[3] = {"Clone Font", "Export Font\xE2\x80\xA6", "Delete Font"};
        bool canClone = stored && cv && !LockedOf(true, doc ? origContHash : pxtf);
        const int fl[3] = {TH_BTN_OFF(canClone), TH_BTN_OFF(stored), TH_BTN_DANGER | TH_BTN_OFF(stored)};
        const char *tip[3] = {!cv ? "The font belongs to no typeface" : canClone ? "A new variant of the typeface with the same font, its style \"... copy\"" : "The typeface is locked: unlock it first",
                              "Write the font into a .pxfp package", "Delete the font from the database"};
        int c = ButtonRow(x, &y, w, 3, lab, fl, tip);
        if (c >= 0) LibCmd(c == 0 ? LC_CLONE : c == 1 ? LC_EXPORT : LC_DELETE, false, doc ? origContHash : pxtf, doc ? origHash : pxfm, variant);
    }
    y += 4;
    /* the fields */
    if (typeface && cv && Th_Section("TYPEFACE", x, &y, w, TextFormat("%d variant%s%s%s", tf->count, tf->count == 1 ? "" : "s", cv->format && cv->format[0] ? "  \xC2\xB7  " : "", cv->format ? cv->format : ""))) {
        FieldRows(x, &y, w, (PxtfTypeface *)tf, cv, kTfFields, (int)(sizeof kTfFields / sizeof kTfFields[0]), doc && !contLocked, true);
        y += 4;
    }
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

/* hex digits of a code, "U+" or "0x" before them allowed; up to FFFFFF (SetGlyphCode says what is wrong with it) */
static bool ParseHexCode(const char *in, int32_t *cp)
{
    while (*in == ' ') in++;
    if ((in[0] == 'U' || in[0] == 'u') && in[1] == '+') in += 2;
    else if (in[0] == '0' && (in[1] == 'x' || in[1] == 'X')) in += 2;
    long v = 0;
    int n = 0;
    for (; (in[n] >= '0' && in[n] <= '9') || ((in[n] | 32) >= 'a' && (in[n] | 32) <= 'f'); n++) v = v * 16 + (in[n] <= '9' ? in[n] - '0' : (in[n] | 32) - 'a' + 10);
    if (n == 0 || n > 6) return false;
    for (const char *q = in + n; *q; q++) if (*q != ' ') return false;
    *cp = (int32_t)v;
    return true;
}

/* one character (UTF-8), or the word "space" */
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

/* Gives glyph gi (not the default glyph) another code point.  A code another glyph has, the code of the default glyph and a number that is no code point
 * are refused with a message.  The glyph keeps its bitmap, offsets and advance and moves to its place in the order; its kerning pairs follow the new
 * code.  Returns the glyph's index afterwards (gi when nothing changed). */
static int SetGlyphCode(int gi, int32_t cp)
{
    if (readOnly || gi < 0 || gi >= font.count) return gi;
    int32_t oc = font.glyphs[gi].codepoint;
    if (cp == oc) return gi;
    if (oc == PX_CODE_DEFAULT) { Th_Toast(TH_TOAST_ERR, "The default glyph keeps its code"); return gi; }
    if (cp == PX_CODE_DEFAULT) { Th_Toast(TH_TOAST_ERR, "FFFFFF is the code of the default glyph: use Set Default"); return gi; }
    if (!px_code_is_codepoint(cp)) { Th_Toast(TH_TOAST_ERR, "%X is no code point (the largest is 10FFFF)", (unsigned)cp); return gi; }
    if (px_font_find(&font, cp) >= 0) { Th_Toast(TH_TOAST_ERR, "U+%04X belongs to another glyph of this font: re-code or delete that one first", (unsigned)cp); return gi; }
    PxGlyph t;
    if (!px_glyph_copy(&t, &font.glyphs[gi]) && font.glyphs[gi].pix) return gi;
    PushUndo(0);
    PxKern *kept = (PxKern *)malloc(sizeof(PxKern) * (size_t)(font.kernCount > 0 ? font.kernCount : 1));
    int nk = 0;
    for (int i = 0; kept && i < font.kernCount; i++)
        if (font.kern[i].first == oc || font.kern[i].second == oc) kept[nk++] = font.kern[i];
    px_font_remove_glyph(&font, gi);   /* drops the pairs of the old code, and renumbers the later uncoded glyphs when it was one */
    t.codepoint = cp;
    int ni = px_font_add_glyph(&font, &t);
    px_glyph_free(&t);
    for (int i = 0; i < nk; i++) {   /* the pairs follow the glyph (the other side of a pair may have been renumbered) */
        int32_t a = kept[i].first, b = kept[i].second;
        bool ao = a == oc, bo = b == oc;   /* decided before the renumbering: a later uncoded glyph takes over the number oc had */
        if (px_code_is_uncoded(oc)) {
            if (px_code_is_uncoded(a) && a > oc) a--;
            if (px_code_is_uncoded(b) && b > oc) b--;
        }
        px_font_set_kern(&font, ao ? cp : a, bo ? cp : b, kept[i].value);
    }
    free(kept);
    SelResize();
    SelOnly(ni);
    revealIdx = ni;
    if (inEditor && ed) Editor_SetGlyphIndex(ed, ni);   /* the same glyph, now at another place */
    needHash = true;
    Th_Toast(TH_TOAST_OK, "The glyph is now U+%04X%s", (unsigned)cp, nk ? " (its kerning pairs came along)" : "");
    return ni;
}

/* The facts of glyph gi as label/value rows (the Glyph tab of the glyph view, the selection of the font view).  `editable`: the code point, the
 * character, the advance and the offsets are fields (dim and inert while the font is locked); the font view's selection shows them as text. */
static void GlyphFacts(float px, float *y, int gi, bool character, bool editable)
{
    float x = px + 16, w = PANEL_W - 32;
    const PxGlyph *g = &font.glyphs[gi];
    char cl[64], ch[16] = "";
    CodeLabel(g->codepoint, cl, sizeof cl);
    if (px_code_is_codepoint(g->codepoint)) {
        int len = 0;
        const char *u = CodepointToUTF8(g->codepoint, &len);
        bool shown = g->codepoint >= 0x21 && g->codepoint != 0x7F && g->codepoint != 0xAD && g->codepoint <= 0x17F && !(g->codepoint >= 0x80 && g->codepoint < 0xA1);
        if (shown) snprintf(ch, sizeof ch, "%.*s", len, u);
        else if (g->codepoint == 0x20) snprintf(ch, sizeof ch, "space");
    }
    const char *codeLabel = px_code_is_uncoded(g->codepoint) ? "No code point" : g->codepoint == PX_CODE_DEFAULT ? "Glyph" : "Code point";
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
    f = Th_TextField(r, "g.code", cl, 40, TH_TF_COMMIT | TH_TF_SELECT | (en && g->codepoint != PX_CODE_DEFAULT ? 0 : TH_TF_DISABLED), NULL);
    if (f & (TH_TF_ENTER | TH_TF_BLUR)) {
        int32_t cp;
        if (!strcmp(Th_TextValue(), cl)) { /* untouched */ }
        else if (!ParseHexCode(Th_TextValue(), &cp)) Th_Toast(TH_TOAST_ERR, "Type a code point as hex digits, for example U+00E9");
        else gi = SetGlyphCode(gi, cp);
    }
    g = &font.glyphs[gi];
    if (character) {
        r = Th_Row(x, y, w, "Character", NULL, 0);
        f = Th_TextField(r, "g.char", ch, 16, TH_TF_COMMIT | TH_TF_SELECT | (en && g->codepoint != PX_CODE_DEFAULT ? 0 : TH_TF_DISABLED), "none");
        if (f & (TH_TF_ENTER | TH_TF_BLUR)) {
            int32_t cp;
            if (!strcmp(Th_TextValue(), ch)) { /* untouched */ }
            else if (!ParseCharCode(Th_TextValue(), &cp)) Th_Toast(TH_TOAST_ERR, "Type one character");
            else gi = SetGlyphCode(gi, cp);
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
static void DrawSelectionSection(float px, float *yp, int32_t insCode)
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
    if (selCount == 1) {
        char h[33];
        px_glyph_hash(&font.glyphs[fi], h);
        Th_HashField(px, &y, PANEL_W, "GLYPH HASH", h, "hash_glyph", "Glyph", 0);
    }
    *yp = y;
}

/* The View tab of the font view: the zoom of the grid, the metric lines and the captions of the cells. */
static void DrawFontViewTab(float px, float *yp)
{
    float x = px + 16, w = PANEL_W - 32, y = *yp;
    if (Th_Section("VIEW", x, &y, w, NULL)) {
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
    static const char *labels[5] = {"View", "Font", "Glyph", "Similar", "Layers"};
    static const int ids[5] = {4, 0, 1, 2, 3};
    int tab = panelTab, c = PanelTabs(p, 5, labels, ids, tab, "ptab");
    if (c >= 0) { PanelSetTab(c); tab = c; }
    Rectangle cr = {p.x, p.y + PANEL_TABS_H, p.width, p.height - PANEL_TABS_H};   /* the scrolling content */
    float maxS0 = panelContentH - cr.height;
    if (panelScroll > maxS0) panelScroll = maxS0 > 0 ? maxS0 : 0;
    if (panelScroll < 0) panelScroll = 0;

    /* what the Insight queries are about: the selected glyphs (usage), the code of a single one, the font (similar) */
    enum { INS_MAX_SEL = 32 };
    static char insH[INS_MAX_SEL][33];
    int nIns = 0;
    int32_t insCode = -1;
    for (int i = 0; i < font.count && i < selN && selCount >= 1 && selCount <= INS_MAX_SEL && nIns < INS_MAX_SEL; i++) {
        if (!sel[i]) continue;
        char h[33];
        px_glyph_hash(&font.glyphs[i], h);      /* "" = no ink: no bitmap to look for */
        bool dup = !h[0];
        for (int j = 0; j < nIns && !dup; j++) if (Same(insH[j], h)) dup = true;
        if (!dup) memcpy(insH[nIns++], h, 33);
        if (selCount == 1 && px_code_is_codepoint(font.glyphs[i].codepoint)) insCode = font.glyphs[i].codepoint;
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
    if (tab == 4) DrawFontViewTab(px, &y);
    else if (tab == 0) {
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

/* Export Typeface... / Export Font...: a .pxtp or .pxfp package (pixelglyphlib spec, src/package.c), written where the user picks.  The open
 * document's edits go into the store first, so the package holds what the user sees. */
static void ExportPackage(bool typeface, const char *pxtfIn, const char *pxfmIn, int variant)
{
    (void)variant;
    if (!store) return;
    char h[33], name[PATH_MAX_LEN], path[PATH_MAX_LEN], err[PX_ERR_LEN] = "";
    bool doc = typeface ? hasCont && docStored && Same(pxtfIn, origContHash) : hasFont && docStored && Same(pxfmIn, origHash);
    snprintf(h, sizeof h, "%s", typeface ? pxtfIn : pxfmIn);
    if (!CommitEdits()) return;
    if (doc) snprintf(h, sizeof h, "%s", typeface ? origContHash : origHash);
    StoreConn *c = store_main_conn(store);
    if (typeface) pkg_default_name_typeface(c, h, name, sizeof name); else pkg_default_name_font(c, h, name, sizeof name);
    if (dlgSavePath[0]) snprintf(path, sizeof path, "%s", dlgSavePath);
    else if (noDialogs || !Win_SaveFileDialog(path, (int)sizeof path, typeface ? "Pixel typeface package (*.pxtp)|*.pxtp|All files|*.*" : "Pixel font package (*.pxfp)|*.pxfp|All files|*.*",
                                              name, typeface ? "pxtp" : "pxfp")) return;
    Slashes(path);
    if (typeface ? pkg_write_typeface(c, h, path, err) : pkg_write_font(c, h, path, err)) Th_Toast(TH_TOAST_OK, "%s written: %s", typeface ? "Typeface package" : "Font package", path);
    else ShowError(typeface ? "Cannot export the typeface" : "Cannot export the font", err[0] ? err : "Unknown error.");
}
