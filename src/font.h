#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include <stdint.h>

// Text alignment and horizontal anchor point
#define A_LEFT 0
#define A_CENTER 1
#define A_RIGHT 2

typedef struct {
	uint32_t magic;
    uint16_t n_glyphs;  // number of glyphs in this font file
    // simple ascci mapping parameters
    uint16_t map_start;  // the first glyph maps to this codepoint
    uint16_t map_n;  // how many glyphs map to incremental codepoints
    // offset to optional glyph id to codepoint mapping table
    uint16_t map_table_offset; // equal to glyph_description_offset when not present
    uint32_t glyph_description_offset;
    uint32_t glyph_data_offset;
    uint16_t linespace;
    int8_t yshift;  // to vertically center the digits, add this to tsb
    // bit0: has_outline. glyph index of outline = glyph index of fill * 2
    uint8_t flags;
} font_header_t;

typedef struct {
    uint8_t width;  // bitmap width [pixels]
    uint8_t height;  // bitmap height [pixels]
    int8_t lsb;  // left side bearing
    int8_t tsb;  // top side bearing
    int8_t advance;  // cursor advance width
    uint32_t start_index;  // offset to first byte of bitmap (relative to start of glyph description table)
} glyph_description_t;

// Load a <filePrefix>.fnt file
bool initFont(const char *filePrefix);

// draws a string of length `n` into `layer`
// to center it use x_a = 0 and
// colors cOutline and cFill
void push_str(
    int x_a,  // x-offset of the anchor point in pixels
    int y_a,  // y-offset in pixels
    const char *c,  // the UTF8 string to draw (can be zero terminated)
    unsigned n,  // length of the string
    unsigned align,  // Anchor point. One of A_LEFT, A_CENTER, A_RIGHT
    uint8_t layer,  // framebuffer layer to draw into
    unsigned color,  // RGBA color
    bool is_outline  // draw the outline glyphs if True, otherwise the fill glyphs
);

// Simplified clock string drawing
void drawStrCentered(const char *c, unsigned c_outline, unsigned c_fill);

// Returns the number of consecutive `path/0.fnt` files
int cntFntFiles(const char *path);

#endif
