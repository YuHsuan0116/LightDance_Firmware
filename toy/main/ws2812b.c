#include "ws2812b.h"

#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

static const char* TAG = "ws2812b";

static const rmt_transmit_config_t tx_config = {
    .loop_count = 0,
};

#define WS2812B_RMT_RESOLUTION_HZ 10000000
#define WS2812B_T0H_US 0.4f
#define WS2812B_T0L_US 0.85f
#define WS2812B_T1H_US 0.8f
#define WS2812B_T1L_US 0.45f
#define WS2812B_RESET_US 50

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t* bytes_encoder;
    rmt_encoder_t* copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} encoder_t;

RMT_ENCODER_FUNC_ATTR
static size_t ws2812b_encode(rmt_encoder_t* rmt_encoder, rmt_channel_handle_t rmt_channel,
                             const void* buffer, size_t buffer_size,
                             rmt_encode_state_t* ret_state) {
    encoder_t* ws2812b_encoder = __containerof(rmt_encoder, encoder_t, base);

    rmt_encoder_handle_t bytes_encoder = ws2812b_encoder->bytes_encoder;

    rmt_encoder_handle_t copy_encoder = ws2812b_encoder->copy_encoder;

    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;

    size_t encoded_symbols = 0;

    switch (ws2812b_encoder->state) {
        case 0:
            encoded_symbols += bytes_encoder->encode(bytes_encoder, rmt_channel, buffer,
                                                     buffer_size, &session_state);

            if (session_state & RMT_ENCODING_COMPLETE) {
                ws2812b_encoder->state = 1;
            }

            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;
            }

            // fall-through

        case 1:
            encoded_symbols +=
                copy_encoder->encode(copy_encoder, rmt_channel, &ws2812b_encoder->reset_code,
                                     sizeof(ws2812b_encoder->reset_code), &session_state);

            if (session_state & RMT_ENCODING_COMPLETE) {
                ws2812b_encoder->state = RMT_ENCODING_RESET;
                state |= RMT_ENCODING_COMPLETE;
            }

            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;
            }
    }

out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t ws2812b_del_encoder(rmt_encoder_t* rmt_encoder) {
    encoder_t* ws2812b_encoder = __containerof(rmt_encoder, encoder_t, base);

    esp_err_t ret = ESP_OK;

    if (ws2812b_encoder->bytes_encoder) {
        esp_err_t err = rmt_del_encoder(ws2812b_encoder->bytes_encoder);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "bytes encoder deletion failed: err=%s", esp_err_to_name(err));

            ret = err;
        }
    }

    if (ws2812b_encoder->copy_encoder) {
        esp_err_t err = rmt_del_encoder(ws2812b_encoder->copy_encoder);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "copy encoder deletion failed: err=%s", esp_err_to_name(err));

            if (ret == ESP_OK) {
                ret = err;
            }
        }
    }

    free(ws2812b_encoder);

    return ret;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t ws2812b_encoder_reset(rmt_encoder_t* rmt_encoder) {
    encoder_t* ws2812b_encoder = __containerof(rmt_encoder, encoder_t, base);

    ESP_RETURN_ON_ERROR(rmt_encoder_reset(ws2812b_encoder->bytes_encoder), TAG,
                        "bytes encoder reset failed");

    ESP_RETURN_ON_ERROR(rmt_encoder_reset(ws2812b_encoder->copy_encoder), TAG,
                        "copy encoder reset failed");

    ws2812b_encoder->state = RMT_ENCODING_RESET;

    return ESP_OK;
}

static esp_err_t ws2812b_new_encoder(rmt_encoder_handle_t* ret_encoder) {
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(ret_encoder, ESP_ERR_INVALID_ARG, TAG, "encoder output pointer is NULL");

    encoder_t* ws2812b_encoder = rmt_alloc_encoder_mem(sizeof(encoder_t));

    ESP_GOTO_ON_FALSE(ws2812b_encoder, ESP_ERR_NO_MEM, err, TAG, "encoder allocation failed");

    ws2812b_encoder->base.encode = ws2812b_encode;
    ws2812b_encoder->base.del = ws2812b_del_encoder;
    ws2812b_encoder->base.reset = ws2812b_encoder_reset;

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 =
            {
                .level0 = 1,
                .duration0 = WS2812B_T0H_US * WS2812B_RMT_RESOLUTION_HZ / 1000000,

                .level1 = 0,
                .duration1 = WS2812B_T0L_US * WS2812B_RMT_RESOLUTION_HZ / 1000000,
            },

        .bit1 =
            {
                .level0 = 1,
                .duration0 = WS2812B_T1H_US * WS2812B_RMT_RESOLUTION_HZ / 1000000,

                .level1 = 0,
                .duration1 = WS2812B_T1L_US * WS2812B_RMT_RESOLUTION_HZ / 1000000,
            },

        .flags.msb_first = 1,
    };

    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &ws2812b_encoder->bytes_encoder),
                      err, TAG, "bytes encoder creation failed");

    rmt_copy_encoder_config_t copy_encoder_config = {};

    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &ws2812b_encoder->copy_encoder),
                      err, TAG, "copy encoder creation failed");

    uint32_t reset_ticks = WS2812B_RMT_RESOLUTION_HZ / 1000000 * WS2812B_RESET_US / 2;

    ws2812b_encoder->reset_code = (rmt_symbol_word_t){
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };

    *ret_encoder = &ws2812b_encoder->base;

    return ESP_OK;

