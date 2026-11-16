/* store.h - the library as ONE in-memory SQLite database with the pxfdb schema (DESIGN.md "The store"). No raylib.
 *
 * Spec of the schema and of the conversions: pixelglyphlib/spec/pxfdb.md.  The library is a .pxfdb file loaded into
 * this database (or imported from a text folder of pxgl/ pxfm/ pxtf/); every view reads it from here.
 *
 * HASHES in this API are 32 LOWER-case hex digits (char[33]), as everywhere in the core; the database keeps them as
 * 16-byte blobs.
 *
 * THREADING RULES
 *   - A Store is shared by all threads.  The database is an in-memory shared-cache database; every thread that
 *     queries it uses its OWN StoreConn (store_conn_open, one per thread, cheap; the UI thread may use
 *     store_main_conn).  A StoreConn must not be used by two threads at once.
 *   - Queries take the store's reader lock for the length of one call; saving a font (store_save_to_memory) and the similarity
 *     cache update take the writer lock, so a query never sees a half-saved font and never gets SQLITE_LOCKED.
 *   - Every result (lists, fonts, typefaces) is the caller's own copy: free it with its *_free function.
 *   - store_close() needs every StoreConn closed first.
 *   - Progress / cancel (StoreProgress) are plain volatile longs, written by the worker and read by the UI thread.
 *   - Paths are in the ANSI code page of the process, like the rest of the core.  Windows only.
 *
 * LOAD, SAVE, IMPORT, EXPORT (DESIGN.md "Version 5").  The store is a database file loaded into memory (store_open_pxfdb);
 * everything is changed in memory; store_save writes it back (to a temporary file beside the file, then an atomic replace).
 * The text folders pxtf/ pxfm/ pxgl/ are reached only by store_import_folder and store_export_folder.  The store keeps a
 * change counter: store_modified says whether memory differs from the file as loaded or last written.
 */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pxfont.h"
#include "pxtf.h"

struct sqlite3;

typedef struct Store Store;
typedef struct StoreConn StoreConn;

typedef struct StoreCounts { int fonts, typefaces, glyphs; } StoreCounts;

typedef struct StoreProgress {
    volatile long phase;        /* 0 idle, 1 listing, 3 reading files, 4 inserting, 5 indexing, 7 loading the pxfdb, 8 writing the database,
                                   9 exporting: backup, 10 exporting: deleting, 11 exporting: writing files */
    volatile long done, total;  /* of the current phase */
    volatile long cancel;       /* set to 1 by the UI thread to stop a build */
} StoreProgress;

typedef struct StoreInfo {
    char path[1100];            /* the database file the store was loaded from / is saved to */
    int nGlyph, nPxfm, nPxtf;   /* rows */
    double openSeconds;         /* total time of the open or import call */
    double buildSeconds;        /* time of reading the files (0 when loaded) */
    bool imported;              /* made by store_import_folder */
} StoreInfo;

/* ------------------------------------------------------------------ load, import, save */

/* Load: checks `meta` (format PXFDB, format_version 1) and copies the file into memory (sqlite3_backup).  prog may be NULL.
 * Blocking: call it from a worker thread and watch prog. */
bool store_open_pxfdb(const char *path, StoreProgress *prog, Store **out, char err[PX_ERR_LEN]);
/* Import: reads every file of <folder>/pxgl, pxfm and pxtf with the strict readers, builds the database in memory, writes it
 * to `pxfdbPath` (temporary file, then replace) and returns it as the loaded store.  A file that fails the strict check stops
 * the import with its name in err, and nothing is written.  Blocking, as above. */
bool store_import_folder(const char *folder, const char *pxfdbPath, StoreProgress *prog, Store **out, char err[PX_ERR_LEN]);
/* The same without keeping the store: the folder's data into the database file `pxfdbPath` (the backup of an Overwrite). */
bool store_import_to_file(const char *folder, const char *pxfdbPath, StoreProgress *prog, char err[PX_ERR_LEN]);
/* Save: writes memory to the store's file: a temporary file in the same folder (VACUUM INTO), then MoveFileEx with
 * REPLACE_EXISTING and WRITE_THROUGH.  A failure leaves the old file as it was.  Clears the modified state. */
