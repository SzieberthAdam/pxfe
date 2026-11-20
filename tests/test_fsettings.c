/* test_fsettings.c - tests of src/fsettings.c.  Run from the pxfe directory (build.bat test does).  Output files go to tests/tmp. */
#include "fsettings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#define MKDIR(d) mkdir(d, 0777)
#endif

#include "sqlite3.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

#define TMP "tests/tmp"
#define HA "3b52125b2e5f6466823baa4fa3186fa0"
#define HB "03696f658319e9dfddc2cb6ea2520592"
#define HC "4fba4190f6ba7e10b296c7070234e1ce"

static bool exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

static void write_text(const char *p, const char *s) { FILE *f = fopen(p, "wb"); if (f) { fputs(s, f); fclose(f); } }

static int add(FSettings *fs, const char *hash, int kind, const char *src, const char *label, int pos) {
    FsLayer l = fs_layer_default();
    l.kind = kind;
    snprintf(l.source, sizeof l.source, "%s", src);
    snprintf(l.label, sizeof l.label, "%s", label);
    l.position = pos;
    return fs_add_layer(fs, hash, &l);
}

static bool corr_eq(const FsCorrection *a, const FsCorrection *b) { return a->dx == b->dx && a->dy == b->dy && a->dsize == b->dsize && a->hidden == b->hidden; }

static bool layer_eq(const FsLayer *a, const FsLayer *b, bool sameId) {
    return (!sameId || a->id == b->id) && strcmp(a->font, b->font) == 0 && a->position == b->position && a->kind == b->kind &&
           strcmp(a->source, b->source) == 0 && a->face == b->face && a->fileSize == b->fileSize && a->fileMtime == b->fileMtime &&
           a->scale == b->scale && a->dx == b->dx && a->dy == b->dy && a->r == b->r && a->g == b->g && a->b == b->b &&
           a->opacity == b->opacity && a->visible == b->visible && strcmp(a->label, b->label) == 0;
}

static void test_sidecar_path(void) {
    char o[64];
    CHECK(fs_sidecar_path("lib.pxfdb", o, sizeof o) && strcmp(o, "lib.pxfdb-settings") == 0, "sidecar_path simple");
    CHECK(fs_sidecar_path("C:\\a b\\lib.pxfdb", o, sizeof o) && strcmp(o, "C:\\a b\\lib.pxfdb-settings") == 0, "sidecar_path with directory and space");
    CHECK(!fs_sidecar_path("", o, sizeof o), "sidecar_path empty fails");
    CHECK(!fs_sidecar_path("lib.pxfdb", o, 10), "sidecar_path too small fails");
    CHECK(fs_sidecar_path("lib.pxfdb", o, 19) && strlen(o) == 18, "sidecar_path exact fit");
}

