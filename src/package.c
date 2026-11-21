/* package.c - font packages (pxfp) and typeface packages (pxtp).  See package.h. */
#include "package.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "pxtf.h"
#include "zlib/zlib.h"

/* ------------------------------------------------------------------ small helpers */

static bool fail(char err[PX_ERR_LEN], const char *fmt, ...) {
    if (!err) return false;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, PX_ERR_LEN, fmt, ap);
    va_end(ap);
    return false;
}

typedef struct Buf { unsigned char *p; size_t n, cap; bool oom; } Buf;

static void buf_add(Buf *b, const void *s, size_t n) {
    if (b->oom) return;
    if (b->n + n > b->cap) {
        size_t nc = b->cap ? b->cap : 4096;
        while (nc < b->n + n) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(b->p, nc);
        if (!np) { b->oom = true; return; }
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}
static void put16(Buf *b, unsigned v) { unsigned char t[2] = {(unsigned char)v, (unsigned char)(v >> 8)}; buf_add(b, t, 2); }
static void put32(Buf *b, uint32_t v) { unsigned char t[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; buf_add(b, t, 4); }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* a file of a package: its name in the zip and its bytes (owned) */
typedef struct Entry { char *name; unsigned char *data; size_t len; } Entry;
typedef struct EntryList { Entry *e; int n, cap; } EntryList;

static bool list_add(EntryList *l, const char *name, unsigned char *data, size_t len) {
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 64;
        Entry *ne = (Entry *)realloc(l->e, sizeof(Entry) * (size_t)nc);
        if (!ne) return false;
        l->e = ne;
        l->cap = nc;
    }
    char *nm = _strdup(name);
    if (!nm) return false;
    l->e[l->n].name = nm;
    l->e[l->n].data = data;
    l->e[l->n].len = len;
    l->n++;
    return true;
}
static void list_free(EntryList *l) {
    for (int i = 0; i < l->n; i++) { free(l->e[i].name); free(l->e[i].data); }
    free(l->e);
    memset(l, 0, sizeof *l);
}
static int find_entry(const EntryList *l, const char *name) {
    for (int i = 0; i < l->n; i++) if (strcmp(l->e[i].name, name) == 0) return i;
    return -1;
}
/* strcmp compares as unsigned char: the byte order of the names */
static int cmp_entry(const void *a, const void *b) { return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name); }

/* a sorted set of lower-case hashes */
typedef struct HSet { char (*h)[33]; int n, cap; } HSet;
static bool hset_add(HSet *s, const char *h) {
    if (s->n == s->cap) {
        int nc = s->cap ? s->cap * 2 : 64;
        char (*nh)[33] = (char (*)[33])realloc(s->h, 33 * (size_t)nc);
        if (!nh) return false;
        s->h = nh;
        s->cap = nc;
    }
    snprintf(s->h[s->n++], 33, "%s", h);
    return true;
}
static int cmp33(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void hset_sort(HSet *s) {
    if (s->n > 1) qsort(s->h, (size_t)s->n, 33, cmp33);
    int m = 0;
    for (int i = 0; i < s->n; i++) if (m == 0 || strcmp(s->h[m - 1], s->h[i]) != 0) memmove(s->h[m++], s->h[i], 33);
    s->n = m;
}
static bool hset_has(const HSet *s, const char *h) { return s->n > 0 && bsearch(h, s->h, (size_t)s->n, 33, cmp33) != NULL; }
static void hset_free(HSet *s) { free(s->h); memset(s, 0, sizeof *s); }

static bool is_lower_hash(const char *s, size_t n) {
    if (n != 32) return false;
    for (size_t i = 0; i < 32; i++) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

/* The pxgl hashes (field 5) and the palette hashes (field 6) of the GLYPH lines of a pxfm text, lower case, as the reference
 * reads them (pkgbase.glyph_hashes, palette_hashes).  The text is canonical (LF lines). */
static bool pxfm_names(const char *t, size_t len, HSet *glyphs, HSet *palettes) {
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && t[e] != '\n') e++;
        if (e - i > 6 && memcmp(t + i, "GLYPH\t", 6) == 0) {
            const char *f[8];
            size_t fl[8];
            int nf = 0;
            size_t s = i + 6;
            for (size_t k = s; k <= e && nf < 8; k++) {
                if (k == e || t[k] == '\t') { f[nf] = t + s; fl[nf] = k - s; nf++; s = k + 1; }
            }
            for (int q = 4; q <= 5 && q < nf; q++) {   /* code, advance, x, y, glyph file, palette */
                if (q == 5 && nf != 6) break;
                char h[33];
                if (fl[q] == 32) {
                    for (size_t k = 0; k < 32; k++) h[k] = (char)(f[q][k] >= 'A' && f[q][k] <= 'Z' ? f[q][k] + 32 : f[q][k]);
                    h[32] = 0;
                } else snprintf(h, sizeof h, "?%.*s", (int)(fl[q] < 31 ? fl[q] : 31), f[q]);   /* no hash: it matches no file */
                if (!hset_add(q == 4 ? glyphs : palettes, h)) return false;
            }
        }
        i = e + 1;
    }
    hset_sort(glyphs);
    hset_sort(palettes);
    return true;
}