bool store_save(Store *s, char err[PX_ERR_LEN]);
/* Save As: writes to a new file and continues on it (path and modified state). */
bool store_save_as(Store *s, const char *path, char err[PX_ERR_LEN]);
/* A copy of memory in another file (the store's path and modified state do not change). */
bool store_write_pxfdb(Store *s, const char *path, char err[PX_ERR_LEN]);
/* True when memory differs from the file as loaded or last written: some change was made since (undoing back does not count). */
bool store_modified(const Store *s);
void store_close(Store *s);
const StoreInfo *store_info(const Store *s);

StoreConn *store_conn_open(Store *s);
void store_conn_close(StoreConn *c);
StoreConn *store_main_conn(Store *s);       /* owned by the store: the opening thread's */
Store *store_of(StoreConn *c);
struct sqlite3 *store_sqlite(StoreConn *c); /* the raw handle, for tests and ad-hoc SQL; take no locks, mind the threading rules */

/* ------------------------------------------------------------------ lists for the browser */

/* One row per typeface VARIANT (the browser's "Containers") or per pxfm file ("Fonts", with the metadata of the
 * first variant, by typeface hash and number, that names it).  Strings point into the list's own arena. */
typedef struct StoreEntry {
    char pxfm[33];
    char pxtf[33];              /* "" when no variant names the font */
    int  variant;               /* 0 when none */
    const char *family, *style, *fullname, *designer, *format;   /* never NULL, "" when not stated */
    bool italic, struckout, underlined;
    bool hasAscent, hasDescent; int ascent, descent;
    bool hasHeight; int height;             /* ascent + descent when both are stated */
    bool monospaced;
    int  colors;                             /* ink colours: palette entries other than ".", at least 1 */
    bool hasRatio; int ratioA, ratioB;
    int  glyphCount;                         /* glyph lines of the font without the FFFFFF line */
    bool hasDefault;                         /* has a FFFFFF line */
    int  nVariants;                          /* typefaces' variants naming this font (fonts list) */
} StoreEntry;

typedef struct StoreEntryList {
    StoreEntry *e; int count;
    void *arena;                /* internal: owns the strings */
} StoreEntryList;

bool store_list_typefaces(StoreConn *c, StoreEntryList *out, char err[PX_ERR_LEN]);   /* per variant, by pxtf hash, number */
bool store_list_fonts(StoreConn *c, StoreEntryList *out, char err[PX_ERR_LEN]);       /* per pxfm, by hash */
void store_entry_list_free(StoreEntryList *l);

/* Metadata of one typeface (all variants) as the model. */
bool store_load_typeface(StoreConn *c, const char *pxtfHash, PxtfTypeface *out, char err[PX_ERR_LEN]);
bool store_has_pxfm(StoreConn *c, const char *pxfmHash);
bool store_has_pxtf(StoreConn *c, const char *pxtfHash);

/* ------------------------------------------------------------------ rows <-> model */

/* The font of a pxfm hash.  codes NULL: every bitmap is loaded.  Else only the glyphs whose code is in codes[0..n)
 * have bitmaps; the others stay lazy (hash only), exactly as px_read_pxfm_lines + px_load_glyphs(cps) leave them.
 * out->fileHash is set.  false + err when the font is not in the store. */
bool store_load_font(StoreConn *c, const char *pxfmHash, const int32_t *codes, int ncodes, PxFont *out, char err[PX_ERR_LEN]);
/* Loads the bitmaps of the lazy glyphs of f whose code is in codes (NULL: all lazy ones). */
bool store_load_glyph_bitmaps(StoreConn *c, PxFont *f, const int32_t *codes, int ncodes, char err[PX_ERR_LEN]);
/* The text of the file made from the ROWS, as pxfdb.md "Converting the database into the files" says (an
 * implementation independent of PxFont: the test compares it with the files).  malloc'd; len; hash lower-case. */
char *store_pxfm_text(StoreConn *c, const char *pxfmHash, size_t *len, char hashOut[33]);
char *store_pxtf_text(StoreConn *c, const char *pxtfHash, size_t *len, char hashOut[33]);
char *store_pxgl_text(StoreConn *c, const char *glyphHash, size_t *len, char hashOut[33]);

/* ------------------------------------------------------------------ saving a font into memory */

typedef struct StoreSaveResult {
    char pxfm[33];              /* hash of the saved font */
    char pxtf[33];              /* hash of the saved typeface, "" when none */
    int  newGlyphs;             /* glyph rows that were new */
    bool newPxfm, newPxtf;      /* false: it was in memory already */
} StoreSaveResult;