static void test_layers(void) {
    FSettings *fs = fs_new_empty();
    CHECK(fs != NULL, "new_empty");
    CHECK(!fs_has_any(fs) && !fs_dirty(fs) && fs_path(fs)[0] == 0, "new_empty: empty, clean, no path");
    CHECK(!fs_has_font(fs, HA), "has_font false");

    FsLayer l = fs_layer_default();
    l.kind = 1; snprintf(l.source, sizeof l.source, "%s", "C:\\fonts\\\xC3\xA1rv\xC3\xADzt\xC5\xB1r\xC5\x91.ttc");
    l.face = 2; l.fileSize = 123456789012LL; l.fileMtime = 1700000000123LL;
    l.scale = 0.75f; l.dx = 1.5f; l.dy = -2.25f; l.r = 10; l.g = 200; l.b = 255; l.opacity = 40; l.visible = 0;
    snprintf(l.label, sizeof l.label, "%s", "Arvizturo");
    int a = fs_add_layer(fs, HA, &l);
    CHECK(a > 0, "add_layer returns an id");
    CHECK(fs_dirty(fs) && fs_changes(fs) == 1, "dirty after add");
    int b = add(fs, HA, 0, HB, "second", -1);
    int c = add(fs, HA, 0, HC, "third", -1);
    int d = add(fs, HA, 0, HB, "top", 0);
    CHECK(a > 0 && b > a && c > b && d > c, "ids are increasing and unique");
    FsLayer out[8];
    int n = fs_list_layers(fs, HA, out, 8);
    CHECK(n == 4 && out[0].id == d && out[1].id == a && out[2].id == b && out[3].id == c, "list order: top insert, then append");
    CHECK(out[1].position == 1 && out[3].position == 3, "positions are contiguous");
    FsLayer g;
    CHECK(fs_get_layer(fs, a, &g) && layer_eq(&g, &out[1], true) && strcmp(g.source, l.source) == 0 && g.fileSize == l.fileSize &&
          g.fileMtime == l.fileMtime && g.scale == 0.75f && g.dy == -2.25f && g.opacity == 40 && g.visible == 0 && g.face == 2,
          "get_layer round trip of every field (UTF-8 path)");
    CHECK(fs_list_layers(fs, HA, out, 2) == 4, "list with small array returns the full count");
    CHECK(fs_list_layers(fs, HB, out, 8) == 0 && fs_list_layers(fs, "nothex", out, 8) == 0, "list of other / bad font is empty");

    /* update */
    g.scale = 2.0f; g.dx = 3; g.visible = 1; g.opacity = 55; g.r = 1; snprintf(g.label, sizeof g.label, "%s", "renamed");
    g.position = 99; g.id = a;
    CHECK(fs_update_layer(fs, &g), "update_layer");
    FsLayer g2;
    fs_get_layer(fs, a, &g2);
    CHECK(g2.scale == 2.0f && g2.dx == 3 && g2.visible == 1 && g2.opacity == 55 && g2.r == 1 && strcmp(g2.label, "renamed") == 0 &&
          g2.position == 1, "update changed the fields, not the position");
    g.id = 99999;
    CHECK(!fs_update_layer(fs, &g) && fs_error(fs)[0], "update of unknown id fails with a message");
    g.id = a; g.scale = 0;
    CHECK(!fs_update_layer(fs, &g), "scale 0 rejected");
    g.scale = 1; g.opacity = 500; g.g = -5; g.visible = 7;
    CHECK(fs_update_layer(fs, &g), "opacity/colour clamped");
    fs_get_layer(fs, a, &g2);
    CHECK(g2.opacity == 100 && g2.g == 0 && g2.visible == 1, "clamped values stored");

    /* move: order now d a b c */
    CHECK(fs_move_layer(fs, c, 0), "move to top");
    n = fs_list_layers(fs, HA, out, 8);
    CHECK(out[0].id == c && out[1].id == d && out[2].id == a && out[3].id == b, "after move c to 0: c d a b");
    CHECK(fs_move_layer(fs, c, 99), "move beyond the end clamps");
    n = fs_list_layers(fs, HA, out, 8);
    CHECK(out[0].id == d && out[1].id == a && out[2].id == b && out[3].id == c, "c at the bottom: d a b c");
    unsigned ch = fs_changes(fs);
    CHECK(fs_move_layer(fs, c, 3) && fs_changes(fs) == ch, "move to the same place changes nothing");
    CHECK(fs_move_layer(fs, d, 2), "move down");
    n = fs_list_layers(fs, HA, out, 8);
    CHECK(out[0].id == a && out[1].id == b && out[2].id == d && out[3].id == c, "a b d c");
    CHECK(!fs_move_layer(fs, 424242, 0), "move unknown id fails");

    /* other font keeps its own stack */
    int e = add(fs, HB, 0, HA, "other", -1);
    CHECK(fs_list_layers(fs, HB, out, 8) == 1 && out[0].position == 0 && out[0].id == e, "second font has its own stack");

    /* corrections and remove */
    FsCorrection k = fs_correction_default();
    k.dx = 1;
    CHECK(fs_set_correction(fs, b, 65, &k) && fs_set_correction(fs, b, 66, &k), "set corrections");
    CHECK(fs_remove_layer(fs, b), "remove_layer");
    n = fs_list_layers(fs, HA, out, 8);
    CHECK(n == 3 && out[0].id == a && out[1].id == d && out[2].id == c && out[1].position == 1 && out[2].position == 2, "positions close up");
    CHECK(!fs_get_correction(fs, b, 65, NULL) && fs_list_corrections(fs, b, NULL, 0) == 0, "removed layer's corrections are gone");
    CHECK(!fs_remove_layer(fs, b), "remove again fails");
    CHECK(!fs_set_correction(fs, b, 65, &k), "correction for a removed layer fails");
    int e2 = add(fs, HA, 0, HC, "after remove", -1);
    CHECK(e2 > c, "ids are not reused");

    fs_close(fs);
}

