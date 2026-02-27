SD Logger Module for ESP32
===

SD Logger 會將 ESP_LOGx 的所有輸出重新導向到 SD 卡上的檔案。

## 1. Initialize
```
#include "sd_logger.h"

esp_err_t ret = sd_log_init("/sd/LOGGER.log");
if (ret != ESP_OK) {
    ESP_LOGE("APP", "SD Logger init failed: %s", esp_err_to_name(ret));
}
```
初始化後，所有 ESP_LOGI、ESP_LOGE、ESP_LOGW 等輸出都會自動寫入 "/sd/LOGGER.log"

## 2. deinit
```
esp_err_t ret = sd_log_deinit();
if (ret != ESP_OK) {
    // 處理錯誤
}
```