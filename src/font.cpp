#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "frame_buffer.h"
#include "bmp.h"
#include "json_settings.h"
#include "font.h"


static FILE *g_bmpFile = NULL;
static bitmapFileHeader_t g_bmpFileHeader;
static bitmapInfoHeader_t g_bmpInfoHeader;
static font_t *g_fontInfo = NULL;

// loads a binary angelcode .fnt file into a font_t object
font_t *load_font_info(char *file_name) {
	font_t *fDat = NULL;
	uint8_t temp[4], blockType;
	uint32_t blockSize;

	FILE *f = fopen(file_name, "rb");
	if (!f) {
		log_e(" fopen(%s, rb) failed: %s", file_name, strerror(errno));
		return NULL;
	}

	fread(temp, 4, 1, f);
	if(strncmp((char*)temp, "BMF", 3) != 0) {
		log_e("Wrong header");
		fclose(f);
		return NULL;
	}

	fDat = (font_t *)malloc(sizeof(font_t));
	memset(fDat, 0x00, sizeof(font_t));

	while(1) {
		blockType = 0;
		if (fread(&blockType, 1, 1, f) < 1) {
			log_v("No more data");
			break;
		}
		if (fread(&blockSize, 1, 4, f) < 4) {
			log_e("failed reading blockSize");
			break;
		}
		log_v("loading block %d of size %d", blockType, blockSize);
		switch(blockType) {
			case 1:
				fDat->info = (fontInfo_t*)malloc(blockSize);
				if (!fDat->info || fread(fDat->info, 1, blockSize, f) != blockSize) {
					log_e("failed reading fontInfo_t");
					goto lfi_error_exit;
				}
				break;

			case 2:
				fDat->common = (fontCommon_t*)malloc(blockSize);
				if (!fDat->common || fread(fDat->common, 1, blockSize, f) != blockSize) {
					log_e("failed reading fontCommon_t");
					goto lfi_error_exit;
				}
				break;

			case 3:
				fDat->pageNamesLen = blockSize;
				fDat->pageNames = (char*)malloc(blockSize);
				if (!fDat->pageNames || fread(fDat->pageNames, 1, blockSize, f) != blockSize) {
					log_e("failed reading pageNames");
					goto lfi_error_exit;
				}
				break;

			case 4:
				fDat->charsLen = blockSize;
				fDat->chars = (fontChar_t*)malloc(blockSize);
				if (!fDat->chars || fread(fDat->chars, 1, blockSize, f) != blockSize) {
					log_e("failed reading fontChar_t");
					goto lfi_error_exit;
				}
				break;

			case 5:
				log_w("Ignoring kerning table. Ain't nobody got heap for that!!!");
				fseek(f, blockSize, SEEK_CUR);
				// fDat->kernsLen = blockSize;
				// fDat->kerns = (fontKern_t*)malloc(blockSize);
				// if (!fDat->kerns || fread(fDat->kerns, 1, blockSize, f) != blockSize) {
				// 	log_e("failed reading fontKern_t");
				// 	goto lfi_error_exit;
				// }
				break;

			default:
				log_w("Unknown block type: %d\n", blockType);
				fseek(f, blockSize, SEEK_CUR);
				continue;
		}
	}
	fclose(f);
	return fDat;

lfi_error_exit:
	fclose(f);
	free_font_info(fDat);
	return NULL;
}

// frees up allocated memory of an font_t object
void free_font_info(font_t *f)
{
	if (f == NULL)
		return;
	free(f->info);
	free(f->common);
	free(f->pageNames);
	free(f->chars);
	free(f->kerns);
	free(f);
}

// prints (some) content of a font_t object
void print_font_info(font_t *fDat) {
	if (fDat == NULL)
		return;
	log_i(
		"%s, %s, s: %d, lH: %d, base: %d, sW: %d, sH: %d",
		fDat->info->fontName,
		fDat->pageNames,
		fDat->info->fontSize,
		fDat->common->lineHeight,
		fDat->common->base,
		fDat->common->scaleW,
		fDat->common->scaleH
	);
}

// get pointer to fontChar_t for a specific character, or NULL if not found
fontChar_t *getCharInfo(font_t *fDat, char c) {
	fontChar_t *tempC = fDat->chars;
	int nChars = fDat->charsLen/(int)sizeof(fontChar_t);
	for(int i=0; i<nChars; i++) {
		if(tempC->id == c) {
			return tempC;
		}
		tempC++;
	}
	return NULL;
}

