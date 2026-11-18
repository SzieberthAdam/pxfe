/* colormath.c - colour models of the colour picker (see colormath.h).  The sRGB matrices and the transfer function are those of
 * IEC 61966-2-1; the white point is D65 (0.95047, 1, 1.08883). */
#include "colormath.h"

#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

double cm_srgb_to_linear(double c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

double cm_linear_to_srgb(double c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

/* the hue (degrees) of r, g, b with max mx and min mn */
static double hue_of(double r, double g, double b, double mx, double mn)
{
    double d = mx - mn, h;
    if (d <= 0) return 0;
    if (mx == r) h = fmod((g - b) / d, 6.0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
    if (h >= 360) h -= 360;
    return h;
}

void cm_rgb_to_hsv(double r, double g, double b, double *h, double *s, double *v)
{
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
    *h = hue_of(r, g, b, mx, mn);
    *s = mx > 0 ? (mx - mn) / mx : 0;
    *v = mx;
}

/* the r, g, b of hue h at chroma c, with m added to each */
static void from_hue(double h, double c, double m, double *r, double *g, double *b)
{
    h = fmod(h, 360.0);
    if (h < 0) h += 360;
    double hp = h / 60, x = c * (1 - fabs(fmod(hp, 2.0) - 1)), r1 = 0, g1 = 0, b1 = 0;
    switch ((int)hp) {
    case 0: r1 = c; g1 = x; break;
    case 1: r1 = x; g1 = c; break;
    case 2: g1 = c; b1 = x; break;
    case 3: g1 = x; b1 = c; break;
    case 4: r1 = x; b1 = c; break;
    default: r1 = c; b1 = x; break;
    }
    *r = r1 + m; *g = g1 + m; *b = b1 + m;
}

void cm_hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b)
{
    s = clamp01(s); v = clamp01(v);
    double c = v * s;
    from_hue(h, c, v - c, r, g, b);
}

void cm_rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l)
{
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
    *h = hue_of(r, g, b, mx, mn);
    *l = (mx + mn) / 2;
    *s = d <= 0 ? 0 : d / (1 - fabs(2 * *l - 1));
    if (*s > 1) *s = 1;
}

void cm_hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b)
{
    s = clamp01(s); l = clamp01(l);
    double c = (1 - fabs(2 * l - 1)) * s;
    from_hue(h, c, l - c / 2, r, g, b);
}

#define XN 0.95047
#define YN 1.0
#define ZN 1.08883
static const double D = 6.0 / 29.0;

static double lab_f(double t) { return t > D * D * D ? cbrt(t) : t / (3 * D * D) + 4.0 / 29.0; }
static double lab_finv(double t) { return t > D ? t * t * t : 3 * D * D * (t - 4.0 / 29.0); }

void cm_rgb_to_lab(double r, double g, double b, double *L, double *A, double *B)
{
    double R = cm_srgb_to_linear(r), G = cm_srgb_to_linear(g), Bl = cm_srgb_to_linear(b);
    double X = 0.4124564 * R + 0.3575761 * G + 0.1804375 * Bl;
    double Y = 0.2126729 * R + 0.7151522 * G + 0.0721750 * Bl;
    double Z = 0.0193339 * R + 0.1191920 * G + 0.9503041 * Bl;
    double fx = lab_f(X / XN), fy = lab_f(Y / YN), fz = lab_f(Z / ZN);
    *L = 116 * fy - 16;
    *A = 500 * (fx - fy);
    *B = 200 * (fy - fz);
}

bool cm_lab_to_rgb(double L, double A, double B, double *r, double *g, double *b)
{
    double fy = (L + 16) / 116, fx = fy + A / 500, fz = fy - B / 200;
    double X = XN * lab_finv(fx), Y = YN * lab_finv(fy), Z = ZN * lab_finv(fz);
    double R = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    double G = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    double Bl = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
    const double eps = 1e-4;
    bool in = R >= -eps && R <= 1 + eps && G >= -eps && G <= 1 + eps && Bl >= -eps && Bl <= 1 + eps;
    *r = cm_linear_to_srgb(clamp01(R));
    *g = cm_linear_to_srgb(clamp01(G));
    *b = cm_linear_to_srgb(clamp01(Bl));
    return in;
}

static unsigned byte_of(double x) { return (unsigned)floor(clamp01(x) * 255.0 + 0.5); }

uint32_t cm_pack(double r, double g, double b, double a)
{
    return (uint32_t)byte_of(r) << 24 | (uint32_t)byte_of(g) << 16 | (uint32_t)byte_of(b) << 8 | (uint32_t)byte_of(a);
}

void cm_unpack(uint32_t c, double *r, double *g, double *b, double *a)
{
    if (r) *r = ((c >> 24) & 0xFF) / 255.0;
    if (g) *g = ((c >> 16) & 0xFF) / 255.0;
    if (b) *b = ((c >> 8) & 0xFF) / 255.0;
    if (a) *a = (c & 0xFF) / 255.0;
}

bool cm_parse_hex(const char *s, uint32_t *rgba)
{
    if (!s) return false;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') s++;
    char d[9];
    int n = 0;
    for (; *s && !(*s == ' ' || *s == '\t'); s++) {
        if (!isxdigit((unsigned char)*s) || n >= 8) return false;
        d[n++] = *s;
    }
    while (*s == ' ' || *s == '\t') s++;
    if (*s || (n != 6 && n != 8)) return false;
    d[n] = 0;
    unsigned long v = strtoul(d, NULL, 16);
    *rgba = n == 6 ? (uint32_t)(v << 8 | 0xFFu) : (uint32_t)v;
    return true;
}

void cm_format_hex(uint32_t c, char out[10])
{
    if ((c & 0xFFu) == 0xFFu) snprintf(out, 10, "%06X", (unsigned)(c >> 8));
    else snprintf(out, 10, "%08X", (unsigned)c);
}

double cm_luminance(uint32_t c)
{
    double r, g, b;
    cm_unpack(c, &r, &g, &b, NULL);
    return 0.2126729 * cm_srgb_to_linear(r) + 0.7151522 * cm_srgb_to_linear(g) + 0.0721750 * cm_srgb_to_linear(b);
}
