/* test_pxtf.c - tests for src/pxtf.c and for the library scan / lazy loader (src/library.c) on testdata/lib. */
#include "library.h"
#include "pxtf.h"
#include "pxfont.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

#define TMP "tests/tmp"
#define LIB "testdata/lib"

static const char EXAMPLE[] =   /* the example of the pxtf spec (version 2) */
    "NAME\tASI_Mono\n"
    "0001\n\tCHARSET\t0\n\tCOPYRIGHT\t(c)Copyright Altura Software Inc. 1992. All rights reserved\n"
    "\tPXFM\tFBB120E8372CDFA484F0107DFBA46860\n\tSOURCE-FORMAT\tFON\n\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\n\tSOURCE-TYPE\tfnt-v2\n\tSTYLE\tRegular\n"
    "0002\n\tCHARSET\t0\n\tCOPYRIGHT\t(c) Copyright Microsoft Corp. 1987. All rights reserved.\n"
    "\tPXFM\t7F3882B4A3788FDE3BC0111426A12C8F\n\tSOURCE-FORMAT\tFON\n\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\n\tSOURCE-TYPE\tfnt-v2\n\tSTYLE\tRegular\n";

/* the lines every variant must have but PXFM: SOURCE-FORMAT, SOURCE-HASH and SOURCE-TYPE */
#define SRC "\tSOURCE-FORMAT\tFON\n\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\n\tSOURCE-TYPE\tfnt-v2\n"
#define PXFM_LINE "\tPXFM\tFBB120E8372CDFA484F0107DFBA46860\n"

static bool pfails(const char *text, const char *needle) {
    PxtfTypeface c;
    char err[PX_ERR_LEN] = "";
    if (pxtf_parse(text, strlen(text), &c, err)) { pxtf_free(&c); printf("      (accepted)\n"); return false; }
    if (needle && !strstr(err, needle)) { printf("      error was: %s\n", err); return false; }
    return true;
}

static void test_escapes(void) {
    char *e = pxtf_escape("a\tb\\c\r\nd\re\nf\x01g  ");
    CHECK(strcmp(e, "a\\tb\\\\c\\nd\\ne\\nf\\x01g") == 0, "escape: tab, backslash, line ends, control, trailing spaces");
    char *u = NULL;
    char err[PX_ERR_LEN];
    CHECK(pxtf_unescape(e, &u, err) && strcmp(u, "a\tb\\c\nd\ne\nf\x01g") == 0, "unescape is the inverse (line ends become LF)");
    free(e); free(u);
    CHECK(pxtf_unescape("a\\x00", &u, err) && strcmp(u, "a\xC0\x80") == 0, "unescape: \\x00 is kept as C0 80");
    e = pxtf_escape(u);
    CHECK(strcmp(e, "a\\x00") == 0, "escape: C0 80 gives \\x00 again");
    free(e); free(u);
    CHECK(!pxtf_unescape("bad\\q", &u, err), "unescape rejects \\q");
    CHECK(!pxtf_unescape("bad\\x1", &u, err), "unescape rejects short \\x");
    CHECK(!pxtf_unescape("bad\\x1f", &u, err), "unescape rejects lower-case hex");
    CHECK(!pxtf_unescape("bad\\", &u, err), "unescape rejects trailing backslash");
    e = pxtf_escape("\x7F");
    CHECK(strcmp(e, "\\x7F") == 0, "DEL is escaped");
    free(e);
    CHECK(pxtf_utf8_valid("\xC3\xA1\xE2\x82\xAC\xF0\x9F\x98\x80", 9), "utf8: valid sequences");
    CHECK(!pxtf_utf8_valid("\xC0\x80", 2) && !pxtf_utf8_valid("\xED\xA0\x80", 3) && !pxtf_utf8_valid("\xC3", 1) &&
          !pxtf_utf8_valid("\xF4\x90\x80\x80", 4) && !pxtf_utf8_valid("\x80", 1), "utf8: overlong, surrogate, truncated, too big, stray");
    CHECK(pxtf_nfc_ok("plain ASCII e", 13) && pxtf_nfc_ok("\xC3\xA9\xC3\x85", 4), "nfc: ASCII and precomposed pass");
    CHECK(!pxtf_nfc_ok("e\xCC\x81", 3) && !pxtf_nfc_ok("A\xCC\x8A", 3), "nfc: e + acute and A + ring are rejected");
    CHECK(pxtf_nfc_ok("\xCC\x81", 2), "nfc: a lone combining mark passes (partial check)");
}

