// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "soc/gpio_periph.h"
#include "soc/i2s_reg.h"
#include "soc/i2s_struct.h"
#include "soc/io_mux_reg.h"

#include "driver/gpio.h"
#include "esp_private/periph_ctrl.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "rom/gpio.h"
#include "rom/lldesc.h"

#include "i2s_parallel.h"

static const char *T = "I2S_P";

typedef struct {
    volatile lldesc_t *dmadesc_a, *dmadesc_b;
    int desccount_a, desccount_b;
} i2s_parallel_state_t;

static i2s_parallel_state_t *i2s_state[2] = {NULL, NULL};

#define DMA_MAX (4096 - 4)

// Calculate the amount of dma descs needed for a buffer desc
static int calc_needed_dma_descs_for(i2s_parallel_buffer_desc_t *desc) {
    int ret = 0;
    for (int i = 0; desc[i].memory != NULL; i++) {
        ret += (desc[i].size + DMA_MAX - 1) / DMA_MAX;
    }
    return ret;
}

static void
fill_dma_desc(volatile lldesc_t *dmadesc, i2s_parallel_buffer_desc_t *bufdesc) {
    int n = 0;
    for (int i = 0; bufdesc[i].memory != NULL; i++) {
        int len = bufdesc[i].size;
        uint8_t *data = (uint8_t *)bufdesc[i].memory;
        while (len) {
            int dmalen = len;
            if (dmalen > DMA_MAX)
                dmalen = DMA_MAX;
            dmadesc[n].size = dmalen;
            dmadesc[n].length = dmalen;
            dmadesc[n].buf = data;
            dmadesc[n].eof = 0;
            dmadesc[n].sosf = 0;
            dmadesc[n].owner = 1;
            dmadesc[n].qe.stqe_next = (lldesc_t *)&dmadesc[n + 1];
            dmadesc[n].offset = 0;
            len -= dmalen;
            data += dmalen;
            n++;
        }
    }
    // Loop last back to first
    dmadesc[n - 1].qe.stqe_next = (lldesc_t *)&dmadesc[0];
}

static void gpio_setup_out(gpio_num_t gpio, int sig, bool isInverted) {
    if (gpio == -1)
        return;
    // PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
    gpio_pad_select_gpio(gpio);
    gpio_set_direction(gpio, GPIO_MODE_DEF_OUTPUT);
    gpio_set_drive_capability(gpio, (gpio_drive_cap_t)3);
    gpio_matrix_out(gpio, sig, isInverted, false);
}

static void dma_reset(i2s_dev_t *dev) {
    dev->lc_conf.in_rst = 1;
    dev->lc_conf.in_rst = 0;
    dev->lc_conf.out_rst = 1;
    dev->lc_conf.out_rst = 0;

    dev->lc_conf.ahbm_rst = 1;
    dev->lc_conf.ahbm_rst = 0;

    dev->in_link.val = 0;
    dev->out_link.val = 0;
}

static void fifo_reset(i2s_dev_t *dev) {
    dev->conf.rx_fifo_reset = 1;
    dev->conf.rx_fifo_reset = 0;
    dev->conf.tx_fifo_reset = 1;
    dev->conf.tx_fifo_reset = 0;
}

static int i2snum(i2s_dev_t *dev) { return (dev == &I2S0) ? 0 : 1; }

