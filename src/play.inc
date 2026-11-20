/* play.inc - scripted input for end-to-end tests (hidden flag --play <script.txt>), included by main.c.
 *
 * The runner turns one script command at a time into per-frame raylib automation events
 * (PlayAutomationEvent: key, mouse button, mouse position, wheel, window close), so the app is exercised through
 * its real input path. Text typing goes through the Play_GetChar shim (raylib has no automation event for chars).
 * Widgets are addressed by the names they register each frame (Th_Reg / Th_RegLabel), as @Name.
 * See tests/e2e/README.md for the script language. */

#undef GetCharPressed
int GetCharPressed(void); /* the raylib function (the e2e build renames the declaration of raylib.h to Play_GetChar) */

/* AutomationEventType values of raylib 6 (the enum is private to rcore.c). */
enum { PEV_KEY_UP = 1, PEV_KEY_DOWN = 2, PEV_MOUSE_UP = 5, PEV_MOUSE_DOWN = 6, PEV_MOUSE_POS = 7, PEV_WHEEL = 8, PEV_CLOSE = 18 };

typedef struct PEv { int type, p0, p1; } PEv;
typedef struct PFrame {
    PEv ev[16]; int n; char *chars; char *shot;
    bool chk; int cx, cy, line; unsigned want; char desc[420]; /* deferred screen pixel check, run after the frame is drawn */
} PFrame;

static struct {
    bool active, finished, expectQuit;
    char **lines; int nlines, cur, lineNo;
    int revealLine, revealStep, scrollLine; bool retry;   /* a target in a panel tab not shown: the tabs tried for this line, and the line runs again */
    PFrame *q; int qn, qhead, qcap;
    int fails, passes;
    int *chq; int chn, chhead, chcap;
    int mx, my;
    char script[512];
    Rectangle gridArea;
    int fps;
    double sleepUntil;
    bool waiting; char waitName[64], waitOp[8]; long waitVal; double waitUntil;   /* waitfor <name> <op> <value> [ms] */
} play;

int Play_GetChar(void)
{
    if (play.chhead < play.chn) return play.chq[play.chhead++];
    return GetCharPressed();
}

static void PlayLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void PlayLog(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vprintf(fmt, ap);
    __builtin_va_end(ap);
    printf("\n");
    fflush(stdout);
}

#define PLAY_FAIL(...) do { char m_[1200]; snprintf(m_, sizeof m_, __VA_ARGS__); play.fails++; PlayLog("FAIL %s:%d: %s", play.script, play.lineNo, m_); } while (0)
#define PLAY_PASS(...) do { char m_[1200]; snprintf(m_, sizeof m_, __VA_ARGS__); play.passes++; PlayLog("PASS %s:%d: %s", play.script, play.lineNo, m_); } while (0)

/* ---- frame queue ---- */
static PFrame *QNew(void)
{
    if (play.qn == play.qcap) {
        play.qcap = play.qcap ? play.qcap * 2 : 64;
        play.q = (PFrame *)realloc(play.q, sizeof(PFrame) * (size_t)play.qcap);
    }
    PFrame *f = &play.q[play.qn++];
    memset(f, 0, sizeof *f);
    return f;
}

static void QEv(PFrame *f, int type, int p0, int p1)
{
    if (f->n < 16) { f->ev[f->n].type = type; f->ev[f->n].p0 = p0; f->ev[f->n].p1 = p1; f->n++; }
}

typedef struct Mods { bool ctrl, shift, alt; } Mods;

static void QMods(PFrame *f, Mods m, bool down)
{
    int t = down ? PEV_KEY_DOWN : PEV_KEY_UP;
    if (m.ctrl) QEv(f, t, KEY_LEFT_CONTROL, 0);
    if (m.shift) QEv(f, t, KEY_LEFT_SHIFT, 0);
    if (m.alt) QEv(f, t, KEY_LEFT_ALT, 0);
}

static void QWait(int n) { for (int i = 0; i < n; i++) QNew(); }

static void QMove(int x, int y) { PFrame *f = QNew(); QEv(f, PEV_MOUSE_POS, x, y); }

static void QClick(int x, int y, int btn, Mods m)
{
    PFrame *f = QNew();
    QMods(f, m, true);
    QEv(f, PEV_MOUSE_POS, x, y);
    QEv(f, PEV_MOUSE_DOWN, btn, 0);
    f = QNew();
    QEv(f, PEV_MOUSE_UP, btn, 0);
    f = QNew();
    QMods(f, m, false);
}

static void QDrag(int x1, int y1, int x2, int y2, int btn, Mods m)
{
    const int steps = 8;
    PFrame *f = QNew();
    QMods(f, m, true);
    QEv(f, PEV_MOUSE_POS, x1, y1);
    QEv(f, PEV_MOUSE_DOWN, btn, 0);
    for (int i = 1; i <= steps; i++) {
        f = QNew();
        QEv(f, PEV_MOUSE_POS, x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps);
    }
    f = QNew();
    QEv(f, PEV_MOUSE_POS, x2, y2);
    f = QNew();
    QEv(f, PEV_MOUSE_UP, btn, 0);
    f = QNew();
    QMods(f, m, false);
}

/* ---- key names ---- */
static int KeyByName(const char *n)
{
    static const struct { const char *n; int k; } tab[] = {
        {"ENTER", KEY_ENTER}, {"RETURN", KEY_ENTER}, {"ESC", KEY_ESCAPE}, {"ESCAPE", KEY_ESCAPE}, {"DELETE", KEY_DELETE},
        {"DEL", KEY_DELETE}, {"BACKSPACE", KEY_BACKSPACE}, {"TAB", KEY_TAB}, {"SPACE", KEY_SPACE}, {"UP", KEY_UP},
        {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"PGUP", KEY_PAGE_UP}, {"PGDN", KEY_PAGE_DOWN},
        {"HOME", KEY_HOME}, {"END", KEY_END}, {"EQUAL", KEY_EQUAL}, {"PLUS", KEY_EQUAL}, {"MINUS", KEY_MINUS},
        {"KP_ADD", KEY_KP_ADD}, {"KP_SUBTRACT", KEY_KP_SUBTRACT}, {"SHIFT", KEY_LEFT_SHIFT}, {"CTRL", KEY_LEFT_CONTROL}, {"ALT", KEY_LEFT_ALT},
        {"BACK", KEY_BACKSPACE}, {"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3},
    };
    if (n[0] && !n[1]) {
        char c = n[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return KEY_ZERO + (c - '0');
    }
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        const char *a = tab[i].n, *b = n;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return tab[i].k;
    }
    return -1;
}

/* ---- helpers for commands ---- */
/* a code of a script: what the clone dialog takes (a character, U+0041, U+0041-1), #n for the n-th glyph without a code point,
 * default for the default glyph (px_code_parse_user) */
static bool PlayCode(const char *tok, PxCode *cp)
{
    return ParseCodepoint(tok, cp) || px_code_parse_user(tok, cp);
}

static int GlyphByTok(const char *t)
{
    PxCode cp;
    if (!PlayCode(t, &cp)) return -2;
    return px_font_find_code(&font, cp);
}

static bool CellCenter(const char *tok, float *x, float *y)
{
    int i = GlyphByTok(tok);
    if (i < 0) { PLAY_FAIL("no such glyph '%s'", tok); return false; }
    float cx = play.gridArea.x + lx[i] + lw[i] / 2, cy = play.gridArea.y + ly[i] - scrollY + rowH / 2;
    if (cy < play.gridArea.y || cy > play.gridArea.y + play.gridArea.height) { PLAY_FAIL("cell '%s' is scrolled out of view (use reveal)", tok); return false; }
    *x = cx; *y = cy;
    return true;
}

/* A widget that is not drawn may be in a tab of the right panel that is not shown: the player opens the panel's tabs one after the other (as a
 * user looks for a command) and runs the line again.  False when every tab of this view was tried for this line. */
static bool RevealTab(void)
{
    static const char *fontTabs[] = {"ptab_Font", "ptab_Glyph", "ptab_Layers", NULL}, *glyphTabs[] = {"gtab_Glyph", "gtab_Layers", "gtab_Kerning", NULL},
                      *libTabs[] = {"bside_Font", "bside_Typeface", NULL};
    const char **tabs = inFinder ? NULL : inBrowser ? libTabs : !hasFont ? NULL : inEditor ? glyphTabs : fontTabs;
    if (play.revealLine != play.lineNo) { play.revealLine = play.lineNo; play.revealStep = 0; }
    Rectangle tr;
    if (tabs && play.revealStep == 0 && !Th_SectionOpen("VIEW") && Th_RegFind("VIEW_Fold", &tr)) {   /* a folded View area is opened first */
        QClick((int)(tr.x + tr.width / 2), (int)(tr.y + tr.height / 2), MOUSE_BUTTON_LEFT, (Mods){false, false, false});
        QWait(3);
        play.revealStep = -1;   /* the tabs come next (the loop below starts at 0 again) */
        play.retry = true;
        play.cur = play.lineNo - 1;
        return true;
    }
    if (play.revealStep < 0) play.revealStep = 0;
    while (tabs && tabs[play.revealStep]) {
        const char *tn = tabs[play.revealStep++];
        if (!Th_RegFind(tn, &tr)) continue;
        QClick((int)(tr.x + tr.width / 2), (int)(tr.y + tr.height / 2), MOUSE_BUTTON_LEFT, (Mods){false, false, false});
        QWait(3);
        play.retry = true;
        play.cur = play.lineNo - 1;   /* the line runs again once the tab is shown */
        return true;
    }
    return false;
}

/* Resolve a target at tok[0] (@name or a number pair). Returns tokens used (0 on failure). */
static int Target(char **tok, int n, float *x, float *y)
{
    if (n < 1) return 0;
    if (tok[0][0] == '@') {
        const char *nm = tok[0] + 1;
        if (!strncmp(nm, "cell:", 5)) return CellCenter(nm + 5, x, y) ? 1 : 0;
        if (!strncmp(nm, "ecell:", 6)) {
            int cx, cy;
            Vector2 v;
            if (!inEditor || !ed || sscanf(nm + 6, "%d,%d", &cx, &cy) != 2 || !Editor_CellCenter(ed, cx, cy, &v)) { PLAY_FAIL("bad editor cell '%s'", tok[0]); return 0; }
            *x = v.x; *y = v.y;
            return 1;
        }
        if (!strncmp(nm, "cellorigin:", 11)) { /* pen origin x, top row y of the glyph box of a cell */
            int i = GlyphByTok(nm + 11);
            if (i < 0) { PLAY_FAIL("no such glyph '%s'", nm + 11); return 0; }
            *x = play.gridArea.x + lx[i] + CellPen(&font.glyphs[i], lw[i]);
            *y = play.gridArea.y + ly[i] - scrollY + CELL_PADY;
            return 1;
        }
        if (!strncmp(nm, "card:", 5)) {
            Vector2 v;
            if (!browser || !Browser_CardCenter(browser, atoi(nm + 5), &v)) { PLAY_FAIL("no browser card '%s'", tok[0]); return 0; }
            *x = v.x; *y = v.y;
            return 1;
        }
        if (!strncmp(nm, "hdr:", 4)) {   /* the header of the n-th typeface box of the browser's Typefaces tab */
            Vector2 v;
            if (!browser || !Browser_TypefaceHeaderCenter(browser, atoi(nm + 4), &v)) { PLAY_FAIL("no typeface header '%s'", tok[0]); return 0; }
            *x = v.x; *y = v.y;
            return 1;
        }
        if (!strncmp(nm, "fcell:", 6)) {   /* the n-th result cell of the glyph finder */
            Vector2 v;
            if (!finder || !Finder_CellCenter(finder, atoi(nm + 6), &v)) { PLAY_FAIL("no finder cell '%s'", tok[0]); return 0; }
            *x = v.x; *y = v.y;
            return 1;
        }
        if (!strcmp(nm, "eadvance")) {
            Vector2 v;
            if (!inEditor || !ed || !Editor_AdvanceHandle(ed, &v)) { PLAY_FAIL("editor is not open"); return 0; }
            *x = v.x; *y = v.y;
            return 1;
        }
        if (!strcmp(nm, "grid.empty")) { *x = play.gridArea.x + play.gridArea.width - 40; *y = play.gridArea.y + play.gridArea.height - 8; return 1; }
        if (!strcmp(nm, "grid.scrollbar")) { *x = play.gridArea.x + play.gridArea.width - 8; *y = play.gridArea.y + 40; return 1; }
        Rectangle r;
        if (!Th_RegFind(nm, &r) && !RevealTab()) { PLAY_FAIL("no widget named '%s'", tok[0]); return 0; }
        if (play.retry) return 0;
        if (play.scrollLine != play.lineNo && Th_ScrollInto(r)) {   /* below the fold of a scrolled panel: scroll it there, once a line */
            play.scrollLine = play.lineNo;
            QWait(2);
            play.retry = true;
            play.cur = play.lineNo - 1;
            return 0;
        }
        *x = r.x + r.width / 2; *y = r.y + r.height / 2;
        return 1;
    }
    if (n < 2) return 0;
    *x = (float)atof(tok[0]); *y = (float)atof(tok[1]);
    return 2;
}

static char *ReadWhole(const char *path, long *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    size_t r = fread(b, 1, (size_t)n, fp);
    b[r] = 0;
    fclose(fp);
    if (len) *len = (long)r;
    return b;
}

static bool FileHas(const char *path) { FILE *fp = fopen(path, "rb"); if (fp) fclose(fp); return fp != NULL; }

/* "\t" and "\n" escapes in script strings */
static void Unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (s[0] == 0x5C && s[1] == 't') { *o++ = '\t'; s++; }
        else if (s[0] == 0x5C && s[1] == 'n') { *o++ = '\n'; s++; }
        else if (s[0] == 0x5C && s[1] == 'r') { *o++ = '\r'; s++; }
        else *o++ = *s;
    }
    *o = 0;
}