static void test_example(void) {
    PxtfTypeface c;
    char err[PX_ERR_LEN];
    CHECK(pxtf_parse(EXAMPLE, sizeof EXAMPLE - 1, &c, err), "pxtf example parses");
    if (g_fail) printf("      %s\n", err);
    CHECK(c.count == 2 && c.v[0].number == 1 && c.v[1].number == 2 && c.name && strcmp(c.name, "ASI_Mono") == 0, "example: variants and the NAME top line");
    CHECK(strcmp(c.v[0].format, "FON") == 0 && strcmp(c.v[1].type, "fnt-v2") == 0 && strcmp(c.v[1].source, "43b49707749923470cdd7f5fcd6a552d") == 0,
          "example: SOURCE-FORMAT, SOURCE-TYPE and SOURCE-HASH of each variant");
    CHECK(c.v[0].text[PXTF_NAME] == NULL && c.v[0].text[PXTF_DESIGNER] == NULL && c.v[0].text[PXTF_COPYRIGHT], "example: text fields");
    CHECK(strcmp(c.v[1].pxfm, "7f3882b4a3788fde3bc0111426a12c8f") == 0 && c.v[0].nopts == 1 && strcmp(c.v[0].opts[0], "CHARSET\t0") == 0, "example: PXFM lower-case, CHARSET kept raw");
    size_t n;
    char hash[33];
    char *t = pxtf_text(&c, &n, hash);
    CHECK(n == sizeof EXAMPLE - 1 && memcmp(t, EXAMPLE, n) == 0, "example: written again is the same bytes");
    CHECK(strcmp(hash, "201c9ca4b9c357621f90cdf98ac47321") == 0, "example: the hash of the spec, 201c9ca4...");
    if (strcmp(hash, "201c9ca4b9c357621f90cdf98ac47321")) printf("      hash %s\n", hash);
    free(t);
    pxtf_free(&c);
}

static void test_roundtrip_variants(void) {
    /* different SOURCE- lines per variant, options, flags, an escape, UTF-8 */
    const char *text =
        "NAME\tRound \xC3\x81" "d\xC3\xA1m\n"
        "0001\n\tADVANCEGAP\t-1\n\tCELL\t8x16\n\tCODEPAGE\tcp437-graphic\n\tGRID\t44.487\t44.487\t0\t0\tPERGLYPH\n"
        "\tIGNORE\t000000\t00001F\n\tIGNORE\t0000C0\t0000FF\n\tITALIC\n\tNINTHCOLUMN\t192\t223\n"
        "\tPFM\t0123456789ABCDEF0123456789ABCDEF\n\tPXFM\t0123456789ABCDEF0123456789ABCDEF\n\tRECORDSIZE\t16\n\tSLANT\tI\n"
        "\tSOURCE-FORMAT\tRAW\n\tSOURCE-HASH\t0123456789ABCDEF0123456789ABCDEF\n\tSOURCE-TYPE\tt\\tx\n"
        "\tSTRUCKOUT\n\tUNDERLINED\n\tWEIGHTNAME\tBold\n"
        "0010\n\tDESCRIPTION\tline one\\nline two\\ttab \\\\ \\x01 \xC3\xA1\xC5\x91\n\tPXFM\t0123456789ABCDEF0123456789ABCDE0\n"
        "\tSOURCE-FORMAT\tFON\n\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\n\tSOURCE-TYPE\tfnt-v2\n\tSPACEADVANCE\t4\n";
    PxtfTypeface c;
    char err[PX_ERR_LEN];
    CHECK(pxtf_parse(text, strlen(text), &c, err), "variants with their own source, options and flags parse");
    if (g_fail) printf("      %s\n", err);
    CHECK(c.count == 2 && c.v[1].number == 10 && strcmp(c.v[0].format, "RAW") == 0 && strcmp(c.v[1].format, "FON") == 0, "own SOURCE-FORMAT per variant");
    CHECK(strcmp(c.v[0].type, "t\tx") == 0 && strcmp(c.v[0].source, "0123456789abcdef0123456789abcdef") == 0, "SOURCE-TYPE unescaped, SOURCE-HASH lower-case");
    CHECK(c.v[0].italic && c.v[0].struckout && c.v[0].underlined && !c.v[1].italic, "flags");
    CHECK(c.v[0].nopts == 8 && strcmp(c.v[0].opts[2], "GRID\t44.487\t44.487\t0\t0\tPERGLYPH") == 0 && strcmp(c.v[0].text[PXTF_CODEPAGE], "cp437-graphic") == 0,
          "options kept as raw lines, CODEPAGE a text");
    CHECK(strcmp(c.v[1].text[PXTF_DESCRIPTION], "line one\nline two\ttab \\ \x01 \xC3\xA1\xC5\x91") == 0, "text unescaped");
    size_t n;
    char *t = pxtf_text(&c, &n, NULL);
    CHECK(n == strlen(text) && memcmp(t, text, n) == 0, "round trip: same bytes");
    free(t);
    /* editing text */
    pxtf_set_text(&c.v[0], PXTF_NAME, "New  ");
    pxtf_set_text(&c.v[0], PXTF_WEIGHTNAME, "");
    pxtf_set_name(&c, "Renamed  ");
    t = pxtf_text(&c, &n, NULL);
    CHECK(strstr(t, "\tNAME\tNew\n") != NULL && strstr(t, "WEIGHTNAME") == NULL, "set text, trailing spaces cut, empty clears");
    CHECK(strstr(t, "\n\tNAME") < strstr(t, "\n\tNINTHCOLUMN"), "set text lands in sorted place");
    CHECK(strncmp(t, "NAME\tRenamed\n0001\n", 18) == 0, "pxtf_set_name: the NAME top line");
    free(t);
    PxtfTypeface d;
    CHECK(pxtf_copy(&d, &c), "copy");
    char h1[33], h2[33];
    pxtf_hash(&c, h1);
    pxtf_hash(&d, h2);
    CHECK(strcmp(h1, h2) == 0, "copy has the same hash");
    pxtf_set_text(&d.v[0], PXTF_STYLE, "Changed");
    pxtf_hash(&d, h2);
    CHECK(strcmp(h1, h2) != 0, "an edit changes the hash");
    pxtf_free(&d);
    pxtf_free(&c);
}