/* ------------------------------------------------------------------ the zip file: writing */

static bool deflate9(const unsigned char *in, size_t n, unsigned char **out, size_t *outLen) {
    z_stream z;
    memset(&z, 0, sizeof z);
    if (deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    uLong bound = deflateBound(&z, (uLong)n);
    unsigned char *o = (unsigned char *)malloc(bound + 16);
    if (!o) { deflateEnd(&z); return false; }
    z.next_in = (Bytef *)in;
    z.avail_in = (uInt)n;
    z.next_out = o;
    z.avail_out = (uInt)(bound + 16);
    int rc = deflate(&z, Z_FINISH);
    *outLen = z.total_out;
    deflateEnd(&z);
    if (rc != Z_STREAM_END) { free(o); return false; }
    *out = o;
    return true;
}

/* The zip of the entries in the byte order of their names, as Python's zipfile writes it for pkgbase.zip_bytes. */
static unsigned char *zip_entries(EntryList *l, size_t *len, char err[PX_ERR_LEN]) {
    qsort(l->e, (size_t)l->n, sizeof(Entry), cmp_entry);
    Buf out = {0}, cd = {0};
    for (int i = 0; i < l->n; i++) {
        const Entry *e = &l->e[i];
        if (e->len > 0x7FFFFFFF || out.n > 0x7FFFFFFF) { free(out.p); free(cd.p); fail(err, "the package is too large"); return NULL; }
        unsigned char *z;
        size_t zl;
        if (!deflate9(e->data, e->len, &z, &zl)) { free(out.p); free(cd.p); fail(err, "deflate failed"); return NULL; }
        uint32_t crc = (uint32_t)crc32(0L, e->data, (uInt)e->len);
        size_t nl = strlen(e->name);
        uint32_t offset = (uint32_t)out.n;
        put32(&out, 0x04034b50);
        put16(&out, 20); put16(&out, 0); put16(&out, 8);       /* version needed, flags, deflate */
        put16(&out, 0); put16(&out, 0x21);                       /* 00:00:00, 1980-01-01 */
        put32(&out, crc); put32(&out, (uint32_t)zl); put32(&out, (uint32_t)e->len);
        put16(&out, (unsigned)nl); put16(&out, 0);
        buf_add(&out, e->name, nl);
        buf_add(&out, z, zl);
        free(z);
        put32(&cd, 0x02014b50);
        put16(&cd, 20 | 3 << 8);                                 /* made by: version 2.0, Unix */
        put16(&cd, 20); put16(&cd, 0); put16(&cd, 8);
        put16(&cd, 0); put16(&cd, 0x21);
        put32(&cd, crc); put32(&cd, (uint32_t)zl); put32(&cd, (uint32_t)e->len);
        put16(&cd, (unsigned)nl); put16(&cd, 0); put16(&cd, 0); /* name, extra, comment lengths */
        put16(&cd, 0); put16(&cd, 0);                            /* disk, internal attributes */
        put32(&cd, (uint32_t)0100644 << 16);                     /* a regular file, mode 0644 */
        put32(&cd, offset);
        buf_add(&cd, e->name, nl);
    }
    uint32_t cdOffset = (uint32_t)out.n;
    buf_add(&out, cd.p, cd.n);
    put32(&out, 0x06054b50);
    put16(&out, 0); put16(&out, 0);
    put16(&out, (unsigned)l->n); put16(&out, (unsigned)l->n);
    put32(&out, (uint32_t)cd.n); put32(&out, cdOffset);
    put16(&out, 0);
    free(cd.p);
    if (out.oom || cd.oom || l->n > 0xFFFF) { free(out.p); fail(err, out.oom || cd.oom ? "out of memory" : "too many files for a zip file"); return NULL; }
    *len = out.n;
    return out.p;
}

/* ------------------------------------------------------------------ the zip file: reading */

static bool inflate_raw(const unsigned char *in, size_t n, unsigned char *out, size_t outLen) {
    z_stream z;
    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) != Z_OK) return false;
    z.next_in = (Bytef *)in;
    z.avail_in = (uInt)n;
    unsigned char dummy;
    z.next_out = outLen ? out : &dummy;
    z.avail_out = (uInt)outLen;
    int rc = inflate(&z, Z_FINISH);
    bool ok = rc == Z_STREAM_END && z.total_out == outLen;
    inflateEnd(&z);
    return ok;
}