/* Layer state (assert l.<what>[.N]): count, cur (index of the current glyph row), sel, pop (the Add Layer popover is open),
 * focus (a layer text field has the keyboard), fonts (fonts in the glyph cache), and per layer N (0 = top): vis, op (opacity %),
 * dx, dy, color (0xRRGGBB), same (follows the edited glyph), this (from this font), code (the code it shows, -1 none),
 * ink (inked pixels of its glyph, -1 = not in font). */
static bool LayerStat(const char *k, long *v)
{
    Layers *L = ed ? Editor_Layers(ed) : NULL;
    *v = -1;
    if (!L) return true;
    if (!strcmp(k, "count")) *v = Layers_Count(L);
    else if (!strcmp(k, "cur")) *v = Layers_CurrentIndex(L);
    else if (!strcmp(k, "sel")) *v = Layers_Selected(L);
    else if (!strcmp(k, "pop")) *v = Layers_PopoverOpen(L);
    else if (!strcmp(k, "focus")) *v = Th_TextIs(NULL);
    else if (!strcmp(k, "fonts")) *v = Layers_CachedFonts(L);
    else {
        const char *dot = strchr(k, '.');
        if (!dot) return false;
        int i = atoi(dot + 1);
        const Layer *a = Layers_Get(L, i);
        if (!a) { *v = -1; return true; }
        size_t n = (size_t)(dot - k);
        if (n == 3 && !strncmp(k, "vis", 3)) *v = a->visible;
        else if (n == 2 && !strncmp(k, "op", 2)) *v = a->opacity;
        else if (n == 2 && !strncmp(k, "dx", 2)) *v = a->dx;
        else if (n == 2 && !strncmp(k, "dy", 2)) *v = a->dy;
        else if (n == 5 && !strncmp(k, "color", 5)) *v = ((long)a->color.r << 16) | ((long)a->color.g << 8) | a->color.b;
        else if (n == 4 && !strncmp(k, "same", 4)) *v = a->sameCode;
        else if (n == 4 && !strncmp(k, "this", 4)) *v = a->pxfm[0] == 0;
        else if (n == 3 && !strncmp(k, "ink", 3)) *v = a->isCurrent ? -2 : Layers_FoundCount(L, i);
        else return false;
    }
    return true;
}

