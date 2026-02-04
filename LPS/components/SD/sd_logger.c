#include "sd_logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "esp_log.h"
#include <errno.h>
#include "esp_vfs_fat.h" 

static const char *TAG = "SD_LOG";
//static FIL log_file;
static FILE* log_file = NULL;

static bool is_logging = false;
static vprintf_like_t default_vprintf = NULL;

//vprintf（直接寫入檔案）
static int sd_log_vprintf(const char *fmt, va_list l) {
    
    
    int ret_len = 0;
    
    //original terminal (UART)
    if (default_vprintf) {
        va_list l_copy;
        va_copy(l_copy, l);
        ret_len = default_vprintf(fmt, l_copy);
        va_end(l_copy);
    }
    

    //direct into SD

    /*
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
    */

    if (is_logging && log_file) {
        // 直接使用 vfprintf，不需要手動格式化
        int len = vfprintf(log_file, fmt, l);
        
        static int write_count = 0;
        if (++write_count >= 10) {
            fflush(log_file);  // 替代 f_sync
            write_count = 0;
        }
        return len;
    }
    return 0;
    //return ret_len;
}

esp_err_t sd_logger_init(const char* log_path){
    if (!log_path) return ESP_ERR_INVALID_ARG;
    if (is_logging) return ESP_ERR_INVALID_STATE;

    //FRESULT fr = f_open(&log_file, log_path, FA_WRITE);
    const char* vfs_path = log_path;
    // 使用標準 C 庫打開檔案
    log_file = fopen(vfs_path, "w+");  // "a" 表示追加模式
    if (log_file == NULL) {
        ESP_LOGE(TAG, "Failed to open log file: %s (errno: %d)", vfs_path, errno);
        return ESP_FAIL;
    }
    /*
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
        */

    is_logging = true;
    
    default_vprintf = esp_log_set_vprintf(sd_log_vprintf);
    /*
    const char* start_msg = "\n========== Logger Session Start ==========\n";
    UINT bw;
    f_write(&log_file, start_msg, strlen(start_msg), &bw);
    f_sync(&log_file);
    ESP_LOGI(TAG, "SD Logger start at: %s", log_path);
    */

    fprintf(log_file, "\n========== Logger Session Start ==========\n");
    fflush(log_file);
    
    ESP_LOGI(TAG, "SD Logger started at: %s", vfs_path);
    return ESP_OK;
}

void sd_logger_deinit(void) {
    if (!is_logging) return;
    /*
    const char* end_msg = "========== Logger Session End ==========\n\n";
    UINT bw;
    f_write(&log_file, end_msg, strlen(end_msg), &bw);
    */
    fprintf(log_file, "========== Logger Session End ==========\n\n");


    is_logging = false;
    
    if (default_vprintf){
        esp_log_set_vprintf(default_vprintf);
        default_vprintf = NULL;
    }
    
    //f_sync(&log_file);
    //f_close(&log_file);
    if (log_file) {
        fflush(log_file);      // 確保所有資料寫入
        fclose(log_file);      // 關閉檔案
        log_file = NULL;
    }

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
        /*
        
        UINT bw;
        FRESULT fr = f_write(&log_file, buffer, len, &bw);
        if (fr != FR_OK) {
            return -2;
        }
        if (bw != (UINT)len) {
            return -3;
        }
            */
        if (!is_logging || !log_file) return -1;
    
        va_list args;
        va_start(args, format);
        int len = vfprintf(log_file, format, args);
        va_end(args);
        return len;
    }
    return -4;
}