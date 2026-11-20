#pragma once

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t g;
    uint8_t r;
    uint8_t b;
} grb8_t;

typedef struct {
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t rmt_encoder;

    gpio_num_t gpio_num;
} ws2812b_dev_t;

esp_err_t ws2812b_init(ws2812b_dev_t* strip, gpio_num_t gpio_num);
esp_err_t ws2812b_set_grb_n(ws2812b_dev_t* strip, const grb8_t* pixels, size_t count);
esp_err_t ws2812b_wait_done(ws2812b_dev_t* strip);
esp_err_t ws2812b_deinit(ws2812b_dev_t* strip);

#ifdef __cplusplus
}
#endif
