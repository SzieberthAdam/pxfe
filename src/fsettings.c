/* fsettings.c - the settings sidecar. See fsettings.h for the schema and the empty policy. Portable C99 + SQLite. */
#include "fsettings.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

struct FSettings {
    sqlite3 *db;                 /* the in-memory working copy */
    char path[FS_PATH_LEN];      /* "" = no file yet */
    char err[FS_ERR_LEN];
    unsigned gen, savedGen;
};

static const char SCHEMA[] =
"CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;\n"
"INSERT INTO meta VALUES ('format', 'pxfe-settings'), ('schema_version', '3');\n"
"CREATE TABLE layer (id INTEGER PRIMARY KEY AUTOINCREMENT, font TEXT NOT NULL, pos INTEGER NOT NULL, kind INTEGER NOT NULL,\n"
"  source TEXT NOT NULL, face INTEGER NOT NULL, file_size INTEGER NOT NULL, file_mtime INTEGER NOT NULL, scale REAL NOT NULL,\n"
"  dx REAL NOT NULL, dy REAL NOT NULL, r INTEGER NOT NULL, g INTEGER NOT NULL, b INTEGER NOT NULL, opacity INTEGER NOT NULL,\n"
"  visible INTEGER NOT NULL, label TEXT NOT NULL,\n"
"  em REAL NOT NULL DEFAULT 0, aspect REAL NOT NULL DEFAULT 1, follow INTEGER NOT NULL DEFAULT 0,\n"
"  mode INTEGER NOT NULL DEFAULT 0, threshold REAL NOT NULL DEFAULT 0.5,\n"
"  CHECK (kind IN (0, 1) AND scale > 0 AND opacity BETWEEN 0 AND 100 AND visible IN (0, 1)));\n"
"CREATE INDEX layer_by_font ON layer (font, pos);\n"
"CREATE TABLE correction (font TEXT NOT NULL, layer INTEGER NOT NULL REFERENCES layer, code INTEGER NOT NULL,\n"
"  dx REAL NOT NULL, dy REAL NOT NULL, dsize REAL NOT NULL, hidden INTEGER NOT NULL,\n"
"  PRIMARY KEY (layer, code)) WITHOUT ROWID;\n"
"CREATE TABLE unlocked (kind INTEGER NOT NULL, hash TEXT NOT NULL, PRIMARY KEY (kind, hash)) WITHOUT ROWID;\n";

static bool seterr(FSettings *fs, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(fs->err, FS_ERR_LEN, fmt, ap);
    va_end(ap);
    return false;
}

static bool sqlerr(FSettings *fs, const char *what) {
    return seterr(fs, "%s: sqlite: %s", what, sqlite3_errmsg(fs->db));
}

const char *fs_error(const FSettings *fs) { return fs ? fs->err : ""; }
const char *fs_path(const FSettings *fs) { return fs->path; }
bool fs_dirty(const FSettings *fs) { return fs->gen != fs->savedGen; }
unsigned fs_changes(const FSettings *fs) { return fs->gen; }

FsLayer fs_layer_default(void) {
    FsLayer l;
    memset(&l, 0, sizeof l);
    l.position = -1;
    l.scale = 1.0f;
    l.aspect = 1.0f;
    l.threshold = 0.5f;
    l.opacity = 100;
    l.visible = 1;
    return l;
}

FsCorrection fs_correction_default(void) {
    FsCorrection c = {0.0f, 0.0f, 1.0f, 0};
    return c;
}

static bool is_default(const FsCorrection *c) { return c->dx == 0.0f && c->dy == 0.0f && c->dsize == 1.0f && !c->hidden; }

bool fs_sidecar_path(const char *dbPath, char *out, int n) {
    if (!dbPath || !*dbPath || !out || n <= 0) return false;
    int len = snprintf(out, (size_t)n, "%s-settings", dbPath);
    if (len < 0 || len >= n) { out[0] = 0; return false; }
    return true;
}

static bool valid_hash(const char *h) {
    if (!h) return false;
    for (int i = 0; i < 32; i++) if (!((h[i] >= '0' && h[i] <= '9') || (h[i] >= 'a' && h[i] <= 'f'))) return false;
    return h[32] == 0;
}

