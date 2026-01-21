#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "console_new.h"
#include "player.hpp"
#include "readframe.h"

static const char* TAG = "APP";

table_frame_t buffer;

static void app_task(void* arg) {
    (void)arg;

    ESP_LOGI(TAG, "app_task start, HWM=%u", uxTaskGetStackHighWaterMark(NULL));

    esp_err_t err = frame_system_init("0:/control.dat", "0:/frame.dat");
    ESP_LOGI(TAG, "frame_system_init=%s", esp_err_to_name(err));
    ESP_LOGI(TAG, "HWM after frame_system_init=%u", uxTaskGetStackHighWaterMark(NULL));

    if(err != ESP_OK) {
        ESP_LOGE(TAG, "init failed, stop here");
        vTaskDelay(portMAX_DELAY);
    }

    Player::getInstance().init();
    ESP_LOGI(TAG, "HWM after Player::init=%u", uxTaskGetStackHighWaterMark(NULL));

    start_console();

    // for(int i = 0; i < 5; i++) {
    //     read_frame(&buffer);
    //     print_table_frame(buffer);
    // }
    // frame_reset();
    // for(int i = 0; i < 5; i++) {
    //     read_frame(&buffer);
    //     print_table_frame(buffer);
    // }

    vTaskDelete(NULL);
}

extern "C" void app_main(void) {
    xTaskCreate(app_task, "app_task", 16384, NULL, 5, NULL);
}
