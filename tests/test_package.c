/* test_package.c - tests of src/package.c (font and typeface packages) on testdata/lib.pxfdb, lib_colors.pxfdb, lib_many.pxfdb.
 * Run from the pxfe directory (build.bat test does).
 *
 * The module is included whole, so that the crafted packages can be made with its own zip reader and writer.
 * Byte identity with the reference: the store is exported to a folder, tests/pkg_reference.py runs pixelglyphlib's
 * `package export` on it (Python; the pixelglyphlib folder is PXFE_PGL, by default the one beside pxfe), and the packages
 * pxfe writes into another folder must have the same names and bytes.  Python's own zlib may be zlib-ng (Windows builds of
 * Python 3.14 and later), whose deflate differs from zlib's; so the reference is run twice: with the deflate of a zlib DLL
 * (PXFE_ZLIB_DLL, by default MSYS2's C:/msys64/mingw64/bin/zlib1.dll) in place of Python's, where every byte must match,
 * and with Python's own zlib, where the names and the files must match (and, with zlib-ng, the bytes are only counted).
 * Without Python or pixelglyphlib those parts are skipped with a note. */
#include "package.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "sqlite3.h"

static int g_pass = 0, g_fail = 0, g_skip = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)
#define SKIP(name) do { g_skip++; printf("SKIP  %s\n", name); } while (0)

#define TMP "tests/tmp/pkg"
#define H_EXAMPLE "3b52125b2e5f6466823baa4fa3186fa0"   /* test-sans-regular */
#define H_NOASC "4fba4190f6ba7e10b296c7070234e1ce"     /* test-sans-italic */
#define H_GREY "894ef821e147533c34b39e2131016133"      /* a colour font of no typeface */
#define H_UNC "54caf68fe9ec18634701a55bfc87b5c0"
#define T_SANS "ff6354ce745de4d0170855e161a491be"      /* test sans: H_EXAMPLE and H_NOASC */
#define T_UNC "0021a99dbbf1ae9dc9c27c685f581c17"

static const char *const TABLES[] = {"glyph", "pxfm", "palette", "pxfm_glyph", "pxfm_kerning", "pxtf", "pxtf_variant", "pxtf_field"};

/* ------------------------------------------------------------------ helpers */

static void bs(char *p) { for (; *p; p++) if (*p == '/') *p = 0x5C; }

static void rm_rf(const char *dir) {
    char d[400], cmd[500];
    snprintf(d, sizeof d, "%s", dir);
    bs(d);
    snprintf(cmd, sizeof cmd, "rmdir /S /Q \"%s\" 2>nul", d);
    system(cmd);
}

/* an ordered FNV-1a checksum of a table (primary-key order) */
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
                if (t == SQLITE_INTEGER) {
                    long long v = sqlite3_column_int64(st, i);
                    const unsigned char *p = (const unsigned char *)&v;
                    for (int k = 0; k < 8; k++) h = (h ^ p[k]) * 1099511628211ULL;
                    continue;
                }
                const unsigned char *p = (const unsigned char *)sqlite3_column_blob(st, i);
                int n = sqlite3_column_bytes(st, i);
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

/* the hashes of a table, ascending */
static int hashes_of(Store *s, const char *table, char (*out)[33], int cap) {
    StoreConn *c = store_conn_open(s);
    char sql[100];
    snprintf(sql, sizeof sql, "SELECT hash FROM %s ORDER BY hash", table);
    sqlite3_stmt *st = NULL;
    int n = 0;
    if (sqlite3_prepare_v2(store_sqlite(c), sql, -1, &st, NULL) == SQLITE_OK)
        while (sqlite3_step(st) == SQLITE_ROW && n < cap) store_blob_to_hex((const uint8_t *)sqlite3_column_blob(st, 0), out[n++]);
    sqlite3_finalize(st);
    store_conn_close(c);
    return n;
}

/* An empty store: a copy of the database with every row deleted. */
static Store *empty_store(const char *db, const char *copy) {
    char err[PX_ERR_LEN];
    if (!CopyFileA(db, copy, FALSE)) return NULL;
    sqlite3 *h = NULL;
    if (sqlite3_open(copy, &h) != SQLITE_OK) { sqlite3_close(h); return NULL; }
    sqlite3_exec(h, "DELETE FROM pxtf_field; DELETE FROM pxtf_variant; DELETE FROM pxtf; DELETE FROM pxfm_kerning; DELETE FROM pxfm_glyph;"
                    " DELETE FROM pxfm; DELETE FROM palette; DELETE FROM glyph;", NULL, NULL, NULL);
    sqlite3_close(h);
    Store *s = NULL;
    if (!store_open_pxfdb(copy, NULL, &s, err)) { printf("      %s\n", err); return NULL; }
    return s;
}

