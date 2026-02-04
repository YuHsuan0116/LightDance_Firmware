#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "readframe.h"
#include "board.h"
#include "table_frame.h"
#include "sd_logger.h"

static const char *TAG = "main_test";

// 模式定義
typedef enum {
    MODE_W = 0,  // Write模式：讀取frame並記錄日誌
    MODE_R,      // Read模式：讀取日誌檔案並輸出到終端
    MODE_T       // Test模式：測試寫入效能
} operation_mode_t;

// 設定模式（在這裡更改）
static const operation_mode_t CURRENT_MODE = MODE_T;  // 改為 MODE_W, MODE_R, 或 MODE_T

// 路徑定義
static const char *control_path = "0:/control.dat";
static const char *frame_path = "0:/frame.dat";
static const char *log_path = "/sd/A.log";

// ==================== 模式W：寫入模式 ====================
static void mode_write(void) {
    ESP_LOGI(TAG, "===== MODE W: Write Mode =====");
    ESP_LOGI(TAG, "Will read frames and log to: %s", log_path);
    
    // 初始化系統
    esp_err_t ret = frame_system_init(control_path, frame_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System INIT failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "System INIT success");
    
    // 初始化SD日誌器
    ret = sd_logger_init(log_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD Logger INIT fail");
        frame_system_deinit();
        return;
    }
    ESP_LOGI(TAG, "SD Logger INIT success");
    
    // 讀取並顯示通道資訊
    ch_info_t info = get_channel_info();
    ESP_LOGI(TAG, "Channels: ");
    for (int i = 0; i < WS2812B_NUM; i++) {
        if (info.rmt_strips[i] > 0) {
            ESP_LOGI(TAG, "Strip[%d]: %d LEDs", i, info.rmt_strips[i]);
        }
    }
    
    // 開始讀取幀
    table_frame_t current_frame;
    uint32_t frame_count = 0;
    
    ESP_LOGI(TAG, "START reading frames...");
    
    while (1) {
        ret = read_frame(&current_frame);
        
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "EOF reached. Total frames: %u", frame_count);
            break;
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error: %s (Frame: %u)", esp_err_to_name(ret), frame_count);
            continue;
        }
        
        frame_count++;
        
        // 簡單記錄每幀資訊
        ESP_LOGI(TAG, "Frame %u: timestamp %llu", frame_count, current_frame.timestamp);
    }
    
    ESP_LOGI(TAG, "Write mode completed. Total frames: %u", frame_count);
    
    // 清理
    frame_system_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    sd_logger_deinit();
}

