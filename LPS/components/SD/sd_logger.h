#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SD 卡 Log 系統
 * @param log_path 檔案路徑，例如 "0:/system.log"
 * @return esp_err_t 
 */
esp_err_t sd_logger_init(const char* log_path);

/**
 * @brief 停止 Log 重定向並關閉檔案
 */
void sd_logger_deinit(void);

/**
 * @brief 直接寫入 Log 到 SD 卡（不經過 Queue/Task）
 * @param format 格式化字串
 * @param ... 參數
 * @return int 寫入的位元組數，<0 表示錯誤
 */
int sd_log_printf(const char* format, ...);

#ifdef __cplusplus
}
#endif