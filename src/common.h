#ifndef COMMON_H
#define COMMON_H

// ---------------
//  SD card GPIOs
// ---------------
// !!! HARDWARE BUG !!!
// according to
// https://randomnerdtutorials.com/esp32-pinout-reference-gpios/
// GPIO12 must be low during boot!
// unfortunately the SD card leaves MISO = GPIO12 IDLE high.
// ... the ESP will get stuck in a boot loop after a hard reset when the
// SD card is present.
// #define GPIO_SD_MISO 12
#define GPIO_SD_MISO GPIO_NUM_35
#define GPIO_SD_CS   GPIO_NUM_14
#define GPIO_SD_MOSI GPIO_NUM_32
#define GPIO_SD_CLK  GPIO_NUM_33

// -----------------
//  LED panel GPIOs
// -----------------
// Upper half RGB
#define GPIO_R1     GPIO_NUM_13
#define GPIO_G1     GPIO_NUM_15
#define GPIO_B1     GPIO_NUM_2
// Lower half RGB
#define GPIO_R2     GPIO_NUM_17
#define GPIO_G2     GPIO_NUM_27
#define GPIO_B2     GPIO_NUM_16
// Control signals
#define GPIO_A      GPIO_NUM_5
#define GPIO_B      GPIO_NUM_18
#define GPIO_C      GPIO_NUM_19
#define GPIO_D      GPIO_NUM_23
#define GPIO_LAT    GPIO_NUM_26
#define GPIO_BLANK  GPIO_NUM_25
#define GPIO_CLK    GPIO_NUM_22

// Width and height of the chain of panels [pixels]
// ... don't change it, things will catch fire! :p
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT  32

#define ANIMATION_FILE "/sd/runDmd.img"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

extern unsigned g_f_del;  // global delay between frame updates [ms]

// Random number within the range [a,b]
int RAND_AB(int a, int b);

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern TaskHandle_t t_backg;
extern TaskHandle_t t_pinb;

#endif