static void test_corrections(void) {
    FSettings *fs = fs_new_empty();
    int a = add(fs, HA, 0, HB, "a", -1), b = add(fs, HA, 0, HC, "b", -1);
    FsCorrection k = fs_correction_default(), o;
    CHECK(!fs_get_correction(fs, a, 65, &o), "get: none yet");
    unsigned ch = fs_changes(fs);
    CHECK(fs_set_correction(fs, a, 65, &k) && fs_changes(fs) == ch, "set default on nothing: no row, no change");
    k.dx = 0.5f; k.dy = -1; k.dsize = 1.25f; k.hidden = 0;
    CHECK(fs_set_correction(fs, a, 65, &k), "set");
    CHECK(fs_get_correction(fs, a, 65, &o) && corr_eq(&k, &o), "get returns what was set");
    CHECK(!fs_get_correction(fs, b, 65, &o), "other layer unaffected");
    FsCorrection h = fs_correction_default(); h.hidden = 1;
    CHECK(fs_set_correction(fs, a, 0x1F600, &h) && fs_set_correction(fs, a, 0, &h) && fs_set_correction(fs, a, 66, &k), "set more (hidden only, astral, zero)");
    k.dx = 9;
    CHECK(fs_set_correction(fs, a, 65, &k) && fs_get_correction(fs, a, 65, &o) && o.dx == 9, "set replaces");
    PxCode codes[8];
    int n = fs_list_corrections(fs, a, codes, 8);
    CHECK(n == 4 && codes[0] == 0 && codes[1] == 65 && codes[2] == 66 && codes[3] == 0x1F600, "list codes ascending");
    CHECK(fs_list_corrections(fs, a, codes, 1) == 4, "list count with small array");
    FsCorrection def = fs_correction_default();
    CHECK(fs_set_correction(fs, a, 65, &def) && !fs_get_correction(fs, a, 65, &o), "set all-default deletes the row");
    CHECK(fs_list_corrections(fs, a, codes, 8) == 3, "3 left");
    FsCorrection bad = fs_correction_default(); bad.dsize = 0;
    CHECK(!fs_set_correction(fs, a, 70, &bad), "dsize 0 rejected");
    fs_set_correction(fs, b, 65, &h);
    CHECK(fs_clear_corrections(fs, a) && fs_list_corrections(fs, a, codes, 8) == 0 && fs_list_corrections(fs, b, codes, 8) == 1, "clear affects only one layer");
    fs_close(fs);
}

