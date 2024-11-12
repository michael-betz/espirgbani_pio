#ifndef I2S_PARALLEL_H
#define I2S_PARALLEL_H

#include "driver/gpio.h"
#include "soc/i2s_struct.h"
#include <stdint.h>

typedef enum {
	I2S_PARALLEL_BITS_8 = 8,
	I2S_PARALLEL_BITS_16 = 16,
	I2S_PARALLEL_BITS_32 = 32,
} i2s_parallel_cfg_bits_t;

typedef struct {
	void *memory;
	size_t size;
} i2s_parallel_buffer_desc_t;

typedef struct {
	gpio_num_t gpio_bus[24];
	gpio_num_t gpio_clk;
	int clk_div;
    // .clk_div=1,     // illegal
    // .clk_div=2,     // = 20 MHz
    // .clk_div=3,     // = 13.33 MHz
    // .clk_div=4,     // = 10 MHz
    // .clk_div=8,     // = 5 MHz
    // .clk_div=16,    // = 2.5 MHz
	bool is_clk_inverted;
	i2s_parallel_cfg_bits_t bits;
	i2s_parallel_buffer_desc_t *bufa;
	// set to NULL if no double buffering is required
	i2s_parallel_buffer_desc_t *bufb;
} i2s_parallel_config_t;

void i2s_parallel_setup(i2s_dev_t *dev, const i2s_parallel_config_t *cfg);
void i2s_parallel_flip_to_buffer(i2s_dev_t *dev, int bufid);

#endif
