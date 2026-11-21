/* pxtf.h - pixel typeface model, reader and writer (pxtf version 2). No raylib.
 *
 * Spec: C:/Users/jnsGEzD3/Documents/GitHub/pxfdb/spec/pxtf.md (version 2).
 * A typeface is its NAME (the top line) and its variants; each variant names its pxfm and its source file
 * (SOURCE-HASH, SOURCE-FORMAT, SOURCE-TYPE), so the variants can come from several source files.
 * Hashes in the model are 32 LOWER-case hex digits (as file names); the file has them upper case.
 * Text fields are kept UNESCAPED UTF-8 in the model (what a UI shows); the writer escapes them.
 * A C string cannot hold U+0000 (the library has values ending in the escape \x00): it is kept as the two bytes
 * C0 80 ("modified UTF-8") in the model, unescaped from and escaped to \x00 again.
 * The reading options (ADVANCEGAP CELL CHARSET GRID IGNORE NINTHCOLUMN PFM RECORDSIZE SPACEADVANCE) are
 * kept as their raw canonical lines (`KEY\tfield\tfield`, no leading tab) so they round-trip exactly; CODEPAGE, a reading
 * option with a text field, is kept as a text key.
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

/* The text keys of a variant: what the source states, in alphabetical key order, then the text keys of the licence terms.
 * PXTF_NAME is the variant key NAME (the font's name); the typeface's NAME top line is PxtfTypeface.name. */
typedef enum PxtfText {
    PXTF_CODEPAGE, PXTF_COPYRIGHT, PXTF_DESCRIPTION, PXTF_DESIGNER, PXTF_DESIGNERURL, PXTF_LICENSE, PXTF_LICENSEURL,
    PXTF_NAME, PXTF_SLANT, PXTF_STYLE, PXTF_TRADEMARK, PXTF_VENDOR, PXTF_VENDORURL, PXTF_VERSION, PXTF_WEIGHTNAME,
    PXTF_TERMS_DERIVED_FROM, PXTF_TERMS_EVIDENCE, PXTF_TERMS_LICENSE, PXTF_TERMS_NOTE, PXTF_TERMS_SOURCE, PXTF_TERMS_SPDX,
    PXTF_TERMS_UPSTREAM_HOLDER,
    PXTF_TEXT_COUNT
} PxtfText;
const char *pxtf_text_key(PxtfText t);     /* "COPYRIGHT", ..., "NAME" for PXTF_NAME */
const char *pxtf_text_label(PxtfText t);   /* "Copyright", "Designer URL", ... */

/* A font made in an editor (spec "A font made in an editor"): SOURCE-FORMAT PXFE, SOURCE-TYPE pxfe, a random SOURCE-HASH and no
 * reading options.  A typeface made in an editor may have no variant; then it must have a NAME. */
#define PXTF_EDITOR_FORMAT "PXFE"
#define PXTF_EDITOR_TYPE   "pxfe"

typedef struct PxtfVariant {
    int number;                    /* >= 1 */
    char pxfm[33];                 /* lower-case hash of the variant's pxfm */
    char source[33];               /* lower-case SOURCE-HASH: the hash of the source file (random for a font made in an editor) */
    char *format;                  /* SOURCE-FORMAT "FON", "BDF", ... (malloc'd) */
    char *type;                    /* SOURCE-TYPE, how it is read, "fnt-v2", ... (malloc'd, unescaped) */
    char *text[PXTF_TEXT_COUNT];   /* unescaped UTF-8, NULL when not stated */
    bool italic, struckout, underlined;
    char **opts; int nopts;        /* raw reading-option lines, in file order */
} PxtfVariant;

typedef struct PxtfTypeface {
    char *name;                    /* the NAME top line (malloc'd, unescaped), NULL when it has none */
    PxtfVariant *v; int count, cap;  /* ascending by number */
} PxtfTypeface;

