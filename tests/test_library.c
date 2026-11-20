/* test_library.c - THE KEY TESTS on a whole real library (slow; build.bat libtest).
 *
 *   test_library.exe [library root] [--quick]     (default root: C:/Users/jnsGEzD3/Documents/GitHub/pixelglyphlib;
 *                                                  --quick: lines-only round trip, no full glyph loading from the files)
 *
 *  A. text folders: every pxfm and pxtf read with the strict readers and written again gives the bytes of its file and
 *     the hash of its name (pxfm: lines only, and with every glyph file loaded).
 *  B. the store: open <root>/pxfdb.pxfdb, open the root (valid pxfdb present), build from the files into a fresh cache
 *     (the root's pxfdb ignored), open again through the cache; timings.
 *  C. the store built from the files equals the store opened from the pxfdb: row counts and a content checksum per table.
 *  D. every pxgl hash, every pxfm and pxtf made from the store's rows is the file, byte for byte, and has its name; every
 *     font and typeface loaded into the model from the rows is written as the file.
 *  E. queries and similarity: timings and sample results.
 * Nothing is written into the library: the cache goes to tests/tmp. */
#include "store.h"
#include "pxfont.h"
#include "pxtf.h"
#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define DEFAULT_ROOT "C:/Users/jnsGEzD3/Documents/GitHub/pixelglyphlib"
#define CACHE_DIR "tests/tmp/libcache"

static double now(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static int g_fail = 0;
static void report(const char *what, const char *path, const char *msg) {
    g_fail++;
    if (g_fail <= 60) printf("FAIL  %s  %s: %s\n", what, path, msg);
}

/* ---- the files of a folder: <root>/<sub>/xx/<hash>.<ext>, sorted by hash */

typedef struct Names { char (*h)[33]; int n; } Names;

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void list_files(const char *root, const char *sub, Names *out) {
    char d[600];
    snprintf(d, sizeof d, "%s/%s/*", root, sub);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(d, &fd);
    int cap = 1024;
    out->h = (char (*)[33])malloc(33 * (size_t)cap);
    out->n = 0;
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char d2[600];
        snprintf(d2, sizeof d2, "%s/%s/%s/*", root, sub, fd.cFileName);
        WIN32_FIND_DATAA f2;
        HANDLE h2 = FindFirstFileExA(d2, FindExInfoBasic, &f2, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
        if (h2 == INVALID_HANDLE_VALUE) continue;
        do {
            if (f2.cFileName[0] == '.' || strlen(f2.cFileName) < 32) continue;
            if (out->n == cap) { cap *= 2; out->h = (char (*)[33])realloc(out->h, 33 * (size_t)cap); }
            memcpy(out->h[out->n], f2.cFileName, 32);
            out->h[out->n][32] = 0;
            out->n++;
        } while (FindNextFileA(h2, &f2));
        FindClose(h2);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(out->h, (size_t)out->n, 33, cmp_name);
}

static void path_of(const char *root, const char *kind, const char *hash, char *out, size_t n) {
    snprintf(out, n, "%s/%s/%c%c/%s.%s", root, kind, hash[0], hash[1], hash, kind);
}

/* ---- A. round trip of the files */

static void part_a(const char *root, bool quick, const Names *pxfm, const Names *pxtf) {
    char err[PX_ERR_LEN], path[800];
    int okLines = 0, okFull = 0, okTf = 0;
    long glyphLines = 0;
    double t0 = now(), tFull = 0;
    for (int i = 0; i < pxfm->n; i++) {
        path_of(root, "pxfm", pxfm->h[i], path, sizeof path);
        size_t rl;
        char *raw = px_read_file(path, &rl);
        if (!raw) { report("pxfm", path, "cannot read"); continue; }
        PxFont f;
        if (!px_read_pxfm_lines(path, &f, err)) { report("pxfm", path, err); free(raw); continue; }
        size_t n;
        char hash[33];
        char *t = px_pxfm_text(&f, &n, hash);
        bool same = t && n == rl && memcmp(t, raw, n) == 0;
        free(t);
        if (!same) report("pxfm lines round trip (bytes differ)", path, hash);
        else if (strcmp(hash, pxfm->h[i]) != 0 || strcmp(f.fileHash, pxfm->h[i]) != 0) report("pxfm hash != name", path, hash);
        else okLines++;
        if (same && !quick) {
            double a = now();
            if (!px_load_glyphs(&f, root, NULL, 0, err)) report("pxfm load glyphs", path, err);
            else {
                tFull += now() - a;
                glyphLines += f.count;
                t = px_pxfm_text(&f, &n, hash);
                same = t && n == rl && memcmp(t, raw, n) == 0;
                free(t);
                char h2[33];
                px_font_hash(&f, h2);
                if (!same) report("pxfm full round trip (bytes differ)", path, hash);
                else if (strcmp(h2, pxfm->h[i]) != 0) report("px_font_hash != name", path, h2);
                else okFull++;
            }
        }
        px_font_free(&f);
        free(raw);
    }
    printf("A. pxfm: %d files, %d lines round trips ok, %d full round trips ok (%ld glyph lines, %.1f s loading glyph files), %.1f s\n",
           pxfm->n, okLines, okFull, glyphLines, tFull, now() - t0);
    for (int i = 0; i < pxtf->n; i++) {
        path_of(root, "pxtf", pxtf->h[i], path, sizeof path);
        size_t rl;
        char *raw = px_read_file(path, &rl);
        if (!raw) { report("pxtf", path, "cannot read"); continue; }
        PxtfTypeface c;
        if (!pxtf_parse(raw, rl, &c, err)) { report("pxtf", path, err); free(raw); continue; }
        size_t n;
        char hash[33];
        char *t = pxtf_text(&c, &n, hash);
        if (!t || n != rl || memcmp(t, raw, n) != 0) report("pxtf round trip (bytes differ)", path, hash);
        else if (strcmp(hash, pxtf->h[i]) != 0) report("pxtf hash != name", path, hash);
        else okTf++;
        free(t);
        free(raw);
        pxtf_free(&c);
    }
    printf("A. pxtf: %d files, %d round trips ok (bytes and hash == name)\n", pxtf->n, okTf);
}

/* ---- C. checksums per table */

static const char *const TABLES[8][2] = {
    {"glyph", "hash"}, {"pxfm", "hash"}, {"palette", "hash"}, {"pxfm_glyph", "pxfm, code"},
    {"pxfm_kerning", "pxfm, first, second"}, {"pxtf", "hash"}, {"pxtf_variant", "pxtf, number"},
    {"pxtf_field", "pxtf, number, key, value"}};

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const unsigned char *s = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= s[i]; h *= 1099511628211ull; }
    return h;
}

