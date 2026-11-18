/* fitdoc.inc - importing a font file, and glyphs that are DRAWN FROM A FONT LAYER (DESIGN.md "Importing a font file"); included by main.c.
 *
 * FOLLOWING.  One font layer of the open font may have `follow` set (an outline file with an absolute size, s.em > 0): the glyphs of
 * the font are then the layer's outlines turned into pixels by fit.h, with the layer's own size, width, offset, mode and threshold and
 * each glyph's correction.  When one of those settings changes, every glyph that still IS what the layer drew before is drawn again
 * (FitTick, once per frame); a glyph that was edited by hand is no longer what the layer drew, so it is left alone.  Nothing is stored
 * for this: "what the layer drew before" is drawn again from fitSnap, a copy of the layer's settings at the last redraw.
 * Undo takes the layer's settings along with the font (PushUndo keeps a copy of fitSnap beside each snapshot).
 *
 * IMPORT.  Menu, Import Font File: the file is opened as a font layer would open it; for an outline font the grid of a pixel font
 * (fit_find_grid) and the best small size of any other font (fit_find_size) are searched, the dialog offers the choice, and Import
 * makes a new font (a document without an original, like New Font) whose glyphs are drawn from the file, with the file as a font
 * layer that the glyphs follow.  A bitmap font is copied pixel by pixel. */

#define FONTFILE_FILTER "Font files and pictures|*.ttf;*.otf;*.ttc;*.otc;*.woff;*.eot;*.pfb;*.bdf;*.pcf;*.fnt;*.fon;*.json;*.png;*.gif;*.jpg;*.jpeg;*.bmp;*.tga|All files|*.*"

static FlLayer *fitSnap = NULL;        /* the settings of the follow layer the glyphs were last drawn with */
static int fitSnapSerial = 0;          /* the layer it is a copy of */
static struct { bool valid; char hash[33]; unsigned rev; int follow, kept, absent; } fitStats;
static int fitLastChanged = 0, fitLastKept = 0;   /* what the last redraw did (play.inc) */

static void FitSnapDrop(void)
{
    FlLayer_Free(fitSnap);
    fitSnap = NULL;
    fitSnapSerial = 0;
    fitStats.valid = false;
}

/* the layer the glyphs are drawn from, NULL when the font has none (or its file cannot be read) */
static FlLayer *FollowLayer(void)
{
    if (!hasFont || !flDoc) return NULL;
    for (int i = 0; i < flDoc->n; i++) {
        FlLayer *l = flDoc->v[i];
        if (l->s.follow && l->s.kind == 1 && l->s.em > 0 && FlLayer_Ready(l) && l->outline) return l;
    }
    return NULL;
}

/* for an undo snapshot: the layer settings that belong to the font as it is now */
static FlLayer *FollowCopy(void)
{
    if (readOnly) return NULL;
    FlLayer *l = FollowLayer();
    if (!l) return NULL;
    return FlLayer_Copy(fitSnap && fitSnapSerial == l->serial ? fitSnap : l);
}

static void PlacementCopy(FlLayer *dst, const FlLayer *src)
{
    dst->s.em = src->s.em; dst->s.aspect = src->s.aspect; dst->s.dx = src->s.dx; dst->s.dy = src->s.dy;
    dst->s.mode = src->s.mode; dst->s.threshold = src->s.threshold; dst->s.scale = src->s.scale;
    for (int i = dst->ncorr - 1; i >= 0; i--) {   /* "hidden" is no placement: it stays as it is */
        FlCorr c = {dst->corr[i].code, 0, 0, 1.0f, dst->corr[i].hidden};
        FlLayer_SetCorr(dst, c.code, &c);
    }
    for (int i = 0; i < src->ncorr; i++) {
        const FlCorr *old = FlLayer_Corr(dst, src->corr[i].code);
        FlCorr c = src->corr[i];
        c.hidden = old ? old->hidden : 0;
        FlLayer_SetCorr(dst, c.code, &c);
    }
}

static bool PlacementSame(const FlLayer *a, const FlLayer *b)
{
    if (a->s.em != b->s.em || a->s.aspect != b->s.aspect || a->s.dx != b->s.dx || a->s.dy != b->s.dy || a->s.mode != b->s.mode ||
        a->s.threshold != b->s.threshold) return false;
    for (int pass = 0; pass < 2; pass++) {   /* every correction of one has the same offset and size in the other ("hidden" does not count) */
        const FlLayer *p = pass ? b : a, *q = pass ? a : b;
        for (int i = 0; i < p->ncorr; i++) {
            const FlCorr *x = &p->corr[i], *y = FlLayer_Corr(q, x->code);
            float ydx = y ? y->dx : 0, ydy = y ? y->dy : 0, yds = y ? y->dsize : 1.0f;
            if (x->dx != ydx || x->dy != ydy || x->dsize != yds) return false;
        }
    }
    return true;
}

/* an undo or redo put back the font of a snapshot: the layer gets the settings that font was drawn with (takes snap) */
static void FollowRestore(FlLayer *snap)
{
    FlLayer *l = FollowLayer();
    FitSnapDrop();
    if (!snap || !l || snap->origin != l->serial) { FlLayer_Free(snap); return; }   /* the snapshot is of another layer: nothing to put back */
    if (!PlacementSame(l, snap)) { PlacementCopy(l, snap); flDoc->rev++; }
    fitSnap = snap;
    fitSnapSerial = l->serial;
}

/* The baseline moved: every font layer sits on the baseline origin, so its offset moves by d rows (y up) and the layers stay where they were on the
 * screen.  What the followed layer drew the glyphs with moves too (the glyphs moved with the baseline, so none of them has to be drawn again). */
static void LayersShiftY(float d)
{
    if (d == 0 || !flDoc) return;
    for (int i = 0; i < flDoc->n; i++) flDoc->v[i]->s.dy += d;
    if (fitSnap) fitSnap->s.dy += d;
    flDoc->rev++;
}

/* ---- glyphs from pixels ---- */

static bool GlyphFromFit(const FitBitmap *b, int32_t code, PxGlyph *g)
{
    memset(g, 0, sizeof *g);
    g->codepoint = code;
    g->advance = b->advance < 0 ? 0 : b->advance;
    if (b->w <= 0 || b->h <= 0 || !b->pix) return true;
    g->pix = (char *)malloc((size_t)b->w * (size_t)b->h);
    if (!g->pix) return false;
    for (int i = 0; i < b->w * b->h; i++) g->pix[i] = b->pix[i] ? '0' : '.';
    g->w = b->w; g->h = b->h;
    g->x = b->left;
    g->y = b->top - b->h;
    return true;
}

/* the glyph is exactly what the layer drew: the same ink at the same place, the same advance */
static bool GlyphIsFit(const PxGlyph *g, const FitBitmap *b)
{
    PxGlyph t;
    if (g->advance != (b->advance < 0 ? 0 : b->advance)) return false;
    if (!GlyphFromFit(b, g->codepoint, &t)) return false;
    int gx, gy;
    px_glyph_written_offset(g, &gx, &gy);
    bool same = px_glyph_same_shape(g, &t) && gx == t.x && gy == t.y;
    px_glyph_free(&t);
    return same;
}

static void GlyphSetFit(PxGlyph *g, const FitBitmap *b)
{
    PxGlyph t;
    if (!GlyphFromFit(b, g->codepoint, &t)) return;
    free(g->pix);
    g->pix = t.pix; g->w = t.w; g->h = t.h; g->x = t.x; g->y = t.y; g->advance = t.advance;
    g->asDrawn = false;
    g->lazy = false;
    px_glyph_touch(g);
}

/* draws a code point as the settings `set` say (the file is `src`'s); false when the file has no glyph for it */
static bool FitDrawCode(FlLayer *src, const FlLayer *set, int32_t code, FitBitmap *out)
{
    int gi = px_code_is_codepoint(code) ? outline_glyph_index(src->ol, code) : 0;
    if (gi <= 0) return false;
    FitParams p;
    FitCorr c;
    FlLayer_FitParams(set, 0, &p);
    bool hc = FlLayer_FitCorr(set, code, &c);
    return fit_render(src->ol, gi, &p, hc ? &c : NULL, out) == 0;
}