/* The entries of a zip file by its central directory, after the checks of the zip file itself (pkgbase.read_files). */
static bool read_zip(const unsigned char *p, size_t len, EntryList *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    if (len < 22) return fail(err, "not a zip file");
    size_t eocd = (size_t)-1;
    size_t lowest = len > 22 + 65535 ? len - 22 - 65535 : 0;
    for (size_t i = len - 22 + 1; i-- > lowest;) {
        if (get32(p + i) == 0x06054b50 && i + 22 + get16(p + i + 20) <= len) { eocd = i; break; }
    }
    if (eocd == (size_t)-1) return fail(err, "not a zip file (no end of central directory)");
    const unsigned char *e = p + eocd;
    unsigned count = get16(e + 10);
    uint32_t cdSize = get32(e + 12), cdOff = get32(e + 16);
    if (get16(e + 4) != 0 || get16(e + 6) != 0 || get16(e + 8) != count) return fail(err, "not a zip file (a zip file of several disks)");
    if (count == 0xFFFF || cdOff == 0xFFFFFFFF || cdSize == 0xFFFFFFFF) return fail(err, "a zip64 file: too large for a package");
    if ((size_t)cdOff + cdSize > eocd) return fail(err, "not a zip file (the central directory is out of the file)");
    size_t q = cdOff;
    for (unsigned i = 0; i < count; i++) {
        if (q + 46 > (size_t)cdOff + cdSize || get32(p + q) != 0x02014b50) { list_free(out); return fail(err, "not a zip file (a bad central directory)"); }
        unsigned flags = get16(p + q + 8), method = get16(p + q + 10);
        uint32_t crc = get32(p + q + 16), csize = get32(p + q + 20), usize = get32(p + q + 24);
        unsigned nl = get16(p + q + 28), xl = get16(p + q + 30), cl = get16(p + q + 32);
        uint32_t lho = get32(p + q + 42);
        if (q + 46 + nl + xl + cl > (size_t)cdOff + cdSize) { list_free(out); return fail(err, "not a zip file (a bad central directory)"); }
        char name[1024];
        if (nl == 0 || nl >= sizeof name) { list_free(out); return fail(err, "an entry with an empty or too long name"); }
        memcpy(name, p + q + 46, nl);
        name[nl] = 0;
        q += 46 + (size_t)nl + xl + cl;
        if (memchr(name, 0, nl)) { list_free(out); return fail(err, "an entry name with a NUL byte"); }
        if (name[nl - 1] == '/') { list_free(out); return fail(err, "%s: a folder entry", name); }
        if (find_entry(out, name) >= 0) { list_free(out); return fail(err, "%s: twice", name); }
        bool dots = false;
        for (const char *s = name; *s;) {
            const char *t = strchr(s, '/');
            size_t sl = t ? (size_t)(t - s) : strlen(s);
            if (sl == 2 && s[0] == '.' && s[1] == '.') dots = true;
            s += sl + (t ? 1 : 0);
        }
        if (strchr(name, '\\') || name[0] == '/' || dots) { list_free(out); return fail(err, "%s: not a plain relative name", name); }
        if (flags & 1) { list_free(out); return fail(err, "%s: encrypted", name); }
        if (method != 0 && method != 8) { list_free(out); return fail(err, "%s: compression method %u (only stored and deflated entries are read)", name, method); }
        if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF || lho == 0xFFFFFFFF) { list_free(out); return fail(err, "%s: a zip64 entry: too large for a package", name); }
        if ((size_t)lho + 30 > len || get32(p + lho) != 0x04034b50) { list_free(out); return fail(err, "%s: not a zip file (a bad local header)", name); }
        unsigned lnl = get16(p + lho + 26), lxl = get16(p + lho + 28);
        if (lnl != nl || (size_t)lho + 30 + lnl > len || memcmp(p + lho + 30, name, nl) != 0) { list_free(out); return fail(err, "%s: the name in the directory and in the local header differ", name); }
        size_t dataAt = (size_t)lho + 30 + lnl + lxl;
        if (dataAt + csize > len) { list_free(out); return fail(err, "%s: not a zip file (the data is out of the file)", name); }
        if (usize > 64u * 1024 * 1024) { list_free(out); return fail(err, "%s: too large for a library file", name); }
        unsigned char *data = (unsigned char *)malloc(usize + 1);
        if (!data) { list_free(out); return fail(err, "out of memory"); }
        bool ok = method == 0 ? (csize == usize && (memcpy(data, p + dataAt, usize), true)) : inflate_raw(p + dataAt, csize, data, usize);
        if (!ok) { free(data); list_free(out); return fail(err, "%s: not a zip file (bad compressed data)", name); }
        if ((uint32_t)crc32(0L, data, (uInt)usize) != crc) { free(data); list_free(out); return fail(err, "%s: not a zip file (bad CRC-32)", name); }
        data[usize] = 0;
        if (!list_add(out, name, data, usize)) { free(data); list_free(out); return fail(err, "out of memory"); }
    }
    return true;
}

