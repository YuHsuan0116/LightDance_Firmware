#include <stdio.h>
#include <string.h>
#include <sys/stat.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "readframe.h"
#include "board.h"
#include "table_frame.h"
#include "sd_logger.h"

static const char *TAG = "main_test";

void test_task(void *pvParameters) {
    ESP_LOGI(TAG, "===== Start frame_system init testing =====");

    const char *control_path = "0:/control.dat";
    const char *frame_path = "0:/frame.dat";
    const char *log_path = "0:/LOGGER.log";

    //init system
    esp_err_t ret = frame_system_init(control_path, frame_path);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "system INIT failed: %s", esp_err_to_name(ret));
        sd_logger_deinit();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "system INIT success");

    //init SD Logger
    ret = sd_logger_init(log_path);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "SD Logger INIT fail");
    }
    else{
        ESP_LOGI(TAG, "SD Logger INIT success");
    }

    //channel info
    ch_info_t info = get_channel_info();
    ESP_LOGI(TAG, "------ channel info ------");
    for (int i = 0; i < WS2812B_NUM; i++) {
        if (info.rmt_strips[i] > 0) {
            ESP_LOGI(TAG, "WS2812B Strip[%d]: %d LEDs", i, info.rmt_strips[i]);
        }
    }
    ESP_LOGI(TAG, "-------------------------");

    //read frame to end
    table_frame_t current_frame;
    uint32_t frame_count = 0;

    ESP_LOGI(TAG, "START read_frame()...");

    while (true){
        ret = read_frame(&current_frame);

        if (ret == ESP_ERR_NOT_FOUND){
            ESP_LOGI(TAG, "Read_Frame() done, reach EOF. Total Frames: %u)", frame_count);
            break;
        }
        else if (ret != ESP_OK){
            ESP_LOGE(TAG, "Error as: %s (Frame: %u)", esp_err_to_name(ret), frame_count);
            continue;
        }

        frame_count++;

        //log info & sd
        if (frame_count % 10 == 0 || frame_count == 1) {
            ESP_LOGI(TAG, "Frame #%u | Time: %llu ms | Fade: %s", 
                     frame_count, 
                     current_frame.timestamp, 
                     current_frame.fade ? "ON" : "OFF");
        }
        //sd
        if (frame_count % 50 == 0) {
            //extra example
            sd_log_printf("[DIRECT_LOG] Frame %u at %llu us\n", frame_count, current_frame.timestamp);
        }
    }

    ESP_LOGI(TAG, "read_frame end");
    frame_system_deinit(); 
    
    vTaskDelay(pdMS_TO_TICKS(100));
    sd_logger_deinit();

    ESP_LOGI(TAG, "test end ,system DEINIT");
    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
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