/* Draws the glyphs again from the layer.  all: every glyph the file has, also those edited by hand.  Returns how many changed. */
static int FitApply(FlLayer *l, bool all)
{
    FlLayer *old = fitSnap;
    FitParams po, pn;
    bool pushed = false;
    int changed = 0, kept = 0;
    FlLayer_FitParams(old, 0, &po);
    FlLayer_FitParams(l, 0, &pn);
    bool global = memcmp(&po, &pn, sizeof po) != 0;
    for (int i = 0; i < font.count; i++) {
        int32_t code = font.glyphs[i].codepoint;
        if (!px_code_is_codepoint(code)) continue;
        if (!all && !global) {
            FitCorr co, cn;
            FlLayer_FitCorr(old, code, &co);
            FlLayer_FitCorr(l, code, &cn);
            if (memcmp(&co, &cn, sizeof co) == 0) continue;
        }
        FitBitmap b;
        if (!all) {
            if (!FitDrawCode(l, old, code, &b)) continue;
            bool was = GlyphIsFit(&font.glyphs[i], &b);
            fit_bitmap_free(&b);
            if (!was) { kept++; continue; }
        }
        if (!FitDrawCode(l, l, code, &b)) continue;
        if (!GlyphIsFit(&font.glyphs[i], &b)) {
            if (!pushed) { PushUndo(950); pushed = true; }
            GlyphSetFit(&font.glyphs[i], &b);
            changed++;
        }
        fit_bitmap_free(&b);
    }
    {   /* the font box follows while it is what the layer gave */
        int ao, do_, an, dn;
        fit_box(l->ol, &po, &ao, &do_);
        fit_box(l->ol, &pn, &an, &dn);
        bool av = font.has[PXM_ASCENT] && (all || font.m[PXM_ASCENT] == ao) && font.m[PXM_ASCENT] != an;
        bool dv = font.has[PXM_DESCENT] && (all || font.m[PXM_DESCENT] == do_) && font.m[PXM_DESCENT] != dn;
        if ((av || dv) && !pushed) { PushUndo(950); pushed = true; }
        if (av) font.m[PXM_ASCENT] = an;
        if (dv) font.m[PXM_DESCENT] = dn;
        if (av || dv) needHash = true;
    }
    FlLayer *ns = FlLayer_Copy(l);
    FitSnapDrop();
    fitSnap = ns;
    fitSnapSerial = l->serial;
    fitLastChanged = changed;
    fitLastKept = kept;
    if (changed) needHash = true;
    return changed;
}

/* once per frame: the follow layer's settings changed -> the glyphs it drew are drawn again */
static void FitTick(void)
{
    if (!hasFont) { if (fitSnap) FitSnapDrop(); return; }
    FlLayer *l = FollowLayer();
    if (!l) { if (fitSnap) FitSnapDrop(); return; }
    if (!fitSnap || fitSnapSerial != l->serial) {   /* a layer starts to be followed: the font as it is is the baseline */
        FitSnapDrop();
        fitSnap = FlLayer_Copy(l);
        fitSnapSerial = l->serial;
        return;
    }
    if (PlacementSame(l, fitSnap)) return;
    if (readOnly) {   /* the glyphs of a locked font cannot follow: the placement they were drawn with stays */
        PlacementCopy(l, fitSnap);
        flDoc->rev++;
        Th_Toast(2, "The font is locked: unlock it first");
        return;
    }
    FitApply(l, false);
}

/* how many glyphs are still what the layer draws, how many were edited by hand, how many the file does not have */
static void FitStatsUpdate(FlLayer *l)
{
    EnsureHashes();
    if (fitStats.valid && Same(fitStats.hash, curHash) && fitStats.rev == flDoc->rev) return;
    fitStats.follow = fitStats.kept = fitStats.absent = 0;
    for (int i = 0; i < font.count; i++) {
        FitBitmap b;
        if (!FitDrawCode(l, l, font.glyphs[i].codepoint, &b)) { fitStats.absent++; continue; }
        if (GlyphIsFit(&font.glyphs[i], &b)) fitStats.follow++; else fitStats.kept++;
        fit_bitmap_free(&b);
    }
    fitStats.valid = true;
    fitStats.rev = flDoc->rev;
    snprintf(fitStats.hash, sizeof fitStats.hash, "%s", curHash);
}

/* a grid's per-glyph columns become corrections of the layer (the x offset of each glyph) */
static void GridCorrections(FlLayer *l, const FitGrid *g)
{
    for (int i = l->ncorr - 1; i >= 0; i--) {
        FlCorr c = l->corr[i];
        c.dx = 0;
        FlLayer_SetCorr(l, c.code, &c);
    }
    for (int i = 0; i < g->nphase; i++) {
        FitCorr fc;
        if (!fit_grid_corr(g, g->phaseCp[i], &fc)) continue;
        const FlCorr *old = FlLayer_Corr(l, g->phaseCp[i]);
        FlCorr c = old ? *old : (FlCorr){g->phaseCp[i], 0, 0, 1.0f, 0};
        c.dx = fc.dx;
        FlLayer_SetCorr(l, g->phaseCp[i], &c);
    }
}

static void PlacementFromParams(FlLayer *l, const FitParams *p)
{
    l->s.em = p->em; l->s.aspect = p->aspect; l->s.dx = p->dx; l->s.dy = p->dy;
    l->s.threshold = p->threshold; l->s.mode = p->mode == FIT_PIXEL;
}

/* "Find Best Fit": the search of the import again, for the mode the layer is in.  keepSize (smooth mode): the size stays as it is set,
 * the position and the threshold are searched for it. */
static void FollowFindFit(FlLayer *l, bool keepSize)
{
    char msg[160] = "";
    FitParams p;
    if (l->s.mode) {
        FitGrid g;
        if (fit_find_grid(l->ol, &g, msg, sizeof msg) != 1) { Th_Toast(TH_TOAST_ERR, "No pixel grid found in the outlines"); return; }
        fit_grid_params(l->ol, &g, &p);
        PlacementFromParams(l, &p);
        GridCorrections(l, &g);
        Th_Toast(TH_TOAST_OK, g.sure ? "Grid found: %.4g px per em" : "Best guess: %.4g px per em", (double)p.em);
        fit_grid_free(&g);
    } else {
        FitSizeOpts o = {0, 0, keepSize ? l->s.em : 0};
        FitSizeReport r;
        float aspect = l->s.aspect, dx = l->s.dx;
        if (fit_find_size(l->ol, &o, &p, &r, msg, sizeof msg) != 1) { Th_Toast(TH_TOAST_ERR, "No size found: %s", msg); return; }
        PlacementFromParams(l, &p);
        if (keepSize) { l->s.aspect = aspect; l->s.dx = dx; }   /* only what was searched changes */
        if (keepSize) Th_Toast(TH_TOAST_OK, "At %.4g px per em: offset Y %.3g, threshold %d %%; %d of %d sample glyphs keep their shape", (double)p.em, (double)p.dy,
                               (int)lroundf(p.threshold * 100.0f), (int)lroundf(r.topo * (float)r.probes), r.probes);
        else Th_Toast(TH_TOAST_OK, "Best size: %.4g px per em", (double)p.em);
    }
    flDoc->rev++;
}

/* ---- the Layers tab of the font view's panel ---- */

static bool FStep(Rectangle r, const char *prefix, float *v, float step, float lo, float hi, bool off)
{
    float nv;
    if (!Th_NumberF(r, prefix, *v, lo, hi, step, off ? TH_TF_DISABLED : 0, &nv)) return false;
    *v = nv;
    return true;
}