/* ------------------------------------------------------------------ sqlite helpers */

static sqlite3_stmt *prep(FSettings *fs, const char *sql) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(fs->db, sql, -1, &st, NULL) != SQLITE_OK) { sqlerr(fs, "prepare"); return NULL; }
    return st;
}

/* steps to completion and finalizes; true when the statement was DONE */
static bool finish(FSettings *fs, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    bool ok = rc == SQLITE_DONE;
    if (!ok) sqlerr(fs, "step");
    sqlite3_finalize(st);
    return ok;
}

static bool exec(FSettings *fs, const char *sql) {
    char *m = NULL;
    if (sqlite3_exec(fs->db, sql, NULL, NULL, &m) != SQLITE_OK) {
        seterr(fs, "sqlite: %s", m ? m : "error");
        sqlite3_free(m);
        return false;
    }
    return true;
}

static void txt(sqlite3_stmt *st, int i, const char *s) { sqlite3_bind_text(st, i, s, -1, SQLITE_TRANSIENT); }

static int query_int(FSettings *fs, sqlite3_stmt *st, int dflt) {   /* first column of the first row; finalizes */
    int v = dflt;
    if (st && sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    if (st) sqlite3_finalize(st);
    (void)fs;
    return v;
}

static int layer_count(FSettings *fs, const char *hash) {
    sqlite3_stmt *st = prep(fs, "SELECT count(*) FROM layer WHERE font=?");
    if (!st) return -1;
    txt(st, 1, hash);
    return query_int(fs, st, -1);
}

/* ------------------------------------------------------------------ files */

enum { PROBE_OK, PROBE_MISSING, PROBE_BAD };

/* Opens `path` read-only and checks that it is a settings database of a known schema. */
static int probe(const char *path, sqlite3 **out, char err[FS_ERR_LEN]) {
    *out = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return PROBE_MISSING;
    fclose(f);
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        snprintf(err, FS_ERR_LEN, "%s: cannot open: %s", path, db ? sqlite3_errmsg(db) : "?");
        if (db) sqlite3_close(db);
        return PROBE_BAD;
    }
    sqlite3_stmt *st = NULL;
    int ver = -1;
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='format'", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW && strcmp((const char *)sqlite3_column_text(st, 0), "pxfe-settings") == 0) ver = 0;
        sqlite3_finalize(st);
        st = NULL;
    }
    if (ver == 0 && sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key='schema_version'", -1, &st, NULL) == SQLITE_OK) {
        ver = sqlite3_step(st) == SQLITE_ROW ? atoi((const char *)sqlite3_column_text(st, 0)) : -1;
        sqlite3_finalize(st);
    } else ver = -1;
    if (ver < 1) {
        snprintf(err, FS_ERR_LEN, "%s: not a settings database", path);
        sqlite3_close(db);
        return PROBE_BAD;
    }
    if (ver > FS_SCHEMA_VERSION) {
        snprintf(err, FS_ERR_LEN, "%s: the settings file has schema version %d, this program understands up to %d", path, ver, FS_SCHEMA_VERSION);
        sqlite3_close(db);
        return PROBE_BAD;
    }
    *out = db;
    return PROBE_OK;
}