/* the file names of a folder, sorted; returns the count */
static int list_dir(const char *dir, char (*out)[260], int cap) {
    char pat[400];
    snprintf(pat, sizeof pat, "%s/*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (n < cap) snprintf(out[n++], 260, "%s", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(out, (size_t)n, 260, (int (*)(const void *, const void *))strcmp);
    return n;
}

static bool same_file(const char *a, const char *b) {
    size_t na, nb;
    char *pa = px_read_file(a, &na), *pb = px_read_file(b, &nb);
    bool same = pa && pb && na == nb && memcmp(pa, pb, na) == 0;
    free(pa);
    free(pb);
    return same;
}

static bool same_result(const PkgImportResult *a, const PkgImportResult *b) {
    return a->kind == b->kind && strcmp(a->pxtf, b->pxtf) == 0 && a->nPxfm == b->nPxfm && a->nGlyphs == b->nGlyphs &&
           a->nPalettes == b->nPalettes && memcmp(a->pxfm, b->pxfm, 33 * (size_t)a->nPxfm) == 0;
}

/* ------------------------------------------------------------------ crafting packages */

static void clone_list(const EntryList *src, EntryList *dst) {
    memset(dst, 0, sizeof *dst);
    for (int i = 0; i < src->n; i++) {
        unsigned char *d = (unsigned char *)malloc(src->e[i].len + 1);
        memcpy(d, src->e[i].data, src->e[i].len);
        d[src->e[i].len] = 0;
        list_add(dst, src->e[i].name, d, src->e[i].len);
    }
}

static int first_with(const EntryList *l, const char *prefix) {
    for (int i = 0; i < l->n; i++) if (strncmp(l->e[i].name, prefix, strlen(prefix)) == 0) return i;
    return -1;
}
static int root_of(const EntryList *l) {
    for (int i = 0; i < l->n; i++) if (!strchr(l->e[i].name, '/')) return i;
    return -1;
}

static void remove_entry(EntryList *l, int i) {
    free(l->e[i].name);
    free(l->e[i].data);
    memmove(&l->e[i], &l->e[i + 1], sizeof(Entry) * (size_t)(l->n - i - 1));
    l->n--;
}

static void set_data(EntryList *l, int i, const char *text) {
    free(l->e[i].data);
    l->e[i].len = strlen(text);
    l->e[i].data = (unsigned char *)_strdup(text);
}
static void set_name(EntryList *l, int i, const char *name) { free(l->e[i].name); l->e[i].name = _strdup(name); }

static void add_text(EntryList *l, const char *name, const char *text) {
    list_add(l, name, (unsigned char *)_strdup(text), strlen(text));
}

/* the entry's name made the hash of its content: <dir><hash><ext> */
static void rehash(EntryList *l, int i, char hashOut[33]) {
    const char *nm = l->e[i].name, *slash = strrchr(nm, '/');
    char dir[64] = "", ext[16];
    if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - nm + 1), nm);
    snprintf(ext, sizeof ext, "%s", strrchr(nm, '.'));
    size_t cl;
    char *can = px_canonical((const char *)l->e[i].data, l->e[i].len, &cl), h[33], name[128];
    px_hash128_hex(can, cl, h, false);
    free(can);
    snprintf(name, sizeof name, "%s%s%s", dir, h, ext);
    set_name(l, i, name);
    if (hashOut) memcpy(hashOut, h, 33);
}

/* the entry's text with `extra` appended */
static void append_text(EntryList *l, int i, const char *extra) {
    size_t el = strlen(extra);
    unsigned char *d = (unsigned char *)malloc(l->e[i].len + el + 1);
    memcpy(d, l->e[i].data, l->e[i].len);
    memcpy(d + l->e[i].len, extra, el + 1);
    free(l->e[i].data);
    l->e[i].data = d;
    l->e[i].len += el;
}

static void upper(char *s) { for (int i = 0; i < 32 && s[i]; i++) if (s[i] >= 'a' && s[i] <= 'f') s[i] = (char)(s[i] - 32); }

/* replace every `from` by `to` in the entry's text (same length) */
static void replace_in(EntryList *l, int i, const char *from, const char *to) {
    size_t fl = strlen(from);
    for (size_t k = 0; k + fl <= l->e[i].len; k++)
        if (memcmp(l->e[i].data + k, from, fl) == 0) memcpy(l->e[i].data + k, to, fl);
}

/* The first file in `folder` gets the content `text` and the hash of it as its name; the pxfm file at the root names it so. */
static void renamed_file(EntryList *l, const char *folder, const char *text) {
    int i = first_with(l, folder), r = root_of(l);
    char oldH[33], newH[33];
    snprintf(oldH, sizeof oldH, "%.32s", l->e[i].name + strlen(folder));
    set_data(l, i, text);
    rehash(l, i, newH);
    upper(oldH);
    upper(newH);
    replace_in(l, r, oldH, newH);
    rehash(l, r, NULL);
}

static Store *g_into;   /* the crafted packages are imported into it too: refused, and it stays unchanged */

