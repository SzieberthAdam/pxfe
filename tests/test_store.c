/* test_store.c - quick tests of src/store.c and src/store_sim.c on testdata/lib and testdata/lib.pxfdb.
 * Run from the pxfe directory (build.bat test does).  The whole real library is tested by test_library.c (build.bat libtest). */
#include "store.h"
#include "pxfont.h"
#include "pxtf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "sqlite3.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

#define TMP "tests/tmp"
#define LIB "testdata/lib"
#define DB "testdata/lib.pxfdb"
#define H_EXAMPLE "0820dc69a21b1cdb3a44d2cff7d884d8"
#define H_GREY "408c4d7dd946543f8e9b4553dd1ba62e"
#define G_GREY_A "3ad25ae0043eddcc6bc11e8794fccdd3"
#define P_GREY "ebbba94934da6897d86c5e8ea2ac5409"
#define H_NOASC "e6109c134eda8d747bac9a3c2a62d204"
#define H_UNC "fa96e3583da8e003fc25404ac00f764e"
#define T_SANS "b925951bb94de1d15ec27404b3261476"
#define T_UNC "b5fbcdd0dd5009013337c15e57fa7672"
#define G_A "c0fc54c2ec60cb8617b041d991da1622"
#define G_H "abe675527c81c31d04cda5e2aed7623a"

static const char *const TABLES[] = {"glyph", "pxfm", "palette", "pxfm_glyph", "pxfm_kerning", "pxtf", "pxtf_variant", "pxtf_field"};

/* count and an order-independent... no: ordered FNV-1a content checksum of a table (the tables have primary keys, so the
 * scan order is the key order in both stores) */
static uint64_t table_sum(Store *s, const char *table, long *rows) {
    StoreConn *c = store_conn_open(s);
    sqlite3 *db = store_sqlite(c);
    char sql[100];
    snprintf(sql, sizeof sql, "SELECT * FROM %s", table);
    sqlite3_stmt *st = NULL;
    uint64_t h = 1469598103934665603ULL;
    *rows = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            (*rows)++;
            for (int i = 0; i < sqlite3_column_count(st); i++) {
                int t = sqlite3_column_type(st, i);
                h = (h ^ (uint64_t)t) * 1099511628211ULL;
                const unsigned char *p = (const unsigned char *)sqlite3_column_blob(st, i);
                int n = sqlite3_column_bytes(st, i);
                if (t == SQLITE_INTEGER) { long long v = sqlite3_column_int64(st, i); p = (const unsigned char *)&v; n = 8; for (int k = 0; k < n; k++) h = (h ^ p[k]) * 1099511628211ULL; continue; }
                for (int k = 0; k < n; k++) h = (h ^ p[k]) * 1099511628211ULL;
            }
        }
    }
    sqlite3_finalize(st);
    store_conn_close(c);
    return h;
}

static bool same_content(Store *a, Store *b) {
    bool ok = true;
    for (size_t t = 0; t < sizeof TABLES / sizeof *TABLES; t++) {
        long ra, rb;
        uint64_t ha = table_sum(a, TABLES[t], &ra), hb = table_sum(b, TABLES[t], &rb);
        if (ra != rb || ha != hb) { printf("      table %s differs: %ld rows / %ld rows\n", TABLES[t], ra, rb); ok = false; }
    }
    return ok;
}

static long count_rows(Store *s, const char *table) {
    long r;
    table_sum(s, table, &r);
    return r;
}

static void bs(char *p) { for (; *p; p++) if (*p == '/') *p = 0x5C; }

static void rm_rf(const char *dir) {
    char d[300], cmd[400];
    snprintf(d, sizeof d, "%s", dir);
    bs(d);
    snprintf(cmd, sizeof cmd, "rmdir /S /Q \"%s\" 2>nul", d);
    system(cmd);
}

static void copy_tree(const char *from, const char *to) {
    char a[300], b[300], cmd[800];
    snprintf(a, sizeof a, "%s", from);
    snprintf(b, sizeof b, "%s", to);
    bs(a); bs(b);
    snprintf(cmd, sizeof cmd, "xcopy /E /I /Y /Q \"%s\" \"%s\" >nul", a, b);
    system(cmd);
}

/* ------------------------------------------------------------------ tests */

static void test_open_pxfdb(void) {
    char err[PX_ERR_LEN];
    Store *s = NULL;
    CHECK(store_open_pxfdb(DB, NULL, &s, err), "open testdata/lib.pxfdb");
    if (!s) return;
    const StoreInfo *si = store_info(s);
    CHECK(strcmp(si->path, DB) == 0 && !store_modified(s) && si->nGlyph == 6 && si->nPxfm == 4 && si->nPxtf == 2, "6 glyphs, 4 fonts, 2 typefaces");
    CHECK(count_rows(s, "pxfm_glyph") == 18, "18 glyph lines (the FFFFFF lines of the default glyphs included)");
    CHECK(count_rows(s, "pxtf_variant") == 3, "3 typeface variants");
    store_close(s);
    CHECK(!store_open_pxfdb(TMP "/does-not-exist.pxfdb", NULL, &s, err), "a missing file is an error");
    CHECK(!store_open_pxfdb(LIB "/pxfm/08/" H_EXAMPLE ".pxfm", NULL, &s, err) && err[0], "a text file is not a pxfdb");
}

static void test_text_from_rows(void) {
    char err[PX_ERR_LEN];
    Store *s = NULL;
    if (!store_open_pxfdb(DB, NULL, &s, err)) { CHECK(false, "open"); return; }
    StoreConn *c = store_conn_open(s);
    StoreEntryList fl, tl;
    CHECK(store_list_fonts(c, &fl, err) && store_list_typefaces(c, &tl, err), "lists");
    int okm = 0, okf = 0, okt = 0, okt2 = 0;
    for (int i = 0; i < fl.count; i++) {
        char path[300], h[33];
        snprintf(path, sizeof path, LIB "/pxfm/%.2s/%s.pxfm", fl.e[i].pxfm, fl.e[i].pxfm);
        size_t fn, tn;
        char *file = px_read_file(path, &fn);
        char *txt = store_pxfm_text(c, fl.e[i].pxfm, &tn, h);
        if (file && txt && fn == tn && memcmp(file, txt, fn) == 0 && strcmp(h, fl.e[i].pxfm) == 0) okm++;
        free(txt);
        PxFont f;
        if (store_load_font(c, fl.e[i].pxfm, NULL, 0, &f, err)) {
            txt = px_pxfm_text(&f, &tn, h);
            if (file && fn == tn && memcmp(file, txt, fn) == 0 && strcmp(h, fl.e[i].pxfm) == 0 && strcmp(f.fileHash, fl.e[i].pxfm) == 0) okf++;
            free(txt);
            px_font_free(&f);
        }
        free(file);
    }
    CHECK(okm == fl.count && fl.count == 4, "pxfm text made from the rows is byte-equal to each file, hash == name (4 of 4)");
    CHECK(okf == fl.count, "pxfm text of the PxFont loaded from the rows is byte-equal to each file (4 of 4)");
    const char *seen[8];
    int nseen = 0;
    for (int i = 0; i < tl.count; i++) {
        bool dup = false;
        for (int k = 0; k < nseen; k++) if (strcmp(seen[k], tl.e[i].pxtf) == 0) dup = true;
        if (dup) continue;
        seen[nseen++] = tl.e[i].pxtf;
        char path[300], h[33];
        snprintf(path, sizeof path, LIB "/pxtf/%.2s/%s.pxtf", tl.e[i].pxtf, tl.e[i].pxtf);
        size_t fn, tn;
        char *file = px_read_file(path, &fn);
        char *txt = store_pxtf_text(c, tl.e[i].pxtf, &tn, h);
        if (file && txt && fn == tn && memcmp(file, txt, fn) == 0 && strcmp(h, tl.e[i].pxtf) == 0) okt++;
        free(txt);
        PxtfTypeface tf;
        if (store_load_typeface(c, tl.e[i].pxtf, &tf, err)) {
            txt = pxtf_text(&tf, &tn, h);
            if (file && fn == tn && memcmp(file, txt, fn) == 0 && strcmp(h, tl.e[i].pxtf) == 0) okt2++;
            free(txt);
            pxtf_free(&tf);
        }
        free(file);
    }
    CHECK(nseen == 2 && okt == 2 && okt2 == 2, "pxtf text from the rows, and from the loaded typeface, byte-equal to each file (2 of 2)");
    store_entry_list_free(&fl);
    store_entry_list_free(&tl);
    const char *gl[] = {G_A, G_H, G_GREY_A, "41e0be1b2ef9b46f1f72092b18408ce2", "7c2001124fdbbcfee5cbf19cc5bc1911", "f83ad16c225c33bc1d6bfe2178677638"};
    int okg = 0;
    for (int i = 0; i < 6; i++) {
        char path[300], h[33];
        snprintf(path, sizeof path, LIB "/pxgl/%.2s/%s.pxgl", gl[i], gl[i]);
        size_t fn, tn;
        char *file = px_read_file(path, &fn);
        char *txt = store_pxgl_text(c, gl[i], &tn, h);
        if (file && txt && fn == tn && memcmp(file, txt, fn) == 0 && strcmp(h, gl[i]) == 0) okg++;
        free(file); free(txt);
    }
    CHECK(okg == 6, "pxgl text from the rows is byte-equal to each file (6 of 6)");
    {
        size_t fn, tn;
        char h[33];
        char *file = px_read_file(LIB "/palette/eb/" P_GREY ".hex", &fn);
        char *txt = store_palette_text(c, P_GREY, &tn, h);
        CHECK(file && txt && fn == tn && memcmp(file, txt, fn) == 0 && strcmp(h, P_GREY) == 0, "palette text from the row is byte-equal to the file");
        free(file); free(txt);
    }
    store_conn_close(c);
    store_close(s);
}

