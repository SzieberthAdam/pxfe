/* test_selection.c - tests of src/selection.c (the cell mask of the glyph editor's selection tools).  build.bat test runs it. */
#include "selection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

static SelMask rect(int x0, int y0, int x1, int y1) { SelMask s; memset(&s, 0, sizeof s); sel_set_rect(&s, x0, y0, x1, y1); return s; }

static void test_rect_and_get(void) {
    SelMask s = rect(5, 3, 2, 1);   /* corners in any order */
    CHECK(s.x == 2 && s.y == 1 && s.w == 4 && s.h == 3, "rect: box from swapped corners");
    CHECK(sel_count(&s) == 12, "rect: 12 cells");
    CHECK(sel_get(&s, 2, 1) && sel_get(&s, 5, 3) && !sel_get(&s, 6, 3) && !sel_get(&s, 1, 1) && !sel_get(&s, 3, 4), "rect: get inside and outside");
    sel_shift(&s, -3, 2);
    CHECK(sel_get(&s, -1, 3) && !sel_get(&s, 2, 1), "rect: shift moves the cells");
    sel_free(&s);
    CHECK(sel_empty(&s) && sel_count(&s) == 0 && !sel_get(&s, 0, 0), "free: empty");
}

static void test_combine(void) {
    SelMask a = rect(0, 0, 3, 3), b = rect(2, 2, 5, 5), c;
    memset(&c, 0, sizeof c);
    sel_copy(&c, &a);
    sel_combine(&c, &b, SEL_ADD);
    CHECK(sel_count(&c) == 16 + 16 - 4 && c.x == 0 && c.w == 6, "add: union of two overlapping squares");
    sel_copy(&c, &a);
    sel_combine(&c, &b, SEL_SUB);
    CHECK(sel_count(&c) == 12 && !sel_get(&c, 3, 3) && sel_get(&c, 1, 1) && c.w == 4, "subtract: the overlap goes");
    sel_copy(&c, &a);
    sel_combine(&c, &b, SEL_INTERSECT);
    CHECK(sel_count(&c) == 4 && c.x == 2 && c.y == 2 && c.w == 2 && c.h == 2, "intersect: tight box of the overlap");
    sel_copy(&c, &a);
    sel_combine(&c, &b, SEL_REPLACE);
    CHECK(sel_count(&c) == 16 && c.x == 2, "replace: the new shape only");
    /* the result of a subtract is trimmed */
    SelMask big = rect(0, 0, 9, 9), cut = rect(0, 0, 9, 4);
    sel_combine(&big, &cut, SEL_SUB);
    CHECK(big.y == 5 && big.h == 5 && big.w == 10, "subtract: box shrinks to what is left");
    /* subtract everything, intersect with nothing */
    SelMask all = rect(0, 0, 2, 2), same = rect(0, 0, 2, 2);
    sel_combine(&all, &same, SEL_SUB);
    CHECK(sel_empty(&all), "subtract: all of it leaves nothing");
    SelMask d = rect(0, 0, 2, 2), far = rect(10, 10, 12, 12);
    sel_combine(&d, &far, SEL_INTERSECT);
    CHECK(sel_empty(&d), "intersect: disjoint shapes leave nothing");
    SelMask e;
    memset(&e, 0, sizeof e);
    sel_combine(&e, &a, SEL_ADD);
    CHECK(sel_count(&e) == 16, "add: to an empty selection");
    sel_combine(&e, &a, SEL_SUB);
    sel_combine(&e, &a, SEL_SUB);
    CHECK(sel_empty(&e), "subtract: from an empty selection");
    sel_free(&a); sel_free(&b); sel_free(&c); sel_free(&big); sel_free(&cut); sel_free(&all); sel_free(&same); sel_free(&d); sel_free(&far); sel_free(&e);
}