static void table_sum(sqlite3 *db, const char *table, const char *order, long *rows, uint64_t *sum) {
    char sql[200];
    snprintf(sql, sizeof sql, "SELECT * FROM %s ORDER BY %s", table, order);
    sqlite3_stmt *st;
    *rows = 0;
    *sum = 1469598103934665603ull;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) { *rows = -1; return; }
    int nc = sqlite3_column_count(st);
    while (sqlite3_step(st) == SQLITE_ROW) {
        (*rows)++;
        for (int c = 0; c < nc; c++) {
            int ty = sqlite3_column_type(st, c);
            unsigned char tag = (unsigned char)ty;
            *sum = fnv(*sum, &tag, 1);
            if (ty == SQLITE_INTEGER) { sqlite3_int64 v = sqlite3_column_int64(st, c); *sum = fnv(*sum, &v, 8); }
            else if (ty == SQLITE_TEXT || ty == SQLITE_BLOB) {
                int n = sqlite3_column_bytes(st, c);
                const void *p = ty == SQLITE_TEXT ? (const void *)sqlite3_column_text(st, c) : sqlite3_column_blob(st, c);
                *sum = fnv(*sum, &n, 4);
                if (n) *sum = fnv(*sum, p, (size_t)n);
            }
        }
    }
    sqlite3_finalize(st);
}

static const char *label(const StoreEntry *e, char *buf, size_t n) {
    snprintf(buf, n, "%s %s [%.8s]", e->family[0] ? e->family : "(no family)", e->style, e->pxfm);
    return buf;
}