static bool GetInt(const char *name, long *v)
{
    if (!strcmp(name, "glyphs")) *v = hasFont ? font.count - (px_font_default_index(&font) >= 0) : 0;   /* without the default glyph */
    else if (!strcmp(name, "allglyphs")) *v = hasFont ? font.count : 0;
    else if (!strcmp(name, "defidx")) *v = hasFont ? px_font_default_index(&font) : -1;
    else if (!strcmp(name, "selected")) { RecountSel(); *v = selCount; }
    else if (!strcmp(name, "dirty") || !strcmp(name, "modified")) { EnsureHashes(); *v = modified; }
    else if (!strcmp(name, "unsaved") || !strcmp(name, "dbdirty")) *v = DbDirty();                  /* memory differs from the file, or the open font has edits not in the store */
    else if (!strcmp(name, "dbmodified")) *v = store && store_modified(store);                       /* the store's change counter differs from the saved one */
    else if (!strcmp(name, "editpending")) *v = EditPending();
    else if (!strcmp(name, "readonly")) *v = hasFont && readOnly;
    else if (!strcmp(name, "locked")) *v = hasFont && readOnly;                                            /* the font is locked */
    else if (!strcmp(name, "contlocked")) *v = hasCont && contLocked;                                      /* its typeface is locked */
    else if (!strcmp(name, "stored")) *v = hasFont && docStored;
    else if (!strcmp(name, "hascont")) *v = hasCont;
    else if (!strcmp(name, "variant")) *v = contVariant;
    else if (!strcmp(name, "xdopen")) *v = xd.open;                                                  /* the Export dialog */
    else if (!strcmp(name, "nfopen")) *v = nf.open;
    else if (!strcmp(name, "afopen")) *v = af.open;                                                  /* the Add Font dialog */
    else if (!strcmp(name, "afrows")) *v = af.nvis;                                                  /* its fonts that match the filter */                                                  /* the New Font dialog */
    else if (!strcmp(name, "ifopen")) *v = fimp.open;                                                /* the Import Font File dialog */
    else if (!strcmp(name, "ifsheet")) *v = fimp.sheet;                                              /* the dialog is of a picture (sheet.h) */
    else if (!strcmp(name, "iflayout")) *v = fimp.sheet ? fimp.sh.layout : 0;                        /* 1 grid, 2 rows, 3 marker row */
    else if (!strcmp(name, "ifcells")) *v = fimp.sheet ? fimp.sh.ncells : 0;
    else if (!strcmp(name, "ifglyphs")) *v = fimp.sheet ? fimp.sh.glyphs : 0;
    else if (!strcmp(name, "ifcellw")) *v = fimp.sheet ? fimp.sh.cellW : 0;
    else if (!strcmp(name, "ifcellh")) *v = fimp.sheet ? fimp.sh.cellH : 0;
    else if (!strcmp(name, "ifcolours")) *v = fimp.sheet ? fimp.sh.npalette : 0;
    else if (!strcmp(name, "ifblocks")) *v = fimp.sheet ? fimp.sh.blocks : 0;
    else if (!strcmp(name, "colors")) *v = hasFont ? px_font_colors(&font) : -1;                       /* COLORS of the open font (0: one colour) */
    else if (!strcmp(name, "symbolic")) *v = hasFont ? font.symbolic : -1;                              /* SYMBOLIC of the open font */
    else if (!strcmp(name, "palette")) { const uint32_t *l; int n = hasFont ? px_font_color_list(&font, &l) : 0; *v = hasFont && px_font_is_colour(&font) ? n : 0; }   /* colours of the open font's colour list, the ink chooser's (0: a one-colour font, black) */
    else if (!strcmp(name, "palettes")) *v = hasFont ? font.npals : 0;                                 /* entries of the open font's palette table (0: none, every glyph has the default) */
    else if (!strcmp(name, "epal") || !strcmp(name, "ecolors")) {                                    /* the edited glyph: entries of its palette / colours its pixels use */
        int gi = inEditor && ed ? Editor_GlyphIndex(ed) : -1;
        if (gi < 0 || gi >= font.count) *v = -1;
        else if (name[1] == 'p') *v = px_glyph_palette(&font, &font.glyphs[gi])->n;
        else { char h[33]; px_glyph_hash(&font.glyphs[gi], h); *v = font.glyphs[gi].ncol; }
    }
    else if (!strcmp(name, "ifpixel")) *v = fimp.pixel;                                              /* its choice: a pixel font */
    else if (!strcmp(name, "ifgrid")) *v = fimp.hasGrid ? (fimp.grid.sure ? 1 : 2) : 0;              /* a grid was found (2: only a guess) */
    else if (!strcmp(name, "ifoutline")) *v = fimp.outline;
    else if (!strcmp(name, "ifem100")) { FitParams p; memset(&p, 0, sizeof p); if (fimp.probe && fimp.outline && !fimp.err[0]) ImpParams(&p); *v = (long)lroundf(p.em * 100.0f); }
    else if (!strcmp(name, "fitfollow") || !strcmp(name, "fitkept") || !strcmp(name, "fitabsent")) {   /* glyphs that are what the follow layer draws / edited by hand / not in its file */
        FlLayer *l = FollowLayer();
        if (!l) *v = -1;
        else { FitStatsUpdate(l); *v = name[3] == 'f' ? fitStats.follow : name[3] == 'k' ? fitStats.kept : fitStats.absent; }
    }
    else if (!strcmp(name, "fitchanged")) *v = fitLastChanged;                                       /* glyphs the last redraw changed */
    else if (!strcmp(name, "fitlastkept")) *v = fitLastKept;                                         /* glyphs the last redraw left alone (edited by hand) */
    else if (!strcmp(name, "docnew")) *v = docIsNew;                                                 /* the open font was made by New Font */
    else if (!strcmp(name, "flayers")) *v = flDoc ? flDoc->n : 0;                                    /* font layers of the open font */
    else if (!strcmp(name, "fsdirty")) *v = FsDirty();                                               /* the settings differ from the sidecar file */
    else if (!strcmp(name, "fspending")) *v = FlPending();                                           /* font layers not yet copied into the settings */
    else if (!strcmp(name, "fslayers")) { EnsureHashes(); *v = fsettings && hasFont ? fs_list_layers(fsettings, curHash, NULL, 0) : -1; }   /* layers the settings hold for the font's current hash */
    else if (!strcmp(name, "fshasany")) *v = fsettings && fs_has_any(fsettings);                     /* the settings hold any layer at all */
    else if (!strcmp(name, "flcache")) *v = FlCache_Count();                                         /* outline textures in the cache */
    else if (!strcmp(name, "xbackup")) *v = xd.backup;
    else if (!strcmp(name, "xfound_fonts")) *v = xd.scan.found.fonts;
    else if (!strcmp(name, "xfound_typefaces")) *v = xd.scan.found.typefaces;
    else if (!strcmp(name, "xfound_glyphs")) *v = xd.scan.found.glyphs;
    else if (!strcmp(name, "xw_fonts")) *v = job.xres.written.fonts;                                  /* the last export: written, skipped, deleted */
    else if (!strcmp(name, "xw_typefaces")) *v = job.xres.written.typefaces;
    else if (!strcmp(name, "xw_glyphs")) *v = job.xres.written.glyphs;
    else if (!strcmp(name, "xs_fonts")) *v = job.xres.skipped.fonts;
    else if (!strcmp(name, "xs_typefaces")) *v = job.xres.skipped.typefaces;
    else if (!strcmp(name, "xs_glyphs")) *v = job.xres.skipped.glyphs;
    else if (!strcmp(name, "xd_fonts")) *v = job.xres.deleted.fonts;
    else if (!strcmp(name, "xd_typefaces")) *v = job.xres.deleted.typefaces;
    else if (!strcmp(name, "xd_glyphs")) *v = job.xres.deleted.glyphs;
    else if (!strcmp(name, "xbackupfailed")) *v = job.backupFailed;
    else if (!strcmp(name, "zoom")) *v = zoom;
    else if (!strcmp(name, "boxabove")) { int a, b; px_font_box(&font, &a, &b); *v = a; }
    else if (!strcmp(name, "boxbelow")) { int a, b; px_font_box(&font, &a, &b); *v = b; }
    else if (!strcmp(name, "ratioa")) *v = font.ratioA;
    else if (!strcmp(name, "ratiob")) *v = font.ratioB;
    else if (!strcmp(name, "hasratio")) *v = font.hasRatio;
    else if (!strcmp(name, "mono")) *v = font.monospaced;
    else if (!strcmp(name, "default")) *v = hasFont && px_font_default_index(&font) >= 0;
    else if (!strcmp(name, "kerncount")) *v = font.kernCount;
    else if (!strcmp(name, "undo")) *v = undoN;
    else if (!strcmp(name, "redo")) *v = redoN;
    else if (!strcmp(name, "editor")) *v = inEditor;
    else if (!strcmp(name, "baseshift")) *v = baseShift;          /* how many rows the baseline was moved up in all (down: negative) */
    else if (!strcmp(name, "inbrowser")) *v = inBrowser && !inFinder;
    else if (!strcmp(name, "nav")) *v = navView;                  /* 0 Typefaces, 1 Typeface, 2 Fonts, 3 Font, 4 Glyph, 5 Glyphs */
    else if (!strcmp(name, "navactive")) *v = navActive;          /* the blue segment: 0, 1 or 2 */
    else if (!strcmp(name, "intypeface")) *v = browser && Browser_InTypeface(browser);
    else if (!strcmp(name, "infinder")) *v = inFinder;
    else if (!strcmp(name, "infont")) *v = hasFont && !inBrowser && !inFinder;
    else if (!strcmp(name, "loading")) *v = job.active;
    else if (!strcmp(name, "hasstore")) *v = store != NULL;
    else if (!strcmp(name, "storeimported")) *v = store ? store_info(store)->imported : -1;     /* 1 when the database was just imported from a folder */
    else if (!strcmp(name, "storefonts")) *v = store ? store_info(store)->nPxfm : -1;
    else if (!strcmp(name, "storeglyphs")) *v = store ? store_info(store)->nGlyph : -1;
    else if (!strcmp(name, "storetypefaces")) *v = store ? store_info(store)->nPxtf : -1;
    else if (!strcmp(name, "uncoded")) *v = hasFont ? px_font_uncoded_count(&font) : 0;
    else if (!strcmp(name, "finder_count")) *v = finder ? Finder_ResultCount(finder) : -2;       /* -1 while loading */
    else if (!strcmp(name, "finder_code")) *v = finder ? Finder_Code(finder) : -2;
    else if (!strcmp(name, "usage")) *v = insight ? Insight_UsageFontCount(insight) : -2;        /* fonts holding the selected glyphs; -1 loading */
    else if (!strcmp(name, "usagevar")) *v = insight ? Insight_UsageVariantCount(insight) : -2;     /* typeface variants naming those fonts; -1 loading */
    else if (!strcmp(name, "usagecode")) *v = insight ? Insight_UsageCodeFonts(insight) : -2;       /* fonts that hold the glyph under the code of the selected glyph / the finder; -1 none */
    else if (!strcmp(name, "ptab")) *v = panelTab;                                                   /* tab of the font view's right panel: 0 Font, 1 Glyph, 2 Similar, 3 Layers, 4 View */
    else if (!strcmp(name, "fside")) *v = finder ? Finder_SideTab(finder) : -2;                        /* side panel of the Glyphs view: 0 View, 1 Usage */
    else if (!strcmp(name, "fzoom")) *v = finder ? Finder_Zoom(finder) : -2;
    else if (!strcmp(name, "fcellfonts")) *v = finder ? Finder_CellFonts(finder, 0) : -2;           /* the "N fonts" of the first finder cell */
    else if (!strcmp(name, "similar")) *v = insight ? Insight_SimilarCount(insight) : -2;        /* entries of the similar list; -1 loading */
    else if (!strcmp(name, "insightbusy")) *v = insight ? Insight_Busy(insight) : 0;
    else if (!strcmp(name, "filemenu") || !strcmp(name, "libmenu")) *v = fileMenu.open;
    else if (!strcmp(name, "opentime")) *v = (long)(job.seconds * 1000);                           /* ms of the last library open */
    else if (!strcmp(name, "bvisible")) *v = browser ? Browser_VisibleCount(browser) : -1;
    else if (!strcmp(name, "btotal")) *v = browser ? Browser_TotalCount(browser) : -1;
    else if (!strcmp(name, "bside")) *v = browser ? Browser_SideTab(browser) : -1;                  /* side panel of the browser: 0 View, 1 Similar, 2 Typeface, 3 Font */
    else if (!strcmp(name, "btab")) *v = browser ? Browser_Tab(browser) : -1;
    else if (!strcmp(name, "bzoom")) *v = browser ? Browser_Zoom(browser) : -1;
    else if (!strcmp(name, "bcols")) *v = browser ? Browser_Columns(browser) : -1;
    else if (!strcmp(name, "bsel")) *v = browser ? Browser_SelectedVisible(browser) : -1;
    else if (!strcmp(name, "btypefaces")) *v = browser ? Browser_TypefaceTotal(browser) : -1;     /* typefaces of the library (badge of the Typefaces tab) */
    else if (!strcmp(name, "bboxes")) *v = browser ? Browser_BoxCount(browser) : -1;               /* typeface boxes of the Typefaces tab */
    else if (!strcmp(name, "tfocus")) *v = Th_TextIs(NULL);                                         /* a text field has the keyboard */
    else if (!strcmp(name, "bcolors")) *v = browser ? Browser_ColorFilter(browser) : -1;             /* the Colours switch: 1 on */
    else if (!strncmp(name, "bboxvars", 8)) *v = browser ? Browser_BoxVariants(browser, atoi(name + 8)) : -1;   /* bboxvars0: variants shown in box 0 */
    else if (!strcmp(name, "bupd10") || !strcmp(name, "bupdmax10") || !strcmp(name, "bfr10") || !strcmp(name, "bfrmax10")) {   /* browser frame times, tenths of ms */
        float ua = 0, um = 0, fa = 0, fm = 0;
        if (browser) Browser_FrameStats(browser, &ua, &um, &fa, &fm);
        *v = (long)((!strcmp(name, "bupd10") ? ua : !strcmp(name, "bupdmax10") ? um : !strcmp(name, "bfr10") ? fa : fm) * 10.0f + 0.5f);
    }
    else if (!strncmp(name, "bink", 4)) *v = browser ? Browser_CardInkLines(browser, atoi(name + 4)) : -1;   /* bink0: lines of the sample of visible entry 0 that show ink */
    else if (!strcmp(name, "bselhdr")) *v = browser && Browser_SelectedIsHeader(browser);        /* a box header (the typeface) is selected */
    else if (!strcmp(name, "blast")) *v = browser ? Browser_LastAction(browser) : -1;            /* BrowserAction of the last choice the browser returned */
    else if (!strcmp(name, "samplelines")) *v = Sample_LineCount(Sample_Get());
    else if (!strcmp(name, "samplefocus")) *v = Sample_TextFocused();
    else if (!strcmp(name, "stripzoom")) *v = SampleStrip_Zoom();
    else if (!strcmp(name, "striph")) *v = SampleStrip_Setting();
    else if (!strcmp(name, "stripcollapsed")) *v = SampleStrip_Collapsed();
    else if (!strcmp(name, "bpending")) *v = browser ? Browser_Pending(browser) : -1;
    else if (!strcmp(name, "editor_default")) *v = inEditor && ed && Editor_GlyphIndex(ed) < font.count && font.glyphs[Editor_GlyphIndex(ed)].code == PX_CODE_DEFAULT;
    else if (!strncmp(name, "l.", 2)) return LayerStat(name + 2, v);
    else if (!strncmp(name, "w.", 2)) { Rectangle wr; *v = Th_RegFind(name + 2, &wr); }          /* assert w.<widget> 0|1: a widget of that name was drawn in the last frame */
    else if (!strcmp(name, "gtab")) *v = glyphTab;                                                   /* tab of the glyph view's right panel: 0 Glyph, 2 Layers, 3 View, 4 Kerning */
    else if (!strcmp(name, "emetrics")) *v = ed ? Editor_ShowMetrics(ed) : -1;                      /* metric lines of the glyph view on */
    else if (!strcmp(name, "ehidden")) *v = ed ? Editor_CurrentHidden(ed) : -1;                     /* the edited glyph's layer is hidden */
    else if (!strcmp(name, "etool")) *v = ed ? Editor_Tool(ed) : -1;
    else if (!strncmp(name, "etoolslot", 9)) *v = Editor_StripTool(atoi(name + 9));                    /* etoolslot3: the tool number of the strip's 4th button, in the strip's order */
    else if (!strcmp(name, "etoolcols")) *v = ed ? Editor_ToolCols(ed) : -1;
    else if (!strcmp(name, "eink")) *v = ed ? Editor_Ink(ed) : -1;                                    /* '.' the transparent ink, else the ink's slot in the edited glyph as a pixel character (its key), '?' when the glyph has not got it */
    else if (!strcmp(name, "einkrgb")) *v = ed ? (long)(Editor_InkColor(ed) >> 8) : -1;               /* the ink colour as 0xRRGGBB (the alpha left out) */
    else if (!strcmp(name, "ebarrows")) *v = ed ? Editor_BarRows(ed) : -1;                           /* rows of the palette bar: 1 + the rows of the font's other colours */
    else if (!strcmp(name, "ebarextras")) *v = ed ? Editor_BarExtras(ed) : -1;                       /* tiles of the font's other colours in the palette bar */
    else if (!strcmp(name, "eunused") || !strncmp(name, "eslotof:", 8) || !strncmp(name, "eslotused:", 10)) {   /* the edited glyph's slots: unused ones; the slot of a colour (-1); slot N used */
        int gi = inEditor && ed ? Editor_GlyphIndex(ed) : -1;
        if (gi < 0 || gi >= font.count) *v = -1;
        else {
            const PxGlyph *g = &font.glyphs[gi];
            const PxPalette *p = px_glyph_palette(&font, g);
            int n = p->n < PX_GLYPH_MAX_COLORS ? p->n : PX_GLYPH_MAX_COLORS, k = px_glyph_used_colors(g);
            if (!(g->pi > 0) && !g->pix) k = 0;
            if (name[1] == 'u') *v = n - k;
            else if (name[5] == 'o') { uint32_t c = (uint32_t)strtoul(name + 8, NULL, 16); if (strlen(name + 8) <= 6) c = c << 8 | 0xFFu; *v = -1; for (int i = 0; i < n; i++) if (p->c[i] == c) *v = i; }
            else { int i = atoi(name + 10); *v = i < 0 || i >= n ? -1 : i < k; }
        }
    }
    else if (!strcmp(name, "epicker")) *v = Th_ColorPickIsOpen();                                   /* the colour picker is open */
    else if (!strcmp(name, "epickermode")) *v = Th_ColorPickMode();                                 /* its model: 0 HSV, 1 Wheel, 2 RGB, 3 HSL, 4 Lab */
    else if (!strcmp(name, "epickerrgb")) *v = (long)(Th_ColorPickValue() >> 8);                    /* the colour it shows, 0xRRGGBB */
    else if (!strcmp(name, "epickera")) *v = (long)(Th_ColorPickValue() & 0xFFu);                   /* and its alpha */
    else if (!strcmp(name, "ezoom")) *v = ed ? Editor_Zoom(ed) : -1;
    else if (!strcmp(name, "ekern")) *v = ed ? Editor_Kerning(ed) : -1;                              /* the glyph view's Kerning is on */
    else if (!strcmp(name, "eselcount")) *v = ed ? Editor_SelCount(ed) : -1;                        /* cells selected in the glyph view */
    else if (!strcmp(name, "efloating")) *v = ed ? Editor_Floating(ed) : -1;                        /* selected pixels are lifted and float over the glyph */
    else if (!strcmp(name, "epp")) *v = ed ? Editor_PixelPerfect(ed) : -1;                           /* the glyph view's Pixel-perfect option is on */
    else if (!strcmp(name, "efilled")) *v = ed ? Editor_Filled(ed) : -1;                             /* the bar's Filled option of the rectangle and the ellipse is on */
    else if (!strcmp(name, "ecurve")) *v = ed ? Editor_CurveStage(ed) : -1;                         /* the curve being made: 0 none, 1 end points set, 2 first control point set */
    else if (!strcmp(name, "epoly")) *v = ed ? Editor_PolyVertices(ed) : -1;                         /* the polygon being made: its vertices (0 none) */
    else if (!strcmp(name, "econtig")) *v = ed ? Editor_Contiguous(ed) : -1;                        /* the bar's Contiguous option of the fill is on */
    else if (!strcmp(name, "eclip")) *v = Editor_ClipCount();                                       /* cells on the glyph view's clipboard */
    else if (!strcmp(name, "scroll")) *v = (long)scrollY;
    else if (!strcmp(name, "panelscroll")) *v = (long)panelScroll;
    else if (!strcmp(name, "toasts")) *v = Th_ToastCount();
    else if (!strcmp(name, "menuhi")) *v = Th_MenuHi();
    else if (!strcmp(name, "hashlen")) *v = (long)strlen(curHash);
    else if (!strncmp(name, "m.", 2) || !strncmp(name, "has.", 4)) {
        const char *k = name[0] == 'm' ? name + 2 : name + 4;
        for (int m = 0; m < PXM_COUNT; m++) {
            char low[32];
            LowerKey(px_metric_key((PxMetric)m), low, sizeof low);
            if (!strcmp(low, k)) { *v = name[0] == 'm' ? font.m[m] : font.has[m]; return true; }
        }
        return false;
    } else if (!strcmp(name, "c.italic") || !strcmp(name, "c.underlined") || !strcmp(name, "c.struckout")) {
        PxtfVariant *cv = hasCont ? pxtf_find_variant(&cont, contVariant) : NULL;
        *v = !cv ? -1 : (name[2] == 'i' ? cv->italic : (name[2] == 'u' ? cv->underlined : cv->struckout));
    }
    else return false;
    return true;
}