/* ------------------------------------------------------------------ checking a package */

typedef struct Checked {
    PkgKind kind;
    char pxtf[33];
    StoreFile *files; int nFiles;   /* canonical texts (owned by `texts`) */
    char **texts;
    HSet pxfm;
    int nGlyphs, nPalettes;
} Checked;

static void checked_free(Checked *c) {
    for (int i = 0; i < c->nFiles; i++) free(c->texts[i]);
    free(c->texts);
    free(c->files);
    hset_free(&c->pxfm);
    memset(c, 0, sizeof *c);
}

/* the file kinds of the folders of a package */
static const char *const FOLDER[4] = {"pxgl", "pxfm", "pxtf", "palette"};
static const char *const EXT[4] = {".pxgl", ".pxfm", ".pxtf", ".hex"};

/* Checks the entries of a package as pkgbase.check does, then, when `strict`, the files with the store's strict readers
 * (store_add_files without a store; an import leaves them to its own store_add_files, which reads them first).  The kind
 * is decided by the file at the root. */
static bool check_package(const EntryList *l, bool strict, Checked *out, char err[PX_ERR_LEN]) {
    memset(out, 0, sizeof *out);
    int root = -1, nRoot = 0, nInPxfm = 0;
    int *kindOf = (int *)malloc(sizeof(int) * (size_t)(l->n ? l->n : 1));
    HSet named = {0}, needG = {0}, needP = {0}, haveG = {0}, haveP = {0}, haveF = {0};
    bool ok = false;
    if (!kindOf) return fail(err, "out of memory");
    for (int i = 0; i < l->n; i++) {
        const char *nm = l->e[i].name, *slash = strchr(nm, '/');
        if (!slash) { kindOf[i] = -1; root = i; nRoot++; continue; }
        size_t fl = (size_t)(slash - nm);
        int k = -1;
        for (int q = 0; q < 4; q++) if (q != STORE_FILE_PXTF && strlen(FOLDER[q]) == fl && memcmp(nm, FOLDER[q], fl) == 0) k = q;
        if (k < 0 || !slash[1] || strchr(slash + 1, '/')) { fail(err, "%s: not at the root, in pxfm/, pxgl/ or palette/", nm); goto done; }
        kindOf[i] = k;
        if (k == STORE_FILE_PXFM) nInPxfm++;
    }
    if (nRoot != 1) { fail(err, "%d files at the root, not one .pxfm or .pxtf file", nRoot); goto done; }
    {
        const char *rn = l->e[root].name;
        size_t rl = strlen(rn);
        if (rl == 37 && strcmp(rn + 32, ".pxfm") == 0 && is_lower_hash(rn, 32)) out->kind = PKG_FONT;
        else if (rl == 37 && strcmp(rn + 32, ".pxtf") == 0 && is_lower_hash(rn, 32)) out->kind = PKG_TYPEFACE;
        else { fail(err, "%s: not <hash>.pxfm (a font package) or <hash>.pxtf (a typeface package)", rn); goto done; }
        kindOf[root] = out->kind == PKG_FONT ? STORE_FILE_PXFM : STORE_FILE_PXTF;
    }
    if (out->kind == PKG_FONT && nInPxfm) { fail(err, "a pxfm/ folder in a font package"); goto done; }
    if (out->kind == PKG_TYPEFACE && !nInPxfm) { fail(err, "no pxfm/ folder in a typeface package"); goto done; }
    out->files = (StoreFile *)calloc((size_t)l->n, sizeof(StoreFile));
    out->texts = (char **)calloc((size_t)l->n, sizeof(char *));
    if (!out->files || !out->texts) { fail(err, "out of memory"); goto done; }
    /* every name is the hash of the canonical form of the content */
    for (int i = 0; i < l->n; i++) {
        const Entry *e = &l->e[i];
        int k = kindOf[i];
        const char *leaf = i == root ? e->name : strchr(e->name, '/') + 1;
        size_t ll = strlen(leaf), xl = strlen(EXT[k]);
        if (ll != 32 + xl || strcmp(leaf + 32, EXT[k]) != 0 || !is_lower_hash(leaf, 32)) { fail(err, "%s: not <hash>%s", e->name, EXT[k]); goto done; }
        if (k != STORE_FILE_PXTF) {
            for (size_t q = 0; q < e->len; q++) if (e->data[q] >= 0x80) { fail(err, "%s: not ASCII text", e->name); goto done; }
        }
        size_t cl;
        char *can = px_canonical((const char *)e->data, e->len, &cl);
        if (!can) { fail(err, "out of memory"); goto done; }
        StoreFile *f = &out->files[out->nFiles];
        out->texts[out->nFiles] = can;
        out->nFiles++;
        f->kind = k;
        f->data = can;
        f->len = cl;
        px_hash128_hex(can, cl, f->hash, false);
        if (memcmp(f->hash, leaf, 32) != 0) { fail(err, "%s: its hash is %s", e->name, f->hash); goto done; }
        HSet *have = k == STORE_FILE_PXGL ? &haveG : k == STORE_FILE_PALETTE ? &haveP : k == STORE_FILE_PXFM ? &haveF : NULL;
        if (have && !hset_add(have, f->hash)) { fail(err, "out of memory"); goto done; }
    }
    hset_sort(&haveG); hset_sort(&haveP); hset_sort(&haveF);
    if (out->kind == PKG_TYPEFACE) {   /* the pxtf names exactly the fonts of pxfm/ */
        const StoreFile *tf = NULL;
        for (int i = 0; i < out->nFiles; i++) if (out->files[i].kind == STORE_FILE_PXTF) tf = &out->files[i];
        PxtfTypeface t;
        pxtf_init(&t);
        char perr[PX_ERR_LEN];
        if (!pxtf_parse(tf->data, tf->len, &t, perr)) { pxtf_free(&t); fail(err, "%s.pxtf: %s", tf->hash, perr); goto done; }
        for (int v = 0; v < t.count; v++) if (!hset_add(&named, t.v[v].pxfm)) { pxtf_free(&t); fail(err, "out of memory"); goto done; }
        pxtf_free(&t);
        hset_sort(&named);
        snprintf(out->pxtf, sizeof out->pxtf, "%s", tf->hash);
        for (int i = 0; i < named.n; i++) if (!hset_has(&haveF, named.h[i])) { fail(err, "the pxfm file %s, named by the pxtf file, is not in the package", named.h[i]); goto done; }
        for (int i = 0; i < haveF.n; i++) if (!hset_has(&named, haveF.h[i])) { fail(err, "the pxfm file %s is not named by the pxtf file", haveF.h[i]); goto done; }
    }
    for (int i = 0; i < out->nFiles; i++) {   /* the pxgl and palette files are exactly those the fonts name */
        if (out->files[i].kind != STORE_FILE_PXFM) continue;
        if (!pxfm_names(out->files[i].data, out->files[i].len, &needG, &needP)) { fail(err, "out of memory"); goto done; }
    }
    for (int i = 0; i < needP.n; i++) if (!hset_has(&haveP, needP.h[i])) { fail(err, "the palette file %s, named by a pxfm file, is not in the package", needP.h[i]); goto done; }
    for (int i = 0; i < haveP.n; i++) if (!hset_has(&needP, haveP.h[i])) { fail(err, "the palette file %s is named by no pxfm file", haveP.h[i]); goto done; }
    for (int i = 0; i < needG.n; i++) if (!hset_has(&haveG, needG.h[i])) { fail(err, "the pxgl file %s, named by a pxfm file, is not in the package", needG.h[i]); goto done; }
    for (int i = 0; i < haveG.n; i++) if (!hset_has(&needG, haveG.h[i])) { fail(err, "the pxgl file %s is named by no pxfm file", haveG.h[i]); goto done; }
    /* every file with the strict readers, the package as a library of its own */
    if (strict && !store_add_files(NULL, out->files, out->nFiles, NULL, NULL, err)) goto done;
    out->pxfm = haveF;
    memset(&haveF, 0, sizeof haveF);
    out->nGlyphs = haveG.n;
    out->nPalettes = haveP.n;
    ok = true;
done:
    free(kindOf);
    hset_free(&named); hset_free(&needG); hset_free(&needP); hset_free(&haveG); hset_free(&haveP); hset_free(&haveF);
    if (!ok) checked_free(out);
    return ok;
}