static void test_model(void) {
    char err[PX_ERR_LEN];
    Store *s = NULL;
    if (!store_open_pxfdb(DB, NULL, &s, err)) { CHECK(false, "open"); return; }
    StoreConn *c = store_conn_open(s);
    PxFont f;
    CHECK(store_load_font(c, H_UNC, NULL, 0, &f, err), "the font with uncoded glyphs loads");
    CHECK(f.count == 6 && px_font_uncoded_count(&f) == 2 && f.glyphs[3].code == 0xF00000 && f.glyphs[4].code == 0xF00001, "codes F00000 and F00001 after the code points");
    CHECK(px_font_default_index(&f) == 5 && f.glyphs[5].code == PX_CODE_DEFAULT, "the FFFFFF line is the last glyph of the font");
    CHECK(px_font_kern(&f, 0x41, 0xF00000) == -1 && px_font_kern(&f, 0xF00001, 0xF00000) == 1, "kerning pairs naming F-codes");
    CHECK(f.hasRatio && f.monospaced && f.glyphs[3].w == 5 && f.glyphs[3].h == 4 && f.glyphs[3].pix[0] == '0', "ratio, monospaced, bitmaps of the uncoded glyphs");
    px_font_free(&f);
    int32_t want[] = {0x41, 0xF00001};
    CHECK(store_load_font(c, H_UNC, want, 2, &f, err), "load only some codes");
    CHECK(!f.glyphs[px_font_find(&f, 0x41)].lazy && f.glyphs[px_font_find(&f, 0x48)].lazy && !f.glyphs[px_font_find(&f, 0xF00001)].lazy && f.glyphs[px_font_find(&f, 0xF00000)].lazy, "only the asked glyphs have bitmaps");
    char h[33];
    px_font_hash(&f, h);
    CHECK(strcmp(h, H_UNC) == 0, "its hash is right with glyphs still lazy");
    CHECK(store_load_glyph_bitmaps(c, &f, NULL, 0, err) && !f.glyphs[px_font_find(&f, 0x48)].lazy, "store_load_glyph_bitmaps loads the rest");
    px_font_free(&f);
    CHECK(!store_load_font(c, "00000000000000000000000000000000", NULL, 0, &f, err) && err[0], "an unknown font is an error");
    CHECK(store_load_font(c, H_GREY, NULL, 0, &f, err) && f.hasPalette && f.color[1] == 0x808080FFu && f.color[2] == 0xFFFFFFFFu && px_font_colors(&f) == 2 && strcmp(f.glyphs[px_font_find(&f, 0x41)].pal, P_GREY) == 0, "grey font with its palette");
    px_font_free(&f);
    CHECK(store_load_font(c, H_NOASC, NULL, 0, &f, err) && !f.has[PXM_ASCENT] && f.glyphs[px_font_find(&f, 0x41)].x == -1, "no-ascent font, offset -1");
    px_font_free(&f);
    PxtfTypeface tf;
    CHECK(store_load_typeface(c, T_UNC, &tf, err) && tf.count == 1 && strcmp(tf.v[0].format, "RAW") == 0 && strcmp(tf.v[0].text[PXTF_CODEPAGE], "cp437-graphic") == 0 && tf.v[0].nopts == 1, "typeface: RAW format, CODEPAGE as a text, CELL as an option");
    pxtf_free(&tf);
    CHECK(store_has_pxfm(c, H_GREY) && !store_has_pxfm(c, "11111111111111111111111111111111") && store_has_pxtf(c, T_SANS), "has_pxfm / has_pxtf");

    StoreEntryList tl, fl;
    CHECK(store_list_typefaces(c, &tl, err) && tl.count == 3, "typeface list: 3 variants");
    CHECK(store_list_fonts(c, &fl, err) && fl.count == 4, "font list: 4 fonts");
    int iu = -1, ig = -1;
    for (int i = 0; i < fl.count; i++) { if (strcmp(fl.e[i].pxfm, H_UNC) == 0) iu = i; if (strcmp(fl.e[i].pxfm, H_GREY) == 0) ig = i; }
    CHECK(iu >= 0 && ig >= 0 && strcmp(fl.e[iu].family, "Uncoded Mono") == 0 && fl.e[iu].glyphCount == 5 && fl.e[iu].hasDefault && fl.e[iu].monospaced && fl.e[iu].hasRatio && fl.e[iu].hasHeight && fl.e[iu].height == 8, "font entry facts");
    CHECK(fl.e[ig].pxtf[0] == 0 && fl.e[ig].family[0] == 0 && fl.e[ig].nVariants == 0, "a font no typeface names has no metadata");
    store_entry_list_free(&tl);
    store_entry_list_free(&fl);
    store_conn_close(c);
    store_close(s);
}

static void test_queries(void) {
    char err[PX_ERR_LEN];
    Store *s = NULL;
    if (!store_open_pxfdb(DB, NULL, &s, err)) { CHECK(false, "open"); return; }
    StoreConn *c = store_conn_open(s);
    StoreUsageList ul;
    int nf = 0, nt = 0;
    CHECK(store_glyph_usage(c, G_A, &ul, &nf, &nt, err), "glyph usage of the A");
    CHECK(nf == 3 && nt == 2 && ul.count >= 3, "A: 3 fonts, 2 typefaces");
    store_usage_list_free(&ul);
    CHECK(store_glyph_usage(c, G_H, &ul, &nf, &nt, err) && nf == 2 && ul.count == 2, "H: 2 fonts and 2 rows (the FFFFFF line of the default glyph is left out)");
    store_usage_list_free(&ul);
    StoreEntryList el;
    const char both[2][33] = {G_A, G_H};
    CHECK(store_fonts_with_glyphs(c, both, 2, &el, err) && el.count == 2, "fonts that hold both A and H: 2");
    store_entry_list_free(&el);
    const char one[1][33] = {G_GREY_A};
    CHECK(store_fonts_with_glyphs(c, one, 1, &el, err) && el.count == 1 && strcmp(el.e[0].pxfm, H_GREY) == 0, "the grey A is only in the grey font");
    store_entry_list_free(&el);
    StoreCodeGlyphList cg;
    CHECK(store_glyphs_for_code(c, 0x41, &cg, err) && cg.count == 2 && cg.g[0].nFonts == 3 && strcmp(cg.g[0].glyph, G_A) == 0 && cg.g[0].width == 5 && cg.g[0].height == 5, "code U+0041: 2 distinct bitmaps, the common one first");
    store_code_glyph_list_free(&cg);
    CHECK(store_glyphs_for_code(c, 0x110000, &cg, err) && cg.count == 0, "code above 10FFFF: none");
    store_code_glyph_list_free(&cg);
    CHECK(store_glyphs_for_code(c, 0x2C, &cg, err) && cg.count == 1 && cg.g[0].yMin == -2, "comma: one bitmap, y -2");
    store_code_glyph_list_free(&cg);
    store_conn_close(c);
    store_close(s);
}

/* ------------------------------------------------------------------ saving */

static PxGlyph make_glyph(int adv, int x, int y, const char *rows) {
    PxGlyph g;
    memset(&g, 0, sizeof g);
    g.advance = adv; g.x = x; g.y = y;
    int w = 0, h = 0;
    const char *p = rows;
    while (*p && *p != '\n') { w++; p++; }
    for (p = rows; *p; p++) if (*p == '\n') h++;
    if (rows[strlen(rows) - 1] != '\n') h++;
    g.w = w; g.h = h;
    g.pix = (char *)malloc((size_t)w * (size_t)h);
    int o = 0;
    for (p = rows; *p; p++) if (*p != '\n') g.pix[o++] = *p;
    return g;
}

/* codes U+0021..U+003A (20 glyphs: more than the 10 shared reference code points the shape score needs) */
static void shape_font(PxFont *f, const char *rows, int adv, int ascent) {
    px_font_init(f);
    f->has[PXM_ASCENT] = true; f->m[PXM_ASCENT] = ascent;
    f->has[PXM_DESCENT] = true; f->m[PXM_DESCENT] = 2;
    for (int c = 0x21; c <= 0x3A; c++) {
        PxGlyph g = make_glyph(adv, 0, 0, rows);
        g.code = c;
        px_font_add_glyph(f, &g);
        px_glyph_free(&g);
    }
}