/* The crafted package must be rejected, with an error that says `what`, by the check and by an import */
static void expect_reject(EntryList *l, const char *what, const char *name) {
    size_t len;
    char err[PX_ERR_LEN] = "", zerr[PX_ERR_LEN], ierr[PX_ERR_LEN] = "";
    unsigned char *z = zip_entries(l, &len, zerr);
    bool ok = z && !pkg_check_bytes(z, len, NULL, err) && strstr(err, what) != NULL;
    ok = ok && !pkg_import_bytes(g_into, z, len, NULL, ierr) && strcmp(err, ierr) == 0 && !store_modified(g_into);
    char label[400];
    snprintf(label, sizeof label, "rejected: %s", name);
    CHECK(ok, label);
    printf("      %s\n", err);
    free(z);
    list_free(l);
}

static void expect_reject_bytes(const unsigned char *z, size_t len, const char *what, const char *name) {
    char err[PX_ERR_LEN] = "", ierr[PX_ERR_LEN] = "", label[400];
    bool ok = !pkg_check_bytes(z, len, NULL, err) && strstr(err, what) != NULL;
    ok = ok && !pkg_import_bytes(g_into, z, len, NULL, ierr) && strcmp(err, ierr) == 0 && !store_modified(g_into);
    snprintf(label, sizeof label, "rejected: %s", name);
    CHECK(ok, label);
    printf("      %s\n", err);
}

/* ------------------------------------------------------------------ the reference */

static char g_pgl[400], g_dll[400];
static bool g_python;

static void find_reference(void) {
    const char *p = getenv("PXFE_PGL"), *d = getenv("PXFE_ZLIB_DLL");
    snprintf(g_pgl, sizeof g_pgl, "%s", p && *p ? p : "C:/Users/jnsGEzD3/Documents/GitHub/pixelglyphlib");
    snprintf(g_dll, sizeof g_dll, "%s", d && *d ? d : "C:/msys64/mingw64/bin/zlib1.dll");
    char probe[500];
    snprintf(probe, sizeof probe, "%s/pixelglyphlib/pkgbase.py", g_pgl);
    g_python = px_file_exists(probe) && system("python -c \"import zlib\" >nul 2>nul") == 0;
    if (!px_file_exists(g_dll)) g_dll[0] = 0;
}

/* runs tests/pkg_reference.py; the first line of its output names the deflate */
static bool run_reference(const char *root, const char *out, bool withDll, char *deflateLine, size_t cap, bool *oldPxtf) {
    char cmd[1600], log[500];
    snprintf(log, sizeof log, "%s/reference.log", out);
    px_mkdir_p(out);
    snprintf(cmd, sizeof cmd, "python tests/pkg_reference.py --lib \"%s\" --root \"%s\" --out \"%s\"%s%s%s > \"%s\" 2>&1",
             g_pgl, root, out, withDll ? " --zlib \"" : "", withDll ? g_dll : "", withDll ? "\"" : "", log);
    int rc = system(cmd);
    size_t n;
    char *t = px_read_file(log, &n);
    deflateLine[0] = 0;
    *oldPxtf = t && strstr(t, "top line 'NAME");   /* a reader of pxtf version 1 refuses the NAME top line of version 2 */
    if (t) { snprintf(deflateLine, cap, "%.*s", (int)strcspn(t, "\r\n"), t); free(t); }
    return rc == 0;
}

/* the packages pxfe writes of every font and typeface of the store, under their recommended names, in hash order */
static bool export_all(Store *s, const char *out) {
    char err[PX_ERR_LEN], dir[400];
    char (*h)[33] = (char (*)[33])malloc(33 * 1000);
    bool ok = true;
    StoreConn *c = store_conn_open(s);
    int n = hashes_of(s, "pxfm", h, 1000);
    snprintf(dir, sizeof dir, "%s/pxfp", out);
    for (int i = 0; i < n && ok; i++) if (!pkg_export_font(c, h[i], dir, NULL, 0, err)) { printf("      %s\n", err); ok = false; }
    n = hashes_of(s, "pxtf", h, 1000);
    snprintf(dir, sizeof dir, "%s/pxtp", out);
    for (int i = 0; i < n && ok; i++) if (!pkg_export_typeface(c, h[i], dir, NULL, 0, err)) { printf("      %s\n", err); ok = false; }
    store_conn_close(c);
    free(h);
    return ok;
}

/* the same names in both folders; `identical` gets how many are byte for byte the same; `sameFiles` whether every pair holds
 * the same files (the same check result: the names are the hashes of the contents) */
static bool compare_dirs(const char *a, const char *b, int *names, int *identical, bool *sameFiles) {
    static char na[200][260], nb[200][260];
    int ca = list_dir(a, na, 200), cb = list_dir(b, nb, 200);
    *names = ca;
    *identical = 0;
    *sameFiles = true;
    if (ca != cb) { printf("      %d files / %d files\n", ca, cb); return false; }
    for (int i = 0; i < ca; i++) {
        if (strcmp(na[i], nb[i]) != 0) { printf("      %s / %s\n", na[i], nb[i]); return false; }
        char pa[700], pb[700], err[PX_ERR_LEN];
        snprintf(pa, sizeof pa, "%s/%s", a, na[i]);
        snprintf(pb, sizeof pb, "%s/%s", b, nb[i]);
        if (same_file(pa, pb)) { (*identical)++; continue; }
        PkgImportResult ra, rb;
        bool ok = pkg_check(pa, &ra, err) && pkg_check(pb, &rb, err) && same_result(&ra, &rb);
        if (!ok) { *sameFiles = false; printf("      %s: other files\n", na[i]); }
        pkg_result_free(&ra);
        pkg_result_free(&rb);
    }
    return true;
}

