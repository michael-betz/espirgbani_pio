//  opens bitmap (.bmp) files
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "esp_log.h"
#include "frame_buffer.h"
#include "bmp.h"

static const char *T = "BMP";

// Returns a filepointer seeked to the beginngin of the bitmap data
FILE *loadBitmapFile(char *file_name, bitmapFileHeader_t *bitmapFileHeader, bitmapInfoHeader_t *bitmapInfoHeader) {
	if (bitmapFileHeader == NULL || bitmapInfoHeader == NULL)
		return NULL;

	FILE *filePtr;
	filePtr = fopen(file_name, "rb");
	if (!filePtr) {
		ESP_LOGE(T, " fopen(%s, rb) failed: %s", file_name, strerror(errno));
		return NULL;
	}

	// read data into the bitmap file header
	fread(bitmapFileHeader, sizeof(bitmapFileHeader_t), 1, filePtr);
	// verify that this is a bmp file by check bitmap id
	if (bitmapFileHeader->bfType != 0x4D42) {
		fclose(filePtr);
		return NULL;
	}

	// read data into the bitmap info header
	fread(bitmapInfoHeader, sizeof(bitmapInfoHeader_t), 1, filePtr);

	//move file point to the beginning of bitmap data
	fseek(filePtr, bitmapFileHeader->bfOffBits, SEEK_SET);
	return filePtr;
}

// copys a rectangular area from a font bitmap to the frambuffer layer
void copyBmpToFbRect(FILE *bmpF, bitmapInfoHeader_t *bmInfo, uint16_t xBmp, uint16_t yBmp, uint16_t w, uint16_t h, int xFb, int yFb, uint8_t layerFb, uint32_t color, uint8_t chOffset) {
	if (bmpF == NULL || bmInfo == NULL)
		return;

	if (chOffset >= bmInfo->biWidth / 8) {
		ESP_LOGE(T, "There's only %d color channels dude!", bmInfo->biWidth/8);
		chOffset = bmInfo->biWidth/8 - 1;
	}
	int rowSize = ((bmInfo->biBitCount * bmInfo->biWidth + 31) / 32 * 4);  //how many bytes per row
	uint8_t *rowBuffer = (uint8_t *)malloc(rowSize);  //allocate buffer for one input row
	if (rowBuffer == NULL) {
		ESP_LOGE(T, "Could not allocate rowbuffer");
		return;
	}
	int startPos = ftell(bmpF);
	//move vertically to the right row
	fseek(bmpF, rowSize*(bmInfo->biHeight-yBmp-h), SEEK_CUR);
	for (int rowId=0; rowId<h; rowId++) {
		//read the whole row
		if(fread(rowBuffer, 1, rowSize, bmpF) != rowSize) {
			fseek(bmpF, startPos, SEEK_SET);               // skipped over last row
			free(rowBuffer);
			return;
		}
		//copy one row of relevant pixels
		for (uint16_t colId=0; colId<w; colId++) {
			uint16_t rowAddr = (xBmp+colId) * (bmInfo->biBitCount/8);
			if(rowAddr >= rowSize) {
				break;
			}
			uint8_t shade = rowBuffer[rowAddr+chOffset];
			// Decoding for packed format (doesn't anti-alias the outline :(
			// if(isOutline) {
			//     shade = shade > 127 ? 255 : 2*shade;
			// } else {
			//     shade = shade > 127 ? 2*shade-255 : 0;
			// }
			int xPixel = colId+xFb;
			int yPixel = h-rowId-1+yFb;
			if(xPixel < 0 || yPixel < 0) continue;
			setPixelOver(layerFb, xPixel, yPixel, (shade<<24)|scale32(shade,color));
		}
	}
	fseek(bmpF, startPos, SEEK_SET);
	free(rowBuffer);
}
