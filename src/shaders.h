#ifndef SHADERS_H
#define SHADERS_H

// draws the animated background on layer 0
// also handles compositing the 3 layers
void aniBackgroundTask(void *pvParameters);

void drawXorFrame(unsigned frm);
void drawBendyFrame(unsigned frm);
void drawAlienFlameFrame(unsigned frm);
void drawDoomFlameFrame(unsigned frm);
void drawLasers(unsigned frm);

void flameSeedRow();
#endif
