#include <string.h>
#include "Arduino.h"
#include "fast_hsv2rgb.h"
#include "frame_buffer.h"
#include "json_settings.h"
#include "animations.h"

// Reads the filehader and fills `fh`
static int getFileHeader(FILE *f, fileHeader_t *fh)
{
	char tempCh[3];

	if (f == NULL || fh == NULL)
		return -1;

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

// Fills the headerEntry struct with data.
// Specify an `headerIndex` from 0 to nAnimations
static int readHeaderEntry(FILE *f, headerEntry_t *h, int headerIndex)
{
	if (f == NULL || h == NULL)
		return -1;

	// Copys header info into h. Note that h->nFrameEntries must be freed!
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

	log_d(
		"%s: index: %d, w: %d, h: %d, nMem: %d, nAni: %d, del: %d",
		h->name,
		headerIndex,
		h->width,
		h->height,
		h->nStoredFrames,
		h->nFrameEntries,
		h->frameHeader->frameDur
	);
	return 0;
}

// seek the file f to the beginning of a specific animation frame
static void seekToFrame(FILE *f, int byteOffset, int frameId)
{
	// without fast-seek enabled, this takes tens of ms when seeking backwards
	// http://www.elm-chan.org/fsw/ff/doc/lseek.html
	if (f == NULL || frameId <= 0)
		return;
	byteOffset += DISPLAY_WIDTH * DISPLAY_HEIGHT * (frameId - 1) / 2;
	fseek(f, byteOffset, SEEK_SET);
}

// play a single animation, start to finish
static void playAni(FILE *f, headerEntry_t *h)
{
	if (f == NULL || h == NULL || h->nFrameEntries == 0)
		return;

	// get a random color
	uint8_t r, g, b;
	fast_hsv2rgb_32bit(RAND_AB(0,HSV_HUE_MAX), HSV_SAT_MAX, HSV_VAL_MAX, &r, &g, &b);
	unsigned color = SRGBA(r,g,b,0xFF);

	// pre-seek the file to beginning of frame
	frameHeaderEntry_t fh = h->frameHeader[0];
	seekToFrame(f, h->byteOffset + HEADER_SIZE, fh.frameId);
	unsigned cur_delay = fh.frameDur;
	TickType_t xLastWakeTime = xTaskGetTickCount();

	for(int i=0; i<h->nFrameEntries; i++) {
		startDrawing(2);
		if(fh.frameId <= 0)
			setAll(2, 0xFF000000);  // invalid frame = translucent black
		else
			setFromFile(f, 2, color);
		doneDrawing(2);

		// get the next frame ready in advance
		if (i < h->nFrameEntries - 1) {
			fh = h->frameHeader[i + 1];
			seekToFrame(f, h->byteOffset + HEADER_SIZE, fh.frameId);
		}

		// wait for N ms, measured from last call to vTaskDelayUntil()
		if (cur_delay < 30)
			cur_delay = 30;
		vTaskDelayUntil(&xLastWakeTime, cur_delay / portTICK_PERIOD_MS);
		cur_delay = fh.frameDur;
	}
}

void aniPinballTask(void *pvParameters)
{
	FILE *f = (FILE *)pvParameters;
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
		// aniId = 0x0619;
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
			vTaskDelayUntil(&xLastWakeTime, 30 / portTICK_PERIOD_MS);
		}
	}
	fclose(f);
}
