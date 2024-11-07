#ifndef RGB_LED_PANEL_H
#define RGB_LED_PANEL_H
#include "common.h"

void init_rgb();
void reload_rgb_config();
void updateFrame();

// Set the global brightness of the display, range 0 .. 120
void set_brightness(int value);

// number of updateFrame() calls since boot (frame counter)
extern unsigned g_frames;

// delay [ms] between updateFrame() calls (determines max. global frame-rate)
extern unsigned g_f_del;

#endif