void pxtf_init(PxtfTypeface *c);
void pxtf_free(PxtfTypeface *c);                    /* frees what it holds, not c itself */
bool pxtf_copy(PxtfTypeface *dst, const PxtfTypeface *src);
PxtfVariant *pxtf_find_variant(PxtfTypeface *c, int number);   /* or NULL */
PxtfVariant *pxtf_clone_variant(PxtfTypeface *c, int number);  /* a copy of that variant appended under the next number; NULL on failure */
PxtfVariant *pxtf_add_variant(PxtfTypeface *c);                /* an empty variant appended under the next number (1 in an empty typeface) */
bool pxtf_set_text(PxtfVariant *v, PxtfText t, const char *utf8); /* NULL or "" clears; copies */
bool pxtf_set_name(PxtfTypeface *c, const char *utf8);           /* the typeface's NAME; NULL or "" clears; copies */
bool pxtf_set_editor_source(PxtfVariant *v);                     /* SOURCE-FORMAT PXFE, SOURCE-TYPE pxfe and a random SOURCE-HASH */
void pxtf_random_hash(char out[33]);                             /* 32 random lower-case hex digits */

/* Licence terms (spec "Licence terms").  The text keys are PxtfText (PXTF_TERMS_*); the keys of a word, a date or a hash
 * (TERMS-CLASS, -COMMERCIAL, -CONFIDENCE, -DATE, -MODIFY, -OBLIGATION, -PROVENANCE, -REDISTRIBUTE, -REDISTRIBUTE-MODIFIED,
 * -TEXT, -UPSTREAM-STATUS, -USE-LIMIT) are lines of `opts`, checked by the reader like the reading options; a typeface edit
 * that copies a variant copies them with it.  TERMS-TEXT and TERMS-OBLIGATION may come several times. */
typedef struct PxtfWord { const char *word, *meaning; } PxtfWord;
const PxtfWord *pxtf_terms_words(const char *key, int *n);        /* the words a TERMS- key takes, with their meanings in plain words; NULL for another key */
const char *pxtf_terms_meaning(const char *key, const char *word); /* the meaning of a word of that key, else the word itself ("" for NULL) */
const char *pxtf_opt(const PxtfVariant *v, const char *key);       /* the fields of the first line of that key in `opts` (into it), else NULL */
int pxtf_opt_all(const PxtfVariant *v, const char *key, const char **out, int max);   /* the fields of every such line, in order; their number */
const char *pxtf_license_short(const PxtfVariant *v);              /* TERMS-SPDX, else TERMS-LICENSE, else the TERMS-CLASS name, else LICENSE, else "" */

/* Licence text (spec license.md): the canonical form of `data` (lines split at CR LF, CR and LF; spaces and tabs cut at the
 * line ends; empty lines at the start and the end left out and runs inside made one; each line ended with LF), malloc'd with
 * *outLen, or NULL with err when the text is not UTF-8, begins with a byte order mark, is not in NFC, holds a control
 * character other than the tab, CR and LF, or is empty.  Its hash is px_hash128_hex of the canonical form. */
char *pxtf_license_canonical(const char *data, size_t len, size_t *outLen, char err[PX_ERR_LEN]);

/* Text field escapes (spec "Text"): backslash, tab, line end (LF, CR LF, CR -> \n), other controls \xHH, trailing
 * spaces cut.  pxtf_escape(utf8) -> malloc'd escaped text; pxtf_unescape(escaped, &out, err) -> malloc'd UTF-8
 * or false on a backslash that is no escape. */
char *pxtf_escape(const char *utf8);
bool pxtf_unescape(const char *escaped, char **out, char err[PX_ERR_LEN]);
bool pxtf_utf8_valid(const char *s, size_t len);     /* strict: no overlong, no surrogates, <= U+10FFFF */
bool pxtf_nfc_ok(const char *s, size_t len);         /* partial check, see the limitation above */

/* Strict reader: UTF-8, no BOM, NFC (partial), escapes, order (re-serialised and compared), the four keys every variant has. */
bool pxtf_parse(const char *data, size_t len, PxtfTypeface *out, char err[PX_ERR_LEN]);
bool pxtf_read(const char *path, PxtfTypeface *out, char err[PX_ERR_LEN]);

/* Canonical text (malloc'd, *len, hash lower-case 32 hex in hashOut).  Variants are written in ascending number. */
char *pxtf_text(const PxtfTypeface *c, size_t *len, char hashOut[33]);
void pxtf_hash(const PxtfTypeface *c, char out[33]);

/* Writes <root>/pxtf/xx/<hash>.pxtf unless it exists (never overwrites). */
bool pxtf_save(const PxtfTypeface *c, const char *root, char hashOut[33], char *pathOut, size_t pathSize,
               char err[PX_ERR_LEN]);

#endif
