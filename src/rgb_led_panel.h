#ifndef RGB_LED_PANEL_H
#define RGB_LED_PANEL_H
#include "common.h"

// Change to set the global brightness of the display, range 0 ...
// (DISPLAY_WIDTH-8)
extern int g_rgbLedBrightness;

void init_rgb();
void updateFrame();

// number of updateFrame() calls since boot (frame counter)
extern unsigned g_frames;

// delay [ms] between updateFrame() calls (determines max. global frame-rate)
extern unsigned g_f_del;

#endif