/* The stats whose value is a glyph code (compared with = or != against a code of PlayCode, 'c for a character, none):
 * eglyph and editor_glyph, the code of the glyph the glyph view shows; l.code.N, the code layer N draws. */
static bool CodeStat(const char *name, PxCode *c)
{
    *c = PX_NO_CODE;
    if (!strcmp(name, "eglyph") || !strcmp(name, "editor_glyph")) {
        if (inEditor && ed && Editor_GlyphIndex(ed) >= 0 && Editor_GlyphIndex(ed) < font.count) *c = font.glyphs[Editor_GlyphIndex(ed)].code;
        return true;
    }
    if (!strncmp(name, "l.code.", 7)) {
        Layers *L = ed ? Editor_Layers(ed) : NULL;
        const Layer *a = L ? Layers_Get(L, atoi(name + 7)) : NULL;
        if (a && ed && Editor_GlyphIndex(ed) >= 0 && Editor_GlyphIndex(ed) < font.count)
            *c = a->isCurrent || a->sameCode ? font.glyphs[Editor_GlyphIndex(ed)].code : a->code;
        return true;
    }
    return false;
}

static bool Compare(long a, const char *op, long b)
{
    if (!strcmp(op, "=") || !strcmp(op, "==")) return a == b;
    if (!strcmp(op, "!=")) return a != b;
    if (!strcmp(op, ">")) return a > b;
    if (!strcmp(op, "<")) return a < b;
    if (!strcmp(op, ">=")) return a >= b;
    if (!strcmp(op, "<=")) return a <= b;
    return false;
}

/* verifylib <pxfm> <libroot> [glyphs N]: reads with the core reader, checks cropping, hash naming and pxgl files */
static void VerifyLib(const char *pxfm, const char *root, int expect)
{
    PxFont f;
    char err[PX_ERR_LEN] = "";
    px_font_init(&f);
    if (!px_read_pxfm(pxfm, root, &f, err)) { PLAY_FAIL("core reader rejects %s: %s", pxfm, err); return; }
    int bad = 0;
    for (int i = 0; i < f.count; i++) {
        PxGlyph c;
        if (!f.glyphs[i].pix) continue;
        px_glyph_copy(&c, &f.glyphs[i]);
        px_glyph_crop(&c);
        if (!f.glyphs[i].asDrawn && (c.w != f.glyphs[i].w || c.h != f.glyphs[i].h)) bad++;
        px_glyph_free(&c);
        char h[33], path[PATH_MAX_LEN + 64];
        px_glyph_hash(&f.glyphs[i], h);
        snprintf(path, sizeof path, "%s/pxgl/%c%c/%s.pxgl", root, h[0], h[1], h);
        if (!FileHas(path)) bad++;
    }
    char hash[33] = "";
    size_t n;
    char *txt = px_pxfm_text(&f, &n, hash);
    free(txt);
    const char *base = BaseName(pxfm);
    bool hashName = strlen(base) == 37 && strcmp(base + 32, ".pxfm") == 0;
    bool named = !hashName || strncmp(base, hash, 32) == 0;
    if (bad) PLAY_FAIL("%d uncropped or missing pxgl in %s", bad, pxfm);
    else if (!named) PLAY_FAIL("%s is not named by its content hash %s", base, hash);
    else if (expect >= 0 && f.count != expect) PLAY_FAIL("%s has %d glyphs, expected %d", base, f.count, expect);
    else PLAY_PASS("verifylib %s (%d glyphs, cropped, hash-named)", base, f.count);
    px_font_free(&f);
}

/* ---- directory snapshots: "originals untouched" checks (bytes and modification time of every library file) ---- */
typedef struct SnapFile { char *path; long mtime; char h[33]; } SnapFile;
static SnapFile *snapF = NULL;
static int snapN = 0;
static char snapRoot[PATH_MAX_LEN];

static int SnapScan(const char *dir, SnapFile **out)
{
    FilePathList l = LoadDirectoryFilesEx(dir, ".pxfm;.pxgl;.pxtf;.pxfd;.pxfdb", true);
    SnapFile *a = (SnapFile *)calloc((size_t)l.count + 1, sizeof(SnapFile));
    for (unsigned i = 0; i < l.count; i++) {
        long len = 0;
        char *b = ReadWhole(l.paths[i], &len);
        a[i].path = (char *)malloc(strlen(l.paths[i]) + 1);
        strcpy(a[i].path, l.paths[i]);
        a[i].mtime = GetFileModTime(l.paths[i]);
        px_hash128_hex(b ? b : "", (size_t)len, a[i].h, false);
        free(b);
    }
    int n = (int)l.count;
    UnloadDirectoryFiles(l);
    *out = a;
    return n;
}

static void SnapFree(SnapFile *a, int n) { for (int i = 0; i < n; i++) free(a[i].path); free(a); }

static void SnapTake(const char *dir)
{
    SnapFree(snapF, snapN);
    snprintf(snapRoot, sizeof snapRoot, "%s", dir);
    snapN = SnapScan(dir, &snapF);
}

/* "unchanged": every file of the snapshot is still there with the same bytes and time.  "added N": N more files. */
static bool SnapCheck(const char *how, int want)
{
    SnapFile *cur;
    int n = SnapScan(snapRoot, &cur);
    int bad = 0;
    for (int i = 0; i < snapN; i++) {
        bool found = false;
        for (int j = 0; j < n && !found; j++)
            if (!strcmp(snapF[i].path, cur[j].path)) { found = true; if (strcmp(snapF[i].h, cur[j].h) || snapF[i].mtime != cur[j].mtime) bad++; }
        if (!found) bad++;
    }
    int added = n - snapN;
    SnapFree(cur, n);
    if (!strcmp(how, "unchanged")) { if (bad) PlayLog("  %d files of the snapshot changed or vanished", bad); return bad == 0; }
    if (!strcmp(how, "added")) { if (added != want) PlayLog("  %d files added", added); return bad == 0 && added == want; }
    if (!strcmp(how, "addedmin")) { if (added < want) PlayLog("  %d files added", added); return bad == 0 && added >= want; }
    return false;
}

/* Fields of a font layer for `assert flayer <i> <field> <value>`: numbers (compared to 0.001) and texts (equal, or contained for source,
 * label and err) */
static bool FlField(FlLayer *fl, const char *f, double *num, const char **str)
{
    *str = NULL;
    FlLayer_Ready(fl);   /* the source is opened lazily: asking is a use */
    if (!strcmp(f, "kind")) *num = fl->s.kind;
    else if (!strcmp(f, "face")) *num = fl->s.face;
    else if (!strcmp(f, "visible")) *num = fl->s.visible;
    else if (!strcmp(f, "opacity")) *num = fl->s.opacity;
    else if (!strcmp(f, "dx")) *num = fl->s.dx;
    else if (!strcmp(f, "dy")) *num = fl->s.dy;
    else if (!strcmp(f, "scale")) *num = fl->s.scale;
    else if (!strcmp(f, "em")) *num = fl->s.em;
    else if (!strcmp(f, "aspect")) *num = fl->s.aspect;
    else if (!strcmp(f, "follow")) *num = fl->s.follow;
    else if (!strcmp(f, "mode")) *num = fl->s.mode;
    else if (!strcmp(f, "threshold")) *num = fl->s.threshold;
    else if (!strcmp(f, "color")) *num = (fl->s.r << 16) | (fl->s.g << 8) | fl->s.b;
    else if (!strcmp(f, "size")) *num = (double)fl->s.fileSize;
    else if (!strcmp(f, "mtime")) *num = (double)fl->s.fileMtime;
    else if (!strcmp(f, "ncorr")) *num = fl->ncorr;
    else if (!strcmp(f, "nfaces")) *num = fl->nFaces;
    else if (!strcmp(f, "outline")) *num = fl->state == FL_OK && fl->outline;
    else if (!strcmp(f, "bitmap")) *num = fl->state == FL_OK && !fl->outline && fl->s.kind == 1;
    else if (!strcmp(f, "ok")) *num = fl->s.kind == 0 || fl->state == FL_OK;
    else if (!strcmp(f, "source")) *str = fl->s.source;
    else if (!strcmp(f, "label")) *str = fl->s.label;
    else if (!strcmp(f, "err")) *str = fl->err;
    else return false;
    return true;
}