static void DrawFontLayersTab(float px, float *yp)
{
    float y = *yp, x = px + 16, w = PANEL_W - 32;
    if (!flDoc || flDoc->n == 0) {
        Th_Text("This font has no font layers.", x, y, TH_TEXT_DIM);
        y += TH_ROW_H;
        Th_TextSmall("Menu, Import Font File makes a font whose glyphs", x, y, TH_TEXT_DIM); y += 20;
        Th_TextSmall("are drawn from a font file kept as a layer.", x, y, TH_TEXT_DIM); y += 20;
        Th_TextSmall("Add Layer in the glyph view adds one to this font.", x, y, TH_TEXT_DIM); y += 24;
        *yp = y;
        return;
    }
    FlLayer *prim = FollowLayer();
    for (int i = 0; !prim && i < flDoc->n; i++) {
        FlLayer *l = flDoc->v[i];
        if (l->s.kind == 1 && FlLayer_Ready(l) && l->outline) prim = l;
    }
    for (int i = 0; i < flDoc->n; i++) {
        FlLayer *l = flDoc->v[i];
        bool file = l->s.kind == 1, ok = !file || FlLayer_Ready(l);
        char head[200], fit[200];
        snprintf(head, sizeof head, "%.190s", l->s.label[0] ? l->s.label : (file ? BaseName(l->s.source) : "Font"));
        Th_FitText(head, 0, w, fit, sizeof fit);
        Th_Text(fit, x, y, TH_TEXT);
        y += 22;
        const char *kind = !file ? "A font of the database" : !ok ? l->err : l->outline ? "Outline font file" : "Bitmap font file";
        Th_TextSmall(kind, x, y, ok ? TH_TEXT_DIM : TH_TEXT_NOTE);
        y += 20;
        if (file) {
            Th_FitText(l->s.source, TH_F_TAIL, w, fit, sizeof fit);
            Th_TextSmall(fit, x, y, TH_TEXT_DIM);
            y += 24;
        }
        if (l != prim) { y += 8; continue; }
        FsLayer *s = &l->s;
        bool ch = false, lock = readOnly && s->follow;   /* an original's glyphs cannot follow a change */
        Rectangle vr;
        int nv;
        if (s->em > 0) {
            vr = Th_Row(x, &y, w, "Size (px/em)", NULL, 0);
            ch |= FStep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "fl_size", &s->em, 0.25f, 2, 400, lock);
            if (Th_StepperHover()) hintText = "The size of the outlines in pixels per em; the glyphs are drawn again";
        } else {
            vr = Th_Row(x, &y, w, "Size (%)", NULL, 0);
            if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "fl_size", (int)lroundf(s->scale * 100.0f), 10, 1000, 0, &nv)) { s->scale = (float)nv / 100.0f; ch = true; }
        }
        vr = Th_Row(x, &y, w, "Width (%)", NULL, 0);
        if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "fl_width", (int)lroundf(s->aspect * 100.0f), 25, 400, lock ? TH_TF_DISABLED : 0, &nv)) { s->aspect = (float)nv / 100.0f; ch = true; }
        vr = Th_Row(x, &y, w, "Offset X", NULL, 0);
        ch |= FStep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "fl_x", &s->dx, 0.25f, -999, 999, lock);
        vr = Th_Row(x, &y, w, "Offset Y", NULL, 0);
        ch |= FStep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "fl_y", &s->dy, 0.25f, -999, 999, lock);
        y += 4;
        Th_Section("GLYPHS FROM THIS LAYER", x, &y, w, 0, NULL);
        bool fol = s->follow != 0;
        if (Th_Switch((Rectangle){x, y, w, TH_ROW_H}, "Draw the glyphs from this layer", &fol, TH_CK_RIGHT | (readOnly ? TH_CK_OFF : 0), "fl_follow")) {
            for (int k = 0; k < flDoc->n; k++) flDoc->v[k]->s.follow = 0;
            if (fol && !(s->em > 0)) s->em = (float)FontPx() * s->scale;   /* from now on an absolute size: the font box follows it */
            s->follow = fol;
            FitSnapDrop();   /* the font as it is is the baseline: switching on redraws nothing */
            ch = true;
        }
        y += TH_ROW_H + 4;
        if (readOnly) {
            Th_TextSmall("The font is locked: unlock it first.", x, y, TH_TEXT_DIM);
            y += 24;
        }
        if (s->follow && s->em > 0) {
            static const char *modes[2] = {"Smooth", "Pixel font"};
            static const char *mhint[2] = {"Any font: a pixel is inked when the outline covers enough of it", "A pixel font drawn with outlines: a pixel is inked when its centre is inside"};
            int c = Th_Segmented((Rectangle){x, y, w, TH_ROW_H}, &(ThSeg){ modes, 2, s->mode ? 1 : 0, 0u, "fl_mode", NULL, mhint });
            if (c >= 0 && c != (s->mode ? 1 : 0) && !lock) { s->mode = c; ch = true; }
            y += TH_ROW_H + 6;
            if (!s->mode) {
                vr = Th_Row(x, &y, w, "Threshold (%)", NULL, 0);
                if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "fl_thr", (int)lroundf(s->threshold * 100.0f), 5, 95, lock ? TH_TF_DISABLED : 0, &nv)) { s->threshold = (float)nv / 100.0f; ch = true; }
            }
            if (!s->mode) {
                Rectangle b0 = {x, y + 2, w, TH_ROW_H};
                Th_Reg("fl_fitsize", b0);
                if (Th_Button(b0, "Optimize for This Size", TH_BTN_OFF(!lock), "Keep the size; search the vertical position and the threshold that draw it best")) FollowFindFit(l, true);
                y += TH_ROW_H + 6;
            }
            float bw = (w - TH_GAP) / 2;
            Rectangle b1 = {x, y + 2, bw, TH_ROW_H}, b2 = {x + bw + TH_GAP, y + 2, bw, TH_ROW_H};
            Th_Reg("fl_fit", b1);
            Th_Reg("fl_redraw", b2);
            if (Th_Button(b1, "Find Best Fit", TH_BTN_OFF(!lock), s->mode ? "Search the pixel grid of the outlines again" : "Search the best small size, position and threshold again")) FollowFindFit(l, false);
            if (Th_Button(b2, "Redraw All", TH_BTN_OFF(!readOnly), "Draw every glyph from the layer again, also those edited by hand  (Ctrl+Z undoes it)")) {
                if (!fitSnap) { fitSnap = FlLayer_Copy(l); fitSnapSerial = l->serial; }
                int n = FitApply(l, true);
                Th_Toast(TH_TOAST_OK, "%d glyph%s redrawn", n, n == 1 ? "" : "s");
            }
            y += TH_ROW_H + 10;
            if (!readOnly) {
                char st[160];
                FitStatsUpdate(l);
                snprintf(st, sizeof st, "%d glyph%s follow the layer", fitStats.follow, fitStats.follow == 1 ? "" : "s");
                Th_TextSmall(st, x, y, TH_TEXT_DIM);
                y += 20;
                if (fitStats.kept) {
                    snprintf(st, sizeof st, "%d edited by hand: they stay as they are", fitStats.kept);
                    Th_TextSmall(st, x, y, TH_TEXT_DIM);
                    y += 20;
                }
                if (fitStats.absent) {
                    snprintf(st, sizeof st, "%d not in the file", fitStats.absent);
                    Th_TextSmall(st, x, y, TH_TEXT_DIM);
                    y += 20;
                }
            }
        }
        if (ch) flDoc->rev++;
        y += 12;
    }
    *yp = y;
}

/* ---- Import Font File ---- */

static int imax_(int a, int b) { return a > b ? a : b; }

static char dlgFontPath[PATH_MAX_LEN];

static bool DlgFontFile(char *out, int size)
{
    if (dlgFontPath[0]) { snprintf(out, (size_t)size, "%s", dlgFontPath); return true; }
    if (noDialogs) return false;
    return Win_OpenFileDialog(out, size, FONTFILE_FILTER);
}

static struct {
    bool open;
    int frame;
    char path[PATH_MAX_LEN];
    FlLayer *probe;            /* the file, opened as a font layer opens it */
    bool outline;
    int face;
    bool hasGrid;              /* a pixel grid was found (grid.sure: it passed the tests, else a guess) */
    FitGrid grid;
    bool hasSmooth;
    FitParams smooth;
    FitSizeReport rep;
    bool pixel;                /* the choice: a pixel font (drawn with outlines, or a bitmap font) */
    bool typeface;             /* make a typeface with the names of the file */
    char family[160], style[96];
    char err[200];
    double ms;                 /* how long the searches took */
    /* a picture of a font (sheet.h) */
    bool sheet;
    unsigned char *rgba;       /* the picture, iw * ih * 4 */
    int iw, ih;
    SheetOpts so;              /* what the user set (0: found by the analysis) */
    Sheet sh;                  /* what the analysis found */
    char first[16];            /* the "first character" field: empty = recognise */
} fimp;

static void ImpFree(void)
{
    FlLayer_Free(fimp.probe);
    fit_grid_free(&fimp.grid);
    sheet_free(&fimp.sh);
    free(fimp.rgba);
    memset(&fimp, 0, sizeof fimp);
}