static void test_errors(void) {
    const char *top = "NAME\tA\n";
    const char *v1 = "0001\n" PXFM_LINE SRC;
    char t[2048];
#define TRY(fmt, ...) (snprintf(t, sizeof t, fmt, __VA_ARGS__), t)
    CHECK(pfails("\xEF\xBB\xBF" "NAME\tA\n", "byte order mark"), "BOM rejected");
    CHECK(pfails(TRY("%s0001\n\tNAME\t\xC3(\n" PXFM_LINE SRC, top), "UTF-8"), "invalid UTF-8 rejected");
    CHECK(pfails(TRY("%s0001\n\tNAME\te\xCC\x81\n" PXFM_LINE SRC, top), "NFC"), "decomposed e + acute rejected");
    CHECK(pfails(TRY("%s0001\n\tNAME\t\xC3\xA9\n" PXFM_LINE SRC, top), NULL) == false, "precomposed accepted");
    CHECK(pfails(TRY("%s0001\n\tNAME\ta\\qb\n" PXFM_LINE SRC, top), "escape"), "bad escape rejected");
    CHECK(pfails(TRY("%s0001\n\tITALIC\tx\n" PXFM_LINE SRC, top), "no field"), "ITALIC with a field rejected");
    CHECK(pfails(TRY("%s0001\n\tNAME\tA\n" SRC, top), "no PXFM"), "variant without PXFM rejected");
    CHECK(pfails(TRY("%s0001\n\tPXFM\tfbb120e8372cdfa484f0107dfba46860\n" SRC, top), "PXFM"), "lower-case PXFM rejected");
    CHECK(pfails(TRY("%s0001\n" PXFM_LINE "\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\n\tSOURCE-TYPE\tt\n", top), "SOURCE-FORMAT"), "no SOURCE-FORMAT rejected");
    CHECK(pfails(TRY("%s0001\n" PXFM_LINE "\tSOURCE-FORMAT\tFON\n\tSOURCE-TYPE\tt\n", top), "SOURCE-HASH"), "no SOURCE-HASH rejected");
    CHECK(pfails(TRY("%s0001\n" PXFM_LINE "\tSOURCE-FORMAT\tFON\n\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\n", top), "SOURCE-TYPE"), "no SOURCE-TYPE rejected");
    CHECK(pfails(TRY("%s0001\n" PXFM_LINE "\tSOURCE-FORMAT\tfon\n\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\n\tSOURCE-TYPE\tt\n", top), "SOURCE-FORMAT"), "lower-case SOURCE-FORMAT rejected");
    CHECK(pfails(TRY("%s0001\n" PXFM_LINE "\tSOURCE-FORMAT\tFON\n\tSOURCE-HASH\t43b49707749923470cdd7f5fcd6a552d\n\tSOURCE-TYPE\tt\n", top), "SOURCE-HASH"), "lower-case SOURCE-HASH rejected");
    {   /* a typeface with no variant: it must have a NAME (a typeface made in an editor that has no font yet) */
        PxtfTypeface e;
        char err[PX_ERR_LEN] = "";
        bool ok = pxtf_parse(top, strlen(top), &e, err);
        CHECK(ok && e.count == 0 && e.name && strcmp(e.name, "A") == 0, "a typeface with a NAME and no variant accepted");
        if (ok) {
            size_t n;
            char *again = pxtf_text(&e, &n, NULL);
            CHECK(again && n == strlen(top) && memcmp(again, top, n) == 0, "a typeface with no variant written as read");
            free(again);
            PxtfVariant *v = pxtf_add_variant(&e);
            CHECK(v && v->number == 1 && e.count == 1, "pxtf_add_variant: variant 0001 in an empty typeface");
            pxtf_set_editor_source(v);
            CHECK(strcmp(v->format, PXTF_EDITOR_FORMAT) == 0 && strcmp(v->type, PXTF_EDITOR_TYPE) == 0 && px_is_hash(v->source, false, true),
                  "pxtf_set_editor_source: SOURCE-FORMAT PXFE, SOURCE-TYPE pxfe, a random SOURCE-HASH");
            char other[33];
            pxtf_random_hash(other);
            CHECK(strcmp(other, v->source) != 0, "two random source hashes differ");
            pxtf_free(&e);
        }
        CHECK(pfails("", "no variant"), "no variant and no NAME rejected");
        CHECK(pfails(TRY("%s", v1), NULL) == false, "variants and no NAME accepted");
    }
    CHECK(pfails(TRY("FORMAT\tFON\n%s", v1), "version 1"), "a version 1 top line rejected");
    CHECK(pfails(TRY("%s%s%s", top, v1, v1), "ascending"), "variant twice rejected");
    CHECK(pfails(TRY("%s0002\n" PXFM_LINE SRC "0001\n" PXFM_LINE SRC, top), "ascending"), "descending variants rejected");
    CHECK(pfails(TRY("%s0000\n" PXFM_LINE SRC, top), "out of range"), "variant 0 rejected");
    CHECK(pfails(TRY("%s%sNAME\tB\n", top, v1), "top line after"), "top line after a variant rejected");
    CHECK(pfails("NAME\tA\n" PXFM_LINE, "before any"), "variant line before a variant rejected");
    CHECK(pfails("NAME\tA\nNAME\tB\n", "two NAME"), "two NAME top lines rejected");
    CHECK(pfails(TRY("%s0001\n\tSTYLE\tB\n\tNAME\tA\n" PXFM_LINE SRC, top), "canonical order"), "unsorted variant lines rejected");
    CHECK(pfails(TRY("%s0001\n\tNAME\tA\n\tNAME\tB\n" PXFM_LINE SRC, top), "two NAME"), "two NAME lines in a variant rejected");
    CHECK(pfails(TRY("%s0001\n\tFAMILY\tA\n" PXFM_LINE SRC, top), "unknown key"), "the version 1 key FAMILY rejected");
    CHECK(pfails(TRY("%s0001\n\tFOO\tA\n" PXFM_LINE SRC, top), "unknown key"), "unknown variant key rejected");
    CHECK(pfails(TRY("%s0001\n\tCELL\t8X16\n" PXFM_LINE SRC, top), "CELL"), "CELL with upper-case X rejected");
    CHECK(pfails(TRY("%s0001\n\tGRID\t44.50\t1\t0\t0\n" PXFM_LINE SRC, top), "shortest"), "GRID number not in shortest form rejected");
    CHECK(pfails(TRY("%s0001\n\tCHARSET\t007\n" PXFM_LINE SRC, top), "integer"), "CHARSET with leading zero rejected");
    CHECK(pfails(TRY("%s0001\n" PXFM_LINE SRC "\tSTYLE\tB\\\n", top), "escape"), "trailing backslash rejected");
    /* accepted: CRLF and trailing blanks canonicalise */
    PxtfTypeface c;
    char err[PX_ERR_LEN];
    snprintf(t, sizeof t, "NAME\tA  \r\n\r\n0001\r\n\tPXFM\tFBB120E8372CDFA484F0107DFBA46860\t\r\n\tSOURCE-FORMAT\tFON\r\n\tSOURCE-HASH\t43B49707749923470CDD7F5FCD6A552D\r\n\tSOURCE-TYPE\tt\r\n");
    CHECK(pxtf_parse(t, strlen(t), &c, err), "CRLF, blank lines and trailing blanks canonicalise");
    if (g_fail) printf("      %s\n", err);
    pxtf_free(&c);
#undef TRY
}

