#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_init();
esp_err_t app_send_play();
esp_err_t app_send_test_solid(uint8_t r, uint8_t g, uint8_t b);
esp_err_t app_send_test_rainbow();
esp_err_t app_send_stop();

#ifdef __cplusplus
}
#endif