/* ---- a picture of a font (sheet.h) ---- */

static SheetRefs *sheetRefs = NULL;   /* the reference glyphs, made once (about two seconds) */

static SheetRefs *SheetRefsGet(void)
{
    static const char *rel[3] = {"%sassets/fonts", "%s../../assets/fonts", "assets/fonts"};
    if (sheetRefs) return sheetRefs;
    sheetRefs = sheet_refs_new();
    for (int i = 0; sheetRefs && i < 3; i++) {
        char d[1100], t[1200];
        if (i < 2) snprintf(d, sizeof d, rel[i], GetApplicationDirectory()); else snprintf(d, sizeof d, "%s", rel[i]);
        snprintf(t, sizeof t, "%s/unifont/unifont-18.0.01.otf", d);
        if (px_file_exists(t)) { sheet_refs_add_defaults(sheetRefs, d); break; }
    }
    return sheetRefs;
}

static bool IsPicture(const char *p)
{
    return HasExt(p, ".png") || HasExt(p, ".gif") || HasExt(p, ".jpg") || HasExt(p, ".jpeg") || HasExt(p, ".bmp") || HasExt(p, ".tga");
}

/* runs the analysis again with the options as they are */
static void ImpSheetAnalyze(void)
{
    char msg[160] = "";
    sheet_free(&fimp.sh);
    fimp.err[0] = 0;
    double t0 = GetTime();
    if (sheet_analyze(fimp.rgba, fimp.iw, fimp.ih, &fimp.so, SheetRefsGet(), &fimp.sh, msg, sizeof msg) < 0) snprintf(fimp.err, sizeof fimp.err, "%s", msg[0] ? msg : "no font found");
    fimp.ms = (GetTime() - t0) * 1000.0;
}

static void ImpSheetOpen(const char *path)
{
    char err[200] = "";
    fimp.sheet = true;
    fimp.pixel = true;
    fimp.rgba = bm_load_rgba(path, &fimp.iw, &fimp.ih, err, sizeof err);
    if (!fimp.rgba) snprintf(fimp.err, sizeof fimp.err, "%s", err);
    else ImpSheetAnalyze();
    snprintf(fimp.family, sizeof fimp.family, "%.150s", BaseName(path));
    char *dot = strrchr(fimp.family, '.');
    if (dot && dot != fimp.family) *dot = 0;
    snprintf(fimp.style, sizeof fimp.style, "Regular");
}

/* opens the file and runs the searches; fimp.err says why it cannot be imported */
static void ImpAnalyze(const char *path, int face)
{
    bool tf = fimp.open ? fimp.typeface : true;
    ImpFree();
    fimp.typeface = tf;
    snprintf(fimp.path, sizeof fimp.path, "%s", path);
    fimp.face = face;
    if (IsPicture(path)) { ImpSheetOpen(path); return; }
    FsLayer s = fs_layer_default();
    s.kind = 1;
    snprintf(s.source, sizeof s.source, "%s", path);
    s.face = face;
    s.r = 0x2F; s.g = 0x6F; s.b = 0xEB;
    s.opacity = 45;
    fimp.probe = FlLayer_New(&s);
    if (!fimp.probe || !FlLayer_Ready(fimp.probe)) { snprintf(fimp.err, sizeof fimp.err, "%s", fimp.probe ? fimp.probe->err : "out of memory"); return; }
    fimp.outline = fimp.probe->outline;
    double t0 = GetTime();
    if (fimp.outline) {
        char msg[160] = "";
        fimp.hasGrid = fit_find_grid(fimp.probe->ol, &fimp.grid, msg, sizeof msg) == 1;
        fimp.hasSmooth = fit_find_size(fimp.probe->ol, NULL, &fimp.smooth, &fimp.rep, msg, sizeof msg) == 1;
        fimp.pixel = fimp.hasGrid && fimp.grid.sure;
        if (!fimp.hasGrid && !fimp.hasSmooth) snprintf(fimp.err, sizeof fimp.err, "the font has no glyph with ink");
        snprintf(fimp.family, sizeof fimp.family, "%s", outline_family(fimp.probe->ol));
        snprintf(fimp.style, sizeof fimp.style, "%s", outline_style(fimp.probe->ol));
    } else {
        BmStrikeInfo si;
        fimp.pixel = true;
        if (bm_strike_info(fimp.probe->bm, fimp.probe->strike, &si)) {
            snprintf(fimp.family, sizeof fimp.family, "%s", si.family[0] ? si.family : si.name);
            snprintf(fimp.style, sizeof fimp.style, "%s", si.style);
        }
    }
    fimp.ms = (GetTime() - t0) * 1000.0;
    if (!fimp.family[0]) {
        snprintf(fimp.family, sizeof fimp.family, "%.150s", BaseName(path));
        char *dot = strrchr(fimp.family, '.');
        if (dot && dot != fimp.family) *dot = 0;
    }
    if (!fimp.style[0]) snprintf(fimp.style, sizeof fimp.style, "Regular");
}

static void ImpParams(FitParams *p)
{
    if (fimp.pixel && fimp.hasGrid) fit_grid_params(fimp.probe->ol, &fimp.grid, p);
    else *p = fimp.smooth;
}

static void ImportFontOpen(void)
{
    if (!store) { Th_Toast(2, "No database is loaded"); return; }
    char path[PATH_MAX_LEN];
    if (!DlgFontFile(path, sizeof path)) return;
    Slashes(path);
    ImpAnalyze(path, 0);
    fimp.open = true;
    fimp.frame = frameNo;
    Th_TextBlur();
}

/* the typeface of an imported font: one variant with the names the file states; false when they do not make a valid pxtf */
static bool ImpTypeface(PxtfTypeface *c)
{
    static const struct { int id; PxtfText t; } names[] = {
        {0, PXTF_COPYRIGHT}, {4, PXTF_FULLNAME}, {5, PXTF_VERSION}, {7, PXTF_TRADEMARK}, {8, PXTF_VENDOR}, {9, PXTF_DESIGNER},
        {10, PXTF_DESCRIPTION}, {11, PXTF_VENDORURL}, {12, PXTF_DESIGNERURL}, {13, PXTF_LICENSE}, {14, PXTF_LICENSEURL}};
    pxtf_init(c);
    size_t n = 0;
    char *bytes = px_read_file(fimp.path, &n);
    if (!bytes) return false;
    px_hash128_hex(bytes, n, c->hash, false);
    free(bytes);
    c->v = (PxtfVariant *)calloc(1, sizeof(PxtfVariant));
    if (!c->v) return false;
    c->count = c->cap = 1;
    PxtfVariant *v = &c->v[0];
    v->number = 1;
    char fmt[16] = "FONT", type[32];
    if (fimp.outline) snprintf(fmt, sizeof fmt, "%s", outline_format(fimp.probe->ol));
    else { const char *e = strrchr(fimp.path, '.'); if (e && e[1]) snprintf(fmt, sizeof fmt, "%s", e + 1); }
    int o = 0;
    for (int i = 0; fmt[i]; i++) { int ch = toupper((unsigned char)fmt[i]); if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) fmt[o++] = (char)ch; }
    fmt[o] = 0;
    if (!fmt[0]) snprintf(fmt, sizeof fmt, "FONT");
    for (int i = 0; i <= (int)strlen(fmt); i++) type[i] = (char)tolower((unsigned char)fmt[i]);
    snprintf(type + strlen(type), sizeof type - strlen(type), "%s", fimp.sheet ? "-picture" : fimp.outline ? "-outline" : "-bitmap");
    v->format = strdup(fmt);
    v->type = strdup(type);
    snprintf(v->pxfm, sizeof v->pxfm, "00000000000000000000000000000000");
    pxtf_set_text(v, PXTF_FAMILY, fimp.family);
    pxtf_set_text(v, PXTF_STYLE, fimp.style);
    for (size_t i = 0; fimp.outline && i < sizeof names / sizeof names[0]; i++) {
        char t[2000];
        int len = outline_name(fimp.probe->ol, names[i].id, t, sizeof t);
        while (len > 0 && (t[len - 1] == ' ' || t[len - 1] == '\n' || t[len - 1] == '\r' || t[len - 1] == '\t')) t[--len] = 0;
        if (len > 0 && pxtf_utf8_valid(t, (size_t)len) && pxtf_nfc_ok(t, (size_t)len)) pxtf_set_text(v, names[i].t, t);
    }
    /* the text must read back with the strict reader, or the typeface could not be exported and imported again */
    for (int pass = 0; pass < 2; pass++) {
        size_t tl = 0;
        char err[PX_ERR_LEN] = "";
        char *text = pxtf_text(c, &tl, NULL);
        PxtfTypeface back;
        pxtf_init(&back);
        bool ok = text && pxtf_parse(text, tl, &back, err);
        pxtf_free(&back);
        free(text);
        if (ok) return true;
        if (pass) break;
        for (int t = 0; t < PXTF_TEXT_COUNT; t++) if (t != PXTF_FAMILY && t != PXTF_STYLE) pxtf_set_text(v, (PxtfText)t, NULL);   /* the names only */
    }
    pxtf_free(c);
    pxtf_init(c);
    return false;
}

