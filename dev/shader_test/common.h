#pragma once
#include <stdlib.h>
#include <stdio.h>

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 32
#define D_SCALE 2.0

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// Random number within the range [a,b]
#define RAND_AB(a, b) (rand() % (b + 1 - a) + a)