// Loads a <prefix>.bmp and <prefix>.fnt file, to get ready for printing
// returns true on success
bool initFont(const char *filePrefix) {
	char tempFileName[32];
	sprintf(tempFileName, "%s.fnt", filePrefix);

	free_font_info(g_fontInfo);
	g_fontInfo = NULL;

	log_v("loading %s", tempFileName);
	g_fontInfo = load_font_info(tempFileName);
	if (g_fontInfo == NULL) {
		log_e("Could not load %s", tempFileName);
		return false;
	}
	print_font_info(g_fontInfo);

	sprintf(tempFileName, "%s_0.bmp", filePrefix);

	if (g_bmpFile != NULL)
		fclose(g_bmpFile);
	g_bmpFile = NULL;

	log_v("Loading %s", tempFileName);
	g_bmpFile = loadBitmapFile(tempFileName, &g_bmpFileHeader, &g_bmpInfoHeader);
	if (g_bmpFile == NULL) {
		log_e("Could not load %s", tempFileName);
		return false;
	}

	return true;
}

int cursX=0, cursY=8;
char g_lastChar = 0;

void setCur(int x, int y) {
	cursX = x;
	cursY = y;
	g_lastChar = 0;
}

void drawChar(char c, uint8_t layer, uint32_t color, uint8_t chOffset) {
	if (g_bmpFile == NULL || g_fontInfo == NULL)
		return;

	if (c == '\n') {
		cursY += g_fontInfo->common->lineHeight;
	} else {
		fontChar_t *charInfo = getCharInfo(g_fontInfo, c);
		// int16_t k = getKerning(g_lastChar, c);
		if (charInfo) {
			copyBmpToFbRect(
				g_bmpFile,
				&g_bmpInfoHeader,
				charInfo->x,
				charInfo->y,
				charInfo->width,
				charInfo->height,
				cursX + charInfo->xoffset,
				cursY + charInfo->yoffset,
				layer,
				color,
				chOffset
			);
			cursX += charInfo->xadvance;
		}
	}
	g_lastChar = c;
}

// calculate bounding box enclosing all characters
// w = width, h = height, top = vertical offset to the top edge of BB
// left = horizontal offset to left edge of BB
// drawStr("bla", -left, -top) will make chars touch the upper left side of display
void getStrBoundingBox(const char *str, int *w, int *h, int *left, int *top) {
	int i=0, _w=0, _top=INT_MAX, _bottom=INT_MIN;
	while(*str) {
		fontChar_t *charInfo = getCharInfo(g_fontInfo, *str);
		if (charInfo) {
			// left edge of BB
			if (i == 0)
				*left = charInfo->xoffset;

			// width
			_w += charInfo->xadvance;

			// top edge of BB
			_top = MIN(_top, charInfo->yoffset);

			// bottom edge of BB
			_bottom = MAX(_bottom, charInfo->yoffset + charInfo->height);
		}
		str++;
		i++;
	}
	*w = _w;
	*top = _top;
	*h = _bottom - _top;
}

// y = distance from top of display to top of character cell
void drawStr(const char *str, int x, int y, uint8_t layer, uint32_t cOutline, uint32_t cFill) {
	const char *c = str;
	log_v("x: %d, y: %d, str: %s", x, y, str);
	setCur(x, y);

	// Render outline first (from green channel)
	// Note that .bmp color order is  Blue, Green, Red
	while (*c)
		drawChar(*c++, layer, cOutline, 1);

	// Render filling (from red channel)
	c = str;
	setCur(x, y);
	while (*c)
		drawChar(*c++, layer, cFill, 2);
}

void drawStrCentered(const char *str, uint8_t layer, uint32_t cOutline, uint32_t cFill) {
	int w=0, h=0, left=0, top=0, xOff=0, yOff=0;
	if (g_bmpFile == NULL || g_fontInfo == NULL) return;

	// get bounding box of the whole string
	getStrBoundingBox(str, &w, &h, &left, &top);

	// align left of BB with left of display
	xOff = -left;

	// horizontally center `str` on display
	xOff += (DISPLAY_WIDTH - w) / 2;

	// align top of BB with top of display
	yOff = -top;

	// vertically center BB on display
	yOff += (DISPLAY_HEIGHT - h) / 2;

	startDrawing(layer);
	setAll(layer, 0x00000000);
	drawStr(str, xOff, yOff, layer, cOutline, cFill);
	doneDrawing(layer);
}

// Returns the number of consecutive `path/0.fnt` files
int cntFntFiles(const char* path) {
	int nFiles = 0;
	char fNameBuffer[32];
	struct stat   buffer;
	while(1) {
		sprintf(fNameBuffer, "%s/%02d.fnt", path, nFiles);
		if(stat(fNameBuffer, &buffer) == 0) {
			nFiles++;
		} else {
			break;
		}
	}
	return nFiles;
}
