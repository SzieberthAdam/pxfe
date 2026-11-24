/* fsettings.h - the settings sidecar: per-font program settings kept beside a .pxfdb (not in it).
 *
 * File: <dbPath>-settings (lib.pxfdb -> lib.pxfdb-settings), see fs_sidecar_path.  It is a SQLite database.
 * Like store.c the whole file is loaded into an in-memory database, edited there, and written back by fs_save
 * (VACUUM INTO <path>.tmp, then rename over the old file: the old file stays intact when anything fails).
 *
 * Key of everything: the font's pxfm hash, 32 lowercase hex characters.  Layer ids are unique in the file and never reused
 * (AUTOINCREMENT), a layer's corrections belong to its id.
 *
 * Schema, schema_version = 5 (an older file is upgraded in memory when it is opened, and on disk at the next save):
 *   meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)              'format' = 'pxfe-settings', 'schema_version' = '1'
 *   layer(id INTEGER PRIMARY KEY AUTOINCREMENT,
 *         font TEXT NOT NULL,                  pxfm hash of the edited font
 *         pos INTEGER NOT NULL,                0 = top of the stack; contiguous 0..n-1 per font
 *         kind INTEGER NOT NULL,               0 = database font (source = its pxfm hash), 1 = file (source = path, UTF-8)
 *         source TEXT NOT NULL, face INTEGER NOT NULL,        .ttc face / bitmap strike / .fon font index
 *         file_size INTEGER NOT NULL, file_mtime INTEGER NOT NULL,   to detect a changed file; 0 for kind 0
 *         scale REAL NOT NULL,                 1.0 = natural size (outline: the edited font's pixel size; bitmap: the strike size)
 *         dx REAL NOT NULL, dy REAL NOT NULL,  pixels of the edited font, y up
 *         r, g, b INTEGER NOT NULL, opacity INTEGER NOT NULL (0..100), visible INTEGER NOT NULL (0/1),
 *         label TEXT NOT NULL,
 *         em REAL NOT NULL DEFAULT 0,          version 2.  > 0: the size of an outline in pixels per em, whatever the font box is
 *                                              (then `scale` is not used); 0: the size is the font box times `scale`
 *         aspect REAL NOT NULL DEFAULT 1,      width of an outline: pixels per em across = size * aspect
 *         follow INTEGER NOT NULL DEFAULT 0,   1: the glyphs of the font are DRAWN from this layer (fit.h) while they are not edited by hand
 *         mode INTEGER NOT NULL DEFAULT 0,     how they are drawn: 0 smooth (coverage >= threshold), 1 pixel font (the centre of a pixel)
 *         threshold REAL NOT NULL DEFAULT 0.5)
 *   unlocked(kind INTEGER NOT NULL, hash TEXT NOT NULL, PRIMARY KEY (kind, hash))     version 3.  The fonts (kind 0, pxfm hash) and
 *            typefaces (kind 1, pxtf hash) the user unlocked for editing; every other one is locked.  The rows follow the hash when
 *            editing changes it (fs_rename_font, fs_rename_typeface).
 *   correction(font TEXT NOT NULL, layer INTEGER NOT NULL, code INTEGER NOT NULL (a glyph code, pxfont.h CODES; version 4: of pxfm version 3), dx REAL, dy REAL,
 *              dsize REAL NOT NULL,              factor multiplied onto the layer's scale, 1.0 = none
 *              hidden INTEGER NOT NULL, PRIMARY KEY (layer, code))
 *   A correction row exists only if it differs from the defaults (dx = dy = 0, dsize = 1, hidden = 0).
 *   repo_link(id INTEGER PRIMARY KEY CHECK (id = 1), folder TEXT NOT NULL)     version 5.  The linked library folder (DESIGN.md
 *            "A linked library folder"), one row or none.
 *   repo_base(kind INTEGER PRIMARY KEY, hashes BLOB NOT NULL)     version 5.  The baseline: per kind (store.h STORE_FILE_*), the
 *            16-byte hashes of the files the folder and the database both held at the last sync, sorted, concatenated.
 *
 * Empty policy: fs_save on a handle with no layers writes NOTHING and deletes an existing sidecar at that path (only if it is a
 * settings database), so a user without font layers never gets a stray file.  fs_save_as on an empty handle does the same at
 * the new path.  Opening a missing file gives an empty handle (nothing is created until a layer exists and fs_save is called).
 *
 * Every function that can fail returns false / -1 / NULL and leaves a message in fs_error(fs) (open: in `err`). */
#ifndef FSETTINGS_H
#define FSETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "pxfont.h"   /* PxCode */

#define FS_SCHEMA_VERSION 5
#define FS_ERR_LEN 256
#define FS_HASH_LEN 33         /* 32 hex characters + NUL */
#define FS_PATH_LEN 1100
#define FS_LABEL_LEN 128

typedef struct FSettings FSettings;