static void test_invert(void) {
    SelMask s = rect(1, 1, 2, 2);
    sel_invert(&s, 0, 0, 3, 3);
    CHECK(sel_count(&s) == 12 && !sel_get(&s, 1, 1) && sel_get(&s, 0, 0) && sel_get(&s, 3, 3), "invert: the other 12 cells of a 4x4 area");
    sel_invert(&s, 0, 0, 3, 3);
    CHECK(sel_count(&s) == 4 && sel_get(&s, 1, 1) && sel_get(&s, 2, 2), "invert twice: back");
    SelMask o = rect(10, 10, 11, 11);
    sel_invert(&o, 0, 0, 3, 3);
    CHECK(sel_count(&o) == 16 && !sel_get(&o, 10, 10), "invert: cells outside the area are dropped");
    SelMask n;
    memset(&n, 0, sizeof n);
    sel_invert(&n, 0, 0, 2, 2);
    CHECK(sel_count(&n) == 9, "invert: nothing selected becomes everything");
    sel_free(&s); sel_free(&o); sel_free(&n);
}

static void test_polygon(void) {
    SelMask p;
    memset(&p, 0, sizeof p);
    int sq[] = {1, 1, 6, 1, 6, 4, 1, 4};
    sel_polygon(&p, sq, 4);
    CHECK(p.x == 1 && p.y == 1 && p.w == 6 && p.h == 4 && sel_count(&p) == 24, "polygon: a rectangle fills its box");
    int tri[] = {0, 0, 8, 0, 0, 8};
    sel_polygon(&p, tri, 3);
    CHECK(sel_get(&p, 1, 1) && sel_get(&p, 6, 0) && !sel_get(&p, 7, 7) && !sel_get(&p, 6, 6), "polygon: a triangle: inside, outside");
    CHECK(sel_get(&p, 0, 8) && sel_get(&p, 8, 0), "polygon: the outline's own cells are in");
    int line[] = {0, 0, 5, 0};
    sel_polygon(&p, line, 2);
    CHECK(sel_count(&p) == 6, "polygon: a line selects its cells");
    int one[] = {3, 4};
    sel_polygon(&p, one, 1);
    CHECK(sel_count(&p) == 1 && sel_get(&p, 3, 4), "polygon: one point is one cell");
    int ring[] = {0, 0, 4, 0, 4, 4, 0, 4, 0, 1, 3, 1, 3, 3, 1, 3, 1, 0};   /* self-touching path: even-odd */
    sel_polygon(&p, ring, 9);
    CHECK(sel_count(&p) > 0 && sel_get(&p, 0, 0), "polygon: a self-touching path still gives a mask");
    sel_polygon(&p, NULL, 0);
    CHECK(sel_empty(&p), "polygon: no points, no cells");
    sel_free(&p);
}

static SelBitmap bm(char *pix, int x, int y, int w, int h) { SelBitmap b; b.pix = pix; b.x = x; b.y = y; b.w = w; b.h = h; return b; }

static void test_wand(void) {
    char pix[] = "0.0."
                 "00.0"
                 "..00"
                 "0..0";
    SelBitmap b = bm(pix, -2, 5, 4, 4);
    SelMask s;
    memset(&s, 0, sizeof s);
    sel_wand(&s, &b, -2, 5, false);
    CHECK(sel_count(&s) == 3 && sel_get(&s, -2, 5) && sel_get(&s, -2, 6) && sel_get(&s, -1, 6) && !sel_get(&s, 0, 5), "wand: 4-connectivity leaves the diagonal cell");
    sel_wand(&s, &b, -1, 6, true);
    CHECK(sel_count(&s) == 8 && sel_get(&s, 0, 5) && sel_get(&s, 1, 6) && sel_get(&s, 1, 8) && !sel_get(&s, -2, 8), "wand: 8-connectivity joins the diagonal neighbours");
    sel_wand(&s, &b, 0, 7, false);
    CHECK(sel_count(&s) == 4 && sel_get(&s, 0, 7) && sel_get(&s, 1, 7) && sel_get(&s, 1, 6) && sel_get(&s, 1, 8), "wand: a second ink group");
    sel_wand(&s, &b, -1, 5, false);
    CHECK(sel_count(&s) == 1 && sel_get(&s, -1, 5), "wand: an empty cell is a group of empty cells");
    sel_wand(&s, &b, -2, 7, false);
    CHECK(sel_count(&s) == 4 && sel_get(&s, -2, 7) && sel_get(&s, -1, 7) && sel_get(&s, -1, 8) && sel_get(&s, 0, 8), "wand: a bigger empty group");
    sel_wand(&s, &b, 50, 50, false);
    CHECK(sel_empty(&s), "wand: outside the region selects nothing");
    char one[] = "..";
    SelBitmap o = bm(one, 0, 0, 2, 1);
    sel_wand(&s, &o, 1, 0, false);
    CHECK(sel_count(&s) == 2, "wand: a region of one character is all selected");
    sel_free(&s);
}