static void fill_result(const Checked *c, PkgImportResult *res) {
    if (!res) return;
    res->kind = c->kind;
    snprintf(res->pxtf, sizeof res->pxtf, "%s", c->pxtf);
    res->nPxfm = c->pxfm.n;
    res->pxfm = (char (*)[33])malloc(33 * (size_t)(c->pxfm.n ? c->pxfm.n : 1));
    if (res->pxfm) memcpy(res->pxfm, c->pxfm.h, 33 * (size_t)c->pxfm.n);
    else res->nPxfm = 0;
    res->nGlyphs = c->nGlyphs;
    res->nPalettes = c->nPalettes;
}

static bool check_or_import(Store *s, bool add, const unsigned char *zip, size_t len, PkgImportResult *res, char err[PX_ERR_LEN]) {
    if (res) memset(res, 0, sizeof *res);
    EntryList l;
    if (!read_zip(zip, len, &l, err)) return false;
    Checked c;
    bool ok = check_package(&l, !add, &c, err);
    list_free(&l);
    if (!ok) return false;
    StoreCounts added = {0, 0, 0, 0}, skipped = {0, 0, 0, 0};
    if (add) ok = store_add_files(s, c.files, c.nFiles, &added, &skipped, err);
    if (ok) {
        fill_result(&c, res);
        if (res) { res->added = added; res->skipped = skipped; }
    }
    checked_free(&c);
    return ok;
}