/* ------------------------------------------------------------------ round trips and the reference */

static void test_library(const char *name) {
    char err[PX_ERR_LEN], db[200], dir[200], folder[300], mine[300], again[300], label[700];
    snprintf(db, sizeof db, "testdata/%s.pxfdb", name);
    snprintf(dir, sizeof dir, TMP "/%s", name);
    rm_rf(dir);
    px_mkdir_p(dir);
    printf("\n-- %s\n", db);
    Store *a = NULL;
    if (!store_open_pxfdb(db, NULL, &a, err)) { CHECK(false, "open the database"); printf("      %s\n", err); return; }
    /* pxfe's packages */
    snprintf(mine, sizeof mine, "%s/pxfe", dir);
    snprintf(label, sizeof label, "%s: a package of every font and typeface", name);
    CHECK(export_all(a, mine), label);
    /* round trip: every typeface package, then every font package, into an empty store */
    char copy[300];
    snprintf(copy, sizeof copy, "%s/empty.pxfdb", dir);
    Store *b = empty_store(db, copy);
    CHECK(b != NULL, "an empty store");
    if (b) {
        static char files[200][260];
        bool ok = true;
        int nAdded = 0, nSkipped = 0;
        for (int pass = 0; pass < 2 && ok; pass++) {
            char sub[400];
            snprintf(sub, sizeof sub, "%s/%s", mine, pass == 0 ? "pxtp" : "pxfp");
            int n = list_dir(sub, files, 200);
            for (int i = 0; i < n && ok; i++) {
                char p[700];
                PkgImportResult r;
                snprintf(p, sizeof p, "%s/%s", sub, files[i]);
                if (!pkg_import(b, p, &r, err)) { printf("      %s: %s\n", files[i], err); ok = false; break; }
                ok = r.kind == (pass == 0 ? PKG_TYPEFACE : PKG_FONT);
                nAdded += r.added.fonts + r.added.typefaces + r.added.glyphs + r.added.palettes;
                nSkipped += r.skipped.fonts + r.skipped.typefaces + r.skipped.glyphs + r.skipped.palettes;
                pkg_result_free(&r);
            }
        }
        printf("      %d files added, %d skipped (there already)\n", nAdded, nSkipped);
        snprintf(label, sizeof label, "%s: every package imported into an empty store", name);
        CHECK(ok, label);
        snprintf(label, sizeof label, "%s: the store made from the packages has the rows of the database", name);
        CHECK(ok && same_content(a, b), label);
        CHECK(store_modified(b), "the imports are a change of the store");
        snprintf(again, sizeof again, "%s/again", dir);
        int nn, ni;
        bool sf;
        CHECK(export_all(b, again), "the packages of the new store");
        char d1[400], d2[400];
        snprintf(d1, sizeof d1, "%s/pxfp", mine); snprintf(d2, sizeof d2, "%s/pxfp", again);
        bool c1 = compare_dirs(d1, d2, &nn, &ni, &sf) && ni == nn;
        snprintf(d1, sizeof d1, "%s/pxtp", mine); snprintf(d2, sizeof d2, "%s/pxtp", again);
        int nn2, ni2;
        bool c2 = compare_dirs(d1, d2, &nn2, &ni2, &sf) && ni2 == nn2;
        snprintf(label, sizeof label, "%s: its packages are byte for byte those of the database (%d + %d)", name, nn, nn2);
        CHECK(c1 && c2, label);
        store_close(b);
    }
    /* the reference, on the folder export of the same database */
    snprintf(folder, sizeof folder, "%s/folder", dir);
    StoreExportResult er;
    CHECK(store_export_folder(a, folder, STORE_EXPORT_OVERWRITE, NULL, &er, err), "the database exported to a folder");
    if (!g_python) {
        SKIP("byte identity with the reference: no python, or no pixelglyphlib (PXFE_PGL)");
    } else {
        for (int mode = 0; mode < 2; mode++) {
            if (mode == 0 && !g_dll[0]) { SKIP("byte identity with the reference under zlib: no zlib DLL (PXFE_ZLIB_DLL)"); continue; }
            char ref[300], line[200];
            snprintf(ref, sizeof ref, "%s/reference-%s", dir, mode == 0 ? "zlib" : "python");
            bool oldPxtf = false;
            bool ran = run_reference(folder, ref, mode == 0, line, sizeof line, &oldPxtf);
            if (!ran && oldPxtf) { SKIP("byte identity with the reference: its pixelglyphlib reads pxtf version 1 only"); continue; }
            snprintf(label, sizeof label, "%s: the reference wrote its packages (%s)", name, line);
            CHECK(ran, label);
            if (!ran) continue;
            bool zlibNg = strstr(line, "zlib-ng") != NULL;
            for (int k = 0; k < 2; k++) {
                char d1[400], d2[400];
                const char *sub = k == 0 ? "pxfp" : "pxtp";
                snprintf(d1, sizeof d1, "%s/%s", mine, sub);
                snprintf(d2, sizeof d2, "%s/%s", ref, sub);
                int nn, ni;
                bool sf;
                bool names = compare_dirs(d1, d2, &nn, &ni, &sf);
                if (zlibNg) {
                    snprintf(label, sizeof label, "%s: the same %d %s files as the reference (names, files; %d of them byte for byte under zlib-ng)", name, nn, sub, ni);
                    CHECK(names && sf, label);
                } else {
                    snprintf(label, sizeof label, "%s: the %d %s files of the reference, byte for byte (%s)", name, nn, sub, mode == 0 ? "zlib DLL" : "Python's zlib");
                    CHECK(names && ni == nn, label);
                }
            }
        }
    }
    store_close(a);
}