static void DoAssert(char **t, int n)
{
    /* t[0] == "assert" */
    char line[400] = "";
    for (int i = 1; i < n; i++) { strncat(line, t[i], sizeof line - strlen(line) - 2); strncat(line, " ", sizeof line - strlen(line) - 1); }
    if (n < 3) { PLAY_FAIL("assert needs arguments"); return; }
    const char *k = t[1];
    bool ok = false;
    long v;
    PxCode cv;
    if (!strcmp(k, "pixel")) {
        /* assert pixel <target|x y> [dx dy] RRGGBB: checked on the screen after the next frame is drawn */
        float x, y;
        int u = Target(t + 2, n - 2, &x, &y);
        if (!u) return;
        int rest = n - 2 - u;
        int dx = 0, dy = 0;
        if (rest >= 3) { dx = atoi(t[2 + u]); dy = atoi(t[3 + u]); }
        if (rest < 1) { PLAY_FAIL("assert pixel needs a colour"); return; }
        PFrame *f = QNew();
        f->chk = true; f->cx = (int)x + dx; f->cy = (int)y + dy; f->line = play.lineNo;
        f->want = (unsigned)strtoul(t[n - 1], NULL, 16);
        snprintf(f->desc, sizeof f->desc, "assert %s", line);
        return;
    }
    if (!strcmp(k, "glyph")) {
        /* assert glyph U+0041 exists|pixel x y C (its own character)|color x y RRGGBB[AA]|none|advance V|left N|top N|size WxH|empty */
        if (n < 4) { PLAY_FAIL("assert glyph needs a property"); return; }
        int gi = GlyphByTok(t[2]);
        const char *p = t[3];
        if (!strcmp(p, "exists")) {
            bool want = n < 5 || atoi(t[4]) != 0;
            ok = (gi >= 0) == want;
        } else if (gi < 0) { PLAY_FAIL("no glyph %s", t[2]); return; }
        else {
            const PxGlyph *g = &font.glyphs[gi];
            int ab, be;
            px_font_box(&font, &ab, &be);
            if (!strcmp(p, "pixel") && n >= 7) { char c = px_glyph_get(g, atoi(t[4]) - g->x, atoi(t[5]) - px_glyph_box_row(g, ab)); ok = c == t[6][0]; if (!ok) { PlayLog("  actual pixel '%c'", c); } }
            else if (!strcmp(p, "advance") && n >= 5) { ok = g->advance == atoi(t[4]); if (!ok) PlayLog("  actual advance %d", g->advance); }
            else if (!strcmp(p, "x") && n >= 5) { ok = g->x == atoi(t[4]); if (!ok) PlayLog("  actual x %d", g->x); }
            else if (!strcmp(p, "y") && n >= 5) { ok = g->y == atoi(t[4]); if (!ok) PlayLog("  actual y %d", g->y); }
            else if (!strcmp(p, "left") && n >= 5) { ok = g->x == atoi(t[4]); if (!ok) PlayLog("  actual left %d", g->x); }
            else if (!strcmp(p, "top") && n >= 5) { ok = px_glyph_box_row(g, ab) == atoi(t[4]); if (!ok) PlayLog("  actual top (box row) %d", px_glyph_box_row(g, ab)); }
            else if (!strcmp(p, "hash") && n >= 5) { char h[33]; px_glyph_hash(g, h); ok = !strcmp(h, t[4]) || (!strcmp(t[4], "none") && !h[0]); if (!ok) PlayLog("  actual glyph hash %s", h); }
            else if (!strcmp(p, "origchanged") && n >= 5) { const char *oh = OrigGlyphHash(g->code); char h[33]; px_glyph_hash(g, h); ok = (oh && strcmp(oh, h) != 0) == (atoi(t[4]) != 0); }
            else if (!strcmp(p, "size") && n >= 5) { int w = 0, h = 0; sscanf(t[4], "%dx%d", &w, &h); ok = g->w == w && g->h == h; if (!ok) PlayLog("  actual size %dx%d", g->w, g->h); }
            else if (!strcmp(p, "empty")) ok = g->w == 0;
            else if (!strcmp(p, "color") && n >= 7) {   /* assert glyph U+0041 color x y RRGGBB|RRGGBBAA|none: the colour of the pixel (none: '.') */
                char c = px_glyph_get(g, atoi(t[4]) - g->x, atoi(t[5]) - px_glyph_box_row(g, ab));
                uint32_t have = c == '.' ? 0 : px_glyph_color(&font, g, c), want = (uint32_t)strtoul(t[6], NULL, 16);
                if (strlen(t[6]) == 6) want = want << 8 | 0xFFu;
                if (!strcmp(t[6], "none")) want = 0;
                ok = c == '.' ? !strcmp(t[6], "none") : have == want;
                if (!ok) PlayLog("  actual colour %08X (pixel '%c')", (unsigned)have, c);
            }
            else if (!strcmp(p, "ink") && n >= 5) { int c = 0; for (int i = 0; i < g->w * g->h; i++) c += g->pix[i] != '.'; ok = c == atoi(t[4]); if (!ok) PlayLog("  actual ink %d", c); }
            else { PLAY_FAIL("bad glyph property '%s'", p); return; }
        }
    } else if (!strcmp(k, "esel")) {
        /* assert esel bounds X Y W H | cell X Y [0|1] | none: the glyph view's selection; X is a column from the pen, Y a row of the font box (0 = its top row) */
        int bx, by, bw, bh;
        if (!ed || n < 3) { PLAY_FAIL("assert esel bounds|cell|none"); return; }
        if (!strcmp(t[2], "none")) ok = Editor_SelCount(ed) == 0;
        else if (!strcmp(t[2], "bounds") && n >= 7) {
            ok = Editor_SelBounds(ed, &bx, &by, &bw, &bh) && (t[3][0] == '*' || bx == atoi(t[3])) && (t[4][0] == '*' || by == atoi(t[4])) && bw == atoi(t[5]) && bh == atoi(t[6]);   /* * matches any X or Y */
            if (!ok) { if (Editor_SelBounds(ed, &bx, &by, &bw, &bh)) PlayLog("  actual bounds %d %d %d %d", bx, by, bw, bh); else PlayLog("  nothing selected"); }
        } else if (!strcmp(t[2], "cell") && n >= 5) ok = Editor_SelCell(ed, atoi(t[3]), atoi(t[4])) == (n < 6 || atoi(t[5]) != 0);
        else { PLAY_FAIL("bad esel property '%s'", t[2]); return; }
    } else if (!strcmp(k, "sel")) {
        int gi = GlyphByTok(t[2]);
        RecountSel();
        ok = gi >= 0 && gi < selN && sel[gi] == (n < 4 || atoi(t[3]) != 0);
    } else if (!strcmp(k, "ctext")) {
        /* assert ctext KEY text...   (the text of the typeface variant; KEY is FAMILY, STYLE, ...; "-" = not stated) */
        PxtfVariant *cv = hasCont ? pxtf_find_variant(&cont, contVariant) : NULL;
        char want[300] = "";
        for (int i = 3; i < n; i++) { if (i > 3) strcat(want, " "); strncat(want, t[i], 200); }
        const char *have = NULL;
        for (int q = 0; cv && q < PXTF_TEXT_COUNT; q++) if (!strcmp(pxtf_text_key((PxtfText)q), t[2])) have = cv->text[q] ? cv->text[q] : "-";
        ok = have && !strcmp(have, want);
        if (!ok) PlayLog("  actual text '%s'", have ? have : "(none)");
    } else if (!strcmp(k, "navseg")) {
        /* assert navseg <0|1|2> <text>: the full text of a segment of the navigation bar */
        char want[300] = "";
        for (int i = 3; i < n; i++) { if (i > 3) strcat(want, " "); strncat(want, t[i], 200); }
        int sg = n > 2 ? atoi(t[2]) : -1;
        ok = sg >= 0 && sg < 3 && !strcmp(navText[sg], want);
        if (!ok) PlayLog("  actual segment '%s'", sg >= 0 && sg < 3 ? navText[sg] : "(none)");
    } else if (!strcmp(k, "ekernpartner")) {
        /* assert ekernpartner <code>|none: the partner glyph Kerning draws after the edited one */
        PxCode want = PX_NO_CODE, have = ed ? Editor_KernPartner(ed) : PX_NO_CODE;
        ok = n >= 3 && (!strcmp(t[2], "none") || PlayCode(t[2], &want)) && have == want;
        if (!ok) { char l[40] = "none"; if (have != PX_NO_CODE) px_code_label(have, l, sizeof l); PlayLog("  actual partner %s", l); }
    } else if (!strcmp(k, "kern")) {
        PxCode a, b;
        ok = n >= 5 && PlayCode(t[2], &a) && PlayCode(t[3], &b) && px_font_kern(&font, a, b) == atoi(t[4]);
    } else if (!strcmp(k, "title")) {
        char want[200] = "";
        for (int i = 2; i < n; i++) { if (i > 2) strcat(want, " "); strncat(want, t[i], 150); }
        ok = !strcmp(title, want);
        if (!ok) PlayLog("  actual title '%s'", title);
    } else if (!strcmp(k, "snapdir")) {
        ok = SnapCheck(n > 2 ? t[2] : "unchanged", n > 3 ? atoi(t[3]) : 0);
    } else if (!strcmp(k, "dirfiles")) { /* assert dirfiles <dir> <op> <N>: number of files below dir */
        SnapFile *cur = NULL;
        int cnt = n > 2 ? SnapScan(t[2], &cur) : 0;
        SnapFree(cur, cnt);
        ok = n > 4 && Compare(cnt, t[3], atol(t[4]));
        if (!ok) PlayLog("  actual files %d", cnt);
    } else if (!strcmp(k, "dirhas")) { /* assert dirhas <dir> <substring of a path> */
        SnapFile *cur = NULL;
        int cnt = n > 2 ? SnapScan(t[2], &cur) : 0;
        for (int i = 0; i < cnt && !ok; i++) ok = n > 3 && strstr(cur[i].path, t[3]) != NULL;
        SnapFree(cur, cnt);
    } else if (!strcmp(k, "bfilter") || !strcmp(k, "bsample")) {
        /* assert bfilter|bsample <text>   ("-" = empty) */
        char want[300] = "";
        for (int i = 2; i < n; i++) { if (i > 2) strcat(want, " "); strncat(want, t[i], 200); }
        if (!strcmp(want, "-")) want[0] = 0;
        Unescape(want);
        const char *have = !browser ? "" : (k[1] == 'f' ? Browser_FilterText(browser) : Browser_SampleText(browser));
        ok = !strcmp(have, want);
        if (!ok) PlayLog("  actual '%s'", have);
    } else if (!strcmp(k, "sample")) {
        /* assert sample <text>   (
 for a new line, "-" = empty): the shared sample text */
        char want[600] = "";
        for (int i = 2; i < n; i++) { if (i > 2) strcat(want, " "); strncat(want, t[i], 200); }
        if (!strcmp(want, "-")) want[0] = 0;
        Unescape(want);
        ok = !strcmp(Sample_Get(), want);
        if (!ok) PlayLog("  actual '%s'", Sample_Get());
    } else if (!strcmp(k, "blastpxfm") || !strcmp(k, "blastpxtf")) {
        /* assert blastpxfm|blastpxtf <hash>: the font / typeface of the last choice the browser returned */
        const char *have = !browser ? "" : (k[8] == 'm' ? Browser_LastPxfm(browser) : Browser_LastPxtf(browser));
        ok = !strncmp(have, t[2], strlen(t[2])) && have[0];
        if (!ok) PlayLog("  actual '%s'", have);
    } else if (!strcmp(k, "tabhash")) {
        /* assert tabhash <hash>: the hash in the Typeface tab's boxed field (a prefix is enough; a leading ! negates) */
        bool not = t[2][0] == '!';   /* "!<hash>": it is another hash */
        ok = tabHash[0] && (!strncmp(tabHash, t[2] + not, strlen(t[2] + not)) != not);
        if (!ok) PlayLog("  actual '%s'", tabHash);
    } else if (!strcmp(k, "bselpxfm")) {
        /* assert bselpxfm <hash>: the selected browser entry names this pxfm */
        char h[33];
        ok = browser && Browser_SelectedPxfm(browser, h) && !strcmp(h, t[2]);
        if (!ok) PlayLog("  actual selected pxfm %s", browser && Browser_SelectedPxfm(browser, h) ? h : "(none)");
    } else if (!strcmp(k, "sql") || !strcmp(k, "sqlfile")) {
        /* assert sql "<query>" <expected>        the first column of the first row of a query on the open store, as a number or text
         * assert sqlfile <file> "<query>" <expected>   the same on a .pxfdb file (read-only) */
        bool isFile = !strcmp(k, "sqlfile");
        int q0 = isFile ? 3 : 2;
        char q[400] = "";
        for (int i = q0; i < n - 1; i++) { if (i > q0) strcat(q, " "); strncat(q, t[i], 300); }
        size_t ql = strlen(q);
        if (ql >= 2 && q[0] == '"' && q[ql - 1] == '"') { q[ql - 1] = 0; memmove(q, q + 1, ql - 1); }
        const char *want = t[n - 1];
        sqlite3 *db = NULL;
        sqlite3_stmt *st = NULL;
        bool opened = false;
        if (isFile) opened = sqlite3_open_v2(t[2], &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK;
        else if (store) { db = store_sqlite(store_main_conn(store)); opened = true; }
        char got[200] = "(error)";
        if (opened && sqlite3_prepare_v2(db, q, -1, &st, NULL) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) { const unsigned char *tx = sqlite3_column_text(st, 0); snprintf(got, sizeof got, "%s", tx ? (const char *)tx : "NULL"); }
            else snprintf(got, sizeof got, "(no row)");
        } else if (opened) snprintf(got, sizeof got, "(error: %s)", sqlite3_errmsg(db));
        if (st) sqlite3_finalize(st);
        if (isFile && db) sqlite3_close(db);
        ok = !strcmp(got, want);
        if (!ok) PlayLog("  query result '%s', expected '%s'  [%s]", got, want, q);
        snprintf(line, sizeof line, "%s", q);
    } else if (!strcmp(k, "caption")) {
        /* assert caption <glyph> <text>: the code label the grid draws for the glyph ("U+0041", "#0") */
        int gi = GlyphByTok(t[2]);
        char cl[24] = "";
        if (gi >= 0) CodeLabel(font.glyphs[gi].code, cl, sizeof cl);
        ok = gi >= 0 && n > 3 && !strcmp(cl, t[3]);
        if (!ok) PlayLog("  caption '%s'", cl);
    } else if (!strcmp(k, "codeimg") || !strcmp(k, "codeform") || !strcmp(k, "codeflush")) {
        /* assert codeimg U+XXXX <0|1>: Unifont has a visible image for the code point (it is rasterised on demand);
         * assert codeform U+XXXX <0|1|2>: how the last caption drew it (1 character, 2 U+XXXX, 0 not drawn);
         * assert codeflush <n> <0|1>: rasterises n code points from U+3400 (far more than the atlas holds, so it is flushed) and checks that all took */
        int32_t cp = (int32_t)strtol(t[2] + (t[2][0] == 'U' ? 2 : 0), NULL, 16);
        int want = n > 3 ? atoi(t[3]) : 0, have = 0;
        if (!strcmp(k, "codeimg")) have = Th_CodeVisible(cp);
        else if (!strcmp(k, "codeform")) have = Th_CodeForm(cp);
        else { int ok2 = 0, cnt = atoi(t[2]); for (int i = 0; i < cnt; i++) ok2 += Th_CodeVisible(0x3400 + i); have = ok2 == cnt; }
        ok = have == want;
        if (!ok) PlayLog("  %s %s is %d", k, t[2], have);
    } else if (!strcmp(k, "hashname")) {
        /* assert hashname: the current hash of the font (and typeface) equals the hash it was opened with and names a row of the store */
        EnsureHashes();
        ok = store && Same(curHash, origHash) && store_has_pxfm(store_main_conn(store), curHash) &&
             (!hasCont || (Same(curContHash, origContHash) && store_has_pxtf(store_main_conn(store), curContHash)));
        if (!ok) PlayLog("  hash %s (orig %s) typeface %s (orig %s) in %s", curHash, origHash, curContHash, origContHash, libRoot);
    } else if (!strcmp(k, "sidecar")) {
        /* assert sidecar exists|missing: the settings file beside the open database (<db>-settings) */
        char sp[1200] = "";
        bool have = store && fs_sidecar_path(store_info(store)->path, sp, sizeof sp) && FileHas(sp);
        ok = !strcmp(t[2], "exists") ? have : (!strcmp(t[2], "missing") ? !have : false);
        if (!ok) PlayLog("  sidecar %s is %s", sp, have ? "there" : "not there");
    } else if (!strcmp(k, "flayer") || !strcmp(k, "fcorr")) {
        /* assert flayer <i> <field> <value>   a font layer (0 = top of the font layers): kind face visible opacity dx dy scale color size mtime
         *                                     ncorr nfaces outline bitmap ok (numbers), source label err (text; contained)
         * assert fcorr <i> <code> <field> <value>   its correction for a code (as PlayCode reads it, U+0041): exists dx dy dsize hidden */
        bool corr = k[1] == 'c';
        int i = n > 2 ? atoi(t[2]) : -1, fa = corr ? 4 : 3;
        FlLayer *fl = flDoc && i >= 0 && i < flDoc->n ? flDoc->v[i] : NULL;
        if (!fl || n <= fa || (n <= fa + 1 && strcmp(t[fa], "exists"))) { PlayLog("  no font layer %d", i); ok = false; }
        else if (corr) {
            PxCode code = PX_NO_CODE;
            if (!PlayCode(t[3], &code)) { PLAY_FAIL("bad code '%s'", t[3]); return; }
            const FlCorr *c = FlLayer_Corr(fl, code);
            double have = 0;
            if (!strcmp(t[fa], "exists")) { have = c != NULL; ok = have == (n < fa + 2 || atof(t[fa + 1]) != 0); }
            else {
                if (!strcmp(t[fa], "dx")) have = c ? c->dx : 0;
                else if (!strcmp(t[fa], "dy")) have = c ? c->dy : 0;
                else if (!strcmp(t[fa], "dsize")) have = c ? c->dsize : 1;
                else if (!strcmp(t[fa], "hidden")) have = c ? c->hidden : 0;
                else { PLAY_FAIL("bad correction field '%s'", t[fa]); return; }
                ok = fabs(have - atof(t[fa + 1])) < 0.001;
            }
            if (!ok) PlayLog("  actual %s %g", t[fa], have);
        } else {
            double num = 0;
            const char *str;
            if (!FlField(fl, t[fa], &num, &str)) { PLAY_FAIL("bad font layer field '%s'", t[fa]); return; }
            char want[300] = "";
            for (int q = fa + 1; q < n; q++) { if (q > fa + 1) strcat(want, " "); strncat(want, t[q], 200); }
            if (str) {
                bool contained = !strcmp(t[fa], "source") || !strcmp(t[fa], "label") || !strcmp(t[fa], "err");
                ok = contained ? strstr(str, want) != NULL : !strcmp(str, want);
                if (!ok) PlayLog("  actual %s '%s'", t[fa], str);
            } else {
                ok = fabs(num - (want[0] == '0' && (want[1] == 'x' || want[1] == 'X') ? (double)strtol(want, NULL, 16) : atof(want))) < 0.001;
                if (!ok) PlayLog("  actual %s %g", t[fa], num);
            }
        }
    } else if (!strcmp(k, "clip")) {   /* assert clip <text>: the clipboard holds exactly this */
        const char *c = GetClipboardText();
        ok = c && n > 2 && !strcmp(c, t[2]);
        if (!ok) PlayLog("  clipboard '%s'", c ? c : "(none)");
    } else if (!strcmp(k, "ohash")) {
        ok = !strcmp(origHash, t[2]);
        if (!ok) PlayLog("  actual original hash %s", origHash);
    } else if (!strcmp(k, "modal")) {
        char title[80];
        snprintf(title, sizeof title, "%s", t[2]);
        for (char *p = title; *p; p++) if (*p == '_') *p = ' ';
        ok = !strcmp(title, "none") ? modal.kind == M_NONE : (modal.kind != M_NONE && !strcmp(modal.title, title));
        if (!ok) PlayLog("  actual modal %s", modal.kind == M_NONE ? "none" : modal.title);
    } else if (!strcmp(k, "toast")) {
        char want[200] = "";
        for (int i = 2; i < n; i++) { if (i > 2) strcat(want, " "); strncat(want, t[i], 150); }
        ok = Th_ToastHas(want);
        if (!ok) PlayLog("  no toast containing '%s'", want);
    } else if (!strcmp(k, "statusdb") || !strcmp(k, "statusdoc") || !strcmp(k, "wtitle")) {
        /* assert statusdb <text>: the file part of the status bar (path, modified / saved) contains the text; statusdoc: the
         * view's own text; wtitle: the window title ("name.pxfdb" + " - modified") */
        char want[300] = "";
        for (int i = 2; i < n; i++) { if (i > 2) strcat(want, " "); strncat(want, t[i], 150); }
        Unescape(want);
        const char *have = !strcmp(k, "statusdb") ? statusDb : (!strcmp(k, "wtitle") ? winTitle : statusDoc);
        ok = strstr(have, want) != NULL;
        if (!ok) PlayLog("  actual %s: %s", k, have);
    } else if (!strcmp(k, "hash")) {
        EnsureHashes();
        if (!strcmp(t[2], "set")) ok = strlen(curHash) == 32;
        else if (!strcmp(t[2], "empty")) ok = curHash[0] == 0;
        else ok = !strcmp(curHash, t[2]);
        if (!ok) PlayLog("  actual hash %s", curHash);
    } else if (!strcmp(k, "chash")) {
        EnsureHashes();
        ok = !strcmp(curContHash, t[2]) || (!strcmp(t[2], "set") && strlen(curContHash) == 32);
        if (!ok) PlayLog("  actual typeface hash %s", curContHash);
    } else if (!strcmp(k, "saved")) {
        /* assert saved pxfm|pxtf  -> a hash was written (set) / equals the current hash (current) */
        const char *h = !strcmp(t[2], "pxtf") ? lastPxtf : lastPxfm;
        if (n > 3 && !strcmp(t[3], "current")) ok = !strcmp(!strcmp(t[2], "pxtf") ? curContHash : curHash, h);
        else ok = strlen(h) == 32;
    } else if (!strcmp(k, "file")) {
        const char *how = n > 3 ? t[3] : "exists";
        if (!strcmp(how, "exists")) ok = FileHas(t[2]);
        else if (!strcmp(how, "missing")) ok = !FileHas(t[2]);
        else if (!strcmp(how, "contains") || !strcmp(how, "notcontains")) {
            char want[300] = "";
            for (int i = 4; i < n; i++) { if (i > 4) strcat(want, " "); strncat(want, t[i], 200); }
            Unescape(want);
            char *b = ReadWhole(t[2], NULL);
            bool has = b && strstr(b, want) != NULL;
            ok = !strcmp(how, "contains") ? has : (b != NULL && !has);
            free(b);
        } else if (!strcmp(how, "lines") && n > 4) {
            char *b = ReadWhole(t[2], NULL);
            long c = 0;
            if (b) for (char *p = b; *p; p++) if (*p == '\n') c++;
            free(b);
            ok = b && c == atol(t[4]);
            if (!ok) PlayLog("  actual lines %ld", c);
        } else { PLAY_FAIL("bad file check '%s'", how); return; }
    } else if (!strcmp(k, "samefile")) {
        long la, lb;
        char *a = ReadWhole(t[2], &la), *b = n > 3 ? ReadWhole(t[3], &lb) : NULL;
        ok = a && b && la == lb && memcmp(a, b, (size_t)la) == 0;
        free(a); free(b);
    } else if (CodeStat(k, &cv)) {
        const char *op = n >= 4 ? t[2] : "=", *val = n >= 4 ? t[3] : t[2];
        PxCode want = PX_NO_CODE;
        bool parsed = !strcmp(val, "none") || (val[0] == '\'' && val[1] && (want = px_code_cp((unsigned char)val[1]), true)) || PlayCode(val, &want);
        ok = parsed && (!strcmp(op, "!=") ? cv != want : cv == want);
        if (!ok) { char cl[40]; CodeLabel(cv, cl, sizeof cl); PlayLog("  actual %s = %s", k, cl); }
    } else if (GetInt(k, &v)) {
        const char *op = "=";
        const char *val = t[2];
        if (n >= 4) { op = t[2]; val = t[3]; }
        long want = (val[0] == '\'' && val[1]) ? val[1] : ((val[0] == 'U' && val[1] == '+') ? strtol(val + 2, NULL, 16) : ((val[0] == '0' && val[1] == 'x') ? strtol(val + 2, NULL, 16) : atol(val)));
        ok = Compare(v, op, want);
        if (!ok && v == 0 && want == 1 && !strncmp(k, "w.", 2) && RevealTab()) return;   /* a widget in a panel tab not shown: show the tabs in turn */
        if (!ok) PlayLog("  actual %s = %ld", k, v);
    } else { PLAY_FAIL("unknown assert '%s'", k); return; }
    if (ok) PLAY_PASS("assert %s", line); else PLAY_FAIL("assert %s", line);
}