typedef struct FsLayer {
    int id;                    /* set by fs_add_layer; the key for update/remove/move */
    char font[FS_HASH_LEN];    /* the edited font's pxfm hash */
    int position;              /* 0 = top; filled by list/get; on add: the wanted position (clamped; <0 or too big = bottom) */
    int kind;                  /* 0 = database font, 1 = file */
    char source[FS_PATH_LEN];  /* pxfm hash (kind 0) or file path (kind 1) */
    int face;
    int64_t fileSize, fileMtime;
    float scale;               /* > 0, 1.0 = natural size */
    float dx, dy;
    int r, g, b;               /* 0..255 */
    int opacity;               /* 0..100 */
    int visible;               /* 0/1 */
    char label[FS_LABEL_LEN];
    float em;                  /* > 0: pixels per em of an outline (absolute); 0: the font box times scale */
    float aspect;              /* > 0, 1.0 = square */
    int follow;                /* 0/1: the font's glyphs are drawn from this layer */
    int mode;                  /* 0 smooth, 1 pixel font (FIT_* of fit.h) */
    float threshold;           /* coverage that inks a pixel in smooth mode */
} FsLayer;

typedef struct FsCorrection {
    float dx, dy;
    float dsize;               /* factor on the layer's scale, 1.0 = none */
    int hidden;
} FsCorrection;

/* a layer with the defaults: kind 0, scale 1, no offset, colour 0,0,0, opacity 100, visible, position -1 (bottom), em 0, aspect 1,
 * follow 0, mode 0, threshold 0.5 */
FsLayer fs_layer_default(void);
FsCorrection fs_correction_default(void);

/* "<dbPath>-settings" into out[n]; false when it does not fit or dbPath is empty */
bool fs_sidecar_path(const char *dbPath, char *out, int n);

/* Opens the sidecar at `path` (an existing file is loaded; a missing one gives an empty handle bound to the path).
 * NULL + err when the file is not a settings database or its schema_version is newer than FS_SCHEMA_VERSION. */
FSettings *fs_open(const char *path, char err[FS_ERR_LEN]);
FSettings *fs_new_empty(void);                 /* no file yet (path ""); fs_save needs fs_save_as first */
void fs_close(FSettings *fs);

const char *fs_error(const FSettings *fs);
const char *fs_path(const FSettings *fs);
bool fs_save(FSettings *fs);                   /* see the empty policy; fails without a path */
bool fs_save_as(FSettings *fs, const char *newPath);   /* writes there and switches the handle's path (not on failure) */
bool fs_dirty(const FSettings *fs);            /* changed since opened / last saved */
unsigned fs_changes(const FSettings *fs);      /* the change counter */

bool fs_has_any(const FSettings *fs);          /* any layer, unlocked row or linked folder */
bool fs_has_font(const FSettings *fs, const char *hash);
bool fs_remove_font(FSettings *fs, const char *hash);                    /* layers and corrections of the font */
bool fs_copy_font(FSettings *fs, const char *fromHash, const char *toHash);   /* new ids; replaces what toHash had; from == to is an error */

/* the lock (kind 0 font, 1 typeface); locked is the default, so only unlocked ones are stored */
bool fs_is_unlocked(FSettings *fs, int kind, const char *hash);
bool fs_set_unlocked(FSettings *fs, int kind, const char *hash, bool unlocked);
/* everything the font's hash keys follows it to its new hash: layers, corrections, the unlocked row, and layers of other fonts that
 * use it as their source (what `to` had is replaced).  The same for a typeface's unlocked row. */
bool fs_rename_font(FSettings *fs, const char *from, const char *to);
bool fs_rename_typeface(FSettings *fs, const char *from, const char *to);

/* the linked library folder: fs_repo_folder says whether there is one (out gets it); "" unlinks; a new folder drops the baseline */
bool fs_repo_folder(FSettings *fs, char *out, int n);
bool fs_set_repo_folder(FSettings *fs, const char *folder);
/* the baseline of one kind: a malloc'd array of 16-byte hashes in *out (free it), the count (0 when none), -1 on error */
int fs_repo_base(FSettings *fs, int kind, uint8_t (**out)[16]);
bool fs_set_repo_base(FSettings *fs, int kind, const uint8_t (*h)[16], int n);

/* layers.  list: fills out[0..max) sorted by position, returns the number of layers the font has (can be > max), -1 on error */
int fs_list_layers(FSettings *fs, const char *hash, FsLayer *out, int max);
bool fs_get_layer(FSettings *fs, int id, FsLayer *out);
int fs_add_layer(FSettings *fs, const char *hash, const FsLayer *init);   /* the new id, -1 on error; init->id/font ignored */
bool fs_update_layer(FSettings *fs, const FsLayer *l);   /* by l->id: every field except id, font, position */
bool fs_remove_layer(FSettings *fs, int id);             /* with its corrections; the positions close up */
bool fs_move_layer(FSettings *fs, int id, int newPosition);   /* clamped to the stack */

/* corrections of a layer (the layer id implies the font).  get: found?  set: all-default deletes the row. */
bool fs_get_correction(FSettings *fs, int layerId, PxCode code, FsCorrection *out);
bool fs_set_correction(FSettings *fs, int layerId, PxCode code, const FsCorrection *c);
int fs_list_corrections(FSettings *fs, int layerId, PxCode *codes, int max);   /* ascending; returns the count (can be > max), -1 on error */
bool fs_clear_corrections(FSettings *fs, int layerId);

#endif
