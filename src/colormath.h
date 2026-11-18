/* colormath.h - colour models for the colour picker (see DESIGN.md, "Version 23"): sRGB with its gamma, HSV, HSL and CIE Lab (D65).
 * No raylib: pure C99, tested by tests/test_colormath.c.
 *
 * Every component is a double: r, g, b, a in 0..1 (sRGB, gamma encoded); h in degrees 0..360 (0 for a grey); s, v, l in 0..1;
 * CIE L* in 0..100, a* and b* about -128..127.  A colour of the formats is 0xRRGGBBAA (pxfont.h). */
#ifndef COLORMATH_H
#define COLORMATH_H

#include <stdbool.h>
#include <stdint.h>

/* sRGB transfer function (IEC 61966-2-1): gamma-encoded value to linear light and back, both 0..1. */
double cm_srgb_to_linear(double c);
double cm_linear_to_srgb(double c);

void cm_rgb_to_hsv(double r, double g, double b, double *h, double *s, double *v);
void cm_hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b);
void cm_rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l);
void cm_hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b);
/* CIE 1976 L*a*b* of an sRGB colour, D65 white.  cm_lab_to_rgb returns false when the colour is outside sRGB (the result is clamped). */
void cm_rgb_to_lab(double r, double g, double b, double *L, double *A, double *B);
bool cm_lab_to_rgb(double L, double A, double B, double *r, double *g, double *b);

/* 0xRRGGBBAA <-> components 0..1 (rounded to the nearest byte; out-of-range values are clamped). */
uint32_t cm_pack(double r, double g, double b, double a);
void     cm_unpack(uint32_t rgba, double *r, double *g, double *b, double *a);
/* Hex text: "RRGGBB" or "RRGGBBAA", an optional leading '#', either case, surrounding blanks ignored.  False when it is neither. */
bool cm_parse_hex(const char *s, uint32_t *rgba);
/* "RRGGBB" when the alpha is FF, else "RRGGBBAA" (upper case, as the formats write it). */
void cm_format_hex(uint32_t rgba, char out[10]);
/* Relative luminance (0..1, linear) of a colour: a label over a swatch is dark above 0.36, light below. */
double cm_luminance(uint32_t rgba);

#endif