/* ------------------------------------------------------------------ files, snapshots */

typedef struct SnapFile { char path[440]; char *data; size_t len; } SnapFile;
typedef struct Snap { SnapFile *f; int n, cap; } Snap;

static void snap_walk(const char *dir, Snap *s) {
    char pat[420];
    snprintf(pat, sizeof pat, "%s/*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char p[420];
        snprintf(p, sizeof p, "%s/%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { snap_walk(p, s); continue; }
        if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 64; s->f = (SnapFile *)realloc(s->f, sizeof(SnapFile) * (size_t)s->cap); }
        SnapFile *f = &s->f[s->n++];
        snprintf(f->path, sizeof f->path, "%s", p);
        f->data = px_read_file(p, &f->len);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static Snap snap_take(const char *dir) { Snap s; memset(&s, 0, sizeof s); snap_walk(dir, &s); return s; }

/* files of the snapshot that are missing or changed now, not counting those whose path contains one of skip[] */
static int snap_changed(const Snap *s, const char **skip, int nskip) {
    int bad = 0;
    for (int i = 0; i < s->n; i++) {
        bool sk = false;
        for (int k = 0; k < nskip; k++) if (strstr(s->f[i].path, skip[k])) sk = true;
        if (sk) continue;
        size_t len;
        char *d = px_read_file(s->f[i].path, &len);
        if (!d || len != s->f[i].len || memcmp(d, s->f[i].data, len) != 0) { bad++; printf("      changed or missing: %s\n", s->f[i].path); }
        free(d);
    }
    return bad;
}

static int snap_count_in(const char *dir) { Snap s = snap_take(dir); int n = s.n; for (int i = 0; i < s.n; i++) free(s.f[i].data); free(s.f); return n; }
static void snap_free(Snap *s) { for (int i = 0; i < s->n; i++) free(s->f[i].data); free(s->f); memset(s, 0, sizeof *s); }

static bool same_file(const char *a, const char *b) {
    size_t la, lb;
    char *x = px_read_file(a, &la), *y = px_read_file(b, &lb);
    bool r = x && y && la == lb && memcmp(x, y, la) == 0;
    free(x); free(y);
    return r;
}

static long sql_int(Store *s, const char *sql) {
    StoreConn *c = store_conn_open(s);
    sqlite3_stmt *st;
    long v = -1;
    if (sqlite3_prepare_v2(store_sqlite(c), sql, -1, &st, NULL) == SQLITE_OK) { if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0); sqlite3_finalize(st); }
    store_conn_close(c);
    return v;
}

static long file_sql_int(const char *path, const char *sql) {
    sqlite3 *db = NULL;
    long v = -1;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) { if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0); sqlite3_finalize(st); }
    }
    sqlite3_close(db);
    return v;
}