bool pkg_check_bytes(const unsigned char *zip, size_t len, PkgImportResult *res, char err[PX_ERR_LEN]) {
    return check_or_import(NULL, false, zip, len, res, err);
}

bool pkg_import_bytes(Store *s, const unsigned char *zip, size_t len, PkgImportResult *res, char err[PX_ERR_LEN]) {
    if (!s) return fail(err, "no store");
    return check_or_import(s, true, zip, len, res, err);
}

static bool file_op(Store *s, bool add, const char *path, PkgImportResult *res, char err[PX_ERR_LEN]) {
    if (res) memset(res, 0, sizeof *res);
    size_t n;
    char *data = px_read_file(path, &n);
    if (!data) return fail(err, "%s: cannot read", path);
    bool ok = check_or_import(s, add, (const unsigned char *)data, n, res, err);
    free(data);
    return ok;
}

bool pkg_check(const char *path, PkgImportResult *res, char err[PX_ERR_LEN]) { return file_op(NULL, false, path, res, err); }

bool pkg_import(Store *s, const char *path, PkgImportResult *res, char err[PX_ERR_LEN]) {
    if (!s) return fail(err, "no store");
    return file_op(s, true, path, res, err);
}

void pkg_result_free(PkgImportResult *r) {
    if (!r) return;
    free(r->pxfm);
    memset(r, 0, sizeof *r);
}

/* ------------------------------------------------------------------ writing a package */

/* the text of a file of the store, checked against its hash; added to the entries under `name` */
static bool add_store_file(StoreConn *c, int kind, const char *hash, const char *name, EntryList *l, char err[PX_ERR_LEN]) {
    if (find_entry(l, name) >= 0) return true;
    size_t len = 0;
    char got[33] = "";
    char *t = kind == STORE_FILE_PXGL ? store_pxgl_text(c, hash, &len, got) : kind == STORE_FILE_PXFM ? store_pxfm_text(c, hash, &len, got)
            : kind == STORE_FILE_PXTF ? store_pxtf_text(c, hash, &len, got) : store_palette_text(c, hash, &len, got);
    if (!t) return fail(err, "the %s file %s is not in the library", FOLDER[kind], hash);
    if (strcmp(got, hash) != 0) { free(t); return fail(err, "the %s file %s made from the rows has the hash %s", FOLDER[kind], hash, got); }
    if (!list_add(l, name, (unsigned char *)t, len)) { free(t); return fail(err, "out of memory"); }
    return true;
}