int main(int argc, char **argv) {
    const char *root = DEFAULT_ROOT;
    bool quick = false;
    for (int i = 1; i < argc; i++) { if (strcmp(argv[i], "--quick") == 0) quick = true; else root = argv[i]; }
    char err[PX_ERR_LEN];
    char libdb[1200];
    snprintf(libdb, sizeof libdb, "%s/pxfdb.pxfdb", root);

    Names fm, ft, fg;
    double t0 = now();
    list_files(root, "pxfm", &fm);
    list_files(root, "pxtf", &ft);
    list_files(root, "pxgl", &fg);
    printf("files: %d pxgl, %d pxfm, %d pxtf (listed in %.2f s)\n", fg.n, fm.n, ft.n, now() - t0);

    part_a(root, quick, &fm, &ft);

    /* ---- B. the store */
    Store *sDb = NULL, *sRoot = NULL, *sBuilt = NULL;
    t0 = now();
    if (!store_open_pxfdb(libdb, NULL, &sDb, err)) { printf("B. cannot open the pxfdb: %s\n", err); return 2; }
    printf("B. store_open_pxfdb (warm): %.3f s, %d glyph, %d pxfm, %d pxtf rows\n", now() - t0, store_info(sDb)->nGlyph, store_info(sDb)->nPxfm, store_info(sDb)->nPxtf);
    t0 = now();
    if (!store_open_root(root, NULL, &sRoot, err)) { printf("B. cannot open the root: %s\n", err); return 2; }
    printf("B. store_open_root with a valid pxfdb present: %.3f s (%s)\n", now() - t0, kind_name(store_info(sRoot)->loadKind));
    {   /* the build from the files (the root's pxfdb ignored) */
        StoreProgress prog = {0};
        t0 = now();
        if (!store_import_folder(root, "tests/tmp/lib_built.pxfdb", &prog, &sBuilt, err)) { printf("B. build from files FAILED: %s\n", err); report("build", root, err); return 1; }
        printf("B. import from files (warm), checked, indexed and written: %.2f s total, %.2f s of building\n",
               now() - t0, store_info(sBuilt)->buildSeconds);
    }

    /* ---- C. equality */
    {
        printf("C. content of the built store against the real pxfdb:\n");
        int equal = 0;
        for (int t = 0; t < 8; t++) {
            long r1, r2;
            uint64_t s1, s2;
            table_sum(store_sqlite(store_main_conn(sBuilt)), TABLES[t][0], TABLES[t][1], &r1, &s1);
            table_sum(store_sqlite(store_main_conn(sDb)), TABLES[t][0], TABLES[t][1], &r2, &s2);
            bool same = r1 == r2 && s1 == s2 && r1 >= 0;
            printf("   %-13s %8ld rows  %016llx   %s\n", TABLES[t][0], r1, (unsigned long long)s1, same ? "equal" : "DIFFERENT");
            if (same) equal++; else report("store equality", TABLES[t][0], "rows or checksum differ");
        }
        printf("C. %d of 8 tables equal\n", equal);
        /* the real pxfdb passes the reference's foreign key check too */
        sqlite3_stmt *st;
        int broken = 0;
        if (sqlite3_prepare_v2(store_sqlite(store_main_conn(sBuilt)), "PRAGMA foreign_key_check", -1, &st, NULL) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) broken++;
            sqlite3_finalize(st);
        }
        printf("C. foreign_key_check of the built store: %d broken references\n", broken);
        if (broken) report("foreign keys", "built store", "broken references");
    }

    /* ---- D. conversions */
    {
        StoreConn *c = store_main_conn(sBuilt);
        int okGl = 0, okM = 0, okMm = 0, okT = 0, okTm = 0;
        t0 = now();
        for (int i = 0; i < fg.n; i++) {
            char h[33];
            size_t n;
            char *t = store_pxgl_text(c, fg.h[i], &n, h);
            if (t && strcmp(h, fg.h[i]) == 0) okGl++; else report("pxgl from rows: hash != name", fg.h[i], h);
            free(t);
        }
        double tg = now() - t0;
        t0 = now();
        for (int i = 0; i < fm.n; i++) {
            char path[800], h[33];
            path_of(root, "pxfm", fm.h[i], path, sizeof path);
            size_t rl, n;
            char *raw = px_read_file(path, &rl);
            char *t = store_pxfm_text(c, fm.h[i], &n, h);
            if (!raw || !t || n != rl || memcmp(t, raw, n) != 0) report("pxfm from rows: bytes differ", path, h);
            else if (strcmp(h, fm.h[i]) != 0) report("pxfm from rows: hash != name", path, h);
            else okM++;
            free(t);
            PxFont f;
            if (!store_load_font(c, fm.h[i], NULL, 0, &f, err)) report("store_load_font", fm.h[i], err);
            else {
                char *t2 = px_pxfm_text(&f, &n, h);
                if (!raw || !t2 || n != rl || memcmp(t2, raw, n) != 0) report("pxfm model from rows: bytes differ", path, h);
                else okMm++;
                free(t2);
                px_font_free(&f);
            }
            free(raw);
        }
        double tm = now() - t0;
        t0 = now();
        for (int i = 0; i < ft.n; i++) {
            char path[800], h[33];
            path_of(root, "pxtf", ft.h[i], path, sizeof path);
            size_t rl, n;
            char *raw = px_read_file(path, &rl);
            char *t = store_pxtf_text(c, ft.h[i], &n, h);
            if (!raw || !t || n != rl || memcmp(t, raw, n) != 0) report("pxtf from rows: bytes differ", path, h);
            else if (strcmp(h, ft.h[i]) != 0) report("pxtf from rows: hash != name", path, h);
            else okT++;
            free(t);
            PxtfTypeface tf;
            if (!store_load_typeface(c, ft.h[i], &tf, err)) report("store_load_typeface", ft.h[i], err);
            else {
                char *t2 = pxtf_text(&tf, &n, h);
                if (!raw || !t2 || n != rl || memcmp(t2, raw, n) != 0) report("pxtf model from rows: bytes differ", path, h);
                else okTm++;
                free(t2);
                pxtf_free(&tf);
            }
            free(raw);
        }
        printf("D. pxgl text from rows has its name: %d of %d (%.1f s)\n", okGl, fg.n, tg);
        printf("D. pxfm: %d of %d byte-equal to the file from rows, %d of %d from the PxFont loaded from rows (%.1f s)\n", okM, fm.n, okMm, fm.n, tm);
        printf("D. pxtf: %d of %d byte-equal to the file from rows, %d of %d from the PxtfTypeface loaded from rows (%.1f s)\n", okT, ft.n, okTm, ft.n, now() - t0);
    }

    /* ---- E. queries and similarity */
    {
        StoreConn *c = store_main_conn(sBuilt);
        StoreEntryList tl, fl;
        t0 = now();
        store_list_typefaces(c, &tl, err);
        double a = now() - t0;
        t0 = now();
        store_list_fonts(c, &fl, err);
        printf("E. store_list_typefaces: %d variants in %.3f s; store_list_fonts: %d fonts in %.3f s\n", tl.count, a, fl.count, now() - t0);

        /* a selected-codes load of every font (what the browser does) */
        PxCode sample[15];
        for (int i = 0; i < 15; i++) sample[i] = px_code_cp((unsigned char)"Hamburgefontsiv"[i]);
        t0 = now();
        for (int i = 0; i < fl.count; i++) {
            PxFont f;
            if (store_load_font(c, fl.e[i].pxfm, sample, 15, &f, err)) px_font_free(&f); else report("sample load", fl.e[i].pxfm, err);
        }
        printf("E. store_load_font with the 15 code points of \"Hamburgefontsiv\" for all %d fonts: %.3f s\n", fl.count, now() - t0);

        t0 = now();
        StoreCodeGlyphList cg;
        store_glyphs_for_codepoint(c, 0x65, &cg, err);
        printf("E. store_glyphs_for_codepoint(U+0065): %d distinct bitmaps in %.4f s, the first used by %d fonts\n", cg.count, now() - t0, cg.count ? cg.g[0].nFonts : 0);
        if (cg.count) {
            StoreUsageList ul;
            int nf, nt;
            t0 = now();
            store_glyph_usage(c, cg.g[0].glyph, &ul, &nf, &nt, err);
            printf("E. store_glyph_usage(most common 'e'): %d rows, %d fonts, %d typefaces in %.4f s\n", ul.count, nf, nt, now() - t0);
            store_usage_list_free(&ul);
            if (cg.count > 1) {
                char hs[2][33];
                memcpy(hs[0], cg.g[0].glyph, 33);
                memcpy(hs[1], cg.g[1].glyph, 33);
                StoreEntryList wl;
                t0 = now();
                store_fonts_with_glyphs(c, (const char(*)[33])hs, 1, &wl, err);
                printf("E. store_fonts_with_glyphs (1 glyph): %d fonts in %.4f s\n", wl.count, now() - t0);
                store_entry_list_free(&wl);
            }
        }
        store_code_glyph_list_free(&cg);

        double prep;
        if (!store_similarity_prepare(sBuilt, &prep, err)) report("similarity prepare", "", err);
        printf("E. similarity precompute (reference bitmaps of %d fonts as bit rows): %.2f s\n", fl.count, prep);

        /* one font against all, several times; the sample fonts */
        const char *wanted[] = {"Fixedsys", "Terminus", "Topaz", "Courier", "MS Sans Serif", "Chicago"};
        int shown = 0;
        for (size_t w = 0; w < sizeof wanted / sizeof *wanted && shown < 3; w++) {
            int fi = -1;
            for (int i = 0; i < tl.count; i++) if (strstr(tl.e[i].family, wanted[w])) { fi = i; break; }
            if (fi < 0) continue;
            StoreSimilar sim[20];
            int n;
            t0 = now();
            store_similar_fonts(sBuilt, tl.e[fi].pxfm, 20, sim, &n, err);
            double dt = now() - t0;
            char lb[200];
            printf("E. similar fonts to %s (one against all %d fonts: %.3f s):\n", label(&tl.e[fi], lb, sizeof lb), fl.count, dt);
            for (int k = 0; k < 5 && k < n; k++) {
                int ti = -1;
                for (int i = 0; i < tl.count; i++) if (strcmp(tl.e[i].pxfm, sim[k].hash) == 0) { ti = i; break; }
                printf("      %6.2f (shape %s%.1f, shared %.1f, metrics %.1f; %d ref cps)  %s\n", sim[k].score, sim[k].shapeUsed ? "" : "n/a ", sim[k].shapeUsed ? sim[k].shape : 0.0,
                       sim[k].shared, sim[k].metrics, sim[k].sharedRef, ti >= 0 ? label(&tl.e[ti], lb, sizeof lb) : sim[k].hash);
            }
            shown++;
            if (shown == 1) {
                StoreSimilar self;
                store_font_similarity(sBuilt, tl.e[fi].pxfm, tl.e[fi].pxfm, &self, err);
                printf("      score of the font against itself: %.4f\n", self.score);
                StoreSimilar sm[5];
                t0 = now();
                store_similar_typefaces(sBuilt, tl.e[fi].pxtf, 5, sm, &n, err);
                printf("E. similar typefaces to %s (%.3f s):\n", tl.e[fi].family, now() - t0);
                for (int k = 0; k < n; k++) {
                    PxtfTypeface tf;
                    const char *fam = "?";
                    if (store_load_typeface(c, sm[k].hash, &tf, err)) fam = tf.count && tf.v[0].text[PXTF_FAMILY] ? tf.v[0].text[PXTF_FAMILY] : "(no family)";
                    printf("      %6.2f (%d fonts)  %s [%.8s]\n", sm[k].score, sm[k].nFonts, fam, sm[k].hash);
                    if (fam[0] != '?') pxtf_free(&tf);
                }
            }
        }
        {   /* the worst case for timing: the average over 30 fonts */
            StoreSimilar sim[20];
            int n;
            t0 = now();
            int runs = 30 < fl.count ? 30 : fl.count;
            for (int i = 0; i < runs; i++) store_similar_fonts(sBuilt, fl.e[i * (fl.count / runs)].pxfm, 20, sim, &n, err);
            printf("E. one-vs-all over %d different fonts: %.3f s each on average\n", runs, (now() - t0) / runs);
        }
        store_entry_list_free(&tl);
        store_entry_list_free(&fl);
    }

    store_close(sBuilt);
    store_close(sDb);
    printf("\n%s: %d failures\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