/* ------------------------------------------------------------------ reading: every rejection rule */

static void test_rejections(void) {
    char err[PX_ERR_LEN];
    printf("\n-- rejections\n");
    Store *s = NULL, *sc = NULL;
    if (!store_open_pxfdb("testdata/lib.pxfdb", NULL, &s, err) || !store_open_pxfdb("testdata/lib_colors.pxfdb", NULL, &sc, err)) { CHECK(false, "open"); return; }
    StoreConn *c = store_main_conn(s);
    g_into = sc;
    size_t fl, tl, gl;
    unsigned char *fz = pkg_font_bytes(c, H_EXAMPLE, &fl, err);
    unsigned char *tz = pkg_typeface_bytes(c, T_SANS, &tl, err);
    unsigned char *gz = pkg_font_bytes(c, H_GREY, &gl, err);
    EntryList font, tf, grey, l;
    CHECK(fz && tz && gz && read_zip(fz, fl, &font, err) && read_zip(tz, tl, &tf, err) && read_zip(gz, gl, &grey, err), "good packages to break");
    PkgImportResult r;
    CHECK(pkg_check_bytes(fz, fl, &r, err) && r.kind == PKG_FONT && r.nPxfm == 1 && strcmp(r.pxfm[0], H_EXAMPLE) == 0 && !r.pxtf[0], "a font package is read as one");
    pkg_result_free(&r);
    CHECK(pkg_check_bytes(tz, tl, &r, err) && r.kind == PKG_TYPEFACE && r.nPxfm == 2 && strcmp(r.pxtf, T_SANS) == 0, "a typeface package is read as one");
    pkg_result_free(&r);
    CHECK(pkg_check_bytes(gz, gl, &r, err) && r.nPalettes == 1, "a colour font package has its palette");
    pkg_result_free(&r);

    /* the zip file */
    expect_reject_bytes((const unsigned char *)"hello, world", 12, "not a zip file", "not a zip file");
    expect_reject_bytes(fz, fl - 10, "not a zip file", "a zip file cut short");
    {
        unsigned char *z = (unsigned char *)malloc(fl);
        memcpy(z, fz, fl);
        uint32_t cd = get32(z + fl - 22 + 16);
        z[cd + 16] ^= 0x55;                 /* the CRC-32 of the first entry in the central directory */
        expect_reject_bytes(z, fl, "CRC", "a bad CRC-32");
        memcpy(z, fz, fl);
        z[get16(z + 26) + 30 + 2] ^= 0xFF;   /* the first entry's compressed data */
        expect_reject_bytes(z, fl, "not a zip file", "bad compressed data");
        free(z);
    }
    clone_list(&font, &l); add_text(&l, l.e[1].name, (const char *)l.e[1].data); expect_reject(&l, "twice", "an entry twice");
    clone_list(&font, &l); add_text(&l, "pxgl/", ""); expect_reject(&l, "a folder entry", "an entry for a folder");
    clone_list(&font, &l); { char nm[80]; int i = first_with(&l, "pxgl/"); snprintf(nm, sizeof nm, "pxgl\\%s", l.e[i].name + 5); set_name(&l, i, nm); }
    expect_reject(&l, "not a plain relative name", "a backslash in a name");
    clone_list(&font, &l); { char nm[80]; int i = root_of(&l); snprintf(nm, sizeof nm, "/%s", l.e[i].name); set_name(&l, i, nm); }
    expect_reject(&l, "not a plain relative name", "a leading slash");
    clone_list(&font, &l); { char nm[80]; int i = first_with(&l, "pxgl/"); snprintf(nm, sizeof nm, "pxgl/../%s", l.e[i].name + 5); set_name(&l, i, nm); }
    expect_reject(&l, "not a plain relative name", "a .. part");
    /* Contents */
    clone_list(&font, &l); add_text(&l, "readme/notes.txt", "hello\n"); expect_reject(&l, "not at the root, in pxfm/, pxgl/ or palette/", "another folder");
    clone_list(&font, &l); { char nm[80]; int i = first_with(&l, "pxgl/"); snprintf(nm, sizeof nm, "pxgl/%.2s/%s", l.e[i].name + 5, l.e[i].name + 5); set_name(&l, i, nm); }
    expect_reject(&l, "not at the root", "a prefix folder in pxgl/");
    clone_list(&tf, &l); { char nm[80]; int i = root_of(&l); snprintf(nm, sizeof nm, "pxtf/%s", l.e[i].name); set_name(&l, i, nm); }
    expect_reject(&l, "not at the root", "a pxtf/ folder");
    clone_list(&font, &l); add_text(&l, "readme.txt", "hello\n"); expect_reject(&l, "2 files at the root", "a second file at the root");
    clone_list(&font, &l); remove_entry(&l, root_of(&l)); expect_reject(&l, "0 files at the root", "no file at the root");
    clone_list(&font, &l); { char nm[80]; int i = root_of(&l); snprintf(nm, sizeof nm, "pxfm/%s", l.e[i].name); add_text(&l, nm, (const char *)l.e[i].data); }
    expect_reject(&l, "a pxfm/ folder in a font package", "a font package with a pxfm/ folder");
    clone_list(&tf, &l); for (int i = l.n - 1; i >= 0; i--) if (strncmp(l.e[i].name, "pxfm/", 5) == 0) remove_entry(&l, i);
    expect_reject(&l, "no pxfm/ folder in a typeface package", "a typeface package without a pxfm/ folder");
    /* names and hashes */
    clone_list(&font, &l); { char nm[80]; int i = root_of(&l); snprintf(nm, sizeof nm, "%s", l.e[i].name); upper(nm); set_name(&l, i, nm); }
    expect_reject(&l, "not <hash>.pxfm", "an upper-case hash");
    clone_list(&font, &l); set_name(&l, root_of(&l), "font.pxfm"); expect_reject(&l, "not <hash>.pxfm", "a root that is no hash");
    clone_list(&font, &l); { char nm[80]; int i = first_with(&l, "pxgl/"); snprintf(nm, sizeof nm, "pxgl/%.32s.txt", l.e[i].name + 5); set_name(&l, i, nm); }
    expect_reject(&l, "not <hash>.pxgl", "a wrong extension");
    clone_list(&grey, &l); { char nm[80]; int i = first_with(&l, "palette/"); snprintf(nm, sizeof nm, "palette/%.32s.pxgl", l.e[i].name + 8); set_name(&l, i, nm); }
    expect_reject(&l, "not <hash>.hex", "a palette named .pxgl");
    clone_list(&font, &l); { int i = first_with(&l, "pxgl/"); set_data(&l, i, "0.\n.0\n"); } expect_reject(&l, "its hash is", "a glyph that is not the hash of its name");
    clone_list(&tf, &l); { int i = root_of(&l); append_text(&l, i, "\t"); l.e[i].data[l.e[i].len - 2] ^= 1; }   /* a changed letter */
    expect_reject(&l, "its hash is", "a pxtf that is not the hash of its name");
    clone_list(&font, &l); { int i = first_with(&l, "pxgl/"); set_data(&l, i, "0\xE9\n"); rehash(&l, i, NULL); } expect_reject(&l, "not ASCII", "a glyph that is no ASCII text");
    /* the strict readers */
    clone_list(&font, &l); { int i = root_of(&l); append_text(&l, i, "BOGUS\t1\n"); rehash(&l, i, NULL); }
    expect_reject(&l, "BOGUS", "a pxfm that fails its strict reader");
    clone_list(&font, &l); {   /* its lines out of canonical order */
        int i = root_of(&l);
        char *t = (char *)l.e[i].data, *nl = strchr(t, '\n'), buf[4096];
        snprintf(buf, sizeof buf, "%s%.*s", nl + 1, (int)(nl - t + 1), t);
        set_data(&l, i, buf);
        rehash(&l, i, NULL);
    }
    expect_reject(&l, "canonical", "a pxfm whose lines are not in canonical order");
    clone_list(&font, &l); renamed_file(&l, "pxgl/", "0x\n"); expect_reject(&l, ".pxgl: ", "a pxgl that fails its strict reader");
    clone_list(&grey, &l); renamed_file(&l, "palette/", "nothex\n"); expect_reject(&l, ".hex: ", "a palette that fails its strict reader");
    clone_list(&tf, &l); { int i = root_of(&l); append_text(&l, i, "\tBOGUS\tx\n"); rehash(&l, i, NULL); }
    expect_reject(&l, "BOGUS", "a pxtf that fails its strict reader");
    clone_list(&tf, &l); {   /* a pxfm of the typeface that fails its strict reader, the pxtf naming it */
        int i = first_with(&l, "pxfm/"), t = root_of(&l);
        char oldH[33], newH[33];
        snprintf(oldH, sizeof oldH, "%.32s", l.e[i].name + 5);
        append_text(&l, i, "BOGUS\t1\n");
        rehash(&l, i, newH);
        upper(oldH); upper(newH);
        replace_in(&l, t, oldH, newH);
        rehash(&l, t, NULL);
    }
    expect_reject(&l, "BOGUS", "a pxfm of a typeface package that fails its strict reader");
    /* the files name each other exactly */
    clone_list(&font, &l); remove_entry(&l, first_with(&l, "pxgl/")); expect_reject(&l, "is not in the package", "a pxgl missing");
    clone_list(&font, &l); { add_text(&l, "pxgl/x.pxgl", "0\n0\n0\n0\n0\n0\n0\n0\n0\n"); rehash(&l, l.n - 1, NULL); }
    expect_reject(&l, "is named by no pxfm file", "a pxgl named by no pxfm");
    clone_list(&grey, &l); remove_entry(&l, first_with(&l, "palette/")); expect_reject(&l, "is not in the package", "a palette missing");
    clone_list(&grey, &l); { add_text(&l, "palette/x.hex", "123456\n"); rehash(&l, l.n - 1, NULL); }
    expect_reject(&l, "is named by no pxfm file", "a palette named by no pxfm");
    clone_list(&tf, &l); remove_entry(&l, first_with(&l, "pxfm/")); expect_reject(&l, "named by the pxtf file, is not in the package", "a pxfm the pxtf names missing");
    clone_list(&tf, &l); {
        size_t n;
        char got[33], nm[80];
        char *t = store_pxfm_text(c, H_UNC, &n, got);
        snprintf(nm, sizeof nm, "pxfm/%s.pxfm", H_UNC);
        add_text(&l, nm, t);
        free(t);
    }
    expect_reject(&l, "is not named by the pxtf file", "a pxfm the pxtf does not name");
    /* positive: a file in another form than its canonical one is read in its canonical form, whatever the package is called */
    clone_list(&font, &l); {
        int i = first_with(&l, "pxgl/");
        char buf[4096];
        size_t k = 0;
        for (size_t q = 0; q < l.e[i].len && k + 4 < sizeof buf; q++) { if (l.e[i].data[q] == '\n') { buf[k++] = ' '; buf[k++] = '\r'; } buf[k++] = (char)l.e[i].data[q]; }
        buf[k] = 0;
        set_data(&l, i, buf);
        size_t len;
        unsigned char *z = zip_entries(&l, &len, err);
        px_write_file(TMP "/crlf.weird", z, len);
        CHECK(pkg_check(TMP "/crlf.weird", &r, err) && r.kind == PKG_FONT, "CR LF line ends and trailing blanks are read as the canonical form");
        if (r.kind != PKG_FONT) printf("      %s\n", err);
        pkg_result_free(&r);
        free(z);
        list_free(&l);
    }
    {   /* stored (not deflated) entries are read too */
        Buf out = {0}, cd = {0};
        for (int i = 0; i < font.n; i++) {
            const Entry *e = &font.e[i];
            uint32_t crc = (uint32_t)crc32(0L, e->data, (uInt)e->len), off = (uint32_t)out.n;
            size_t nl = strlen(e->name);
            put32(&out, 0x04034b50); put16(&out, 10); put16(&out, 0); put16(&out, 0); put16(&out, 0); put16(&out, 0x21);
            put32(&out, crc); put32(&out, (uint32_t)e->len); put32(&out, (uint32_t)e->len); put16(&out, (unsigned)nl); put16(&out, 0);
            buf_add(&out, e->name, nl); buf_add(&out, e->data, e->len);
            put32(&cd, 0x02014b50); put16(&cd, 10); put16(&cd, 10); put16(&cd, 0); put16(&cd, 0); put16(&cd, 0); put16(&cd, 0x21);
            put32(&cd, crc); put32(&cd, (uint32_t)e->len); put32(&cd, (uint32_t)e->len); put16(&cd, (unsigned)nl);
            put16(&cd, 0); put16(&cd, 0); put16(&cd, 0); put16(&cd, 0); put32(&cd, 0); put32(&cd, off);
            buf_add(&cd, e->name, nl);
        }
        uint32_t cdo = (uint32_t)out.n;
        buf_add(&out, cd.p, cd.n);
        put32(&out, 0x06054b50); put16(&out, 0); put16(&out, 0); put16(&out, (unsigned)font.n); put16(&out, (unsigned)font.n);
        put32(&out, (uint32_t)cd.n); put32(&out, cdo); put16(&out, 0);
        CHECK(pkg_check_bytes(out.p, out.n, &r, err) && r.kind == PKG_FONT, "a package of stored entries is read");
        pkg_result_free(&r);
        free(out.p);
        free(cd.p);
    }
    list_free(&font); list_free(&tf); list_free(&grey);
    free(fz); free(tz); free(gz);
    store_close(s);
    store_close(sc);
}