void i2s_parallel_setup(i2s_dev_t *dev, const i2s_parallel_config_t *cfg) {
    // Figure out which signal numbers to use for routing
    int sig_data_base, sig_clk;
    if (dev == &I2S0) {
        sig_data_base = I2S0O_DATA_OUT0_IDX;
        sig_clk = I2S0O_WS_OUT_IDX;
    } else {
        if (cfg->bits == I2S_PARALLEL_BITS_32) {
            sig_data_base = I2S1O_DATA_OUT0_IDX;
        } else {
            // Because of... reasons... the 16-bit values for i2s1 appear on
            // d8...d23
            sig_data_base = I2S1O_DATA_OUT8_IDX;
        }
        sig_clk = I2S1O_WS_OUT_IDX;
    }

    // Route the signals
    for (int x = 0; x < cfg->bits; x++) {
        gpio_setup_out(cfg->gpio_bus[x], sig_data_base + x, false);
    }
    // ToDo: Clk/WS may need inversion?
    gpio_setup_out(cfg->gpio_clk, sig_clk, cfg->is_clk_inverted);

    // Power on dev
    if (dev == &I2S0) {
        periph_module_enable(PERIPH_I2S0_MODULE);
    } else {
        periph_module_enable(PERIPH_I2S1_MODULE);
    }

    // I2S conf2 reg
    dev->conf2.val = 0;
    dev->conf2.lcd_en = 1;
    dev->conf2.lcd_tx_wrx2_en=0;
    dev->conf2.lcd_tx_sdx2_en=0;

    // --------------------------------------------
    //  Clock config
    // --------------------------------------------
    dev->sample_rate_conf.val = 0;
    dev->sample_rate_conf.rx_bits_mod = cfg->bits;
    dev->sample_rate_conf.tx_bits_mod = cfg->bits;
    // bit-clock divider, min is 2
    // ESP32 and ESP32-S2 TRM clearly say that "Note that I2S_TX_BCK_DIV_NUM[5:0] must not be configured as 1."
    dev->sample_rate_conf.tx_bck_div_num = 2;
    dev->sample_rate_conf.rx_bck_div_num = 2;

    dev->clkm_conf.clk_en = 1;
    dev->clkm_conf.val = 0;
     // Use the 80mhz system clock (PLL_D2_CLK) when '0'
    dev->clkm_conf.clka_en = 0;

    // Frequency will be (80Mhz / clkm_div_num / tx_bck_div_num (2))
    // integral divider, min is 2
    // Note tx_bck_div_num value of 2 will further divide clock rate
    int div_ = cfg->clk_div;
    if (div_ < 2)
        div_ = 2;
    ESP_LOGD(T, "i2s pll_d2_clock clkm_div_num is: %u", div_);
    dev->clkm_conf.clkm_div_num = div_;

    dev->clkm_conf.clkm_div_a = 1;
    dev->clkm_conf.clkm_div_b = 0;

    // --------------------------------------------
    //  DMA and FIFO config
    // --------------------------------------------
    dev->fifo_conf.val = 0;
    dev->fifo_conf.rx_data_num = 32; // Thresholds.
    dev->fifo_conf.tx_data_num = 32;
    dev->fifo_conf.dscr_en = 1;
    // Mode 1, single 16-bit channel, load 16 bit sample(*) into fifo and pad to 32 bit with zeros
    // *Actually a 32 bit read where two samples are read at once. Length of fifo must thus still be word-aligned
    dev->fifo_conf.tx_fifo_mod = 1;

    dev->fifo_conf.rx_fifo_mod_force_en = 1;
    dev->fifo_conf.tx_fifo_mod_force_en = 1;

    dev->conf1.val = 0;
    dev->conf1.tx_stop_en = 0;
    dev->conf1.tx_pcm_bypass = 1;

    // 16-bit single channel data
    dev->conf_chan.val = 0;
    dev->conf_chan.tx_chan_mod=1;
    dev->conf_chan.rx_chan_mod=1;


    // --------------------------------------------
    // Reset everything
    // --------------------------------------------
    fifo_reset(dev);
    dma_reset(dev);

    // Device reset
    dev->conf.val = 0;
    dev->conf.rx_reset=1;
    dev->conf.tx_reset=1;
    dev->conf.rx_reset=0;
    dev->conf.tx_reset=0;

    dev->conf1.val = 0;
    dev->conf1.tx_stop_en = 0;
    dev->timing.val = 0;

    // Allocate DMA descriptors
    i2s_state[i2snum(dev)] =
        (i2s_parallel_state_t *)malloc(sizeof(i2s_parallel_state_t));
    i2s_parallel_state_t *st = i2s_state[i2snum(dev)];
    st->desccount_a = calc_needed_dma_descs_for(cfg->bufa);
    st->dmadesc_a = (volatile lldesc_t *)heap_caps_malloc(
        st->desccount_a * sizeof(lldesc_t), MALLOC_CAP_DMA
    );
    // and fill them
    fill_dma_desc(st->dmadesc_a, cfg->bufa);

    if (cfg->bufb) {
        st->desccount_b = calc_needed_dma_descs_for(cfg->bufb);
        st->dmadesc_b = (volatile lldesc_t *)heap_caps_malloc(
            st->desccount_b*sizeof(lldesc_t), MALLOC_CAP_DMA
        );
        fill_dma_desc(st->dmadesc_b, cfg->bufb);
    } else {
        st->desccount_b = 0;
    }

    // Reset FIFO/DMA -> needed? Doesn't dma_reset/fifo_reset do this?
    dev->lc_conf.in_rst = 1;
    dev->lc_conf.out_rst = 1;
    dev->lc_conf.ahbm_rst = 1;
    dev->lc_conf.ahbm_fifo_rst = 1;
    dev->lc_conf.in_rst = 0;
    dev->lc_conf.out_rst = 0;
    dev->lc_conf.ahbm_rst = 0;
    dev->lc_conf.ahbm_fifo_rst = 0;
    dev->conf.tx_reset = 1;
    dev->conf.tx_fifo_reset = 1;
    dev->conf.rx_fifo_reset = 1;
    dev->conf.tx_reset = 0;
    dev->conf.tx_fifo_reset = 0;
    dev->conf.rx_fifo_reset = 0;

    // Start dma on front buffer
    dev->lc_conf.val =
        I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN | I2S_OUT_DATA_BURST_EN;
    dev->out_link.addr = ((uint32_t)(&st->dmadesc_a[0]));
    dev->out_link.start = 1;
    dev->conf.tx_start = 1;
}

void i2s_parallel_flip_to_buffer(i2s_dev_t *dev, int bufid) {
    int no = i2snum(dev);

    if (i2s_state[no] == NULL)
        return;

    // not using double buffering mode
    if (i2s_state[no]->desccount_b <= 0)
        return;

    lldesc_t *active_dma_chain;
    if (bufid==0) {
        active_dma_chain=(lldesc_t*)&i2s_state[no]->dmadesc_a[0];
    } else {
        active_dma_chain=(lldesc_t*)&i2s_state[no]->dmadesc_b[0];
    }

    i2s_state[no]->dmadesc_a[i2s_state[no]->desccount_a-1].qe.stqe_next=active_dma_chain;
    i2s_state[no]->dmadesc_b[i2s_state[no]->desccount_b-1].qe.stqe_next=active_dma_chain;
}
