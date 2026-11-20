#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t delayed_execute(void (*task)(void*), void* user_ctx, size_t delayed_us);

#ifdef __cplusplus
}
#endif