/* Adds the font (and the typeface, when given) to the in-memory database (a change only when something is new).  `variant` is the
 * number of the variant of `tf` that names the saved font: its PXFM line is set to the new font's hash in the saved copy (0: tf is
 * saved as it is).  Lazy glyphs must be in memory already.  Fails with nothing changed.  Never overwrites or deletes. */
bool store_save_to_memory(Store *s, const PxFont *font, const PxtfTypeface *tf, int variant, StoreSaveResult *res, char err[PX_ERR_LEN]);
/* Editing in place: the font `oldPxfm` is replaced by `font` (NULL: the font is unchanged) and the typeface `oldPxtf` by `tf` (NULL: as it is).
 * The new font is added, every typeface that names the old font is rewritten under its new hash with its variant lines following, the
 * old font goes (nothing else can name it any more: all its typefaces were rewritten) and so do the glyphs no font names now.
 * Nothing changes when the hashes stay.  Fails with nothing changed (the new font may have been added). */
typedef struct StoreEditResult {
    char pxfm[33];                       /* the font's hash now */
    char pxtf[33];                       /* the hash now of `oldPxtf`, "" when none was given */
    char movedFrom[32][33], movedTo[32][33]; int nMoved;   /* every typeface that was rewritten: old hash, new hash */
} StoreEditResult;
bool store_edit_in_place(Store *s, const char *oldPxfm, const PxFont *font, const char *oldPxtf, const PxtfTypeface *tf, StoreEditResult *out, char err[PX_ERR_LEN]);

/* ------------------------------------------------------------------ export to folders */

typedef struct StoreFolderScan {
    StoreCounts found;          /* files of pxtf/ pxfm/ pxgl/ named <hash>.<ext> under their prefix folder */
    int other;                  /* other entries in those folders */
} StoreFolderScan;
/* What the folder holds of the three library folders (a missing folder holds nothing). */
void store_scan_folder(const char *folder, StoreFolderScan *out);
bool store_scan_has_data(const StoreFolderScan *s);

typedef enum StoreExportMode {
    STORE_EXPORT_APPEND = 0,    /* write the files the folder lacks, leave the others, delete nothing */
    STORE_EXPORT_OVERWRITE = 1  /* delete the contents of pxtf/ pxfm/ pxgl/ (nothing else), then write every file */
} StoreExportMode;

typedef struct StoreExportResult {
    StoreCounts written, skipped, deleted;
} StoreExportResult;

/* Writes every typeface, font and glyph of memory to <folder>/pxtf|pxfm|pxgl/xx/<hash>.<ext> (the text made from the rows must hash to its
 * name).  prog may be NULL.  Uses its own connection: call it from any thread while the UI does not change the store. */
bool store_export_folder(Store *s, const char *folder, StoreExportMode mode, StoreProgress *prog, StoreExportResult *res, char err[PX_ERR_LEN]);

/* ------------------------------------------------------------------ deleting fonts and typefaces */

typedef struct StoreRewrite {
    char oldPxtf[33], newPxtf[33];
    int lost[64]; int nLost;     /* the variant numbers that go */
} StoreRewrite;

typedef struct StoreDeletePlan {
    char (*fonts)[33]; int nFonts;          /* pxfm rows that go */
    char (*gone)[33];  int nGone;           /* pxtf rows that go: whole typefaces, and the old rows of rewritten ones */
    int nWhole;                              /* of those, typefaces that go entirely (no variant left) */
    StoreRewrite *rw; int nRw;              /* typefaces that lose variants and are rewritten */
    PxtfTypeface *newTf;                    /* the rewritten typefaces (parallel to rw) */
    char (*glyphs)[33]; int nGlyphs;        /* pxgl rows the deletion leaves named by no pxfm */
    char summary[900];                       /* "1 font, 1 typeface, 14 glyphs; Typeface b925951b loses variant 0002 and is rewritten" */
} StoreDeletePlan;

