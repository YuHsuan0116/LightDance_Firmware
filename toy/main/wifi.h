#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_connect(const char* ssid, const char* pwd);

#ifdef __cplusplus
}
#endif
