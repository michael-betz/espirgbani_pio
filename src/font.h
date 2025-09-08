#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include <stdint.h>

// Text alignment and horizontal anchor point
#define A_LEFT 0
#define A_CENTER 1
#define A_RIGHT 2

// Load a <filePrefix>.fnt file
bool initFont(const char *filePrefix);

// draws a string of length `n` into `layer`
// to center it use x_a = 0 and
// colors cOutline and cFill
void push_str(
	int x_a,		// x-offset of the anchor point in pixels
	int y_a,		// y-offset in pixels
	const char *c,	// the UTF8 string to draw (can be zero terminated)
	unsigned n,		// length of the string
	unsigned align, // Anchor point. One of A_LEFT, A_CENTER, A_RIGHT
	uint8_t layer,	// framebuffer layer to draw into
	unsigned color, // RGBA color
	bool
		is_outline // draw the outline glyphs if True, otherwise the fill glyphs
);

// Simplified clock string drawing
void drawStrCentered(const char *c, unsigned c_outline, unsigned c_fill);

// Enable console mode, switch font to spiffs/lemon.fnt
void init_print();
// Print a status message like in a console, shifting previous content up.
void push_print(unsigned color, const char *format, ...);
// Restore previous font, before console mode was enabled
void restore_print();

#endif
