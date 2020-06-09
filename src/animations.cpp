#include <string.h>
#include "Arduino.h"
#include "fast_hsv2rgb.h"
#include "frame_buffer.h"
#include "json_settings.h"
#include "animations.h"

int getFileHeader(FILE *f, fileHeader_t *fh)
{
	char tempCh[3];
	fseek(f, 0x00000000, SEEK_SET);
	fread(tempCh, 3, 1, f);
	if(memcmp(tempCh,"DGD", 3) != 0) {
		log_e("Invalid file header!");
		return -1;
	}
	fread(&fh->nAnimations, 2, 1, f);
	fh->nAnimations = SWAP16(fh->nAnimations);
	fseek(f, 0x000001EF, SEEK_SET);
	fread(&fh->buildStr, 8, 1, f);

	log_i("Build: %s, Found %d animations", fh->buildStr, fh->nAnimations);
	return 0;
}

int readHeaderEntry(FILE *f, headerEntry_t *h, int headerIndex)
{
	// Copys header info into h. Note that h->nFrameEntries must be freed!
	// leaves file f seeked to the beginning of the animation data
	fseek(f, HEADER_OFFS + HEADER_SIZE * headerIndex, SEEK_SET);
	fread(h, sizeof(*h), 1, f);
	h->name[31] = '\0';
	h->frameHeader = NULL;
	h->animationId = SWAP16(h->animationId);
	h->byteOffset =  SWAP32(h->byteOffset) * HEADER_SIZE;
	// Exract frame header
	frameHeaderEntry_t *fh = (frameHeaderEntry_t*)malloc(sizeof(frameHeaderEntry_t) * h->nFrameEntries);
	if (fh == NULL) {
		log_e("Memory allocation error!");
		return -1;
	}
	h->frameHeader = fh;
	fseek(f, h->byteOffset, SEEK_SET);
	for(int i=0; i<h->nFrameEntries; i++) {
		fread(&fh->frameId,  1, 1, f);
		fread(&fh->frameDur, 1, 1, f);
		// Hack to sort out invalid headers
		if(fh->frameDur <= 0 || fh->frameId > h->nStoredFrames) {
			log_e("Invalid header!");
			return -1;
		}
		fh++;
	}
	fseek(f, h->byteOffset+HEADER_SIZE, SEEK_SET);

	log_v("%s", h->name);
	log_v("--------------------------------");
	log_v("animationId:       0x%04x", h->animationId);
	log_v("nStoredFrames:         %d", h->nStoredFrames);
	log_v("byteOffset:    0x%08x", h->byteOffset);
	log_v("nFrameEntries:         %d", h->nFrameEntries);
	log_v("width: 0x%02x  height: 0x%02x  unknown0: 0x%02x", h->width, h->height, h->unknown0);
	return 0;
}

void seekToFrame(FILE *f, int byteOffset, int frameOffset)
{
	byteOffset += DISPLAY_WIDTH * DISPLAY_HEIGHT * frameOffset / 2;
	fseek(f, byteOffset, SEEK_SET);
}

void playAni(FILE *f, headerEntry_t *h)
{
	uint8_t r, g, b;
	TickType_t xLastWakeTime;

	// get a random color
	fast_hsv2rgb_32bit(RAND_AB(0,HSV_HUE_MAX), HSV_SAT_MAX, HSV_VAL_MAX, &r, &g, &b);
	unsigned color = SRGBA(r,g,b,0xFF);

	 xLastWakeTime = xTaskGetTickCount();

	for(int i=0; i<h->nFrameEntries; i++) {
		frameHeaderEntry_t fh = h->frameHeader[i];
		if(fh.frameId == 0) {
			// invalid frame = translucent black
			startDrawing(2);
			setAll(2, 0xFF000000);
		} else {
			seekToFrame(f, h->byteOffset + HEADER_SIZE, fh.frameId - 1);
			startDrawing(2);
			setFromFile(f, 2, color);
		}
		doneDrawing(2);
		if (i == 0)
			 xLastWakeTime = xTaskGetTickCount();
		vTaskDelayUntil(&xLastWakeTime, fh.frameDur / portTICK_PERIOD_MS);
	}
}

void aniPinballTask(FILE *f)
{
	fileHeader_t fh;
	getFileHeader(f, &fh);
	fseek(f, HEADER_OFFS, SEEK_SET);
	headerEntry_t myHeader;

	int aniId;
	cJSON *jDelay = jGet(getSettings(), "delays");
	TickType_t xLastWakeTime;

	while (1) {
		// delay between animations
		delay(jGetI(jDelay, "ani", 15) * 1000);

		aniId = RAND_AB(0, fh.nAnimations - 1);
		readHeaderEntry(f, &myHeader, aniId);
		playAni(f, &myHeader);
		free(myHeader.frameHeader);
		myHeader.frameHeader = NULL;

		// Keep a single frame displayed for a bit
		if (myHeader.nStoredFrames <= 3 || myHeader.nFrameEntries <= 3)
			delay(3000);

		// Fade out the frame
		uint32_t nTouched = 1;
		xLastWakeTime = xTaskGetTickCount();
		while (nTouched) {
			startDrawing(2);
			nTouched = fadeOut(2, 10);
			doneDrawing(2);
			vTaskDelayUntil(&xLastWakeTime, 20 / portTICK_PERIOD_MS);
		}
	}
	fclose(f);
}
