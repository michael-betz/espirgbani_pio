#include <string.h>
#include <stdio.h>
#include <errno.h>
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

FILE 				*g_bmpFile = NULL;
bitmapFileHeader_t 	g_bmpFileHeader;
bitmapInfoHeader_t 	g_bmpInfoHeader;
font_t         		*g_fontInfo = NULL;

// loads a binary angelcode .fnt file into a font_t object
font_t *loadFntFile(char *file_name) {
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
        if (fread(&blockType, 1,1,f) < 1) {
            log_v("No more data");
            break;
        }
        fread(&blockSize, 1, 4, f);
        log_v("block %d of size %d", blockType, blockSize);
        switch(blockType) {
            case 1:
                fDat->info = (fontInfo_t*)malloc(blockSize);
                fread(fDat->info, 1, blockSize, f);
                break;

            case 2:
                fDat->common = (fontCommon_t*)malloc(blockSize);
                fread(fDat->common, 1, blockSize, f);
                break;

            case 3:
                fDat->pageNames = (char*)malloc(blockSize);
                fDat->pageNamesLen = blockSize;
                fread(fDat->pageNames, 1, blockSize, f);
                break;

            case 4:
                fDat->chars = (fontChar_t*)malloc(blockSize);
                fDat->charsLen = blockSize;
                fread(fDat->chars, 1, blockSize, f);
                break;

            case 5:
                fDat->kerns = (fontKern_t*)malloc(blockSize);
                fDat->kernsLen = blockSize;
                fread(fDat->kerns, 1, blockSize, f);
                break;

            default:
                log_w("Unknown block type: %d\n", blockType);
                fseek(f, blockSize, SEEK_CUR);
                continue;
        }
    }
    fclose(f);
    return fDat;
}

// prints (some) content of a font_t object
void printFntFile(font_t *fDat) {
    log_d(
        "name: %s, bmp: %s",
        fDat->info->fontName,
        fDat->pageNames
    );
    log_d(
        "size: %d, aa: %d, height: %d, scaleW: %d, scaleH: %d, pages: %d",
        fDat->info->fontSize,
        fDat->info->aa,
        fDat->common->lineHeight,
        fDat->common->scaleW,
        fDat->common->scaleH,
        fDat->common->pages
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

// frees up allocated memory of an font_t object
void freeFntFile(font_t *fDat) {
    if(fDat != NULL) {
        if(fDat->info != NULL)
            free(fDat->info);
        if(fDat->common != NULL)
            free(fDat->common);
        if(fDat->pageNames != NULL)
            free(fDat->pageNames);
        if(fDat->chars != NULL)
            free(fDat->chars);
        if(fDat->kerns != NULL)
            free(fDat->kerns);
        free(fDat);
    }
}

// Loads a <prefix>.bmp and <prefix>.fnt file, to get ready for printing
// returns true on success
bool initFont(const char *filePrefix) {
    char tempFileName[32];
    if (g_bmpFile != NULL) {
        fclose(g_bmpFile);
        g_bmpFile = NULL;
    }
    sprintf(tempFileName, "%s.fnt", filePrefix);
    log_v("Loading %s", tempFileName);
    g_fontInfo = loadFntFile(tempFileName);
    if (g_fontInfo == NULL) {
        log_e("Could not load %s", tempFileName);
        return false;
    }
    printFntFile(g_fontInfo);
    sprintf(tempFileName, "%s_0.bmp", filePrefix);
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
	if (g_bmpFile == NULL || g_fontInfo == NULL) return;
	if (c == '\n') {
		cursY += g_fontInfo->common->lineHeight;
	} else {
		fontChar_t *charInfo = getCharInfo(g_fontInfo, c);
		// int16_t k = getKerning(g_lastChar, c);
		if (charInfo) {
			copyBmpToFbRect(g_bmpFile,
							 &g_bmpInfoHeader,
							 charInfo->x,
							 charInfo->y,
							 charInfo->width,
							 charInfo->height,
							 cursX + charInfo->xoffset,
							 cursY + charInfo->yoffset,
							 layer, color, chOffset);
			cursX += charInfo->xadvance;
		}
	}
	g_lastChar = c;
}

int getStrWidth(const char *str) {
    int w=0;
    while(*str) {
        fontChar_t *charInfo = getCharInfo(g_fontInfo, *str);
        if(charInfo)
            w += charInfo->xadvance;
        str++;
    }
    return w;
}

void drawStr(const char *str, int x, int y, uint8_t layer, uint32_t cOutline, uint32_t cFill) {
	const char *c = str;
    log_v("x: %d, y: %d, str: %s", x, y, str);
    setCur(x, y);
	while(*c) {
        // Render outline first (from green channel)
        // Note that .bmp color order is  Blue, Green, Red
		drawChar(*c++, layer, cOutline, 1);
	}
    c = str;
    setCur(x, y);
    while(*c) {
        // Render filling (from red channel)
        drawChar(*c++, layer, cFill, 2);
    }
}

void drawStrCentered(const char *str, uint8_t layer, uint32_t cOutline, uint32_t cFill) {
    int w, h, xOff, yOff;
    if (g_bmpFile == NULL || g_fontInfo == NULL) return;
    h = g_fontInfo->common->lineHeight;
    w = getStrWidth(str);
    log_v("w: %d, h: %d", w, h);
    xOff = (DISPLAY_WIDTH - w)/2;
    yOff = (DISPLAY_HEIGHT - h)/2;
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
        sprintf(fNameBuffer, "%s/%d.fnt", path, nFiles);
        if(stat(fNameBuffer, &buffer) == 0) {
            nFiles++;
        } else {
            break;
        }
    }
    return nFiles;
}

void aniClockTask(void *pvParameters) {
    // takes care of things which need to happen
    // every minute (like redrawing the clock)
    time_t now = 0;
    struct tm timeinfo;
    timeinfo.tm_min =  0;
    timeinfo.tm_hour= 18;
    char strftime_buf[64];

    unsigned col = rand();
    unsigned ticks_font=0, ticks_color=0;  // for counting ticks [min]
    cJSON *jDelay = jGet(getSettings(), "delays");
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    // count font files and choose a random one
    int maxFnt = cntFntFiles("/sd/fnt") - 1;
    int fntRequest = -1;
    if (maxFnt < 0) {
        log_e("no fonts found on SD card :( :( :(");
    } else {
        log_i("last font file: /sd/fnt/%d.fnt", maxFnt);
        fntRequest = RAND_AB(0, maxFnt);
    }

    while(1) {
        log_v(" ticks_font: %u, max: %d", ticks_font, jGetI(jDelay, "font", 60));
        if (maxFnt > 0 && ticks_font > jGetI(jDelay, "font", 60)) {
            // every delays.font minutes
            fntRequest = RAND_AB(0, maxFnt);
            ticks_font = 0;
        }
        if (ticks_color > jGetI(jDelay, "color", 10)) {
            col = 0xFF000000 | rand();
            ticks_color = 0;
        }
        // every minute
        if(maxFnt > 0 && fntRequest >= 0 && fntRequest <= maxFnt) {
            sprintf(strftime_buf, "/sd/fnt/%d", fntRequest);
            initFont(strftime_buf);
            fntRequest = -1;
        }
        // Redraw the clock
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%H:%M", &timeinfo);
        drawStrCentered(strftime_buf, 1, col, 0xFF000000);
        // manageBrightness(&timeinfo);
        ticks_font++;
        ticks_color++;

        // wait for the next minute
        vTaskDelayUntil(&xLastWakeTime, 1000 * 60 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}