static void test_save(void) {
    PxtfTypeface c;
    char err[PX_ERR_LEN], hash[33], path[1024];
    CHECK(pxtf_parse(EXAMPLE, sizeof EXAMPLE - 1, &c, err), "parse for save");
    pxtf_set_name(&c, "Saved");
    CHECK(pxtf_save(&c, TMP "/pxtflib", hash, path, sizeof path, err), "pxtf_save");
    PxtfTypeface r;
    CHECK(pxtf_read(path, &r, err), "saved pxtf reads back");
    char h2[33];
    pxtf_hash(&r, h2);
    CHECK(strcmp(h2, hash) == 0 && strstr(path, hash) != NULL, "file name == hash");
    CHECK(pxtf_save(&c, TMP "/pxtflib", hash, path, sizeof path, err), "saving again does not fail or overwrite");
    pxtf_free(&r);
    pxtf_free(&c);
}

#define H_EXAMPLE "3b52125b2e5f6466823baa4fa3186fa0"
#define H_GREY "894ef821e147533c34b39e2131016133"
#define H_NOASC "4fba4190f6ba7e10b296c7070234e1ce"
#define H_UNC "54caf68fe9ec18634701a55bfc87b5c0"
#define T_SANS "236dc4b75338cd1dfd6af2da597df169"
#define T_UNC "0021a99dbbf1ae9dc9c27c685f581c17"