static FSettings *fs_alloc(void) {
    FSettings *fs = (FSettings *)calloc(1, sizeof *fs);
    if (!fs) return NULL;
    if (sqlite3_open_v2(":memory:", &fs->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        if (fs->db) sqlite3_close(fs->db);
        free(fs);
        return NULL;
    }
    return fs;
}

FSettings *fs_new_empty(void) {
    FSettings *fs = fs_alloc();
    if (!fs) return NULL;
    if (!exec(fs, SCHEMA)) { fs_close(fs); return NULL; }
    return fs;
}

void fs_close(FSettings *fs) {
    if (!fs) return;
    sqlite3_close(fs->db);
    free(fs);
}

FSettings *fs_open(const char *path, char err[FS_ERR_LEN]) {
    char msg[FS_ERR_LEN] = "";
    if (err) err[0] = 0;
    if (!path || !*path || strlen(path) >= FS_PATH_LEN) { if (err) snprintf(err, FS_ERR_LEN, "bad path"); return NULL; }
    sqlite3 *src = NULL;
    int pr = probe(path, &src, msg);
    if (pr == PROBE_BAD) { if (err) snprintf(err, FS_ERR_LEN, "%s", msg); return NULL; }
    FSettings *fs = pr == PROBE_OK ? fs_alloc() : fs_new_empty();
    if (!fs) { if (src) sqlite3_close(src); if (err) snprintf(err, FS_ERR_LEN, "out of memory"); return NULL; }
    if (pr == PROBE_OK) {
        sqlite3_backup *b = sqlite3_backup_init(fs->db, "main", src, "main");
        int rc = b ? sqlite3_backup_step(b, -1) : SQLITE_ERROR;
        if (b) sqlite3_backup_finish(b);
        sqlite3_close(src);
        if (rc != SQLITE_DONE) {
            if (err) snprintf(err, FS_ERR_LEN, "%s: cannot read the settings file (sqlite %d)", path, rc);
            fs_close(fs);
            return NULL;
        }
    }
    if (pr == PROBE_OK && query_int(fs, prep(fs, "SELECT value FROM meta WHERE key='schema_version'"), 0) < 2) {
        /* version 1 -> 2: the placement of an imported font (DESIGN.md "Importing a font file"); the file itself changes at the next save */
        if (!exec(fs, "ALTER TABLE layer ADD COLUMN em REAL NOT NULL DEFAULT 0;"
                      "ALTER TABLE layer ADD COLUMN aspect REAL NOT NULL DEFAULT 1;"
                      "ALTER TABLE layer ADD COLUMN follow INTEGER NOT NULL DEFAULT 0;"
                      "ALTER TABLE layer ADD COLUMN mode INTEGER NOT NULL DEFAULT 0;"
                      "ALTER TABLE layer ADD COLUMN threshold REAL NOT NULL DEFAULT 0.5;"
                      "UPDATE meta SET value='2' WHERE key='schema_version';")) {
            if (err) snprintf(err, FS_ERR_LEN, "%.100s: cannot upgrade the settings: %.100s", path, fs->err);
            fs_close(fs);
            return NULL;
        }
    }
    if (pr == PROBE_OK && query_int(fs, prep(fs, "SELECT value FROM meta WHERE key='schema_version'"), 0) < 3) {
        /* version 2 -> 3: the lock of fonts and typefaces (DESIGN.md "Locks") */
        if (!exec(fs, "CREATE TABLE IF NOT EXISTS unlocked (kind INTEGER NOT NULL, hash TEXT NOT NULL, PRIMARY KEY (kind, hash)) WITHOUT ROWID;"
                      "UPDATE meta SET value='3' WHERE key='schema_version';")) {
            if (err) snprintf(err, FS_ERR_LEN, "%.100s: cannot upgrade the settings: %.100s", path, fs->err);
            fs_close(fs);
            return NULL;
        }
    }
    snprintf(fs->path, sizeof fs->path, "%s", path);
    return fs;
}

bool fs_has_any(const FSettings *fs) {
    FSettings *m = (FSettings *)fs;
    return query_int(m, prep(m, "SELECT EXISTS (SELECT 1 FROM layer) OR EXISTS (SELECT 1 FROM unlocked)"), 0) != 0;
}

/* VACUUM INTO path.tmp, then rename over path */
static bool write_file(FSettings *fs, const char *path) {
    char tmp[FS_PATH_LEN + 8], sql[2 * FS_PATH_LEN + 40];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    remove(tmp);
    size_t o = (size_t)snprintf(sql, sizeof sql, "VACUUM INTO '");
    for (const char *p = tmp; *p && o + 4 < sizeof sql; p++) { if (*p == '\'') sql[o++] = '\''; sql[o++] = *p; }
    sql[o++] = '\''; sql[o] = 0;
    if (!exec(fs, sql)) { remove(tmp); return false; }
    if (rename(tmp, path) != 0) {
        /* a rename onto an existing file fails on some systems: remove it, then retry (not atomic there) */
        remove(path);
        if (rename(tmp, path) != 0) { remove(tmp); return seterr(fs, "%s: cannot replace the file; the old file is unchanged or removed", path); }
    }
    return true;
}

static bool save_to(FSettings *fs, const char *path) {
    if (fs_has_any(fs)) return write_file(fs, path);
    /* the empty policy: write nothing, delete a settings file that is there */
    sqlite3 *old = NULL;
    char msg[FS_ERR_LEN];
    if (probe(path, &old, msg) == PROBE_OK) {
        sqlite3_close(old);
        if (remove(path) != 0) return seterr(fs, "%s: cannot delete the empty settings file", path);
    }
    return true;
}

bool fs_save(FSettings *fs) {
    if (!fs->path[0]) return seterr(fs, "no settings file is chosen: use save as");
    unsigned g = fs->gen;
    if (!save_to(fs, fs->path)) return false;
    fs->savedGen = g;
    return true;
}

bool fs_save_as(FSettings *fs, const char *newPath) {
    if (!newPath || !*newPath || strlen(newPath) >= FS_PATH_LEN) return seterr(fs, "bad path");
    unsigned g = fs->gen;
    if (!save_to(fs, newPath)) return false;
    snprintf(fs->path, sizeof fs->path, "%s", newPath);
    fs->savedGen = g;
    return true;
}

/* ------------------------------------------------------------------ layers */

static void read_layer(sqlite3_stmt *st, FsLayer *l) {
    const char *t;
    memset(l, 0, sizeof *l);
    l->id = sqlite3_column_int(st, 0);
    t = (const char *)sqlite3_column_text(st, 1); snprintf(l->font, sizeof l->font, "%s", t ? t : "");
    l->position = sqlite3_column_int(st, 2);
    l->kind = sqlite3_column_int(st, 3);
    t = (const char *)sqlite3_column_text(st, 4); snprintf(l->source, sizeof l->source, "%s", t ? t : "");
    l->face = sqlite3_column_int(st, 5);
    l->fileSize = sqlite3_column_int64(st, 6);
    l->fileMtime = sqlite3_column_int64(st, 7);
    l->scale = (float)sqlite3_column_double(st, 8);
    l->dx = (float)sqlite3_column_double(st, 9);
    l->dy = (float)sqlite3_column_double(st, 10);
    l->r = sqlite3_column_int(st, 11);
    l->g = sqlite3_column_int(st, 12);
    l->b = sqlite3_column_int(st, 13);
    l->opacity = sqlite3_column_int(st, 14);
    l->visible = sqlite3_column_int(st, 15);
    t = (const char *)sqlite3_column_text(st, 16); snprintf(l->label, sizeof l->label, "%s", t ? t : "");
    l->em = (float)sqlite3_column_double(st, 17);
    l->aspect = (float)sqlite3_column_double(st, 18);
    l->follow = sqlite3_column_int(st, 19);
    l->mode = sqlite3_column_int(st, 20);
    l->threshold = (float)sqlite3_column_double(st, 21);
}

#define LAYER_COLS "id,font,pos,kind,source,face,file_size,file_mtime,scale,dx,dy,r,g,b,opacity,visible,label,em,aspect,follow,mode,threshold"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool check_layer(FSettings *fs, const FsLayer *l) {
    if (l->kind != 0 && l->kind != 1) return seterr(fs, "layer kind must be 0 or 1");
    if (!(l->scale > 0.0f)) return seterr(fs, "layer scale must be > 0");
    if (!l->source[0]) return seterr(fs, "layer source is empty");
    return true;
}

