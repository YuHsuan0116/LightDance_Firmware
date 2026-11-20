#include "app.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ws2812b.h"

typedef enum {
    APP_CMD_PLAY,
    APP_CMD_TEST,
    APP_CMD_STOP,
} app_cmd_type_t;

typedef struct {
    app_cmd_type_t type;

    union {
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
        } test;
    } data;
} app_cmd_t;

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task_handle = NULL;

static ws2812b_dev_t strip[8];
static const gpio_num_t strip_gpio[8] = {
    GPIO_NUM_32, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27,
    GPIO_NUM_19, GPIO_NUM_18, GPIO_NUM_5,  GPIO_NUM_17,
};
static grb8_t data[100];

static const char* TAG = "app";

static void app_task(void*) {
    app_cmd_t cmd;
    while (true) {
        xQueueReceive(s_queue, &cmd, portMAX_DELAY);
        switch (cmd.type) {
            case APP_CMD_PLAY:
                ESP_LOGI(TAG, "Received play");
                for (int i = 0; i < 100; i++) {
                    data[i].r = 255;
                    data[i].g = 255;
                    data[i].b = 255;
                }
                for (int i = 0; i < 8; i++) {
                    ws2812b_set_grb_n(&strip[i], data, 100);
                }
                break;
            case APP_CMD_TEST:
                ESP_LOGI(TAG, "Received test");
                for (int i = 0; i < 100; i++) {
                    data[i].r = cmd.data.test.r;
                    data[i].g = cmd.data.test.g;
                    data[i].b = cmd.data.test.b;
                }
                for (int i = 0; i < 8; i++) {
                    ws2812b_set_grb_n(&strip[i], data, 100);
                }
                break;
            case APP_CMD_STOP:
                ESP_LOGI(TAG, "Received stop");
                for (int i = 0; i < 100; i++) {
                    data[i].r = 0;
                    data[i].g = 0;
                    data[i].b = 0;
                }
                for (int i = 0; i < 8; i++) {
                    ws2812b_set_grb_n(&strip[i], data, 100);
                }
                break;
            default:
                break;
        }
    }
}

esp_err_t app_init() {
    s_queue = xQueueCreate(50, sizeof(app_cmd_t));
    xTaskCreatePinnedToCore(app_task, "app_task", 8192, NULL, 5, &s_task_handle, 1);

    for (int i = 0; i < 8; i++) {
        ws2812b_init(&strip[i], strip_gpio[i]);
    }

    return ESP_OK;
}

esp_err_t app_send_play() {
    app_cmd_t _cmd;
    _cmd.type = APP_CMD_PLAY;
    xQueueSend(s_queue, &_cmd, 0);
    return ESP_OK;
}

esp_err_t app_send_test_solid(uint8_t r, uint8_t g, uint8_t b) {
    app_cmd_t _cmd;
    _cmd.type = APP_CMD_TEST;
    _cmd.data.test.r = r;
    _cmd.data.test.g = g;
    _cmd.data.test.b = b;
    xQueueSend(s_queue, &_cmd, 0);
    return ESP_OK;
}

esp_err_t app_send_test_rainbow() {
    app_cmd_t _cmd;
    _cmd.type = APP_CMD_TEST;
    xQueueSend(s_queue, &_cmd, 0);
    return ESP_OK;
}

esp_err_t app_send_stop() {
    app_cmd_t _cmd;
    _cmd.type = APP_CMD_TEST;
    xQueueSend(s_queue, &_cmd, 0);
    return ESP_OK;
}