static void test_files(void) {
    MKDIR(TMP);
    const char *P = TMP "/fs_main.pxfdb-settings";
    remove(P);
    char err[FS_ERR_LEN];
    FSettings *fs = fs_open(P, err);
    CHECK(fs != NULL && !fs_has_any(fs) && !fs_dirty(fs) && strcmp(fs_path(fs), P) == 0, "open missing file: empty handle bound to the path");
    CHECK(fs_save(fs) && !exists(P), "save of an empty handle writes no file");

    FsLayer l = fs_layer_default();
    l.kind = 1; snprintf(l.source, sizeof l.source, "%s", "x/y.ttf"); l.fileSize = 5000000000LL; l.fileMtime = 99; l.scale = 1.5f;
    l.dx = 0.1f; l.r = 9; l.opacity = 33; snprintf(l.label, sizeof l.label, "%s", "Lbl");
    int a = fs_add_layer(fs, HA, &l);
    add(fs, HA, 0, HB, "b", -1);
    int c = add(fs, HB, 0, HC, "c", -1);
    FsCorrection k = {0.5f, -0.25f, 1.1f, 1}, k2 = {2, 0, 1, 0};
    fs_set_correction(fs, a, 65, &k);
    fs_set_correction(fs, a, 66, &k2);
    fs_set_correction(fs, c, 1, &k2);
    CHECK(fs_dirty(fs), "dirty before save");
    CHECK(fs_save(fs), "save");
    CHECK(!fs_dirty(fs) && exists(P), "clean after save, file exists");
    char tmp[200]; snprintf(tmp, sizeof tmp, "%s.tmp", P);
    CHECK(!exists(tmp), "no temp file left");
    CHECK(fs_save(fs), "save over an existing file");

    FSettings *r = fs_open(P, err);
    CHECK(r != NULL, "reopen");
    if (r) {
        FsLayer x[4], y[4];
        int n1 = fs_list_layers(fs, HA, x, 4), n2 = fs_list_layers(r, HA, y, 4);
        bool same = n1 == 2 && n2 == 2;
        for (int i = 0; same && i < 2; i++) same = layer_eq(&x[i], &y[i], true);
        CHECK(same, "layers of HA equal after reopen (ids too)");
        n1 = fs_list_layers(fs, HB, x, 4); n2 = fs_list_layers(r, HB, y, 4);
        CHECK(n1 == 1 && n2 == 1 && layer_eq(&x[0], &y[0], true), "layers of HB equal");
        FsCorrection o;
        CHECK(fs_get_correction(r, a, 65, &o) && corr_eq(&o, &k) && fs_get_correction(r, a, 66, &o) && corr_eq(&o, &k2) &&
              fs_get_correction(r, c, 1, &o) && corr_eq(&o, &k2) && fs_list_corrections(r, a, NULL, 0) == 2, "corrections equal after reopen");
        CHECK(!fs_dirty(r), "reopened handle is clean");
        int d = add(r, HA, 0, HC, "new", -1);
        CHECK(d > c, "ids continue after reopen");
        CHECK(fs_dirty(r), "dirty after change");

        /* copy_font */
        CHECK(fs_copy_font(r, HA, HC), "copy_font");
        FsLayer z[8];
        int nz = fs_list_layers(r, HC, z, 8), na = fs_list_layers(r, HA, x, 4);
        CHECK(nz == 3 && na == 3, "copy has the same number of layers, original stays");
        bool ok = nz == 3;
        int maxId = 0;
        for (int i = 0; i < na; i++) if (x[i].id > maxId) maxId = x[i].id;
        for (int i = 0; ok && i < 3; i++) {
            ok = z[i].id > maxId && z[i].position == x[i].position && strcmp(z[i].source, x[i].source) == 0 && z[i].scale == x[i].scale &&
                 z[i].fileSize == x[i].fileSize && strcmp(z[i].label, x[i].label) == 0 && strcmp(z[i].font, HC) == 0;
            for (int j = 0; j < 3; j++) if (z[i].id == x[j].id) ok = false;
        }
        CHECK(ok, "copy has new ids and the same fields");
        FsCorrection o2;
        CHECK(fs_get_correction(r, z[0].id, 65, &o2) && corr_eq(&o2, &k) && fs_list_corrections(r, z[0].id, NULL, 0) == 2 &&
              fs_get_correction(r, a, 65, &o2) && corr_eq(&o2, &k), "copy has the corrections; original keeps its own");
        fs_set_correction(r, z[0].id, 65, &k2);
        fs_get_correction(r, a, 65, &o2);
        CHECK(corr_eq(&o2, &k), "editing the copy does not touch the original");
        /* copy replaces what was there */
        CHECK(fs_copy_font(r, HB, HC), "copy onto an existing font");
        nz = fs_list_layers(r, HC, z, 8);
        CHECK(nz == 1 && strcmp(z[0].source, HC) == 0 && fs_list_corrections(r, z[0].id, NULL, 0) == 1, "target's layers were replaced");
        CHECK(!fs_copy_font(r, HA, HA), "copy onto itself fails");
        CHECK(!fs_copy_font(r, HA, "zz"), "copy to bad hash fails");
        CHECK(fs_copy_font(r, HC, HB) && fs_list_layers(r, HB, z, 8) == 1, "copy HC onto HB");
        CHECK(fs_copy_font(r, "11111111111111111111111111111111", HB) && !fs_has_font(r, HB), "copy of a font without layers clears the target");

        /* remove_font */
        CHECK(fs_has_font(r, HA) && fs_remove_font(r, HA) && !fs_has_font(r, HA), "remove_font");
        CHECK(fs_list_corrections(r, a, NULL, 0) == 0 && fs_has_font(r, HC), "its corrections are gone, others stay");
        CHECK(fs_remove_font(r, HA), "remove_font of an absent font is fine");

        /* save_as */
        const char *P2 = TMP "/fs_copy.pxfdb-settings";
        remove(P2);
        CHECK(fs_save_as(r, P2) && strcmp(fs_path(r), P2) == 0 && exists(P2) && !fs_dirty(r), "save_as switches the path");
        FSettings *r2 = fs_open(P2, err);
        CHECK(r2 && fs_has_font(r2, HC) && !fs_has_font(r2, HA), "save_as file has the new content");
        fs_close(r2);
        FSettings *orig = fs_open(P, err);
        CHECK(orig && fs_has_font(orig, HA), "the old file is untouched by save_as");
        fs_close(orig);

        /* empty policy */
        fs_remove_font(r, HC);
        fs_remove_font(r, HB);
        CHECK(!fs_has_any(r) && fs_dirty(r), "all removed: empty and dirty");
        CHECK(fs_save(r) && !exists(P2) && !fs_dirty(r), "save of an empty handle deletes the existing sidecar");
        CHECK(fs_save(r), "and saving again is fine");
        fs_close(r);
    }
    fs_close(fs);

    /* import: new_empty -> save_as */
    FSettings *n = fs_new_empty();
    CHECK(!fs_save(n), "save without a path fails");
    add(n, HA, 0, HB, "x", -1);
    const char *P3 = TMP "/fs_new.pxfdb-settings";
    remove(P3);
    CHECK(fs_save_as(n, P3) && exists(P3), "new_empty + save_as creates the file");
    fs_close(n);
    remove(P3);
}