/* makes the font and its layer; the document is new (no original), the glyphs follow the layer */
static bool ImpTypeface(PxtfTypeface *c);

/* a font from the picture: the cells become glyphs, the colours of the ink the palette */
static bool ImportSheetCreate(void)
{
    Sheet *sh = &fimp.sh;
    if (fimp.err[0] || !store || !sh->ncells) return false;
    PxFont f;
    px_font_init(&f);
    bool used[PX_NUM_PIXELS] = {false}, mono = sh->npalette <= 1, rows = sh->layout == SHEET_ROWS;
    int base = sh->lineTop + sh->ascent, maxArea = 1, gap = imax_(1, (sh->ascent + sh->descent + 6) / 12), uncoded = 0;
    for (int i = 0; i < sh->ncells; i++) if (sh->cells[i].w * sh->cells[i].h > maxArea) maxArea = sh->cells[i].w * sh->cells[i].h;
    unsigned char *buf = (unsigned char *)malloc((size_t)maxArea);
    if (!buf) { px_font_free(&f); return false; }
    for (int i = 0; i < sh->ncells; i++) {
        const SheetCell *c = &sh->cells[i];
        PxGlyph g;
        memset(&g, 0, sizeof g);
        if (!c->iw) {
            if (c->code != 32 || px_font_find(&f, 32) >= 0) continue;
            g.codepoint = 32;
            g.advance = c->w;
            px_font_add_glyph(&f, &g);
            continue;
        }
        sheet_cell_pixels(sh, c, buf);
        g.w = c->iw; g.h = c->ih;
        g.pix = (char *)malloc((size_t)g.w * (size_t)g.h);
        if (!g.pix) continue;
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int v = buf[(size_t)(c->iy - c->y + y) * (size_t)c->w + (size_t)(c->ix - c->x + x)];
                int pi = !v ? 0 : mono ? 1 : v;
                used[pi] = true;
                g.pix[y * g.w + x] = PX_PIXELS[pi];
            }
        g.x = rows ? 0 : c->ix - c->x;
        g.y = base - (c->iy + c->ih - c->y);
        g.advance = rows ? c->iw + gap : c->w;
        if (c->code >= 0 && c->code <= PX_CODE_MAX_CODEPOINT && px_font_find(&f, c->code) < 0) { g.codepoint = c->code; px_font_add_glyph(&f, &g); }
        else { px_font_add_uncoded(&f, &g); uncoded++; }   /* a glyph without a known character is kept, without a code point */
        px_glyph_free(&g);
    }
    free(buf);
    if (px_font_find(&f, 32) < 0) {   /* a sheet rarely draws the space */
        PxGlyph g;
        memset(&g, 0, sizeof g);
        g.codepoint = 32;
        g.advance = sh->layout == SHEET_GRID ? sh->cellW : imax_(2, (sh->ascent + sh->descent) / 3);
        px_font_add_glyph(&f, &g);
    }
    if (f.count <= 1) { px_font_free(&f); ShowError("Cannot import the picture", "No glyph was found in it."); return false; }
    f.has[PXM_ASCENT] = f.has[PXM_DESCENT] = true;
    f.m[PXM_ASCENT] = sh->ascent < 0 ? 0 : sh->ascent;
    f.m[PXM_DESCENT] = sh->descent < 0 ? 0 : sh->descent;
    f.monospaced = sh->layout == SHEET_GRID;
    if (!mono) {   /* the colours of the ink on a transparent background: each glyph gets the palette of its own colours */
        uint32_t cc[PX_NUM_PIXELS];
        for (int k = 0; k < PX_NUM_PIXELS; k++) cc[k] = k >= 1 && k - 1 < sh->npalette ? sh->palette[k - 1] : 0x000000FFu;
        for (int i = 0; i < f.count; i++) px_glyph_set_colors(&f, &f.glyphs[i], cc);
    }
    (void)used;
    PxtfTypeface tf;
    bool haveTf = fimp.typeface && ImpTypeface(&tf);
    char t[300];
    snprintf(t, sizeof t, "%s%s%s", fimp.family, fimp.family[0] && fimp.style[0] ? " " : "", fimp.style);
    int count = f.count, colours = mono ? 1 : sh->npalette;
    ImpFree();
    AdoptDocument(&f, haveTf ? &tf : NULL, haveTf ? 1 : 0, false, t, t, NULL, NULL, "imported picture", "");
    docIsNew = true;
    needHash = true;
    EnsureHashes();
    FitSnapDrop();
    if (uncoded) Th_Toast(TH_TOAST_OK, "Imported %d glyphs, %d of them without a known character (no code point)", count, uncoded);
    else Th_Toast(TH_TOAST_OK, "Imported %d glyphs in %d colour%s", count, colours, colours == 1 ? "" : "s");
    return true;
}

static bool ImportFontCreate(void)
{
    if (fimp.sheet) return ImportSheetCreate();
    if (!fimp.probe || fimp.err[0] || !store) return false;
    PxFont f;
    px_font_init(&f);
    int skipped = 0;
    FitParams p;
    memset(&p, 0, sizeof p);
    FlLayer *l = fimp.probe;
    if (fimp.outline) {
        ImpParams(&p);
        PlacementFromParams(l, &p);
        l->s.follow = 1;
        if (fimp.pixel && fimp.hasGrid) GridCorrections(l, &fimp.grid);
        int *cp = NULL, ncp = fit_codepoints(l->ol, &cp);
        for (int i = 0; i < ncp; i++) {
            FitBitmap b;
            PxGlyph g;
            if (!FitDrawCode(l, l, cp[2 * i], &b)) { skipped++; continue; }
            if (GlyphFromFit(&b, cp[2 * i], &g)) { px_font_add_glyph(&f, &g); px_glyph_free(&g); }
            fit_bitmap_free(&b);
        }
        free(cp);
        int a, d;
        fit_box(l->ol, &p, &a, &d);
        f.has[PXM_ASCENT] = f.has[PXM_DESCENT] = true;
        f.m[PXM_ASCENT] = a;
        f.m[PXM_DESCENT] = d;
        f.symbolic = outline_symbolic(l->ol) != 0;
    } else {
        BmStrikeInfo si;
        int n = bm_glyph_count(l->bm, l->strike);
        for (int i = 0; i < n; i++) {
            const BmGlyph *bg = bm_glyph_at(l->bm, l->strike, i);
            if (!bg || bg->codepoint < 0 || bg->codepoint > PX_CODE_MAX_CODEPOINT || px_font_find(&f, bg->codepoint) >= 0) { skipped++; continue; }
            PxGlyph g;
            memset(&g, 0, sizeof g);
            g.codepoint = bg->codepoint;
            g.advance = bg->advance < 0 ? 0 : bg->advance;
            if (bg->w > 0 && bg->h > 0 && bg->pix) {
                g.pix = (char *)malloc((size_t)bg->w * (size_t)bg->h);
                if (!g.pix) continue;
                for (int k = 0; k < bg->w * bg->h; k++) g.pix[k] = (bg->depth == 1 ? bg->pix[k] != 0 : bg->pix[k] >= 128) ? '0' : '.';
                g.w = bg->w; g.h = bg->h; g.x = bg->x; g.y = bg->y;
                px_glyph_crop(&g);
            }
            px_font_add_glyph(&f, &g);
            px_glyph_free(&g);
        }
        if (bm_strike_info(l->bm, l->strike, &si)) f.symbolic = si.symbolic != 0;
        if (bm_strike_info(l->bm, l->strike, &si) && si.ascent + si.descent > 0) {
            f.has[PXM_ASCENT] = f.has[PXM_DESCENT] = true;
            f.m[PXM_ASCENT] = si.ascent < 0 ? 0 : si.ascent;
            f.m[PXM_DESCENT] = si.descent < 0 ? 0 : si.descent;
        }
    }
    if (f.count == 0) {
        px_font_free(&f);
        ShowError("Cannot import the font", "The file has no glyph with a code point.");
        return false;
    }
    PxtfTypeface tf;
    bool haveTf = fimp.typeface && ImpTypeface(&tf);
    char t[300];
    snprintf(t, sizeof t, "%s%s%s", fimp.family, fimp.family[0] && fimp.style[0] ? " " : "", fimp.style);
    FlLayer_Label(l, l->s.label, (int)sizeof l->s.label);
    l->metaChanged = false;
    fimp.probe = NULL;                 /* the layer now belongs to the document */
    int count = f.count;
    bool outline = fimp.outline, pixel = fimp.pixel, noTf = fimp.typeface && !haveTf;
    ImpFree();
    AdoptDocument(&f, haveTf ? &tf : NULL, haveTf ? 1 : 0, false, t, t, NULL, NULL, "imported font", "");
    docIsNew = true;
    needHash = true;
    EnsureHashes();
    FlDoc_Add(flDoc, l, -1);
    flDoc->rev++;
    if (ed) { Layers_SetFontDoc(Editor_Layers(ed), NULL); Layers_SetFontDoc(Editor_Layers(ed), flDoc); }
    FitSnapDrop();
    if (noTf) Th_Toast(TH_TOAST_ERR, "The names of the file make no valid typeface: imported as a font without one");
    if (outline) Th_Toast(TH_TOAST_OK, "Imported %d glyphs at %.4g px per em%s", count, (double)p.em, pixel ? " (pixel grid)" : "");
    else Th_Toast(TH_TOAST_OK, "Imported %d glyphs", count);
    return true;
}