static bool bind_layer_values(sqlite3_stmt *st, int p, const FsLayer *l) {   /* kind..threshold, 19 values from index p */
    sqlite3_bind_int(st, p++, l->kind);
    txt(st, p++, l->source);
    sqlite3_bind_int(st, p++, l->face);
    sqlite3_bind_int64(st, p++, l->fileSize);
    sqlite3_bind_int64(st, p++, l->fileMtime);
    sqlite3_bind_double(st, p++, l->scale);
    sqlite3_bind_double(st, p++, l->dx);
    sqlite3_bind_double(st, p++, l->dy);
    sqlite3_bind_int(st, p++, clampi(l->r, 0, 255));
    sqlite3_bind_int(st, p++, clampi(l->g, 0, 255));
    sqlite3_bind_int(st, p++, clampi(l->b, 0, 255));
    sqlite3_bind_int(st, p++, clampi(l->opacity, 0, 100));
    sqlite3_bind_int(st, p++, l->visible ? 1 : 0);
    txt(st, p++, l->label);
    sqlite3_bind_double(st, p++, l->em > 0 ? l->em : 0);
    sqlite3_bind_double(st, p++, l->aspect > 0 ? l->aspect : 1);
    sqlite3_bind_int(st, p++, l->follow ? 1 : 0);
    sqlite3_bind_int(st, p++, l->mode ? 1 : 0);
    sqlite3_bind_double(st, p++, l->threshold > 0 ? l->threshold : 0.5);
    return true;
}