static void test_errors(void) {
    char err[FS_ERR_LEN];
    const char *C = TMP "/fs_corrupt.pxfdb-settings";
    write_text(C, "this is not a sqlite database, just some text that is long enough to look like something");
    FSettings *fs = fs_open(C, err);
    CHECK(fs == NULL && err[0], "corrupt file: NULL + message");
    printf("      (%s)\n", err);
    write_text(C, "");
    fs = fs_open(C, err);
    CHECK(fs == NULL && err[0], "zero-byte file: not a settings db");
    remove(C);

    /* a sqlite file that is not a settings db */
    sqlite3 *db;
    sqlite3_open(C, &db);
    sqlite3_exec(db, "CREATE TABLE foo (x)", NULL, NULL, NULL);
    sqlite3_close(db);
    fs = fs_open(C, err);
    CHECK(fs == NULL && strstr(err, "not a settings"), "foreign sqlite db rejected");
    remove(C);

    /* too-new schema */
    FSettings *f = fs_new_empty();
    add(f, HA, 0, HB, "x", -1);
    CHECK(fs_save_as(f, C), "write a good file");
    fs_close(f);
    sqlite3_open(C, &db);
    sqlite3_exec(db, "UPDATE meta SET value='99' WHERE key='schema_version'", NULL, NULL, NULL);
    sqlite3_close(db);
    fs = fs_open(C, err);
    CHECK(fs == NULL && strstr(err, "schema version 99"), "too-new schema: NULL + message");
    printf("      (%s)\n", err);
    remove(C);

    /* a version 1 file (no em, aspect, follow, mode, threshold) is upgraded when it is opened */
    sqlite3_open(C, &db);
    sqlite3_exec(db,
        "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;"
        "INSERT INTO meta VALUES ('format', 'pxfe-settings'), ('schema_version', '1');"
        "CREATE TABLE layer (id INTEGER PRIMARY KEY AUTOINCREMENT, font TEXT NOT NULL, pos INTEGER NOT NULL, kind INTEGER NOT NULL,"
        "  source TEXT NOT NULL, face INTEGER NOT NULL, file_size INTEGER NOT NULL, file_mtime INTEGER NOT NULL, scale REAL NOT NULL,"
        "  dx REAL NOT NULL, dy REAL NOT NULL, r INTEGER NOT NULL, g INTEGER NOT NULL, b INTEGER NOT NULL, opacity INTEGER NOT NULL,"
        "  visible INTEGER NOT NULL, label TEXT NOT NULL,"
        "  CHECK (kind IN (0, 1) AND scale > 0 AND opacity BETWEEN 0 AND 100 AND visible IN (0, 1)));"
        "CREATE INDEX layer_by_font ON layer (font, pos);"
        "CREATE TABLE correction (font TEXT NOT NULL, layer INTEGER NOT NULL REFERENCES layer, code INTEGER NOT NULL,"
        "  dx REAL NOT NULL, dy REAL NOT NULL, dsize REAL NOT NULL, hidden INTEGER NOT NULL, PRIMARY KEY (layer, code)) WITHOUT ROWID;"
        "INSERT INTO layer (font,pos,kind,source,face,file_size,file_mtime,scale,dx,dy,r,g,b,opacity,visible,label)"
        "  VALUES ('" HA "',0,1,'old.ttf',0,10,20,1.25,0.5,-1,1,2,3,80,1,'Old');"
        "INSERT INTO correction VALUES ('" HA "',1,65,1,0,1,0), ('" HA "',1,15728641,2,0,1,0), ('" HA "',1,16777215,3,0,1,0);", NULL, NULL, NULL);
    sqlite3_close(db);
    fs = fs_open(C, err);
    CHECK(fs != NULL, "a version 1 file opens");
    if (fs) {
        FsLayer v[2];
        CHECK(fs_list_layers(fs, HA, v, 2) == 1 && v[0].scale == 1.25f && v[0].dx == 0.5f && strcmp(v[0].label, "Old") == 0, "version 1 layer is read");
        CHECK(v[0].em == 0 && v[0].aspect == 1 && v[0].follow == 0 && v[0].mode == 0 && v[0].threshold == 0.5f, "version 1 layer gets the defaults of version 2");
        CHECK(!fs_dirty(fs), "the upgrade alone does not make the settings unsaved");
        PxCode cc[4];
        CHECK(fs_list_corrections(fs, v[0].id, cc, 4) == 3 && cc[0] == px_code_cp(65) && cc[1] == px_code_symbol(1) && cc[2] == PX_CODE_DEFAULT,
              "the codes of version 3 corrections become pxfm version 3 codes (65, F00001, FFFFFF)");
        v[0].em = 13.5f; v[0].aspect = 0.75f; v[0].follow = 1; v[0].mode = 1; v[0].threshold = 0.375f;
        CHECK(fs_update_layer(fs, &v[0]) && fs_save(fs), "the new fields are written");
        fs_close(fs);
        fs = fs_open(C, err);
        FsLayer w[2];
        CHECK(fs && fs_list_layers(fs, HA, w, 2) == 1 && w[0].em == 13.5f && w[0].aspect == 0.75f && w[0].follow == 1 && w[0].mode == 1 && w[0].threshold == 0.375f,
              "the new fields come back from the file");
        if (fs) {
            CHECK(fs_copy_font(fs, HA, HB) && fs_list_layers(fs, HB, w, 2) == 1 && w[0].em == 13.5f && w[0].follow == 1, "a copied layer keeps the new fields");
            fs_close(fs);
        }
        sqlite3_open(C, &db);
        sqlite3_stmt *st = NULL;
        sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='schema_version'", -1, &st, NULL);
        CHECK(sqlite3_step(st) == SQLITE_ROW && atoi((const char *)sqlite3_column_text(st, 0)) == 4, "the saved file is version 4");
        sqlite3_finalize(st);
        sqlite3_close(db);
    }
    remove(C);

    /* an empty handle at a corrupt path: save must not clobber a foreign file */
    write_text(C, "precious data that is not a settings file");
    FSettings *e = fs_new_empty();
    CHECK(fs_save_as(e, C) && exists(C), "empty save_as leaves a foreign file alone");
    fs_close(e);
    remove(C);

    FSettings *z = fs_new_empty();
    CHECK(add(z, "short", 0, HB, "x", -1) < 0, "bad hash rejected");
    CHECK(add(z, "3B52125B2E5F6466823BAA4FA3186FA0", 0, HB, "x", -1) < 0, "upper-case hash rejected");
    CHECK(add(z, HA, 5, HB, "x", -1) < 0, "bad kind rejected");
    CHECK(add(z, HA, 0, "", "x", -1) < 0, "empty source rejected");
    CHECK(!fs_has_any(z), "nothing was added by failed calls");
    fs_close(z);
}