/* the sample of the dialog, drawn as the import would draw it */
static void ImpPreview(Rectangle r)
{
    static const char *text = "Hamburgefonstiv 0123";
    DrawRectangleRec(r, TH_PANEL_ALT);
    DrawRectangleLinesEx(r, 1, TH_LINE);
    if (!fimp.probe || fimp.err[0]) return;
    int asc = 0, desc = 0;
    FitParams p;
    BmStrikeInfo si;
    memset(&p, 0, sizeof p);
    if (fimp.outline) { ImpParams(&p); fit_box(fimp.probe->ol, &p, &asc, &desc); }
    else if (bm_strike_info(fimp.probe->bm, fimp.probe->strike, &si)) { asc = si.ascent; desc = si.descent; }
    int hgt = asc + desc > 0 ? asc + desc : 16;
    int z = (int)((r.height - 16) / (float)hgt);
    if (z < 1) z = 1;
    if (z > 4) z = 4;
    int by = (int)(r.y + (r.height - (float)(hgt * z)) / 2) + asc * z, pen = (int)r.x + 10;
    BeginScissorMode((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, (int)r.height - 2);
    for (const char *c = text; *c && pen < r.x + r.width; c++) {
        if (fimp.outline) {
            FitBitmap b;
            FitCorr fc = {0, 0, 1};
            int gi = outline_glyph_index(fimp.probe->ol, (unsigned char)*c);
            bool hc = fimp.pixel && fimp.hasGrid && fit_grid_corr(&fimp.grid, (unsigned char)*c, &fc);
            if (gi <= 0 || fit_render(fimp.probe->ol, gi, &p, hc ? &fc : NULL, &b) != 0) { pen += (hgt / 3 + 1) * z; continue; }
            for (int yy = 0; yy < b.h; yy++)
                for (int xx = 0; xx < b.w; xx++)
                    if (b.pix[yy * b.w + xx]) DrawRectangle(pen + (b.left + xx) * z, by - (b.top - yy) * z, z, z, TH_TEXT);
            pen += (b.advance > 0 ? b.advance : b.w + 1) * z;
            fit_bitmap_free(&b);
        } else {
            const BmGlyph *g = bm_find(fimp.probe->bm, fimp.probe->strike, (unsigned char)*c);
            if (!g) { pen += (hgt / 3 + 1) * z; continue; }
            for (int yy = 0; yy < g->h; yy++)
                for (int xx = 0; xx < g->w; xx++)
                    if (g->pix && (g->depth == 1 ? g->pix[yy * g->w + xx] != 0 : g->pix[yy * g->w + xx] >= 128))
                        DrawRectangle(pen + (g->x + xx) * z, by - (g->y + g->h - yy) * z, z, z, TH_TEXT);
            pen += (g->advance > 0 ? g->advance : g->w + 1) * z;
        }
    }
    EndScissorMode();
}

/* the dialog of a picture: what was found, and every step of it can be set by hand */
static void DrawImportSheetDialog(int W, int H)
{
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    uiBlocked = false;
    Th_SetInputBlocked(false);
    bool fresh = fimp.frame == frameNo, again = false;
    Sheet *sh = &fimp.sh;
    bool can = !fimp.err[0] && sh->ncells > 0;
    const float pw = 720, pad = 24;
    float ph = pad + TH_SIZE_HEAD + TH_PAD + 22 + 6 * (TH_ROW_H + TH_GAP / 2) + 3 * TH_LINE_H + 8 + 150 + TH_PAD + TH_ROW_H + pad;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    Th_Raised(pr, 1);
    float x = pr.x + pad, w = pr.width - 2 * pad, y = pr.y + pad;
    Th_TextHead("Import Font Picture", x, y, TH_TEXT);
    y += TH_SIZE_HEAD + TH_PAD;
    char fit[PATH_MAX_LEN + 16];
    Th_FitText(fimp.path, TH_F_TAIL, w, fit, sizeof fit);
    Th_TextSmall(fit, x, y, TH_TEXT_DIM);
    y += 22;
    int nv;
    Rectangle vr = Th_Row(x, &y, w, "Family", NULL, 0);
    if (Th_TextField((Rectangle){vr.x, vr.y, vr.width, TH_ROW_H}, "if.family", fimp.family, (int)sizeof fimp.family, 0, "Family") & TH_TF_CHANGED) snprintf(fimp.family, sizeof fimp.family, "%s", Th_TextValue());
    vr = Th_Row(x, &y, w, "Style", NULL, 0);
    if (Th_TextField((Rectangle){vr.x, vr.y, vr.width, TH_ROW_H}, "if.style", fimp.style, (int)sizeof fimp.style, 0, "Regular") & TH_TF_CHANGED) snprintf(fimp.style, sizeof fimp.style, "%s", Th_TextValue());
    {   /* the layout: found, or chosen */
        static const char *lay[4] = {"Auto", "Grid", "Rows", "Marker row"};
        static const char *lh[4] = {"Find the layout", "Cells of one size", "Rows of glyphs of their own widths, cut at blank columns", "A strip: single pixels in the top row stand over the columns between the glyphs"};
        vr = Th_Row(x, &y, w, "Layout", NULL, 0);
        int c = Th_Segmented((Rectangle){vr.x, vr.y, 380, TH_ROW_H}, &(ThSeg){ lay, 4, fimp.so.layout, 0u, "if.layout", NULL, lh });
        if (c >= 0 && c != fimp.so.layout) { fimp.so.layout = c; fimp.so.cellW = fimp.so.cellH = 0; again = true; }
        if (can) {
            const char *found = sh->layout == SHEET_GRID ? "found: grid" : sh->layout == SHEET_ROWS ? "found: rows" : "found: marker row";
            Th_Text(found, vr.x + 390, Fl(vr.y + (TH_ROW_H - Th_Measure(found).y) / 2), TH_TEXT_DIM);
        }
    }
    {   /* the cell of a grid, and the scale of an enlarged picture */
        bool grid = can && sh->layout == SHEET_GRID;
        vr = Th_Row(x, &y, w, "Cell (w, h)", NULL, 0);
        int cw = grid ? sh->cellW : 0, ch = grid ? sh->cellH : 0;
        if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "if.cellw", cw, 2, 512, grid ? 0 : TH_TF_DISABLED, &nv)) { fimp.so.layout = SHEET_GRID; fimp.so.cellW = nv; fimp.so.cellH = ch; fimp.so.offX = sh->offX; fimp.so.offY = sh->offY; again = true; }
        if (Th_Number((Rectangle){vr.x + 104, vr.y, 96, TH_ROW_H}, "if.cellh", ch, 2, 512, grid ? 0 : TH_TF_DISABLED, &nv)) { fimp.so.layout = SHEET_GRID; fimp.so.cellW = cw; fimp.so.cellH = nv; fimp.so.offX = sh->offX; fimp.so.offY = sh->offY; again = true; }
        Th_Text("Scale", vr.x + 230, Fl(vr.y + (TH_ROW_H - Th_Measure("Scale").y) / 2), TH_TEXT_DIM);
        if (Th_Number((Rectangle){vr.x + 284, vr.y, 96, TH_ROW_H}, "if.scale", can ? sh->scale : 1, 1, 24, fimp.rgba ? 0 : TH_TF_DISABLED, &nv)) { fimp.so.scale = nv; again = true; }
        if (can && sh->blocks > 1) {
            Th_Text("Copy", vr.x + 400, Fl(vr.y + (TH_ROW_H - Th_Measure("Copy").y) / 2), TH_TEXT_DIM);
            if (Th_Number((Rectangle){vr.x + 448, vr.y, 96, TH_ROW_H}, "if.block", sh->block + 1, 1, sh->blocks, 0, &nv)) { fimp.so.block = nv - 1; again = true; }
        }
    }
    {   /* the characters: recognised, or counted from a first one */
        vr = Th_Row(x, &y, w, "First character", NULL, 0);
        if (Th_TextField((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "if.first", fimp.first, (int)sizeof fimp.first, fimp.rgba ? 0 : TH_TF_DISABLED, "auto") & TH_TF_CHANGED) {
            snprintf(fimp.first, sizeof fimp.first, "%s", Th_TextValue());
            int code = 0, len = 0;
            if ((fimp.first[0] == 'U' || fimp.first[0] == 'u') && fimp.first[1] == '+') code = (int)strtol(fimp.first + 2, NULL, 16);
            else if (fimp.first[0]) code = GetCodepoint(fimp.first, &len);
            fimp.so.firstCode = code > 0 ? code : 0;
            again = true;
        }
        const char *hint = fimp.so.firstCode ? "the glyphs are counted from it" : "empty: recognised from the shapes";
        Th_TextSmall(hint, vr.x + 106, Fl(vr.y + (TH_ROW_H - Th_MeasureSmall(hint).y) / 2), TH_TEXT_DIM);
        bool monoV = fimp.so.mono != 0;
        if (Th_Check((Rectangle){x + w - 150, vr.y, 150, TH_ROW_H}, "One colour", &monoV, fimp.rgba ? 0 : TH_CK_OFF, "if.mono")) { fimp.so.mono = monoV; again = true; }
    }
    if (again && fimp.rgba) { ImpSheetAnalyze(); can = !fimp.err[0] && sh->ncells > 0; }
    char l1[240] = "", l2[240] = "";
    if (!can) snprintf(l1, sizeof l1, "Cannot import: %.180s", fimp.err[0] ? fimp.err : "no glyph found");
    else {
        snprintf(l1, sizeof l1, "Found %s: %d glyphs in %d text row%s%s%s.", sh->layout == SHEET_GRID ? TextFormat("a grid of %d x %d cells", sh->cellW, sh->cellH) : sh->layout == SHEET_ROWS ? "rows of glyphs" : "a strip with a marker row",
                 sh->glyphs, sh->nrows, sh->nrows == 1 ? "" : "s", sh->scale > 1 ? TextFormat(", enlarged %d times", sh->scale) : "", sh->blocks > 1 ? TextFormat(", %d copies of the font", sh->blocks) : "");
        snprintf(l2, sizeof l2, "Font box %d + %d px, %d colour%s; the characters were %s.", sh->ascent, sh->descent, sh->npalette, sh->npalette == 1 ? "" : "s",
                 sh->recognised ? TextFormat("recognised (%d %% sure)", (int)lroundf(fminf(1.0f, sh->confidence / 0.75f) * 100.0f)) : "counted from the first");
    }
    Th_TextSmall(l1, x, y, can ? TH_TEXT : TH_TEXT_NOTE); y += TH_LINE_H;
    Th_TextSmall(l2, x, y, TH_TEXT_DIM); y += TH_LINE_H;
    Th_TextSmall(can ? "Each glyph is shown with the character it will get; check the order before importing." : "", x, y, TH_TEXT_DIM); y += TH_LINE_H + 8;
    /* the preview: the glyphs in their colours, each over its character */
    Rectangle pv = {x, y, w, 150};
    Th_Reg("if.preview", pv);
    DrawRectangleRec(pv, TH_PANEL_ALT);
    DrawRectangleLinesEx(pv, 1, TH_LINE);
    if (can) {
        int boxH = imax_(1, sh->ascent + sh->descent), z = boxH <= 12 ? 2 : 1;
        float lineH = (float)(boxH * z) + 20, px0 = pv.x + 8, py = pv.y + 6, cx = px0;
        while (z > 1 && lineH * 3 > pv.height) { z--; lineH = (float)(boxH * z) + 20; }
        bool mono = sh->npalette <= 1;
        BeginScissorMode((int)pv.x + 1, (int)pv.y + 1, (int)pv.width - 2, (int)pv.height - 2);
        for (int i = 0; i < sh->ncells && py < pv.y + pv.height; i++) {
            const SheetCell *c = &sh->cells[i];
            if (!c->iw) continue;
            float cwd = (float)imax_(c->iw * z, 12) + 6;
            if (cx + cwd > pv.x + pv.width - 6) { cx = px0; py += lineH; }
            for (int yy = 0; yy < c->ih; yy++)
                for (int xx = 0; xx < c->iw; xx++) {
                    size_t p = (size_t)(c->iy + yy) * (size_t)sh->w + (size_t)(c->ix + xx);
                    if (!sh->ink[p]) continue;
                    Color col = mono ? TH_TEXT : Th_FromRGBA(sh->px[p] | 0xFFu);
                    DrawRectangle((int)cx + xx * z, (int)py + (c->iy - c->y - sh->lineTop + yy) * z, z, z, col);
                }
            if (c->code >= 0) Th_CodeDraw(c->code, (Rectangle){cx - 3, py + (float)(boxH * z) + 1, cwd, 16}, c->score < 0.45f && sh->recognised ? TH_TEXT_NOTE : TH_TEXT_DIM, false, 1);
            else Th_TextSmall("?", cx + 2, py + (float)(boxH * z) + 2, TH_TEXT_NOTE);
            cx += cwd;
        }
        EndScissorMode();
    }
    {
        bool tfv = fimp.typeface;
        if (Th_Check((Rectangle){x, pr.y + pr.height - pad - TH_ROW_H, 300, TH_ROW_H}, "Make a typeface with these names", &tfv, can ? 0 : TH_CK_OFF, "if.typeface")) fimp.typeface = tfv;
    }
    y = pr.y + pr.height - pad - TH_ROW_H;
    float bx = pr.x + pr.width - pad - 96;
    bool create = Th_Button((Rectangle){bx, y, 96, TH_ROW_H}, "Import", TH_BTN_PRIMARY | TH_BTN_OFF(can), NULL) && !fresh;
    bool cancel = Th_Button((Rectangle){bx - 8 - 96, y, 96, TH_ROW_H}, "Cancel", 0, NULL) && !fresh;
    if (!fresh && IsKeyPressed(KEY_ESCAPE)) cancel = true;
    if (!fresh && !Th_TextIs(NULL) && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && can) create = true;
    if (cancel) ImpFree();
    else if (create && !ImportSheetCreate()) ImpFree();
}

