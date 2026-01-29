#include "sd_logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "esp_log.h"
#include "ff.h"

static const char *TAG = "SD_LOG";
static FIL log_file;
static bool is_logging = false;
static vprintf_like_t default_vprintf = NULL;

//vprintf（直接寫入檔案）
static int sd_log_vprintf(const char *fmt, va_list l) {
    /*
    
    int ret_len = 0;
    
    //original terminal (UART)
    if (default_vprintf) {
        va_list l_copy;
        va_copy(l_copy, l);
        ret_len = default_vprintf(fmt, l_copy);
        va_end(l_copy);
    }
    */

    //direct into SD
    if (is_logging) {
        char buffer[256];
        int len = vsnprintf(buffer, sizeof(buffer), fmt, l);
        if (len > 0 && len < (int)sizeof(buffer)) {
            UINT bw;
            FRESULT fr = f_write(&log_file, buffer, len, &bw);
            static int write_count = 0;
            if (++write_count >= 10) {
                f_sync(&log_file);
                write_count = 0;
            }
        }
        return len;
    }
    return 0;
    //return ret_len;
}

esp_err_t sd_logger_init(const char* log_path){
    if (!log_path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (is_logging) {
        ESP_LOGW(TAG, "SD Logger INIT already");
        return ESP_ERR_INVALID_STATE;
    }

    FRESULT fr = f_open(&log_file, log_path, FA_WRITE);
    
    if (fr != FR_OK) {
        //error msg
        const char* err_str;
        switch(fr) {
            case FR_DISK_ERR:       err_str = "DISK_ERR"; break;
            case FR_INT_ERR:        err_str = "INT_ERR"; break;
            case FR_NOT_READY:      err_str = "NOT_READY"; break;
            case FR_NO_FILE:        err_str = "NO_FILE"; break;
            case FR_NO_PATH:        err_str = "NO_PATH"; break;
            case FR_INVALID_NAME:   err_str = "INVALID_NAME"; break;
            case FR_DENIED:         err_str = "DENIED"; break;
            case FR_EXIST:          err_str = "EXIST"; break;
            case FR_INVALID_OBJECT: err_str = "INVALID_OBJECT"; break;
            case FR_WRITE_PROTECTED:err_str = "WRITE_PROTECTED"; break;
            case FR_INVALID_DRIVE:  err_str = "INVALID_DRIVE"; break;
            case FR_NOT_ENABLED:    err_str = "NOT_ENABLED"; break;
            case FR_NO_FILESYSTEM:  err_str = "NO_FILESYSTEM"; break;
            case FR_MKFS_ABORTED:   err_str = "MKFS_ABORTED"; break;
            case FR_TIMEOUT:        err_str = "TIMEOUT"; break;
            default:                err_str = "UNKNOWN"; break;
        }
        ESP_LOGE(TAG, "Unable open Log file %s: %d (%s)", log_path, fr, err_str);
        return ESP_FAIL;
    }

    is_logging = true;
    
    default_vprintf = esp_log_set_vprintf(sd_log_vprintf);
    const char* start_msg = "\n========== Logger Session Start ==========\n";
    UINT bw;
    f_write(&log_file, start_msg, strlen(start_msg), &bw);
    f_sync(&log_file);
    ESP_LOGI(TAG, "SD Logger start at: %s", log_path);
    return ESP_OK;
}

void sd_logger_deinit(void) {
    if (!is_logging) {
        return;
    }

    const char* end_msg = "========== Logger Session End ==========\n\n";
    UINT bw;
    f_write(&log_file, end_msg, strlen(end_msg), &bw);
    
    is_logging = false;
    
    if (default_vprintf){
        esp_log_set_vprintf(default_vprintf);
        default_vprintf = NULL;
    }
    
    f_sync(&log_file);
    f_close(&log_file);
    ESP_LOGI(TAG, "SD Logger closed");
}

// direct into Log (optional)
int sd_log_printf(const char* format, ...) {
    if (!is_logging) {
        return -1;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < (int)sizeof(buffer)) {
        UINT bw;
        FRESULT fr = f_write(&log_file, buffer, len, &bw);
        if (fr != FR_OK) {
            return -2;
        }
        if (bw != (UINT)len) {
            return -3;
        }
        return len;
    }
    return -4;
}