static void Subst(const char *in, char *out, size_t cap)
{
    static const char *names[] = {"$OHASH", "$HASH", "$DOC", "$LIB", "$SRC", "$NEWPXFM", "$NEWPXTF", "$SAVEDPXFM", "$SAVEDPXTF", "$CHASH", "$GHASH", "$GOHASH"};
    char gh[33] = "", goh[33] = "";   /* the pxgl hash of the glyph being edited, now and when the font was opened */
    if (inEditor && ed && Editor_GlyphIndex(ed) >= 0 && Editor_GlyphIndex(ed) < font.count) {
        px_glyph_hash(&font.glyphs[Editor_GlyphIndex(ed)], gh);
        if (Editor_OriginalHash(ed)) snprintf(goh, sizeof goh, "%s", Editor_OriginalHash(ed));
    }
    const char *vals[] = {origHash, curHash, docPath, libRoot, sourcePath, lastPxfm, lastPxtf, lastPxfmPath, lastPxtfPath, curContHash, gh, goh};
    size_t o = 0;
    while (*in && o + 1 < cap) {
        bool done = false;
        for (int i = 0; i < 12 && !done; i++) {
            size_t l = strlen(names[i]);
            if (!strncmp(in, names[i], l)) {
                o += (size_t)snprintf(out + o, cap - o, "%s", vals[i]);
                in += l;
                done = true;
            }
        }
        if (!done) out[o++] = *in++;
    }
    out[o < cap ? o : cap - 1] = 0;
}

