/* pxtf.h - pixel typeface model, reader and writer (pxtf version 1; formerly pxfc). No raylib.
 *
 * Spec: C:/Users/jnsGEzD3/Documents/GitHub/pixelglyphlib/spec/pxtf.md.
 * Hashes in the model are 32 LOWER-case hex digits (as file names); the file has them upper case.
 * Text fields are kept UNESCAPED UTF-8 in the model (what a UI shows); the writer escapes them.
 * A C string cannot hold U+0000 (the library has values ending in the escape \x00): it is kept as the two bytes
 * C0 80 ("modified UTF-8") in the model, unescaped from and escaped to \x00 again.
 * The reading options (ADVANCEGAP CELL CHARSET GRID IGNORE NINTHCOLUMN PFM RECORDSIZE SPACEADVANCE) are
 * kept as their raw canonical lines (`KEY\tfield\tfield`, no leading tab) so they round-trip
 * exactly.  FORMAT and TYPE are kept per variant; the writer puts them at the top when every variant has the same
 * (as the spec demands) and the reader fills every variant from the top line.
 *
 * NFC LIMITATION: the reader checks UTF-8 validity strictly, but Unicode Normalization Form C only partially: it
 * rejects nothing that is ASCII, accepts precomposed Latin text, and rejects a decomposed sequence of an ASCII letter
 * and one of the combining marks U+0300, U+0301, U+0302, U+0303, U+0308, U+030A, U+030C, U+0327 where a precomposed
 * Latin letter exists (A E I O U a e i o u + 0300 0301 0302 0303 0308; N n + 0303; C c + 0327 030C; A a + 030A;
 * S s Z z + 030C; E e + 030C; R r + 030C; D d + 030C; T t + 030C).  The writer does not normalise: it writes what the
 * model holds (a UI should hand NFC text, which every Windows text input does).
 */
#ifndef PXTF_H
#define PXTF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pxfont.h"

/* The text keys, in alphabetical key order. */
typedef enum PxtfText {
    PXTF_CODEPAGE, PXTF_COPYRIGHT, PXTF_DESCRIPTION, PXTF_DESIGNER, PXTF_DESIGNERURL, PXTF_FAMILY, PXTF_FULLNAME,
    PXTF_LICENSE, PXTF_LICENSEURL, PXTF_SLANT, PXTF_STYLE, PXTF_TRADEMARK, PXTF_VENDOR, PXTF_VENDORURL,
    PXTF_VERSION, PXTF_WEIGHTNAME,
    PXTF_TEXT_COUNT
} PxtfText;
const char *pxtf_text_key(PxtfText t);     /* "COPYRIGHT", ... */
const char *pxtf_text_label(PxtfText t);   /* "Copyright", "Designer URL", ... */

typedef struct PxtfVariant {
    int number;                    /* >= 1 */
    char pxfm[33];                 /* lower-case hash of the variant's pxfm */
    char *format;                  /* source format "FON", "BDF", ... (malloc'd) */
    char *type;                    /* how it is read, "fnt-v2", ... (malloc'd, unescaped) */
    char *text[PXTF_TEXT_COUNT];   /* unescaped UTF-8, NULL when not stated */
    bool italic, struckout, underlined;
    char **opts; int nopts;        /* raw reading-option lines, in file order */
} PxtfVariant;

typedef struct PxtfTypeface {
    char hash[33];                 /* lower-case hash of the SOURCE file (the HASH top line) */
    PxtfVariant *v; int count, cap;  /* ascending by number */
} PxtfTypeface;

void pxtf_init(PxtfTypeface *c);
void pxtf_free(PxtfTypeface *c);                    /* frees what it holds, not c itself */
bool pxtf_copy(PxtfTypeface *dst, const PxtfTypeface *src);
PxtfVariant *pxtf_find_variant(PxtfTypeface *c, int number);   /* or NULL */
PxtfVariant *pxtf_clone_variant(PxtfTypeface *c, int number);  /* a copy of that variant appended under the next number; NULL on failure */
bool pxtf_set_text(PxtfVariant *v, PxtfText t, const char *utf8); /* NULL or "" clears; copies */

/* Text field escapes (spec "Text"): backslash, tab, line end (LF, CR LF, CR -> \n), other controls \xHH, trailing
 * spaces cut.  pxtf_escape(utf8) -> malloc'd escaped text; pxtf_unescape(escaped, &out, err) -> malloc'd UTF-8
 * or false on a backslash that is no escape. */
char *pxtf_escape(const char *utf8);
bool pxtf_unescape(const char *escaped, char **out, char err[PX_ERR_LEN]);
bool pxtf_utf8_valid(const char *s, size_t len);     /* strict: no overlong, no surrogates, <= U+10FFFF */
bool pxtf_nfc_ok(const char *s, size_t len);         /* partial check, see the limitation above */

/* Strict reader: UTF-8, no BOM, NFC (partial), escapes, order (re-serialised and compared), FORMAT/TYPE placement. */
bool pxtf_parse(const char *data, size_t len, PxtfTypeface *out, char err[PX_ERR_LEN]);
bool pxtf_read(const char *path, PxtfTypeface *out, char err[PX_ERR_LEN]);

/* Canonical text (malloc'd, *len, hash lower-case 32 hex in hashOut).  Variants are written in ascending number. */
char *pxtf_text(const PxtfTypeface *c, size_t *len, char hashOut[33]);
void pxtf_hash(const PxtfTypeface *c, char out[33]);

/* Writes <root>/pxtf/xx/<hash>.pxtf unless it exists (never overwrites). */
bool pxtf_save(const PxtfTypeface *c, const char *root, char hashOut[33], char *pathOut, size_t pathSize,
               char err[PX_ERR_LEN]);

#endif