static int find_entry(const Library *lib, LibKind kind, const char *pxtf, int variant) {
    for (int i = 0; i < lib->count; i++)
        if (lib->e[i].kind == kind && (!pxtf || (strcmp(lib->e[i].pxtf, pxtf) == 0 && lib->e[i].variant == variant))) return i;
    return -1;
}

static void test_library(void) {
    Library lib;
    char err[PX_ERR_LEN];
    Store *st = NULL;
    CHECK(store_open_pxfdb("testdata/lib.pxfdb", NULL, &st, err), "open testdata/lib.pxfdb");
    if (!st) return;
    CHECK(library_scan(st, &lib, err), "library scan of the store");
    CHECK(lib.nTypefaceEntries == 3 && lib.nFontEntries == 4 && lib.count == 7, "3 typeface variants, 4 fonts");
    CHECK(lib.nerrors == 0, "no scan errors");
    int c1 = find_entry(&lib, LIB_TYPEFACE, T_SANS, 1), c2 = find_entry(&lib, LIB_TYPEFACE, T_SANS, 2), c3 = find_entry(&lib, LIB_TYPEFACE, T_UNC, 1);
    CHECK(c1 >= 0 && c2 >= 0 && c3 >= 0, "typeface entries found by (pxtf, variant)");
    if (c1 < 0 || c2 < 0 || c3 < 0) { library_free(&lib); store_close(st); return; }
    LibEntry *e1 = &lib.e[c1];
    CHECK(strcmp(e1->typefaceName, "Test Sans") == 0 && strcmp(e1->style, "Regular") == 0 && strcmp(e1->format, "BDF") == 0 &&
          strcmp(e1->designer, "\xC3\x81" "d\xC3\xA1m Test") == 0 && strcmp(e1->pxfm, H_EXAMPLE) == 0, "typeface entry metadata");
    CHECK(strcmp(lib.e[c2].style, "Italic") == 0 && lib.e[c2].italic && lib.e[c2].fontPresent, "second variant: italic");
    CHECK(strcmp(lib.e[c3].format, "RAW") == 0 && strcmp(lib.e[c3].pxfm, H_UNC) == 0, "RAW typeface entry");
    CHECK(e1->info.ready && e1->info.glyphCount == 6 && e1->info.above == 6 && e1->info.below == 2 && e1->info.boxStated, "stated box: info ready from the store at once");
    CHECK(lib.e[c3].info.ready && lib.e[c3].info.monospaced && lib.e[c3].info.hasRatio && lib.e[c3].info.glyphCount == 5, "monospaced, ratio, 5 glyphs (the default line is no glyph)");
    int fi = library_find_by_pxfm(&lib, LIB_FONT, H_EXAMPLE);
    CHECK(fi >= 0 && strcmp(lib.e[fi].typefaceName, "Test Sans") == 0 && lib.e[fi].fontPresent, "font entry takes the metadata of the typeface that names it");
    int gi = library_find_by_pxfm(&lib, LIB_FONT, H_GREY);
    CHECK(gi >= 0 && lib.e[gi].pxtf[0] == 0 && lib.e[gi].typefaceName[0] == 0, "font without typeface: empty metadata");
    int ni = library_find_by_pxfm(&lib, LIB_FONT, H_NOASC);
    CHECK(ni >= 0 && !lib.e[ni].info.ready, "no-ascent font: info not ready until loaded");
    char title[128];
    library_entry_title(&lib.e[c1], title, sizeof title);
    CHECK(strcmp(title, "Test Sans Regular") == 0, "title");
    library_entry_title(&lib.e[gi], title, sizeof title);
    CHECK(strcmp(title, "894ef821") == 0, "title of an unnamed font is its short hash");

    /* the loader */
    LibLoader *L = libloader_create(st, 3, 2);
    PxCode cps[] = {px_code_cp(0x41), px_code_cp(0x48)};
    CHECK(!libloader_request(L, H_EXAMPLE, cps, 2), "request queued");
    const char hashes[1][33] = {H_EXAMPLE};
    libloader_set_wanted(L, hashes, 1);
    LibLoadResult r;
    bool got = false;
    for (int i = 0; i < 500 && !got; i++) { got = libloader_poll(L, &r); if (!got) Sleep(10); }
    CHECK(got && r.font && !r.info.failed && r.info.glyphCount == 6, "result arrives with info");
    CHECK(got && r.info.boxStated && r.info.boxExact && r.info.above == 6 && r.info.below == 2 && r.info.hasRatio == false && true, "info: stated box");
    if (got) {
        const PxFont *f = &r.font->font;
        CHECK(!f->glyphs[px_font_find_cp(f, 0x41)].lazy && !f->glyphs[px_font_find_cp(f, 0x48)].lazy && f->glyphs[px_font_find_cp(f, 0x4D)].lazy, "only the requested glyphs have bitmaps");
        library_apply_result(&lib, &r);
        CHECK(lib.e[fi].info.ready && lib.e[fi].info.glyphCount == 6 && lib.e[c1].info.ready, "info applied to font and typeface entries");
        libloader_release(L, r.font);
    }
    CHECK(libloader_request(L, H_EXAMPLE, cps, 2), "second request is satisfied from the cache");
    LibFont *lf = libloader_get(L, H_EXAMPLE);
    CHECK(lf != NULL, "libloader_get");
    libloader_release(L, lf);
    /* more code points: merged, the old ones stay loaded */
    PxCode more[] = {px_code_cp(0x4D)};
    libloader_request(L, H_EXAMPLE, more, 1);
    got = false;
    for (int i = 0; i < 500 && !got; i++) { got = libloader_poll(L, &r); if (!got) Sleep(10); }
    CHECK(got && r.font && !r.font->font.glyphs[px_font_find_cp(&r.font->font, 0x4D)].lazy && !r.font->font.glyphs[px_font_find_cp(&r.font->font, 0x41)].lazy, "new code points are added, old ones kept");
    if (got) libloader_release(L, r.font);
    /* the no-ascent font gets probe glyphs, an unknown font fails, LRU of 2 */
    libloader_request(L, H_NOASC, cps, 1);
    libloader_request(L, "00000000000000000000000000000000", cps, 1);
    libloader_request(L, H_GREY, cps, 1);
    int seen = 0;
    bool noasc = false, failed = false;
    for (int i = 0; i < 800 && seen < 3; i++) {
        if (libloader_poll(L, &r)) {
            seen++;
            if (strcmp(r.hash, H_NOASC) == 0) noasc = r.font && !r.info.boxStated && r.info.above == 5 && r.info.below == 2;
            if (strcmp(r.hash, "00000000000000000000000000000000") == 0) failed = r.info.failed && r.font == NULL;
            if (r.font) libloader_release(L, r.font);
        } else Sleep(10);
    }
    CHECK(seen == 3, "three results");
    CHECK(noasc, "no-ascent font: box derived from probe glyphs");
    CHECK(failed, "missing pxfm gives a failed info");
    LibFont *old = libloader_get(L, H_EXAMPLE);
    CHECK(old == NULL, "LRU: the least recently used font was evicted (cap 2)");
    libloader_release(L, old);
    /* cancellation */
    for (int i = 0; i < 20; i++) {
        char h[33];
        snprintf(h, sizeof h, "%032x", i + 1);
        libloader_request(L, h, cps, 1);
    }
    libloader_set_wanted(L, hashes, 0);
    CHECK(libloader_pending(L) <= 3, "set_wanted cancels pending requests");
    libloader_destroy(L);
    library_free(&lib);

    /* drafts */
    PxFont f;
    StoreConn *sc = store_conn_open(st);
    CHECK(store_load_font(sc, H_EXAMPLE, NULL, 0, &f, err), "load font from the store for a draft");
    PxDraftInfo info;
    px_draft_info_init(&info);
    snprintf(info.name, sizeof info.name, "Duplicate of Test Sans");
    snprintf(info.originalHash, 33, "%s", H_EXAMPLE);
    snprintf(info.originalPxtfHash, 33, "%s", T_SANS);
    info.originalVariant = 1;
    info.typeface = (PxtfTypeface *)calloc(1, sizeof(PxtfTypeface));
    CHECK(store_load_typeface(sc, T_SANS, info.typeface, err), "load typeface from the store for a draft");
    store_conn_close(sc);
    px_mkdir_p(TMP "/unit_drafts");
    DeleteFileA(TMP "/unit_drafts/a.pxfd"); DeleteFileA(TMP "/unit_drafts/b.pxfd"); DeleteFileA(TMP "/unit_drafts/c.pxfd");
    CHECK(px_save_draft(&f, &info, TMP "/unit_drafts/a.pxfd", err) && px_save_draft(&f, NULL, TMP "/unit_drafts/b.pxfd", err), "two drafts saved");
    FILE *fp = fopen(TMP "/unit_drafts/c.pxfd", "wb");
    fputs("PXFEDRAFT\t3\nNAME\told\n", fp);
    fclose(fp);
    px_draft_info_free(&info);
    px_font_free(&f);
    CHECK(library_scan_drafts(TMP "/unit_drafts", &lib, err), "drafts scan");
    int ia = -1;
    for (int i = 0; i < lib.count; i++) if (strcmp(lib.e[i].name ? lib.e[i].name : "", "Duplicate of Test Sans") == 0) ia = i;
    CHECK(lib.count == 2 && ia >= 0, "two drafts listed");
    CHECK(lib.nerrors == 1 && strstr(lib.errors[0], "version 3 draft") != NULL, "a version 3 draft is skipped with a message that says so");
    if (ia >= 0) {
        LibEntry *e = &lib.e[ia];
        CHECK(e->kind == LIB_DRAFT && strcmp(e->pxfm, H_EXAMPLE) == 0 && strcmp(e->pxtf, T_SANS) == 0 && strcmp(e->typefaceName, "Test Sans") == 0 &&
              e->variant == 1 && e->typeface && e->tvariant && e->info.ready && e->info.glyphCount == 6 && e->info.above == 6 && e->info.below == 2 && e->fileSize > 0,
              "draft entry: name, original, typeface metadata, info");
    }
    library_free(&lib);
    CHECK(library_scan_drafts(TMP "/nonexistent-drafts", &lib, err) && lib.count == 0, "missing drafts folder: empty list");
    library_free(&lib);
    store_close(st);
}

