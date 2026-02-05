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

static const char *TAG = "sd_test";

// 測試參數
#define TEST_DATA_SIZE  1024    // 每次寫入 1KB
#define TEST_ITERATIONS 1000    // 測試 1000 次
#define REPORT_INTERVAL 100     // 每100次報告一次

// 性能測試函數
static void test_sd_write_performance(void) {
    ESP_LOGI(TAG, "===== SD Card Write Performance Test =====");
    ESP_LOGI(TAG, "Config: %d bytes x %d iterations", TEST_DATA_SIZE, TEST_ITERATIONS);
    
    // 初始化系統（掛載SD卡）
    esp_err_t ret = frame_system_init("0:/control.dat", "0:/frame.dat");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // 初始化日誌器
    ret = sd_logger_init("/sd/logger.log");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD Logger init failed");
        frame_system_deinit();
        return;
    }
    
    ESP_LOGI(TAG, "Starting performance test...");
    
    // 準備測試資料（1KB）
    char test_data[TEST_DATA_SIZE];
    memset(test_data, 'X', TEST_DATA_SIZE - 1);
    test_data[TEST_DATA_SIZE - 1] = '\n';
    
    // 性能統計變數
    uint64_t total_time_us = 0;
    uint64_t min_time_us = UINT64_MAX;
    uint64_t max_time_us = 0;
    double m = 0.0;  // 用於Welford算法的平均值
    double s = 0.0;  // 用於Welford算法的平方和
    
    // 主測試循環
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        // 記錄開始時間
        int64_t start_us = esp_timer_get_time();
        
        // ========== 測試1: sd_log_printf ==========
        sd_log_printf("[TEST%04d] ", i + 1);
        sd_log_printf("%.*s", TEST_DATA_SIZE - 10, test_data);
        
        // ========== 測試2: 直接寫入（可選）==========
        // 如果需要測試更底層的寫入，可以在此添加
        
        // 記錄結束時間
        int64_t end_us = esp_timer_get_time();
        int64_t duration_us = end_us - start_us;
        
        // 更新統計數據
        total_time_us += duration_us;
        
        if (duration_us < min_time_us) min_time_us = duration_us;
        if (duration_us > max_time_us) max_time_us = duration_us;
        
        // 使用Welford在線算法計算標準差
        double delta = (double)duration_us - m;
        m += delta / (i + 1);
        double delta2 = (double)duration_us - m;
        s += delta * delta2;
        
        // 定期報告進度
        if ((i + 1) % REPORT_INTERVAL == 0 || i == 0) {
            double current_avg = (double)total_time_us / (i + 1);
            double throughput = 0;
            
            if (total_time_us > 0) {
                throughput = ((i + 1) * TEST_DATA_SIZE * 8.0) / 
                           (total_time_us / 1000000.0);
            }
            
            ESP_LOGI(TAG, "[%04d/%04d] Time: %lld µs, Avg: %.1f µs, Throughput: %.1f bps", 
                     i + 1, TEST_ITERATIONS, duration_us, current_avg, throughput);
        }
    }
    
    // 計算最終統計結果
    double avg_time_us = (double)total_time_us / TEST_ITERATIONS;
    double total_time_sec = total_time_us / 1000000.0;
    double total_data_mb = (TEST_DATA_SIZE * TEST_ITERATIONS) / (1024.0 * 1024.0);
    
    // 計算標準差
    double variance = (TEST_ITERATIONS > 1) ? s / (TEST_ITERATIONS - 1) : 0;
    double std_dev_us = sqrt(variance);
    
    // 計算吞吐量
    double avg_throughput = (total_time_us > 0) ? 
        (TEST_ITERATIONS * TEST_DATA_SIZE * 8.0) / total_time_sec : 0;
    
    // ========== 輸出最終報告 ==========
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "          SD CARD PERFORMANCE REPORT");
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "Test Configuration:");
    ESP_LOGI(TAG, "  Write size:      %d bytes", TEST_DATA_SIZE);
    ESP_LOGI(TAG, "  Iterations:      %d", TEST_ITERATIONS);
    ESP_LOGI(TAG, "  Total data:      %.3f MB", total_data_mb);
    ESP_LOGI(TAG, "  Total time:      %.3f seconds", total_time_sec);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Write Performance (per %d bytes):", TEST_DATA_SIZE);
    ESP_LOGI(TAG, "  Average time:    %.2f µs", avg_time_us);
    ESP_LOGI(TAG, "  Minimum time:    %llu µs", min_time_us);
    ESP_LOGI(TAG, "  Maximum time:    %llu µs", max_time_us);
    ESP_LOGI(TAG, "  Standard deviation: %.2f µs", std_dev_us);
    ESP_LOGI(TAG, "  Coefficient of variation: %.1f%%", 
             (std_dev_us / avg_time_us) * 100.0);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Performance Metrics:");
    ESP_LOGI(TAG, "  Average throughput:  %.1f bps", avg_throughput);
    ESP_LOGI(TAG, "  Average throughput:  %.2f KB/s", avg_throughput / (8 * 1024));
    ESP_LOGI(TAG, "  Average throughput:  %.3f MB/s", avg_throughput / (8 * 1024 * 1024));
    ESP_LOGI(TAG, "  Write operations/sec: %.1f", TEST_ITERATIONS / total_time_sec);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Summary:");
    ESP_LOGI(TAG, "  %.3f MB written in %.3f seconds", total_data_mb, total_time_sec);
    ESP_LOGI(TAG, "  Average: %.2f µs per %d-byte write", avg_time_us, TEST_DATA_SIZE);
    ESP_LOGI(TAG, "==============================================");
    
    // 將結果寫入SD卡（供後續分析）
    sd_log_printf("\n=== SD Card Performance Test Results ===\n");
    sd_log_printf("Test size: %d bytes, Iterations: %d\n", TEST_DATA_SIZE, TEST_ITERATIONS);
    sd_log_printf("Total data: %.3f MB, Total time: %.3f s\n", total_data_mb, total_time_sec);
    sd_log_printf("Average: %.2f µs, Min: %llu µs, Max: %llu µs\n", 
                  avg_time_us, min_time_us, max_time_us);
    sd_log_printf("StdDev: %.2f µs, Throughput: %.1f bps\n", std_dev_us, avg_throughput);
    
    // 清理資源
    frame_system_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    sd_logger_deinit();
    
    ESP_LOGI(TAG, "Performance test completed");
}

void test_task(void *pvParameters) {
    // 只進行SD卡性能測試
    test_sd_write_performance();
    
    // 測試完成後刪除任務
    vTaskDelete(NULL);
}

extern "C" void app_main(void) {
    // 創建測試任務
    xTaskCreatePinnedToCore(
        test_task,
        "sd_perf_test_task",
        8192,  // 減少堆疊大小，因為我們不需要讀取frame
        NULL,
        5,
        NULL,
        0
    );
}