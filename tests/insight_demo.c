/* insight_demo.c - standalone harness for src/insight.c and src/finder.c.
 *   insight_demo.exe <store.pxfdb | library root>
 *       --finder [query]            the glyph finder full window (query: a character, U+00E9, hex; --query-file f for UTF-8)
 *       --font <pxfm> [--glyph <pxgl>...] [--tf <pxtf>]   a 360 px panel with the insight sections of that font / those glyphs
 *       --sections ufT              panel sections: u usage, f similar fonts, t similar typefaces (default all)
 *       --select N  --zoom N  --sort count|height|width   finder: select cell N / zoom 1..8 / sort order
 *       --show-all                  panel: expand the usage list
 *       --size WxH  --scroll px  --screenshot out.png  --frames N  (after the work is done)  --type text (finder: typed into the box)
 * Prints timings (open, first result). */
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "finder.h"
#include "insight.h"
#include "store.h"
#include "theme.h"

int main(int argc, char **argv)
{
    const char *path = NULL, *shot = NULL, *font = NULL, *tf = NULL, *sections = "ufT", *query = NULL;
    bool finder = false, showAll = false;
    int w = 0, h = 0, select = -1, zoom = 0, frames = 6;
    float scroll = 0;
    int mx = -1, my = -1;
    const char *sort = "count";
    char glyphs[64][33]; int ng = 0;
    static char qbuf[256];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--finder")) { finder = true; if (i + 1 < argc && strncmp(argv[i + 1], "--", 2)) query = argv[++i]; }
        else if (!strcmp(argv[i], "--query-file") && i + 1 < argc) {
            size_t n = 0; char *t = px_read_file(argv[++i], &n);
            if (t) { snprintf(qbuf, sizeof qbuf, "%s", t); size_t l = strlen(qbuf); while (l && (qbuf[l - 1] == 10 || qbuf[l - 1] == 13)) qbuf[--l] = 0; query = qbuf; free(t); }
        }
        else if (!strcmp(argv[i], "--font") && i + 1 < argc) font = argv[++i];
        else if (!strcmp(argv[i], "--tf") && i + 1 < argc) tf = argv[++i];
        else if (!strcmp(argv[i], "--glyph") && i + 1 < argc && ng < 64) { snprintf(glyphs[ng++], 33, "%s", argv[++i]); }
        else if (!strcmp(argv[i], "--sections") && i + 1 < argc) sections = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--select") && i + 1 < argc) select = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--zoom") && i + 1 < argc) zoom = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sort") && i + 1 < argc) sort = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scroll") && i + 1 < argc) scroll = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--show-all")) showAll = true;
        else if (!strcmp(argv[i], "--mouse") && i + 1 < argc) { sscanf(argv[++i], "%d,%d", &mx, &my); }
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!path) path = argv[i];
    }
    if (!path || (!finder && !font && !ng)) { fprintf(stderr, "usage: insight_demo <store> --finder [query] | --font <pxfm> [--glyph <pxgl>...]\n"); return 2; }
    if (!w) w = finder ? 1280 : 360;
    if (!h) h = finder ? 800 : 900;

    Store *store = NULL;
    char err[PX_ERR_LEN];
    double t0 = GetTime();
    size_t pl = strlen(path);
    bool ok;
    if (pl > 6 && !strcmp(path + pl - 6, ".pxfdb")) ok = store_open_pxfdb(path, NULL, &store, err);
    else ok = store_import_folder(path, "tests/tmp/insight_demo.pxfdb", NULL, &store, err);
    if (!ok) { fprintf(stderr, "open failed: %s\n", err); return 1; }
    printf("store opened in %.2f s (%d glyphs, %d fonts, %d typefaces)\n", GetTime() - t0, store_info(store)->nGlyph, store_info(store)->nPxfm, store_info(store)->nPxtf);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(w, h, "pxfe insight demo");
    SetTargetFPS(60);
    Theme_Load();

    Insight *in = Insight_Create(store);
    Finder *fd = NULL;
    int secMask = 0;
    for (const char *p = sections; *p; p++) secMask |= *p == 'u' ? INSIGHT_SECTION_USAGE : *p == 'f' ? INSIGHT_SECTION_SIMILAR_FONTS : *p == 'T' || *p == 't' ? INSIGHT_SECTION_SIMILAR_TYPEFACES : 0;
    if (finder) {
        fd = Finder_Create(store, in);
        if (query) { Finder_SetCode(fd, Finder_ParseQuery(query)); }
        Finder_SetZoom(fd, zoom);
        Finder_SetSort(fd, !strcmp(sort, "height") ? 1 : !strcmp(sort, "width") ? 2 : 0);
    } else {
        char tfh[33] = "";
        if (tf) snprintf(tfh, sizeof tfh, "%s", tf);
        else if (font) {
            StoreEntryList l;
            if (store_list_typefaces(store_main_conn(store), &l, err)) {
                for (int i = 0; i < l.count; i++) if (!strcmp(l.e[i].pxfm, font)) { snprintf(tfh, sizeof tfh, "%s", l.e[i].pxtf); break; }
                store_entry_list_free(&l);
            }
        }
        if (ng) Insight_SetGlyphs(in, (const char (*)[33])glyphs, ng);
        if (font) Insight_SetFont(in, font, tfh);
        printf("font %s typeface %s\n", font ? font : "-", tfh[0] ? tfh : "-");
    }

    double tStart = GetTime(), tReady = -1;
    int frame = 0, settle = 0;
    bool selected = false;
    float panelScroll = -scroll;
    while (!WindowShouldClose()) {
        if (mx >= 0) SetMousePosition(mx, my);
        BeginDrawing();
        ClearBackground(TH_BG);
        Th_RegClear();
        int W = GetScreenWidth(), H = GetScreenHeight();
        if (finder) {
            InsightTarget t = { 0 };
            if (Finder_UpdateDraw(fd, (Rectangle){ 0, 0, (float)W, (float)H }, &t))
                printf("OPEN action=%d pxfm=%s pxtf=%s variant=%d code=%d\n", t.action, t.pxfm, t.pxtf, t.variant, (int)t.code);
            if (select >= 0 && !selected && Finder_ResultCount(fd) > 0) { Finder_Select(fd, select); selected = true; }
        } else {
            Rectangle panel = { 0, 0, (float)W, (float)H };
            DrawRectangleRec(panel, TH_PANEL);
            if (showAll) { /* click the "Show all" row once the list is there: handled by the scripted mouse below */ }
            float wheel = GetMouseWheelMove();
            panelScroll += wheel * 48.0f;
            if (panelScroll > 0) panelScroll = 0;
            BeginScissorMode(0, 0, W, H);
            Insight_SetVisible(in, panel);
            bool clicked = false; InsightTarget t = { 0 };
            Rectangle area = { 0, panelScroll, (float)W, 100 };
            float used = Insight_Draw(in, area, secMask, &clicked, &t);
            EndScissorMode();
            if (clicked) printf("OPEN action=%d pxfm=%s pxtf=%s variant=%d code=%d\n", t.action, t.pxfm, t.pxtf, t.variant, (int)t.code);
            if (panelScroll < -(used - H) && used > H) panelScroll = -(used - H);
            if (used <= H) panelScroll = 0;
        }
        EndDrawing();
        frame++;
        bool busy = Insight_Busy(in);
        if (finder) busy = busy || Finder_ResultCount(fd) < 0 || (select >= 0 && !selected);
        if (!busy && tReady < 0 && frame > 1) {
            tReady = GetTime() - tStart;
            printf("ready after %.3f s (%d frames)\n", tReady, frame);
            if (!finder) printf("usage fonts %d, similar %d\n", Insight_UsageFontCount(in), Insight_SimilarCount(in));
            else {
                printf("results %d\n", Finder_ResultCount(fd));
                if (query) {
                    StoreCodeGlyphList l; char e2[PX_ERR_LEN]; double q0 = GetTime();
                    if (store_glyphs_for_code(store_main_conn(store), Finder_ParseQuery(query), &l, e2)) {
                        printf("store_glyphs_for_code on one thread: %.1f ms (%d)\n", (GetTime() - q0) * 1000, l.count);
                        store_code_glyph_list_free(&l);
                    }
                }
                InsightTarget bt;
                if (select >= 0 && Insight_BestUsage(in, NULL, &bt)) printf("best usage: pxfm=%s code=%d\n", bt.pxfm, (int)bt.code);
            }
        }
        if (tReady >= 0) {
            if (++settle > frames) {
                if (shot) {
                    Image img = LoadImageFromScreen();
                    ExportImage(img, shot);
                    UnloadImage(img);
                    printf("wrote %s\n", shot);
                    break;
                }
            }
        }
        if (frame > 60 * 120) { fprintf(stderr, "timeout\n"); break; }
    }
    if (fd) Finder_Destroy(fd);
    Insight_Destroy(in);
    CloseWindow();
    store_close(store);
    return 0;
}