/* The plan of deleting a font / a whole typeface: nothing changes.  The font / typeface must be in memory. */
bool store_plan_delete_font(Store *s, const char *pxfm, StoreDeletePlan *out, char err[PX_ERR_LEN]);
bool store_plan_delete_typeface(Store *s, const char *pxtf, StoreDeletePlan *out, char err[PX_ERR_LEN]);
/* Carries a plan out in memory (one transaction; a change). */
bool store_apply_delete(Store *s, const StoreDeletePlan *plan, char err[PX_ERR_LEN]);
void store_plan_free(StoreDeletePlan *p);
/* "2 fonts, 1 typeface, 14 glyphs" ("nothing" for zeros). */
void store_counts_text(const StoreCounts *c, char *out, size_t n);

/* ------------------------------------------------------------------ glyph usage and the glyph finder */

typedef struct StoreUsage {
    char pxfm[33];
    int32_t code;               /* the code of the glyph line (a code point, or an F-code) */
    char pxtf[33];              /* a typeface variant naming the font, "" when none */
    int  variant;
    const char *family, *style; /* of that variant, "" when none */
} StoreUsage;
typedef struct StoreUsageList { StoreUsage *u; int count; void *arena; } StoreUsageList;

/* The fonts and the codes a glyph (pxgl hash) is used for, one row per (font code, typeface variant naming the font;
 * one row with an empty pxtf when none does).  The FFFFFF line of the default glyph is left out when the font has
 * another line with the glyph.  `nFonts`, `nTypefaces` (may be NULL) get the number of distinct fonts and typefaces. */
bool store_glyph_usage(StoreConn *c, const char *glyphHash, StoreUsageList *out, int *nFonts, int *nTypefaces, char err[PX_ERR_LEN]);
/* The fonts that hold ALL the glyphs hashes[0..n): the browser's entry rows (font list form). */
bool store_fonts_with_glyphs(StoreConn *c, const char (*hashes)[33], int n, StoreEntryList *out, char err[PX_ERR_LEN]);
void store_usage_list_free(StoreUsageList *l);

typedef struct StoreCodeGlyph {
    char glyph[33];
    int width, height;
    int yMin, yTypical;         /* the lowest and the most common y offset this bitmap is placed at */
    int xTypical;
    int nFonts;                 /* fonts that use the bitmap for the code point */
} StoreCodeGlyph;
typedef struct StoreCodeGlyphList { StoreCodeGlyph *g; int count; } StoreCodeGlyphList;
/* The distinct bitmaps drawn for a code point (code >= 0x110000 gives none), most fonts first, then by hash. */
bool store_glyphs_for_code(StoreConn *c, int32_t code, StoreCodeGlyphList *out, char err[PX_ERR_LEN]);
void store_code_glyph_list_free(StoreCodeGlyphList *l);

/* ------------------------------------------------------------------ similarity (src/store_sim.c) */

typedef struct StoreSimilar {
    char hash[33];              /* pxfm or pxtf hash */
    double score;               /* 0..100 */
    double shape, shared, metrics;   /* the three parts, each 0..100 (shape = -1 when not used) */
    bool shapeUsed;             /* false: fewer than 10 shared reference code points, scored by the other two */
    int  sharedRef;             /* reference code points both fonts have */
    int  nFonts;                /* typefaces: fonts of it */
} StoreSimilar;

/* Builds (once) the cache of reference bitmaps of every font as bit rows.  Call from a worker thread at start-up;
 * the similarity functions call it if it has not run.  *seconds gets the time of the build when it ran here. */
bool store_similarity_prepare(Store *s, double *seconds, char err[PX_ERR_LEN]);
/* The n most similar fonts to a pxfm (the font itself left out), best first.  *count gets how many were found. */
bool store_similar_fonts(Store *s, const char *pxfmHash, int n, StoreSimilar *out, int *count, char err[PX_ERR_LEN]);
/* The n most similar typefaces to a pxtf (itself left out): the mean over its fonts of the best score against the
 * fonts of the other typeface. */
bool store_similar_typefaces(Store *s, const char *pxtfHash, int n, StoreSimilar *out, int *count, char err[PX_ERR_LEN]);
/* The score of two fonts (both must be in the store). */
bool store_font_similarity(Store *s, const char *pxfmA, const char *pxfmB, StoreSimilar *out, char err[PX_ERR_LEN]);

/* ------------------------------------------------------------------ helpers */

/* hex <-> 16-byte blob */
bool store_hex_to_blob(const char *hex, uint8_t out[16]);
void store_blob_to_hex(const uint8_t *blob, char out[33]);
#endif