/* ------------------------------------------------------------------ importing: skipped files, a clash, names */

static void test_import(void) {
    char err[PX_ERR_LEN], path[400];
    printf("\n-- importing\n");
    Store *s = NULL;
    if (!store_open_pxfdb("testdata/lib.pxfdb", NULL, &s, err)) { CHECK(false, "open"); return; }
    StoreConn *c = store_main_conn(s);
    CHECK(pkg_write_typeface(c, T_SANS, TMP "/sans.pxfp", err), "a typeface package written under a font package's extension");
    CHECK(pkg_write_font(c, H_EXAMPLE, TMP "/regular.pxtp", err), "a font package written under a typeface package's extension");
    CHECK(pkg_write_font(c, H_GREY, TMP "/grey.pxfp", err), "a colour font package");
    PkgImportResult r;
    /* a store that has every file: all skipped, no change */
    CHECK(pkg_import(s, TMP "/sans.pxfp", &r, err) && r.kind == PKG_TYPEFACE && r.added.fonts + r.added.glyphs + r.added.typefaces + r.added.palettes == 0 &&
          r.skipped.typefaces == 1 && r.skipped.fonts == 2 && r.skipped.glyphs == r.nGlyphs, "into a store that has the files: each skipped (the kind by the content)");
    CHECK(!store_modified(s), "nothing skipped is a change");
    pkg_result_free(&r);
    store_close(s);

    /* an empty store: the font, then the typeface that holds it */
    Store *b = empty_store("testdata/lib.pxfdb", TMP "/import.pxfdb");
    if (!b) { CHECK(false, "an empty store"); return; }
    CHECK(pkg_import(b, TMP "/regular.pxtp", &r, err) && r.kind == PKG_FONT && r.added.fonts == 1 && r.added.glyphs == r.nGlyphs && r.added.typefaces == 0,
          "a font package into an empty store (the kind by the content)");
    CHECK(store_modified(b) && store_has_pxfm(store_main_conn(b), H_EXAMPLE), "the font is in the store, a change");
    int gl = r.nGlyphs;
    pkg_result_free(&r);
    CHECK(pkg_import(b, TMP "/grey.pxfp", &r, err) && r.added.palettes == 1, "a colour font package brings its palette");
    pkg_result_free(&r);
    /* a clash: a glyph of the store is made another bitmap of the same size, under the same hash */
    {
        sqlite3 *db = store_sqlite(store_main_conn(b));
        char sql[300];
        snprintf(sql, sizeof sql, "UPDATE glyph SET pixels = replace(replace(replace(pixels, '0', 'x'), '.', '0'), 'x', '.') WHERE hash = (SELECT glyph FROM pxfm_glyph WHERE pxfm = X'%s' AND glyph IS NOT NULL AND code = 266240)", H_EXAMPLE);
        CHECK(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK && sqlite3_changes(db) == 1, "a glyph row changed under its hash");
    }
    CHECK(!pkg_import(b, TMP "/sans.pxfp", &r, err) && strstr(err, "another content") != NULL, "a package with a file the store has with another content is refused");
    printf("      %s\n", err);
    CHECK(!store_has_pxtf(store_main_conn(b), T_SANS) && !store_has_pxfm(store_main_conn(b), H_NOASC), "and nothing of it was placed");
    (void)gl;
    store_close(b);

    /* names: the recommended ones, and -<8 hex> for a name taken by other bytes */
    if (!store_open_pxfdb("testdata/lib.pxfdb", NULL, &s, err)) { CHECK(false, "open"); return; }
    c = store_main_conn(s);
    char n1[300], n2[300], n3[300], n4[300];
    pkg_default_name_font(c, H_EXAMPLE, n1, sizeof n1);
    pkg_default_name_font(c, H_GREY, n2, sizeof n2);
    pkg_default_name_typeface(c, T_SANS, n3, sizeof n3);
    pkg_default_name_typeface(c, T_UNC, n4, sizeof n4);
    CHECK(strcmp(n1, "test-sans-regular.pxfp") == 0 && strcmp(n2, H_GREY ".pxfp") == 0 && strcmp(n3, "test-sans.pxtp") == 0 && strcmp(n4, "uncoded-mono.pxtp") == 0,
          "the recommended names (family-style, the hash, family)");
    printf("      %s %s %s %s\n", n1, n2, n3, n4);
    rm_rf(TMP "/names");
    char p1[400], p2[400], p3[400];
    bool ok = pkg_export_font(c, H_EXAMPLE, TMP "/names", p1, sizeof p1, err) && pkg_export_font(c, H_EXAMPLE, TMP "/names", p2, sizeof p2, err);
    CHECK(ok && strcmp(p1, TMP "/names/test-sans-regular.pxfp") == 0 && strcmp(p1, p2) == 0, "the same package again keeps its name");
    px_write_file(TMP "/names/test-sans-regular.pxfp", "other bytes", 11);
    ok = pkg_export_font(c, H_EXAMPLE, TMP "/names", p3, sizeof p3, err);
    snprintf(path, sizeof path, TMP "/names/test-sans-regular-%.8s.pxfp", H_EXAMPLE);
    CHECK(ok && strcmp(p3, path) == 0, "a name taken by other bytes gets -<8 hex digits of the hash>");
    store_close(s);
}

int main(void) {
    px_mkdir_p(TMP);
    find_reference();
    test_rejections();
    test_import();
    test_library("lib");
    test_library("lib_colors");
    test_library("lib_many");
    printf("\n%d passed, %d failed, %d skipped\n", g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
