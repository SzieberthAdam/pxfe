/* package.h - font packages (pxfp) and typeface packages (pxtp): writing them from the store, checking and importing them.
 * No raylib.  DESIGN.md "Storage: Load, Save, Import, Export", section "Packages".
 *
 * Specs: pixelglyphlib/spec/pxfp.md and spec/pxtp.md; reference implementation pixelglyphlib/pxfp.py, pxtp.py, pkgbase.py.
 * A package is a plain zip file of library files named by their lower-case hashes: a pxfp holds `<hash>.pxfm` at its root and
 * the files it names in `pxgl/` and `palette/`; a pxtp holds `<hash>.pxtf` at its root, the fonts it names in `pxfm/` and
 * the files those name in `pxgl/` and `palette/`.  The text of every file comes from the rows (store_pxfm_text and so on).
 *
 * WRITING is deterministic and gives the bytes the reference gives (with zlib's deflate): entries in the byte order of their
 * names, deflate level 9 (raw, window 15, memory level 8: vendor/zlib), time 1980-01-01 00:00:00, made on Unix, mode 0644,
 * no extra field, no comment, no folder entry.  A package file is written to `<path>.tmp` and then moved over `path`.
 *
 * READING checks the whole package by its content, never by its file name or extension, before anything is used: the zip
 * file itself (stored or deflated entries, CRC-32, no entry twice, no folder entry, no backslash, leading slash or `..`),
 * the shape of either kind, every name against the hash of the canonical form of its content, the cross-references (exactly
 * the files that are named), and every file with the strict readers of the store's folder import.  The files are kept in
 * their canonical form (pxgl.md, pxfm.md, pxtf.md, hex.md: a reader makes the canonical form of what it reads).
 *
 * IMPORTING adds the files of a checked package to the in-memory store, all or nothing (store_add_files): a file the store
 * has already with the same text is skipped; if the store has a file of that hash with another text, nothing is added and
 * the call fails.  It is a change of the store (store_modified), saved with store_save like any other.
 *
 * THREADING: as the store's (store.h): the writers take a StoreConn of the calling thread; pkg_import changes the store, so
 * call it where the UI would save a font (store_save_to_memory).  Errors are complete sentences of PX_ERR_LEN.
 */
#ifndef PACKAGE_H
#define PACKAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "pxfont.h"
#include "store.h"

typedef enum PkgKind { PKG_NONE = 0, PKG_FONT = 1, PKG_TYPEFACE = 2 } PkgKind;   /* pxfp, pxtp */

#define PKG_FONT_EXT ".pxfp"
#define PKG_TYPEFACE_EXT ".pxtp"

/* ------------------------------------------------------------------ writing */

/* The package of a font (pxfm hash) / of a typeface (pxtf hash) of the store, written to exactly `path`. */
bool pkg_write_font(StoreConn *c, const char *pxfmHash, const char *path, char err[PX_ERR_LEN]);
bool pkg_write_typeface(StoreConn *c, const char *pxtfHash, const char *path, char err[PX_ERR_LEN]);

/* The same as bytes in memory (malloc'd; *len gets the size); NULL + err on failure. */
unsigned char *pkg_font_bytes(StoreConn *c, const char *pxfmHash, size_t *len, char err[PX_ERR_LEN]);
unsigned char *pkg_typeface_bytes(StoreConn *c, const char *pxtfHash, size_t *len, char err[PX_ERR_LEN]);

/* The recommended file name, without a folder:
 *   font:     `<typeface name>-<style>.pxfp` of the first typeface variant that names the font (pxtf hashes ascending, then variant
 *             numbers), each part the slug of the value (lower case, every run of characters other than a-z 0-9 one hyphen,
 *             none at the ends; an empty part is left out with its hyphen), else `<pxfm hash>.pxfp`;
 *   typeface: `<typeface name>.pxtp`, its NAME top line, else `typeface.pxtp`.
 * The rule for a name taken in the folder (`-<first 8 hex digits>` before the extension) needs the folder: pkg_export_*. */
void pkg_default_name_font(StoreConn *c, const char *pxfmHash, char *out, size_t cap);
void pkg_default_name_typeface(StoreConn *c, const char *pxtfHash, char *out, size_t cap);

/* Write the package into `folder` under its recommended name, as the reference's `package export` does: if a file of that
 * name is there already with other bytes, `-<first 8 hex digits of the hash>` goes before the extension (a file of that name
 * is then replaced).  pathOut (may be NULL) gets the path written. */
bool pkg_export_font(StoreConn *c, const char *pxfmHash, const char *folder, char *pathOut, size_t cap, char err[PX_ERR_LEN]);
bool pkg_export_typeface(StoreConn *c, const char *pxtfHash, const char *folder, char *pathOut, size_t cap, char err[PX_ERR_LEN]);

/* ------------------------------------------------------------------ reading and importing */

typedef struct PkgImportResult {
    PkgKind kind;               /* decided by the content: a pxfm at the root is a pxfp, a pxtf a pxtp */
    char pxtf[33];              /* the typeface of a pxtp, "" for a pxfp */
    char (*pxfm)[33]; int nPxfm;   /* the fonts of the package, ascending (malloc'd: pkg_result_free) */
    int nGlyphs, nPalettes;     /* pxgl and palette files of the package */
    int nLicenses;              /* licence text files of the package (license/) */
    StoreCounts added, skipped; /* pkg_import: files added to the store / there already with the same text */
} PkgImportResult;

/* Read and check the package file in full (as above) without touching any store.  res may be NULL. */
bool pkg_check(const char *path, PkgImportResult *res, char err[PX_ERR_LEN]);
/* Check the package in full, then add its files to the store, all or nothing.  res may be NULL. */
bool pkg_import(Store *s, const char *path, PkgImportResult *res, char err[PX_ERR_LEN]);
/* The same two for a package in memory (the bytes of a file). */
bool pkg_check_bytes(const unsigned char *zip, size_t len, PkgImportResult *res, char err[PX_ERR_LEN]);
bool pkg_import_bytes(Store *s, const unsigned char *zip, size_t len, PkgImportResult *res, char err[PX_ERR_LEN]);
void pkg_result_free(PkgImportResult *r);

#endif