err:
    if (ws2812b_encoder) {
        if (ws2812b_encoder->bytes_encoder) {
            rmt_del_encoder(ws2812b_encoder->bytes_encoder);
        }

        if (ws2812b_encoder->copy_encoder) {
            rmt_del_encoder(ws2812b_encoder->copy_encoder);
        }

        free(ws2812b_encoder);
    }

    return ret;
}

static esp_err_t ws2812b_transmit(ws2812b_dev_t* strip, const void* data, size_t size_bytes) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, TAG, "transmission rejected: strip is NULL");

    ESP_RETURN_ON_FALSE(strip->rmt_channel && strip->rmt_encoder, ESP_ERR_INVALID_STATE, TAG,
                        "transmission rejected: device is not initialized");

    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "transmission rejected: data is NULL");

    ESP_RETURN_ON_ERROR(
        rmt_transmit(strip->rmt_channel, strip->rmt_encoder, data, size_bytes, &tx_config), TAG,
        "transmission failed: gpio=%d size=%u", strip->gpio_num, (unsigned)size_bytes);

    return ESP_OK;
}

esp_err_t ws2812b_init(ws2812b_dev_t* strip, gpio_num_t gpio_num) {
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, TAG, "initialization rejected: strip is NULL");

    *strip = (ws2812b_dev_t){
        .gpio_num = gpio_num,
    };

    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = gpio_num,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812B_RMT_RESOLUTION_HZ,
        .intr_priority = 0,
        .mem_block_symbols = 64,
        .trans_queue_depth = 8,
        .flags.with_dma = 0,
    };

    ESP_GOTO_ON_ERROR(ws2812b_new_encoder(&strip->rmt_encoder), err, TAG,
                      "encoder creation failed: gpio=%d", gpio_num);

    ESP_GOTO_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &strip->rmt_channel), err, TAG,
                      "RMT channel creation failed: gpio=%d", gpio_num);

    ESP_GOTO_ON_ERROR(gpio_set_drive_capability(gpio_num, GPIO_DRIVE_CAP_0), err, TAG,
                      "GPIO drive capability setup failed: gpio=%d", gpio_num);

    ESP_GOTO_ON_ERROR(rmt_enable(strip->rmt_channel), err, TAG,
                      "RMT channel enable failed: gpio=%d", gpio_num);

    ESP_LOGI(TAG, "initialized: gpio=%d", gpio_num);

    return ESP_OK;

err:
    ESP_LOGE(TAG, "initialization failed: gpio=%d err=%s", gpio_num, esp_err_to_name(ret));

    if (strip->rmt_channel) {
        esp_err_t cleanup_err = rmt_del_channel(strip->rmt_channel);

        if (cleanup_err != ESP_OK) {
            ESP_LOGW(TAG, "RMT channel cleanup failed: gpio=%d err=%s", gpio_num,
                     esp_err_to_name(cleanup_err));
        }

        strip->rmt_channel = NULL;
    }

    if (strip->rmt_encoder) {
        esp_err_t cleanup_err = rmt_del_encoder(strip->rmt_encoder);

        if (cleanup_err != ESP_OK) {
            ESP_LOGW(TAG, "encoder cleanup failed: gpio=%d err=%s", gpio_num,
                     esp_err_to_name(cleanup_err));
        }

        strip->rmt_encoder = NULL;
    }

    return ret;
}

esp_err_t ws2812b_set_grb_n(ws2812b_dev_t* strip, const grb8_t* pixels, size_t count) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, TAG, "transmission rejected: strip is NULL");

    if (count == 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(pixels, ESP_ERR_INVALID_ARG, TAG, "transmission rejected: pixels is NULL");

    return ws2812b_transmit(strip, pixels, count * sizeof(grb8_t));
}

esp_err_t ws2812b_wait_done(ws2812b_dev_t* strip) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, TAG, "wait rejected: strip is NULL");

    ESP_RETURN_ON_FALSE(strip->rmt_channel, ESP_ERR_INVALID_STATE, TAG,
                        "wait rejected: RMT channel is not initialized");

    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(strip->rmt_channel, -1), TAG, "wait failed: gpio=%d",
                        strip->gpio_num);

    return ESP_OK;
}

esp_err_t ws2812b_deinit(ws2812b_dev_t* strip) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, TAG,
                        "deinitialization rejected: strip is NULL");

    esp_err_t ret = ESP_OK;

    if (strip->rmt_channel) {
        esp_err_t err = rmt_disable(strip->rmt_channel);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "RMT channel disable failed: gpio=%d err=%s", strip->gpio_num,
                     esp_err_to_name(err));

            ret = err;
        }

        err = rmt_del_channel(strip->rmt_channel);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "RMT channel deletion failed: gpio=%d err=%s", strip->gpio_num,
                     esp_err_to_name(err));

            if (ret == ESP_OK) {
                ret = err;
            }
        } else {
            strip->rmt_channel = NULL;
        }
    }

    if (strip->rmt_encoder) {
        esp_err_t err = rmt_del_encoder(strip->rmt_encoder);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "encoder deletion failed: gpio=%d err=%s", strip->gpio_num,
                     esp_err_to_name(err));

            if (ret == ESP_OK) {
                ret = err;
            }
        } else {
            strip->rmt_encoder = NULL;
        }
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "deinitialized: gpio=%d", strip->gpio_num);
    }

    return ret;
}