static void PlayExecLine(const char *line)
{
    char raw[1024];
    Subst(line, raw, sizeof raw);
    char *s = raw;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '#') return;
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", s);
    char *t[24];
    int n = 0;
    char restBuf[1024] = "";
    char *rest = restBuf; /* rest of line after the first word, for type/echo/repeat */
    for (char *p = buf; *p && n < 24;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        t[n++] = p;
        if (n == 1) { char *q = p; while (*q && *q != ' ') q++; snprintf(restBuf, sizeof restBuf, "%s", *q ? q + 1 : q); rest = restBuf; }
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    if (n == 0) return;
    if (!strcmp(t[0], "repeat") && n >= 3) { /* repeat N <command...> */
        const char *sub = rest;
        while (*sub == ' ') sub++;
        while (*sub && *sub != ' ') sub++;
        while (*sub == ' ') sub++;
        char subc[1024];
        snprintf(subc, sizeof subc, "%s", sub);
        for (int i = atoi(t[1]); i > 0 && !play.retry; i--) PlayExecLine(subc);
        return;
    }
    const char *c = t[0];
    Mods m = {false, false, false};
    int btn = MOUSE_BUTTON_LEFT;
    bool isKey = !strcmp(t[0], "key") || !strcmp(t[0], "keydown") || !strcmp(t[0], "keyup");
    bool isMouse = !strcmp(t[0], "click") || !strcmp(t[0], "dblclick") || !strcmp(t[0], "drag") || !strcmp(t[0], "move") || !strcmp(t[0], "wheel") || !strcmp(t[0], "mousedown") || !strcmp(t[0], "mouseup");   /* mousedown and mouseup take the words right and middle (and the modifiers) */
    for (int i = (isKey || isMouse) ? (isKey ? 2 : 1) : n; i < n; i++) {
        if (!strcmp(t[i], "ctrl")) m.ctrl = true;
        else if (!strcmp(t[i], "shift")) m.shift = true;
        else if (!strcmp(t[i], "alt")) m.alt = true;
        else if (!strcmp(t[i], "right")) btn = MOUSE_BUTTON_RIGHT;
        else if (!strcmp(t[i], "middle")) btn = MOUSE_BUTTON_MIDDLE;
    }
    /* drop modifier/button words so positional parsing sees only the target */
    int nn = 1;
    for (int i = 1; i < n; i++)
        if ((!isMouse && !isKey) || (isKey && i < 2) || (strcmp(t[i], "ctrl") && strcmp(t[i], "shift") && strcmp(t[i], "alt") && strcmp(t[i], "right") && strcmp(t[i], "left") && strcmp(t[i], "middle"))) t[nn++] = t[i];
    int na = nn;
    float x, y, x2, y2;

    if (!strcmp(c, "wait")) QWait(na > 1 ? atoi(t[1]) : 1);
    else if (!strcmp(c, "bselect")) { if (browser && na > 1) Browser_SelectVisible(browser, atoi(t[1])); else PLAY_FAIL("bselect N needs the library"); }   /* select the N-th visible entry: a click on it then opens it */
    else if (!strcmp(c, "move")) { if (Target(t + 1, na - 1, &x, &y)) QMove((int)x, (int)y); }
    else if (!strcmp(c, "click") || !strcmp(c, "dblclick")) {
        if (Target(t + 1, na - 1, &x, &y)) {
            QClick((int)x, (int)y, btn, m);
            if (!strcmp(c, "dblclick")) QClick((int)x, (int)y, btn, m);
            QWait(1);
        }
    } else if (!strcmp(c, "drag")) {
        int u = Target(t + 1, na - 1, &x, &y);
        if (u && Target(t + 1 + u, na - 1 - u, &x2, &y2)) { QDrag((int)x, (int)y, (int)x2, (int)y2, btn, m); QWait(1); }
    } else if (!strcmp(c, "mousedown") || !strcmp(c, "mouseup")) {
        PFrame *f = QNew();
        QMods(f, m, !strcmp(c, "mousedown"));
        QEv(f, !strcmp(c, "mousedown") ? PEV_MOUSE_DOWN : PEV_MOUSE_UP, btn, 0);
    } else if (!strcmp(c, "resize")) {
        if (na < 3) PLAY_FAIL("resize W H"); else { SetWindowMinSize(1, 1); SetWindowSize(atoi(t[1]), atoi(t[2])); QWait(3); }   /* below the application's minimum too */
    } else if (!strcmp(c, "wheel")) {
        int dy = na > 1 ? atoi(t[1]) : 1;
        if (na > 2 && Target(t + 2, na - 2, &x, &y)) QMove((int)x, (int)y);
        PFrame *f = QNew();
        QMods(f, m, true);
        QEv(f, PEV_WHEEL, 0, dy);
        f = QNew();
        QMods(f, m, false);
        QWait(1);
    } else if (!strcmp(c, "key")) {
        int k = na > 1 ? KeyByName(t[1]) : -1;
        if (k < 0) PLAY_FAIL("unknown key '%s'", na > 1 ? t[1] : "");
        else {
            PFrame *f = QNew();
            QMods(f, m, true);
            QEv(f, PEV_KEY_DOWN, k, 0);
            f = QNew();
            QEv(f, PEV_KEY_UP, k, 0);
            QMods(f, m, false);
            QWait(1);
        }
    } else if (!strcmp(c, "keydown") || !strcmp(c, "keyup")) {
        int k = na > 1 ? KeyByName(t[1]) : -1;
        if (k < 0) PLAY_FAIL("unknown key"); else { PFrame *f = QNew(); QEv(f, !strcmp(c, "keydown") ? PEV_KEY_DOWN : PEV_KEY_UP, k, 0); }
    } else if (!strcmp(c, "type")) {
        PFrame *f = QNew();
        f->chars = (char *)malloc(strlen(rest) + 1);
        strcpy(f->chars, rest);
        QWait(1);
    } else if (!strcmp(c, "shot")) {
        PFrame *f = QNew();
        f->shot = (char *)malloc(strlen(t[1]) + 1);
        strcpy(f->shot, na > 1 ? t[1] : "tests/tmp/shot.png");
    } else if (!strcmp(c, "closewindow")) { PFrame *f = QNew(); QEv(f, PEV_CLOSE, 0, 0); QWait(1); }
    else if (!strcmp(c, "reveal")) { int gi = na > 1 ? GlyphByTok(t[1]) : -1; if (gi < 0) PLAY_FAIL("no such glyph"); else { revealIdx = gi; QWait(2); } }
    else if (!strcmp(c, "assert")) DoAssert(t, na);
    else if (!strcmp(c, "setdialog")) {
        if (na < 3) PLAY_FAIL("setdialog pxfdb|folder|save <path> (none clears)");
        else if (!strcmp(t[1], "folder")) snprintf(dlgFolderPath, sizeof dlgFolderPath, "%s", t[2]);
        else if (!strcmp(t[1], "pxfdb")) snprintf(dlgDbPath, sizeof dlgDbPath, "%s", t[2]);
        else if (!strcmp(t[1], "save")) snprintf(dlgSavePath, sizeof dlgSavePath, "%s", t[2]);
        else if (!strcmp(t[1], "layerfile")) snprintf(dlgLayerPath, sizeof dlgLayerPath, "%s", t[2]);
        else if (!strcmp(t[1], "fontfile")) snprintf(dlgFontPath, sizeof dlgFontPath, "%s", t[2]);
        else PLAY_FAIL("setdialog kind '%s'", t[1]);
        if (na >= 3 && !strcmp(t[2], "none")) { dlgFolderPath[0] = dlgDbPath[0] = dlgSavePath[0] = dlgLayerPath[0] = dlgFontPath[0] = 0; }
    } else if (!strcmp(c, "addlayerfile")) {
        /* addlayerfile <path> [face] [font|session] [expectfail]: what the Add Layer popover does after the file dialog (the face, strike or
         * .fon font index; a font layer by default).  expectfail: the layer must NOT be added (a missing or unreadable file). */
        if (na < 2) PLAY_FAIL("addlayerfile <path> [face] [font|session] [expectfail]");
        else {
            int face = 0;
            bool fontLevel = true, expectFail = false;
            for (int i = 2; i < na; i++) {
                if (!strcmp(t[i], "session")) fontLevel = false;
                else if (!strcmp(t[i], "font")) fontLevel = true;
                else if (!strcmp(t[i], "expectfail")) expectFail = true;
                else face = atoi(t[i]);
            }
            if (!ed) ed = Editor_Create();
            Layers *L = Editor_Layers(ed);
            Layers_SetFontDoc(L, flDoc);
            char err[200];
            int r = Layers_AddFile(L, t[1], face, fontLevel, err, (int)sizeof err);
            if (expectFail ? r >= 0 : r < 0) PLAY_FAIL("addlayerfile %s: %s", t[1], r < 0 ? err : "was added");
        }
    } else if (!strcmp(c, "importfont")) {
        /* importfont <path> [auto|pixel|smooth] [face N] [em X] [notypeface] [expectfail]: what Menu, Import Font File does after its file
         * dialog and the Import button (auto: what the dialog proposes; em: the size of a smooth font, as the dialog's size control). */
        if (na < 2) PLAY_FAIL("importfont <path> [auto|pixel|smooth] [face N] [em X] [notypeface] [expectfail]");
        else {
            int face = 0, mode = -1;
            float em = 0;
            bool tf = true, expectFail = false;
            for (int i = 2; i < na; i++) {
                if (!strcmp(t[i], "pixel")) mode = 1;
                else if (!strcmp(t[i], "smooth")) mode = 0;
                else if (!strcmp(t[i], "auto")) mode = -1;
                else if (!strcmp(t[i], "notypeface")) tf = false;
                else if (!strcmp(t[i], "expectfail")) expectFail = true;
                else if (!strcmp(t[i], "face") && i + 1 < na) face = atoi(t[++i]);
                else if (!strcmp(t[i], "em") && i + 1 < na) em = (float)atof(t[++i]);
            }
            ImpAnalyze(t[1], face);
            fimp.typeface = tf;
            bool ok = (fimp.probe || fimp.sheet) && !fimp.err[0];
            if (ok && mode == 1 && !(fimp.outline ? fimp.hasGrid : true)) ok = false;
            if (ok && mode >= 0 && fimp.outline) fimp.pixel = mode == 1;
            if (ok && em > 0 && fimp.outline && !fimp.pixel) {
                FitSizeOpts o = {0, 0, em};
                if (fit_find_size(fimp.probe->ol, &o, &fimp.smooth, &fimp.rep, NULL, 0) != 1) ok = false;
            }
            if (ok) ok = ImportFontCreate();
            else PlayLog("  importfont: %s", fimp.err[0] ? fimp.err : "no pixel grid");
            if (!ok) ImpFree();
            if (expectFail ? ok : !ok) PLAY_FAIL("importfont %s: %s", t[1], ok ? "was imported" : "failed");
        }
    } else if (!strcmp(c, "setlayer")) {
        /* setlayer <i> em|aspect|dx|dy|threshold|mode|follow <value>: a font layer's placement, as the Layers tab of the font view sets it */
        FlLayer *fl = na > 3 && flDoc && atoi(t[1]) >= 0 && atoi(t[1]) < flDoc->n ? flDoc->v[atoi(t[1])] : NULL;
        float v = na > 3 ? (float)atof(t[3]) : 0;
        if (!fl) PLAY_FAIL("setlayer <i> <field> <value>");
        else {
            if (!strcmp(t[2], "em")) fl->s.em = v;
            else if (!strcmp(t[2], "aspect")) fl->s.aspect = v;
            else if (!strcmp(t[2], "dx")) fl->s.dx = v;
            else if (!strcmp(t[2], "dy")) fl->s.dy = v;
            else if (!strcmp(t[2], "threshold")) fl->s.threshold = v;
            else if (!strcmp(t[2], "mode")) fl->s.mode = v != 0;
            else if (!strcmp(t[2], "follow")) fl->s.follow = v != 0;
            else PLAY_FAIL("setlayer field '%s'", t[2]);
            flDoc->rev++;
        }
    } else if (!strcmp(c, "setclip")) {   /* setclip <text>: the clipboard (
 is a new line), for Ctrl+V */
        Unescape(restBuf);
        SetClipboardText(restBuf);
    } else if (!strcmp(c, "snapdir")) {
        if (na < 2) PLAY_FAIL("snapdir <dir>"); else SnapTake(t[1]);
    } else if (!strcmp(c, "panelscroll")) {
        panelScroll = na > 1 ? (float)atof(t[1]) : 0;
        QWait(2);
    } else if (!strcmp(c, "verifylib")) {
        if (na < 3) PLAY_FAIL("verifylib <pxfm> <libroot> [N]"); else VerifyLib(t[1], t[2], na > 3 ? atoi(t[3]) : -1);
    } else if (!strcmp(c, "copyfile")) {
        long len = 0;
        char *b = na > 2 ? ReadWhole(t[1], &len) : NULL;
        char dir[PATH_MAX_LEN];
        if (b) { DirName(t[2], dir, sizeof dir); if (dir[0]) px_mkdir_p(dir); }
        FILE *fp = b ? fopen(t[2], "wb") : NULL;
        if (!fp) PLAY_FAIL("copyfile %s failed", na > 1 ? t[1] : ""); else { fwrite(b, 1, (size_t)len, fp); fclose(fp); }
        free(b);
    } else if (!strcmp(c, "writefile")) {
        char dir[PATH_MAX_LEN];
        Unescape(rest + (na > 1 ? strlen(t[1]) + 1 : 0));
        DirName(na > 1 ? t[1] : "", dir, sizeof dir);
        if (dir[0]) px_mkdir_p(dir);
        FILE *fp = na > 1 ? fopen(t[1], "wb") : NULL;
        if (!fp) PLAY_FAIL("writefile failed"); else { fputs(rest + strlen(t[1]) + 1, fp); fclose(fp); }
    } else if (!strcmp(c, "delete")) {
        if (na < 2) PLAY_FAIL("delete <path>"); else remove(t[1]);
    } else if (!strcmp(c, "expectquit")) play.expectQuit = true;
    else if (!strcmp(c, "fps")) { play.fps = na > 1 ? atoi(t[1]) : 240; SetTargetFPS(play.fps); }
    else if (!strcmp(c, "sleep")) play.sleepUntil = GetTime() + (na > 1 ? atof(t[1]) : 500) / 1000.0; /* wall-clock wait, ms */
    else if (!strcmp(c, "waitfor")) {
        /* waitfor <name> <op> <value> [timeout ms, default 60000]: idle frames until the state (an assert name) holds */
        if (na < 4) PLAY_FAIL("waitfor <name> <op> <value> [ms]");
        else {
            play.waiting = true;
            snprintf(play.waitName, sizeof play.waitName, "%s", t[1]);
            snprintf(play.waitOp, sizeof play.waitOp, "%s", t[2]);
            play.waitVal = atol(t[3]);
            play.waitUntil = GetTime() + (na > 4 ? atof(t[4]) : 60000) / 1000.0;
        }
    }
    else if (!strcmp(c, "show")) {   /* show <state name>...: prints the values (frame times, counts) into the log */
        for (int i = 1; i < na; i++) { long v = 0; if (GetInt(t[i], &v)) PlayLog("SHOW %s = %ld", t[i], v); else PlayLog("SHOW %s unknown", t[i]); }
    }
    else if (!strcmp(c, "echo")) PlayLog("echo: %s", rest);
    else if (!strcmp(c, "quit")) { play.finished = true; play.cur = play.nlines; }
    else PLAY_FAIL("unknown command '%s'", c);
}