static void test_locks(void) {
    char err[FS_ERR_LEN];
    const char *C = TMP "/fs_lock.pxfdb-settings";
    remove(C);
    FSettings *fs = fs_new_empty();
    CHECK(!fs_is_unlocked(fs, 0, HA) && !fs_has_any(fs), "locked by default, and a handle with nothing set holds nothing");
    CHECK(fs_set_unlocked(fs, 0, HA, true) && fs_is_unlocked(fs, 0, HA) && !fs_is_unlocked(fs, 1, HA) && fs_has_any(fs) && fs_dirty(fs), "a font unlocked: a typeface of the same hash is not");
    int id = add(fs, HA, 0, HB, "L", -1);
    FsCorrection c = fs_correction_default();
    c.dx = 2;
    CHECK(id > 0 && fs_set_correction(fs, id, 0x41, &c) && fs_set_unlocked(fs, 1, HB, true), "a layer with a correction, and an unlocked typeface");
    CHECK(fs_save_as(fs, C), "saved (the lock alone keeps the file alive)");
    fs_close(fs);
    fs = fs_open(C, err);
    CHECK(fs && fs_is_unlocked(fs, 0, HA) && fs_is_unlocked(fs, 1, HB), "the lock state is read back from the file");
    CHECK(fs_rename_font(fs, HA, HC) && !fs_is_unlocked(fs, 0, HA) && fs_is_unlocked(fs, 0, HC), "renaming a font: the unlocked row follows");
    FsLayer l[2];
    CHECK(fs_list_layers(fs, HA, l, 2) == 0 && fs_list_layers(fs, HC, l, 2) == 1 && fs_get_correction(fs, l[0].id, 0x41, &c) && c.dx == 2, "the layers and corrections follow");
    int id2 = add(fs, HB, 0, HC, "uses C", -1);
    CHECK(id2 > 0 && fs_rename_font(fs, HC, HA) && fs_list_layers(fs, HB, l, 2) == 1 && strcmp(l[0].source, HA) == 0, "a layer of another font that uses the renamed one as its source follows");
    CHECK(fs_rename_typeface(fs, HB, HC) && !fs_is_unlocked(fs, 1, HB) && fs_is_unlocked(fs, 1, HC), "renaming a typeface: the unlocked row follows");
    CHECK(fs_set_unlocked(fs, 0, HA, false) && fs_set_unlocked(fs, 1, HC, false) && !fs_is_unlocked(fs, 0, HA) && !fs_is_unlocked(fs, 1, HC), "locking again");
    fs_close(fs);
    remove(C);
}