/* a font: its pxfm file under `name`, and the glyph and palette files it names */
static bool add_font(StoreConn *c, const char *hash, const char *name, EntryList *l, char err[PX_ERR_LEN]) {
    if (!add_store_file(c, STORE_FILE_PXFM, hash, name, l, err)) return false;
    const Entry *e = &l->e[find_entry(l, name)];
    HSet g = {0}, p = {0};
    bool ok = pxfm_names((const char *)e->data, e->len, &g, &p);
    if (!ok) fail(err, "out of memory");
    char nm[64];
    for (int i = 0; i < g.n && ok; i++) {
        if (!is_lower_hash(g.h[i], strlen(g.h[i]))) { ok = fail(err, "the pxfm file %s names a glyph that is no hash", hash); break; }
        snprintf(nm, sizeof nm, "pxgl/%s.pxgl", g.h[i]);
        ok = add_store_file(c, STORE_FILE_PXGL, g.h[i], nm, l, err);
    }
    for (int i = 0; i < p.n && ok; i++) {
        if (!is_lower_hash(p.h[i], strlen(p.h[i]))) { ok = fail(err, "the pxfm file %s names a palette that is no hash", hash); break; }
        snprintf(nm, sizeof nm, "palette/%s.hex", p.h[i]);
        ok = add_store_file(c, STORE_FILE_PALETTE, p.h[i], nm, l, err);
    }
    hset_free(&g);
    hset_free(&p);
    return ok;
}

static bool lower_hash_arg(const char *in, char out[33], char err[PX_ERR_LEN]) {
    if (!in || !px_is_hash(in, true, true)) return fail(err, "not a hash: %s", in ? in : "(null)");
    for (int i = 0; i < 32; i++) out[i] = (char)(in[i] >= 'A' && in[i] <= 'F' ? in[i] + 32 : in[i]);
    out[32] = 0;
    return true;
}

unsigned char *pkg_font_bytes(StoreConn *c, const char *pxfmHash, size_t *len, char err[PX_ERR_LEN]) {
    char h[33], name[64];
    if (!lower_hash_arg(pxfmHash, h, err)) return NULL;
    EntryList l = {0};
    snprintf(name, sizeof name, "%s.pxfm", h);
    unsigned char *z = add_font(c, h, name, &l, err) ? zip_entries(&l, len, err) : NULL;
    list_free(&l);
    return z;
}

unsigned char *pkg_typeface_bytes(StoreConn *c, const char *pxtfHash, size_t *len, char err[PX_ERR_LEN]) {
    char h[33], name[64];
    if (!lower_hash_arg(pxtfHash, h, err)) return NULL;
    EntryList l = {0};
    snprintf(name, sizeof name, "%s.pxtf", h);
    unsigned char *z = NULL;
    if (add_store_file(c, STORE_FILE_PXTF, h, name, &l, err)) {
        const Entry *e = &l.e[0];
        PxtfTypeface t;
        pxtf_init(&t);
        char perr[PX_ERR_LEN];
        bool ok = pxtf_parse((const char *)e->data, e->len, &t, perr);
        if (!ok) fail(err, "the pxtf file %s: %s", h, perr);
        HSet fonts = {0};
        for (int v = 0; v < t.count && ok; v++) if (!hset_add(&fonts, t.v[v].pxfm)) ok = fail(err, "out of memory");
        pxtf_free(&t);
        hset_sort(&fonts);
        for (int i = 0; i < fonts.n && ok; i++) {
            snprintf(name, sizeof name, "pxfm/%s.pxfm", fonts.h[i]);
            ok = add_font(c, fonts.h[i], name, &l, err);
        }
        hset_free(&fonts);
        if (ok) z = zip_entries(&l, len, err);
    }
    list_free(&l);
    return z;
}

/* write to path.tmp, then move it over path */
static bool write_replace(const char *path, const unsigned char *data, size_t len, char err[PX_ERR_LEN]) {
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return fail(err, "%s: cannot write", path);
    bool w = fwrite(data, 1, len, f) == len;
    w = fclose(f) == 0 && w;
    if (!w || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp);
        return fail(err, "%s: cannot write", path);
    }
    return true;
}

bool pkg_write_font(StoreConn *c, const char *pxfmHash, const char *path, char err[PX_ERR_LEN]) {
    size_t n;
    unsigned char *z = pkg_font_bytes(c, pxfmHash, &n, err);
    if (!z) return false;
    bool ok = write_replace(path, z, n, err);
    free(z);
    return ok;
}

bool pkg_write_typeface(StoreConn *c, const char *pxtfHash, const char *path, char err[PX_ERR_LEN]) {
    size_t n;
    unsigned char *z = pkg_typeface_bytes(c, pxtfHash, &n, err);
    if (!z) return false;
    bool ok = write_replace(path, z, n, err);
    free(z);
    return ok;
}

/* ------------------------------------------------------------------ names */