/* the licence terms of a variant (spec "Licence terms") and licence texts (license.md) */
static void test_terms(void) {
#define TV(lines) ("0001\n" PXFM_LINE SRC lines)
    static const char full[] = "0001\n" PXFM_LINE SRC
        "\tTERMS-CLASS\tcc-by\n\tTERMS-COMMERCIAL\tyes\n\tTERMS-CONFIDENCE\thigh\n\tTERMS-DATE\t2026-11-21\n\tTERMS-LICENSE\tCC BY 4.0\n"
        "\tTERMS-OBLIGATION\tattribution\n\tTERMS-OBLIGATION\tinclude-license\n\tTERMS-PROVENANCE\toriginal\n\tTERMS-SPDX\tCC-BY-4.0\n"
        "\tTERMS-TEXT\t0123456789ABCDEF0123456789ABCDEF\n\tTERMS-TEXT\tFEDCBA9876543210FEDCBA9876543210\n";
    PxtfTypeface c;
    char err[PX_ERR_LEN] = "";
    bool ok = pxtf_parse(full, sizeof full - 1, &c, err);
    CHECK(ok, "terms: a variant with terms, two obligations and two licence texts parses");
    if (ok) {
        PxtfVariant *v = &c.v[0];
        const char *o[4];
        CHECK(strcmp(v->text[PXTF_TERMS_LICENSE], "CC BY 4.0") == 0 && strcmp(v->text[PXTF_TERMS_SPDX], "CC-BY-4.0") == 0, "terms: TERMS-LICENSE and TERMS-SPDX are text");
        CHECK(pxtf_opt(v, "TERMS-COMMERCIAL") && strcmp(pxtf_opt(v, "TERMS-COMMERCIAL"), "yes") == 0 && pxtf_opt(v, "TERMS-MODIFY") == NULL, "terms: pxtf_opt");
        CHECK(pxtf_opt_all(v, "TERMS-OBLIGATION", o, 4) == 2 && strcmp(o[1], "include-license") == 0 && pxtf_opt_all(v, "TERMS-TEXT", o, 4) == 2,
              "terms: pxtf_opt_all gives the repeated lines");
        CHECK(strcmp(pxtf_license_short(v), "CC-BY-4.0") == 0, "terms: the short licence is TERMS-SPDX first");
        size_t n;
        char *t = pxtf_text(&c, &n, NULL);
        CHECK(t && n == sizeof full - 1 && memcmp(t, full, n) == 0, "terms: written again is the same bytes");
        free(t);
        pxtf_free(&c);
    }
    CHECK(pxtf_parse(TV("\tTERMS-CLASS\tfree-personal\n"), strlen(TV("\tTERMS-CLASS\tfree-personal\n")), &c, err), "terms: TERMS-CLASS alone parses");
    if (c.count == 1) { CHECK(strcmp(pxtf_license_short(&c.v[0]), pxtf_terms_meaning("TERMS-CLASS", "free-personal")) == 0 && pxtf_license_short(&c.v[0])[0],
                              "terms: the short licence falls back to the class name"); pxtf_free(&c); }
    CHECK(strcmp(pxtf_terms_meaning("TERMS-COMMERCIAL", "ask"), "ask the holder") == 0 && strcmp(pxtf_terms_meaning("TERMS-NOPE", "x"), "x") == 0,
          "terms: plain meanings, the word itself when unknown");
    int nw = 0;
    CHECK(pxtf_terms_words("TERMS-CLASS", &nw) && nw == 15 && pxtf_terms_words("TERMS-OBLIGATION", &nw) && nw == 15 && pxtf_terms_words("TERMS-NOTE", &nw) == NULL,
          "terms: 15 classes, 15 obligations; a text key has no words");
    CHECK(pfails(TV("\tTERMS-COMMERCIAL\tmaybe\n"), "not one of its words"), "terms: an unknown word rejected");
    CHECK(pfails(TV("\tTERMS-DATE\t2026-02-30\n"), "not a date"), "terms: 30 February rejected");
    CHECK(pfails(TV("\tTERMS-DATE\t2026-1-02\n"), "not a date"), "terms: a short date rejected");
    CHECK(pfails(TV("\tTERMS-TEXT\t0123456789abcdef0123456789abcdef\n"), "upper-case"), "terms: a lower-case TERMS-TEXT rejected");
    CHECK(pfails(TV("\tTERMS-DERIVED-FROM\tX\n"), "need TERMS-PROVENANCE"), "terms: TERMS-DERIVED-FROM without derived provenance rejected");
    CHECK(pfails(TV("\tTERMS-PROVENANCE\toriginal\n\tTERMS-UPSTREAM-STATUS\tcleared\n"), "need TERMS-PROVENANCE"), "terms: TERMS-UPSTREAM-STATUS with original rejected");
    CHECK(!pfails(TV("\tTERMS-PROVENANCE\tpossibly-derived\n\tTERMS-UPSTREAM-HOLDER\tX\n"), NULL), "terms: TERMS-UPSTREAM-HOLDER with possibly-derived accepted");
    CHECK(pfails(TV("\tTERMS-COMMERCIAL\tyes\n\tTERMS-USE-LIMIT\tpersonal-only\n"), "TERMS-USE-LIMIT"), "terms: TERMS-USE-LIMIT with commercial yes rejected");
    CHECK(pfails(TV("\tTERMS-COMMERCIAL\tno\n\tTERMS-COMMERCIAL\tyes\n"), NULL), "terms: TERMS-COMMERCIAL twice rejected");
#undef TV

    size_t n = 0;
    const char *src = "\r\n  \nLine one  \t\r\r\n\nLine two\n\n\n";
    char *can = pxtf_license_canonical(src, strlen(src), &n, err);
    CHECK(can && n == 19 && memcmp(can, "Line one\n\nLine two\n", 19) == 0, "licence text: line ends, trailing spaces, empty lines folded");
    if (can && n != 19) printf("      got %zu: [%s]\n", n, can);
    free(can);
    can = pxtf_license_canonical("A", 1, &n, err);
    CHECK(can && n == 2 && memcmp(can, "A\n", 2) == 0, "licence text: LF after the last line");
    free(can);
    CHECK(!pxtf_license_canonical(" \n\t\n", 4, &n, err), "licence text: only white space rejected");
    CHECK(!pxtf_license_canonical("\xEF\xBB\xBFX", 4, &n, err) && strstr(err, "byte order"), "licence text: BOM rejected");
    CHECK(!pxtf_license_canonical("a\x01", 2, &n, err) && strstr(err, "control"), "licence text: a control character rejected");
    CHECK(!pxtf_license_canonical("e\xCC\x81", 3, &n, err) && strstr(err, "NFC"), "licence text: not NFC rejected");
    CHECK(!pxtf_license_canonical("\xC3(", 2, &n, err) && strstr(err, "UTF-8"), "licence text: bad UTF-8 rejected");
}

int main(void) {
    px_mkdir_p(TMP);
    test_escapes();
    test_example();
    test_roundtrip_variants();
    test_errors();
    test_terms();
    test_save();
    test_library();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