static void DrawImportFontDialog(int W, int H)
{
    if (!fimp.open) return;
    if (fimp.sheet) { DrawImportSheetDialog(W, H); return; }
    DrawRectangle(0, 0, W, H, TH_SCRIM);
    uiBlocked = false;
    Th_SetInputBlocked(false);
    bool fresh = fimp.frame == frameNo;
    const float pw = 620, pad = 24;
    float ph = pad + TH_SIZE_HEAD + TH_PAD + 22 + 3 * (TH_ROW_H + TH_GAP / 2) + TH_ROW_H + 8 + 3 * TH_LINE_H + 8 + 96 + TH_PAD + TH_ROW_H + 6 + TH_PAD + TH_ROW_H + pad;
    Rectangle pr = {Fl((W - pw) / 2), Fl((H - ph) / 2.5f), pw, ph};
    Th_Raised(pr, 1);
    float x = pr.x + pad, w = pr.width - 2 * pad, y = pr.y + pad;
    Th_TextHead("Import Font File", x, y, TH_TEXT);
    y += TH_SIZE_HEAD + TH_PAD;
    char fit[PATH_MAX_LEN + 16];
    Th_FitText(fimp.path, TH_F_TAIL, w, fit, sizeof fit);
    Th_TextSmall(fit, x, y, TH_TEXT_DIM);
    y += 22;
    bool can = fimp.probe && !fimp.err[0];
    Rectangle vr = Th_Row(x, &y, w, "Family", NULL, 0);
    if (Th_TextField((Rectangle){vr.x, vr.y, vr.width, TH_ROW_H}, "if.family", fimp.family, (int)sizeof fimp.family, can ? 0 : TH_TF_DISABLED, "Family") & TH_TF_CHANGED) snprintf(fimp.family, sizeof fimp.family, "%s", Th_TextValue());
    vr = Th_Row(x, &y, w, "Style", NULL, 0);
    if (Th_TextField((Rectangle){vr.x, vr.y, vr.width, TH_ROW_H}, "if.style", fimp.style, (int)sizeof fimp.style, can ? 0 : TH_TF_DISABLED, "Regular") & TH_TF_CHANGED) snprintf(fimp.style, sizeof fimp.style, "%s", Th_TextValue());
    int nfaces = fimp.probe && fimp.probe->state == FL_OK ? fimp.probe->nFaces : 1;
    vr = Th_Row(x, &y, w, fimp.probe && !fimp.outline ? "Strike" : "Face", NULL, 0);
    {
        int nv;
        if (Th_Number((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "if.face", fimp.face + 1, 1, nfaces > 1 ? nfaces : 1, nfaces > 1 ? 0 : TH_TF_DISABLED, &nv) && nv - 1 != fimp.face) {
            char keep[PATH_MAX_LEN];
            snprintf(keep, sizeof keep, "%s", fimp.path);
            ImpAnalyze(keep, nv - 1);
            fimp.open = true;
            fimp.frame = frameNo - 1;
            can = fimp.probe && !fimp.err[0];
        }
        char ft[64];
        snprintf(ft, sizeof ft, "of %d", nfaces > 1 ? nfaces : 1);
        Th_Text(ft, vr.x + 96 + 10, Fl(vr.y + (TH_ROW_H - Th_Measure(ft).y) / 2), TH_TEXT_DIM);
    }
    /* pixel font or not */
    bool canPixel = can && fimp.outline && fimp.hasGrid, canSmooth = can && fimp.outline && fimp.hasSmooth;
    {
        bool px = fimp.pixel;
        int off = (can && fimp.outline && canPixel && canSmooth) ? 0 : TH_CK_OFF;
        if (Th_Switch((Rectangle){x, y, w, TH_ROW_H}, "This is a pixel font (its pixels are drawn as outlines or stored as a bitmap)", &px, TH_CK_RIGHT | off, "if.pixel")) fimp.pixel = px;
        y += TH_ROW_H + 8;
    }
    char l1[200] = "", l2[200] = "", l3[200] = "";
    if (!can) snprintf(l1, sizeof l1, "Cannot import: %.160s", fimp.err[0] ? fimp.err : "unknown error");
    else if (!fimp.outline) {
        BmStrikeInfo si;
        memset(&si, 0, sizeof si);
        bm_strike_info(fimp.probe->bm, fimp.probe->strike, &si);
        snprintf(l1, sizeof l1, "A bitmap font: %d glyphs, %d px high. Its pixels are copied as they are.", si.glyph_count, si.ascent + si.descent > 0 ? si.ascent + si.descent : si.pixel_size);
        snprintf(l2, sizeof l2, "The file stays under the font as a layer.");
    } else {
        FitParams p;
        int a, d;
        ImpParams(&p);
        fit_box(fimp.probe->ol, &p, &a, &d);
        if (fimp.pixel) {
            snprintf(l1, sizeof l1, fimp.grid.sure ? "Pixel grid found (%s): a pixel is %.5g x %.5g font units." : "No sure pixel grid; the best guess (%s) has pixels of %.5g x %.5g font units.",
                     fimp.grid.sure ? fimp.grid.how : TextFormat("%.0f %% of the ink", fimp.grid.match * 100), fimp.grid.x, fimp.grid.y);
            snprintf(l2, sizeof l2, "Size %.5g px per em, font box %d + %d px%s.", (double)p.em, a, d, fimp.grid.perGlyph ? ", columns set per glyph" : "");
        } else {
            snprintf(l1, sizeof l1, fimp.hasGrid && fimp.grid.sure ? "Drawn as a smooth font although a pixel grid was found." : "No pixel grid in the outlines: the best small size was searched.");
            snprintf(l2, sizeof l2, "Size %.5g px per em, font box %d + %d px; %d of %d sample glyphs keep their parts and holes.", (double)p.em, a, d,
                     (int)lroundf(fimp.rep.topo * (float)fimp.rep.probes), fimp.rep.probes);
        }
        snprintf(l3, sizeof l3, "The file stays under the font as a layer: its size and position can be changed later, the glyphs follow.");
    }
    Th_TextSmall(l1, x, y, can ? TH_TEXT : TH_TEXT_NOTE); y += TH_LINE_H;
    Th_TextSmall(l2, x, y, TH_TEXT_DIM); y += TH_LINE_H;
    Th_TextSmall(l3, x, y, TH_TEXT_DIM); y += TH_LINE_H + 8;
    Rectangle pv = {x, y, w, 96};
    Th_Reg("if.preview", pv);
    ImpPreview(pv);
    y += 96 + TH_PAD;
    if (can && fimp.outline && !fimp.pixel) {   /* the size may be chosen: the position and the threshold are searched for it */
        vr = Th_Row(x, &y, w, "Size (px/em)", NULL, 0);
        float em = fimp.smooth.em;
        if (FStep((Rectangle){vr.x, vr.y, 96, TH_ROW_H}, "if.size", &em, 0.5f, 4, 200, false)) {
            FitSizeOpts o = {0, 0, em};
            FitParams np;
            FitSizeReport nr;
            if (fit_find_size(fimp.probe->ol, &o, &np, &nr, NULL, 0) == 1) { fimp.smooth = np; fimp.rep = nr; }
        }
        Rectangle ar = {vr.x + 96 + 10, vr.y, Th_ButtonW("Best Size"), TH_ROW_H};
        Th_Reg("if.best", ar);
        if (Th_Button(ar, "Best Size", 0, "Search the best small size again")) {
            FitParams np;
            FitSizeReport nr;
            if (fit_find_size(fimp.probe->ol, NULL, &np, &nr, NULL, 0) == 1) { fimp.smooth = np; fimp.rep = nr; }
        }
    } else y += TH_ROW_H + TH_GAP / 2;
    y += 2;
    {
        bool tfv = fimp.typeface;
        if (Th_Check((Rectangle){x, pr.y + pr.height - pad - TH_ROW_H, 300, TH_ROW_H}, "Make a typeface with these names", &tfv, can ? 0 : TH_CK_OFF, "if.typeface")) fimp.typeface = tfv;
    }
    y = pr.y + pr.height - pad - TH_ROW_H;
    bool create = false, cancel = false;
    float bx = pr.x + pr.width - pad - 96;
    create = Th_Button((Rectangle){bx, y, 96, TH_ROW_H}, "Import", TH_BTN_PRIMARY | TH_BTN_OFF(can), NULL) && !fresh;
    cancel = Th_Button((Rectangle){bx - 8 - 96, y, 96, TH_ROW_H}, "Cancel", 0, NULL) && !fresh;
    if (!fresh && IsKeyPressed(KEY_ESCAPE)) cancel = true;
    if (!fresh && !Th_TextIs(NULL) && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && can) create = true;
    if (cancel) ImpFree();
    else if (create) { if (!ImportFontCreate()) { if (fimp.open) fimp.open = false; ImpFree(); } }
}