/* store.slug of the reference: lower case, every run of characters other than a-z 0-9 one hyphen, none at the ends.  The
 * value is UTF-8; a non-ASCII character is never a-z 0-9. */
static void slug(const char *s, char *out, size_t cap) {
    size_t n = 0;
    bool pend = false;
    if (cap == 0) return;
    for (; s && *s; s++) {
        char ch = *s;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            if (pend && n > 0 && n + 1 < cap) out[n++] = '-';
            pend = false;
            if (n + 1 < cap) out[n++] = ch;
        } else pend = true;
    }
    out[n] = 0;
}

void pkg_default_name_font(StoreConn *c, const char *pxfmHash, char *out, size_t cap) {
    char h[33], err[PX_ERR_LEN], name[600] = "";
    if (!lower_hash_arg(pxfmHash, h, err)) { snprintf(out, cap, "font%s", PKG_FONT_EXT); return; }
    StoreEntryList vl;
    if (store_list_typefaces(c, &vl, err)) {   /* per variant, by pxtf hash, then number */
        for (int i = 0; i < vl.count; i++) {
            if (strcmp(vl.e[i].pxfm, h) != 0) continue;
            PxtfTypeface t;
            pxtf_init(&t);
            if (store_load_typeface(c, vl.e[i].pxtf, &t, err)) {
                PxtfVariant *v = pxtf_find_variant(&t, vl.e[i].variant);
                if (v) {
                    char fam[300], sty[300];
                    slug(t.name, fam, sizeof fam);
                    slug(v->text[PXTF_STYLE], sty, sizeof sty);
                    snprintf(name, sizeof name, "%s%s%s", fam, fam[0] && sty[0] ? "-" : "", sty);
                }
            }
            pxtf_free(&t);
            break;
        }
        store_entry_list_free(&vl);
    }
    snprintf(out, cap, "%s%s", name[0] ? name : h, PKG_FONT_EXT);
}

void pkg_default_name_typeface(StoreConn *c, const char *pxtfHash, char *out, size_t cap) {
    char h[33], err[PX_ERR_LEN], fam[300] = "";
    if (lower_hash_arg(pxtfHash, h, err)) {
        PxtfTypeface t;
        pxtf_init(&t);
        if (store_load_typeface(c, h, &t, err)) slug(t.name, fam, sizeof fam);   /* the NAME top line */
        pxtf_free(&t);
    }
    snprintf(out, cap, "%s%s", fam[0] ? fam : "typeface", PKG_TYPEFACE_EXT);
}

/* pkgbase.write_zip: the name, or with -<8 hex> when a file of the name is there with other bytes */
static bool export_bytes(const unsigned char *z, size_t n, const char *folder, const char *name, const char *hash,
                         char *pathOut, size_t cap, char err[PX_ERR_LEN]) {
    char path[1200];
    if (!px_mkdir_p(folder)) return fail(err, "%s: cannot make the folder", folder);
    snprintf(path, sizeof path, "%s/%s", folder, name);
    size_t ol;
    char *old = px_read_file(path, &ol);
    if (old && (ol != n || memcmp(old, z, n) != 0)) {
        const char *dot = strrchr(name, '.');
        snprintf(path, sizeof path, "%s/%.*s-%.8s%s", folder, (int)(dot - name), name, hash, dot);
    }
    free(old);
    if (!write_replace(path, z, n, err)) return false;
    if (pathOut && cap) snprintf(pathOut, cap, "%s", path);
    return true;
}

bool pkg_export_font(StoreConn *c, const char *pxfmHash, const char *folder, char *pathOut, size_t cap, char err[PX_ERR_LEN]) {
    size_t n;
    unsigned char *z = pkg_font_bytes(c, pxfmHash, &n, err);
    if (!z) return false;
    char name[700], h[33];
    lower_hash_arg(pxfmHash, h, err);
    pkg_default_name_font(c, h, name, sizeof name);
    bool ok = export_bytes(z, n, folder, name, h, pathOut, cap, err);
    free(z);
    return ok;
}

bool pkg_export_typeface(StoreConn *c, const char *pxtfHash, const char *folder, char *pathOut, size_t cap, char err[PX_ERR_LEN]) {
    size_t n;
    unsigned char *z = pkg_typeface_bytes(c, pxtfHash, &n, err);
    if (!z) return false;
    char name[700], h[33];
    lower_hash_arg(pxtfHash, h, err);
    pkg_default_name_typeface(c, h, name, sizeof name);
    bool ok = export_bytes(z, n, folder, name, h, pathOut, cap, err);
    free(z);
    return ok;
}
