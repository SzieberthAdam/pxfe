/* colorpick.h - the colour picker of the design system (DESIGN.md, "The palette bar and the colour picker" and "Colour picker" in the component catalogue).
 *
 * One modal window for every place that chooses a colour: a view opens it with a colour and a tag, main.c draws it last
 * (Th_ColorPickDraw, after the dialogs) and blocks everything under it while it is open, and the view takes the answer with its tag.
 * Models: HSV (saturation/value square and hue bar), Wheel (hue and saturation disc and value bar), RGB, HSL and CIE Lab (D65)
 * sliders, all with an alpha slider; a hex field (RRGGBB or RRGGBBAA), the new and the old colour side by side, OK and Cancel.
 * Enter confirms, Esc cancels.  The maths is colormath.c. */
#ifndef COLORPICK_H
#define COLORPICK_H

#include <stdbool.h>
#include <stdint.h>

enum { TH_CP_HSV, TH_CP_WHEEL, TH_CP_RGB, TH_CP_HSL, TH_CP_LAB, TH_CP_MODES };

/* Opens the picker on the colour rgba (0xRRGGBBAA) with a title ("Add a colour"); `tag` (not 0) names the caller's question. */
void Th_ColorPickOpen(uint32_t rgba, const char *title, int tag);
bool Th_ColorPickIsOpen(void);
/* The answer to the question `tag`, once: 1 and *rgba when OK was chosen, -1 when it was cancelled, 0 while there is none. */
int  Th_ColorPickTake(int tag, uint32_t *rgba);
/* Draws the open picker over a W x H window (a scrim under it) and takes its input; nothing when it is closed.  The caller
 * blocks every other input while Th_ColorPickIsOpen(), and draws this with the input gates open. */
void Th_ColorPickDraw(int W, int H);
/* The model shown (TH_CP_*): main.c keeps it in pxfe.ini ([app] pickerMode). */
int  Th_ColorPickMode(void);
void Th_ColorPickSetMode(int mode);
/* For scripts: the colour being chosen (0xRRGGBBAA). */
uint32_t Th_ColorPickValue(void);

#endif
