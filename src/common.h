#ifndef COMMON_H
#define COMMON_H

#if defined(ESP_PLATFORM)
#include "driver/gpio.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t t_backg;
extern TaskHandle_t t_pinb;

#define HW_REV2  // REV2 PCB, reworked to have GPIO 12 and 33 swapped

// ---------------
//  SD card GPIOs
// ---------------
#define GPIO_SD_MISO GPIO_NUM_25 // REV2 of the PCB
#define GPIO_SD_CS GPIO_NUM_14
#define GPIO_SD_MOSI GPIO_NUM_27
#define GPIO_SD_CLK GPIO_NUM_26

// -----------------
//  LED panel GPIOs
// -----------------
// Upper half RGB
#define GPIO_R1 GPIO_NUM_21
#define GPIO_G1 GPIO_NUM_23
#define GPIO_B1 GPIO_NUM_22
// Lower half RGB
#define GPIO_R2 GPIO_NUM_5
#define GPIO_G2 GPIO_NUM_19
#define GPIO_B2 GPIO_NUM_18
// Row address
#define GPIO_A GPIO_NUM_16
#define GPIO_B GPIO_NUM_17
#define GPIO_C GPIO_NUM_2
#define GPIO_D GPIO_NUM_4
#define GPIO_E GPIO_NUM_32
// Control signals
#define GPIO_LAT GPIO_NUM_15
#define GPIO_CLK GPIO_NUM_13

#ifdef HW_REV2
	#define GPIO_OE_N GPIO_NUM_33
	// green LED at the back
	#define GPIO_LED GPIO_NUM_12
#endif

// -----------------
//  Misc GPIOs
// -----------------
// This pin is set high by the CH224K chip if USB-PD failed to negotiate and we're
// running from 5 V instead of 12 V. The clock will enter low-power mode and limit its
// max. brightness.
// If this define is commented, the clock will never enter low-power mode.
// #define GPIO_PD_BAD GPIO_NUM_35

// Ambient light sensor (ALS-PDIC144-6C/L378)
#define GPIO_LIGHT_SENSOR GPIO_NUM_36


// Wifi button
#define GPIO_WIFI GPIO_NUM_34

// Random number within the range [a,b]
#define RAND_AB(a, b) (esp_random() % (b + 1 - a) + a)
#else
#include <stdlib.h>
#define RAND_AB(a, b) (rand() % (b + 1 - a) + a)
#endif

// Width and height of the chain of panels [pixels]
// ... don't change it, things will catch fire! :p
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 32

#define ANIMATION_FILE "/sd/animations.img"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#endif