static bool dir_has(const char *dir, const char *rel) {
    char p[460];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

/* every file of LIB exists under `dir` with the same bytes, and `dir` holds nothing of pxtf/pxfm/pxgl besides: counts */
static int lib_differences(const char *dir) {
    Snap lib = snap_take(LIB);
    int diff = 0;
    for (int i = 0; i < lib.n; i++) {
        char other[460];
        snprintf(other, sizeof other, "%s%s", dir, lib.f[i].path + strlen(LIB));
        if (!same_file(lib.f[i].path, other)) { diff++; printf("      differs or missing: %s\n", other); }
    }
    snap_free(&lib);
    return diff;
}

static int lib_file_count(void) { return snap_count_in(LIB); }

static void test_import(void) {
    char err[PX_ERR_LEN];
    Store *a = NULL, *b = NULL, *c = NULL;
    CHECK(store_open_pxfdb(DB, NULL, &a, err), "pxfdb opened");
    rm_rf(TMP "/imp_a");
    px_mkdir_p(TMP "/imp_a");
    StoreProgress prog;
    memset(&prog, 0, sizeof prog);
    CHECK(store_import_folder(LIB, TMP "/imp_a/new.pxfdb", &prog, &b, err), "import of the testdata folder into a new pxfdb");
    if (!a || !b) return;
    CHECK(prog.phase == 0 && !store_modified(b) && strcmp(store_info(b)->path, TMP "/imp_a/new.pxfdb") == 0, "progress back to idle; the new file is the store's file; not modified");
    CHECK(same_content(a, b), "store imported from files == store opened from the pxfdb (8 tables, content checksums)");
    CHECK(px_file_exists(TMP "/imp_a/new.pxfdb") && !px_file_exists(TMP "/imp_a/new.pxfdb.tmp"), "the file was written, no temporary file is left");
    CHECK(store_open_pxfdb(TMP "/imp_a/new.pxfdb", NULL, &c, err) && same_content(a, c), "the written file loads and has the same content");
    store_close(c);
    c = NULL;
    CHECK(store_import_to_file(LIB, TMP "/imp_a/backup.pxfdb", NULL, err) && file_sql_int(TMP "/imp_a/backup.pxfdb", "SELECT COUNT(*) FROM pxfm") == 4, "store_import_to_file: the backup form");
    store_close(a);
    store_close(b);

    /* a failing file: nothing is written, an existing target stays as it was */
    char sub[300];
    snprintf(sub, sizeof sub, "%s", TMP "/store_root_cp");
    rm_rf(sub);
    copy_tree(LIB, sub);
    px_mkdir_p(TMP "/store_root_cp/pxgl/00");
    px_write_file(TMP "/store_root_cp/pxgl/00/00000000000000000000000000000000.pxgl", "0\n", 2);
    CHECK(px_write_file(TMP "/imp_a/old.pxfdb", "OLD", 3), "an old target file");
    c = NULL;
    CHECK(!store_import_folder(sub, TMP "/imp_a/old.pxfdb", NULL, &c, err) && err[0] && strstr(err, "00000000000000000000000000000000"), "a pxgl whose content hash is not its name stops the import, naming the file");
    printf("      error was: %s\n", err);
    { size_t l; char *d = px_read_file(TMP "/imp_a/old.pxfdb", &l); CHECK(d && l == 3 && memcmp(d, "OLD", 3) == 0 && !px_file_exists(TMP "/imp_a/old.pxfdb.tmp"), "the old target file is untouched, no temporary file"); free(d); }
    CHECK(!store_import_folder(TMP "/imp_a/no-such-folder", TMP "/imp_a/x.pxfdb", NULL, &c, err) && !px_file_exists(TMP "/imp_a/x.pxfdb"), "a missing folder is an error and writes nothing");
    px_mkdir_p(TMP "/imp_empty");
    CHECK(!store_import_folder(TMP "/imp_empty", TMP "/imp_a/x.pxfdb", NULL, &c, err) && !px_file_exists(TMP "/imp_a/x.pxfdb"), "a folder without library files is an error and writes nothing");

    StoreFolderScan sc;
    store_scan_folder(LIB, &sc);
    CHECK(sc.found.fonts == 4 && sc.found.typefaces == 2 && sc.found.glyphs == 6 && sc.found.palettes == 1 && sc.other == 0 && store_scan_has_data(&sc), "scan of the testdata folder: 4 fonts, 2 typefaces, 6 glyphs, 1 palette");
    store_scan_folder(TMP "/imp_empty", &sc);
    CHECK(!store_scan_has_data(&sc), "scan of an empty folder: no data");
}

static PxFont g_f;

static void test_save(void) {
    char err[PX_ERR_LEN];
    rm_rf(TMP "/sv");
    px_mkdir_p(TMP "/sv");
    CopyFileA(DB, TMP "/sv/a.pxfdb", FALSE);
    Store *s = NULL;
    CHECK(store_open_pxfdb(TMP "/sv/a.pxfdb", NULL, &s, err) && !store_modified(s), "load: a copy of the pxfdb, not modified");
    if (!s) return;
    shape_font(&g_f, "00000\n0...0\n0...0\n0...0\n00000", 6, 5);
    PxtfTypeface tf;
    pxtf_init(&tf);
    StoreConn *c = store_conn_open(s);
    CHECK(store_load_typeface(c, T_SANS, &tf, err), "typeface to duplicate");
    store_conn_close(c);
    StoreSaveResult r;
    CHECK(store_save_to_memory(s, &g_f, &tf, 1, &r, err) && r.newPxfm && r.newPxtf && r.newGlyphs == 1, "a font committed into memory: a new font, typeface and glyph");
    CHECK(store_modified(s), "memory differs from the file: modified");
    CHECK(count_rows(s, "pxfm") == 5 && count_rows(s, "glyph") == 7, "the rows are in memory");
    CHECK(file_sql_int(TMP "/sv/a.pxfdb", "SELECT COUNT(*) FROM pxfm") == 4, "the file still has 4 fonts: only Save writes it");
    CHECK(same_file(DB, TMP "/sv/a.pxfdb"), "the file is byte for byte the loaded one");
    StoreSaveResult r2;
    CHECK(store_save_to_memory(s, &g_f, &tf, 1, &r2, err) && !r2.newPxfm && !r2.newPxtf && r2.newGlyphs == 0, "committing the same again adds nothing");
    CHECK(store_save(s, err), "Save");
    CHECK(!store_modified(s) && !px_file_exists(TMP "/sv/a.pxfdb.tmp"), "after Save: not modified; no temporary file is left");
    CHECK(file_sql_int(TMP "/sv/a.pxfdb", "SELECT COUNT(*) FROM pxfm") == 5 && file_sql_int(TMP "/sv/a.pxfdb", "SELECT COUNT(*) FROM glyph") == 7 && file_sql_int(TMP "/sv/a.pxfdb", "SELECT COUNT(*) FROM pxtf") == 3, "the file has the new rows (SQL)");
    Store *chk = NULL;
    CHECK(store_open_pxfdb(TMP "/sv/a.pxfdb", NULL, &chk, err) && same_content(s, chk), "reloading the file gives the content of memory");
    store_close(chk);
    CHECK(store_save_to_memory(s, &g_f, &tf, 1, &r2, err) && !store_modified(s), "committing what is there changes nothing: still not modified");

    /* Save As: continues on the new file; the old one is left */
    CHECK(store_save_to_memory(s, &g_f, NULL, 0, &r2, err) && !r2.newPxfm && !store_modified(s), "a font that is there already: no change");
    PxFont g2;
    shape_font(&g2, "0.0.0\n.0.0.\n0.0.0\n.0.0.\n0.0.0", 6, 5);
    CHECK(store_save_to_memory(s, &g2, NULL, 0, &r2, err) && store_modified(s), "another font: modified again");
    CHECK(store_save_as(s, TMP "/sv/b.pxfdb", err) && !store_modified(s) && strcmp(store_info(s)->path, TMP "/sv/b.pxfdb") == 0, "Save As: written, the store continues on it");
    CHECK(file_sql_int(TMP "/sv/b.pxfdb", "SELECT COUNT(*) FROM pxfm") == 6 && file_sql_int(TMP "/sv/a.pxfdb", "SELECT COUNT(*) FROM pxfm") == 5, "the new file has 6 fonts, the old one still 5");

    /* a failing replace leaves the old file and the modified state */
    px_mkdir_p(TMP "/sv/adir");
    PxFont g3;
    shape_font(&g3, "0.0.0\n0.0.0\n0.0.0\n0.0.0\n0.0.0", 6, 5);
    CHECK(store_save_to_memory(s, &g3, NULL, 0, &r2, err) && store_modified(s), "a third font: modified");
    CHECK(!store_save_as(s, TMP "/sv/adir", err) && err[0] && store_modified(s) && strcmp(store_info(s)->path, TMP "/sv/b.pxfdb") == 0 && !px_file_exists(TMP "/sv/adir.tmp"), "a failing replace: error, no temporary file, path and modified state unchanged");
    CHECK(file_sql_int(TMP "/sv/b.pxfdb", "SELECT COUNT(*) FROM pxfm") == 6, "and the old file still has its 6 fonts");
    CHECK(store_save(s, err) && file_sql_int(TMP "/sv/b.pxfdb", "SELECT COUNT(*) FROM pxfm") == 7, "Save then works");
    store_close(s);
    px_font_free(&g2);
    px_font_free(&g3);

    /* editing in place: the font changes its hash, its typeface follows, the old font and its glyphs go */
    CopyFileA(DB, TMP "/sv/c.pxfdb", FALSE);
    CHECK(store_open_pxfdb(TMP "/sv/c.pxfdb", NULL, &s, err), "edit: load");
    if (s) {
        long g0 = count_rows(s, "glyph"), f0 = count_rows(s, "pxfm"), t0 = count_rows(s, "pxtf");
        PxFont f;
        px_font_init(&f);
        StoreConn *cc = store_conn_open(s);
        CHECK(store_load_font(cc, H_EXAMPLE, NULL, 0, &f, err), "edit: load the font");
        store_conn_close(cc);
        StoreEditResult er;
        CHECK(store_edit_in_place(s, H_EXAMPLE, NULL, NULL, NULL, &er, err) && strcmp(er.pxfm, H_EXAMPLE) == 0 && er.nMoved == 0 && count_rows(s, "pxfm") == f0, "edit: nothing changed, nothing done");
        f.m[PXM_ASCENT]++;
        CHECK(store_edit_in_place(s, H_EXAMPLE, &f, T_SANS, NULL, &er, err), "edit: the font edited in place");
        CHECK(strcmp(er.pxfm, H_EXAMPLE) != 0 && er.nMoved == 1 && strcmp(er.movedFrom[0], T_SANS) == 0 && strcmp(er.pxtf, er.movedTo[0]) == 0 && strcmp(er.pxtf, T_SANS) != 0, "edit: a new font hash, the typeface has a new one");
        cc = store_conn_open(s);
        CHECK(!store_has_pxfm(cc, H_EXAMPLE) && store_has_pxfm(cc, er.pxfm) && !store_has_pxtf(cc, T_SANS) && store_has_pxtf(cc, er.pxtf), "edit: the old font and typeface are gone, the new ones are there");
        PxtfTypeface nt;
        pxtf_init(&nt);
        CHECK(store_load_typeface(cc, er.pxtf, &nt, err) && pxtf_find_variant(&nt, 1) && strcmp(pxtf_find_variant(&nt, 1)->pxfm, er.pxfm) == 0 && nt.count == 2, "edit: variant 1 of the typeface names the new font, variant 2 is as it was");
        store_conn_close(cc);
        CHECK(count_rows(s, "pxfm") == f0 && count_rows(s, "pxtf") == t0 && count_rows(s, "glyph") == g0, "edit: the same number of fonts, typefaces and glyphs (the glyphs are shared with the new font)");
        CHECK(sql_int(s, "SELECT COUNT(*) FROM pxfm_glyph WHERE glyph IS NOT NULL AND glyph NOT IN (SELECT hash FROM glyph)") == 0, "edit: no dangling glyph line");
        /* a glyph edited: the glyph only the old font had goes */
        char oldH[33];
        snprintf(oldH, sizeof oldH, "%s", er.pxfm);
        int gi = px_font_find(&f, 0x41);
        PxGlyph *g = &f.glyphs[gi];
        g->pix[0] = g->pix[0] == '0' ? '.' : '0';
        px_glyph_touch(g);
        StoreEditResult e2;
        CHECK(store_edit_in_place(s, oldH, &f, er.pxtf, NULL, &e2, err) && strcmp(e2.pxfm, oldH) != 0 && count_rows(s, "pxfm") == f0 && count_rows(s, "glyph") <= g0 + 1, "edit: a glyph edited: the new bitmap comes, the old font goes");
        /* only the typeface edited */
        PxtfTypeface c2;
        pxtf_init(&c2);
        cc = store_conn_open(s);
        CHECK(store_load_typeface(cc, e2.pxtf, &c2, err), "edit: load the typeface");
        store_conn_close(cc);
        pxtf_set_text(pxtf_find_variant(&c2, 1), PXTF_FAMILY, "Edited Sans");
        StoreEditResult e3;
        CHECK(store_edit_in_place(s, e2.pxfm, NULL, e2.pxtf, &c2, &e3, err) && strcmp(e3.pxfm, e2.pxfm) == 0 && strcmp(e3.pxtf, e2.pxtf) != 0 && count_rows(s, "pxtf") == t0 && store_has_pxfm(store_main_conn(s), e2.pxfm), "edit: a typeface field edit: a new typeface hash, the font stays");
        /* two typefaces name the font: both follow */
        PxtfTypeface c3;
        pxtf_init(&c3);
        CHECK(pxtf_copy(&c3, &c2), "edit: copy the typeface");
        pxtf_set_text(pxtf_find_variant(&c3, 1), PXTF_FAMILY, "Twin Sans");
        StoreSaveResult dummy;
        PxFont fe;
        px_font_init(&fe);
        cc = store_conn_open(s);
        CHECK(store_load_font(cc, e2.pxfm, NULL, 0, &fe, err), "edit: reload the edited font");
        store_conn_close(cc);
        CHECK(store_save_to_memory(s, &fe, &c3, 1, &dummy, err) && count_rows(s, "pxtf") == t0 + 1, "edit: a second typeface names the font");
        fe.m[PXM_ASCENT]++;
        StoreEditResult e4;
        CHECK(store_edit_in_place(s, e2.pxfm, &fe, e3.pxtf, NULL, &e4, err) && e4.nMoved == 2 && count_rows(s, "pxtf") == t0 + 1 && count_rows(s, "pxfm") == f0 &&
              sql_int(s, "SELECT COUNT(*) FROM pxtf_variant v WHERE v.pxfm NOT IN (SELECT hash FROM pxfm)") == 0, "edit: both typefaces follow, nothing dangles");
        pxtf_free(&nt); pxtf_free(&c2); pxtf_free(&c3);
        px_font_free(&f); px_font_free(&fe);
        store_close(s);
    }
    pxtf_free(&tf);
    px_font_free(&g_f);
}

static void test_export(void) {
    char err[PX_ERR_LEN];
    Store *s = NULL;
    CHECK(store_open_pxfdb(DB, NULL, &s, err), "export: load");
    if (!s) return;
    StoreExportResult res;
    StoreProgress prog;
    memset(&prog, 0, sizeof prog);

    /* into an empty folder */
    rm_rf(TMP "/ex_a");
    CHECK(store_export_folder(s, TMP "/ex_a", STORE_EXPORT_APPEND, &prog, &res, err), "export into a new folder");
    CHECK(res.written.fonts == 4 && res.written.typefaces == 2 && res.written.glyphs == 6 && res.written.palettes == 1 && res.skipped.fonts + res.skipped.typefaces + res.skipped.glyphs == 0 && prog.phase == 0, "wrote 4 fonts, 2 typefaces, 6 glyphs, 1 palette; nothing skipped");
    CHECK(lib_differences(TMP "/ex_a") == 0 && snap_count_in(TMP "/ex_a") == lib_file_count(), "the 13 files are byte for byte the library's files");

    /* Append on a partial folder: existing files untouched, missing ones added */
    rm_rf(TMP "/ex_b");
    copy_tree(LIB, TMP "/ex_b");
    DeleteFileA(TMP "/ex_b/pxfm/08/" H_EXAMPLE ".pxfm");
    DeleteFileA(TMP "/ex_b/pxgl/c0/" G_A ".pxgl");
    px_write_file(TMP "/ex_b/pxfm/40/" H_GREY ".pxfm", "EDITED\n", 7);
    px_write_file(TMP "/ex_b/notes.txt", "mine\n", 5);
    px_mkdir_p(TMP "/ex_b/other");
    px_write_file(TMP "/ex_b/other/x.txt", "x", 1);
    Snap before = snap_take(TMP "/ex_b");
    CHECK(store_export_folder(s, TMP "/ex_b", STORE_EXPORT_APPEND, NULL, &res, err), "Append");
    CHECK(res.written.fonts == 1 && res.written.glyphs == 1 && res.written.typefaces == 0 && res.skipped.fonts == 3 && res.skipped.typefaces == 2 && res.skipped.glyphs == 5 && res.skipped.palettes == 1 && res.deleted.fonts == 0, "Append: wrote the 2 missing files, skipped the 11 that were there, deleted nothing");
    CHECK(snap_changed(&before, NULL, 0) == 0 && snap_count_in(TMP "/ex_b") == before.n + 2, "Append: every earlier file is byte for byte the same (the edited one too); exactly 2 added");
    CHECK(same_file(TMP "/ex_b/pxfm/08/" H_EXAMPLE ".pxfm", LIB "/pxfm/08/" H_EXAMPLE ".pxfm") && same_file(TMP "/ex_b/pxgl/c0/" G_A ".pxgl", LIB "/pxgl/c0/" G_A ".pxgl"), "Append: the new files are the library's bytes");
    snap_free(&before);

    /* Overwrite: only the contents of pxtf/ pxfm/ pxgl/ go */
    CHECK(store_export_folder(s, TMP "/ex_b", STORE_EXPORT_OVERWRITE, &prog, &res, err), "Overwrite");
    CHECK(res.deleted.fonts == 4 && res.deleted.typefaces == 2 && res.deleted.glyphs == 6 && res.deleted.palettes == 1 && res.written.fonts == 4 && res.written.typefaces == 2 && res.written.glyphs == 6 && res.written.palettes == 1 && res.skipped.fonts == 0, "Overwrite: deleted 13 files, wrote 13, skipped none");
    CHECK(lib_differences(TMP "/ex_b") == 0 && dir_has(TMP "/ex_b", "notes.txt") && dir_has(TMP "/ex_b", "other/x.txt"), "Overwrite: pxtf/ pxfm/ pxgl/ equal the library; other files and folders of the folder stay");
    CHECK(snap_count_in(TMP "/ex_b") == lib_file_count() + 2, "Overwrite: nothing else is in the folder (13 + the 2 other files)");

    /* Overwrite removes stray files and folders inside the three folders */
    px_write_file(TMP "/ex_b/pxgl/stray.txt", "s", 1);
    px_mkdir_p(TMP "/ex_b/pxfm/zz/deep");
    px_write_file(TMP "/ex_b/pxfm/zz/deep/y.bin", "y", 1);
    CHECK(store_export_folder(s, TMP "/ex_b", STORE_EXPORT_OVERWRITE, NULL, &res, err) && res.deleted.glyphs == 7 && res.deleted.fonts == 5, "Overwrite: stray files inside the three folders are deleted too (counted)");
    CHECK(!dir_has(TMP "/ex_b", "pxfm/zz") && !dir_has(TMP "/ex_b", "pxgl/stray.txt") && snap_count_in(TMP "/ex_b") == lib_file_count() + 2, "Overwrite: and they are gone");

    /* what memory has beyond the file: a committed font is exported */
    shape_font(&g_f, "00000\n0...0\n0...0\n0...0\n00000", 6, 5);
    StoreSaveResult r;
    CHECK(store_save_to_memory(s, &g_f, NULL, 0, &r, err), "export: commit a new font into memory");
    rm_rf(TMP "/ex_c");
    CHECK(store_export_folder(s, TMP "/ex_c", STORE_EXPORT_APPEND, NULL, &res, err) && res.written.fonts == 5 && res.written.glyphs == 7, "export of memory: 5 fonts, 7 glyphs");
    char np[330];
    snprintf(np, sizeof np, TMP "/ex_c/pxfm/%.2s/%s.pxfm", r.pxfm, r.pxfm);
    PxFont back;
    CHECK(px_file_exists(np) && px_read_pxfm(np, NULL, &back, err) && strcmp(back.fileHash, r.pxfm) == 0, "the new pxfm reads back with the strict reader, named by its hash");
    px_font_free(&back);
    px_font_free(&g_f);
    store_close(s);
}

/* ------------------------------------------------------------------ deletions */

static void test_deletions(void) {
    char err[PX_ERR_LEN];
    CopyFileA(DB, TMP "/del_a.pxfdb", FALSE);
    Store *s = NULL;
    CHECK(store_open_pxfdb(TMP "/del_a.pxfdb", NULL, &s, err), "del: open a copy of the pxfdb");
    if (!s) return;
    StoreDeletePlan p;

    /* a pre-existing orphan stays */
    {
        StoreConn *c = store_conn_open(s);
        char *e = NULL;
        sqlite3_exec(store_sqlite(c), "INSERT INTO glyph VALUES (x'00112233445566778899aabbccddeeff', 1, 1, '0')", NULL, NULL, &e);
        sqlite3_free(e);
        store_conn_close(c);
    }
    long glyphsBefore = count_rows(s, "glyph");
    /* the glyphs that only the italic font uses */
    long only = sql_int(s, "SELECT COUNT(DISTINCT glyph) FROM pxfm_glyph a WHERE pxfm = x'" "e6109c134eda8d747bac9a3c2a62d204" "' AND glyph IS NOT NULL"
                           " AND NOT EXISTS (SELECT 1 FROM pxfm_glyph b WHERE b.glyph = a.glyph AND b.pxfm <> a.pxfm)");
    long shared = sql_int(s, "SELECT COUNT(DISTINCT glyph) FROM pxfm_glyph a WHERE pxfm = x'" "e6109c134eda8d747bac9a3c2a62d204" "' AND glyph IS NOT NULL"
                             " AND EXISTS (SELECT 1 FROM pxfm_glyph b WHERE b.glyph = a.glyph AND b.pxfm <> a.pxfm)");
    printf("      the italic font: %ld glyphs only it uses, %ld shared with others\n", only, shared);

    /* 1: a font that is one of two variants: its typeface is rewritten */
    CHECK(store_plan_delete_font(s, H_NOASC, &p, err), "del 1: plan: delete the font of variant 0002 of Test Sans");
    printf("      %s\n", p.summary);
    CHECK(p.nFonts == 1 && p.nWhole == 0 && p.nRw == 1 && p.nGone == 1 && strcmp(p.rw[0].oldPxtf, T_SANS) == 0 && p.rw[0].nLost == 1 && p.rw[0].lost[0] == 2, "del 1: one font; the typeface loses variant 2 and is rewritten");
    CHECK(p.nGlyphs == only, "del 1: the glyphs that only it used are deleted, the shared ones stay");
    CHECK(p.newTf[0].count == 1 && p.newTf[0].v[0].number == 1 && strcmp(p.rw[0].newPxtf, T_SANS) != 0, "del 1: the new typeface has variant 0001 and another hash");
    CHECK(count_rows(s, "pxfm") == 4, "del 1: planning changed nothing");
    char newTs[33];
    snprintf(newTs, sizeof newTs, "%s", p.rw[0].newPxtf);
    CHECK(store_apply_delete(s, &p, err), "del 1: applied in Memory");
    store_plan_free(&p);
    CHECK(count_rows(s, "pxfm") == 3 && count_rows(s, "pxtf") == 2 && count_rows(s, "pxtf_variant") == 2 && count_rows(s, "glyph") == glyphsBefore - only, "del 1: 3 fonts, 2 typefaces (rewritten), 2 variants; its glyphs gone");
    {
        StoreConn *c = store_conn_open(s);
        PxtfTypeface t;
        CHECK(store_load_typeface(c, newTs, &t, err) && t.count == 1 && t.v[0].number == 1 && strcmp(t.v[0].pxfm, H_EXAMPLE) == 0, "del 1: the rewritten typeface loads and names the other font");
        pxtf_free(&t);
        CHECK(!store_has_pxtf(c, T_SANS) && store_has_pxtf(c, newTs), "del 1: the old typeface row is gone");
        store_conn_close(c);
    }
    CHECK(sql_int(s, "SELECT COUNT(*) FROM glyph WHERE hash = x'00112233445566778899aabbccddeeff'") == 1, "del 1: a glyph that was an orphan before stays");
    CHECK(store_modified(s), "del 1: memory is modified");
    CHECK(store_save(s, err) && !store_modified(s), "del 1: Save writes the deletion into the file");
    CHECK(file_sql_int(TMP "/del_a.pxfdb", "SELECT COUNT(*) FROM pxfm") == 3 && file_sql_int(TMP "/del_a.pxfdb", "SELECT COUNT(*) FROM pxtf") == 2 &&
          file_sql_int(TMP "/del_a.pxfdb", "SELECT COUNT(*) FROM pxfm_glyph WHERE pxfm NOT IN (SELECT hash FROM pxfm)") == 0 &&
          file_sql_int(TMP "/del_a.pxfdb", "SELECT COUNT(*) FROM pxtf_variant WHERE pxfm NOT IN (SELECT hash FROM pxfm)") == 0, "del 1: the file has no row of the font and no dangling row");
    {
        Store *re = NULL;
        CHECK(store_open_pxfdb(TMP "/del_a.pxfdb", NULL, &re, err) && count_rows(re, "glyph") == glyphsBefore - only && same_content(s, re), "del 1: reloaded: the same rows as memory (the orphan row stays, as it was an orphan before)");
        store_close(re);
    }

    /* 2: its other font: the typeface is left with no variant and goes */
    CHECK(store_plan_delete_font(s, H_EXAMPLE, &p, err) && p.nFonts == 1 && p.nWhole == 1 && p.nRw == 0 && p.nGone == 1, "del 2: the last variant: the typeface is deleted");
    printf("      %s\n", p.summary);
    CHECK(store_apply_delete(s, &p, err), "del 2: applied");
    store_plan_free(&p);
    CHECK(count_rows(s, "pxtf") == 1 && count_rows(s, "pxfm") == 2, "del 2: 2 fonts, 1 typeface left");

    /* 3: a font named by two typefaces: deleting a typeface keeps it; deleting the font rewrites/deletes both */
    {
        PxtfTypeface t2;
        pxtf_init(&t2);
        StoreConn *c = store_conn_open(s);
        CHECK(store_load_typeface(c, T_UNC, &t2, err), "del 3: load Uncoded Mono");
        store_conn_close(c);
        free(t2.v[0].text[PXTF_FAMILY]);
        t2.v[0].text[PXTF_FAMILY] = (char *)malloc(8);
        strcpy(t2.v[0].text[PXTF_FAMILY], "Twin");
        PxFont fu;
        px_font_init(&fu);
        c = store_conn_open(s);
        CHECK(store_load_font(c, H_UNC, NULL, 0, &fu, err), "del 3: load the font");
        store_conn_close(c);
        StoreSaveResult r;
        CHECK(store_save_to_memory(s, &fu, &t2, 1, &r, err) && !r.newPxfm && r.newPxtf, "del 3: a second typeface naming the same font");
        px_font_free(&fu);
        pxtf_free(&t2);
        CHECK(store_plan_delete_typeface(s, T_UNC, &p, err) && p.nFonts == 0 && p.nGone == 1 && p.nGlyphs == 0, "del 3: deleting one typeface keeps the font and its glyphs");
        printf("      %s\n", p.summary);
        store_plan_free(&p);
        CHECK(store_plan_delete_font(s, H_UNC, &p, err) && p.nFonts == 1 && p.nWhole == 2 && p.nGone == 2, "del 3: deleting the font deletes both typefaces");
        store_plan_free(&p);
        CHECK(store_plan_delete_typeface(s, r.pxtf, &p, err) && p.nFonts == 0, "del 3: the other one keeps it too");
        CHECK(store_apply_delete(s, &p, err), "del 3: applied");
        store_plan_free(&p);
        CHECK(count_rows(s, "pxfm") == 2 && count_rows(s, "pxtf") == 1, "del 3: 2 fonts, 1 typeface");
        CHECK(store_plan_delete_typeface(s, T_UNC, &p, err) && p.nFonts == 1, "del 3: now the last typeface of the font takes it with it");
        CHECK(store_apply_delete(s, &p, err), "del 3: applied");
        store_plan_free(&p);
        CHECK(count_rows(s, "pxfm") == 1 && count_rows(s, "pxtf") == 0, "del 3: one font (the grey one) and no typeface");
    }
    CHECK(!store_plan_delete_font(s, H_UNC, &p, err) && err[0], "del: deleting a font that is not in Memory is an error");
    store_close(s);

    /* 4: a deletion, Save, and Export: the folder has no file of what was deleted */
    CopyFileA(DB, TMP "/del_b.pxfdb", FALSE);
    Store *s2 = NULL;
    CHECK(store_open_pxfdb(TMP "/del_b.pxfdb", NULL, &s2, err), "del 4: open a copy");
    if (!s2) return;
    CHECK(store_plan_delete_typeface(s2, T_UNC, &p, err) && p.nFonts == 1 && p.nWhole == 1, "del 4: delete the typeface Uncoded Mono, it takes its font with it");
    CHECK(store_apply_delete(s2, &p, err), "del 4: applied");
    store_plan_free(&p);
    CHECK(store_save(s2, err), "del 4: Save");
    store_close(s2);
    CHECK(store_open_pxfdb(TMP "/del_b.pxfdb", NULL, &s2, err) && store_info(s2)->nPxfm == 3 && store_info(s2)->nPxtf == 1, "del 4: reloaded: 3 fonts, 1 typeface, nothing resurrected");
    if (s2) {
        StoreExportResult er;
        rm_rf(TMP "/del_root");
        CHECK(store_export_folder(s2, TMP "/del_root", STORE_EXPORT_APPEND, NULL, &er, err) && er.written.fonts == 3 && er.written.typefaces == 1, "del 4: Export writes only what is left");
        CHECK(!px_file_exists(TMP "/del_root/pxfm/fa/" H_UNC ".pxfm") && !px_file_exists(TMP "/del_root/pxtf/b5/" T_UNC ".pxtf"), "del 4: the deleted font and typeface have no file");
        store_close(s2);
    }
}

/* ------------------------------------------------------------------ similarity */

/* The default glyph (FFFFFF) is a glyph of its own: a glyph that appears nowhere else survives save, reload, export and deletion rules */
static void test_default_glyph(void) {
    char err[PX_ERR_LEN];
    rm_rf(TMP "/dg");
    px_mkdir_p(TMP "/dg");
    CopyFileA(DB, TMP "/dg/a.pxfdb", FALSE);
    Store *s = NULL;
    CHECK(store_open_pxfdb(TMP "/dg/a.pxfdb", NULL, &s, err), "default glyph: open a copy");
    if (!s) return;
    PxFont f;
    shape_font(&f, "00000\n0...0\n0...0\n0...0\n00000", 6, 5);
    PxGlyph d = make_glyph(7, 1, -1, "0.0\n.0.\n0.0");
    d.code = PX_CODE_DEFAULT;
    px_font_add_glyph(&f, &d);
    px_glyph_free(&d);
    long glyphs0 = count_rows(s, "glyph");
    StoreSaveResult r;
    CHECK(store_save_to_memory(s, &f, NULL, 0, &r, err) && r.newPxfm && r.newGlyphs == 2, "default glyph: the font and both bitmaps (the default's too) are new");
    CHECK(count_rows(s, "glyph") == glyphs0 + 2, "default glyph: its bitmap is in the glyph table");
    char q[400];
    snprintf(q, sizeof q, "SELECT COUNT(*) FROM pxfm_glyph WHERE code = 16777215 AND x = 1 AND y = -1 AND advance = 7 AND pxfm = x'%s'", r.pxfm);
    CHECK(sql_int(s, q) == 1, "default glyph: one FFFFFF row with its own offsets and advance");
    StoreConn *c = store_conn_open(s);
    PxFont l;
    CHECK(store_load_font(c, r.pxfm, NULL, 0, &l, err) && l.count == f.count && px_font_default_index(&l) == l.count - 1, "default glyph: loads as the last glyph");
    int di = px_font_default_index(&l);
    CHECK(di >= 0 && l.glyphs[di].x == 1 && l.glyphs[di].y == -1 && l.glyphs[di].advance == 7 && l.glyphs[di].w == 3 && l.glyphs[di].h == 3 && l.glyphs[di].pix && l.glyphs[di].pix[0] == '0' && l.glyphs[di].pix[1] == '.', "default glyph: the bitmap came back");
    CHECK(px_font_find(&l, 0x21) >= 0 && l.glyphs[px_font_find(&l, 0x21)].w == 5, "default glyph: the other glyphs are not touched");
    px_font_free(&l);
    store_conn_close(c);
    CHECK(store_save(s, err) && file_sql_int(TMP "/dg/a.pxfdb", "SELECT COUNT(*) FROM pxfm_glyph WHERE code = 16777215") == 3, "default glyph: Save keeps the FFFFFF row (3 fonts have one)");
    Store *chk = NULL;
    CHECK(store_open_pxfdb(TMP "/dg/a.pxfdb", NULL, &chk, err) && same_content(s, chk), "default glyph: the reloaded file has the same content");
    store_close(chk);
    StoreExportResult xr;
    /* export and import: the line and the glyph file are there */
    CHECK(store_export_folder(s, TMP "/dg/out", STORE_EXPORT_APPEND, NULL, &xr, err), "default glyph: export");
    CHECK(store_import_to_file(TMP "/dg/out", TMP "/dg/b.pxfdb", NULL, err), "default glyph: import of the exported folder");
    CHECK(file_sql_int(TMP "/dg/b.pxfdb", "SELECT COUNT(*) FROM pxfm_glyph WHERE code = 16777215") == 3 && file_sql_int(TMP "/dg/b.pxfdb", "SELECT COUNT(*) FROM glyph") == file_sql_int(TMP "/dg/a.pxfdb", "SELECT COUNT(*) FROM glyph"), "default glyph: nothing dropped by export and import");
    /* deleting the font removes the glyph only the default used */
    StoreDeletePlan p;
    char hash[33];
    snprintf(hash, sizeof hash, "%s", r.pxfm);
    CHECK(store_plan_delete_font(s, hash, &p, err) && p.nGlyphs == 2 && store_apply_delete(s, &p, err), "default glyph: deleting the font removes its glyphs, the default's too");
    store_plan_free(&p);
    CHECK(count_rows(s, "glyph") == glyphs0, "default glyph: back to the original glyph rows");
    px_font_free(&f);
    store_close(s);
}

static void test_similarity(void) {
    char err[PX_ERR_LEN];
    CopyFileA(DB, TMP "/store_sim.pxfdb", FALSE);
    Store *s = NULL;
    if (!store_open_pxfdb(TMP "/store_sim.pxfdb", NULL, &s, err)) { CHECK(false, "open"); return; }
    PxFont x, x2, y, z;
    shape_font(&x, "00000\n00000\n00000\n00000\n00000", 6, 5);
    shape_font(&x2, "00000\n00000\n00000\n00000\n00000", 7, 6);        /* the same shapes, other metrics */
    shape_font(&y, "0.0.0\n.0.0.\n0.0.0\n.0.0.\n0.0.0", 6, 5);         /* a checkerboard */
    shape_font(&z, "0....\n.....\n.....\n.....\n.....", 6, 5);         /* a single pixel */
    StoreSaveResult rx, rx2, ry, rz;
    CHECK(store_save_to_memory(s, &x, NULL, 0, &rx, err) && store_save_to_memory(s, &x2, NULL, 0, &rx2, err) && store_save_to_memory(s, &y, NULL, 0, &ry, err) && store_save_to_memory(s, &z, NULL, 0, &rz, err), "four test fonts saved");
    double sec = 0;
    CHECK(store_similarity_prepare(s, &sec, err), "similarity cache prepared");
    StoreSimilar a, b, ab, ba, ay;
    CHECK(store_font_similarity(s, rx.pxfm, rx.pxfm, &a, err) && a.score > 99.999 && a.score < 100.001, "a font against itself scores 100");
    CHECK(store_font_similarity(s, rx.pxfm, rx2.pxfm, &ab, err) && store_font_similarity(s, rx2.pxfm, rx.pxfm, &ba, err), "scores of the pair, both directions");
    CHECK(ab.score == ba.score || (ab.score - ba.score < 1e-9 && ba.score - ab.score < 1e-9), "the score is symmetric");
    CHECK(ab.shapeUsed && ab.shape > 99.9 && ab.shared > 99.9 && ab.metrics < 100.0 && ab.score > 90.0 && ab.score < 100.0, "same shapes, other metrics: parts 100 / 100 / below 100, a high score");
    CHECK(store_font_similarity(s, rx.pxfm, ry.pxfm, &ay, err) && ay.score < ab.score && ay.shape < 60.0, "a checkerboard scores clearly lower than the same shapes");
    CHECK(store_font_similarity(s, rx.pxfm, rz.pxfm, &b, err) && b.score < ab.score && b.shape < 40.0, "a single pixel scores low against a solid block");
    CHECK(ab.score >= 0 && ab.score <= 100 && ay.score >= 0 && b.score >= 0, "scores stay within 0..100");
    CHECK(ab.score == ab.score && ab.sharedRef >= 10, "reference code points shared: at least 10");
    StoreSimilar top[5];
    int n = 0;
    CHECK(store_similar_fonts(s, rx.pxfm, 5, top, &n, err) && n == 5 && strcmp(top[0].hash, rx2.pxfm) == 0, "the most similar font to the block font is its twin with other metrics");
    bool sorted = true, noself = true;
    for (int i = 0; i < n; i++) { if (i && top[i].score > top[i - 1].score) sorted = false; if (strcmp(top[i].hash, rx.pxfm) == 0) noself = false; }
    CHECK(sorted && noself, "best first, the font itself left out");
    CHECK(!store_similar_fonts(s, "00000000000000000000000000000000", 5, top, &n, err) && err[0], "an unknown font is an error");
    StoreSimilar tt[3];
    CHECK(store_similar_typefaces(s, T_SANS, 3, tt, &n, err) && n == 1 && strcmp(tt[0].hash, T_UNC) == 0, "similar typefaces: the other typeface of the library, not itself");
    for (int i = 0; i < n; i++) printf("      typeface %s: %.2f (%d fonts)\n", tt[i].hash, tt[i].score, tt[i].nFonts);
    px_font_free(&x); px_font_free(&x2); px_font_free(&y); px_font_free(&z);
    store_close(s);
}

/* ------------------------------------------------------------------ threads */

typedef struct Worker { Store *s; volatile long stop; int loads, bad; } Worker;

static DWORD WINAPI worker_main(LPVOID p) {
    Worker *w = (Worker *)p;
    StoreConn *c = store_conn_open(w->s);
    while (!w->stop) {
        PxFont f;
        char err[PX_ERR_LEN];
        if (store_load_font(c, H_UNC, NULL, 0, &f, err) && f.count == 6) { w->loads++; px_font_free(&f); }
        else w->bad++;
        StoreEntryList l;
        if (store_list_fonts(c, &l, err)) store_entry_list_free(&l); else w->bad++;
    }
    store_conn_close(c);
    return 0;
}

static void test_threads(void) {
    char err[PX_ERR_LEN];
    CopyFileA(DB, TMP "/store_thr.pxfdb", FALSE);
    Store *s = NULL;
    if (!store_open_pxfdb(TMP "/store_thr.pxfdb", NULL, &s, err)) { CHECK(false, "open"); return; }
    Worker w[4];
    HANDLE th[4];
    for (int i = 0; i < 4; i++) { w[i].s = s; w[i].stop = 0; w[i].loads = w[i].bad = 0; th[i] = CreateThread(NULL, 0, worker_main, &w[i], 0, NULL); }
    bool allSaved = true;
    for (int k = 0; k < 6; k++) {
        PxFont f;
        char rows[64];
        snprintf(rows, sizeof rows, "0%c0%c0\n00000\n0...0", k & 1 ? '0' : '.', k & 2 ? '0' : '.');
        shape_font(&f, rows, 5 + k, 6);
        StoreSaveResult r;
        if (!store_save_to_memory(s, &f, NULL, 0, &r, err)) allSaved = false;
        px_font_free(&f);
    }
    Sleep(100);
    for (int i = 0; i < 4; i++) w[i].stop = 1;
    WaitForMultipleObjects(4, th, TRUE, INFINITE);
    int loads = 0, bad = 0;
    for (int i = 0; i < 4; i++) { loads += w[i].loads; bad += w[i].bad; CloseHandle(th[i]); }
    printf("      %d loads on 4 threads while 6 fonts were saved\n", loads);
    CHECK(allSaved && bad == 0 && loads > 0, "4 reader threads with their own connections while fonts are saved: no error");
    CHECK(count_rows(s, "pxfm") >= 8, "the saved fonts are all there");
    store_close(s);
}

/* hex palettes, COLORS and SYMBOLIC in the store; databases made before them */
static void test_palettes(void) {
    char err[PX_ERR_LEN];
    Store *s = NULL;
    CHECK(store_open_pxfdb(DB, NULL, &s, err), "palettes: open");
    if (!s) return;
    CHECK(count_rows(s, "palette") == 1, "palettes: one palette row");
    StoreConn *c = store_conn_open(s);
    sqlite3 *db = store_sqlite(c);
    sqlite3_stmt *st;
    int ncol = -1, nnull = -1;
    if (sqlite3_prepare_v2(db, "SELECT (SELECT colors FROM pxfm WHERE hex(hash) = upper('" H_GREY "')), (SELECT COUNT(*) FROM pxfm WHERE colors IS NULL)", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) { ncol = sqlite3_column_int(st, 0); nnull = sqlite3_column_int(st, 1); }
        sqlite3_finalize(st);
    }
    CHECK(ncol == 2 && nnull == 3, "palettes: pxfm.colors is 2 for the grey font, NULL for the 3 others");
    int pal = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM pxfm_glyph WHERE hex(palette) = upper('" P_GREY "') AND code = 65", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) pal = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    CHECK(pal == 1, "palettes: the A of the grey font names its palette");
    if (sqlite3_prepare_v2(db, "SELECT hex(colors), hex(colorset) FROM palette", -1, &st, NULL) == SQLITE_OK) {
        bool ok = sqlite3_step(st) == SQLITE_ROW && strcmp((const char *)sqlite3_column_text(st, 0), "808080FFFFFFFFFF") == 0 &&
                  strcmp((const char *)sqlite3_column_text(st, 1), "808080FFFFFFFFFF") == 0;
        sqlite3_finalize(st);
        CHECK(ok, "palettes: colors and colorset as RGBA bytes");
    }
    StoreEntryList fl;
    int c1 = 0, c2 = 0;
    if (store_list_fonts(c, &fl, err)) {
        for (int i = 0; i < fl.count; i++) { if (fl.e[i].colors == 1) c1++; if (fl.e[i].colors == 2 && strcmp(fl.e[i].pxfm, H_GREY) == 0) c2++; }
        store_entry_list_free(&fl);
    }
    CHECK(c1 == 3 && c2 == 1, "palettes: the entries' colours from pxfm.colors (NULL is 1)");

    /* a colour font of 3 colours and SYMBOLIC saved into memory, loaded back, exported */
    PxFont f;
    px_font_init(&f);
    f.has[PXM_ASCENT] = true; f.m[PXM_ASCENT] = 6;
    f.has[PXM_DESCENT] = true; f.m[PXM_DESCENT] = 2;
    f.symbolic = true;
    f.hasPalette = true;
    f.colorUsed[1] = f.colorUsed[2] = f.colorUsed[3] = true;
    f.color[1] = 0x000000FFu; f.color[2] = 0x808080FFu; f.color[3] = 0xFF000080u;
    PxGlyph g;
    memset(&g, 0, sizeof g);
    g.codepoint = 0x41; g.advance = 4; g.w = 3; g.h = 2;
    g.pix = (char *)malloc(6); memcpy(g.pix, "2.01.0", 6);
    px_font_add_glyph(&f, &g);
    g.codepoint = 0x42; memcpy(g.pix, "0.0.00", 6);
    px_font_add_glyph(&f, &g);
    px_glyph_free(&g);
    StoreSaveResult sr;
    CHECK(store_save_to_memory(s, &f, NULL, 0, &sr, err) && sr.newPxfm, "colour font: saved into memory");
    CHECK(count_rows(s, "palette") == 3, "colour font: its 2 palettes added (the B shares none: it is black only)");
    PxFont r;
    CHECK(store_load_font(c, sr.pxfm, NULL, 0, &r, err), "colour font: loaded back");
    char h[33];
    px_font_hash(&r, h);
    CHECK(strcmp(h, sr.pxfm) == 0 && r.symbolic && px_font_colors(&r) == 3, "colour font: same hash, SYMBOLIC, COLORS 3");
    size_t tn;
    char *txt = store_pxfm_text(c, sr.pxfm, &tn, h);
    CHECK(txt && strstr(txt, "COLORS\t3\n") && strstr(txt, "SYMBOLIC\n") && strcmp(h, sr.pxfm) == 0, "colour font: the text from the rows has COLORS and SYMBOLIC and its hash");
    free(txt);
    px_font_free(&r);
    px_font_free(&f);
    StoreEntryList el;
    bool sym = false;
    if (store_list_fonts(c, &el, err)) {
        for (int i = 0; i < el.count; i++) if (strcmp(el.e[i].pxfm, sr.pxfm) == 0) sym = el.e[i].symbolic && el.e[i].colors == 3;
        store_entry_list_free(&el);
    }
    CHECK(sym, "colour font: the entry is symbolic with 3 colours");
    rm_rf(TMP "/ex_pal");
    StoreExportResult res;
    CHECK(store_export_folder(s, TMP "/ex_pal", STORE_EXPORT_APPEND, NULL, &res, err) && res.written.palettes == 3, "colour font: exported with 3 palette files");
    Store *b = NULL;
    rm_rf(TMP "/imp_pal");
    px_mkdir_p(TMP "/imp_pal");
    CHECK(store_import_folder(TMP "/ex_pal", TMP "/imp_pal/x.pxfdb", NULL, &b, err) && same_content(s, b), "colour font: the exported folder imports to the same content");
    store_close(b);

    /* deleting the font deletes its palettes */
    StoreDeletePlan plan;
    CHECK(store_plan_delete_font(s, sr.pxfm, &plan, err) && plan.nPalettes == 2 && strstr(plan.summary, "2 palettes"), "colour font: the deletion plan names its 2 palettes");
    CHECK(store_apply_delete(s, &plan, err) && count_rows(s, "palette") == 1, "colour font: deleted with its palettes; the grey one stays");
    store_plan_free(&plan);
    store_conn_close(c);
    store_close(s);

    /* the build checks of the files: COLORS that is not the count, a palette with too few lines */
    rm_rf(TMP "/bad_pal");
    copy_tree(TMP "/ex_pal", TMP "/bad_pal");
    {
        size_t n;
        char *t = px_read_file(LIB "/pxfm/40/" H_GREY ".pxfm", &n);
        char *q = t ? strstr(t, "COLORS\t2") : NULL;
        if (q) q[7] = '3';
        char hh[33];
        px_hash128_hex(t, n, hh, false);
        char path[400], dir[300];
        snprintf(dir, sizeof dir, TMP "/bad_pal/pxfm/%.2s", hh);
        snprintf(path, sizeof path, "%s/%s.pxfm", dir, hh);
        px_mkdir_p(dir);
        px_write_file(path, t, n);
        free(t);
        CHECK(!store_import_folder(TMP "/bad_pal", TMP "/imp_pal/y.pxfdb", NULL, &b, err) && strstr(err, "COLORS 3"), "build: COLORS that is not the number of colours is refused");
        printf("      error was: %s\n", err);
        DeleteFileA(path);
    }

    /* a pxfdb made before hex palettes (pxfm_color, no palette table) is refused with a clear message */
    DeleteFileA(TMP "/old.pxfdb");
    sqlite3 *odb;
    if (sqlite3_open(TMP "/old.pxfdb", &odb) == SQLITE_OK) {
        sqlite3_exec(odb, "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;"
                          "INSERT INTO meta VALUES ('format', 'PXFDB'), ('format_version', '1');"
                          "CREATE TABLE pxfm (hash BLOB PRIMARY KEY, monospaced INTEGER) WITHOUT ROWID;"
                          "CREATE TABLE pxfm_color (pxfm BLOB, pixel TEXT, color INTEGER, PRIMARY KEY (pxfm, pixel)) WITHOUT ROWID;", NULL, NULL, NULL);
        sqlite3_close(odb);
    }
    s = NULL;
    CHECK(!store_open_pxfdb(TMP "/old.pxfdb", NULL, &s, err) && strstr(err, "older pixelglyphlib") && strstr(err, "rebuild"), "a pxfdb made before hex palettes: refused, rebuild it");
    printf("      error was: %s\n", err);
}

/* testdata/lib_colors.pxfdb: fonts of 1, 2, 2 and 3 colours (the Colours filter test) */
static void test_lib_colors(void) {
    char err[PX_ERR_LEN];
    Store *s = NULL;
    CHECK(store_open_pxfdb("testdata/lib_colors.pxfdb", NULL, &s, err), "lib_colors: open");
    if (!s) return;
    StoreConn *c = store_conn_open(s);
    StoreEntryList fl;
    int n[4] = {0, 0, 0, 0};
    if (store_list_fonts(c, &fl, err)) {
        for (int i = 0; i < fl.count; i++) if (fl.e[i].colors >= 1 && fl.e[i].colors <= 3) n[fl.e[i].colors]++;
        store_entry_list_free(&fl);
    }
    CHECK(n[1] == 1 && n[2] == 2 && n[3] == 1, "lib_colors: one font of 1 colour, two of 2, one of 3");
    store_conn_close(c);
    store_close(s);
}

int main(void) {
    px_mkdir_p(TMP);
    test_open_pxfdb();
    test_import();
    test_text_from_rows();
    test_model();
    test_queries();
    test_save();
    test_export();
    test_deletions();
    test_default_glyph();
    test_similarity();
    test_threads();
    test_palettes();
    test_lib_colors();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
