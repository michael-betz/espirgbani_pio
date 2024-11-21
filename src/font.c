#include "font.h"
#include "esp_log.h"
#include "frame_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "json_settings.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define FLAG_HAS_OUTLINE (1 << 0)

typedef struct {
	uint32_t magic;
	uint16_t n_glyphs; // number of glyphs in this font file
	// simple ascci mapping parameters
	uint16_t map_start; // the first glyph maps to this codepoint
	uint16_t map_n;		// how many glyphs map to incremental codepoints
	// offset to optional glyph id to codepoint mapping table
	uint16_t
		map_table_offset; // equal to glyph_description_offset when not present
	uint32_t glyph_description_offset;
	uint32_t glyph_data_offset;
	uint16_t linespace;
	int8_t yshift; // to vertically center the digits, add this to tsb
	// bit0: has_outline. glyph index of outline = glyph index of fill * 2
	uint8_t flags;
} font_header_t;

typedef struct {
	uint8_t width;		  // bitmap width [pixels]
	uint8_t height;		  // bitmap height [pixels]
	int8_t lsb;			  // left side bearing
	int8_t tsb;			  // top side bearing
	int8_t advance;		  // cursor advance width
	uint32_t start_index; // offset to first byte of bitmap (relative to start
						  // of glyph description table)
} glyph_description_t;

static const char *T = "FONT";

static FILE *fntFile = NULL;
static font_header_t fntHeader;

static char *fontName = NULL;
static unsigned *map_unicode_table = NULL;
static glyph_description_t *glyph_description_table = NULL;

// glyph cursor
static int cursor_x = 0, cursor_y = 0;

// decodes one UTF8 character at a time, keeping internal state
// returns a valid unicode character once complete or 0
static unsigned utf8_dec(char c) {
	// Unicode return value                   UTF-8 encoded chars
	// 00000000 00000000 00000000 0xxxxxxx -> 0xxxxxxx
	// 00000000 00000000 00000yyy yyxxxxxx -> 110yyyyy 10xxxxxx
	// 00000000 00000000 zzzzyyyy yyxxxxxx -> 1110zzzz 10yyyyyy 10xxxxxx
	// 00000000 000wwwzz zzzzyyyy yyxxxxxx -> 11110www 10zzzzzz 10yyyyyy
	// 10xxxxxx
	static unsigned readN = 0, result = 0;

	if ((c & 0x80) == 0) {
		// 1 byte character, nothing to decode
		readN = 0; // reset state
		return c;
	}

	if (readN == 0) {
		result = 0;

		// first byte of several, initialize N bytes decode
		if ((c & 0xE0) == 0xC0) {
			readN = 1; // 1 more byte to decode
			result |= (c & 0x1F) << 6;
			return 0;
		} else if ((c & 0xF0) == 0xE0) {
			readN = 2;
			result |= (c & 0x0F) << 12;
			return 0;
		} else if ((c & 0xF8) == 0xF0) {
			readN = 3;
			result |= (c & 0x07) << 18;
			return 0;
		} else { // shouldn't happen?
			return 0;
		}
	}

	switch (readN) {
	case 1:
		result |= c & 0x3F;
		readN = 0;
		return result;

	case 2:
		result |= (c & 0x3F) << 6;
		break;

	case 3:
		result |= (c & 0x3F) << 12;
		break;

	default:
		readN = 1;
	}
	readN--;

	return 0;
}

static int binary_search(unsigned target, const unsigned *arr, int length) {
	int left = 0;
	int right = length - 1;

	while (left <= right) {
		int mid = left + (right - left) / 2;

		// Check if target is present at mid
		if (arr[mid] == target) {
			return mid;
		}

		// If target greater, ignore left half
		if (arr[mid] < target) {
			left = mid + 1;
		}
		// If target is smaller, ignore right half
		else {
			right = mid - 1;
		}
	}

	// Target is not present in array
	return -1;
}

static bool get_glyph_description(
	unsigned glyph_index, glyph_description_t *desc, bool is_outline
) {
	if (desc == NULL)
		return false;

	if (glyph_index >= fntHeader.n_glyphs) {
		ESP_LOGE(T, "invalid glyph index :( %d", glyph_index);
		return false;
	}

	if (is_outline) {
		if ((fntHeader.flags & FLAG_HAS_OUTLINE) == 0)
			return false;

		glyph_index += fntHeader.n_glyphs / 2;
	}

	// Read glyph description from SD card
	// unsigned offs = fntHeader.glyph_description_offset;
	// offs += glyph_index * sizeof(glyph_description_t);
	// if (fseek(fntFile, offs, SEEK_SET) == -1) {
	// 	ESP_LOGE(T, "seek failed :( %s", strerror(errno));
	// 	return false;
	// }

	// int n_read = fread(desc, sizeof(glyph_description_t), 1, fntFile);
	// if (n_read != 1) {
	// 	ESP_LOGE(T, "read glyph failed :( %s", strerror(errno));
	// 	return false;
	// }

	memcpy(
		desc, &glyph_description_table[glyph_index], sizeof(glyph_description_t)
	);
	return true;
}