int fs_list_layers(FSettings *fs, const char *hash, FsLayer *out, int max) {
    if (!valid_hash(hash)) return 0;
    sqlite3_stmt *st = prep(fs, "SELECT " LAYER_COLS " FROM layer WHERE font=? ORDER BY pos");
    if (!st) return -1;
    txt(st, 1, hash);
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (out && n < max) read_layer(st, &out[n]);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

bool fs_get_layer(FSettings *fs, int id, FsLayer *out) {
    sqlite3_stmt *st = prep(fs, "SELECT " LAYER_COLS " FROM layer WHERE id=?");
    if (!st) return false;
    sqlite3_bind_int(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_ROW;
    if (ok) { if (out) read_layer(st, out); } else seterr(fs, "no layer %d", id);
    sqlite3_finalize(st);
    return ok;
}

static bool shift(FSettings *fs, const char *hash, int from, int to, int delta) {   /* pos in [from, to] += delta */
    sqlite3_stmt *st = prep(fs, "UPDATE layer SET pos=pos+? WHERE font=? AND pos>=? AND pos<=?");
    if (!st) return false;
    sqlite3_bind_int(st, 1, delta);
    txt(st, 2, hash);
    sqlite3_bind_int(st, 3, from);
    sqlite3_bind_int(st, 4, to);
    return finish(fs, st);
}

#define BIG 2000000000

int fs_add_layer(FSettings *fs, const char *hash, const FsLayer *init) {
    if (!valid_hash(hash)) { seterr(fs, "bad font hash"); return -1; }
    if (!check_layer(fs, init)) return -1;
    if (!exec(fs, "BEGIN")) return -1;
    int id = -1;
    int n = layer_count(fs, hash);
    if (n < 0) goto fail;
    int pos = init->position;
    if (pos < 0 || pos > n) pos = n;
    if (!shift(fs, hash, pos, BIG, 1)) goto fail;
    sqlite3_stmt *st = prep(fs, "INSERT INTO layer (font,pos,kind,source,face,file_size,file_mtime,scale,dx,dy,r,g,b,opacity,visible,label,"
                                "em,aspect,follow,mode,threshold) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (!st) goto fail;
    txt(st, 1, hash);
    sqlite3_bind_int(st, 2, pos);
    bind_layer_values(st, 3, init);
    if (!finish(fs, st)) goto fail;
    id = (int)sqlite3_last_insert_rowid(fs->db);
    if (!exec(fs, "COMMIT")) goto fail;
    fs->gen++;
    return id;
fail:
    sqlite3_exec(fs->db, "ROLLBACK", NULL, NULL, NULL);
    return -1;
}

bool fs_update_layer(FSettings *fs, const FsLayer *l) {
    if (!check_layer(fs, l)) return false;
    sqlite3_stmt *st = prep(fs, "UPDATE layer SET kind=?,source=?,face=?,file_size=?,file_mtime=?,scale=?,dx=?,dy=?,r=?,g=?,b=?,"
                                "opacity=?,visible=?,label=?,em=?,aspect=?,follow=?,mode=?,threshold=? WHERE id=?");
    if (!st) return false;
    bind_layer_values(st, 1, l);
    sqlite3_bind_int(st, 20, l->id);
    if (!finish(fs, st)) return false;
    if (sqlite3_changes(fs->db) != 1) return seterr(fs, "no layer %d", l->id);
    fs->gen++;
    return true;
}

bool fs_remove_layer(FSettings *fs, int id) {
    FsLayer l;
    if (!fs_get_layer(fs, id, &l)) return false;
    if (!exec(fs, "BEGIN")) return false;
    sqlite3_stmt *st = prep(fs, "DELETE FROM correction WHERE layer=?");
    if (!st) goto fail;
    sqlite3_bind_int(st, 1, id);
    if (!finish(fs, st)) goto fail;
    st = prep(fs, "DELETE FROM layer WHERE id=?");
    if (!st) goto fail;
    sqlite3_bind_int(st, 1, id);
    if (!finish(fs, st)) goto fail;
    if (!shift(fs, l.font, l.position + 1, BIG, -1)) goto fail;
    if (!exec(fs, "COMMIT")) goto fail;
    fs->gen++;
    return true;
fail:
    sqlite3_exec(fs->db, "ROLLBACK", NULL, NULL, NULL);
    return false;
}

bool fs_move_layer(FSettings *fs, int id, int newPosition) {
    FsLayer l;
    if (!fs_get_layer(fs, id, &l)) return false;
    int n = layer_count(fs, l.font);
    if (n < 0) return false;
    int to = clampi(newPosition, 0, n - 1);
    if (to == l.position) return true;
    if (!exec(fs, "BEGIN")) return false;
    if (to > l.position ? !shift(fs, l.font, l.position + 1, to, -1) : !shift(fs, l.font, to, l.position - 1, 1)) goto fail;
    sqlite3_stmt *st = prep(fs, "UPDATE layer SET pos=? WHERE id=?");
    if (!st) goto fail;
    sqlite3_bind_int(st, 1, to);
    sqlite3_bind_int(st, 2, id);
    if (!finish(fs, st)) goto fail;
    if (!exec(fs, "COMMIT")) goto fail;
    fs->gen++;
    return true;
fail:
    sqlite3_exec(fs->db, "ROLLBACK", NULL, NULL, NULL);
    return false;
}

/* ------------------------------------------------------------------ fonts */

bool fs_has_font(const FSettings *fs, const char *hash) {
    FSettings *m = (FSettings *)fs;
    if (!valid_hash(hash)) return false;
    return layer_count(m, hash) > 0;
}

static bool remove_font_rows(FSettings *fs, const char *hash) {
    sqlite3_stmt *st = prep(fs, "DELETE FROM correction WHERE layer IN (SELECT id FROM layer WHERE font=?)");
    if (!st) return false;
    txt(st, 1, hash);
    if (!finish(fs, st)) return false;
    st = prep(fs, "DELETE FROM layer WHERE font=?");
    if (!st) return false;
    txt(st, 1, hash);
    return finish(fs, st);
}

bool fs_remove_font(FSettings *fs, const char *hash) {
    if (!valid_hash(hash)) return seterr(fs, "bad font hash");
    if (!fs_has_font(fs, hash)) return true;
    if (!exec(fs, "BEGIN")) return false;
    if (!remove_font_rows(fs, hash) || !exec(fs, "COMMIT")) { sqlite3_exec(fs->db, "ROLLBACK", NULL, NULL, NULL); return false; }
    fs->gen++;
    return true;
}

bool fs_copy_font(FSettings *fs, const char *fromHash, const char *toHash) {
    if (!valid_hash(fromHash) || !valid_hash(toHash)) return seterr(fs, "bad font hash");
    if (strcmp(fromHash, toHash) == 0) return seterr(fs, "cannot copy a font onto itself");
    int n = fs_list_layers(fs, fromHash, NULL, 0);
    if (n < 0) return false;
    int had = layer_count(fs, toHash);
    if (n == 0 && had <= 0) return true;
    int *ids = (int *)malloc((size_t)(n > 0 ? n : 1) * sizeof *ids);
    if (!ids) return seterr(fs, "out of memory");
    bool ok = false;
    if (!exec(fs, "BEGIN")) { free(ids); return false; }
    sqlite3_stmt *st = prep(fs, "SELECT id FROM layer WHERE font=? ORDER BY pos");
    if (!st) goto fail;
    txt(st, 1, fromHash);
    int k = 0;
    while (k < n && sqlite3_step(st) == SQLITE_ROW) ids[k++] = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (!remove_font_rows(fs, toHash)) goto fail;
    for (int i = 0; i < k; i++) {
        st = prep(fs, "INSERT INTO layer (font,pos,kind,source,face,file_size,file_mtime,scale,dx,dy,r,g,b,opacity,visible,label,em,aspect,follow,mode,threshold)"
                      " SELECT ?,pos,kind,source,face,file_size,file_mtime,scale,dx,dy,r,g,b,opacity,visible,label,em,aspect,follow,mode,threshold FROM layer WHERE id=?");
        if (!st) goto fail;
        txt(st, 1, toHash);
        sqlite3_bind_int(st, 2, ids[i]);
        if (!finish(fs, st)) goto fail;
        sqlite3_int64 nid = sqlite3_last_insert_rowid(fs->db);
        st = prep(fs, "INSERT INTO correction SELECT ?,?,code,dx,dy,dsize,hidden FROM correction WHERE layer=?");
        if (!st) goto fail;
        txt(st, 1, toHash);
        sqlite3_bind_int64(st, 2, nid);
        sqlite3_bind_int(st, 3, ids[i]);
        if (!finish(fs, st)) goto fail;
    }
    if (!exec(fs, "COMMIT")) goto fail;
    fs->gen++;
    ok = true;
    free(ids);
    return ok;
fail:
    sqlite3_exec(fs->db, "ROLLBACK", NULL, NULL, NULL);
    free(ids);
    return false;
}

/* ------------------------------------------------------------------ locks and renames */

bool fs_is_unlocked(FSettings *fs, int kind, const char *hash) {
    sqlite3_stmt *st = prep(fs, "SELECT 1 FROM unlocked WHERE kind=? AND hash=?");
    if (!st) return false;
    sqlite3_bind_int(st, 1, kind);
    txt(st, 2, hash);
    bool r = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return r;
}

bool fs_set_unlocked(FSettings *fs, int kind, const char *hash, bool unlocked) {
    if (!valid_hash(hash)) return seterr(fs, "bad hash");
    if (fs_is_unlocked(fs, kind, hash) == unlocked) return true;
    sqlite3_stmt *st = prep(fs, unlocked ? "INSERT INTO unlocked VALUES (?, ?)" : "DELETE FROM unlocked WHERE kind=? AND hash=?");
    if (!st) return false;
    sqlite3_bind_int(st, 1, kind);
    txt(st, 2, hash);
    if (!finish(fs, st)) return false;
    fs->gen++;
    return true;
}

static bool rename_rows(FSettings *fs, const char *sql, const char *a, const char *b) {
    sqlite3_stmt *st = prep(fs, sql);
    if (!st) return false;
    txt(st, 1, a);
    txt(st, 2, b);
    return finish(fs, st);
}

bool fs_rename_font(FSettings *fs, const char *from, const char *to) {
    if (!valid_hash(from) || !valid_hash(to)) return seterr(fs, "bad font hash");
    if (strcmp(from, to) == 0) return true;
    if (!exec(fs, "BEGIN")) return false;
    /* what the settings hold under `to` is replaced only by what they hold under `from` (the open font syncs its layers under its new
     * hash before it is committed, and there is nothing under `from` then) */
    bool ok = (!fs_has_font(fs, from) || remove_font_rows(fs, to)) &&
        rename_rows(fs, "UPDATE layer SET font=?1 WHERE font=?2", to, from) &&
        rename_rows(fs, "UPDATE correction SET font=?1 WHERE font=?2", to, from) &&
        rename_rows(fs, "UPDATE layer SET source=?1 WHERE kind=0 AND source=?2", to, from) &&
        rename_rows(fs, "DELETE FROM unlocked WHERE kind=0 AND hash=?1", to, from) &&
        rename_rows(fs, "UPDATE unlocked SET hash=?1 WHERE kind=0 AND hash=?2", to, from) &&
        exec(fs, "COMMIT");
    if (!ok) { sqlite3_exec(fs->db, "ROLLBACK", NULL, NULL, NULL); return false; }
    fs->gen++;
    return true;
}

bool fs_rename_typeface(FSettings *fs, const char *from, const char *to) {
    if (!valid_hash(from) || !valid_hash(to)) return seterr(fs, "bad typeface hash");
    if (strcmp(from, to) == 0) return true;
    if (!fs_is_unlocked(fs, 1, from)) return true;
    if (!exec(fs, "BEGIN")) return false;
    bool ok = rename_rows(fs, "DELETE FROM unlocked WHERE kind=1 AND hash=?1", to, from) &&
        rename_rows(fs, "UPDATE unlocked SET hash=?1 WHERE kind=1 AND hash=?2", to, from) && exec(fs, "COMMIT");
    if (!ok) { sqlite3_exec(fs->db, "ROLLBACK", NULL, NULL, NULL); return false; }
    fs->gen++;
    return true;
}

/* ------------------------------------------------------------------ corrections */

bool fs_get_correction(FSettings *fs, int layerId, int32_t code, FsCorrection *out) {
    sqlite3_stmt *st = prep(fs, "SELECT dx,dy,dsize,hidden FROM correction WHERE layer=? AND code=?");
    if (!st) return false;
    sqlite3_bind_int(st, 1, layerId);
    sqlite3_bind_int(st, 2, code);
    bool found = sqlite3_step(st) == SQLITE_ROW;
    if (found && out) {
        out->dx = (float)sqlite3_column_double(st, 0);
        out->dy = (float)sqlite3_column_double(st, 1);
        out->dsize = (float)sqlite3_column_double(st, 2);
        out->hidden = sqlite3_column_int(st, 3);
    }
    sqlite3_finalize(st);
    return found;
}

bool fs_set_correction(FSettings *fs, int layerId, int32_t code, const FsCorrection *c) {
    sqlite3_stmt *st;
    if (is_default(c)) {
        st = prep(fs, "DELETE FROM correction WHERE layer=? AND code=?");
        if (!st) return false;
        sqlite3_bind_int(st, 1, layerId);
        sqlite3_bind_int(st, 2, code);
        if (!finish(fs, st)) return false;
        if (sqlite3_changes(fs->db) > 0) fs->gen++;
        return true;
    }
    if (!(c->dsize > 0.0f)) return seterr(fs, "dsize must be > 0");
    st = prep(fs, "INSERT OR REPLACE INTO correction SELECT font,id,?,?,?,?,? FROM layer WHERE id=?");
    if (!st) return false;
    sqlite3_bind_int(st, 1, code);
    sqlite3_bind_double(st, 2, c->dx);
    sqlite3_bind_double(st, 3, c->dy);
    sqlite3_bind_double(st, 4, c->dsize);
    sqlite3_bind_int(st, 5, c->hidden ? 1 : 0);
    sqlite3_bind_int(st, 6, layerId);
    if (!finish(fs, st)) return false;
    if (sqlite3_changes(fs->db) != 1) return seterr(fs, "no layer %d", layerId);
    fs->gen++;
    return true;
}

int fs_list_corrections(FSettings *fs, int layerId, int32_t *codes, int max) {
    sqlite3_stmt *st = prep(fs, "SELECT code FROM correction WHERE layer=? ORDER BY code");
    if (!st) return -1;
    sqlite3_bind_int(st, 1, layerId);
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (codes && n < max) codes[n] = (int32_t)sqlite3_column_int(st, 0);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

bool fs_clear_corrections(FSettings *fs, int layerId) {
    sqlite3_stmt *st = prep(fs, "DELETE FROM correction WHERE layer=?");
    if (!st) return false;
    sqlite3_bind_int(st, 1, layerId);
    if (!finish(fs, st)) return false;
    if (sqlite3_changes(fs->db) > 0) fs->gen++;
    return true;
}