static void test_speed(void) {
    FSettings *fs = fs_new_empty();
    int a = add(fs, HA, 0, HB, "a", -1);
    clock_t t0 = clock();
    FsCorrection k = {0.5f, 0.5f, 1.0f, 0};
    bool ok = true;
    for (int i = 0; i < 1000; i++) { k.dx = (float)(i % 7) + 0.5f; ok = fs_set_correction(fs, a, 0x4E00 + i, &k) && ok; }
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    PxCode *codes = (PxCode *)malloc(1000 * sizeof *codes);
    int n = fs_list_corrections(fs, a, codes, 1000);
    int found = 0;
    t0 = clock();
    for (int i = 0; i < 1000; i++) { FsCorrection o; if (fs_get_correction(fs, a, 0x4E00 + i, &o)) found++; }
    double ms2 = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    CHECK(ok && n == 1000 && found == 1000 && codes[0] == 0x4E00 && codes[999] == 0x4E00 + 999, "1000 corrections set, listed, found");
    printf("      1000 sets: %.1f ms, 1000 gets: %.1f ms\n", ms, ms2);
    CHECK(ms < 2000 && ms2 < 2000, "1000 corrections are fast");
    const char *P = TMP "/fs_speed.pxfdb-settings";
    remove(P);
    CHECK(fs_save_as(fs, P), "save 1000 corrections");
    FSettings *r = fs_open(P, NULL);
    CHECK(r && fs_list_corrections(r, a, NULL, 0) == 1000, "reopened 1000 corrections");
    fs_close(r);
    remove(P);
    free(codes);
    fs_close(fs);
}

int main(void) {
    MKDIR(TMP);
    test_sidecar_path();
    test_layers();
    test_corrections();
    test_files();
    test_errors();
    test_locks();
    test_speed();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
