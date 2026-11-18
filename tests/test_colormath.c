/* test_colormath.c - tests of src/colormath.c (the colour models of the colour picker).  build.bat test runs it. */
#include "colormath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("PASS  %s\n", name); } \
    else { g_fail++; printf("FAIL  %s   (%s:%d)\n", name, __FILE__, __LINE__); } } while (0)

static bool near(double a, double b, double eps) { return fabs(a - b) <= eps; }

static void test_gamma(void)
{
    CHECK(near(cm_srgb_to_linear(0), 0, 1e-12) && near(cm_srgb_to_linear(1), 1, 1e-12), "gamma: ends");
    CHECK(near(cm_srgb_to_linear(0.5), 0.21404, 1e-4), "gamma: 0.5 is 0.214 linear");
    CHECK(near(cm_srgb_to_linear(0.04), 0.04 / 12.92, 1e-12), "gamma: linear segment");
    bool ok = true;
    for (int i = 0; i <= 255; i++) { double c = i / 255.0; ok = ok && near(cm_linear_to_srgb(cm_srgb_to_linear(c)), c, 1e-9); }
    CHECK(ok, "gamma: round trip of every byte");
}

static void test_hsv_hsl(void)
{
    double h, s, v, l, r, g, b;
    cm_rgb_to_hsv(1, 0, 0, &h, &s, &v);
    CHECK(near(h, 0, 1e-9) && near(s, 1, 1e-9) && near(v, 1, 1e-9), "hsv: red");
    cm_rgb_to_hsv(0, 0.5, 0.5, &h, &s, &v);
    CHECK(near(h, 180, 1e-9) && near(s, 1, 1e-9) && near(v, 0.5, 1e-9), "hsv: teal");
    cm_rgb_to_hsv(0.5, 0.5, 0.5, &h, &s, &v);
    CHECK(near(h, 0, 1e-9) && near(s, 0, 1e-9) && near(v, 0.5, 1e-9), "hsv: grey has no hue");
    cm_hsv_to_rgb(120, 1, 1, &r, &g, &b);
    CHECK(near(r, 0, 1e-9) && near(g, 1, 1e-9) && near(b, 0, 1e-9), "hsv: 120 is green");
    cm_hsv_to_rgb(360, 1, 1, &r, &g, &b);
    CHECK(near(r, 1, 1e-9) && near(g, 0, 1e-9) && near(b, 0, 1e-9), "hsv: 360 is red");
    cm_rgb_to_hsl(1, 0, 0, &h, &s, &l);
    CHECK(near(h, 0, 1e-9) && near(s, 1, 1e-9) && near(l, 0.5, 1e-9), "hsl: red");
    cm_rgb_to_hsl(1, 1, 1, &h, &s, &l);
    CHECK(near(s, 0, 1e-9) && near(l, 1, 1e-9), "hsl: white");
    cm_hsl_to_rgb(240, 1, 0.25, &r, &g, &b);
    CHECK(near(r, 0, 1e-9) && near(g, 0, 1e-9) && near(b, 0.5, 1e-9), "hsl: navy");
    bool okv = true, okl = true;
    for (int i = 0; i < 4096; i++) {   /* a grid of sRGB colours, byte exact after the round trips */
        double R = (i % 16) * 17 / 255.0, G = ((i / 16) % 16) * 17 / 255.0, B = (i / 256) * 17 / 255.0;
        cm_rgb_to_hsv(R, G, B, &h, &s, &v); cm_hsv_to_rgb(h, s, v, &r, &g, &b);
        okv = okv && cm_pack(r, g, b, 1) == cm_pack(R, G, B, 1);
        cm_rgb_to_hsl(R, G, B, &h, &s, &l); cm_hsl_to_rgb(h, s, l, &r, &g, &b);
        okl = okl && cm_pack(r, g, b, 1) == cm_pack(R, G, B, 1);
    }
    CHECK(okv, "hsv: round trip of 4096 colours");
    CHECK(okl, "hsl: round trip of 4096 colours");
}