static void PlayExec(void)
{
    if (play.cur >= play.nlines) { play.finished = true; return; }
    play.lineNo = play.cur + 1;
    play.retry = false;
    PlayExecLine(play.lines[play.cur++]);
}

static bool PlayLoad(const char *path)
{
    snprintf(play.script, sizeof play.script, "%s", BaseName(path));
    char *b = ReadWhole(path, NULL);
    if (!b) { printf("FAIL cannot read script %s\n", path); return false; }
    int cap = 64;
    play.lines = (char **)malloc(sizeof(char *) * (size_t)cap);
    for (char *p = b; *p;) {
        char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (play.nlines == cap) { cap *= 2; play.lines = (char **)realloc(play.lines, sizeof(char *) * (size_t)cap); }
        char *ln = (char *)malloc(l + 1);
        memcpy(ln, p, l);
        ln[l] = 0;
        if (l && ln[l - 1] == '\r') ln[l - 1] = 0;
        play.lines[play.nlines++] = ln;
        p += l + (e ? 1 : 0);
    }
    free(b);
    play.active = true;
    play.fps = 240;
    return true;
}

static const char *playShotPending = NULL;
static PFrame playCur;
static char playShotBuf[512];

/* Called at the start of every frame. */
static void PlayBeginFrame(void)
{
    if (job.active) { playCur.n = 0; playCur.chk = false; playCur.chars = NULL; playCur.shot = NULL; return; }   /* the library is opening: the script waits */
    if (play.waiting && play.qhead >= play.qn) {
        long v = 0;
        if (GetInt(play.waitName, &v) && Compare(v, play.waitOp, play.waitVal)) play.waiting = false;
        else if (GetTime() > play.waitUntil) {
            play.waiting = false;
            play.lineNo = play.cur;
            PLAY_FAIL("waitfor %s %s %ld timed out (now %ld)", play.waitName, play.waitOp, play.waitVal, v);
        }
    }
    while (play.qhead >= play.qn && !play.finished && !play.waiting && GetTime() >= play.sleepUntil) {
        play.qhead = play.qn = 0;
        PlayExec();
    }
    if (play.qhead >= play.qn && !play.finished) { playCur.n = 0; playCur.chk = false; return; } /* sleeping: idle frame */
    if (play.qhead >= play.qn) { quit = true; return; }
    PFrame f = play.q[play.qhead++];
    playCur = f;
    for (int i = 0; i < f.n; i++) {
        AutomationEvent e;
        memset(&e, 0, sizeof e);
        e.type = (unsigned)f.ev[i].type;
        e.params[0] = f.ev[i].p0;
        e.params[1] = f.ev[i].p1;
        PlayAutomationEvent(e);
    }
    if (f.chars) {
        const char *p = f.chars;
        while (*p) {
            int sz = 0;
            int cp = GetCodepointNext(p, &sz);
            if (sz <= 0) break;
            p += sz;
            if (play.chn == play.chcap) { play.chcap = play.chcap ? play.chcap * 2 : 64; play.chq = (int *)realloc(play.chq, sizeof(int) * (size_t)play.chcap); }
            play.chq[play.chn++] = cp;
        }
        free(f.chars);
    } else if (play.chhead >= play.chn) play.chhead = play.chn = 0;
    if (f.shot) {
        /* taken after this frame is drawn: stash the path */
        snprintf(playShotBuf, sizeof playShotBuf, "%s", f.shot);
        free(f.shot);
        playShotPending = playShotBuf;
    }
}

static void PlayEndFrame(void)
{
    if (playCur.chk) {
        Image im = LoadImageFromScreen();
        Color c = GetImageColor(im, playCur.cx, playCur.cy);
        UnloadImage(im);
        unsigned got = ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b;
        int saved = play.lineNo;
        play.lineNo = playCur.line;
        if (got == playCur.want) PLAY_PASS("%s", playCur.desc);
        else PLAY_FAIL("%s (screen pixel %d,%d is %06X)", playCur.desc, playCur.cx, playCur.cy, got);
        play.lineNo = saved;
        playCur.chk = false;
    }
    if (playShotPending) {
        char dir[PATH_MAX_LEN];
        DirName(playShotPending, dir, sizeof dir);
        if (dir[0]) px_mkdir_p(dir);
        Image im = LoadImageFromScreen();
        ExportImage(im, playShotPending);
        UnloadImage(im);
        playShotPending = NULL;
    }
}

static int PlayFinish(void)
{
    if (play.cur < play.nlines && !play.finished && !play.expectQuit) {
        PLAY_FAIL("app exited before the script finished (line %d of %d)", play.cur, play.nlines);
    } else if (play.qhead < play.qn && !play.expectQuit) {
        PLAY_FAIL("app exited with %d scripted frames pending", play.qn - play.qhead);
    }
    PlayLog("RESULT %s: %d passed, %d failed", play.script, play.passes, play.fails);
    return play.fails ? 1 : 0;
}
