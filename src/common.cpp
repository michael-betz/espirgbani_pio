#include "esp_system.h"

int RAND_AB(int a, int b)
{
	return (esp_random() % (b + 1 - a) + a);
}