static void test_bitmap(void) {
    char pix[] = "0.1"
                 ".2.";
    SelBitmap b = bm(pix, 3, 4, 3, 2);
    CHECK(selbm_get(&b, 3, 4) == '0' && selbm_get(&b, 4, 5) == '2' && selbm_get(&b, 6, 4) == '.' && selbm_get(&b, 2, 4) == '.', "bitmap: get, '.' outside");
    SelMask m = rect(3, 4, 4, 5);
    CHECK(selbm_ink_in(&b, &m) == 2, "bitmap: ink in a mask");
    SelFloat f;
    memset(&f, 0, sizeof f);
    CHECK(selfloat_lift(&f, &m, &b), "lift");
    CHECK(f.shape.w == 2 && f.shape.h == 2 && f.pix[0] == '0' && f.pix[1] == '.' && f.pix[2] == '.' && f.pix[3] == '2', "lift: the pixels of the selected cells");
    int x0, y0, x1, y1;
    CHECK(selfloat_ink_bounds(&f, &x0, &y0, &x1, &y1) && x0 == 3 && y0 == 4 && x1 == 4 && y1 == 5, "float: ink bounds");
    CHECK(selbm_clear(&b, &m) == 2 && !memcmp(pix, "..1...", 6), "clear: the selected cells go, the others stay");
    CHECK(selbm_clear(&b, &m) == 0, "clear: again changes nothing");
    selfloat_shift(&f, 1, 0);   /* the 0 lands on (4,4), the 2 on (5,5) */
    CHECK(selbm_stamp(&b, &f) == 2 && !memcmp(pix, ".01..2", 6), "stamp: writes the ink at the new place");
    /* a transparent cell does not overwrite, a cell outside the bitmap is skipped */
    SelFloat t;
    memset(&t, 0, sizeof t);
    SelMask tm = rect(3, 4, 4, 4);
    char src[] = "0.";
    SelBitmap sb = bm(src, 3, 4, 2, 1);
    selfloat_lift(&t, &tm, &sb);
    selfloat_shift(&t, 2, 0);   /* the 0 lands on (5,4), the '.' on (6,4) outside the bitmap */
    CHECK(selbm_stamp(&b, &t) == 1 && pix[2] == '0', "stamp: partly outside is clipped");
    SelFloat t2;
    memset(&t2, 0, sizeof t2);
    selfloat_lift(&t2, &tm, &sb);   /* on (3,4) and (4,4): ink on the first, transparent on the second */
    pix[1] = 'A';
    selbm_stamp(&b, &t2);
    CHECK(pix[0] == '0' && pix[1] == 'A', "stamp: ink overwrites, '.' leaves the cell as it was");
    /* a copy is deep */
    SelFloat cp;
    memset(&cp, 0, sizeof cp);
    selfloat_copy(&cp, &f);
    cp.pix[0] = 'Z';
    CHECK(f.pix[0] == '0' && cp.shape.w == f.shape.w, "float copy is deep");
    selfloat_free(&cp); selfloat_free(&f); selfloat_free(&t); selfloat_free(&t2);
    sel_free(&m); sel_free(&tm);
    CHECK(f.pix == NULL && sel_empty(&f.shape), "float free");
}

int main(void) {
    test_rect_and_get();
    test_combine();
    test_invert();
    test_polygon();
    test_wand();
    test_bitmap();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