// finds the glyph_index for unicode character `codepoint`
// returns -1 on failure
static int find_glyph_index(unsigned codepoint) {
	int glyph_index = -1;

	// check if the character is in the ascii map
	if (codepoint >= fntHeader.map_start &&
		(codepoint - fntHeader.map_start) < fntHeader.map_n) {
		glyph_index = codepoint - fntHeader.map_start;
	} else if (map_unicode_table != NULL) {
		// otherwise binary search in map_unicode_table
		glyph_index = binary_search(
			codepoint, map_unicode_table, fntHeader.n_glyphs - fntHeader.map_n
		);
		if (glyph_index > -1)
			glyph_index += fntHeader.map_n;
	}

	if (glyph_index < 0 || glyph_index >= fntHeader.n_glyphs) {
		ESP_LOGW(T, "glyph not found: %d", glyph_index);
		return -1;
	}

	return glyph_index;
}

void freeFont() {
	if (fntFile != NULL)
		fclose(fntFile);
	fntFile = NULL;

	free(fontName);
	fontName = NULL;

	free(map_unicode_table);
	map_unicode_table = NULL;

	free(glyph_description_table);
	glyph_description_table = NULL;

	memset(&fntHeader, 0, sizeof(font_header_t));
}

static bool load_helper(void **target, int len, const char *name) {
	if (len > 0) {
		*target = malloc(len);
		if (*target == NULL) {
			ESP_LOGW(T, "Couldn't allocate %s :( %d", name, len);
			return false;
		}

		int n_read = fread(*target, 1, len, fntFile);
		if (n_read != len) {
			ESP_LOGE(T, "Failed to read %s: %s", name, strerror(errno));
			return false;
		}
	}
	return true;
}

// Loads a .fnt file, to get ready for drawing something returns true on success
bool initFont(const char *fileName) {
	freeFont();

	ESP_LOGD(T, "loading %s", fileName);

	fntFile = fopen(fileName, "r");
	if (fntFile == NULL) {
		ESP_LOGE(T, "Failed to open %s: %s", fileName, strerror(errno));
		return false;
	}

	int n_read = fread(&fntHeader, sizeof(fntHeader), 1, fntFile);
	if (n_read != 1) {
		ESP_LOGE(T, "Failed to read fntHeader: %s", strerror(errno));
		return false;
	}

	if (fntHeader.magic != 0x005A54BE) {
		ESP_LOGE(T, "Wrong magic :( %x", (unsigned)fntHeader.magic);
		return false;
	}

	if (!load_helper(
			(void **)&fontName,
			fntHeader.map_table_offset - sizeof(font_header_t), "fontName"
		)) {
		return false;
	}

	if (!load_helper(
			(void **)&map_unicode_table,
			fntHeader.glyph_description_offset - fntHeader.map_table_offset,
			"unicode mapping table"
		)) {
		return false;
	}

	if (!load_helper(
			(void **)&glyph_description_table,
			fntHeader.glyph_data_offset - fntHeader.glyph_description_offset,
			"glyph description table"
		)) {
		return false;
	}

	ESP_LOGD(T, "fontName: %s", fontName);
	ESP_LOGD(T, "n_glyphs: %d", fntHeader.n_glyphs);
	ESP_LOGD(T, "map_start: %d", fntHeader.map_start);
	ESP_LOGD(T, "map_n: %d", fntHeader.map_n);
	ESP_LOGD(T, "linespace: %d", fntHeader.linespace);
	ESP_LOGD(T, "yshift: %d", fntHeader.yshift);
	ESP_LOGD(T, "flags: %x", fntHeader.flags);
	return true;
}