static void test_lab(void)
{
    double L, A, B, r, g, b;
    cm_rgb_to_lab(1, 1, 1, &L, &A, &B);
    CHECK(near(L, 100, 1e-3) && near(A, 0, 1e-3) && near(B, 0, 1e-3), "lab: white is 100 0 0 (D65)");
    cm_rgb_to_lab(0, 0, 0, &L, &A, &B);
    CHECK(near(L, 0, 1e-9) && near(A, 0, 1e-9) && near(B, 0, 1e-9), "lab: black");
    cm_rgb_to_lab(1, 0, 0, &L, &A, &B);
    CHECK(near(L, 53.24, 0.02) && near(A, 80.09, 0.05) && near(B, 67.20, 0.05), "lab: red 53.24 80.09 67.20");
    cm_rgb_to_lab(0, 0, 1, &L, &A, &B);
    CHECK(near(L, 32.30, 0.02) && near(A, 79.19, 0.05) && near(B, -107.86, 0.05), "lab: blue 32.30 79.19 -107.86");
    cm_rgb_to_lab(0.5, 0.5, 0.5, &L, &A, &B);
    CHECK(near(L, 53.39, 0.02) && near(A, 0, 1e-3) && near(B, 0, 1e-3), "lab: grey 808080 is L 53.39");
    CHECK(cm_lab_to_rgb(53.24, 80.09, 67.20, &r, &g, &b) && cm_pack(r, g, b, 1) == 0xFF0000FFu, "lab: back to red");
    CHECK(!cm_lab_to_rgb(50, 120, -120, &r, &g, &b), "lab: out of sRGB is reported");
    CHECK(r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1, "lab: out of sRGB is clamped");
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        double R = (i % 16) * 17 / 255.0, G = ((i / 16) % 16) * 17 / 255.0, Bl = (i / 256) * 17 / 255.0;
        cm_rgb_to_lab(R, G, Bl, &L, &A, &B);
        ok = ok && cm_lab_to_rgb(L, A, B, &r, &g, &b) && cm_pack(r, g, b, 1) == cm_pack(R, G, Bl, 1);
    }
    CHECK(ok, "lab: round trip of 4096 colours");
}

static void test_hex(void)
{
    uint32_t c = 0;
    char t[10];
    CHECK(cm_parse_hex("FF8000", &c) && c == 0xFF8000FFu, "hex: six digits get alpha FF");
    CHECK(cm_parse_hex("#00000080", &c) && c == 0x00000080u, "hex: eight digits with #");
    CHECK(cm_parse_hex(" ab12cd ", &c) && c == 0xAB12CDFFu, "hex: lower case and blanks");
    CHECK(!cm_parse_hex("12345", &c) && !cm_parse_hex("1234567", &c) && !cm_parse_hex("GG0000", &c) && !cm_parse_hex("", &c), "hex: wrong lengths and digits");
    cm_format_hex(0x123456FFu, t);
    CHECK(!strcmp(t, "123456"), "hex: alpha FF is written as six digits");
    cm_format_hex(0x12345680u, t);
    CHECK(!strcmp(t, "12345680"), "hex: other alpha as eight digits");
    CHECK(cm_pack(1, 0.5, 0, 0.5) == 0xFF800080u, "pack: rounds to the nearest byte");
    double r, g, b, a;
    cm_unpack(0x336699CCu, &r, &g, &b, &a);
    CHECK(cm_pack(r, g, b, a) == 0x336699CCu, "pack: unpack round trip");
    CHECK(cm_pack(-1, 2, 0.5, 1) == 0x00FF80FFu, "pack: clamps");
    CHECK(near(cm_luminance(0xFFFFFFFFu), 1, 1e-6) && near(cm_luminance(0x000000FFu), 0, 1e-9), "luminance: white and black");
}

int main(void)
{
    test_gamma();
    test_hsv_hsl();
    test_lab();
    test_hex();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
