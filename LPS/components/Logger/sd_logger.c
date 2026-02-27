#include "sd_logger.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define BUFFER_SIZE (64 * 1024)  // 64KB 緩衝區
#define FLUSH_INTERVAL_MS 1000   // 每秒刷一次

typedef struct {
    char data[BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
    SemaphoreHandle_t mutex;
    TaskHandle_t flush_task;
    FILE* file;
    bool running;
} ring_buffer_t;

static ring_buffer_t* g_buf = NULL;
static vprintf_like_t orig_vprintf = NULL;

static int ring_buffer_write(const char* fmt, va_list args) {
    if (!g_buf || !g_buf->running) return 0;
    
    char temp[512];
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    if (len <= 0) return 0;
    
    xSemaphoreTake(g_buf->mutex, portMAX_DELAY);
    
    // 環形緩衝區寫入
    for (int i = 0; i < len; i++) {
        uint32_t next = (g_buf->head + 1) % BUFFER_SIZE;
        if (next != g_buf->tail) { 
            g_buf->data[g_buf->head] = temp[i];
            g_buf->head = next;
        } else {
            g_buf->tail = (g_buf->tail + 1) % BUFFER_SIZE;
            g_buf->data[g_buf->head] = temp[i];
            g_buf->head = next;
        }
    }
    
    xSemaphoreGive(g_buf->mutex);
    return len;
}

// 定期寫入SD卡的任務
static void flush_task(void* arg) {
    ring_buffer_t* buf = (ring_buffer_t*)arg;
    char write_buf[1024];
    
    while (buf->running) {
        vTaskDelay(pdMS_TO_TICKS(FLUSH_INTERVAL_MS));
        
        while (1) {
            xSemaphoreTake(buf->mutex, portMAX_DELAY);
            
            if (buf->head == buf->tail) {
                xSemaphoreGive(buf->mutex);
                break;
            }
            
            // 計算可讀取的連續區塊
            uint32_t available;
            if (buf->head > buf->tail) {
                available = buf->head - buf->tail;
            } else {
                available = BUFFER_SIZE - buf->tail;
            }
            
            uint32_t to_read = (available < sizeof(write_buf)) ? available : sizeof(write_buf);
            memcpy(write_buf, &buf->data[buf->tail], to_read);
            buf->tail = (buf->tail + to_read) % BUFFER_SIZE;
            
            xSemaphoreGive(buf->mutex);
            
            fwrite(write_buf, 1, to_read, buf->file);
        }
        
        fflush(buf->file);
    }
    
    vTaskDelete(NULL);
}

esp_err_t sd_log_init(const char* path) {
    g_buf = heap_caps_malloc(sizeof(ring_buffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!g_buf) return ESP_ERR_NO_MEM;
    
    memset(g_buf, 0, sizeof(ring_buffer_t));
    g_buf->mutex = xSemaphoreCreateMutex();
    if (!g_buf->mutex) {
        free(g_buf);
        return ESP_ERR_NO_MEM;
    }
    
    g_buf->file = fopen(path, "w+");
    if (!g_buf->file) {
        vSemaphoreDelete(g_buf->mutex);
        free(g_buf);
        return ESP_FAIL;
    }
    
    g_buf->running = true;
    
    xTaskCreatePinnedToCore(flush_task, "sd_flush", 4096, g_buf, 1, &g_buf->flush_task, 1);
    
    // 取代 vprintf
    orig_vprintf = esp_log_set_vprintf(ring_buffer_write);
    
    return ESP_OK;
}

esp_err_t sd_log_deinit(void) {
    if (!g_buf) return ESP_ERR_INVALID_STATE;
    
    g_buf->running = false;

    if (g_buf->flush_task) {
        vTaskDelay(pdMS_TO_TICKS(FLUSH_INTERVAL_MS + 100));
    }
    
    // 寫入剩餘數據
    if (g_buf->file) {
        xSemaphoreTake(g_buf->mutex, portMAX_DELAY);
        if (g_buf->head != g_buf->tail) {
            if (g_buf->head > g_buf->tail) {
                fwrite(&g_buf->data[g_buf->tail], 1, g_buf->head - g_buf->tail, g_buf->file);
            } else {
                fwrite(&g_buf->data[g_buf->tail], 1, BUFFER_SIZE - g_buf->tail, g_buf->file);
                fwrite(g_buf->data, 1, g_buf->head, g_buf->file);
            }
        }
        xSemaphoreGive(g_buf->mutex);
        
        fclose(g_buf->file);
    }
    
    esp_log_set_vprintf(orig_vprintf);
    
    if (g_buf->mutex) vSemaphoreDelete(g_buf->mutex);
    free(g_buf);
    g_buf = NULL;
    
    return ESP_OK;
}
/*      ===     test code      ===     */

/*
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sd_logger.h"
#include "readframe.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sd_test";

#define TEST_SIZE 250
#define TEST_COUNT 4000

static void test_task(void *pvParameters) {

    frame_system_init("0:/control.dat", "0:/frame.dat");
    sd_log_init("/sd/logger.log");
    
    char test_data[TEST_SIZE+1];
    memset(test_data, 'Q', TEST_SIZE );
    test_data[TEST_SIZE ] = '\0';
    
    printf("start esp log test\n");
    uint64_t start = esp_timer_get_time();
    
    for(int i = 0; i < TEST_COUNT; i++) {
        ESP_LOGI(TAG, "%s", test_data);
    }
    
    uint64_t end = esp_timer_get_time();
    printf("end esp log test\n");

    sd_log_deinit();
    frame_system_deinit();
    
    // 計算結果
    double total_us = end - start;
    double total_sec = total_us / 1000000.0;
    double total_mb = (double)(TEST_SIZE * TEST_COUNT) / (1024.0 * 1024.0);
    double speed = total_mb / total_sec;
    
    ESP_LOGI(TAG, "總時間: %.2f 秒", total_sec);
    ESP_LOGI(TAG, "總寫入: %.2f MB", total_mb);
    ESP_LOGI(TAG, "平均每次: %.2f µs", total_us / TEST_COUNT);
    ESP_LOGI(TAG, "速度: %.2f MB/s", speed);
    
    vTaskDelete(NULL);
}

extern "C" void app_main(void) {
    xTaskCreatePinnedToCore(
        test_task,
        "log_test",
        8192,
        NULL,
        5,
        NULL,
        1
    );
}


I (6278) sd_test: 總時間: 0.57 秒
I (6279) sd_test: 總寫入: 0.95 MB
I (6279) sd_test: 平均每次: 142.54 µs
I (6283) sd_test: 速度: 1.7 MB/s

*/