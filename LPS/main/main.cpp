#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "readframe.h"
#include "player.hpp"
#include "console_new.h"

static const char *TAG = "APP";

static void app_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "app_task start, HWM=%u", uxTaskGetStackHighWaterMark(NULL));

    esp_err_t err = frame_system_init("0:/control.dat", "0:/frame.dat");
    ESP_LOGI(TAG, "frame_system_init=%s", esp_err_to_name(err));
    ESP_LOGI(TAG, "HWM after frame_system_init=%u", uxTaskGetStackHighWaterMark(NULL));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed, stop here");
        vTaskDelay(portMAX_DELAY);
    }

    Player::getInstance().init();
    ESP_LOGI(TAG, "HWM after Player::init=%u", uxTaskGetStackHighWaterMark(NULL));

    start_console(); // 進入 REPL（通常不會 return）

    vTaskDelete(NULL);
}

extern "C" void app_main(void)
{
    xTaskCreate(app_task, "app_task", 16384, NULL, 5, NULL);
    // app_main return 讓 main task 結束，不再承擔後續 stack 壓力
}
