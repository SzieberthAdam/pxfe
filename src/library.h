/* library.h - the browser's font index and the threaded lazy font loader, both reading from the Store. No raylib.
 *
 * The index (Library) is made from the store's lists (store_list_typefaces, store_list_fonts) plus the drafts of a
 * drafts folder; nothing is read from files any more except the drafts.  The loader loads a font from the store's
 * rows (store_load_font) with only the bitmaps of the requested codes, on worker threads that each have their own
 * StoreConn.
 *
 * What changed against the file-scanning version (version 2): library_scan takes a Store; LIB_CONTAINER is
 * LIB_TYPEFACE (one entry per typeface VARIANT); LibEntry.pxfc is .pxtf, .container/.cvariant are .typeface/.tvariant
 * and are set for DRAFT entries only (a store entry's typeface comes from store_load_typeface(pxtf)); LibEntry.path is
 * the .pxfd path of a DRAFT only (a pxfm has no path: it is a row); libloader_create takes a Store.
 * The info of a store entry whose ASCENT and DESCENT are stated is READY at once (glyph count, box, ratio, monospaced),
 * without loading anything; the other fonts get their box from the loader's probe glyphs as before.
 *
 * Windows only (Win32 threads and directory listing for the drafts).
 */
#ifndef LIBRARY_H
#define LIBRARY_H

#include <stdbool.h>
#include <stdint.h>

#include "pxfont.h"
#include "pxtf.h"
#include "store.h"

typedef enum LibKind {
    LIB_TYPEFACE = 0,    /* one typeface variant (pxtf, number) */
    LIB_FONT = 1,        /* one pxfm */
    LIB_DRAFT = 2        /* one .pxfd file */
} LibKind;

/* What is known about a font without loading its glyph bitmaps. */
typedef struct LibInfo {
    bool ready;              /* the fields below are valid */
    bool failed;             /* the pxfm could not be read / is missing: err says why, nothing else is valid */
    char err[160];
    int  glyphCount;
    bool boxStated;          /* ASCENT and DESCENT are both stated */
    bool boxExact;           /* false: the box was derived from the glyphs loaded so far only (the loader also loads
                                a few probe glyphs H g p y j | [ ] Å É Ç for it) */
    int  above, below;       /* the font box rows (px_font_box); size in pixels = above + below */
    bool monospaced;
    bool hasRatio; int ratioA, ratioB;
    int32_t defaultCodepoint;  /* or PX_NO_CODEPOINT (unknown until the font is loaded) */
} LibInfo;

typedef struct LibEntry {
    LibKind kind;
    char pxfm[33];             /* lower-case pxfm hash ("" for a draft without original) */
    char pxtf[33];             /* lower-case pxtf hash ("" none) */
    int  variant;              /* variant number in that typeface (0 none) */
    const PxtfTypeface *typeface;     /* DRAFT only: its typeface (owned by the Library), else NULL */
    const PxtfVariant *tvariant;      /* DRAFT only: the variant, else NULL */
    /* metadata, never NULL ("" when not stated): pointers valid until library_free */
    const char *family, *style, *fullname, *designer, *format;
    bool italic, struckout, underlined;
    bool fontPresent;          /* the pxfm exists in the store */
    char *path;                /* DRAFT: the .pxfd path, malloc'd; otherwise NULL */
    char *name;                /* DRAFT: its display name; otherwise NULL */
    uint64_t fileSize, mtime;  /* DRAFT only: bytes, last write (FILETIME units) */
    LibInfo info;
} LibEntry;

typedef struct Library {
    char root[1100];                        /* the store's path (root or pxfdb), or the drafts folder */
    LibEntry *e; int count;                 /* typeface variants first (by pxtf hash, variant), then fonts (by hash) */
    int nTypefaceEntries, nFontEntries;
    PxtfTypeface **typefaces; int ntypefaces;   /* drafts' typefaces */
    int *byPxfm; int nByPxfm;               /* indices of TYPEFACE and FONT entries sorted by pxfm hash */
    char **errors; int nerrors;             /* "path: message" for each draft skipped */
    double scanSeconds;
    StoreEntryList tl, fl;                  /* owned: the strings of the entries */
} Library;

/* The index of a store.  false only when the lists cannot be read. */
bool library_scan(Store *store, Library *lib, char err[PX_ERR_LEN]);
/* Scan a drafts folder (*.pxfd) into lib->e (kind LIB_DRAFT; lib->root is the folder).  Missing folder: empty list.
 * A version 2 draft (before the pxtf / F-code change) is skipped with an error that says so. */
bool library_scan_drafts(const char *dir, Library *lib, char err[PX_ERR_LEN]);
void library_free(Library *lib);

/* Index of the first entry of the given kind with this pxfm hash (FONT or TYPEFACE), or -1. */
int  library_find_by_pxfm(const Library *lib, LibKind kind, const char *pxfmHash);
/* "Family Style", else the full name, else the first 8 hash digits (draft: its name) */
void library_entry_title(const LibEntry *e, char *buf, size_t size);
/* Set the info of every entry (TYPEFACE and FONT) with this pxfm hash. */
void library_apply_info(Library *lib, const char *pxfmHash, const LibInfo *info);

/* ------------------------------------------------------------------ the threaded loader */

/* A loaded font: immutable once published, reference counted (the loader's cache holds one reference). */
typedef struct LibFont {
    char hash[33];
    PxFont font;               /* metrics + glyph lines; only the requested glyphs (and probes) have bitmaps */
    LibInfo info;
    int refs;                  /* internal */
} LibFont;

typedef struct LibLoadResult {
    char hash[33];
    LibFont *font;             /* retained: libloader_release() it; NULL when the pxfm could not be read */
    LibInfo info;              /* always valid (info.failed + err when font is NULL); a glyph error leaves info.err
                                  set but info.failed false and the font without bitmaps */
} LibLoadResult;

typedef struct LibLoader LibLoader;
/* The loader reads from `store` (which must outlive it); every worker thread opens its own StoreConn. */
LibLoader *libloader_create(Store *store, int threads /* 0: 6 */, int maxFonts /* LRU cap, 0: 256 */);
void libloader_destroy(LibLoader *L);        /* cancels everything, joins the threads */

/* Ask for font `hash` with the bitmaps of the codes cps[0..n).  Returns true when the cache already holds it
 * with all of them (then libloader_get gives it at once and no result will be posted); otherwise the request is
 * queued (merged with a pending one for the same font) and a result is posted when done. */
bool libloader_request(LibLoader *L, const char *hash, const int32_t *cps, int n);
/* The visible entries, most urgent first: pending requests for other fonts are cancelled, the rest are ordered
 * as given.  A load already running finishes (and is cached). */
void libloader_set_wanted(LibLoader *L, const char (*hashes)[33], int n);
/* Next finished load, if any (thread-safe, non-blocking).  Release r->font when done. */
bool libloader_poll(LibLoader *L, LibLoadResult *r);
/* The cached font (retained, release it) or NULL; marks it recently used. */
LibFont *libloader_get(LibLoader *L, const char *hash);
void libloader_release(LibLoader *L, LibFont *f);
int  libloader_pending(LibLoader *L);        /* queued + running requests */

/* Convenience: library_apply_info with a poll result. */
void library_apply_result(Library *lib, const LibLoadResult *r);

#endif