// ==================== 模式R：讀取模式 ====================
static void mode_read(void) {
    ESP_LOGI(TAG, "===== MODE R: Read Mode =====");
    ESP_LOGI(TAG, "Will read log file: %s", log_path);
    
    esp_err_t ret = frame_system_init(control_path, frame_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System INIT failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "System INIT success");

    // 打開日誌檔案
    FILE* log_file = fopen(log_path, "r");
    if (log_file == NULL) {
        ESP_LOGE(TAG, "Failed to open log file: %s", log_path);
        return;
    }
    
    char buffer[256];
    uint32_t line_count = 0;
    
    ESP_LOGI(TAG, "--- Start of log file ---");
    
    // 逐行讀取並輸出
    while (fgets(buffer, sizeof(buffer), log_file) != NULL) {
        // 移除換行符號
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        
        // 輸出到終端
        printf("[LOG Line %lu] %s\n", ++line_count, buffer);
        
        // 每100行暫停一下
        if (line_count % 100 == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    ESP_LOGI(TAG, "--- End of log file ---");
    ESP_LOGI(TAG, "Total lines: %u", line_count);
    
    fclose(log_file);
}

// ==================== 模式T：測試模式 ====================
static void mode_test(void) {
    ESP_LOGI(TAG, "===== MODE T: Performance Test Mode =====");
    ESP_LOGI(TAG, "Testing write performance...");
    
    // 測試參數
    const size_t TEST_SIZE = 1000000;        // 每次寫入 1000 bytes
    const int TOTAL_TESTS = 1000;         // 重複 1000 次
    const int REPORT_INTERVAL = 100;      // 每100次報告一次
    
    // 初始化SD日誌器
    // 初始化系統
    esp_err_t ret = frame_system_init(control_path, frame_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System INIT failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "System INIT success");
    
    // 初始化SD日誌器
    ret = sd_logger_init(log_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD Logger INIT fail");
        frame_system_deinit();
        return;
    }
    ESP_LOGI(TAG, "SD Logger INIT success");
    
    // 準備測試資料（1000 bytes）
    char test_data[TEST_SIZE + 1];  // +1 for null terminator
    
    // 填充有意義的測試資料
    for (size_t i = 0; i < TEST_SIZE; i++) {
        // 創建可讀的測試資料
        test_data[i] = 'A' + (i % 26);  // A-Z循環
    }
    test_data[TEST_SIZE] = '\0';  // 確保字串結尾
    
    // 統計數據 - 改為動態計算，不儲存所有數據
    uint64_t total_time_us = 0;
    uint64_t min_time_us = UINT64_MAX;
    uint64_t max_time_us = 0;
    uint64_t sum_squared_us = 0;  // 用於計算標準差
    
    // 用於標準差計算的累加變數（不儲存所有數據）
    double m = 0.0;  // 平均值
    double s = 0.0;  // 平方和
    
    ESP_LOGI(TAG, "Starting performance test:");
    ESP_LOGI(TAG, "  Test size: %d bytes", TEST_SIZE);
    ESP_LOGI(TAG, "  Total tests: %d", TOTAL_TESTS);
    ESP_LOGI(TAG, "  Total data: %.2f KB", (TEST_SIZE * TOTAL_TESTS) / 1024.0);
    
    // ========== 主測試循環 ==========
    for (int test_num = 0; test_num < TOTAL_TESTS; test_num++) {
        // 記錄開始時間
        int64_t start_us = esp_timer_get_time();
        
        // ========== 寫入測試資料 ==========
        // 簡單寫入測試
        sd_log_printf("[TEST%04d] Data length: %d bytes\n", test_num + 1, TEST_SIZE);
        
        // 記錄結束時間
        int64_t end_us = esp_timer_get_time();
        int64_t duration_us = end_us - start_us;
        
        // 更新統計 - 使用Welford's online algorithm計算標準差
        total_time_us += duration_us;
        
        // 更新最小最大值
        if (duration_us < min_time_us) {
            min_time_us = duration_us;
        }
        if (duration_us > max_time_us) {
            max_time_us = duration_us;
        }
        
        // Welford's online algorithm for variance
        double delta = (double)duration_us - m;
        m += delta / (test_num + 1);
        double delta2 = (double)duration_us - m;
        s += delta * delta2;
        
        // 每 REPORT_INTERVAL 次或第一次輸出進度
        if ((test_num + 1) % REPORT_INTERVAL == 0 || test_num == 0) {
            double current_avg = (double)total_time_us / (test_num + 1);
            double throughput = 0;
            
            if (total_time_us > 0) {
                throughput = ((test_num + 1) * TEST_SIZE * 8.0) / 
                           (total_time_us / 1000000.0);
            }
            
            ESP_LOGI(TAG, "[%04d/%04d] Time: %lld µs | Avg: %.1f µs | Throughput: %.1f bps", 
                     test_num + 1, TOTAL_TESTS, duration_us, current_avg, throughput);
        }
        
        // 避免太快
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // ========== 計算統計結果 ==========
    double avg_time_us = (double)total_time_us / TOTAL_TESTS;
    double total_time_sec = total_time_us / 1000000.0;
    double total_data_kb = (TEST_SIZE * TOTAL_TESTS) / 1024.0;
    
    // 計算標準差（使用Welford's algorithm結果）
    double variance = 0;
    double std_dev_us = 0;
    if (TOTAL_TESTS > 1) {
        variance = s / (TOTAL_TESTS - 1);
        std_dev_us = sqrt(variance);
    }
    
    // 計算吞吐量
    double avg_throughput = 0;
    if (total_time_us > 0) {
        avg_throughput = (TOTAL_TESTS * TEST_SIZE * 8.0) / total_time_sec;
    }
    
    // ========== 輸出最終報告 ==========
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "        PERFORMANCE TEST RESULTS");
    ESP_LOGI(TAG, "==============================================");
    
    // 測試參數
    ESP_LOGI(TAG, "Test Parameters:");
    ESP_LOGI(TAG, "  Test size per write: %d bytes", TEST_SIZE);
    ESP_LOGI(TAG, "  Number of tests:     %d", TOTAL_TESTS);
    ESP_LOGI(TAG, "  Total data written:  %.2f KB", total_data_kb);
    ESP_LOGI(TAG, "  Total test time:     %.3f seconds", total_time_sec);
    
    // 時間統計
    ESP_LOGI(TAG, "Time Statistics (microseconds):");
    ESP_LOGI(TAG, "  Average time:        %.2f µs", avg_time_us);
    ESP_LOGI(TAG, "  Minimum time:        %llu µs", min_time_us);
    ESP_LOGI(TAG, "  Maximum time:        %llu µs", max_time_us);
    ESP_LOGI(TAG, "  Standard deviation:  %.2f µs", std_dev_us);
    ESP_LOGI(TAG, "  Coefficient of variation: %.2f%%", 
             (std_dev_us / avg_time_us) * 100.0);
    
    // 性能指標
    ESP_LOGI(TAG, "Performance Metrics:");
    ESP_LOGI(TAG, "  Average throughput:  %.1f bps", avg_throughput);
    ESP_LOGI(TAG, "  Average throughput:  %.2f KB/s", avg_throughput / (8 * 1024));
    ESP_LOGI(TAG, "  Operations per second: %.1f ops", TOTAL_TESTS / total_time_sec);
    
    ESP_LOGI(TAG, "==============================================");
    
    // 清理
    ESP_LOGI(TAG, "Test completed, cleaning up...");
    vTaskDelay(pdMS_TO_TICKS(100));
    sd_logger_deinit();
}
// ==================== 主任務 ====================
void test_task(void *pvParameters) {
    ESP_LOGI(TAG, "===== Starting in Mode %c =====", 
             CURRENT_MODE == MODE_W ? 'W' : 
             CURRENT_MODE == MODE_R ? 'R' : 'T');
    
    // 根據模式執行對應功能
    switch (CURRENT_MODE) {
        case MODE_W:
            mode_write();
            break;
        case MODE_R:
            mode_read();
            break;
        case MODE_T:
            mode_test();
            break;
        default:
            ESP_LOGE(TAG, "Unknown mode: %d", CURRENT_MODE);
            break;
    }
    
    ESP_LOGI(TAG, "===== Mode %c completed =====",
             CURRENT_MODE == MODE_W ? 'W' : 
             CURRENT_MODE == MODE_R ? 'R' : 'T');
    
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    xTaskCreatePinnedToCore(
        test_task,
        "test_task",
        12288,  // 減少堆疊大小
        NULL,
        5,
        NULL,
        0
    );
}