#ifndef COMMON_H
#define COMMON_H

// ---------------
//  SD card GPIOs
// ---------------
#define GPIO_SD_MISO GPIO_NUM_25  // REV2 of the PCB
#define GPIO_SD_CS   GPIO_NUM_14
#define GPIO_SD_MOSI GPIO_NUM_27
#define GPIO_SD_CLK  GPIO_NUM_26

// -----------------
//  LED panel GPIOs
// -----------------
// Upper half RGB
#define GPIO_R1     GPIO_NUM_22
#define GPIO_G1     GPIO_NUM_23
#define GPIO_B1     GPIO_NUM_21
// Lower half RGB
#define GPIO_R2     GPIO_NUM_18
#define GPIO_G2     GPIO_NUM_19
#define GPIO_B2     GPIO_NUM_29
// Control signals
#define GPIO_A      GPIO_NUM_16
#define GPIO_B      GPIO_NUM_17
#define GPIO_C      GPIO_NUM_2
#define GPIO_D      GPIO_NUM_4
#define GPIO_e      GPIO_NUM_32
#define GPIO_LAT    GPIO_NUM_15
#define GPIO_BLANK  GPIO_NUM_12
#define GPIO_CLK    GPIO_NUM_13

// Width and height of the chain of panels [pixels]
// ... don't change it, things will catch fire! :p
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT  32

#define ANIMATION_FILE "/sd/animations.img"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// Random number within the range [a,b]
int RAND_AB(int a, int b);

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern TaskHandle_t t_backg;
extern TaskHandle_t t_pinb;

#endif
