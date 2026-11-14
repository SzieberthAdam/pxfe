/* fit_tool.c - a command line look at fit.h, for comparing the grid search with pixelglyphlib and for trying sizes.
 *
 *   fit_tool grid <font> [face]          one line: the grid found, as pixelglyphlib's find_grid tells it
 *   fit_tool size <font> [fixedEm]       the size search of a font that is no pixel font
 *   fit_tool show <font> <text> [pixel|smooth] [em]   the text drawn as pixels
 *
 * Build: gcc tests\fit_tool.c src\fit.c src\outline.c -o tests\fit_tool.exe -O2 -I src */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fit.h"

static void num(char *out, size_t n, double v)
{
    if (v == floor(v)) snprintf(out, n, "%.0f", v);
    else snprintf(out, n, "%.3f", v);
    if (strchr(out, '.')) { char *e = out + strlen(out) - 1; while (*e == '0') *e-- = 0; if (*e == '.') *e = 0; }
}

int main(int argc, char **argv)
{
    char err[256] = "";
    if (argc < 3) { fprintf(stderr, "usage: fit_tool grid|size|show <font> ...\n"); return 2; }
    Outline *ol = outline_open(argv[2], err, sizeof err);
    if (!ol) { printf("error %s\n", err); return 1; }
    if (!strcmp(argv[1], "grid")) {
        if (argc > 3 && outline_select_face(ol, atoi(argv[3]), err, sizeof err) != 0) { printf("error %s\n", err); return 1; }
        FitGrid g;
        clock_t t0 = clock();
        int rc = fit_find_grid(ol, &g, err, sizeof err);
        double ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
        if (rc <= 0) printf("none %s ms=%.0f\n", err, ms);
        else if (!g.sure) { char a[32], b[32]; num(a, sizeof a, g.x); num(b, sizeof b, g.y); printf("none guess x=%s y=%s match=%.3f ms=%.0f\n", a, b, g.match, ms); }
        else if (g.x == g.y && g.x == floor(g.x) && !g.xPhase && !g.yPhase && !g.perGlyph) printf("unit=%.0f how=%s ms=%.0f\n", g.x, g.how, ms);
        else {
            char a[32], b[32];
            num(a, sizeof a, g.x);
            num(b, sizeof b, g.y);
            printf("grid x=%s y=%s xp=%.0f yp=%.0f per=%d how=%s ms=%.0f\n", a, b, g.xPhase, g.yPhase, g.perGlyph, g.how, ms);
        }
        fit_grid_free(&g);
    } else if (!strcmp(argv[1], "size")) {
        FitSizeOpts o = {0, 0, argc > 3 ? (float)atof(argv[3]) : 0};
        FitParams p;
        FitSizeReport r;
        clock_t t0 = clock();
        int rc = fit_find_size(ol, &o, &p, &r, err, sizeof err);
        double ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
        if (rc <= 0) printf("none %s\n", err);
        else {
            int a, d;
            fit_box(ol, &p, &a, &d);
            printf("em=%.2f dy=%.2f thr=%.2f topo=%.3f (best %.3f) cover=%.3f probes=%d box=%d+%d ms=%.0f\n", p.em, p.dy, p.threshold, r.topo, r.bestTopo, r.cover, r.probes, a, d, ms);
        }
    } else if (!strcmp(argv[1], "show") && argc > 3) {
        FitParams p;
        FitGrid g;
        int pixel = argc > 4 && !strcmp(argv[4], "pixel");
        memset(&g, 0, sizeof g);
        if (pixel) {
            if (fit_find_grid(ol, &g, err, sizeof err) <= 0) { printf("no grid: %s\n", err); return 1; }
            fit_grid_params(ol, &g, &p);
        } else {
            FitSizeOpts o = {0, 0, argc > 5 ? (float)atof(argv[5]) : 0};
            if (fit_find_size(ol, &o, &p, NULL, err, sizeof err) <= 0) { printf("no size: %s\n", err); return 1; }
        }
        int asc, desc;
        fit_box(ol, &p, &asc, &desc);
        printf("em=%.2f aspect=%.3f dx=%.2f dy=%.2f thr=%.2f box=%d+%d\n", p.em, p.aspect, p.dx, p.dy, p.threshold, asc, desc);
        int H = asc + desc + 4, W = 0, pen = 1;
        static char canvas[80][400];
        memset(canvas, '.', sizeof canvas);
        for (const char *t = argv[3]; *t; t++) {
            FitBitmap b;
            FitCorr c = {0, 0, 1};
            int has = pixel && fit_grid_corr(&g, (unsigned char)*t, &c);
            int gi = outline_glyph_index(ol, (unsigned char)*t);
            if (gi <= 0 || fit_render(ol, gi, &p, has ? &c : NULL, &b) < 0) continue;
            for (int y = 0; y < b.h; y++)
                for (int x = 0; x < b.w; x++) {
                    int cy = asc + 2 - b.top + y, cx = pen + b.left + x;
                    if (b.pix[y * b.w + x] && cy >= 0 && cy < 80 && cx >= 0 && cx < 399) canvas[cy][cx] = '#';
                }
            pen += b.advance > 0 ? b.advance : b.w + 1;
            fit_bitmap_free(&b);
        }
        W = pen + 1 < 399 ? pen + 1 : 399;
        for (int y = 0; y < H && y < 80; y++) printf("%.*s\n", W, canvas[y]);
        fit_grid_free(&g);
    }
    outline_close(ol);
    return 0;
}