static void glyphToBuffer(
	glyph_description_t *desc, int offs_x, int offs_y, unsigned layer,
	unsigned color
) {
	if (desc == NULL)
		return;

	// Find the beginning and length of the glyph blob
	unsigned data_start = fntHeader.glyph_data_offset + desc->start_index;
	unsigned len = desc->width * desc->height;

	if (fseek(fntFile, data_start, SEEK_SET) == -1) {
		ESP_LOGE(T, "glyph seek failed :( %s", strerror(errno));
		return;
	}

	uint8_t *buff = malloc(len);
	if (buff == NULL) {
		ESP_LOGE(T, "glyph overflow :( %s", strerror(errno));
		return;
	}

	int n_read = fread(buff, 1, len, fntFile);
	if (n_read != len) {
		ESP_LOGE(T, "glyph read failed :( %s", strerror(errno));
		free(buff);
		return;
	}

	uint8_t *p = buff;
	for (int y = 0; y < desc->height; y++) {
		int yPixel = y + offs_y;
		if (yPixel < 0 || yPixel >= DISPLAY_HEIGHT) {
			p += desc->width;
			continue;
		}

		for (int x = 0; x < desc->width; x++) {
			int xPixel = x + offs_x;
			if (xPixel >= 0 && xPixel < DISPLAY_WIDTH)
				setPixelOver(
					layer, xPixel, yPixel, (*p << 24) | scale32(*p, color)
				);
			p++;
		}
	}
	free(buff);
}

static void
push_char(unsigned codepoint, uint8_t layer, uint32_t color, bool is_outline) {
	glyph_description_t desc;

	if (fntFile == NULL) {
		ESP_LOGE(T, "no font loaded");
		return;
	}

	int glyph_index = find_glyph_index(codepoint);
	if (glyph_index < 0)
		return;

	if (!get_glyph_description(glyph_index, &desc, is_outline))
		return;

	ESP_LOGV(
		T, "push_char(%c, %d, %d)", (char)codepoint, cursor_x + desc.lsb,
		cursor_y - desc.tsb
	);
	glyphToBuffer(
		&desc, cursor_x + desc.lsb, cursor_y - desc.tsb, layer, color
	);

	cursor_x += desc.advance;
}

static int get_char_width(unsigned codepoint) {
	glyph_description_t desc;

	int glyph_index = find_glyph_index(codepoint);
	if (glyph_index < 0)
		return 0;

	if (!get_glyph_description(glyph_index, &desc, false))
		return 0;

	return desc.advance;
}

static int get_str_width(const char *c, unsigned n) {
	const char *p = c;
	int w = 0;

	if (c == NULL)
		return w;

	while (*p && n > 0) {
		unsigned codepoint = utf8_dec(*p++);
		n--;
		if (codepoint == 0)
			continue;

		if (codepoint == '\n')
			break;

		w += (get_char_width(codepoint));
	}
	utf8_dec('\0'); // reset internal state

	ESP_LOGV(T, "%s width is %d pixels", c, w);
	return w;
}

static void set_x_cursor(int x_a, const char *c, unsigned n, unsigned align) {
	int w_str = get_str_width(c, n);
	if (align == A_RIGHT)
		cursor_x = x_a - w_str;
	else if (align == A_CENTER)
		cursor_x = x_a - w_str / 2;
	else
		cursor_x = x_a;
}

void push_str(
	int x_a, int y_a, const char *c, unsigned n, unsigned align, uint8_t layer,
	unsigned color, bool is_outline
) {
	if (fntFile == NULL) {
		ESP_LOGE(T, "No font file loaded");
		return;
	}

	if (is_outline) {
		if ((fntHeader.flags & FLAG_HAS_OUTLINE) == 0) {
			ESP_LOGW(T, "Font doesn't have an outline");
			return;
		}
	}

	cursor_y = y_a;
	set_x_cursor(x_a, c, n, align);

	while (*c && n > 0) {
		unsigned codepoint = utf8_dec(*c++);
		n--;
		if (codepoint == 0)
			continue;

		if (codepoint == '\n') {
			cursor_y += fntHeader.linespace;
			set_x_cursor(x_a, c, n, align);
			continue;
		}

		push_char(codepoint, layer, color, is_outline);
	}
	utf8_dec('\0'); // reset internal state
}

// convenience function to center a small text with outline and fill color
void drawStrCentered(const char *c, unsigned c_outline, unsigned c_fill) {
	lockFrameBuffer();

	// transparent black
	setAll(1, 0x00000000);

	// Draw the outline first (if the font supports it)
	if ((fntHeader.flags & FLAG_HAS_OUTLINE)) {
		push_str(
			DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - fntHeader.yshift, c, 16,
			A_CENTER, 1, c_outline, true
		);
	} else {
		// Otherwise, just fill the letters
		c_fill = c_outline;
	}

	// Then overwrite with the fill
	push_str(
		DISPLAY_WIDTH / 2, DISPLAY_HEIGHT - fntHeader.yshift, c, 16, A_CENTER,
		1, c_fill, false
	);

	releaseFrameBuffer();
}
