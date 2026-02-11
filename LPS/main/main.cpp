#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "readframe.h"
#include "board.h"
#include "frame.h"
#include "sd_logger.h"

static const char *TAG = "main_test";

void test_task(void *pvParameters) {
    ESP_LOGI(TAG, "===== Start frame_system init testing =====");
    
    const char *control_path = "0:control.dat";
    const char *frame_path = "0:frame.dat";
    const char *log_path = "/sd/LOGGER.log";
    
    esp_err_t ret = frame_system_init(control_path, frame_path);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "system INIT failed: %s", esp_err_to_name(ret));
        sd_logger_deinit();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "system INIT success");

    ret = sd_logger_init(log_path);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "SD Logger INIT fail");
    }
    else{
        ESP_LOGI(TAG, "SD Logger INIT success");
    }

    const char* sd_card_id = get_sd_card_id();
    ESP_LOGI(TAG, "SD Card ID: %s", sd_card_id);

    ESP_LOGI(TAG, "====== Control.dat Information ======");
    
    uint32_t of_enabled_count = 0;
    for (int i = 0; i < PCA9955B_CH_NUM; i++) {
        if (ch_info_snapshot.i2c_leds[i]) {
            of_enabled_count++;
        }
    }
    ESP_LOGI(TAG, "Enabled OF channels: %u", of_enabled_count);
    
    uint32_t total_led_count = 0;
    for (int i = 0; i < WS2812B_NUM; i++) {
        if (ch_info_snapshot.rmt_strips[i] > 0) {
            ESP_LOGI(TAG, "Strip[%d]: %d LEDs", i, ch_info_snapshot.rmt_strips[i]);
            total_led_count += ch_info_snapshot.rmt_strips[i];
        }
    }
    ESP_LOGI(TAG, "Total LEDs on all strips: %u", total_led_count);
    
    // 讀取幀直到結束
    table_frame_t current_frame;
    uint32_t frame_count = 0;

    ESP_LOGI(TAG, "====== Start reading frames ======");
    
    while (true) {
        ret = read_frame(&current_frame);

        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "Reached EOF. Total Frames read: %u", frame_count);
            break;
        }
        else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error reading frame: %s (Frame: %u)", esp_err_to_name(ret), frame_count);
            break;
        }

        frame_count++;
        ESP_LOGI(TAG, "Reading frame - frame %d", frame_count );
        ESP_LOGI(TAG, "Timestamp: %llu", current_frame.timestamp);
        ESP_LOGI(TAG, "Fade: %s", current_frame.fade ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    frame_reset();
    ESP_LOGI(TAG, "====== Test Reset ======");
    read_frame(&current_frame);
    read_frame(&current_frame);

    while (true) {
        ret = read_frame(&current_frame);
        
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "Reached EOF after reset");
            break;
        }
        else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error reading frame after reset: %s", esp_err_to_name(ret));
            break;
        }

        frame_count++;
        
        ESP_LOGI(TAG, "Reading after reset - frame %d", frame_count );
        ESP_LOGI(TAG, "Timestamp: %llu", current_frame.timestamp);
        ESP_LOGI(TAG, "Fade: %s", current_frame.fade ? "ON" : "OFF");
            
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(TAG, "====== Reading Complete ======");
    ESP_LOGI(TAG, "Total frames processed: %u", frame_count);
    
    frame_system_deinit(); 
    
    vTaskDelay(pdMS_TO_TICKS(100));
    sd_logger_deinit();

    ESP_LOGI(TAG, "Test completed, system deinitialized");
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "Starting test task...");
    
    xTaskCreatePinnedToCore(
        test_task,
        "test_task",
        16384,
        NULL,
        5,
        NULL,
        0
    );
}